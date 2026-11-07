#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace pp {

struct PhysicsParams {
  enum Key { GravityY, GravityX, LaunchSpeed, BallBounce, PegBounce, Drag, MaxSpeed, BallRadius, KeyCount };

  float gravityY = 0.05f;
  float gravityX = 0.0f;
  float launchSpeed = 4.0f;
  float ballBounce = 1.0f;
  float pegBounce = 1.0f;
  float drag = 0.0f;
  float maxSpeed = 15.0f;
  float ballRadius = 6.0f;

  float& at(int k) {
    switch (k) {
      case GravityY: return gravityY;
      case GravityX: return gravityX;
      case LaunchSpeed: return launchSpeed;
      case BallBounce: return ballBounce;
      case PegBounce: return pegBounce;
      case Drag: return drag;
      case MaxSpeed: return maxSpeed;
      default: return ballRadius;
    }
  }
  float at(int k) const { return const_cast<PhysicsParams*>(this)->at(k); }

  bool operator==(const PhysicsParams& o) const {
    return gravityY == o.gravityY && gravityX == o.gravityX && launchSpeed == o.launchSpeed && ballBounce == o.ballBounce && pegBounce == o.pegBounce &&
           drag == o.drag && maxSpeed == o.maxSpeed && ballRadius == o.ballRadius;
  }
  bool operator!=(const PhysicsParams& o) const { return !(*this == o); }
  bool isDefault() const { return *this == PhysicsParams(); }
  bool windOrDrag() const { return gravityX != 0.0f || drag != 0.0f; }
  bool bounceChanged() const { return ballBounce != 1.0f || pegBounce != 1.0f; }

  void sanitize();
};

struct PhysicsKeyInfo {
  const char* name;
  const char* alias;
  float lo, hi, def;
};

inline const PhysicsKeyInfo& physicsKey(int k) {
  static const PhysicsKeyInfo t[PhysicsParams::KeyCount] = {
      {"gravity_y", "gravity", -0.5f, 0.5f, 0.05f},      {"gravity_x", "wind", -0.2f, 0.2f, 0.0f},           {"launch_speed", "launch_speed", 0.5f, 40.0f, 4.0f},
      {"ball_bounce", "bounce", 0.0f, 2.0f, 1.0f},       {"peg_bounce", "peg_bounce", 0.0f, 2.0f, 1.0f},     {"drag", "drag", 0.0f, 0.5f, 0.0f},
      {"max_speed", "max_speed", 1.0f, 40.0f, 15.0f},    {"ball_radius", "ball_radius", 2.0f, 20.0f, 6.0f}};
  return t[k < 0 ? 0 : (k >= PhysicsParams::KeyCount ? PhysicsParams::KeyCount - 1 : k)];
}

inline float physicsClamp(int k, double v) {
  const PhysicsKeyInfo& i = physicsKey(k);
  if (!(v == v)) return i.def;
  if (v < (double)i.lo) return i.lo;
  if (v > (double)i.hi) return i.hi;
  return (float)v;
}

inline bool physicsKeyByName(const std::string& name, int& k) {
  std::string n;
  for (char c : name) n.push_back((char)std::tolower((unsigned char)c));
  for (int i = 0; i < PhysicsParams::KeyCount; i++)
    if (n == physicsKey(i).name || n == physicsKey(i).alias) {
      k = i;
      return true;
    }
  return false;
}

inline void PhysicsParams::sanitize() {
  for (int k = 0; k < KeyCount; k++) at(k) = physicsClamp(k, (double)at(k));
}

struct PhysicsEdit {
  uint32_t mask = 0;
  PhysicsParams v;

  bool any() const { return mask != 0; }
  bool has(int k) const { return ((mask >> k) & 1u) != 0; }
  void set(int k, double x) {
    mask |= 1u << k;
    v.at(k) = physicsClamp(k, x);
  }
  void clear(int k) {
    mask &= ~(1u << k);
    v.at(k) = PhysicsParams().at(k);
  }
  void applyTo(PhysicsParams& p) const {
    for (int k = 0; k < PhysicsParams::KeyCount; k++)
      if (has(k)) p.at(k) = v.at(k);
  }
  void merge(const PhysicsEdit& o) {
    o.applyTo(v);
    mask |= o.mask;
  }
  bool operator==(const PhysicsEdit& o) const { return mask == o.mask && v == o.v; }
  bool operator!=(const PhysicsEdit& o) const { return !(*this == o); }
};

inline std::string physicsFormat(float v) {
  char buf[40];
  for (int digits = 1; digits <= 9; digits++) {
    std::snprintf(buf, sizeof buf, "%.*g", digits, (double)v);
    if (std::strtof(buf, nullptr) == v) break;
  }
  return buf;
}

inline bool physicsParseNumber(const std::string& text, double& out) {
  const char* s = text.c_str();
  char* end = nullptr;
  double d = std::strtod(s, &end);
  if (end == s) return false;
  while (*end == ' ' || *end == '\t' || *end == '\r') end++;
  if (*end) return false;
  if (!std::isfinite(d)) return false;
  out = d;
  return true;
}

}
