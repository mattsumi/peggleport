#pragma once
#include <utility>
#include <vector>

#include "vec2.h"

namespace pp {
namespace gun {

constexpr float SPEED = 4.0f;
constexpr float TIP_OFFSET = 26.0f;
constexpr float PRE_OFFSET = 60.0f;
constexpr float DEF_RADIUS = 6.0f;
constexpr float TURN_STEP = 0.2f;
constexpr float MAX_GUN_ANGLE = 83.0f;

struct State {
  float lastTracked = 0;
  float settledAngle = 0;
  int stableTicks = 0;
  int settledUpdate = 0;
  int updateCount = 0;
  float angVel = 0;
  std::vector<std::pair<float, int>> history;
};

float normalizeAngle(float a);
V2 rotateXY(V2 p, V2 c, float angle);
float clampAim(float a, float maxGunAngle = MAX_GUN_ANGLE);
float aimAngle(V2 gunPos, int mx, int my, float maxGunAngle = MAX_GUN_ANGLE);
V2 launchPoint(V2 gunPos, float angle, float ballRadius = DEF_RADIUS);
V2 launchVelocity(float angle, float speed = SPEED);

float angularVelocity(State& s);
void updateCommon(State& s, float angle);
bool setAngle(State& s, float& angle, float& target, float newAngle, bool immediate);
void update(State& s, float& angle, float& target);
float fireAngle(const State& s, float angle, bool useSettled);

}
}
