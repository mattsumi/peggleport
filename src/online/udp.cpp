#include "online/udp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <cstdint>
#include <utility>
#include <cstring>
#include <memory>
#include <vector>
#include <string>

#include "sockcompat.h"

#ifdef _WIN32
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

namespace pp::online {

uint64_t nowMs() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static bool isV4Mapped(const uint8_t* ip) {
  static const uint8_t m[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
  return std::memcmp(ip, m, 12) == 0;
}

std::string Endpoint::str() const {
  char b[96];
  if (isV4Mapped(ip)) {
    std::snprintf(b, sizeof b, "%u.%u.%u.%u:%u", ip[12], ip[13], ip[14], ip[15], port);
  } else {
    char t[INET6_ADDRSTRLEN] = "?";
    in6_addr a;
    std::memcpy(&a, ip, 16);
    inet_ntop(AF_INET6, &a, t, sizeof t);
    std::snprintf(b, sizeof b, "[%s]:%u", t, port);
  }
  return b;
}

Endpoint Endpoint::v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
  Endpoint e;
  e.ip[10] = e.ip[11] = 0xff;
  e.ip[12] = a; e.ip[13] = b; e.ip[14] = c; e.ip[15] = d;
  e.port = port;
  return e;
}

bool Endpoint::resolve(const std::string& host, int port, Endpoint* out, std::string* err) {
  netStartup();
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
    if (err) *err = "cannot resolve " + host;
    return false;
  }
  bool ok = false;
  for (int pass = 0; pass < 2 && !ok; pass++)
    for (addrinfo* ai = res; ai && !ok; ai = ai->ai_next) {
      Endpoint e;
      e.port = (uint16_t)port;
      if (ai->ai_family == AF_INET && pass == 0) {
        const uint8_t* b = (const uint8_t*)&((sockaddr_in*)ai->ai_addr)->sin_addr;
        e = v4(b[0], b[1], b[2], b[3], (uint16_t)port);
        *out = e;
        ok = true;
      } else if (ai->ai_family == AF_INET6 && pass == 1) {
        std::memcpy(e.ip, &((sockaddr_in6*)ai->ai_addr)->sin6_addr, 16);
        *out = e;
        ok = true;
      }
    }
  freeaddrinfo(res);
  if (!ok && err) *err = "no usable address for " + host;
  return ok;
}

namespace {
class UdpPort : public DatagramPort {
 public:
  sock_t s = BAD_SOCK;
  bool v6 = false;
  int port = 0;
  ~UdpPort() override { if (s != BAD_SOCK) closeSock(s); }

  bool sendTo(const Endpoint& to, const uint8_t* d, size_t n) override {
    sockaddr_storage ss{};
    socklen_t sl;
    if (v6) {
      sockaddr_in6* a = (sockaddr_in6*)&ss;
      a->sin6_family = AF_INET6;
      std::memcpy(&a->sin6_addr, to.ip, 16);
      a->sin6_port = htons(to.port);
      sl = sizeof *a;
    } else {
      if (!isV4Mapped(to.ip)) return false;
      sockaddr_in* a = (sockaddr_in*)&ss;
      a->sin_family = AF_INET;
      std::memcpy(&a->sin_addr, to.ip + 12, 4);
      a->sin_port = htons(to.port);
      sl = sizeof *a;
    }
    auto r = ::sendto(s, (const char*)d, (int)n, MSG_NOSIGNAL, (sockaddr*)&ss, sl);
    return r == (decltype(r))n;
  }

  int recvFrom(Endpoint* from, uint8_t* buf, size_t cap) override {
    sockaddr_storage ss{};
    socklen_t sl = sizeof ss;
    auto r = ::recvfrom(s, (char*)buf, (int)cap, 0, (sockaddr*)&ss, &sl);
    if (r <= 0) return 0;
    Endpoint e;
    if (ss.ss_family == AF_INET6) {
      std::memcpy(e.ip, &((sockaddr_in6*)&ss)->sin6_addr, 16);
      e.port = ntohs(((sockaddr_in6*)&ss)->sin6_port);
    } else {
      e = Endpoint::v4(0, 0, 0, 0, ntohs(((sockaddr_in*)&ss)->sin_port));
      std::memcpy(e.ip + 12, &((sockaddr_in*)&ss)->sin_addr, 4);
    }
    if (from) *from = e;
    return (int)r;
  }
  int localPort() const override { return port; }
};
}

std::unique_ptr<DatagramPort> openUdpPort(int bindPort, std::string* err, bool ipv4Broadcast) {
  netStartup();
  auto p = std::make_unique<UdpPort>();
  sock_t s = ipv4Broadcast ? BAD_SOCK : socket(AF_INET6, SOCK_DGRAM, 0);
  p->v6 = s != BAD_SOCK;
  if (p->v6) {
    int zero = 0;
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&zero, sizeof zero);
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_any;
    a.sin6_port = htons((uint16_t)bindPort);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) {
      closeSock(s);
      s = BAD_SOCK;
      p->v6 = false;
    }
  }
  if (!p->v6) {
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCK) { if (err) *err = "udp socket() failed"; return nullptr; }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)bindPort);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) {
      closeSock(s);
      if (err) *err = "cannot bind udp port " + std::to_string(bindPort);
      return nullptr;
    }
  }
  if (ipv4Broadcast) {
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one) != 0) {
      closeSock(s);
      if (err) *err = "cannot enable UDP broadcast";
      return nullptr;
    }
  }
  sockaddr_storage ss{};
  socklen_t sl = sizeof ss;
  getsockname(s, (sockaddr*)&ss, &sl);
  p->port = ntohs(ss.ss_family == AF_INET6 ? ((sockaddr_in6*)&ss)->sin6_port : ((sockaddr_in*)&ss)->sin_port);
#ifdef _WIN32
  DWORD bytes = 0;
  BOOL off = FALSE;
  WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &bytes, nullptr, nullptr);
#endif
  nonBlock(s);
  p->s = s;
  return p;
}

static uint64_t splitmix(uint64_t& x) {
  uint64_t z = (x += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

LossyPort::LossyPort(std::unique_ptr<DatagramPort> inner, LinkSim sim) : inner_(std::move(inner)), sim_(sim), rng_(0x1234567ull * (sim.seed + 1)) {}

void LossyPort::attach(Tally* t) {
  if (tally_) {
    tally_->live.erase(this);
    tally_->dropped += dropped;
    tally_->duplicated += duplicated;
    tally_->reordered += reordered;
  }
  tally_ = t;
  if (t) t->live.insert(this);
}
LossyPort::~LossyPort() { attach(nullptr); }

void LossyPort::releaseObservers() {
  attach(nullptr);
  inner_->releaseObservers();
}

double LossyPort::rnd() { return (double)(splitmix(rng_) >> 11) / 9007199254740992.0; }

void LossyPort::flush() {
  if (q_.empty()) return;
  uint64_t now = nowMs();
  std::stable_sort(q_.begin(), q_.end(), [](const Item& a, const Item& b) { return a.due < b.due; });
  while (!q_.empty() && q_.front().due <= now) {
    Item& it = q_.front();
    if (it.order < lastDeliveredOrder_) reordered++;
    lastDeliveredOrder_ = std::max(lastDeliveredOrder_, it.order);
    inner_->sendTo(it.to, it.data.data(), it.data.size());
    delivered++;
    q_.pop_front();
  }
}

bool LossyPort::sendTo(const Endpoint& to, const uint8_t* d, size_t n) {
  flush();
  sent++;
  if (rnd() < sim_.loss) { dropped++; return true; }
  int copies = rnd() < sim_.dup ? 2 : 1;
  if (copies == 2) duplicated++;
  for (int c = 0; c < copies; c++) {
    int delay = sim_.baseDelayMs + (sim_.jitterMs > 0 ? (int)(splitmix(rng_) % (uint64_t)(sim_.jitterMs + 1)) : 0);
    q_.push_back({nowMs() + (uint64_t)delay, order_++, to, std::vector<uint8_t>(d, d + n)});
  }
  flush();
  return true;
}

int LossyPort::recvFrom(Endpoint* from, uint8_t* buf, size_t cap) {
  flush();
  return inner_->recvFrom(from, buf, cap);
}

static inline uint64_t rotl(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

uint64_t siphash24(const SipKey& k, const uint8_t* d, size_t n) {
  uint64_t v0 = 0x736f6d6570736575ull ^ k.k0, v1 = 0x646f72616e646f6dull ^ k.k1, v2 = 0x6c7967656e657261ull ^ k.k0, v3 = 0x7465646279746573ull ^ k.k1;
  auto round = [&] {
    v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);
    v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;
    v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;
    v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);
  };
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t m = 0;
    for (int j = 0; j < 8; j++) m |= (uint64_t)d[i + j] << (8 * j);
    v3 ^= m; round(); round(); v0 ^= m;
  }
  uint64_t b = (uint64_t)n << 56;
  for (size_t j = 0; i + j < n; j++) b |= (uint64_t)d[i + j] << (8 * j);
  v3 ^= b; round(); round(); v0 ^= b;
  v2 ^= 0xff;
  round(); round(); round(); round();
  return v0 ^ v1 ^ v2 ^ v3;
}

bool parseHexKey(const std::string& h, SipKey* out) {
  if (h.size() != 32) return false;
  uint64_t v[2] = {0, 0};
  for (int i = 0; i < 32; i++) {
    char c = h[i];
    int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (x < 0) return false;
    v[i / 16] = (v[i / 16] << 4) | (uint64_t)x;
  }
  out->k0 = v[0];
  out->k1 = v[1];
  return true;
}

std::string randomHex(int bytes) {
  static thread_local std::random_device rd;
  std::string s;
  char b[4];
  for (int i = 0; i < bytes; i++) {
    std::snprintf(b, sizeof b, "%02x", (unsigned)(rd() & 0xff));
    s += b;
  }
  return s;
}

}
