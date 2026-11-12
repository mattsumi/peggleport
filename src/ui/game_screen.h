#pragma once
#include "content.h"
#include "render.h"
#include <memory>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <string>

#include "ui/aimgr.h"
#include "ui/dialogs.h"
#include "ui/replay.h"
#include "ui/screens.h"

namespace pp::ui {

struct SavedSession;
struct ReadySession;

class EndLevelDialog : public Dialog {
 public:
  enum Mode { AdventureWin, AdventureLose, QuickWin, QuickLose, ChallengeWin, ChallengeLose, AdventureComplete,
              ChallengeLevelDone  ,
              ChallengeLives   };
  EndLevelDialog(App& a, Mode m, const LevelOutcome& o, const std::string& levelName, const std::string& levelTitle, int rank, bool newBest);
  Mode mode;
  LevelOutcome out;
  std::string levelName, levelTitle, tip;
  int rank, shownTicks = 0, highlight = -1;
  bool countUp = false;
  float pitchAcc = 0;
  int cur[5]{}, tgt[5]{}, levelCur = 0, levelTgt = 0, totalCur = 0, delay = 0;
  std::vector<HighScoreEntry> table;
  bool newBest;
  int advTotal = 0, bonus = 0;
  Button *bMenu = nullptr, *bNext = nullptr, *bRetry = nullptr;
  bool msgMode = false;
  std::string msgBody;
  bool duelEnd = false;
  int dScore[2]{};
  std::string dName[2];
  App::DuelSession dses;
  float paintDuel(Gfx& g, bool draw);
  void drawLayer(Gfx& g) override;
  void update() override;
  void onButton(int id) override;
  float preferredHeight(float width) override;
  void layout();
  float paint(Gfx& g, bool draw);
};

class GameScreen : public Screen {
 public:
  GameScreen(App& a, bool fromAdventure, bool newLevel);
  GameScreen(App& a, const ReplayData& d);
  GameScreen(App& a, const ReadySession& r);
  ~GameScreen() override;
  const char* name() const override { return "board"; }
  bool isGame() const override { return true; }
  bool wantsMouseHidden() const override { return false; }
  int canvasWidth() const override {
    if (!ready_) return 800;
    int w = 800 + (board_.W - 650);
    if (app.settings.vis.ultraWideHud && app.boardRenderer) w = std::max(w, app.boardRenderer->canvasWidth());
    return w;
  }
  void update() override;
  void drawLayer(Gfx& g) override;
  void drawOverlay(Gfx& g) override;
  bool input(const Input& in) override;
  void onButton(int id) override;
  void keyDown(int key, int mod) override;
  void lostFocus() override;
  void onLeave() override;
  void startChallengeLevel();
  void restartLevel(bool fromMenu = true);
  bool hasProgress() const;
  bool keepsSession() const;
  bool fillSession(SavedSession& s) const;
  void masterChosen(bool picked);
  void nextLevel();
  bool ok() const { return ready_; }
  const LevelBundle& bundle() const { return lb_; }
  Board& board() { return viewer_ ? replay_ : board_; }
  LevelOutcome collectOutcome() const;
  bool levelDone() const { return done_; }
  bool saveReplay(bool all, std::filesystem::path* pathOut);
  bool canSaveReplay() const { return recording_ && haveSnap_ && !log_.empty(); }
  bool startViewer(const ReplayData& d);
  bool viewing() const { return viewer_; }
  void pause(bool on);
  bool paused() const { return pauseCount_ > 0; }
  int pauseFade() const { return pauseFade_; }
  bool fastForwardHeld() const { return rightDown_; }
  int stepsThisTick() const;
  bool rightClickSpeedup = false;
  bool guide = true;
  uint32_t seed = 0;
  std::string loadError;
  int overTicks = 0;

 private:
  bool startLevel(const std::string& name);
  void finishLevel();
  void applyProgress(const LevelOutcome& o, bool won);
  void submitStats();
  bool statsSent_ = false;
  GameMode statsMode_ = GameMode::None;
  void pushMouse();
  bool duel_ = false;
  int aiWait_ = 0, aiState_ = 0, aiTick_ = 0;
  std::unique_ptr<AIMgr> ai_;
  int aiSkill() const;
  bool demo_ = false;
  bool human2_ = false;
  std::string oppName() const { return human2_ ? "Player 2" : "Computer"; }
  bool humanTurn() const { return !demo_ && (!duel_ || board_.cur == 0 || human2_); }
  Board snap_, replay_;
  bool haveSnap_ = false, replaying_ = false, replayRan_ = false, mouseOverReplay_ = false;
  int replayTicks_ = 0;
  void clickBoard();
  std::vector<ReplayEvent> log_;
  bool recording_ = false, viewer_ = false, replayPanel_ = false;
  Button *rbReplay_ = nullptr, *rbSave_ = nullptr, *rbDone_ = nullptr;
  void showReplayPanel();
  void layoutReplayPanel();
  void drawReplayHousing(Gfx& g);
  void drawReplayText(Gfx& g);
  BoardConfig cfg_;
  ReplayData view_;
  size_t viewIdx_ = 0;
  bool replayAll_ = false;
  void recMouse(int x, int y);
  void recClick();
  void recRelease();
  void recWheel(int n);
  void applyViewEvents();
  void replayFinished();
  void startReplay(bool all = false);
  void stopReplay();
  void stepReplay();
  bool charPaused_ = false, charChosen_ = false, keepRoll_ = false;
  int rolled_ = -1;
  void showCharacterDialog();
  bool choosesMaster() const;
  int chosenMaster();
  int hudStage_ = 0, hudLevel_ = 0;
  int boardResets_ = 0;
  void countLevelStart();
  int stageDialogDelay_ = 0;
  bool tipPaused_ = false;
  bool focusPaused_ = false;
  void showStageDialog();
  void aiTurn();
  std::string cornerText() const;
  Board board_;
  LevelBundle lb_;
  Hud hud_;
  bool ready_ = false, done_ = false, endShown_ = false, pendingBest_ = false;
  bool rightDown_ = false, keyUp_ = false, keyDown_ = false, mouseOverMenu_ = false;
  int heldButton_ = 0;
  int pauseCount_ = 0, pauseFade_ = 0;
  int mouseBx_ = 0, mouseBy_ = 0;
  bool haveMouse_ = false;
  std::string levelFile_;
  int pendingMode_ = 0, pendingRank_ = 0;
  LevelOutcome pendingOut_;
  std::string pendingTitle_;
  std::vector<Event> spinHeld_;
};

}
