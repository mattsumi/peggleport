#include "aiscore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "dmath.h"
#include "physball.h"
#include "sim_internal.h"

namespace pp {

namespace {

constexpr float kPiF = 3.14159274101257f;
constexpr double kTwoPiD = 6.2831854820251465;
constexpr double kHalfPiD = 1.5707963705062866;
constexpr float kQuarterF = 4.71238899230957f;

inline float f32(double v) { return (float)v; }

}

struct AIMgr::Run {
  struct Cand {
    float angle = 0;
    int left = 0, score = 0, sum = 0, hits = 0, purple = 0;
    int coll[2] = {-1, -1};
    int pegF[3] = {-1, -1, -1};
    int orgF[2] = {-1, -1};
    int greenF = -1, holeF = -1, holeExit = -1;
    int nColl = 0, nPeg = 0, nOrg = 0;
    std::vector<char> seen;
    int m = 0;
    int bestScore = -1, bestFrame = -1, bestLeft = 100;
    int state = 0;
    bool coverHit = false, firstMover = false, bad = false;
    bool fireball = false;
    bool clone = false;
    int partner = -1;
  };

  Board w;
  std::vector<Cand> cands;
  std::vector<int> owner;
  int frame = 0;
  int target = 500;
  int skill = 0;
  int mode = 0;
  int cur = 0;
  bool computer = true;
  bool duel = false;
  bool guide = false;
  bool fireball = false;
  bool charge = false;
  int power = 0;
  bool hasCharge = false;
  float minRad = 0, maxRad = 0, minDeg = 0, maxDeg = 0;
  int ownScore = 0, oppScore = 0;
  std::vector<int> pegList;
  std::vector<int> orange, other;
  bool clearAll = false;
  bool nights = false;
  float aimDefault = 0;
  float gunStart = 0;
  int aimedIdx = -1;

  float keep(float a) const {
    a = gun::normalizeAngle(a);
    const double d = (double)a;
    if (d > kHalfPiD) {
      if (a < kQuarterF) return a < maxRad ? maxRad : a;
      a = f32((double)a - kTwoPiD);
    }
    if (minRad < a) a = minRad;
    return a;
  }

  V2 pegPos(int oi) const { return w.objs[(size_t)oi].pos; }

  float defaultAngle(int mode) const {
    if (mode != 0) return aimDefault;
    float def = gunStart;
    int bestN = 0;
    for (int oi : orange) {
      const V2 p = w.objs[(size_t)oi].pos;
      int cnt = 0;
      for (int oj : orange) {
        const V2 q = w.objs[(size_t)oj].pos;
        const float ddx = q.x - p.x, ddy = q.y - p.y;
        if (ddx * ddx + ddy * ddy < 10000.0f) cnt++;
      }
      if (cnt > bestN) {
        bestN = cnt;
        def = angleForPeg(oi);
      }
    }
    return def;
  }

  float angleForPeg(int oi) const {
    const float gx = w.pivot().x, gy = w.pivot().y;
    const V2 p = pegPos(oi);
    const float dx = p.x - gx;
    const float ny = gy - p.y;
    const float A = (float)dm::atan2((double)ny, (double)dx);
    const float dyp = p.y - gy;
    float B = (float)dm::atan2((double)dx, (double)dyp);
    B = std::fabs(B);
    const float k = 1.3000000080864993e-06f;
    const float s = dyp * dyp + dx * dx;
    float c = k * s;
    c = c * B;
    if (c > 1.0f) c = 1.0f;
    if (dx < 0.0f) c = c * -1.0f;
    return keep(c + A);
  }

  bool isPeg(const Obj& o) const { return o.pegType != 0; }

  bool collides(const Cand& c, const Obj& o, int oi) const {
    if (o.pegType == 0) return true;
    const bool passes = o.pegType == 4 && power == 3;
    if (!passes && !c.fireball) return true;
    return !c.seen[(size_t)oi];
  }

  int scoreMult(int left) const { return clearAll && left <= 0 ? 10 : Board::multiplier(left); }

  void sample(Cand& c, int bi) {
    const Ball& b = w.balls[(size_t)bi];
    const float bx = b.pos.x, by = b.pos.y;
    const float lim = 80.0f * 80.0f;
    int m = c.m;
    int sum = c.sum, score = c.score, left = c.left;
    bool anyOrange = c.hits > 0;
    for (int oi : pegList) {
      const Obj& o = w.objs[(size_t)oi];
      if (o.lit || !o.visible) continue;
      if (c.seen[(size_t)oi]) continue;
      const float dx = o.pos.x - bx;
      const float dy = o.pos.y - by;
      const float d2 = dx * dx + dy * dy;
      if (d2 <= lim) {
        const int mult = scoreMult(left);
        int v = 10;
        if (o.pegType == 3) v = 500;
        else if (o.pegType == 2) { left--; v = 100; anyOrange = true; }
        sum += v * mult;
        score += v * mult * m + sum;
        m++;
      }
    }
    if (anyOrange && c.bestScore <= score) {
      c.bestScore = score;
      c.bestFrame = frame;
      c.bestLeft = left;
    }
  }

  void notifyBall(int ci, int oi);
  void setHole(int ci, int bi, const Obj& h);
  void doMultiball(int ci, int oi);
  void doExploder(int ci, int oi);
  void doFlower(int ci, int oi);
  int ballIndex(int ci) const {
    for (size_t i = 0; i < owner.size(); i++)
      if (owner[i] == ci) return (int)i;
    return -1;
  }
  void addCand(const Cand& c, const Ball& b) {
    cands.push_back(c);
    w.balls.push_back(b);
    owner.push_back((int)cands.size() - 1);
    Ball& nb = w.balls.back();
    nb.uid = ++w.ballUid_;
    phys::ballSyncBounds(nb);
    w.sapAdd(Board::SAP_BALL + nb.uid);
    for (auto it = w.sapPairs_.begin(); it != w.sapPairs_.end();) {
      if ((*it)[1] == 2 && ((*it)[0] == nb.uid || (*it)[2] == nb.uid)) it = w.sapPairs_.erase(it);
      else ++it;
    }
  }
  void stepFrame();
  void jumpMovers(int n);
  Ball makeBall(float angle, bool fire, V2& vel) const;
};

void AIMgr::Run::jumpMovers(int n) {
  for (Obj& o : w.objs) {
    if (!o.hasMover || !o.canMove) continue;
    o.mtick += n;
    const V2 old = o.pos;
    float x = o.pos.x, y = o.pos.y, r = o.rot;
    w.moverStep(o, x, y, r);
    if (o.shape == Shape::Hole) {
      const float dx = x - old.x, dy = y - old.y;
      o.pos = {dx + old.x, dy + old.y};
      o.x0 = o.x0 + dx; o.x1 = o.x1 + dx;
      o.y0 = o.y0 + dy; o.y1 = dy + o.y1;
      continue;
    }
    o.pos = {x, y};
    switch (o.shape) {
      case Shape::Circle:
        o.x0 = x - o.radius; o.x1 = o.radius + x;
        o.y0 = y - o.radius; o.y1 = o.radius + y;
        break;
      case Shape::Line:
        o.pline.setPos(x, y);
        break;
      case Shape::Poly:
      case Shape::Brick:
        o.ppoly.setPos(x, y);
        o.x0 = o.ppoly.bx0; o.y0 = o.ppoly.by0; o.x1 = o.ppoly.bx1; o.y1 = o.ppoly.by1;
        break;
      default: break;
    }
  }
}

Ball AIMgr::Run::makeBall(float angle, bool fire, V2& vel) const {
  const float gx = w.pivot().x, gy = w.pivot().y;
  const float radius = w.armedBallRadius();
  float px = ((gun::TIP_OFFSET + gx) + gun::PRE_OFFSET) + (float)(int)radius;
  float py = 1.0f + gy;
  const V2 p = gun::rotateXY({px, py}, {gx, gy}, angle);
  Ball b;
  b.fire = fire;
  b.r = w.ballRadiusFor(fire);
  phys::ballSetPos(b, p.x, p.y);
  b.pos = b.prev = p;
  b.vel = gun::launchVelocity(angle, w.gunSpeed_);
  vel = b.vel;
  return b;
}

void AIMgr::Run::setHole(int ci, int bi, const Obj& h) {
  Cand& c = cands[(size_t)ci];
  if (h.exitIdx < 0) {
    c.state = 1;
  } else {
    if (c.holeF == -1) c.holeF = frame;
    Ball& b = w.balls[(size_t)bi];
    const Obj& e = w.objs[(size_t)h.exitIdx];
    if (h.outDelay < 1) {
      phys::ballSetPos(b, e.pos.x, e.pos.y);
      b.prev = b.pos;
    } else {
      b.holeOut = h.outDelay;
      b.holeTarget = e.pos;
      c.holeExit = h.exitIdx;
    }
  }
}

void AIMgr::Run::doMultiball(int ci, int oi) {
  const int bi = ballIndex(ci);
  const Ball sb = w.balls[(size_t)bi];
  const V2 peg = w.objs[(size_t)oi].pos;
  const float spd = nights ? 0.75f : 0.5f;
  float dir = spd;
  const float dxp = peg.x - sb.pos.x;
  if (!(0.0f <= dxp)) {
    dir = -spd;
  } else if (dxp == 0.0f && !(w.shotRand[0] & 1u)) {
    dir = -spd;
  }
  Cand c;
  const Cand& src = cands[(size_t)ci];
  c.angle = src.angle;
  c.score = src.score;
  c.sum = src.sum;
  c.hits = src.hits;
  c.seen = src.seen;
  c.m = src.m;
  c.left = 0;
  c.clone = true;
  c.partner = ci;
  c.fireball = src.fireball;
  Ball nb;
  nb.fire = sb.fire;
  nb.r = sb.r;
  phys::ballSetPos(nb, peg.x, peg.y);
  nb.pos = nb.prev = peg;
  nb.vel = {dir, nights ? -3.5f : -3.0f};
  addCand(c, nb);
  cands[(size_t)ci].partner = (int)cands.size() - 1;
}

void AIMgr::Run::doExploder(int ci, int oi) {
  const V2 pp = w.objs[(size_t)oi].pos;
  const float r = 100.0f;
  for (int pi : pegList) {
    const Obj& o = w.objs[(size_t)pi];
    if (o.lit || !o.visible) continue;
    const float dy = o.pos.y - pp.y;
    const float dx = o.pos.x - pp.x;
    if (dy * dy + dx * dx <= r * r) notifyBall(ci, pi);
  }
}

void AIMgr::Run::doFlower(int ci, int oi) {
  const V2 pp = w.objs[(size_t)oi].pos;
  std::vector<std::pair<float, int>> near;
  for (int pi : pegList) {
    const Obj& o = w.objs[(size_t)pi];
    if (o.lit || !o.visible || o.pegType != 2) continue;
    if (!cands[(size_t)ci].seen[(size_t)pi]) {
      const float dx = o.pos.x - pp.x;
      const float dy = o.pos.y - pp.y;
      near.push_back({dx * dx + dy * dy, pi});
    }
  }
  std::stable_sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  const float cnt = (float)w.orangeSet().size() * 0.2f;
  int n = (int)std::ceil((double)cnt);
  for (int i = 0; i < n && i < (int)near.size(); i++) notifyBall(ci, near[(size_t)i].second);
}

void AIMgr::Run::notifyBall(int ci, int oi) {
  Cand& c = cands[(size_t)ci];
  const Obj& o = w.objs[(size_t)oi];
  const int bi = ballIndex(ci);
  const int type = o.shape == Shape::Hole ? 1 : o.shape == Shape::Circle ? 3 : o.shape == Shape::Line ? 4 : 5;
  if (c.nColl < 2) {
    if (c.nColl == 0 && o.hasMover && o.canMove) c.firstMover = true;
    c.coll[c.nColl] = frame;
    c.nColl++;
  }
  if (type == 5) {
    if (o.id == "freeball") {
      if (skill < 10) {
        if (!c.fireball) c.state = 1;
        else c.score += 5000;
      } else if (c.left == 1) {
        c.score -= 15000;
      }
      if (c.bad) c.state = 1;
    } else if (o.id == "flipper") {
      c.state = 1;
    } else if (o.id == "freeballcover") {
      if (!c.coverHit) {
        c.coverHit = true;
        if (c.fireball) c.score += 5000;
      } else {
        c.state = 1;
      }
    } else {
      const bool bump = o.id == "bumperhole";
      if (bump && skill < 0) {
        c.state = 1;
      } else if (bump && skill > 9 && c.left < 3 && duel && computer) {
        c.score = (int)0.10000000149011612f;
      }
    }
  }
  if (type == 1) {
    setHole(ci, bi, o);
    if (o.id != "freeball") return;
    if (skill < 10) return;
    if (duel) return;
    if (c.score < 1) return;
    if (!c.bad) c.score += 0x9c4;
    return;
  }
  if (o.pegType == 0) return;
  if (o.lit) return;
  if (c.nPeg < 3) c.pegF[c.nPeg++] = frame;
  if (c.seen[(size_t)oi]) return;
  c.seen[(size_t)oi] = 1;
  c.m++;
  if (o.pegType == 2 && c.nOrg < 2) c.orgF[c.nOrg++] = frame;
  if (o.pegType == 4 && c.greenF == -1) c.greenF = frame;
  bool mult = true;
  int v = 10;
  if (o.pegType == 2) v = 100;
  else if (o.pegType == 3) v = 500;
  else if (o.pegType == 4) {
    if (computer) {
      switch (power) {
        case 1: case 6: case 8: case 12: v = 300; break;
        case 2: case 4:
          v = 300;
          if (!hasCharge) c.bad = true;
          break;
        case 7: v = skill > 10 ? 10 : 300; break;
        case 13: v = 5000; mult = false; break;
        default: v = 100; break;
      }
    } else {
      v = 50;
    }
  }
  if (computer) {
    const int m = c.m;
    const int limit = skill == 10 ? 6 : skill >= 1 ? 5 : skill >= 0 ? 3 : 2;
    if (nights && m > limit) {
      v = 0;
    } else {
      if (m == 1 && c.left == 1 && o.pegType == 2) v = nights ? (int)((double)v * 1.5) : 1;
      v = (int)((double)(10 * v) / (2.0 + (double)m * 1.0));
      if (c.left < 3) v = (int)((double)(10 * v) / (2.0 + (double)(c.left + m) * 1.0));
      if (m == 1 && o.pegType == 2 && duel) v = v * 4;
    }
  }
  if (mult) v *= scoreMult(c.left);
  if (v > 10000 && o.pegType == 3) v = 10000;
  if (o.pegType == 4) {
    if (power == 3) doMultiball(ci, oi);
    else if (power == 5) doExploder(ci, oi);
    else if (power == 9) doFlower(ci, oi);
    else if (power == 11) c.bestFrame = 0;
  } else if (o.pegType == 3) {
    c.purple = v;
  }
  Cand& cc = cands[(size_t)ci];
  cc.sum += v;
  const int add = (cc.m - 1) * v + cc.sum;
  cc.score += add;
  if (o.pegType == 2) {
    cc.hits++;
    cc.left--;
    if (!cc.clone) return;
    if (cc.partner >= 0) {
      cands[(size_t)cc.partner].hits++;
      cands[(size_t)cc.partner].left--;
    }
  }
  if (cc.clone && cc.partner >= 0) {
    cands[(size_t)cc.partner].sum += v;
    cands[(size_t)cc.partner].score += add;
  }
}

void AIMgr::Run::stepFrame() {
  frame++;
  for (Obj& o : w.objs) w.updateObj(o);
  w.notes_.clear();
  w.sapSync();
  const size_t nb = w.balls.size();
  std::vector<char> live(nb, 0);
  for (size_t bi = 0; bi < nb; bi++) {
    Cand& c = cands[(size_t)owner[bi]];
    if (c.state == 0) {
      if (c.bestFrame >= 0 && frame % 20 == 0) sample(c, (int)bi);
      Ball& b = w.balls[bi];
      b.stuck = 0;
      if (b.holeOut > 0 && c.holeExit >= 0) b.holeTarget = w.objs[(size_t)c.holeExit].pos;
      live[bi] = w.updateBall(b) ? 1 : 0;
      if (frame > 500 && c.bestFrame < 0) c.state = 1;
    } else if (c.state == 1) {
      live[bi] = 1;
    }
  }
  w.sapSort();
  std::vector<char> done(nb, 0);
  auto indexOf = [&](int uid) {
    for (int i = 0; i < (int)w.balls.size(); i++)
      if (w.balls[(size_t)i].uid == uid) return i;
    return -1;
  };
  int curFirst = -1;
  std::vector<int> cand;
  auto flush = [&]() {
    if (curFirst < 0) return;
    const int bi = indexOf(curFirst - Board::SAP_BALL);
    if (bi >= 0) {
      w.collide(w.balls[(size_t)bi], bi, &cand);
      done[(size_t)bi] = 1;
    }
    cand.clear();
  };
  for (const auto& key : std::vector<std::array<int, 4>>(w.sapPairs_.begin(), w.sapPairs_.end())) {
    const int first = Board::SAP_BALL + key[0];
    const int second = key[3];
    const int fi = indexOf(first - Board::SAP_BALL);
    if (fi < 0 || !live[(size_t)fi]) continue;
    if (first != curFirst) {
      flush();
      curFirst = first;
    }
    if (second >= Board::SAP_BALL) {
      const int si = indexOf(second - Board::SAP_BALL);
      if (si >= 0 && live[(size_t)si] && cands[(size_t)owner[(size_t)fi]].partner == owner[(size_t)si])
        phys::checkBallCollision(w.balls[(size_t)fi], w.balls[(size_t)si]);
      continue;
    }
    const Obj& o = w.objs[(size_t)second];
    if (!o.collidable || o.gone) continue;
    if (o.shape == Shape::Hole) {
      const Ball& b = w.balls[(size_t)fi];
      if (!phys::holeContains(b.pos.x, b.pos.y, o.pos.x, o.pos.y, o.hw, o.hh, o.circular)) continue;
      w.notes_.push_back({fi, second});
    } else if (collides(cands[(size_t)owner[(size_t)fi]], o, second)) {
      cand.push_back(second);
    }
  }
  flush();
  std::vector<char> dead(nb, 0);
  for (size_t bi = 0; bi < nb; bi++) {
    Cand& c = cands[(size_t)owner[bi]];
    if (c.state == 0) {
      if (live[bi] && !done[bi]) phys::ballTranslate(w.balls[bi], w.balls[bi].vel.x, w.balls[bi].vel.y);
    } else if (c.state == 1) {
      c.state = 2;
      dead[bi] = 1;
      w.sapRemove(Board::SAP_BALL + w.balls[bi].uid);
    }
  }
  const auto notes = w.notes_;
  for (const auto& nt : notes) {
    const int ci = owner[(size_t)nt.ball];
    notifyBall(ci, nt.obj);
  }
  for (size_t bi = nb; bi-- > 0;) {
    if (dead[bi] || cands[(size_t)owner[bi]].state == 2) {
      w.balls.erase(w.balls.begin() + (long)bi);
      owner.erase(owner.begin() + (long)bi);
    }
  }
}

AIMgr::AIMgr() = default;
AIMgr::~AIMgr() = default;

bool AIMgr::thinking() const { return thinking_; }

void AIMgr::begin(const Board& b, int skill, const std::function<uint32_t()>& rnd, int mode, float aim) {
  run_ = std::make_unique<Run>();
  Run& r = *run_;
  rnd_ = rnd;
  mode_ = mode;
  bestInfo_ = aimInfo_ = BallInfo{};
  bestScore_ = 0;
  chosen_ = 0;
  r.w = b;
  r.w.emit = false;
  r.w.noModHooks = true;
  r.w.events.clear();
  r.w.balls.clear();
  r.w.sapX_.clear();
  r.w.sapY_.clear();
  r.w.sapPairs_.clear();
  r.w.sapIn_.clear();
  r.w.sapAll_ = true;
  r.mode = mode;
  r.skill = skill;
  r.cur = b.cur;
  r.computer = mode == 0;
  r.duel = b.players > 1;
  r.power = b.master[b.cur];
  r.hasCharge = r.power > 0 && r.power < masters::kPowerSlots && b.pturn[b.cur][r.power] > 0;
  r.guide = b.pturn[b.cur][Board::P_GUIDE] > 0;
  r.fireball = b.pturn[b.cur][Board::P_FIREBALL] > 0;
  r.charge = b.pturn[b.cur][Board::P_TIMEBOMB] > 0;
  r.clearAll = b.cfg.clearLevel;
  r.nights = b.cfg.nights;
  r.gunStart = b.gunAngle;
  r.aimDefault = aim;
  r.ownScore = b.score[b.cur];
  r.oppScore = b.players > 1 ? b.score[1 - b.cur] : 0;
  r.target = 500;
  r.orange = b.orangeSet();
  r.other = b.otherSet();
  std::sort(r.orange.begin(), r.orange.end());
  std::sort(r.other.begin(), r.other.end());
  bool greenIn = false;
  for (int oi : r.other)
    if (b.objs[(size_t)oi].pegType == 4) greenIn = true;
  if (r.charge || (greenIn && r.power == 11)) r.target = 1000;
  const float maxGun = 83.0f;
  r.minDeg = 90.0f - maxGun;
  r.maxDeg = maxGun + 90.0f;
  r.minRad = (r.minDeg * kPiF) / 180.0f;
  r.maxRad = (r.maxDeg * kPiF) / 180.0f;
  for (size_t i = 0; i < b.objs.size(); i++)
    if (b.objs[i].pegType != 0 && !b.objs[i].gone) r.pegList.push_back((int)i);
  int ahead = 0;
  if (mode == 0) ahead = r.target / 10 - 1 + 100 + 1;
  if (ahead > 0) r.jumpMovers(ahead);
  r.w.sapSync();

  std::vector<float> angles;
  int aimedAt = -1;
  const int orangeCount = (int)r.orange.size();
  if (mode != 0) {
    const float step = 0.0010000000474974513f;
    for (int k = -120; k <= 120; k++) {
      if (k == 0) aimedAt = (int)angles.size();
      angles.push_back(r.keep((float)k * step + aim));
    }
  } else {
    int randoms = 30;
    if (orangeCount < 10 || skill < 0) {
      int nb = 0;
      if (orangeCount == 1) nb = 5;
      else if (orangeCount == 2) nb = 2;
      else if (orangeCount == 3) nb = 1;
      for (int oi : r.orange) {
        const float a = r.angleForPeg(oi);
        angles.push_back(a);
        for (int k = 0; k < nb; k++) {
          angles.push_back(r.keep(a - (float)k * 0.009999999776482582f));
          angles.push_back(r.keep((float)k * 0.009999999776482582f + a));
        }
      }
      if (nb == 0) randoms -= orangeCount;
    }
    for (int oi : r.other)
      if (b.objs[(size_t)oi].pegType == 4) angles.push_back(r.angleForPeg(oi));
    if (skill >= 0 && randoms > 0) {
      float st = (360.0f - (r.maxDeg - r.minDeg)) / (float)randoms;
      int istep = (int)std::ceil((double)st);
      if (istep < 1) istep = 1;
      for (int i = 0; i < randoms; i++) {
        float base = r.minDeg - (float)i * st;
        const int r1 = (int)(rnd_() % (uint32_t)istep);
        float d = base - (float)r1;
        const int r2 = (int)(rnd_() % 1000u);
        float q = (float)r2 / 1000.0f;
        float t = q + d;
        t = (t * kPiF) / 180.0f;
        angles.push_back(r.keep(t));
      }
    }
  }
  const int n = (int)angles.size();
  candidates_ = n;
  r.cands.reserve((size_t)n + 8);
  for (int i = 0; i < n; i++) {
    Run::Cand c;
    c.angle = angles[(size_t)i];
    c.left = orangeCount;
    c.seen.assign(b.objs.size(), 0);
    c.fireball = r.fireball;
    if (r.charge) c.bestFrame = 0;
    V2 v;
    Ball ball = r.makeBall(angles[(size_t)i], r.fireball, v);
    r.cands.push_back(c);
    r.w.balls.push_back(ball);
    r.owner.push_back(i);
  }
  r.w.sapSync();
  r.aimedIdx = aimedAt;
  thinking_ = true;
  if (r.target == 0 || n == 0) finish();
}

bool AIMgr::update(int framesPerTick) {
  if (!thinking_) return true;
  Run& r = *run_;
  int n = r.target - r.frame;
  if (framesPerTick < n) n = framesPerTick;
  for (int i = 0; i < n; i++) r.stepFrame();
  if (r.frame >= r.target) finish();
  return !thinking_;
}

void AIMgr::finish() {
  thinking_ = false;
  Run& r = *run_;
  const int skill = r.skill;
  const int n = (int)r.cands.size();
  int best = -1, bestScore = 0;
  bool bestB = false, bestC = false, bestE = false;
  for (int i = 0; i < n; i++) {
    Run::Cand& c = r.cands[(size_t)i];
    if (c.clone) continue;
    const int P0 = c.pegF[0], P1 = c.pegF[1], P2 = c.pegF[2], O0 = c.orgF[0], G = c.greenF;
    bool A = P0 != c.coll[0];
    bool B = P0 == O0;
    bool C = !(P1 < O0) && !(P0 + 25 <= O0);
    bool D = !(G < 0 || P1 < G || P0 + 20 <= G);
    bool E = false;
    if (P2 == -1 || O0 <= P2 + 25) E = O0 - P0 < 150;
    if (r.fireball && O0 < c.coll[0] + 200 && O0 >= 0) { C = B = true; c.hits = 1; }
    if (r.power == 8 && D) { C = B = true; c.hits = 1; }
    if (r.guide) {
      A = false;
      if (!B) B = !(P1 < O0) && (P0 + 50 > O0);
    }
    if (c.score < c.bestScore) {
      c.score = c.bestScore;
      c.left = c.bestLeft;
      if (c.hits < 1) c.hits = 1;
    } else {
      c.bestFrame = -1;
    }
    if (c.left == 0 && c.hits > 0 && !r.clearAll) {
      int bonus = 25000;
      if (r.duel && r.oppScore < r.ownScore + 10000) bonus = 100000;
      if (skill < 10 && !B && !C && (skill == 0 || !E)) bonus = (int)((unsigned)bonus / 20u);
      c.score += bonus;
    } else if (!r.duel && best >= 0 && skill >= 10) {
      if (c.hits < r.cands[(size_t)best].hits) {
        bool keepIt = false;
        if (c.score > 7999) keepIt = 1.5 * (double)r.cands[(size_t)best].score <= (double)(float)c.score;
        if (!keepIt && c.score < 25000) c.score = 0;
      }
    }
    if (skill < 10) {
      bool any = true;
      if (B) c.score *= 20;
      else if (C) c.score *= 10;
      else if (E) c.score *= 3;
      else any = false;
      if (any && !A) c.score *= 2;
      else if (skill < 0) c.score /= 5;
      if (skill < 0 && B) c.score *= 10;
    }
    if (r.duel && c.hits == 0) c.score = 0;
    if (bestScore < c.score) {
      bestB = B; bestC = C; bestE = E;
      bestScore = c.score;
      best = i;
    }
  }
  bestScore_ = bestScore;
  if (r.aimedIdx >= 0) {
    const auto& a = r.cands[(size_t)r.aimedIdx];
    aimInfo_ = {true, a.left, a.score, a.sum};
  }
  float angle;
  bool special = false;
  if (best < 0) {
    angle = r.defaultAngle(mode_);
  } else {
    const auto& c = r.cands[(size_t)best];
    angle = c.angle;
    special = c.bestFrame > 0;
    bestInfo_ = {true, c.left, c.score, c.sum};
  }
  const bool neg = !special;
  const bool cond2 = skill < 1 && neg;
  const bool cond1 = skill < 10 && !bestB && !bestC && !bestE;
  bool go = cond1 || cond2;
  if (!go && !r.guide && skill < 10) go = (int)(rnd_() % 100u) < 25;
  if (go) {
    float mag = skill < 0 ? 0.0010000000474974513f : 9.999999747378752e-05f;
    const uint32_t q = rnd_();
    if ((q & 1u) != 0u) mag = mag * -1.0f;
    if (r.guide && (int)(rnd_() % 100u) < 70) mag = 0.0f;
    angle = mag + angle;
  }
  chosen_ = angle;
  run_.reset();
}

}
