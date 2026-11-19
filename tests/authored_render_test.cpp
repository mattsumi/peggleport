#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "render.h"

using namespace pp;

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <Deluxe asset directory> [Nights asset directory]\n";
    return 2;
  }
  if (!SDL_Init(0)) { std::cerr << SDL_GetError() << '\n'; return 2; }
  int checks = 0, failures = 0;
  std::string testCase;
  auto check = [&](bool pass, const char* name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << testCase << ": " << name << '\n'; }
    return pass;
  };
  for (int arg = 1; arg < argc; ++arg) {
    Vfs vfs;
    std::string err;
    if (!vfs.mountGame(argv[arg], &err)) { std::cerr << err << '\n'; return 2; }
    ImageManifest manifest(vfs);
    const auto* art = manifest.find("IMAGE_BALL");
    if (!art) { std::cerr << "Missing IMAGE_BALL\n"; return 2; }
    const auto image = loadGameImage(vfs, art->path);
    if (!image || std::max(image->w, image->h) / image->scale >= 100) {
      std::cerr << "Expected compact native ball artwork\n";
      return 2;
    }
    LevelBundle bundle;
    const auto stages = listStageLevels(vfs);
    if (stages.empty() || !loadLevelBundle(vfs, stages.front().name, bundle, &err)) {
      std::cerr << "Cannot load native background: " << err << '\n';
      return 2;
    }
    auto level = std::make_shared<Level>();
    for (float x : {-70.0f, 325.0f, 720.0f}) {
      auto node = std::make_shared<Node>();
      node->type = 5;
      node->flags["visible"] = node->flags["collidable"] = true;
      node->setF("x", x);
      node->setF("y", 320);
      node->setF("radius", 10);
      node->fields["image"].k = VK::Str;
      node->fields["image"].s = art->path;
      Val entry;
      entry.k = VK::Ptr;
      entry.n = node;
      level->entries.push_back(entry);
    }
    const auto source = writeLevel(*level);
    for (int authored : {650, 917}) for (int display : {650, 917, 1400}) {
      if (display < authored) continue;
      testCase = std::string(arg == 1 ? "Deluxe" : "Nights") + " authored=" + std::to_string(authored) + " display=" + std::to_string(display);
      BoardConfig cfg;
      cfg.authoredWidth = authored;
      cfg.boardWidth = display;
      cfg.nights = arg == 2;
      cfg.greens = cfg.purple = false;
      Board board;
      if (!board.load(nullptr, level, cfg, &err) || board.objs.size() != 3) {
        std::cerr << err << '\n';
        return 2;
      }
      const auto geometry = board.objs;
      std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(display + 150, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
      if (!surface) { std::cerr << SDL_GetError() << '\n'; return 2; }
      std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
      if (!renderer) { std::cerr << SDL_GetError() << '\n'; return 2; }
      Renderer draw(renderer.get(), vfs);
      Hud hud;
      hud.showGuide = false;
      auto frame = [&]() {
        draw.draw(board, bundle, hud);
        SDL_RenderPresent(renderer.get());
        auto* pixels = static_cast<const uint8_t*>(surface->pixels);
        return std::vector<uint8_t>(pixels, pixels + surface->pitch * surface->h);
      };
      for (auto& obj : board.objs) obj.visible = false;
      const auto hidden = frame();
      auto changed = [&](const std::vector<uint8_t>& pixels, size_t i) {
        const auto& obj = board.objs[i];
        const int cx = (int)(75 + obj.pos.x), cy = (int)(40 + obj.pos.y);
        int count = 0;
        for (int y = cy - 50; y < cy + 50; ++y)
          for (int x = std::max(0, cx - 50); x < std::min(surface->w, cx + 50); ++x) {
            const size_t at = (size_t)y * surface->pitch + x * 4;
            if (std::memcmp(&pixels[at], &hidden[at], 4) != 0) ++count;
          }
        return count;
      };
      for (auto& obj : board.objs) obj.visible = true;
      const auto visible = frame();
      check(changed(visible, 1) > 0, "native textured center object renders");
      check((changed(visible, 0) > 0) == (authored > 650), "left extension respects authored width, not display override");
      check((changed(visible, 2) > 0) == (authored > 650), "right extension respects authored width, not display override");
      for (auto& obj : board.objs) obj.visible = false;
      check(frame() == hidden, "visible=false suppresses native textured objects");
      for (auto& obj : board.objs) { obj.visible = true; obj.collidable = false; }
      check(frame() == visible, "decoration and collider use identical native artwork");
      for (size_t i = 0; i < board.objs.size(); ++i) {
        const auto& obj = board.objs[i];
        const auto& before = geometry[i];
        check(obj.pos.x == before.pos.x && obj.pos.y == before.pos.y && obj.radius == before.radius && obj.rot == before.rot && obj.scale == before.scale && obj.x0 == before.x0 && obj.x1 == before.x1 && obj.y0 == before.y0 && obj.y1 == before.y1, "rendering preserves collision geometry");
      }
      for (auto& obj : board.objs) obj.fromBase = true;
      const auto base = frame();
      check((changed(base, 0) > 0) == (display > 650) && (changed(base, 2) > 0) == (display > 650), "base artwork retains its stock culling exemption");
      for (auto& obj : board.objs) { obj.fromBase = false; obj.collidable = true; obj.image.clear(); }
      check(frame() == hidden, "untextured non-peg collision geometry gains no automatic artwork");
      check(writeLevel(*level) == source, "rendering never changes authored level data");
    }
  }
  std::cout << "AUTHORED_RENDER checks=" << checks << " failures=" << failures << '\n';
  SDL_Quit();
  return failures ? 1 : 0;
}
