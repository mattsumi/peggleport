#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "mods/manager.h"
#include "font.h"
#include "gamepaths.h"
#include "ui/screens.h"
#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/replay.h"
#include "ui/visuals_dialog.h"


using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;
using UiWide = pp::ui::WidescreenMode;

struct StoryTestFont : FontProvider {
  struct Line { float x, bottom; std::string text; };
  struct Draw { std::string font, text; float x, w, y; };
  std::vector<Line> lines;
  std::vector<Draw> draws;
  std::unique_ptr<FontProvider> native;
  FontManager fonts;
  StoryTestFont(SDL_Renderer* r, Gfx& g) : native(makeDefaultFontProvider(r, &g)), fonts(g.vfs()) {}
  float height(const std::string& n) override { return native->height(n); }
  float ascent(const std::string& n) override { return native->ascent(n); }
  float ascentPadding(const std::string& n) override { return native->ascentPadding(n); }
  float lineSpacing(const std::string& n) override { return native->lineSpacing(n); }
  float width(const std::string& n, const std::string& s) override { return native->width(n, s); }
  void draw(const std::string& n, float x, float y, const std::string& s, uint32_t rgb, float alpha) override {
    draws.push_back({n, s, x, native->width(n, s), y});
    if (n == "STAGE" && !s.empty()) {
      float bottom = y;
      if (auto* f = fonts.get("FONT_STAGE")) {
        std::vector<GlyphQuad> quads;
        f->layout(s, 0xffffffff, quads);
        for (const auto& q : quads) if (q.sh > 0) bottom = std::max(bottom, y + q.dy + q.dh);
      }
      lines.push_back({x, bottom, s});
    }
    native->draw(n, x, y, s, rgb, alpha);
  }
};

static void key(App& app, int k) {
  Input in;
  in.type = Input::KeyDown;
  in.key = k;
  app.input(in);
}

static void click(App& app, float wx, float wy) {
  Input in;
  app.gfx.toCanvas(wx, wy, in.x, in.y);
  for (auto type : {Input::MouseMove, Input::MouseDown, Input::MouseUp}) {
    in.type = type;
    app.input(in);
  }
}

static bool probe(const fs::path& gameDir, const fs::path& art, const fs::path& scratch, Game edition, int width, int height, UiWide mode) {
  auto fail = [=](const char* what) {
    std::cerr << (edition == Game::Deluxe ? "Deluxe" : "Nights") << ' ' << width << 'x' << height << ": " << what << '\n';
    return false;
  };
  Vfs vfs;
  if (!vfs.mountGame(gameDir, nullptr)) return fail("game mount failed");
  if (!art.empty()) {
    if (!vfs.mountPresentation(art)) return fail("native artwork did not load");
    vfs.unmountMods();
    const auto hit = vfs.read("console/menu/home.png");
    if (!hit || hit->layer->isMod || !vfs.modSignature().empty()) return fail("native artwork depended on a mod");
  }
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surf(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surf) return fail(SDL_GetError());
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surf.get()), SDL_DestroyRenderer);
  if (!renderer) return fail(SDL_GetError());
  Gfx gfx(renderer.get(), vfs);
  PlayerInfo player;
  player.name = "Preview Player";
  Renderer boardView(renderer.get(), vfs);
  App::Config cfg;
  cfg.game = edition;
  cfg.widescreen = static_cast<int>(mode);
  cfg.userDir = scratch / (edition == Game::Deluxe ? "deluxe" : "nights") / "profile";
  cfg.noSave = cfg.noAudio = true;
  cfg.fastBoot = true;
  cfg.seed = 1;
  App app(gfx, vfs, nullptr, &boardView, cfg);
  if (!art.empty()) app.setGames(true, edition);
  app.curProfile = &player;
  app.showMainMenu();
  for (int i = 0; i < 25; ++i) app.tick();
  bool captureOk = true;
  bool visualBoundsOk = true;
  auto frame = [&](const char* page) {
    app.frame();
    SDL_RenderPresent(renderer.get());
    if (const char* out = page ? SDL_getenv("PP_MENU_CAPTURE") : nullptr) {
      auto dest = fs::path(out) / (std::string(art.empty() ? "pc-art-" : "") + (edition == Game::Deluxe ? "deluxe-" : "nights-") + std::to_string(width) + 'x' + std::to_string(height) + '-' + page + ".bmp");
      if (!SDL_SaveBMP(surf.get(), dest.string().c_str())) captureOk = false;
    }
  };
  auto settle = [&] { for (int i = 0; i < 25; ++i) app.tick(); };
  auto canvasClick = [&](float x, float y) { click(app, gfx.offX() + x * gfx.scale(), gfx.offY() + y * gfx.scale()); };
  auto detail = [&](float x, float y, float w, float h) {
    double edges = 0, total = 0;
    const int x0 = int(gfx.offX() + x * gfx.scale()), y0 = int(gfx.offY() + y * gfx.scale());
    const int x1 = int(gfx.offX() + (x + w) * gfx.scale()), y1 = int(gfx.offY() + (y + h) * gfx.scale());
    auto light = [&](int px, int py) {
      Uint8 r, g, b, a;
      return SDL_ReadSurfacePixel(surf.get(), px, py, &r, &g, &b, &a) ? r + g + b : 0;
    };
    for (int py = y0; py < y1 - 2; py += 2)
      for (int px = x0; px < x1 - 2; px += 2) {
        const int v = light(px, py);
        total += v;
        edges += std::abs(v - light(px + 2, py)) + std::abs(v - light(px, py + 2));
      }
    return edges / std::max(1.0, total);
  };
  auto dialogClick = [&](Dialog& d, const Widget& w) {
    canvasClick((gfx.canvasW() - 800) * 0.5f + d.x + w.x + w.w * 0.5f, d.y + w.y + w.h * 0.5f);
  };
  const bool tour = !art.empty() && width == 1920 && mode == UiWide::Auto;
  const int canvas = widescreenCanvasWidth(mode == UiWide::W16x9 ? mode : UiWide::Auto, width, height);
  const bool wide = mode != UiWide::Classic4x3 && canvas >= 960;
  if (mode == UiWide::W16x9 && app.frameCanvasWidth(800) != 1066)
    return fail("explicit 16:9 did not fit a full 16:9 frame in a narrower window");
  if (tour || mode == UiWide::Classic4x3) {
    app.showLoadingScreen();
    auto* title = static_cast<LoadingScreen*>(app.screen());
    for (int i = 0; i < 1400 && !title->loaded; ++i) app.tick();
    frame("title");
    if (gfx.canvasW() != (wide ? canvas : 800)) return fail("startup used the wrong aspect ratio");
    if (!title->loaded || !title->playBtn->visible) return fail("startup never enabled Start");
    if (wide && edition == Game::Deluxe && vfs.exists("console/menu/title_plate.png")) {
      if (std::abs(title->playBtn->x + title->playBtn->w * .5f - canvas * .5125f) > 1.0f)
        return fail("Deluxe Start control is not aligned to the original title plate");
    }
    canvasClick(title->playBtn->x + title->playBtn->w * .5f, title->playBtn->y + title->playBtn->h * .5f);
    settle();
    if (app.screenName() != "mainmenu") return fail("clicking Start did not reach Home");
  }
  frame(wide ? "home" : "classic");
  if (gfx.canvasW() != (wide ? canvas : 800)) return fail("wrong home canvas (widescreen menus must not require extras)");
  for (const auto& w : app.screen()->widgets)
    if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && (b->label == "DEMO" || b->label == "Demo"))
      return fail("Home still has a visible Demo button");
  if (!wide) {
    auto* menu = dynamic_cast<MainMenu*>(app.screen());
    if (!menu || !menu->bAdv->visible || !menu->bOptions->visible) return fail("classic controls are missing");
    if (mode == UiWide::Classic4x3) {
      app.settings.vis.ultraWideHud = true;
      if (app.frameCanvasWidth(800) != 800) return fail("Ultra-Wide HUD overrode Classic 4:3");
      if (app.frameCanvasWidth(1080) != 1080) return fail("Classic 4:3 narrowed an authored wide board");
    }
    key(app, SDLK_ESCAPE);
    return app.hasModal() || fail("classic Escape did not open quit confirmation");
  }
  if (edition == Game::Deluxe) {
    auto focusLabel = [&]() -> std::string {
      for (const auto& w : app.screen()->widgets)
        if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->focused) return b->label;
      return {};
    };
    key(app, SDLK_RIGHT);
    if (focusLabel() != "PROFILE") return fail("right did not focus the visible Profile footer action");
    key(app, SDLK_RIGHT);
    if (focusLabel() != "QUIT") return fail("right reached a removed footer action instead of Quit");
    key(app, SDLK_LEFT);
    if (focusLabel() != "PROFILE") return fail("left reached a removed footer action instead of Profile");
    key(app, SDLK_TAB);
    if (focusLabel() != "QUIT") return fail("Tab reached a removed footer action instead of Quit");
    key(app, SDLK_DOWN);
    if (focusLabel() != "SINGLE PLAYER") return fail("footer exit did not restore Single Player focus");
  }
  if (canvas == 1067) {
    auto move = [&](float x, float y) {
      Input in;
      in.type = Input::MouseMove;
      in.x = x;
      in.y = y;
      app.input(in);
    };
    move(canvas - 234.0f, 165);
    Button* hovered = nullptr;
    for (const auto& w : app.screen()->widgets)
      if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->over && b->label == "MULTIPLAYER") hovered = b;
    if (!hovered) return fail("could not exercise a hovered button before an aspect change");
    app.settings.widescreen = static_cast<int>(UiWide::W16x10);
    frame(nullptr);
    if (gfx.canvasW() != 960 || hovered->over) return fail("old pointer hover survived relocation during widescreen resize");
    app.settings.widescreen = static_cast<int>(mode);
    frame(nullptr);
    move(canvas - 234.0f, 102);
  }
  key(app, SDLK_RETURN);
  settle();
  frame("single");
  if (app.hasModal() || app.screenName() != "mainmenu" || gfx.canvasW() != canvas)
    return fail("keyboard did not stay on wide Single Player");
  if (vfs.exists("console/menu/footer_sleeve.png")) {
    fs::create_directories(scratch);
    const std::string tile = pathToUtf8(scratch / "pad-glyph.tga");
    {
      std::string tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 8, 0, 32, 0x28};
      for (int i = 0; i < 64; ++i) tga.append({char(255), char(0), char(255), char(255)});
      if (!(std::ofstream(scratch / "pad-glyph.tga", std::ios::binary) << tga)) return fail("could not write the pad glyph fixture");
    }
    struct PadCase { PadGlyphs pad; std::string select, back; const char* name; };
    const PadCase pads[] = {
        {{}, "", "ESC", "keyboard"},
        {{Pad::PlayStation, {}}, "", "", "PlayStation"},
        {{Pad::Xbox, {}}, "A", "B", "Xbox"},
        {{Pad::SteamDeck, {}}, "A", "B", "Steam Deck"},
        {{Pad::SteamController, {}}, "A", "B", "Steam Controller"},
        {{Pad::Nintendo, {}}, "B", "A", "Nintendo"},
        {{Pad::Xbox, {tile, tile, tile, tile}}, "", "", "Steam glyph"},
    };
    for (const auto& c : pads) {
      if (c.pad.pad == Pad::PlayStation && !vfs.exists("console/menu/pad_glyphs.png")) continue;
      auto font = std::make_unique<StoryTestFont>(renderer.get(), gfx);
      auto* recorder = font.get();
      gfx.setFontProvider(std::move(font));
      app.pad = c.pad;
      app.tick();
      frame(nullptr);
      float select = -1, back = -1;
      for (const auto& d : recorder->draws) {
        if (d.text == "Select") select = d.x;
        if (d.text == "Back" && d.font == "OVERLOAD16OUTLINE" && d.x < 400) back = d.x;
      }
      const float backSleeve = 66 + std::max(112.0f, 82 + recorder->native->width("OVERLOAD16OUTLINE", "Select"));
      std::string selectFace, backFace;
      for (const auto& d : recorder->draws) {
        if (d.y < 529 || d.w > 26) continue;
        if (std::abs(d.x + d.w / 2 - (66 + 29.7f)) <= 1.5f) selectFace = d.text;
        if (std::abs(d.x + d.w / 2 - (backSleeve + 29.7f)) <= 1.5f) backFace = d.text;
      }
      Uint8 r = 0, gr = 0, b = 0, a = 0;
      SDL_ReadSurfacePixel(surf.get(), static_cast<int>(gfx.offX() + (66 + 29.7f) * gfx.scale()),
                           static_cast<int>(gfx.offY() + (529 + 25.3f) * gfx.scale()), &r, &gr, &b, &a);
      gfx.setFontProvider(makeDefaultFontProvider(renderer.get(), &gfx));
      app.pad = {};
      if (select < 66 + 56 || back < backSleeve + 56) return fail("footer prompt label overlaps the sleeve socket collar");
      if (selectFace != c.select || backFace != c.back) return fail((std::string(c.name) + " footer prompts show the wrong button").c_str());
      const bool marked = r > 200 && gr < 60 && b > 200;
      if (marked != !c.pad.art[0].empty())
        return fail((std::string(c.name) + (marked ? " footer prompt drew glyph art it was not given" : " footer prompt ignored its glyph art")).c_str());
    }
  }
  if (canvas == 1067) {
    if (vfs.exists("console/menu/header_rail.png") && vfs.exists("console/menu/header_cap.png")) {
      Uint8 r, g, b, a;
      if (!SDL_ReadSurfacePixel(surf.get(), static_cast<int>(gfx.offX() + 609 * gfx.scale()),
                               static_cast<int>(gfx.offY() + 36 * gfx.scale()), &r, &g, &b, &a) ||
          g < 120 || b < 185)
        return fail("Single Player header rail has a gap before the title plate");
    }
    std::vector<std::string> rows;
    for (const auto& w : app.screen()->widgets)
      if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->h == 50) rows.push_back(b->label);
    if (rows != std::vector<std::string>{"ADVENTURE", "QUICK PLAY", "CUSTOM LEVELS", "MASTER DUEL", "CHALLENGE"})
      return fail("16:9 Single Player does not expose the requested menu order");
    canvasClick(canvas - 202.0f, 343);
    if (app.stages.numAllStages() == app.stages.numStages()) {
      if (!app.getDialog(D_Locked)) return fail("Custom Levels did not handle an empty pack list");
      app.killDialog(D_Locked);
    } else {
      auto* picks = dynamic_cast<LevelScreen*>(app.screen());
      if (!picks || picks->stage != app.stages.numStages()) return fail("Custom Levels did not open the first installed extra stage");
      app.showMainMenu();
      settle();
      frame(nullptr);
      key(app, SDLK_RETURN);
    }
    settle();
    if (!art.empty()) {
      canvasClick(canvas - 202.0f, 217);
      if (!app.getDialog(D_WhichAdventure)) return fail("Adventure lost its edition selector");
      app.killDialog(D_WhichAdventure);
      settle();
    }
  }
  frame(nullptr);
  const double sharpScene = detail(20, 120, 200, 380);
  canvasClick(canvas - 202.0f, 408);
  if (!app.getDialog(D_Locked)) return fail("Single Player Master Duel hit box did not open the original locked-mode dialog");
  settle();
  frame("locked");
  if (gfx.canvasW() >= 960) {
    auto* lock = app.getDialog(D_Locked);
    if (std::abs(lock->x + lock->w * 0.5f - 400) > 1 || lock->w < 480) return fail("16:9 lock dialog is not centred at the PS3 width");
    if (detail(20, 120, 200, 380) > sharpScene * 0.75) return fail("16:9 menu behind a modal dialog was not blurred");
  }
  app.killDialog(D_Locked);
  settle();
  key(app, SDLK_ESCAPE);
  settle();
  frame("returned");
  if (app.hasModal()) return fail("Escape should return to Home, not quit");
  canvasClick(canvas - 234.0f, 356);
  auto* options = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
  if (!options) return fail("Home Help & Options hit box did not open application Options");
  settle();
  frame("options");
  if (options->w != 720 || std::abs(options->x + options->w * .5f - 400) > 0.5f || options->y + options->h > 540 ||
      options->music->x + options->music->w > options->help->x)
    return fail("widescreen Help & Options did not lay out two centered columns above the footer");
  if (tour) {
    frame(nullptr);
    app.frame(800);
    dialogClick(*options, *options->visuals);
    auto* resizedVisuals = dynamic_cast<VisualsDialog*>(app.getDialog(D_Visuals));
    if (!resizedVisuals) return fail("Enhanced Visuals did not open while the footer was hidden by a narrow canvas");
    settle();
    app.frame(800);
    frame("enhanced-visuals-widened");
    if (resizedVisuals->y + resizedVisuals->yesBtn->y + resizedVisuals->yesBtn->h > Dialog::kOptionsFooterTop)
      visualBoundsOk = fail("restored widescreen footer covers the Enhanced Visuals Done button");
    dialogClick(*resizedVisuals, *resizedVisuals->yesBtn);
    settle();
    if (app.getDialog(D_Visuals) || app.getDialog(D_Options) != options)
      return fail("resized Enhanced Visuals Done did not return to the same Options dialog");
    frame(nullptr);
    dialogClick(*options, *options->help);
    key(app, SDLK_ESCAPE);
    if (app.getDialog(D_Options) != options || options->scrollingOff)
      return fail("Escape reached Options while How to Play was sliding in");
    settle();
    if (!dynamic_cast<HelpScreen*>(app.helpOverlay())) return fail("How to Play did not open from Help & Options");
    frame("help");
    key(app, SDLK_ESCAPE);
    dialogClick(*options, *options->visuals);
    if (app.getDialog(D_Visuals)) return fail("click reached Options while How to Play was sliding out");
    settle();
    if (app.helpOverlay() || app.getDialog(D_Options) != options)
      return fail("How to Play did not return to the original Help & Options");
    frame(nullptr);
    dialogClick(*options, *options->visuals);
    settle();
    auto* visuals = dynamic_cast<VisualsDialog*>(app.getDialog(D_Visuals));
    if (!visuals) return fail("Enhanced Visuals did not open from Options");
    frame("enhanced-visuals");
    if (visuals->y + visuals->h > Dialog::kOptionsFooterTop)
      return fail("Enhanced Visuals covers the widescreen Options footer");
    dialogClick(*visuals, *visuals->yesBtn);
    settle();
    for (int i = 0; i < 4; ++i) {
      dialogClick(*options, *options->widescreen);
      settle();
      frame(i == 3 ? "options-classic" : nullptr);
    }
    if (options->widescreen->label != "Widescreen: Classic 4:3" || gfx.canvasW() != 800)
      return fail("Options did not switch to Classic 4:3");
    app.cfg.noSave = false;
    dialogClick(*options, *options->yesBtn);
    app.cfg.noSave = true;
    Settings saved;
    if (!saved.load(app.userDir.parent_path() / "settings.ini") || saved.widescreen != static_cast<int>(UiWide::Classic4x3))
      return fail("Done did not save Classic 4:3");
    settle();
    frame("classic-selected");
    auto* classicMenu = static_cast<MainMenu*>(app.screen());
    const auto& b = *classicMenu->bOptions;
    canvasClick(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
    settle();
    frame(nullptr);
    options = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
    if (!options) return fail("Classic Options did not reopen");
    if (options->w != 440) return fail("Classic Options did not use its original width");
    dialogClick(*options, *options->widescreen);
    settle();
    frame(nullptr);
    if (app.widescreenMode() != UiWide::Auto || gfx.canvasW() != canvas) return fail("Auto did not restore the console layout");
    dialogClick(*options, *options->editor);
    settle();
    if (app.screenName() != "editor") return fail("Options Level Editor button did not open the editor");
    frame("editor");
    canvasClick(74, 18);
    frame("editor-open");
    key(app, SDLK_RETURN);
    frame("editor-level");
    app.showMainMenu();
    settle();
    frame(nullptr);
    key(app, SDLK_F9);
    if (app.screenName() != "editor") return fail("F9 did not open the editor from the main menu");
    app.showMainMenu();
    settle();
    frame(nullptr);
    canvasClick(canvas - 234.0f, 356);
    settle();
    frame(nullptr);
    options = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
    if (!options) return fail("Options not found after returning from editor");
    dialogClick(*options, *options->gamesBtn);
    if (!app.takeGameFoldersRequest()) return fail("Game Folders button did not request setup");
    {
      mods::ModManager manager;
      manager.setUserDir(app.userDir);
      manager.discover();
      manager.activate(&vfs);
      dialogClick(*options, *options->modsBtn);
      settle();
      if (!app.helpOverlay() || std::string(app.helpOverlay()->name()) != "mods") return fail("Options Mods button did not open Mods");
      frame("mods");
      {
        auto font = std::make_unique<StoryTestFont>(renderer.get(), gfx);
        auto* recorder = font.get();
        gfx.setFontProvider(std::move(font));
        app.tick();
        frame(nullptr);
        const bool sleeve = std::any_of(recorder->draws.begin(), recorder->draws.end(), [](const auto& d) { return d.text == "Select" || d.text == "QUIT"; });
        gfx.setFontProvider(makeDefaultFontProvider(renderer.get(), &gfx));
        if (sleeve) return fail("Home footer prompts show through the Mods screen");
      }
      app.endHelpScreen();
      settle();
    }
  } else app.killDialog(D_Options);
  settle();
  frame(nullptr);
  if (tour && !app.profiles.add(player.name)) return fail("could not create isolated profile fixture");
  canvasClick(canvas - 270.0f, 565);
  if (!app.getDialog(D_UserList)) return fail("profile button did not open the original profile dialog");
  settle();
  if (tour) frame("profiles");
  app.killDialog(D_UserList);
  settle();
  if (tour) {
    ReplayData replay;
    replay.game = edition == Game::Nights ? 1 : 0;
    replay.level = replay.game ? "Bjorn1" : "Level1";
    replay.title = replay.game ? "Crimewave" : "Peggleland";
    replay.player = player.name;
    if (!replay.save(app.userDir / "replays" / ("Sample replay" + replayExt(replay.game))))
      return fail("could not write isolated replay fixture");
  }
  canvasClick(canvas - 234.0f, 232);
  if (!app.getDialog(D_Tip) && !app.getDialog(D_Replay2)) return fail("Replays did not open the original replay UI");
  if (tour) {
    if (!app.getDialog(D_Replay2)) return fail("Replays did not list the fixture");
    settle();
    frame("replays");
  }
  app.killDialog(D_Tip);
  app.killDialog(D_Replay2);
  settle();
  if (tour && height == 1080) {
    std::ofstream prefs(app.userDir.parent_path() / "online.ini");
    prefs << "lobbyset=1\nlobby=127.0.0.1:0\n";
    prefs.close();
    if (!prefs) return fail("could not write offline multiplayer fixture");
    auto visibleButton = [&](const char* label) -> Button* {
      for (const auto& w : app.screen()->widgets)
        if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->label == label) return b;
      return nullptr;
    };
    auto row = [&](int i) {
      app.cfg.noSave = false;
      canvasClick(canvas / 2.0f + 30, 164.0f + 59 * i);
      app.cfg.noSave = true;
      settle();
    };
    auto back = [&] {
      key(app, SDLK_ESCAPE);
      settle();
    };
    canvasClick(canvas - 234.0f, 165);
    settle();
    if (app.screenName() != "mainmenu" || !visibleButton("ONLINE PEG PARTY") || !visibleButton("JOIN WITH CODE"))
      return fail("Multiplayer did not open the multiplayer menu");
    frame("multiplayer");
    row(0);
    if (!visibleButton("HOST PEG PARTY") || visibleButton("JOIN WITH CODE")) return fail("Online Peg Party did not open its picker");
    frame("online-party");
    row(1);
    if (app.screenName() != "online") return fail("Custom Match did not open the online screen");
    if (gfx.canvasW() != canvas) return fail("Multiplayer used the Classic canvas in 16:9");
    if (visibleButton("Create Room") || visibleButton("Refresh")) return fail("Custom Match showed the Classic room browser buttons");
    frame("custom-match");
    back();
    if (app.screenName() != "mainmenu" || !visibleButton("HOST PEG PARTY")) return fail("Back from Custom Match did not return to the Peg Party picker");
    row(2);
    if (app.screenName() != "online" || !visibleButton("Host Match") || visibleButton("Cancel")) return fail("Host Peg Party did not open the party options");
    frame("create-room");
    back();
    back();
    if (!visibleButton("JOIN WITH CODE")) return fail("Back from the Peg Party picker did not return to the multiplayer menu");
    row(3);
    if (app.screenName() != "online" || !visibleButton("Paste Invite") || !visibleButton("Join")) return fail("Join With Code did not open the code entry");
    frame("join-code");
    back();
    back();
    if (!visibleButton("MULTIPLAYER")) return fail("Back from the multiplayer menu did not return home");
    frame(nullptr);
    player.maxStage = 3;
    player.advCompletions = 1;
    canvasClick(canvas - 234.0f, 292);
    settle();
    if (app.screenName() != "trophy") return fail("Trophies did not open for an unlocked profile");
    frame("trophies");
    app.showMainMenu();
    settle();
    frame(nullptr);
    canvasClick(canvas - 234.0f, 102);
    settle();
    frame("single-unlocked");
    canvasClick(canvas - 202.0f, 280);
    settle();
    if (app.screenName() != "levelscreen") return fail("Quick Play did not open the level picker");
    frame("quick-play");
    app.showMainMenu();
    settle();
    frame(nullptr);
    canvasClick(canvas - 234.0f, 102);
    settle();
    frame(nullptr);
    canvasClick(canvas - 202.0f, 406);
    settle();
    if (app.screenName() != "levelscreen" || app.gameMode != GameMode::Duel) return fail("Master Duel did not open the level picker");
    frame("master-duel");
    app.showMainMenu();
    settle();
    frame(nullptr);
    player.maxStage = 0;
    player.advCompletions = 0;
  }
  canvasClick(canvas - 234.0f, 102);
  settle();
  if (edition == Game::Nights) {
    frame("single-resume");
    if (canvas == 1067) {
      key(app, SDLK_ESCAPE);
      settle();
      frame(nullptr);
      canvasClick(canvas - 173.0f, 565);
    } else key(app, SDLK_RETURN);
    settle();
    frame("nights-hub");
    if (gfx.canvasW() != canvas) return fail("Nights hub lost widescreen canvas");
    if (canvas == 1067 && vfs.exists("console/menu/header_rail.png")) {
      const int y = static_cast<int>(gfx.offY() + 36 * gfx.scale());
      for (int x : {566, 590, 622}) {
        Uint8 r, g, b, a;
        const int col = static_cast<int>(gfx.offX() + x * gfx.scale());
        if (!SDL_ReadSurfacePixel(surf.get(), col, y, &r, &g, &b, &a) || r < 75 || g < 120 || b < 185)
          return fail("Nights Hub header rail has a gap before the title plate");
      }
    }
    canvasClick(canvas - 202.0f, 469);
    frame("lounge");
    if (gfx.canvasW() != 800 || !static_cast<MainMenu*>(app.screen())->bSolo->visible) return fail("Nights lounge did not restore the original UI");
    key(app, SDLK_ESCAPE);
    settle();
    frame("nights-hub-returned");
    if (gfx.canvasW() != canvas || app.hasModal()) return fail("Escape did not return from the lounge to the Nights hub");
    if (tour) {
      canvasClick(canvas - 202.0f, 407);
      settle();
      if (app.screenName() != "adventure") return fail("Nights Stats did not open the progress screen");
      frame("stats");
      if (gfx.canvasW() != canvas) return fail("Nights Stats used the Classic canvas in widescreen");
      const auto* trophies = static_cast<AdventureScreen*>(app.screen())->bContinue;
      canvasClick(trophies->x + trophies->w / 2, trophies->y + trophies->h / 2);
      settle();
      frame("nights-trophy-room");
      if (gfx.canvasW() != canvas || app.screenName() != "progress") return fail("Nights Stats did not open the widescreen Trophy Room");
      app.showIntroScreen();
      for (int i = 0; i < 200; ++i) app.tick();
      frame("intro");
      if (gfx.canvasW() != canvas || app.screenName() != "intro") return fail("Nights Adventure intro did not use the widescreen scene");
      app.showMainMenu();
      settle();
      frame(nullptr);
      if (canvas == 1067) canvasClick(canvas - 173.0f, 565);
      else {
        key(app, SDLK_RETURN);
        settle();
        key(app, SDLK_RETURN);
      }
      settle();
      frame(nullptr);
      canvasClick(canvas - 202.0f, 280);
      if (!app.getDialog(D_Locked)) return fail("Nights Hub Challenge lost its unlock check");
      app.killDialog(D_Locked);
      settle();
      canvasClick(canvas - 202.0f, 343);
      settle();
      frame("nights-hub-trophy-room");
      const auto* room = dynamic_cast<ProgressScreen*>(app.screen());
      if (!room || room->mode != 2 || gfx.canvasW() != canvas) return fail("Nights Hub Trophy Room did not open the portrait room directly");
      key(app, SDLK_ESCAPE);
      settle();
      frame("nights-hub-after-trophies");
      bool returnedToHub = false;
      for (const auto& w : app.screen()->widgets)
        if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->label == "TROPHY ROOM") returnedToHub = true;
      if (!returnedToHub) return fail("Trophy Room Escape returned to Home rather than the Nights Hub");
      canvasClick(canvas - 202.0f, 343);
      settle();
      frame(nullptr);
      room = dynamic_cast<ProgressScreen*>(app.screen());
      if (!room || room->mode != 2) return fail("returning to the Hub broke reopening the Trophy Room");
      const auto& back = *room->bContinue;
      canvasClick(back.x + back.w * .5f, back.y + back.h * .5f);
      settle();
      if (app.screenName() != "mainmenu") return fail("clicking Trophy Room Back did not return to the menu");
    }
  }
  if ((width == 1920 && height == 1080) || mode == UiWide::W16x9) {
    if (tour) {
      const bool nights = edition == Game::Nights;
      app.curStage = nights ? 11 : 6;
      app.showStoryScreen(false, false);
      settle();
      auto* longStory = dynamic_cast<StoryScreen*>(app.screen());
      if (!longStory || longStory->stage != app.curStage || longStory->text.empty() || (nights && longStory->text2.empty()))
        return fail("could not load the long stock story fixture");
      longStory->skipTyping();
      settle();
      auto font = std::make_unique<StoryTestFont>(renderer.get(), gfx);
      auto* recorder = font.get();
      gfx.setFontProvider(std::move(font));
      frame("long-story");
      std::string first, second;
      bool overlaps = false;
      for (const auto& line : recorder->lines) {
        const bool secondBox = nights && line.x < canvas * 0.5f - 145;
        (secondBox ? second : first) += line.text;
        overlaps = overlaps || line.bottom > (secondBox ? 476 : nights ? 322 : 318);
      }
      const auto bare = [](std::string s) {
        s = stripColorCodes(s);
        std::erase_if(s, [](unsigned char c) { return std::isspace(c); });
        return s;
      };
      const bool complete = bare(first) == bare(longStory->text) && bare(second) == bare(longStory->text2);
      gfx.setFontProvider(makeDefaultFontProvider(renderer.get(), &gfx));
      if (!complete || overlaps) return fail("long stock story loses text or overlaps the following panel");
      app.curStage = 0;
      app.showStoryScreen(false, false);
      settle();
      frame("story");
    }
    player.storyPending = false;
    app.startAdventureGame();
    settle();
    if (app.getDialog(D_Tip)) {
      app.killDialog(D_Tip);
      settle();
    }
    auto* game = dynamic_cast<GameScreen*>(app.screen());
    if (!game || !game->ok()) return fail("Adventure board did not load");
    if (app.anyDialog() || game->paused()) return fail("Adventure board did not clear the intro dialog");
    const int inset = Renderer::hudInset(game->canvasWidth(), game->board().W);
    if (inset != 133) return fail("real 16:9 board did not use the centered HUD");
    const auto hash = game->board().hash();
    frame("gameplay");
    const auto view = boardView.consoleView(game->canvasWidth(), game->board().W);
    const auto shown = [&](float x, float y) { return std::pair{view.x + view.k * x, view.y + view.k * y}; };
    const auto [midX, midY] = shown(inset + 400.0f, 300);
    auto middle = boardView.toBoard(gfx.offX() + midX * gfx.scale(), gfx.offY() + midY * gfx.scale());
    if (game->board().hash() != hash || std::abs(middle.x - 325.0f) > 0.1f || std::abs(middle.y - 260.0f) > 0.1f)
      return fail("console rendering changed board state or aiming");
    const auto [menuX, menuY] = shown(inset + 38.0f, 565);
    canvasClick(menuX, menuY);
    if (!app.getDialog(D_Options) || !game->paused()) return fail("centered Menu button did not open the original in-game Options");
    settle();
    frame(tour || mode == UiWide::W16x9 ? "pause" : nullptr);
    const auto* pause = static_cast<OptionsDialog*>(app.getDialog(D_Options));
    if (pause->w != 720 || std::abs(pause->x + pause->w * .5f - 400) > 0.5f || pause->y + pause->h > 540 ||
        pause->music->x + pause->music->w > pause->yesBtn->x)
      return fail("widescreen Pause did not lay out two centered columns above the footer");
    if (gfx.offY() > 4) {
      for (int y = 0; y < static_cast<int>(gfx.offY()) - 1; ++y) {
        Uint8 r, g, b, a;
        if (!SDL_ReadSurfacePixel(surf.get(), width / 2, y, &r, &g, &b, &a) || r || g || b)
          return fail("Pause artwork leaked into the upper 16:9 letterbox");
      }
    }
    app.finishOptionsDialog(false, false);
    if (app.getDialog(D_Options) || game->paused()) return fail("closing centered Options did not resume the original board");
    if (tour) {
      app.settings.widescreen = static_cast<int>(UiWide::Classic4x3);
      app.settings.vis.ultraWideHud = true;
      frame("classic-gameplay");
      if (gfx.canvasW() != 800) return fail("Classic 4:3 did not restore the original playfield frame");
    }
  }
  if (tour && canvas == 1067) {
    app.settings.widescreen = static_cast<int>(UiWide::Auto);
    app.showMainMenu();
    settle();
    frame(nullptr);
    canvasClick(canvas - 234.0f, 356);
    settle();
    frame(nullptr);
    options = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
    if (!options || !options->homeBtn) return fail("theme toggle missing");
    dialogClick(*options, *options->homeBtn);
    dialogClick(*options, *options->yesBtn);
    settle();
    GameRequest request;
    const auto expected = edition == Game::Deluxe ? Game::Nights : Game::Deluxe;
    if (!app.takeGameSwitchRequest(request) || request.game != expected || request.start.place != StartPlace::MainMenu)
      return fail("theme change did not request the selected Home edition");
  }
  return (captureOk || fail("could not save menu captures")) && visualBoundsOk;
}

int main(int argc, char** argv) {
  if (argc != 5 || !SDL_Init(0)) return 2;
  bool ok = true;
  for (const auto edition : {Game::Deluxe, Game::Nights}) {
    const auto& gameDir = argv[edition == Game::Deluxe ? 1 : 2];
    ok &= probe(gameDir, argv[3], argv[4], edition, 1920, 1200, UiWide::Auto);
    ok &= probe(gameDir, argv[3], argv[4], edition, 1920, 1080, UiWide::Auto);
    ok &= probe(gameDir, argv[3], argv[4], edition, 1572, 1067, UiWide::W16x9);
    ok &= probe(gameDir, argv[3], argv[4], edition, 800, 600, UiWide::Auto);
    ok &= probe(gameDir, argv[3], argv[4], edition, 1067, 600, UiWide::Classic4x3);
    ok &= probe(gameDir, {}, argv[4], edition, 960, 600, UiWide::Auto);
    ok &= probe(gameDir, {}, argv[4], edition, 1920, 1080, UiWide::Auto);
  }
  SDL_Quit();
  return !ok;
}
