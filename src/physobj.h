#pragma once
#include <cmath>
#include <vector>

#include "vec2.h"

namespace pp {
namespace phys {

inline V2 sexyNormalize(V2 v) {
  float mag = std::sqrt(v.x * v.x + v.y * v.y);
  if (mag == 0.0f) return v;
  return {v.x / mag, v.y / mag};
}

struct Line {
  float dx = 0, dy = 0;
  float x1 = 0, x2 = 0;
  float y1 = 0, y2 = 0;
  float vx = 0, vy = 0;
  float nx = 0, ny = 0, off = 0;
  bool vertical = false;
  bool twoSided = false;
  float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;

  void init(float ax, float ay, float bx, float by);
  void setPos(float x, float y);
  void setVelocity(float x, float y) { vx = x; vy = y; }
  V2 edgeHitVelocity(V2 n) const;
  int circleIntersect(float cx, float cy, float r, V2* p1, V2* p2) const;
  bool edgeTime(V2 pos, V2 vel, float r, V2& hitPt, V2& hitN, V2& hitV, float& hitT, bool& kiss) const;
  bool vertexTime(V2 pos, V2 vel, float r, float& hitT, V2& hitPt, float cx, float cy, V2& hitN, V2& hitV) const;
  bool penetration(V2 pos, float r, V2& outPos, V2& outN, V2& outV) const;
  bool check(V2 pos, V2 vel, float r, float& hitT, V2& hitPt, V2& hitN, V2& hitV, bool checkB, bool& kiss, bool classic = false) const;
};


struct Poly {
  std::vector<V2> pts;
  std::vector<V2> starts;
  std::vector<Line> lines;
  float cx = 0, cy = 0;
  float rot = 0;
  float angVel = 0;
  float scale = 1;
  float prevRot = 0;
  bool convex = true;
  bool closed = true;
  int normalDir = 0;
  bool delayedRot = false;
  bool moved = false;
  float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;

  void calcBoundingBox();
  void checkConvex();
  void initFromPoints();
  void initRotatedFromPoints();
  void delayedInitRotatedFromPoints(float angle);
  void actualSetPos(float x, float y);
  void ensureLines();
  void updateRotation(float moverRot);
  void setPos(float x, float y);
  void setVelocity(float x, float y);
  bool check(V2 pos, V2 vel, float r, float& hitT, V2& hitPt, V2& hitN, V2& hitV, V2& angV, bool& kiss, bool classic = false);
};

bool circleCheck(V2 center, float pegR, V2 pos, V2 vel, float r, float& hitT, V2& hitPt);


bool holeContains(float bx, float by, float cx, float cy, float hw, float hh, bool circular);

}
}
