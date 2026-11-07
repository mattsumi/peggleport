#include "online/direct.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <cstring>
#include <memory>
#include <vector>
#include <string>

#include "log.h"
#include "online/rendezvous.h"

#include <chrono>
#include <thread>

namespace pp::online {

static constexpr uint8_t kPunch = 0x10;

DirectLink::~DirectLink() { close(); }

const std::string& DirectLink::deadReason() const {
  static const std::string none;
  return rudp_ ? rudp_->deadReason() : none;
}

uint64_t DirectLink::lastRecvAgeMs() const { return rudp_ ? nowMs() - rudp_->lastRecvMs() : 0; }

std::string DirectLink::stateName() const {
  static const char* n[] = {"probing", "registering", "punching", "direct", "relay", "closed"};
  return n[state_];
}

void DirectLink::enter(State s, const std::string& note) {
  state_ = s;
  stateSince_ = nowMs();
  lastSend_ = 0;
  if (!note.empty()) {
    note_ = note;
    pp::log::write(s == Failed ? pp::log::Level::Warn : pp::log::Level::Info, "net", "direct link: " + note);
  }
  if (s == Failed && !rudp_) port_.reset();
}

static bool keyBytes(const std::string& hex, uint8_t out[16]) {
  if (hex.size() != 32) return false;
  for (int i = 0; i < 16; i++) {
    unsigned v = 0;
    for (int j = 0; j < 2; j++) {
      char c = hex[2 * i + j];
      int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (x < 0) return false;
      v = v * 16 + (unsigned)x;
    }
    out[i] = (uint8_t)v;
  }
  return true;
}

bool DirectLink::start(const DirectParams& p, std::string* err) {
  p_ = p;
  if (!parseHexKey(p.pairKeyHex, &key_)) { if (err) *err = "bad pair key"; return false; }
  if (!Endpoint::resolve(p.serverHost, p.serverPort, &server_, err)) return false;
  if (p.auxPort > 0 && !Endpoint::resolve(p.serverHost, p.auxPort, &aux_, nullptr)) p_.auxPort = 0;
  port_ = openUdpPort(0, err);
  if (!port_) return false;
  if (p_.wrap) port_ = p_.wrap(std::move(port_));
  t0_ = stateSince_ = nowMs();
  state_ = Probing;
  note_ = "contacting the rendezvous";
  pp::log::info("net", "direct link: contacting the rendezvous at " + p.serverHost + ":" + std::to_string(p.serverPort));
  return true;
}

void DirectLink::sendRendezvous(uint8_t kind, bool withCookie) {
  std::vector<uint8_t> d(rdv::kMinRequest, 0);
  d[0] = rdv::kMagic0;
  d[1] = rdv::kMagic1;
  d[2] = kind;
  if (kind == rdv::kReg) {
    std::memcpy(&d[3], cookie_, 8);
    keyBytes(p_.pairKeyHex, &d[11]);
    d[27] = p_.isHost ? 0 : 1;
  }
  (void)withCookie;
  port_->sendTo(kind == rdv::kObs ? aux_ : server_, d.data(), d.size());
}

void DirectLink::sendPunch() {
  std::vector<uint8_t> d = {kPunch, uint8_t(heard_ ? 1 : 0)};
  Rudp::tagged(key_, d);
  raw(d.data(), d.size());
}

static bool readEp(const uint8_t* d, Endpoint* e) {
  std::memcpy(e->ip, d, 16);
  e->port = uint16_t(d[16] << 8 | d[17]);
  return e->port != 0;
}

void DirectLink::onServer(const Endpoint& from, const uint8_t* d, size_t n) {
  if (n < 3 || d[1] != rdv::kMagic1 || (from != server_ && from != aux_)) return;
  switch (d[2]) {
    case rdv::kCookie:
      if (n >= 29 && from == server_ && state_ == Probing) {
        std::memcpy(cookie_, d + 3, 8);
        readEp(d + 11, &obsMain_);
        haveCookie_ = true;
      }
      break;
    case rdv::kObsr:
      if (n >= 21 && from == aux_ && readEp(d + 3, &obsAux_)) haveAux_ = true;
      break;
    case rdv::kPeer:
      if (n >= 39 && state_ == Registering && readEp(d + 3, &peer_)) {
        readEp(d + 21, &obsMain_);
        enter(Punching, "punching through to " + peer_.str());
      }
      break;
    default: break;
  }
}

void DirectLink::ensureStream() {
  if (!rudp_) rudp_ = std::make_unique<Rudp>(key_, p_.rudp, nowMs());
}

bool DirectLink::onPeer(const Endpoint& from, const uint8_t* d, size_t n) {
  if (n < 9 || state_ < Registering || state_ > Up) return false;
  uint8_t kind = d[0];
  if (kind == kPunch) {
    if (n != 10 || !Rudp::verify(key_, d, n)) return false;
    heard_ = true;
    peer_ = from;
    if (state_ == Registering) enter(Punching, "punching through to " + peer_.str());
    if (state_ == Punching && (d[1] & 1)) {
      ensureStream();
      enter(Up, "direct UDP link to " + peer_.str());
    }
    uint64_t now = nowMs();
    if (now - lastPunchReply_ >= 30) {
      lastPunchReply_ = now;
      sendPunch();
    }
    return true;
  }
  if (kind == Rudp::kData || kind == Rudp::kAck || kind == Rudp::kClose) {
    if (!Rudp::verify(key_, d, n)) return false;
    peer_ = from;
    heard_ = true;
    ensureStream();
    if (state_ != Up) enter(Up, "direct UDP link to " + peer_.str());
    peerConfirmed_ = true;
    rudp_->onDatagram(d, n, nowMs());
    return true;
  }
  return false;
}

void DirectLink::poll() {
  if (state_ == Failed || state_ == Closed || !port_) return;
  uint64_t now = nowMs();
  uint8_t buf[2048];
  Endpoint from;
  for (int i = 0; i < 128; i++) {
    int n = port_->recvFrom(&from, buf, sizeof buf);
    if (n <= 0) break;
    if (n >= 3 && buf[0] == rdv::kMagic0 && buf[1] == rdv::kMagic1) onServer(from, buf, (size_t)n);
    else onPeer(from, buf, (size_t)n);
  }
  if (!port_) return;
  now = nowMs();
  auto since = [&](uint64_t t) { return (int)(now - t); };
  switch (state_) {
    case Probing: {
      if (haveCookie_ && (haveAux_ || p_.auxPort == 0 || since(t0_) >= p_.auxWaitMs)) {
        if (haveAux_ && obsAux_.port != obsMain_.port) {
          symmetric_ = true;
          enter(Failed, "symmetric NAT detected (public ports " + std::to_string(obsMain_.port) + " and " + std::to_string(obsAux_.port) +
                            " differ): using the relay");
        } else {
          enter(Registering, "registering with the rendezvous (public address " + obsMain_.str() + ")");
        }
        break;
      }
      if (!haveCookie_ && since(t0_) >= p_.probeTimeoutMs) {
        enter(Failed, "the rendezvous did not answer over UDP (is the lobby's UDP port open?): using the relay");
        break;
      }
      if (lastSend_ == 0 || since(lastSend_) >= p_.retryMs) {
        lastSend_ = now;
        if (!haveCookie_) sendRendezvous(rdv::kHello, false);
        if (p_.auxPort > 0 && !haveAux_) sendRendezvous(rdv::kObs, false);
      }
      break;
    }
    case Registering:
      if (since(stateSince_) >= p_.registerTimeoutMs) {
        enter(Failed, "the other player never registered for a direct link: using the relay");
        break;
      }
      if (lastSend_ == 0 || since(lastSend_) >= p_.retryMs) {
        lastSend_ = now;
        sendRendezvous(rdv::kReg, true);
      }
      break;
    case Punching:
      if (since(stateSince_) >= p_.punchTimeoutMs) {
        enter(Failed, "the NAT/firewall blocked the direct connection: using the relay");
        break;
      }
      if (lastSend_ == 0 || since(lastSend_) >= p_.punchEveryMs) {
        lastSend_ = now;
        sendPunch();
      }
      break;
    case Up:
      if (rudp_) rudp_->update(now, [this](const uint8_t* d, size_t n) { raw(d, n); });
      if (!peerConfirmed_ && since(stateSince_) < p_.punchTimeoutMs && (lastSend_ == 0 || since(lastSend_) >= p_.punchEveryMs)) {
        lastSend_ = now;
        sendPunch();
      }
      break;
    default: break;
  }
}

void DirectLink::close() {
  if (state_ == Closed) return;
  if (rudp_ && port_ && peer_.valid()) {
    rudp_->sendClose([this](const uint8_t* d, size_t n) { raw(d, n); });
    uint8_t b[64];
    Endpoint f;
    for (int i = 0; i < 3; i++) port_->recvFrom(&f, b, sizeof b);
  }
  state_ = Closed;
  note_ = "closed";
}

std::string probeLobbyUdp(const std::string& host, int port, bool* reachable) {
  std::string err, out;
  Endpoint main, aux;
  if (reachable) *reachable = false;
  if (!Endpoint::resolve(host, port, &main, &err) || !Endpoint::resolve(host, port + 1, &aux, &err)) {
    pp::log::warn("net", "UDP probe of " + host + ":" + std::to_string(port) + " failed: " + err);
    return err;
  }
  auto sock = openUdpPort(0, &err);
  if (!sock) {
    pp::log::warn("net", "UDP probe of " + host + ":" + std::to_string(port) + " failed: " + err);
    return err;
  }
  std::vector<uint8_t> hello(rdv::kMinRequest, 0), obs(rdv::kMinRequest, 0);
  hello[0] = obs[0] = rdv::kMagic0;
  hello[1] = obs[1] = rdv::kMagic1;
  hello[2] = rdv::kHello;
  obs[2] = rdv::kObs;
  Endpoint seenMain, seenAux;
  bool gotMain = false, gotAux = false;
  uint64_t t0 = nowMs();
  uint8_t buf[256];
  Endpoint from;
  for (uint64_t last = 0; nowMs() - t0 < 3000 && !(gotMain && gotAux);) {
    if (nowMs() - last > 300) {
      last = nowMs();
      sock->sendTo(main, hello.data(), hello.size());
      sock->sendTo(aux, obs.data(), obs.size());
    }
    int n = sock->recvFrom(&from, buf, sizeof buf);
    if (n >= 21 && buf[0] == rdv::kMagic0 && buf[1] == rdv::kMagic1) {
      if (buf[2] == rdv::kCookie && n >= 29 && from == main) { readEp(buf + 11, &seenMain); gotMain = true; }
      if (buf[2] == rdv::kObsr && from == aux) { readEp(buf + 3, &seenAux); gotAux = true; }
    } else if (n <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  out += "UDP " + std::to_string(port) + ": " + (gotMain ? "answered, the lobby sees you as " + seenMain.str() : std::string("no answer (firewall / router not open for UDP, or an old pp_lobbyd)")) + "\n";
  out += "UDP " + std::to_string(port + 1) + " (NAT probe): " + (gotAux ? "answered, public port " + std::to_string(seenAux.port) : std::string("no answer (optional: only needed to detect symmetric NAT early)")) + "\n";
  if (gotMain && gotAux)
    out += seenMain.port == seenAux.port ? "NAT: one public port for both destinations (cone NAT): direct connections should work.\n"
                                         : "NAT: a different public port per destination (symmetric NAT): direct connections will not work, the TCP relay is used.\n";
  else if (gotMain) out += "NAT type unknown (probe port not reachable); a direct connection is still attempted and falls back to the relay.\n";
  if (reachable) *reachable = gotMain;
  pp::log::write(gotMain ? pp::log::Level::Info : pp::log::Level::Warn, "net", "UDP probe of " + host + ":" + std::to_string(port) + "\n" + out);
  return out;
}

}
