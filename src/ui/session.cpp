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
constexpr uint32_t kVersion = 1;
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

bool configSane(const BoardConfig& c) {
  if (c.boardWidth < 650 || c.boardWidth > 1400) return false;
  if (c.players != 1 || c.duel) return false;
  if (c.balls < 0 || c.balls > 10000 || c.orange < 0 || c.orange > 10000) return false;
  if (c.startScore != 0 || c.startScore2 != 0) return false;
  for (int m : c.master)
    if (m < 0 || m >= masters::kPowerSlots) return false;
  if (c.masterIndex < -1 || c.masterIndex > 64) return false;
  if (c.tryNumber < 0 || c.tryNumber > 100000000) return false;
  if (c.ch.levelIndex < 0 || c.ch.levelCount < 0) return false;
  return true;
}

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
}

bool bodySane(const SavedSession& s) {
  if (s.game < 0 || s.game > 1) return false;
  if (s.mode != (int)GameMode::Adventure) return false;
  if (s.level.empty()) return false;
  if (s.frames < 0 || s.frames > SavedSession::kMaxFrames || s.tick < 0 || s.tick > s.frames) return false;
  if (s.shots < 0 || s.ballsLeft < 0) return false;
  if (!configSane(s.cfg)) return false;
  int last = 0;
  for (const ReplayEvent& e : s.log) {
    if (e.kind > 3) return false;
    if (e.frame < last || e.frame > s.frames) return false;
    if (e.kind == 2 && (e.a < -1000000 || e.a > 1000000)) return false;
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

bool SavedSession::rebuild(const LevelBundle& lb, Board& board, Board& snap, bool& haveSnap) const {
  if (lb.contentHash != content) return false;
  std::string err;
  Board b;
  if (!b.load(lb.base, lb.level, cfg, &err)) return false;
  haveSnap = false;
  size_t next = 0;
  for (;;) {
    while (next < log.size() && log[next].frame <= b.frames) {
      const ReplayEvent& e = log[next++];
      if (e.kind == 0) b.gunMouse(e.a, e.b);
      else if (e.kind == 1) {
        if (b.phase == Phase::Aim) { snap = b; haveSnap = true; }
        b.gunClick();
      } else if (e.kind == 3) b.gunRelease();
      else b.gunWheel(e.a);
    }
    if (b.frames >= frames) break;
    b.step();
    b.events.clear();
    if (b.phase == Phase::Over) return false;
  }
  if (next != log.size() || b.tick != tick || b.hash() != hash) return false;
  if (b.result.shots != shots || b.score[0] != score || b.ballsLeft[0] != ballsLeft) return false;
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
  if (m != GameMode::Adventure) return false;
  const std::filesystem::path path = file(m);
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec)) return false;
  auto r = std::make_unique<ReadySession>();
  const LevelInfo* li = app_.curLevelInfo();
  std::string err;
  const bool ok = SavedSession::load(path, r->saved) && r->saved.game == (app_.game == Game::Nights ? 1 : 0) && r->saved.mode == (int)m && li &&
                  li->file == r->saved.level && loadLevelBundle(app_.vfs, r->saved.level, r->bundle, &err) &&
                  r->saved.rebuild(r->bundle, r->board, r->snap, r->haveSnap);
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
  if (app_.curProfile) app_.applyColorblind(app_.curProfile->colorblind);
  app_.setScreen(std::make_unique<GameScreen>(app_, *r), App::Left);
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

class ContinueDialog : public Dialog {
 public:
  ContinueDialog(App& a, const std::string& text, bool newGame) : Dialog(a, D_ContinueGame, true, "Continue Game?", text, "Cancel", BUTTONS_FOOTER) {
    textAlign = -1;
    goOn_ = extra(0, "Continue");
    again_ = extra(1, "Restart Level");
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
  if (!p || app_.gameMode != GameMode::Adventure) return false;
  if (ask && app_.game == Game::Deluxe && p->advCompletions > 0) return false;
  app_.curStage = std::clamp(p->advStage, 0, std::max(0, app_.numStages() - 1));
  app_.curLevel = std::clamp(p->advLevel, 0, 5);
  if (!find(GameMode::Adventure)) return false;
  if (ready_->saved.shots == 0 || !ask) return resume();
  const std::string text = "Do you want to ^00FF00^continue^oldclr^ the level in progress or ^00FF00^restart^oldclr^ the level?\n\n^44DAFB^Score: " +
                           withCommas(ready_->saved.score) + "\nBalls Left: " + std::to_string(ready_->saved.ballsLeft) + "\n";
  app_.killDialog(D_ContinueGame);
  auto d = std::make_unique<ContinueDialog>(app_, text, app_.game == Game::Nights && p->advCompletions > 0);
  ContinueDialog* raw = d.get();
  app_.positionDialog(raw, 0x177);
  raw->place();
  raw->doScroll(true);
  app_.addDialog(D_ContinueGame, std::move(d));
  return true;
}

void SessionKeeper::startAsUsual() {
  if (auto* menu = dynamic_cast<MainMenu*>(app_.screen())) {
    app_.gameMode = GameMode::Adventure;
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
  drop(GameMode::Adventure);
  if (!goOn && app_.game == Game::Nights && app_.curProfile) app_.curProfile->countFailedTry();
  startAsUsual();
}

bool SessionKeeper::askToLeave() {
  auto* gs = dynamic_cast<GameScreen*>(app_.screen());
  if (!gs || !gs->keepsSession() || file(GameMode::Adventure).empty()) return false;
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

bool GameScreen::keepsSession() const {
  if (!ready_ || done_ || viewer_ || demo_ || duel_ || !recording_ || !app.curProfile) return false;
  if (app.gameMode != GameMode::Adventure || statsMode_ != GameMode::Adventure) return false;
  return board_.phase != Phase::Over && board_.frames <= SavedSession::kMaxFrames && log_.size() <= SavedSession::kMaxEvents;
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
  return true;
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
  if (mouseBx_ != board_.mouseX || mouseBy_ != board_.mouseY) pushMouse();
  a.playMusic("level");
}

}
