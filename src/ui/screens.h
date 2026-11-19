#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "ui/app.h"
#include "ui/sparkle.h"
#include "ui/fireworks.h"
#include "ui/nights_loading.h"

namespace pp::ui {

class LoadingScreen : public Screen {
 public:
  explicit LoadingScreen(App& a);
  const char* name() const override { return "loading"; }
  int canvasWidth() const override { return app.consoleCanvasWidth(); }
  void update() override;
  void drawLayer(Gfx& g) override;
  void drawOverlay(Gfx& g) override;
  bool input(const Input& in) override;
  void onButton(int id) override;
  float loadProgress = 0, visualProgress = 0, sunAngle = 0;
  int doneCnt = 0, playPulse = 0, ballState = 0, logoBounce = 0, logoFrames = 0, beams = 0, adventureStartDelay = 0;
  float ballX = 0, ballY = 0, ballVX = 0, ballVY = 0;
  bool ballBounced = false, showDeluxe = false, loaded = false, fastBoot = false;
  struct P { float x, y, vx, vy, angle; int alpha; bool flip; };
  std::vector<P> particles;
  size_t nextRes = 0;
  std::vector<std::string> resList;
  int startedFrom = 0;
  Button* playBtn = nullptr;
  std::unique_ptr<NightsLoading> nights;
  std::vector<NightsCmd> cmds;
  int mouseX = 0, mouseY = 0;
  void updateNights();
  void updateDeluxe(int cnt, float loadTime);
  void renderNights(Gfx& g);
  void drawConsoleTitle(Gfx& g);
  Rect classicPlayRect{};
  void doBallBounce();
  void updateBall();
  void spawnParticles(float x, float y, int n);
};

class MainMenu : public Screen {
 public:
  explicit MainMenu(App& a, bool nightsHub = false);
  const char* name() const override { return "mainmenu"; }
  int canvasWidth() const override;
  void update() override;
  void drawLayer(Gfx& g) override;
  void drawOverlay(Gfx& g) override;
  bool input(const Input& in) override;
  void onButton(int id) override;
  void onButtonPress(int id, int clicks) override;
  void onButtonEnter(int id) override;
  void keyDown(int key, int mod) override;
  void lostFocus() override;
  void syncPlayerInfo();
  void startGame();
  void startAdventure();
  void drawSpeechBubble(Gfx& g, int which, float alpha, bool fading);
  static bool mantelEarned(App& app, int id);
  static int mantelAt(App& app, float mx, float my);
  static const DecalInfo* decalAt(App& app, float mx, float my, int* stageOut);
  void drawNights(Gfx& g);
  struct Sparkle {
    std::string name;
    SparkleField field;
    float fade;
    bool live;
  };
  std::vector<Sparkle> sparkles;
  float fadeOf(const std::string& name) const;
  void buildSparkles();
  void updateSparkles();
  void drawSparkles(Gfx& g);
  Button *bAdv, *bSolo, *bDuel, *bChal, *bReplay, *bOptions, *bDemo, *bQuit, *bLink, *bUpsell;
  void setBubble(int which, int delay);
  int fadeTicks() const { return isNights ? 15 : 20; }
  void placeButtons();
  void updateBubbles();
  int advStage = 0, advLevel = 0, bubbleCount = 0;
  int bubble = 0, bubbleOld = 0, bubbleFade = 0, bubbleDwell = 0, trophyTier = 0, hoverSeq = 0, mouseMoveDelay = 0, sunTick = 0, bubbleDimmed = 0;
  bool mouseMoved = false, randomPick = false, cry = false, secretDone = false;
  int trophyAnim = 0, trophyAnimMax = 0;
  bool isNights = false;
 private:
  enum class Page { Home, Single, Nights, Lounge, Multiplayer, Party, Duel };
  Page page_ = Page::Home;
  static constexpr int kMenuRows = 5;
  static constexpr int kFooterButtons = 3;
  std::array<Button*, kMenuRows> consoleRows_{};
  std::array<Button*, kFooterButtons> consoleFooter_{};
  Button* loungeBack_ = nullptr;
  int consoleFocus_ = 0;
  int consoleLayoutWidth_ = 800;
  int slideTicks_ = 0;
  float slideDirection_ = 1;
  int consoleWidth() const;

  bool consoleButton(int id) const;
  void placeConsole(int width);
  void drawConsole(Gfx& g);
  void setConsolePage(Page page, int focus);
  void consoleAction(int id);
};

class AdventureScreen : public Screen {
 public:
  explicit AdventureScreen(App& a);
  const char* name() const override { return "adventure"; }
  int canvasWidth() const override;
  void drawLayer(Gfx& g) override;
  void drawOverlay(Gfx& g) override;
  void onButton(int id) override;
  void keyDown(int key, int mod) override;
  void startGame();
  void drawHighScores(Gfx& g, float x, float y);
  void drawPersonalHighScores(Gfx& g, float x, float y);
  void drawStats(Gfx& g, float x, float y, bool current);
  void drawFrame(Gfx& g, float x, float y, float w, float h, const std::string& title);
  bool hasSave = false, nights = false;
  Button *bContinue, *bRestart, *bMenu;
};

class LevelScreen : public Screen {
 public:
  explicit LevelScreen(App& a);
  const char* name() const override { return "levelscreen"; }
  void update() override;
  void drawLayer(Gfx& g) override;
  void onButton(int id) override;
  void onButtonEnter(int id) override;
  void onButtonLeave(int id) override;
  void keyDown(int key, int mod) override;
  void syncStageNum();
  void doPlay(int id);
  bool canSelect(int id) const;
  void drawLevelInfo(Gfx& g, float x, float y);
  const StageInfo* pageStage() const;
  bool groupOpen() const;
  int stage = 0, hovered = -1, hoverTicks = 0;
  Button* bLevel[5];
  Button *bBack, *bNext, *bMenu, *bRandom;
  OtherGame* og = nullptr;
  bool other = false, pick = false;
};

class StoryScreen : public Screen {
 public:
  StoryScreen(App& a, bool trophyReveal, bool easter);
  const char* name() const override { return "story"; }
  int canvasWidth() const override;
  void update() override;
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  void keyDown(int key, int mod) override;
  void onButton(int id) override;
  void finish();
  void initText();
  void dreamIn();
  void skipTyping();
  void drawNights(Gfx& g);
  void drawConsole(Gfx& g, const std::string& title);
  void drawDeluxeProgress(Gfx& g, float center, bool wide);
  void layoutContinue(float width);
  bool reveal, easter;
  int stage = 0;
  Button* bContinue = nullptr;
  int page = 1;
  int typed = 0, wait = 0, start = 20, boxFade = 0, boxes = 0, shownChars = 0;
  bool done = false, dreamy = false;
  std::string text, text2, head;
  int totalVisible = 0, headVisible = 0;
  bool win = false;
  int tier = 0;
  Button* bLink = nullptr;
  std::unique_ptr<StoryFireworks> fw;
  void initWin();
  void updateWin();
  void drawWin(Gfx& g);
  void drawFireworks(Gfx& g);
};

class HelpScreen : public Screen {
 public:
  HelpScreen(App& a, bool fromBoard);
  const char* name() const override { return "help"; }
  void drawLayer(Gfx& g) override;
  void onButton(int id) override;
  void keyDown(int key, int mod) override;
  int page = 0;
  bool fromBoard;
  int numPages() const;
 private:
  void layoutFooter(bool wide);
  void drawConsole(Gfx& g);
};

class TrophyScreen : public Screen {
 public:
  explicit TrophyScreen(App& a);
  const char* name() const override { return "trophy"; }
  void update() override;
  void drawLayer(Gfx& g) override;
  void onButton(int id) override;
  void onButtonEnter(int id) override;
  void onButtonLeave(int id) override;
  void keyDown(int key, int mod) override;
  void syncPage();
  void doPlay(int slot);
  const TrophyPage* pageShown(bool& theirs) const;
  int ownPages() const;
  int theirPages() const;
  int page = 0, hovered = -1, hoverTicks = 0, badgeTicks = 0, applauseTicks = 0;
  Button* bSlot[5];
  Button *bBack, *bPrev, *bNext;
  OtherGame* og = nullptr;
};

class ProgressScreen : public Screen {
 public:
  ProgressScreen(App& a, int mode, bool fromNightsHub);
  const char* name() const override { return "progress"; }
  int canvasWidth() const override;
  void update() override;
  void drawLayer(Gfx& g) override;
  void onButton(int id) override;
  void keyDown(int key, int mod) override;
  bool input(const Input& in) override;
  void finish();
  int mode, t = 0, newest = -1, hovered = -1, prevHovered = -1, drumEnd = 0, closing = 0;
  Button* bContinue;
 private:
  bool fromNightsHub_;
};

class CreditsScreen : public Screen {
 public:
  explicit CreditsScreen(App& a);
  const char* name() const override { return "credits"; }
  void update() override;
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  void keyDown(int key, int mod) override;
  void finish();
  struct Line { std::string text, font; uint32_t color = 0xffffff; float x = 0, y = 0; bool centred = true; };
  struct Page { std::vector<Line> lines; int hold = 200, start = 0, trophy = 0, shown = -1; };
  std::vector<Page> pages;
  size_t page = 0;
  int t = 0, total = 0;
};

class IntroScreen : public Screen {
 public:
  explicit IntroScreen(App& a);
  const char* name() const override { return "intro"; }
  int canvasWidth() const override;
  void update() override;
  void drawLayer(Gfx& g) override;
  void onButton(int id) override;
  bool input(const Input& in) override;
  void keyDown(int key, int mod) override;
  void advance();
  int pageNo = 1, t = 0, typed = 0;
  Button* bContinue;
};

void drawSpeechBox(Gfx& g, float x, float y, float w, float h, float alpha = 1.0f, int pointer = 0, float pointerX = 30);
void drawTrophyRoom(Gfx& g, App& app, int newest, int t);

}
