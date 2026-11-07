#include "gun.h"

#include <cmath>
#include <cstdint>

#include "dmath.h"

namespace pp {
namespace gun {

namespace {
constexpr float PIF = 3.14159274101257f;
constexpr float TWO_PIF = 6.28318548202515f;
constexpr float HALF_PIF = 1.57079637050629f;

inline float cosf_(float a) { double s, c; dm::sincos((double)a, s, c); return (float)c; }
inline float sinf_(float a) { double s, c; dm::sincos((double)a, s, c); return (float)s; }

float angleDelta(float p1, float p2) {
  while (p2 > TWO_PIF) p2 = p2 - TWO_PIF;
  while (p1 > p2) p1 = p1 - TWO_PIF;
  float d = p2 - p1;
  if (d > PIF) d = -(TWO_PIF - d);
  return d;
}
inline int sign(float v) { return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0); }
}

float normalizeAngle(float a) {
  if (a >= 0.0f && a < TWO_PIF) return a;
  if (a < 0.0f) return TWO_PIF - std::fmod(-a, TWO_PIF);
  return std::fmod(a, TWO_PIF);
}

V2 rotateXY(V2 p, V2 c, float angle) {
  float dx = p.x - c.x;
  float dy = p.y - c.y;
  float co = cosf_(angle);
  float si = sinf_(angle);
  float x = (c.x + co * dx) + si * dy;
  float y = (c.y + dy * co) - si * dx;
  return {x, y};
}

float clampAim(float a, float maxGunAngle) {
  a = normalizeAngle(a);
  float lo = (float)(90.0 - (double)maxGunAngle);
  float hi = (float)((double)maxGunAngle + 90.0);
  lo = (lo * PIF) / 180.0f;
  hi = (hi * PIF) / 180.0f;
  if (lo < a && a < hi) {
    float mid = (hi + lo) * 0.5f;
    a = (a <= mid) ? lo : hi;
  }
  return a;
}

float aimAngle(V2 gunPos, int mx, int my, float maxGunAngle) {
  const float gx = gunPos.x, gy = gunPos.y;
  const float fmx = (float)mx, fmy = (float)my;
  float ddx = fmx - gx;
  float up = gy - fmy;
  float base = (float)dm::atan2((double)up, (double)ddx);
  float ddy = fmy - gy;
  float a2 = std::fabs((float)dm::atan2((double)ddx, (double)ddy));
  const float k = 1.3000000080864993e-06f;
  float sx = ddx * ddx;
  float sy = ddy * ddy;
  float d2 = sy + sx;
  float corr = (k * d2) * a2;
  if (1.0f < corr) corr = 1.0f;
  if (ddx < 0.0f) corr = corr * -1.0f;
  const float A = base + corr;
  float best = A, bestErr = 1000000.0f;

  const float p3x = ((TIP_OFFSET + gx) + PRE_OFFSET) + DEF_RADIUS;
  const float p3y = gy + 1.0f;
  float dxm = fmx - p3x;
  float dym = fmy - p3y;
  float dist = std::sqrt(dxm * dxm + dym * dym);
  if (((TIP_OFFSET + PRE_OFFSET) + DEF_RADIUS) + 10.0f < dist) {
    const float g = 0.05f;
    for (int i = -10; i < 10; i++) {
      float ang = (float)i * 0.01f + A;
      V2 r = rotateXY({p3x, p3y}, gunPos, ang);
      float ex = fmx - r.x;
      float ey = fmy - r.y;
      float c = cosf_(ang);
      float err;
      if (std::fabs(c) < 0.001f) {
        err = (ex == 0.0f) ? 0.0f : 1000000.0f;
      } else {
        float t = ex / (c * SPEED);
        if (t < 0.0f) {
          err = 1000000.0f;
        } else {
          float t2 = t * t;
          float h = t2 * (g * 0.5f);
          float s = sinf_(ang);
          float y = h - s * (t * SPEED);
          err = std::fabs(y - ey);
        }
      }
      if (err < bestErr) {
        best = ang;
        bestErr = err;
      }
    }
  }
  return clampAim(best, maxGunAngle);
}

V2 launchPoint(V2 gunPos, float angle, float ballRadius) {
  float tip = TIP_OFFSET + ballRadius;
  float px = tip + gunPos.x;
  float py = 1.0f + gunPos.y;
  px = px + PRE_OFFSET;
  return rotateXY({px, py}, gunPos, angle);
}

V2 launchVelocity(float angle, float speed) {
  return {speed * cosf_(angle), -speed * sinf_(angle)};
}

float angularVelocity(State& s) {
  if (s.history.empty()) return 0.0f;
  while (!s.history.empty() && s.updateCount - s.history.front().second > 20) s.history.erase(s.history.begin());
  if (s.history.empty()) return 0.0f;
  float total = 0.0f;
  for (auto& h : s.history) total += h.first;
  int elapsed = s.updateCount - s.history.front().second;
  if (elapsed <= 0) elapsed = 1;
  return total / (float)elapsed;
}

void updateCommon(State& s, float angle) {
  if (angle == s.lastTracked) {
    s.stableTicks++;
    if (s.stableTicks >= 30) {
      s.settledAngle = angle;
      s.settledUpdate = s.updateCount;
    }
  } else {
    s.stableTicks = 0;
    s.lastTracked = angle;
  }
}

bool setAngle(State& s, float& angle, float& target, float newAngle, bool immediate) {
  if (newAngle == angle) return false;
  if (immediate) {
    s.history.clear();
    angle = newAngle;
    target = newAngle;
    s.settledUpdate = s.updateCount - 1000;
    return true;
  }
  if (newAngle == target) return true;
  float delta = angleDelta(angle, newAngle);
  if (!s.history.empty() && sign(s.history.back().first) == sign(delta)) s.history.clear();
  s.history.push_back({delta, s.updateCount});
  target = newAngle;
  return true;
}

void update(State& s, float& angle, float& target) {
  s.updateCount++;
  s.angVel = angularVelocity(s);
  updateCommon(s, angle);
  if (target != angle) {
    float aT = target, aC = angle;
    if (aT < HALF_PIF) aT = aT + TWO_PIF;
    if (aC < HALF_PIF) aC = aC + TWO_PIF;
    const float step = TURN_STEP;
    if (aT <= aC) {
      angle = angle - step;
      if (aC - step <= aT) angle = target = normalizeAngle(target);
    } else {
      angle = angle + step;
      if (aT <= aC + step) angle = target = normalizeAngle(target);
    }
  }
}

float fireAngle(const State& s, float angle, bool useSettled) {
  if (useSettled && (uint32_t)(s.updateCount - s.settledUpdate) < 10u) return s.settledAngle;
  return angle;
}

}
}
