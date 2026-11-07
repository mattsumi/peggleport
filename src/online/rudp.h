#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "online/udp.h"

namespace pp::online {

struct RudpConfig {
  int mtuPayload = 1100;
  int initialRtoMs = 250, minRtoMs = 60, maxRtoMs = 2000;
  int keepaliveMs = 1000, timeoutMs = 10000;
  int window = 64;
  int maxRetries = 40;
};

class Rudp {
 public:
  using SendFn = std::function<void(const uint8_t*, size_t)>;
  static constexpr uint8_t kData = 0x40, kAck = 0x41, kClose = 0x43;

  Rudp(const SipKey& key, const RudpConfig& cfg, uint64_t now);
  void write(const uint8_t* d, size_t n);
  void write(const std::vector<uint8_t>& v) { write(v.data(), v.size()); }
  void read(std::vector<uint8_t>* out);
  bool onDatagram(const uint8_t* d, size_t n, uint64_t now);
  void update(uint64_t now, const SendFn& send);
  void sendClose(const SendFn& send);
  bool dead() const { return dead_; }
  const std::string& deadReason() const { return reason_; }
  uint64_t lastRecvMs() const { return lastRecv_; }
  size_t unacked() const { return out_.size(); }
  size_t queued() const { return sendBuf_.size(); }
  double rttMs() const { return srtt_; }
  struct Stats {
    uint64_t dataSent = 0, retransmits = 0, fastRetransmits = 0, dataRecv = 0, dupRecv = 0, oooRecv = 0, acksSent = 0, badPackets = 0;
  } stats;

  static bool tagged(const SipKey& key, std::vector<uint8_t>& pkt);
  static bool verify(const SipKey& key, const uint8_t* d, size_t n);

 private:
  struct Out {
    uint32_t seq;
    std::vector<uint8_t> data;
    uint64_t firstSent = 0, lastSent = 0;
    int sends = 0;
    bool acked = false;
  };
  SipKey key_;
  RudpConfig cfg_;
  std::deque<uint8_t> sendBuf_;
  std::deque<Out> out_;
  uint32_t sndNext_ = 0, rcvNext_ = 0;
  std::map<uint32_t, std::vector<uint8_t>> ooo_;
  std::vector<uint8_t> recvBuf_;
  bool ackDue_ = false, dead_ = false;
  std::string reason_;
  uint64_t lastSend_, lastRecv_;
  double srtt_ = 0, rttvar_ = 0;
  bool haveRtt_ = false;

  int rto() const;
  void emit(uint8_t kind, uint32_t seq, const uint8_t* payload, size_t n, const SendFn& send);
  void processAck(uint32_t ack, uint32_t sack, uint64_t now);
};

}
