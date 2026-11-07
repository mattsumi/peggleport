#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace pp::online {

uint64_t nowMs();

struct Endpoint {
  uint8_t ip[16]{};
  uint16_t port = 0;
  bool valid() const { return port != 0; }
  bool operator==(const Endpoint& o) const { return port == o.port && std::memcmp(ip, o.ip, 16) == 0; }
  bool operator!=(const Endpoint& o) const { return !(*this == o); }
  bool operator<(const Endpoint& o) const {
    int c = std::memcmp(ip, o.ip, 16);
    return c != 0 ? c < 0 : port < o.port;
  }
  bool sameHost(const Endpoint& o) const { return std::memcmp(ip, o.ip, 16) == 0; }
  std::string str() const;
  static Endpoint v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);
  static bool resolve(const std::string& host, int port, Endpoint* out, std::string* err);
};

class DatagramPort {
 public:
  virtual ~DatagramPort() = default;
  virtual bool sendTo(const Endpoint& to, const uint8_t* d, size_t n) = 0;
  virtual int recvFrom(Endpoint* from, uint8_t* buf, size_t cap) = 0;
  virtual int localPort() const = 0;
};
std::unique_ptr<DatagramPort> openUdpPort(int bindPort, std::string* err);

struct LinkSim {
  uint32_t seed = 1;
  double loss = 0.0, dup = 0.0;
  int baseDelayMs = 0, jitterMs = 0;
};
class LossyPort : public DatagramPort {
 public:
  LossyPort(std::unique_ptr<DatagramPort> inner, LinkSim sim);
  bool sendTo(const Endpoint& to, const uint8_t* d, size_t n) override;
  int recvFrom(Endpoint* from, uint8_t* buf, size_t cap) override;
  int localPort() const override { return inner_->localPort(); }
  uint64_t sent = 0, dropped = 0, duplicated = 0, delivered = 0, reordered = 0;
  struct Tally {
    uint64_t dropped = 0, duplicated = 0, reordered = 0;
    std::set<const LossyPort*> live;
  };
  void attach(Tally* t);
  ~LossyPort() override;

 private:
  struct Item {
    uint64_t due, order;
    Endpoint to;
    std::vector<uint8_t> data;
  };
  Tally* tally_ = nullptr;
  std::unique_ptr<DatagramPort> inner_;
  LinkSim sim_;
  uint64_t rng_, order_ = 0, lastDeliveredOrder_ = 0;
  std::deque<Item> q_;
  double rnd();
  void flush();
};

struct SipKey {
  uint64_t k0 = 0, k1 = 0;
};
uint64_t siphash24(const SipKey& k, const uint8_t* d, size_t n);
bool parseHexKey(const std::string& hex32, SipKey* out);
std::string randomHex(int bytes);

}
