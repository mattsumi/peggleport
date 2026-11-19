#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "brick.h"
#include "render.h"

using namespace pp;

static Obj circle(V2 pos) {
  Obj o;
  o.shape = Shape::Circle;
  o.pos = pos;
  o.radius = 10;
  o.pegType = 1;
  o.collidable = o.visible = true;
  o.x0 = pos.x - o.radius;
  o.x1 = pos.x + o.radius;
  o.y0 = pos.y - o.radius;
  o.y1 = pos.y + o.radius;
  return o;
}

static Board fixture(int width = 650) {
  Board b;
  b.W = b.cfg.boardWidth = b.cfg.authoredWidth = width;
  b.objs = {circle({width * 0.5f + 8, 230})};
  b.rng.seed(4357);
  b.emit = false;
  b.noModHooks = true;
  b.master[0] = Board::P_GUIDE;
  b.pturn[0][Board::P_GUIDE] = b.turns[0] = 2;
  b.ballsLeft[0] = 8;
  b.orangeLeft = b.orangeTotal = 25;
  return b;
}

struct PhysicalPath {
  std::vector<V2> pts;
  std::vector<int> contacts;
  std::vector<int> objects;
};

static PhysicalPath physicalPath(Board board) {
  PhysicalPath out;
  board.emit = false;
  board.noModHooks = true;
  if (!board.fire(board.gunAngle)) return out;
  for (int frame = 0; frame < 1000 && out.pts.size() < 200; ++frame) {
    const int tick = board.tick;
    board.stepCore();
    if (board.tick == tick) continue;
    if (board.balls.empty()) break;
    for (auto [ball, obj] : board.contactNotes()) {
      if (ball == 0) { out.contacts.push_back(static_cast<int>(out.pts.size())); out.objects.push_back(obj); }
    }
    out.pts.push_back(board.balls[0].pos);
    if (out.contacts.size() >= 2) break;
  }
  return out;
}

static bool samePrefix(const Board::GuideTrace& trace, const PhysicalPath& path) {
  if (trace.pts.size() > path.pts.size()) {
    std::cerr << "PREFIX size trace=" << trace.pts.size() << " physical=" << path.pts.size() << '\n';
    return false;
  }
  for (size_t i = 0; i < trace.pts.size(); ++i)
    if (trace.pts[i].x != path.pts[i].x || trace.pts[i].y != path.pts[i].y) {
      std::cerr << "PREFIX step=" << i << " trace=" << trace.pts[i].x << ',' << trace.pts[i].y << " physical=" << path.pts[i].x << ',' << path.pts[i].y << '\n';
      return false;
    }
  return true;
}

static Obj wall(V2 a, V2 b) {
  Obj o;
  o.shape = Shape::Line;
  o.collidable = true;
  o.local = {a, b};
  o.pline.init(a.x, a.y, b.x, b.y);
  o.x0 = o.pline.bx0; o.x1 = o.pline.bx1;
  o.y0 = o.pline.by0; o.y1 = o.pline.by1;
  return o;
}

static Obj brick(V2 pos) {
  Obj o = circle(pos);
  o.shape = Shape::Brick;
  o.rot = 0.45f;
  o.local = brickInitFromParams(BrickParams{}).pts;
  o.ppoly.pts = o.local;
  o.ppoly.cx = pos.x; o.ppoly.cy = pos.y;
  o.ppoly.rot = o.ppoly.prevRot = o.rot;
  o.ppoly.normalDir = o.normalDir;
  o.ppoly.initFromPoints();
  o.x0 = o.ppoly.bx0; o.x1 = o.ppoly.bx1;
  o.y0 = o.ppoly.by0; o.y1 = o.ppoly.by1;
  return o;
}

static std::vector<unsigned char> pixels(SDL_Surface* surface) {
  const auto* data = static_cast<const unsigned char*>(surface->pixels);
  return {data, data + surface->pitch * surface->h};
}

struct HookProbe : SimHooks {
  int calls = 0;
  void levelStart(Board&) override { ++calls; }
  void stepBegin(Board&) override { ++calls; }
  void stepEnd(Board&) override { ++calls; }
  void event(Board&, int, int, int, int, int) override { ++calls; }
  bool power(Board&, int, int, bool) override { ++calls; return false; }
};

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const std::string& name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
    return pass;
  };
  Board b = fixture();
  const auto before = b.hash();
  const auto normal = b.guideTrace(b.gunAngle, false);
  const auto super = b.guideTrace(b.gunAngle, true);
  const auto path = b.superGuide(b.gunAngle, 200);
  check(super.hitStep >= 0 && super.hitPeg, "fixture hits a peg");
  check(super.pts.size() > static_cast<size_t>(super.hitStep + 20), "Super Guide retains a post-peg bounce trajectory");
  check(normal.pts.size() <= static_cast<size_t>(normal.hitStep + 5), "normal trace remains short at first contact");
  bool matches = super.pts.size() <= path.size();
  for (size_t i = 0; matches && i < super.pts.size(); ++i)
    matches = super.pts[i].x == path[i].x && super.pts[i].y == path[i].y;
  check(matches, "trace follows production collision integration");
  check(b.hash() == before && b.rng.drawCount == 0 && b.events.empty(), "guide queries preserve board and RNG");
  std::cout << "TRACE first=" << super.hitStep << " normal=" << normal.pts.size() << " super=" << super.pts.size() << " board=" << before << '\n';
  check(samePrefix(super, physicalPath(b)), "static guide agrees with fired shot stepCore");
  Board moving = fixture();
  moving.objs[0].hasMover = moving.objs[0].canMove = true;
  moving.objs[0].mv.type = 1;
  moving.objs[0].mv.time = 120;
  moving.objs[0].mv.ax = moving.objs[0].pos.x;
  moving.objs[0].mv.ay = moving.objs[0].pos.y;
  moving.objs[0].mv.ry = 20;
  const auto movingBefore = moving.hash();
  const auto movingTrace = moving.guideTrace(moving.gunAngle, true);
  const auto movingPath = physicalPath(moving);
  check(!movingPath.contacts.empty(), "moving fixture has physical contact");
  check(!movingPath.contacts.empty() && movingTrace.hitStep == movingPath.contacts[0], "moving guide predicts actual contact time");
  check(samePrefix(movingTrace, movingPath), "moving guide agrees with fired shot stepCore");
  check(moving.hash() == movingBefore, "moving guide preserves source board");
  std::vector<std::pair<std::string, Board>> cases;
  cases.push_back({"peg", b});
  cases.push_back({"moving-peg", moving});
  Board second = b;
  V2 secondPos = path[static_cast<size_t>(super.hitStep + 35)];
  secondPos.y += 10;
  second.objs.push_back(circle(secondPos));
  cases.push_back({"second-peg", second});
  Board line = b;
  line.objs = {wall({500, 300}, {100, 200})};
  cases.push_back({"sloping-wall", line});
  for (bool right : {false, true}) {
    Board side = b;
    side.gunAngle = right ? 5.6f : 3.8f;
    side.objs = {right ? wall({530, 0}, {530, 560}) : wall({120, 560}, {120, 0})};
    cases.push_back({right ? "right-wall" : "left-wall", side});
  }
  Board block = b;
  block.objs = {brick({325, 230})};
  cases.push_back({"rotated-brick", block});
  Board movingBlock = block;
  movingBlock.objs[0].hasMover = movingBlock.objs[0].canMove = true;
  movingBlock.objs[0].mv = moving.objs[0].mv;
  movingBlock.objs[0].mv.ax = 325;
  movingBlock.objs[0].mv.rotation = 0.45f;
  cases.push_back({"moving-brick", movingBlock});
  Board wide = fixture(1100);
  cases.push_back({"authored-wide", wide});
  for (bool right : {false, true}) {
    Board extension = wide;
    V2 target{right ? 1000.0f : 100.0f, 250};
    extension.objs = {circle(target)};
    extension.gunAngle = extension.aimAt(target);
    cases.push_back({right ? "authored-right-extension" : "authored-left-extension", extension});
  }
  Board ignored = b;
  ignored.objs.push_back(circle({325, 170}));
  ignored.objs.back().gone = true;
  ignored.objs.push_back(circle({325, 190}));
  ignored.objs.back().collidable = false;
  cases.push_back({"gone-and-disabled-pegs", ignored});
  Board tuned = b;
  tuned.physics.gravityX = 0.012f;
  tuned.physics.drag = 0.001f;
  tuned.physics.ballBounce = 0.85f;
  tuned.physics.pegBounce = 1.1f;
  tuned.physics.ballRadius = 8;
  tuned.derivePhysics();
  cases.push_back({"tuned-physics", tuned});
  Board sameFrame = b;
  sameFrame.gunAngle = 5.9f;
  sameFrame.grav_ = sameFrame.physics.gravityY = 0;
  sameFrame.gunSpeed_ = sameFrame.physics.launchSpeed = sameFrame.physics.maxSpeed = 15;
  const V2 muzzle = sameFrame.muzzle(sameFrame.gunAngle);
  const V2 u = gun::launchVelocity(sameFrame.gunAngle, 1);
  auto offset = [&](float x, float y) { return V2{muzzle.x + x * u.x - y * u.y, muzzle.y + x * u.y + y * u.x}; };
  sameFrame.objs = {circle(offset(16, 5)), circle(offset(-20, -10))};
  cases.push_back({"same-frame-contacts", sameFrame});
  for (const auto& [name, board] : cases) {
    const auto state = board.hash();
    const auto draws = board.rng.drawCount;
    const auto trace = board.guideTrace(board.gunAngle, true);
    const auto reference = physicalPath(board);
    check(!reference.contacts.empty(), name + " physical first contact");
    check(!reference.contacts.empty() && trace.hitStep == reference.contacts[0], name + " first contact timing");
    check(samePrefix(trace, reference), name + " exact production trajectory");
    if (reference.contacts.size() >= 2) check(trace.pts.size() <= static_cast<size_t>(reference.contacts[1]), name + " stops before second bounce");
    check(trace.pts.size() <= 200 && (trace.hitStep < 0 || trace.pts.size() <= static_cast<size_t>(trace.hitStep + 100)), name + " bounded trace");
    bool finite = true;
    for (V2 p : trace.pts) finite = finite && std::isfinite(p.x) && std::isfinite(p.y);
    check(finite, name + " finite points");
    const auto repeat = board.guideTrace(board.gunAngle, true);
    check(repeat.hitStep == trace.hitStep && repeat.pts.size() == trace.pts.size() && samePrefix(repeat, {trace.pts, {}, {}}), name + " repeatable query");
    check(board.hash() == state && board.rng.drawCount == draws, name + " unchanged board and RNG");
    std::cout << "CASE " << name << " first=" << trace.hitStep << " points=" << trace.pts.size() << " contacts=";
    for (int step : reference.contacts) std::cout << step << ',';
    std::cout << " hash=" << state << '\n';
  }
  Board empty = fixture();
  empty.objs.clear();
  check(empty.guideTrace(empty.gunAngle, true).hitStep == -1, "empty field has no contact");
  check(empty.guideTrace(empty.gunAngle, true).pts.size() <= 200, "empty field trace is bounded");
  Board limit = empty;
  limit.grav_ = limit.gunSpeed_ = 0;
  check(limit.guideTrace(limit.gunAngle, true).pts.size() == 200, "stationary trace hits normal step cap");
  limit.lstate = 10;
  check(limit.guideTrace(limit.gunAngle, true).pts.size() == 300, "special guide state hits its step cap");
  Board guarded = b;
  guarded.noModHooks = false;
  guarded.objs[0].toggle = true;
  guarded.objs[0].moveCount = 41;
  const auto guardedHash = guarded.hash();
  HookProbe hooks;
  SimHooks* previousHooks = g_simHooks;
  g_simHooks = &hooks;
  guarded.guideTrace(guarded.gunAngle, true);
  guarded.guideTrace(guarded.gunAngle, false);
  g_simHooks = previousHooks;
  check(hooks.calls == 0, "guide mover updates cannot invoke external mod hooks");
  check(guarded.hash() == guardedHash && guarded.rng.drawCount == 0 && guarded.fxTotal == 0 && guarded.events.empty(), "guide toggle effects leave source state and RNG untouched");
  check(guarded.pturn[0][Board::P_GUIDE] == 2 && guarded.turns[0] == 2 && guarded.ballsLeft[0] == 8 && guarded.score[0] == 0, "guide does not consume powers or balls or award score");
  if (!SDL_Init(0)) { std::cerr << SDL_GetError() << '\n'; return 2; }
  const std::vector<std::pair<std::string, Board>> renderCases = {{"super", b}, {"second", second}, {"wide", wide}};
  for (int arg = 1; arg < argc; ++arg) {
    Vfs vfs;
    std::string err;
    if (!vfs.mountGame(argv[arg], &err)) { std::cerr << err << '\n'; return 2; }
    const std::string game = arg == 1 ? "deluxe" : "nights";
    auto levels = listStageLevels(vfs);
    LevelBundle lb;
    if (levels.size() < 2 || !loadLevelBundle(vfs, levels[1].name, lb, &err)) { std::cerr << err << '\n'; return 2; }
    auto scenes = renderCases;
    for (int width : {650, 1100}) {
      Board loaded;
      BoardConfig cfg = lb.cfg;
      cfg.boardWidth = width;
      cfg.nights = arg == 2;
      cfg.greens = cfg.purple = false;
      cfg.master[0] = Board::P_GUIDE;
      cfg.seed = 4357;
      if (!loaded.load(lb.base, lb.level, cfg, &err)) { std::cerr << err << '\n'; return 2; }
      loaded.pturn[0][Board::P_GUIDE] = loaded.turns[0] = 2;
      loaded.gunAngle = 4.73f;
      for (float angle : {3.7f, 4.2f, 4.73f, 5.2f, 5.7f}) {
        loaded.gunAngle = angle;
        const auto trace = loaded.guideTrace(angle, true);
        const auto reference = physicalPath(loaded);
        const std::string name = game + " " + lb.name + " width=" + std::to_string(width) + " angle=" + std::to_string(angle);
        check(samePrefix(trace, reference), name + " actual level physics");
        if (!reference.contacts.empty()) check(trace.hitStep == reference.contacts[0], name + " contact time");
      }
      loaded.gunAngle = 4.73f;
      scenes.push_back({"stage_" + std::to_string(width), loaded});
    }
    for (auto [sceneName, board] : scenes) {
      const std::string name = game + "_" + sceneName;
      const int width = board.W + 150;
      std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(width, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
      if (!surface) { std::cerr << SDL_GetError() << '\n'; return 2; }
      std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> sdl(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
      if (!sdl) { std::cerr << SDL_GetError() << '\n'; return 2; }
      Renderer renderer(sdl.get(), vfs);
      VisualOptions visual;
      visual.bloom = visual.interpolate = visual.sparks = false;
      renderer.setVisualOptions(visual);
      board.cfg.nights = arg == 2;
      for (int i = 0; i < 65; ++i) { ++board.frames; ++board.tick; renderer.advance(board); }
      Hud hud;
      hud.title = "Guide regression";
      auto draw = [&](bool guide) {
        hud.showGuide = guide;
        renderer.draw(board, lb, hud);
        SDL_RenderPresent(sdl.get());
        return pixels(surface.get());
      };
      auto changedNear = [&](const std::vector<unsigned char>& hidden, const std::vector<unsigned char>& shown, V2 sample) {
        int changed = 0;
        for (int y = std::max(0, static_cast<int>(sample.y + 40) - 9); y < std::min(600, static_cast<int>(sample.y + 40) + 10); ++y)
          for (int x = std::max(0, static_cast<int>(sample.x + 75) - 9); x < std::min(width, static_cast<int>(sample.x + 75) + 10); ++x) {
            const size_t p = static_cast<size_t>(y * surface->pitch + x * 4);
            if (hidden[p] != shown[p] || hidden[p + 1] != shown[p + 1] || hidden[p + 2] != shown[p + 2]) ++changed;
          }
        return changed;
      };
      const auto hidden = draw(false);
      const auto shown = draw(true);
      const auto trace = board.guideTrace(board.gunAngle, true);
      const auto reference = physicalPath(board);
      if (sceneName == "super" || sceneName == "second" || sceneName == "wide") {
        const V2 sample = reference.pts[static_cast<size_t>(trace.hitStep + 17)];
        const int changed = changedNear(hidden, shown, sample);
        check(changed > 5, name + " production renderer displays post-bounce dots");
        std::cout << "RENDER " << name << " post_bounce_pixels=" << changed << " sample=" << sample.x << ',' << sample.y << '\n';
        board.pturn[0][Board::P_GUIDE] = board.turns[0] = 0;
        const auto normalHidden = draw(false);
        const auto normalShown = draw(true);
        check(changedNear(normalHidden, normalShown, sample) == 0, name + " normal guide stops at first contact");

        board.pturn[0][Board::P_GUIDE] = board.turns[0] = 2;
      }
      if (sceneName == "second") {
        const auto continuation = board.superGuide(board.gunAngle, 100);
        bool isolated = false;
        if (reference.contacts.size() >= 2) {
          for (size_t i = static_cast<size_t>(reference.contacts[1] + 15); i < continuation.size(); ++i) {
            const V2 beyond = continuation[i];
            bool separate = beyond.x > 20 && beyond.x < board.W - 20 && beyond.y > 100 && beyond.y < 530;
            for (V2 p : trace.pts) separate = separate && std::hypot(p.x - beyond.x, p.y - beyond.y) > 25;
            if (!separate) continue;
            check(changedNear(hidden, shown, beyond) == 0, name + " does not draw beyond second bounce");
            isolated = true;
            break;
          }
        }
        check(isolated, name + " isolated second-bounce continuation fixture");
        const auto ballistic = empty.superGuide(empty.gunAngle, 80);
        check(changedNear(hidden, shown, ballistic[70]) == 0, name + " does not resume unreflected ballistic fallback");
      }
      if (sceneName.find("stage_") == 0) {
        int post = 0;
        for (size_t i = static_cast<size_t>(std::max(0, trace.hitStep + 8)); i < reference.pts.size() && i < 78; ++i)
          post += changedNear(hidden, shown, reference.pts[i]);
        check(post > 0, name + " shipped level displays reflected path");
        std::cout << "STAGE " << name << " level=" << lb.name << " first=" << trace.hitStep << " points=" << trace.pts.size() << " post_pixels=" << post << '\n';
      }
    }
  }
  SDL_Quit();
  std::cout << "CHECKS " << checks << " FAILURES " << failures << '\n';
  return failures ? 1 : 0;
}
