#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "physball.h"
#include "render.h"

using namespace pp;
namespace fs = std::filesystem;
using Frame = std::vector<Uint8>;

static Obj peg(V2 p) {
  Obj o;
  o.pos = p;
  o.shape = Shape::Circle;
  o.radius = 9;
  o.pegType = 2;
  o.visible = o.collidable = true;
  o.x0 = p.x - 9; o.x1 = p.x + 9;
  o.y0 = p.y - 9; o.y1 = p.y + 9;
  return o;
}

static bool run(const Vfs& vfs, const LevelBundle& level, bool nights, int w, int h, int canvas, const fs::path& captures) {
  const std::string tag = std::string(nights ? "nights-" : "deluxe-") + std::to_string(w) + "x" + std::to_string(h) + "-" + std::to_string(canvas);
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surf(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surf) throw std::runtime_error(SDL_GetError());
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> sdl(SDL_CreateSoftwareRenderer(surf.get()), SDL_DestroyRenderer);
  if (!sdl) throw std::runtime_error(SDL_GetError());
  Renderer renderer(sdl.get(), vfs);
  bool ok = true;
  auto check = [&](bool pass, const char* what) {
    if (!pass) { std::cerr << "FAIL " << tag << ": " << what << '\n'; ok = false; }
  };
  auto draw = [&](const Board& b, bool guide, const char* name = nullptr) {
    Hud hud; hud.showGuide = guide;
    renderer.setFrameCanvas(canvas);
    renderer.draw(b, level, hud);
    if (!SDL_RenderPresent(sdl.get())) throw std::runtime_error(SDL_GetError());
    if (name && !captures.empty() && !SDL_SaveBMP(surf.get(), (captures / (tag + "-" + name + ".bmp")).string().c_str())) throw std::runtime_error(SDL_GetError());
    const auto* p = static_cast<const Uint8*>(surf->pixels);
    return Frame(p, p + surf->pitch * h);
  };
  auto delta = [&](const Frame& a, const Frame& b, V2 p) {
    const V2 origin = renderer.toBoard(0, 0);
    const float scale = w / (renderer.toBoard((float)w, 0).x - origin.x);
    int x = (int)std::floor((p.x - origin.x) * scale), y = (int)std::floor((p.y - origin.y) * scale);
    if (x < 0 || x >= w || y < 0 || y >= h) throw std::runtime_error("sample outside frame");
    int i = y * surf->pitch + x * 4;
    return std::max({std::abs(a[i] - b[i]), std::abs(a[i+1] - b[i+1]), std::abs(a[i+2] - b[i+2])});
  };
  auto settle = [&](Board& b) { for (int i = 0; i < 65; ++i) { ++b.tick; ++b.frames; renderer.advance(b); } };
  Board b;
  BoardConfig cfg = level.cfg;
  cfg.nights = nights;
  cfg.master[0] = Board::P_GUIDE;
  std::string err;
  if (!b.load(level.base, level.level, cfg, &err)) throw std::runtime_error(err);
  b.turns[0] = b.pturn[0][Board::P_GUIDE] = 3;
  b.noModHooks = true;
  b.emit = false;
  b.gunAngle = nights ? 4.205f : 4.23f;
  settle(b);
  auto hidden = draw(b, false), shown = draw(b, true, "stage");
  auto stage = b.guideTrace(b.gunAngle, true);
  if (stage.hitStep < 0 || (int)stage.pts.size() <= stage.hitStep + 8) throw std::runtime_error("stock bounce fixture");
  check(delta(hidden, shown, stage.hit) > 35, "stock first-contact marker missing");

  b.objs.clear();
  b.emitters.clear();
  b.gunAngle = 4.44f;
  auto empty = b.guideTrace(b.gunAngle, true);
  b.objs.push_back(peg(empty.pts.at(67) + V2{-6, 14}));
  auto one = b.guideTrace(b.gunAngle, true);
  b.objs.push_back(peg(one.pts.at(one.hitStep + 40)));
  const auto tr = b.guideTrace(b.gunAngle, true);
  if (tr.hitStep < 50 || (int)tr.pts.size() < tr.hitStep + 24) throw std::runtime_error("late two-peg fixture");
  settle(b);
  hidden = draw(b, false, "no-guide");
  shown = draw(b, true, "powered");
  Board fired = b;
  if (!fired.fire(b.gunAngle)) throw std::runtime_error("could not fire");
  const auto muzzle = b.muzzleArmed(b.gunAngle);
  check((fired.balls[0].pos - muzzle).len2() == 0, "fired origin differs from armed muzzle");
  int first = -1, second = -1, step = 0;
  for (int frame = 0; frame < 500 && second < 0; ++frame) {
    int tick = fired.tick; fired.stepCore();
    if (tick == fired.tick) continue;
    if (fired.balls.empty()) break;
    if (!fired.contactNotes().empty()) { if (first < 0) first = step; else second = step; }
    if (step < (int)tr.pts.size()) check((tr.pts[step] - fired.balls[0].pos).len2() == 0, "trace differs from fired step");
    ++step;
  }
  if (first != tr.hitStep || second != (int)tr.pts.size()) throw std::runtime_error("trace does not bracket actual contacts");
  check(delta(hidden, shown, tr.hit) > 50 && delta(hidden, shown, tr.hit + V2{4,0}) > 30, "missing ball-sized first-contact marker");
  int preDots = 0, preGaps = 0;
  for (int i = 11; i < tr.hitStep - 5; i += 4) {
    preDots += delta(hidden, shown, tr.pts[i]) > 40;
    preGaps += delta(hidden, shown, tr.pts[i+2]) < 12;
  }
  check(preDots >= 10 && preGaps >= 10, "incoming leg lost spaced dots");
  int center = 0, flank = 0, samples = 0;
  for (int i = first + 4; i < second - 1; ++i) {
    V2 d = tr.pts[i+1] - tr.pts[i];
    const V2 normal = V2{-d.y, d.x} * (1 / std::sqrt(d.len2()));
    for (float u : {0.f, 0.5f}) {
      const V2 p = tr.pts[i] + d * u;
      ++samples;
      center += delta(hidden, shown, p) > 25;
      flank += std::max(delta(hidden, shown, p + normal * 5), delta(hidden, shown, p - normal * 5)) > 25;
    }
  }
  std::cout << tag << " first=" << first << " second=" << second << " outgoing center=" << center << '/' << samples << " wide=" << flank << " incoming dots/gaps=" << preDots << '/' << preGaps << '\n';
  check(center >= samples * .95, "reflected centerline has gaps or ends early");
  check(flank <= samples * .05, "reflected stroke too wide");
  check(delta(hidden, shown, one.pts.at(second + 8)) == 0, "line continues through second peg");
  check(delta(hidden, shown, empty.pts.at(first + 5)) == 0, "guide continues straight after contact");

  Board bare = b; bare.phase = Phase::Shot; bare.shotSteps = 10;
  const auto unloaded = draw(bare, false);
  const V2 axis = gun::launchVelocity(b.gunAngle, 1);
  const int lip = delta(hidden, unloaded, muzzle - axis * 5), tip = delta(hidden, unloaded, muzzle + axis * 2);
  check(lip < 8, "queued ball overpaints muzzle lip");
  check(tip > 12, "queued ball not seated visibly at opening");
  b.turns[0] = b.pturn[0][Board::P_GUIDE] = 0;
  settle(b);
  const auto normalOff = draw(b, false), normalOn = draw(b, true, "ordinary");
  check(delta(normalOff, normalOn, tr.pts[19]) > 30, "ordinary guide missing before contact");
  check(delta(normalOff, normalOn, tr.pts[first + 8]) == 0, "ordinary guide has a reflected leg");
  b.objs.clear(); b.turns[0] = b.pturn[0][Board::P_GUIDE] = 3;
  settle(b);
  auto missOff = draw(b, false), missOn = draw(b, true);
  check(delta(missOff, missOn, empty.pts[19]) > 30 && delta(missOff, missOn, empty.pts[21]) < 12, "contact-free guide is not dotted");
  check(delta(missOff, missOn, tr.pts[first + 15]) == 0, "spurious bounce on miss");

  Board cross = b;
  cross.gunAngle = 4.712389f;
  if (!cross.fire(cross.gunAngle)) throw std::runtime_error("crossing fixture could not fire");
  const V2 shaft = cross.pivot() + V2{0,75};
  auto& ball = cross.balls.front();
  phys::ballSetPos(ball, shaft.x - 28, shaft.y); ball.prev = ball.pos; ball.vel = {4,0};
  int inside = 0, outside = 0, edgeDelta[3]{};
  for (int i = 0; i < 30 && outside < 2; ++i) {
    int tick = cross.tick; cross.stepCore(); if (cross.tick == tick || cross.balls.empty()) continue;
    V2 p = cross.balls[0].pos;
    const bool under = std::abs(p.x - shaft.x) < 2, left = p.x < shaft.x - 20, right = p.x > shaft.x + 20;
    if (!under && !left && !right) continue;
    if ((left && outside) || (right && outside != 1)) continue;
    Board noBall = cross; noBall.balls.clear();
    auto bg = draw(noBall, false), fg = draw(cross, true, under ? "crossing" : right ? "emerged" : nullptr);
    const int diff = delta(bg, fg, p);
    edgeDelta[under ? 1 : right ? 2 : 0] = diff;
    if (under) { ++inside; check(diff < 8, "returning ball overpaints solid barrel"); }
    else { ++outside; check(diff > 15, "crossing ball hidden in clear field"); }
    check(delta(bg, fg, tr.pts[19]) == 0, "guide appears during shot");
  }
  check(inside && outside == 2, "ball did not traverse both barrel edges");
  std::cout << tag << " lip/tip=" << lip << '/' << tip << " crossing left/under/right=" << edgeDelta[0] << '/' << edgeDelta[1] << '/' << edgeDelta[2] << '\n';
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 4 || argc > 5 || !SDL_Init(0)) return 2;
  bool ok = true;
  try {
    for (int i : {1,2}) {
      Vfs vfs; std::string err; LevelBundle lb;
      if (!vfs.mountGame(argv[i], &err)) throw std::runtime_error(err);
      const auto levels = listStageLevels(vfs);
      if (levels.empty() || !loadLevelBundle(vfs, levels.front().name, lb, &err)) throw std::runtime_error(err);
      auto scene = [&](int w, int h, int cw) { ok &= run(vfs, lb, i == 2, w, h, cw, argc == 5 ? argv[4] : ""); };
      scene(800,600,800);
      scene(1280,720,1066);
      if (!vfs.mountPresentation(argv[3])) throw std::runtime_error("presentation mount failed");
      scene(1920,1080,1066);
      scene(1280,800,960);
    }
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
  SDL_Quit();
  return ok ? 0 : 1;
}
