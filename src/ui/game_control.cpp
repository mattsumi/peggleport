#include "ui/game_screen.h"
#include "mods/json.h"
#include "mods/manager.h"

#include <chrono>
#include <cmath>
#include <string_view>

namespace pp::ui {
namespace {
using mods::Json;
constexpr int controlHeight = 560;

bool activeController() {
  if (!mods::g_mods) return false;
  for (const auto& mod : mods::g_mods->mods())
    if (mod->enabled && mod->state == mods::ModInfo::Active && mod->m.control && mod->consented && mod->lua) return true;
  return false;
}

bool controlTokens(std::string_view s) {
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  for (size_t i = 0; i < s.size();) {
    const char c = s[i++];
    if (c == '"') {
      while (i < s.size() && s[i] != '"') {
        const auto ch = static_cast<unsigned char>(s[i++]);
        if (ch < 32) return false;
        if (ch != '\\') continue;
        if (i == s.size()) return false;
        const char escape = s[i++];
        if (escape == 'u') {
          for (int k = 0; k < 4; ++k) {
            if (i == s.size()) return false;
            const char h = s[i++];
            if (!digit(h) && !(h >= 'a' && h <= 'f') && !(h >= 'A' && h <= 'F')) return false;
          }
        } else if (std::string_view("\"\\/bfnrt").find(escape) == std::string_view::npos) return false;
      }
      if (i == s.size()) return false;
      ++i;
    } else if (c == 't' && s.substr(i - 1, 4) == "true") i += 3;
    else if (c == 'f' && s.substr(i - 1, 5) == "false") i += 4;
    else if (c == 'n' && s.substr(i - 1, 4) == "null") i += 3;
    else if (digit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
      --i;
      if (s[i] == '-') ++i;
      if (i == s.size()) return false;
      if (s[i] == '0') ++i;
      else {
        if (s[i] < '1' || s[i] > '9') return false;
        while (i < s.size() && digit(s[i])) ++i;
      }
      if (i < s.size() && s[i] == '.') {
        ++i;
        if (i == s.size() || !digit(s[i])) return false;
        while (i < s.size() && digit(s[i])) ++i;
      }
      if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (i == s.size() || !digit(s[i])) return false;
        while (i < s.size() && digit(s[i])) ++i;
      }
      if (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') return false;
    }
  }
  return true;
}

bool textField(const Json* j, size_t limit) {
  return j && j->t == Json::Str && !j->s.empty() && j->s.size() <= limit;
}

bool coordinate(const Json* j, int max) {
  return j && j->t == Json::Num && std::isfinite(j->n) && j->n >= 0 && j->n <= max && std::floor(j->n) == j->n;
}

Json position(float x, float y) {
  auto j = Json::object();
  j.set("x", Json::num(x));
  j.set("y", Json::num(y));
  return j;
}
}

std::string GameScreen::modControlReason() const {
  if (!ready_) return "not_ready";
  if (!activeController()) return "no_controller";
  if (viewer_) return "viewer";
  if (demo_) return "demo";
  if (duel_ || cfg_.duel || cfg_.players != 1 || board_.cfg.duel || board_.cfg.players != 1) return "multiplayer";
  if (done_ || board_.phase == Phase::Over) return "finished";
  if (app.screen() != this || app.moving() || app.gameSwitchPending() || app.wantsQuit()) return "transition";
  if (!app.windowFocused) return "unfocused";
  if (paused()) return "paused";
  if (app.anyDialog() || app.helpOverlay()) return "modal";
  if (replaying_ || replayPanel_) return "replay";
  if (board_.phase == Phase::Popping) return "popping";
  return {};
}

void GameScreen::releaseModControl() {
  if (controlFirePending_) {
    controlFirePending_ = false;
    if (board_.phase == Phase::Aim) {
      board_.gunCancelFire();
      if (recording_) log_.push_back({board_.tick, 7, 0, 0, board_.frames});
    }
  }
  if (!controlHeld_) return;
  recRelease();
  controlHeld_ = false;
}

void GameScreen::updateModControl() {
  if (board_.phase != Phase::Aim) controlFirePending_ = false;
  if (controlArmed_ && !activeController()) controlArmed_ = false;
  if (!controlArmed_ || !modControlReason().empty()) releaseModControl();
  else if (controlHeld_ && (board_.phase != Phase::Shot || std::chrono::steady_clock::now() >= controlLease_)) releaseModControl();
}

void GameScreen::resetModControl() {
  controlArmed_ = false;
  releaseModControl();
  controlLease_ = {};
  controlSession_ = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string GameScreen::modControl(const std::string& request) {
  const auto snapshot = [&] {
    const Board& b = board_;
    auto state = Json::object();
    std::string reason = modControlReason();
    if (reason.empty() && !controlArmed_) reason = "disarmed";
    state.set("session", Json::str(controlSession_));
    state.set("armed", Json::boolean(controlArmed_));
    state.set("pending_fire", Json::boolean(controlFirePending_ && b.phase == Phase::Aim && (b.clickPending || b.fireHeld || b.lstate == 10)));
    state.set("available", Json::boolean(reason.empty()));
    state.set("reason", Json::str(reason));
    state.set("width", Json::num(b.W));
    state.set("height", Json::num(controlHeight));
    state.set("tick", Json::num(b.tick));
    const char* phases[] = {"aim", "shot", "popping", "over"};
    state.set("phase", Json::str(phases[static_cast<int>(b.phase)]));
    state.set("score", Json::num(b.score[0]));
    state.set("balls_left", Json::num(b.ballsLeft[0]));
    state.set("orange_left", Json::num(b.orangeLeft));
    state.set("aim_x", Json::num(b.mouseX));
    state.set("aim_y", Json::num(b.mouseY));
    state.set("cannon", position(b.pivot().x, b.pivot().y));
    const int bucket = b.bucketIndex();
    if (bucket >= 0 && bucket < static_cast<int>(b.objs.size())) state.set("bucket", position(b.objs[bucket].pos.x, b.objs[bucket].pos.y));
    auto pegs = Json::array();
    for (size_t i = 0; i < b.objs.size(); ++i) {
      const auto& o = b.objs[i];
      if (!o.pegType) continue;
      auto peg = position(o.pos.x, o.pos.y);
      peg.set("id", Json::num(static_cast<double>(i)));
      peg.set("type", Json::num(o.pegType));
      peg.set("lit", Json::boolean(o.lit));
      peg.set("gone", Json::boolean(o.gone));
      const char* shapes[] = {"none", "circle", "line", "poly", "brick", "hole"};
      peg.set("shape", Json::str(shapes[static_cast<int>(o.shape)]));
      peg.set("radius", Json::num(o.radius));
      peg.set("rotation", Json::num(o.rot));
      pegs.a.push_back(std::move(peg));
    }
    state.set("pegs", std::move(pegs));
    auto balls = Json::array();
    for (const auto& live : b.balls) {
      auto ball = position(live.pos.x, live.pos.y);
      ball.set("id", Json::num(live.uid));
      ball.set("vx", Json::num(live.vel.x));
      ball.set("vy", Json::num(live.vel.y));
      ball.set("r", Json::num(live.r));
      ball.set("alive", Json::boolean(live.alive));
      balls.a.push_back(std::move(ball));
    }
    state.set("balls", std::move(balls));
    return state;
  };
  if (request.empty()) return snapshot().dump();
  auto reply = Json::object();
  reply.set("id", Json());
  reply.set("ok", Json::boolean(false));
  const auto fail = [&](const std::string& reason) { reply.set("error", Json::str(reason)); return reply.dump(); };
  Json command;
  if (request.size() > 4096 || !controlTokens(request) || !Json::parse(request, command, nullptr) || command.t != Json::Obj) return fail("invalid_request");
  const auto* id = command.get("id");
  if (textField(id, 64)) reply.set("id", *id);
  for (size_t i = 0; i < command.o.size(); ++i) {
    const auto& key = command.o[i].first;
    if (key != "id" && key != "session" && key != "expires" && key != "action" && key != "x" && key != "y" && key != "pressed") return fail("invalid_field");
    for (size_t k = 0; k < i; ++k) if (command.o[k].first == key) return fail("duplicate_field");
  }
  const auto* session = command.get("session");
  const auto* action = command.get("action");
  if (!textField(id, 64) || !textField(session, 128) || !textField(action, 32)) return fail("invalid_field");
  if (session->s != controlSession_) return fail("stale_session");
  const auto* expires = command.get("expires");
  const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  if (!expires || expires->t != Json::Num || !std::isfinite(expires->n) || expires->n <= now || expires->n > now + 10) return fail("expired_request");
  if (!controlArmed_) return fail("disarmed");
  const std::string reason = modControlReason();
  if (!reason.empty()) return fail(reason);
  const auto* x = command.get("x");
  const auto* y = command.get("y");
  const auto* pressed = command.get("pressed");
  if (action->s == "aim" || action->s == "fire") {
    if (board_.phase != Phase::Aim) return fail("not_aiming");
    if (pressed || ((action->s == "aim" || x || y) && (!coordinate(x, board_.W) || !coordinate(y, controlHeight)))) return fail("invalid_coordinates");
    if (action->s == "fire" && (board_.clickPending || board_.fireHeld || board_.fireCooldown > 0 || board_.ballsLeft[0] <= 0 || board_.lstate == 10)) return fail("not_ready_to_fire");
    if (x && y) {
      mouseBx_ = static_cast<int>(x->n);
      mouseBy_ = static_cast<int>(y->n);
      haveMouse_ = true;
      recMouse(mouseBx_, mouseBy_);
    }
    if (action->s == "fire") {
      clickBoard();
      controlFirePending_ = true;
    }
  } else if (action->s == "flippers") {
    if (x || y || !pressed || pressed->t != Json::Bool) return fail("invalid_flippers");
    if (board_.phase != Phase::Shot || board_.pturn[0][Board::P_FLIPPERS] <= 0) return fail("flippers_unavailable");
    if (pressed->b) {
      if (!controlHeld_) recClick();
      controlHeld_ = true;
      controlLease_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    } else releaseModControl();
  } else return fail("unknown_action");
  reply.set("ok", Json::boolean(true));
  reply.set("state", snapshot());
  return reply.dump();
}
}
