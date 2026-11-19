#include "content.h"
#include "physball.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace pp;

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  Vfs v;
  LevelBundle lb;
  std::string err;
  if (!v.mountGame(argv[1], &err) || !loadLevelBundle(v, "Level1", lb, &err)) {
    std::cerr << err << '\n';
    return 2;
  }
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const std::string& name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  for (int width : {650, 1000, 1400}) {
    lb.cfg.boardWidth = width;
    Board original;
    if (!original.load(lb.base, lb.level, lb.cfg, &err)) return 2;
    original.emit = false;
    original.fire(5.125f);
    int lines = 0;
    for (int i = 0; i < (int)original.objs.size(); ++i) {
      const Obj& o = original.objs[i];
      if (o.fromBase || o.shape != Shape::Line || !o.collidable) continue;
      ++lines;
      const auto& l = o.pline;
      for (int side : {-1, 1}) for (float distance : {3.0f, 18.0f}) {
        Board b = original;
        Ball ball;
        V2 n{side * l.nx, side * l.ny};
        phys::ballSetPos(ball, (l.x1 + l.x2) * 0.5f + n.x * distance, (l.y1 + l.y2) * 0.5f + n.y * distance);
        ball.vel = {-n.x * 15, -n.y * 15 - b.physics.gravityY};
        b.balls[0] = ball;
        b.step();
        bool contact = false;
        for (auto [bi, oi] : b.contactNotes()) contact |= oi == i;
        bool bounced = !b.balls.empty() && b.balls[0].vel.x * n.x + b.balls[0].vel.y * n.y > 0;
        check(contact && bounced, "branch width=" + std::to_string(width) + " object=" + std::to_string(i) + " side=" + std::to_string(side) + " distance=" + std::to_string(distance));
      }
    }
    check(lines == 6, "six shipped branch colliders width=" + std::to_string(width));
    Board a = original, b = original;
    bool deterministic = true, finite = true, rightContact = false;
    for (int frame = 0; frame < 1000; ++frame) {
      a.step();
      b.step();
      deterministic &= a.hash() == b.hash();
      for (const auto& ball : a.balls) finite &= std::isfinite(ball.pos.x) && std::isfinite(ball.pos.y) && std::isfinite(ball.vel.x) && std::isfinite(ball.vel.y);
      if (a.tick == 487) for (auto [bi, oi] : a.contactNotes()) rightContact |= oi == 127;
    }
    check(deterministic && finite, "finite deterministic replay width=" + std::to_string(width));
    if (width == 650) check(rightContact, "default LEVEL1 seed=4357 angle=5.125 right branch contact tick=487");
  }
  std::cout << "SIDE_COLLISION checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
