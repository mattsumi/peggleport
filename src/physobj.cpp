#include "physobj.h"
#include "physball.h"

#include <cmath>
#include <vector>

#include "dmath.h"

namespace pp {
namespace phys {

namespace {
constexpr float EPS = 0.01f;
constexpr float SLIDE_MIN = 0.2f;
constexpr float MARGIN = 20.0f;

inline float sq(float v) { return std::sqrt(v); }

inline V2 normalize(V2 v) {
  float mag = sq(v.x * v.x + v.y * v.y);
  if (mag == 0.0f) return v;
  return {v.x / mag, v.y / mag};
}
}

void Line::init(float ax, float ay, float bx, float by) {
  x1 = ax;
  x2 = bx;
  y1 = ay;
  y2 = by;
  dx = bx - ax;
  dy = by - ay;
  bx0 = ax;
  bx1 = bx;
  by0 = ay;
  by1 = by;
  if (bx < ax) { float t = bx0; bx0 = bx1; bx1 = t; }
  if (by1 < by0) { float t = by0; by0 = by1; by1 = t; }
  float ddy = y1 - y2;
  vertical = !(std::fabs(ddy) <= std::fabs(x1 - x2));
  V2 n = normalize({y1 - y2, x2 - x1});
  nx = n.x;
  ny = n.y;
  off = -(nx * x1 + ny * y1);
}

void Line::setPos(float x, float y) {
  x1 = x;
  y1 = y;
  float ex = dx + x;
  x2 = ex;
  float ey = dy + y;
  y2 = ey;
  off = -(nx * x + ny * y);
  bx0 = x;
  bx1 = ex;
  by0 = y;
  by1 = ey;
  if (ex < x) { float t = bx0; bx0 = bx1; bx1 = t; }
  if (by1 < by0) { float t = by0; by0 = by1; by1 = t; }
}

V2 Line::edgeHitVelocity(V2 n) const {
  float d = n.x * vx + n.y * vy;
  return {d * n.x, d * n.y};
}

int Line::circleIntersect(float cx, float cy, float r, V2* p1, V2* p2) const {
  float sx = x1 - cx, sy = y1 - cy;
  float ddx = x2 - x1, ddy = y2 - y1;
  float a = ddx * ddx + ddy * ddy;
  float b = sy * ddy + sx * ddx;
  b = b + b;
  float disc = b * b - a * 4.0f * ((sx * sx + sy * sy) - r * r);
  if (disc < 0.0f) return 0;
  float s = sq(disc);
  float t1 = (s - b) / (a + a);
  float t2 = (-b - s) / (a + a);
  sx = sx + cx;
  sy = sy + cy;
  int count = 0;
  if (0.0f <= t1 && t1 <= 1.0f) {
    if (p1) *p1 = {ddx * t1 + sx, ddy * t1 + sy};
    count = 1;
  }
  if (t2 < 0.0f) return count;
  if (1.0f < t2) return count;
  if (count == 0) {
    if (p1) {
      *p1 = {sx + ddx * t2, sy + ddy * t2};
      return 1;
    }
  } else if (p2) {
    *p2 = {sx + ddx * t2, sy + ddy * t2};
    return count + 1;
  }
  return count + 1;
}

bool Line::edgeTime(V2 pos, V2 vel, float r, V2& hitPt, V2& hitN, V2& hitV, float& hitT, bool& kiss) const {
  float tnx = nx, tny = ny, toff = off;
  if (!twoSided && tnx * (pos.x - x1) + tny * (pos.y - y1) < 0.0f) {
    tnx = tnx * -1.0f;
    tny = tny * -1.0f;
    toff = -1.0f * toff;
  }
  float speed = tny * vel.y + vel.x * tnx;
  if (SLIDE_MIN < speed) return false;
  if (speed == 0.0f) {
    if (!kiss) return false;
    if (tny != 0.0f) return false;
    float ex = pos.x - tnx * r, ey = pos.y - tny * r;
    bool onEdge = !vertical ? (bx0 <= ex && ex <= bx1) : (by0 <= ey && ey <= by1);
    if (onEdge) {
      if (r < std::fabs(pos.y * tny + pos.x * tnx + toff)) return false;
      hitT = 0.0f;
      hitPt = pos;
      hitN = {tnx, tny};
      hitV = {0.0f, 0.0f};
      kiss = false;
      return true;
    }
    return false;
  }
  float t = (((r - EPS) - toff) - (tnx * pos.x + pos.y * tny)) / speed;
  if (t < 0.0f || hitT <= t) return false;
  float cx = vel.x * t + pos.x;
  float cy = pos.y + t * vel.y;
  float ex = cx - tnx * r, ey = cy - tny * r;
  bool onEdge = !vertical ? (bx0 <= ex && ex <= bx1) : (by0 <= ey && ey <= by1);
  if (!onEdge) return false;
  hitT = t;
  hitPt = {cx + tnx * EPS, cy + EPS * tny};
  hitN = {tnx, tny};
  hitV = edgeHitVelocity(hitN);
  return true;
}

bool Line::vertexTime(V2 pos, V2 vel, float r, float& hitT, V2& hitPt, float cx, float cy, V2& hitN, V2& hitV) const {
  float dx_ = pos.x - cx, dy_ = pos.y - cy;
  float a = vel.x * vel.x;
  a = a + vel.y * vel.y;
  float b = dy_ * vel.y + dx_ * vel.x;
  b = b + b;
  float disc = b * b - a * 4.0f * ((dx_ * dx_ + dy_ * dy_) - (r - EPS) * (r - EPS));
  if (0.0f <= disc) {
    float s = sq(disc);
    float t = (-b - s) / (a + a);
    if (0.0f <= t && t < hitT) {
      float mag = sq(a);
      float t2 = t - EPS / mag;
      hitPt = {pos.x + t2 * vel.x, pos.y + t2 * vel.y};
      if (t2 <= 0.0f) t2 = 0.0f;
      hitT = t2;
      hitN = normalize({hitPt.x - cx, hitPt.y - cy});
      hitV = edgeHitVelocity(hitN);
      return true;
    }
  }
  return false;
}

bool Line::penetration(V2 pos, float r, V2& outPos, V2& outN, V2& outV) const {
  float rr = r - EPS;
  float plane = pos.x * nx + ny * pos.y + off;
  float absPlane = std::fabs(plane);
  if (rr < absPlane) return false;
  if (!vertical) {
    if (pos.x + r < bx0) return false;
    if (bx1 < pos.x - r) return false;
  } else {
    if (r + pos.y < by0) return false;
    if (by1 < pos.y - r) return false;
  }
  V2 hit;
  int n = circleIntersect(pos.x, pos.y, rr, &hit, nullptr);
  if (n == 0) return false;
  if (n != 1) {
    outN = {nx, ny};
    float p = plane;
    if (!twoSided && plane < 0.0f) {
      outN.x = outN.x * -1.0f;
      outN.y = outN.y * -1.0f;
      p = -1.0f * plane;
    }
    float d = r - p;
    float ox = d * outN.x, oy = d * outN.y;
    outPos = {pos.x + ox, oy + pos.y};
    outV = edgeHitVelocity(outN);
    return true;
  }
  float sx = x1 - pos.x, sy = y1 - pos.y, ex = x2 - pos.x, ey = y2 - pos.y;
  V2 push, vertex;
  if (ey * ey + ex * ex <= sy * sy + sx * sx) {
    push = {x2 - hit.x, y2 - hit.y};
    if (push.x == 0.0f && push.y == 0.0f) {
      V2 u = normalize({x2 - x1, y2 - y1});
      push = {u.x * EPS, EPS * u.y};
    }
    outPos = {pos.x + push.x, push.y + pos.y};
    vertex = {x2, y2};
  } else {
    push = {x1 - hit.x, y1 - hit.y};
    if (push.x == 0.0f && push.y == 0.0f) {
      V2 u = normalize({x1 - x2, y1 - y2});
      push = {u.x * EPS, EPS * u.y};
    }
    outPos = {pos.x + push.x, push.y + pos.y};
    vertex = {x1, y1};
  }
  outN = normalize({outPos.x - vertex.x, outPos.y - vertex.y});
  float kx = outN.x * EPS, ky = EPS * outN.y;
  outPos = {outPos.x + kx, ky + outPos.y};
  outV = edgeHitVelocity(outN);
  return true;
}

bool Line::check(V2 pos, V2 vel, float r, float& hitT, V2& hitPt, V2& hitN, V2& hitV, bool checkB, bool& kiss) const {
  if (bx0 - MARGIN <= r + pos.x && pos.x - r <= bx1 + MARGIN && by0 - MARGIN <= pos.y + r && pos.y - r <= by1 + MARGIN) {
    if (penetration(pos, r, hitPt, hitN, hitV)) {
      hitT = 0.0f;
      return true;
    }
    bool res = edgeTime(pos, vel, r, hitPt, hitN, hitV, hitT, kiss);
    bool v1 = vertexTime(pos, vel, r, hitT, hitPt, x1, y1, hitN, hitV);
    res = v1 || res;
    if (checkB && vertexTime(pos, vel, r, hitT, hitPt, x2, y2, hitN, hitV)) res = true;
    return res;
  }
  return false;
}


namespace {
constexpr float BIG = 999999995904.0f;
constexpr float DEPTH_INIT = 1.0e7f;
}

void Poly::calcBoundingBox() {
  bx0 = by0 = BIG;
  bx1 = by1 = -BIG;
  for (const Line& l : lines) {
    if (l.bx0 < bx0) bx0 = l.bx0;
    if (bx1 < l.bx1) bx1 = l.bx1;
    if (l.by0 < by0) by0 = l.by0;
    if (by1 < l.by1) by1 = l.by1;
  }
}

void Poly::checkConvex() {
  if (pts.size() <= 2) return;
  const V2& first = pts.front();
  const V2& last = pts.back();
  closed = first.x == last.x && first.y == last.y;
  if (!closed) {
    convex = false;
    return;
  }
  convex = true;
  float sign = 0.0f;
  float curx = first.x, cury = first.y;
  float prevx = first.x - pts[pts.size() - 2].x, prevy = first.y - pts[pts.size() - 2].y;
  for (size_t i = 1; i < pts.size(); i++) {
    float dx = pts[i].x - curx;
    float dy = pts[i].y - cury;
    float cross = dy * prevx - dx * prevy;
    if ((sign < 0.0f && cross > 0.0f) || (sign > 0.0f && cross < 0.0f)) {
      convex = false;
      return;
    }
    if (sign == 0.0f && cross != 0.0f) sign = cross;
    curx = pts[i].x;
    cury = pts[i].y;
    prevx = dx;
    prevy = dy;
  }
}

void Poly::initFromPoints() {
  size_t n = pts.size() ? pts.size() - 1 : 0;
  if (lines.size() != n) {
    lines.assign(n, Line());
    for (auto& l : lines) l.twoSided = normalDir != 0;
    starts.assign(n, V2());
  }
  if (rot == 0.0f) {
    for (size_t i = 0; i < n; i++) {
      lines[i].init(pts[i].x + cx, pts[i].y + cy, cx + pts[i + 1].x, pts[i + 1].y + cy);
      starts[i] = {lines[i].x1 - cx, lines[i].y1 - cy};
    }
    calcBoundingBox();
  } else {
    initRotatedFromPoints();
  }
  checkConvex();
}

void Poly::initRotatedFromPoints() {
  if (pts.size() <= 1) return;
  delayedRot = false;
  moved = false;
  double sd, cd;
  dm::sincos((double)rot, sd, cd);
  float c = (float)cd, s = (float)sd;
  float px = pts[0].x, py = pts[0].y;
  float prevY = py * c - s * px;
  float prevX = s * py + c * px;
  size_t n = pts.size() - 1;
  for (size_t i = 0; i < n; i++) {
    float x = pts[i + 1].x, y = pts[i + 1].y;
    float X = s * y + c * x;
    float Y = y * c - s * x;
    lines[i].init(prevX + cx, cy + prevY, X + cx, cy + Y);
    starts[i] = {lines[i].x1 - cx, lines[i].y1 - cy};
    prevY = Y;
    prevX = X;
  }
  calcBoundingBox();
}

void Poly::delayedInitRotatedFromPoints(float angle) {
  by0 = bx0 = BIG;
  by1 = bx1 = -BIG;
  double sd, cd;
  dm::sincos((double)angle, sd, cd);
  float c = (float)cd, s = (float)sd;
  for (const V2& p : pts) {
    float X = s * p.y + c * p.x + cx;
    float Y = (p.y * c - s * p.x) + cy;
    if (X < bx0) bx0 = X;
    if (bx1 < X) bx1 = X;
    if (Y < by0) by0 = Y;
    if (!(Y <= by1)) by1 = Y;
  }
  delayedRot = true;
}

void Poly::updateRotation(float moverRot) {
  if (rot == moverRot) {
    angVel = 0.0f;
    return;
  }
  constexpr float PIF = 3.14159274101257f, TWO_PIF = 6.28318548202515f;
  angVel = moverRot - rot;
  while (PIF < angVel) angVel = angVel - TWO_PIF;
  while (angVel < -PIF) angVel = angVel + TWO_PIF;
  prevRot = moverRot;
  rot = moverRot;
  delayedInitRotatedFromPoints(moverRot);
}

void Poly::actualSetPos(float x, float y) {
  float ddx = x - cx;
  moved = false;
  float ddy = y - cy;
  cx = x;
  cy = y;
  bx0 = ddx + bx0;
  bx1 = ddx + bx1;
  by0 = ddy + by0;
  by1 = ddy + by1;
  for (size_t i = 0; i < lines.size(); i++) lines[i].setPos(cx + starts[i].x, starts[i].y + cy);
}

void Poly::ensureLines() {
  if (delayedRot) initRotatedFromPoints();
  else if (moved) actualSetPos(cx, cy);
}

void Poly::setPos(float x, float y) {
  moved = true;
  float ddx = x - cx;
  float ddy = y - cy;
  cx = x;
  cy = y;
  bx0 += ddx;
  bx1 += ddx;
  by0 += ddy;
  by1 += ddy;
}

void Poly::setVelocity(float x, float y) {
  for (auto& l : lines) l.setVelocity(x, y);
}

bool Poly::check(V2 pos, V2 vel, float r, float& hitT, V2& hitPt, V2& hitN, V2& hitV, V2& angV, bool& kiss) {
  if (!(bx0 - MARGIN <= r + pos.x && pos.x - r <= bx1 + MARGIN && by0 - MARGIN <= pos.y + r && pos.y - r <= by1 + MARGIN)) return false;
  ensureLines();
  bool isConvex = convex;
  bool checkPen = normalDir == 1 && closed;
  float closest = DEPTH_INIT;
  const Line* closestLine = nullptr;
  const Line* hitLine = nullptr;
  unsigned crossings = 0;
  for (const Line& l : lines) {
    if (l.check(pos, vel, r, hitT, hitPt, hitN, hitV, false, kiss)) hitLine = &l;
    if (checkPen) {
      float ax = pos.x - l.x1, ay = pos.y - l.y1;
      float depth = -(l.nx * ax + l.ny * ay);
      bool candidate = isConvex;
      if (!isConvex) {
        float sy = l.y1, ey = l.y2;
        if ((sy != ey && sy <= pos.y && pos.y < ey) || (ey <= pos.y && pos.y < sy)) {
          float ix = ((pos.y - sy) * (l.x2 - l.x1)) / (ey - sy) + l.x1;
          if (pos.x < ix) crossings++;
        }
        candidate = depth > 0.0f;
      }
      if (candidate && depth < closest) {
        closest = depth;
        closestLine = &l;
      }
    }
  }
  if (isConvex && closest > 0.0f) crossings = 1;
  if ((crossings & 1) != 0 && closestLine && hitT != 0.0f) {
    hitT = 0.0f;
    float k = closest + r;
    float ox = k * closestLine->nx, oy = k * closestLine->ny;
    hitPt = {ox + pos.x, pos.y + oy};
    hitV = closestLine->edgeHitVelocity({closestLine->nx, closestLine->ny});
    hitN = {closestLine->nx, closestLine->ny};
    hitLine = closestLine;
  }
  if (hitLine) {
    if (angVel != 0.0f) {
      angV.x = (hitPt.y - cy) * angVel + angV.x;
      angV.y = angV.y - angVel * (hitPt.x - cx);
    }
    return true;
  }
  return false;
}

bool circleCheck(V2 c, float pegR, V2 pos, V2 vel, float r, float& hitT, V2& hitPt) {
  float R = (pegR - EPS) + r;
  R = R * R;
  float dx = pos.x - c.x, dy = pos.y - c.y;
  if (dy * dy + dx * dx < R) {
    hitT = 0.0f;
    hitPt = pos;
    return true;
  }
  float a = vel.x * vel.x + vel.y * vel.y;
  float b = dy * vel.y + vel.x * dx;
  b = b + b;
  float dot = pos.x * c.x + pos.y * c.y;
  float sq1 = pos.x * pos.x + pos.y * pos.y;
  float sq2 = c.x * c.x + c.y * c.y;
  float disc = b * b - a * 4.0f * (((sq1 + sq2) - (dot + dot)) - R);
  if (0.0f <= disc) {
    float s = sq(disc);
    float t = (-b - s) / (a + a);
    if (t < 0.0f) t = (s - b) / (a + a);
    if (0.0f <= t && t < hitT) {
      hitT = t;
      hitPt = {pos.x + vel.x * t, pos.y + t * vel.y};
      return true;
    }
  }
  return false;
}


void ballSetPos(Ball& b, float x, float y) {
  b.pos = {x, y};
  b.bx0 = x - b.r;
  b.bx1 = b.r + x;
  b.by0 = y - b.r;
  b.by1 = b.r + y;
}

void ballTranslate(Ball& b, float dx, float dy) {
  b.pos.x = dx + b.pos.x;
  b.pos.y = dy + b.pos.y;
  b.bx0 = b.bx0 + dx;
  b.bx1 = b.bx1 + dx;
  b.by0 = b.by0 + dy;
  b.by1 = dy + b.by1;
}

void ballSweptBounds(Ball& b) {
  float vx = b.vel.x, vy = b.vel.y;
  float minx = (vx < 0.0f) ? vx : 0.0f;
  float maxx = (vx > 0.0f) ? vx : 0.0f;
  float miny = (vy < 0.0f) ? vy : 0.0f;
  float maxy = (vy > 0.0f) ? vy : 0.0f;
  b.bx0 = (b.pos.x - b.r) + minx;
  b.bx1 = (b.r + b.pos.x) + maxx;
  b.by0 = (b.pos.y - b.r) + miny;
  b.by1 = (b.r + b.pos.y) + maxy;
}

bool ballUpdate(Ball& b, float gravity, float maxSpeed) {
  b.since++;
  b.sinceHit++;
  if (b.slideA != 0 && --b.slideA == 0) b.slideB = 0;
  b.stuck++;
  if (b.stuck % 10 == 0) {
    int n = b.stuck / 10;
    if (n < 1) n = 1;
    else if (n > 6) n = 6;
    b.hist[n - 1] = b.pos;
    bool wasSlow = b.slowFlag;
    b.slowFlag = false;
    for (int i = 0; i < n - 1; i++) {
      float dx = b.hist[i].x - b.pos.x;
      float dy = b.hist[i].y - b.pos.y;
      float d2 = dx * dx + dy * dy;
      if (!(d2 < 4.0f)) {
        b.stuck = 0;
        break;
      }
      if (!(d2 < 0.04000000283122063f) && !wasSlow) {
        b.stuck -= 5;
        wasSlow = true;
        b.slowFlag = true;
      }
    }
  }
  b.prev = b.pos;
  if (b.holeOut > 0) {
    b.stuck = 0;
    if (--b.holeOut != 0) return false;
    b.pos = b.holeTarget;
  }
  b.vel.y = b.vel.y + gravity;
  float vsq = b.vel.y * b.vel.y + b.vel.x * b.vel.x;
  if (vsq > maxSpeed * maxSpeed) {
    V2 n = sexyNormalize({b.vel.x, b.vel.y});
    b.vel = {n.x * maxSpeed, maxSpeed * n.y};
  }
  ballSweptBounds(b);
  b.hitFlag142 = false;
  b.hist[5] = b.pos;
  return true;
}

void ballSyncBounds(Ball& b) {
  b.bx0 = b.pos.x - b.r;
  b.bx1 = b.r + b.pos.x;
  b.by0 = b.pos.y - b.r;
  b.by1 = b.r + b.pos.y;
}

namespace {
constexpr float DEFAULT_PEG_BOUNCE = 0.9f;
constexpr float DEFAULT_PEG_SLIDE = 1.0f;
constexpr double PEG_SCALE_DIV = 999999995905.0;
constexpr double PEG_SCALE_MUL = 999999995904.0;
constexpr float LINE_BOUNCE = 0.82f;
constexpr float LINE_SLIDE = 1.4f;
constexpr float WALL_X = 0.99f;
constexpr float WALL_BOOST = 5.0f;
constexpr float TWO = 2.0f;
inline float modeBounce(const Ball& b, float bounce) {
  if (b.mode == 1) return (bounce + 1.0f) * 0.5f;
  if (b.mode == 2) return (bounce + 3.0f) * 0.25f;
  return bounce;
}
}

void doBallPegCollision(Ball& b, const Target& peg, std::vector<const Target*>* notify) {
  float dx = b.pos.x - peg.center.x;
  float dy = b.pos.y - peg.center.y;
  float distSq = dx * dx + dy * dy;
  float rThis = b.r;
  float rPeg = peg.radius;
  V2 n = normalize({peg.center.x - b.pos.x, peg.center.y - b.pos.y});
  float vx = b.vel.x, vy = b.vel.y;
  float ovx = peg.vel.x, ovy = peg.vel.y;
  b.lastN = {-n.x, -n.y};
  float bounce = peg.bounce;
  float slide = peg.slide;
  if (bounce == 0.0f) bounce = DEFAULT_PEG_BOUNCE;
  if (slide == 0.0f) slide = DEFAULT_PEG_SLIDE;
  bounce = modeBounce(b, bounce);
  b.sinceHit = 0;
  float imp = ((vx - ovx) * n.x + (vy - ovy) * n.y) * bounce;
  if (std::fabs(imp) <= slide) b.since = 0;
  else imp = imp + imp;
  b.impact = std::fabs(imp);
  if (!b.fire || !peg.hasPegInfo) {
    float k = (float)((double)(-(float)((double)imp / PEG_SCALE_DIV)) * PEG_SCALE_MUL);
    b.vel.x = k * n.x + vx;
    b.vel.y = vy + n.y * k;
    float over = (rPeg + rThis) - sq(distSq);
    ballTranslate(b, -(over * n.x), -(over * n.y));
    if (peg.stopReset) b.stuck = 0;
  }
  if (notify) notify->push_back(&peg);
}

void doLineCollision(Ball& b, V2& vel, const Target& obj, V2 n, V2 hitPt, V2 hitV, V2& angV, std::vector<const Target*>* notify) {
  b.lastN = n;
  if (b.fire && obj.hasPegInfo) {
    if (notify) notify->push_back(&obj);
    return;
  }
  ballSetPos(b, hitPt.x, hitPt.y);
  float rx = vel.x - hitV.x;
  float ry = vel.y - hitV.y;
  float impact = -(n.x * (rx - angV.x) + n.y * (ry - angV.y));
  if (impact < 0.0f) {
    b.impact = 0.0f;
    return;
  }
  b.impact = impact;
  float bounce = obj.bounce;
  float slide = obj.slide;
  if (bounce == 0.0f) bounce = LINE_BOUNCE;
  if (slide == 0.0f) slide = LINE_SLIDE;
  bounce = modeBounce(b, bounce);
  if (WALL_X < std::fabs(n.x)) impact = impact * WALL_BOOST;
  b.sinceHit = 0;
  if (impact <= slide) {
    b.since = 0;
    float d = vel.x * n.x + vel.y * n.y;
    float tx = vel.x - d * n.x;
    float ty = vel.y - d * n.y;
    float ry2 = 0.0f;
    float rx2;
    float rd = n.y * angV.y + n.x * angV.x;
    if (rd <= 0.0f) {
      rx2 = 0.0f;
    } else {
      rd = n.y * angV.y + n.x * angV.x;
      rx2 = rd * n.x;
      ry2 = rd * n.y;
    }
    angV = {rx2, ry2};
    b.vel.x = hitV.x + tx + angV.x;
    b.vel.y = hitV.y + ty + angV.y;
  } else {
    float dn = vel.y * n.y + vel.x * n.x;
    if (dn < 0.0f) {
      vel.x = (vel.x - dn * n.x * TWO) * bounce;
      vel.y = (vel.y - TWO * dn * n.y) * bounce;
    }
    float ax = (bounce + 1.0f) * (hitV.x + angV.x);
    float ay = (bounce + 1.0f) * (hitV.y + angV.y);
    if (0.0f < ax * n.x + ay * n.y) ballTranslate(b, ax, ay);
    vel.x = ax + vel.x;
    vel.y = ay + vel.y;
    b.vel = vel;
    float maxV = obj.maxVel;
    if (0.0f < maxV && maxV * maxV < b.vel.x * b.vel.x + b.vel.y * b.vel.y) {
      V2 u = normalize(b.vel);
      b.vel = {maxV * u.x, maxV * u.y};
    }
  }
  if (obj.stopReset) b.stuck = 0;
  if (notify) notify->push_back(&obj);
}

bool doCollideUpdate(Ball& b, std::vector<Target*>& cands, V2* firstHit, std::vector<const Target*>* notify) {
  float remaining = 1.0f;
  float hitT = 1.0f;
  bool didCollide = false;
  bool kiss = true;
  int iteration = 0;
  for (;;) {
    V2 pos = b.pos, vel = b.vel;
    V2 hitPt, hitN, hitV, angV;
    Target* hit = nullptr;
    if ((int)cands.empty()) break;
    for (size_t i = 0; i < cands.size(); i++) {
      Target* t = cands[i];
      bool ok = false;
      if (t->kind == 4) {
        ok = t->line->check(pos, vel, b.r, hitT, hitPt, hitN, hitV, true, kiss);
        if (ok) angV = {0.0f, 0.0f};
      } else if (t->kind == 5) {
        ok = t->poly->check(pos, vel, b.r, hitT, hitPt, hitN, hitV, angV, kiss);
      } else if (t->kind == 3) {
        ok = circleCheck(t->center, t->radius, pos, vel, b.r, hitT, hitPt);
      } else {
        continue;
      }
      if (ok) hit = t;
    }
    if (!hit) break;
    if (!b.fire || !hit->hasPegInfo) {
      b.hitFlag142 = true;
      b.hitFlag165 = true;
      didCollide = true;
      if (firstHit && iteration == 0) *firstHit = hitPt;
      if (hit->kind == 3) {
        ballSetPos(b, hitPt.x, hitPt.y);
        doBallPegCollision(b, *hit, notify);
      } else {
        doLineCollision(b, vel, *hit, hitN, hitPt, hitV, angV, notify);
      }
      b.hist[5] = b.pos;
      remaining = remaining - hitT;
      hitT = remaining;
    } else {
      if (notify) notify->push_back(hit);
      for (size_t i = 0; i < cands.size(); i++)
        if (cands[i] == hit) {
          cands.erase(cands.begin() + (long)i);
          break;
        }
    }
    iteration++;
    if (iteration > 19) return didCollide;
  }
  b.pos.x = b.pos.x + remaining * b.vel.x;
  b.pos.y = remaining * b.vel.y + b.pos.y;
  return didCollide;
}

void doBallBallCollision(Ball& t, Ball& o) {
  V2 n = sexyNormalize({o.pos.x - t.pos.x, o.pos.y - t.pos.y});
  float tvx = t.vel.x, tvy = t.vel.y, ovx = o.vel.x, ovy = o.vel.y;
  float dvx = tvx - ovx;
  float dvy = tvy - ovy;
  float imp = (dvx * n.x) + (n.y * dvy);
  imp = imp * 0.95f;
  imp = imp + imp;
  float a = std::fabs(imp);
  o.impact = a;
  t.impact = a;
  float h = imp * 0.5f;
  float nh = -h;
  float ax = n.x * nh, ay = n.y * nh;
  float bx = n.x * h, by = h * n.y;
  float tvx2 = tvx + ax, tvy2 = tvy + ay;
  float ovx2 = ovx + bx, ovy2 = by + ovy;
  o.vel = {ovx2, ovy2};
  t.vel = {tvx2, tvy2};
  float dx = t.pos.x - o.pos.x, dy = t.pos.y - o.pos.y;
  float rsum = o.r + t.r;
  float dx2 = dx * dx;
  float dy2 = dy * dy;
  float dist = std::sqrt(dx2 + dy2);
  float ov = rsum - dist;
  if (ov > 0.0f) {
    float px = n.x * ov;
    float py = ov * n.y;
    float x = px * 0.5f, y = 0.5f * py;
    ballTranslate(o, x, y);
    ballTranslate(t, -x, -y);
  }
}

bool checkBallCollision(Ball& t, Ball& o) {
  float fx = t.pos.x - o.pos.x;
  float fy = t.pos.y - o.pos.y;
  float rs = (o.r + t.r) - 0.01f;
  float fx2 = fx * fx;
  float fy2 = fy * fy;
  float d2 = fx2 + fy2;
  float rs2 = rs * rs;
  if (!(rs2 >= d2)) return false;
  doBallBallCollision(t, o);
  return true;
}

bool holeContains(float bx, float by, float cx, float cy, float hw, float hh, bool circular) {
  if (bx < cx - hw || bx > cx + hw || by < cy - hh || by > cy + hh) return false;
  if (!circular) return true;
  float dx = bx - cx, dy = by - cy;
  return !(dx * dx / (hw * hw) + dy * dy / (hh * hh) > 1.0f);
}

}
}
