#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "image.h"
#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/visuals_dialog.h"
#include "ui/session.h"

#include "ui/run.cpp"

pp::ui::PadGlyphs pp::ui::readPad(SDL_Gamepad*) { return {Pad::PlayStation, {}}; }

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

static bool replayPadSave(App& app, GameScreen& board) {
  Live live;
  live.app = &app;
  live.gfx = &app.gfx;
  live.ren = app.gfx.renderer();
  auto edge = [&](SDL_GamepadButton button, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.button = button;
    handleEvent(live, e);
  };
  auto press = [&](SDL_GamepadButton button) { edge(button, true); edge(button, false); };
  auto settle = [&]() { for (int i = 0; i < 35; ++i) app.tick(); };
  const int game = app.game == Game::Nights;
  const auto dir = app.userDir / "replays";
  const auto path = dir / ("Replay1" + replayExt(game));
  if (fs::exists(dir)) return false;
  press(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  settle();
  if (!app.getDialog(D_SaveReplay)) return false;
  press(SDL_GAMEPAD_BUTTON_SOUTH);
  settle();
  ReplayData data;
  if (app.anyDialog() || !board.consoleReplayPanel() || !ReplayData::load(path, data) ||
      data.game != game || data.shots != board.board().shotIndex || data.log.empty()) return false;
  auto bytes = [&]() {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
  };
  const auto original = bytes();
  if (original.empty()) return false;
  press(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  settle();
  auto* save = app.getDialog(D_SaveReplay);
  if (!save) return false;
  app.frame();
  for (auto& w : save->widgets) {
    if (auto* list = dynamic_cast<ListBox*>(w.get()); list && list->lines.size() == 1) list->onClick(0, 1);
  }
  save->onButton(102);
  auto* child = app.getDialog(D_DeleteReplay);
  if (!child || !child->noBtn || child->noBtn->label != "Cancel") return false;
  settle();
  Input move;
  move.type = Input::MouseMove;
  move.x = (app.canvasW() - 800) / 2 + child->x + child->noBtn->x + child->noBtn->w / 2;
  move.y = child->y + child->noBtn->y + child->noBtn->h / 2;
  app.input(move);
  edge(SDL_GAMEPAD_BUTTON_SOUTH, true);
  const bool held = child->capture == child->noBtn && child->noBtn->down && !child->scrollingOff;
  edge(SDL_GAMEPAD_BUTTON_SOUTH, false);
  const bool closing = child->scrollingOff && !save->scrollingOff;
  press(SDL_GAMEPAD_BUTTON_SOUTH);
  settle();
  const bool cancelled = app.getDialog(D_SaveReplay) == save && !app.getDialog(D_DeleteReplay) &&
                         app.hasModal() && !save->scrollingOff && board.consoleReplayPanel() && bytes() == original;
  std::cout << (game ? "Nights" : "Deluxe") << " SDL South: saved=" << original.size()
            << " bytes, after Cancel=" << bytes().size() << ", Cancel held=" << held << ", Save still open="
            << (app.getDialog(D_SaveReplay) == save) << '\n';
  if (app.getDialog(D_SaveReplay) != save) return false;
  save->onButton(102);
  child = app.getDialog(D_DeleteReplay);
  bool entering = false;
  if (child) {
    press(SDL_GAMEPAD_BUTTON_SOUTH);
    entering = !child->scrollingOff && bytes() == original;
    child->doScrollOff();
    settle();
  }
  save->doScrollOff();
  settle();
  return entering && held && closing && cancelled && !app.anyDialog();
}

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
  cfg.noRoomDiscovery = cfg.noSave = cfg.noAudio = true;
  cfg.userDir = fs::current_path() / ("render-test-profile-" + std::to_string(SDL_GetPerformanceCounter()));
  if (!fs::create_directory(cfg.userDir)) return false;
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
    app.gameMode = GameMode::QuickPlay;
    const auto withoutObjective = frame();
    app.gameMode = GameMode::Adventure;
    check(active.same(withoutObjective, {700, 320, 525, 40}) == (edition == Game::Nights),
          "opening objective should be present on the Deluxe board and absent on Nights");
    const V2 mid = boardRenderer.toBoard(985.5f, 541.5f);
    check(std::abs(mid.x - 325) < 1.5f && std::abs(mid.y - 260) < 1.5f, "stock 16:9 board is not at the measured PS3 scale and position");
    check(active.light(0, 540) > 40 && active.light(w - 1, 540) > 40, "16:9 gameplay leaves a black column at the screen edge");
    const auto average = [](const auto& pixel, int x0, int y0, int size) {
      long sum[3] = {};
      for (int y = y0; y < y0 + size; ++y)
        for (int x = x0; x < x0 + size; ++x)
          for (int c = 0; c < 3; ++c) sum[c] += pixel(x, y, c);
      return std::array<long, 3>{sum[0] / (size * size), sum[1] / (size * size), sum[2] / (size * size)};
    };
    for (auto [side, screenX, textureX] : {std::tuple{"left", 200, 269}, std::tuple{"right", 1750, 46}}) {
      auto gutter = loadGameImage(vfs, std::string("console/gutters/default_") + side);
      if (!gutter) { check(false, "gutter art did not load"); continue; }
      const auto shown = average([&](int x, int y, int c) { return (long)active.bytes[(size_t)(y * active.w + x) * 4 + c]; }, screenX, 90, 30);
      const auto art = average([&](int x, int y, int c) { return (long)gutter->rgba[(size_t)(y * gutter->w + x) * 4 + c]; }, textureX, 60, 20);
      bool close = true;
      for (int c = 0; c < 3; ++c) close &= std::abs(shown[c] - art[c]) < 14;
      check(close, side == std::string("left") ? "left PS3 gutter art does not reach the bezel" : "right PS3 gutter art does not reach the bezel");
    }
    check(active.detail({388, 620, 26, 280}) > .03, "balls in the ball tube are hidden");
    auto menuInk = [](const Pixels& p) {
      int ink = 0;
      for (int y = 960; y < 1005; ++y) for (int x = 355; x < 442; ++x) {
        const auto* c = p.bytes.data() + (size_t)(y * p.w + x) * 4;
        ink += c[0] > 2 * c[1] && c[2] > 1.7f * c[1] && c[2] > 75;
      }
      return ink;
    };
    check(menuInk(active) < 100, "idle PS3 ball tube is obscured by the PC MENU plate");
    Input hover;
    hover.type = Input::MouseMove;
    hover.x = (399.0f - g.offX()) / g.scale();
    hover.y = (971.0f - g.offY()) / g.scale();
    app.input(hover);
    check(menuInk(frame("menu-hover")) > 500, "hovering the PS3 tube cap did not reveal the MENU orb");
    hover.type = Input::MouseLeave;
    app.input(hover);
    check(menuInk(frame()) < 100, "leaving the PS3 tube cap did not hide the MENU orb");
    Hud duel;
    duel.mode = Hud::ModeDuel;
    duel.topScore = 1234567;
    Board scoreBoard = board->board();
    scoreBoard.dispScore[0] = duel.topScore;
    const auto scores = [&]() {
      boardRenderer.setFrameCanvas(board->canvasWidth());
      boardRenderer.draw(scoreBoard, board->bundle(), duel);
      SDL_RenderPresent(renderer.get());
      return snapshot();
    };
    const auto noNames = scores();
    duel.challengeText = duel.topName = "WWWWWWWWWWWWWWWW";
    const auto withNames = scores();
    check(!noNames.same(withNames, {535, 65, 95, 35}) && !noNames.same(withNames, {1135, 65, 92, 35}), "duel player labels disappeared");
    check(noNames.same(withNames, {665, 65, 190, 35}) && noNames.same(withNames, {1280, 65, 195, 35}), "duel player labels spill into the scores or past their windows");
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
  for (int i = 0; i < 3000 && board->board().phase != Phase::Aim; ++i) app.tick();
  check(board->board().phase == Phase::Aim, "shot fixture did not return to aiming");
  const auto ready = frame("replay-ready");
  if (wide && !art.empty() && w == 1920 && h == 1080) {
    check(!ready.same(active, {1530, 930, 84, 84}), "instant replay prompt did not appear after a shot");
    check(!ready.same(active, {1536, 1029, 76, 12}), "the board clip cuts off the bottom of the PS3 replay ring");
    app.pad = {Pad::PlayStation, {}};
    const auto ps = frame("replay-pad");
    check(!ready.same(ps, {1548, 948, 42, 42}), "the keyboard and PlayStation replay buttons look identical");
    int green = 0, keyInk = 0, keyX = 0, keyY = 0;
    for (int y = 933; y <= 1008; ++y) for (int x = 1530; x <= 1609; ++x) {
      const auto* c = ready.bytes.data() + (y * ready.w + x) * 4;
      green += c[1] > 120 && c[1] > c[0] + 52 && c[1] > c[2] + 65;
      if (std::abs(x - 1570) < 23 && std::abs(y - 971) < 26 && std::min({c[0],c[1],c[2]}) > 211 &&
          std::max({c[0],c[1],c[2]}) - std::min({c[0],c[1],c[2]}) < 32) { ++keyInk; keyX += x; keyY += y; }
    }
    check(green < 20, "the PC Instant Replay button duplicates the words on the console ring");
    check(keyInk > 70 && std::abs(keyX / keyInk - 1570) <= 3 && std::abs(keyY / keyInk - 971) <= 3,
          "the R key is not centered in the replay socket");
    app.pad = {Pad::Xbox, {}};
    const auto xbox = frame("replay-xbox-fallback");
    app.pad = {Pad::Nintendo, {}};
    const auto nintendo = frame("replay-switch-fallback");
    check(!ready.same(xbox, {1556, 953, 29, 37}) && !xbox.same(nintendo, {1556, 953, 29, 37}),
          "the replay socket does not distinguish the R key, Xbox Y and Switch X");
    app.pad = {Pad::PlayStation, {}};
  }
  if (wide && !art.empty() && w == 1920 && h == 1080) {
    app.gameMode = GameMode::QuickPlay;
    const auto quick = frame();
    app.gameMode = GameMode::Adventure;
    check(!quick.same(frame(), {680, 329, 595, 23}), "first-shot replay instruction is absent or leaks into Quick Play");
  }
  auto press = [&](int k) {
    Input e;
    e.key = k;
    for (auto type : {Input::KeyDown, Input::KeyUp}) { e.type = type; app.input(e); }
  };
  if (wide && !art.empty() && w == 1920) {
    Input hit;
    hit.button = 1;
    hit.wy = 971;
    auto pointer = [&](Input::Type t, float x) {
      hit.wx = x; hit.type = t; g.toCanvas(hit.wx, hit.wy, hit.x, hit.y); app.input(hit);
    };
    const float edge = edition == Game::Deluxe ? 1545.f : 1594.f;
    pointer(Input::MouseDown, edge); pointer(Input::MouseUp, 1624);
    check(!board->replaying() && !board->board().clickPending, "releasing off the replay button activated it or fired a shot");
    pointer(Input::MouseDown, edge); pointer(Input::MouseUp, edge);
  } else press(SDLK_R);
  check(board->replaying(), "the fitted button or R key did not start instant replay");
  if (!wide) {
    tick(14);
    frame("replay-playing");
    const int shots = board->board().shotIndex;
    press(SDLK_RETURN);
    check(board->replaying() && !board->board().clickPending && board->board().shotIndex == shots,
          "Classic Enter queued a live shot during replay");
    press(SDLK_ESCAPE);
    tick(14);
    check(!board->replaying() && board->board().phase == Phase::Aim && board->board().shotIndex == shots,
          "Classic replay exit fired a queued shot");
  }
  if (wide && !art.empty() && w == 1920 && h == 1080) {
    tick(14);
    const auto playing = frame("replay-playing");
    int orangeHint = 0;
    for (int y = 320; y < 357; ++y) for (int x = 900; x < 1155; ++x) {
      const auto* c = &active.bytes[(y * active.w + x) * 4];
      orangeHint += c[0] > 200 && c[1] > 70 && c[1] < 170 && c[2] < 45;
    }
    if (edition == Game::Deluxe) check(orangeHint > 150, "Deluxe first aim does not identify orange pegs in orange");
    auto yellowReplay = [](const Pixels& pixels, int left) {
      int count = 0;
      for (int y = 151; y < 196; ++y) for (int x = left; x < left + 111; ++x) {
        const auto* c = &pixels.bytes[(y * pixels.w + x) * 4];
        count += c[0] > 125 && c[1] > 120 && c[2] < c[1] / 2;
      }
      return count > 150;
    };
    check(yellowReplay(playing, 491) && yellowReplay(playing, 1364), "playback is missing one or both yellow REPLAY labels");
    check(playing.same(active, {1155, 54, 258, 44}), "playback draws over the right score window");
    const int replayShots = board->board().shotIndex, replayTick = board->board().tick;
    for (int key : {SDLK_SPACE, SDLK_RETURN}) {
      SavedSession beforePause, afterPause;
      check(board->fillSession(beforePause), "missing live replay log before focus pause");
      app.input(Input{Input::FocusLost});
      tick(10);
      app.input(Input{Input::FocusGained});
      check(board->paused() && board->replaying(), "focus regain did not leave replay paused");
      press(key);
      check(!board->paused() && board->replaying() && !board->board().clickPending &&
            board->board().shotIndex == replayShots && board->board().tick == replayTick,
            key == SDLK_SPACE ? "Space did not resume the paused replay cleanly" : "Enter did not resume the paused replay cleanly");
      check(board->fillSession(afterPause) && afterPause.log.size() == beforePause.log.size(), "replay resume recorded live-board input");
      if (board->paused()) board->pause(false);
      tick(1);
    }
    for (int i = 0; i < 8001 && board->replaying(); ++i) app.tick();
    check(!board->replaying(), "instant replay did not finish");
    const auto finished = frame("replay-finished");
    check(yellowReplay(finished, 491) && yellowReplay(finished, 1364), "finished replay lost its two REPLAY labels");
    check(finished.same(ready, {1535, 700, 63, 170}), "finished replay covers the fever meter with the PC button housing");
    app.pad = {};
    const auto keys = frame("replay-finished-keys");
    check(!keys.same(finished, {680, 330, 610, 120}), "end-panel prompts do not follow the input device");
    auto click = [&](int x, int y) {
      Input e;
      g.toCanvas((float)x, (float)y, e.x, e.y);
      e.button = 1;
      for (auto type : {Input::MouseMove, Input::MouseDown, Input::MouseUp}) { e.type = type; app.input(e); }
    };
    auto finish = [&]() { for (int i = 0; i < 8001 && board->replaying(); ++i) app.tick(); };
    auto cancelSave = [&]() {
      check(app.getDialog(D_SaveReplay) != nullptr, "Save did not open the local replay dialog");
      if (app.getDialog(D_SaveReplay)) { tick(); press(SDLK_ESCAPE); tick(); }
    };
    const int liveTick = board->board().tick, shots = board->board().shotIndex;
    const auto heldTick = board->board().tick;
    click(450, 700);
    tick(1);
    check(board->board().tick == heldTick && board->board().phase == Phase::Aim, "click outside the replay panel reached the board");
    check(replayPadSave(app, *board), "SDL pad Save / nested Delete Cancel failed");
    press(SDLK_F2);
    cancelSave();
    click(982, 434);
    cancelSave();
    click(982, 394);
    check(board->replaying(), "Watch Again row did not restart playback");
    finish();
    press(SDLK_R);
    check(board->replaying(), "R did not watch again from the end panel");
    finish();
    click(982, 354);
    tick(1);
    check(board->board().tick > liveTick && board->board().phase == Phase::Aim && board->board().shotIndex == shots,
          "Return row did not resume the same board without firing");
    press(SDLK_R);
    finish();
    const int beforeReturn = board->board().tick;
    press(SDLK_ESCAPE);
    tick(1);
    check(!app.anyDialog() && board->board().tick > beforeReturn && board->board().phase == Phase::Aim,
          "replay dismissal reached the board or failed to return");
    const auto liveHash = board->board().hash();
    press(SDLK_R);
    press(SDLK_LEFT);
    press(SDLK_RETURN);
    check(board->replaying() && board->board().hash() == liveHash && !board->board().clickPending && !board->board().wheelLock, "aim or fire changed the live board during playback");
    click(982, 394);
    tick(1);
    check(!board->replaying() && board->board().phase == Phase::Aim && board->board().shotIndex == shots, "click to skip changed the live board");
  }
  fs::remove_all(cfg.userDir);
  return ok;
}

static bool autoResize(const fs::path& assets, const fs::path& art, const fs::path& captures, Game edition) {
  Vfs vfs;
  if (!vfs.mountGame(assets, nullptr) || (!art.empty() && !vfs.mountPresentation(art))) return false;
  int w = 1900, h = 1057;
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
      SDL_CreateWindow("gameplay resize", w, h, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
  if (!window) return false;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(), "software"), SDL_DestroyRenderer);
  if (!renderer) return false;
  Gfx g(renderer.get(), vfs);
  Renderer render(renderer.get(), vfs);
  PlayerInfo player;
  App::Config cfg;
  cfg.game = edition;
  cfg.noRoomDiscovery = cfg.noSave = cfg.noAudio = true;
  cfg.userDir = fs::current_path() / "no-save-auto-render";
  cfg.widescreen = static_cast<int>(ui::WidescreenMode::Auto);
  cfg.seed = 1;
  App app(g, vfs, nullptr, &render, cfg);
  app.curProfile = &player;
  app.gameMode = GameMode::Adventure;
  app.curStage = app.curLevel = 0;
  app.showBoard(true, true);
  auto* board = dynamic_cast<GameScreen*>(app.screen());
  if (!board || !board->ok()) return false;
  auto tick = [&](int n) { while (n-- > 0) app.tick(); };
  tick(35);
  if (auto* tip = app.getDialog(D_Tip)) { app.dialogButton(tip->dialogId, ID_YES); tick(35); }
  bool ok = true;
  const std::string tag = std::string(edition == Game::Nights ? "Nights" : "Deluxe") + (art.empty() ? "-PC" : "");
  auto check = [&](bool pass, const char* msg) {
    if (!pass) { std::cerr << tag << " Auto " << w << 'x' << h << ": " << msg << '\n'; ok = false; }
  };
  auto frame = [&](const char* name = nullptr) {
    app.frame();
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> raw(SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(raw ? SDL_ConvertSurface(raw.get(), SDL_PIXELFORMAT_RGBA32) : nullptr, SDL_DestroySurface);
    SDL_RenderPresent(renderer.get());
    if (!rgba || rgba->w != w || rgba->h != h) throw std::runtime_error("wrong resize readback dimensions");
    if (name && !captures.empty()) check(SDL_SaveBMP(rgba.get(), (captures / (tag + "-" + std::to_string(w) + "x" + std::to_string(h) + name + ".bmp")).string().c_str()), "capture failed");
    auto* p = static_cast<Uint8*>(rgba->pixels);
    return Pixels{{p, p + size_t(rgba->pitch) * h}, rgba->pitch / 4};
  };
  Live live;
  live.app = &app; live.gfx = &g; live.ren = renderer.get(); live.win = window.get();
  auto event = [&](Uint32 type, float x = 0, float y = 0) {
    SDL_Event e{};
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) { e.motion.x = x; e.motion.y = y; }
    if (type == SDL_EVENT_MOUSE_BUTTON_DOWN || type == SDL_EVENT_MOUSE_BUTTON_UP) { e.button.x = x; e.button.y = y; e.button.button = SDL_BUTTON_LEFT; }
    if (type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP) e.key.key = SDLK_SPACE;
    handleEvent(live, e);
  };
  auto space = [&] { event(SDL_EVENT_KEY_DOWN); event(SDL_EVENT_KEY_UP); };
  for (auto [nw, nh, wide] : {std::tuple{1900,1057,true}, {800,600,false}, {1920,1080,true}, {1599,1000,false},
                              {1600,1000,true}, {1601,1000,true}, {1065,600,true}, {1067,600,true}}) {
    if (w != nw || h != nh) frame();
    w = nw; h = nh;
    if (!SDL_SetWindowSize(window.get(), w, h) || !SDL_SyncWindow(window.get())) return false;
    SDL_PumpEvents();
    const auto hash = board->board().hash();
    const auto actual = frame(w == 1900 ? "-auto" : nullptr);
    const int cw = wide ? 1066 : 800;
    std::cout << tag << " Auto " << w << 'x' << h << " first menu/gfx/render " << app.consoleCanvasWidth() << '/' << g.canvasW() << '/' << render.canvasWidth() << '\n';
    check(app.consoleCanvasWidth() == (wide ? 1067 : 800) && g.canvasW() == cw && render.canvasWidth() == cw, "first frame disagrees with selected presentation");
    app.settings.widescreen = static_cast<int>(wide ? ui::WidescreenMode::W16x9 : ui::WidescreenMode::Classic4x3);
    const auto expected = frame();
    check(actual.same(expected, {0,0,w,h}), "Auto pixels differ from the explicit presentation");
    check(board->board().W == 650 && hash == board->board().hash(), "presentation changed stock physics");
    const auto physical = [&](float x, float y) {
      const auto view = board->consoleView();
      return V2{g.offX() + (view.x + view.k * x) * g.scale(), g.offY() + (view.y + view.k * y) * g.scale()};
    };
    const int pad = (cw - 800) / 2;
    const auto aim = physical(pad + 309.5f, 327.5f), menu = physical(pad + 38.0f, 565);
    app.settings.widescreen = static_cast<int>(ui::WidescreenMode::Auto);
    frame();
    event(SDL_EVENT_MOUSE_MOTION, aim.x, aim.y);
    check(board->board().mouseX == 234 && board->board().mouseY == 287, "physical aim missed the rendered board point");
    const auto onBoard = render.toBoard(aim.x, aim.y);
    check(std::abs(onBoard.x - 234.5f) < .01f && std::abs(onBoard.y - 287.5f) < .01f, "renderer inverse disagrees with physical aim");
    for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) event(type, menu.x, menu.y);
    check(app.getDialog(D_Options) && board->paused() && !board->board().clickPending, "physical Menu click missed or fired");
    if (app.getDialog(D_Options)) app.finishOptionsDialog(false, false);
    if (art.empty() || (w != 1900 && w != 1920 && w != 1600 && w != 800)) continue;
    event(SDL_EVENT_WINDOW_MOUSE_LEAVE);
    event(SDL_EVENT_WINDOW_FOCUS_LOST);
    event(SDL_EVENT_WINDOW_FOCUS_GAINED);
    check(board->paused() && !app.anyDialog(), "focus did not leave a bare pause");
    const auto base = frame();
    tick(10);
    const auto paused = frame("-bare-paused");
    const float scale = g.scale(), ox = g.offX(), oy = g.offY();
    for (auto [x,y] : {std::pair{.5f,.005f}, {.5f,.995f}, {.015f,.48f}, {.985f,.48f}, {.26f,.32f}}) {
      int px = int(ox + g.canvasW() * scale * x), py = int(oy + 600 * scale * y);
      const int before = base.light(px,py), after = paused.light(px,py);
      std::cout << tag << " shade " << px << ',' << py << ' ' << before << " -> " << after << '\n';
      check(before > 15 && std::abs(after - before * 155.f / 255) <= 4, "bare pause does not apply one shade to frame sample");
    }
    if (ox >= 1) check(paused.same(base, {0,0,int(ox),h}) && paused.same(base, {w-int(ox),0,int(ox),h}), "pause changed side bars");
    if (oy >= 1) check(paused.same(base, {0,0,w,int(oy)}) && paused.same(base, {0,h-int(oy),w,int(oy)}), "pause changed top/bottom bars");
    int l = w, r = -1, t = h, b = -1;
    for (int y = int(h * .4f); y < int(h * .6f); ++y) for (int x = int(w * .3f); x < int(w * .7f); ++x) {
      if (paused.light(x,y) <= base.light(x,y) * 155 / 255 + 30) continue;
      l = std::min(x,l); r = std::max(x,r); t = std::min(y,t); b = std::max(y,b);
    }
    check(r-l > 60*scale && b-t > 15*scale && std::abs(l+r+1-w) <= 5*scale && std::abs(t+b+1-h) <= 5*scale, "PAUSED ink is not centered on the frame");
    if (w == 1600) {
      const int frozen = board->board().tick;
      for (int resized : {1599, 1600}) {
        w = resized;
        if (!SDL_SetWindowSize(window.get(), w, h) || !SDL_SyncWindow(window.get())) return false;
        SDL_PumpEvents();
        const auto changed = frame();
        check(board->paused() && board->board().tick == frozen && g.canvasW() == (w == 1599 ? 800 : 1066), "resize lost the bare pause or previous canvas");
        if (w == 1600) check(changed.same(paused, {0,0,w,h}), "paused pixels did not survive resize round trip");
      }
    }
    space();
    check(!board->paused(), "Space did not resume focus pause");
    tick(10);
    space();
    check(board->paused() && !app.anyDialog(), "Space did not enter bare pause");
    if (board->paused()) {
      event(SDL_EVENT_WINDOW_FOCUS_LOST);
      event(SDL_EVENT_WINDOW_FOCUS_GAINED);
      tick(10);
      space();
      check(!board->paused(), "Space and focus loss stacked two bare pauses");
      if (board->paused()) space();
      tick(10);
    }
  }
  w = 1400; h = 600;
  if (!SDL_SetWindowSize(window.get(), w, h) || !SDL_SyncWindow(window.get())) return false;
  SDL_PumpEvents();
  for (auto [mode, cw] : {std::pair{ui::WidescreenMode::W16x10,960}, {ui::WidescreenMode::W21x9,1400}}) {
    app.settings.widescreen = static_cast<int>(mode);
    frame();
    check(g.canvasW() == cw && render.canvasWidth() == cw && board->board().W == 650, "explicit aspect frame or board width changed");
  }
  app.settings.widescreen = static_cast<int>(ui::WidescreenMode::Auto);
  app.showMainMenu();
  tick(35);
  for (auto [nw, nh, wide] : {std::tuple{1900,1057,true}, {1599,1000,false}, {1600,1000,true}}) {
    w = nw; h = nh;
    if (!SDL_SetWindowSize(window.get(), w, h) || !SDL_SyncWindow(window.get())) return false;
    SDL_PumpEvents();
    const auto first = frame();
    auto* menu = dynamic_cast<MainMenu*>(app.screen());
    check(menu && g.canvasW() == (wide ? 1067 : 800) && menu->bOptions->visible != wide, "first resized Home frame used the wrong controls");
    if (!menu) return false;
    app.settings.widescreen = static_cast<int>(wide ? ui::WidescreenMode::W16x9 : ui::WidescreenMode::Classic4x3);
    check(first.same(frame(), {0,0,w,h}), "Auto Home pixels differ from the explicit presentation");
    app.settings.widescreen = static_cast<int>(ui::WidescreenMode::Auto);
    const auto* b = menu->bOptions;
    const float x = g.offX() + g.scale() * (wide ? 833.0f : b->x + b->w / 2);
    const float y = g.offY() + g.scale() * (wide ? 356.0f : b->y + b->h / 2);
    for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) event(type, x, y);
    check(app.getDialog(D_Options) != nullptr, "physical Home Options click missed");
    if (app.getDialog(D_Options)) app.finishOptionsDialog(false, false);
  }
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: gameplay_render_test deluxe nights presentation [capture-dir]\n";
    return 2;
  }
  const fs::path art = argv[3], captures = argc > 4 ? fs::path(argv[4]) : fs::path{};
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
  if (!SDL_Init(SDL_INIT_VIDEO)) return 2;
  bool ok = true;
  for (auto edition : {Game::Deluxe, Game::Nights}) {
    const fs::path assets = argv[edition == Game::Deluxe ? 1 : 2];
    ok &= autoResize(assets, art, captures, edition);
    ok &= autoResize(assets, {}, captures, edition);
    ok &= run(assets, art, captures, edition, 1920, 1080, true);
    ok &= run(assets, art, captures, edition, 1572, 1067, true);
    ok &= run(assets, {}, captures, edition, 1920, 1080, true);
    ok &= run(assets, art, captures, edition, 1920, 1080, false);
  }
  SDL_Quit();
  return ok ? 0 : 1;
}
