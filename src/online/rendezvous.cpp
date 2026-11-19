#include "online/rendezvous.h"

#include <algorithm>
#include <vector>
#include <cstdint>
#include <utility>
#include <cstring>
#include <memory>
#include <string>
#include <cstdio>

namespace pp::online {

using namespace rdv;

Rendezvous::Rendezvous() {
  secret_.k0 = (uint64_t)std::stoull(randomHex(8), nullptr, 16);
  secret_.k1 = (uint64_t)std::stoull(randomHex(8), nullptr, 16);
}

bool Rendezvous::listen(int port, int auxPort, std::string* err) {
  std::string e;
  auto m = openUdpPort(port, &e);
  if (!m) { if (err) *err = e; return false; }
  std::unique_ptr<DatagramPort> a;
  if (auxPort >= 0) {
    a = openUdpPort(auxPort, nullptr);
    if (!a && auxPort != 0) a = openUdpPort(0, nullptr);
  }
  attach(std::move(m), std::move(a));
  return true;
}

void Rendezvous::attach(std::unique_ptr<DatagramPort> main, std::unique_ptr<DatagramPort> aux) {
  main_ = std::move(main);
  aux_ = std::move(aux);
}

bool Rendezvous::addPair(const std::string& key, uint64_t now) {
  if (!main_ || key.size() != 32 || pairs_.size() >= limits.maxPairs) return false;
  Pair p;
  p.created = now;
  pairs_[key] = p;
  return true;
}

void Rendezvous::removePair(const std::string& key) { pairs_.erase(key); }

static void refill(double& tokens, uint64_t& last, uint64_t now, double rate, double burst) {
  if (now > last) tokens = std::min(burst, tokens + (double)(now - last) * rate / 1000.0);
  last = now;
}

bool Rendezvous::allow(const Endpoint& from, uint64_t now) {
  if (total_.last == 0) { total_.tokens = limits.totalBurst; total_.last = now; }
  refill(total_.tokens, total_.last, now, limits.totalPerSec, limits.totalBurst);
  if (total_.tokens < 1) return false;
  Endpoint k = from;
  k.port = 0;
  auto it = buckets_.find(k);
  if (it == buckets_.end()) {
    if (buckets_.size() >= limits.maxTrackedIps) {
      sweep(now);
      if (buckets_.size() >= limits.maxTrackedIps) return false;
    }
    it = buckets_.emplace(k, Bucket{limits.perIpBurst, now}).first;
  }
  refill(it->second.tokens, it->second.last, now, limits.perIpPerSec, limits.perIpBurst);
  if (it->second.tokens < 1) return false;
  it->second.tokens -= 1;
  total_.tokens -= 1;
  return true;
}

void Rendezvous::sweep(uint64_t now) {
  lastSweep_ = now;
  for (auto it = buckets_.begin(); it != buckets_.end();) {
    if (now > it->second.last + 10000) it = buckets_.erase(it);
    else ++it;
  }
  for (auto it = pairs_.begin(); it != pairs_.end();) {
    if (now > it->second.created + (uint64_t)limits.pairTtlMs) it = pairs_.erase(it);
    else ++it;
  }
}

uint64_t Rendezvous::cookieFor(const Endpoint& e, uint64_t bucket) const {
  uint8_t b[26];
  std::memcpy(b, e.ip, 16);
  b[16] = uint8_t(e.port);
  b[17] = uint8_t(e.port >> 8);
  for (int i = 0; i < 8; i++) b[18 + i] = uint8_t(bucket >> (8 * i));
  return siphash24(secret_, b, sizeof b);
}

static void putEp(std::vector<uint8_t>& o, const Endpoint& e) {
  o.insert(o.end(), e.ip, e.ip + 16);
  o.push_back(uint8_t(e.port >> 8));
  o.push_back(uint8_t(e.port));
}

void Rendezvous::handle(DatagramPort& port, const Endpoint& from, const uint8_t* d, size_t n, uint64_t now) {
  if (n < kMinRequest || n > kMaxRequest || d[0] != kMagic0 || d[1] != kMagic1) { stats.malformed++; return; }
  std::vector<uint8_t> r = {kMagic0, kMagic1};
  uint64_t bucket = now / 30000;
  switch (d[2]) {
    case kDiscover: {
      if (&port != main_.get()) { stats.malformed++; return; }
      r.push_back(kDiscovered);
      r.insert(r.end(), d + 3, d + 11);
      r.push_back(uint8_t(main_->localPort() >> 8));
      r.push_back(uint8_t(main_->localPort()));
      const uint8_t label[] = "PegglePort LAN discovery";
      const uint64_t identity = siphash24(secret_, label, sizeof label - 1);
      for (int i = 0; i < 8; i++) r.push_back(uint8_t(identity >> (8 * i)));
      break;
    }
    case kHello: {
      if (&port != main_.get()) { stats.malformed++; return; }
      r.push_back(kCookie);
      uint64_t c = cookieFor(from, bucket);
      for (int i = 0; i < 8; i++) r.push_back(uint8_t(c >> (8 * i)));
      putEp(r, from);
      break;
    }
    case kObs:
      r.push_back(kObsr);
      putEp(r, from);
      break;
    case kReg: {
      if (&port != main_.get()) { stats.malformed++; return; }
      uint64_t got = 0;
      for (int i = 0; i < 8; i++) got |= (uint64_t)d[3 + i] << (8 * i);
      if (got != cookieFor(from, bucket) && got != cookieFor(from, bucket - 1)) { stats.badCookie++; return; }
      char hex[33];
      for (int i = 0; i < 16; i++) std::snprintf(hex + 2 * i, 3, "%02x", d[11 + i]);
      int role = d[27];
      auto it = pairs_.find(hex);
      if (role > 1 || it == pairs_.end()) { stats.unknownPair++; return; }
      Pair& p = it->second;
      p.ep[role] = from;
      p.have[role] = true;
      if (p.have[1 - role]) {
        r.push_back(kPeer);
        putEp(r, p.ep[1 - role]);
        putEp(r, from);
        stats.paired++;
      } else {
        r.push_back(kRegOk);
        putEp(r, from);
      }
      break;
    }
    default:
      stats.malformed++;
      return;
  }
  if (r.size() >= n) { stats.malformed++; return; }
  port.sendTo(from, r.data(), r.size());
  stats.answered++;
}

void Rendezvous::poll(uint64_t now) {
  if (!main_) return;
  if (now > lastSweep_ + 5000) sweep(now);
  uint8_t buf[512];
  for (DatagramPort* p : {main_.get(), aux_.get()}) {
    if (!p) continue;
    for (int i = 0; i < 128; i++) {
      Endpoint from;
      int n = p->recvFrom(&from, buf, sizeof buf);
      if (n <= 0) break;
      stats.received++;
      if (!allow(from, now)) { stats.rateLimited++; continue; }
      handle(*p, from, buf, (size_t)n, now);
    }
  }
}

}
