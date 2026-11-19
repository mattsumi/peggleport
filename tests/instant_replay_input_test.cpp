#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
#include "ui/session.h"
#define private public
#include "ui/game_screen.h"
#undef private
#include "ui/run.cpp"

pp::ui::PadGlyphs pp::ui::readPad(SDL_Gamepad*) { return {Pad::PlayStation, {}}; }
using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;
using BallSample = std::array<float, 5>;

static BallSample sample(const Board& b) {
  const Ball& ball = b.balls.front();
  return {b.gunAngle, ball.pos.x, ball.pos.y, ball.vel.x, ball.vel.y};
}

static int coinHistory(GameScreen& gs, Renderer& renderer, bool nights) {
  gs.app.curStage = 0;
  gs.haveMouse_ = false;
  gs.levelFile_ = nights ? "Jimmy4" : "Level2";
  gs.restartLevel(false);
  if (!gs.ok()) return 1;
  Board& live = gs.board();
  auto step = [&] { gs.app.tick(); renderer.advance(gs.replaying() ? gs.replay_ : live); };
  auto coins = [&] {
    int n = 0;
    for (const auto& layer : renderer.fx_.eff) for (const auto& fx : layer) if (fx.type == 8) ++n;
    return n;
  };
  auto catcher = [&] {
    std::vector<std::array<float, 4>> particles;
    for (const auto& fx : renderer.fx_.eff[1])
      if (fx.type == 0x1a || fx.type == 0x1b) particles.push_back({float(fx.type), float(fx.age), fx.x, fx.y});
    return particles;
  };
  Input in{};
  in.type = Input::Wheel; in.wheel = nights ? 602.f : -69.f;
  gs.input(in);
  for (int i = 0; i < 50; ++i) step();
  for (int shot = 0; shot < 2; ++shot) {
    if (shot) {
      const auto view = gs.consoleView();
      in.type = Input::MouseMove;
      in.x = view.x + view.k * (gs.boardLeft() + (nights ? 275.5f : 440.5f));
      in.y = view.y + view.k * 500.5f;
      gs.input(in);
      for (int i = 0; i < 60; ++i) step();
    }
    gs.keyDown(SDLK_RETURN, 0);
    in.type = Input::KeyUp; in.key = SDLK_RETURN; gs.input(in);
    int spinFrame = -1;
    for (int i = 0; i < 8000 && live.shotIndex == shot; ++i) {
      step();
      if (spinFrame < 0 && live.coin.on) spinFrame = live.frames;
    }
    if (live.shotIndex != shot + 1 || (spinFrame >= 0) != (shot == 0) || (shot && !live.lastShotPegs && !live.bucketShot)) {
      std::cerr << "coin fixture: shot=" << shot << " spin=" << spinFrame << " pegs=" << live.lastShotPegs << " bucket=" << live.bucketShot << '\n'; return 1;
    }
    if (!shot) for (int i = 0; i < 260; ++i) step();
    gs.app.frame();
    const auto paused = catcher();
    if (shot && (live.phase != Phase::Aim || live.result.freeBalls[0] != 1 || !live.lastShotPegs || paused.size() < 2 ||
                 std::none_of(paused.begin(), paused.end(), [](const auto& p) { return p[0] == 0x1a && p[1] > 0; }))) return 1;
    renderer.takeSounds();
    auto returned = [&](const char* route) {
      gs.app.frame();
      if (catcher() == paused && renderer.takeSounds().empty()) return true;
      std::cerr << route << ": paused catcher particles=" << paused.size() << " returned=" << catcher().size() << '\n';
      return false;
    };
    const auto hash = live.hash();
    if (coins()) return 1;
    gs.keyDown(SDLK_R, 0);
    if (!gs.replaying()) return 1;
    if (shot) {
      step(); gs.app.frame(); gs.keyDown(SDLK_ESCAPE, 0);
      if (!returned("skip")) return 1;
      gs.keyDown(SDLK_R, 0);
    }
    renderer.advance(gs.replay_);
    bool sawSpin = false;
    for (int i = 0; i < 8000 && gs.replaying(); ++i) {
      const auto& r = gs.replay_;
      if ((spinFrame < 0 || r.frames < spinFrame) && coins()) { std::cerr << "historical coin rendered before a toss, shot=" << shot << '\n'; return 1; }
      if (r.frames == spinFrame) sawSpin = coins() == 1 && r.coin.on;
      step();
    }
    if (gs.replaying() || !gs.replayPanel_ || sawSpin != (spinFrame >= 0) || coins() || live.hash() != hash) return 1;
    if (!returned("finish")) return 1;
    gs.onButton(9101);
    if (!gs.replaying()) return 1;
    if (spinFrame >= 0) {
      while (gs.replaying() && gs.replay_.frames < spinFrame + 10) step();
      if (!gs.replaying() || coins() != 1) return 1;
      gs.keyDown(SDLK_ESCAPE, 0);
    } else {
      step(); if (coins()) return 1;
      while (gs.replaying()) step();
      if (!gs.replayPanel_ || !returned("Watch Again")) return 1;
      gs.onButton(9103);
      if (gs.replayPanel_ || !returned("Done")) return 1;
    }
    renderer.advance(live);
    if (gs.replaying() || coins() || live.hash() != hash) return 1;
  }
  return 0;
}

static int run(const fs::path& path, const fs::path& art, Game game, bool wide, int moveFrame, BallSample& release) {
  Vfs v;
  if (!v.mountGame(path, nullptr) || !v.mountPresentation(art)) return 1;
  const int w = wide ? 1920 : 800, h = wide ? 1080 : 600;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surface) return 1;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> ren(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  if (!ren) return 1;
  Gfx g(ren.get(), v);
  Renderer draw(ren.get(), v);
  App::Config cfg;
  cfg.game = game;
  cfg.noRoomDiscovery = cfg.noSave = cfg.noAudio = true;
  cfg.userDir = fs::current_path() / "instant-replay-no-save";
  cfg.seed = 1;
  cfg.widescreen = static_cast<int>(wide ? ui::WidescreenMode::W16x9 : ui::WidescreenMode::Classic4x3);
  PlayerInfo player;
  player.name = "Replay Test";
  App app(g, v, nullptr, &draw, cfg);
  app.curProfile = &player;
  app.gameMode = GameMode::Adventure;
  app.curStage = player.maxStage = game == Game::Nights ? 5 : 4;
  app.curLevel = 0;
  app.showBoard(true, true);
  auto* screen = dynamic_cast<GameScreen*>(app.screen());
  if (!screen || !screen->ok()) return 1;
  auto ticks = [&](int n) { while (n-- > 0) app.tick(); };
  ticks(35);
  if (auto* d = app.getDialog(D_Tip)) { app.dialogButton(d->dialogId, ID_YES); ticks(35); }
  if (screen->paused()) return 1;
  app.frame();
  Live live;
  live.app = &app; live.gfx = &g; live.ren = ren.get();
  auto click = [&] {
    for (auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
      SDL_Event e{};
      e.type = type; e.button.button = SDL_BUTTON_LEFT;
      e.button.x = wide ? 1415.f : 665.f; e.button.y = wide ? 865.f : 485.f;
      handleEvent(live, e);
    }
  };
  auto wheel = [&] { SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = 3; handleEvent(live, e); };
  Board& board = screen->board();
  const int start = board.frames;
  click();
  if (moveFrame >= 0) {
    ticks(moveFrame);
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = wide ? 650.f : 170.f; e.motion.y = wide ? 630.f : 320.f;
    handleEvent(live, e);
  }
  for (int i = 0; i < 100 && board.phase != Phase::Shot; ++i) ticks(1);
  if (board.balls.empty()) return 1;
  release = sample(board);
  const int launch = board.frames;
  std::vector<BallSample> track;
  for (int i = 0; i < 25; ++i) {
    if (board.balls.empty()) return 1;
    track.push_back(sample(board)); ticks(1);
  }
  for (int i = 0; i < 8000 && board.phase != Phase::Aim; ++i) ticks(1);
  if (board.phase != Phase::Aim) return 1;
  SavedSession saved;
  if (!screen->fillSession(saved)) return 1;
  const auto bytes = saved.bytes();
  const int lock = board.wheelLock, shots = board.shotIndex;
  const float aim = board.lmAim;
  int errors = 0;
  auto check = [&](bool ok, const char* msg) { if (!ok) { std::cerr << msg << '\n'; ++errors; } };
  for (int pass = 0; pass < 4; ++pass) {
    const bool all = pass == 3;
    if (all) {
      Input in;
      const auto view = screen->consoleView();
      in.x = view.x + view.k * (760 + screen->canvasWidth() - 800 - Renderer::hudInset(screen->canvasWidth(), board.W));
      in.y = view.y + view.k * 560;
      in.type = Input::MouseDown; in.button = 3; app.input(in);
      in.type = Input::MouseUp; app.input(in);
    } else if (pass == 0) screen->keyDown(SDLK_R, 0);
    else screen->onButton(9101);
    if (!screen->replaying() || screen->replayAllShots() != all) return 1;
    wheel();
    check(board.wheelLock == lock && board.lmAim == aim && screen->log_.size() == saved.log.size(), "wheel changed live state during review");
    ticks(launch - (all ? 0 : start) - 1);
    check(screen->replay_.phase == Phase::Aim && screen->replay_.balls.empty(), "replay launched early");
    ticks(1);
    bool exact = true;
    for (const auto& expected : track) {
      const auto& play = screen->replay_;
      if (play.balls.empty() || sample(play) != expected) exact = false;
      ticks(1);
    }
    check(exact, "release or subsequent ball track differs");
    for (int i = 0; i < 8100 && screen->replaying(); ++i) ticks(1);
    if (!screen->replayPanel_ || screen->replaying()) return 1;
    check(screen->replay_.frames == saved.frames, "review finished on a different frame");
    wheel();
    if (pass >= 2) screen->onButton(9103);
  }
  screen->keyDown(SDLK_R, 0);
  if (!screen->replaying()) return 1;
  wheel(); click();
  SavedSession after;
  check(!screen->replaying() && !screen->replayPanel_ && screen->fillSession(after) && after.bytes() == bytes, "return changed live board or history");
  ticks(1);
  check(board.frames == saved.frames + 1 && board.shotIndex == shots && !board.clickPending && board.wheelLock == lock, "live board did not resume cleanly");
  wheel();
  check(board.wheelLock == 30 && board.lmAim != aim && screen->log_.size() == saved.log.size() + 1, "live wheel did not resume");
  if (moveFrame == -1) errors += coinHistory(*screen, draw, game == Game::Nights);
  std::cout << (game == Game::Nights ? "Nights" : "Deluxe") << (wide ? " wide" : " Classic") << " motion=" << moveFrame
            << " elapsed=" << launch - start << " velocity=" << release[3] << ',' << release[4] << " samples=" << track.size() << " errors=" << errors << '\n';
  return errors;
}

int main(int argc, char** argv) {
  if (argc != 4 || !SDL_Init(0)) return 2;
  int errors = 0;
  for (bool wide : {false, true}) for (int e = 0; e < 2; ++e) {
    BallSample control{};
    for (int motion : {-1, 0, 2}) {
      BallSample release{};
      errors += run(argv[e + 1], argv[3], e ? Game::Nights : Game::Deluxe, wide, motion, release);
      if (motion < 0) control = release;
      else if (release[3] == control[3] && release[4] == control[4]) { std::cerr << "motion did not change release velocity\n"; ++errors; }
    }
  }
  SDL_Quit();
  return errors ? 1 : 0;
}
