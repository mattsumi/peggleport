#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <cstdio>
#include <cctype>

#include "log.h"
#include "pegtypes.h"
#include "sim.h"
#include "sim_internal.h"
#include "physball.h"

namespace pp {

using namespace detail;

namespace {
struct StyleDef {
  int id;
  const char* name;
  int dSolo, dDuel, nSolo, nDuel;
};
constexpr StyleDef kStyle[] = {
    {0, "Long Shot", 25000, 5000, 25000, 5000},       {1, "Flipper Maniac", 25000, 5000, 25000, 5000},
    {2, "Extreme Slide", 50000, 5000, 50000, 5000},   {3, "Orange Attack", 50000, 5000, 50000, 5000},
    {4, "Cool Clear", 50000, 5000, 50000, 5000},      {5, "Multiball Madness", 25000, 5000, 25000, 5000},
    {6, "Hat Trick", 10000, 2500, 10000, 2500},       {7, "Hot Stuff", 25000, 5000, 25000, 5000},
    {8, "Free Ball Frenzy", 100000, 10000, 100000, 10000}, {9, "Free Ball Skills", 5000, 2500, 5000, 2500},
    {10, "Master Style", 25000, 2500, 25000, 2500},   {11, "Kick the Bucket", 25000, 5000, 25000, 5000},
    {12, "Lucky Bounce", 25000, 2500, 25000, 2500},   {13, "Mad Skillz", 25000, 5000, 25000, 5000},
    {14, "Crazy Mad Skillz", 100000, 10000, 100000, 10000}, {15, "Super Long Shot", 50000, 10000, 50000, 10000},
    {16, "Pyramid Pickup", 10000, 5000, 10000, 5000}, {17, "Bucket Bonus", 0, 2500, 0, 2500},
    {18, "", 0, 2500, 0, 2500},                        {19, "Mega Shot Bonus", 0, 2500, 0, 2500},
    {20, "Eye of the Pyramid", -1, -1, 10000, 5000},  {21, "Double Long Shot", -1, -1, 25000, 10000},
    {22, "Off the Wall", -1, -1, 25000, 5000},        {23, "Shock It to Me", -1, -1, 25000, 5000},
};
const char* kMasterStyle[] = {"Double Guide", "Flip Out", "Triple Play", "Pyramid Pileup", "Big Bang", "Spooktacular", "Zen Maniac",
                              "Shot Extender", "Petal to the Metal", "Nudge", "Time Bomb", "Great Balls of Fire", "Spin Again", "Power Surge"};

float dist2(V2 a, V2 b) { return (a - b).len2(); }
}

void Board::shuffle(std::vector<int>& v) {
  int n = (int)v.size();
  if (n < 2) return;
  for (int i = 0; i < 1000; i++) {
    int a = (int)(rng.rand() % (uint32_t)n);
    int b = (int)(rng.rand() % (uint32_t)n);
    std::swap(v[a], v[b]);
  }
}

static void insertSorted(std::vector<int>& v, int x) { v.insert(std::upper_bound(v.begin(), v.end(), x), x); }

void Board::makeGoalPegs() {
  orangeSet_.clear();
  otherSet_.clear();
  std::vector<int> cand;
  for (int i = 0; i < (int)objs.size(); i++) {
    Obj& o = objs[i];
    if (!o.pegType) continue;
    o.lit = false;
    o.litAge = o.flash = o.popTimer = 0;
    o.gone = false;
    o.collidable = o.visible = true;
    if (!o.canBeOrange) {
      if (o.pegType == 2) orangeSet_.push_back(i);
      else otherSet_.push_back(i);
    } else {
      o.pegType = 1;
      cand.push_back(i);
    }
  }
  shuffle(cand);
  int target = cfg.orange > 0 ? cfg.orange : 25;
  while ((int)orangeSet_.size() < target && !cand.empty()) {
    insertSorted(orangeSet_, cand.back());
    cand.pop_back();
  }
  for (int i : cand) insertSorted(otherSet_, i);
  bool onMax = cfg.curLevel >= 0 && cfg.maxLevel == cfg.curLevel;
  for (int i : otherSet_)
    if (objs[i].pegType != 1 && !onMax) objs[i].pegType = 1;
  for (int i : orangeSet_) objs[i].pegType = 2;
  orangeLeft = orangeTotal = (int)orangeSet_.size();
  nonOrange_ = otherSet_;
}

void Board::makeScorePeg() {
  if (cfg.maxLevel < (cfg.nights ? 2 : 3) || !cfg.purple) return;
  std::vector<int> blue;
  for (int i : otherSet_) {
    int t = objs[i].pegType;
    if (t == 3 && i != purple_ && !objs[i].custom) {
      purple_ = i;
      return;
    }
    if (t == 1 && !objs[i].custom) blue.push_back(i);
  }
  if (purple_ >= 0) {
    if (blue.empty()) return;
    Obj& p = objs[purple_];
    if (p.pegType != 2 && !p.lit) p.pegType = 1;
    purple_ = -1;
  }
  if (blue.empty()) return;
  uint32_t pickR = rng.rand();
  purple_ = blue[pickR % (uint32_t)blue.size()];
  objs[purple_].pegType = 3;
}

void Board::makePowerupPeg() {
  if (cfg.maxLevel < (cfg.nights ? 1 : 2) || !cfg.greens) return;
  const float ymax = cfg.nights ? 350.0f : 370.0f;
  V2 prev(-1000.0f, -1000.0f);
  int want = 2 - ((cfg.duel && players > 1) ? 1 : 0);
  std::vector<int> vec;
  int greens = 0;
  for (int i : otherSet_) {
    int t = objs[i].pegType;
    if (t == 4) {
      prev = objs[i].pos;
      greens++;
    } else if (t == 1 && objs[i].pos.y < ymax && !objs[i].custom) {
      vec.push_back(i);
    }
  }
  want -= greens;
  if (want <= 0) return;
  if (vec.empty()) {
    for (int i : otherSet_)
      if (objs[i].pegType == 1 && objs[i].canBeOrange && !objs[i].custom) vec.push_back(i);
    if (vec.empty()) return;
  }
  shuffle(vec);
  const int thr = 150 * 150;
  for (int pass = 0; pass < 2 && want > 0; pass++) {
    for (int k = 0; k < (int)vec.size() && want > 0; k++) {
      int i = vec[k];
      if (objs[i].pegType == 4) continue;
      if (pass == 0) {
        float dx = prev.x - objs[i].pos.x, dy = prev.y - objs[i].pos.y;
        float d2 = dx * dx + dy * dy;
        if (!((float)thr <= d2)) continue;
        prev = objs[i].pos;
      }
      objs[i].pegType = 4;
      want--;
    }
  }
}

void Board::setupPegs() {
  makeGoalPegs();
  makeScorePeg();
  makePowerupPeg();
  if (cfg.ch.active && cfg.ch.noGreens)
    for (auto& o : objs)
      if (o.pegType == 4) o.pegType = 1;
  greenRepicks = (cfg.duel && players > 1) ? 1 : 0;
  if (cfg.duel && players > 1)
    for (auto& o : objs)
      if (o.shape == Shape::Hole && o.id == "fever") o.score /= 2;
  result = LevelResult();
  shotBanked = false;
  dispScore[0] = dispScore[1] = 0;
  result.orangeTotal = orangeTotal;
  for (auto& o : objs) result.pegsTotal += o.pegType != 0;
}

bool Board::boardClear() const {
  return orangeLeft <= 0 && (!cfg.clearLevel || otherSet_.empty());
}

bool Board::levelWon(bool strict) const {
  if (cfg.ch.active && !(cfg.duel && players > 1) && (!strict || !cfg.ch.noForceWin) && challengePassed(false)) return true;
  return boardClear();
}

bool Board::clearedLevel() const { return levelWon(false); }

bool Board::styleConditionsMet() const {
  const ChallengeRules& c = cfg.ch;
  bool required = false, pending = false;
  for (int i = 0; i < 24; i++) {
    if (c.styleShots[i] > 0) required = true;
    if (styleReq[i] > 0) pending = true;
  }
  if (required && pending) return false;
  int unique = 0;
  for (int i = 0; i < 24; i++) unique += result.styleCount[i] > 0;
  if (unique < c.uniqueStyleReq || result.stylePoints < c.styleScoreReq) return false;
  return true;
}

bool Board::challengePassed(bool final) const {
  const ChallengeRules& c = cfg.ch;
  if (!c.active) return false;
  const bool last = c.levelIndex >= c.levelCount - 1;
  bool ok = boardClear() || (c.noForceWin && (final || !c.noEndOnWin));
  if (c.scoreCeil > 0 && last) ok = ok && score[0] < c.scoreCeil;
  if (c.scoreReq > 0 && last) {
    if (!ok || score[0] <= c.scoreReq) return false;
  } else if (!ok) {
    return false;
  }
  return styleConditionsMet();
}

void Board::applyChallenge() {
  const ChallengeRules& c = cfg.ch;
  for (int p = 0; p < 15; p++)
    for (int pl = 0; pl < 2; pl++) pturn[pl][p] += c.powerups[p];
  hatCount[cur] += c.magicHat;
  triplerCount[cur] += c.tripleScore;
  for (int i = 0; i < 24; i++) styleReq[i] = c.styleShots[i];
  derivePhysics();
  if (c.freeballCovered)
    for (auto& o : objs)
      if (o.id == "freeballcover") o.collidable = o.visible = true;
}

bool ChallengeRules::set(const std::string& key, const std::string& value) {
  auto lc = [](std::string t) {
    for (auto& ch : t) ch = (char)std::tolower((unsigned char)ch);
    return t;
  };
  auto trim = [](std::string t) {
    size_t a = t.find_first_not_of(" \t\r\n\""), b = t.find_last_not_of(" \t\r\n\"");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
  };
  const std::string k = lc(key);
  const std::string v = trim(value);
  const bool flag = lc(v) == "true";
  const long long raw = std::strtoll(v.c_str(), nullptr, 10);
  const int n = (int)std::clamp(raw, 0LL, 1000000000LL);
  const int count = (int)std::clamp(raw, 0LL, 1000000LL);
  if (k == "nofreeballs") noFreeballs = flag;
  else if (k == "nogreens") noGreens = flag;
  else if (k == "noforcewin") noForceWin = flag;
  else if (k == "noendonwin") noEndOnWin = flag;
  else if (k == "freeballcovered") freeballCovered = flag;
  else if (k == "scorereq") scoreReq = n;
  else if (k == "scoreceil") scoreCeil = n;
  else if (k == "stylescorereq") styleScoreReq = n;
  else if (k == "uniquestyleshotsreq") uniqueStyleReq = count;
  else if (k == "gravitymod") gravityMod = (float)std::atof(v.c_str());
  else if (k == "projspeedmod") projSpeedMod = (float)std::atof(v.c_str());
  else if (k == "stylershots" || k == "styleshots") {
    size_t pos = 0;
    while (pos <= v.size()) {
      size_t e = v.find(',', pos);
      std::string nm = trim(v.substr(pos, e == std::string::npos ? std::string::npos : e - pos));
      int cnt = 1;
      size_t eq = nm.find('=');
      if (eq != std::string::npos) { cnt = (int)std::clamp(std::strtoll(nm.c_str() + eq + 1, nullptr, 10), 0LL, 1000000LL); nm = trim(nm.substr(0, eq)); }
      std::string want;
      for (char ch : lc(nm)) if (ch != ' ') want.push_back(ch);
      for (int i = 0; i < 24 && !want.empty(); i++) {
        std::string nn;
        for (char ch : lc(kStyle[i].name)) if (ch != ' ') nn.push_back(ch);
        if (nn == want) styleShots[i] = std::min(styleShots[i] + cnt, 1000000);
      }
      if (e == std::string::npos) break;
      pos = e + 1;
    }
  } else if (k == "powerups") {
    static const char* names[] = {"", "guide", "flippers", "multiball", "pyramid", "spaceblast", "spookyball", "zenball", "shotextender",
                                  "flowerpower", "nudge", "timebomb", "fireball", "luckyspin", "chainlightning"};
    size_t pos = 0;
    while (pos <= v.size()) {
      size_t e = v.find(',', pos);
      std::string nm = trim(v.substr(pos, e == std::string::npos ? std::string::npos : e - pos));
      int cnt = 1;
      size_t eq = nm.find('=');
      if (eq != std::string::npos) { cnt = (int)std::clamp(std::strtoll(nm.c_str() + eq + 1, nullptr, 10), 0LL, 1000000LL); nm = trim(nm.substr(0, eq)); }
      const std::string ln = lc(nm);
      if (ln == "magichat") magicHat = cnt;
      else if (ln == "triplescore") tripleScore = cnt;
      else
        for (int i = 1; i < 15; i++)
          if (ln == names[i]) powerups[i] = std::min(powerups[i] + cnt + 1, 1000000);
      if (e == std::string::npos) break;
      pos = e + 1;
    }
  } else {
    return false;
  }
  return true;
}

void Board::eraseFromSets(int oi) {
  auto er = [&](std::vector<int>& v) {
    auto it = std::lower_bound(v.begin(), v.end(), oi);
    if (it != v.end() && *it == oi) v.erase(it);
  };
  if (objs[oi].pegType == 2) er(orangeSet_);
  else er(otherSet_);
}

void Board::jimmyCameo() {
  if (jimmyShown || balls.empty()) return;
  float lo = 100000.0f, hi = -100000.0f;
  for (auto& bl : balls) {
    lo = std::min(lo, bl.pos.x);
    hi = std::max(hi, bl.pos.x);
  }
  if (200.0f <= lo || hi <= 400.0f) {
    ev(Event::Jimmy, (float)(W / 2) > lo ? 1 : 0, (int)shotRand[1]);
    jimmyShown = true;
  }
}

void Board::setPyramid(bool on) {
  for (auto& o : objs) {
    if (o.id != "bumperhole") continue;
    if (o.shape == Shape::Poly) setPolyActive(o, on);
    else o.collidable = o.visible = on;
    if (o.subId == 1) o.collidable = false;
    else o.visible = false;
  }
}

void Board::setLState(int s) {
  lstate = s;
  stateTick = 0;
  setPyramid(s == 2 && pturn[cur][P_PYRAMID] > 0);
  phase = s == 1 || s == 10 ? Phase::Aim : s == 2 ? Phase::Shot : s == 5 ? Phase::Over : Phase::Popping;
}

int Board::tryBonus(int tryNumber) {
  return tryNumber == 1 ? 100000 : tryNumber == 2 ? 50000 : tryNumber == 3 ? 25000 : 0;
}

int Board::multiplier(int n) {
  if (n >= 16) return 1;
  if (n >= 11) return 2;
  if (n >= 7) return 3;
  if (n >= 4) return 5;
  if (n >= 1) return 10;
  return 100;
}

void Board::addBalls(int n, bool fromBucket) {
  ballsLeft[cur] += n;
  if (ballsLeft[cur] < 0) ballsLeft[cur] = 0;
  (void)fromBucket;
}

void Board::incShotScore(int n) {
  int old = shotScore;
  shotScore += n;
  if (clearedLevel()) return;
  static const int th[3] = {25000, 75000, 125000};
  for (int i = 0; i < 3; i++)
    if (th[i] <= shotScore && old < th[i]) {
      if (cfg.ch.active && cfg.ch.noFreeballs && !(cfg.duel && players > 1)) {
      } else if (!(cfg.duel && players > 1)) {
        addBalls(1, false);
        say("Free Ball!", 120);
      } else {
        awardStyle(19);
      }
      if (!(cfg.ch.active && cfg.ch.noFreeballs)) {
        result.freeBalls[1 + i]++;
        ev(Event::ExtraBall, i);
        extraFlag = true;
      }
    }
}

void Board::awardStyle(int id, const char* nameOverride) {
  const StyleDef& d = kStyle[id];
  bool duel = cfg.duel && players > 1;
  int pts = cfg.nights ? (duel ? d.nDuel : d.nSolo) : (duel ? d.dDuel : d.dSolo);
  if (pts < 0) pts = duel ? d.nDuel : d.nSolo;
  if (id == 17 && duel) {
    pts += kStyle[18].dDuel * styleSeq;
    styleSeq++;
  }
  style[cur] += pts;
  result.stylePoints += pts;
  if (id >= 0 && id < 24) result.styleCount[id]++;
  if (cfg.ch.active && id >= 0 && id < 24) {
    styleReq[id]--;
    if (styleReq[id] < 0 && (id == 21 || id == 15) && styleReq[0] > 0) styleReq[0]--;
  }
  std::string nm = nameOverride ? nameOverride : d.name;
  if (id == 10 && !nameOverride && master[cur] >= 1 && master[cur] <= 14) nm = kMasterStyle[master[cur] - 1];
  if (!nm.empty()) say(nm + "! " + std::to_string(pts), 150);
  fxNote = nm;
  ev(Event::StyleShot, id, pts);
}

void Board::pegHit(int oi, bool byPower) {
  Obj& o = objs[oi];
  if (!o.pegType || o.gone) return;
  Ball* ball = hatCur_ ? hatCur_ : (curBall_ >= 0 && curBall_ < (int)balls.size()) ? &balls[curBall_] : nullptr;
  const bool duel = cfg.duel && players > 1;
  if (cfg.nights) {
    if (!byPower && ball && ball->sinceHit == 0 && o.shape == Shape::Brick) {
      if (stateTick - ball->slideT < 20) {
        float s2 = ball->vel.x * ball->vel.x;
        s2 = s2 + ball->vel.y * ball->vel.y;
        float sp = std::sqrt(s2);
        if (!(sp <= 10.0)) sp = 10.0f;
        ball->slideA = 7 - (int)((10.0 - (double)sp) * -0.5);
      }
      ball->slideT = stateTick;
    }
  } else if (!byPower && ball && ball->since == 0 && o.litAge < 20 && o.shape == Shape::Brick) {
    if (stateTick - ball->slideT < 20 && 2.5 < (double)(ball->vel.x * ball->vel.x + ball->vel.y * ball->vel.y)) ball->slideA = 20;
    ball->slideT = stateTick;
  }
  if (o.lit) return;
  if (o.custom && !customHit(oi, byPower)) return;
  totalHits++;
  if (!byPower && ball && ball->slot == 0) {
    hitHist.push_back({stateTick, o.pegType, ball->pos});
    bool skip = fever && o.pegType == 4 && master[cur] == P_MULTIBALL;
    if (!skip && cfg.nights) {
      size_t n = hitHist.size();
      auto longShot = [&](const HitRec& a, const HitRec& b) {
        float d = dist2(a.pos, b.pos);
        if (202500.0f < d) { awardStyle(15); return true; }
        if (40000.0f < d) { awardStyle(0); return true; }
        return false;
      };
      if (n == 1) {
        if (hitHist[0].type != 1 && ball->t188 > 0 && 40 < stateTick - ball->t188) awardStyle(22);
      } else if (n == 2) {
        if (hitHist[0].type != 1 && hitHist[1].type != 1) longShot(hitHist[0], hitHist[1]);
      } else if (n == 3 && hitHist[0].type != 1) {
        if (hitHist[1].type == 1 && hitHist[2].type != 1) {
          if (hitHist[2].tick - hitHist[1].tick < 10) longShot(hitHist[0], hitHist[1]);
        } else if (hitHist[1].type != 1 && hitHist[2].type != 1) {
          if (40000.0f < dist2(hitHist[0].pos, hitHist[1].pos) && 40000.0f < dist2(hitHist[2].pos, hitHist[1].pos)) awardStyle(21);
        }
      }
    } else if (!skip) {
      size_t n = hitHist.size();
      if (n == 2) {
        if (hitHist[0].type != 1 && hitHist[1].type != 1) {
          float d = dist2(hitHist[0].pos, hitHist[1].pos);
          if (202500.0f < d) awardStyle(15);
          else if (40000.0f < d) awardStyle(0);
        }
      } else if (n == 3) {
        if (hitHist[0].type != 1 && hitHist[1].type == 1 && hitHist[2].type != 1 && hitHist[2].tick - hitHist[1].tick < 10) {
          float d = dist2(hitHist[0].pos, hitHist[1].pos);
          if (202500.0f < d) awardStyle(15);
          else if (40000.0f < d) awardStyle(0);
        }
      }
    }
  }
  if (!byPower && ball && ball->flipperTouched) {
    ball->flipperTouched = false;
    if (++flipperHits == 4) awardStyle(1);
  }
  if (!byPower && ball && ball->slideA > 0) {
    int sn = ++ball->slideB;
    if (sn == 12) awardStyle(2);
    else if (sn == 20 || (sn == 16 && shotRand[0] % 100 < 20)) jimmyCameo();
  }
  if (killTimer > 1000) killTimer = 1000;
  int mult = multiplier(orangeLeft);
  if (cfg.clearLevel && orangeLeft <= 0) mult = 10;
  scoreMult = mult;
  o.lit = true;
  o.litAge = 1;
  o.flash = 1;
  if (ball && ball->fire) o.collidable = o.visible = false;
  result.pegsCleared++;
  eraseFromSets(oi);
  if (o.pegType == 2) {
    orangeHit = true;
    orangeLeft--;
    orangeHitShot++;
    int m2 = multiplier(orangeLeft);
    if (cfg.clearLevel && orangeLeft <= 0) m2 = 10;
    if (m2 != mult && m2 < 100) ev(Event::MultiplierUp, m2);
    int R = orangeLeft + orangeHitShot, tgt = R >= 31 ? 15 : R >= 21 ? 10 : R >= 16 ? 9 : R >= 11 ? 8 : R >= 7 ? R - 2 : R >= 5 ? R - 1 : 0;
    if (tgt > 0 && orangeHitShot == tgt) awardStyle(3);
  }
  if (orangeLeft <= 0 && otherSet_.empty() && !duel) {
    result.cleared = true;
    if (cfg.nights) {
      if (hitHist.size() > 1)
        for (const HitRec& r : hitHist)
          if (r.type == 2) { awardStyle(4); break; }
    } else if ((fever || hitHist.size() > 2) && hitHist.size() > 1 && ball) {
      const HitRec& rec = hitHist[hitHist.size() - 2];
      float d = std::sqrt(dist2(rec.pos, ball->pos));
      if (120.0f < d || rec.type != 2 || hitHist.size() > 2) awardStyle(4);
    }
  }
  int base = o.pegType == 2 ? 100 : o.pegType == 3 ? 500 : 10;
  const PegTypeDef* cd = o.custom ? pegTypeDef(o.custom) : nullptr;
  const bool fixedScore = cd && cd->score > 0;
  if (fixedScore) base = cd->score;
  if (!fixedScore && mult >= 100 && base >= 500) base = 100;
  int p = mult * base;
  pegSum += p;
  incShotScore(pegsHitShot * p + pegSum);
  hitList_.push_back(oi);
  pegsHitShot++;
  result.pegsHit++;
  if (o.pegType == 2) result.orangeHit++;
  ev(Event::PegHit, pegsHitShot, shotScore, byPower ? (flowerK > 0 ? 1 + flowerK : 1) : 0);
  ev(Event::PegLit, oi, p | (fever ? 0x40000000 : 0), byPower ? (flowerK > 0 ? 1 + flowerK : 1) : 0);
  if (cd && cd->onClear && g_simHooks && !noModHooks) g_simHooks->pegCustom(*this, oi, 1, curBall_, o.custom);
  if (o.pegType == 3) {
    ev(Event::PointBoost);
    say("Point Boost!", 80);
  }
  if (levelWon(true) && !fever) {
    for (int i = 0; i < (cfg.nights ? 16 : 1086); i++) rng.rand();
    if (balls.size() == 2) awardStyle(5);
    else if (ball && ball->rimTick > 0) awardStyle(11);
    startFever();
  }
  if (orangeLeft <= 0 && otherSet_.empty() && !duel)
    for (auto& h : objs)
      if (h.shape == Shape::Hole && h.id == "fever") h.score = 100000;
  if (o.pegType == 4) {
    greenHits++;
    greenFlag = true;
    doPower(oi);
  }
  if (Ball* cb = (curBall_ >= 0 && curBall_ < (int)balls.size()) ? &balls[(size_t)curBall_] : nullptr; cb && cb->chain) {
    cb->chain = false;
    chainLightning(oi);
  }
}

bool Board::customHit(int oi, bool force) {
  Obj& o = objs[(size_t)oi];
  const PegTypeDef* d = pegTypeDef(o.custom);
  if (!d) return true;
  Ball* ball = hatCur_ ? hatCur_ : (curBall_ >= 0 && curBall_ < (int)balls.size()) ? &balls[(size_t)curBall_] : nullptr;
  const bool hooks = g_simHooks && !noModHooks && d->onHit;
  const bool fire = ball && ball->fire;
  if (!d->clears && force) return false;
  if (d->clears && d->hits <= 1) {
    o.hits = 1;
    o.hitTick = tick;
    if (hooks) g_simHooks->pegCustom(*this, oi, 0, curBall_, o.custom);
    return true;
  }
  const bool direct = force || fire;
  if (!direct && tick - o.hitTick < kPegHitGap) return false;
  o.hitTick = tick;
  if (direct && d->clears) o.hits = d->hits;
  else if (o.hits < 1000000) o.hits++;
  if (hooks) g_simHooks->pegCustom(*this, oi, 0, curBall_, o.custom);
  if (!d->clears) {
    int mult = multiplier(orangeLeft);
    if (cfg.clearLevel && orangeLeft <= 0) mult = 10;
    int base = d->score > 0 ? d->score : (o.pegType == 2 ? 100 : o.pegType == 3 ? 500 : 10);
    incShotScore(mult * base);
    ev(Event::Bump, fire ? 1 : 0);
    return false;
  }
  if (o.hits >= d->hits) return true;
  ev(Event::Bump, fire ? 1 : 0);
  return false;
}

bool Board::setPegType(int oi, int custom) {
  if (oi < 0 || oi >= (int)objs.size()) return false;
  Obj& o = objs[(size_t)oi];
  const PegTypeDef* d = pegTypeDef(custom);
  if (!d || !o.pegType || o.gone || o.lit) return false;
  bool wantOrange;
  if (!d->clears) wantOrange = false;
  else if (d->orange == 1) wantOrange = true;
  else if (d->orange == 0) wantOrange = false;
  else wantOrange = d->base == 2 || (o.pegType == 2 && d->base == 1);
  const int kind = wantOrange ? 2 : (d->base == 2 ? 1 : d->base);
  auto take = [&](std::vector<int>& v) {
    auto it = std::lower_bound(v.begin(), v.end(), oi);
    if (it != v.end() && *it == oi) { v.erase(it); return true; }
    return false;
  };
  const bool wasOrange = std::binary_search(orangeSet_.begin(), orangeSet_.end(), oi);
  const bool wasOther = std::binary_search(otherSet_.begin(), otherSet_.end(), oi);
  if (kind == 2 && !wasOrange && (int)orangeSet_.size() >= kMaxOrangePegs) return false;
  take(orangeSet_);
  take(otherSet_);
  if (wasOrange) { orangeLeft--; orangeTotal--; }
  if (oi == purple_) purple_ = -1;
  o.pegType = kind;
  o.custom = custom;
  o.hits = 0;
  o.hitTick = -1000000;
  if (d->clears && (wasOrange || wasOther || (o.visible && o.collidable))) {
    if (kind == 2) { insertSorted(orangeSet_, oi); orangeLeft++; orangeTotal++; }
    else insertSorted(otherSet_, oi);
  }
  result.orangeTotal = orangeTotal;
  if (o.shape == Shape::Circle && (o.radius0 != 0.0f || d->radiusScale != 1.0f)) {
    if (o.radius0 == 0.0f) o.radius0 = o.radius;
    o.radius = o.radius0 * d->radiusScale;
    rebuild(o);
    if ((size_t)oi < sapIn_.size() && sapIn_[(size_t)oi]) {
      sapRemove(oi);
      sapIn_[(size_t)oi] = 0;
    }
  }
  return true;
}

void Board::syncModTypes() {
  modSig = g_modTypeSig;
  for (size_t i = 0; i < objs.size(); i++) {
    Obj& o = objs[i];
    if (!o.custom) continue;
    o.custom = 0;
    o.hits = 0;
    o.hitTick = -1000000;
    if (o.shape == Shape::Circle && o.radius0 != 0.0f && o.radius != o.radius0) {
      o.radius = o.radius0;
      rebuild(o);
      if (i < sapIn_.size() && sapIn_[i]) {
        sapRemove((int)i);
        sapIn_[i] = 0;
      }
    }
  }
  bool dropped = false;
  for (int p = 0; p < 4; p++) {
    if (master[p] < masters::kFirstPower) continue;
    master[p] = 0;
    for (int k = masters::kFirstPower; k < masters::kPowerSlots; k++) pturn[p][k] = 0;
    dropped = true;
  }
  if (dropped) syncTurns();
}

void Board::applyPegTypes() {
  for (const auto& [n, key] : cfg.pegTypes) {
    const int custom = findPegType(key);
    if (!custom) {
      pp::log::warn("game", "this level uses the peg type '" + key + "' but no loaded mod provides it; those pegs stay normal");
      continue;
    }
    if (n < 0) continue;
    const size_t oi = baseCount_ + (size_t)n;
    if (oi < objs.size()) setPegType((int)oi, custom);
  }
}

void Board::holeHit(Ball& b, int oi) {
  Obj& h = objs[oi];
  if (b.holeOut > 0 || b.holeCooldown > 0) return;
  h.flash = 100;
  incShotScore(h.score);
  if (h.exitIdx >= 0) {
    const Obj& e = objs[h.exitIdx];
    ev(Event::HoleIn, oi, (int)(&b - balls.data()));
    b.holeCooldown = 20;
    if (h.outDelay < 1) b.pos = b.prev = holeExitPoint(e);
    else {
      b.holeOut = h.outDelay;
      b.holeExit = h.exitIdx;
      b.holeTarget = holeExitPoint(e);
    }
    return;
  }
  if (h.id == "freeball") {
    freeBallHit(b, h);
  } else if (h.id == "fever") {
    ev(Event::FeverSlot, h.score, 2 + oi);
    say("Fever bonus +" + std::to_string(h.score), 300);
    if (b.spooky) {
      spookyRespawn((int)(&b - balls.data()));
      return;
    }
  }
  b.alive = false;
}

void Board::freeBallHit(Ball& b, const Obj& h) {
  const bool duel = cfg.duel && players > 1;
  const bool noFree = cfg.nights && cfg.ch.active && cfg.ch.noFreeballs;
  if (pegsHitShot == 0 && duel && !noFree) {
    say("Total Miss - No Free Ball", 200);
    return;
  }
  int lbThr = b.rimCount >= 1 ? 50 : 150;
  if (cfg.nights && H - b.rimMaxY > 125) lbThr = (int)((float)lbThr * 0.4f);
  bool skill = false;
  if (pegsHitShot == 1 && b.slot == 0) skill = true;
  else if (hitHist.size() == 2 && b.slot == 0 && hitHist[1].tick - hitHist[0].tick < 20 && stateTick - hitHist[1].tick < 50) skill = true;
  bool plain = true, seq = false;
  bool chain = false;
  if (!skill && (b.slot != 0 || pegsHitShot < 1)) {
    chain = true;
  } else {
    streak = std::abs(streak) + 1;
    if (streak % 10 != 0) {
      seq = true;
      chain = true;
    } else {
      awardStyle(14);
      plain = false;
    }
  }
  if (chain) {
    int fireCount = pturn[cur][P_FIREBALL] > 0 ? 1 : 0;
    if (b.hat && pegsHitShot >= 1) { awardStyle(6); plain = false; }
    else if (b.coverHit && fireCount >= 1) { awardStyle(7); plain = false; }
    else if (bucketShot) { awardStyle(8); plain = false; }
    else if (b.rimTick >= 1 && pegsHitShot >= 1 && stateTick - b.rimTick > lbThr) { awardStyle(12); plain = false; }
    else if (seq && streak == 5) { awardStyle(13); plain = false; }
    else if (skill) { awardStyle(9); plain = false; }
  }
  if (duel) { awardStyle(17); plain = false; }
  if (b.touchedBumper) awardStyle(16);
  else if (cfg.nights && pturn[cur][P_PYRAMID] > 0 && pegsHitShot > 0) awardStyle(20);
  if (noFree) {
    ev(Event::FreeBall, 2);
    return;
  }
  if (plain) say(b.touchedBumper ? "Free Ball! (bumper)" : "Free Ball!", 120);
  bucketShot = true;
  result.freeBalls[0]++;
  if (!duel) addBalls(1, true);
  ev(Event::FreeBall, plain ? 1 : 0);
  (void)h;
}

void Board::noteContact(Ball& b, Obj& o) {
  if (o.id == "bumperhole") {
    b.touchedBumper = true;
  } else if (o.id == "freeball") {
    if (15.0f < b.pos.x && b.pos.x < (float)W - 15.0f) {
      if (b.rimTick < 1) b.rimTick = stateTick;
      else if (stateTick - b.rimTick > 9) b.rimCount++;
    }
  } else if (o.id == "flipper") {
    b.flipperTouched = true;
    if (killTimer > 500) killTimer = 500;
  } else if (o.id == "freeballcover" && b.fire) {
    b.coverHit = true;
    if (!(cfg.nights && cfg.ch.active && cfg.ch.freeballCovered)) {
      o.collidable = false;
      o.visible = false;
    }
  } else if (cfg.nights && o.id == "wall") {
    if (b.t188 <= 0) b.t188 = stateTick;
  }
}

void Board::startFever() {
  if (fever) return;
  fever = true;
  tallyDisp = tallyDelay = 0;
  feverBallsPaid = 0;
  for (auto& o : objs) {
    if (o.id == "fever") {
      o.collidable = true;
      o.visible = true;
      const int n = o.hasMover ? updatesToPhase(o.mv, o.mtick, 0.5f) : 0;
      if (n > 0) {
        o.canMove = true;
        o.moveCount = n;
      }
    } else if (o.id == "freeball" || o.id == "freeballcover" || o.id == "bumperhole") {
      o.collidable = false;
      o.visible = false;
    }
  }
  say("EXTREME FEVER", 200);
  ev(Event::Fever, (orangeLeft <= 0 && otherSet_.empty()) ? 1 : 0);
  if (!slowOn) startSlow();
}

void Board::beginShotRules() {
  setLState(2);
  killTimer = 0;
  forceStop = false;
  hitHist.clear();
  popCountdown = popSkip = popCount = 0;
  hitList_.clear();
  popIdx_ = 0;
  orangeHit = bucketShot = false;
  greenFlag = false;
  coin.on = false;
  result.shots++;
}

void Board::removeHitPegs() {
  int pending = (int)hitList_.size() - popIdx_;
  if (pending == 0 && popSkip < 1) return;
  int n;
  if (popSkip < 1) {
    int oi = hitList_[popIdx_++];
    Obj& p = objs[oi];
    if (p.visible && !p.gone && p.popTimer == 0) p.popTimer = 4;
    ev(Event::PegPop, oi);
    n = pending - 1;
  } else {
    n = pending + popSkip;
    popSkip--;
  }
  if (n > 0) popCountdown = std::clamp(8 - n / 10, 1, 5);
  popCount++;
  if (killTimer > 500) killTimer = 500;
  for (auto& b : balls) b.stuck = 0;
}

void Board::checkBallStop() {
  if (popCountdown != 0) return;
  if (!forceStop) {
    for (auto& b : balls)
      if (b.stuck < 50) return;
  } else {
    forceStop = false;
  }
  for (auto& b : balls) b.stuck = 0;
  int pending = (int)hitList_.size() - popIdx_;
  if (pending == 0) {
    balls.clear();
    ev(Event::BallLost);
    return;
  }
  popCount = pegsHitShot - pending;
  popSkip = 0;
  removeHitPegs();
}

void Board::endShot() {
  stopSlow();
  if (extenderFlag && !fever) {
    say("Shot Extender!", 200);
    extenderFlag = false;
    if (ballsLeft[cur] == 0 || (shotScore >= 25000 && !(cfg.duel && players > 1))) addBalls(1, false);
    setLState(7);
    return;
  }
  setLState(3);
  endText = (pegSum != 0 || (cfg.duel && players > 1)) && !fever;
  result.pegsCleared = 0;
  for (const Obj& o : objs)
    if (o.pegType != 0 && (o.lit || !o.visible)) result.pegsCleared++;
  if (clearedLevel()) {
    for (int i : otherSet_) hitList_.push_back(i);
    otherSet_.clear();
    for (int i = 0; i < (int)objs.size(); i++) {
      const Obj& o = objs[(size_t)i];
      const PegTypeDef* d = o.custom ? pegTypeDef(o.custom) : nullptr;
      if (d && !d->clears && o.pegType && !o.gone && !o.lit && o.visible) hitList_.push_back(i);
    }
  }
  int pending = (int)hitList_.size() - popIdx_;
  if (pegsHitShot > 0) {
    popCount = 0;
    if (!orangeHit && cfg.duel && players > 1) {
      popCountdown = 25;
      popCount = pegsHitShot - pending;
    } else {
      popSkip = std::max(0, pegsHitShot - pending);
      popCountdown = 50;
    }
  }
}

bool Board::feverTally() {
  const bool duel = cfg.duel && players > 1;
  const int showAt = 70, doneAt = 120;
  if (stateTick < 2) {
    if (hitList_.size() != (size_t)popIdx_) {
      stateTick = 0;
      return true;
    }
    stateTick = 2;
    return true;
  }
  if (stateTick == showAt) endText = true;
  if (stateTick <= doneAt) return true;
  if (tallyDelay > 0) {
    tallyDelay--;
    return true;
  }
  auto canPay = [&]() { return ballsLeft[cur] - feverBallsPaid > 0; };
  if (tallyDisp < shotScore) {
    int diff = shotScore - tallyDisp;
    int step = diff > 100000 ? 5000 : diff > 10000 ? 1000 : diff > 1000 ? 200 : diff > 100 ? 100 : 10;
    tallyDisp = std::min(tallyDisp + step, shotScore);
    ev(Event::FeverSlot, tallyDisp, 0);
    if (shotScore <= tallyDisp && canPay()) tallyDelay = 20;
    return true;
  }
  if (canPay()) {
    shotScore += duel ? 5000 : 10000;
    tallyDisp = shotScore;
    tallyDelay = 10;
    feverBallsPaid++;
    ev(Event::FeverSlot, duel ? 5000 : 10000, 1);
    return true;
  }
  return false;
}

void Board::updatePostShot() {
  const bool duel = cfg.duel && players > 1;
  bool busy = fever ? feverTally() : false;
  if (popCountdown != 0) return;
  if (busy) return;
  if (triplerOn && tripleTick == 0 && (!duel || orangeHit)) tripleTick = 1;
  if (tripleTick > 0 && tripleTick < 125) {
    tripleTick++;
    if (tripleTick == 75 && endText) {
      incShotScore(shotScore * 2);
      tallyDisp = shotScore;
      ev(Event::Power, 100 + 2);
    }
    return;
  }
  if (!orangeHit && duel) {
    ev(Event::Penalty, pegsHitShot < 1 ? 0 : 30);
    int n = (int)std::floor((double)((float)(score[cur] * 0.25 / 5.0)));
    score[cur] = std::max(0, score[cur] - 5 * n);
    shotScore = 0;
    style[cur] = 0;
  } else if (shotScore < 1 && pegsHitShot == 0 && !duel && !bucketShot && !(cfg.ch.active && cfg.ch.noFreeballs)) {
    const bool win = cfg.legacyMissCoin()
        ? !cfg.coinFlipSeen || shotRand[1] % 100u <= (ballsLeft[cur] < 1 ? 80u : 60u)
        : (shotRand[1] & 1u) != 0;
    coin.on = true;
    coin.win = win;
    coin.angle = (int)(rng.rand() % 360);
    coin.age = 0;
    ev(Event::CoinSpin, win ? 1 : 0, coin.angle);
    setLState(4);
    return;
  }
  int banked = shotScore + style[cur];
  score[cur] += banked;
  shotBanked = true;
  lastShotScore = shotScore;
  lastShotPegs = pegsHitShot;
  if (fever) {
    result.feverPoints = shotScore;
    result.bestFever = shotScore;
  } else {
    result.shotPoints += shotScore;
    result.bestShot = std::max(result.bestShot, shotScore);
  }
  ev(Event::ShotEnd, shotScore, style[cur]);
  setLState(6);
}

void Board::updateTotalMiss() {
  if (coin.on) {
    int prev = coin.angle;
    coin.age++;
    coin.angle = (coin.angle + 9) % 360;
    if (90 - 3 < coin.angle && coin.angle < 90 + 3) coin.angle = 93;
    else if (270 - 3 < coin.angle && coin.angle < 270 + 3) coin.angle = 273;
    if (250 - coin.age < 150) {
      if (coin.win) {
        if (coin.angle < prev) coin.angle = -coin.age;
      } else if (prev < 181 && 179 < coin.angle) {
        coin.angle = -coin.age;
      }
    }
    if (coin.angle >= 0) return;
  }
  coin.on = false;
  bool win = coin.win;
  ev(Event::CoinFlip, win ? 1 : 0);
  if (!win) {
    beginTurn(true);
  } else {
    addBalls(1, false);
    say("Free Ball!", 120);
    setLState(6);
  }
}

void Board::updatePostPostShot() {
  const bool duel = cfg.duel && players > 1;
  if (!duel || stateTick > 0x95) {
    if (clearedLevel()) {
      shotIndex++;
      levelDone();
    } else {
      beginTurn(true);
    }
  }
}

void Board::updateScoreDisplay() {
  for (int p = 0; p < 2; p++) {
    int d = dispScore[p], t = score[p];
    if (d == t) continue;
    if (d < t) {
      int diff = t - d;
      d += lstate == 5 ? 5000 : diff < 0x65 ? 10 : diff < 0x3e9 ? 100 : diff < 0x2711 ? 200 : diff < 0x186a1 ? 1000 : 5000;
      if (t < d) d = t;
    } else {
      int diff = d - t;
      if (diff > 100000) d -= 5000;
      d += diff < 0x65 ? -10 : diff < 0x3e9 ? -100 : diff < 0x2711 ? -200 : -1000;
      if (d < t) d = t;
    }
    dispScore[p] = d;
  }
}

void Board::levelDone() {
  setLState(5);
  if (cfg.ch.active && !(cfg.duel && players > 1)) won = (boardClear() || cfg.ch.noForceWin) ? challengePassed(true) : false;
  else won = clearedLevel();
  result.finished = true;
  result.won = won;
  result.totalScore = score[0];
  result.ballsLeft = ballsLeft[cur];
  result.ticks = tick;
  if (won) say("LEVEL CLEARED", 100000);
  else say(cfg.duel && players > 1 ? "GAME OVER" : "OUT OF BALLS", 100000);
  ev(Event::LevelDone, won ? 1 : 0);
}

void Board::updateExtender() {
  killTimer = 0;
  flipperHits = 0;
  orangeHitShot = 0;
  forceStop = false;
  shotSteps = 0;
  setLState(1);
}

void Board::beginTurn(bool next) {
  const bool duel = cfg.duel && players > 1;
  shotIndex++;
  bool any = false;
  for (int p = 0; p < players; p++) any |= ballsLeft[p] > 0;
  if (!any) {
    levelDone();
    return;
  }
  if (flipHeld) {
    if (pturn[cur][P_FLIPPERS] > 0) flipFlippers(false);
    flipHeld = false;
  }
  if (duel && next) {
    int nx = (cur + 1) % players;
    while (ballsLeft[nx] <= 0) nx = (nx + 1) % players;
    cur = nx;
  } else if (!duel && ballsLeft[cur] <= 0) {
    levelDone();
    return;
  }
  streak = (!duel && streak >= 0) ? -streak : 0;
  shotScore = pegSum = 0;
  shotBanked = false;
  style[cur] = 0;
  popCountdown = popSkip = popCount = 0;
  orangeHit = bucketShot = false;
  extraFlag = false;
  feverTallyDone = false;
  feverBallsPaid = 0;
  tallyDelay = 0;
  tripleTick = 0;
  greenHits = 0;
  orangeHitShot = 0;
  superOn = ultraOn = false;
  triplerOn = hatOn = false;
  activatePowers();
  if (cfg.nights && pturn[cur][P_CHAIN] > 0) {
    chainUsed = true;
    ev(Event::ChainStart);
  }
  if (pegsHitShot > 0) {
    pegsHitShot = 0;
    makeScorePeg();
  }
  if (greenFlag) {
    greenFlag = false;
    if (greenRepicks > 0) {
      greenRepicks--;
      makePowerupPeg();
    }
  }
  shotSteps = 0;
  hitList_.clear();
  popIdx_ = 0;
  killTimer = 0;
  flipperHits = 0;
  jimmyShown = false;
  forceStop = false;
  setLState(1);
}

void Board::shotLogic() {
  if (lstate == 10 && phase == Phase::Aim) {
    stateTick++;
    updateZen();
    return;
  }
  if (phase == Phase::Aim || phase == Phase::Over) return;
  stateTick++;
  if (popCountdown != 0 && --popCountdown == 0) removeHitPegs();
  if (phase == Phase::Shot) {
    killTimer++;
    if (killTimer >= 1500 && (killTimer - 1500) % 700 == 0) {
      forceStop = true;
      killTimer = 500;
    }
    bool have = !balls.empty();
    if (have) checkBallStop();
    if (!have && popCountdown == 0) endShot();
    return;
  }
  switch (lstate) {
    case 4: updateTotalMiss(); break;
    case 6: updatePostPostShot(); break;
    case 7: updateExtender(); break;
    default: updatePostShot(); break;
  }
}

void Board::resolveShot() {}

const char* Board::powerName(int id) {
  static const char* n[] = {"", "SuperGuide!", "Flippers!", "Multiball!", "Pyramid!", "SpaceBlast!", "SpookyBall!", "ZenShot!",
                            "ShotExtender!", "FlowerPower!", "Nudge!", "TimeBomb!", "Fireball!", "LuckySpin!", "Electrobolt!"};
  if (id >= 0 && id <= 14) return n[id];
  if (const ModMaster* m = masters::byPower(id)) return m->banner.c_str();
  return "";
}

int Board::masterByName(const std::string& in, bool allowMods) {
  if (allowMods)
    if (const ModMaster* m = masters::byName(in)) return m->power;
  if (allowMods && in.find(':') != std::string::npos) return 0;
  std::string n;
  for (char c : in) n.push_back((char)std::tolower((unsigned char)c));
  struct M { const char* name; int power; };
  static const M t[] = {{"bjorn", 1}, {"claude", 2}, {"jimmy", 3}, {"kat", 4}, {"tut", 4}, {"splork", 5}, {"renfield", 6}, {"hu", 7},
                        {"tula", 9}, {"cinder", 12}, {"lord", 12}, {"warren", 13}, {"marina", 14}, {"kalah", 14}};
  for (auto& m : t)
    if (n.find(m.name) != std::string::npos) return m.power;
  const int v = std::atoi(n.c_str());
  if (v >= masters::kFirstPower) return allowMods && masters::byPower(v) ? v : 0;
  return v;
}


void Board::syncTurns() {
  for (int p = 0; p < 4; p++) {
    turns[p] = (master[p] > 0 && master[p] < masters::kPowerSlots) ? pturn[p][master[p]] : 0;
    spookyCount[p] = pturn[p][P_SPOOKY];
  }
}

static bool inPoly4(const float (*pt)[2], float px, float py) {
  int n = 0;
  for (int i = 0; i < 4; i++) {
    float x1 = pt[i][0], y1 = pt[i][1], x2 = pt[(i + 1) % 4][0], y2 = pt[(i + 1) % 4][1];
    bool cross = (y1 != y2) && ((y1 <= py && py < y2) || (y2 <= py && py < y1));
    if (cross && ((py - y1) * (x2 - x1)) / (y2 - y1) + x1 > px) n++;
  }
  return (n & 1) != 0;
}

static bool inPolyN(const float (*pt)[2], int n, float px, float py) {
  int c = 0;
  for (int i = 0; i < n; i++) {
    float x1 = pt[i][0], y1 = pt[i][1], x2 = pt[(i + 1) % n][0], y2 = pt[(i + 1) % n][1];
    bool cross = (y1 != y2) && ((y1 <= py && py < y2) || (y2 <= py && py < y1));
    if (cross && ((py - y1) * (x2 - x1)) / (y2 - y1) + x1 > px) c++;
  }
  return (c & 1) != 0;
}

bool Board::inFlipperZone(const Obj& o) const {
  if (o.zoneMode == 1) return true;
  if (o.zoneMode == 2) return false;
  const bool legacy = cfg.legacyFlippers();
  const float x = legacy && !o.fromBase ? o.pos.x - (W - 650) * 0.5f : o.pos.x;
  const float rightX = legacy ? x : x - (W - 650);
  const float y = o.pos.y;
  if (cfg.nights) {
    static const float NA[5][2] = {{0, 448}, {50, 460}, {92, 470}, {95, 523}, {0, 530}};
    static const float NB[5][2] = {{644, 450}, {600, 455}, {558, 460}, {555, 523}, {644, 530}};
    if (400.0 <= (double)y) {
      if (x < 150.0f && inPolyN(NA, 5, x, y)) return true;
      if (500.0 < (double)rightX && inPolyN(NB, 5, rightX, y)) return true;
    }
    return false;
  }
  static const float A[4][2] = {{0, 380}, {110, 470}, {110, 600}, {0, 600}};
  static const float B[4][2] = {{650, 380}, {540, 470}, {540, 600}, {650, 600}};
  if (400.0f <= y) {
    if (x < 150.0f && (x < 80.0f || inPoly4(A, x, y))) return true;
    if (500.0f < rightX && (570.0f < rightX || inPoly4(B, rightX, y))) return true;
  }
  return false;
}

void Board::clearFlipperZones() {
  int removedOrange = 0;
  for (int i = 0; i < (int)objs.size(); i++) {
    Obj& o = objs[i];
    if (!o.pegType || !o.visible || !inFlipperZone(o)) continue;
    o.visible = false;
    o.collidable = false;
    auto er = [&](std::vector<int>& v) {
      auto it = std::lower_bound(v.begin(), v.end(), i);
      if (it != v.end() && *it == i) { v.erase(it); return true; }
      return false;
    };
    er(otherSet_);
    if (er(orangeSet_)) { orangeLeft--; removedOrange++; }
  }
  for (int i = 0; i < (int)objs.size() && (int)otherSet_.size() < removedOrange; i++) {
    Obj& o = objs[i];
    if (o.pegType && !o.visible && !o.lit && !o.gone && o.canBeOrange && !inFlipperZone(o)) {
      o.visible = o.collidable = true;
      insertSorted(otherSet_, i);
    }
  }
  std::vector<int> vec;
  for (int i : otherSet_)
    if (!objs[(size_t)i].custom) vec.push_back(i);
  shuffle(vec);
  removedOrange = std::min(removedOrange, (int)vec.size());
  for (int k = 0; k < removedOrange; k++) {
    int i = vec[(size_t)k];
    objs[i].zoneMode = 2;
    objs[i].pegType = 2;
    otherSet_.erase(std::lower_bound(otherSet_.begin(), otherSet_.end(), i));
    insertSorted(orangeSet_, i);
    orangeLeft++;
  }
}

void Board::setPowerActive(int power, bool on) {
  if (power == P_FLIPPERS && !on) flipFlippers(false);
  if (power == P_FLIPPERS && on && !flippersFx) {
    flippersFx = true;
    clearFlipperZones();
  }
  for (auto& o : objs) {
    if (o.id == "flipper" && power == P_FLIPPERS) {
      setPolyActive(o, on);
      if (on && lstate == 2 && o.shape == Shape::Poly) o.moveCount = 180;
    }
  }
}

void Board::doMultiball(Ball* ball, int pegIdx) {
  Obj& peg = objs[pegIdx];
  if (!ball) return;
  float dx = (float)((double)peg.pos.x - (double)ball->pos.x);
  const float spd = cfg.nights ? 0.75f : 0.5f;
  float vx = spd;
  if (dx < 0.0f || (dx == 0.0f && (shotRand[0] & 1u) == 0u)) vx = -spd;
  Ball nb;
  nb.r = ballRadiusFor(false);
  nb.slot = 100 + multiSerial++;
  phys::ballSetPos(nb, peg.pos.x, peg.pos.y);
  nb.pos = nb.prev = peg.pos;
  nb.vel = {vx, cfg.nights ? -3.5f : -3.0f};
  if (ball->hat) hatInit(nb);
  balls.push_back(nb);
  peg.collidable = false;
  peg.visible = false;
  if (fever && balls.size() == 2) awardStyle(5);
}

void Board::doSpaceBlast(Ball* ball, int pegIdx) {
  const V2 c = objs[pegIdx].pos;
  blastNest++;
  burnRand(300);
  const float r2 = 100.0f * 100.0f;
  (void)ball;
  for (int i = 0; i < (int)objs.size(); i++) {
    Obj& o = objs[i];
    if (!o.pegType || o.lit || o.gone || !o.visible) continue;
    float dx = o.pos.x - c.x, dy = o.pos.y - c.y;
    if (dx * dx + dy * dy <= r2) pegHit(i, true);
  }
  blastNest--;
}

void Board::doFlower(Ball* ball, int pegIdx) {
  (void)ball;
  const V2 c = objs[pegIdx].pos;
  std::vector<std::pair<float, int>> m;
  for (int i = 0; i < (int)objs.size(); i++) {
    const Obj& o = objs[i];
    if (o.pegType == 2 && !o.lit && !o.gone && o.visible) {
      float dx = o.pos.x - c.x, dy = o.pos.y - c.y;
      m.push_back({dx * dx + dy * dy, i});
    }
  }
  std::stable_sort(m.begin(), m.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first < b.first; });
  float prod = (float)((double)orangeLeft * 0.20000000298023224);
  int n = (int)std::ceil((double)prod);
  if (cfg.nights && n < 2 && orangeLeft > 1) n = 2;
  for (int k = 0; k < (int)m.size() && k < n; k++) {
    flowerK = k + 1;
    pegHit(m[(size_t)k].second, true);
    flowerK = 0;
    burnRand(200);
  }
}

int Board::slotMachinePower() const {
  static const int deluxe[10] = {P_GUIDE, P_MULTIBALL, P_PYRAMID, P_SPACEBLAST, P_FLIPPERS, P_SPOOKY, P_FLOWER, P_LUCKYSPIN, P_FIREBALL, P_ZEN};
  std::vector<int> pool;
  int n = cfg.masterIndex + 1;
  for (int i = 0; i < n && i < (cfg.nights ? 11 : 10); i++) {
    int pw = i < 10 ? deluxe[i] : P_CHAIN;
    if (pw != P_LUCKYSPIN) pool.push_back(pw);
  }
  if (pool.empty()) pool.push_back(P_GUIDE);
  uint32_t r = greenHits == 1 ? shotRand[1] : shotRand[0];
  return pool[r % pool.size()];
}

void Board::doLuckySpin(Ball* ball, int pegIdx) {
  int outcome;
  spinCode_ = 0;
  if (cfg.liveSpin) {
    static const int kResults[4] = {2, 1, 0, 3};
    int start = (int)(rng.rand() % 360u), t0 = (int)(rng.rand() % 100u);
    outcome = kResults[spinLight(start, t0)];
    spinCode_ = 1 + start + 360 * t0;
  } else {
    outcome = (int)(rng.rand() & 3u);
  }
  slotOutcome[greenHits == 1 ? 0 : 1] = outcome;
  if (cfg.nights && outcome == 3) outcome = 1;
  switch (outcome) {
    case 1:
      hatCount[cur]++;
      if (hatOn) hatCount[cur]++;
      hatOn = true;
      break;
    case 2:
      triplerCount[cur]++;
      if (triplerOn) triplerCount[cur]++;
      triplerOn = true;
      break;
    default: break;
  }
  ev(Event::Power, 200 + outcome);
  if (outcome != 0) ev(Event::LuckySpin, outcome, 0, spinCode_);
  finishSlotMachine(outcome, ball, pegIdx);
}

void Board::finishSlotMachine(int outcome, Ball* ball, int pegIdx) {
  if (outcome == 1) {
    if (ball && !ball->hat) hatInit(*ball);
    say("Magic Hat!", 120);
  } else if (outcome == 3) {
    addBalls(1, false);
    result.freeBalls[0]++;
    say("Extra Ball!", 120);
  } else if (outcome == 2) {
    say("Score Tripler!", 120);
  } else {
    const int pick = slotMachinePower();
    ev(Event::LuckySpin, 0, pick, spinCode_);
    doPowerup(pegIdx, pick, true);
  }
}

void Board::doPowerup(int oi, int power, bool silent) {
  const int pw = power ? power : master[cur];
  if (pw >= masters::kFirstPower) {
    doModPower(oi, pw, silent);
    return;
  }
  if (g_simHooks && !noModHooks && g_simHooks->power(*this, oi, pw, silent)) return;
  if (pw < 0 || pw > 14) return;
  Ball* ball = hatCur_ ? hatCur_ : curBall_ >= 0 && curBall_ < (int)balls.size() ? &balls[(size_t)curBall_] : nullptr;
  int& counter = pturn[cur][pw];
  const int first = counter == 0 ? 1 : 0;
  const int greensNow = greenHits;
  bool grant = true;
  int g = 2;
  if (pw) {
    say(powerName(pw), 120);
    int pb = 0;
    if (pw == P_SPACEBLAST && blastNest > 0) pb = 1;
    if (pw == P_SPOOKY) pb = counter >= 1 ? 1 : (ball && ball->spookyUsed ? 2 : 0);
    ev(Event::Power, pw, pb);
  }
  switch (pw) {
    case P_GUIDE: g = 3 + first; break;
    case P_FLIPPERS: g = 3; break;
    case P_MULTIBALL: doMultiball(ball, oi); grant = false; break;
    case P_PYRAMID: g = 5; break;
    case P_SPACEBLAST: doSpaceBlast(ball, oi); grant = false; break;
    case P_SPOOKY:
      if (ball) {
        burnRand(1);
        ball->spooky = true;
      }
      g = 1;
      break;
    case P_ZEN: g = 1 + first; break;
    case P_EXTENDER: extenderFlag = true; grant = false; break;
    case P_FLOWER: doFlower(ball, oi); grant = false; break;
    case P_NUDGE:
    case P_TIMEBOMB: g = 1; break;
    case P_FIREBALL: g = 1 + first; break;
    case P_LUCKYSPIN: doLuckySpin(ball, oi); grant = false; break;
    case P_CHAIN: g = 1 + first; break;
    default: break;
  }
  if (greensNow == 2 && !silent) awardStyle(10);
  if (grant) {
    counter += g;
    setPowerActive(pw, true);
  }
  syncTurns();
}

void Board::doPower(int oi) { doPowerup(oi, 0, false); }

bool Board::modMasterInUse() const {
  for (int p = 0; p < 4; p++)
    if (master[p] >= masters::kFirstPower) return true;
  return false;
}

void Board::doModPower(int oi, int pw, bool silent) {
  const ModMaster* mm = masters::byPower(pw);
  if (!mm || pw >= masters::kPowerSlots || mm->trigger != ModMaster::GreenPeg) return;
  const int greensNow = greenHits;
  int& counter = pturn[cur][pw];
  say(mm->banner, 120);
  ev(Event::Power, pw, 0);
  if (greensNow == 2 && !silent) awardStyle(10);
  counter = std::max(counter, mm->turns + 1);
  setPowerActive(pw, true);
  syncTurns();
  if (g_simHooks && !noModHooks) g_simHooks->masterCall(*this, cur, pw, 0, oi);
}

void Board::modMasterStart() {
  for (int p = 0; p < players; p++) {
    const int pw = master[p];
    const ModMaster* mm = pw >= masters::kFirstPower ? masters::byPower(pw) : nullptr;
    if (!mm) continue;
    if (mm->trigger == ModMaster::EveryShot) pturn[p][pw] = 1;
    else if (mm->trigger == ModMaster::LevelStart) pturn[p][pw] = mm->turns;
  }
  syncTurns();
  for (int p = 0; p < players; p++) {
    const int pw = master[p];
    const ModMaster* mm = pw >= masters::kFirstPower ? masters::byPower(pw) : nullptr;
    if (!mm || mm->trigger != ModMaster::LevelStart) continue;
    if (p == cur) {
      say(mm->banner, 120);
      ev(Event::Power, pw, 0);
    }
    if (g_simHooks && !noModHooks) g_simHooks->masterCall(*this, p, pw, 0, -1);
  }
}

void Board::modMasterTurn() {
  const int pw = master[cur];
  const ModMaster* mm = pw >= masters::kFirstPower ? masters::byPower(pw) : nullptr;
  if (!mm) return;
  int& c = pturn[cur][pw];
  if (c > 0) {
    c--;
    if (c == 0 && mm->trigger != ModMaster::EveryShot) {
      ev(Event::PowerEnd, pw);
      if (g_simHooks && !noModHooks) g_simHooks->masterCall(*this, cur, pw, 2, -1);
    }
  }
  if (mm->trigger == ModMaster::EveryShot) c = 1;
  setPowerActive(pw, c != 0);
}

void Board::modMasterShot() {
  const int pw = master[cur];
  const ModMaster* mm = pw >= masters::kFirstPower ? masters::byPower(pw) : nullptr;
  if (!mm || pturn[cur][pw] <= 0 || !g_simHooks || noModHooks) return;
  if (mm->trigger == ModMaster::EveryShot) g_simHooks->masterCall(*this, cur, pw, 0, -1);
  g_simHooks->masterCall(*this, cur, pw, 1, -1);
}

void Board::activatePowers() {
  for (int p = 0; p < (cfg.nights ? 15 : 14); p++) {
    if (pturn[cur][p] > 0) {
      pturn[cur][p]--;
      if (pturn[cur][p] == 0 && p == master[cur]) ev(Event::PowerEnd, p);
    }
    setPowerActive(p, pturn[cur][p] != 0);
  }
  if (master[cur] >= masters::kFirstPower) modMasterTurn();
  if (triplerCount[cur] != 0) { triplerOn = true; triplerCount[cur]--; }
  if (hatCount[cur] != 0) { hatOn = true; hatBorn_ = tick; hatCount[cur]--; }
  if (cfg.nights) {
    if (superCount[cur] > 0) superOn = true;
    if (superCount[cur] > 1) ultraOn = true;
    superCount[cur] = 0;
  }
  syncTurns();
}

void Board::spookyRespawn(int bi) {
  ev(Event::SpookyRespawn, balls[(size_t)bi].spookyUsed ? 2 : (pturn[cur][P_SPOOKY] == 2 ? 1 : 0));
  Ball b = balls[(size_t)bi];
  b.spookyUsed = true;
  if (--pturn[cur][P_SPOOKY] < 1) {
    pturn[cur][P_SPOOKY] = 0;
    b.spooky = false;
  }
  syncTurns();
  float speed = (float)std::sqrt((double)(b.vel.x * b.vel.x + b.vel.y * b.vel.y));
  V2 v = b.vel;
  if (3.0f < speed) v = {(float)((double)b.vel.x * 3.0 / (double)speed), (float)((double)b.vel.y * 3.0 / (double)speed)};
  V2 bestV = v;
  int best = 0;
  for (int tr = 0; tr < 9; tr++) {
    V2 tv = v;
    if (tr > 0) tv.x = (float)(1.0 * (1.0 - (double)(rng.rand() % 2000) / 1000.0) + (double)tv.x);
    Ball tb;
    tb.r = b.r;
    phys::ballSetPos(tb, b.pos.x, 10.0f);
    tb.pos = tb.prev = {b.pos.x, 10.0f};
    tb.vel = tv;
    tb.fire = b.fire;
    int c = trial(tb, tr == 0 ? 400 : 350, -1, nullptr);
    if (best < c) {
      best = c;
      bestV = tv;
    }
  }
  phys::ballSetPos(b, b.pos.x, 10.0f);
  b.pos = b.prev = {b.pos.x, 10.0f};
  b.vel = bestV;
  b.rimTick = b.rimCount = 0;
  b.stuck = 0;
  burnRand(1);
  balls[(size_t)bi] = b;
}

int Board::trial(Ball tb, int steps, int watchObj, bool* hitWatch) {
  std::vector<Note> saved;
  saved.swap(notes_);
  std::vector<int> seen;
  int count = 0;
  for (int i = 0; i < steps; i++) {
    notes_.clear();
    if (!updateBall(tb)) continue;
    collide(tb, -1);
    for (auto& n : notes_) {
      const Obj& o = objs[n.obj];
      if (!o.pegType || o.lit || o.gone) continue;
      if (std::find(seen.begin(), seen.end(), n.obj) == seen.end()) {
        seen.push_back(n.obj);
        count++;
      }
      if (n.obj == watchObj && hitWatch) *hitWatch = true;
    }
    if (hitWatch && *hitWatch) break;
    if ((float)((int)H + 20) < tb.pos.y) break;
  }
  notes_.swap(saved);
  return count;
}

bool Board::blastCovers(int gi) const {
  const float r2 = 100.0f * 100.0f;
  const Obj& g = objs[(size_t)gi];
  auto far = [&](int i) {
    const Obj& o = objs[(size_t)i];
    if (o.lit || o.gone) return false;
    float dx = o.pos.x - g.pos.x, dy = o.pos.y - g.pos.y;
    return r2 < dx * dx + dy * dy;
  };
  for (int i : orangeSet_)
    if (far(i)) return false;
  if (cfg.clearLevel)
    for (int i : otherSet_)
      if (far(i)) return false;
  return true;
}

bool Board::ghostHits(const Ball& g, const Obj& o) const {
  float hitT = 1.0f;
  const float r = g.r + 10.0f;
  V2 hp;
  if (o.shape == Shape::Circle) return phys::circleCheck(o.pos, o.radius, g.pos, g.vel, r, hitT, hp);
  if (o.shape == Shape::Brick || o.shape == Shape::Poly) {
    V2 hn, hv, av;
    bool kiss = true;
    phys::Poly p = o.ppoly;
    return p.check(g.pos, g.vel, r, hitT, hp, hn, hv, av, kiss, cfg.simRules == SimRules::Classic);
  }
  return false;
}

bool Board::checkForFever(const Ball& src) {
  const int nOther = (int)otherSet_.size();
  const int remaining = cfg.clearLevel ? orangeLeft + nOther : orangeLeft;
  int target = -1;
  if (remaining == 1)
    target = !orangeSet_.empty() ? orangeSet_.front() : (!otherSet_.empty() ? otherSet_.front() : -1);
  const int power = master[cur];
  bool greens_ = false;
  if (power == P_SPACEBLAST) greens_ = true;
  else if (power == P_FLOWER) greens_ = cfg.clearLevel ? (orangeLeft == 1 && nOther < 2) : remaining == 1;
  int greens[2] = {-1, -1}, ng = 0;
  if (greens_)
    for (int i : otherSet_) {
      const Obj& o = objs[(size_t)i];
      if (o.pegType == 4 && !o.lit && !o.gone && (power != P_SPACEBLAST || blastCovers(i))) {
        greens[ng++] = i;
        if (ng == 2) break;
      }
    }
  if (target < 0 && ng == 0) return false;
  Ball g;
  g.r = src.r;
  phys::ballSetPos(g, src.pos.x, src.pos.y);
  g.pos = g.prev = src.pos;
  g.vel = src.vel;
  g.fire = src.fire;
  std::vector<Note> saved;
  saved.swap(notes_);
  bool hit = false;
  for (int step = 0; step < 4 && !hit; step++) {
    notes_.clear();
    if (updateBall(g)) collide(g, -1);
    if ((float)((int)H + 20) < g.pos.y) break;
    if (target >= 0 && ghostHits(g, objs[(size_t)target])) hit = true;
    for (int k = 0; k < ng && !hit; k++)
      if (ghostHits(g, objs[(size_t)greens[k]])) hit = true;
  }
  notes_.swap(saved);
  return hit;
}

void Board::lookahead() {
  if (fever || slowOn || phase != Phase::Shot) return;
  const int nOther = (int)otherSet_.size();
  const int remaining = cfg.clearLevel ? orangeLeft + nOther : orangeLeft;
  const int power = master[cur];
  bool gate = false;
  if (remaining == 1) gate = true;
  else if (power == P_FLOWER) gate = cfg.clearLevel && orangeLeft == 1 && nOther == 1;
  else if (power == P_SPACEBLAST) gate = remaining <= 5;
  if (!gate || balls.size() >= 4) return;
  for (size_t bi = 0; bi < balls.size(); bi++)
    if (checkForFever(balls[bi])) {
      startSlow();
      return;
    }
}

void Board::startSlow() {
  slowOn = true;
  slowC3 = false;
  slowAcc = 0;
  slowSpeed = 100;
  slowTarget = 500;
  slowStep = 1;
  slowHold = 10000;
  zoomReq = true;
}

void Board::stopSlow() {
  slowOn = false;
  slowC3 = false;
  zoomReq = false;
  slowAcc = slowHold = slowTarget = 0;
}

bool Board::updateSlow() {
  if (!slowOn) return false;
  slowAcc += slowSpeed;
  bool skip = slowAcc < 1000;
  if (!skip) slowAcc %= 1000;
  if (!slowC3) {
    if (slowHold > 0 && --slowHold == 0) slowC3 = true;
  } else {
    slowSpeed += 20;
    if (slowSpeed > 999) stopSlow();
  }
  if (slowOn && slowTarget != 0) {
    if (slowTarget < slowSpeed) {
      slowSpeed -= slowStep;
      if (slowSpeed <= slowTarget) { slowSpeed = slowTarget; slowTarget = 0; }
    } else {
      slowSpeed += slowStep;
      if (slowTarget <= slowSpeed) { slowSpeed = slowTarget; slowTarget = 0; }
      if (slowSpeed > 999) stopSlow();
    }
  }
  return skip;
}

}
