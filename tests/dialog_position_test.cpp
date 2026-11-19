#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "render.h"
#include "ui/dialogs.h"
#include "ui/import_dialog.h"
#include "ui/replay.h"
#include "ui/screens.h"
#include "ui/start.h"

namespace fs = std::filesystem;
using namespace pp;
using namespace pp::ui;
using Wide = pp::ui::WidescreenMode;

static void require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}

static Button& labeled(Panel& panel, const std::string& label) {
  for (const auto& w : panel.widgets)
    if (auto* b = dynamic_cast<Button*>(w.get()); b && b->visible && b->label == label) return *b;
  throw std::runtime_error("missing button: " + label);
}

static bool run(const fs::path& game, const fs::path& art, const fs::path& root, Game edition, int width, int height, Wide mode) {
  const bool classic = mode == Wide::Classic4x3;
  const std::string tag = std::string(edition == Game::Nights ? "nights-" : "deluxe-") + std::to_string(width) + 'x' + std::to_string(height) + (classic ? "-classic" : "");
  bool ok = true;
  auto check = [&](bool result, const std::string& what) {
    if (!result) std::cerr << tag << ": " << what << '\n';
    ok &= result;
  };
  Vfs vfs;
  require(vfs.mountGame(game, nullptr) && vfs.mountPresentation(art), "missing game/menu assets");
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> s(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  require(s != nullptr, SDL_GetError());
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> r(SDL_CreateSoftwareRenderer(s.get()), SDL_DestroyRenderer);
  require(r != nullptr, SDL_GetError());
  Gfx gfx(r.get(), vfs);
  Renderer board(r.get(), vfs);
  PlayerInfo player;
  player.name = "Preview Player";
  App::Config cfg;
  cfg.game = cfg.home = edition;
  cfg.widescreen = static_cast<int>(mode);
  cfg.userDir = root / tag;
  cfg.noSave = cfg.noAudio = cfg.fastBoot = cfg.noRoomDiscovery = true;
  cfg.seed = 1;
  App app(gfx, vfs, nullptr, &board, cfg);
  app.setGames(true, edition);
  app.curProfile = &player;
  app.showMainMenu();
  auto settle = [&] { for (int i = 0; i < 40; ++i) app.tick(); };
  auto frame = [&] { app.frame(); require(SDL_RenderPresent(r.get()), SDL_GetError()); };
  auto pointer = [&](Input::Type type, float px, float py) {
    Input in;
    in.type = type;
    gfx.toCanvas(std::round(px), std::round(py), in.x, in.y);
    app.input(in);
  };
  auto press = [&](Panel& p, Widget& w, float localY = -1) {
    const float origin = dynamic_cast<Dialog*>(&p) ? (gfx.canvasW() - 800) / 2.f : 0;
    const float px = gfx.offX() + gfx.scale() * (origin + p.x + w.x + w.w / 2);
    const float py = gfx.offY() + gfx.scale() * (p.y + w.y + (localY < 0 ? w.h / 2 : localY));
    pointer(Input::MouseMove, px, py);
    pointer(Input::MouseDown, px, py);
    require(p.capture == &w, "mouse down missed displayed control " + std::to_string(w.id));
    pointer(Input::MouseUp, px, py);
  };
  auto key = [&](int k) { Input in; in.type = Input::KeyDown; in.key = k; app.input(in); };
  auto settled = [&](int id, const std::string& page) -> Dialog& {
    auto* d = app.getDialog(id);
    require(d && d->modal && d->scrollActive && !d->scrollingOff, page + ": missing entering modal");
    check(d->y == 600, page + ": layout skipped entry");
    const float start = d->y;
    app.tick();
    check(d->scrollActive && d->y < start && d->y > d->scrollTarget, page + ": no intermediate animation position");
    settle(); frame();
    require(!d->scrollActive && d->y == d->scrollTarget, page + ": did not settle at destination");
    if (const char* out = SDL_getenv("PP_DIALOG_CAPTURE"); out && width == 1920) {
      const auto file = fs::path(out) / (tag + '-' + page + ".bmp");
      require(SDL_SaveBMP(s.get(), file.string().c_str()), SDL_GetError());
    }
    return *d;
  };
  auto measure = [&](Dialog& d, const std::string& page, float classicX, float classicY) {
    const float wantX = classic ? classicX : (800 - d.w) / 2;
    const float wantY = classic ? classicY : (600 - d.h) / 2;
    check(std::abs(d.x - wantX) < .51f && std::abs(d.y - wantY) < .51f, page + ": wrong settled logical bounds");
    const float x = d.x;
    d.x += 4000;
    frame();
    const Uint8* pixels = static_cast<Uint8*>(s->pixels);
    std::vector<Uint8> background(pixels, pixels + s->pitch * height);
    d.x = x;
    frame();
    int left = width, top = height, right = -1, bottom = -1;
    for (int y = 0; y < height; ++y) {
      int l = width, h = -1, count = 0;
      for (int x = 0; x < width; ++x) {
        const int i = y * s->pitch + x * 4;
        if (std::abs(pixels[i] - background[i]) + std::abs(pixels[i+1] - background[i+1]) + std::abs(pixels[i+2] - background[i+2]) < 30) continue;
        l = std::min(l, x); h = x; ++count;
      }
      if (count < d.w * gfx.scale() / 3) continue;
      left = std::min(left, l); right = std::max(right, h);
      top = std::min(top, y); bottom = y;
    }
    const float scale = gfx.scale();
    const float cx = width / 2.f + (wantX + d.w / 2 - 400) * scale;
    const float cy = height / 2.f + (wantY + d.h / 2 - 300) * scale;
    const auto near = [&](float a, float b) { return std::abs(a-b) <= 5 * scale + 1; };
    check(near((left+right+1)/2.f, cx) && near((top+bottom+1)/2.f, cy), page + ": panel pixels not centered on intended position");
    check(right-left >= .90f * d.w * scale && bottom-top >= .90f * d.h * scale, page + ": incomplete rendered panel");
    std::cout << tag << ' ' << page << " rect=" << x << ',' << d.y << ',' << d.w << ',' << d.h
              << " pixels=" << left << ',' << top << ',' << right << ',' << bottom << '\n';
  };
  auto close = [&](Dialog& d, Widget& w, Dialog* parent = nullptr) {
    const int id = d.dialogId;
    press(d, w);
    require(d.scrollingOff && d.scrollTarget == -d.h, "wrong dismissal target");
    settle(); frame();
    require(!app.getDialog(id) && (parent ? app.getDialog(parent->dialogId) == parent && !parent->scrollingOff : !app.hasModal()), "wrong modal after dismissal");
  };
  settle(); frame();
  auto* home = app.screen();
  press(*home, labeled(*home, "Quit"));
  auto& quit = settled(D_Quit, "home-quit");
  measure(quit, "home-quit", 235, 280);
  close(quit, *quit.noBtn);
  require(!app.wantsQuit() && app.screen() == home, "Quit/No did not return to Home");

  if (!classic) press(*home, labeled(*home, "Profile")); else app.doUserDialog();
  auto& users = settled(D_UserList, "profiles");
  measure(users, "profiles", 150, 60);
  press(users, labeled(users, "Rename"));
  auto& rename = settled(D_RenameUser, "rename");
  measure(rename, "rename", users.x + std::trunc((users.w - rename.w)/2), users.y);
  close(rename, *rename.noBtn, &users);
  press(users, labeled(users, "Delete"));
  auto& del = settled(D_DeleteUser, "profile-delete");
  measure(del, "profile-delete", users.x + std::trunc((users.w - del.w)/2), users.y);
  close(del, *del.noBtn, &users);
  press(users, labeled(users, "Create New User"));
  auto& cert = settled(D_CreateUser, "create");
  measure(cert, "create", 30, 30);
  key(SDLK_RETURN);
  auto& error = settled(D_NameError, "name-error");
  measure(error, "name-error", cert.x + std::trunc((cert.w - error.w)/2), cert.y + 140);
  close(error, *error.yesBtn, &cert);
  close(cert, *cert.noBtn, &users);
  press(users, labeled(users, "Import Player"));
  auto* imported = dynamic_cast<ImportDialog*>(app.getDialog(D_ImportProfile));
  require(imported != nullptr, "missing import dialog");
  fs::create_directories(root / "empty");
  imported->load(root / "empty");
  auto& import = settled(D_ImportProfile, "import");
  measure(import, "import", 150, 40);
  close(import, *import.noBtn, &users);
  close(users, *users.noBtn);
  require(app.curProfile == &player, "profile changed on Cancel");

  const auto folders = [&](Dialog* parent) {
    StartDialogs dialogs;
    dialogs.message = [](auto&&...) -> int { throw std::runtime_error("unexpected system chooser"); };
    GameFolders picker(root / "launcher.ini", FindSources{}, dialogs);
    picker.setGame(&app, [&](const auto& done) {
      auto& box = settled(62, parent ? "folders-nested" : "folders");
      const float cx = parent ? parent->x + std::trunc((parent->w-box.w)/2) : 410-box.w/2;
      measure(box, "folders", cx, std::clamp(parent ? parent->y : 280.f, 10.f, 590-box.h));
      close(box, labeled(box, "Close"), parent);
      require(done(), "folder Close did not finish the chooser");
    });
    require(!picker.chooseFolders(edition), "folder choice changed on Close");
  };
  folders(nullptr);
  app.doOptionsDialog();
  auto& options = settled(D_Options, "options");
  if (!classic) check(options.y + options.h <= Dialog::kOptionsFooterTop, "Options overlaps footer");
  folders(&options);
  close(options, *options.yesBtn);

  const auto replayPath = app.userDir / "replays" / ("Example" + replayExt(edition == Game::Nights));
  fs::create_directories(replayPath.parent_path());
  ReplayData data;
  data.game = edition == Game::Nights;
  data.level = app.stages.stage(0)->levels.front().file;
  data.player = player.name;
  data.log = {{10, 0, 260, 330}, {15, 1}, {16, 3}};
  data.shots = 1;
  require(data.save(replayPath), "cannot seed replay");
  auto contents = [&] {
    std::ifstream file(replayPath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
  };
  const auto bytes = contents();
  for (bool saving : {false, true}) {
    if (saving) app.doSaveReplayDialog();
    else if (!classic) press(*home, labeled(*home, "Replays")); else app.doReplaysDialog();
    auto& replay = settled(saving ? D_SaveReplay : D_Replay2, saving ? "save-replay" : "replays");
    measure(replay, saving ? "save-replay" : "replays", saving ? 200 : 550-replay.w, saving ? 60.f : 50.f);
    ListBox* list = nullptr;
    for (auto& w : replay.widgets) if (auto* l = dynamic_cast<ListBox*>(w.get())) list = l;
    require(list && list->lines.size() == 1, "missing replay fixture");
    press(replay, *list, list->topInset + list->itemH / 2);
    require(list->select == 0, "replay row not selected");
    press(replay, labeled(replay, saving ? "Save" : "Delete"));
    auto& confirm = settled(saving ? D_OverwriteReplay : D_DeleteReplay, saving ? "overwrite" : "replay-delete");
    measure(confirm, saving ? "overwrite" : "replay-delete", replay.x + std::trunc((replay.w-confirm.w)/2), std::max(replay.y, replay.y+255-confirm.h));
    close(confirm, *confirm.noBtn, &replay);
    close(replay, labeled(replay, "Cancel"));
    require(contents() == bytes, "Cancel altered replay");
  }
  if (!classic) {
    press(*home, labeled(*home, "Single Player")); settle(); frame();
    press(*home, labeled(*home, "Adventure"));
  } else app.doAdventureQuestion();
  auto& adventure = settled(D_WhichAdventure, "adventure-choice");
  measure(adventure, "adventure-choice", 227, 280);
  key(SDLK_ESCAPE); settle(); frame();
  require(!app.hasModal() && app.screen() == home && !app.gameSwitchPending(), "Adventure cancel changed edition");
  if (!classic) {
    app.doConfirmNewChallengeDialog();
    auto& changed = settled(D_NewChallenge, "resized");
    measure(changed, "resized", 0, 0);
    close(changed, *changed.noBtn);
  }
  if (width == 1920) {
    app.doNotYet("Complete the first stage of Adventure.");
    auto& locked = settled(D_Locked, "locked");
    check(locked.w == 490 && locked.y == 150, "PS3 locked panel lost its suspension point");
    close(locked, *locked.yesBtn);
    player.storyPending = false;
    app.startAdventureGame();
    for (int i = 0; i < 100 && !app.getDialog(D_Tip); ++i) app.tick();
    auto& tip = settled(D_Tip, "tutorial");
    check(tip.y == 100, "authored tutorial anchor moved");
    close(tip, *tip.yesBtn);
    require(app.screen()->isGame() && !app.boardPaused(), "tutorial dismissal lost the board");
    app.doOptionsDialog();
    auto& pause = static_cast<OptionsDialog&>(settled(D_Options, "pause"));
    check(pause.y + pause.h <= Dialog::kOptionsFooterTop, "Pause overlaps footer");
    press(pause, *pause.newLevel);
    auto& restart = settled(D_RestartLevel, "pause-restart");
    check(restart.y + restart.h / 2 == (Dialog::kOptionsFooterTop + 40) / 2, "PS3 restart panel anchor moved");
    close(restart, *restart.noBtn, &pause);
    close(pause, *pause.yesBtn);
    require(!app.boardPaused(), "cancel/resume failed");
  }
  return ok;
}

int main(int argc, char** argv) {
  if (argc != 5 || !SDL_Init(0)) return 2;
  const auto root = fs::path(argv[4]) / ("dialog-position-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  bool ok = true;
  try {
    for (Game game : {Game::Deluxe, Game::Nights}) {
      auto runAt = [&](int w, int h, Wide m) { return run(argv[game == Game::Deluxe ? 1 : 2], argv[3], root, game, w, h, m); };
      ok &= runAt(1920, 1080, Wide::Auto);
      ok &= runAt(1365, 997, Wide::W16x9);
      ok &= runAt(1280, 800, Wide::Auto);
      ok &= runAt(1280, 720, Wide::Classic4x3);
    }
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; ok = false; }
  SDL_Quit();
  fs::remove_all(root);
  return !ok;
}
