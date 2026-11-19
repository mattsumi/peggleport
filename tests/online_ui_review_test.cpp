#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <thread>
#include "online/discovery.h"
#include "online/online_ui.h"
#include "online/steam.h"
#include "ui/screens.h"

#define private public
#include "online/online_ui.cpp"
#undef private

using namespace pp;
using namespace pp::ui;
using namespace pp::ui::online;
namespace fs = std::filesystem;

static int failures;
static void check(bool ok, const std::string& what) {
  if (!ok) { std::cerr << "FAIL " << what << '\n'; ++failures; }
}

struct Probe : OnlineScreen {
  using OnlineScreen::OnlineScreen;
  int action = 0;
  void onButton(int id) override { action = id; }
};

static void hostControls(App& app) {
  Probe host(app, "", false, Flow::Host);
  host.placeWidgets(float(app.gfx.canvasW() - 800));
  for (int id : {B_LvlNext, B_LvlPrev, B_MasNext, B_MasPrev, B_Destination, B_Go}) {
    Widget* w = host.findWidget(id);
    check(w->visible, "host control visible");
    app.mouseX = w->x + w->w / 2; app.mouseY = w->y + w->h / 2;
    host.keyDown(SDLK_RETURN, 0);
    check(host.action == B_Go, "Enter keeps its primary Host action");
    host.action = 0;
    host.keyDown(host.padKey(SDL_GAMEPAD_BUTTON_SOUTH), 0);
    check(host.action == id, "South over control " + std::to_string(id) + " activated " + std::to_string(host.action));
  }
  app.mouseX = app.mouseY = 0;
  for (auto [button, expected] : {std::pair{SDL_GAMEPAD_BUTTON_SOUTH, B_Go}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, B_LvlPrev},
                                  {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, B_LvlNext}, {SDL_GAMEPAD_BUTTON_DPAD_UP, B_MasPrev},
                                  {SDL_GAMEPAD_BUTTON_DPAD_DOWN, B_MasNext}}) {
    host.action = 0;
    host.keyDown(host.padKey(button), 0);
    check(host.action == expected, "parked host pad action " + std::to_string(expected));
  }
}

static std::vector<uint32_t> pixels(SDL_Surface* surface, int x, int width, int top = 529, int bottom = 594) {
  std::vector<uint32_t> result;
  for (int y = top; y < bottom; ++y) for (int i = x; i < x + width; ++i) {
    uint8_t r, g, b, a;
    if (!SDL_ReadSurfacePixel(surface, i, y, &r, &g, &b, &a)) throw std::runtime_error(SDL_GetError());
    result.push_back((r << 16) | (g << 8) | b);
  }
  return result;
}

static void footer(App& app, SDL_Surface* surface) {
  Gfx& g = app.gfx;
  float quitX = 0, end = 0;
  uint64_t enabledInk = 0;
  std::vector<uint32_t> enabledQuit;
  for (bool enabled : {true, false}) {
    Probe screen(app, "", false, Flow::Host);
    g.clear(0);
    float x = screen.prompt(g, 66, 0, "ENTER", "Start Game", enabled ? SDLK_RETURN : 0);
    float next = screen.prompt(g, x, 1, "ESC", "Quit", SDLK_ESCAPE);
    SDL_RenderPresent(g.renderer());
    auto quit = pixels(surface, int(x), int(next - x));
    uint64_t ink = 0;
    for (auto rgb : pixels(surface, 66, int(x - 66))) ink += (rgb >> 16) + ((rgb >> 8) & 255) + (rgb & 255);
    if (enabled) { quitX = x; end = next; enabledInk = ink; enabledQuit = quit; continue; }
    std::cout << "Start RGB sum enabled=" << enabledInk << " disabled=" << ink << " Quit=[" << x << ',' << next << ")\n";
    check(ink > 0 && ink < enabledInk * 9 / 10, "disabled Start is visible and dim");
    check(x == quitX && next == end && quit == enabledQuit, "Quit geometry and pixels unchanged");
    Input click; click.type = Input::MouseDown; click.button = 1; click.y = 565;
    for (float at : {66.f, x - 0.25f, x, next - 0.25f}) {
      click.x = at; screen.action = 0; screen.input(click);
      check(screen.action == (at < x ? 0 : B_Cancel), "disabled Start and both edges of Quit hitbox");
    }
  }
}

static void searchBars(App& app, SDL_Surface* surface) {
  auto& g = app.gfx;
  OnlineScreen quick(app, "", false, Flow::Quick, true), custom(app, "", false, Flow::Custom, true);
  custom.browse_.discovering = true;
  Tex* plate = quick.menuTex(g, "title_plate");
  const bool animated = app.vfs.exists("console/menu/title_plate.png") && app.vfs.exists("console/menu/loader_lights.png");
  check(bool(plate) == app.vfs.exists("console/menu/title_plate.png"), "decode search plate");
  constexpr int x = 367, width = 360;
  constexpr int centers[] = {33,47,61,75,89,102,116,130,144,158,172,186,199,213,227,241,255,269,283,296,310,324,338,352};
  for (int tick = 0; tick <= 100; tick += 4) {
    for (bool plural : {false, true}) {
      auto& screen = plural ? custom : quick;
      while (screen.updateCount < tick) screen.Panel::update();
      const int y = plural ? 278 : 228;
      const auto tag = std::string(plural ? "custom" : "quick") + " tick " + std::to_string(tick);
      g.clear(0); screen.draw(g); SDL_RenderPresent(g.renderer());
      const auto actual = pixels(surface, x, width, y, y + 90);
      const auto caption = pixels(surface, x, width, y + 91, y + 130);
      g.clear(0); screen.drawWaitScene(g, g.canvasW());
      if (plate) g.drawStretch(plate, x, y, width, 90);
      else secondaryPanel(g, x, y + 18, width, 53);
      screen.consoleText(g, "OVERLOAD24OUTLINE", x + 180, y + 112, plural ? "SEARCHING FOR GAMES" : "SEARCHING FOR GAME", 0xffffff);
      SDL_RenderPresent(g.renderer());
      check(caption == pixels(surface, x, width, y + 91, y + 130), tag + " search caption");
      const auto dark = pixels(surface, x, width, y, y + 90);
      if (!animated) {
        check(actual == dark, tag + " unlit fallback");
        continue;
      }
      bool rim = true;
      for (int row = 0; row < 90; ++row) for (int col = 0; col < width; ++col) {
        const int p = row * width + col;
        const auto rgb = dark[p];
        if (row < 23 || row > 67 || col < 24 || col > 333 || ((rgb & 255) > (rgb >> 16) && ((rgb >> 8) & 255) > (rgb >> 16)))
          rim &= actual[p] == rgb;
      }
      check(rim, tag + " unobscured rim");
      auto gain = [&](int col, int row) {
        const int p = int(row * .9f) * width + int(col * .9f);
        return int(actual[p] >> 16) - int(dark[p] >> 16);
      };
      const int head = (tick % 100) * 24 / 100;
      auto trail = [&](int age) { return gain(centers[(head + 24 - age) % 24], 50); };
      check(trail(0) > 140 && trail(2) > 90 && trail(2) < trail(0) && trail(6) > 25 && trail(6) < trail(2)
            && trail(12) == 0 && trail(23) == 0 && gain(365, 50) == 0, tag + " moving crest and wrapped fading trail");
      if (head > 0) {
        const int c = centers[head];
        check(gain(c - 2, 36) > 70 && gain(c + 2, 68) > 75, tag + " full body, not a moving cap");
        check(gain(c - 7, 50) < 65 && gain(c + 7, 50) < 65, tag + " dark grooves flanking lit cell");
      } else {
        check(gain(30, 50) > 90 && gain(30, 36) < 10 && gain(30, 66) < 10, tag + " outward curved end");
      }
    }
  }
}

static void captions(App& app, SDL_Surface* surface, const fs::path& scratch) {
  auto& g = app.gfx;
  auto first = levelChoices(app).at(0);
  auto data = app.vfs.read("levels/" + first.file + ".dat").value();
  const int firstPackStage = app.stages.numAllStages() + 1;
  const std::string prefix = "Community Challenge Collection Volume ";
  for (std::string id : {"Amber", "Violet"}) {
    auto pack = scratch / id, levels = pack / "levels";
    fs::create_directories(levels);
    std::ofstream(levels / "mod_levels.cfg") << "Stage\n{\nName: \"" << prefix << (id == "Amber" ? "One" : "Two")
      << "\"\nLevel: " << id << "Warmup, \"Warmup\", 100000, 3\nLevel: " << id << ", \"Twins\", 100000, 3\n}\n";
    for (auto file : {id + "Warmup", id})
      std::ofstream(levels / (file + ".dat"), std::ios::binary).write(reinterpret_cast<const char*>(data.bytes.data()), data.bytes.size());
    if (!app.vfs.mountMod(pack, nullptr)) throw std::runtime_error("mount test pack");
  }
  if (!app.stages.load(app.vfs)) throw std::runtime_error("load test stages");
  OnlineScreen host(app, "", false, Flow::Host), room(app, "", false, Flow::Host);
  room.sess_ = std::make_unique<Session>();
  room.isHost_ = room.room_ = true;
  const bool sidebar = app.vfs.exists("console/lobby/sidebar.png");
  const std::string waiting = sidebar ? "waiting-art" : "waiting-fallback";
  constexpr float cx = kHostX + kHostW / 2, bottom = kHostY + 187;
  auto hostPixels = [&] { return pixels(surface, int(cx) - 89, 178, int(bottom) - 52, int(bottom) + 2); };
  auto roomPixels = [&] { return sidebar ? pixels(surface, 703, 196, 242, 297) : pixels(surface, 787, 200, 279, 331); };
  std::vector<uint32_t> firstHost, firstRoom;
  int twins = 0;
  for (const auto& l : levelChoices(app)) {
    if (l.file == first.file || l.file == "Amber" || l.file == "Violet") {
      std::string group = "Stage 1-1";
      if (l.file != first.file) {
        const bool one = l.file == "Amber";
        LevelBundle loaded;
        check(loadLevelBundle(app.vfs, l.file, loaded, nullptr) && loaded.source == "mod:" + l.file &&
              !g.img("levels/cached_thumbnails/" + l.file) && l.title == "Twins" && l.group == prefix + (one ? "One" : "Two"),
              "mounted thumbnail-less Twins: " + l.file);
        const std::string number = std::to_string(firstPackStage + (one ? 0 : 1)) + "-2";
        group = fitText(g, "OVERLOAD10OUTLINE", number + " " + l.group, 176);
        check(group.starts_with(number + " Community") && group.ends_with("..."), "fitted pack label for " + l.file);
      }
      g.clear(0); host.drawLayer(g); SDL_RenderPresent(g.renderer());
      auto actual = hostPixels();
      host.drawPreviewWindow(g, l.file, {cx - 92, kHostY + 71, 184, 122});
      g.drawString("OVERLOAD10OUTLINE", cx, bottom - 27, group, 0xffffff, 1, Align::Center);
      g.drawString("OVERLOAD16OUTLINE", cx, bottom, fitText(g, "OVERLOAD16OUTLINE", l.title, 176), 0xffffff, 1, Align::Center);
      SDL_RenderPresent(g.renderer());
      check(actual == hostPixels(), "rendered host caption: " + group + " / " + l.title);
      if (l.file != first.file) {
        room.hostInfo_.level = l.file;
        g.clear(0); room.drawLayer(g); SDL_RenderPresent(g.renderer());
        auto roomActual = roomPixels();
        if (twins++) {
          check(actual != firstHost, "Host: Twins caption pixels must differ between Volume One and Two");
          check(roomActual != firstRoom, waiting + ": Twins caption pixels must differ between Volume One and Two");
        } else {
          firstHost = actual;
          firstRoom = roomActual;
        }
      }
      std::cout << l.file << ": expected " << group << " / " << l.title << '\n';
    }
    host.onButton(B_LvlNext);
  }
  check(twins == 2, "rendered both mounted Twins");
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3 || !SDL_Init(0)) return 2;
  auto scratch = fs::temp_directory_path() / ("pp-online-ui-" + std::to_string(SDL_GetPerformanceCounter()));
  if (!fs::create_directory(scratch)) return 2;
  try {
    Vfs vfs;
    if (!vfs.mountGame(argv[1], nullptr) || (argc == 3 && !vfs.mountPresentation(argv[2]))) throw std::runtime_error("mount test assets");
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(1067, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    Gfx gfx(renderer.get(), vfs); gfx.begin(1067);
    App::Config cfg; cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = true; cfg.userDir = scratch / "userdata";
    PlayerInfo player;
    App app(gfx, vfs, nullptr, nullptr, cfg); app.curProfile = &player; app.pad.pad = Pad::PlayStation;
    searchBars(app, surface.get()); hostControls(app); footer(app, surface.get()); captions(app, surface.get(), scratch);
  } catch (const std::exception& e) { check(false, e.what()); }
  fs::remove_all(scratch); SDL_Quit(); std::cout << "failures=" << failures << '\n'; return failures ? 1 : 0;
}
