#include "lobby.h"

#include <algorithm>
#include <iterator>
#include <random>
#include <sstream>
#include <cstdint>
#include <utility>
#include <cstring>
#include <memory>
#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "online/rendezvous.h"
#include "sockcompat.h"

namespace pp {

using Clock = std::chrono::steady_clock;

struct LobbyServer::Conn {
  sock_t s = BAD_SOCK;
  std::string in;
  std::string out;
  enum Role { Unknown, HostCtl, WatchCtl, Waiting, Pipe } role = Unknown;
  Conn* peer = nullptr;
  std::string code, ip;
  bool dead = false, closeAfter = false, countedInRoom = false, wantUdp = false;
  bool loopback = false, initial = true, proxied = false;
  std::string pairKey;
  Clock::time_point created = Clock::now(), lastActivity = Clock::now();
};

static uint32_t entropy() {
  static std::random_device rd;
  return rd();
}
static std::string hex64() {
  char b[20];
  std::snprintf(b, sizeof b, "%08x%08x", entropy(), entropy());
  return b;
}
static bool tokenEq(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned d = 0;
  for (size_t i = 0; i < a.size(); i++) d |= (unsigned char)(a[i] ^ b[i]);
  return d == 0;
}
static std::string clean(const std::string& s, size_t max) {
  std::string o;
  for (char c : s)
    if (c > 32 && c < 127 && o.size() < max) o.push_back(c);
  return o;
}

LobbyServer::LobbyServer() { netStartup(); }

LobbyServer::~LobbyServer() {
  for (auto& c : conns_)
    if (c->s != BAD_SOCK) closeSock(c->s);
  if (listen_ != -1) closeSock((sock_t)listen_);
}

bool LobbyServer::listen(int port, std::string* err) {
  for (int attempt = 0; attempt < 40; attempt++) {
    if (!listenTcp(port, err)) return false;
    if (!enableUdp) return true;
    auto r = std::make_unique<online::Rendezvous>();
    std::string e;
    int aux = udpAuxOffset < 0 ? -1 : port == 0 ? 0 : port + udpAuxOffset;
    if (r->listen(port_, aux, &e)) {
      rdv_ = std::move(r);
      return true;
    }
    if (port != 0) {
      udpError = e;
      return true;
    }
    closeSock((sock_t)listen_);
    listen_ = -1;
  }
  if (err) *err = "no free tcp+udp port pair";
  return false;
}

bool LobbyServer::listenTcp(int port, std::string* err) {
  sock_t s = socket(AF_INET6, SOCK_STREAM, 0);
  bool v6 = s != BAD_SOCK;
  if (v6) {
    int one = 1, zero = 0;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&zero, sizeof zero);
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_any;
    a.sin6_port = htons((uint16_t)port);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || ::listen(s, 64) != 0) {
      closeSock(s);
      v6 = false;
    }
  }
  if (!v6) {
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) { if (err) *err = "socket() failed"; return false; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || ::listen(s, 64) != 0) {
      closeSock(s);
      if (err) *err = "cannot listen on port " + std::to_string(port);
      return false;
    }
  }
  sockaddr_storage ss{};
  socklen_t sl = sizeof ss;
  getsockname(s, (sockaddr*)&ss, &sl);
  port_ = ntohs(ss.ss_family == AF_INET6 ? ((sockaddr_in6*)&ss)->sin6_port : ((sockaddr_in*)&ss)->sin_port);
  nonBlock(s);
  listen_ = (intptr_t)s;
  return true;
}

size_t LobbyServer::pipes() const {
  size_t n = 0;
  for (auto& c : conns_) n += c->role == Conn::Pipe && !c->dead;
  return n / 2;
}

std::string LobbyServer::newCode() {
  static const char* alpha = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (;;) {
    std::string c;
    for (int i = 0; i < 5; i++) c.push_back(alpha[entropy() % 32]);
    if (!rooms_.count(c)) return c;
  }
}

void LobbyServer::reply(Conn& c, const std::string& s, bool closeAfter) {
  c.out += s;
  if (closeAfter) c.closeAfter = true;
}

static std::string peerKey(const sockaddr_storage& ss);

bool LobbyServer::resolveSource(Conn& c, const std::string& header) {
  c.initial = false;
  if (!header.empty()) {
    if (!proxyLocal || !c.loopback || c.proxied || c.role != Conn::Unknown || header.size() > 107 || header.back() != '\r') return false;
    for (size_t i = 0; i + 1 < header.size(); i++) if ((unsigned char)header[i] < 32 || (unsigned char)header[i] >= 127) return false;
    std::istringstream in(header.substr(0, header.size() - 1));
    std::string command, family, source, destination, sourcePort, destinationPort, extra;
    in >> command >> family >> source >> destination >> sourcePort >> destinationPort;
    if (command != "PROXY" || (family != "TCP4" && family != "TCP6") || (in >> extra)) return false;
    auto validPort = [](const std::string& text) {
      if (text.empty() || text.size() > 5) return false;
      unsigned port = 0;
      for (unsigned char ch : text) {
        if (ch < '0' || ch > '9') return false;
        port = port * 10 + ch - '0';
      }
      return port > 0 && port <= 65535;
    };
    if (!validPort(sourcePort) || !validPort(destinationPort)) return false;
    sockaddr_storage ss{}, ds{};
    ss.ss_family = ds.ss_family = family == "TCP4" ? AF_INET : AF_INET6;
    void* sa = family == "TCP4" ? (void*)&((sockaddr_in*)&ss)->sin_addr : (void*)&((sockaddr_in6*)&ss)->sin6_addr;
    void* da = family == "TCP4" ? (void*)&((sockaddr_in*)&ds)->sin_addr : (void*)&((sockaddr_in6*)&ds)->sin6_addr;
    if (inet_pton(ss.ss_family, source.c_str(), sa) != 1 || inet_pton(ds.ss_family, destination.c_str(), da) != 1) return false;
    c.ip = peerKey(ss);
    c.proxied = true;
  }
  size_t same = 0;
  for (auto& other : conns_)
    same += other.get() != &c && !other->dead && other->s != BAD_SOCK && !other->initial && other->ip == c.ip;
  return same < limits.maxPerIp;
}

void LobbyServer::line(Conn& c, const std::string& l) {
  if (c.closeAfter || c.dead) return;
  if (l.rfind("PROXY", 0) == 0) {
    if (!c.initial || !resolveSource(c, l)) reply(c, "ERR not allowed\n", true);
    return;
  }
  if (c.initial && !resolveSource(c, {})) { reply(c, "ERR too many connections from your address\n", true); return; }
  std::istringstream is(l);
  std::string cmd;
  is >> cmd;
  if (cmd == "HOST" && c.role == Conn::Unknown) {
    Room r;
    std::string name, party, players, level, hash, capability;
    is >> name >> players >> party >> level >> hash >> capability;
    r.name = clean(name, 32);
    r.level = clean(level, 32);
    r.hash = clean(hash, 16);
    r.players = std::clamp(std::atoi(players.c_str()), 2, 4);
    r.party = party == "1";
    if (r.name.empty() || r.level.empty()) { reply(c, "ERR bad HOST\n", true); return; }
    if (rooms_.size() >= limits.maxRooms) { reply(c, "ERR lobby is full\n", true); return; }
    size_t mine = 0;
    for (auto& [k, rm] : rooms_) mine += rm.hostIp == c.ip;
    if (mine >= limits.maxRoomsPerIp) { reply(c, "ERR too many rooms from your address\n", true); return; }
    r.code = newCode();
    r.token = hex64();
    r.host = &c;
    r.hostIp = c.ip;
    c.role = Conn::HostCtl;
    c.code = r.code;
    reply(c, "OK " + r.code + " " + r.token + (capability == "H" ? " H\n" : "\n"));
    rooms_[r.code] = r;
  } else if (cmd == "WATCH" && c.role == Conn::Unknown) {
    std::string capability;
    is >> capability;
    if (capability != "H") { reply(c, "ERR unknown command\n", true); return; }
    c.role = Conn::WatchCtl;
    reply(c, "OK H\n");
  } else if (cmd == "PING" && (c.role == Conn::HostCtl || c.role == Conn::WatchCtl)) {
    std::string nonce;
    is >> nonce;
    if (nonce.empty() || nonce.size() > 20 || nonce.find_first_not_of("0123456789") != std::string::npos) reply(c, "ERR bad PING\n", true);
    else {
      reply(c, "PONG " + nonce + "\n");
      auto room = rooms_.find(c.code);
      if (c.role == Conn::HostCtl && room != rooms_.end() && room->second.host == &c)
        for (auto& pipe : conns_)
          if (!pipe->dead && pipe->role == Conn::Pipe && pipe->code == c.code) pipe->lastActivity = Clock::now();
    }
  } else if (cmd == "LIST" && c.role == Conn::Unknown) {
    std::string o;
    for (auto& [code, r] : rooms_)
      o += "ROOM " + code + " " + std::to_string(r.players) + " " + std::to_string(r.joined + 1) + " " + (r.party ? "1" : "0") + " " + r.level + " " + r.hash + " " + r.name + "\n";
    reply(c, o + "END\n", true);
  } else if (cmd == "JOIN" && c.role == Conn::Unknown) {
    std::string code, flag;
    is >> code >> flag;
    c.wantUdp = flag == "U" && !c.proxied;
    auto it = rooms_.find(code);
    if (it == rooms_.end()) { reply(c, "ERR no such room\n", true); return; }
    Room& r = it->second;
    int pending = 0;
    for (auto& [id, pc] : pending_) pending += pc->code == code;
    if (r.joined + pending >= r.players - 1) { reply(c, "ERR room is full\n", true); return; }
    uint32_t id;
    do { id = entropy(); } while (id == 0 || pending_.count(id));
    c.role = Conn::Waiting;
    c.code = code;
    pending_[id] = &c;
    reply(*r.host, "INCOMING " + std::to_string(id) + "\n");
  } else if (cmd == "ATTACH" && c.role == Conn::Unknown) {
    std::string code, token, flag;
    uint32_t id = 0;
    is >> code >> id >> token >> flag;
    auto pi = pending_.find(id);
    auto ri = rooms_.find(code);
    if (pi == pending_.end() || ri == rooms_.end() || pi->second->code != code || !tokenEq(ri->second.token, token)) {
      reply(c, "ERR not allowed\n", true);
      return;
    }
    Conn* g = pi->second;
    pending_.erase(pi);
    c.role = g->role = Conn::Pipe;
    c.peer = g;
    g->peer = &c;
    c.code = g->code = code;
    c.countedInRoom = true;
    ri->second.joined++;
    std::string pk;
    if (rdv_ && !c.proxied && !g->proxied && (flag == "U" || g->wantUdp)) {
      pk = online::randomHex(16);
      if (!rdv_->addPair(pk, online::nowMs())) pk.clear();
    }
    c.pairKey = g->pairKey = pk;
    auto okLine = [&](bool wants) { return wants && !pk.empty() ? "OK U " + pk + " " + std::to_string(rdv_->auxPort()) + "\n" : std::string("OK\n"); };
    reply(c, okLine(flag == "U"));
    reply(*g, okLine(g->wantUdp));
  } else {
    reply(c, "ERR unknown command\n", true);
  }
}

void LobbyServer::drop(Conn& c) {
  if (c.dead) return;
  c.dead = true;
  if (c.role == Conn::HostCtl) {
    auto it = rooms_.find(c.code);
    if (it != rooms_.end()) rooms_.erase(it);
    for (auto pi = pending_.begin(); pi != pending_.end();) {
      if (pi->second->code == c.code) {
        reply(*pi->second, "ERR host left\n", true);
        pi = pending_.erase(pi);
      } else ++pi;
    }
  } else if (c.role == Conn::Waiting) {
    for (auto pi = pending_.begin(); pi != pending_.end();)
      pi = pi->second == &c ? pending_.erase(pi) : std::next(pi);
  } else if (c.role == Conn::Pipe && c.peer) {
    if (!c.pairKey.empty() && rdv_) rdv_->removePair(c.pairKey);
    if (c.countedInRoom || c.peer->countedInRoom) {
      auto it = rooms_.find(c.code);
      if (it != rooms_.end() && it->second.joined > 0) it->second.joined--;
    }
    c.peer->closeAfter = true;
    c.peer->peer = nullptr;
    c.peer = nullptr;
  }
}

static std::string peerKey(const sockaddr_storage& ss) {
  if (ss.ss_family == AF_INET6) {
    unsigned char b[16];
    std::memcpy(b, &((const sockaddr_in6*)&ss)->sin6_addr, 16);
    static const unsigned char mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    bool v4 = std::memcmp(b, mapped, 12) == 0;
    if (!v4) std::memset(b + 8, 0, 8);
    char o[48];
    if (v4) std::snprintf(o, sizeof o, "%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
    else std::snprintf(o, sizeof o, "v6:%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    return o;
  }
  const unsigned char* b = (const unsigned char*)&((const sockaddr_in*)&ss)->sin_addr;
  char o[24];
  std::snprintf(o, sizeof o, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return o;
}

static bool loopbackPeer(const sockaddr_storage& ss) {
  if (ss.ss_family == AF_INET) return ((const unsigned char*)&((const sockaddr_in*)&ss)->sin_addr)[0] == 127;
  if (ss.ss_family != AF_INET6) return false;
  const auto* b = (const unsigned char*)&((const sockaddr_in6*)&ss)->sin6_addr;
  static const unsigned char local[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  static const unsigned char mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
  return std::memcmp(b, local, 16) == 0 || (std::memcmp(b, mapped, 12) == 0 && b[12] == 127);
}

void LobbyServer::poll() {
  if (rdv_) rdv_->poll(online::nowMs());
  auto now = Clock::now();
  auto since = [&](const Clock::time_point& t) { return (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - t).count(); };
  for (;;) {
    sockaddr_storage ss{};
    socklen_t sl = sizeof ss;
    sock_t s = accept((sock_t)listen_, (sockaddr*)&ss, &sl);
    if (s == BAD_SOCK) break;
    std::string ip = peerKey(ss);
    size_t same = 0;
    for (auto& c : conns_) same += c->ip == ip && c->s != BAD_SOCK;
    const bool localProxy = proxyLocal && loopbackPeer(ss);
    if (conns_.size() >= limits.maxConns || (!localProxy && same >= limits.maxPerIp)) {
      closeSock(s);
      refused_++;
      continue;
    }
    tuneSock(s);
    auto c = std::make_unique<Conn>();
    c->s = s;
    c->ip = ip;
    c->loopback = loopbackPeer(ss);
    conns_.push_back(std::move(c));
  }
  for (size_t i = 0; i < conns_.size(); i++) {
    Conn& c = *conns_[i];
    if (c.dead) continue;
    char buf[8192];
    for (;;) {
      auto n = recv(c.s, buf, sizeof buf, 0);
      if (n > 0) {
        c.lastActivity = now;
        if (c.role == Conn::Pipe && c.peer) {
          c.peer->out.append(buf, (size_t)n);
          if (c.peer->out.size() > limits.maxOutBuf) {
            Conn* slow = c.peer;
            drop(*slow);
            slow->closeAfter = true;
            break;
          }
        } else c.in.append(buf, (size_t)n);
        if (c.in.size() > 4096) { drop(c); break; }
      } else {
        if (n == 0 || !wouldBlock()) drop(c);
        break;
      }
    }
    while (!c.dead && !c.closeAfter && c.role != Conn::Pipe) {
      size_t nl = c.in.find('\n');
      if (nl == std::string::npos) break;
      std::string l = c.in.substr(0, nl);
      c.in.erase(0, nl + 1);
      if (l.rfind("PROXY", 0) != 0 && !l.empty() && l.back() == '\r') l.pop_back();
      line(c, l);
      if (c.role == Conn::Pipe && !c.in.empty() && c.peer) {
        c.peer->out += c.in;
        c.in.clear();
      }
    }
    if (c.dead) continue;
    bool expired = false;
    if (c.role == Conn::Unknown || c.role == Conn::Waiting) expired = since(c.created) > limits.handshakeMs;
    else if (c.role == Conn::Pipe) expired = since(c.lastActivity) > limits.pipeIdleMs && c.peer && since(c.peer->lastActivity) > limits.pipeIdleMs;
    else if (c.role == Conn::HostCtl || c.role == Conn::WatchCtl) expired = since(c.lastActivity) > limits.hostIdleMs;
    if (expired) { c.closeAfter = true; drop(c); }
  }
  for (auto& cp : conns_) {
    Conn& c = *cp;
    if (c.s == BAD_SOCK) continue;
    if (c.out.size() > limits.maxOutBuf) drop(c);
    while (!c.out.empty()) {
      auto n = ::send(c.s, c.out.data(), (socklen_arg)c.out.size(), MSG_NOSIGNAL);
      if (n > 0) c.out.erase(0, (size_t)n);
      else {
        if (n < 0 && !wouldBlock()) drop(c);
        break;
      }
    }
    if (c.dead || (c.closeAfter && c.out.empty())) {
      drop(c);
      closeSock(c.s);
      c.s = BAD_SOCK;
    }
  }
  conns_.erase(std::remove_if(conns_.begin(), conns_.end(), [](const std::unique_ptr<Conn>& c) { return c->s == BAD_SOCK; }), conns_.end());
}

}
