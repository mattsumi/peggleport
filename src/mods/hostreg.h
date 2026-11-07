#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mods/hostproto.h"
#include "pegtypes.h"
#include "physics.h"

namespace pp::modhost {

constexpr size_t kRawText = 1024;
constexpr size_t kMaxPegTypes = 64;
constexpr size_t kMaxMasters = 16;
constexpr size_t kMaxStages = 16;
constexpr uint32_t kPhysicsAll = 0xffu;

inline bool relPathOk(const std::string& rel) {
  if (rel.empty() || rel[0] == '/' || rel[0] == '\\' || rel.find(':') != std::string::npos || rel.find('\0') != std::string::npos) return false;
  std::string seg;
  for (size_t i = 0; i <= rel.size(); i++) {
    if (i == rel.size() || rel[i] == '/' || rel[i] == '\\') {
      if (seg == "..") return false;
      seg.clear();
    } else seg += rel[i];
  }
  return true;
}

inline bool reservedMasterName(const std::string& id) {
  static const char* names[] = {"bjorn", "jimmy", "tut", "kat", "splork", "claude", "renfield", "hu", "tula", "cinder", "lord", "warren", "marina", "kalah", "none", "random"};
  std::string n;
  for (char c : id) n.push_back((char)std::tolower((unsigned char)c));
  for (const char* r : names)
    if (n == r) return true;
  return false;
}

inline std::string clipText(const std::string& s, size_t n) {
  if (s.size() <= n) return s;
  size_t k = n;
  while (k > 0 && ((unsigned char)s[k] & 0xc0) == 0x80) k--;
  return s.substr(0, k);
}

struct RawPegType {
  std::string id, name, image, litImage;
  std::vector<std::string> stages;
  uint32_t color = 0;
  int32_t base = 0, score = 0, countsAsOrange = 0, hitsToClear = 0, indestructible = 0;
  float bounce = 0.0f, radiusScale = 0.0f;
  bool hasHit = false, hasClear = false;
};

struct RawMaster {
  std::string id, name, description, powerName, powerDescription, portrait, banner;
  int32_t turns = 0, trigger = 0;
  uint32_t color = 0;
  bool hasActivate = false, hasShot = false, hasEnd = false;
};

struct MasterSpec {
  std::string id, name, description, powerName, powerDescription, portrait, banner;
  int turns = 3;
  int trigger = 0;
  uint32_t color = 0x88ff88;
  bool hasActivate = false, hasShot = false, hasEnd = false;
};

inline bool buildPegType(const RawPegType& in, const std::string& mod, const std::vector<PegTypeDef>& have, PegTypeDef& d) {
  if (have.size() >= kMaxPegTypes) return false;
  d = PegTypeDef();
  d.mod = mod;
  d.id = in.id;
  if (d.id.empty() || d.id.size() > 32) return false;
  for (char c : d.id)
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  for (auto& o : have)
    if (o.id == d.id) return false;
  auto pathOk = [](const std::string& p) { return p.size() <= 200 && (p.empty() || relPathOk(p)); };
  d.name = !in.name.empty() ? in.name.substr(0, 60) : d.id;
  if (!pathOk(in.image) || !pathOk(in.litImage)) return false;
  d.image = in.image;
  d.litImage = in.litImage;
  for (size_t i = 0; i < in.stages.size() && i < kMaxStages; i++) {
    if (in.stages[i].empty() || !pathOk(in.stages[i])) return false;
    d.stages.push_back(in.stages[i]);
  }
  if (in.color) {
    d.color = in.color & 0xffffffu;
    d.hasColor = true;
  }
  if (in.base < 0 || in.base > 4) return false;
  d.base = in.base ? in.base : 1;
  d.score = std::max(0, std::min(1000000, in.score));
  d.orange = in.countsAsOrange > 0 ? 1 : in.countsAsOrange < 0 ? 0 : -1;
  d.hits = in.hitsToClear > 0 ? std::min(100, in.hitsToClear) : 1;
  d.clears = in.indestructible == 0;
  if (!d.clears) {
    d.hits = 1;
    if (d.orange == 1) d.orange = 0;
  }
  d.bounce = in.bounce > 0.0f ? std::max(0.1f, std::min(3.0f, in.bounce)) : 1.0f;
  d.radiusScale = in.radiusScale > 0.0f ? std::max(0.5f, std::min(3.0f, in.radiusScale)) : 1.0f;
  d.onHit = in.hasHit;
  d.onClear = in.hasClear;
  return true;
}

inline int buildMaster(const RawMaster& in, const std::vector<MasterSpec>& have, MasterSpec& m) {
  m = MasterSpec();
  m.id = clipText(in.id, 64);
  for (char& c : m.id) {
    c = (char)std::tolower((unsigned char)c);
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return 3;
  }
  if (m.id.empty() || m.id.size() > 32 || reservedMasterName(m.id)) return 3;
  for (auto& o : have)
    if (o.id == m.id) return 4;
  if (have.size() >= kMaxMasters) return 5;
  m.name = clipText(in.name, 40);
  if (m.name.empty()) m.name = m.id;
  m.description = clipText(in.description, 240);
  m.powerName = !in.powerName.empty() ? clipText(in.powerName, 40) : "Master Power";
  m.powerDescription = clipText(in.powerDescription, 240);
  m.portrait = clipText(in.portrait, 200);
  m.banner = !in.banner.empty() ? clipText(in.banner, 40) : m.powerName + "!";
  m.turns = in.turns < 1 ? 1 : in.turns > 99 ? 99 : in.turns;
  if (in.trigger < 0 || in.trigger > 2) return 6;
  m.trigger = in.trigger;
  m.color = in.color & 0xffffffu;
  m.hasActivate = in.hasActivate;
  m.hasShot = in.hasShot;
  m.hasEnd = in.hasEnd;
  return 0;
}

inline void putPegType(Writer& w, const RawPegType& p) {
  w.str(p.id, kRawText);
  w.str(p.name, kRawText);
  w.str(p.image, kRawText);
  w.str(p.litImage, kRawText);
  const size_t ns = std::min(p.stages.size(), kMaxStages);
  w.u8((uint8_t)ns);
  for (size_t i = 0; i < ns; i++) w.str(p.stages[i], kRawText);
  w.u32(p.color);
  w.i32(p.base);
  w.i32(p.score);
  w.i32(p.countsAsOrange);
  w.i32(p.hitsToClear);
  w.i32(p.indestructible);
  w.f32(p.bounce);
  w.f32(p.radiusScale);
  w.u8((uint8_t)((p.hasHit ? 1 : 0) | (p.hasClear ? 2 : 0)));
}

inline bool getPegType(Reader& r, RawPegType& p) {
  p = RawPegType();
  p.id = r.str(kRawText);
  p.name = r.str(kRawText);
  p.image = r.str(kRawText);
  p.litImage = r.str(kRawText);
  const size_t ns = r.u8();
  if (ns > kMaxStages) return false;
  for (size_t i = 0; i < ns && r.ok(); i++) p.stages.push_back(r.str(kRawText));
  p.color = r.u32();
  p.base = r.i32();
  p.score = r.i32();
  p.countsAsOrange = r.i32();
  p.hitsToClear = r.i32();
  p.indestructible = r.i32();
  p.bounce = r.f32();
  p.radiusScale = r.f32();
  const uint8_t f = r.u8();
  p.hasHit = (f & 1) != 0;
  p.hasClear = (f & 2) != 0;
  return r.ok();
}

inline void putMaster(Writer& w, const RawMaster& m) {
  w.str(m.id, kRawText);
  w.str(m.name, kRawText);
  w.str(m.description, kRawText);
  w.str(m.powerName, kRawText);
  w.str(m.powerDescription, kRawText);
  w.str(m.portrait, kRawText);
  w.str(m.banner, kRawText);
  w.i32(m.turns);
  w.i32(m.trigger);
  w.u32(m.color);
  w.u8((uint8_t)((m.hasActivate ? 1 : 0) | (m.hasShot ? 2 : 0) | (m.hasEnd ? 4 : 0)));
}

inline bool getMaster(Reader& r, RawMaster& m) {
  m = RawMaster();
  m.id = r.str(kRawText);
  m.name = r.str(kRawText);
  m.description = r.str(kRawText);
  m.powerName = r.str(kRawText);
  m.powerDescription = r.str(kRawText);
  m.portrait = r.str(kRawText);
  m.banner = r.str(kRawText);
  m.turns = r.i32();
  m.trigger = r.i32();
  m.color = r.u32();
  const uint8_t f = r.u8();
  m.hasActivate = (f & 1) != 0;
  m.hasShot = (f & 2) != 0;
  m.hasEnd = (f & 4) != 0;
  return r.ok();
}

inline void putPhysics(Writer& w, uint32_t mask, const PhysicsParams& v) {
  w.u32(mask);
  for (int k = 0; k < PhysicsParams::KeyCount; k++)
    if ((mask >> k) & 1u) w.f32(v.at(k));
}

inline bool getPhysics(Reader& r, PhysicsEdit& e) {
  const uint32_t mask = r.u32();
  if (!r.ok() || (mask & ~kPhysicsAll) != 0) return false;
  for (int k = 0; k < PhysicsParams::KeyCount; k++)
    if ((mask >> k) & 1u) e.set(k, (double)r.f32());
  return r.ok();
}

}
