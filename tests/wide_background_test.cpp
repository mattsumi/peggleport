#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "render.h"

using namespace pp;

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3 || !SDL_Init(0)) return 2;
  int checks = 0, failures = 0;
  std::string testCase;
  auto check = [&](bool pass, const char* name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << testCase << ": " << name << '\n'; }
  };
  for (int arg = 1; arg < argc; ++arg) {
    Vfs vfs;
    std::string error;
    if (!vfs.mountGame(argv[arg], &error)) { std::cerr << error << '\n'; return 2; }
    auto stages = listStageLevels(vfs);
    if (stages.size() < 3) return 2;
    for (size_t index : {size_t(0), size_t(2), stages.size() - 1}) {
      LevelBundle bundle;
      if (!loadLevelBundle(vfs, stages[index].name, bundle, &error)) return 2;
      const auto source = writeLevel(*bundle.level);
      std::vector<uint8_t> classic;
      for (bool expanded : {false, true}) for (int mode : {WS_OFF, WS_16_10, WS_16_9, WS_21_9, WS_AUTO}) {
        const int canvas = widescreenBoardWidth(mode, 2.4f) + 150;
        testCase = std::string(arg == 1 ? "Deluxe " : "Nights ") + bundle.name + " " + widescreenName(mode) + (expanded ? " expanded" : " frame");
        auto cfg = bundle.cfg;
        cfg.nights = arg == 2;
        cfg.boardWidth = expanded ? canvas - 150 : 650;
        Board board;
        if (!board.load(bundle.base, bundle.level, cfg, &error)) return 2;
        for (auto& object : board.objs) object.visible = false;
        const auto state = board.hash();
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(canvas, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!surface) return 2;
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
        if (!renderer) return 2;
        Renderer draw(renderer.get(), vfs);
        draw.setFrameCanvas(canvas);
        Hud hud;
        hud.showGuide = false;
        draw.draw(board, bundle, hud);
        SDL_RenderPresent(renderer.get());
        const auto* pixels = static_cast<const uint8_t*>(surface->pixels);
        if (mode == WS_OFF) classic.assign(pixels, pixels + surface->pitch * surface->h);
        else {
          int mismatched = 0;
          const int shift = (canvas - 800) / 2;
          for (int y = 180; y < 500; ++y)
            for (int x = 125; x < 675; ++x)
              if (std::memcmp(&classic[(size_t)y * 800 * 4 + x * 4], &pixels[(size_t)y * surface->pitch + (x + shift) * 4], 3)) ++mismatched;
          check(mismatched == 0, "widescreen preserves original central artwork");
          if (mode == WS_21_9) {
            for (bool right : {false, true}) {
              int textured = 0;
              const int x1 = right ? canvas - 126 : 125, x2 = right ? canvas - 176 : 175;
              for (int y = 180; y < 500; ++y)
                if (std::memcmp(&pixels[(size_t)y * surface->pitch + x1 * 4], &pixels[(size_t)y * surface->pitch + x2 * 4], 3)) ++textured;
              std::cout << testCase << (right ? " right=" : " left=") << textured << '\n';
              check(textured >= 60, "side backdrop retains horizontal variation instead of edge streaks");
            }
          }
        }
        check(board.hash() == state, "background rendering preserves simulation state");
        check(writeLevel(*bundle.level) == source, "background rendering preserves original level data");
      }
    }
  }
  std::cout << "WIDE_BACKGROUND checks=" << checks << " failures=" << failures << '\n';
  SDL_Quit();
  return failures ? 1 : 0;
}
