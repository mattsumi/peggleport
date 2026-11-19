#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "ui/app.h"
#include "ui/cfg.h"
#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/log_view.h"
#include "ui/screens.h"
#include "ui/replay.h"
#include "ui/visuals_dialog.h"

using namespace pp;
using namespace pp::ui;

static Input event(Input::Type type, float x, float y) {
  Input in;
  in.type = type;
  in.x = x;
  in.y = y;
  return in;
}

static void click(App& app, Panel& panel, Widget& widget, float localX = -1) {
  float x = panel.x + widget.x + (localX < 0 ? widget.w / 2 : localX);
  float y = panel.y + widget.y + widget.h / 2;
  app.input(event(Input::MouseDown, x, y));
  app.input(event(Input::MouseUp, x, y));
}

static void settle(App& app) {
  for (int i = 0; i < 100; ++i) app.tick();
  app.frame();
}

struct LogTestFont : FontProvider {
  struct Line { float y; uint32_t color; std::string text; };
  std::vector<Line> lines;
  std::unique_ptr<FontProvider> font;
  float height(const std::string& n) override { return font->height(n); }
  float ascent(const std::string& n) override { return font->ascent(n); }
  float lineSpacing(const std::string& n) override { return font->lineSpacing(n); }
  float width(const std::string& n, const std::string& s) override { return font->width(n, s); }
  void draw(const std::string& n, float x, float y, const std::string& s, uint32_t rgb, float alpha) override {
    if (n == "PLAIN") lines.push_back({y, rgb, s});
    font->draw(n, x, y, s, rgb, alpha);
  }
};

static bool challengeGroupLabels() {
  const auto items = cfgParse("Stage\n{\nName: Garden\n}\nPage \"Pack Page\" {\n"
      "Trophy \"First Tile\" {\nId: 501\n}\nTrophy Fern\r\n{\nId: 502\n}\nTrophy Ivy {\nId: 503\n}\n}\n");
  if (items.size() != 2 || items[0].key != "Stage" || !items[0].label.empty() ||
      items[1].key != "Page" || items[1].label != "Pack Page" || items[1].kids.size() != 3) return false;
  const char* names[] = {"First Tile", "Fern", "Ivy"};
  for (int i = 0; i < 3; ++i) {
    const auto& t = items[1].kids[i];
    if (!t.isGroup || t.key != "Trophy" || t.label != names[i] || t.kids.size() != 1 || t.kids[0].key != "Id" ||
        cfgValues(t.kids[0].label) != std::vector<std::string>{std::to_string(501 + i)}) return false;
  }
  return true;
}

int main(int argc, char** argv) {
  if (!challengeGroupLabels()) { std::cerr << "FAIL quoted and bare challenge group labels\n"; return 1; }
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <Deluxe asset directory> [Nights asset directory]\n";
    return 2;
  }
  if (!SDL_Init(0)) { std::cerr << SDL_GetError() << '\n'; return 2; }
  const char* tmp = std::getenv("TMPDIR");
  auto userDir = (tmp && *tmp ? std::filesystem::path(tmp) : std::filesystem::temp_directory_path()) / "peggle-ui-controls-test" / "userdata";
  int checks = 0, failures = 0;
  std::string testCase;
  auto check = [&](bool pass, const char* name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << testCase << ": " << name << '\n'; }
    return pass;
  };
  for (int arg = 1; arg < argc; ++arg) {
    Game game = arg == 1 ? Game::Deluxe : Game::Nights;
    Vfs vfs;
    std::string reason;
    if (!vfs.mountGame(argv[arg], &reason)) { std::cerr << reason << '\n'; return 2; }
    for (bool both : {false, true}) {
      testCase = std::string(game == Game::Deluxe ? "Deluxe" : "Nights") + " bothGames=" + (both ? "true" : "false");
      std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
      if (!surface) { std::cerr << SDL_GetError() << '\n'; return 2; }
      std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
      if (!renderer) { std::cerr << SDL_GetError() << '\n'; return 2; }
      Gfx gfx(renderer.get(), vfs);
      App::Config cfg;
      cfg.game = game;
      cfg.home = game;
      cfg.bothGames = both;
      cfg.noSave = true;
      cfg.noAudio = true;
      cfg.noRoomDiscovery = true;
      cfg.userDir = userDir;
      PlayerInfo profile;
      App app(gfx, vfs, nullptr, nullptr, cfg);
      if (!both) {
        auto recording = std::make_unique<LogTestFont>();
        auto* font = recording.get();
        font->font = makeDefaultFontProvider(renderer.get(), &gfx);
        gfx.setFontProvider(std::move(recording));
        LogView view(app);
        view.setMode(LogView::Mode::Full);
        auto messages = [&] {
          font->lines.clear();
          gfx.begin();
          gfx.clear();
          view.drawView(gfx);
          SDL_RenderPresent(renderer.get());
          std::vector<LogTestFont::Line> out;
          for (const auto& line : font->lines) if (line.color == 0xffffff) out.push_back(line);
          return out;
        };
        const std::string folder(25, 'M');
        const std::string path = "C:/" + folder + "/" + folder + "/" + folder + "/TestLevel.dat";
        log::clear();
        log::info("fixture", path);
        auto wrapped = messages();
        for (auto mode : {LogView::Mode::Full, LogView::Mode::Docked}) {
          view.setMode(mode);
          bool capsFit = view.widgets.size() == 9, layoutFits = capsFit;
          for (size_t i = 0; i < view.widgets.size(); ++i) {
            auto* b = dynamic_cast<Button*>(view.widgets[i].get());
            capsFit = capsFit && b && b->image && b->w >= 2 * std::floor(b->image->lw() / 3);
            if (!b) { layoutFits = false; continue; }
            layoutFits = layoutFits && b->x >= 0 && b->y >= 0 && b->x + b->w <= view.w && b->y + b->h <= view.h;
            if (i > 0) {
              const auto& prev = *view.widgets[i - 1];
              layoutFits = layoutFits && ((b->y == prev.y && b->x >= prev.x + prev.w) || b->y >= prev.y + prev.h);
            }
          }
          check(capsFit, "all log toolbar buttons fit both native caps");
          check(layoutFits, "log toolbar wraps without overlapping buttons or crossing the panel edge");
        }
        view.setMode(LogView::Mode::Full);
        std::string joined;
        bool breaks = wrapped.size() > 1;
        for (size_t i = 0; i < wrapped.size(); ++i) {
          joined += wrapped[i].text;
          if (i + 1 < wrapped.size()) breaks = breaks && wrapped[i].text.ends_with('/');
        }
        check(breaks && joined == path, "log wraps a long path at directory boundaries without losing characters");
        for (int i = 0; i < 80; ++i) log::info("fixture", "Sample " + std::to_string(i));
        auto followed = messages();
        const int thumbX = (int)(view.x + view.w - 12);
        int thumbY = -1;
        for (int y = (int)view.y + 45; y < surface->h - 30; ++y) {
          const auto* p = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch + thumbX * 4;
          if (p[1] > 100 && p[1] > p[0] + 35 && p[1] > p[2] + 35) { thumbY = y + 3; break; }
        }
        if (check(thumbY >= 0 && !followed.empty(), "following log exposes its scrollbar thumb")) {
          view.handle(event(Input::MouseDown, (float)thumbX, (float)thumbY));
          auto grabbed = messages();
          check(!grabbed.empty() && grabbed.front().text == followed.front().text, "grabbing a log scrollbar thumb does not move it");
          view.handle(event(Input::MouseMove, (float)thumbX, (float)thumbY - 50));
          auto dragged = messages();
          check(!dragged.empty() && dragged.front().text != followed.front().text, "dragging the log scrollbar scrolls back");
          view.handle(event(Input::MouseUp, (float)thumbX, (float)thumbY - 50));
        }
        log::clear();
        log::info("fixture", path);
        const std::string expectedTime = log::timeText(log::snapshot().front());
        for (size_t i = 1; i < followed.size(); ++i) log::info("queue", "Sample " + std::to_string(i));
        Input end;
        end.type = Input::KeyDown;
        end.key = SDLK_END;
        view.handle(end);
        auto continued = messages();
        bool startsWithTail = !continued.empty() && !wrapped.empty() && continued.front().text == wrapped.back().text;
        check(startsWithTail, "log fixture starts midway through a wrapped entry");
        auto hasHeader = [&](uint32_t color, const std::string& text) {
          return startsWithTail && std::any_of(font->lines.begin(), font->lines.end(), [&](const auto& line) {
            return line.y == continued.front().y && line.color == color && line.text == text;
          });
        };
        check(hasHeader(0x8c9ab0, expectedTime) && hasHeader(0x9fc0e8, "[fixture]"), "a visible log continuation keeps its timestamp and source");
        check(std::any_of(font->lines.begin(), font->lines.end(), [](const auto& line) { return line.text.find("entries match") != std::string::npos; }),
              "log footer describes matching entries rather than screen lines");
        gfx.setFontProvider(makeDefaultFontProvider(renderer.get(), &gfx));
      }
      profile.reset();
      profile.name = "UI Test";
      app.curProfile = &profile;
      app.showMainMenu();
      settle(app);
      auto* menu = dynamic_cast<MainMenu*>(app.screen());
      if (!check(menu != nullptr, "main menu opens")) return 1;
      int demos = 0, onlines = 0;
      Button* online = nullptr;
      for (auto& widget : menu->widgets) {
        if (auto* button = dynamic_cast<Button*>(widget.get())) {
          if (button->label == "Demo") ++demos;
          if (button->label == "Online") { ++onlines; online = button; }
        }
      }
      check(demos == 0, "main menu has no Demo");
      check(onlines == 1 && online->visible && online->enabled, "main menu has exactly one available Online");
      if (game == Game::Deluxe) {
        Tex* footer = gfx.img("IMAGE_DLG_BROWNBUTTON1");
        check(online && footer && online->x == 0x6c && online->y == 0x22a && online->w == footer->lw() && online->h == footer->lh() / 3, "Deluxe Online retains original Demo footer bounds");
      } else {
        auto* stats = dynamic_cast<Button*>(menu->findWidget(9));
        check(stats && stats->label == "Stats" && stats->visible && stats->enabled && stats->x == 0x6c && stats->y == 0x22a, "Nights retains Stats in footer");
      }
      auto keypress = [&](int code) {
        Input in;
        in.type = Input::KeyDown;
        in.key = code;
        app.input(in);
        settle(app);
      };
      auto speechPixels = [&] {
        gfx.begin();
        gfx.clear();
        menu->drawSpeechBubble(gfx, 0, 1, true);
        SDL_RenderPresent(renderer.get());
        int pixels = 0;
        for (int y = 0; y < surface->h; ++y) {
          auto* row = static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch;
          for (int x = 0; x < surface->w; ++x)
            pixels += row[x * 4] || row[x * 4 + 1] || row[x * 4 + 2];
        }
        return pixels;
      };
      if (online) {
        for (float dx : {1.0f, online->w - 1}) {
          app.input(event(Input::MouseMove, menu->x + online->x + dx, menu->y + online->y + online->h / 2));
          check(menu->hover == online, "Online native hit target reaches both inside edges");
        }
        app.input(event(Input::MouseMove, menu->x + online->x - 1, menu->y + online->y + online->h / 2));
        check(menu->hover != online, "Online native hit target excludes outside edge");
      }
      app.input(event(Input::MouseMove, 40, 40));
      settle(app);
      if (game == Game::Nights) {
        for (float alpha : {1.0f, 0.95f}) {
          auto boxPixels = [&](int pointer) {
            gfx.begin();
            gfx.clear();
            drawSpeechBox(gfx, 100, 100, 230, 80, alpha, pointer);
            SDL_RenderPresent(renderer.get());
            std::vector<uint8_t> pixels;
            for (int y = 100; y < 160; ++y) {
              const auto* row = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
              pixels.insert(pixels.end(), row + 100 * 4, row + 330 * 4);
            }
            return pixels;
          };
          auto plain = boxPixels(0);
          check(plain == boxPixels(1), "speech tail does not overwrite the bubble text area");
          int tailPixels = 0;
          for (int y = 180; y < 220; ++y) {
            const auto* row = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
            for (int x = 100; x < 330; ++x)
              tailPixels += row[x * 4] || row[x * 4 + 1] || row[x * 4 + 2];
          }
          check(tailPixels > 200, "speech tail extends below the bubble instead of inside it");
          auto rightTail = [&](bool reference) {
            gfx.begin();
            gfx.clear();
            if (reference) gfx.drawCelMirror(gfx.img("IMAGE_TROPHY_SPEECH_BP"), 229, 160, 0, 0, Mode(alpha));
            else drawSpeechBox(gfx, 100, 100, 230, 80, alpha, 1, 180);
            SDL_RenderPresent(renderer.get());
            std::vector<uint8_t> pixels;
            for (int y = 180; y < 220; ++y) {
              const auto* row = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
              pixels.insert(pixels.end(), row + 100 * 4, row + 330 * 4);
            }
            return pixels;
          };
          check(rightTail(false) == rightTail(true), "right-side tooltip tail uses native mirrored art toward its target");
        }
        Tex* footer = gfx.img("IMAGE_DLG_BROWNBUTTON1");
        check(online && footer && online->x == 0x1a1 && online->y == 0x22a && online->w == footer->lw() && online->h == footer->lh() / 3 && online->image == footer, "Nights Online uses the exact former Replays native footer slot");
        int replays = 0;
        for (auto& widget : menu->widgets)
          if (auto* b = dynamic_cast<Button*>(widget.get()); b && b->label == "Replays") ++replays;
        check(replays == 0, "Nights main menu no longer duplicates Replays");
        const DecalInfo* spring = nullptr;
        for (int i = app.stages.numStages(); i < app.stages.numAllStages(); ++i)
          if (const auto* stage = app.stages.stage(i))
            for (const auto& decal : stage->decals)
              if (decal.hover.find("Spring Fever") != std::string::npos) spring = &decal;
        if (check(spring != nullptr, "real Nights assets expose Spring Fever hover target")) {
          app.input(event(Input::MouseMove, spring->hx + spring->hw / 2.0f, spring->hy + spring->hh / 2.0f));
          app.frame();
          SDL_RenderPresent(renderer.get());
          auto lightPixel = [&](int x, int y) {
            const auto* pixel = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch + x * 4;
            return pixel[0] > 200 && pixel[1] > 190 && pixel[2] > 150;
          };
          const int sunX = spring->hx + spring->hw / 2;
          check(lightPixel(sunX + 8, spring->hy - 14), "Spring Fever tail points just above the sun hit target");
          check(lightPixel(sunX, spring->hy - 96), "Spring Fever body reserves full corner height and external tail space");
          const int bodyCenter = spring->hy - 66;
          int firstInk = surface->h, lastInk = -1;
          for (int y = bodyCenter - 26; y <= bodyCenter + 30; ++y) {
            const auto* row = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
            int ink = 0;
            for (int x = sunX - 95; x < sunX + 95; ++x)
              ink += std::max({row[x * 4], row[x * 4 + 1], row[x * 4 + 2]}) < 103;
            if (ink < 3) continue;
            firstInk = std::min(firstInk, y);
            lastInk = y;
          }
          check(lastInk - firstInk >= 28 && std::abs(firstInk + lastInk - 2 * bodyCenter) <= 4,
                "Spring Fever text ink is vertically centered in the bubble body");
          NewUserDialog validation(app, false, false, true);
          std::string longestName(validation.edit->maxChars, 'i');
          for (size_t i = 0; i < longestName.size(); ++i) {
            longestName[i] = 'W';
            if (gfx.textWidth(validation.edit->widthFont, longestName) > validation.edit->maxWidth) longestName[i] = 'i';
          }
          for (const std::string& name : {std::string("A"), longestName}) {
            check(name.size() <= validation.edit->maxChars && gfx.textWidth(validation.edit->widthFont, name) <= validation.edit->maxWidth, "hover fixture profile name is allowed by native input limits");
            profile.name = name;
            menu->syncPlayerInfo();
            app.input(event(Input::MouseMove, 40, 40));
            settle(app);
            check(menu->bLink->visible && speechPixels() > 0, "welcome plaque and profile link appear without tooltip");
            app.input(event(Input::MouseMove, spring->hx + spring->hw / 2.0f, spring->hy + spring->hh / 2.0f));
            const int bubbleCount = menu->bubbleCount, bubbleFade = menu->bubbleFade, sunTick = menu->sunTick;
            app.frame();
            check(!menu->bLink->visible && speechPixels() == 0, "Spring Fever frame suppresses welcome and profile link before a tick");
            check(menu->bubbleCount == bubbleCount && menu->bubbleFade == bubbleFade && menu->sunTick == sunTick, "rendering hover does not advance bubble or animation timers");
            app.tick();
            check(!menu->bLink->visible, "Spring Fever tooltip suppresses overlapping profile link on first tick");
            check(speechPixels() == 0, "Spring Fever tooltip suppresses overlapping welcome plaque");
            settle(app);
            app.input(event(Input::MouseMove, 40, 40));
            app.frame();
            check(menu->bLink->visible && speechPixels() > 0, "mouse-out frame restores welcome and profile link before a tick");
            app.tick();
            check(menu->bLink->visible && speechPixels() > 0, "mouse-out restores welcome and profile link");
          }
          profile.name = "UI Test";
          profile.advCompletions = 1;
          app.input(event(Input::MouseMove, 450, 200));
          app.frame();
          check(!menu->bLink->visible && speechPixels() == 0, "mantel hover frame suppresses welcome before a tick");
          app.tick();
          check(!menu->bLink->visible && speechPixels() == 0, "mantel tooltip also replaces overlapping welcome content");
          app.doDialog(D_Tip, true, "QA", "Modal hover guard", "OK", BUTTONS_FOOTER);
          app.frame();
          check(menu->bLink->visible && speechPixels() > 0, "modal frame restores welcome before a tick");
          settle(app);
          check(speechPixels() > 0, "modal suppresses contextual tooltip without hiding welcome");
          keypress(SDLK_ESCAPE);
          profile.advCompletions = 0;
          app.input(event(Input::MouseMove, online->x + online->w / 2, online->y + online->h / 2));
          settle(app);
          check(menu->bubble == 5, "Nights Online footer uses Online hover help instead of Replays help");
          app.input(event(Input::MouseDown, spring->hx + spring->hw / 2.0f, spring->hy + spring->hh / 2.0f));
          app.input(event(Input::MouseUp, spring->hx + spring->hw / 2.0f, spring->hy + spring->hh / 2.0f));
          settle(app);
          check(std::string(app.screen()->name()) == "levelscreen" && app.packStage >= app.stages.numStages(), "Spring Fever click still opens its native level pack");
          keypress(SDLK_ESCAPE);
          menu = dynamic_cast<MainMenu*>(app.screen());
          if (!check(menu != nullptr, "Spring Fever level picker returns to main menu")) return 1;
        }
        click(app, *menu, *menu->findWidget(9));
        settle(app);
        auto* stats = dynamic_cast<AdventureScreen*>(app.screen());
        if (check(stats != nullptr, "Nights Stats opens from native footer")) {
          Button* replay = nullptr;
          for (auto& widget : stats->widgets)
            if (auto* b = dynamic_cast<Button*>(widget.get()); b && b->label == "Replays") replay = b;
          if (check(replay && replay->visible && replay->enabled, "Stats exposes Replays")) {
            check(stats->bMenu->x + stats->bMenu->w <= replay->x && replay->x + replay->w <= stats->bContinue->x && replay->y == stats->bMenu->y && replay->y == stats->bContinue->y, "Stats Replays lies between Main Menu and Trophies without overlap");
            check(replay->image == gfx.img("IMAGE_DLG_BROWNBUTTON1"), "Stats Replays preserves native footer artwork");
            click(app, *stats, *replay, replay->w - 1);
            settle(app);
            auto* tip = app.getDialog(D_Tip);
            if (check(tip && tip->header == "Replays", "Stats Replays hit target opens empty replay message")) {
              keypress(SDLK_ESCAPE);
              check(app.screen() == stats && !app.hasModal(), "Replays Escape returns to the same Stats screen");
            }
            ReplayData replayData;
            replayData.game = 1;
            replayData.level = "qa-menu-fixture";
            replayData.title = "Menu QA";
            replayData.player = profile.name;
            const auto replayDir = userDir / "replays";
            std::filesystem::create_directories(replayDir);
            const auto replayPath = replayDir / "menu-qa.pegn";
            check(replayData.save(replayPath), "scratch-only replay fixture saved");
            click(app, *stats, *replay);
            settle(app);
            auto* list = app.getDialog(D_Replay2);
            if (check(list && list->findWidget(104), "Stats Replays opens populated native list")) {
              click(app, *list, *list->findWidget(104));
              settle(app);
              check(app.screen() == stats && !app.hasModal(), "Replays Cancel returns to the same Stats screen");
              click(app, *stats, *replay);
              settle(app);
              keypress(SDLK_ESCAPE);
              check(app.screen() == stats && !app.hasModal(), "populated Replays Escape returns to Stats");
            }
            std::filesystem::remove(replayPath);
          }
          click(app, *stats, *stats->bContinue);
          settle(app);
          check(std::string(app.screen()->name()) == "progress", "Stats Trophies retains existing progress-screen callback");
          keypress(SDLK_ESCAPE);
          check(dynamic_cast<AdventureScreen*>(app.screen()) != nullptr, "Trophies Escape returns to Stats");
          keypress(SDLK_ESCAPE);
          check(dynamic_cast<MainMenu*>(app.screen()) != nullptr, "Stats Escape returns to main menu");
          app.showAdventureScreen();
          settle(app);
          stats = dynamic_cast<AdventureScreen*>(app.screen());
          click(app, *stats, *stats->bMenu);
          settle(app);
          check(dynamic_cast<MainMenu*>(app.screen()) != nullptr, "Stats Main Menu button retains callback");
        }
        menu = dynamic_cast<MainMenu*>(app.screen());
        online = nullptr;
        for (auto& widget : menu->widgets)
          if (auto* b = dynamic_cast<Button*>(widget.get()); b && b->label == "Online") online = b;
      } else {
        auto* replay = dynamic_cast<Button*>(menu->findWidget(4));
        check(replay && replay->label == "Replays" && replay->x == 0x1a1 && replay->y == 0x22a, "Deluxe retains native Replays footer");
        if (replay) {
          click(app, *menu, *replay);
          settle(app);
          check(app.getDialog(D_Tip) != nullptr, "Deluxe Replays callback remains available");
          keypress(SDLK_ESCAPE);
          check(app.screen() == menu && !app.hasModal(), "Deluxe Replays returns to main menu");
        }
      }
      Widget* options = menu->findWidget(5);
      if (!check(options != nullptr, "Options button exists")) return 1;
      click(app, *menu, *options);
      settle(app);
      auto* d = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
      if (!check(d != nullptr, "Options opens")) return 1;
      Widget* mods = d->findWidget(12);
      if (!check(mods && mods->visible && mods->enabled, "Classic Options includes Mods")) return 1;
      for (size_t i = 0; i <= d->widgets.size() && d->focusW != mods; ++i) keypress(SDLK_TAB);
      if (!check(d->focusW == mods, "Classic Mods is keyboard reachable")) return 1;
      keypress(SDLK_RETURN);
      if (!check(app.helpOverlay() && std::string(app.helpOverlay()->name()) == "mods", "Classic Mods opens with Enter")) return 1;
      keypress(SDLK_ESCAPE);
      check(!app.helpOverlay() && app.topDialog() == d && d->focusW == mods, "Back restores Options and Mods focus");
      if (game == Game::Nights) {
        check(d->findWidget(17) == nullptr, "Nights Options removes Online instead of hiding its hit target");
        Button* preceding = d->homeBtn->visible ? d->homeBtn : d->visuals;
        check(d->editor->y >= preceding->y + preceding->h && d->editor->y - preceding->y - preceding->h <= 12, "Nights Options closes the removed Online row spacing");
      }
      check(d->yesBtn->x >= 0 && d->yesBtn->y >= 0 && d->yesBtn->x + d->yesBtn->w <= d->w && d->yesBtn->y + d->yesBtn->h <= d->h, "Options footer stays inside dialog");
      for (bool pressed : {false, true}) {
        Button button = *d->yesBtn;
        button.label.clear();
        button.over = true;
        button.down = pressed;
        button.overAlpha = 1;
        gfx.begin();
        gfx.clear();
        button.draw(gfx);
        SDL_RenderPresent(renderer.get());
        int right = -1;
        for (int y = 0; y < surface->h; ++y) {
          auto* row = static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch;
          for (int x = 0; x < surface->w; ++x)
            if (row[x * 4] || row[x * 4 + 1] || row[x * 4 + 2]) right = std::max(right, x);
        }
        check(right >= button.w - 8 && right < button.w, pressed ? "Options footer pressed artwork fills button width" : "Options footer hover artwork fills button width");
      }
      click(app, *d, *d->visuals);
      settle(app);
      auto* v = dynamic_cast<VisualsDialog*>(app.getDialog(D_Visuals));
      if (!check(v != nullptr, "Enhanced Visuals opens")) return 1;
      check(v->x >= 16 && v->y >= 10 && v->x + v->w <= 784 && v->y + v->h <= 590, "visuals panel keeps screen margins");
      check(v->insL >= 44 && v->insR >= 44, "visuals controls have generous panel insets");
      check(v->yesBtn->w == 470 && v->yesBtn->h == v->buttonH && v->yesBtn->x * 2 + v->yesBtn->w == v->w, "visuals Done retains its original size and is centered");
      check(v->rows.front().box->y >= v->contentTop(gfx), "visuals first row clears header spacing");
      for (size_t i = 0; i < v->rows.size(); ++i) {
        const auto& row = v->rows[i];
        check(row.box->x + row.box->labelX + gfx.textWidth("PLAIN", row.hint) <= v->w - v->insR, "visuals descriptions retain right panel padding");
        if (i + 1 < v->rows.size()) check(v->rows[i + 1].box->y - row.box->y >= 64, "visuals setting groups have breathing room");
      }
      const auto* lastBox = v->rows.back().box;
      check(lastBox->y + lastBox->h + gfx.fontHeight("PLAIN") + 24 <= v->yesBtn->y, "visuals Done is separated from the last description");
      auto brightness = [&](bool enabled) {
        Slider slider = *v->bloomStr;
        slider.enabled = enabled;
        gfx.begin();
        gfx.clear();
        gfx.pushOrigin(200, 100);
        slider.draw(gfx);
        gfx.popOrigin();
        SDL_RenderPresent(renderer.get());
        uint64_t total = 0;
        for (int y = 0; y < surface->h; ++y) {
          auto* row = static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch;
          for (int x = 0; x < surface->w; ++x) total += row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2];
        }
        return total;
      };
      check(brightness(false) < brightness(true) * 0.75, "disabled bloom slider is visibly dimmed");
      check(v->bloomStr->w == d->sfx->w, "bloom uses shared slider artwork travel");
      bool* settings[] = {&app.settings.vis.interpolate, &app.settings.vis.smooth, &app.settings.vis.bloom, &app.settings.vis.sparks, &app.settings.vis.ultraWideHud};
      if (!check(v->rows.size() == 5, "five visual settings have checkboxes")) return 1;
      for (size_t i = 0; i < v->rows.size(); ++i) {
        Checkbox* box = v->rows[i].box;
        for (bool checked : {false, true}) {
          for (bool over : {false, true}) {
            Checkbox sample = *box;
            sample.checked = checked;
            sample.over = over;
            const int capWidth = static_cast<int>(sample.image->lw() / 3);
            const int capX = static_cast<int>(std::lround(sample.w)) - capWidth;
            auto capPixels = [&] {
              gfx.begin();
              gfx.clear();
              sample.draw(gfx);
              SDL_RenderPresent(renderer.get());
              std::vector<uint8_t> pixels;
              for (int y = 0; y < sample.h; ++y) {
                const auto* row = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
                pixels.insert(pixels.end(), row + capX * 4, row + (capX + capWidth) * 4);
              }
              return pixels;
            };
            auto labeled = capPixels();
            sample.label.clear();
            check(labeled == capPixels(), "visuals label leaves the native right cap untouched");
          }
        }
        check(box->image == gfx.img("IMAGE_DLG_BUTTON1") && (box->style == 0 || box->style == 2), "visuals toggle shows explicit checkmark");
        bool previous = *settings[i];
        click(app, *v, *box, box->labelX + gfx.textWidth(box->font, box->label) - 1);
        check(*settings[i] != previous && box->checked == *settings[i], "visuals label toggles corresponding setting");
        click(app, *v, *box, 20);
        check(*settings[i] == previous && box->checked == *settings[i], "visuals icon restores corresponding setting");
        if (i + 1 < v->rows.size()) check(box->y + box->h + 18 < v->rows[i + 1].box->y, "visuals hint does not overlap next toggle");
      }
      v->bloom->checked = true;
      v->checkboxChanged(2, true);
      for (Slider* slider : {d->music, d->sfx, d->fever, v->bloomStr}) {
        for (double value : {0.0, 0.25, 0.5, 0.75, 1.0}) {
          slider->value = value;
          float tw = gfx.img("IMAGE_DLG_SLIDERORB")->lw();
          float center = static_cast<float>(value * (slider->w - tw)) + tw / 2;
          slider->mouseDown(center, slider->h / 2, 1, 1);
          slider->mouseUp(center, slider->h / 2, 1);
          check(std::abs(slider->value - value) < 0.000001, "clicking rendered thumb center preserves slider value");
          for (bool enabled : {false, true}) {
            Slider sample = *slider;
            sample.label.clear();
            sample.enabled = enabled;
            sample.value = value;
            auto renderTrack = [&](bool reference) {
              gfx.begin();
              gfx.clear();
              gfx.pushOrigin(200, 100);
              if (reference) {
                Mode mode(sample.alpha * (enabled ? 1.0f : 0.4f));
                Tex* fill = gfx.img("IMAGE_DLG_SLIDERHILIGHT");
                Tex* orb = gfx.img("IMAGE_DLG_SLIDERORB");
                gfx.draw(gfx.img("IMAGE_DLG_SLIDER"), -170, -2, mode);
                gfx.pushClip(0, 9, std::ceil(center), fill->lh());
                gfx.draw(fill, 0, 9, mode);
                gfx.popClip();
                gfx.draw(orb, center - tw / 2, (sample.h - orb->lh()) / 2, mode);
              } else {
                sample.draw(gfx);
              }
              gfx.popOrigin();
              SDL_RenderPresent(renderer.get());
              std::vector<uint8_t> pixels;
              for (int y = 90; y < 150; ++y) {
                const auto* row = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
                pixels.insert(pixels.end(), row + 200 * 4, row + (200 + static_cast<int>(sample.w)) * 4);
              }
              return pixels;
            };
            check(renderTrack(false) == renderTrack(true), "slider fill reaches the thumb center with native pixels and no gap");
          }
        }
      }
      v->bloomStr->value = 0.5;
      v->sliderChanged(6, 0.5);
      v->bloom->checked = false;
      v->checkboxChanged(2, false);
      float previous = app.settings.vis.bloomStrength;
      check(!v->bloomStr->mouseDown(v->bloomStr->w - 20, 10, 1, 1) && !v->bloomStr->dragging, "disabled bloom rejects mouse capture");
      click(app, *v, *v->bloomStr, v->bloomStr->w - 20);
      check(app.settings.vis.bloomStrength == previous, "disabled bloom ignores click");
      check(!v->bloomStr->dragging, "disabled bloom never starts dragging");
      v->bloomStr->dragging = true;
      v->bloomStr->mouseMove(0, 0);
      v->bloomStr->mouseUp(0, 0, 1);
      check(app.settings.vis.bloomStrength == previous, "disabled bloom ignores captured drag");
      v->focusW = v->bloomStr;
      Input key;
      key.type = Input::KeyDown;
      key.key = SDLK_RIGHT;
      v->input(key);
      check(app.settings.vis.bloomStrength == previous, "disabled bloom ignores focused keyboard");
      v->focusW = nullptr;
      click(app, *v, *v->yesBtn);
      settle(app);
      check(app.getDialog(D_Visuals) == nullptr, "visuals Done closes dialog");
      click(app, *d, *d->yesBtn);
      settle(app);
      check(app.getDialog(D_Options) == nullptr, "Options Done closes dialog");
      click(app, *menu, *online);
      settle(app);
      check(std::string(app.screen()->name()) == "online", "main menu Online opens existing online screen");
      check(!app.hasModal(), "main menu Online has no blocking dialog");
      if (game == Game::Nights) {
        app.showMainMenu();
        settle(app);
        menu = dynamic_cast<MainMenu*>(app.screen());
        click(app, *menu, *menu->findWidget(5));
        settle(app);
        d = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
        if (!check(d != nullptr, "Options reopens before Done navigation")) return 1;
        bool fullscreen = !app.settings.fullscreen;
        bool colorblind = !profile.colorblind;
        click(app, *d, *d->fullscreen);
        click(app, *d, *d->colorblind);
        check(d->fullscreen->checked == fullscreen && d->colorblind->checked == colorblind, "Options checkbox hit targets toggle pending values");
        for (size_t i = 0; i <= d->widgets.size(); ++i) {
          Input tab;
          tab.type = Input::KeyDown;
          tab.key = SDLK_TAB;
          app.input(tab);
          check(d->focusW && d->focusW->visible && d->focusW->id != 17, "Options keyboard traversal has no removed Online stop");
        }
        d->focusW = d->yesBtn;
        keypress(SDLK_RETURN);
        check(std::string(app.screen()->name()) == "mainmenu", "Nights Options Done returns to main menu");
        check(app.getDialog(D_Options) == nullptr && !app.hasModal(), "Nights Options Done leaves no blocking dialog");
        check(app.settings.fullscreen == fullscreen && profile.colorblind == colorblind, "Nights Options Done applies pending settings");
      }
      app.showMainMenu();
      settle(app);
      menu = dynamic_cast<MainMenu*>(app.screen());
      click(app, *menu, *menu->findWidget(5));
      settle(app);
      d = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
      click(app, *d, *d->help);
      settle(app);
      auto* help = dynamic_cast<HelpScreen*>(app.helpOverlay());
      if (check(help != nullptr, "Options Help remains reachable after row removal")) {
        help->page = 6;
        keypress(SDLK_ESCAPE);
        check(app.helpOverlay() == nullptr && !app.hasModal(), "Help Escape returns without a blocking dialog");
      }
      for (GameMode mode : {GameMode::Adventure, GameMode::Demo}) {
        app.gameMode = mode;
        app.curStage = app.curLevel = 0;
        if (!check(app.curLevelInfo() != nullptr, "in-game visuals fixture has a stock level")) return 1;
        app.showBoard(mode == GameMode::Adventure, true);
        auto* board = dynamic_cast<GameScreen*>(app.screen());
        if (!check(board && board->ok(), "in-game visuals fixture loads a real board")) return 1;
        settle(app);
        if (auto* tip = app.getDialog(D_Tip)) {
          click(app, *tip, *tip->yesBtn);
          settle(app);
        }
        app.doOptionsDialog();
        settle(app);
        d = dynamic_cast<OptionsDialog*>(app.getDialog(D_Options));
        if (!check(d && d->fromGame, "in-game Options opens")) return 1;
        check(!d->findWidget(12), "Mods remains a front-end setting, not a Pause action");
        click(app, *d, *d->visuals);
        settle(app);
        v = dynamic_cast<VisualsDialog*>(app.getDialog(D_Visuals));
        if (!check(v != nullptr, "in-game Enhanced Visuals opens")) return 1;
        check(v->x >= 16 && v->y >= 10 && v->x + v->w <= 784 && v->y + v->h <= 590, "in-game visuals panel keeps screen margins");
        check(v->y + v->yesBtn->y + v->yesBtn->h <= 590, "in-game visuals Done is fully on screen");
        check(v->yesBtn->w == 470 && v->yesBtn->h == v->buttonH && v->yesBtn->x * 2 + v->yesBtn->w == v->w, "in-game visuals Done retains its original size and is centered");
        click(app, *v, *v->yesBtn);
        settle(app);
        check(app.getDialog(D_Visuals) == nullptr && app.getDialog(D_Options) == d, "in-game visuals Done returns to Options");
        click(app, *d, *d->yesBtn);
        settle(app);
        check(app.getDialog(D_Options) == nullptr, "in-game Options returns to the board");
      }
    }
  }
  std::cout << "UI_CONTROLS checks=" << checks << " failures=" << failures << '\n';
  SDL_Quit();
  return failures ? 1 : 0;
}
