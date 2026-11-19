#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "online/udp.h"

namespace pp::online {

namespace rdv {
constexpr uint8_t kMagic0 = 'P', kMagic1 = 'R';
enum : uint8_t { kHello = 1, kCookie = 2, kReg = 3, kRegOk = 4, kPeer = 5, kObs = 6, kObsr = 7, kDiscover = 8, kDiscovered = 9 };
constexpr size_t kMinRequest = 64, kMaxRequest = 128;
}

class Rendezvous {
 public:
  struct Limits {
    size_t maxPairs = 512, maxTrackedIps = 4096;
    double perIpPerSec = 20, perIpBurst = 40;
    double totalPerSec = 4000, totalBurst = 8000;
    int pairTtlMs = 10 * 60 * 1000;
  };
  Limits limits;

  Rendezvous();
  bool listen(int port, int auxPort, std::string* err);
  void attach(std::unique_ptr<DatagramPort> main, std::unique_ptr<DatagramPort> aux);
  void poll(uint64_t now);
  bool addPair(const std::string& key128hex, uint64_t now);
  void removePair(const std::string& key128hex);
  size_t pairs() const { return pairs_.size(); }
  int port() const { return main_ ? main_->localPort() : 0; }
  int auxPort() const { return aux_ ? aux_->localPort() : 0; }
  bool active() const { return main_ != nullptr; }

  struct Stats {
    uint64_t received = 0, answered = 0, rateLimited = 0, malformed = 0, badCookie = 0, unknownPair = 0, paired = 0;
  } stats;

 private:
  struct Pair {
    uint64_t created = 0;
    Endpoint ep[2];
    bool have[2]{false, false};
  };
  struct Bucket {
    double tokens = 0;
    uint64_t last = 0;
  };
  std::unique_ptr<DatagramPort> main_, aux_;
  std::map<std::string, Pair> pairs_;
  std::map<Endpoint, Bucket> buckets_;
  Bucket total_;
  SipKey secret_;
  uint64_t lastSweep_ = 0;

  bool allow(const Endpoint& from, uint64_t now);
  uint64_t cookieFor(const Endpoint& e, uint64_t bucket) const;
  void handle(DatagramPort& port, const Endpoint& from, const uint8_t* d, size_t n, uint64_t now);
  void sweep(uint64_t now);
};

}
