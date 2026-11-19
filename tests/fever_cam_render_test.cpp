#include "render.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

using namespace pp;

struct Output { int w, h, canvas; bool art; };

static bool run(const char* mount, const char* art, bool nights, Output out) {
  Vfs vfs;
  LevelBundle level;
  std::string error;
  if (!vfs.mountGame(mount, &error) || (out.art && !vfs.mountPresentation(art)) ||
      !loadLevelBundle(vfs, nights ? "Bjorn1" : "Level1", level, &error)) {
    std::cerr << "Fever Cam assets: " << error << '\n';
    return false;
  }
  level.cfg.nights = nights;
  level.cfg.orange = 1;
  level.cfg.seed = 1;
  Board board;
  if (!board.load(level.base, level.level, level.cfg, &error) || board.orangeLeft != 1 ||
      !board.fire(nights ? 2.1f + .024f * 49 : 2.1f)) return false;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(out.w, out.h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surface) return false;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> sdl(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  if (!sdl) return false;
  Renderer renderer(sdl.get(), vfs), reference(sdl.get(), vfs);
  const bool console = out.canvas == 1066 && out.art;
  const float scale = std::min(out.w / float(out.canvas), out.h / 600.f);
  const float left = (out.w - out.canvas * scale) / 2, top = (out.h - 600 * scale) / 2;
  bool ok = true, skipped = false;
  auto check = [&](bool pass, const char* state, const char* message) {
    if (!pass) {
      std::cerr << level.name << ' ' << out.w << 'x' << out.h << " canvas=" << out.canvas << " art=" << out.art
                << ' ' << state << ": " << message << '\n';
      ok = false;
    }
  };
  auto frame = [&](Renderer& r, const Board& b) {
    r.setFrameCanvas(out.canvas);
    r.draw(b, level, {});
    SDL_RenderPresent(sdl.get());
    const auto* p = static_cast<const Uint32*>(surface->pixels);
    return std::vector<Uint32>(p, p + surface->pitch / 4 * out.h);
  };
  auto sample = [&](const char* state) {
    const auto original = board.hash();
    const auto shown = frame(renderer, board);
    Board clean = board;
    clean.zoomFade = 0;
    const auto bare = frame(reference, clean);
    int corner = 0, right = 0, interior = 0, bars = 0;
    for (int y = 0; y < out.h; ++y) for (int x = 0; x < out.w; ++x) {
      const int i = y * (surface->pitch / 4) + x;
      if (shown[i] == bare[i]) continue;
      const float u = (x + .5f - left) / scale, v = (y + .5f - top) / scale;
      if (u < -1 || v < -1 || u > out.canvas + 1 || v > 601) ++bars;
      else if (u < 98 && v < 54) ++corner;
      else if (u > out.canvas - 52 && v > 150 && v < 450) ++right;
      else if (console || (u > 52 && u < out.canvas - 52 && v > 36 && v < 564)) ++interior;
    }
    check(!bars && !interior, state, "camera chrome paints inside the scene or outside the viewport");
    if (board.zoomFade == 20) {
      check(corner > 300, state, "camera indicator missing at viewport origin");
      check(console ? !right : right > 300, state, "wrong camera frame on right viewport edge");
    }
    if (!board.zoomFade) check(shown == bare, state, "camera overlay visible while inactive");
    check(original == board.hash() && !SDL_RenderClipEnabled(sdl.get()), state, "draw changed simulation or leaked clip");
  };
  auto ballBounds = [&](float strength) {
    Board copy = board;
    copy.cfg.zoomK = strength;
    const auto shown = frame(renderer, copy);
    copy.balls[0].holeOut = 1;
    const auto hidden = frame(reference, copy);
    int x0 = out.w, x1 = -1, y0 = out.h, y1 = -1;
    for (int y = 0; y < out.h; ++y) for (int x = 0; x < out.w; ++x) {
      const int i = y * (surface->pitch / 4) + x;
      if (shown[i] == hidden[i]) continue;
      x0 = std::min(x0, x); x1 = std::max(x1, x);
      y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
    return SDL_Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
  };
  int entry = -1, peak = -1, hit = -1, release = -1;
  for (int i = 1; i <= 1800; ++i) {
    const int steps = board.shotSteps;
    board.step();
    renderer.advance(board);
    reference.advance(board);
    if (i == 10) {
      check(!board.slowOn && !board.fever && !board.zoom && !board.zoomFade, "normal", "unexpected Fever Cam on normal shot");
      sample("normal");
    }
    if (entry < 0 && board.slowOn && !board.fever) entry = i;
    if (entry >= 0 && !board.fever && board.shotSteps == steps) skipped = true;
    if (entry >= 0 && i == entry + 10) sample("entry");
    if (peak < 0 && board.zoom >= .99f) {
      peak = i;
      sample("tracking");
      if (!board.slowOn || board.balls.size() != 1) return false;
      const auto flat = ballBounds(0), close = ballBounds(1);
      check(flat.w > 4 && flat.h > 4 && std::abs(close.w - flat.w * 2) <= 4 && std::abs(close.h - flat.h * 2) <= 4,
            "tracking", "expected a magnified ball, not just slower physics");
      const auto mid = [&](SDL_Rect box) { return renderer.toBoard(box.x + box.w * .5f, box.y + box.h * .5f); };
      const auto a = mid(flat), z = mid(close);
      const auto& ball = board.balls.front();
      const float f = board.slowAcc / 1000.f;
      const V2 pos = ball.prev * (1 - f) + ball.pos * f;
      const float bias = std::clamp(pos.y - 360.f, 0.f, std::max(0.f, 570.f - pos.y));
      check(std::abs(a.x - pos.x) < 4 && std::abs(a.y - pos.y) < 4 && std::abs(z.x - pos.x) < 5 &&
            std::abs(z.y - (pos.y - bias)) < 6, "tracking", "camera lost its interpolated, off-center ball");
    }
    if (hit < 0 && board.fever) { hit = i; sample("hit"); }
    if (hit >= 0 && release < 0 && board.zoom > 0 && board.zoom < .5f) { release = i; sample("release"); }
    if (release >= 0 && !board.zoom && !board.zoomFade) {
      sample("exit");
      check(entry > 10 && peak > entry && hit > entry && board.orangeLeft == 0 && skipped, "shot", "missing last-orange approach, slowdown or hit");
      return ok;
    }
  }
  check(false, "shot", "Fever Cam did not enter and exit");
  return false;
}

int main(int argc, char** argv) {
  if (argc != 4 || !SDL_Init(0)) return 2;
  bool ok = true;
  for (bool nights : {false, true})
    for (Output o : {Output{1280, 720, 1066, true}, {1280, 800, 1066, true}, {1280, 720, 1066, false},
                     {1280, 800, 960, true}, {1400, 600, 1400, true}, {800, 600, 800, true}})
      ok &= run(argv[nights ? 2 : 1], argv[3], nights, o);
  SDL_Quit();
  return ok ? 0 : 1;
}
