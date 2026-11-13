#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

#include "audio.h"
#include "profile.h"
#include "ui/gfx.h"
#include "ui/fireworks.h"
#include "ui/stages.h"
#include "ui/start_target.h"
#include "ui/widescreen.h"
#include "ui/widgets.h"
#include "vfs.h"

namespace pp {
class Renderer;
}

namespace pp::ui {

class App;
class Dialog;
struct OtherGame;
class SessionKeeper;
class LogView;

class Panel : public WidgetHost {
 public:
  explicit Panel(App& a) : app(a) {}
  virtual ~Panel() = default;
  App& app;
  float x = 0, y = 0, w = 800, h = 600;
  std::vector<std::unique_ptr<Widget>> widgets;
  Widget* capture = nullptr;
  Widget* hover = nullptr;
  Widget* focusW = nullptr;
  bool mouseVisible = true;

  template <class T>
  T* add(std::unique_ptr<T> p) {
    T* raw = p.get();
    raw->host = this;
    widgets.push_back(std::move(p));
    return raw;
  }
  Button* addButton(int id, Button::Kind kind, const std::string& imageId, const std::string& label = "");
  Button* embeddedButton(int id, const std::string& imageId, float bx, float by);
  Button* patchButton(int id, const std::string& label, const std::string& imageId, float bx, float by);

  virtual void update();
  virtual void drawLayer(Gfx& g) {}
  virtual void drawOverlay(Gfx& g) {}
  void draw(Gfx& g);
  virtual bool input(const Input& in);
  virtual void keyDown(int key, int mod) {}
  virtual void onShow() {}

  void buttonDepress(int id) override { onButton(id); }
  void buttonPress(int id, int clicks) override { onButtonPress(id, clicks); }
  void buttonEnter(int id) override { onButtonEnter(id); }
  void buttonLeave(int id) override { onButtonLeave(id); }
  void playSound(const std::string& id, float pitch = 0) override;
  bool labelFullGradient() const override;
  virtual void onButton(int id) {}
  virtual void onButtonPress(int id, int clicks) {}
  virtual void onButtonEnter(int id) {}
  virtual void onButtonLeave(int id) {}
  Widget* findWidget(int id);
  int updateCount = 0;
};

class Screen : public Panel {
 public:
  using Panel::Panel;
  virtual const char* name() const = 0;
  virtual bool isGame() const { return false; }
  virtual bool wantsMouseHidden() const { return false; }
  virtual int canvasWidth() const { return 800; }
  virtual int canvasHeight() const { return 600; }
  virtual void lostFocus() {}
  virtual void onLeave() {}
};

class App : public WidgetHost {
 public:
  struct Config {
    Game game = Game::Deluxe;
    StartTarget start;
    bool bothGames = false;
    Game home = Game::Deluxe;
    std::filesystem::path otherDir, otherUserDir;
    std::filesystem::path userDir;
    bool fastBoot = false;
    int boardWidth = 0;
    int widescreen = -1;
    uint32_t seed = 0;
    bool noAudio = false;
    bool noSave = false;
  };
  App(Gfx& gfx, Vfs& vfs, Audio* audio, Renderer* boardRenderer, Config cfg);
  ~App();

  void tick();
  WidescreenMode widescreenMode() const;
  int boardWidthForNewBoard() const;
  int frameCanvasWidth(int boardCanvas) const;
  float pointerOnCanvas(int canvasW) const;
  void frame(int canvasW = 0);
  void input(const Input& in);
  void shutdown();
  bool wantsQuit() const { return quit_; }
  void requestQuit() { quit_ = true; }
  void requestGameSwitch(Game g, StartTarget start = {});
  bool takeGameSwitchRequest(GameRequest& r);
  void gameSwitchRefused(const GameRequest& r);
  bool gameSwitchPending() const { return switchWanted_; }
  void startAt(const StartTarget& start, SDL_Surface* lastPicture, bool pictureFillsWindow);
  bool pictureFillsWindow() const { return cur_ && (cur_->isGame() || cur_->canvasWidth() != 800); }
  bool bothGames = false;
  Game homeGame = Game::Deluxe;
  std::function<void(Game)> homeHook;
  void setHomeGame(Game g);
  void setGames(bool both, Game home);
  bool goHome();
  bool goHomeList(StartPlace place);
  bool startInList(const StartTarget& start);
  OtherGame* otherLevels();
  OtherGame* otherFiles();
  bool otherOpen(GameMode mode);
  bool modeOpen(GameMode mode) const;
  int listPage = -1;
  bool pickOther = false;
  void enterAdventure();
  void doAdventureQuestion();
  void requestGameFolders() { foldersWanted_ = true; }
  bool takeGameFoldersRequest() {
    bool wanted = foldersWanted_;
    foldersWanted_ = false;
    return wanted;
  }
  void onWindowClose();
  int updateCount() const { return updates_; }
  float renderAlpha = 1.0f;

  Gfx& gfx;
  Vfs& vfs;
  Audio* audio;
  Renderer* boardRenderer;
  Config cfg;
  Game game;
  StageMgr stages;
  TrophyMgr trophies;
  ProfileMgr profiles;
  HighScoreMgr highScores;
  Settings settings;
  PlayerInfo* curProfile = nullptr;
  std::filesystem::path userDir;

  GameMode gameMode = GameMode::None;
  int curStage = 0, curLevel = 0;
  int curChallenge = 0;
  int maxStage = 0, maxLevel = 0;
  bool colorblind = false;
  bool menuNewLevels = false, menuUnlocked = false;
  int faeriesCaught = 0;
  void keepFaeries();
  bool trialDisabled = true;
  std::string titleText() const;
  int numStages() const { return stages.numStages(); }
  int curLevelIndex() const { return curStage * 5 + curLevel; }
  const LevelInfo* curLevelInfo() const;

  Screen* screen() const { return cur_.get(); }
  std::string screenName() const { return cur_ ? cur_->name() : "none"; }
  void showLoadingScreen();
  void showMainMenu();
  void showAdventureScreen();
  void showLevelScreen(bool fromTrophy = false);
  void showTrophyScreen();
  void showStoryScreen(bool trophyReveal, bool fromEasterEgg);
  void showHelpScreen(bool fromBoard = false);
  void endHelpScreen();
  void showEditor(const std::string& level = "");
  void showModsScreen();
  void toggleLogView();
  void showLogView(bool large);
  void showModConsent(const std::string& modId, bool ask);
  void offerModConsent();
  void requestModApply();
  bool modApplyPending() const { return modApplyPending_; }
  bool modApplyBlocked(std::string* why = nullptr) const;
  const std::string& modApplyStatus() const { return modApplyStatus_; }
  int modApplyCount() const { return modApplyCount_; }
  std::function<void()> modAssetsHook;
  void showBoard(bool fromAdventure, bool newLevel);
  void startDemo();
  void startAdventureGame();
  void showIntroScreen();
  void showProgressScreen(int mode);
  void showCreditsScreen();
  bool moving() const { return !movers_.empty(); }

  Dialog* doDialog(int id, bool modal, const std::string& header, const std::string& lines, const std::string& footer, int buttonMode);
  Dialog* getDialog(int id) const;
  void killDialog(int id);
  void addDialog(int id, std::unique_ptr<Dialog> d);
  bool hasModal() const;
  bool anyDialog() const;
  bool boardPaused() const;
  void pauseBoard(bool on);
  void doOptionsDialog();
  void doVisualsDialog();
  void doUserDialog();
  void doImportProfileDialog();
  void importDialogButton(int dlg, int btn);
  void doCreateUserDialog();
  void doRenameUserDialog(const std::string& name);
  void doConfirmQuitDialog();
  void doConfirmRestartAdventureDialog();
  void doConfirmNewChallengeDialog();
  void doConfirmMainMenuDialog();
  void doConfirmRestartLevelDialog(float top = -1);
  void restartChallenge();
  void alignQuestionWithMenu(Dialog* d);
  void doToMenu();
  void doConfirmDeleteUserDialog(const std::string& name);
  void finishUserDialog(bool accepted);
  void finishCreateUserDialog(bool accepted);
  void finishRenameUserDialog(bool accepted);
  void finishNameErrorDialog(int id);
  void onProfileCreated();
  bool hasBoard() const;
  void showEndLevel(int mode, const struct LevelOutcome& o, const std::string& level, const std::string& title, int rank, bool newBest);
  void endLevelButton(int id, int mode);
  bool playNewLevel();
  void doDialogScroll(int id, bool modal, const std::string& header, const std::string& lines, const std::string& footer, int mode);
  bool challengeMore = false;
  std::string resolveChallengeLevel(const std::string& token);
  int packStage = -1;
  using DuelSession = DuelTally;
  DuelSession duelSes;
  struct DuelEnd { bool active = false; int score[2]{}; std::string name[2]; } duelEnd;
  std::string endBodyOverride;
  std::string endHeaderOverride;
  std::string challengeLevel;
  int challengeScore = 0;
  int challengeScore2 = 0;
  int challengeBalls = 0;
  int challengeLives = 0;
  bool challengeKeepLives = false;
  int mantelBefore = 0;
  bool quitPaused = false;
  bool newMantel = false;
  std::set<int> newTrophies;
  int challengeStep = 0;
  void showScreen(const std::string& name);
  void doNotYet(const std::string& text);
  void doReplaysDialog();
  void doConfirmDeleteReplay(const std::string& name);
  void doSaveReplayDialog();
  void replayDialogButton(int dlg, int btn);
  void playReplayFile(const std::filesystem::path& path);
  void dialogButton(int dialogId, int buttonId);
  void finishOptionsDialog(bool apply, bool scrollOff);
  void positionDialog(Dialog* d, float width, bool fromMenu = false);

  void incLevel(int delta);
  void checkMaxStage();
  bool isLevelUnlocked(int stage, int level) const { return !curProfile || curProfile->isLevelUnlocked(stage, level); }
  void syncProfile();
  void saveProfile() { if (curProfile && !cfg.noSave) profiles.saveIfDirty(curProfile); }
  void saveAll();
  bool tryShowNewTrophy();
  void applySettings();
  void applyColorblind(bool on);

  void playSample(const std::string& id, float pitchSemis = 0, float vol = 1);
  void playMusic(const std::string& which);
  void stopMusic();
  Screen* helpOverlay() const { return overlay_.get(); }
  uint32_t rand();
  uint32_t randState() const { return rng_; }
  void setRandState(uint32_t v) { if (v) rng_ = v; }
  int lastOpponent = -1, lastRolled = -1;
  CrtRand& crtRand() { return crt_; }

  void buttonDepress(int id) override {}

  enum Dir { Stay = 0, Left = 1, Right = 2, Down = 3, Up = 4 };
  void scrollOff(Panel* l, Dir d, bool remove);
  void scrollOn(Panel* l);
  int scrollDir = 0;

  std::shared_ptr<SessionKeeper> sessions;
  void setScreen(std::unique_ptr<Screen> s, Dir offDir);
  float canvasW() const { return canvasW_; }
  float mouseX = 0, mouseY = 0;
  bool windowFocused = true;
  void quitToDesktop() { quit_ = true; }
  std::function<void(bool)> setFullscreenHook;
  std::function<void(bool)> showCursorHook;
  std::function<void(int, int)> resizeHook;

 private:
  struct Mover {
    Panel* l;
    int ticks;
    float sx, sy, tx, ty;
    bool remove;
    std::unique_ptr<Screen> owned;
  };
  void updateMovers();
  void serviceModApply();
  void runModApply();
  void refreshAssets(const VfsChange& change);
  bool modApplyPending_ = false;
  int modApplyCount_ = 0;
  std::string modApplyStatus_;
  Mover* mover(Panel* l);
  void inputToLayers(const Input& in);

  std::unique_ptr<Screen> cur_, overlay_, stashed_;
  std::string curMusic_;
  std::vector<std::unique_ptr<Dialog>> dialogs_;
  std::vector<std::pair<int, Dialog*>> dialogIds_;
  std::vector<Mover> movers_;
  std::vector<std::unique_ptr<Dialog>> deadDialogs_;
  std::unique_ptr<LogView> logView_;
  int updates_ = 0;
  bool quit_ = false;
  bool switchWanted_ = false;
  bool switchHome_ = false;
  int switchWait_ = 0;
  bool foldersWanted_ = false;
  GameRequest switchTo_;
  std::unique_ptr<OtherGame> other_;
  float canvasW_ = 800;
  uint32_t rng_ = 0x2545f491;
  CrtRand crt_;
  friend class Dialog;
};

enum DialogId {
  D_DeleteUser = 2, D_ReturnToMenu = 4, D_Quit = 5, D_RestartLevel = 6, D_NewChallenge = 7, D_RestartAdventure = 8, D_CreateUser = 10,
  D_NameError = 11, D_ToMenu = 13, D_Tip = 14, D_HighScoreEntry = 16, D_Options = 19, D_RenameUser = 20, D_NameErrorRename = 21,
  D_UserList = 24, D_WhichAdventure = 25, D_SaveGameError = 26, D_Replay1 = 27, D_Replay2 = 34, D_Locked = 38, D_SaveAll = 41, D_DeleteReplay = 42, D_ReplayError = 43, D_ImportProfile = 44, D_SaveReplay = 47, D_OverwriteReplay = 48,
  D_EndLevel = 1, D_Character = 30, D_Pause = 40
};

const char* fontById(int id);

}
