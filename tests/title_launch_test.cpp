#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "gamepaths.h"
#include "ui/screens.h"

using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;

static bool probe(Game edition, const fs::path& dir, const fs::path& art, const fs::path& scratch, int width, int height) {
  const bool classic = width == 800;
  const std::string name = std::string(edition == Game::Nights ? "nights-" : "deluxe-") + std::to_string(width);
  bool ok = true;
  auto check = [&](bool pass, const char* what) {
    if (!pass) { ok = false; std::cerr << name << ": " << what << '\n'; }
    return pass;
  };
  Vfs vfs;
  if (!check(classifyGameDir(dir) == edition && vfs.mountGame(dir, nullptr), "wrong edition or missing data") ||
      !check(vfs.mountPresentation(art), "missing presentation")) return false;
  for (auto file : {"home", "home_front", "logo", "sun_glow", "title_sun", "title_plate", "title_buttons", "loader_lights"})
    if (!check(vfs.exists(std::string("console/menu/") + file + ".png"), file)) return false;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surface) return false;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  if (!renderer) return false;
  Gfx gfx(renderer.get(), vfs);
  PlayerInfo player;
  player.name = "Title fixture";
  App::Config cfg;
  cfg.game = edition;
  cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = true;
  cfg.fastBoot = width < 1900;
  cfg.userDir = scratch / name;
  using Mode = pp::ui::WidescreenMode;
  cfg.widescreen = static_cast<int>(classic ? Mode::Classic4x3 : width >= 1900 ? Mode::Auto : Mode::W16x9);
  cfg.seed = 1;
  App app(gfx, vfs, nullptr, nullptr, cfg);
  app.curProfile = &player;
  app.showLoadingScreen();
  auto* title = dynamic_cast<LoadingScreen*>(app.screen());
  if (!check(title && !title->resList.empty() && app.game == edition, "no edition load queue")) return false;
  auto frame = [&](const char* phase) {
    app.frame(); SDL_RenderPresent(renderer.get());
    if (auto* p = SDL_getenv("PP_TITLE_CAPTURE")) check(SDL_SaveBMP(surface.get(), (fs::path(p) / (name + '-' + phase + ".bmp")).string().c_str()), "capture failed");
  };
  auto pixel = [&](int refX, int refY) {
    SDL_Color c{};
    SDL_ReadSurfacePixel(surface.get(), int(gfx.offX() + gfx.scale() * refX * (1067.f / 1455)),
                         int(gfx.offY() + gfx.scale() * refY * (600.f / 818)), &c.r, &c.g, &c.b, &c.a);
    return c;
  };
  auto click = [&](Input::Type type, int rx, int ry, int button = 1) {
    Input in{}; in.type = type; in.button = button;
    in.wx = gfx.offX() + gfx.scale() * rx * (1067.f / 1455);
    in.wy = gfx.offY() + gfx.scale() * ry * (600.f / 818);
    gfx.toCanvas(in.wx, in.wy, in.x, in.y); app.input(in);
  };
  auto key = [&](SDL_Keycode k) { Input in{}; in.type = Input::KeyDown; in.key = k; app.input(in); };
  frame("empty");
  if (!classic) {
    const auto sky = pixel(100, 80), logo = pixel(724, 310), rim = pixel(610, 571);
    check(sky.r > 75 && sky.b > 155 && sky.g < 108 && logo.r > 135 && logo.g < 80 && rim.r > 175 && rim.g > 145,
          "first widescreen frame lacks the daylight scene, Peggle logo or load tray");
    check(!title->loaded && !title->playBtn->visible, "starts active");
    key(SDLK_RETURN); click(Input::MouseDown, 610, 617);
    if (!check(app.screen() == title && !app.anyDialog(), "input started during preload")) return false;
  }
  auto checkFill = [&](float progress) {
    const int expected = static_cast<int>(25 * progress);
    for (int i = 0; i < 25; ++i) {
      const int mid = static_cast<int>(543 + 17.0f * i);
      int lit = 0;
      for (int x = mid - 4; x <= mid + 5; ++x) {
        auto c = pixel(x, 626);
        lit += c.r > 160 && c.g > 60 && c.b < 90;
      }
      if (!check((lit >= 3) == (i < expected), "lit cells differ from displayed load progress")) break;
    }
  };
  int phase = 0;
  if (!classic) checkFill(0);
  for (int i = 0; i < 1800 && !title->loaded; ++i) {
    app.tick();
    const float progress = title->nights ? title->nights->progress() : title->loadProgress;
    constexpr float checkpoints[]{.24f, .48f, .84f};
    if (classic || phase == 3 || progress < checkpoints[phase]) continue;
    frame(phase == 0 ? "quarter" : phase == 1 ? "partial" : "late");
    checkFill(progress);
    check(!title->playBtn->visible && !title->loaded, "partial frame is interactive");
    ++phase;
  }
  if (!check(title->loaded && title->playBtn->visible && title->nextRes == title->resList.size(), "ready before preload or never ready")) return false;
  frame("ready");
  if (classic) return ok && title->canvasWidth() == 800;
  check(phase == 3 && title->canvasWidth() == 1067, "never saw partial widescreen load");
  int mark = 0, hill = 0;
  for (int y = 418; y < 431; ++y) for (int x = 957; x < 978; ++x) {
    const auto c = pixel(x, y);
    if (std::max({c.r, c.g, c.b}) < 100) ++mark;
  }
  for (int y = 481; y < 490; ++y) for (int x = 721; x < 730; ++x) hill += pixel(x, y).g;
  check(mark >= 33 && mark <= 82, "black trademark missing beneath the e");
  check(hill / 81 >= 78 && hill / 81 <= 104, "hill beneath the logo does not carry its shadow");
  auto sky = pixel(100, 80), sun = pixel(690, 190), logo = pixel(724, 310), gold = pixel(610, 571), face = pixel(605, 630);
  check(sky.r > 75 && sky.b > 155 && sky.g < 108 && sun.r > 218 && sun.g > 160 && logo.r > 135 && logo.g < 80, "shared daylight title layers missing");
  check(gold.r > 175 && gold.g > 145 && gold.b < 140 && face.g > 225 && face.r < 130 && face.b < 50, "original gold plate / green face missing");
  auto ink = [&](bool legal) {
    int xmin = 2000, xmax = -1, ymin = 1000, ymax = -1;
    for (int y = legal ? 684 : 595; y <= (legal ? 711 : 641); ++y) for (int x = legal ? 422 : 596; x <= (legal ? 1070 : 898); ++x) {
      const auto c = pixel(x,y);
      if (!(legal ? c.r > 185 && c.g > 182 && c.r - c.b > 45 : c.r > 190 && c.g > 222 && c.b > 165 && c.r + 35 > c.g)) continue;
      xmin = std::min(xmin,x); xmax = std::max(xmax,x); ymin = std::min(ymin,y); ymax = std::max(ymax,y);
    }
    return legal ? std::abs(xmin-438)<=8 && std::abs(xmax-1058)<=7 && std::abs(ymin-689)<=3 && std::abs(ymax-708)<=3
                 : std::abs(xmin-653)<=5 && std::abs(xmax-839)<=5 && std::abs(ymin-604)<=3 && std::abs(ymax-629)<=3;
  };
  check(ink(false), "Press Start ink outside reference bounds");
  check(ink(true), "copyright ink outside reference bounds");
  if (gfx.offY() > 2) { SDL_Color c{}; SDL_ReadSurfacePixel(surface.get(), width / 2, 1, &c.r, &c.g, &c.b, &c.a); check(!c.r && !c.g && !c.b, "letterbox painted"); }
  click(Input::MouseUp, 610, 617);
  click(Input::MouseDown, 998, 617); click(Input::MouseUp, 998, 617);
  if (!check(app.screen() == title && !app.anyDialog(), "orphan/outside release activated title")) return false;
  click(Input::MouseDown, 610, 617);
  if (!check(app.screen() == title && title->capture == title->playBtn, "press did not capture plate")) return false;
  frame("pressed");
  face = pixel(605, 630); check(face.r > 200 && face.g > 100 && face.b < 70, "pressed face missing");
  click(Input::MouseUp, 610, 617, 3);
  if (!check(app.screen() == title && title->capture == title->playBtn, "unrelated release activated or cancelled title press")) return false;
  click(Input::MouseUp, 998, 617);
  if (!check(app.screen() == title && !title->capture, "release outside activated title")) return false;
  click(Input::MouseDown, 610, 617);
  Input focus{}; focus.type = Input::FocusLost; app.input(focus);
  focus.type = Input::FocusGained; app.input(focus);
  click(Input::MouseUp, 610, 617);
  if (!check(app.screen() == title && !title->capture, "stale press survived window focus loss")) return false;
  if (width == 1455) { click(Input::MouseDown, 979, 622); click(Input::MouseUp, 979, 622); }
  else key(edition == Game::Deluxe ? SDLK_RETURN : SDLK_SPACE);
  check(dynamic_cast<MainMenu*>(app.screen()) && !app.anyDialog() && app.game == edition, "confirmed title did not enter edition menu");
  if (ok) std::cout << name << ": passed\n";
  return ok;
}

int main(int argc, char** argv) {
  if (argc != 5 || !SDL_Init(0)) return 2;
  bool ok = true;
  for (auto e : {Game::Deluxe, Game::Nights}) for (auto size : {std::pair{1920,1080}, std::pair{1900,1057}, std::pair{1455,818}, std::pair{1280,900}, std::pair{800,600}})
    ok = probe(e, argv[e == Game::Deluxe ? 1 : 2], argv[3], argv[4], size.first, size.second) && ok;
  SDL_Quit();
  return ok ? 0 : 1;
}
