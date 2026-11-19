#include "content.h"
#include "physball.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace pp;

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const std::string& name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  for (int game = 0; game < 2; ++game) {
    Vfs v;
    LevelBundle lb;
    std::string err;
    const std::string name = game == 0 ? "DOUBLEHELIX" : "HU5";
    if (!v.mountGame(argv[game + 1], &err) || !loadLevelBundle(v, name, lb, &err)) {
      std::cerr << err << '\n';
      return 2;
    }
    lb.cfg.nights = game != 0;
    lb.cfg.greens = lb.cfg.purple = false;
    lb.cfg.seed = 4357;
    Board a;
    if (!a.load(lb.base, lb.level, lb.cfg, &err)) return 2;
    a.emit = false;
    a.fire(game == 0 ? 5.0f : 4.7f);
    Board b = a;
    const int tick = game == 0 ? 765 : 38;
    const int first = game == 0 ? 150 : 57;
    const int second = game == 0 ? 2 : 58;
    bool deterministic = true, finite = true;
    while (a.tick < tick && a.phase == Phase::Shot) {
      a.step();
      b.step();
      deterministic &= a.hash() == b.hash();
      for (const auto& ball : a.balls) finite &= std::isfinite(ball.pos.x) && std::isfinite(ball.pos.y) && std::isfinite(ball.vel.x) && std::isfinite(ball.vel.y);
    }
    bool firstSeen = false, secondSeen = false;
    for (auto [bi, oi] : a.contactNotes()) {
      if (oi == first) firstSeen = true;
      if (oi == second && firstSeen) secondSeen = true;
    }
    check(a.tick == tick && firstSeen && secondSeen && a.contactNotes().size() == 2, name + " same-tick contact after bounce without duplicate notifications");
    bool separated = false;
    if (!a.balls.empty()) {
      const auto& ball = a.balls[0];
      const auto& obj = a.objs[second];
      if (game == 0) separated = ball.pos.x + ball.r <= obj.x0 + 0.01f;
      else separated = (ball.pos - obj.pos).len2() >= (ball.r + obj.radius - 0.01f) * (ball.r + obj.radius - 0.01f);
    }
    check(separated, name + " no residual penetration of second collider");
    check(deterministic && finite, name + " finite deterministic replay");
    std::cout << "REPLAY " << name << " tick=" << a.tick << " contacts=";
    for (auto [bi, oi] : a.contactNotes()) std::cout << oi << ',';
    if (!a.balls.empty()) std::cout << " position=" << a.balls[0].pos.x << ',' << a.balls[0].pos.y << " velocity=" << a.balls[0].vel.x << ',' << a.balls[0].vel.y;
    std::cout << '\n';
  }
  {
    Vfs v;
    LevelBundle lb;
    std::string err;
    if (!v.mountGame(argv[1], &err) || !loadLevelBundle(v, "SPACEBALLS", lb, &err)) return 2;
    lb.cfg.seed = 4357;
    lb.cfg.greens = lb.cfg.purple = false;
    Board a;
    if (!a.load(lb.base, lb.level, lb.cfg, &err)) return 2;
    a.emit = false;
    a.fire(3.8f);
    while (a.tick < 9) a.step();
    Board isolated = a;
    for (size_t i = 0; i < isolated.objs.size(); ++i) if (i != 33) isolated.objs[i].collidable = false;
    a.step();
    isolated.step();
    check(a.contactNotes().size() == 1 && a.contactNotes()[0].second == 33 && isolated.contactNotes() == a.contactNotes(), "SPACEBALLS seed=4357 angle=3.8 tick=10 only brick33 contacted");
    bool same = a.balls.size() == 1 && isolated.balls.size() == 1;
    if (same) {
      const auto& x = a.balls[0];
      const auto& y = isolated.balls[0];
      same = x.pos.x == y.pos.x && x.pos.y == y.pos.y && x.vel.x == y.vel.x && x.vel.y == y.vel.y;
    }
    check(same, "SPACEBALLS uncontacted rotating bricks cannot change brick33 bounce");
  }
  std::cout << "COLLISION_REGRESSION checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
