#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#include "mods/manager.h"
#include "font.h"
#include "gamepaths.h"
#include "ui/screens.h"
#include "ui/dialogs.h"
#include "ui/import_dialog.h"
#include "ui/other_game.h"
#include "ui/game_screen.h"
#include "ui/replay.h"
#include "ui/visuals_dialog.h"


using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;
using UiWide = pp::ui::WidescreenMode;

struct StoryTestFont : FontProvider {
  struct Line { float x, bottom; std::string text; };
  struct Ink { float l, t, r, b; };
  struct Draw { std::string font, text; float x, w, y; Ink bounds{}; };
  std::vector<Line> lines;
  std::vector<Draw> draws;
  std::unique_ptr<FontProvider> native;
  Gfx& gfx;
  std::vector<Ink> ink;
  bool measure = false;
  FontManager fonts;
  bool layerColors(const std::string& n, size_t i, uint32_t& mult, uint32_t& add) override { return native->layerColors(n, i, mult, add); }
  void setLayerColors(const std::string& n, size_t i, uint32_t mult, uint32_t add) override { native->setLayerColors(n, i, mult, add); }
  StoryTestFont(SDL_Renderer* r, Gfx& g) : native(makeDefaultFontProvider(r, &g)), gfx(g), fonts(g.vfs()) {}
  float height(const std::string& n) override { return native->height(n); }
  float ascent(const std::string& n) override { return native->ascent(n); }
  float ascentPadding(const std::string& n) override { return native->ascentPadding(n); }
  float lineSpacing(const std::string& n) override { return native->lineSpacing(n); }
  float width(const std::string& n, const std::string& s) override { return native->width(n, s); }
  void draw(const std::string& n, float x, float y, const std::string& s, uint32_t rgb, float alpha) override {
    draws.push_back({n, s, x, native->width(n, s), y});
    if (measure) {
      auto* f = fonts.get("FONT_" + n);
      if (!f) f = fonts.get("FONT_PLAIN");
      std::vector<GlyphQuad> quads;
      if (f) f->layout(s, 0xffffffff, quads);
      const size_t first = ink.size();
      for (const auto& q : quads) {
        if (!q.img || !q.sw || !q.sh || !(q.rgba & 255)) continue;
        int l = q.sw, r = 0, t = q.sh, b = 0;
        for (int j = 0; j < q.sh; ++j) for (int i = 0; i < q.sw; ++i) {
          if (!q.img->rgba[((q.sy + j) * q.img->w + q.sx + i) * 4 + 3]) continue;
          l = std::min(l, i); r = std::max(r, i + 1); t = std::min(t, j); b = std::max(b, j + 1);
        }
        if (r <= l) continue;
        const float k = gfx.scale(), dx = gfx.offX() + k * (x + q.dx), dy = gfx.offY() + k * (y + q.dy);
        float sx, sy;
        SDL_GetRenderScale(gfx.renderer(), &sx, &sy);
        ink.push_back({sx * (dx + k*l*q.dw/q.sw), sy * (dy + k*t*q.dh/q.sh), sx * (dx + k*r*q.dw/q.sw), sy * (dy + k*b*q.dh/q.sh)});
      }
      if (ink.size() > first) {
        auto& box = draws.back().bounds = ink[first];
        for (size_t i = first + 1; i < ink.size(); ++i) {
          box.l = std::min(box.l, ink[i].l); box.r = std::max(box.r, ink[i].r);
          box.t = std::min(box.t, ink[i].t); box.b = std::max(box.b, ink[i].b);
        }
      }
    }
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
  for (auto type : {Input::MouseMove, Input::MouseDown, Input::MouseUp}) {
    app.gfx.toCanvas(wx, wy, in.x, in.y);
    in.type = type;
    app.input(in);
  }
}

static Button* visibleButton(Panel& panel, const char* label) {
  for (const auto& widget : panel.widgets)
    if (auto* b = dynamic_cast<Button*>(widget.get()); b && b->visible && b->label == label) return b;
  return nullptr;
}

static bool autoResizeOptions(const fs::path& assets, const fs::path& scratch, Game edition) {
  auto fail = [](const char* why) { std::cerr << "Auto resize Options: " << why << '\n'; return false; };
  Vfs vfs;
  if (!vfs.mountGame(assets, nullptr)) return fail("game mount failed");
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
      SDL_CreateWindow("Options resize", 800, 600, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
  if (!window) return fail(SDL_GetError());
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(), "software"), SDL_DestroyRenderer);
  if (!renderer) return fail(SDL_GetError());
  Gfx gfx(renderer.get(), vfs);
  PlayerInfo player;
  App::Config cfg;
  cfg.game = edition;
  cfg.widescreen = static_cast<int>(UiWide::Auto);
  cfg.noRoomDiscovery = cfg.noSave = cfg.noAudio = true;
  cfg.userDir = scratch / "resize-profile";
  App app(gfx, vfs, nullptr, nullptr, cfg);
  app.curProfile = &player;
  app.showMainMenu();
  app.doOptionsDialog();
  auto settle = [&] { for (int i = 0; i < 30; ++i) app.tick(); };
  settle();
  app.frame();
  auto* options = dynamic_cast<OptionsDialog*>(app.topDialog());
  auto* mods = options ? visibleButton(*options, "Mods") : nullptr;
  if (!mods) return fail("missing Classic Mods before resize");
  const float x = options->x + mods->x + mods->w / 2, y = options->y + mods->y + mods->h / 2;
  for (size_t i = 0; i <= options->widgets.size() && options->focusW != mods; ++i) key(app, SDLK_TAB);
  if (options->focusW != mods) return fail("could not focus Classic Mods");
  auto resize = [&](int width, int height) {
    if (!SDL_SetWindowSize(window.get(), width, height) || !SDL_SyncWindow(window.get())) return false;
    SDL_PumpEvents();
    int w, h;
    return SDL_GetRenderOutputSize(renderer.get(), &w, &h) && w == width && h == height;
  };
  if (!resize(1600, 900)) return fail(SDL_GetError());
  Input in;
  in.type = Input::KeyUp;
  app.input(in);
  auto* hidden = options->findWidget(12);
  if (app.topDialog() != options || (hidden && (hidden->visible || hidden->focused)) ||
      !options->focusW || !options->focusW->visible || !options->focusW->focused)
    return fail("widening left focus on a hidden or invalid action before redraw");
  for (size_t i = 0; i <= options->widgets.size(); ++i) {
    key(app, SDLK_TAB);
    if (!options->focusW || options->focusW->id == 12 || !options->focusW->visible) return fail("hidden Mods is a Tab stop");
  }
  app.frame();
  click(app, gfx.offX() + (x + (gfx.canvasW() - 800) / 2.0f) * gfx.scale(), gfx.offY() + y * gfx.scale());
  if (app.helpOverlay() || app.topDialog() != options) return fail("widening left an active Classic hit target");
  if (!resize(800, 600)) return fail(SDL_GetError());
  click(app, x, y);
  settle();
  if (!app.helpOverlay() || std::string(app.helpOverlay()->name()) != "mods") return fail("narrowing missed Classic Mods before redraw");
  key(app, SDLK_ESCAPE);
  settle();
  if (app.helpOverlay() || app.topDialog() != options) return fail("Back lost resized Options");
  app.frame();
  in.type = Input::MouseDown;
  gfx.toCanvas(x, y, in.x, in.y);
  app.input(in);
  if (!options->capture || options->capture->id != 12) return fail("Classic Mods did not capture the press");
  if (!resize(1600, 900)) return fail(SDL_GetError());
  in.type = Input::MouseUp;
  app.input(in);
  hidden = options->findWidget(12);
  if (app.helpOverlay() || app.topDialog() != options || options->capture || (hidden && (hidden->down || hidden->visible)))
    return fail("resizing during a press activated hidden Mods");
  return true;
}

static bool probe(const fs::path& gameDir, const fs::path& art, const fs::path& scratch, Game edition, int width, int height, UiWide mode, bool offline = false, const fs::path& otherDir = {}) {
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
  cfg.noRoomDiscovery = true;
  cfg.fastBoot = true;
  cfg.seed = 1;
  App app(gfx, vfs, nullptr, &boardView, cfg);
  if (!art.empty()) app.setGames(true, edition);
  app.curProfile = &player;
  app.showMainMenu();
  for (int i = 0; i < 25; ++i) app.tick();
  bool captureOk = true;
  bool visualBoundsOk = true;
  const bool fit = width == 1920 && height == 1080 && mode == UiWide::Auto && !art.empty();
  auto checkLabels = [&](const char* page, Panel* panel) {
    if (!panel) return;
    const float sc = gfx.scale(), ox = gfx.offX(), oy = gfx.offY();
    gfx.setTransform(1, 0, 0);
    auto font = std::make_unique<StoryTestFont>(renderer.get(), gfx);
    auto* rec = font.get();
    gfx.setFontProvider(std::move(font));
    rec->measure = true;
    if (auto* d = dynamic_cast<Dialog*>(panel); d && (!d->header.empty() || !d->lines.empty())) {
      d->Dialog::drawLayer(gfx);
      if (rec->ink.empty() || std::any_of(rec->ink.begin(), rec->ink.end(), [&](const auto& v) {
        return v.l < 5 || v.r > d->w - 5 || v.t < 0 || v.b > (d->yesBtn ? d->yesBtn->y : d->h);
      })) {
        std::cerr << page << ": dialog text escapes the panel or crosses its action row\n";
        visualBoundsOk = false;
      }
    }
    for (const auto& widget : panel->widgets) {
      auto* b = dynamic_cast<Button*>(widget.get());
      auto* c = dynamic_cast<Checkbox*>(widget.get());
      if (!widget->visible || (b && b->kind == Button::Kind::Link) || (!b && !c) || (b && (b->label.empty() || (b->kind == Button::Kind::Embedded && !b->labelOnEmbedded))) ||
          (c && c->label.empty())) continue;
      const auto& label = b ? b->label : c->label;
      rec->ink.clear(); rec->draws.clear();
      widget->draw(gfx);
      if (rec->draws.empty()) continue;
      if ((std::string(page) == "restart-confirm" || std::string(page) == "local-duel-locked") && !rec->ink.empty()) {
        float top = widget->h, bottom = 0;
        for (const auto& v : rec->ink) { top = std::min(top, v.t); bottom = std::max(bottom, v.b); }
        if (std::abs((top + bottom - widget->h) / 2) > 2) visualBoundsOk = fail("modal action ink is not vertically centred");
      }
      if (std::string(page) == "home" && b && b->id == 100 && !rec->ink.empty()) {
        float l = b->w, r = 0;
        for (const auto& v : rec->ink) { l = std::min(l, v.l); r = std::max(r, v.r); }
        if (r - l < 144 || r - l > 161 || rec->draws.back().text != "Single Player")
          visualBoundsOk = fail("Home Single Player spelling or glyph span differs from PS3");
      }
      if (rec->ink.empty() || std::any_of(rec->ink.begin(), rec->ink.end(), [&](const auto& v) {
        return v.l < 1 || v.r > widget->w - 1 || v.t < 0 || v.b > widget->h;
      })) {
        std::cerr << page << ": " << label << " ink outside " << widget->w << 'x' << widget->h << '\n';
        visualBoundsOk = false;
      }
    }
    gfx.setTransform(sc, ox, oy);
    gfx.setFontProvider(makeDefaultFontProvider(renderer.get(), &gfx));
  };
  auto frame = [&](const char* page, Panel* target = nullptr) {
    if (fit && page && std::string(page) != "long-story") {
      app.frame();
      Panel* panel = app.screen();
      for (int id : std::initializer_list<int>{D_UserList, D_Options, D_WhichAdventure, D_RestartLevel, D_Visuals, D_Locked, D_Replay2, D_Quit})
        if (auto* d = app.getDialog(id)) panel = d;
      checkLabels(page, target ? target : app.helpOverlay() ? app.helpOverlay() : panel);
    }
    app.frame();
    SDL_RenderPresent(renderer.get());
    if (fit && page && std::string(page) == "home") {
      auto* row = dynamic_cast<Button*>(app.screen()->findWidget(100));
      if (!row) { visualBoundsOk = fail("Home has no primary row"); return; }
      int bright = 0;
      for (int py = -7; py < 8; ++py) for (int px = -65; px < 65; ++px) {
        Uint8 rr, gg, bb, aa;
        SDL_ReadSurfacePixel(surf.get(), int(gfx.offX() + (row->x + row->w / 2 + px) * gfx.scale()),
                             int(gfx.offY() + (row->y + row->h / 2 + py) * gfx.scale()), &rr, &gg, &bb, &aa);
        bright += rr > 238 && gg > 238 && bb > 238;
      }
      if (bright < 150) visualBoundsOk = fail("focused Home label lost its bright foreground layer");
    }
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
  auto inspect = [&](Panel& panel) {
    const float sc = gfx.scale(), ox = gfx.offX(), oy = gfx.offY();
    gfx.setTransform(1, 0, 0);
    auto font = std::make_unique<StoryTestFont>(renderer.get(), gfx);
    auto* rec = font.get();
    rec->measure = true;
    gfx.setFontProvider(std::move(font));
    panel.drawLayer(gfx);
    auto result = std::move(rec->draws);
    gfx.setFontProvider(makeDefaultFontProvider(renderer.get(), &gfx));
    gfx.setTransform(sc, ox, oy);
    return result;
  };
  if (!otherDir.empty()) {
    if (!fit || app.userDir != cfg.userDir || !cfg.noSave || !cfg.noAudio || !cfg.noRoomDiscovery) return fail("extended fit requires an isolated 16:9 fixture");
    frame(nullptr);
    using Draw = StoryTestFont::Draw;
    auto bounds = [&](const std::string& page, const Draw& d, float l, float t, float r, float b) {
      const auto& a = d.bounds;
      if (a.r <= a.l || a.l < l || a.r > r || a.t < t || a.b > b) {
        std::cerr << page << " | " << d.text << " [" << a.l << ',' << a.t << ',' << a.r << ',' << a.b << "] outside [" << l << ',' << t << ',' << r << ',' << b << "]\n";
        visualBoundsOk = false;
      }
    };
    auto bare = [](std::string s) {
      s = stripColorCodes(s);
      std::erase_if(s, [](unsigned char c) { return std::isspace(c); });
      return s;
    };
    auto capture = [&](const std::string& name, Panel* p = nullptr) { frame(name.c_str(), p); };
    auto modal = [&](int id, const char* name) -> Dialog* {
      settle();
      Dialog* d = app.getDialog(id);
      if (!d || d->scrollingOff) { fail(name); return nullptr; }
      frame(name, d);
      const auto draws = inspect(*d);
      for (const auto& run : draws) if (!bare(run.text).empty()) bounds(name, run, 2, 0, d->w - 2, d->h);
      if (auto* imp = dynamic_cast<ImportDialog*>(d)) {
        bool note = false;
        for (const auto& run : draws) if (run.font == "PLAIN" && !bare(run.text).empty()) {
          note = true;
          bounds(name, run, imp->list->x - 1, imp->list->y + imp->list->h, imp->list->x + imp->list->w + 1, imp->fileBtn->y - 1);
        }
        if (!note) visualBoundsOk = fail("import status is missing");
      }
      if (d->x < (800 - gfx.canvasW()) / 2 || d->x + d->w > (800 + gfx.canvasW()) / 2 || d->y < 0 || d->y + d->h > 600)
        visualBoundsOk = fail((std::string(name) + " outside viewport").c_str());
      return d;
    };
    auto dismiss = [&](int id, Dialog* parent) {
      key(app, SDLK_ESCAPE); settle();
      return !app.getDialog(id) && (parent ? app.getDialog(parent->dialogId) == parent && !parent->scrollingOff : !app.hasModal());
    };
    const bool nights = edition == Game::Nights;
    canvasClick(float(gfx.canvasW()) - 234, 356);
    settle();
    auto* opts = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
    if (!opts) return fail("Help parent missing");
    if (auto* mods = opts->findWidget(12); mods && mods->visible) return fail("Help & Options retained a duplicate Mods action");
    frame(nullptr);
    dialogClick(*opts, *opts->help); settle();
    auto* help = dynamic_cast<HelpScreen*>(app.helpOverlay());
    if (!help || help->numPages() != (nights ? 13 : 8)) return fail("Help page count");
    const int helpCount = help->numPages();
    std::set<std::string> titles;
    for (int i = 0; i < help->numPages(); ++i) {
      const auto name = "help-" + std::to_string(i + 1);
      if (help->page != i) return fail("Help Next skipped a page");
      capture(name, help);
      bool count = false;
      int body = 0;
      for (const auto& d : inspect(*help)) {
        if (bare(d.text).empty()) continue;
        if (d.y > 535) {
          const int id = d.text == "Back" || d.text == "Esc" ? 0 : d.text == "Previous" || d.text == "Left arrow" ? 1 : 2;
          auto& b = *help->findWidget(id);
          bounds(name, d, b.x + 2, b.y, b.x + b.w - 2, b.y + b.h);
          continue;
        }
        bounds(name, d, 94, 45, 729, 508);
        count |= d.text == "Page " + std::to_string(i + 1) + " of " + std::to_string(help->numPages());
        if (d.font == "OVERLOAD24OUTLINE") titles.insert(d.text);
        if (d.y > 185) { ++body; bounds(name, d, 226, 170, 715, 508); }
      }
      if (!count || titles.size() != size_t(i + 1) || body < 2) return fail("Help title/count/body missing or repeated");
      const auto& next = *help->findWidget(2);
      canvasClick((gfx.canvasW() - 800) / 2 + next.x + next.w / 2, next.y + next.h / 2);
    }
    if (help->page != 0) return fail("Help end did not wrap");
    key(app, SDLK_LEFT);
    if (help->page != help->numPages()-1) return fail("Help Previous did not wrap");
    key(app, SDLK_ESCAPE); settle();
    if (app.helpOverlay() || app.getDialog(D_Options) != opts) return fail("Help did not return to Options");
    dialogClick(*opts, *opts->yesBtn); settle();
    if (app.hasModal()) return fail("Options did not dismiss");
    if (!app.profiles.add(player.name)) return fail("could not seed scratch roster");
    app.doUserDialog();
    auto* users = dynamic_cast<UserDialog*>(modal(D_UserList, "users"));
    if (!users) return false;
    const auto names = app.profiles.names();
    dialogClick(*users, *users->rename);
    auto* rename = dynamic_cast<NewUserDialog*>(modal(D_RenameUser, "rename"));
    if (!rename || rename->name() != player.name || !dismiss(D_RenameUser, users)) return fail("rename cancellation");
    dialogClick(*users, *users->create);
    auto* create = dynamic_cast<NewUserDialog*>(modal(D_CreateUser, "certificate"));
    if (!create || !create->certificate) return fail("certificate missing");
    create->setName("");
    key(app, SDLK_RETURN);
    if (!modal(D_NameError, "empty-name") || !dismiss(D_NameError, create)) return fail("certificate validation/cancellation");
    create->setName(player.name);
    key(app, SDLK_RETURN);
    if (!modal(D_NameError, "duplicate-name") || !dismiss(D_NameError, create) || !dismiss(D_CreateUser, users)) return fail("duplicate name cancellation");
    dialogClick(*users, *users->del);
    if (!modal(D_DeleteUser, "delete-user") || !dismiss(D_DeleteUser, users)) return fail("profile delete cancellation");
    dialogClick(*users, *users->importBtn);
    auto* import = dynamic_cast<ImportDialog*>(app.getDialog(D_ImportProfile));
    if (!import) return fail("import screen missing");
    const auto empty = scratch / "empty-import";
    fs::create_directories(empty);
    import->load(empty);
    if (!modal(D_ImportProfile, "import-empty") || import->yesBtn->enabled || !dismiss(D_ImportProfile, users) || !dismiss(D_UserList, nullptr)) return fail("empty import/cancellation");
    if (names != app.profiles.names() || app.curProfile != &player) return fail("profile changed on cancellation");
    const auto replayDir = app.userDir / "replays";
    if (!listReplays(replayDir, nights ? 1 : 0).empty()) return fail("extended fit needs an empty scratch replay directory");
    app.doReplaysDialog();
    if (!modal(D_Tip, "replays-empty") || !dismiss(D_Tip, nullptr)) return fail("empty replay tip");
    const std::string stem = "Sample Replay";
    const auto file = replayDir / (stem + replayExt(nights ? 1 : 0));
    fs::create_directories(replayDir);
    ReplayData replay;
    replay.game = nights ? 1 : 0;
    replay.level = app.stages.stage(0)->levels.front().file;
    replay.log = {{10, 0, 260, 330}, {15, 1}, {16, 3}};
    replay.shots = 1;
    replay.player = player.name;
    if (!replay.save(file)) return fail("fixture write failed");
    auto readReplay = [&] { std::ifstream in(file, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}); };
    const auto bytes = readReplay();
    auto selectReplay = [&](Dialog& d) -> bool {
      for (auto& widget : d.widgets) if (auto* list = dynamic_cast<ListBox*>(widget.get()); list && list->lines.size() == 1 && list->lines[0] == stem) {
        canvasClick((gfx.canvasW() - 800) * .5f + d.x + list->x + 16, d.y + list->y + list->topInset + list->itemH / 2);
        return list->select == 0;
      }
      return false;
    };
    app.doReplaysDialog();
    auto* show = modal(D_Replay2, "replay-list");
    if (!show || !selectReplay(*show)) return fail("could not select scratch replay");
    dialogClick(*show, *show->findWidget(102));
    if (!modal(D_DeleteReplay, "delete-replay") || !dismiss(D_DeleteReplay, show)) return fail("replay delete cancellation");
    if (readReplay() != bytes) return fail("replay delete cancellation changed bytes");
    if (!(std::ofstream(file, std::ios::binary) << "unreadable")) return fail("could not invalidate scratch replay");
    dialogClick(*show, *show->findWidget(105));
    if (!modal(D_ReplayError, "bad-replay") || !dismiss(D_ReplayError, show) || !dismiss(D_Replay2, nullptr)) return fail("unreadable replay handling");
    if (!(std::ofstream(file, std::ios::binary) << bytes)) return fail("could not restore fixture");
    app.doSaveReplayDialog();
    auto* save = modal(D_SaveReplay, "save-replay");
    if (!save || !selectReplay(*save)) return fail("save replay list");
    dialogClick(*save, *save->findWidget(105));
    if (!modal(D_OverwriteReplay, "overwrite-replay") || !dismiss(D_OverwriteReplay, save) || !dismiss(D_SaveReplay, nullptr)) return fail("overwrite cancellation");
    if (readReplay() != bytes) return fail("replay changed after cancellation");
    app.doReplaysDialog();
    settle(); frame(nullptr);
    show = app.getDialog(D_Replay2);
    if (!show || !selectReplay(*show)) return fail("playback selection");
    dialogClick(*show, *show->findWidget(105));
    for (int i = 0; i < 25000 && !app.getDialog(D_Replay1) && !app.getDialog(D_ReplayError); ++i) app.tick();
    auto* finished = modal(D_Replay1, "replay-finished");
    if (!finished) return fail("scratch shot never finished playback");
    dialogClick(*finished, *finished->yesBtn); settle();
    if (app.screenName() != "mainmenu" || readReplay() != bytes) return fail("replay Done did not restore Home/file");
    player.maxStage = 3; player.advCompletions = 1;
    constexpr int score = 987654321;
    const std::string name = "WWWide Player";
    app.highScores.submit("_adventure", name, score);
    player.advScores = {score};
    auto checkScore = [&](Panel& p, const std::string& label, const std::string& who) {
      const auto draws = inspect(p);
      int found = 0;
      for (const auto& d : draws) if (d.text == who) {
        auto value = std::find_if(draws.begin(), draws.end(), [&](const Draw& v) { return v.text == "987,654,321" && v.bounds.l > d.bounds.l && std::abs(v.y - d.y) <= 5; });
        if (value != draws.end()) { ++found; bounds(label, d, 0, 0, value->bounds.l - 3, 600); }
      }
      if (!found) visualBoundsOk = fail((label + " score/name missing").c_str());
    };
    app.showAdventureScreen(); settle(); capture("adventure-scores");
    checkScore(*app.screen(), "adventure high scores", name);
    checkScore(*app.screen(), "personal bests", player.name);
    app.gameMode = GameMode::QuickPlay;
    app.showLevelScreen(false); settle(); frame(nullptr);
    auto* levels = dynamic_cast<LevelScreen*>(app.screen());
    if (!levels || !levels->pageStage() || levels->pageStage()->levels.empty()) return fail("Quick Play picker missing");
    app.highScores.submit(levels->pageStage()->levels[0].file, name, score);
    auto hover = [&](Widget& target) {
      Input in; in.type = Input::MouseMove; in.x = target.x + target.w / 2; in.y = target.y + target.h / 2; app.input(in);
    };
    hover(*levels->bLevel[0]); capture("level-scores");
    if (levels->hovered != 0) return fail("missing per-level high scores");
    checkScore(*levels, "level top five", name);
    app.gameMode = GameMode::Challenge;
    app.showTrophyScreen(); settle(); frame(nullptr);
    auto* trophies = dynamic_cast<TrophyScreen*>(app.screen());
    if (!trophies || trophies->ownPages() < 1 || trophies->theirPages() != 0) return fail("challenge inventory missing or nonisolated");
    int seen = 0, pickPage = -1, pickSlot = -1;
    for (int i = 0; i < trophies->ownPages(); ++i) {
      bool other;
      const auto* pg = trophies->pageShown(other);
      if (trophies->page != i || !pg || other || pg->trophies.empty() || pg->trophies.size() > 5) return fail("wrong challenge page");
      const auto page = "challenge-" + std::to_string(i + 1);
      Input leave; leave.type = Input::MouseMove; leave.x = 400; leave.y = 30; app.input(leave); settle();
      capture(page);
      const auto overview = inspect(*trophies);
      for (size_t j = 0; j < pg->trophies.size(); ++j) {
        const auto& entry = pg->trophies[j];
        auto& slot = *trophies->bSlot[j];
        const auto title = std::find_if(overview.begin(), overview.end(), [&](const Draw& d) { return d.font == "OVERLOAD16OUTLINE2" && d.text == entry.name; });
        if (title == overview.end()) return fail("missing challenge tile label");
        bounds(page, *title, slot.x + 8, slot.y, slot.x + slot.w - 8, slot.y + 64);
        player.trophyTop[entry.id] = score;
        hover(slot);
        if (trophies->hovered != int(j)) return fail("challenge hover mismatch");
        const auto detail = inspect(*trophies);
        std::string emitted, header;
        float lastTitle = 0, lastText = 0, scoreTop = 600;
        for (const auto& d : detail) {
          if (d.y < 320 || d.y > 550 || bare(d.text).empty()) continue;
          if (d.text.starts_with("Your Best:")) scoreTop = d.bounds.t;
          if (d.font == "PLAINOUTLINE") { emitted += d.text; bounds(page, d, 296, lastTitle, 504, 501); lastText = std::max(lastText, d.bounds.b); }
          if (d.font == "OVERLOAD16OUTLINE") { header += d.text; lastTitle = std::max(lastTitle, d.bounds.b); }
        }
        if (bare(emitted) != bare(entry.desc.empty() ? pg->desc : entry.desc) || bare(header) != bare(entry.name.empty() ? pg->name : entry.name) || scoreTop == 600 || lastText > scoreTop - 1) {
          std::cerr << page << " detail: " << entry.name << " text bottom " << lastText << " score top " << scoreTop << '\n'; visualBoundsOk = false;
        }
        if (pickPage < 0 && !entry.levels.empty() && entry.levels[0] == "pick") { pickPage = i; pickSlot = int(j); }
        if (j == 0 && i == 0) capture("challenge-with-best");
        ++seen;
      }
      key(app, SDLK_RIGHT);
    }
    if (trophies->page != 0 || pickPage < 0) return fail("challenge wrap or pick-level challenge missing");
    for (int i = 0; i < pickPage; ++i) key(app, SDLK_RIGHT);
    const auto& slot = *trophies->bSlot[pickSlot];
    canvasClick(slot.x + slot.w / 2, slot.y + slot.h / 2); settle();
    if (!dynamic_cast<LevelScreen*>(app.screen()) || app.gameMode != GameMode::Challenge) return fail("challenge level picker not reached");
    capture("challenge-level-picker");
    std::cout << (nights ? "Nights" : "Deluxe") << " challenge pages=" << app.trophies.pages().size() << " detail views=" << seen << '\n';
    app.cfg.otherDir = otherDir;
    app.cfg.otherUserDir = scratch / (nights ? "paired-deluxe" : "paired-nights");
    auto* peer = app.otherLevels();
    if (!peer) return fail("paired edition not mounted");
    auto* peerPlayer = peer->profiles.add(player.name);
    if (!peerPlayer) return fail("paired scratch roster failed");
    peerPlayer->advCompletions = 1; peerPlayer->maxStage = 3; peerPlayer->dirty = true;
    app.showTrophyScreen(); settle(); frame(nullptr);
    trophies = dynamic_cast<TrophyScreen*>(app.screen());
    if (!trophies || !trophies->theirPages()) return fail("paired challenge pages missing");
    for (int i = 0; trophies->page != trophies->ownPages() && i < trophies->ownPages() + trophies->theirPages(); ++i) key(app, SDLK_RIGHT);
    bool theirs;
    const auto* joined = trophies->pageShown(theirs);
    if (!joined || !theirs) return fail("paired page boundary");
    capture("challenge-other-edition");
    hover(*trophies->bSlot[0]);
    if (trophies->hovered != 0) return fail("paired detail not reached");
    capture("challenge-other-detail");
    const auto paired = inspect(*trophies);
    std::string desc;
    for (const auto& d : paired) if (d.font == "PLAINOUTLINE" && d.y > 320 && d.y < 510) {
      desc += d.text; bounds("paired detail", d, 296, 320, 504, 505);
    }
    if (bare(desc) != bare(joined->trophies[0].desc.empty() ? joined->desc : joined->trophies[0].desc)) return fail("paired description incomplete");
    auto markTrophies = [&] { for (const auto& pg : app.trophies.pages()) for (const auto& tr : pg.trophies) player.trophies.insert(tr.id); };
    auto markLevels = [&](auto& marks) { for (int i = 0; i < app.stages.numStages(); ++i) for (const auto& l : app.stages.stage(i)->levels) marks.insert(l.file); };
    if (!nights) {
      for (int tier = 0; tier < 4; ++tier) {
        if (tier == 1) markTrophies();
        if (tier == 2) markLevels(player.clearedLevels);
        app.showStoryScreen(true, tier == 3); settle();
        auto* story = dynamic_cast<StoryScreen*>(app.screen());
        if (!story || !story->win || story->tier != tier) return fail("Deluxe credits tier missing");
        for (int i = 0; i < 1100; ++i) app.tick();
        const auto page = "credits-roll-" + std::to_string(tier + 1);
        capture(page);
        std::string expected, emitted;
        for (const auto& line : app.stages.stage(app.numStages()-1)->credits[tier]) if (line.arg != 2) expected += line.text;
        int visible = 0;
        for (const auto& d : inspect(*story)) if (!bare(d.text).empty()) {
          emitted += d.text;
          bounds(page, d, 280, -10000, 546, 10000);
          visible += d.bounds.t >= 117 && d.bounds.b <= 433;
        }
        if (!visible || bare(emitted) != bare(expected)) return fail("Deluxe credits not visible/complete");
      }
    } else {
      for (int awards = 1; awards <= 4; ++awards) {
        if (awards == 2) markLevels(player.clearedLevels);
        if (awards == 3) markTrophies();
        if (awards == 4) markLevels(player.acedLevels);
        app.showProgressScreen(4); settle(); key(app, SDLK_RETURN); settle();
        auto* credits = dynamic_cast<CreditsScreen*>(app.screen());
        if (!credits || credits->pages.empty()) return fail("progress did not open scripted credits");
        const auto count = credits->pages.size();
        for (size_t i = 0; i < count; ++i) {
          for (int n = 0; n < 30; ++n) app.tick();
          if (app.screen() != credits || credits->page != i) return fail("credits skipped a page");
          const auto page = "credits-" + std::to_string(awards) + "-" + std::to_string(i + 1);
          if (awards == 1 || credits->pages[i].trophy > 0) capture(page);
          const auto draws = inspect(*credits);
          std::string emitted, expected;
          for (const auto& line : credits->pages[i].lines) expected += line.text;
          for (const auto& d : draws) if (!bare(d.text).empty()) { emitted += d.text; bounds(page, d, 0, 0, 800, 600); }
          if (bare(emitted) != bare(expected)) return fail("credits page missing content");
          key(app, SDLK_RETURN);
        }
        settle();
        if (app.screenName() != "mainmenu") return fail("credits did not return to Home");
        std::cout << "Nights awards=" << awards << " credits pages=" << count << '\n';
      }
    }
    std::cout << (nights ? "Nights" : "Deluxe") << " help pages=" << helpCount << " profile/replay cancel routes complete; fit " << (visualBoundsOk ? "PASS" : "FAIL") << '\n';
    return (captureOk || fail("could not save extended captures")) && visualBoundsOk;
  }
  const bool tour = !art.empty() && width == 1920 && mode == UiWide::Auto;
  const int canvas = widescreenCanvasWidth(mode, width, height);
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
    for (const auto& w : menu->widgets)
      if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->label == "Mods") return fail("Classic Home still shows Mods");
    canvasClick(40, 508);
    if (app.helpOverlay() || app.hasModal()) return fail("old Classic Mods hit target is active");
    const auto* options = menu->bOptions;
    canvasClick(options->x + options->w / 2, options->y + options->h / 2);
    settle();
    auto* d = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
    auto* mods = d ? dynamic_cast<Button*>(d->findWidget(12)) : nullptr;
    if (!mods || !mods->visible || mods->label != "Mods") return fail("Classic Options has no Mods button");
    frame("classic-options");
    dialogClick(*d, *mods);
    key(app, SDLK_ESCAPE);
    if (d->scrollingOff) return fail("opening Mods passed Escape through to Options");
    settle();
    if (!app.helpOverlay() || std::string(app.helpOverlay()->name()) != "mods") return fail("Options did not open Mods");
    frame("classic-mods");
    key(app, SDLK_ESCAPE);
    dialogClick(*d, *d->visuals);
    settle();
    if (app.helpOverlay() || app.topDialog() != d || d->scrollingOff || app.screen() != menu) return fail("Mods Back did not restore Classic Options");
    key(app, SDLK_ESCAPE);
    settle();
    if (mode == UiWide::Classic4x3) {
      app.settings.vis.ultraWideHud = true;
      if (app.frameCanvasWidth(800) != 800) return fail("Ultra-Wide HUD overrode Classic 4:3");
      if (app.frameCanvasWidth(1080) != 1080) return fail("Classic 4:3 narrowed an authored wide board");
    }
    key(app, SDLK_ESCAPE);
    return app.hasModal() || fail("classic Escape did not open quit confirmation");
  }
  {
    auto focused = [&]() -> std::string {
      for (const auto& w : app.screen()->widgets)
        if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->focused) return b->label;
      return {};
    };
    key(app, SDLK_RIGHT);
    if (focused() != "Profile") return fail("right did not reach Profile");
    key(app, SDLK_RIGHT);
    if (focused() != "Mods") return fail("right from Profile did not reach Mods");
    key(app, SDLK_TAB);
    if (focused() != "Quit") return fail("Tab from Mods did not reach Quit");
    key(app, SDLK_LEFT);
    key(app, SDLK_RETURN);
    settle();
    if (!app.helpOverlay() || std::string(app.helpOverlay()->name()) != "mods") return fail("Home confirm did not open Mods");
    key(app, SDLK_ESCAPE);
    settle();
    if (app.hasModal() || app.helpOverlay() || focused() != "Mods") return fail("Mods Back did not restore Home focus");
    key(app, SDLK_DOWN);
    if (focused() != "Single Player") return fail("footer exit did not restore Single Player focus");
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
      if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->over && b->label == "Multiplayer") hovered = b;
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
  }
  {
    std::vector<std::string> rows;
    for (const auto& w : app.screen()->widgets)
      if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->h == 50) rows.push_back(b->label);
    if (rows != std::vector<std::string>{"Adventure", "Quick Play", "Custom Levels", "Master Duel", "Challenge"})
      return fail("widescreen Single Player does not expose the requested menu order");
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
      settle();
      frame("edition-choice");
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
  if (fit) {
    key(app, SDLK_ESCAPE);
    settle();
    if (!app.getDialog(D_Quit)) return fail("Home Escape did not open Quit");
    frame("quit");
    key(app, SDLK_ESCAPE);
    settle();
  }
  canvasClick(canvas - 234.0f, 356);
  auto* options = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
  if (!options) return fail("Home Help & Options hit box did not open application Options");
  if (auto* mods = options->findWidget(12); mods && mods->visible) return fail("Options retained a duplicate Mods action");
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
    const bool savedColor = player.colorblind, savedFullscreen = app.settings.fullscreen;
    dialogClick(*options, *options->colorblind);
    dialogClick(*options, *options->fullscreen);
    for (int i = 0; i < 4; ++i) {
      dialogClick(*options, *options->widescreen);
      settle();
      frame(i == 3 ? "options-classic" : nullptr);
    }
    if (options->widescreen->label != "Widescreen: Classic 4:3" || gfx.canvasW() != 800)
      return fail("Options did not switch to Classic 4:3");
    auto* mods = visibleButton(*options, "Mods");
    if (!mods || app.topDialog() != options || options->w != 440) return fail("switching open widescreen Options to Classic did not expose Mods");
    dialogClick(*options, *mods);
    settle();
    if (!app.helpOverlay() || std::string(app.helpOverlay()->name()) != "mods") return fail("switched Classic Mods did not open");
    key(app, SDLK_ESCAPE);
    settle();
    if (app.helpOverlay() || app.topDialog() != options || options->colorblind->checked == savedColor ||
        options->fullscreen->checked == savedFullscreen || player.colorblind != savedColor || app.settings.fullscreen != savedFullscreen)
      return fail("Mods Back lost the original Options or applied its pending settings");
    dialogClick(*options, *options->widescreen);
    key(app, SDLK_TAB);
    if (app.widescreenMode() != UiWide::Auto || app.topDialog() != options || options->w != 720 ||
        visibleButton(*options, "Mods") || !options->focusW || !options->focusW->visible)
      return fail("widening the same Classic Options retained Mods or invalid focus before redraw");
    for (int i = 0; i < 4; ++i) dialogClick(*options, *options->widescreen);
    key(app, SDLK_TAB);
    if (app.topDialog() != options || options->w != 440 || !visibleButton(*options, "Mods"))
      return fail("returning the same Options to Classic did not restore Mods before redraw");
    app.cfg.noSave = false;
    dialogClick(*options, *options->yesBtn);
    app.cfg.noSave = true;
    Settings saved;
    if (player.colorblind == savedColor || app.settings.fullscreen == savedFullscreen) return fail("Done lost pending Options changes");
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
    if (app.topDialog() != options || visibleButton(*options, "Mods")) return fail("returning to Auto left Mods in widescreen Options");
    dialogClick(*options, *options->editor);
    settle();
    if (app.screenName() != "editor") return fail("Options Level Editor button did not open the editor");
    frame("editor");
    canvasClick(106, 26);
    const auto browserLabels = inspect(*app.screen());
    if (std::none_of(browserLabels.begin(), browserLabels.end(), [](const auto& d) { return d.text.starts_with("Open a level"); }))
      return fail("the editor Open button did not show the level browser");
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
      dialogClick(*options, *options->yesBtn);
      settle();
      frame(nullptr);
      canvasClick(canvas - 173.0f, 565);
      settle();
      if (!app.helpOverlay() || std::string(app.helpOverlay()->name()) != "mods") return fail("Home Mods button did not open Mods");
      frame("mods");
      {
        auto font = std::make_unique<StoryTestFont>(renderer.get(), gfx);
        auto* recorder = font.get();
        gfx.setFontProvider(std::move(font));
        app.tick();
        frame(nullptr);
        const bool sleeve = std::any_of(recorder->draws.begin(), recorder->draws.end(), [](const auto& d) { return d.text == "Select" || d.text == "Quit"; });
        gfx.setFontProvider(makeDefaultFontProvider(renderer.get(), &gfx));
        if (sleeve) return fail("Home footer prompts show through the Mods screen");
      }
      key(app, SDLK_ESCAPE);
      settle();
      if (app.hasModal() || app.helpOverlay() || app.screenName() != "mainmenu") return fail("Mods Escape did not restore Home");
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
  const std::string replayName = fit ? std::string(42, 'W') : "Sample replay";
  if (tour) {
    ReplayData replay;
    replay.game = edition == Game::Nights ? 1 : 0;
    replay.level = replay.game ? "Bjorn1" : "Level1";
    replay.title = replay.game ? "Crimewave" : "Peggleland";
    replay.player = player.name;
    if (!replay.save(app.userDir / "replays" / (replayName + replayExt(replay.game))))
      return fail("could not write isolated replay fixture");
  }
  canvasClick(canvas - 234.0f, 232);
  if (!app.getDialog(D_Tip) && !app.getDialog(D_Replay2)) return fail("Replays did not open the original replay UI");
  if (tour) {
    if (!app.getDialog(D_Replay2)) return fail("Replays did not list the fixture");
    settle();
    frame("replays");
    if (fit) {
      auto* dialog = app.getDialog(D_Replay2);
      ListBox* list = nullptr;
      EditBox* edit = nullptr;
      for (const auto& widget : dialog->widgets) {
        if (auto* l = dynamic_cast<ListBox*>(widget.get())) list = l;
        if (auto* e = dynamic_cast<EditBox*>(widget.get())) edit = e;
      }
      if (!list || !edit || list->lines.empty()) return fail("replay filename controls missing");
      const auto it = std::find(list->lines.begin(), list->lines.end(), replayName);
      if (it == list->lines.end()) return fail("long replay fixture missing");
      list->onClick(static_cast<int>(it - list->lines.begin()), 1);
      if (edit->value != replayName) return fail("selecting replay lost its full filename");
      const float tw = gfx.textWidth(edit->font, edit->value);
      if (tw <= edit->w || edit->textShift(gfx) + edit->w - edit->textPad * 2 < tw)
        visualBoundsOk = fail("long selected replay name crosses the paper field instead of scrolling to the caret");
      frame("long-replay-name");
      key(app, SDLK_HOME);
      if (edit->textShift(gfx) != 0 || edit->value != replayName) return fail("Home did not reveal the start of the complete replay name");
    }
  }
  app.killDialog(D_Tip);
  app.killDialog(D_Replay2);
  settle();
  if (tour && height == 1080) {
    std::ofstream prefs(app.userDir.parent_path() / "online.ini");
    prefs << "lobbyset=1\nlobby=127.0.0.1:0\n";
    prefs.close();
    if (!prefs) return fail("could not write offline multiplayer fixture");
    auto visibleButton = [&](const char* label) { return ::visibleButton(*app.screen(), label); };
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
    if (app.screenName() != "mainmenu" || !visibleButton("Online Peg Party") || !visibleButton("Join With Code"))
      return fail("Multiplayer did not open the multiplayer menu");
    frame("multiplayer");
    row(0);
    if (!visibleButton("Host Peg Party") || visibleButton("Join With Code")) return fail("Online Peg Party did not open its picker");
    frame("online-party");
    if (vfs.exists("console/menu/header_rail.png")) {
      Uint8 r, g, b, a;
      if (!SDL_ReadSurfacePixel(surf.get(), int(gfx.offX() + (canvas - 12) * gfx.scale()), int(gfx.offY() + 36 * gfx.scale()), &r, &g, &b, &a) ||
          g < 120 || b < 185) visualBoundsOk = fail("online mode picker is missing the right header rail");
    }
    if (!offline) {
      row(1);
      if (app.screenName() != "online") return fail("Custom Match did not open the online screen");
      if (gfx.canvasW() != canvas) return fail("Multiplayer used the Classic canvas in 16:9");
      if (visibleButton("Create Room") || visibleButton("Refresh")) return fail("Custom Match showed the Classic room browser buttons");
      frame("custom-match");
      back();
      if (app.screenName() != "mainmenu" || !visibleButton("Host Peg Party")) return fail("Back from Custom Match did not return to the Peg Party picker");
      row(2);
      if (app.screenName() != "online" || !visibleButton("Host Match") || visibleButton("Cancel")) return fail("Host Peg Party did not open the party options");
      frame("create-room");
      back();
      back();
      if (!visibleButton("Join With Code")) return fail("Back from the Peg Party picker did not return to the multiplayer menu");
      row(3);
      if (app.screenName() != "online" || !visibleButton("Paste Invite") || !visibleButton("Join")) return fail("Join With Code did not open the code entry");
      frame("join-code");
      back();
    } else back();
    row(1);
    if (!visibleButton("Host Duel")) return fail("Online Duel did not open its picker");
    frame("online-duel");
    back();
    row(2);
    if (!app.getDialog(D_Locked)) return fail("local duel did not apply the profile unlock");
    auto* lock = app.getDialog(D_Locked);
    if (lock->yesBtn->font != "OVERLOAD16OUTLINE") visualBoundsOk = fail("locked OK uses the small PC patch label");
    if (lock->yesBtn->w > 200 || lock->lines.find("first stage") == std::string::npos || !lock->yesBtn->focused)
      visualBoundsOk = fail("local Duel lock is missing the concise message or compact focused OK");
    frame("local-duel-locked");
    if (visibleButton("Online Peg Party") || visibleButton("Profile")) visualBoundsOk = fail("locked modal did not clear its underlying actions");
    key(app, SDLK_RETURN);
    settle();
    if (app.getDialog(D_Locked) || !visibleButton("Same Console Duel") || !visibleButton("Same Console Duel")->focused)
      return fail("accepting the lock did not restore the local Duel selection");
    back();
    if (!visibleButton("Multiplayer")) return fail("Back from the multiplayer menu did not return home");
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
    auto enterNightsHub = [&] {
      canvasClick(canvas - 202.0f, 217);
      settle();
      if (auto* d = app.getDialog(D_WhichAdventure)) {
        frame(nullptr);
        dialogClick(*d, *d->noBtn);
        settle();
      }
      return visibleButton(*app.screen(), "Lounge") && !app.hasModal();
    };
    frame("single-resume");
    if (!enterNightsHub()) return fail("Adventure did not reach the Nights Hub");
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
    auto* lounge = static_cast<MainMenu*>(app.screen());
    auto* gear = lounge->bOptions;
    canvasClick(gear->x + gear->w / 2, gear->y + gear->h / 2);
    settle();
    frame(nullptr);
    auto* loungeOptions = dynamic_cast<OptionsDialog*>(app.topDialog());
    auto* loungeMods = loungeOptions ? visibleButton(*loungeOptions, "Mods") : nullptr;
    if (!loungeMods) return fail("wide Nights Lounge Options has no Classic Mods action");
    dialogClick(*loungeOptions, *loungeMods);
    settle();
    if (!app.helpOverlay() || std::string(app.helpOverlay()->name()) != "mods") return fail("Lounge Mods did not open");
    key(app, SDLK_ESCAPE);
    settle();
    if (app.helpOverlay() || app.topDialog() != loungeOptions) return fail("Mods Back lost Lounge Options");
    key(app, SDLK_ESCAPE);
    settle();
    if (app.hasModal() || app.screen() != lounge || !lounge->bSolo->visible || lounge->canvasWidth() != 800)
      return fail("Options Back did not return to the Lounge");
    key(app, SDLK_ESCAPE);
    settle();
    frame("nights-hub-returned");
    if (gfx.canvasW() != canvas || app.hasModal() || !visibleButton(*app.screen(), "Lounge")) return fail("Escape did not return from the lounge to the Nights hub");
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
      key(app, SDLK_RETURN);
      settle();
      if (!enterNightsHub()) return fail("could not reopen the Nights Hub");
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
        if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->label == "Trophy Room") returnedToHub = true;
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
  if ((tour && canvas == 1067) || mode == UiWide::W16x9) {
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
    auto* pause = static_cast<OptionsDialog*>(app.getDialog(D_Options));
    if (pause->findWidget(12)) return fail("widescreen Pause has a Mods action");
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
    if (fit) {
      dialogClick(*app.getDialog(D_Options), *pause->newLevel);
      settle();
      auto* question = app.getDialog(D_RestartLevel);
      if (!question) return fail("Pause did not open Restart Level");
      if (question->noBtn->label != "Cancel" || question->yesBtn->font != "OVERLOAD16OUTLINE")
        visualBoundsOk = fail("Restart actions lost console lettering or casing");
      frame("restart-confirm");
      key(app, SDLK_ESCAPE);
      settle();
      if (app.getDialog(D_RestartLevel) || app.getDialog(D_Options) != pause) return fail("Restart cancellation lost Pause");
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

static bool adventureRoute(const fs::path& deluxe, const fs::path& nights, const fs::path& scratch,
                           Game home, Game wanted, bool paired, bool back) {
  GameRequest request;
  request.game = home;
  for (int hop = 0; hop < 3; ++hop) {
    const Game edition = request.game;
    auto fail = [&](const char* message) {
      std::cerr << "Adventure from " << gameTitle(home) << " to " << gameTitle(wanted) << " paired=" << paired
                << " at " << gameTitle(edition) << ": " << message << '\n';
      return false;
    };
    Vfs vfs;
    if (!vfs.mountGame(edition == Game::Nights ? nights : deluxe, nullptr)) return fail("mount failed");
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(1067, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    if (!renderer) return fail(SDL_GetError());
    Gfx gfx(renderer.get(), vfs);
    Renderer board(renderer.get(), vfs);
    App::Config cfg;
    cfg.game = edition; cfg.home = home; cfg.bothGames = paired;
    cfg.widescreen = static_cast<int>(UiWide::W16x9);
    cfg.userDir = scratch / (edition == Game::Nights ? "nights" : "deluxe") / "profile";
    if (paired) {
      cfg.otherDir = edition == Game::Nights ? deluxe : nights;
      cfg.otherUserDir = scratch / (edition == Game::Nights ? "deluxe" : "nights") / "profile";
    }
    cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = true;
    cfg.start = request.start;
    PlayerInfo player;
    player.storyPending = false;
    App app(gfx, vfs, nullptr, &board, cfg);
    app.curProfile = &player;
    auto settle = [&] { for (int i = 0; i < 60; ++i) app.tick(); app.frame(); };
    auto single = [&] { return app.screen() && visibleButton(*app.screen(), "Custom Levels"); };
    if (hop) app.startAt(request.start, nullptr, false);
    else app.showMainMenu();
    settle();
    if (hop == 2) return (edition == home && single() && !app.hasModal()) || fail("switch Back missed Single Player");
    if (!hop) {
      key(app, SDLK_RETURN); settle();
      if (!single()) return fail("Single Player missing");
      key(app, SDLK_RETURN); settle();
      Dialog* choice = app.getDialog(D_WhichAdventure);
      if (paired != bool(choice)) return fail("wrong installed-edition choices");
      if (choice) {
        auto* button = visibleButton(*choice, wanted == Game::Nights ? "Peggle Nights" : "Peggle Deluxe");
        if (!button) return fail("edition not offered");
        click(app, gfx.offX() + ((gfx.canvasW() - 800) / 2.f + choice->x + button->x + button->w / 2) * gfx.scale(),
                   gfx.offY() + (choice->y + button->y + button->h / 2) * gfx.scale());
        settle();
      }
      if (wanted != home) {
        if (!app.takeGameSwitchRequest(request) || request.game != wanted) return fail("wrong edition handoff");
        continue;
      }
    }
    if (wanted == Game::Nights) {
      if (app.hasModal() || !app.screen() || !visibleButton(*app.screen(), "Lounge") || app.takeGameSwitchRequest(request))
        return fail("Nights choice did not stay on the Nights Hub");
      if (back) {
        key(app, SDLK_ESCAPE); settle();
        if (edition == home) return (single() && !app.hasModal()) || fail("Hub Back missed Single Player");
        if (!app.takeGameSwitchRequest(request) || request.game != home) return fail("Hub Back did not request home edition");
        continue;
      }
      key(app, SDLK_RETURN); settle();
    }
    auto* gs = dynamic_cast<GameScreen*>(app.screen());
    return (gs && gs->ok() && app.gameMode == GameMode::Adventure && !app.takeGameSwitchRequest(request)) || fail("Adventure did not start a board");
  }
  return false;
}

int main(int argc, char** argv) {
  const bool routes = argc == 6 && std::string(argv[5]) == "--navigation";
  const bool extended = argc == 6 && std::string(argv[5]) == "--extended-fit";
  const bool offline = extended || (argc == 6 && std::string(argv[5]) == "--offline-fit");
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
  if ((argc != 5 && !offline && !routes) || !SDL_Init(SDL_INIT_VIDEO)) return 2;
  bool ok = true;
  for (const auto edition : {Game::Deluxe, Game::Nights}) {
    const auto& gameDir = argv[edition == Game::Deluxe ? 1 : 2];
    if (routes || argc == 5) {
      ok &= autoResizeOptions(gameDir, argv[4], edition);
      for (bool paired : {false, true}) {
        for (auto wanted : {Game::Deluxe, Game::Nights}) {
          if (!paired && wanted != edition) continue;
          ok &= adventureRoute(argv[1], argv[2], argv[4], edition, wanted, paired, false);
          if (wanted == Game::Nights) ok &= adventureRoute(argv[1], argv[2], argv[4], edition, wanted, paired, true);
        }
      }
      if (routes) {
        ok &= probe(gameDir, {}, argv[4], edition, 800, 600, UiWide::Classic4x3);
        ok &= probe(gameDir, argv[3], argv[4], edition, 1067, 600, UiWide::W16x9, true);
        ok &= probe(gameDir, argv[3], argv[4], edition, 960, 600, UiWide::W16x10, true);
        continue;
      }
    }
    if (offline) {
      ok &= probe(gameDir, argv[3], argv[4], edition, 1920, 1080, UiWide::Auto, true, extended ? argv[edition == Game::Deluxe ? 2 : 1] : "");
      continue;
    }
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
