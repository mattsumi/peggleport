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
  bool pauseDone = false;
  int cur[5]{}, tgt[5]{}, levelCur = 0, levelTgt = 0, totalCur = 0, delay = 0, tingStep = 0;
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
  void paintPortrait(Gfx& g, float top);
  int portraitStage = -1, portraitLevel = 0;
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
  void discordPresence(pp::online::Discord::Presence& presence) const override {
    presence.state = !ready_ ? "Level unavailable" : viewer_ ? "Watching a replay" : demo_ ? "Watching Demo mode" : paused() ? "Level paused" : done_ ? "Level finished" : "Playing a level";
  }
  bool wantsMouseHidden() const override { return false; }
  int canvasWidth() const override { return app.frameCanvasWidth(800 + (board_.W - 650)); }
  void update() override;
  void drawLayer(Gfx& g) override;
  void drawOverlay(Gfx& g) override;
  void drawBarePause(Gfx& g);
  bool input(const Input& in) override;
  void menuThroughDialog(const Input& in);
  Renderer::View consoleView() const;
  void onButton(int id) override;
  void keyDown(int key, int mod) override;
  void lostFocus() override { beginBarePause(); }
  void onLeave() override;
  void startChallengeLevel();
  void restartLevel(bool fromMenu = true);
  bool hasProgress() const;
  bool keepsSession() const;
  int betweenLevels() const;
  void dropSession() { sessionOff_ = true; }
  bool fillSession(SavedSession& s) const;
  void masterChosen(bool picked);
  void nextLevel();
  bool ok() const { return ready_; }
  const LevelBundle& bundle() const { return lb_; }
  Board& board() { return viewer_ ? replay_ : board_; }
  LevelOutcome collectOutcome() const;
  bool levelDone() const { return done_; }
  bool saveReplay(bool all, std::filesystem::path* pathOut, const std::string& stem = std::string());
  bool replayAllShots() const { return replayAll_; }
  bool canSaveReplay() const { return recording_ && !duel_ && haveSnap_ && !log_.empty(); }
  bool startViewer(const ReplayData& d);
  bool viewing() const { return viewer_; }
  bool replaying() const { return replaying_; }
  bool consoleReplayPanel() const { return replayPanel_ && hudInset() != 0; }
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
  bool startLevel(const std::string& name, bool legacyMissCoin = false);
  void finishLevel();
  void applyProgress(const LevelOutcome& o, bool won);
  void submitStats();
  bool statsSent_ = false;
  bool sessionOff_ = false, endPending_ = false;
  uint32_t randAfter_ = 0;
  GameMode statsMode_ = GameMode::None;
  void pushMouse();
  bool duel_ = false;
  int aiWait_ = 0, aiState_ = 0, aiTick_ = 0, aiThink_ = 0, aiFixed_ = -100;
  uint32_t aiDrawn_ = 0;
  float aiChosen_ = 0;
  std::unique_ptr<AIMgr> ai_;
  std::unique_ptr<Board> aiBoard_;
  int aiSkill() const;
  uint32_t aiDraw();
  void logAi(uint8_t kind, int a, int b);
  void restoreAi(const ReadySession& r);
  bool demo_ = false;
  bool human2_ = false;
  std::string oppName() const { return human2_ ? "Player 2" : "Computer"; }
  bool humanTurn() const { return !demo_ && (!duel_ || board_.cur == 0 || human2_); }
  Board snap_, replay_;
  std::optional<BoardEffects> pausedEffects_;
  bool haveSnap_ = false, replaying_ = false, replayRan_ = false, mouseOverReplay_ = false;
  int replayTicks_ = 0;
  bool watchedReplay_ = false;
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
  void beginBarePause();
  bool barePaused_ = false;
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
  int boardLeft() const { return 75 + (canvasWidth() - 800 - (board_.W - 650)) / 2; }
  int hudInset() const { return Renderer::hudInset(canvasWidth(), board_.W); }
  bool haveMouse_ = false;
  std::string levelFile_;
  int pendingMode_ = 0, pendingRank_ = 0;
  LevelOutcome pendingOut_;
  std::string pendingTitle_;
  std::vector<Event> spinHeld_;
};

}
