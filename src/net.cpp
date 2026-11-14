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
#include <thread>
#include <system_error>

#include <cstring>

#include "log.h"
#include "masters.h"
#include "online/endpoint.h"
#include "online/websocket.h"
#include "sockcompat.h"

namespace pp {

namespace {
enum : uint8_t { HELLO = 1, WELCOME, READY, START, SHOT, HASH, DESYNC, ERR, SWITCH, BYE, REMATCH_REQ, REMATCH, ROSTER, LOBBY_LOST };
constexpr uint32_t PROTOCOL = 0x50500002;

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
  std::unique_ptr<online::WebSocket> ws;
  int player = -1;
  bool ready = false, dead = false, retiring = false, awaitOk = false;
  std::string name;
  std::unique_ptr<online::DirectLink> link;
  std::vector<uint8_t> udpOut, uin;
  bool sentSwitch = false, gotSwitch = false, tcpClosed = false, bye = false, writeClosed = false;
  uint64_t tcpLostAt = 0;
  ~Peer() { ws.reset(); if (s != BAD_SOCK) closeSock(s); }
  int read(char* data, size_t cap) {
    if (ws) return ws->read(data, cap);
    int n = (int)recv(s, data, (socklen_arg)cap, 0);
    return n >= 0 ? n : wouldBlock() ? -1 : -2;
  }
  int write(const char* data, size_t len) {
    if (ws) return ws->write(data, len);
    int n = (int)::send(s, data, (socklen_arg)len, MSG_NOSIGNAL);
    return n >= 0 ? n : wouldBlock() ? -1 : -2;
  }
  void loseTcp() {
    if (tcpClosed) return;
    tcpClosed = true;
    tcpLostAt = online::nowMs();
  }
};

struct Session::Attach {
  std::thread thread;
  std::atomic<bool> stop{false}, done{false};
  std::unique_ptr<Peer> peer;
  std::string error;
  ~Attach() { stop = true; if (thread.joinable()) thread.join(); }
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
    delete lobbyCtl_;
  }
  for (auto p : peers_) {
    delete p;
  }
  if (listen_ != -1) closeSock((sock_t)listen_);
}

static void tune(sock_t s) { tuneSock(s); }

static sock_t connectTo(const std::string& h, int port, std::string* err, const std::atomic<bool>* stop = nullptr) {
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
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  for (addrinfo* ai = res; ai; ai = ai->ai_next) {
    if ((stop && stop->load()) || std::chrono::steady_clock::now() >= deadline) break;
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == BAD_SOCK) continue;
    nonBlock(s);
    bool ok = connect(s, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0;
    if (!ok && wouldBlock()) {
      while (!(stop && stop->load()) && std::chrono::steady_clock::now() < deadline) {
        fd_set w, x;
        FD_ZERO(&w); FD_ZERO(&x);
        FD_SET(s, &w); FD_SET(s, &x);
        timeval tv{0, 50000};
        int result = select((int)s + 1, nullptr, &w, &x, &tv);
        if (result == 0) continue;
        if (result > 0 && FD_ISSET(s, &w)) {
          int soerr = 0;
          socklen_t sl = sizeof soerr;
          ok = getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl) == 0 && soerr == 0;
        }
        break;
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

std::unique_ptr<Session::Peer> Session::connectLobby(const std::string& host, int port, const std::atomic<bool>* stop, std::string* err) {
  std::string ignored;
  if (!err) err = &ignored;
  online::LobbyEndpoint ep;
  const std::string address = online::endpointAddress(host, port);
  if (!online::parseEndpoint(address, &ep)) { *err = "invalid lobby address"; return nullptr; }
  auto peer = std::make_unique<Peer>();
  peer->s = connectTo(ep.host, ep.port, err, stop);
  if (peer->s == BAD_SOCK) return nullptr;
  if (ep.secure) {
    peer->ws = online::WebSocket::connect(peer->s, ep, stop, err);
    if (!peer->ws) return nullptr;
  }
  return peer;
}

template<class P>
static bool sendAllBlocking(P& peer, const std::string& data, const std::atomic<bool>* stop = nullptr) {
  size_t off = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (off < data.size() || (peer.ws && !peer.ws->drained())) {
    if ((stop && stop->load()) || std::chrono::steady_clock::now() >= deadline) return false;
    int n = off < data.size() ? peer.write(data.data() + off, data.size() - off) : -1;
    if (peer.ws && !peer.ws->poll()) return false;
    if (n > 0) off += (size_t)n;
    else if (n == 0 || n == -2) return false;
    else std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
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

template<class P>
static bool readLineBlocking(P& peer, std::string& buf, std::string& line, int timeoutMs, const std::atomic<bool>* stop = nullptr) {
  const auto giveUp = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    if (buf.size() > 4096 || (stop && stop->load()) || std::chrono::steady_clock::now() >= giveUp) return false;
    size_t nl = buf.find('\n');
    if (nl != std::string::npos) {
      line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return true;
    }
    char tmp[512];
    auto n = peer.read(tmp, sizeof tmp);
    if (n > 0) buf.append(tmp, (size_t)n);
    else if (n == 0 || n == -2) return false;
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

bool Session::hostViaLobby(const std::string& lh, int lp, const std::string& roomName, const MatchInfo& mi, std::string* err, const std::atomic<bool>* stop) {
  host_ = true;
  mi_ = mi;
  local_ = 0;
  welcomed_ = true;
  lobbyHost_ = lh;
  lobbyPort_ = lp;
  pp::log::info("net", "opening a room on the lobby " + online::endpointAddress(lh, lp));
  auto connection = connectLobby(lh, lp, stop, err);
  if (!connection) return false;
  char hash[32];
  std::snprintf(hash, sizeof hash, "%016llx", (unsigned long long)mi.contentHash);
  std::string name = roomName;
  for (auto& c : name) if (c == ' ') c = '_';
  std::string req = "HOST " + (name.empty() ? std::string("room") : name) + " " + std::to_string(mi.players) + " " + (mi.party ? "1" : "0") + " " + (mi.level.empty() ? "?" : mi.level) + " " + hash + " H\n";
  std::string buf, ln;
  if (!sendAllBlocking(*connection, req, stop) || !readLineBlocking(*connection, buf, ln, 5000, stop) || ln.rfind("OK ", 0) != 0) {
    if (err) *err = "lobby refused the room: " + ln;
    pp::log::error("net", "lobby refused the room: " + (ln.empty() ? std::string("no answer") : ln));
    return false;
  }
  {
    std::istringstream is(ln.substr(3));
    std::string capability;
    is >> roomCode_ >> lobbyToken_ >> capability;
    if (capability != "H") {
      if (err) *err = "lobby heartbeat unavailable";
      return false;
    }
  }
  lobbyCtl_ = connection.release();
  lobbyCtl_->in.assign(buf.begin(), buf.end());
  lobbyReplyAt_ = online::nowMs();
  if (mi_.players <= 1) started_ = true;
  pp::log::info("net", "room " + roomCode_ + " is open on the lobby, waiting for " + std::to_string(std::max(0, mi_.players - 1)) + " player(s)");
  return true;
}

bool Session::joinViaLobby(const std::string& lh, int lp, const std::string& code, std::string* err, const std::atomic<bool>* stop) {
  if (code.size() != 5 || code.find_first_not_of("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") != std::string::npos) {
    if (err) *err = "invalid room code";
    return false;
  }
  host_ = false;
  lobbyHost_ = lh;
  lobbyPort_ = lp;
  pp::log::info("net", "joining room " + code + " on the lobby " + lh);
  auto control = connectLobby(lh, lp, stop, err);
  if (!control) return false;
  std::string buf, line;
  if (!sendAllBlocking(*control, "WATCH H\n", stop) || !readLineBlocking(*control, buf, line, 3000, stop) || line != "OK H") {
    if (err) *err = "lobby heartbeat unavailable";
    return false;
  }
  auto connection = connectLobby(lh, lp, stop, err);
  if (!connection) return false;
  if (!sendAllBlocking(*connection, "JOIN " + code + (udpEnabled && !connection->ws ? " U" : "") + "\n", stop)) {
    if (err) *err = "lobby connection failed";
    return false;
  }
  lobbyCtl_ = control.release();
  lobbyCtl_->in.assign(buf.begin(), buf.end());
  lobbyReplyAt_ = online::nowMs();
  connection->player = 0;
  connection->awaitOk = true;
  peers_.push_back(connection.release());
  return true;
}

bool Session::listRooms(const std::string& lh, int lp, std::vector<std::string>* lines, std::string* err, const std::atomic<bool>* stop) {
  auto connection = connectLobby(lh, lp, stop, err);
  if (!connection) return false;
  std::string buf, ln;
  bool ok = sendAllBlocking(*connection, "LIST\n", stop), ended = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (ok && !ended && lines->size() < 256) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0 || !readLineBlocking(*connection, buf, ln, (int)left, stop)) break;
    if (ln == "END") ended = true;
    else lines->push_back(ln);
  }
  if (connection->ws) connection->ws->close();
  if (!ok && err) *err = "lobby connection failed";
  if (ok && !ended) {
    if (err) *err = "no room list";
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
  if (n > (1u << 20) || dst.size() + n + 4 > (1u << 20) + 4) { p.loseTcp(); return; }
  for (int i = 0; i < 4; i++) dst.push_back(uint8_t(n >> (8 * i)));
  dst.push_back(type);
  dst.insert(dst.end(), body.begin(), body.end());
}

void Session::broadcast(uint8_t type, const std::vector<uint8_t>& body, const Peer* except) {
  for (auto p : peers_)
    if (p != except && !p->dead) send(*p, type, body);
}

void Session::poll() {
  if (left_) return;
  for (auto it = attaches_.begin(); it != attaches_.end();) {
    Attach& attach = **it;
    if (!attach.done) { ++it; continue; }
    if (attach.thread.joinable()) attach.thread.join();
    if (attach.peer && !failed()) {
      attach.peer->awaitOk = true;
      peers_.push_back(attach.peer.release());
    } else pp::log::warn("net", "could not attach an incoming player through the lobby");
    it = attaches_.erase(it);
  }
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
  if (lobbyCtl_ && !lobbyCtl_->dead && !left_) {
    char tmp[512];
    for (;;) {
      if (lobbyCtl_->in.size() >= 4096) { lobbyCtl_->loseTcp(); break; }
      auto n = lobbyCtl_->read(tmp, std::min(sizeof tmp, 4096 - lobbyCtl_->in.size()));
      if (n > 0) lobbyCtl_->in.insert(lobbyCtl_->in.end(), tmp, tmp + n);
      else {
        if (n == 0 || n == -2) {
          lobbyCtl_->loseTcp();
        }
        break;
      }
    }
    for (;;) {
      auto nl = std::find(lobbyCtl_->in.begin(), lobbyCtl_->in.end(), (uint8_t)'\n');
      if (nl == lobbyCtl_->in.end()) break;
      std::string l(lobbyCtl_->in.begin(), nl);
      lobbyCtl_->in.erase(lobbyCtl_->in.begin(), nl + 1);
      if (lobbyPing_ && l == "PONG " + std::to_string(lobbyPing_)) {
        lobbyReplyAt_ = online::nowMs();
        lobbyPing_ = 0;
        continue;
      }
      if (lobbyCtl_->tcpClosed || failed() || !host_) continue;
      if (l.rfind("INCOMING ", 0) != 0) continue;
      if (attaches_.size() >= 3 || l.size() > 32 || l.substr(9).find_first_not_of("0123456789") != std::string::npos) continue;
      auto attach = std::make_unique<Attach>();
      Attach* job = attach.get();
      const std::string endpoint = lobbyHost_;
      const int port = lobbyPort_;
      const std::string request = "ATTACH " + roomCode_ + " " + l.substr(9) + " " + lobbyToken_;
      const bool udp = udpEnabled;
      job->thread = std::thread([job, endpoint, port, request, udp] {
        job->peer = connectLobby(endpoint, port, &job->stop, &job->error);
        if (job->peer && !sendAllBlocking(*job->peer, request + (udp && !job->peer->ws ? " U" : "") + "\n", &job->stop)) job->peer.reset();
        job->done = true;
      });
      attaches_.push_back(std::move(attach));
    }
    flush(*lobbyCtl_);
    const uint64_t now = online::nowMs();
    if (!failed() && !lobbyCtl_->tcpClosed && !lobbyPing_ && now - lobbyReplyAt_ >= 1000) {
      lobbyPing_ = now;
      const std::string ping = "PING " + std::to_string(lobbyPing_) + "\n";
      lobbyCtl_->out.insert(lobbyCtl_->out.end(), ping.begin(), ping.end());
      flush(*lobbyCtl_);
    }
    if (lobbyCtl_->tcpClosed || (lobbyPing_ && now - lobbyPing_ >= 5000)) {
      lobbyCtl_->dead = true;
      failMatch(started_ ? host_ ? "lobby connection lost: " + (cleanName(name_).empty() ? std::string("Host") : cleanName(name_)) : "lobby relay connection lost" : "lobby connection lost");
      pp::log::warn("net", "lobby control connection lost or timed out");
    }
  }
  for (auto p : peers_) {
    if (p->dead) {
      if (p->link) p->link->poll();
      continue;
    }
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
        if (p->in.size() >= (1u << 20) + 4) break;
        auto n = p->read(buf, std::min(sizeof buf, (1u << 20) + 4 - p->in.size()));
        if (n > 0) p->in.insert(p->in.end(), buf, buf + n);
        else {
          if (n == 0 || n == -2) {
            p->loseTcp();
          }
          break;
        }
      }
    }
    if (p->awaitOk) {
      auto nl = std::find(p->in.begin(), p->in.end(), (uint8_t)'\n');
      if (nl == p->in.end()) {
        if (p->in.size() >= 4096) p->loseTcp();
        if (!p->tcpClosed) continue;
        p->dead = true;
        p->bye = true;
        if (host_) pp::log::info("net", "the lobby closed the connection for an incoming player without an answer");
        else if (error.empty()) {
          error = "lobby: connection closed";
          pp::log::error("net", "the lobby closed the connection without an answer");
        }
        continue;
      }
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
    if (p->tcpClosed && (!p->link || !(p->sentSwitch || p->gotSwitch)) && (!started_ || lobbyHost_.empty() || online::nowMs() - p->tcpLostAt >= 300)) p->dead = true;
    if (started_ && !lobbyHost_.empty() && p->tcpClosed && p->tcpLostAt && online::nowMs() - p->tcpLostAt >= 300 && !p->bye) {
      failMatch(host_ ? "lobby player connection lost: " + who(*p) : "lobby relay connection lost");
    }
    if (!linkLost.empty() && !p->dead) {
      p->dead = true;
      failMatch(linkLost);
      pp::log::warn("net", linkLost);
    }
    if (p->dead && !p->bye) {
      if (host_ && !started_) {
        if (notice.empty()) notice = who(*p) + " left the room";
        pp::log::info("net", who(*p) + " left the room");
      } else {
        std::string lost = !host_ && !lobbyHost_.empty() && p->tcpClosed ? "lobby relay connection lost" : who(*p) + " lost connection";
        failMatch(lost);
        pp::log::warn("net", lost);
      }
    }
  }
  if (host_) refreshRoster();
  std::vector<std::unique_ptr<Peer>> ending;
  for (auto it = peers_.begin(); it != peers_.end();) {
    if ((*it)->retiring) {
      ending.emplace_back(*it);
      it = peers_.erase(it);
    } else ++it;
  }
  if (!ending.empty()) drain(std::move(ending), {});
}

void Session::failMatch(const std::string& reason) {
  if (!error.empty()) return;
  error = reason;
  if (host_ && started_) {
    broadcast(ERR, W().str(reason).b);
    for (auto p : peers_) if (!p->dead) flush(*p);
  } else if (started_ && !lobbyHost_.empty() && (reason == "lobby relay connection lost" || reason == "lobby connection lost")) {
    broadcast(LOBBY_LOST, {});
    for (auto p : peers_) if (!p->dead) flush(*p);
  }
}

void Session::flush(Peer& p) {
  if (p.link && !p.udpOut.empty()) {
    p.link->write(p.udpOut);
    p.udpOut.clear();
  }
  if (p.link) p.link->poll();
  if (p.ws && !p.ws->poll()) p.loseTcp();
  while (!p.tcpClosed && !p.out.empty()) {
    auto n = p.write((const char*)p.out.data(), p.out.size());
    if (n > 0) p.out.erase(p.out.begin(), p.out.begin() + n);
    else {
      if (n == 0 || n == -2) p.loseTcp();
      break;
    }
  }
}

std::string Session::who(const Peer& p) const {
  if (!host_) return !roster_.empty() && !cleanName(roster_[0].name).empty() ? "the host " + cleanName(roster_[0].name) : "the host";
  if (!p.name.empty()) return p.name;
  return p.player >= 0 ? "player " + std::to_string(p.player + 1) : std::string("a player");
}

void Session::startDirect(Peer& p, const std::string& pairKey, int auxPort) {
  if (lobbyHost_.empty() || p.link || p.ws) return;
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
  for (auto& attach : attaches_) attach->stop = true;
  pp::log::info("net", "left the session");
  if (lobbyCtl_ && lobbyCtl_->s != BAD_SOCK) {
    if (lobbyCtl_->ws) { lobbyCtl_->ws->close(); lobbyCtl_->ws.reset(); }
    closeSock(lobbyCtl_->s);
    lobbyCtl_->s = BAD_SOCK;
    lobbyCtl_->dead = true;
  }
  for (auto p : peers_) {
    if (p->dead || failed()) continue;
    send(*p, BYE, {});
    flush(*p);
  }
  std::vector<std::unique_ptr<Peer>> ending;
  for (auto p : peers_) ending.emplace_back(p);
  peers_.clear();
  drain(std::move(ending), std::move(attaches_));
}

void Session::drain(std::vector<std::unique_ptr<Peer>> peers, std::vector<std::unique_ptr<Attach>> attaches) {
  if (peers.empty() && attaches.empty()) return;
  for (auto& p : peers) if (p->link) p->link->releaseObservers();
  auto ending = std::make_shared<std::vector<std::unique_ptr<Peer>>>(std::move(peers));
  auto attaching = std::make_shared<std::vector<std::unique_ptr<Attach>>>(std::move(attaches));
  const uint64_t deadline = online::nowMs() + 2500;
  try {
    std::thread([ending, attaching, deadline] {
      do {
        for (auto it = ending->begin(); it != ending->end();) {
          Peer& p = **it;
          flush(p);
          bool done = p.tcpClosed && (!p.link || !(p.sentSwitch || p.gotSwitch));
          if (p.link && (p.sentSwitch || p.gotSwitch)) {
            std::vector<uint8_t> received;
            p.link->read(&received);
            const auto* stream = p.link->stream();
            done = p.link->streamDead() || (p.out.empty() && p.udpOut.empty() && stream && stream->queued() == 0 && stream->unacked() == 0);
          } else if (!p.tcpClosed) {
            if (p.out.empty() && (!p.ws || p.ws->drained()) && !p.writeClosed) {
              if (p.ws) p.ws->close();
              else {
#ifdef _WIN32
              shutdown(p.s, SD_SEND);
#else
              shutdown(p.s, SHUT_WR);
#endif
              }
              p.writeClosed = true;
            }
            char received[4096];
            for (int i = 0; i < 32; i++) {
              auto n = p.read(received, sizeof received);
              if (n > 0) continue;
              if (n == 0 || n == -2) p.loseTcp();
              break;
            }
            done = p.tcpClosed;
          }
          if (done) it = ending->erase(it);
          else ++it;
        }
        if (ending->empty() || online::nowMs() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      } while (true);
      attaching->clear();
    }).detach();
  } catch (const std::system_error&) {
    pp::log::warn("net", "could not finish sending the final match message");
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
  r[0] = {cleanName(name_).empty() ? "Host" : cleanName(name_), true, true};
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
    else if (host_) failMatch(m);
    else if (!terminalReceived_) { error = m; terminalReceived_ = true; }
    return;
  }
  if (host_) {
    switch (type) {
      case LOBBY_LOST:
        if (started_ && p.player > 0 && body.empty()) failMatch("lobby player connection lost: " + who(p));
        return;
      case HELLO: {
        if (r.u32() != PROTOCOL) {
          pp::log::warn("net", "refused a player: protocol version mismatch");
          send(p, ERR, W().str("protocol version mismatch").b);
          flush(p);
          p.dead = p.retiring = true;
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
          p.dead = p.retiring = true;
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
          p.dead = p.retiring = true;
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
      mi_.party = r.u8() != 0;
      if (!mi_.party) mi_.players = 2;
      local_ = std::min(local_, mi_.players - 1);
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
    case ERR: {
      std::string reason = r.str();
      if (r.ok && !terminalReceived_) {
        error = reason;
        terminalReceived_ = true;
        pp::log::error("net", "the host refused or ended the connection: " + error);
      }
      return;
    }
  }
}

void Session::sendReady(uint64_t h) {
  if (!host_ && !peers_.empty()) send(*peers_[0], READY, W().u64(h).b);
}

void Session::sendShot(const ShotCmd& c0) {
  if (failed() || left_) return;
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
