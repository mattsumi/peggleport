#include "aiscore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <functional>
#include <vector>

namespace pp {

static const double kPi = 3.14159274101257;

void AIMgr::begin(const Board& b, int skill, const std::function<uint32_t()>& rnd, int mode, float aim) {
  cands_.clear();
  rnd_ = rnd;
  mode_ = mode;
  computer_ = mode == 0;
  aimIdx_ = -1;
  bestInfo_ = aimInfo_ = BallInfo{};
  skill_ = skill;
  cur_ = b.cur;
  base_ = b.score[b.cur];
  ownScore_ = b.score[b.cur];
  oppScore_ = b.players > 1 ? b.score[(b.cur + 1) % b.players] : 0;
  duel_ = b.players > 1;
  master_ = b.master[b.cur];
  masterOn_ = master_ >= 0 && master_ < 16 && b.pturn[b.cur][master_] > 0;
  guide_ = b.pturn[b.cur][Board::P_GUIDE] > 0;
  fireball_ = b.pturn[b.cur][Board::P_FIREBALL] > 0;
  target_ = fireball_ ? 1000 : 500;
  std::vector<float> angles;
  std::vector<V2> orange, green;
  for (const Obj& o : b.objs) {
    if (o.gone || o.lit) continue;
    if (o.pegType == 2) orange.push_back(o.pos);
    else if (o.pegType == 4) green.push_back(o.pos);
  }
  {
    int bestN = 0;
    for (const V2& p : orange) {
      int n = 0;
      for (const V2& q : orange)
        if ((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y) < 10000.0f) n++;
      if (n > bestN) { bestN = n; defAngle_ = Board::clampAngle(b.aimAt(p)); }
    }
    if (bestN == 0) defAngle_ = Board::clampAngle(4.712389f);
  }
  int count = (int)orange.size();
  int randoms = 30;
  if (mode == 1) {
    const int n = 120;
    const double step = (double)0.001f;
    for (int k = -n; k <= n; k++) {
      if (k == 0) aimIdx_ = (int)angles.size();
      angles.push_back(Board::clampAngle((float)((double)k * step + (double)aim)));
    }
    randoms = 0;
  } else if (count < 10 || skill < 0) {
    int nb = count == 1 ? 5 : count == 2 ? 2 : count == 3 ? 1 : 0;
    for (const V2& p : orange) {
      float a = b.aimAt(p);
      angles.push_back(Board::clampAngle(a));
      for (int k = 0; k < nb; k++) {
        angles.push_back(Board::clampAngle(a - (float)k * 0.01f));
        angles.push_back(Board::clampAngle(a + (float)k * 0.01f));
      }
    }
    if (nb == 0) randoms -= count;
  }
  if (mode != 1)
    for (const V2& p : green) angles.push_back(Board::clampAngle(b.aimAt(p)));
  if (skill >= 0 && randoms > 0 && mode != 1) {
    float arc = 360.0f - (173.0f - 7.0f);
    float step = arc / (float)randoms;
    int istep = std::max(1, (int)step);
    for (int i = 0; i < randoms; i++) {
      double deg = 7.0f - (float)i * step;
      deg -= (double)(int)(rnd() % (uint32_t)istep);
      deg = ((double)(rnd() % 1000) / 1000.0 + deg);
      angles.push_back(Board::clampAngle((float)(deg * kPi / 180.0)));
    }
  }
  for (float a : angles) {
    Cand c;
    c.angle = a;
    c.sim = b;
    c.sim.emit = false;
    c.sim.noModHooks = true;
    c.sim.events.clear();
    if (!c.sim.fire(a)) c.done = true;
    c.left = b.orangeLeft;
    c.seen.assign(c.sim.objs.size(), 0);
    for (size_t i = 0; i < c.sim.objs.size(); i++) c.seen[i] = (c.sim.objs[i].lit || c.sim.objs[i].gone) ? 1 : 0;
    cands_.push_back(std::move(c));
  }
  thinking_ = true;
  if (cands_.empty()) finish();
}

bool AIMgr::update(int framesPerTick) {
  if (!thinking_) return true;
  bool all = true;
  for (Cand& c : cands_) {
    if (c.done) continue;
    for (int k = 0; k < framesPerTick && !c.done; k++) {
      c.sim.step();
      c.frames++;
      observe(c);
      if (c.sim.phase == Phase::Aim || c.sim.phase == Phase::Over || c.frames >= target_) c.done = true;
    }
    if (!c.done) all = false;
  }
  if (all) finish();
  return all;
}

void AIMgr::observe(Cand& c) {
  Board& s = c.sim;
  bool coll = false;
  for (const auto& bl : s.balls)
    if (bl.alive && bl.sinceHit == 0) coll = true;
  if (coll && c.nColl < 2) {
    if (c.nColl == 0) c.fColl = c.frames;
    c.nColl++;
  }
  for (const auto& n : s.contactNotes()) {
    if (n.second < 0 || n.second >= (int)s.objs.size()) continue;
    const Obj& o = s.objs[(size_t)n.second];
    if (o.shape == Shape::Hole) {
      if (o.id == "freeball" && skill_ >= 10 && !duel_ && c.aiScore > 0 && !c.f205) c.aiScore += 0x9c4;
    } else if (o.shape == Shape::Poly) {
      if (o.id == "freeball") {
        if (skill_ < 10) {
          if (fireball_) c.aiScore += 5000;
          else c.done = true;
        } else if (c.left == 1) {
          c.aiScore -= 15000;
        }
        if (c.f205) c.done = true;
      } else if (o.id == "flipper") {
        c.done = true;
      } else if (o.id == "freeballcover") {
        if (!c.f1f5) {
          c.f1f5 = true;
          if (fireball_) c.aiScore += 5000;
        } else {
          c.done = true;
        }
      }
    }
  }
  if (c.seen.size() < s.objs.size()) c.seen.resize(s.objs.size(), 0);
  for (size_t i = 0; i < s.objs.size(); i++) {
    const Obj& o = s.objs[i];
    if (c.seen[i] || !(o.lit || o.gone)) continue;
    c.seen[i] = 1;
    const int type = o.pegType;
    if (type < 1 || type > 4) continue;
    if (c.nPeg < 3) c.pegF[c.nPeg++] = c.frames;
    if (type == 2 && c.nOrg < 2) c.orgF[c.nOrg++] = c.frames;
    if (type == 4 && c.greenF < 0) c.greenF = c.frames;
    int v = 10;
    bool mult = true;
    if (type == 2) v = 100;
    else if (type == 3) v = 500;
    else if (type == 4) {
      if (!computer_) {
        v = 50;
      } else {
        switch (master_) {
          case 1: case 6: case 8: case 12: case 7: v = 300; break;
          case 2: case 4:
            v = 300;
            if (!masterOn_) c.f205 = true;
            break;
          case 13: v = 5000; mult = false; break;
          default: v = 100; break;
        }
      }
    }
    const int m = 0;
    if (computer_) {
      v = (int)((double)(10 * v) / (2.0 + 1.0 * m));
      if (c.left < 3) v = (int)((double)(10 * v) / (2.0 + (double)(c.left + m) * 1.0));
    }
    if (mult) v *= Board::multiplier(c.left);
    if (v > 10000 && type == 3) v = 10000;
    c.sum += v;
    c.aiScore += (m - 1) * v + c.sum;
    if (type == 2) {
      c.hits++;
      c.left--;
    }
  }
}

void AIMgr::finish() {
  thinking_ = false;
  const int skill = skill_;
  int best = -1, bestAi = 0;
  for (int i = 0; i < (int)cands_.size(); i++) {
    Cand& c = cands_[i];
    const int P0 = c.pegF[0], P1 = c.pegF[1], P2 = c.pegF[2], O0 = c.orgF[0], G = c.greenF;
    bool A = P0 != c.fColl;
    bool B = P0 == O0;
    bool C = !(P1 < O0) && !(P0 + 25 <= O0);
    bool D = G >= 0 && !(P1 < G) && !(P0 + 20 <= G);
    bool E = (P2 == -1 || O0 <= P2 + 25) && (O0 - P0 < 150);
    if (fireball_ && O0 < c.fColl + 200 && O0 >= 0) { C = B = true; c.hits = 1; }
    if (master_ == Board::P_EXTENDER && D) { C = B = true; c.hits = 1; }
    if (guide_) {
      A = false;
      if (!B) B = !(P1 < O0) && (P0 + 50 > O0);
    }
    int score = c.aiScore;
    if (c.left == 0 && c.hits > 0) {
      int bonus = 25000;
      if (duel_ && oppScore_ < ownScore_ + 10000) bonus = 100000;
      if (skill < 10 && !B && !C && (skill == 0 || !E)) bonus /= 20;
      score += bonus;
    } else if (!duel_ && best >= 0 && skill >= 10) {
      if (c.hits < cands_[best].hits) {
        bool keep = false;
        if (score > 7999) keep = 1.5 * (double)bestAi <= (double)score;
        if (!keep && score < 25000) score = 0;
      }
    }
    if (skill < 10) {
      bool any = true;
      if (B) score *= 20;
      else if (C) score *= 10;
      else if (E) score *= 3;
      else any = false;
      if (any && !A) score *= 2;
      if (any && A && skill < 0) score /= 5;
      if (!any && skill < 0) score /= 5;
      if (skill < 0 && B) score *= 10;
    }
    if (duel_ && c.hits == 0) score = 0;
    c.fB = B; c.fC = C; c.fE = E;
    c.score = score;
    if (bestAi < score) { bestAi = score; best = i; }
  }
  bestScore_ = bestAi;
  if (aimIdx_ >= 0 && aimIdx_ < (int)cands_.size()) {
    const Cand& a = cands_[aimIdx_];
    aimInfo_ = {true, a.left, a.score, a.sum};
  }
  bool useFlags = true;
  if (best < 0) {
    useFlags = false;
    chosen_ = defAngle_;
    chosenGain_ = maxGain_ = 0;
  } else {
    chosen_ = cands_[best].angle;
    bestInfo_ = {true, cands_[best].left, cands_[best].score, cands_[best].sum};
    chosenGain_ = cands_[best].sim.score[cur_] - base_;
    maxGain_ = -2000000000;
    for (const Cand& c : cands_) maxGain_ = std::max(maxGain_, c.sim.score[cur_] - base_);
  }
  const bool bf = useFlags && best >= 0 && (cands_[best].fB || cands_[best].fC || cands_[best].fE);
  bool inject = (skill < 10 && !bf) || skill < 1;
  if (!inject && !guide_ && skill < 10 && rnd_ && (int)(rnd_() % 100) < 25) inject = true;
  if (inject && rnd_) {
    float mag = skill < 0 ? 0.001f : 0.0001f;
    if (rnd_() & 1) mag = -mag;
    if (guide_ && (int)(rnd_() % 100) < 70) mag = 0;
    chosen_ = Board::clampAngle(chosen_ + mag);
  }
  cands_.clear();
}

}
