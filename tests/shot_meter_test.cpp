#include "render.h"
#include <algorithm>
#include <iostream>
#include <memory>

using namespace pp;

static bool run(const char* game, const char* art, bool nights) {
  Vfs vfs;
  LevelBundle level;
  std::string error;
  if (!vfs.mountGame(game, &error) || !vfs.mountPresentation(art) ||
      !loadLevelBundle(vfs, nights ? "Bjorn1" : "Level1", level, &error)) {
    std::cerr << "Cannot load shot meter fixture: " << error << '\n';
    return false;
  }
  level.cfg.nights = nights;
  level.cfg.seed = nights ? 2 : 4357;
  Board board;
  if (!board.load(level.base, level.level, level.cfg, &error)) return false;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(1920, 1080, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surface) return false;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> sdl(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  if (!sdl) return false;
  Renderer render(sdl.get(), vfs);
  bool ok = true;
  auto check = [&](const char* state, int minTop, int maxTop) {
    render.setFrameCanvas(1066);
    render.draw(board, level, {});
    SDL_RenderPresent(sdl.get());
    for (int left : {344, 432}) {
      int top = 1080, bottom = -1, width = 0, missing = 0, first = left + 28, last = left - 5;
      for (int y = 310; y < 1025; ++y) {
        int row = 0;
        for (int x = left - 5; x < left + 28; ++x) {
          const auto* c = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch + 4 * x;
          if (c[1] > 175 && c[1] > c[0] + 45 && c[1] > c[2] + 45) {
            ++row;
            first = std::min(first, x);
            last = std::max(last, x);
          }
        }
        if (row) { top = std::min(top, y); bottom = y; }
        else if (y > top && y < 1007) ++missing;
        width = std::max(width, row);
      }
      const bool pass = minTop < 0 ? bottom < 0 : top >= minTop && top <= maxTop &&
          bottom >= 1007 && bottom <= 1011 && !missing && first >= left - 1 && last <= left + 23 &&
          (minTop > 990 ? width >= 6 && width <= 18 : width >= 20 && width <= 24);
      if (!pass) {
        std::cerr << level.name << ' ' << state << " channel x=" << left << " green y=" << top << ".." << bottom
                  << " width=" << width << " missing rows=" << missing << '\n';
        ok = false;
      }
    }
  };
  check("empty", -1, -1);
  if (!board.fire(nights ? 3.63f : 3.49f)) return false;
  for (int i = 0; i < 2000; ++i) {
    board.step();
    render.advance(board);
    if (board.tick == (nights ? 40 : 392)) check("small", 998, 1008);
    if (board.tick == (nights ? 335 : 600)) check("low", 945, 979);
    if (board.tick == (nights ? 610 : 830)) check("intermediate", 520, 725);
    if (board.shotScore >= 25000 && board.lstate == 2) { check("full", 316, 327); return ok; }
  }
  std::cerr << level.name << " shot did not earn a free ball\n";
  return false;
}

int main(int argc, char** argv) {
  if (argc != 4 || !SDL_Init(0)) return 2;
  const bool deluxe = run(argv[1], argv[3], false);
  const bool nights = run(argv[2], argv[3], true);
  SDL_Quit();
  return deluxe && nights ? 0 : 1;
}
