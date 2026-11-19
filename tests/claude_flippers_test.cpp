#include "content.h"
#include "physball.h"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace pp;

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int checks = 0, failures = 0;
  for (int edition = 0; edition < 2; ++edition) for (int width : {650, 917, 1400}) {
    Vfs v;
    LevelBundle lb;
    std::string err;
    if (!v.mountGame(argv[1 + edition], &err) || !loadLevelBundle(v, edition ? "Bjorn1" : "Level1", lb, &err)) return 2;
    lb.cfg.nights = edition != 0;
    lb.cfg.boardWidth = width;
    lb.cfg.master[0] = Board::P_FLIPPERS;
    Board start;
    if (!start.load(lb.base, lb.level, lb.cfg, &err) || !start.fire(4.71f)) return 2;
    auto check = [&](bool yes, const char* which) {
      ++checks;
      if (!yes) { ++failures; std::cerr << (edition ? "Nights " : "Deluxe ") << width << ": " << which << '\n'; }
    };
    std::vector<int> claws;
    int green = -1;
    for (int i = 0; i < (int)start.objs.size(); ++i) {
      const auto& o = start.objs[i];
      if (o.id == "flipper") claws.push_back(i);
      if (green < 0 && o.visible && o.pegType == 4 && o.shape == Shape::Circle) green = i;
    }
    if (claws.size() != 2 || green < 0) return 2;
    auto drop = [](Board& board, float x, float y) {
      auto& ball = board.balls.front();
      phys::ballSetPos(ball, x, y);
      ball.vel = {0, 5};
    };
    auto hit = [](const Board& board, int object) {
      for (auto [bi, oi] : board.contactNotes()) if (bi == 0 && oi == object) return true;
      return false;
    };
    for (int i : claws) check(!start.objs[i].visible && !start.objs[i].collidable, "inactive claw");
    const auto& g = start.objs[green];
    drop(start, g.pos.x, g.pos.y - g.radius - start.balls[0].r - 1);
    start.step();
    check(hit(start, green) && start.turns[0] == 3, "green contact activates three flipper turns");
    if (start.turns[0] != 3) return 1;
    auto steps = [](Board& b, int ticks) {
      const int target = b.tick + ticks;
      for (int n = 0; n < 2000 && b.tick < target; ++n) b.step();
    };
    for (int i : claws) {
      const auto& claw = start.objs[i];
      const bool left = claw.pos.x < width * .5f;
      check(claw.visible && claw.collidable && (left ? claw.pos.x < 5 : claw.pos.x > width - 5), "active polygon at physical wall");
      for (bool raised : {false, true}) {
        Board probe = start;
        if (raised) { probe.gunClick(); steps(probe, claw.mv.time / 2); }
        const auto& o = probe.objs[i];
        const float x = left ? 55.f : width - 55.f;
        drop(probe, x, o.y0 - 15);
        bool contact = false;
        for (int t = 0; t < 25 && !probe.balls.empty(); ++t) {
          probe.step();
          if (hit(probe, i)) { contact = probe.balls[0].flipperTouched && probe.balls[0].vel.y < 0; break; }
        }
        check(contact, raised ? "raised polygon bounces descending ball" : "rest polygon bounces descending ball");
      }
    }
    const int period = start.objs[claws.front()].mv.time;
    Board tap = start;
    tap.gunClick(); tap.gunRelease();
    steps(tap, period / 2);
    for (int i : claws) check(std::abs(tap.objs[i].rot - start.objs[i].rot) > .5f, "short tap still strokes");
    steps(tap, period / 2 + 1);
    for (int i : claws) check(!tap.flipHeld && !tap.objs[i].canMove && std::abs(tap.objs[i].rot - start.objs[i].rot) < .001f, "short tap returns to rest");
    Board held = start;
    held.gunClick(); steps(held, period / 2 + 1);
    held.gunRelease(); held.gunClick(); steps(held, period / 2 + 1);
    for (int i : claws) check(held.flipHeld && !held.objs[i].canMove && std::abs(held.objs[i].rot - start.objs[i].rot) > .5f, "re-press at apex holds raised");
    held.gunRelease(); steps(held, period + 1);
    for (int i : claws) check(!held.flipHeld && std::abs(held.objs[i].rot - start.objs[i].rot) < .001f, "release returns to rest");
    if (width > 650) {
      Board zones;
      if (!zones.load(lb.base, lb.level, lb.cfg, &err) || !zones.fire(4.71f)) return 2;
      std::vector<int> pegs;
      for (int i = 0; i < (int)zones.objs.size() && pegs.size() < 5; ++i)
        if (zones.objs[i].visible && zones.objs[i].pegType == 1) pegs.push_back(i);
      if (pegs.size() != 5) return 2;
      for (size_t n = 0; n < pegs.size(); ++n) {
        const float side = n < 2 ? 55.f : (width - 650) * .5f + 55.f;
        zones.objs[pegs[n]].pos = {n == 4 ? width * .5f : n % 2 == 0 ? side : width - side, 490};
      }
      zones.pegHitBy(0, green);
      for (size_t n = 0; n < pegs.size(); ++n)
        check(zones.objs[pegs[n]].visible == (n >= 2) && zones.objs[pegs[n]].collidable == (n >= 2), "clear only the actual wall-side flipper zones");
    }
  }
  std::cout << "CLAUDE_FLIPPERS checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
