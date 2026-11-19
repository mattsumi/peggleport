#include "sim.h"

#include <functional>
#include <iostream>

#define private public
#include "aiscore.h"
#undef private
#include "../src/aiscore.cpp"

using namespace pp;

static Obj circle(V2 pos, int pegType) {
  Obj o;
  o.shape = Shape::Circle;
  o.pos = pos;
  o.radius = 10;
  o.pegType = pegType;
  o.collidable = o.visible = true;
  o.x0 = pos.x - o.radius;
  o.x1 = pos.x + o.radius;
  o.y0 = pos.y - o.radius;
  o.y1 = pos.y + o.radius;
  return o;
}

static AIMgr::Run fixture(bool second, int power = 3, bool fire = false, int type = 4, bool seen = true, float x = 80, bool extra = false) {
  AIMgr::Run r;
  r.power = power;
  r.w.grav_ = 0;
  r.w.physics.maxSpeed = 15;
  r.w.emit = false;
  r.w.noModHooks = true;
  r.w.objs = {circle({116, 105}, 0), circle({x, 90}, type)};
  r.w.objs[1].collidable = second;
  if (extra) r.w.objs.push_back(circle({400, 400}, 0));
  AIMgr::Run::Cand c;
  c.seen.assign(r.w.objs.size(), 0);
  c.seen[1] = seen;
  c.fireball = fire;
  c.left = 10;
  r.cands.push_back(c);
  r.owner.push_back(0);
  Ball b;
  phys::ballSetPos(b, 100, 100);
  b.prev = b.pos;
  b.vel = {15, 0};
  b.fire = fire;
  r.w.balls.push_back(b);
  return r;
}

static bool sameBall(const Ball& a, const Ball& b) {
  return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.vel.x == b.vel.x && a.vel.y == b.vel.y && a.since == b.since && a.sinceHit == b.sinceHit && a.impact == b.impact;
}

static bool finite(const Board& b) {
  for (const auto& ball : b.balls)
    if (!std::isfinite(ball.pos.x) || !std::isfinite(ball.pos.y) || !std::isfinite(ball.vel.x) || !std::isfinite(ball.vel.y)) return false;
  return true;
}

int main() {
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const std::string& name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  auto r = fixture(true);
  auto control = fixture(false);
  check(!r.collides(r.cands[0], r.w.objs[1], 1), "real multiball filter excludes already-seen green");
  r.stepFrame();
  control.stepFrame();
  const auto notes = r.w.contactNotes();
  check(notes == control.w.contactNotes(), "multiball retry preserves eligible contact notifications");
  check(notes == std::vector<std::pair<int, int>>{{0, 0}}, "multiball retry contacts only bumper");
  check(r.cands[0].nColl == 1 && r.cands[0].nPeg == 0, "excluded green cannot affect AI collision counters");
  const auto& a = r.w.balls[0];
  const auto& b = control.w.balls[0];
  check(a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.vel.x == b.vel.x && a.vel.y == b.vel.y, "excluded green cannot change AI bounce");
  std::cout << "MULTIBALL contacts=";
  for (auto [bi, oi] : notes) std::cout << oi << ',';
  std::cout << " position=" << a.pos.x << ',' << a.pos.y << " velocity=" << a.vel.x << ',' << a.vel.y << '\n';
  for (int type : {1, 2, 3, 4}) for (float x : {80.0f, 90.0f}) for (bool extra : {false, true}) {
    auto fire = fixture(true, 12, true, type, true, x, extra);
    auto absent = fixture(false, 12, true, type, true, x, extra);
    const std::string name = "fireball type=" + std::to_string(type) + " x=" + std::to_string((int)x) + " extra=" + std::to_string(extra);
    check(!fire.collides(fire.cands[0], fire.w.objs[1], 1), name + " real filter excludes seen peg");
    fire.stepFrame();
    absent.stepFrame();
    check(fire.w.contactNotes() == std::vector<std::pair<int, int>>{{0, 0}}, name + " excluded peg is never notified");
    check(fire.cands[0].nColl == 1 && fire.cands[0].nPeg == 0 && fire.cands[0].score == 0, name + " excluded peg cannot change AI scoring counters");
    check(sameBall(fire.w.balls[0], absent.w.balls[0]) && finite(fire.w), name + " excluded peg cannot change collision state");
  }
  for (float x : {80.0f, 90.0f}) {
    auto multi = fixture(true, 3, false, 4, true, x, true);
    auto absent = fixture(false, 3, false, 4, true, x, true);
    multi.stepFrame();
    absent.stepFrame();
    check(multi.w.contactNotes() == absent.w.contactNotes() && sameBall(multi.w.balls[0], absent.w.balls[0]), "multiball filter survives recursive eligible expansion");
  }
  for (bool fire : {false, true}) for (bool seen : {false, true}) {
    auto eligible = fixture(true, fire ? 12 : 3, fire, seen ? 0 : 1, seen);
    auto repeat = eligible;
    phys::ballSweptBounds(eligible.w.balls[0]);
    check(eligible.w.balls[0].bx0 > eligible.w.objs[1].x1, "eligible second collider starts outside spatial shortlist");
    eligible.stepFrame();
    repeat.stepFrame();
    check(eligible.w.contactNotes() == std::vector<std::pair<int, int>>{{0, 0}, {0, 1}}, "eligible post-bounce contact retained without duplicate notifications");
    check(sameBall(eligible.w.balls[0], repeat.w.balls[0]) && finite(eligible.w), "eligible AI bounce remains finite and deterministic");
    check(eligible.cands[0].nColl == 2 && eligible.cands[0].nPeg == (seen ? 0 : 1), "eligible contact reaches AI counters exactly once");
  }
  {
    auto fresh = fixture(true, 3, false, 4, false);
    fresh.stepFrame();
    check(fresh.w.contactNotes() == std::vector<std::pair<int, int>>{{0, 0}, {0, 1}}, "unseen green remains eligible after bounce");
    check(fresh.cands.size() == 2 && fresh.w.balls.size() == 2 && fresh.cands[0].seen[1] && fresh.cands[1].seen[1], "eligible green triggers real multiball notification exactly once");
    check(finite(fresh.w), "multiball clone remains finite");
  }
  {
    auto ordinary = fixture(true, 0);
    Board physical = ordinary.w;
    physical.phase = Phase::Shot;
    physical.objs[1].pegType = 0;
    ordinary.stepFrame();
    physical.stepCore();
    check(ordinary.w.contactNotes() == std::vector<std::pair<int, int>>{{0, 0}, {0, 1}}, "ordinary AI still collides with already-seen pegs");
    check(physical.contactNotes() == ordinary.w.contactNotes() && sameBall(physical.balls[0], ordinary.w.balls[0]), "normal physics retains both post-bounce contacts");
  }
  {
    Board guide;
    guide.grav_ = 0;
    guide.gunSpeed_ = guide.physics.maxSpeed = 15;
    const V2 p = guide.muzzle(0);
    guide.objs = {circle({p.x + 16, p.y + 5}, 0), circle({p.x - 20, p.y - 10}, 0)};
    auto expected = fixture(true, 0, false, 0);
    expected.w = guide;
    Ball ball;
    phys::ballSetPos(ball, p.x, p.y);
    ball.vel = {15, 0};
    expected.w.balls.push_back(ball);
    expected.stepFrame();
    const auto points = guide.superGuide(0, 1);
    const auto trace = guide.guideTrace(0, false);
    check(expected.w.contactNotes() == std::vector<std::pair<int, int>>{{0, 0}, {0, 1}}, "guide fixture has two same-frame contacts");
    check(points.size() == 1 && points[0].x == expected.w.balls[0].pos.x && points[0].y == expected.w.balls[0].pos.y, "super guide retains post-bounce refresh");
    check(trace.hitStep == 0 && !trace.pts.empty() && trace.pts[0].x == points[0].x && trace.pts[0].y == points[0].y, "aim trace retains post-bounce refresh");
    guide.objs[1].collidable = false;
    const auto single = guide.guideTrace(0, false);
    check(trace.hit.x == single.hit.x && trace.hit.y == single.hit.y, "retry preserves first-hit position for aim");
  }
  {
    auto level = std::make_shared<Level>();
    for (int i = 0; i < 9; ++i) {
      auto node = std::make_shared<Node>();
      node->type = 5;
      node->flags["visible"] = node->flags["collidable"] = true;
      node->setF("x", 265.0f + (i % 3) * 60.0f);
      node->setF("y", 150.0f + (i / 3) * 50.0f);
      node->setF("radius", 10);
      auto peg = std::make_shared<Node>();
      peg->setF("type", i < 2 ? 2.0f : i == 4 ? 4.0f : 1.0f);
      node->fields["peg"].k = VK::Ptr;
      node->fields["peg"].n = peg;
      Val entry;
      entry.k = VK::Ptr;
      entry.n = node;
      level->entries.push_back(entry);
    }
    for (int power : {0, 3, 12}) {
      Board board;
      BoardConfig cfg;
      cfg.greens = cfg.purple = false;
      cfg.master[0] = power;
      std::string err;
      if (!board.load(nullptr, level, cfg, &err)) { std::cerr << err << '\n'; return 2; }
      if (power == 12) board.pturn[0][Board::P_FIREBALL] = 1;
      AIMgr first, second;
      MTRand rng1, rng2;
      rng1.seed(4357);
      rng2.seed(4357);
      first.begin(board, 10, [&]() { return rng1.rand(); });
      second.begin(board, 10, [&]() { return rng2.rand(); });
      bool valid = true, deterministic = true;
      for (int frame = 0; frame < 500 && first.thinking() && second.thinking(); ++frame) {
        valid &= finite(first.run_->w) && finite(second.run_->w);
        deterministic &= first.run_->w.hash() == second.run_->w.hash();
        first.update(1);
        second.update(1);
      }
      check(!first.thinking() && !second.thinking() && first.candidates() > 0 && first.best().valid, "full AI search completes with scored candidates");
      check(valid && std::isfinite(first.angle()) && std::isfinite(second.angle()), "full AI search has finite trajectories and result");
      check(deterministic && first.angle() == second.angle() && first.bestScore() == second.bestScore() && first.best().left == second.best().left && rng1.drawCount == rng2.drawCount, "full AI search returns deterministic angle score and RNG use");
      std::cout << "AI_SEARCH power=" << power << " candidates=" << first.candidates() << " angle=" << first.angle() << " score=" << first.bestScore() << '\n';
    }
  }
  std::cout << "AI_COLLISION_FILTER checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
