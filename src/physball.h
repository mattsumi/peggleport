#pragma once
#include <vector>

#include "physobj.h"
#include "sim.h"

namespace pp {
namespace phys {

struct Target {
  int kind = 0;
  V2 center;
  float radius = 0;
  V2 vel;
  Line* line = nullptr;
  Poly* poly = nullptr;
  float bounce = 0, slide = 0, maxVel = 0;
  bool stopReset = false;
  bool hasPegInfo = false;
  bool scaled = false;
  float bounceScale = 1.0f;
  int user = -1;
};

void ballSetPos(Ball& b, float x, float y);
void ballTranslate(Ball& b, float dx, float dy);
void ballSyncBounds(Ball& b);
bool ballUpdate(Ball& b, float gravity, float maxSpeed, const PhysicsParams* extra = nullptr);
void ballSweptBounds(Ball& b);
void doBallPegCollision(Ball& b, const Target& peg, std::vector<const Target*>* notify);
void doLineCollision(Ball& b, V2& vel, const Target& obj, V2 n, V2 hitPt, V2 hitV, V2& angV, std::vector<const Target*>* notify);
bool doCollideUpdate(Ball& b, std::vector<Target*>& cands, V2* firstHit, std::vector<const Target*>* notify);

bool checkBallCollision(Ball& self, Ball& other);
void doBallBallCollision(Ball& self, Ball& other);

}
}
