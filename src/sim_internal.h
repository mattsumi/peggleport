#pragma once
#include <cmath>

#include "dmath.h"
#include "sim.h"

namespace pp {
namespace detail {
inline constexpr float G = 0.05f;
inline constexpr float MAXV = 15.0f;
inline constexpr float BALL_R = 6.0f;
inline constexpr float GUN_SPEED = 4.0f;
inline constexpr float GUN_TURN = 0.2f;
inline constexpr float EPS = 0.01f;
inline constexpr float MARGIN = 20.0f;
inline constexpr double PI_D = 3.14159265358979323846;
inline constexpr float PI_F = 3.14159265f;
inline constexpr float H = 560.0f;

inline float len(V2 v) { return std::sqrt(v.len2()); }
inline V2 normalize(V2 v) {
  float l = len(v);
  return l > 0 ? v * (1.0f / l) : V2();
}
inline V2 rotate(V2 p, float a) {
  double s, c;
  dm::sincos(a, s, c);
  float cs = (float)c, sn = (float)s;
  return {p.x * cs + p.y * sn, p.y * cs - p.x * sn};
}
inline V2 dirOf(float a) {
  double s, c;
  dm::sincos(a, s, c);
  return {(float)c, -(float)s};
}

}
}
