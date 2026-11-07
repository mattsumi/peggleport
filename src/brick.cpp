#include "brick.h"
#include <vector>
#include <cmath>

#include "dmath.h"

namespace pp {

namespace {
constexpr float PIF = 3.14159274101257f;
constexpr float EPS = 0.01f;

inline float C(float a) { double s, c; dm::sincos((double)a, s, c); return (float)c; }
inline float S(float a) { double s, c; dm::sincos((double)a, s, c); return (float)s; }
inline float rad(float deg) { return (deg * PIF) / 180.0f; }
}

BrickShape brickInitFromParams(const BrickParams& p) {
  BrickShape out;
  const int type = p.type;
  bool inner = !(type == 1 || type == 2);
  bool outer = !(type == 3 || type == 4);
  float r = p.radius;
  if (type == 5) {
    if (600.0f < r) r = 600.0f;
    inner = outer = false;
  }
  out.radius = r;
  const int n = p.sub <= 0 ? 0 : p.sub;
  const int count = 5 + (inner ? n : 0) + (outer ? n : 0);
  const float thick = p.thick;
  const float R = thick + r;
  const float nr = -r, nR = -R;
  const float dir = rad(p.rot);
  const float h = p.ext * 0.5f;
  const float P = rad(p.rot - h);
  const float Q = rad(p.rot + h);
  std::vector<V2>& pts = out.pts;
  pts.assign((size_t)count, V2());
  V2 a1, a2;
  V2 midIn, midOut;
  int i = 0;

  auto snap = [&]() {
    if (EPS <= std::fabs(a1.x - a2.x)) {
      if (std::fabs(a1.y - a2.y) < EPS) a2.y = a1.y;
    } else {
      a2.x = a1.x;
    }
  };

  if (inner) {
    out.innerStart = 0;
    pts[0] = {C(Q) * r, S(Q) * nr};
    i = 1;
    for (int k = 0; k < n; k++) {
      float t = (float)(k + 1) / (float)(n + 1);
      float ang = ((1.0f - t) * Q) + (t * P);
      pts[(size_t)i++] = {C(ang) * r, S(ang) * nr};
    }
    out.innerEnd = i;
    pts[(size_t)i++] = {C(P) * r, S(P) * nr};
    const V2& pa = pts[(size_t)((n + 2) / 2)];
    const V2& pb = pts[(size_t)((n + 1) / 2)];
    midIn = {(pa.x + pb.x) * 0.5f, (pb.y + pa.y) * 0.5f};
  } else {
    if (type == 2) {
      float cd = C(dir), nsd = -S(dir);
      float A = cd * thick, B = thick * nsd;
      V2 q1{C(P) * R, S(P) * nR};
      a1 = {q1.x - A, q1.y - B};
      V2 q2{C(Q) * R, S(Q) * nR};
      a2 = {q2.x - A, q2.y - B};
    } else if (type == 5) {
      float c = C(dir), s = S(dir);
      float cr = c * r;
      float nsr = nr * s;
      float hs = (s * r) * 0.5f;
      float hc = 0.5f * (c * r);
      a1 = {hs + cr, nsr + hc};
      a2 = {cr - hs, nsr - hc};
    } else {
      a1 = {C(P) * r, S(P) * nr};
      a2 = {C(Q) * r, S(Q) * nr};
    }
    snap();
    out.innerStart = 0;
    pts[0] = a2;
    out.innerEnd = 1;
    pts[1] = a1;
    i = 2;
    midIn = {(a2.x + a1.x) * 0.5f, 0.5f * (a2.y + a1.y)};
  }

  if (outer) {
    const int i0 = i;
    out.outerStart = i0;
    pts[(size_t)i++] = {C(P) * R, S(P) * nR};
    for (int k = 0; k < n; k++) {
      float t = (float)(k + 1) / (float)(n + 1);
      float ang = ((1.0f - t) * P) + (t * Q);
      pts[(size_t)i++] = {C(ang) * R, S(ang) * nR};
    }
    out.outerEnd = i;
    pts[(size_t)i++] = {C(Q) * R, S(Q) * nR};
    const V2& pa = pts[(size_t)((n + 2) / 2 + i0)];
    const V2& pb = pts[(size_t)((n + 1) / 2 + i0)];
    midOut = {(pa.x + pb.x) * 0.5f, (pb.y + pa.y) * 0.5f};
  } else {
    if (type == 4) {
      float cd = C(dir), nsd = -S(dir);
      float A = cd * thick, B = thick * nsd;
      V2 q1{C(P) * r, S(P) * nr};
      a1 = {q1.x + A, q1.y + B};
      V2 q2{C(Q) * r, S(Q) * nr};
      a2 = {q2.x + A, q2.y + B};
    } else if (type == 5) {
      float e = rad(p.ang150);
      float Pp = e + dir;
      float e2 = rad(p.ang154);
      float Qp = dir + e2;
      float nthick = -thick;
      a1 = {C(Pp) * thick + a1.x, S(Pp) * nthick + a1.y};
      a2 = {C(Qp) * thick + a2.x, S(Qp) * nthick + a2.y};
    } else {
      a1 = {C(P) * R, S(P) * nR};
      a2 = {C(Q) * R, S(Q) * nR};
    }
    snap();
    out.outerStart = i;
    pts[(size_t)i++] = a1;
    out.outerEnd = i;
    pts[(size_t)i++] = a2;
    midOut = {(a2.x + a1.x) * 0.5f, (a2.y + a1.y) * 0.5f};
  }

  pts[(size_t)i] = pts[0];

  float mx = midOut.x + midIn.x;
  float my = midOut.y + midIn.y;
  float cx = mx * 0.5f;
  float cy = 0.5f * my;
  float ox = p.off.x + cx;
  float oy = p.off.y + cy;
  for (V2& q : pts) {
    q.x = q.x - ox;
    q.y = q.y - oy;
  }
  return out;
}

}
