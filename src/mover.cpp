#include <cmath>
#include <cstdint>

#include "dmath.h"
#include "sim.h"

namespace pp {

namespace {
constexpr double TAU_W = 6.2831854820251465;
constexpr double HALFPI_W = 1.5707963705062866;
constexpr float PI_F = 3.1415927410125732f;

inline float fsin(float a) { return (float)dm::sin((double)a); }
inline float fcos(float a) { return (float)dm::cos((double)a); }
inline float fatan2(float y, float x) { return (float)dm::atan2((double)y, (double)x); }

template <class R>
inline int ftol(R v) {
  return v >= R(-2147483648.0) && v < R(2147483648.0) ? (int)v : -2147483647 - 1;
}

template <class R>
float movePos(const Mover& m, int tick) {
  int t = tick + m.offset;
  if (m.pause1 > 0) {
    int total = m.pause1 + m.time + m.pause2;
    if (total > 0) {
      int A = (int)((int64_t)m.time * m.phase1 / 100);
      int B = (int)((int64_t)(m.phase2 - m.phase1) * m.time / 100);
      int bb = B + (m.pause1 + A);
      int e = (ftol<R>(R(m.postDelay) * R(total)) + t) % total;
      if (e <= A) t = e;
      else if (e <= m.pause1 + A) t = A;
      else if (e <= bb) t = e - m.pause1;
      else if (e <= bb + m.pause2) t = bb - m.pause1;
      else t = e - m.pause2 - m.pause1;
    }
  }
  int s = ftol<R>(R(m.phase) * R(m.time) + R(t));
  int e = s % m.time;
  return (float)(R(e) / R(m.time));
}

template <class R>
void rotateXY(float& x, float& y, float cx, float cy, float ang) {
  float dx = (float)(R(x) - R(cx));
  float dy = (float)(R(y) - R(cy));
  float c = fcos(ang), s = fsin(ang);
  float nx = (float)((R(c) * R(dx) + R(cx)) + R(s) * R(dy));
  float ny = (float)((R(dy) * R(c) + R(cy)) - R(s) * R(dx));
  x = nx;
  y = ny;
}

template <class R>
void calcT(const Mover& m, int tick, float frac, float& X, float& Y, float& ROT) {
  if (m.type == 0 || m.time < 1) {
    X = m.ax;
    Y = m.ay;
    ROT = m.rotation;
    return;
  }
  const int at = m.type < 0 ? -m.type : m.type;
  const int dir = m.type >= 0 ? 1 : -1;
  const float P = (float)(R(frac) / R(m.time) + R(movePos<R>(m, tick)));
  const float rx = m.rx;
  const float ry = m.ry != 0 ? m.ry : m.rx;
  const float ax = m.ax, ay = m.ay;
  auto angle = [&] { return (float)(R(R(P) * R(TAU_W)) * R(dir)); };
  auto yMinus = [&](float s) { return (float)(R(ay) - R(s) * R(ry)); };
  auto xPlus = [&](float c) { return (float)(R(c) * R(rx) + R(ax)); };
  const float M = m.maxAngle == 0.0f ? PI_F : m.maxAngle;
  switch (at) {
    case 1: {
      float a = angle();
      X = ax;
      Y = yMinus(fsin(a));
      ROT = m.rotation;
      break;
    }
    case 2: {
      float a = angle();
      X = xPlus(fcos(a));
      Y = ay;
      ROT = m.rotation;
      break;
    }
    case 3: {
      float a = angle();
      X = xPlus(fcos(a));
      Y = yMinus(fsin(a));
      ROT = m.rotation;
      break;
    }
    case 4: {
      float a = angle();
      X = xPlus(fcos(a));
      float a2 = (float)(R(a) + R(a));
      float s = fsin(a2);
      Y = (float)(R(ay) - R(R(s) * R(ry)) * R(0.5));
      ROT = m.rotation;
      break;
    }
    case 5: {
      float a = angle();
      float a2 = (float)(R(a) + R(a));
      X = xPlus(fsin(a2));
      Y = yMinus(fsin(a));
      ROT = m.rotation;
      break;
    }
    case 6:
    case 7: {
      float t0 = (float)(R(P) * R(TAU_W) - R(HALFPI_W));
      float s1 = fsin(t0);
      float T = (float)((R(s1) + R(1.0)) * R(M) * R(0.5));
      if (at == 6) {
        X = xPlus(fcos(T));
        Y = yMinus(fsin(T));
      } else {
        X = xPlus(fsin(T));
        Y = yMinus(fcos(T));
      }
      ROT = m.rotation;
      break;
    }
    case 8:
      if (m.maxAngle == 0.0f) ROT = (float)(R(R(R(P) * R(TAU_W)) * R(dir)) + R(m.rotation));
      else ROT = (float)(R(dir) * R(m.maxAngle) + R(m.rotation));
      X = ax;
      Y = ay;
      break;
    case 9: {
      float a = (float)(R(P) * R(TAU_W));
      float s = fsin(a);
      float t = (float)((R(s) * R(M) + R(m.moveRot)) * R(dir));
      ROT = (float)(R(t) + R(m.rotation));
      X = ax;
      Y = ay;
      break;
    }
    case 11: {
      int i = ftol<R>(R(P));
      float f = (float)(R(P) - R(i));
      X = ax;
      Y = (float)(R(ay) - R(R(R(f) * R(ry)) * R(dir)));
      ROT = m.rotation;
      break;
    }
    case 12: {
      int i = ftol<R>(R(P));
      float f = (float)(R(P) - R(i));
      X = (float)(R(R(rx) * R(f)) * R(dir) + R(ax));
      Y = ay;
      ROT = m.rotation;
      break;
    }
    case 13: {
      float a = angle();
      float c = fcos(a);
      X = xPlus(c);
      float s = fsin(a);
      Y = yMinus(s);
      if (m.ry == 0 || m.rx == 0) {
        ROT = (float)(R(m.rotation) + R(a));
      } else {
        float a2 = (float)(R(a) + R(HALFPI_W));
        float s2 = fsin(a2);
        float yy = (float)(R(ay) - R(s2) * R(ry));
        float v = (float)(R(ay) - R(yy));
        float c2 = fcos(a2);
        float xx = (float)(R(c2) * R(rx) + R(ax));
        float dx = (float)(R(xx) - R(ax));
        ROT = (float)(R(fatan2(v, dx)) + R(m.rotation));
      }
      break;
    }
    case 14: {
      const float M14 = m.maxAngle == 0.0f ? PI_F : m.maxAngle;
      const float pos = movePos<R>(m, tick);
      const float arg = (float)((R(pos) - R(m.phase)) * R(TAU_W));
      const float s = fsin(arg);
      const float dirf = (float)dir;
      const float B = (float)(R(dirf) * (R(m.phase) * R(TAU_W)));
      const float a = (float)(R(dirf) * (R(s) * R(M14) + R(m.moveRot)) + R(B));
      X = xPlus(fcos(a));
      Y = yMinus(fsin(a));
      if (m.ry == 0 || m.rx == 0) {
        ROT = (float)(R(m.rotation) + R(a));
      } else {
        float a2 = (float)(R(a) + R(HALFPI_W));
        float s2 = fsin(a2);
        float yy = (float)(R(ay) - R(s2) * R(ry));
        float v = (float)(R(ay) - R(yy));
        float c2 = fcos(a2);
        float xx = (float)(R(c2) * R(rx) + R(ax));
        float dx = (float)(R(xx) - R(ax));
        ROT = (float)(R(fatan2(v, dx)) + R(m.rotation));
      }
      break;
    }
    case 15: {
      const float a = (float)(R(P) * R(TAU_W) * R(dir));
      const float a4 = (float)(R(4.0) * R(a));
      const float a3 = (float)(R(a) * R(3.0));
      const double K08 = 0.800000011920929, K02 = 0.20000000298023224, K17 = 0.1772499978542328;
      const double K1 = -0.0;
      const R t1 = R(fcos(a4)) * (R(rx) * R(K08)) + R(ax);
      X = (float)(R(fcos(a3)) * (R(rx) * R(K02)) + t1);
      const R u = R(ay) - R(fsin(a3)) * (R(ry) * R(K08));
      const float a2 = (float)(R(a) + R(a));
      const R v = R(fcos(a2)) * (R(ry) * R(K1)) + u;
      Y = (float)(v - R(fsin(a4)) * (R(ry) * R(K17)));
      break;
    }
    default: break;
  }
  if (m.moveRot != 0.0f) rotateXY<R>(X, Y, ax, ay, m.moveRot);
}
}

Mover Mover::fromNode(const Node& mn, bool single) {
  Mover m;
  m.type = (int)mn.i("move_type");
  m.ax = mn.f("anchor_x");
  m.ay = mn.f("anchor_y");
  m.time = (int)mn.i("time", 200);
  m.offset = (int)mn.i("offset");
  m.rx = (float)mn.i("radius");
  m.ry = (float)mn.i("radius2");
  m.phase = mn.f("phase");
  m.moveRot = mn.f("move_rotation");
  m.maxAngle = mn.f("max_angle");
  m.rotation = mn.f("rotation");
  m.pause1 = (int)mn.i("pause1");
  m.pause2 = (int)mn.i("pause2");
  m.phase1 = (int)mn.i("phase1");
  m.phase2 = (int)mn.i("phase2");
  m.postDelay = mn.f("post_delay_phase");
  m.single = single;
  m.lastX = mn.f("sub_x");
  m.lastY = mn.f("sub_y");
  m.outX = mn.has("obj_x") ? mn.f("obj_x") : m.ax;
  m.outY = mn.has("obj_y") ? mn.f("obj_y") : m.ay;
  return m;
}

void Mover::translate(float dx, float dy) {
  outX = dx + outX;
  outY = dy + outY;
  ax = dx + ax;
  ay = dy + ay;
}

void Mover::follow(const Mover& s) {
  const float dx = s.outX - lastX;
  const float dy = s.outY - lastY;
  translate(dx, dy);
  lastX = s.outX;
  lastY = s.outY;
}

void Mover::step(int tick, float frac, float& x, float& y, float& rot, const Mover* s) {
  if (s) follow(*s);
  x = outX;
  y = outY;
  calc(tick, frac, x, y, rot);
  outX = x;
  outY = y;
}

void Mover::calc(int tick, float& x, float& y, float& rot) const { calc(tick, 0.0f, x, y, rot); }

void Mover::calc(int tick, float frac, float& x, float& y, float& rot) const {
  if (single) calcT<float>(*this, tick, frac, x, y, rot);
  else calcT<double>(*this, tick, frac, x, y, rot);
}

}
