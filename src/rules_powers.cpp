#include <algorithm>
#include <cmath>
#include <vector>

#include "physobj.h"
#include "sim.h"
#include "sim_internal.h"

namespace pp {

using namespace detail;

namespace {

int posMod(int v, int m) { return v < 0 ? m - (-v) % m : v % m; }

int phaseImpl(const Mover& m, int cur, float frac) {
  int t, mod;
  if (m.pause1 > 0) {
    mod = m.time + m.pause2 + m.pause1;
    if (m.time <= 0 || mod <= 0) return 0;
    t = (int)((double)m.time * (double)frac);
    if ((double)m.phase1 < (double)frac) t += m.pause1;
    if ((double)m.phase2 < (double)frac) t += m.pause2;
  } else {
    if (m.time <= 0) return 0;
    mod = m.time;
    t = (int)((double)m.time * (double)frac);
  }
  return posMod(t - m.offset - cur, mod);
}

}

int Board::updatesToPhase(const Mover& m, int cur, float frac) { return phaseImpl(m, cur, frac); }

void Board::flipFlippers(bool up) {
  bool any = false;
  for (auto& o : objs) {
    if (o.id != "flipper" || !o.hasMover) continue;
    const int T = o.mv.time;
    if (T <= 0) continue;
    const int cur = posMod(o.mtick, T);
    float frac;
    if (!up) {
      if (cur < 1) continue;
      frac = 1.0f;
    } else {
      if (T / 2 < cur) {
        o.mtick = T - cur;
      }
      frac = 0.5f;
    }
    const int n = phaseImpl(o.mv, o.mtick, frac);
    if (n >= 1) {
      o.canMove = true;
      o.moveCount = n;
    }
    any = true;
  }
  if (any) ev(Event::FlipperMove, up ? 1 : 0);
}

void Board::setPolyActive(Obj& o, bool on) {
  const bool wasShown = o.visible && (o.fadeT == 0 || !o.fadeOut);
  o.collidable = o.visible = on;
  if (wasShown != o.visible) {
    if (!on) {
      o.fadeOut = true;
      o.visible = true;
    } else {
      o.fadeOut = false;
    }
    o.fadeT = 20;
  }
}

void Board::gunRelease() {
  const bool was = flipHeld;
  flipHeld = false;
  if (pturn[cur][P_FLIPPERS] > 0 && was && phase == Phase::Shot) flipFlippers(false);
}

void Board::timeBomb() {
  if (balls.empty()) return;
  Ball& b0 = balls.front();
  const V2 c = b0.pos;
  ev(Event::TimeBomb, (int)std::lround(c.x), (int)std::lround(c.y));
  const float r2 = 80.0f * 80.0f;
  for (int i = 0; i < (int)objs.size(); i++) {
    Obj& o = objs[(size_t)i];
    if (!o.pegType || o.lit || o.gone || !o.visible) continue;
    float dx = o.pos.x - c.x, dy = o.pos.y - c.y;
    if (dx * dx + dy * dy <= r2) pegHit(i, true);
  }
  balls.front().alive = false;
}

void Board::clickInShot() {
  flipHeld = true;
  if (pturn[cur][P_FLIPPERS] > 0) flipFlippers(true);
  else if (pturn[cur][P_TIMEBOMB] > 0) timeBomb();
  else if (cfg.maxLevel == 0) ev(Event::Hint, 1);
}

int Board::spinLight(int startDeg, int timer0) {
  const double kPi = 3.14159274101257;
  float angle = (float)(startDeg * kPi / 180.0), speed = 0.15f;
  int light = 0, lit = 0, timer = timer0, hold = 40;
  auto calc = [&]() {
    float ang[4];
    for (int i = 0; i < 4; i++) ang[i] = (float)((45 + 90 * i) * kPi / 180.0);
    int l = 0;
    for (; l < 4; l++)
      if (angle < ang[l]) break;
    light = l % 4;
    float off = (float)(-5 * kPi / 180.0);
    int o = (int)((angle - off) * 20.0 / kPi);
    if (o < 0) {
      int r = -o % 40;
      lit = r == 0 ? 0 : 40 - r;
    } else {
      lit = o % 40;
    }
  };
  calc();
  for (int i = 0; i < 20000; i++) {
    bool stopping = false;
    int oldLit = lit;
    if (hold > 0) {
      hold--;
      stopping = true;
    } else {
      angle -= speed;
    }
    calc();
    if (angle > 6.2831854820251465) angle = (float)(angle - 6.2831854820251465);
    else if (angle < 0.0f) angle = (float)(angle + 6.2831854820251465);
    if (stopping) continue;
    if (timer > 0) timer--;
    if ((lit - 5) % 10 != 0) {
      if ((lit + 4) % 10 == 0 && oldLit != lit) {
        if (timer == 0) speed = (float)((double)speed - 0.0016f);
      } else if (timer == 0) {
        speed = (float)((double)speed - 0.0008f);
      }
    }
    if (speed <= 0.0f) return light;
  }
  return light;
}

void Board::hatInit(Ball& b) {
  b.hat = true;
  b.hatAge = 0;
  b.disp = {b.pos.x <= 325.0f ? b.pos.x + 200.0f : b.pos.x - 200.0f, b.pos.y - 200.0f};
  b.hatPos = b.hatPrev = {b.disp.x, b.disp.y - 21.0f};
}

void Board::hatUpdate(Ball& b) {
  float y2 = b.pos.y + 2.0f;
  if (y2 < b.disp.y) b.disp.y = y2;
  b.disp.x = (b.pos.x - b.disp.x) * 0.25f + b.disp.x;
  b.disp.y = b.disp.y + 0.25f * (b.pos.y - b.disp.y);
  b.hatPrev = b.hatPos;
  b.hatPos = {0.0f + b.disp.x, b.disp.y - 21.0f};
  b.hatAge++;
}

void Board::hatContacts(std::vector<Note>& out) {
  for (int bi = 0; bi < (int)balls.size(); bi++) {
    const Ball& b = balls[(size_t)bi];
    if (!b.alive || !b.hat || b.hatAge < 11) continue;
    for (int oi = 0; oi < (int)objs.size(); oi++) {
      const Obj& o = objs[(size_t)oi];
      if (!o.pegType || o.lit || o.gone || !o.collidable) continue;
      float hitT = 1e30f;
      V2 hitPt;
      if (phys::circleCheck(o.pos, o.radius, b.hatPos, {0.0f, 0.0f}, 20.0f, hitT, hitPt)) out.push_back({bi, oi, true});
    }
  }
}

}
