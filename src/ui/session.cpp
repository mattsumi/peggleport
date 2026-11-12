#include "ui/session.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gamepaths.h"
#include "gun.h"
#include "log.h"
#include "masters.h"
#include "ui/app.h"
#include "ui/dialog.h"
#include "ui/game_screen.h"
#include "ui/screens.h"
#include "ui/widgets.h"

namespace pp::ui {

namespace {

constexpr uint32_t kMagic = 0x53535050;
constexpr uint32_t kVersion = 2;
constexpr size_t kHeadBytes = 20;

struct Ar {
  std::vector<uint8_t>* out = nullptr;
  const uint8_t* in = nullptr;
  size_t n = 0, pos = 0;
  bool bad = false;

  size_t left() const { return n - pos; }
  void u8(uint8_t& v) {
    if (out) { out->push_back(v); return; }
    if (left() < 1) { bad = true; v = 0; return; }
    v = in[pos++];
  }
  void u32(uint32_t& v) {
    if (out) {
      for (int i = 0; i < 4; i++) out->push_back((uint8_t)(v >> (8 * i)));
      return;
    }
    if (left() < 4) { bad = true; v = 0; return; }
    v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)in[pos + (size_t)i] << (8 * i);
    pos += 4;
  }
  void u64(uint64_t& v) {
    uint32_t lo = (uint32_t)(v & 0xffffffffu), hi = (uint32_t)(v >> 32);
    u32(lo);
    u32(hi);
    v = ((uint64_t)hi << 32) | lo;
  }
  void i32(int& v) {
    uint32_t u = (uint32_t)v;
    u32(u);
    v = (int)u;
  }
  void flag(bool& v) {
    uint8_t u = v ? 1 : 0;
    u8(u);
    if (!out && u > 1) bad = true;
    v = u != 0;
  }
  void f32(float& v) {
    uint32_t u = 0;
    static_assert(sizeof u == sizeof v);
    std::memcpy(&u, &v, 4);
    u32(u);
    std::memcpy(&v, &u, 4);
    if (!out && !std::isfinite(v)) bad = true;
  }
  void str(std::string& s) {
    uint32_t len = (uint32_t)s.size();
    u32(len);
    if (out) { out->insert(out->end(), s.begin(), s.end()); return; }
    if (bad || len > 4096 || left() < len) { bad = true; s.clear(); return; }
    s.assign((const char*)in + pos, len);
    pos += len;
  }
};

uint64_t digest(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

void syncRules(Ar& a, ChallengeRules& c) {
  a.flag(c.active); a.flag(c.noFreeballs); a.flag(c.noGreens); a.flag(c.noForceWin); a.flag(c.noEndOnWin); a.flag(c.freeballCovered);
  a.i32(c.scoreReq); a.i32(c.scoreCeil); a.i32(c.styleScoreReq); a.i32(c.uniqueStyleReq);
  for (int& v : c.styleShots) a.i32(v);
  for (int& v : c.powerups) a.i32(v);
  a.i32(c.magicHat); a.i32(c.tripleScore);
  a.f32(c.gravityMod); a.f32(c.projSpeedMod);
  a.i32(c.levelIndex); a.i32(c.levelCount);
}

void syncConfig(Ar& a, BoardConfig& c) {
  a.i32(c.boardWidth); a.i32(c.players); a.flag(c.duel); a.i32(c.balls); a.i32(c.startScore); a.i32(c.startScore2); a.i32(c.orange);
  a.u32(c.seed); a.flag(c.greens); a.flag(c.purple);
  for (int& v : c.master) a.i32(v);
  a.f32(c.zoomK); a.i32(c.fpBits); a.i32(c.masterIndex); a.i32(c.maxLevel); a.i32(c.curLevel);
  a.flag(c.clearLevel); a.flag(c.coinFlipSeen); a.flag(c.nights);
  a.i32(c.prevFirst); a.i32(c.prevFirstRun);
  syncRules(a, c.ch);
  a.flag(c.liveSpin); a.i32(c.tryNumber);
  uint32_t mask = c.physics.mask;
  a.u32(mask);
  PhysicsEdit edit;
  for (int k = 0; k < PhysicsParams::KeyCount; k++) {
    float v = c.physics.v.at(k);
    a.f32(v);
    if (!a.out && !a.bad && ((mask >> k) & 1u)) edit.set(k, (double)v);
  }
  if (!a.out) {
    if (mask >> PhysicsParams::KeyCount) a.bad = true;
    c.physics = edit;
  }
  uint32_t count = (uint32_t)c.pegTypes.size();
  a.u32(count);
  if (!a.out) {
    if (a.bad || count > 100000 || a.left() / 8 < count) { a.bad = true; return; }
    c.pegTypes.assign(count, {});
  }
  for (auto& [index, key] : c.pegTypes) {
    a.i32(index);
    a.str(key);
  }
}

bool rulesSane(const ChallengeRules& c) {
  for (int v : c.styleShots)
    if (v < 0 || v > 1000000) return false;
  for (int v : c.powerups)
    if (v < 0 || v > 1000000) return false;
  if (c.magicHat < 0 || c.magicHat > 1000000 || c.tripleScore < 0 || c.tripleScore > 1000000 || c.uniqueStyleReq < 0 || c.uniqueStyleReq > 1000000) return false;
  return c.scoreReq >= 0 && c.scoreReq <= 1000000000 && c.scoreCeil >= 0 && c.scoreCeil <= 1000000000 && c.styleScoreReq >= 0 && c.styleScoreReq <= 1000000000;
}

bool configSane(const BoardConfig& c) {
  if (c.boardWidth < 650 || c.boardWidth > 1400) return false;
  if (c.players < 1 || c.players > 2 || c.duel != (c.players == 2)) return false;
  if (c.balls < 0 || c.balls > 10000 || c.orange < 0 || c.orange > 10000) return false;
  for (int m : c.master)
    if (m < 0 || m >= masters::kPowerSlots) return false;
  if (c.masterIndex < -1 || c.masterIndex > 64) return false;
  if (c.tryNumber < 0 || c.tryNumber > 100000000) return false;
  if (c.ch.levelIndex < 0 || c.ch.levelCount < 0 || !rulesSane(c.ch)) return false;
  return true;
}

void syncChallenge(Ar& a, SavedChallenge& c) {
  a.i32(c.trophy); a.i32(c.step); a.i32(c.score); a.i32(c.score2); a.i32(c.lives); a.i32(c.balls); a.i32(c.over);
  a.flag(c.more);
}

void syncHistory(Ar& a, SavedHistory& h) {
  a.u32(h.rng); a.i32(h.lastOpponent); a.i32(h.lastRolled); a.i32(h.useCounter);
  uint32_t count = (uint32_t)h.use.size();
  a.u32(count);
  if (!a.out) {
    if (a.bad || count > 4096 || a.left() / 12 < count) { a.bad = true; return; }
    h.use.assign(count, {});
  }
  for (StageMgr::UseMark& m : h.use) { a.i32(m.stage); a.i32(m.level); a.i32(m.use); }
}

void syncDuel(Ar& a, SavedDuel& d) {
  a.i32(d.score2); a.i32(d.skill); a.i32(d.state); a.i32(d.wait); a.i32(d.tick); a.i32(d.think); a.i32(d.chosen);
}

int floatBits(float f) {
  int v = 0;
  std::memcpy(&v, &f, sizeof v);
  return v;
}

float bitsFloat(int v) {
  float f = 0;
  std::memcpy(&f, &v, sizeof f);
  return f;
}

bool angleSane(int bits) { return std::fabs(bitsFloat(bits)) <= 16.0f; }

void syncBody(Ar& a, SavedSession& s) {
  a.i32(s.game); a.i32(s.mode); a.str(s.level); a.u64(s.content);
  a.i32(s.stage); a.i32(s.levelNo);
  a.i32(s.shots); a.i32(s.score); a.i32(s.ballsLeft);
  a.i32(s.frames); a.i32(s.tick); a.u64(s.hash);
  syncConfig(a, s.cfg);
  uint32_t count = (uint32_t)s.log.size();
  a.u32(count);
  if (!a.out) {
    if (a.bad || count > SavedSession::kMaxEvents || a.left() / 17 < count) { a.bad = true; return; }
    s.log.assign(count, {});
  }
  for (ReplayEvent& e : s.log) {
    a.i32(e.frame); a.i32(e.tick); a.u8(e.kind); a.i32(e.a); a.i32(e.b);
  }
  a.u8(s.flags);
  syncHistory(a, s.hist);
  if (s.mode == (int)GameMode::Challenge) syncChallenge(a, s.ch);
  if (s.cfg.players == 2) syncDuel(a, s.duel);
}

bool bodySane(const SavedSession& s) {
  if (s.game < 0 || s.game > 1) return false;
  const bool challenge = s.mode == (int)GameMode::Challenge;
  const bool duel = s.mode == (int)GameMode::Duel;
  if (s.mode != (int)GameMode::Adventure && s.mode != (int)GameMode::QuickPlay && !challenge && !duel) return false;
  if (s.level.empty()) return false;
  if (s.hist.rng == 0 || s.hist.lastOpponent < -1 || s.hist.lastOpponent > 64 || s.hist.lastRolled < -1 || s.hist.lastRolled > 64) return false;
  if (s.hist.useCounter < 0 || s.hist.useCounter > 1000000000) return false;
  for (const StageMgr::UseMark& m : s.hist.use)
    if (m.stage < 0 || m.stage > 4096 || m.level < 0 || m.level > 4096 || m.use < -1000 || m.use > s.hist.useCounter) return false;
  if (!challenge && (s.cfg.startScore != 0 || s.cfg.startScore2 != 0)) return false;
  if (s.flags > 3 || ((s.flags & 2) != 0 && !s.cfg.duel)) return false;
  if (duel != (duel && s.cfg.duel) || (!duel && !challenge && s.cfg.duel)) return false;
  if (s.cfg.duel) {
    const SavedDuel& d = s.duel;
    if (d.state < 0 || d.state > 2 || d.skill < -1 || d.skill > 10) return false;
    if (d.wait < 0 || d.wait > 100000 || d.tick < 0 || d.tick > 100000 || d.think < 0 || d.think > 5000) return false;
    if (d.score2 < -100000000 || d.score2 > 1000000000 || !angleSane(d.chosen)) return false;
    if ((s.flags & 2) != 0 && d.state != 0) return false;
  }
  if (challenge) {
    const SavedChallenge& c = s.ch;
    if (s.cfg.startScore < -100000000 || s.cfg.startScore > 1000000000 || s.cfg.startScore2 < -100000000 || s.cfg.startScore2 > 1000000000) return false;
    if (c.trophy < 0 || c.step < 0 || c.step > 100000 || c.lives < 0 || c.lives > 100000 || c.balls < 0 || c.balls > 100000 || c.over < 0 || c.over > 3) return false;
    if (c.score < -100000000 || c.score > 1000000000 || c.score2 < -100000000 || c.score2 > 1000000000) return false;
    if (c.over != 0 && s.flags != 0) return false;
  } else if (s.ch.over != 0) {
    return false;
  }
  if (s.frames < 0 || s.frames > SavedSession::kMaxFrames || s.tick < 0 || s.tick > s.frames) return false;
  if (s.shots < 0 || s.ballsLeft < 0) return false;
  if (!configSane(s.cfg)) return false;
  int last = 0;
  for (const ReplayEvent& e : s.log) {
    if (e.kind > 6 || (e.kind > 3 && (!s.cfg.duel || (s.flags & 2) != 0))) return false;
    if (e.frame < last || e.frame > s.frames) return false;
    if (e.kind == 2 && (e.a < -1000000 || e.a > 1000000)) return false;
    if (e.kind == 4 && (e.a < 0 || e.a > 100000 || (e.b != 0 && e.b != 1))) return false;
    if ((e.kind == 5 || e.kind == 6) && !angleSane(e.a)) return false;
    last = e.frame;
  }
  return true;
}

}

std::vector<uint8_t> SavedSession::bytes() const {
  std::vector<uint8_t> body;
  Ar w;
  w.out = &body;
  SavedSession copy = *this;
  syncBody(w, copy);
  std::vector<uint8_t> file;
  Ar h;
  h.out = &file;
  uint32_t magic = kMagic, version = kVersion, size = (uint32_t)body.size();
  uint64_t sum = digest(body.data(), body.size());
  h.u32(magic); h.u32(version); h.u32(size); h.u64(sum);
  file.insert(file.end(), body.begin(), body.end());
  return file;
}

bool SavedSession::parse(const std::vector<uint8_t>& data, SavedSession& out) {
  if (data.size() < kHeadBytes || data.size() > kMaxBytes) return false;
  Ar h;
  h.in = data.data();
  h.n = kHeadBytes;
  uint32_t magic = 0, version = 0, size = 0;
  uint64_t sum = 0;
  h.u32(magic); h.u32(version); h.u32(size); h.u64(sum);
  if (h.bad || magic != kMagic || version != kVersion) return false;
  if ((size_t)size != data.size() - kHeadBytes) return false;
  if (digest(data.data() + kHeadBytes, size) != sum) return false;
  Ar r;
  r.in = data.data() + kHeadBytes;
  r.n = size;
  SavedSession s;
  syncBody(r, s);
  if (r.bad || r.pos != r.n || !bodySane(s)) return false;
  out = std::move(s);
  return true;
}

bool SavedSession::save(const std::filesystem::path& file) const {
  if (file.empty() || frames > kMaxFrames || log.size() > kMaxEvents) return false;
  const std::vector<uint8_t> data = bytes();
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::filesystem::path part = file;
  part += ".part";
  bool wrote = false;
  {
    std::ofstream f(part, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*)data.data(), (std::streamsize)data.size());
    f.flush();
    wrote = (bool)f;
  }
  if (!wrote) {
    std::filesystem::remove(part, ec);
    return false;
  }
  std::filesystem::remove(file, ec);
  std::filesystem::rename(part, file, ec);
  if (ec) {
    std::filesystem::remove(part, ec);
    return false;
  }
  return true;
}

bool SavedSession::load(const std::filesystem::path& file, SavedSession& out) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec || size < kHeadBytes || size > kMaxBytes) return false;
  std::ifstream f(file, std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> data((size_t)size);
  f.read((char*)data.data(), (std::streamsize)data.size());
  return (size_t)f.gcount() == data.size() && parse(data, out);
}

bool SavedSession::rebuild(const LevelBundle& lb, Board& board, Board& snap, bool& haveSnap, Board& aiSnap) const {
  if (lb.contentHash != content) return false;
  std::string err;
  Board b;
  if (!b.load(lb.base, lb.level, cfg, &err)) return false;
  haveSnap = false;
  bool begun = false;
  size_t next = 0;
  const bool ended = ch.over != 0;
  for (;;) {
    while (next < log.size() && log[next].frame <= b.frames) {
      const ReplayEvent& e = log[next++];
      if (e.kind == 0) b.gunMouse(e.a, e.b);
      else if (e.kind == 1) {
        if (b.phase == Phase::Aim) { snap = b; haveSnap = true; }
        b.gunClick();
      } else if (e.kind == 3) b.gunRelease();
      else if (e.kind == 4) {
        if (e.b == 1) { aiSnap = b; begun = true; }
        for (int i = 0; i < e.a; i++) b.rng.rand();
      } else if (e.kind == 5) {
        gun::setAngle(b.gunState, b.gunAngle, b.gunTarget, bitsFloat(e.a), true);
        b.lmAim = bitsFloat(e.a);
      } else if (e.kind == 6) {
        b.lmAim = bitsFloat(e.a);
        gun::setAngle(b.gunState, b.gunAngle, b.gunTarget, b.lmAim, true);
        b.fireHeld = true;
        b.fireCooldown = 0;
      } else b.gunWheel(e.a);
    }
    if (b.frames >= frames) break;
    b.step();
    b.events.clear();
    if (b.phase == Phase::Over && !ended) return false;
  }
  if (ended != (b.phase == Phase::Over)) return false;
  if (next != log.size() || b.tick != tick || b.hash() != hash) return false;
  if (b.result.shots != shots || b.score[0] != score || b.ballsLeft[0] != ballsLeft) return false;
  if (cfg.duel && (b.score[1] != duel.score2 || (duel.state == 1 && !begun))) return false;
  board = std::move(b);
  return true;
}

SessionKeeper& sessionsOf(App& a) {
  if (!a.sessions) a.sessions = std::make_shared<SessionKeeper>(a);
  return *a.sessions;
}

std::filesystem::path SessionKeeper::file(GameMode m) const {
  if (!app_.curProfile || app_.cfg.noSave) return {};
  return app_.profiles.savedGamePath(*app_.curProfile, m);
}

bool SessionKeeper::keep(const GameScreen& gs) {
  SavedSession s;
  if (!gs.fillSession(s)) return false;
  const std::filesystem::path path = file((GameMode)s.mode);
  if (path.empty()) return false;
  if (!s.save(path)) {
    pp::log::warn("game", "the level in progress could not be saved to " + pathToUtf8(path));
    return false;
  }
  return true;
}

bool SessionKeeper::find(GameMode m) {
  ready_.reset();
  if (m != GameMode::Adventure && m != GameMode::QuickPlay && m != GameMode::Challenge && m != GameMode::Duel) return false;
  const std::filesystem::path path = file(m);
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec)) return false;
  auto r = std::make_unique<ReadySession>();
  const LevelInfo* li = m == GameMode::Adventure ? app_.curLevelInfo() : nullptr;
  std::string err;
  bool ok = SavedSession::load(path, r->saved) && r->saved.game == (app_.game == Game::Nights ? 1 : 0) && r->saved.mode == (int)m;
  if (ok && m != GameMode::Adventure) li = app_.stages.levelByName(r->saved.level);
  if (ok && m == GameMode::Challenge) {
    const TrophyInfo* t = app_.trophies.byId(r->saved.ch.trophy);
    ok = t && r->saved.ch.step < (int)t->levels.size() && r->saved.cfg.duel == !t->opponents.empty();
  } else {
    ok = ok && li && (m != GameMode::Adventure || li->file == r->saved.level);
  }
  ok = ok && loadLevelBundle(app_.vfs, r->saved.level, r->bundle, &err) && r->saved.rebuild(r->bundle, r->board, r->snap, r->haveSnap, r->aiSnap);
  if (ok && li) {
    r->saved.stage = li->stage;
    r->saved.levelNo = li->index;
  }
  if (!ok) {
    pp::log::warn("game", "the saved level in " + pathToUtf8(path) + " could not be used and was removed");
    std::filesystem::remove(path, ec);
    return false;
  }
  ready_ = std::move(r);
  return true;
}

void SessionKeeper::drop(GameMode m) {
  ready_.reset();
  const std::filesystem::path path = file(m);
  std::error_code ec;
  if (!path.empty()) std::filesystem::remove(path, ec);
}

bool SessionKeeper::resume() {
  if (!ready_) return false;
  std::unique_ptr<ReadySession> r = std::move(ready_);
  drop((GameMode)r->saved.mode);
  app_.gameMode = (GameMode)r->saved.mode;
  app_.lastOpponent = r->saved.hist.lastOpponent;
  app_.lastRolled = r->saved.hist.lastRolled;
  app_.stages.restoreUse(r->saved.hist.use, r->saved.hist.useCounter);
  if (app_.gameMode != GameMode::Adventure) {
    app_.curStage = r->saved.stage;
    app_.curLevel = r->saved.levelNo;
    app_.packStage = -1;
  }
  if (app_.gameMode == GameMode::Challenge) {
    const SavedChallenge& c = r->saved.ch;
    app_.curChallenge = c.trophy;
    app_.challengeStep = c.step;
    app_.challengeLevel = r->saved.level;
    app_.challengeScore = c.score;
    app_.challengeScore2 = c.score2;
    app_.challengeLives = c.lives;
    app_.challengeBalls = c.balls;
    app_.challengeMore = c.more;
    app_.challengeKeepLives = false;
    app_.endBodyOverride.clear();
    app_.endHeaderOverride.clear();
  }
  if (app_.curProfile) app_.applyColorblind(app_.curProfile->colorblind);
  app_.setScreen(std::make_unique<GameScreen>(app_, *r), App::Left);
  app_.setRandState(r->saved.hist.rng);
  return true;
}

namespace {

std::string withCommas(int v) {
  const std::string digits = std::to_string(v < 0 ? -(long long)v : (long long)v);
  std::string out;
  for (size_t i = 0; i < digits.size(); i++) {
    if (i && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return v < 0 ? "-" + out : out;
}

std::string duelLines(const SavedSession& s, const std::string& me, bool human2) {
  return me + " - " + withCommas(s.score) + " points\n" + (human2 ? "Player 2" : "Computer") + " - " + withCommas(s.duel.score2) + " points\n";
}

class ContinueDialog : public Dialog {
 public:
  ContinueDialog(App& a, const std::string& text, bool newGame) : Dialog(a, D_ContinueGame, true, "Continue Game?", text, "Cancel", BUTTONS_FOOTER) {
    textAlign = -1;
    goOn_ = extra(0, "Continue");
    again_ = extra(1, a.gameMode == GameMode::Adventure ? "Restart Level" : "New Game");
    if (newGame) fresh_ = extra(2, "New Game");
  }
  float preferredHeight(float width) override { return Dialog::preferredHeight(width) + (app.game == Game::Nights ? 0x3c : 0x28); }
  void update() override {
    Dialog::update();
    place();
  }
  void place() {
    if (!yesBtn) return;
    const float half = std::floor((w - insL - insR - 8) / 2);
    const float row = yesBtn->y - buttonH;
    Button* left = fresh_ ? again_ : goOn_;
    Button* right = fresh_ ? fresh_ : again_;
    left->x = insL;
    right->x = insL + half + 8;
    left->y = right->y = row;
    left->w = right->w = half;
    left->h = right->h = buttonH;
    if (fresh_) {
      goOn_->x = yesBtn->x;
      goOn_->y = yesBtn->y - buttonH - 0x23;
      goOn_->w = yesBtn->w;
      goOn_->h = buttonH;
    }
  }
  void onButton(int id) override {
    SessionKeeper& keeper = sessionsOf(app);
    if (id == 0 || id == 1) {
      killNow();
      keeper.finishContinue(id == 0);
    } else if (id == 2) {
      doScrollOff();
      app.doConfirmRestartAdventureDialog();
    } else {
      doScrollOff();
      keeper.backOut();
    }
  }

 private:
  Button* extra(int id, const std::string& label) {
    Button* b = addButton(id, Button::Kind::Patch, "IMAGE_DLG_BUTTON3", label);
    if (yesBtn) {
      b->textOffY = yesBtn->textOffY;
      b->labelStyle = yesBtn->labelStyle;
      b->labelFullGradient = yesBtn->labelFullGradient;
    }
    return b;
  }
  Button *goOn_ = nullptr, *again_ = nullptr, *fresh_ = nullptr;
};

}

bool SessionKeeper::askToContinue(bool ask) {
  PlayerInfo* p = app_.curProfile;
  const GameMode mode = app_.gameMode;
  if (!p || (mode != GameMode::Adventure && mode != GameMode::QuickPlay && mode != GameMode::Challenge && mode != GameMode::Duel)) return false;
  if (mode == GameMode::Adventure) {
    if (ask && app_.game == Game::Deluxe && p->advCompletions > 0) return false;
    app_.curStage = std::clamp(p->advStage, 0, std::max(0, app_.numStages() - 1));
    app_.curLevel = std::clamp(p->advLevel, 0, 5);
  }
  if (!find(mode)) return false;
  const SavedSession& s = ready_->saved;
  if (mode == GameMode::Adventure && (s.shots == 0 || !ask)) return resume();
  std::string text;
  if (mode == GameMode::Adventure) {
    text = "Do you want to ^00FF00^continue^oldclr^ the level in progress or ^00FF00^restart^oldclr^ the level?\n\n^44DAFB^Score: " + withCommas(s.score) +
           "\nBalls Left: " + std::to_string(s.ballsLeft) + "\n";
  } else if (mode == GameMode::Challenge) {
    const TrophyInfo* t = app_.trophies.byId(s.ch.trophy);
    text = "Do you want to ^00FF00^continue^oldclr^ the game in progress or start a ^00FF00^new game^oldclr^?\n\n^44DAFB^Challenge: " + (t ? t->name : std::string()) + "\n";
    if (t && t->levels.size() > 1) text += "Level: " + std::to_string(s.ch.step + 1) + "/" + std::to_string(t->levels.size()) + "\n";
    if (t && t->balls > 10) text += "Balls Left: " + std::to_string(s.ballsLeft) + "\n";
    if (s.cfg.duel) text += duelLines(s, p->name, (s.flags & 2) != 0);
    else text += "Score: " + withCommas(s.score) + "\n";
  } else if (mode == GameMode::Duel) {
    text = "Do you want to ^00FF00^continue^oldclr^ the game in progress or start a ^00FF00^new game^oldclr^?\n\n^44DAFB^" + duelLines(s, p->name, (s.flags & 2) != 0);
  } else {
    text = "Do you want to ^00FF00^continue^oldclr^ the game in progress or start a ^00FF00^new game^oldclr^?\n\n^44DAFB^";
    const LevelInfo* li = app_.stages.levelByName(s.level);
    text += "Level: " + (li ? li->title : s.level) + "\nScore: " + withCommas(s.score) + "\n";
  }
  app_.killDialog(D_ContinueGame);
  auto d = std::make_unique<ContinueDialog>(app_, text, mode == GameMode::Adventure && app_.game == Game::Nights && p->advCompletions > 0);
  ContinueDialog* raw = d.get();
  app_.positionDialog(raw, 0x177);
  raw->place();
  raw->doScroll(true);
  app_.addDialog(D_ContinueGame, std::move(d));
  return true;
}

void SessionKeeper::startAsUsual() {
  if (auto* menu = dynamic_cast<MainMenu*>(app_.screen())) {
    if (app_.gameMode == GameMode::None) app_.gameMode = GameMode::Adventure;
    menu->startGame();
  } else if (auto* adv = dynamic_cast<AdventureScreen*>(app_.screen())) {
    adv->startGame();
  } else {
    app_.enterAdventure();
  }
}

void SessionKeeper::backOut() {
  Screen* s = app_.screen();
  if (dynamic_cast<MainMenu*>(s) || dynamic_cast<AdventureScreen*>(s) || dynamic_cast<GameScreen*>(s)) return;
  app_.showMainMenu();
}

void SessionKeeper::finishContinue(bool goOn) {
  if (goOn && resume()) return;
  drop(app_.gameMode);
  if (!goOn && app_.game == Game::Nights && app_.curProfile) app_.curProfile->countFailedTry();
  startAsUsual();
}

bool SessionKeeper::askToLeave() {
  auto* gs = dynamic_cast<GameScreen*>(app_.screen());
  if (!gs || !gs->keepsSession() || file(app_.gameMode).empty()) return false;
  const PlayerInfo* p = app_.curProfile;
  if (!p || p->b58 >= 3 || gs->board().result.shots <= 0) {
    app_.finishOptionsDialog(true, false);
    app_.doToMenu();
    return true;
  }
  Dialog* d = app_.doDialog(D_ReturnToMenu, true, "Leave Game?", "Your game session will be saved upon leaving.  Do you want to continue?", "", BUTTONS_OK_CANCEL);
  d->setYesNo("Leave", "CANCEL");
  app_.alignQuestionWithMenu(d);
  return true;
}

int GameScreen::betweenLevels() const {
  if (app.gameMode != GameMode::Challenge || !done_) return 0;
  switch (pendingMode_) {
    case EndLevelDialog::ChallengeLevelDone: return 1;
    case EndLevelDialog::ChallengeLives: return 2;
    case EndLevelDialog::ChallengeWin: return app.challengeMore ? 3 : 0;
    default: return 0;
  }
}

bool GameScreen::keepsSession() const {
  if (!ready_ || viewer_ || demo_ || sessionOff_ || !recording_ || !app.curProfile) return false;
  if (app.gameMode != statsMode_ || board_.frames > SavedSession::kMaxFrames || log_.size() > SavedSession::kMaxEvents) return false;
  if (done_) return betweenLevels() != 0;
  if (board_.phase == Phase::Over) return false;
  switch (app.gameMode) {
    case GameMode::Adventure: return true;
    case GameMode::QuickPlay: return board_.score[0] > 0;
    case GameMode::Challenge: return app.challengeStep > 0 || board_.score[0] > 0 || (duel_ && board_.score[1] > 0);
    case GameMode::Duel: return board_.score[0] > 0 || board_.score[1] > 0;
    default: return false;
  }
}

bool GameScreen::fillSession(SavedSession& s) const {
  if (!keepsSession()) return false;
  s.game = app.game == Game::Nights ? 1 : 0;
  s.mode = (int)app.gameMode;
  s.level = levelFile_;
  s.content = lb_.contentHash;
  s.stage = hudStage_;
  s.levelNo = hudLevel_;
  s.shots = board_.result.shots;
  s.score = board_.score[0];
  s.ballsLeft = board_.ballsLeft[0];
  s.frames = board_.frames;
  s.tick = board_.tick;
  s.hash = board_.hash();
  s.cfg = cfg_;
  s.log = log_;
  s.flags = (charPaused_ ? 1 : 0) | (human2_ ? 2 : 0);
  if (duel_) {
    SavedDuel& d = s.duel;
    d.score2 = board_.score[1];
    d.skill = human2_ ? 0 : aiSkill();
    d.state = human2_ ? 0 : aiState_;
    d.wait = aiWait_;
    d.tick = aiTick_;
    d.think = aiThink_;
    d.chosen = floatBits(aiChosen_);
  }
  if (app.gameMode == GameMode::Challenge) {
    s.ch.trophy = app.curChallenge;
    s.ch.step = app.challengeStep;
    s.ch.score = app.challengeScore;
    s.ch.score2 = app.challengeScore2;
    s.ch.lives = app.challengeLives;
    s.ch.balls = app.challengeBalls;
    s.ch.more = app.challengeMore;
    s.ch.over = betweenLevels();
  }
  s.hist.rng = app.randState();
  s.hist.lastOpponent = app.lastOpponent;
  s.hist.lastRolled = app.lastRolled;
  s.hist.useCounter = app.stages.useCount();
  s.hist.use = app.stages.useMarks();
  return true;
}

void GameScreen::restoreAi(const ReadySession& r) {
  const SavedDuel& d = r.saved.duel;
  aiWait_ = d.wait;
  aiState_ = d.state;
  aiTick_ = d.tick;
  aiThink_ = d.think;
  aiFixed_ = d.skill;
  aiChosen_ = bitsFloat(d.chosen);
  if (d.state != 1) return;
  ai_ = std::make_unique<AIMgr>();
  aiBoard_ = std::make_unique<Board>(r.aiSnap);
  aiDrawn_ = 0;
  ai_->begin(*aiBoard_, d.skill, [this] { return aiDraw(); });
  for (int i = 0; i < d.think; i++) {
    if (ai_->update(10)) {
      aiChosen_ = ai_->angle();
      aiState_ = 2;
      aiTick_ = 0;
      break;
    }
  }
  aiBoard_.reset();
  aiDrawn_ = 0;
}

GameScreen::GameScreen(App& a, const ReadySession& r) : Screen(a) {
  seed = r.saved.cfg.seed;
  levelFile_ = r.saved.level;
  lb_ = r.bundle;
  cfg_ = r.saved.cfg;
  log_ = r.saved.log;
  board_ = r.board;
  snap_ = r.snap;
  haveSnap_ = r.haveSnap;
  recording_ = true;
  boardResets_ = 1;
  duel_ = r.saved.cfg.duel;
  human2_ = (r.saved.flags & 2) != 0;
  if (duel_) {
    charChosen_ = true;
    restoreAi(r);
  }
  if (r.saved.ch.over != 0) {
    done_ = endShown_ = true;
    endPending_ = true;
    pendingMode_ = r.saved.ch.over == 1 ? (int)EndLevelDialog::ChallengeLevelDone : r.saved.ch.over == 2 ? (int)EndLevelDialog::ChallengeLives : (int)EndLevelDialog::ChallengeWin;
    statsSent_ = true;
    pendingOut_ = collectOutcome();
    randAfter_ = r.saved.hist.rng;
  }
  hudStage_ = a.curStage;
  hudLevel_ = a.curLevel;
  statsMode_ = a.gameMode;
  if (a.game == Game::Nights) {
    a.mantelBefore = 0;
    for (int i = 0; i < 4; i++) a.mantelBefore |= MainMenu::mantelEarned(a, i) ? 1 << i : 0;
    a.newMantel = false;
  }
  if (a.audio) a.audio->dropQueued();
  mouseBx_ = (int)std::floor(a.mouseX) - 75;
  mouseBy_ = (int)std::floor(a.mouseY) - 40;
  haveMouse_ = true;
  ready_ = true;
  guide = true;
  if (!done_ && (mouseBx_ != board_.mouseX || mouseBy_ != board_.mouseY)) pushMouse();
  if (done_) {
    const LevelInfo* li = a.curLevelInfo();
    pendingTitle_ = li ? li->title : levelFile_;
  } else if (r.saved.flags & 1) {
    showCharacterDialog();
  }
  a.playMusic("level");
}

}
