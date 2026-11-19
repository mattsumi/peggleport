#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "render.h"
#include "ui/widescreen.h"

namespace fs = std::filesystem;
using namespace pp;

static void image(const fs::path& path, int w, int h, bool wide, uint8_t blue = 33, int trim = 0) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  const uint8_t header[18]{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8), 32, 40};
  out.write(reinterpret_cast<const char*>(header), sizeof header);
  std::vector<uint8_t> row(w * 4);
  for (int x = 0; x < w; x++) {
    bool wing = wide && std::abs(x * 2 - w) >= 1300;
    const int edge = trim < 0 ? w - 1 - x : x;
    const bool rim = trim && edge >= 120 && edge < 142;
    row[x * 4 + 0] = wing && x > w / 2 ? 199 : rim ? 177 : blue;
    row[x * 4 + 1] = wing ? 21 : 153;
    row[x * 4 + 2] = wing && x < w / 2 ? 227 : 42;
    row[x * 4 + 3] = 255;
  }
  for (int y = 0; y < h; y++) out.write(reinterpret_cast<const char*>(row.data()), row.size());
}

static bool frame(const Vfs& vfs, int width, const Board& board, const LevelBundle& level, bool wide, bool nativeArt = false,
                  int outW = 0, int outH = 600) {
  if (!outW) outW = width;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
    SDL_CreateSurface(outW, outH, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surface) return false;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
    SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  if (!renderer) return false;
  Renderer draw(renderer.get(), vfs);
  draw.setFrameCanvas(width);
  Hud hud;
  hud.showGuide = false;
  const auto hash = board.hash();

  draw.draw(board, level, hud);
  SDL_RenderPresent(renderer.get());
  const float scale = std::min(outW / (float)width, outH / 600.0f);
  const float ox = (outW - width * scale) / 2, oy = (outH - 600 * scale) / 2;
  const auto aim = draw.toBoard(ox + (width / 2.0f + 80) * scale, oy + 300 * scale);
  const auto leftWall = draw.toBoard(outW / 2.0f - board.W / 2.0f * scale, oy + 40 * scale);
  const auto physical = [&](int x, int y) { return static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch + x * 4; };
  const auto pixel = [&](int x, int y) { return physical((int)(ox + x * scale), (int)(oy + y * scale)); };
  const uint8_t center[]{42, 153, 33};
  if (board.hash() != hash || std::abs(aim.x - (board.W / 2.0f + 80)) > 0.01f || std::abs(aim.y - 260.0f) > 0.01f ||
      std::abs(leftWall.x) > 0.01f || std::abs(leftWall.y) > 0.01f ||
      std::memcmp(pixel(width / 2, 219), center, 3)) return false;
  const auto* left = pixel(80, 219);
  const auto* right = pixel(width - 81, 219);
  const uint8_t chrome[]{42, 153, 247};
  if (board.W == 650 && width >= 1064 && width <= 1068) {
    const int inset = (width - 800) / 2;
    const uint8_t trim[]{42, 153, 127};
    const auto match = [&](int x, int y, const uint8_t* expected, bool equal = true) {
      const auto* p = pixel(x, y);
      if ((std::memcmp(p, expected, 3) == 0) == equal) return true;
      std::cerr << "Unexpected console pixel " << x << ',' << y << ": " << (int)p[0] << ',' << (int)p[1] << ',' << (int)p[2] << '\n';
      return false;
    };
    const uint8_t black[]{0, 0, 0};
    if (oy > 1 && (std::memcmp(physical(outW / 2, 0), black, 3) ||
                   std::memcmp(physical(outW / 2, outH - 1), black, 3))) {
      std::cerr << "Level art spilled into the 16:9 letterbox at " << outW << 'x' << outH << '\n';
      return false;
    }
    if (nativeArt) {
      for (const int y : {int(std::round(oy)), int(std::round(outH - oy)) - 1}) {
        const int x = int(ox + 10 * scale);
        if (std::memcmp(physical(x, y), center, 3) || std::memcmp(physical(outW - 1 - x, y), center, 3)) {
          std::cerr << "Console gutters left an unpainted edge at row " << y << '\n';
          return false;
        }
      }
    }
    const uint8_t rim[]{42, 153, 177};
    return match(250, 219, center) && match(15 + inset, 20, chrome) && match(width - 55 - inset, 20, chrome) &&
           match(15, 20, chrome, false) && match(width - 55, 20, chrome, false) &&
           match(10, 219, center, nativeArt) && match(width - 11, 219, center, nativeArt) &&
           match(inset - 9, 219, trim, nativeArt) && match(width - inset + 9, 219, trim, nativeArt) &&
           match(inset - 2, 219, rim, nativeArt) && match(width - inset + 1, 219, rim, nativeArt);
  }
  if ((width == 800 || board.W > 650) &&
      (std::memcmp(pixel(15, 20), chrome, 3) || std::memcmp(pixel(width - 55, 20), chrome, 3))) {
    std::cerr << "HUD no longer follows the board edges in Classic or on an authored wide board\n";
    return false;
  }
  return (left[0] > 2 * left[1] && left[0] > 2 * left[2]) == wide &&
         (right[2] > 2 * right[1] && right[2] > 2 * right[0]) == wide;
}

int main(int argc, char** argv) {
  if (argc != 2 || !SDL_Init(0)) return 2;
  fs::path root = argv[1];
  image(root / "base/levels/test.tga", 1600, 1200, false);
  image(root / "base/chrome.tga", 200, 80, false, 247);
  fs::create_directories(root / "base/properties");
  std::ofstream(root / "base/properties/resources.xml") <<
      "<Image id=\"IMAGE_IP_TLC\" path=\"chrome\"/><Image id=\"IMAGE_IP_TRC\" path=\"chrome\"/>";
  image(root / "wide/widescreen/levels/test.tga", 2880, 1200, true);
  std::ofstream(root / "base/mod.ini") << "image_scale = 2\n";
  std::ofstream(root / "wide/mod.ini") << "image_scale = 2\n";
  Vfs vfs;
  if (!vfs.mountMod(root / "base", nullptr) || !vfs.mountMod(root / "wide", nullptr)) return 2;
  image(root / "alpha/sprite.tga", 8, 8, false);
  image(root / "alpha/sprite_.tga", 8, 8, false);
  Vfs alpha;
  std::ofstream(root / "alpha/mod.ini") << "image_scale = 2\n";
  if (!alpha.mountMod(root / "alpha", nullptr)) return 2;
  const auto masked = loadGameImage(alpha, "sprite");
  alpha.unmountMods();
  std::ofstream(root / "alpha/mod.ini") << "image_alpha = baked\n";
  if (!alpha.mountMod(root / "alpha", nullptr)) return 2;
  const auto baked = loadGameImage(alpha, "sprite", "sprite_");
  const auto bakedExact = loadGameImage(alpha, "sprite.tga");
  Board board;
  LevelBundle level;
  level.background = "test";
  int failures = 0;
  if (!baked || !bakedExact || baked->rgba[3] != 255 || bakedExact->rgba[3] != 255) {
    std::cerr << "Baked alpha was replaced by a sidecar mask\n";
    failures++;
  }
  if (!masked || masked->scale != 2 || masked->rgba[3] != 33) {
    std::cerr << "Stock mask behavior changed\n";
    failures++;
  }
  const auto check = [&](int width, bool wide) {
    if (!frame(vfs, width, board, level, wide)) {
      std::cerr << "Background mismatch: " << width << (wide ? " with" : " without") << " panorama\n";
      failures++;
    }
  };
  check(800, false);
  for (auto mode : {ui::WidescreenMode::W16x10, ui::WidescreenMode::W16x9, ui::WidescreenMode::W21x9}) {
    int width = ui::widescreenCanvasWidth(mode, 0, 0);
    check(width, true);
  }
  image(root / "console/console/gutters/default_left.tga", 260, 600, false);
  image(root / "console/console/gutters/default_right.tga", 260, 600, false);
  image(root / "console/console/trim/left.tga", 237, 689, false, 127, 1);
  image(root / "console/console/trim/right.tga", 241, 689, false, 127, -1);
  if (!vfs.mountMod(root / "console", nullptr)) return 2;
  if (!frame(vfs, 1066, board, level, false, true)) {
    std::cerr << "Console gutters did not sit outside the centered 16:9 frame\n";
    failures++;
  }
  for (const auto [w, h] : {std::pair{1572, 1067}, std::pair{1920, 1080}}) {
    if (!frame(vfs, 1066, board, level, false, true, w, h)) {
      std::cerr << "16:9 HUD mismatch at " << w << 'x' << h << '\n';
      failures++;
    }
  }
  Board authored;
  BoardConfig cfg;
  cfg.boardWidth = cfg.authoredWidth = 916;
  cfg.balls = 1;
  if (!authored.load(nullptr, std::make_shared<Level>(), cfg, nullptr)) return 2;
  if (!frame(vfs, 800, board, level, false, true, 1572, 1067) ||
      !frame(vfs, 1066, authored, level, false, true, 1572, 1067)) {
    std::cerr << "16:9 presentation changed Classic or authored wide gameplay\n";
    failures++;
  }
  check(960, true);
  check(1400, true);
  vfs.unmountMod(root / "console");
  image(root / "incomplete/console/gutters/default_left.tga", 260, 600, false);
  if (!vfs.mountMod(root / "incomplete", nullptr)) return 2;
  check(1066, true);
  vfs.unmountMod(root / "incomplete");
  vfs.unmountMod(root / "wide");
  check(1400, false);
  SDL_Quit();
  return failures ? 1 : 0;
}
