#include "ui/game_screen.h"
#include "ui/session.h"
#include "ui/screens.h"
#include "log.h"
#include <ctime>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>
#include <cctype>

#include "ui/character_dialog.h"
#include "ui/slot_dialog.h"
#include "gun.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pp::ui {

static int masterIndexByName(const std::string& name);
static const char* kMasterNames[] = {"bjorn", "jimmy", "tut", "splork", "claude", "renfield", "tula", "warren", "cinder", "hu", "marina"};

static int masterIndexByName(const std::string& name) {
  std::string c = name;
  for (auto& x : c) x = (char)std::tolower((unsigned char)x);
  for (int k = 0; k < 11; k++)
    if (c.find(kMasterNames[k]) != std::string::npos) return k;
  return -1;
}

static int modPowerFor(App& app, int slot) {
  if (!app.curProfile) return 0;
  const ModMaster* m = masters::byKey(app.profiles.modMaster(*app.curProfile, slot));
  return m ? m->power : 0;
}

GameScreen::GameScreen(App& a, bool fromAdventure, bool newLevel) : Screen(a) {
  (void)fromAdventure;
  (void)newLevel;
  seed = a.rand();
  std::string name;
  if (a.gameMode == GameMode::Challenge) {
    name = a.challengeLevel;
  } else if (const LevelInfo* li = a.curLevelInfo()) {
    name = li->file;
  }
  if (name.empty()) {
    loadError = "no level selected";
    return;
  }
  levelFile_ = name;
  mouseBx_ = (int)std::floor(a.mouseX) - 75;
  mouseBy_ = (int)std::floor(a.mouseY) - 40;
  haveMouse_ = true;
  countLevelStart();
  ready_ = startLevel(name);
  guide = true;
}

GameScreen::GameScreen(App& a, const ReplayData& d) : Screen(a) {
  seed = d.cfg.seed;
  ready_ = startViewer(d);
  guide = false;
}

GameScreen::~GameScreen() = default;

template <class C>
static auto setRuleInputs(C& c, const PlayerInfo* p, int adventureIndex, bool nights, int tries, int) -> decltype((void)c.tryNumber, void()) {
  c.maxLevel = p ? p->maxLevelIndex() : 99;
  c.curLevel = adventureIndex;
  c.coinFlipSeen = p ? p->tipSet(2) : true;
  c.nights = nights;
  c.tryNumber = tries;
}
template <class C>
static void setRuleInputs(C&, const PlayerInfo*, int, bool, int, long) {}

bool GameScreen::startLevel(const std::string& name) {
  submitStats();
  if (app.game == Game::Nights) {
    app.mantelBefore = 0;
    for (int i = 0; i < 4; i++) app.mantelBefore |= MainMenu::mantelEarned(app, i) ? 1 << i : 0;
    app.newMantel = false;
  }
  std::string err;
  LevelBundle lb;
  if (!loadLevelBundle(app.vfs, name, lb, &err)) {
    loadError = err;
    return false;
  }
  lb_ = lb;
  BoardConfig cfg = lb_.cfg;
  cfg.seed = seed;
  int stage = app.curStage;
  auto randomMaster = [&](int c) {
    if (c != CharacterDialog::randomChoice(app.game)) return c;
    return CharacterDialog::masterMetAt(app.game, (int)(app.rand() % (unsigned)CharacterDialog::mastersMet(app.game, app.curProfile)));
  };
  int mi = std::clamp(app.gameMode == GameMode::Adventure || app.gameMode == GameMode::QuickPlay ? CharacterDialog::masterMetAt(app.game, std::clamp(stage, 0, 10)) : (app.curProfile ? randomMaster(app.curProfile->char1) : 0), 0, 10);
  if (app.game == Game::Deluxe) mi = std::min(mi, 9);
  if (const StageInfo* ps = app.stages.stage(stage); ps && ps->pack) mi = std::clamp(app.curProfile ? app.curProfile->char1 : 0, 0, 10);
  if (app.curProfile && stage >= 10 + (app.game == Game::Nights ? 1 : 0)) mi = std::clamp(app.curProfile->char1, 0, app.game == Game::Nights ? 10 : 9);
  const bool ownChoice = choosesMaster();
  if (ownChoice) mi = chosenMaster();
  if (app.gameMode == GameMode::Demo) {
    static int lastDemoMaster = -1;
    const int met = CharacterDialog::mastersMet(app.game, app.curProfile);
    int pick = 0;
    for (int tries = met > 1 ? 50 : 1; tries > 0; tries--) {
      pick = CharacterDialog::masterMetAt(app.game, (int)(app.rand() % (unsigned)met));
      if (pick != lastDemoMaster) break;
    }
    lastDemoMaster = pick;
    mi = std::clamp(pick, 0, app.game == Game::Nights ? 10 : 9);
  }
  cfg.master[0] = Board::masterByName(kMasterNames[mi]);
  {
    const StageInfo* sp = app.stages.stage(stage);
    const bool char1Used = ownChoice || !(app.gameMode == GameMode::Adventure || app.gameMode == GameMode::QuickPlay) || (sp && sp->pack) ||
                           (app.curProfile && stage >= 10 + (app.game == Game::Nights ? 1 : 0));
    if (char1Used)
      if (const int mp = modPowerFor(app, 0)) cfg.master[0] = mp;
  }
  cfg.boardWidth = app.boardWidthForNewBoard();
  bool adv = app.gameMode == GameMode::Adventure;
  setRuleInputs(cfg, app.curProfile, adv ? 5 * app.curStage + app.curLevel : -1, app.game == Game::Nights,
                adv && app.curProfile ? app.curProfile->tryNumber(app.game == Game::Nights) : 1, 0);
  hudStage_ = app.curStage;
  hudLevel_ = app.curLevel;
  if (app.gameMode == GameMode::Challenge)
    if (const TrophyInfo* t = app.trophies.byId(app.curChallenge)) {
      if (t->balls > 0) cfg.balls = app.challengeStep == 0 ? t->balls : std::max(app.challengeBalls, 1);
      if (t->numOrange > 0) cfg.orange = t->numOrange;
      cfg.clearLevel = t->clearLevel;
      auto ng = t->raw.find("NoGreens");
      if (app.game == Game::Nights) {
        cfg.ch = ChallengeRules{};
        cfg.ch.active = true;
        for (const auto& [k, v] : t->raw) cfg.ch.set(k, v);
        if (auto bo = t->raw.find("BackgroundOverride"); bo != t->raw.end() && bo->second.size() > 7 && bo->second.compare(0, 7, "levels/") == 0)
          lb_.background = bo->second.substr(7);
        if (t->scoreReq > 0) cfg.ch.scoreReq = t->scoreReq;
        if (t->scoreCeil > 0) cfg.ch.scoreCeil = t->scoreCeil;
        cfg.ch.levelIndex = std::max(0, app.challengeStep);
        cfg.ch.levelCount = std::max<int>(1, (int)t->levels.size());
        if (app.challengeStep == 0 && !app.challengeKeepLives) { app.challengeLives = std::max(1, t->lives); app.challengeScore = 0; }
        if (!ownChoice || charChosen_) app.challengeKeepLives = false;
        if (!t->scoreReset && app.challengeStep > 0) cfg.startScore = app.challengeScore;
      } else {
        if (ng != t->raw.end() && ng->second.find("true") != std::string::npos) cfg.greens = false;
        if (app.challengeStep == 0) app.challengeScore = app.challengeScore2 = 0;
        else if (!t->scoreReset) { cfg.startScore = app.challengeScore; cfg.startScore2 = app.challengeScore2; }
      }
      auto ch = t->raw.find("Characters");
      if (ch != t->raw.end()) {
        int k = masterIndexByName(ch->second);
        if (k >= 0) cfg.master[0] = Board::masterByName(kMasterNames[k]);
      }
    }
  const TrophyInfo* ctr = app.gameMode == GameMode::Challenge ? app.trophies.byId(app.curChallenge) : nullptr;
  const bool duelChallenge = ctr && !ctr->opponents.empty();
  duel_ = app.gameMode == GameMode::Duel || duelChallenge;
  cfg.players = duel_ ? 2 : 1;
  cfg.duel = duel_;
  human2_ = duel_ && app.gameMode == GameMode::Duel && app.curProfile && !app.curProfile->computerPlayer;
  if (!duel_ && lb_.cfg.master[0] >= masters::kFirstPower && masters::byPower(lb_.cfg.master[0])) cfg.master[0] = lb_.cfg.master[0];
  if (duel_) {
    cfg.master[1] = Board::masterByName(kMasterNames[std::clamp(app.curProfile ? randomMaster(app.curProfile->char2) : 1, 0, app.game == Game::Nights ? 10 : 9)]);
    if (const int mp = modPowerFor(app, 1)) cfg.master[1] = mp;
  }
  demo_ = app.gameMode == GameMode::Demo;
  if (duelChallenge) {
    int oi = masterIndexByName(ctr->opponents[std::min<size_t>((size_t)std::max(0, app.challengeStep), ctr->opponents.size() - 1)]);
    if (oi < 0) {
      std::string low = ctr->opponents[std::min<size_t>((size_t)std::max(0, app.challengeStep), ctr->opponents.size() - 1)];
      for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
      if (low == "rand") {
        static int lastOpponent = -1;
        const int met = CharacterDialog::mastersMet(app.game, app.curProfile);
        int pick = 0;
        for (int tries = met > 1 ? 50 : 1; tries > 0; tries--) {
          pick = CharacterDialog::masterMetAt(app.game, (int)(app.rand() % (unsigned)met));
          if (pick != lastOpponent) break;
        }
        lastOpponent = pick;
        oi = std::clamp(pick, 0, app.game == Game::Nights ? 10 : 9);
      }
    }
    if (oi >= 0) cfg.master[1] = Board::masterByName(kMasterNames[oi]);
  }
  aiWait_ = 0;
  cfg.liveSpin = !demo_;
  if (app.audio) app.audio->dropQueued();
  spinHeld_.clear();
  board_ = Board();
  cfg_ = cfg;
  log_.clear();
  recording_ = !duel_ && !demo_ && !viewer_;
  haveSnap_ = false;
  if (!board_.load(lb_.base, lb_.level, cfg, &err)) {
    loadError = err;
    pp::log::error("game", "cannot start level " + name + ": " + err);
    return false;
  }
  done_ = endShown_ = false;
  statsSent_ = false;
  statsMode_ = app.gameMode;
  overTicks = 0;
  pauseCount_ = pauseFade_ = 0;
  focusPaused_ = false;
  rightDown_ = keyUp_ = keyDown_ = false;
  pushMouse();
  app.playMusic("level");
  stageDialogDelay_ = 15;
  if (!charChosen_ && ((duel_ && app.gameMode == GameMode::Duel) || ownChoice)) showCharacterDialog();
  return true;
}

bool GameScreen::choosesMaster() const {
  if (!app.curProfile || viewer_) return false;
  if (app.gameMode == GameMode::QuickPlay) return true;
  if (app.gameMode == GameMode::Adventure) return app.curProfile->maxStage >= (app.game == Game::Nights ? 11 : 10);
  if (app.gameMode == GameMode::Challenge) {
    const TrophyInfo* t = app.trophies.byId(app.curChallenge);
    return t && t->opponents.empty() && t->raw.find("Characters") == t->raw.end();
  }
  return false;
}

int GameScreen::chosenMaster() {
  static int lastRolled = -1;
  const int c = app.curProfile->char1;
  const bool keep = keepRoll_ && rolled_ >= 0 && rolled_ < 10;
  keepRoll_ = false;
  if (c != CharacterDialog::randomChoice(app.game)) return std::clamp(c, 0, app.game == Game::Nights ? 10 : 9);
  if (keep) return rolled_;
  const int met = CharacterDialog::mastersMet(app.game, app.curProfile);
  for (int tries = met > 1 ? 50 : 1; tries > 0; tries--) {
    rolled_ = CharacterDialog::masterMetAt(app.game, (int)(app.rand() % (unsigned)met));
    if (rolled_ != lastRolled) break;
  }
  lastRolled = rolled_;
  return rolled_;
}

void GameScreen::masterChosen(bool picked) {
  keepRoll_ = !picked;
}

void GameScreen::showStageDialog() {
  if (app.gameMode != GameMode::Adventure || !app.curProfile || boardResets_ >= 1 || choosesMaster()) return;
  const LevelInfo* li = app.curLevelInfo();
  if (!li || li->dialog.empty()) return;
  const std::string& t = li->dialog;
  int img = 0;
  if (t.compare(0, 2, "\n\n") == 0) {
    if (t.find("ORANGE PEGS^oldclr^ to WIN") != std::string::npos) img = 1;
    else if (t.find("BRICKS") != std::string::npos) img = 2;
    else if (t.find("Green pegs") != std::string::npos) img = 3;
    else if (t.find("PURPLE PEG") != std::string::npos) img = 4;
  }
  Dialog* d = app.doDialog(D_Tip, true, li->dialogTitle, t, "OK", BUTTONS_FOOTER);
  d->scrollTarget = 100;
  if (img > 0)
    d->extraDraw = [img](Dialog& dl, Gfx& g) {
      std::string id = img == 1 ? "IMAGE_DLG_INSTRUCTIONS" : "IMAGE_DLG_INSTRUCTIONS" + std::to_string(img);
      if (Tex* c = g.img(id)) g.draw(c, (dl.w - c->lw()) / 2, dl.contentTop(g));
    };
  pause(true);
  tipPaused_ = true;
}

void GameScreen::startChallengeLevel() {
  levelFile_ = app.challengeLevel;
  seed = app.rand();
  done_ = false;
  ready_ = startLevel(levelFile_);
}

void GameScreen::countLevelStart() {
  if (app.game == Game::Deluxe && app.gameMode == GameMode::Adventure && app.curProfile) app.curProfile->countLevelStart();
}

void GameScreen::restartLevel(bool fromMenu) {
  boardResets_++;
  if (app.game == Game::Nights && fromMenu && app.gameMode == GameMode::Adventure && app.curProfile) app.curProfile->countFailedTry();
  countLevelStart();
  seed = app.rand();
  ready_ = startLevel(levelFile_);
}

bool GameScreen::hasProgress() const {
  if (!ready_ || done_ || viewer_ || demo_ || !app.curProfile) return false;
  switch (app.gameMode) {
    case GameMode::Adventure: return board_.result.shots > 0;
    case GameMode::QuickPlay: return board_.score[0] > 0;
    case GameMode::Duel: return board_.score[0] > 0 || board_.score[1] > 0;
    case GameMode::Challenge: return app.challengeStep > 0 || board_.score[0] > 0;
    default: return false;
  }
}

void GameScreen::showCharacterDialog() {
  auto d = std::make_unique<CharacterDialog>(app, duel_);
  CharacterDialog* raw = d.get();
  raw->place();
  raw->doScroll(true);
  app.addDialog(D_Character, std::move(d));
  pause(true);
  charPaused_ = true;
}

void GameScreen::recMouse(int x, int y) {
  board_.gunMouse(x, y);
  if (!recording_) return;
  if (!log_.empty() && log_.back().kind == 0 && log_.back().a == x && log_.back().b == y) return;
  if (!log_.empty() && log_.back().kind == 0 && log_.back().tick == board_.tick) { log_.back().a = x; log_.back().b = y; return; }
  log_.push_back({board_.tick, 0, x, y, board_.frames});
}
void GameScreen::recClick() {
  board_.gunClick();
  if (recording_) log_.push_back({board_.tick, 1, 0, 0, board_.frames});
}
void GameScreen::recRelease() {
  board_.gunRelease();
  if (recording_) log_.push_back({board_.tick, 3, 0, 0, board_.frames});
}
void GameScreen::recWheel(int n) {
  board_.gunWheel(n);
  if (recording_ && n != 0) log_.push_back({board_.tick, 2, n, 0, board_.frames});
}

void GameScreen::clickBoard() {
  if (board_.phase == Phase::Aim && !replaying_) { snap_ = board_; haveSnap_ = true; }
  recClick();
}
void GameScreen::startReplay(bool all) {
  replayAll_ = false;
  if (all && canSaveReplay()) {
    std::string err;
    Board b;
    if (b.load(lb_.base, lb_.level, cfg_, &err)) {
      replay_ = b;
      view_.log = log_;
      viewIdx_ = 0;
      replayAll_ = true;
      replaying_ = true;
      replayRan_ = false;
      replayTicks_ = 0;
      return;
    }
  }
  replay_ = snap_;
  replay_.gunClick();
  replaying_ = true;
  replayRan_ = false;
  replayTicks_ = 0;
}
void GameScreen::stopReplay() { replaying_ = false; }
void GameScreen::applyViewEvents() {
  while (viewIdx_ < view_.log.size() && view_.log[viewIdx_].tick <= replay_.tick) {
    const ReplayEvent& e = view_.log[viewIdx_++];
    if (e.kind == 0) replay_.gunMouse(e.a, e.b);
    else if (e.kind == 1) { replay_.gunClick(); replayRan_ = false; }
    else if (e.kind == 3) replay_.gunRelease();
    else replay_.gunWheel(e.a);
  }
}
void GameScreen::stepReplay() {
  int n = rightDown_ ? 8 : 1;
  for (int k = 0; k < n && replaying_; k++) {
    if (viewer_ || replayAll_) applyViewEvents();
    replay_.step();
    replayTicks_++;
    if (replay_.phase != Phase::Aim) replayRan_ = true;
    bool logDone = !(viewer_ || replayAll_) || viewIdx_ >= view_.log.size();
    if ((logDone && replayRan_ && replay_.phase == Phase::Aim) || replay_.phase == Phase::Over || replayTicks_ > (viewer_ || replayAll_ ? 4000000 : 8000)) {
      stopReplay();
      replayFinished();
    }
  }
  if (app.audio) app.audio->onEvents(replay_.events, replay_);
  replay_.events.clear();
}

void GameScreen::replayFinished() {
  if (viewer_) {
    Dialog* d = app.doDialog(D_Replay1, true, "Replay Finished!", "Hit ^FF44FF^Done^oldclr^ to return to the menu.", "Done", BUTTONS_FOOTER);
    (void)d;
    return;
  }
  Dialog* d = app.doDialog(D_Replay1, true, "Instant Replay Finished!", "Hit ^FF44FF^Done Button^oldclr^ to Return to Game.\nHit ^00FF00^Instant Replay Button^oldclr^ to Watch Again.", "", BUTTONS_YES_NO);
  d->setYesNo("Done", canSaveReplay() ? "Save Replay" : "Close");
  pause(true);
  replayDlgPaused_ = true;
}

bool GameScreen::saveReplay(bool all, std::string* pathOut) {
  if (!canSaveReplay()) return false;
  ReplayData d;
  d.game = app.game == Game::Nights ? 1 : 0;
  d.level = levelFile_;
  { const LevelInfo* li = app.curLevelInfo(); d.title = li && !li->title.empty() ? li->title : lb_.title; }
  d.player = app.curProfile ? app.curProfile->name : "Player";
  d.when = (int64_t)std::time(nullptr);
  d.score = board_.score[0];
  d.shots = board_.shotIndex;
  d.startTick = all ? 0 : snap_.tick;
  d.cfg = cfg_;
  d.log = log_;
  char stamp[32];
  std::time_t t = (std::time_t)d.when;
  std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&t));
  std::string path = (app.userDir / "replays" / (levelFile_ + "_" + stamp + replayExt(d.game))).string();
  if (!d.save(path)) return false;
  if (pathOut) *pathOut = path;
  return true;
}

bool GameScreen::startViewer(const ReplayData& d) {
  std::string err;
  LevelBundle lb;
  if (!loadLevelBundle(app.vfs, d.level, lb, &err)) { loadError = err; return false; }
  lb_ = lb;
  levelFile_ = d.level;
  view_ = d;
  viewer_ = true;
  recording_ = false;
  BoardConfig rcfg = d.cfg;
  rcfg.liveSpin = true;
  cfg_ = rcfg;
  if (app.audio) app.audio->dropQueued();
  spinHeld_.clear();
  board_ = Board();
  if (!board_.load(lb_.base, lb_.level, rcfg, &err)) { loadError = err; return false; }
  replay_ = board_;
  viewIdx_ = 0;
  while (replay_.tick < d.startTick && replay_.phase != Phase::Over) {
    applyViewEvents();
    replay_.step();
    replay_.events.clear();
  }
  replaying_ = true;
  replayRan_ = false;
  replayTicks_ = 0;
  ready_ = true;
  hudStage_ = 0;
  hudLevel_ = 0;
  app.playMusic("level");
  return true;
}

int GameScreen::aiSkill() const {
  auto level = [](int s) { return s == 2 ? 10 : s == 1 ? 1 : s == -1 ? -1 : 0; };
  if (demo_) return 10;
  if (app.gameMode == GameMode::Challenge)
    if (const TrophyInfo* t = app.trophies.byId(app.curChallenge))
      if (!t->opponentDifficulty.empty())
        return level(t->opponentDifficulty[std::min<size_t>((size_t)std::max(0, app.challengeStep), t->opponentDifficulty.size() - 1)]);
  return level(app.curProfile ? app.curProfile->computerSkill : -1);
}

void GameScreen::aiTurn() {
  Board& b = board_;
  aiWait_++;
  if (!ai_) ai_ = std::make_unique<AIMgr>();
  if (aiState_ == 0) {
    if (aiWait_ >= (b.shotIndex > 0 ? 150 : 100)) {
      ai_->begin(b, aiSkill(), [&b] { return (uint32_t)b.rng.rand(); });
      aiState_ = 1;
      aiTick_ = 0;
    }
  } else if (aiState_ == 1) {
    if (ai_->update(10)) {
      aiState_ = 2;
      aiTick_ = 0;
    }
  } else {
    aiTick_++;
    if (aiTick_ >= 20 && ai_->angle() != b.gunAngle) {
      float want = ai_->angle(), cur = b.gunAngle;
      if (want < 1.5707964f) want = want + 6.2831855f;
      if (cur < 1.5707964f) cur = cur + 6.2831855f;
      float rate = 0.1f * std::fabs(want - cur);
      rate = rate < 0.02f ? 0.02f : rate > 0.4f ? 0.4f : rate;
      if (want <= cur) {
        cur = cur - rate;
        if (cur <= want) cur = gun::normalizeAngle(want);
      } else {
        cur = rate + cur;
        if (want <= cur) cur = gun::normalizeAngle(want);
      }
      gun::setAngle(b.gunState, b.gunAngle, b.gunTarget, cur, true);
      b.lmAim = cur;
    }
    if (aiTick_ >= 100) {
      b.lmAim = ai_->angle();
      gun::setAngle(b.gunState, b.gunAngle, b.gunTarget, b.lmAim, true);
      b.fireHeld = true;
      b.fireCooldown = 0;
      aiState_ = 0;
      aiWait_ = 0;
    }
  }
}

void GameScreen::pushMouse() {
  if (ready_ && haveMouse_ && humanTurn() && !viewer_) recMouse(mouseBx_, mouseBy_);
}

void GameScreen::pause(bool on) {
  if (on) {
    pauseCount_++;
  } else if (pauseCount_ > 0) {
    pauseCount_--;
    if (pauseCount_ == 0) {
      focusPaused_ = false;
      keyUp_ = keyDown_ = rightDown_ = false;
      pushMouse();
    }
  }
}

void GameScreen::lostFocus() {
  if (!ready_ || done_ || focusPaused_) return;
  focusPaused_ = true;
  pause(true);
}

int GameScreen::stepsThisTick() const {
  if (!rightDown_) return 1;
  if (board_.lstate == 2) return rightClickSpeedup ? 4 : 1;
  return 8;
}

std::string GameScreen::cornerText() const {
  if (rightDown_ && board_.lstate != 2) return "Fast Forward";
  const Board& b = board_;
  int c = b.cur;
  if (b.turns[c] > 0 && b.master[c] == Board::P_ZEN) return "ZenBall";
  if (b.turns[c] > 0 && b.master[c] == Board::P_FIREBALL) return "Fireball";
  return "";
}

LevelOutcome GameScreen::collectOutcome() const {
  const Board& b = board_;
  const LevelResult& r = b.result;
  LevelOutcome o;
  o.finished = b.phase == Phase::Over;
  o.won = r.finished ? r.won : b.won;
  o.cleared = r.cleared;
  o.totalScore = r.finished ? r.totalScore : b.score[0];
  o.shots = std::max(r.shots, b.shotIndex);
  o.pegsHit = r.pegsHit;
  o.bestShot = r.bestShot;
  o.shotPoints = r.shotPoints;
  o.feverPoints = r.feverPoints;
  o.stylePoints = r.stylePoints;
  o.freeBalls = r.freeBallTotal();
  o.ballsLeft = r.finished ? r.ballsLeft : b.ballsLeft[0];
  o.orangeTotal = r.orangeTotal ? r.orangeTotal : b.orangeTotal;
  o.orangeHit = r.orangeTotal ? r.orangeHit : b.orangeTotal - b.orangeLeft;
  o.pegsTotal = r.pegsTotal;
  o.pegsCleared = r.pegsCleared;
  if (o.pegsTotal == 0) {
    for (const Obj& ob : b.objs)
      if (ob.pegType != 0) {
        o.pegsTotal++;
        if (ob.gone || ob.lit) o.pegsCleared++;
      }
  }
  if (!o.pegsHit) o.pegsHit = o.pegsCleared;
  o.ticks = r.ticks ? r.ticks : b.tick;
  for (int i = 0; i < 24; i++) o.styleCount[i] = r.styleCount[i];
  return o;
}

void GameScreen::update() {
  Panel::update();
  if (!ready_) return;
  Board& b = board_;
  bool dialogs = app.anyDialog();
  if (pauseFade_ != 0 || pauseCount_ != 0) {
    if (!dialogs) {
      if (pauseCount_ == 0) { if (pauseFade_ > 0) pauseFade_--; }
      else if (pauseFade_ < 10) pauseFade_++;
    } else if (pauseFade_ != 0) {
      pauseFade_ = 0;
    }
  }
  if (stageDialogDelay_ > 0 && --stageDialogDelay_ == 0) showStageDialog();
  if (replayDlgPaused_ && !app.getDialog(D_Replay1) && !app.getDialog(D_SaveAll)) { replayDlgPaused_ = false; pause(false); }
  if (tipPaused_ && !app.getDialog(D_Tip)) { tipPaused_ = false; pause(false); }
  if (charPaused_ && !app.getDialog(D_Character)) {
    charPaused_ = false;
    charChosen_ = true;
    pause(false);
    ready_ = startLevel(levelFile_);
    charChosen_ = duel_;
    return;
  }
  if (pauseCount_ > 0) return;
  if (!app.windowFocused) return;
  if (!spinHeld_.empty() && !app.getDialog(D_Slot)) {
    if (app.audio) app.audio->onEvents(spinHeld_, b);
    spinHeld_.clear();
  }
  if (app.getDialog(D_Slot)) return;
  if (replaying_) { stepReplay(); return; }
  int n = stepsThisTick();
  if (((duel_ && b.cur == 1 && !human2_) || demo_) && b.phase == Phase::Aim) aiTurn();
  else { aiWait_ = 0; aiState_ = 0; }
  int spinOutcome = -1, spinPower = 0, spinCode = 0;
  for (int k = 0; k < n; k++) {
    if (keyUp_) recWheel(-5);
    if (keyDown_) recWheel(5);
    b.step();
    if (app.audio) app.audio->tick();
    for (const auto& e : b.events)
      if (e.type == Event::LuckySpin) { spinOutcome = e.a; spinPower = e.b; spinCode = e.c; }
      else if (e.type == Event::Fever && app.gameMode == GameMode::Duel && duel_) app.duelSes.fevers[std::clamp(b.cur, 0, 1)]++;
    if (spinOutcome >= 0) break;
    if (b.phase == Phase::Over) {
      if (!done_) finishLevel();
      overTicks++;
      if (b.result.cleared && !demo_ && overTicks < 100) overTicks = 0;
      if (demo_ && overTicks > 100 && !endShown_) { endShown_ = true; app.doToMenu(); return; }
      if (!endShown_ && overTicks > 100) {
        endShown_ = true;
        app.showEndLevel(pendingMode_, pendingOut_, app.scoreKey(levelFile_, board_.cfg.boardWidth), pendingTitle_, pendingRank_, pendingBest_);
      }
    }
  }
  if (app.audio && spinOutcome >= 0) {
    std::vector<Event> heard;
    bool afterSpin = false;
    for (const auto& e : b.events) {
      if (e.type == Event::LuckySpin) afterSpin = true;
      if (e.type == Event::Power && e.a >= 200 && e.a <= 203) continue;
      if (e.type == Event::Power && afterSpin && e.a == spinPower) {
        spinHeld_.push_back(e);
        continue;
      }
      heard.push_back(e);
    }
    app.audio->onEvents(heard, b);
  } else if (app.audio) {
    app.audio->onEvents(b.events, b);
  }
  if (app.audio && app.boardRenderer) app.audio->playFx(app.boardRenderer->takeSounds());
  for (const auto& e : b.events)
    if (e.type == Event::CoinFlip && app.curProfile && !app.curProfile->tipSet(2)) app.curProfile->setTip(2);
  b.events.clear();
  if (spinOutcome >= 0) app.addDialog(D_Slot, std::make_unique<SlotMachineDialog>(app, spinOutcome, spinPower, spinCode));
}

void GameScreen::finishLevel() {
  done_ = true;
  LevelOutcome o = collectOutcome();
  bool won = o.won;
  if (app.gameMode == GameMode::Challenge)
    if (const TrophyInfo* t = app.trophies.byId(app.curChallenge))
      if (!cfg_.ch.active && t->scoreReq > 0 && app.challengeStep + 1 >= (int)t->levels.size()) { won = o.totalScore > t->scoreReq; o.won = won; }
  if (app.game == Game::Nights && app.gameMode == GameMode::Challenge && cfg_.ch.active && !won) {
    const ChallengeRules& c = cfg_.ch;
    const LevelResult& r = board_.result;
    auto num = [](int v) { std::string s = std::to_string(v), t; for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) t += ','; t += s[i]; } return t; };
    const int score = o.totalScore;
    std::string why;
    if (c.scoreReq > 0 && score <= c.scoreReq) why = "You didn't get " + num(c.scoreReq) + " points.\n\n";
    else if (c.scoreCeil > 0 && score >= c.scoreCeil) why = "You didn't stay under " + num(c.scoreCeil) + " points.\n\n";
    else if (cfg_.clearLevel) why = "You didn't clear all of the pegs on the level.\n\n";
    else if (const TrophyInfo* lt = app.trophies.byId(app.curChallenge); lt && lt->lives >= 2) why = "You ran out of lives.\n\n";
    else if (c.styleScoreReq > 0 && r.stylePoints < c.styleScoreReq) why = "You didn't get " + num(c.styleScoreReq) + " style points.\n\n";
    else if (!c.noForceWin && !board_.boardClear()) why = "You didn't clear all of the orange pegs on the level.\n\n";
    app.endBodyOverride = why;
  }
  if (duel_) {
    const Board& b = board_;
    won = b.score[0] > b.score[1];
    o.won = won;
    o.totalScore = b.score[0];
    std::string me = app.curProfile ? app.curProfile->name : "Player 1";
    app.endHeaderOverride = b.score[0] == b.score[1] ? "It's a Tie!" : (won ? me : oppName()) + " Wins!";
    if (app.gameMode == GameMode::Duel) {
      auto& d = app.duelSes;
      int w = b.score[1] < b.score[0] ? 0 : b.score[0] < b.score[1] ? 1 : -1;
      if (w >= 0) d.wins[w]++;
      if (w == d.streakOwner) d.streak++;
      else { d.streakOwner = w; d.streak = 1; }
      if (d.points[0] > 999999999 || d.points[1] > 999999999) d.points[0] = d.points[1] = 0;
      d.points[0] += b.score[0];
      d.points[1] += b.score[1];
      app.duelEnd.active = true;
      app.duelEnd.score[0] = b.score[0];
      app.duelEnd.score[1] = b.score[1];
      app.duelEnd.name[0] = me;
      app.duelEnd.name[1] = oppName();
    }
  }
  applyProgress(o, won);
}

void GameScreen::submitStats() {
  PlayerInfo* p = app.curProfile;
  const LevelResult& r = board_.result;
  if (statsSent_ || !p || duel_ || demo_ || viewer_ || r.shots <= 0) return;
  if (statsMode_ != GameMode::Adventure && statsMode_ != GameMode::QuickPlay && statsMode_ != GameMode::Challenge) return;
  statsSent_ = true;
  GameStats s;
  s.shots = r.shots;
  s.bestShot = r.bestShot;
  s.bestFever = r.bestFever;
  for (int i = 0; i < 4; i++) s.freeBalls[i] = r.freeBalls[i];
  for (int i = 0; i < 24; i++) s.styleShots[i] = r.styleCount[i];
  p->addTotalStats(s, app.game == Game::Deluxe || statsMode_ == GameMode::Adventure);
}

void GameScreen::onLeave() {
  PlayerInfo* p = app.curProfile;
  if (sessionsOf(app).keep(*this)) {
    app.saveProfile();
    return;
  }
  if (p && ready_ && statsMode_ == GameMode::Adventure && !done_ && !viewer_ && !demo_) {
    const bool played = board_.result.shots > 0;
    if (app.game == Game::Deluxe && !played) p->uncountLevelStart();
    else if (app.game == Game::Nights && played) p->countFailedTry();
  }
  submitStats();
  app.saveProfile();
}

static int tryBonus(int tryNumber) {
  return tryNumber == 1 ? 100000 : tryNumber == 2 ? 50000 : tryNumber == 3 ? 25000 : 0;
}

void GameScreen::applyProgress(const LevelOutcome& oIn, bool won) {
  LevelOutcome o = oIn;
  PlayerInfo* p = app.curProfile;
  int rank = 0;
  bool newBest = false;
  EndLevelDialog::Mode mode = EndLevelDialog::QuickLose;
  const LevelInfo* li = app.curLevelInfo();
  std::string title = li ? li->title : levelFile_;
  if (app.game == Game::Nights && li) o.aceScore = li->ace;
  submitStats();
  if (app.gameMode == GameMode::Adventure) {
    mode = won ? EndLevelDialog::AdventureWin : EndLevelDialog::AdventureLose;
    if (won && p) {
      rank = app.highScores.submit(app.scoreKey(levelFile_, board_.cfg.boardWidth), p->name, o.totalScore);
      o.prevTotal = p->advScore;
      o.tryNumber = p->tryNumber(app.game == Game::Nights);
      o.tryBonus = tryBonus(o.tryNumber);
      p->advScore += o.totalScore + o.tryBonus;
      if (o.cleared) p->clearedLevels.insert(levelFile_);
      if (app.game == Game::Nights && li && li->ace > 0 && o.totalScore >= li->ace) p->acedLevels.insert(levelFile_);
      if (o.cleared) for (auto* t : app.trophies.forClearedLevel(levelFile_)) p->awardTrophy(t->id);
      app.incLevel(1);
      if (p->justBeatAdventure()) {
        o.effBonus = p->efficiencyBonus();
        p->advScore += o.effBonus;
        app.highScores.submit("_adventure", p->name, p->advScore);
        p->submitAdventureScore(p->advScore);
        mode = EndLevelDialog::AdventureComplete;
        if (app.boardRenderer) app.boardRenderer->masterBadge();
      }
    } else if (!won && p && app.game == Game::Nights) {
      p->countFailedTry();
    }
  } else {
    mode = won ? (app.gameMode == GameMode::Challenge ? EndLevelDialog::ChallengeWin : EndLevelDialog::QuickWin)
               : (app.gameMode == GameMode::Challenge ? EndLevelDialog::ChallengeLose : EndLevelDialog::QuickLose);
    if (app.gameMode == GameMode::Challenge) {
      const TrophyInfo* ct = app.trophies.byId(app.curChallenge);
      const bool moreLevels = ct && app.challengeStep + 1 < (int)ct->levels.size();
      app.challengeMore = won && moreLevels;
      app.challengeBalls = o.ballsLeft;
      if (app.challengeMore && !ct->scoreReset && !cfg_.ch.active && !duel_) app.challengeScore = o.totalScore;
      if (app.game == Game::Nights && ct && cfg_.ch.active && !duel_) {
        if (!ct->scoreReset && moreLevels) {
          mode = EndLevelDialog::ChallengeLevelDone;
          app.challengeMore = true;
          app.challengeScore = o.totalScore;
        } else if (!won) {
          if (--app.challengeLives >= 1) mode = EndLevelDialog::ChallengeLives;
        }
        if (mode != EndLevelDialog::ChallengeLose) app.endBodyOverride.clear();
      }
      if (app.game == Game::Deluxe && ct && !ct->scoreReset && moreLevels) {
        mode = EndLevelDialog::ChallengeLevelDone;
        app.challengeMore = true;
        app.challengeScore = o.totalScore;
        app.challengeScore2 = duel_ ? board_.score[1] : 0;
        app.endHeaderOverride.clear();
      }
      if (p && won && ct && !app.challengeMore) {
        p->awardTrophy(ct->id);
        app.newTrophies.insert(ct->id);
      }
    }
    if (p && won && !duel_) {
      if (app.gameMode != GameMode::Challenge) rank = app.highScores.submit(app.scoreKey(levelFile_, board_.cfg.boardWidth), p->name, o.totalScore);
      if (app.game == Game::Nights && app.gameMode != GameMode::Challenge && li && li->ace > 0 && o.totalScore >= li->ace) p->acedLevels.insert(levelFile_);
    }
    if (p && o.cleared && !duel_ && (app.game == Game::Deluxe || (won && app.gameMode != GameMode::Challenge))) {
      p->clearedLevels.insert(levelFile_);
      for (auto* t : app.trophies.forClearedLevel(levelFile_)) p->awardTrophy(t->id);
    }
  }
  newBest = rank == 1;
  const bool aced = app.game == Game::Nights && won && !duel_ && (app.gameMode == GameMode::Adventure || app.gameMode == GameMode::QuickPlay) && li && li->ace > 0 && o.totalScore >= li->ace;
  if (app.audio && !o.cleared && (mode == EndLevelDialog::AdventureComplete || aced)) {
    app.audio->playAfter(130, "TING");
    app.audio->playAfter(160, "APPLAUSE");
  }
  if (app.game == Game::Nights && p && won) {
    int after = 0;
    for (int i = 0; i < 4; i++) after |= MainMenu::mantelEarned(app, i) ? 1 << i : 0;
    app.newMantel = (after & ~app.mantelBefore) != 0 && mode != EndLevelDialog::AdventureComplete;
    app.mantelBefore = after;
  }
  if (p) p->dirty = true;
  app.saveAll();
  pendingMode_ = (int)mode;
  pendingRank_ = rank;
  pendingBest_ = newBest;
  pendingOut_ = o;
  pendingTitle_ = title;
}

void GameScreen::drawLayer(Gfx& g) {
  if (!ready_) {
    g.fill(0, 0, 800, 600, 0x101030);
    g.drawString("PLAIN", 400, 300, "Level could not be loaded: " + loadError, 0xff8080, 1, Align::Center);
    return;
  }
  if (app.boardRenderer) {
    hud_ = Hud();
    hud_.title = lb_.title;
    hud_.showGuide = guide;
    switch (app.gameMode) {
      case GameMode::Adventure:
        hud_.mode = Hud::ModeStage;
        hud_.stageNo = hudStage_;
        hud_.levelNo = hudLevel_;
        hud_.totalScore = app.curProfile ? app.curProfile->advScore : 0;
        break;
      case GameMode::Challenge: {
        hud_.mode = Hud::ModeChallenge;
        const TrophyInfo* t = app.trophies.byId(app.curChallenge);
        if (t) {
          hud_.challengeText = t->levels.empty() ? "Challenge" : "Chal. " + std::to_string(app.challengeStep + 1) + "/" + std::to_string(t->levels.size());
          hud_.challengeRight = t->name;
          if (app.game == Game::Nights && cfg_.ch.active) {
            const ChallengeRules& c = cfg_.ch;
            int need = 0, left = 0, unique = 0;
            for (int i = 0; i < 24; i++) {
              need += c.styleShots[i];
              left += board_.styleReq[i];
              unique += board_.result.styleCount[i] > 0;
            }
            if (need >= 1) hud_.challengeText = "Chal. " + std::to_string(need - left) + "/" + std::to_string(need);
            else if (c.uniqueStyleReq >= 1) hud_.challengeText = "Chal. " + std::to_string(unique) + "/" + std::to_string(c.uniqueStyleReq);
            else if (c.styleScoreReq >= 1) hud_.challengeText = "Chal. " + std::to_string(board_.result.stylePoints / 1000) + "K/" + std::to_string(c.styleScoreReq / 1000) + "K";
            else hud_.challengeText = c.levelCount == 1 ? "Challenge" : "Chal. " + std::to_string(c.levelIndex + 1) + "/" + std::to_string(c.levelCount);
            hud_.challengeRight = t->lives >= 2 ? "Lives: " + std::to_string(app.game == Game::Nights ? app.challengeLives : t->lives) : "";
          }
        }
        break;
      }
      default: {
        hud_.mode = app.gameMode == GameMode::Demo ? Hud::ModeDemo : duel_ ? Hud::ModeDuel : Hud::ModeQuick;
    if (duel_) {
      std::string me = app.curProfile ? app.curProfile->name : "Player 1";
      hud_.challengeText = board_.cur == 0 ? me : oppName();
      hud_.topName = board_.cur == 0 ? oppName() : me;
      hud_.topScore = board_.score[1 - board_.cur] + 0;
    }
        const auto& hs = app.highScores.scores(app.scoreKey(levelFile_, board_.cfg.boardWidth));
        if (!hs.empty() && !duel_) { hud_.topScore = hs[0].score; hud_.topName = hs[0].name; }
        break;
      }
    }
    if (duel_) {
      std::string me = app.curProfile ? app.curProfile->name : "Player 1";
      hud_.mode = Hud::ModeDuel;
      hud_.challengeText = board_.cur == 0 ? me : oppName();
      hud_.topName = board_.cur == 0 ? oppName() : me;
      hud_.topScore = board_.score[1 - board_.cur];
    }
    hud_.clearedBefore = app.curProfile && !levelFile_.empty() && app.curProfile->clearedLevels.count(levelFile_) > 0 && !(board_.cfg.duel && board_.players > 1);
    hud_.menuOver = mouseOverMenu_ && pauseCount_ == 0;
    hud_.replayOver = mouseOverReplay_ && pauseCount_ == 0;
    app.boardRenderer->setVisualOptions(app.settings.vis);
    hud_.alpha = pauseCount_ == 0 && !app.anyDialog() ? app.renderAlpha : 1.0f;
    app.boardRenderer->draw(replaying_ || viewer_ ? replay_ : board_, lb_, hud_);
  }
}

void GameScreen::drawOverlay(Gfx& g) {
  if (!ready_) return;
  float dw = (float)(canvasWidth() - 800);
  std::string corner = replaying_ ? std::string("Instant Replay") : cornerText();
  if (!corner.empty()) g.drawString("OVERLOAD16OUTLINE", 588 + dw / 2, 28, corner, 0xffffff, 1, Align::Center);
  if (board_.phase == Phase::Over && board_.result.cleared && !demo_ && overTicks < 100 && !endShown_)
    g.drawString("OVERLOAD16OUTLINE", 75 + 325 + dw / 2, 40 + 440, "Click to Continue", 0xffffff, 0.6f + 0.4f * std::sin(updateCount * 0.08f), Align::Center);
  if (pauseFade_ > 0 && !app.anyDialog()) {
    g.fill(-dw, 0, 800 + 2 * dw, 600, 0x000000, std::min(1.0f, (100 * pauseFade_ / 10) / 255.0f));
    if (Tex* t = g.img("IMAGE_PAUSED")) g.draw(t, (800 + dw - t->lw()) / 2, (600 - t->lh()) / 2, Mode(pauseFade_ / 10.0f));
  }
}

void GameScreen::nextLevel() {
  levelFile_.clear();
  if (const LevelInfo* li = app.curLevelInfo()) levelFile_ = li->file;
  seed = app.rand();
  countLevelStart();
  ready_ = startLevel(levelFile_);
}

static bool inReplayButton(float x, float y, float dw) { return x >= 736 + dw && x < 787 + dw && y >= 540 && y < 591; }
static bool inMenuButton(float x, float y) { return x >= 12 && x < 63 && y >= 540 && y < 591; }

bool GameScreen::input(const Input& in) {
  if (demo_ && (in.type == Input::MouseDown || in.type == Input::KeyDown)) { app.doToMenu(); return true; }
  const auto menuLive = [&]() { return inMenuButton(in.x, in.y) && !done_ && pauseCount_ == 0; };
  const auto replayLive = [&]() {
    return inReplayButton(in.x, in.y, (float)(canvasWidth() - 800)) && haveSnap_ && board_.shotIndex > 0 && pauseCount_ == 0 && !done_ && board_.phase == Phase::Aim && humanTurn();
  };
  switch (in.type) {
    case Input::MouseMove:
      mouseBx_ = (int)std::floor(in.x) - 75;
      mouseBy_ = (int)std::floor(in.y) - 40;
      haveMouse_ = true;
      mouseOverMenu_ = inMenuButton(in.x, in.y);
      mouseOverReplay_ = inReplayButton(in.x, in.y, (float)(canvasWidth() - 800));
      pushMouse();
      break;
    case Input::MouseLeave:
      mouseOverMenu_ = false;
      break;
    case Input::MouseDown:
      mouseBx_ = (int)std::floor(in.x) - 75;
      mouseBy_ = (int)std::floor(in.y) - 40;
      haveMouse_ = true;
      if (in.button == 3 && ready_ && !replaying_ && inReplayButton(in.x, in.y, (float)(canvasWidth() - 800)) && canSaveReplay() && board_.shotIndex > 0 && pauseCount_ == 0 &&
          !done_ && board_.phase == Phase::Aim && humanTurn()) { startReplay(true); break; }
      if (in.button == 3) { rightDown_ = true; break; }
      if (in.button != 1 || !ready_) break;
      heldButton_ = 0;
      if (replaying_) { stopReplay(); if (viewer_) app.doToMenu(); break; }
      if (menuLive()) { heldButton_ = 1; break; }
      if (replayLive()) { heldButton_ = 2; break; }
      if (pauseCount_ > 0) { if (!app.anyDialog()) pause(false); break; }
      pushMouse();
      if (board_.phase == Phase::Over) { if (board_.result.cleared && overTicks < 100) overTicks = 100; }
      else if (humanTurn()) clickBoard();
      break;
    case Input::MouseUp:
      if (in.button == 3) rightDown_ = false;
      else if (in.button == 1 && heldButton_ != 0) {
        const int held = heldButton_;
        heldButton_ = 0;
        if (held == 1 && menuLive()) app.doOptionsDialog();
        else if (held == 2 && replayLive()) startReplay();
      }
      else if (in.button == 1 && ready_ && !replaying_ && pauseCount_ == 0 && humanTurn()) recRelease();
      break;
    case Input::Wheel:
      if (ready_ && pauseCount_ == 0 && !viewer_) recWheel((int)std::lround(in.wheel));
      break;
    case Input::KeyDown: keyDown(in.key, in.mod); break;
    case Input::KeyUp:
      if (in.key == SDLK_UP) keyUp_ = false;
      else if (in.key == SDLK_DOWN) keyDown_ = false;
      else if (in.key == SDLK_RETURN && ready_ && !replaying_ && pauseCount_ == 0 && humanTurn()) recRelease();
      break;
    default: break;
  }
  return true;
}

void GameScreen::keyDown(int key, int) {
  if (replaying_ && key == SDLK_ESCAPE) { stopReplay(); if (viewer_) app.doToMenu(); return; }
  if (key == SDLK_ESCAPE) {
    if (!app.getDialog(D_Options) && !done_) app.doOptionsDialog();
  } else if (key == SDLK_SPACE) {
    if (pauseCount_ > 0 && !app.anyDialog()) pause(false);
  } else if (key == SDLK_RETURN) {
    if (ready_ && pauseCount_ == 0 && board_.phase != Phase::Over) {
      if (humanTurn()) clickBoard();
    }
    else if (ready_ && board_.phase == Phase::Over && board_.result.cleared && overTicks < 100) overTicks = 100;
    else if (pauseCount_ > 0 && !app.anyDialog()) pause(false);
  } else if (key == SDLK_LEFT) {
    if (ready_ && pauseCount_ == 0 && !viewer_) recWheel(-1);
  } else if (key == SDLK_RIGHT) {
    if (ready_ && pauseCount_ == 0 && !viewer_) recWheel(1);
  } else if (key == SDLK_UP) {
    keyUp_ = true;
  } else if (key == SDLK_DOWN) {
    keyDown_ = true;
  } else if (key == SDLK_G) {
    guide = !guide;
  }
}


static uint32_t hslToRgbEnd(int h, int s, int l) { return hslToRgb(h, s, l); }

static std::string commas(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v), r;
  for (size_t i = 0; i < s.size(); i++) {
    if (i && (s.size() - i) % 3 == 0) r += ',';
    r += s[i];
  }
  return v < 0 ? "-" + r : r;
}

EndLevelDialog::EndLevelDialog(App& a, Mode m, const LevelOutcome& o, const std::string& lname, const std::string& ltitle, int rk, bool nb)
    : Dialog(a, D_EndLevel, true, "", "", "", BUTTONS_NONE), mode(m), out(o), levelName(lname), levelTitle(ltitle), rank(rk), newBest(nb) {
  rope = true;
  bool win = m == AdventureWin || m == QuickWin || m == ChallengeWin || m == AdventureComplete;
  PlayerInfo* p = a.curProfile;
  std::string next;
  if (m == AdventureComplete) { header = "Adventure Complete!"; next = "Continue"; }
  else if (m == AdventureWin) {
    bool stageDone = a.curLevel == 0;
    header = stageDone ? "Stage Complete!" : (o.cleared ? "Level Cleared!" : "Level Complete!");
    next = stageDone ? "Next Stage" : "Next Level";
  } else if (win) {
    header = o.cleared ? "Level Cleared!" : "Level Complete!";
    next = m == ChallengeWin ? (a.challengeMore ? "Next Level" : "Trophy Menu") : "Replay Level";
  } else {
    header = "Try Again!";
    next = m == ChallengeLose ? "Retry Challenge" : "Retry Level";
  }
  const bool challengeEnd = m == ChallengeWin || m == ChallengeLose || m == ChallengeLives;
  const bool deluxeForm = a.game == Game::Deluxe && challengeEnd && a.endHeaderOverride.empty();
  if (m == ChallengeLevelDone || deluxeForm || (a.game == Game::Nights && challengeEnd)) {
    msgMode = true;
    if (m == ChallengeLevelDone) {
      const TrophyInfo* t = a.trophies.byId(a.curChallenge);
      const int left = t ? (int)t->levels.size() - a.challengeStep - 1 : 0;
      header = "Level Done";
      msgBody = left == 1 ? "There is 1 level left in this challenge." : "There are " + std::to_string(left) + " levels left in this challenge.";
      next = "Continue Challenge";
    } else if (m == ChallengeLives) {
      header = "Continue Challenge?";
      msgBody = "Don't let this level stop you.  You can do it!";
      msgBody += a.challengeLives == 1 ? "\n\nThis is your last life." : "\n\nYou have " + std::to_string(a.challengeLives) + " lives left.";
      next = "Continue Challenge";
    } else if (m == ChallengeWin) {
      const bool more = a.challengeMore;
      header = more ? "Level Passed!" : "Way to Go!";
      msgBody = more ? "You're doing great.  Keep up the excellent work on the next level!" : "Yay!  You beat the challenge!";
      next = more ? "Continue Challenge" : "Trophy Menu";
    } else {
      header = "Try Again!";
      std::string why = a.endBodyOverride;
      if (const TrophyInfo* t = deluxeForm ? a.trophies.byId(a.curChallenge) : nullptr) {
        if (t->scoreReq > 0) why = "You didn't get " + commas(t->scoreReq) + " points.\n\n";
        else if (t->clearLevel) why = "You didn't clear all of the pegs on the level.\n\n";
        else if (t->lives > 1) why = "You ran out of lives.\n\n";
      }
      msgBody = why + "Would you like to retry the challenge?";
      next = "Retry Challenge";
    }
    a.endBodyOverride.clear();
  }
  if (a.newMantel && a.game == Game::Nights && (m == AdventureWin || m == QuickWin || m == ChallengeWin)) next = "Grab Your New Trophy!";
  if (!a.endHeaderOverride.empty()) { header = a.endHeaderOverride; a.endHeaderOverride.clear(); }
  if (a.duelEnd.active) {
    duelEnd = true;
    for (int i = 0; i < 2; i++) { dScore[i] = a.duelEnd.score[i]; dName[i] = a.duelEnd.name[i]; }
    dses = a.duelSes;
    a.duelEnd.active = false;
  }
  (void)p;
  bMenu = patchButton(1000, msgMode && m == ChallengeLose && !deluxeForm ? "New Challenge" : msgMode && (m == ChallengeLevelDone || m == ChallengeLives) ? "Restart Challenge" : "Main Menu", "IMAGE_DLG_BUTTON3", 0, 0);
  bNext = patchButton(1001, next, "IMAGE_DLG_BUTTON3", 0, 0);
  bMenu->visible = !(deluxeForm && m == ChallengeWin);
  if (m == QuickWin || m == QuickLose) bRetry = patchButton(0, a.game == Game::Nights ? "Play New Level" : "Play Random Level", "IMAGE_DLG_BUTTON3", 0, 0);
  countUp = m == AdventureWin || m == AdventureComplete;
  tgt[0] = o.shotPoints; tgt[1] = o.feverPoints; tgt[2] = o.stylePoints; tgt[3] = o.tryBonus; tgt[4] = o.effBonus;
  levelTgt = o.totalScore;
  if (countUp) {
    totalCur = o.prevTotal;
  } else {
    for (int i = 0; i < 5; i++) cur[i] = tgt[i];
    levelCur = levelTgt;
    totalCur = levelTgt;
  }
  const std::string& pickedTip = a.stages.tip(a.rand());
  if (p && (m == AdventureLose || (m == AdventureWin && p->advCompletions > 0))) tip = pickedTip;
  w = 425;
  const auto& hs = a.highScores.scores(lname);
  for (auto& e : hs) table.push_back(e);
  highlight = rk > 0 ? rk - 1 : -1;
}

static const char* kBig = "OVERLOAD16OUTLINE";
static const char* kPlain = "PLAIN";

float EndLevelDialog::paintDuel(Gfx& g, bool draw) {
  const char* kBig2 = "OVERLOAD16OUTLINE";
  const char* kPl = "PLAIN";
  int maxS = std::max(dScore[0], dScore[1]);
  int cur = maxS;
  if (shownTicks < 200) cur = (int)((long long)shownTicks * maxS / 200);
  const bool rows = shownTicks >= 200 || dScore[0] <= cur || dScore[1] <= cur;
  const float yh = insT + g.fontAscent(headerFont);
  float nameBase = yh + 10 + g.fontAscent(kBig2);
  float scoreBase = nameBase + 32;
  float rowTop = scoreBase + 20;
  const float pitch = g.fontHeight(kPl);
  for (int i = 0; i < 2 && draw; i++) {
    float cx = i == 0 ? 20 + w / 4 : 3 * w / 4;
    float L = i == 0 ? 40 : 20 + w / 2, R = i == 0 ? w / 2 - 10 : w - 30;
    g.drawString(kBig2, cx, nameBase, dName[i], 0xffff88, 1, Align::Center);
    g.drawString(kBig2, cx, scoreBase, commas(std::min(cur, dScore[i])), 0xffff88, 1, Align::Center);
    g.fill(L, scoreBase + 8, R - L, 1, 0xffff88);
    if (!rows) continue;
    const int streak = dses.streakOwner == i ? dses.streak : 0;
    const char* lab[4] = {"Wins:", "Streak:", "Fevers:", "Points:"};
    const int val[4] = {dses.wins[i], streak, dses.fevers[i], dses.points[i]};
    for (int k = 0; k < 4; k++) {
      float base = rowTop + k * pitch + g.fontAscent(kPl);
      g.drawString(kPl, L, base, lab[k], 0xe3d540);
      std::string v = commas(val[k]);
      g.drawString(kPl, R - g.textWidth(kPl, v), base, v, 0xb9f811);
    }
  }
  float buttons = (bRetry ? 2 : 1) * 36 + (bRetry ? 6 : 0) + insB + 8;
  return rowTop + 4 * pitch + 12 + buttons;
}

float EndLevelDialog::paint(Gfx& g, bool draw) {
  if (duelEnd) return paintDuel(g, draw);
  if (msgMode && app.game == Game::Deluxe) {
    const float tw = w - insL - insR - 4;
    if (draw) g.drawWrapped(linesFont, {insL + 2, contentTop(g), tw, 0}, msgBody, 0xffff88, 1, Align::Center);
    return insT + insB + g.fontHeight(headerFont) + 8 + 36 + 8 + g.wrappedHeight(linesFont, tw, msgBody) + 30;
  }
  if (msgMode) {
    float y = insT + g.fontHeight(headerFont) + 16;
    Rect r{insL, y, w - insL - insR, 400};
    float bodyH = g.wrappedHeight("PLAIN2", r.w, msgBody);
    if (draw) g.drawWrapped("PLAIN2", r, msgBody, 0xffffff, 1, Align::Center);
    return y + bodyH + 18 + 36 + insB + 8;
  }
  bool list = mode == QuickWin || mode == QuickLose;
  const float L = insL, R = 390;
  float y = insT + g.fontHeight(headerFont) + 16;
  auto text = [&](const char* font, float x, float base, const std::string& s, uint32_t c, Align al = Align::Left) {
    if (draw) g.drawString(font, x, base, s, c, 1, al);
  };
  const bool nights = app.game == Game::Nights;
  const uint32_t bigLabel = nights ? 0x66ffbb : 0x66ffff, bigValue = nights ? 0xed88fe : 0xfeed00;
  const uint32_t rowLabel = nights ? 0xed90fe : 0xe3d540, rowValue = nights ? 0x66ffaa : 0xb9f811;
  auto bigRow = [&](const std::string& label, const std::string& v, const char* extra = nullptr) {
    float base = y + g.fontAscent(kBig);
    text(kBig, L, base, label, bigLabel);
    if (extra) text(kBig, L + g.textWidth(kBig, label), base, extra, 0xffff99);
    if (draw) g.drawString(kBig, R - g.textWidth(kBig, v), base, v, bigValue);
    y += g.fontHeight(kBig) + 4;
  };
  const bool advMode = mode == AdventureWin || mode == AdventureLose || mode == AdventureComplete;
  if (mode == AdventureWin || mode == AdventureComplete) {
    bigRow("Total Score:", commas(totalCur));
    if (draw) g.fill(L, y - 3, R - L, 1, 0xffffff);
  }
  const bool aceRow = nights && out.won && out.aceScore > 0;
  if (nights) {
    if (aceRow) bigRow("Ace Score:", commas(out.aceScore));
    bigRow("Your Score:", commas(levelCur), aceRow && levelCur > out.aceScore ? " ACED!!" : nullptr);
  } else {
    bigRow(advMode ? "Level Score:" : "Score:", commas(levelCur));
  }
  if ((mode == AdventureWin || mode == AdventureComplete) && tgt[3] > 0) {
    int t = out.tryNumber;
    bigRow(t == 1 ? "Win on 1st Try:" : t == 2 ? "Win on 2nd Try:" : t == 3 ? "Win on 3rd Try:" : "Try Bonus:", commas(cur[3]));
  }
  y += 8;
  float sy = y;
  auto row = [&](float x0, float x1, const char* l, const std::string& v) {
    float base = sy + g.fontAscent(kPlain);
    text(kPlain, x0, base, l, rowLabel);
    if (draw) g.drawString(kPlain, x1 - g.textWidth(kPlain, v), base, v, rowValue);
    sy += g.fontHeight(kPlain) + 3;
  };
  row(L, L + 140, "Shots:", std::to_string(out.shots));
  row(L, L + 140, "Free Balls:", std::to_string(out.freeBalls));
  row(L, L + 140, "% Cleared:", std::to_string(out.percentCleared()));
  sy = y;
  row(L + 170, R, "Shot Points:", commas(out.shotPoints));
  row(L + 170, R, "Fever Points:", commas(out.feverPoints));
  row(L + 170, R, "Style Points:", commas(out.stylePoints));
  y = sy + 10;
  if (list) {
    if (draw) g.fill(L, y, R - L, 1, 0xffffff);
    y += 8;
    if (newBest) {
      text(kBig, w / 2, y + g.fontAscent(kBig), "New High Score!", 0xffffff, Align::Center);
      y += 24;
    }
    float rx = L + 30, nx = rx + 40, sx = nx + 250, lh = g.fontHeight(kPlain) + 1;
    int n = std::min<int>((int)table.size(), 10);
    if (aceRow && n > 0) lh -= (g.fontHeight(kBig) + 4) / (float)n;
    for (int i = 0; i < n && draw; i++) {
      uint32_t col = i == highlight ? hslToRgbEnd((shownTicks * 3) % 0xff, 0xff, 0x80) : 0xffffff;
      float base = y + g.fontAscent(kPlain) + i * lh;
      g.drawString(kPlain, rx, base, std::to_string(i + 1) + ".", col);
      g.drawString(kPlain, nx, base, table[i].name, col);
      std::string v = commas(table[i].score);
      g.drawString(kPlain, sx - g.textWidth(kPlain, v), base, v, col);
    }
    y += lh * n + 6;
  }
  float buttons = (bRetry ? 2 : 1) * 36 + (bRetry ? 6 : 0) + insB + 8;
  const float tipH = tip.empty() ? 0.0f : std::max(44.0f, g.wrappedHeight("PLAIN2", w - insL - insR, tip));
  bool room = y + buttons + tipH <= 535;
  if (!tip.empty() && room) {
    if (draw) g.drawWrapped("PLAIN2", {insL, y, w - insL - insR, tipH}, tip, 0xfdfd87, 1, Align::Center);
    y += tipH + 8;
  }
  return y + buttons;
}

float EndLevelDialog::preferredHeight(float) { return paint(app.gfx, false); }

void EndLevelDialog::layout() {
  float bw = (w - insL - insR - 10) / 2, bh = bMenu->h;
  float by = h - insB - bh;
  if (bRetry) {
    bRetry->x = insL; bRetry->w = w - insL - insR; bRetry->y = by - bh - 6;
  }
  bMenu->x = insL; bMenu->w = bw; bMenu->y = by;
  bNext->x = insL + bw + 10; bNext->w = bw; bNext->y = by;
  if (!bMenu->visible) { bNext->x = insL; bNext->w = w - insL - insR; }
}

void EndLevelDialog::update() {
  Dialog::update();
  shownTicks++;
  if (duelEnd && shownTicks <= 200 && shownTicks % 10 == 1) app.playSample("SCORECOUNTER");
  if (!countUp || shownTicks < 30) return;
  if (delay > 0) { delay--; return; }
  int i = 0;
  while (i < 5 && cur[i] == tgt[i]) i++;
  if (i >= 5) return;
  int step = std::min(i == 4 ? 100000 : 1500, tgt[i] - cur[i]);
  cur[i] += step;
  totalCur += step;
  if (i < 3) {
    levelCur += step;
    if (levelCur == levelTgt) delay = 50;
  } else if (i == 3 && cur[3] == tgt[3]) {
    delay = 100;
    pitchAcc = 0;
  }
  if (shownTicks % 10 == 0) { pitchAcc += 1.0f; app.playSample("SCORECOUNTER", pitchAcc); }
}

void EndLevelDialog::drawLayer(Gfx& g) {
  layout();
  std::string savedHeader = header;
  if (duelEnd) {
    int maxS = std::max(dScore[0], dScore[1]);
    int cur = shownTicks < 200 ? (int)((long long)shownTicks * maxS / 200) : maxS;
    if (dScore[0] != dScore[1] && !(shownTicks >= 200 || dScore[0] <= cur || dScore[1] <= cur)) header.clear();
  }
  Dialog::drawLayer(g);
  header = savedHeader;
  paint(g, true);
}

void EndLevelDialog::onButton(int id) { app.endLevelButton(id, (int)mode); }

}
