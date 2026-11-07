#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace pp {

struct VisualOptions {
  bool interpolate = false;
  bool smooth = false;
  bool bloom = false;
  bool sparks = false;
  bool ultraWideHud = false;
  float bloomStrength = 1.0f;
  bool any() const { return interpolate || smooth || bloom || sparks || ultraWideHud; }
};

enum WidescreenMode { WS_AUTO = 0, WS_16_9 = 1, WS_16_10 = 2, WS_21_9 = 3, WS_OFF = 4 };

inline const char* widescreenName(int m) {
  switch (m) {
    case WS_OFF: return "Off (classic 4:3)";
    case WS_AUTO: return "Auto (window shape)";
    case WS_16_9: return "16:9";
    case WS_16_10: return "16:10";
    case WS_21_9: return "21:9";
  }
  return "?";
}

inline int widescreenBoardWidth(int mode, float windowAspect) {
  float a = 0;
  switch (mode) {
    case WS_AUTO: a = std::min(windowAspect, 2.4f); break;
    case WS_16_9: a = 16.0f / 9.0f; break;
    case WS_16_10: a = 1.6f; break;
    case WS_21_9: a = 21.0f / 9.0f; break;
    default: return 650;
  }
  if (!(a > 0)) return 650;
  int canvas = (int)std::floor(600.0f * a + 1e-3f);
  int w = (canvas - 150) & ~1;
  return w < 700 ? 650 : std::min(w, 1300);
}

inline bool parseVisualOptions(const std::string& list, VisualOptions& v) {
  bool ok = true;
  size_t i = 0;
  while (i <= list.size()) {
    size_t j = list.find(',', i);
    if (j == std::string::npos) j = list.size();
    std::string t = list.substr(i, j - i);
    i = j + 1;
    if (t.empty()) continue;
    if (t == "interp" || t == "interpolate") v.interpolate = true;
    else if (t == "smooth") v.smooth = true;
    else if (t == "bloom") v.bloom = true;
    else if (t == "sparks") v.sparks = true;
    else if (t == "uwhud" || t == "ultrawidehud") v.ultraWideHud = true;
    else if (t == "all") v.interpolate = v.smooth = v.bloom = v.sparks = v.ultraWideHud = true;
    else if (t == "none") v = VisualOptions();
    else ok = false;
  }
  return ok;
}

inline int parseWidescreen(const std::string& s) {
  if (s == "off" || s == "classic" || s == "4:3") return WS_OFF;
  if (s == "auto") return WS_AUTO;
  if (s == "16:9") return WS_16_9;
  if (s == "16:10") return WS_16_10;
  if (s == "21:9") return WS_21_9;
  return -1;
}

}
