#include "net.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstdio>
#include <cstdint>
#include <iterator>
#include <utility>
#include <memory>
#include <vector>
#include <string>

#include <cstring>

#include "log.h"
#include "masters.h"
#include "sockcompat.h"

namespace pp {

namespace {
enum : uint8_t { HELLO = 1, WELCOME, READY, START, SHOT, HASH, DESYNC, ERR, SWITCH, BYE, REMATCH_REQ, REMATCH, ROSTER };
constexpr uint32_t PROTOCOL = 0x50500001;

struct W {
  std::vector<uint8_t> b;
  W& u8(uint8_t v) { b.push_back(v); return *this; }
  W& u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); return *this; }
  W& u64(uint64_t v) { for (int i = 0; i < 8; i++) b.push_back(uint8_t(v >> (8 * i))); return *this; }
  W& str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); return *this; }
};
struct R {
  const std::vector<uint8_t>& b;
  size_t p = 0;
  bool ok = true;
  uint64_t get(int n) {
    if (p + n > b.size()) { ok = false; return 0; }
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= uint64_t(b[p + i]) << (8 * i);
    p += n;
    return v;
  }
  uint8_t u8() { return (uint8_t)get(1); }
  uint32_t u32() { return (uint32_t)get(4); }
  uint64_t u64() { return get(8); }
  std::string str() {
    uint32_t n = u32();
    if (!ok || p + n > b.size() || n > 4096) { ok = false; return {}; }
    std::string s((const char*)b.data() + p, n);
    p += n;
    return s;
  }
};
std::vector<uint8_t> shotBody(const ShotCmd& c) {
  uint32_t bits;
  std::memcpy(&bits, &c.angle, 4);
  return W().u32(c.seq).u8((uint8_t)c.player).u8((uint8_t)c.board).u32(bits).u32((uint32_t)c.tick).b;
}
ShotCmd readShot(R& r) {
  ShotCmd c;
  c.seq = r.u32();
  c.player = r.u8();
  c.board = r.u8();
  uint32_t bits = r.u32();
  std::memcpy(&c.angle, &bits, 4);
  c.tick = (int32_t)r.u32();
  return c;
}
}

struct Session::Peer {
  sock_t s = BAD_SOCK;
  std::vector<uint8_t> in, out;
  int player = -1;
  bool ready = false, dead = false, awaitOk = false;
  std::string name;
  std::unique_ptr<online::DirectLink> link;
  std::vector<uint8_t> udpOut, uin;
  bool sentSwitch = false, gotSwitch = false, tcpClosed = false, bye = false;
};

static std::string cleanName(const std::string& s) {
  std::string o;
  for (unsigned char c : s)
    if (c >= 32 && c != 127 && o.size() < 24) o.push_back((char)c);
  return o;
}

Session::Session() { netStartup(); }

Session::~Session() {
  leave();
  if (lobbyCtl_) {
    if (lobbyCtl_->s != BAD_SOCK) closeSock(lobbyCtl_->s);
    delete lobbyCtl_;
  }
  for (auto p : peers_) {
    if (p->s != BAD_SOCK) closeSock(p->s);
    delete p;
  }
  if (listen_ != -1) closeSock((sock_t)listen_);
}

static void tune(sock_t s) { tuneSock(s); }

static sock_t connectTo(const std::string& h, int port, std::string* err) {
  netStartup();
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(h.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
    if (err) *err = "cannot resolve " + h;
    pp::log::warn("net", "cannot resolve " + h);
    return BAD_SOCK;
  }
  sock_t s = BAD_SOCK;
  for (addrinfo* ai = res; ai; ai = ai->ai_next) {
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == BAD_SOCK) continue;
    nonBlock(s);
    bool ok = connect(s, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0;
    if (!ok && wouldBlock()) {
      fd_set w, x;
      FD_ZERO(&w);
      FD_ZERO(&x);
      FD_SET(s, &w);
      FD_SET(s, &x);
      timeval tv{4, 0};
      if (select((int)s + 1, nullptr, &w, &x, &tv) > 0 && FD_ISSET(s, &w)) {
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl);
        ok = soerr == 0;
      }
    }
    if (ok) break;
    closeSock(s);
    s = BAD_SOCK;
  }
  freeaddrinfo(res);
  if (s == BAD_SOCK) {
    if (err) *err = "cannot connect to " + h + ":" + std::to_string(port);
    pp::log::warn("net", "cannot connect to " + h + ":" + std::to_string(port));
    return BAD_SOCK;
  }
  tuneSock(s);
  return s;
}

static bool sendAllBlocking(sock_t s, const std::string& d) {
  size_t off = 0;
  for (int spins = 0; off < d.size() && spins < 100000; spins++) {
    auto n = ::send(s, d.data() + off, (socklen_arg)(d.size() - off), MSG_NOSIGNAL);
    if (n > 0) off += (size_t)n;
    else if (!(n < 0 && wouldBlock())) return false;
  }
  return off == d.size();
}

bool Session::host(int port, const MatchInfo& mi, std::string* err) {
  host_ = true;
  mi_ = mi;
  local_ = 0;
  welcomed_ = true;
  sock_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == BAD_SOCK) {
    if (err) *err = "socket() failed";
    pp::log::error("net", "cannot create a socket for hosting");
    return false;
  }
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 8) != 0) {
    closeSock(s);
    if (err) *err = "cannot listen on port " + std::to_string(port);
    pp::log::error("net", "cannot listen on port " + std::to_string(port) + " (is it already in use?)");
    return false;
  }
  socklen_t al = sizeof a;
  getsockname(s, (sockaddr*)&a, &al);
  port_ = ntohs(a.sin_port);
  nonBlock(s);
  listen_ = (intptr_t)s;
  if (mi_.players <= 1) started_ = true;
  pp::log::info("net", "hosting on port " + std::to_string(port_) + " for " + std::to_string(mi_.players) + " player(s)");
  return true;
}

bool Session::join(const std::string& h, int port, std::string* err) {
  host_ = false;
  pp::log::info("net", "connecting to " + h + ":" + std::to_string(port));
  sock_t s = connectTo(h, port, err);
  if (s == BAD_SOCK) return false;
  auto p = new Peer;
  p->s = s;
  p->player = 0;
  peers_.push_back(p);
  send(*p, HELLO, helloBody());
  pp::log::info("net", "connected to " + h + ":" + std::to_string(port));
  return true;
}

std::vector<uint8_t> Session::helloBody() const { return W().u32(PROTOCOL).str(name_).b; }

static bool readLineBlocking(sock_t s, std::string& buf, std::string& line, int timeoutMs) {
  const auto giveUp = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    size_t nl = buf.find('\n');
    if (nl != std::string::npos) {
      line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return true;
    }
    char tmp[512];
    auto n = recv(s, tmp, sizeof tmp, 0);
    if (n > 0) buf.append(tmp, (size_t)n);
    else if (n == 0 || !wouldBlock()) return false;
    else {
      if (std::chrono::steady_clock::now() >= giveUp) return false;
#ifdef _WIN32
      Sleep(2);
#else
      usleep(2000);
#endif
    }
  }
}

bool Session::hostViaLobby(const std::string& lh, int lp, const std::string& roomName, const MatchInfo& mi, std::string* err) {
  host_ = true;
  mi_ = mi;
  local_ = 0;
  welcomed_ = true;
  lobbyHost_ = lh;
  lobbyPort_ = lp;
  pp::log::info("net", "opening a room on the lobby " + lh + ":" + std::to_string(lp));
  sock_t s = connectTo(lh, lp, err);
  if (s == BAD_SOCK) return false;
  char hash[32];
  std::snprintf(hash, sizeof hash, "%016llx", (unsigned long long)mi.contentHash);
  std::string name = roomName;
  for (auto& c : name) if (c == ' ') c = '_';
  std::string req = "HOST " + (name.empty() ? std::string("room") : name) + " " + std::to_string(mi.players) + " " + (mi.party ? "1" : "0") + " " + (mi.level.empty() ? "?" : mi.level) + " " + hash + "\n";
  std::string buf, ln;
  if (!sendAllBlocking(s, req) || !readLineBlocking(s, buf, ln, 5000) || ln.rfind("OK ", 0) != 0) {
    if (err) *err = "lobby refused the room: " + ln;
    pp::log::error("net", "lobby refused the room: " + (ln.empty() ? std::string("no answer") : ln));
    closeSock(s);
    return false;
  }
  {
    std::istringstream is(ln.substr(3));
    is >> roomCode_ >> lobbyToken_;
  }
  lobbyCtl_ = new Peer;
  lobbyCtl_->s = s;
  lobbyCtl_->in.assign(buf.begin(), buf.end());
  if (mi_.players <= 1) started_ = true;
  pp::log::info("net", "room " + roomCode_ + " is open on the lobby, waiting for " + std::to_string(std::max(0, mi_.players - 1)) + " player(s)");
  return true;
}

bool Session::joinViaLobby(const std::string& lh, int lp, const std::string& code, std::string* err) {
  host_ = false;
  lobbyHost_ = lh;
  lobbyPort_ = lp;
  pp::log::info("net", "joining room " + code + " on the lobby " + lh + ":" + std::to_string(lp));
  sock_t s = connectTo(lh, lp, err);
  if (s == BAD_SOCK) return false;
  if (!sendAllBlocking(s, "JOIN " + code + (udpEnabled ? " U" : "") + "\n")) {
    closeSock(s);
    if (err) *err = "lobby connection failed";
    pp::log::error("net", "lobby connection failed while joining room " + code);
    return false;
  }
  auto p = new Peer;
  p->s = s;
  p->player = 0;
  p->awaitOk = true;
  peers_.push_back(p);
  return true;
}

bool Session::listRooms(const std::string& lh, int lp, std::vector<std::string>* lines, std::string* err) {
  sock_t s = connectTo(lh, lp, err);
  if (s == BAD_SOCK) return false;
  std::string buf, ln;
  bool ok = sendAllBlocking(s, "LIST\n"), ended = false;
  while (ok && !ended && readLineBlocking(s, buf, ln, 3000)) {
    if (ln == "END") ended = true;
    else lines->push_back(ln);
  }
  closeSock(s);
  if (!ok && err) *err = "lobby connection failed";
  if (!ok) pp::log::warn("net", "lobby connection failed while listing rooms");
  if (ok && !ended) {
    if (err) *err = "no room list";
    std::string head;
    if (!lines->empty())
      for (unsigned char c : lines->front())
        if (c >= 32 && c < 127 && head.size() < 80) head.push_back((char)c);
    pp::log::warn("net", "no complete room list came from " + lh + ":" + std::to_string(lp) + (head.empty() ? std::string() : ", the answer began with: " + head));
    lines->clear();
    return false;
  }
  return ok;
}

int Session::connectedPeers() const {
  int n = 0;
  for (auto p : peers_) n += !p->dead;
  return n;
}

void Session::send(Peer& p, uint8_t type, const std::vector<uint8_t>& body) {
  uint32_t n = (uint32_t)body.size() + 1;
  std::vector<uint8_t>& dst = p.sentSwitch ? p.udpOut : p.out;
  for (int i = 0; i < 4; i++) dst.push_back(uint8_t(n >> (8 * i)));
  dst.push_back(type);
  dst.insert(dst.end(), body.begin(), body.end());
}

void Session::broadcast(uint8_t type, const std::vector<uint8_t>& body, const Peer* except) {
  for (auto p : peers_)
    if (p != except && !p->dead) send(*p, type, body);
}

void Session::poll() {
  if (host_ && listen_ != -1) {
    for (;;) {
      sock_t c = accept((sock_t)listen_, nullptr, nullptr);
      if (c == BAD_SOCK) break;
      int seated = 0;
      for (auto p : peers_) seated += !p->dead;
      if (started_ || seated >= mi_.players - 1) {
        pp::log::info("net", "refused an incoming connection (the room is full or the match has started)");
        closeSock(c);
        continue;
      }
      tune(c);
      auto p = new Peer;
      p->s = c;
      peers_.push_back(p);
      pp::log::info("net", "a player connected");
    }
  }
  if (lobbyCtl_ && !lobbyCtl_->dead) {
    char tmp[512];
    for (;;) {
      auto n = recv(lobbyCtl_->s, tmp, sizeof tmp, 0);
      if (n > 0) lobbyCtl_->in.insert(lobbyCtl_->in.end(), tmp, tmp + n);
      else {
        if (n == 0 || !wouldBlock()) {
          lobbyCtl_->dead = true;
          error = "lobby connection lost";
          pp::log::warn("net", error);
        }
        break;
      }
    }
    for (;;) {
      auto nl = std::find(lobbyCtl_->in.begin(), lobbyCtl_->in.end(), (uint8_t)'\n');
      if (nl == lobbyCtl_->in.end()) break;
      std::string l(lobbyCtl_->in.begin(), nl);
      lobbyCtl_->in.erase(lobbyCtl_->in.begin(), nl + 1);
      if (l.rfind("INCOMING ", 0) != 0) continue;
      std::string e2;
      sock_t as = connectTo(lobbyHost_, lobbyPort_, &e2);
      if (as == BAD_SOCK || !sendAllBlocking(as, "ATTACH " + roomCode_ + " " + l.substr(9) + " " + lobbyToken_ + (udpEnabled ? " U" : "") + "\n")) {
        if (as != BAD_SOCK) closeSock(as);
        pp::log::warn("net", "could not attach an incoming player through the lobby");
        continue;
      }
      pp::log::info("net", "an incoming player is attaching through the lobby");
      auto p = new Peer;
      p->s = as;
      p->awaitOk = true;
      peers_.push_back(p);
    }
  }
  for (auto p : peers_) {
    if (p->dead) continue;
    std::string linkLost;
    if (p->link) {
      p->link->poll();
      if (p->link->up() && !p->sentSwitch && !p->awaitOk) {
        send(*p, SWITCH, {});
        p->sentSwitch = true;
      }
      if ((p->sentSwitch || p->gotSwitch) && p->link->streamDead()) linkLost = who(*p) + " lost the direct link (" + p->link->deadReason() + ")";
      else if (p->gotSwitch && (p->link->failed() || p->link->state() == online::DirectLink::Closed)) linkLost = "the direct link to " + who(*p) + " could not be completed";
      p->link->read(&p->uin);
    }
    flush(*p);
    if (!p->tcpClosed) {
      char buf[4096];
      for (;;) {
        auto n = recv(p->s, buf, sizeof buf, 0);
        if (n > 0) p->in.insert(p->in.end(), buf, buf + n);
        else {
          if (n == 0 || !wouldBlock()) {
            p->tcpClosed = true;
            if (!p->link || !(p->sentSwitch || p->gotSwitch)) p->dead = true;
          }
          break;
        }
      }
    }
    if (p->tcpClosed && p->link && !p->dead && p->link->lastRecvAgeMs() > 3000) p->dead = true;
    if (p->awaitOk && !p->in.empty()) {
      auto nl = std::find(p->in.begin(), p->in.end(), (uint8_t)'\n');
      if (nl == p->in.end()) { if (!p->dead) continue; }
      else {
        std::string l(p->in.begin(), nl);
        p->in.erase(p->in.begin(), nl + 1);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        p->awaitOk = false;
        if (l != "OK" && l.rfind("OK U ", 0) != 0) {
          p->dead = true;
          if (host_) {
            p->bye = true;
            pp::log::info("net", "a player gave up before the lobby connected them (the lobby said: " + l + ")");
            continue;
          }
          error = "lobby: " + l;
          pp::log::error("net", error);
          continue;
        }
        if (l.size() > 5 && udpEnabled) {
          std::istringstream is(l.substr(5));
          std::string key;
          int aux = 0;
          is >> key >> aux;
          startDirect(*p, key, aux);
        }
        if (!host_) send(*p, HELLO, helloBody());
      }
    } else if (p->awaitOk) {
      if (p->dead && host_ && !p->bye) {
        p->bye = true;
        pp::log::info("net", "the lobby closed the connection for an incoming player without an answer");
      } else if (p->dead && !host_ && error.empty()) {
        error = "lobby: connection closed";
        pp::log::error("net", "the lobby closed the connection without an answer");
      }
      continue;
    }
    for (;;) {
      std::vector<uint8_t>& buf = p->gotSwitch ? p->uin : p->in;
      if (buf.size() < 5) break;
      uint32_t len = buf[0] | buf[1] << 8 | buf[2] << 16 | (uint32_t)buf[3] << 24;
      if (len == 0 || len > (1u << 20)) {
        pp::log::warn("net", "dropped " + who(*p) + ": invalid message size");
        p->dead = true;
        break;
      }
      if (buf.size() < 4 + len) break;
      uint8_t type = buf[4];
      std::vector<uint8_t> body(buf.begin() + 5, buf.begin() + 4 + len);
      buf.erase(buf.begin(), buf.begin() + 4 + len);
      handle(*p, type, body);
      if (p->dead) break;
    }
    if (!linkLost.empty() && !p->dead) {
      p->dead = true;
      if (error.empty()) error = linkLost;
      pp::log::warn("net", linkLost);
    }
    if (p->dead && !p->bye) {
      if (host_ && !started_) {
        if (notice.empty()) notice = who(*p) + " left the room";
        pp::log::info("net", who(*p) + " left the room");
      } else {
        std::string lost = host_ ? who(*p) + " lost connection" : "lost connection to the host";
        if (error.empty()) error = lost;
        pp::log::warn("net", lost);
      }
    }
  }
  if (host_) refreshRoster();
}

void Session::flush(Peer& p) {
  if (p.link && !p.udpOut.empty()) {
    p.link->write(p.udpOut);
    p.udpOut.clear();
  }
  if (p.link) p.link->poll();
  while (!p.tcpClosed && !p.out.empty()) {
    auto n = ::send(p.s, (const char*)p.out.data(), (socklen_arg)p.out.size(), MSG_NOSIGNAL);
    if (n > 0) p.out.erase(p.out.begin(), p.out.begin() + n);
    else {
      if (n < 0 && !wouldBlock()) { p.tcpClosed = true; if (!p.link || !(p.sentSwitch || p.gotSwitch)) p.dead = true; }
      break;
    }
  }
}

std::string Session::who(const Peer& p) const {
  if (!host_) return "the host";
  if (!p.name.empty()) return p.name;
  return p.player >= 0 ? "player " + std::to_string(p.player + 1) : std::string("a player");
}

void Session::startDirect(Peer& p, const std::string& pairKey, int auxPort) {
  if (lobbyHost_.empty() || p.link) return;
  online::DirectParams dp = directTemplate;
  dp.serverHost = lobbyHost_;
  dp.serverPort = lobbyPort_;
  dp.auxPort = auxPort;
  dp.pairKeyHex = pairKey;
  dp.isHost = host_;
  auto l = std::make_unique<online::DirectLink>();
  std::string e;
  if (l->start(dp, &e)) p.link = std::move(l);
  else pp::log::warn("net", "direct link not started: " + e + " (the lobby relay is used)");
}

bool Session::direct() const {
  int n = 0;
  for (auto p : peers_)
    if (!p->dead) {
      if (!(p->sentSwitch && p->gotSwitch)) return false;
      n++;
    }
  return n > 0;
}

std::string Session::linkNote() const {
  for (auto p : peers_) {
    if (p->dead) continue;
    if (p->sentSwitch && p->gotSwitch) {
      char b[96];
      std::snprintf(b, sizeof b, "Direct UDP link (%.0f ms)", p->link ? p->link->rttMs() : 0.0);
      return b;
    }
    if (p->link) return "Relayed through the lobby - " + p->link->note();
    return lobbyHost_.empty() ? "Direct TCP connection" : "Relayed through the lobby";
  }
  return "";
}

uint64_t Session::streamRetransmits() const {
  uint64_t n = 0;
  for (auto p : peers_)
    if (p->link && p->link->stream()) n += p->link->stream()->stats.retransmits;
  return n;
}

void Session::leave() {
  if (left_) return;
  left_ = true;
  pp::log::info("net", "left the session");
  for (auto p : peers_) {
    if (p->dead) continue;
    send(*p, BYE, {});
    flush(*p);
  }
  for (auto p : peers_) {
    if (p->link) p->link->close();
    if (p->s != BAD_SOCK && !p->tcpClosed) { closeSock(p->s); p->s = BAD_SOCK; p->tcpClosed = true; }
  }
}

bool Session::allReady() const {
  if (!host_) return false;
  int ready = 0;
  for (auto q : peers_) ready += q->ready && !q->dead;
  return ready >= mi_.players - 1;
}

void Session::hostStart() {
  if (host_ && !started_ && allReady()) {
    started_ = true;
    pp::log::info("net", "all players are ready: the match starts");
    broadcast(START, {});
  }
}

void Session::refreshRoster() {
  std::vector<Seat> r(mi_.players);
  r[0] = {name_.empty() ? "Host" : name_, true, true};
  for (auto q : peers_)
    if (!q->dead && q->player > 0 && q->player < (int)r.size()) r[q->player] = {q->name.empty() ? "Player " + std::to_string(q->player + 1) : q->name, true, q->ready};
  std::string sig;
  for (auto& st : r) sig += st.name + (st.connected ? "+" : "-") + (st.ready ? "r" : "w") + "|";
  if (sig == rosterSig_) return;
  rosterSig_ = sig;
  roster_ = r;
  W w;
  w.u8((uint8_t)r.size());
  for (auto& st : r) w.u8((uint8_t)((st.connected ? 1 : 0) | (st.ready ? 2 : 0))).str(st.name);
  broadcast(ROSTER, w.b);
}

void Session::resetForRematch(uint32_t seed) {
  rematchVotes_.clear();
  mine_.clear();
  theirs_.clear();
  desync_ = false;
  desyncMsg.clear();
  hashesCompared = 0;
  in_.clear();
  mi_.seed = seed;
  rematchSeeds_.push_back(seed);
}

void Session::tryRematch() {
  if ((int)rematchVotes_.size() < mi_.players) return;
  uint32_t seed = mi_.seed * 1664525u + 1013904223u + (uint32_t)hashesCompared;
  resetForRematch(seed);
  pp::log::info("net", "rematch agreed");
  broadcast(REMATCH, W().u32(seed).b);
}

void Session::requestRematch() {
  if (host_) {
    rematchVotes_.insert(0);
    tryRematch();
  } else if (!peers_.empty()) {
    send(*peers_[0], REMATCH_REQ, {});
  }
}

bool Session::takeRematch(uint32_t* seed) {
  if (rematchSeeds_.empty()) return false;
  *seed = rematchSeeds_.front();
  rematchSeeds_.pop_front();
  return true;
}

void Session::handle(Peer& p, uint8_t type, const std::vector<uint8_t>& body) {
  R r{body};
  if (type == SWITCH) { p.gotSwitch = true; return; }
  if (type == BYE) {
    p.bye = true;
    p.dead = true;
    std::string m = who(p) + (started_ ? " left the match" : host_ ? " left the room" : " closed the room");
    pp::log::info("net", m);
    if (host_ && !started_) notice = m;
    else if (error.empty()) error = m;
    return;
  }
  if (host_) {
    switch (type) {
      case HELLO: {
        if (r.u32() != PROTOCOL) {
          pp::log::warn("net", "refused a player: protocol version mismatch");
          send(p, ERR, W().str("protocol version mismatch").b);
          flush(p);
          p.dead = true;
          return;
        }
        if (r.p < body.size()) p.name = cleanName(r.str());
        bool taken[4] = {true, false, false, false};
        for (auto q : peers_)
          if (q->player > 0 && !q->dead) taken[q->player] = true;
        for (int i = 1; i < mi_.players; i++)
          if (!taken[i]) { p.player = i; break; }
        if (p.player < 0) {
          pp::log::info("net", "refused a player: the match is full");
          send(p, ERR, W().str("match is full").b);
          flush(p);
          p.dead = true;
          return;
        }
        W welcome;
        welcome.u8((uint8_t)p.player).u8((uint8_t)mi_.players).u8(mi_.party).u32(mi_.seed)
            .u32((uint32_t)mi_.boardWidth).u64(mi_.contentHash).str(mi_.level)
            .u8((uint8_t)mi_.master[0]).u8((uint8_t)mi_.master[1]).u8((uint8_t)mi_.master[2]).u8((uint8_t)mi_.master[3]);
        if (std::any_of(std::begin(mi_.master), std::end(mi_.master), [](int m) { return m >= masters::kFirstPower; })) {
          welcome.u8(4);
          for (int i = 0; i < 4; i++) {
            const ModMaster* mm = mi_.master[i] >= masters::kFirstPower ? masters::byPower(mi_.master[i]) : nullptr;
            welcome.str(mm ? mm->key() : std::string());
          }
        }
        send(p, WELCOME, welcome.b);
        pp::log::info("net", (p.name.empty() ? std::string("a player") : p.name) + " joined as player " + std::to_string(p.player + 1));
        return;
      }
      case READY: {
        uint64_t h = r.u64();
        if (h != mi_.contentHash) {
          pp::log::warn("net", "refused " + who(p) + ": the level or mod files differ from yours");
          send(p, ERR, W().str("content mismatch: your level/mod files differ from the host's").b);
          flush(p);
          p.dead = true;
          return;
        }
        p.ready = true;
        int ready = 0;
        for (auto q : peers_) ready += q->ready && !q->dead;
        if (ready == mi_.players - 1 && !started_ && autoStart) {
          started_ = true;
          pp::log::info("net", "all players are ready: the match starts");
          broadcast(START, {});
        }
        return;
      }
      case REMATCH_REQ:
        rematchVotes_.insert(p.player);
        tryRematch();
        return;
      case SHOT: {
        ShotCmd c = readShot(r);
        if (!r.ok || c.player != p.player) return;
        c.seq = seq_++;
        in_.push_back(c);
        broadcast(SHOT, shotBody(c), &p);
        return;
      }
      case HASH: {
        int b = r.u8();
        int idx = (int)r.u32();
        uint64_t h = r.u64();
        if (!r.ok) return;
        theirs_[{b, idx}].push_back(h);
        compare({b, idx});
        return;
      }
    }
    return;
  }
  switch (type) {
    case WELCOME:
      local_ = r.u8();
      mi_.players = std::clamp((int)r.u8(), 2, 4);
      local_ = std::min(local_, mi_.players - 1);
      mi_.party = r.u8() != 0;
      mi_.seed = r.u32();
      mi_.boardWidth = (int)std::clamp<uint32_t>(r.u32(), 650u, 1400u);
      mi_.contentHash = r.u64();
      mi_.level = r.str();
      for (int i = 0; i < 4; i++) mi_.master[i] = r.u8();
      welcomed_ = r.ok;
      mi_.missingMaster.clear();
      if (welcomed_ && r.p < body.size()) {
        const int nk = r.u8();
        for (int i = 0; i < nk && r.ok; i++) {
          const std::string key = r.str();
          if (!r.ok || i >= 4 || key.empty()) continue;
          if (const ModMaster* mm = masters::byKey(key)) mi_.master[i] = mm->power;
          else if (mi_.missingMaster.empty()) mi_.missingMaster = key;
        }
      }
      for (int i = 0; i < 4; i++)
        if (mi_.master[i] >= masters::kFirstPower && !masters::byPower(mi_.master[i]) && mi_.missingMaster.empty()) mi_.missingMaster = "power " + std::to_string(mi_.master[i]);
      if (welcomed_) pp::log::info("net", "joined as player " + std::to_string(local_ + 1) + " of " + std::to_string(mi_.players));
      return;
    case START:
      started_ = true;
      pp::log::info("net", "the match starts");
      return;
    case ROSTER: {
      int n = r.u8();
      std::vector<Seat> v;
      for (int i = 0; i < n && r.ok && i < 4; i++) {
        int f = r.u8();
        std::string nm = r.str();
        v.push_back({cleanName(nm), (f & 1) != 0, (f & 2) != 0});
      }
      if (r.ok) roster_ = v;
      return;
    }
    case REMATCH: {
      uint32_t seed = r.u32();
      if (r.ok) resetForRematch(seed);
      return;
    }
    case SHOT: {
      ShotCmd c = readShot(r);
      if (r.ok) in_.push_back(c);
      return;
    }
    case DESYNC:
      desync_ = true;
      desyncMsg = r.str();
      pp::log::error("net", "the host reports: " + desyncMsg);
      return;
    case ERR:
      error = r.str();
      pp::log::error("net", "the host refused or ended the connection: " + error);
      return;
  }
}

void Session::sendReady(uint64_t h) {
  if (!host_ && !peers_.empty()) send(*peers_[0], READY, W().u64(h).b);
}

void Session::sendShot(const ShotCmd& c0) {
  ShotCmd c = c0;
  if (host_) {
    c.seq = seq_++;
    broadcast(SHOT, shotBody(c));
  } else if (!peers_.empty()) {
    send(*peers_[0], SHOT, shotBody(c));
  }
}

std::vector<ShotCmd> Session::takeShots() {
  std::vector<ShotCmd> v(in_.begin(), in_.end());
  in_.clear();
  return v;
}

void Session::reportHash(int board, int idx, uint64_t h) {
  if (host_) {
    mine_[{board, idx}] = h;
    compare({board, idx});
  } else if (!peers_.empty()) {
    send(*peers_[0], HASH, W().u8((uint8_t)board).u32((uint32_t)idx).u64(h).b);
  }
}

void Session::compare(std::pair<int, int> key) {
  auto m = mine_.find(key);
  auto t = theirs_.find(key);
  if (m == mine_.end() || t == theirs_.end()) return;
  for (uint64_t h : t->second) {
    hashesCompared++;
    if (h != m->second && !desync_) {
      desync_ = true;
      desyncMsg = "desync on board " + std::to_string(key.first) + " after shot " + std::to_string(key.second);
      pp::log::error("net", desyncMsg + ": the players' games no longer match");
      broadcast(DESYNC, W().str(desyncMsg).b);
    }
  }
  t->second.clear();
}

}
