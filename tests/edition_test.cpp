#include "content.h"
#include "gamefind.h"
#include "gamepaths.h"
#include "image.h"
#include "mods/manager.h"
#include "render.h"
#include "ui/game_screen.h"
#include "ui/screens.h"
#include "ui/start.h"
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 4 && argc != 7 && argc != 8) {
    std::cerr << "Usage: pp_edition_test EXTREME WOW STEAM [DELUXE NIGHTS PEGO_MOD [WOW_MOD]]\n";
    return 2;
  }
  const auto check = [](bool pass, std::string_view why) {
    if (!pass) std::cerr << why << '\n';
    return pass;
  };
  SDL_Surface* surface = SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_XRGB8888);
  SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
  if (!renderer) { check(false, SDL_GetError()); SDL_DestroySurface(surface); return 1; }
  bool ok = true;
  const auto verify = [&](const char* path, pp::Game edition, size_t expected) {
    const std::string_view title = pp::gameTitle(edition);
    auto dir = pp::resolveGameDir(pp::pathFromUtf8(path));
    ok &= check(!dir.empty() && pp::classifyGameDir(dir) == edition, title);
    pp::Vfs vfs;
    std::string error;
    if (!vfs.mountGame(dir, &error)) { ok &= check(false, error); return; }
    const auto levels = pp::listStageLevels(vfs);
    ok &= check(levels.size() == expected || (edition == pp::Game::Nights && levels.size() >= expected), "Unexpected level count");
    for (const auto& level : levels) {
      pp::LevelBundle bundle;
      pp::Board board;
      const bool loaded = pp::loadLevelBundle(vfs, level.name, bundle, &error) && board.load(bundle.base, bundle.level, bundle.cfg, &error);
      ok &= check(loaded && board.fire(board.gunAngle), level.name + ": " + error);
    }
    pp::ui::Gfx gfx(renderer, vfs);
    pp::Renderer boardRenderer(renderer, vfs);
    pp::ui::App::Config cfg;
    cfg.game = edition;
    cfg.noSave = cfg.noAudio = true;
    cfg.userDir = pp::pathFromUtf8("unused-edition-test-profile");
    pp::PlayerInfo profile;
    profile.name = "Edition Test";
    pp::ui::App app(gfx, vfs, nullptr, &boardRenderer, cfg);
    app.curProfile = &profile;
    app.showMainMenu();
    for (int i = 0; i < 60; ++i) app.tick();
    app.frame();
    SDL_RenderPresent(renderer);
    ok &= check(app.titleText() == title && app.screenName() == "mainmenu" && !app.bothGames, "Edition did not reach its own menu");
    ok &= check(gfx.img("IMAGE_MM_BACKGROUND") && gfx.img("IMAGE_MM_ADVENTURE"), "Missing menu art");
    pp::ui::MainMenu menu(app);
    menu.onButton(1);
    for (int i = 0; i < 60; ++i) app.tick();
    ok &= check(app.screenName() == "levelscreen", "Quick Play did not open");
    app.curStage = app.curLevel = 0;
    pp::ui::GameScreen board(app, false, true);
    ok &= check(!levels.empty() && board.ok() && board.bundle().name == levels.front().name, "Quick Play did not construct the first board");
  };
  verify(argv[1], pp::Game::Extreme, 10);
  pp::FindSources sources;
  sources.steamRoots.push_back({pp::pathFromUtf8(argv[3]), "fixture Steam"});
  const auto wow = pp::pathFromUtf8(argv[2]);
  sources.remembered[pp::gameIndex(pp::Game::Deluxe)] = wow;
  sources.gamesDirs.push_back({wow.parent_path(), "fixture games"});
  const auto games = pp::findGames(sources);
  ok &= check(pp::kGameCount == 3 && games.has(pp::Game::Extreme), "Expected three built-in editions, including Steam Extreme");
  for (const auto& dir : games.dir) ok &= check(dir != wow, "Discovery accepted the WoW donor as a built-in edition");
  bool checkedDonor = false;
  for (const auto& look : games.looks)
    if (look.kind == pp::FindLook::Kind::Remembered && look.path == wow)
      checkedDonor |= !look.found && look.seen.find("level pack source") != std::string::npos;
  ok &= check(checkedDonor, "Discovery did not reject a remembered WoW folder");
  int action = 0;
  int donorAttempts = 0;
  bool rejected = false;
  pp::ui::StartDialogs dialogs;
  dialogs.folder = [&](SDL_Window*, const std::string&, const std::filesystem::path&) {
    ++donorAttempts;
    return pp::ui::FolderAnswer{pp::ui::FolderAnswer::Status::Chosen, wow};
  };
  dialogs.message = [&](SDL_Window*, const std::string&, const std::string& text, const std::vector<std::string>& buttons, bool) {
    for (const auto& label : buttons) ok &= check(label.find("WoW") == std::string::npos, "Game Folders offered WoW");
    if (buttons.size() == 2 && buttons.front() == "Choose Folder...") {
      if (donorAttempts == 0) return 0;
      rejected = text.find("level pack") != std::string::npos;
      return 1;
    }
    constexpr const char* select[] = {"Extreme", "Open"};
    if (action >= 2) return -1;
    for (int i = 0; i < (int)buttons.size(); ++i)
      if (buttons[i] == select[action]) { ++action; return i; }
    return -1;
  };
  pp::ui::GameFolders folders({}, sources, dialogs);
  folders.detect();
  ok &= check(folders.chooseFolders(pp::Game::Deluxe) == pp::Game::Extreme && action == 2, "Could not switch from Deluxe to Extreme through Game Folders");
  ok &= check(!folders.askForAnyGame() && rejected && donorAttempts == 1, "The startup chooser did not reject the WoW donor as a level pack");
  rejected = false;
  ok &= check(!folders.askForGame(pp::Game::Deluxe) && rejected && donorAttempts == 2, "The Deluxe chooser did not reject the WoW donor as a level pack");
  if (argc >= 7) {
    verify(argv[4], pp::Game::Deluxe, 55);
    verify(argv[5], pp::Game::Nights, 60);
    namespace fs = std::filesystem;
    const auto copied = fs::temp_directory_path() / ("pp-edition-test-" + std::to_string(SDL_GetTicksNS()));
    if (fs::create_directory(copied)) {
      const auto deluxe = copied / "Deluxe with add-ons";
      fs::create_directories(deluxe / "levelpacks");
      fs::copy_file(pp::resolveGameDir(pp::pathFromUtf8(argv[4])) / "main.pak", deluxe / "main.pak");
      std::ofstream(deluxe / "levelpacks" / "fixture.pak").close();
      pp::FindSources remembered;
      remembered.remembered[pp::gameIndex(pp::Game::Deluxe)] = deluxe;
      ok &= check(pp::findGames(remembered).has(pp::Game::Deluxe), "Discovery rejected Deluxe because it has an add-on");
      fs::remove_all(copied);
    } else ok &= check(false, "Could not create the Deluxe discovery fixture");
    pp::Vfs vfs;
    std::string error;
    if (!vfs.mountGame(pp::pathFromUtf8(argv[4]), &error)) { check(false, error); return 1; }
    pp::LevelBundle stock;
    if (!pp::loadLevelBundle(vfs, "Level1", stock, &error)) { check(false, error); return 1; }
    pp::mods::ModManager mods;
    mods.discover({argv[6]});
    mods.activate(&vfs);
    ok &= check(mods.activeCount() == 1, "Prototype pack did not activate as a mod");
    pp::LevelBundle after;
    ok &= check(pp::loadLevelBundle(vfs, "Level1", after, &error) && pp::writeLevel(*after.base) == pp::writeLevel(*stock.base) && pp::writeLevel(*after.level) == pp::writeLevel(*stock.level), "Prototype pack replaced the retail Level1");
    pp::ui::StageMgr stages;
    ok &= check(stages.load(vfs) && stages.numStages() == 11 && stages.numAllStages() == 22 && stages.numAllLevels() == 110, "Prototype stages did not appear separately from Deluxe");
    const auto base = vfs.read("levels/_pego_base.dat");
    int n = 0;
    for (const auto& l : pp::listStageLevels(vfs)) {
      if (!l.name.starts_with("pego_")) continue;
      ++n;
      pp::LevelBundle bundle;
      pp::Board board;
      if (!pp::loadLevelBundle(vfs, l.name, bundle, &error)) { ok &= check(false, error); continue; }
      ok &= check(bundle.base && base && pp::writeLevel(*bundle.base) == base->bytes, "Wrong prototype base geometry: " + l.name);
      ok &= check(board.load(bundle.base, bundle.level, bundle.cfg, &error) && board.fire(board.gunAngle), l.name + ": " + error);
    }
    ok &= check(n == 55, "Expected 55 prototype levels in Quick Play");
    pp::ui::Gfx gfx(renderer, vfs);
    pp::Renderer boardRenderer(renderer, vfs);
    pp::ui::App::Config cfg;
    cfg.noSave = cfg.noAudio = true;
    cfg.userDir = pp::pathFromUtf8("unused-edition-test-profile");
    pp::PlayerInfo profile;
    profile.name = "Edition Test";
    pp::ui::App app(gfx, vfs, nullptr, &boardRenderer, cfg);
    app.curProfile = &profile;
    app.gameMode = pp::GameMode::QuickPlay;
    pp::ui::LevelScreen levels(app);
    levels.keyDown(SDLK_LEFT, 0);
    ok &= check(app.packStage == stages.numStages(), "Could not open Pego from the first Quick Play page");
    for (int i = stages.numStages(); i < stages.numAllStages(); ++i) levels.keyDown(SDLK_RIGHT, 0);
    ok &= check(app.packStage == stages.numAllStages() - 1, "Could not reach the final Pego page");
    levels.onButton(4);
    for (int i = 0; i < 60; ++i) app.tick();
    ok &= check(app.curLevelInfo() && app.curLevelInfo()->file.starts_with("pego_") && app.screenName() == "board", "Quick Play did not start the final Pego board");
    mods.deactivate();
  }
  if (argc == 8) {
    pp::Vfs original;
    std::string error;
    if (!original.mountGame(pp::pathFromUtf8(argv[2]), &error)) { check(false, error); return 1; }
    auto campaign = pp::listStageLevels(original);
    ok &= check(campaign.size() == 10, "Expected ten WoW campaign boards in source");
    campaign.push_back({"hogger1hit", "1-Shot Kill", "main", 2});
    for (int host : {4, 5}) {
      pp::Vfs vfs;
      if (!vfs.mountGame(pp::pathFromUtf8(argv[host]), &error)) { check(false, error); return 1; }
      const auto stock = pp::listStageLevels(vfs).size();
      const auto base = vfs.read("levels/_base.dat");
      pp::mods::ModManager mods;
      mods.discover({argv[7]});
      mods.activate(&vfs);
      const auto mountedBase = vfs.read("levels/_base.dat");
      ok &= check(mods.activeCount() == 1 && base && mountedBase && base->bytes == mountedBase->bytes, "WoW pack did not activate without altering the host base");
      const auto levels = pp::listStageLevels(vfs);
      ok &= check(levels.size() == stock + 11, "Expected ten WoW campaign levels and the bonus layout");
      if (levels.size() != stock + campaign.size()) continue;
      for (size_t i = 0; i < campaign.size(); ++i) {
        const auto& from = campaign[i];
        const auto& to = levels[stock + i];
        const std::string id = "wow_" + from.name;
        ok &= check(to.name == id && to.title == from.title && to.stage == from.stage, "Wrong WoW stage order or title: " + id);
        pp::LevelBundle before, after;
        if (!pp::loadLevelBundle(original, from.name, before, &error) || !pp::loadLevelBundle(vfs, id, after, &error)) { ok &= check(false, error); continue; }
        for (const auto& [a, b] : {std::pair{before.base, after.base}, std::pair{before.level, after.level}}) {
          ok &= check(a->byId.size() == b->byId.size(), "Wrong node count: " + id);
          if (a->byId.size() != b->byId.size()) continue;
          for (size_t n = 0; n < a->byId.size(); ++n) {
            for (const char* field : {"image", "emitter_image", "emitter_image_2"}) {
              const auto ref = a->byId[n]->str(field);
              if (ref.empty()) continue;
              const auto alias = b->byId[n]->str(field);
              const auto sprite = [&](const pp::Vfs& src, const std::string& name) -> std::optional<pp::Image> {
                for (const char* prefix : {"", "images/levels/", "images/game/", "levels/"})
                  if (auto art = pp::loadGameImage(src, prefix + name)) return art;
                return {};
              };
              const auto sa = sprite(original, ref), sb = sprite(vfs, alias);
              ok &= check(sa && sb && sa->w == sb->w && sa->h == sb->h && sa->rgba == sb->rgba, "Wrong WoW object art: " + ref);
              if (ref == "bumperc") ok &= check(alias == ref, "The Pyramid effect requires the stock bumper ID");
            }
          }
        }
        for (const auto& prefix : {std::string("levels/"), std::string("levels/cached_thumbnails/")}) {
          const auto a = pp::loadGameImage(original, prefix + from.name);
          const auto b = pp::loadGameImage(vfs, prefix + id);
          ok &= check(a && b && a->w == b->w && a->h == b->h && a->rgba == b->rgba, "Wrong WoW art: " + prefix + id);
        }
        pp::Board a, b;
        if (!a.load(before.base, before.level, before.cfg, &error) || !b.load(after.base, after.level, after.cfg, &error)) { ok &= check(false, error); continue; }
        ok &= check(a.hash() == b.hash() && a.fire(a.gunAngle) && b.fire(b.gunAngle), "Wrong WoW board geometry: " + id);
        for (int t = 0; t < 400; ++t) { a.stepCore(); b.stepCore(); }
        ok &= check(a.hash() == b.hash(), "Wrong WoW shot trace: " + id);
      }
      pp::ui::Gfx gfx(renderer, vfs);
      pp::Renderer boardRenderer(renderer, vfs);
      pp::ui::App::Config cfg;
      cfg.game = host == 4 ? pp::Game::Deluxe : pp::Game::Nights;
      cfg.noSave = cfg.noAudio = true;
      cfg.userDir = pp::pathFromUtf8("unused-edition-test-profile");
      pp::PlayerInfo profile;
      profile.name = "Edition Test";
      pp::ui::App app(gfx, vfs, nullptr, &boardRenderer, cfg);
      app.curProfile = &profile;
      app.gameMode = pp::GameMode::QuickPlay;
      pp::ui::LevelScreen screen(app);
      screen.keyDown(SDLK_LEFT, 0);
      const auto* first = app.stages.stage(app.packStage);
      ok &= check(first && !first->levels.empty() && first->levels.front().file == "wow_hogger", "Could not open the first WoW Quick Play page");
      screen.keyDown(SDLK_RIGHT, 0);
      screen.keyDown(SDLK_RIGHT, 0);
      screen.onButton(0);
      for (int t = 0; t < 60; ++t) app.tick();
      ok &= check(app.curLevelInfo() && app.curLevelInfo()->file == "wow_hogger1hit" && app.screenName() == "board", "Could not start the bonus WoW Quick Play board");
    }
  }
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
  return ok ? 0 : 1;
}
