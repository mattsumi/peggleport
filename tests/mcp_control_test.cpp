#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include "mods/json.h"
#include "mods/manager.h"
#include "ui/session.h"
#define private public
#include "ui/game_screen.h"
#undef private

using namespace pp;
using namespace pp::ui;
using pp::mods::Json;
namespace fs = std::filesystem;

static void CHECK(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

static Json parse(const std::string& text) {
  Json j;
  CHECK(Json::parse(text, j, nullptr) && j.t == Json::Obj, "control response is a JSON object");
  return j;
}

static const Json& field(const Json& j, const char* name) {
  const auto* p = j.get(name);
  CHECK(p != nullptr, name);
  return *p;
}

static double epoch() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct ControlFont : FontProvider {
  std::unique_ptr<FontProvider> native;
  std::string words;
  explicit ControlFont(Gfx& gfx) : native(makeDefaultFontProvider(gfx.renderer(), &gfx)) {}
  float height(const std::string& n) override { return native->height(n); }
  float ascent(const std::string& n) override { return native->ascent(n); }
  float ascentPadding(const std::string& n) override { return native->ascentPadding(n); }
  float lineSpacing(const std::string& n) override { return native->lineSpacing(n); }
  float width(const std::string& n, const std::string& s) override { return native->width(n, s); }
  bool layerColors(const std::string& n, size_t i, uint32_t& m, uint32_t& a) override { return native->layerColors(n, i, m, a); }
  void setLayerColors(const std::string& n, size_t i, uint32_t m, uint32_t a) override { native->setLayerColors(n, i, m, a); }
  void draw(const std::string& n, float x, float y, const std::string& s, uint32_t rgb, float alpha) override {
    words += s + "\n";
    native->draw(n, x, y, s, rgb, alpha);
  }
};

static Json request(GameScreen& screen, const std::string& action) {
  auto j = Json::object();
  j.set("id", Json::str("native-test"));
  j.set("session", field(parse(screen.modControl("")), "session"));
  j.set("expires", Json::num(epoch() + 5));
  j.set("action", Json::str(action));
  return j;
}

static void rejected(GameScreen& screen, const std::string& command) {
  const auto before = screen.modControl("");
  const auto hash = screen.board_.hash();
  const auto count = screen.log_.size();
  const bool pending = screen.board_.clickPending, held = screen.board_.fireHeld, flip = screen.board_.flipHeld;
  const auto reply = parse(screen.modControl(command));
  CHECK(!field(reply, "ok").asBool(true) && !field(reply, "error").asStr().empty(), "command must be rejected");
  CHECK(screen.modControl("") == before && screen.board_.hash() == hash && screen.log_.size() == count &&
        screen.board_.clickPending == pending && screen.board_.fireHeld == held && screen.board_.flipHeld == flip,
        "rejected command mutated board or recording");
}

static void rejected(GameScreen& screen, const Json& command) {
  rejected(screen, command.dump());
}

static Json accepted(GameScreen& screen, const Json& command) {
  const auto reply = parse(screen.modControl(command.dump()));
  CHECK(field(reply, "id").asStr() == field(command, "id").asStr(), "response correlates request ID");
  CHECK(field(reply, "ok").asBool() && field(reply, "state").dump() == screen.modControl(""), "success returns current applied state");
  return field(reply, "state");
}

static void recordedInputs(GameScreen& screen, const fs::path& scratch, const std::string& name) {
  SavedSession saved, decoded;
  CHECK(screen.fillSession(saved), "capture cancelled-shot session");
  const auto bytes = saved.bytes();
  const bool cancelled = std::any_of(saved.log.begin(), saved.log.end(), [](const ReplayEvent& e) { return e.kind == 7; });
  const int version = cancelled ? 5 : 4;
  CHECK(bytes[4] == version, "only cancellation recordings use session format 5");
  CHECK(SavedSession::parse(bytes, decoded) && decoded.bytes() == bytes, "cancelled-shot session round-trip");
  if (cancelled) {
    auto old = bytes;
    old[4] = 4;
    CHECK(!SavedSession::parse(old, decoded), "old session format cannot contain cancellation");
    for (int fault = 0; fault < 3; ++fault) {
      auto bad = saved;
      if (fault == 0) std::find_if(bad.log.begin(), bad.log.end(), [](const ReplayEvent& e) { return e.kind == 7; })->a = 1;
      if (fault == 1) std::find_if(bad.log.begin(), bad.log.end(), [](const ReplayEvent& e) { return e.kind == 7; })->b = 1;
      if (fault == 2) { bad.mode = static_cast<int>(GameMode::Duel); bad.cfg.duel = true; bad.cfg.players = 2; }
      CHECK(!SavedSession::parse(bad.bytes(), decoded), "cancellation requires zero arguments and single-player session");
    }
  }
  CHECK(saved.save(scratch / (name + ".ppss")) && SavedSession::load(scratch / (name + ".ppss"), decoded), "cancelled-shot session disk round-trip");
  ReadySession ready;
  CHECK(decoded.rebuild(screen.bundle(), ready.board, ready.snap, ready.haveSnap, ready.aiSnap), "cancelled-shot session rebuild");
  CHECK(ready.board.hash() == screen.board_.hash() && ready.board.clickPending == screen.board_.clickPending &&
        ready.board.fireHeld == screen.board_.fireHeld && ready.board.fireCooldown == screen.board_.fireCooldown,
        "session rebuild preserves cancelled launcher state");
  ReplayData replay, loaded;
  replay.game = saved.game;
  replay.level = saved.level;
  replay.cfg = saved.cfg;
  replay.score = saved.score;
  replay.shots = saved.shots;
  replay.log = saved.log;
  const auto path = scratch / (name + replayExt(saved.game));
  CHECK(replay.save(path) && ReplayData::load(path, loaded), "cancelled-shot replay disk round-trip");
  std::ifstream file(path, std::ios::binary);
  file.seekg(4);
  CHECK(file.get() == version, "only cancellation recordings use replay format 5");
  file.close();
  if (cancelled) {
    std::fstream old(path, std::ios::binary | std::ios::in | std::ios::out);
    old.seekp(4);
    old.put(4);
    old.close();
    CHECK(!ReplayData::load(path, loaded), "old replay format cannot contain cancellation");
    for (int fault = 0; fault < 3; ++fault) {
      auto bad = replay;
      if (fault == 0) std::find_if(bad.log.begin(), bad.log.end(), [](const ReplayEvent& e) { return e.kind == 7; })->a = 1;
      if (fault == 1) std::find_if(bad.log.begin(), bad.log.end(), [](const ReplayEvent& e) { return e.kind == 7; })->b = 1;
      if (fault == 2) { bad.cfg.duel = true; bad.cfg.players = 2; }
      CHECK(bad.save(path) && !ReplayData::load(path, loaded), "cancellation requires zero arguments and single-player replay");
    }
    CHECK(replay.save(path) && ReplayData::load(path, loaded), "restore valid cancelled-shot replay");
  }
  auto cfg = screen.app.cfg;
  cfg.userDir = scratch / (name + "-viewer-profile");
  App app(screen.app.gfx, screen.app.vfs, nullptr, nullptr, cfg);
  PlayerInfo player;
  app.curProfile = &player;
  GameScreen viewer(app, loaded);
  CHECK(viewer.ok(), "real cancellation replay viewer loads");
  for (int i = 0; i < saved.frames; ++i) viewer.update();
  CHECK(viewer.board().hash() == saved.hash && viewer.board().result.shots == saved.shots &&
        viewer.board().clickPending == screen.board_.clickPending && viewer.board().fireHeld == screen.board_.fireHeld &&
        viewer.board().fireCooldown == screen.board_.fireCooldown, "real viewer consumes recorded cancellation");
  for (int i = 0; i < 20; ++i) {
    ready.board.step();
    ready.board.events.clear();
    viewer.update();
    CHECK(viewer.board().hash() == ready.board.hash() && viewer.board().result.shots == ready.board.result.shots,
          "rebuilt and viewed cancellation continue on the same trajectory");
  }
}

static void lastShotInputs(GameScreen& screen) {
  Board expected = screen.board_;
  screen.replayPanel_ = true;
  screen.onButton(9101);
  CHECK(!screen.replayAll_, "last-shot playback uses its native snapshot");
  for (int i = screen.snap_.frames; i < expected.frames; ++i) screen.update();
  CHECK(screen.replay_.hash() == expected.hash() && screen.replay_.fireHeld == expected.fireHeld &&
        screen.replay_.clickPending == expected.clickPending, "last-shot replay consumes cancellation and later real input");
  for (int i = 0; i < 20; ++i) {
    expected.step();
    expected.events.clear();
    screen.update();
    CHECK(screen.replay_.hash() == expected.hash() && screen.replay_.result.shots == expected.result.shots,
          "last-shot playback preserves cancelled or genuine shot trajectory");
  }
  screen.keyDown(SDLK_ESCAPE, 0);
}

static void exercise(const fs::path& assets, Game edition, const fs::path& scratch) {
  Vfs vfs;
  CHECK(vfs.mountGame(assets, nullptr), "mount real edition assets");
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  CHECK(surface != nullptr, "create software surface");
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  CHECK(renderer != nullptr, "create software renderer");
  Gfx gfx(renderer.get(), vfs);
  auto font = std::make_unique<ControlFont>(gfx);
  auto& words = font->words;
  gfx.setFontProvider(std::move(font));
  Renderer view(renderer.get(), vfs);
  PlayerInfo player;
  player.name = "MCP Native Test";
  player.maxStage = edition == Game::Nights ? 5 : 4;
  App::Config cfg;
  cfg.game = edition;
  cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = cfg.fastBoot = true;
  cfg.userDir = scratch / (edition == Game::Nights ? "nights-profile" : "deluxe-profile");
  cfg.seed = 1;
  mods::ModManager manager;
  manager.setUserDir(scratch / (edition == Game::Nights ? "nights-mods" : "deluxe-mods"));
  App app(gfx, vfs, nullptr, &view, cfg);
  app.curProfile = &player;
  app.gameMode = GameMode::Adventure;
  app.curStage = player.maxStage;
  app.showBoard(true, true);
  auto* screen = dynamic_cast<GameScreen*>(app.screen());
  CHECK(screen && screen->ok(), "load real local game screen");
  const auto ticks = [&](int n) { while (n-- > 0) app.tick(); };
  ticks(40);
  if (app.getDialog(D_Tip)) { app.killDialog(D_Tip); ticks(40); }
  CHECK(!screen->paused() && !app.anyDialog() && !app.moving(), "settled local board");
  const auto key = [&] { Input in{}; in.type = Input::KeyDown; in.key = SDLK_F8; app.input(in); };
  auto state = parse(screen->modControl(""));
  CHECK(field(state, "session").t == Json::Str && !field(state, "session").s.empty(), "screen session token");
  CHECK(!field(state, "armed").asBool() && !field(state, "available").asBool() && !field(state, "reason").asStr().empty(), "starts disarmed");
  CHECK(field(state, "width").asNum() == screen->board_.W && field(state, "height").asNum() == 560, "board-space dimensions");
  CHECK(field(state, "tick").asNum() == screen->board_.tick && field(state, "phase").asStr() == "aim", "live phase and tick");
  CHECK(field(state, "score").asNum() == screen->board_.score[0] && field(state, "balls_left").asNum() == screen->board_.ballsLeft[0] &&
        field(state, "orange_left").asNum() == screen->board_.orangeLeft, "live counters");
  CHECK(field(state, "pegs").t == Json::Arr && !field(state, "pegs").a.empty() && field(state, "balls").t == Json::Arr, "observable board geometry");
  for (const auto& peg : field(state, "pegs").a) {
    const auto index = static_cast<size_t>(field(peg, "id").asNum());
    CHECK(index < screen->board_.objs.size(), "peg identity");
    const auto& o = screen->board_.objs[index];
    CHECK(field(peg, "x").asNum() == o.pos.x && field(peg, "y").asNum() == o.pos.y && field(peg, "type").asNum() == o.pegType &&
          field(peg, "lit").asBool() == o.lit && field(peg, "gone").asBool() == o.gone && field(peg, "radius").asNum() == o.radius &&
          field(peg, "rotation").asNum() == o.rot, "peg snapshot follows live board");
    field(peg, "shape");
  }
  rejected(*screen, request(*screen, "fire"));
  rejected(*screen, request(*screen, "arm"));
  key();
  CHECK(!field(parse(screen->modControl("")), "armed").asBool(), "absent controller cannot arm");
  const auto controllerDir = scratch / "controller";
  fs::create_directories(controllerDir);
  std::ofstream(controllerDir / "mod.ini") << "id = native-controller\npermissions = control\nscripts = main.lua\ndeterminism = cosmetic\n";
  std::ofstream(controllerDir / "main.lua") << "calls = 0\nfunction on_control() calls = calls + 1; observed = control.state(); if pending then response = control.command(pending); pending = nil end end\n";
  manager.discover({controllerDir.string()});
  manager.activate(nullptr);
  key();
  CHECK(!field(parse(screen->modControl("")), "armed").asBool(), "unconsented controller cannot arm");
  CHECK(manager.setConsent("native-controller", true), "approve isolated controller");
  manager.activate(nullptr);
  auto* controller = manager.find("native-controller");
  CHECK(controller && controller->lua && controller->state == mods::ModInfo::Active, "real consented Lua controller");
  CHECK(manager.setEnabled("native-controller", false), "disable controller");
  key();
  CHECK(!field(parse(screen->modControl("")), "armed").asBool(), "disabled controller cannot arm");
  CHECK(manager.setEnabled("native-controller", true), "enable controller");
  key();
  state = parse(screen->modControl(""));
  CHECK(field(state, "armed").asBool() && field(state, "available").asBool(), "physical F8 arms local control");
  words.clear(); app.frame(); SDL_RenderPresent(renderer.get());
  CHECK(words.find("MCP CONTROL ON - F8 TO STOP") != std::string::npos, "armed status uses visible native text");
  const auto session = field(state, "session").s;
  auto hookAim = request(*screen, "aim"); hookAim.set("x", Json::num(210)); hookAim.set("y", Json::num(410));
  std::string result;
  CHECK(controller->lua->eval("pending = [==[" + hookAim.dump() + "]==]", &result).empty(), "queue isolated Lua input");
  const auto beforeHook = screen->board_.mouseX;
  app.frame();
  CHECK(screen->board_.mouseX == beforeHook && controller->lua->eval("return calls", &result).empty() && result == "0", "draw never executes control hook");
  ticks(1);
  CHECK(screen->board_.mouseX == 210 && screen->board_.mouseY == 410, "update dispatches real Lua control hook");
  CHECK(controller->lua->eval("return response", &result).empty() && field(parse(result), "ok").asBool(), "Lua receives applied native response");
  auto aim = request(*screen, "aim");
  aim.set("x", Json::num(200)); aim.set("y", Json::num(400));
  state = accepted(*screen, aim);
  CHECK(field(state, "aim_x").asNum() == 200 && field(state, "aim_y").asNum() == 400 && screen->board_.mouseX == 200 && screen->board_.mouseY == 400,
        "aim applies through native mouse input");
  CHECK(!screen->log_.empty() && screen->log_.back().kind == 0 && screen->log_.back().a == 200 && screen->log_.back().b == 400,
        "aim is recorded");
  for (const auto* raw : {"{", "[]", "null", "{}", "{\"x\":+1}", "{\"x\":01}", "{\"x\":1e}", "{\"id\":\"bad\\q\"}"}) rejected(*screen, std::string(raw));
  rejected(*screen, std::string(4097, ' '));
  for (const auto* action : {"arm", "pause", "menu", "exec", "unknown"}) rejected(*screen, request(*screen, action));
  for (const auto* name : {"id", "session", "action"}) {
    auto bad = aim; bad.set(name, Json::num(1)); rejected(*screen, bad);
  }
  for (const auto* name : {"id", "session"}) {
    auto bad = aim; bad.set(name, Json::str(std::string(129, 'a'))); rejected(*screen, bad);
  }
  auto bad = aim; bad.set("session", Json::str("stale-screen")); rejected(*screen, bad);
  bad = aim; bad.set("expires", Json::num(epoch() - 1)); rejected(*screen, bad);
  bad = aim; bad.set("expires", Json::num(epoch() + 20)); rejected(*screen, bad);
  bad = aim; bad.set("expires", Json::str("tomorrow")); rejected(*screen, bad);
  bad = aim; bad.o.emplace_back("action", Json::str("fire")); rejected(*screen, bad);
  for (const auto* value : {"+200", "0200", "200e", "200e+", "200.", "1e999", "true", "\"200\""}) {
    auto raw = aim.dump();
    const auto pos = raw.find("\"x\":200");
    CHECK(pos != std::string::npos, "numeric validation fixture");
    raw.replace(pos, 7, std::string("\"x\":") + value);
    rejected(*screen, raw);
  }
  bad = aim; bad.set("unrecognized", Json::num(1)); rejected(*screen, bad);
  bad = aim; bad.set("id", Json::str(std::string(65, 'a'))); rejected(*screen, bad);
  bad = aim; bad.set("id", Json::str("")); rejected(*screen, bad);
  for (auto [name, value] : {std::pair{"x", -1.0}, {"x", screen->board_.W + 1.0}, {"x", 1.5}, {"y", -1.0}, {"y", 561.0}, {"y", 1.5}}) {
    bad = aim; bad.set(name, Json::num(value)); rejected(*screen, bad);
  }
  bad = request(*screen, "aim"); bad.set("x", Json::num(100)); rejected(*screen, bad);
  bad = request(*screen, "fire"); bad.set("y", Json::num(400)); rejected(*screen, bad);
  auto flippers = request(*screen, "flippers"); flippers.set("pressed", Json::boolean(true));
  screen->board_.pturn[0][Board::P_FLIPPERS] = 1;
  rejected(*screen, flippers);
  CHECK(!screen->board_.clickPending && screen->board_.phase == Phase::Aim, "flippers cannot fire while aiming");
  screen->board_.pturn[0][Board::P_FLIPPERS] = 0;
  const auto blocked = [&] {
    CHECK(!field(parse(screen->modControl("")), "available").asBool(), "unsafe state unavailable");
    rejected(*screen, request(*screen, "fire"));
  };
  screen->pause(true); blocked();
  CHECK(controller->lua->eval("pending = [==[" + request(*screen, "fire").dump() + "]==]", &result).empty(), "queue paused controller request");
  ticks(1);
  CHECK(controller->lua->eval("return response", &result).empty() && !field(parse(result), "ok").asBool(true), "update services rejected commands before pause return");
  CHECK(controller->lua->eval("return observed", &result).empty() && field(parse(result), "reason").asStr() == "paused", "paused control state remains observable");
  screen->pause(false);
  app.windowFocused = false; blocked(); app.windowFocused = true;
  app.doDialog(D_Locked, true, "Control Test", "Modal", "OK", BUTTONS_FOOTER); blocked(); app.killDialog(D_Locked); ticks(25);
  screen->replaying_ = true; screen->replay_.score[0] = -123;
  blocked(); CHECK(field(parse(screen->modControl("")), "score").asNum() != -123, "snapshot never observes replay board"); screen->replaying_ = false;
  screen->replayPanel_ = true; blocked(); screen->replayPanel_ = false;
  screen->duel_ = true; blocked(); screen->duel_ = false;
  screen->cfg_.players = 2; blocked(); screen->cfg_.players = 1;
  screen->demo_ = true; blocked(); screen->demo_ = false;
  screen->viewer_ = true; blocked(); screen->viewer_ = false;
  screen->done_ = true; blocked(); screen->done_ = false;
  screen->ready_ = false; blocked(); screen->ready_ = true;
  screen->board_.phase = Phase::Popping; blocked();
  screen->board_.phase = Phase::Over; blocked(); screen->board_.phase = Phase::Aim;
  app.showHelpScreen(true); blocked(); app.endHelpScreen(); ticks(25);
  CHECK(field(parse(screen->modControl("")), "available").asBool(), "control recovers after gates clear");
  screen->board_.W = 970;
  aim = request(*screen, "aim"); aim.set("x", Json::num(970)); aim.set("y", Json::num(560)); accepted(*screen, aim);
  bad = aim; bad.set("x", Json::num(971)); rejected(*screen, bad);
  screen->board_.W = screen->cfg_.boardWidth;
  aim = request(*screen, "aim"); aim.set("x", Json::num(screen->board_.W / 2)); aim.set("y", Json::num(400)); accepted(*screen, aim);
  accepted(*screen, request(*screen, "fire"));
  CHECK(field(parse(screen->modControl("")), "pending_fire").asBool(), "accepted agent shot is visibly pending before simulation");
  rejected(*screen, request(*screen, "fire"));
  ticks(1);
  CHECK(screen->board_.phase == Phase::Aim && screen->board_.fireHeld && screen->board_.fireCooldown > 0,
        "agent shot is pending during the native launcher cooldown");
  CHECK(field(parse(screen->modControl("")), "pending_fire").asBool(), "native cooldown remains visibly pending");
  const auto cooldown = screen->board_.fireCooldown;
  key();
  CHECK(!screen->board_.clickPending && !screen->board_.fireHeld && screen->board_.fireCooldown == cooldown &&
        !field(parse(screen->modControl("")), "pending_fire").asBool(), "F8 cancels only pending launch, not elapsed cooldown");
  key();
  rejected(*screen, request(*screen, "fire"));
  key();
  ticks(20);
  CHECK(screen->board_.phase == Phase::Aim && screen->board_.result.shots == 0 && screen->board_.balls.empty(),
        "F8 during launcher cooldown prevents the pending agent shot");
  CHECK(screen->log_.back().kind == 7, "pending agent cancellation is recorded");
  recordedInputs(*screen, scratch, edition == Game::Nights ? "nights-cancel" : "deluxe-cancel");
  lastShotInputs(*screen);
  key();
  accepted(*screen, request(*screen, "fire"));
  const auto cancelFrame = screen->board_.frames, cancelTick = screen->board_.tick;
  key();
  CHECK(!screen->board_.clickPending && !screen->board_.fireHeld && screen->log_.back().kind == 7 &&
        screen->log_.back().frame == cancelFrame && screen->log_.back().tick == cancelTick,
        "F8 before first simulation step cancels and records the exact input boundary");
  ticks(20);
  CHECK(screen->board_.result.shots == 0, "cancelled unconsumed agent click never launches later");
  recordedInputs(*screen, scratch, edition == Game::Nights ? "nights-immediate" : "deluxe-immediate");
  lastShotInputs(*screen);
  key();
  const auto logBeforeFire = screen->log_.size();
  accepted(*screen, request(*screen, "fire"));
  CHECK(screen->haveSnap_ && screen->board_.clickPending && screen->log_.size() == logBeforeFire + 1 && screen->log_.back().kind == 1,
        "fire uses native snapshot and recording path");
  for (int i = 0; i < 100 && screen->board_.phase == Phase::Aim; ++i) ticks(1);
  CHECK(screen->board_.phase == Phase::Shot && !screen->board_.balls.empty(), "recorded control fire produces a real shot");
  CHECK(!field(parse(screen->modControl("")), "pending_fire").asBool(), "launched shot is no longer agent-owned pending fire");
  recordedInputs(*screen, scratch, edition == Game::Nights ? "nights-later-shot" : "deluxe-later-shot");
  lastShotInputs(*screen);
  state = parse(screen->modControl(""));
  CHECK(!field(state, "balls").a.empty() && field(field(state, "balls").a.front(), "id").asNum() == screen->board_.balls.front().uid,
        "live ball snapshot");
  rejected(*screen, aim);
  rejected(*screen, request(*screen, "fire"));
  rejected(*screen, flippers);
  screen->board_.pturn[0][Board::P_FLIPPERS] = 1;
  flippers = request(*screen, "flippers"); flippers.set("pressed", Json::boolean(true));
  accepted(*screen, flippers);
  CHECK(screen->board_.flipHeld && screen->log_.back().kind == 1, "flippers press is recorded");
  Input physicalRelease{};
  physicalRelease.type = Input::KeyUp;
  physicalRelease.key = SDLK_RETURN;
  app.input(physicalRelease);
  CHECK(!screen->board_.flipHeld, "physical release clears actual flippers");
  accepted(*screen, flippers);
  CHECK(screen->board_.flipHeld, "agent reacquires flippers after a physical release before lease expiry");
  auto release = flippers; release.set("pressed", Json::boolean(false)); accepted(*screen, release);
  CHECK(!screen->board_.flipHeld && screen->log_.back().kind == 3, "explicit flippers release is recorded");
  accepted(*screen, flippers);
  SDL_Delay(1100); ticks(1);
  CHECK(!screen->board_.flipHeld && screen->log_.back().kind == 3, "dead client lease releases and records flippers");
  accepted(*screen, flippers);
  app.windowFocused = false; ticks(1);
  CHECK(!screen->board_.flipHeld && screen->log_.back().kind == 3, "focus loss releases held control");
  app.windowFocused = true;
  accepted(*screen, flippers);
  screen->pause(true); ticks(1);
  CHECK(!screen->board_.flipHeld && screen->log_.back().kind == 3, "pause releases held control before early return");
  screen->pause(false);
  accepted(*screen, flippers);
  app.doDialog(D_Locked, true, "Control Test", "Modal", "OK", BUTTONS_FOOTER); ticks(1);
  CHECK(!screen->board_.flipHeld && screen->log_.back().kind == 3, "modal releases held control");
  app.killDialog(D_Locked); ticks(25);
  accepted(*screen, flippers);
  key();
  CHECK(!screen->board_.flipHeld && !field(parse(screen->modControl("")), "armed").asBool() && screen->log_.back().kind == 3,
        "F8 revokes and releases held control");
  words.clear(); app.frame(); SDL_RenderPresent(renderer.get());
  CHECK(words.find("MCP CONTROL ON - F8 TO STOP") == std::string::npos, "revoked indicator disappears");
  rejected(*screen, request(*screen, "fire"));
  key(); accepted(*screen, flippers);
  CHECK(manager.setEnabled("native-controller", false), "disable active controller");
  ticks(1);
  CHECK(!screen->board_.flipHeld && !field(parse(screen->modControl("")), "armed").asBool(), "disabled mod revokes and releases control");
  CHECK(manager.setEnabled("native-controller", true), "restore controller");
  for (const std::string gate : {"pause", "unfocus", "modal", "disabled", "leave", "brief_pause", "brief_unfocus"}) {
    for (const int delay : {0, 1}) {
      screen->restartLevel(false);
      ticks(25);
      key();
      accepted(*screen, request(*screen, "fire"));
      ticks(delay);
      if (gate == "pause") screen->pause(true);
      if (gate == "unfocus") { Input in{}; in.type = Input::FocusLost; app.input(in); }
      if (gate == "brief_pause") { screen->pause(true); screen->pause(false); }
      if (gate == "brief_unfocus") {
        Input in{};
        in.type = Input::FocusLost; app.input(in);
        in.type = Input::FocusGained; app.input(in);
        screen->pause(false);
      }
      if (gate == "modal") app.doDialog(D_Locked, true, "Control Test", "Modal", "OK", BUTTONS_FOOTER);
      if (gate == "disabled") CHECK(manager.setEnabled("native-controller", false), "disable pending-shot controller");
      if (gate == "leave") screen->onLeave();
      ticks(1);
      CHECK(screen->board_.phase == Phase::Aim && !screen->board_.clickPending && !screen->board_.fireHeld &&
            !field(parse(screen->modControl("")), "pending_fire").asBool() &&
            std::count_if(screen->log_.begin(), screen->log_.end(), [](const ReplayEvent& e) { return e.kind == 7; }) == 1,
            (gate + " cancels pending agent launch before simulation resumes").c_str());
      const auto count = screen->log_.size();
      ticks(3);
      CHECK(screen->log_.size() == count, "closed gate records cancellation only once");
      if (gate == "modal") app.killDialog(D_Locked);
      if (gate == "unfocus") { Input in{}; in.type = Input::FocusGained; app.input(in); }
      if (gate == "pause" || gate == "unfocus") screen->pause(false);
      if (gate == "disabled") CHECK(manager.setEnabled("native-controller", true), "restore pending-shot controller");
      if (field(parse(screen->modControl("")), "armed").asBool()) key();
      ticks(25);
      CHECK(screen->board_.phase == Phase::Aim && screen->board_.result.shots == 0 && screen->board_.balls.empty(),
            (gate + " cannot resurrect agent launch after gate reopens").c_str());
      recordedInputs(*screen, scratch, std::to_string(static_cast<int>(edition)) + "-" + gate + std::to_string(delay));
    }
  }
  for (const bool mouse : {false, true}) for (const int delay : {0, 1}) {
    screen->restartLevel(false);
    ticks(25);
    key();
    aim = request(*screen, "aim"); aim.set("x", Json::num(screen->board_.W / 2)); aim.set("y", Json::num(400)); accepted(*screen, aim);
    Board physical = screen->board_;
    physical.gunClick();
    accepted(*screen, request(*screen, "fire"));
    ticks(delay);
    for (int i = 0; i < delay; ++i) { physical.step(); physical.events.clear(); }
    Input in{};
    in.type = mouse ? Input::MouseDown : Input::KeyDown;
    in.key = SDLK_RETURN;
    in.button = 1;
    const auto v = screen->consoleView();
    in.x = v.x + v.k * (75 + (screen->canvasWidth() - 800 - (screen->board_.W - 650)) / 2 + screen->board_.mouseX + 0.5f);
    in.y = v.y + v.k * (40 + screen->board_.mouseY + 0.5f);
    app.input(in);
    in.type = mouse ? Input::MouseUp : Input::KeyUp;
    app.input(in);
    physical.gunClick();
    physical.gunRelease();
    CHECK(!screen->controlFirePending_, "actual physical click takes pending launch ownership from agent");
    key();
    CHECK(std::none_of(screen->log_.begin(), screen->log_.end(), [](const ReplayEvent& e) { return e.kind == 7; }),
          "revoking agent control does not record cancellation of a physical click");
    ticks(20);
    for (int i = 0; i < 20; ++i) { physical.step(); physical.events.clear(); }
    CHECK(screen->board_.phase == Phase::Shot && screen->board_.result.shots == 1 && screen->board_.hash() == physical.hash(),
          "physical click and release retain normal native launcher timing after agent revocation");
    recordedInputs(*screen, scratch, std::to_string(static_cast<int>(edition)) + (mouse ? "-mouse" : "-enter") + std::to_string(delay));
    lastShotInputs(*screen);
  }
  screen->restartLevel(false);
  ticks(25);
  screen->cfg_.master[0] = Board::P_ZEN;
  CHECK(screen->board_.load(screen->lb_.base, screen->lb_.level, screen->cfg_, nullptr), "load recorded Zen Master fixture");
  screen->log_.clear();
  screen->haveSnap_ = false;
  int zenX = -1;
  for (int x = 0; x <= screen->board_.W && zenX < 0; x += 10) {
    Board probe = screen->board_;
    probe.gunMouse(x, 400);
    probe.gunClick();
    bool launched = false;
    for (int i = 0; i < 12000; ++i) {
      probe.step();
      probe.events.clear();
      if (probe.phase == Phase::Shot) launched = true;
      if (probe.phase == Phase::Over || (launched && probe.phase == Phase::Aim)) break;
    }
    if (probe.phase == Phase::Aim && probe.pturn[0][Board::P_ZEN] > 0) zenX = x;
  }
  CHECK(zenX >= 0, "find a genuine recorded green-peg shot for Zen power");
  key();
  auto earnZen = request(*screen, "fire");
  earnZen.set("x", Json::num(zenX)); earnZen.set("y", Json::num(400));
  accepted(*screen, earnZen);
  for (int i = 0; i < 12000; ++i) {
    ticks(1);
    if (screen->board_.result.shots > 0 && screen->board_.phase == Phase::Aim) break;
  }
  CHECK(screen->board_.phase == Phase::Aim && screen->board_.pturn[0][Board::P_ZEN] > 0,
        "normal recorded shot earns Zen from a real green peg");
  const Board zenReady = screen->board_;
  const auto zenLog = screen->log_;
  for (const std::string gate : {"F8", "pause", "unfocus"}) {
    screen->board_ = zenReady;
    screen->log_ = zenLog;
    screen->controlArmed_ = true;
    accepted(*screen, request(*screen, "fire"));
    for (int i = 0; i < 20 && screen->board_.lstate != 10; ++i) ticks(1);
    CHECK(screen->board_.lstate == 10 && screen->board_.phase == Phase::Aim, "agent fire enters actual Zen preparation");
    CHECK(field(parse(screen->modControl("")), "pending_fire").asBool(), "Zen preparation remains visibly pending");
    const int shots = screen->board_.result.shots;
    if (gate == "F8") key();
    if (gate == "pause") { screen->pause(true); screen->pause(false); }
    if (gate == "unfocus") {
      Input in{}; in.type = Input::FocusLost; app.input(in);
      in.type = Input::FocusGained; app.input(in);
      screen->pause(false);
    }
    CHECK(screen->board_.lstate == 1 && !screen->board_.clickPending && !screen->board_.fireHeld,
          "revocation or a brief control gate leaves Zen preparation before launch");
    ticks(200);
    CHECK(screen->board_.phase == Phase::Aim && screen->board_.result.shots == shots && screen->board_.balls.empty(),
          "cancelled Zen preparation cannot launch later");
    recordedInputs(*screen, scratch, std::to_string(static_cast<int>(edition)) + "-zen-" + gate);
    lastShotInputs(*screen);
  }
  screen->restartLevel(false);
  ticks(25);
  key();
  const auto oldSession = field(parse(screen->modControl("")), "session").asStr();
  const auto oldRequest = request(*screen, "fire");
  accepted(*screen, oldRequest);
  ticks(1);
  screen->restartLevel(false);
  state = parse(screen->modControl(""));
  CHECK(!field(state, "armed").asBool() && field(state, "session").asStr() != oldSession && !screen->controlHeld_ &&
        !screen->controlFirePending_ && !screen->board_.clickPending && !screen->board_.fireHeld,
        "restart rotates session and revokes per-level approval");
  ticks(20);
  CHECK(screen->board_.result.shots == 0, "restart cannot carry a pending agent shot into the new level");
  rejected(*screen, oldRequest);
  key();
  CHECK(field(parse(screen->modControl("")), "armed").asBool(), "restarted level can receive fresh physical approval");
  rejected(*screen, oldRequest);
  key();
  GameScreen other(app, true, true);
  CHECK(field(parse(other.modControl("")), "session").asStr() != session, "different game screens have different sessions");
  CHECK(!field(parse(other.modControl("")), "armed").asBool(), "new game screen requires fresh physical opt-in");
  key();
  app.showMainMenu();
  rejected(*screen, flippers);
  ticks(25);
  CHECK(!fs::exists(cfg.userDir), "no personal or fixture profile was written");
  std::cout << (edition == Game::Nights ? "Nights" : "Deluxe") << " native MCP state, opt-in, validation, gates, pending-shot cancellation, replay/session, physical ownership and lease checks passed\n";
}

static int serve(const fs::path& assets, const fs::path& userDir, const fs::path& modDir) {
  CHECK(!fs::exists(userDir) || fs::is_empty(userDir), "serve requires a fresh disposable userdata directory");
  Vfs vfs;
  CHECK(vfs.mountGame(assets, nullptr), "serve mounts real Deluxe assets");
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  CHECK(surface != nullptr, "serve software surface");
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  CHECK(renderer != nullptr, "serve software renderer");
  Gfx gfx(renderer.get(), vfs);
  Renderer view(renderer.get(), vfs);
  PlayerInfo player;
  player.name = "MCP Disposable Host";
  player.maxStage = 4;
  App::Config cfg;
  cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = cfg.fastBoot = true;
  cfg.userDir = fs::absolute(userDir);
  cfg.seed = 1;
  mods::ModManager manager;
  manager.setUserDir(cfg.userDir);
  manager.discover({fs::absolute(modDir).string()});
  auto* mod = manager.find("mcp_agent");
  CHECK(mod && mod->m.control && manager.setConsent("mcp_agent", true), "approve MCP mod only in disposable userdata");
  manager.activate(nullptr);
  CHECK(mod->state == mods::ModInfo::Active && mod->lua, "serve activates real MCP mod");
  App app(gfx, vfs, nullptr, &view, cfg);
  app.curProfile = &player;
  app.gameMode = GameMode::Adventure;
  app.curStage = 4;
  app.showBoard(true, true);
  auto* screen = dynamic_cast<GameScreen*>(app.screen());
  CHECK(screen && screen->ok(), "serve loads real local board");
  for (int i = 0; i < 40; ++i) app.tick();
  if (app.getDialog(D_Tip)) {
    app.killDialog(D_Tip);
    for (int i = 0; i < 40; ++i) app.tick();
  }
  CHECK(!app.anyDialog() && !screen->paused() && !app.moving(), "serve starts without modal gates");
  struct Commands {
    std::mutex lock;
    std::vector<std::string> lines;
    std::atomic<bool> eof = false;
  };
  const auto input = std::make_shared<Commands>();
  std::thread([input] {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.size() > 32) continue;
      std::lock_guard lock(input->lock);
      if (input->lines.size() < 16) input->lines.push_back(line);
    }
    input->eof = true;
  }).detach();
  std::cout << "READY\n" << std::flush;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  bool stop = false;
  while (!stop && !input->eof && std::chrono::steady_clock::now() < end) {
    std::vector<std::string> lines;
    {
      std::lock_guard lock(input->lock);
      lines.swap(input->lines);
    }
    for (const auto& line : lines) {
      if (line == "stop") stop = true;
      else if (line == "arm") {
        Input in{}; in.type = Input::KeyDown; in.key = SDLK_F8; app.input(in);
        in.type = Input::KeyUp; app.input(in);
        std::cout << (field(parse(screen->modControl("")), "armed").asBool() ? "ARMED\n" : "DISARMED\n") << std::flush;
      }
    }
    app.tick();
    app.frame();
    SDL_RenderPresent(renderer.get());
    SDL_Delay(10);
  }
  if (field(parse(screen->modControl("")), "armed").asBool()) screen->keyDown(SDLK_F8, 0);
  app.tick();
  CHECK(!fs::exists(cfg.userDir / "users.dat"), "serve never writes player profiles");
  std::cout << "STOPPED\n" << std::flush;
  return 0;
}

int main(int argc, char** argv) try {
  const bool hosting = argc == 7 && std::string(argv[4]) == "--serve";
  if ((argc != 4 && !hosting) || !SDL_Init(0)) return 2;
  if (hosting) {
    const int result = serve(argv[1], argv[5], argv[6]);
    SDL_Quit();
    return result;
  }
  const auto scratch = fs::absolute(argv[3]) / ("mcp-native-" + std::to_string(SDL_GetPerformanceCounter()));
  exercise(argv[1], Game::Deluxe, scratch);
  exercise(argv[2], Game::Nights, scratch);
  SDL_Quit();
  return 0;
} catch (const std::exception& e) {
  std::cerr << "FAIL native MCP control: " << e.what() << '\n';
  SDL_Quit();
  return 1;
}
