#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/visuals_dialog.h"

using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;

struct Pixels {
  std::vector<Uint8> bytes;
  int w;
  int light(int x, int y) const {
    const auto* p = bytes.data() + (size_t)(y * w + x) * 4;
    return p[0] + p[1] + p[2];
  }
  double detail(SDL_Rect a, int step = 1) const {
    double edges = 0, total = 0;
    for (int y = a.y; y < a.y + a.h - step; y += step)
      for (int x = a.x; x < a.x + a.w - step; x += step) {
        int v = light(x, y);
        total += v;
        edges += std::abs(v - light(x + step, y)) + std::abs(v - light(x, y + step));
      }
    return edges / std::max(1.0, total);
  }
  bool same(const Pixels& o, SDL_Rect a) const {
    for (int y = a.y; y < a.y + a.h; ++y) {
      size_t start = (size_t)(y * w + a.x) * 4;
      if (!std::equal(bytes.data() + start, bytes.data() + start + a.w * 4, o.bytes.data() + start)) return false;
    }
    return true;
  }
};

static bool run(const fs::path& assets, const fs::path& art, const fs::path& captures, Game edition, int w, int h, bool wide) {
  Vfs vfs;
  if (!vfs.mountGame(assets, nullptr) || (!art.empty() && !vfs.mountPresentation(art))) return false;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surface) return false;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  if (!renderer) return false;
  Gfx g(renderer.get(), vfs);
  Renderer boardRenderer(renderer.get(), vfs);
  PlayerInfo player;
  player.name = "Render Test";
  App::Config cfg;
  cfg.game = edition;
  cfg.noSave = cfg.noAudio = true;
  cfg.userDir = fs::current_path() / "render-test-profile";
  cfg.seed = 1;
  cfg.widescreen = static_cast<int>(wide ? ui::WidescreenMode::W16x9 : ui::WidescreenMode::Classic4x3);
  App app(g, vfs, nullptr, &boardRenderer, cfg);
  app.curProfile = &player;
  app.gameMode = GameMode::Adventure;
  app.curStage = app.curLevel = 0;
  app.showBoard(true, true);
  auto* board = dynamic_cast<GameScreen*>(app.screen());
  if (!board || !board->ok()) return false;
  std::string tag = (edition == Game::Nights ? "nights-" : "deluxe-") + std::to_string(w) + "x" + std::to_string(h) +
                    (wide ? "-wide" : "-classic") + (art.empty() ? "-pc" : "-art");
  bool ok = true;
  auto check = [&](bool pass, const char* msg) { if (!pass) { std::cerr << tag << ": " << msg << '\n'; ok = false; } };
  auto snapshot = [&]() {
    auto* p = static_cast<const Uint8*>(surface->pixels);
    return Pixels{{p, p + (size_t)surface->pitch * h}, surface->pitch / 4};
  };
  auto frame = [&](const char* name = nullptr) {
    app.frame();
    SDL_RenderPresent(renderer.get());
    check(SDL_GetRenderTarget(renderer.get()) == nullptr, "frame left a texture bound");
    check(!SDL_RenderClipEnabled(renderer.get()), "frame left a clip bound");
    if (name && !captures.empty()) check(SDL_SaveBMP(surface.get(), (captures / (tag + "-" + name + ".bmp")).string().c_str()), "could not save capture");
    return snapshot();
  };
  auto tick = [&](int count = 35) { for (int i = 0; i < count; ++i) app.tick(); };
  tick();
  if (auto* tip = app.getDialog(D_Tip)) { app.dialogButton(tip->dialogId, ID_YES); tick(); }
  check(!board->paused(), "fixture board stayed paused");
  auto active = frame("aim");
  if (wide && !art.empty() && w == 1920 && h == 1080) {
    const V2 mid = boardRenderer.toBoard(985.5f, 541.5f);
    check(std::abs(mid.x - 325) < 1.5f && std::abs(mid.y - 260) < 1.5f, "stock 16:9 board is not at the measured PS3 scale and position");
  }
  auto crop = [&](float x, float y, float cw, float ch) {
    return SDL_Rect{int(g.offX() + x * g.scale()), int(g.offY() + y * g.scale()), int(cw * g.scale()), int(ch * g.scale())};
  };
  const auto view = boardRenderer.consoleView(g.canvasW(), 650);
  const auto detailCrop = view.k < 1 ? crop(80, 157, 80, 340) : wide ? crop(138, 157, 66, 340) : crop(5, 157, 66, 340);
  const auto trimCrop = crop(70, 180, 42, 210);
  if (wide && !art.empty() && h == 1080) {
    const double leftScenery = active.detail(crop(8, 150, 45, 210));
    const double rightScenery = active.detail(crop(1011, 167, 47, 233));
    std::cout << tag << " gutter detail " << leftScenery << ' ' << rightScenery << '\n';
    check(leftScenery > .01 && leftScenery < .032, "active left scenery should be soft without being flat");
    check(rightScenery > .025 && rightScenery < .09, "active right scenery should be soft without being flat");
  }
  const auto center = V2{g.offX() + g.canvasW() * g.scale() / 2, g.offY() + 340 * g.scale()};
  const auto point = boardRenderer.toBoard(center.x, center.y);
  app.doOptionsDialog();
  auto* pause = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
  if (!pause) return false;
  const auto entry = frame("entry");
  tick();
  check(board->paused(), "Pause did not freeze board");
  const auto paused = frame("pause");
  const auto pausedPoint = boardRenderer.toBoard(center.x, center.y);
  check(std::abs(point.x - pausedPoint.x) < 0.01f && std::abs(point.y - pausedPoint.y) < 0.01f, "pause moved board-space transform");
  const double activeDetail = active.detail(detailCrop);
  const double pauseRatio = paused.detail(detailCrop) / activeDetail;
  std::cout << tag << " rail ratio " << pauseRatio << '\n';
  check(activeDetail > .025, "rail crop has no active detail");
  if (wide) {
    check(pauseRatio < 0.75 && pauseRatio > 0.15, "Pause should soften the rail, not just tint or erase it");
    check(entry.detail(detailCrop) > paused.detail(detailCrop), "entry should not snap immediately to full softness");
    if (!art.empty()) check(paused.detail(trimCrop) < active.detail(trimCrop) * .75, "Pause did not soften the exterior trim");
    if (g.offY() > 3) check(paused.same(active, {0, 0, w, int(g.offY()) - 2}), "Pause changed letterbox bars");
    auto* visualsButton = pause->visuals;
    check(visualsButton && visualsButton->visible, "Enhanced Visuals unavailable while paused");
    if (visualsButton) {
      pause->onButton(visualsButton->id);
      tick();
      if (auto* visuals = app.getDialog(D_Visuals)) {
        auto nested = frame("nested");
        check(nested.detail(detailCrop) < activeDetail * .70, "nested dialog restored sharp gameplay");
        app.dialogButton(visuals->dialogId, ID_YES);
        tick();
      } else check(false, "Enhanced Visuals did not open");
    }
  } else {
    check(pauseRatio > 0.85, "Classic pause softened the HUD");
  }
  app.finishOptionsDialog(true, true);
  frame("exit");
  tick();
  const auto resumed = frame("resumed");
  check(!app.getDialog(D_Options) && !board->paused(), "Pause did not resume the same game");
  check(active.same(resumed, detailCrop), "resume did not restore exact rail pixels");
  Input fire;
  fire.button = 1;
  fire.x = g.canvasW() / 2 + 40;
  fire.y = 270;
  for (auto type : {Input::MouseMove, Input::MouseDown, Input::MouseUp}) {
    fire.type = type;
    app.input(fire);
  }
  tick(14);
  check(board->board().phase == Phase::Shot && !board->board().balls.empty(), "shot fixture did not launch through board input");
  frame("shot");
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: gameplay_render_test deluxe nights presentation [capture-dir]\n";
    return 2;
  }
  const fs::path art = argv[3], captures = argc > 4 ? fs::path(argv[4]) : fs::path{};
  bool ok = true;
  for (auto edition : {Game::Deluxe, Game::Nights}) {
    const fs::path assets = argv[edition == Game::Deluxe ? 1 : 2];
    ok &= run(assets, art, captures, edition, 1920, 1080, true);
    ok &= run(assets, art, captures, edition, 1572, 1067, true);
    ok &= run(assets, {}, captures, edition, 1920, 1080, true);
    ok &= run(assets, art, captures, edition, 1920, 1080, false);
  }
  return ok ? 0 : 1;
}
