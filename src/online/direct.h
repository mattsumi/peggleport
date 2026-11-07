#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include "online/rudp.h"
#include "online/udp.h"

namespace pp::online {

struct DirectParams {
  std::string serverHost;
  int serverPort = 0, auxPort = 0;
  std::string pairKeyHex;
  bool isHost = false;
  RudpConfig rudp;
  int probeTimeoutMs = 2500, registerTimeoutMs = 4000, punchTimeoutMs = 4000, auxWaitMs = 1000;
  int retryMs = 250, punchEveryMs = 100;
  std::function<std::unique_ptr<DatagramPort>(std::unique_ptr<DatagramPort>)> wrap;
};

class DirectLink {
 public:
  enum State { Probing, Registering, Punching, Up, Failed, Closed };
  ~DirectLink();
  bool start(const DirectParams& p, std::string* err);
  void poll();
  State state() const { return state_; }
  bool up() const { return state_ == Up; }
  bool failed() const { return state_ == Failed; }
  const std::string& note() const { return note_; }
  std::string stateName() const;
  void write(const std::vector<uint8_t>& v) { if (rudp_) rudp_->write(v); }
  void read(std::vector<uint8_t>* out) { if (rudp_) rudp_->read(out); }
  bool streamDead() const { return rudp_ && rudp_->dead(); }
  const std::string& deadReason() const;
  uint64_t lastRecvAgeMs() const;
  void close();
  Endpoint publicEndpoint() const { return obsMain_; }
  Endpoint peerEndpoint() const { return peer_; }
  bool symmetricNat() const { return symmetric_; }
  const Rudp* stream() const { return rudp_.get(); }
  double rttMs() const { return rudp_ ? rudp_->rttMs() : 0; }

 private:
  DirectParams p_;
  SipKey key_;
  State state_ = Probing;
  std::unique_ptr<DatagramPort> port_;
  std::unique_ptr<Rudp> rudp_;
  Endpoint server_, aux_, obsMain_, obsAux_, peer_;
  uint8_t cookie_[8]{};
  bool haveCookie_ = false, haveAux_ = false, symmetric_ = false, heard_ = false, peerConfirmed_ = false;
  uint64_t t0_ = 0, stateSince_ = 0, lastSend_ = 0, lastPunchReply_ = 0;
  std::string note_, deadReason_;
  void enter(State s, const std::string& note);
  void sendRendezvous(uint8_t kind, bool withCookie);
  void sendPunch();
  void onServer(const Endpoint& from, const uint8_t* d, size_t n);
  bool onPeer(const Endpoint& from, const uint8_t* d, size_t n);
  void ensureStream();
  void raw(const uint8_t* d, size_t n) { if (port_ && peer_.valid()) port_->sendTo(peer_, d, n); }
};

std::string probeLobbyUdp(const std::string& host, int port, bool* reachable);

}
