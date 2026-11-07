#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace pp::modhost {

constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxFrame = 4u << 20;
constexpr uint32_t kMaxEffectBytes = 1u << 20;
constexpr size_t kMaxStateKey = 48;
constexpr size_t kMaxStateCount = 256;
constexpr size_t kMaxStateWire = 4096;

enum : uint32_t {
  kNoNetwork = 1,
  kNoFileWrite = 2,
  kNoSpawn = 4,
  kReadRestricted = 8,
  kAllProtections = 15
};

enum Msg : uint8_t {
  MInit = 1,
  MGo = 2,
  MHook = 3,
  MShutdown = 4,
  MRngVal = 5,
  MReady = 16,
  MInitDone = 17,
  MReply = 18,
  MRngReq = 19,
  MBye = 20
};

enum Hook : uint8_t {
  HLevelStart,
  HTick,
  HShot,
  HPegHit,
  HBallLost,
  HLevelEnd,
  HPower,
  HActivate,
  HFrame,
  HShutdown,
  HookCount
};

enum Effect : uint8_t {
  ELog = 1,
  EAddScore,
  EHitPeg,
  EAddBalls,
  ESay,
  EState,
  EReserved,
  ERect,
  ECircle,
  EText
};

enum Context : uint8_t { CtxNone = 0, CtxSim = 1, CtxDraw = 2 };

inline uint32_t hookBit(Hook h) { return 1u << (unsigned)h; }

inline bool stateSetAllowed(const std::map<std::string, double>& m, const char* key, double v) {
  if (!key || std::strlen(key) > kMaxStateKey || !(v == v)) return false;
  if (m.size() >= kMaxStateCount && !m.count(key)) return false;
  return true;
}

class Writer {
 public:
  explicit Writer(std::vector<uint8_t>& b) : b_(b) {}
  void raw(const void* p, size_t n) {
    size_t at = b_.size();
    b_.resize(at + n);
    if (n) std::memcpy(b_.data() + at, p, n);
  }
  void u8(uint8_t v) { b_.push_back(v); }
  void u16(uint16_t v) { raw(&v, 2); }
  void u32(uint32_t v) { raw(&v, 4); }
  void i32(int32_t v) { raw(&v, 4); }
  void f32(float v) { raw(&v, 4); }
  void f64(double v) { raw(&v, 8); }
  void str(const char* s, size_t n, size_t cap) {
    if (n > cap) n = cap;
    u16((uint16_t)n);
    raw(s, n);
  }
  void str(const std::string& s, size_t cap = 65535) { str(s.data(), s.size(), cap); }
  uint8_t* grow(size_t n) {
    size_t at = b_.size();
    b_.resize(at + n);
    return b_.data() + at;
  }
  size_t size() const { return b_.size(); }
  void patch32(size_t at, uint32_t v) { std::memcpy(b_.data() + at, &v, 4); }
  void beginFrame(Msg t) {
    u32(0);
    u8((uint8_t)t);
  }
  void endFrame(size_t start = 0) { patch32(start, (uint32_t)(b_.size() - start - 4)); }

 private:
  std::vector<uint8_t>& b_;
};

class Reader {
 public:
  Reader(const uint8_t* p, size_t n) : p_(p), e_(p + n) {}
  bool ok() const { return ok_; }
  size_t left() const { return (size_t)(e_ - p_); }
  void raw(void* out, size_t n) {
    if (!ok_ || left() < n) {
      ok_ = false;
      if (n) std::memset(out, 0, n);
      return;
    }
    if (n) std::memcpy(out, p_, n);
    p_ += n;
  }
  uint8_t u8() { uint8_t v = 0; raw(&v, 1); return v; }
  uint16_t u16() { uint16_t v = 0; raw(&v, 2); return v; }
  uint32_t u32() { uint32_t v = 0; raw(&v, 4); return v; }
  int32_t i32() { int32_t v = 0; raw(&v, 4); return v; }
  float f32() { float v = 0; raw(&v, 4); return v; }
  double f64() { double v = 0; raw(&v, 8); return v; }
  std::string str(size_t cap = 65535) {
    size_t n = u16();
    if (!ok_ || n > cap || left() < n) {
      ok_ = false;
      return {};
    }
    std::string s((const char*)p_, n);
    p_ += n;
    return s;
  }
  const uint8_t* take(size_t n) {
    if (!ok_ || left() < n) {
      ok_ = false;
      return nullptr;
    }
    const uint8_t* r = p_;
    p_ += n;
    return r;
  }

 private:
  const uint8_t* p_;
  const uint8_t* e_;
  bool ok_ = true;
};

struct PegRec {
  int16_t type;
  uint8_t lit, gone;
  float x, y, radius;
};
struct BallRec {
  int32_t alive;
  float x, y, vx, vy, r;
};

constexpr size_t kPegWire = 16;
constexpr size_t kBallWire = 24;

}
