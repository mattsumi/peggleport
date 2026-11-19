#include "ui/replay.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <system_error>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>

#include "gamepaths.h"

namespace pp::ui {

namespace {
struct W {
  std::vector<uint8_t> b;
  void u32(uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
  void i32(int v) { u32((uint32_t)v); }
  void f32(float f) { uint32_t v; static_assert(sizeof v == sizeof f); std::memcpy(&v, &f, 4); u32(v); }
  void str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
};
struct R {
  const std::vector<uint8_t>& b;
  size_t p = 0;
  bool bad = false;
  uint32_t u32() {
    if (p + 4 > b.size()) { bad = true; return 0; }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)b[p + i] << (8 * i);
    p += 4;
    return v;
  }
  int i32() { return (int)u32(); }
  float f32() { uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
  std::string str() {
    uint32_t n = u32();
    if (bad || p + n > b.size()) { bad = true; return {}; }
    std::string s((const char*)&b[p], n);
    p += n;
    return s;
  }
};

void putCfg(W& w, const BoardConfig& c) {
  w.i32(c.boardWidth); w.i32(c.players); w.i32(c.duel); w.i32(c.balls); w.i32(c.orange); w.u32(c.seed); w.i32(c.greens); w.i32(c.purple);
  for (int i = 0; i < 4; i++) w.i32(c.master[i]);
  w.f32(c.zoomK); w.i32(c.fpBits); w.i32(c.masterIndex); w.i32(c.maxLevel); w.i32(c.curLevel); w.i32(c.clearLevel); w.i32(c.coinFlipSeen);
  w.i32(c.nights); w.i32(c.prevFirst); w.i32(c.prevFirstRun); w.i32(c.tryNumber);
}
void getCfg(R& r, BoardConfig& c) {
  c.boardWidth = r.i32(); c.players = r.i32(); c.duel = r.i32() != 0; c.balls = r.i32(); c.orange = r.i32(); c.seed = r.u32(); c.greens = r.i32() != 0; c.purple = r.i32() != 0;
  for (int i = 0; i < 4; i++) c.master[i] = r.i32();
  c.zoomK = r.f32(); c.fpBits = r.i32(); c.masterIndex = r.i32(); c.maxLevel = r.i32(); c.curLevel = r.i32(); c.clearLevel = r.i32() != 0; c.coinFlipSeen = r.i32() != 0;
  c.nights = r.i32() != 0; c.prevFirst = r.i32(); c.prevFirstRun = r.i32(); c.tryNumber = r.i32();
}
constexpr uint32_t kMagic = 0x50525050;
constexpr uint32_t kVersion = 1;
constexpr uint32_t kVersionPhysics = 2;
constexpr uint32_t kVersionFairCoin = 3;

void putPhysics(W& w, const PhysicsEdit& e) {
  w.u32(e.mask);
  for (int k = 0; k < PhysicsParams::KeyCount; k++) w.f32(e.v.at(k));
}
void getPhysics(R& r, PhysicsEdit& e) {
  uint32_t mask = r.u32();
  PhysicsEdit t;
  for (int k = 0; k < PhysicsParams::KeyCount; k++) {
    float v = r.f32();
    if ((mask >> k) & 1u) t.set(k, (double)v);
  }
  if (!r.bad) e = t;
}
}

std::string replayExt(int game) { return game == 1 ? ".pegn" : ".pego"; }

bool ReplayData::save(const std::filesystem::path& path) const {
  W w;
  const uint32_t version = cfg.legacyMissCoin ? (cfg.physics.any() ? kVersionPhysics : kVersion) : kVersionFairCoin;
  w.u32(kMagic); w.u32(version);
  w.i32(game); w.str(level); w.str(title); w.str(player);
  w.u32((uint32_t)(when & 0xffffffff)); w.u32((uint32_t)(when >> 32));
  w.i32(score); w.i32(shots); w.i32(startTick);
  putCfg(w, cfg);
  if (version != kVersion) putPhysics(w, cfg.physics);
  w.u32((uint32_t)log.size());
  for (const auto& e : log) { w.i32(e.tick); w.u32(e.kind); w.i32(e.a); w.i32(e.b); }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write((const char*)w.b.data(), (std::streamsize)w.b.size());
  f.flush();
  return (bool)f;
}

bool ReplayData::load(const std::filesystem::path& path, ReplayData& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> buf;
  char tmp[4096];
  while (f.read(tmp, sizeof tmp) || f.gcount() > 0) buf.insert(buf.end(), tmp, tmp + f.gcount());
  R r{buf};
  if (r.u32() != kMagic) return false;
  const uint32_t version = r.u32();
  if (version != kVersion && version != kVersionPhysics && version != kVersionFairCoin) return false;
  ReplayData d;
  d.game = r.i32(); d.level = r.str(); d.title = r.str(); d.player = r.str();
  uint32_t lo = r.u32(), hi = r.u32();
  d.when = (int64_t)(((uint64_t)hi << 32) | lo);
  d.score = r.i32(); d.shots = r.i32(); d.startTick = r.i32();
  getCfg(r, d.cfg);
  d.cfg.legacyMissCoin = version != kVersionFairCoin;
  if (version != kVersion) getPhysics(r, d.cfg.physics);
  uint32_t cnt = r.u32();
  if (r.bad || cnt > 5000000 || d.startTick > 4000000 || d.cfg.boardWidth > 1400) return false;
  d.log.resize(cnt);
  for (auto& e : d.log) { e.tick = r.i32(); e.kind = (uint8_t)r.u32(); e.a = r.i32(); e.b = r.i32(); }
  if (r.bad) return false;
  out = std::move(d);
  return true;
}

std::vector<ReplayFile> listReplays(const std::filesystem::path& dir, int game) {
  std::vector<ReplayFile> v;
  std::error_code ec;
  for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
    const std::filesystem::path ext = e.path().extension();
    if (ext != ".pego" && ext != ".pegn") continue;
    ReplayFile rf;
    rf.path = e.path();
    rf.name = pathToUtf8(e.path().filename());
    if (!ReplayData::load(rf.path, rf.head) || rf.head.game != game) {
      continue;
    }
    rf.head.log.clear();
    v.push_back(std::move(rf));
  }
  std::sort(v.begin(), v.end(), [](const ReplayFile& a, const ReplayFile& b) { return a.head.when != b.head.when ? a.head.when > b.head.when : a.name < b.name; });
  return v;
}

}
