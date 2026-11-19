#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "ui/game_screen.h"

using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;

static void require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}

static std::string file(int i) { return "PickerFixture_Expedition_Seven_Faraway_Valleys_Route_" + std::to_string(i + 1); }
static std::string title(int i) {
  return i == 7 ? "Finale" : "The Expedition to the Seven Faraway Valleys of the North - " + std::to_string(i + 1);
}
static const std::string stageName = "The Expedition to the Seven Faraway Valleys of the North";

static void fixture(const fs::path& source, const fs::path& dir, bool shortFirst) {
  require(fs::create_directory(dir), "fixture directory already exists");
  fs::copy_file(source / "main.pak", dir / "main.pak");
  Vfs v;
  StageMgr stages;
  require(v.mountGame(dir, nullptr) && stages.load(v), "invalid private base");
  std::vector<uint8_t> header{0xc0, 0x4a, 0xc0, 0xba, 0, 0, 0, 0}, data;
  auto entry = [&](const std::string& name, const std::vector<uint8_t>& bytes) {
    header.push_back(0); header.push_back((uint8_t)name.size());
    header.insert(header.end(), name.begin(), name.end());
    for (int k = 0; k < 4; ++k) header.push_back((uint8_t)(bytes.size() >> (8 * k)));
    header.insert(header.end(), 8, 0);
    data.insert(data.end(), bytes.begin(), bytes.end());
  };
  std::string cfg = "Stage {\r\nName: \"" + stageName + "\"\r\n";
  std::string finale = "Stage {\r\nName: Finale\r\n";
  for (int i = 0; i < 8; ++i) {
    (i < 7 ? cfg : finale) += "Level: " + file(i) + ", " + (i == 7 ? title(i) : "\"" + title(i) + "\"") + ", " + std::to_string(10000 + i) + "\r\n";
    const auto* st = stages.stage(i / 5);
    const auto* lv = st ? st->level(i % 5) : nullptr;
    require(lv != nullptr, "missing stock geometry");
    const auto dat = v.read("levels/" + lv->file + ".dat");
    require(dat.has_value(), "missing stock DAT");
    entry("levels/" + file(i) + ".dat", dat->bytes);
  }
  cfg += "}\r\n";
  finale += "}\r\n";
  cfg = shortFirst ? finale + cfg : cfg + finale;
  entry("picker.cfg", {cfg.begin(), cfg.end()});
  entry("levels/cached_thumbnails/" + file(5) + ".tga", {0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,24,32,0,0,255});
  header.push_back(0x80);
  header.insert(header.end(), data.begin(), data.end());
  for (auto& b : header) b ^= 0xf7;
  fs::create_directory(dir / "levelpacks");
  std::ofstream out(dir / "levelpacks/picker.pak", std::ios::binary);
  out.write((const char*)header.data(), (std::streamsize)header.size());
  out.close();
  require(out.good(), "cannot write fixture");
}

struct Captions : FontProvider {
  explicit Captions(Gfx& g) : native(makeDefaultFontProvider(g.renderer(), &g)) {}
  std::unique_ptr<FontProvider> native;
  struct Line { std::string text; float left, right; };
  std::vector<Line> tiles, info;
  std::string hint;
  float height(const std::string& f) override { return native->height(f); }
  float ascent(const std::string& f) override { return native->ascent(f); }
  float ascentPadding(const std::string& f) override { return native->ascentPadding(f); }
  float lineSpacing(const std::string& f) override { return native->lineSpacing(f); }
  float width(const std::string& f, const std::string& s) override { return native->width(f, s); }
  void draw(const std::string& f, float x, float y, const std::string& s, uint32_t rgb, float a) override {
    if (f == "OVERLOAD16OUTLINE2" && y < 500) tiles.push_back({s, x, x + width(f, s)});
    if (f == "OVERLOAD16OUTLINE" && y == 365) info.push_back({s, x, x + width(f, s)});
    if (f == "PLAIN" && y >= 415 && y < 495) hint += s + " ";
    native->draw(f, x, y, s, rgb, a);
  }
};

static bool probe(Game edition, const fs::path& dir, const fs::path& art, bool wide) {
  const bool shortFirst = edition == Game::Nights;
  const int w = wide ? 1920 : 800, h = wide ? 1080 : 600;
  const std::string tag = std::string(edition == Game::Nights ? "nights-" : "deluxe-") + std::to_string(w);
  Vfs v;
  require(v.mountGame(dir, nullptr) && v.mountPresentation(art), "private mount failed");
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  require(surface != nullptr, SDL_GetError());
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  require(renderer != nullptr, SDL_GetError());
  Gfx gfx(renderer.get(), v);
  auto recorder = std::make_unique<Captions>(gfx);
  auto& text = *recorder;
  gfx.setFontProvider(std::move(recorder));
  Renderer boardRenderer(renderer.get(), v);
  std::set<std::string> shownTiles, shownHover;
  std::set<uint64_t> boards;
  bool ok = true;
  auto check = [&](bool pass, const std::string& what) { if (!pass) { std::cerr << tag << ": " << what << '\n'; ok = false; } };
  for (int i = -1; i < 8; ++i) {
    PlayerInfo player;
    player.maxLevel = 3;
    player.name = "Picker fixture";
    player.maxStage = player.levelSelectStage = 1;
    App::Config cfg;
    cfg.game = edition; cfg.userDir = dir.parent_path() / (tag + "-user");
    cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = cfg.fastBoot = true;
    cfg.widescreen = int(wide ? pp::ui::WidescreenMode::W16x9 : pp::ui::WidescreenMode::Classic4x3);
    cfg.seed = 1;
    App app(gfx, v, nullptr, &boardRenderer, cfg);
    app.curProfile = &player;
    app.gameMode = GameMode::QuickPlay;
    const int first = app.stages.numStages() + (shortFirst ? 1 : 0);
    const auto* st = app.stages.stage(first);
    require(st && st->levels.size() == 7 && st->name == stageName && st->packLabel == "levelpacks/picker.pak" && app.stages.numAllStages() == app.stages.numStages() + 2, "expected one seven-level stage and one single-level stage");
    auto frame = [&](const std::string& name = {}) {
      text.tiles.clear(); text.info.clear(); text.hint.clear(); app.frame(); SDL_RenderPresent(renderer.get());
      if (const char* p = SDL_getenv("PP_PICKER_CAPTURE"); p && !name.empty())
        require(SDL_SaveBMP(surface.get(), (fs::path(p) / (tag + '-' + name + ".bmp")).string().c_str()), SDL_GetError());
    };
    auto settle = [&] { for (int t = 0; t < 25; ++t) app.tick(); frame(); };
    auto mouse = [&](Input::Type type, float x, float y) {
      Input in{}; in.type = type; in.button = 1; in.clicks = 1;
      in.wx = std::round(gfx.offX() + gfx.scale() * x); in.wy = std::round(gfx.offY() + gfx.scale() * y);
      gfx.toCanvas(in.wx, in.wy, in.x, in.y); app.input(in);
    };
    auto click = [&](float x, float y) { for (auto t : {Input::MouseMove, Input::MouseDown, Input::MouseUp}) mouse(t,x,y); };
    auto key = [&](SDL_Keycode k) { Input in{}; in.key = k; in.type = Input::KeyDown; app.input(in); };
    if (i < 0) {
      app.showLevelScreen(false); settle(); frame("stock");
      require(text.tiles.size() == 5 && text.tiles.front().text == app.stages.stage(1)->levels.front().title, "stock tile caption changed");
      check(text.hint.find("Adventure Mode!") != std::string::npos, "stock unlock hint missing");
      mouse(Input::MouseMove, 140, 175); frame("stock-hover");
      require(text.info.size() == 1 && text.info[0].text == text.tiles.front().text, "stock hover changed");
      auto* stock = app.screen();
      click(664,420); require(app.screen() == stock && !app.anyDialog(), "locked stock level accepted");
      click(139,180);
      auto* game = dynamic_cast<GameScreen*>(app.screen());
      require(game && game->ok() && app.curStage == 1 && app.curLevel == 0 && game->bundle().name == app.stages.stage(1)->level(0)->file, "wrong stock board");
      continue;
    }
    if (wide && !i) {
      app.showMainMenu(); settle();
      click(800, 105); settle(); click(850, 345);
    } else { app.packStage = first; app.showLevelScreen(false); }
    settle();
    auto* picker = dynamic_cast<LevelScreen*>(app.screen());
    if (shortFirst && wide && !i && picker) { click(450,567); frame(); }
    require(picker && picker->stage == first && !app.hasModal(), tag + ": custom picker not open");
    constexpr float centers[]{139,401,665,138,664};
    auto bounded = [&](const Captions::Line& line, float cx, const std::string& label) {
      check(!line.text.empty() && line.left >= cx-100 && line.right <= cx+100, label + " outside panel: " + line.text);
    };
    if (!i) {
      mouse(Input::MouseMove,400,300); settle(); frame("pack");
      require(text.info.size() == 1 && text.tiles.size() == 5, "missing page captions");
      check(text.hint.find("this pack") != std::string::npos && text.hint.find("Adventure") == std::string::npos, "custom level picker shows Adventure unlock instructions");
      bounded(text.info.front(), 400, "stage");
      for (int j = 0; j < 5; ++j) bounded(text.tiles[j], centers[j], "tile");
      uint8_t r,g,b,a;
      require(SDL_ReadSurfacePixel(surface.get(), int(gfx.offX()+gfx.scale()*140), int(gfx.offY()+gfx.scale()*180), &r,&g,&b,&a) && r == 48 && g == 48 && b == 96, "absent thumbnail did not render native fallback");
      check(picker->bBack->visible == shortFirst && picker->bNext->visible && !picker->bRandom->visible, "custom first page controls");
    }
    if (i >= 5) {
      click(450,567); frame();
      if (picker->stage != first || text.tiles.size() != 2) { check(false, "Next skipped levels 6 and 7"); continue; }
      if (i == 5) {
        frame("page-two");
        uint8_t r,g,b,a;
        require(SDL_ReadSurfacePixel(surface.get(), int(gfx.offX()+gfx.scale()*140), int(gfx.offY()+gfx.scale()*180), &r,&g,&b,&a) && r > 100 && !g && !b, "page two did not draw level six's thumbnail");
        click(665,175);
        require(app.screen() == picker && !app.anyDialog(), "empty tile activated a board");
        key(SDLK_LEFT); frame(); check(picker->stage == first && text.tiles.size() == 5, "Left skipped first page");
        key(SDLK_RIGHT); frame(); check(picker->stage == first && text.tiles.size() == 2, "Right skipped second page");
      }
      check(picker->bNext->visible != shortFirst && picker->bBack->visible, "wrong final page arrows");
      if (shortFirst) {
        key(SDLK_RIGHT); frame(); require(picker->stage == first && text.tiles.size() == 2, "Right escaped the final page");
        if (i == 7) {
          click(340,567); click(340,567); frame();
          require(picker->stage == first-1 && text.tiles.size() == 1 && text.tiles[0].text == "Finale" && !picker->bBack->visible, "Back missed preceding stage");
        }
      } else if (i >= 6) {
        click(450,567); frame();
        require(picker->stage == first+1 && text.tiles.size() == 1 && text.tiles[0].text == "Finale" && !picker->bNext->visible, "next stage or end control wrong");
        if (i == 6) { click(340,567); frame(); check(picker->stage == first && text.tiles.size() == 2, "Back did not return to last page"); }
      }
    }
    const int slot = i == 7 ? 0 : i % 5;
    mouse(Input::MouseMove, centers[slot], slot < 3 ? 180.f : 420.f);
    frame(i == 0 ? "hover-one" : i == 6 ? "hover-seven" : i == 7 ? "short-hover" : "");
    require(text.tiles.size() > (size_t)slot && text.info.size() == 1, "missing hovered captions");
    bounded(text.tiles[slot], centers[slot], "tile"); bounded(text.info.front(), 400, "hover");
    check(shownTiles.insert(text.tiles[slot].text).second && shownHover.insert(text.info.front().text).second, "ambiguous displayed level choice");
    const auto* expected = app.stages.levelByName(file(i));
    require(expected && expected->index == (i == 7 ? 0 : i) && expected->title == title(i), "level identity changed");
    const auto* donor = app.stages.stage(i/5)->level(i%5);
    const auto dat = v.read("levels/" + donor->file + ".dat"), base = v.read("levels/_base.dat");
    const int width = 650;
    const auto hash = fnv1a(&width, sizeof width, fnv1a(dat->bytes.data(),dat->bytes.size(),fnv1a(base->bytes.data(),base->bytes.size())));
    click(centers[slot], slot < 3 ? 180.f : 420.f);
    auto* game = dynamic_cast<GameScreen*>(app.screen());
    require(game && game->ok() && app.curLevelInfo() == expected && app.curStage == expected->stage && app.curLevel == expected->index &&
            game->bundle().name == file(i) && game->bundle().title == title(i) && game->bundle().source == st->packLabel && game->bundle().contentHash == hash,
            "click opened wrong board for " + file(i));
    if (i == 6) {
      settle();
      const auto* dialog = app.getDialog(D_Character);
      require(dialog && dialog->yesBtn, "missing character confirmation");
      const auto* play = dialog->yesBtn;
      click((gfx.canvasW()-800)*0.5f + dialog->x + play->x + play->w/2, dialog->y + play->y + play->h/2);
      settle(); frame("board-seven");
      require(!app.hasModal() && app.screen() == game && game->bundle().contentHash == hash, "selected board did not enter play");
    }
    check(boards.insert(hash).second && player.levelSelectStage == 1 && player.maxStage == 1, "duplicate board or overwritten stock index");
    std::cout << tag << " selected " << file(i) << " stage=" << app.curStage << " level=" << app.curLevel << " hash=" << hash << '\n';
  }
  return ok;
}

int main(int argc, char** argv) {
  if (argc != 5 || !SDL_Init(0)) return 2;
  bool ok = true;
  try {
    fs::create_directories(argv[4]);
    const auto root = fs::path(argv[4]) / ("picker-" + std::to_string(SDL_GetPerformanceCounter()));
    require(fs::create_directory(root), "need new private fixture directory");
    for (auto game : {Game::Deluxe, Game::Nights}) {
      const auto dir = root / (game == Game::Deluxe ? "Deluxe" : "Nights");
      fixture(argv[game == Game::Deluxe ? 1 : 2], dir, game == Game::Nights);
      for (bool wide : {false,true}) ok = probe(game,dir,argv[3],wide) && ok;
    }
    fs::remove_all(root);
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; ok = false; }
  SDL_Quit();
  return ok ? 0 : 1;
}
