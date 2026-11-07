#pragma once
#include <vector>

#include "vec2.h"

namespace pp {

struct BrickParams {
  int type = 0;
  int sub = 2;
  float radius = 35;
  float ext = 30;
  float ang150 = 0;
  float ang154 = 0;
  float thick = 20;
  float rot = -90;
  V2 off;
};

struct BrickShape {
  std::vector<V2> pts;
  int innerEnd = 0, outerStart = 0;
  int innerStart = 0, outerEnd = 0;
  float radius = 0;
};

BrickShape brickInitFromParams(const BrickParams& p);

}
