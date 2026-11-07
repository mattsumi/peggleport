#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pp {

constexpr int kMaxOrangePegs = 25;
constexpr int kPegHitGap = 8;

struct PegTypeDef {
  std::string mod, id, name, image, litImage;
  std::vector<std::string> stages;
  uint32_t color = 0xffffff;
  bool hasColor = false;
  int base = 1;
  int score = 0;
  int orange = -1;
  int hits = 1;
  bool clears = true;
  float bounce = 1.0f;
  float radiusScale = 1.0f;
  bool onHit = false, onClear = false;
  std::string key() const { return mod + ":" + id; }
};

struct PegTypeTable {
  std::vector<PegTypeDef> defs;
  int find(const std::string& key) const {
    const size_t colon = key.find(':');
    for (size_t i = 0; i < defs.size(); i++) {
      if (colon != std::string::npos ? (defs[i].mod == key.substr(0, colon) && defs[i].id == key.substr(colon + 1)) : defs[i].id == key) return (int)i + 1;
    }
    return 0;
  }
};

inline const PegTypeTable* g_pegTypes = nullptr;

inline const PegTypeDef* pegTypeDef(int custom) {
  if (custom <= 0 || !g_pegTypes || custom > (int)g_pegTypes->defs.size()) return nullptr;
  return &g_pegTypes->defs[(size_t)custom - 1];
}

inline int findPegType(const std::string& key) { return g_pegTypes ? g_pegTypes->find(key) : 0; }

}
