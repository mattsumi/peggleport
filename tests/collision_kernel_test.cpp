#include "physball.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace pp;

int main() {
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const char* name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  for (float nearAngularVelocity : {0.0f, 0.01f}) {
    phys::Poly near, far;
    near.pts = far.pts = {{-50, 0}, {-50, 10}, {50, 10}, {50, 0}, {-50, 0}};
    near.cx = far.cx = 150;
    near.cy = 200;
    far.cy = 202;
    near.normalDir = far.normalDir = 1;
    near.initFromPoints();
    far.initFromPoints();
    near.angVel = nearAngularVelocity;
    far.angVel = 0.02f;
    phys::Target a, b;
    a.kind = b.kind = 5;
    a.poly = &near;
    b.poly = &far;
    std::vector<Ball> results;
    for (int order = 0; order < 3; ++order) {
      Ball ball;
      phys::ballSetPos(ball, 140, 190);
      ball.vel = {0, 15};
      std::vector<phys::Target*> candidates = order == 0 ? std::vector<phys::Target*>{&a} : order == 1 ? std::vector<phys::Target*>{&b, &a} : std::vector<phys::Target*>{&a, &b};
      std::vector<const phys::Target*> contacts;
      phys::doCollideUpdate(ball, candidates, nullptr, &contacts);
      check(contacts.size() == 1 && contacts.front() == &a, "only nearest polygon contacted");
      results.push_back(ball);
    }
    for (size_t i = 1; i < results.size(); ++i) {
      const auto& a = results[0];
      const auto& b = results[i];
      check(a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.vel.x == b.vel.x && a.vel.y == b.vel.y, "uncontacted rotating polygon cannot change bounce");
    }
  }
  for (int shape = 0; shape < 2; ++shape) for (int side : {-1, 1}) for (float speed : {15.0f, 40.0f}) {
    phys::Line line;
    line.init(100, 200, 200, 200);
    phys::Poly poly;
    poly.pts = {{100, 200}, {200, 200}};
    poly.normalDir = 0;
    poly.initFromPoints();
    const V2 pos{150, 200 + side * speed * 0.75f};
    const V2 velocity{0, -side * speed};
    float time = 1;
    V2 point, normal, hitVelocity, angularVelocity;
    bool kiss = true;
    bool hit = shape == 0 ? line.check(pos, velocity, 6, time, point, normal, hitVelocity, true, kiss) : poly.check(pos, velocity, 6, time, point, normal, hitVelocity, angularVelocity, kiss);
    check(hit && time >= 0 && time < 1 && std::fabs(point.y - (200 + side * 6)) < 0.001f && normal.y * side > 0, "line and polygon swept contact at supported maximum speed");
  }
  {
    phys::Poly poly;
    poly.pts = {{100, 200}, {100, 240}, {200, 240}, {200, 200}, {100, 200}};
    poly.normalDir = 1;
    poly.initFromPoints();
    for (const auto& line : poly.lines) check(line.twoSided, "polygon outward normal mode retained");
    for (int side : {-1, 1}) {
      float time = 1;
      V2 point, normal, velocity, angular;
      bool kiss = true;
      bool hit = poly.check({150, side < 0 ? 190.0f : 250.0f}, {0, -side * 15.0f}, 6, time, point, normal, velocity, angular, kiss);
      check(hit && normal.y * side > 0 && std::fabs(point.y - (side < 0 ? 194.0f : 246.0f)) < 0.001f, "closed polygon outward surface contact retained");
    }
    float time = 1;
    V2 point, normal, velocity, angular;
    bool kiss = true;
    bool hit = poly.check({150, 220}, {}, 6, time, point, normal, velocity, angular, kiss);
    check(hit && time == 0 && (point.y <= 194 || point.y >= 246), "closed polygon interior depenetration retained");
  }
  std::cout << "COLLISION_KERNEL checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
