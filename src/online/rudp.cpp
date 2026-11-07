#include "online/rudp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include <cstdlib>

namespace pp::online {

static constexpr size_t kHdr = 13, kTag = 8;

static void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); }
static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool Rudp::tagged(const SipKey& key, std::vector<uint8_t>& pkt) {
  uint64_t t = siphash24(key, pkt.data(), pkt.size());
  for (int i = 0; i < 8; i++) pkt.push_back(uint8_t(t >> (8 * i)));
  return true;
}

bool Rudp::verify(const SipKey& key, const uint8_t* d, size_t n) {
  if (n < kTag + 1) return false;
  uint64_t t = siphash24(key, d, n - kTag), got = 0;
  for (int i = 0; i < 8; i++) got |= (uint64_t)d[n - kTag + i] << (8 * i);
  return t == got;
}

Rudp::Rudp(const SipKey& key, const RudpConfig& cfg, uint64_t now) : key_(key), cfg_(cfg), lastSend_(now), lastRecv_(now) {}

void Rudp::write(const uint8_t* d, size_t n) {
  if (dead_) return;
  sendBuf_.insert(sendBuf_.end(), d, d + n);
}

void Rudp::read(std::vector<uint8_t>* out) {
  out->insert(out->end(), recvBuf_.begin(), recvBuf_.end());
  recvBuf_.clear();
}

int Rudp::rto() const {
  if (!haveRtt_) return cfg_.initialRtoMs;
  return std::clamp((int)(srtt_ + 4 * rttvar_), cfg_.minRtoMs, cfg_.maxRtoMs);
}

void Rudp::emit(uint8_t kind, uint32_t seq, const uint8_t* payload, size_t n, const SendFn& send) {
  std::vector<uint8_t> p;
  p.reserve(kHdr + n + kTag);
  p.push_back(kind);
  put32(p, seq);
  put32(p, rcvNext_);
  uint32_t sack = 0;
  for (int i = 0; i < 32; i++)
    if (ooo_.count(rcvNext_ + 1 + (uint32_t)i)) sack |= 1u << i;
  put32(p, sack);
  if (n) p.insert(p.end(), payload, payload + n);
  tagged(key_, p);
  send(p.data(), p.size());
}

void Rudp::update(uint64_t now, const SendFn& send) {
  if (dead_) return;
  if (now - lastRecv_ > (uint64_t)cfg_.timeoutMs) {
    dead_ = true;
    reason_ = "direct link timed out";
    return;
  }
  bool sentAny = false;
  auto xmit = [&](Out& o) {
    emit(kData, o.seq, o.data.data(), o.data.size(), send);
    if (o.sends == 0) { o.firstSent = now; stats.dataSent++; }
    else stats.retransmits++;
    o.lastSent = now;
    o.sends++;
    sentAny = true;
    lastSend_ = now;
  };
  while (!sendBuf_.empty() && (int)out_.size() < cfg_.window) {
    Out o;
    o.seq = sndNext_++;
    size_t take = std::min(sendBuf_.size(), (size_t)cfg_.mtuPayload);
    o.data.assign(sendBuf_.begin(), sendBuf_.begin() + (long)take);
    sendBuf_.erase(sendBuf_.begin(), sendBuf_.begin() + (long)take);
    out_.push_back(std::move(o));
    xmit(out_.back());
  }
  int ackedAbove = 0;
  for (size_t i = out_.size(); i-- > 0;) {
    Out& o = out_[i];
    if (o.acked) { ackedAbove++; continue; }
    if (o.sends == 0) continue;
    if (o.sends > cfg_.maxRetries) {
      dead_ = true;
      reason_ = "direct link lost (no acknowledgements)";
      return;
    }
    int t = std::min(rto() << std::min(o.sends - 1, 4), cfg_.maxRtoMs);
    bool timeout = now - o.lastSent >= (uint64_t)t;
    bool fast = ackedAbove >= 3 && now - o.lastSent >= (uint64_t)std::max(10, (int)srtt_);
    if (timeout || fast) {
      if (!timeout) stats.fastRetransmits++;
      xmit(o);
    }
  }
  if (ackDue_ && !sentAny) {
    emit(kAck, 0, nullptr, 0, send);
    stats.acksSent++;
    lastSend_ = now;
    sentAny = true;
  }
  if (sentAny) ackDue_ = false;
  if (now - lastSend_ >= (uint64_t)cfg_.keepaliveMs) {
    emit(kAck, 0, nullptr, 0, send);
    stats.acksSent++;
    lastSend_ = now;
  }
}

void Rudp::sendClose(const SendFn& send) {
  for (int i = 0; i < 3; i++) emit(kClose, 0, nullptr, 0, send);
}

void Rudp::processAck(uint32_t ack, uint32_t sack, uint64_t now) {
  if (ack > sndNext_) return;
  for (auto& o : out_) {
    if (o.acked) continue;
    bool a = o.seq < ack || (o.seq > ack && o.seq - ack - 1 < 32 && ((sack >> (o.seq - ack - 1)) & 1));
    if (!a) continue;
    o.acked = true;
    if (o.sends == 1) {
      double s = (double)(now - o.firstSent);
      if (!haveRtt_) { srtt_ = s; rttvar_ = s / 2; haveRtt_ = true; }
      else { rttvar_ = 0.75 * rttvar_ + 0.25 * std::abs(srtt_ - s); srtt_ = 0.875 * srtt_ + 0.125 * s; }
    }
  }
  while (!out_.empty() && out_.front().acked) out_.pop_front();
}

bool Rudp::onDatagram(const uint8_t* d, size_t n, uint64_t now) {
  if (n < kHdr + kTag || !verify(key_, d, n)) { stats.badPackets++; return false; }
  uint8_t kind = d[0];
  if (kind != kData && kind != kAck && kind != kClose) { stats.badPackets++; return false; }
  lastRecv_ = now;
  if (kind == kClose) {
    dead_ = true;
    reason_ = "the other player closed the link";
    return true;
  }
  uint32_t seq = get32(d + 1), ack = get32(d + 5), sack = get32(d + 9);
  processAck(ack, sack, now);
  if (kind != kData) return true;
  size_t plen = n - kHdr - kTag;
  if (plen == 0) return true;
  ackDue_ = true;
  if (seq < rcvNext_) { stats.dupRecv++; return true; }
  if (seq >= rcvNext_ + 256) return true;
  if (ooo_.count(seq)) { stats.dupRecv++; return true; }
  stats.dataRecv++;
  if (seq != rcvNext_) stats.oooRecv++;
  ooo_[seq].assign(d + kHdr, d + kHdr + plen);
  for (auto it = ooo_.find(rcvNext_); it != ooo_.end(); it = ooo_.find(rcvNext_)) {
    recvBuf_.insert(recvBuf_.end(), it->second.begin(), it->second.end());
    ooo_.erase(it);
    rcvNext_++;
  }
  return true;
}

}
