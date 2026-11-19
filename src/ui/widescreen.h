#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace pp::ui {

enum class WidescreenMode : int { Auto = 0, W16x9 = 1, W16x10 = 2, W21x9 = 3, Classic4x3 = 4 };
constexpr int kWidescreenModes = 5;
constexpr int kMaxCanvasWidth = 1440;

inline const char* widescreenName(WidescreenMode m) {
  switch (m) {
    case WidescreenMode::Auto: return "Auto";
    case WidescreenMode::W16x9: return "16:9";
    case WidescreenMode::W16x10: return "16:10";
    case WidescreenMode::W21x9: return "21:9";
    default: return "Classic 4:3";
  }
}

inline bool parseWidescreen(const char* s, WidescreenMode& out) {
  std::string v = s ? s : "";
  for (auto& c : v) c = (char)std::tolower((unsigned char)c);
  if (v == "auto" || v == "0") out = WidescreenMode::Auto;
  else if (v == "16:9" || v == "16x9" || v == "1") out = WidescreenMode::W16x9;
  else if (v == "16:10" || v == "16x10" || v == "2") out = WidescreenMode::W16x10;
  else if (v == "21:9" || v == "21x9" || v == "3") out = WidescreenMode::W21x9;
  else if (v == "classic" || v == "4:3" || v == "4x3" || v == "4") out = WidescreenMode::Classic4x3;
  else return false;
  return true;
}

inline int widescreenCanvasWidth(WidescreenMode m, int outW, int outH) {
  int w = 800;
  switch (m) {
    case WidescreenMode::Auto: return outH > 0 && 5LL * outW >= 8LL * outH ? 1067 : 800;
    case WidescreenMode::W16x9: w = 1067; break;
    case WidescreenMode::W16x10: w = 960; break;
    case WidescreenMode::W21x9: w = 1400; break;
    case WidescreenMode::Classic4x3: w = 800; break;
  }
  w = std::clamp(w, 800, kMaxCanvasWidth);
  if (w < 808) w = 800;
  return w;
}
inline int widescreenFrameWidth(int boardCanvas, int outW, int outH, int cap, bool stretchHud) {
  if (outW <= 0 || outH <= 0) return boardCanvas;
  const double win = 600.0 * outW / outH;
  const double w = stretchHud ? win : std::min(win, (double)cap);
  if (w <= boardCanvas + 4.0) return boardCanvas;
  return boardCanvas + 2 * (int)std::floor((w - boardCanvas) * 0.5);
}

}
