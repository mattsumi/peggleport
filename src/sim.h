#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <array>
#include <set>
#include <vector>
#include <utility>

#include "gun.h"
#include "level.h"
#include "masters.h"
#include "mods/simhooks.h"
#include "physics.h"
#include "physobj.h"
#include "vec2.h"

namespace pp {

struct MTRand {
  uint32_t mt[624]{};
  int idx = 625;
  void seed(uint32_t s);
  uint32_t drawCount = 0;
  uint32_t next();
  uint32_t rand() { return next() & 0x7fffffffu; }
};

struct Mover {
  int type = 0, time = 200, offset = 0, pause1 = 0, pause2 = 0, phase1 = 0, phase2 = 0;
  float ax = 0, ay = 0, rx = 0, ry = 0, phase = 0, moveRot = 0, maxAngle = 0, rotation = 0, postDelay = 0;
  bool single = false;
  bool linked = false;
  int sub = -1;
  float lastX = 0, lastY = 0, outX = 0, outY = 0;
  static Mover fromNode(const Node& n, bool single);
  void calc(int tick, float& x, float& y, float& rot) const;
  void calc(int tick, float frac, float& x, float& y, float& rot) const;
  void translate(float dx, float dy);
  void follow(const Mover& s);
  void step(int tick, float frac, float& x, float& y, float& rot, const Mover* s);
  bool rotating() const { int t = type < 0 ? -type : type; return t == 8 || t == 9 || t == 13 || t == 14; }
};

enum class Shape { None, Circle, Line, Poly, Brick, Hole };
enum class Phase { Aim, Shot, Popping, Over };

struct Obj {
  Shape shape = Shape::None;
  int sort = 0;
  bool fromBase = false;
  bool collidable = false, visible = false, canMove = false, background = false, foreground = false, foreground2 = false;
  bool stopReset = false, gone = false;
  float bounce = 0, slide = 0, maxBounceVel = 0;
  std::string id, image;
  float imgDx = 0, imgDy = 0;
  int sound = 0;
  int subId = 0;
  int pegType = 0;
  int custom = 0, hits = 0, hitTick = -1000000;
  float radius0 = 0;
  bool canBeOrange = false, lit = false;
  std::string logic;
  bool toggle = false, mirror = false;
  int zoneMode = 0;
  int litAge = 0, popTimer = 0, flash = 0;
  bool hasMover = false, moving = false;
  Mover mv;
  int mtick = 0;
  int fadeT = 0;
  bool fadeOut = false;
  int moveCount = 0;
  V2 pos, vel;
  float rot = 0, scale = 1;
  int normalDir = 1;
  float radius = 0;
  std::vector<V2> local;
  phys::Line pline;
  phys::Poly ppoly;
  std::vector<V2> brickInner, brickOuter;
  int brickIdx[4]{};
  float hw = 0, hh = 0;
  bool circular = false;
  int score = 0, outDelay = 0, exitIdx = -1;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct Emit {
  const Node* node = nullptr;
  bool fromBase = false, canMove = false, hasMover = false;
  Mover mv;
  int mtick = 0;
  size_t before = 0;
  V2 pos;
};

struct Rod {
  struct Spark {
    float ang;
    int life;
  };
  int state = 0;
  int timer = 0, bob = 0, age = 0;
  int x = 0, anchorX = 0;
  float y = 586.0f, vy = 0, baseY = 0;
  std::vector<int> rings;
  std::vector<Spark> sparks;
  uint32_t sfx = 0x2545f491u;
};

struct Event {
  enum Type { PegHit, PegPop, ExtraBall, FreeBall, PointBoost, Fever, FeverSlot, Power, StyleShot, CoinFlip, LevelDone, ShotEnd,
              BallLost, PowerEnd, PegLit, MultiplierUp, CoinSpin,
              Shot,
              HoleIn,
              Jimmy,
              Bump,
              LuckySpin,
              SpookyRespawn,
              Penalty,
              FlipperMove,
              Zen,
              Hint,
              TimeBomb,
              Toggle,
              Bubble,
              ChainStart,
              ChainStrike,
              ChainBolt   } type;
  int a = 0, b = 0;
  int c = 0;
};

struct Ball {
  V2 pos, vel, prev, holeTarget;
  V2 hist[6];
  float r = 6;
  float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
  int since = 1000000;
  int mode = 0;
  int sinceHit = 1000000;
  int t188 = 0;
  float impact = 0;
  V2 lastN;
  bool hitFlag142 = false, hitFlag165 = false;
  int uid = 0;
  bool pairs = true;
  bool alive = true, slowFlag = false, touchedBumper = false;
  bool fire = false, spooky = false;
  bool chain = false;
  bool spookyUsed = false;
  bool hat = false;
  V2 disp, hatPos, hatPrev;
  int hatAge = 0;
  int hatUid = 0;
  float hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
  int stuck = 0, age = 0, holeOut = 0, holeCooldown = 0, holeExit = -1;
  int slot = 0;
  int rimTick = 0, rimCount = 0;
  int rimMaxY = 0;
  bool coverHit = false;
  bool flipperTouched = false;
  int slideA = 0, slideB = 0, slideT = -1000;
};

struct ChallengeRules {
  bool active = false;
  bool noFreeballs = false;
  bool noGreens = false;
  bool noForceWin = false;
  bool noEndOnWin = false;
  bool freeballCovered = false;
  int scoreReq = 0;
  int scoreCeil = 0;
  int styleScoreReq = 0;
  int uniqueStyleReq = 0;
  int styleShots[24]{};
  int powerups[16]{};
  int magicHat = 0, tripleScore = 0;
  float gravityMod = 1.0f;
  float projSpeedMod = 1.0f;
  int levelIndex = 0, levelCount = 1;
  bool set(const std::string& key, const std::string& value);
};

struct BoardConfig {
  int boardWidth = 650;
  int players = 1;
  bool duel = false;
  int balls = 0;
  int startScore = 0;
  int orange = 25;
  uint32_t seed = 4357;
  bool greens = true, purple = true;
  int master[4]{0, 0, 0, 0};
  float zoomK = 1.0f;
  int fpBits = 24;
  int masterIndex = 9;
  int maxLevel = 99;
  int curLevel = -1;
  bool clearLevel = false;
  bool coinFlipSeen = true;
  bool nights = false;
  int prevFirst = -1, prevFirstRun = 0;
  ChallengeRules ch;
  bool liveSpin = false;
  int tryNumber = 1;
  PhysicsEdit physics;
  std::vector<std::pair<int, std::string>> pegTypes;
};

struct LevelResult {
  bool finished = false, won = false, cleared = false;
  int totalScore = 0;
  int shots = 0, pegsHit = 0, bestShot = 0, bestFever = 0;
  int shotPoints = 0, feverPoints = 0, stylePoints = 0;
  int freeBalls[4]{};
  int styleCount[24]{};
  int ballsLeft = 0;
  int orangeTotal = 0, orangeHit = 0, pegsTotal = 0, pegsCleared = 0;
  int ticks = 0;
  int percentCleared() const { return pegsTotal ? pegsCleared * 100 / pegsTotal : 0; }
  int freeBallTotal() const { return freeBalls[0] + freeBalls[1] + freeBalls[2] + freeBalls[3]; }
};

class AIMgr;

class Board {
  friend class AIMgr;

 public:
  bool load(std::shared_ptr<const Level> base, std::shared_ptr<const Level> lv, const BoardConfig& cfg, std::string* err);
  void step();
  void stepCore();
  bool fire(float angle);
  float aimAt(V2 target) const;
  static float clampAngle(float a);
  static int multiplier(int orangeLeft);
  static int tryBonus(int tryNumber);
  V2 pivot() const { return {W * 0.5f, 30.0f}; }
  float armedBallRadius() const;
  float ballRadiusFor(bool fire) const;
  void setPhysics(const PhysicsEdit& e);
  void derivePhysics();
  V2 muzzleArmed(float a) const { return muzzle(a, armedBallRadius()); }
  V2 muzzle(float a, float ballRadius = gun::DEF_RADIUS) const;
  std::vector<V2> guide(float a, int steps = 110) const;
  uint64_t hash() const;
  void pegHit(int oi, bool byPower = false);
  bool setPegType(int oi, int custom);
  int levelIndexOf(int oi) const { return oi >= (int)baseCount_ ? oi - (int)baseCount_ : -1; }
  void startSlow();
  void stopSlow();
  bool levelWon(bool strict) const;
  bool boardClear() const;
  bool challengePassed(bool final) const;
  bool styleConditionsMet() const;
  void applyChallenge();
  void awardStyle(int id, const char* nameOverride = nullptr);
  void pegHitBy(int ball, int peg) { curBall_ = ball; pegHit(peg); curBall_ = -1; }

  BoardConfig cfg;
  int W = 650;
  std::vector<Obj> objs;
  std::vector<Emit> emitters;
  std::vector<Ball> balls;
  Phase phase = Phase::Aim;
  int tick = 0, cur = 0, players = 1;
  int score[4]{}, ballsLeft[4]{};
  int orangeLeft = 0, orangeTotal = 0;
  int shotScore = 0, pegSum = 0, pegsHitShot = 0, shotSteps = 0, greenHits = 0, shotIndex = 0;
  int lastShotScore = 0, lastShotPegs = 0;
  bool fever = false, won = false;
  float gunAngle = 4.712389f, gunTarget = 4.712389f;
  gun::State gunState;
  float lmAim = 4.712389f;
  int wheelLock = 0;
  bool mouseMoved = false;
  int mouseX = 0, mouseY = 0;
  bool clickPending = false, fireHeld = false;
  int fireCooldown = 0;
  void gunMouse(int mx, int my);
  void gunWheel(int notches);
  void gunClick();
  void gunRelease();
  bool flipHeld = false;
  void flipFlippers(bool up);
  static int updatesToPhase(const Mover& m, int cur, float frac);
  void timeBomb();
  static int spinLight(int startDeg, int timer0);
  int spinCode_ = 0;
  void hatInit(Ball& b, bool atOrigin = false);
  int hatBorn_ = 0;
  void polyNotify(Obj& o);
  void polyFlip(Obj& o);
  void addBubbles(const Obj& o, int kind);
  Rod rod;
  bool chainUsed = false;
  int bucketIndex() const;
  bool electroBall() const;
  float rodTargetX(const Obj& bucket) const;
  void rodUpdate();
  void rodMoveX();
  void chainLightning(int pegIdx);
  void hatUpdate(Ball& b);
  static void setPolyActive(Obj& o, bool on);
  void clickInShot();
  float zenAngle = 0;
  int zenNote = 0, zenPct = 0;
  void beginZen();
  void updateZen();
  float zenSearch();
  void gunLogic();
  std::string banner;
  int bannerTimer = 0;
  MTRand rng;
  std::vector<Event> events;
  bool emit = true;
  bool noModHooks = false;
  uint64_t modSig = 0;
  void syncModTypes();
  struct FxRec { Event e; int tick; std::string note; };
  std::vector<FxRec> fxlog;
  uint32_t fxTotal = 0;
  std::string fxNote;
  std::map<std::string, double> modState;
  void ev(Event::Type t, int a = 0, int b = 0, int c = 0) {
    if (mods_::hooks() && !noModHooks) mods_::hooks()->event(*this, (int)t, a, b, c, curBall_);
    if (!emit) { fxNote.clear(); return; }
    events.push_back({t, a, b, c});
    if (fxlog.size() >= 256) fxlog.erase(fxlog.begin(), fxlog.begin() + 128);
    fxlog.push_back({{t, a, b, c}, tick, std::move(fxNote)});
    fxNote.clear();
    fxTotal++;
  }

  enum Power { P_NONE, P_GUIDE, P_FLIPPERS, P_MULTIBALL, P_PYRAMID, P_SPACEBLAST, P_SPOOKY, P_ZEN, P_EXTENDER, P_FLOWER,
               P_NUDGE, P_TIMEBOMB, P_FIREBALL, P_LUCKYSPIN, P_CHAIN };
  static const char* powerName(int id);
  static int masterByName(const std::string& name, bool allowMods = true);
  int master[4]{}, turns[4]{}, spookyCount[4]{}, style[4]{};
  int frames = 0;
  bool slowOn = false, slowC3 = false;
  int slowAcc = 0, slowSpeed = 0, slowTarget = 0, slowStep = 1, slowHold = 0;
  float zoom = 0;
  int zoomHold = 0;
  bool zoomReq = false;
  int zoomFade = 0;
  V2 zoomPivot;
  void updateZoom();
  bool orangeHit = false, bucketShot = false;
  int startOrange = 0, orangeHitShot = 0;
  uint32_t shotRand[2]{};
  bool guideOn() const { return pturn[cur][P_GUIDE] > 0 || lstate == 10; }
  std::vector<V2> superGuide(float angle, int maxSteps = 700) const;
  struct GuideTrace { std::vector<V2> pts; int hitStep = -1; V2 hit; float hitAng = 1000.0f; bool hitPeg = false; };
  GuideTrace guideTrace(float angle, bool superGuide) const;

  int lstate = 1, stateTick = 0;
  int popCountdown = 0, popSkip = 0, popCount = 0, killTimer = 0;
  bool forceStop = false;
  bool greenFlag = false, extraFlag = false;
  int streak = 0;
  bool jimmyShown = false;
  void jimmyCameo();
  int styleSeq = 0, flipperHits = 0;
  struct HitRec { int tick, type; V2 pos; };
  std::vector<HitRec> hitHist;
  struct Coin { bool on = false, win = false; int angle = 0, age = 0; } coin;
  LevelResult result;
  int firstPrev = -1, firstRun = 0;
  int scoreMult = 1;
  int totalHits = 0;
  int greenRepicks = 0;
  bool feverTallyDone = false;
  int feverBallsPaid = 0;
  int tallyDisp = 0, tallyDelay = 0;
  int tripleTick = 0;
  int pturn[4][masters::kPowerSlots]{};
  bool extenderFlag = false;
  bool flippersFx = false;
  int multiSerial = 0;
  int flowerK = 0;
  int blastNest = 0;
  int slotOutcome[2]{-1, -1};
  bool inFlipperZone(const Obj& o) const;
  void clearFlipperZones();
  void syncTurns();
  void setPyramid(bool on);
  void setPowerActive(int power, bool on);
  void doPowerup(int pegIdx, int power, bool silent);
  void doModPower(int pegIdx, int power, bool silent);
  void modMasterStart();
  void modMasterTurn();
  void modMasterShot();
  bool modMasterInUse() const;
  void doMultiball(Ball* ball, int pegIdx);
  void doSpaceBlast(Ball* ball, int pegIdx);
  void doFlower(Ball* ball, int pegIdx);
  void doLuckySpin(Ball* ball, int pegIdx);
  void finishSlotMachine(int outcome, Ball* ball, int pegIdx);
  int slotMachinePower() const;
  void activatePowers();
  void burnRand(int n) { for (int i = 0; i < n; i++) rng.rand(); }
  bool triplerOn = false, hatOn = false;
  int superCount[4]{};
  int styleReq[24]{};
  float grav_ = 0.05f, gunSpeed_ = 4.0f;
  PhysicsParams physics;
  bool superOn = false, ultraOn = false;
  int triplerCount[4]{}, hatCount[4]{};
  bool shotBanked = false;
  int dispScore[2]{};
  int liveScore(int p) const { return score[p] + ((p == cur && !shotBanked) ? shotScore + style[p] : 0); }
  bool endText = false;
  std::vector<int> orangeSet() const { return orangeSet_; }
  std::vector<int> otherSet() const { return otherSet_; }
  void updateScoreDisplay();
  void addShotScore(int n) { incShotScore(n); }
  std::vector<std::pair<int, int>> contactNotes() const { std::vector<std::pair<int, int>> v; for (auto& n : notes_) v.push_back({n.ball, n.obj}); return v; }
  void noteContact(Ball& b, Obj& o);
  const Mover* subMover(const Mover& m) const;
  void drawCalc(const Obj& o, float frac, float& x, float& y, float& r) const;
  void drawCalc(const Mover& mv, int mtick, float frac, float& x, float& y, float& r) const;
  void emitDrawPos(const Emit& e, float frac, float& x, float& y) const;

 private:
  struct Note { int ball, obj; bool hat = false; };
  void collideHat(const Ball& owner, int bi, const std::vector<int>& cand);
  void hatHit(const Ball& owner, int oi);
  Ball* hatCur_ = nullptr;
  std::shared_ptr<const Level> base_, level_;
  std::vector<int> nonOrange_;
  std::vector<int> orangeSet_, otherSet_;
  int purple_ = -1, popIdx_ = 0, popDelay_ = 0, settle_ = 0;
  std::vector<int> hitList_;
  std::vector<Note> notes_;

  static constexpr int SAP_BALL = 1 << 20;
  struct SapEnd { int id; bool start; };
  std::vector<SapEnd> sapX_, sapY_;
  std::set<std::array<int, 4>> sapPairs_;
  std::array<int, 4> sapKey(int first, int second) const;
  void sapInsert(int first, int second) { sapPairs_.insert(sapKey(first, second)); }
  void sapErase(int first, int second) { sapPairs_.erase(sapKey(first, second)); }
  std::vector<char> sapIn_;
  bool sapAll_ = false;
  int ballUid_ = 0;
  const float* sapBox(int id) const;
  bool sapOverlap(int a, int b) const;
  bool sapBallPairs(int a, int b) const;
  void sapAdd(int id);
  void sapRemove(int id);
  void sapSortList(std::vector<SapEnd>& list, int axis);
  void sapSync();
  void sapSort();

  void addLevel(const Level& L, bool fromBase);
  bool makeObj(const Level& L, const Node& n, Obj& o);
  void widen();
  void rebuild(Obj& o);
  void updateObj(Obj& o);
  void moverStep(Obj& o, float& x, float& y, float& r);
  void moverStep(Mover& m, int mtick, float& x, float& y, float& r);
  void updateEmit(Emit& e);
  Emit makeEmit(const Level& L, const Node& n, bool fromBase);
  void linkFollowers(const Level& L, const std::vector<const Node*>& nodeOf, size_t first, const std::vector<const Node*>& emitNodeOf, size_t emitFirst);
  std::vector<Mover> freeMovers_;
  bool updateBall(Ball& b);
  std::vector<int> gather(const Ball& b) const;
  bool collide(Ball& b, int bi, const std::vector<int>* pre = nullptr, V2* firstHit = nullptr);
  void stepBalls();
  void shotLogic();
  void resolveShot();
  void holeHit(Ball& b, int oi);
  void freeBallHit(Ball& b, const Obj& h);
  void incShotScore(int n);
  void startFever();
  void setupPegs();
  void makeGoalPegs();
  void makeScorePeg();
  void makePowerupPeg();
  void pickPurple() { makeScorePeg(); }
  void pickGreens() { makePowerupPeg(); }
  void shuffle(std::vector<int>& v);
  void say(const std::string& s, int t = 150) { banner = s; bannerTimer = t; }
  int curBall_ = -1;
  void doPower(int pegIdx);
  void spookyRespawn(int bi);
  int trial(Ball tb, int steps, int watchObj, bool* hitWatch);
  bool updateSlow();
  void lookahead();
  bool checkForFever(const Ball& src);
  bool blastCovers(int gi) const;
  bool ghostHits(const Ball& g, const Obj& o) const;
  void beginShotRules();
  void endShot();
  void updateExtender();
  void checkBallStop();
  void removeHitPegs();
  void updatePostShot();
  bool feverTally();
  void updateTotalMiss();
  void updatePostPostShot();
  void beginTurn(bool next);
  void levelDone();
  void eraseFromSets(int oi);
  bool customHit(int oi, bool force);
  void applyPegTypes();
  size_t baseCount_ = 0;
  bool clearedLevel() const;
  void setLState(int s);
  void addBalls(int n, bool fromBucket);
  void styleAtHit(Ball* ball, int oi);
};

}
