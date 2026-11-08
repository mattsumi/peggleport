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

void Board::hatInit(Ball& b, bool atOrigin) {
  b.hat = true;
  b.hatAge = 0;
  const V2 from = atOrigin ? V2{0.0f, 0.0f} : b.pos;
  b.disp = {from.x <= 325.0f ? from.x + 200.0f : from.x - 200.0f, from.y - 200.0f};
  b.hatPos = b.hatPrev = {b.disp.x, b.disp.y - 21.0f};
  b.hx0 = b.hy0 = -6.0f;
  b.hx1 = b.hy1 = 6.0f;
}

void Board::hatUpdate(Ball& b) {
  float y2 = b.pos.y + 2.0f;
  if (y2 < b.disp.y) b.disp.y = y2;
  b.disp.x = (b.pos.x - b.disp.x) * 0.25f + b.disp.x;
  b.disp.y = b.disp.y + 0.25f * (b.pos.y - b.disp.y);
  b.hatPrev = b.hatPos;
  b.hatPos = {0.0f + b.disp.x, b.disp.y - 21.0f};
  b.hx0 = b.hatPos.x - 20.0f;
  b.hy0 = b.hatPos.y - 20.0f;
  b.hx1 = b.hatPos.x + 20.0f;
  b.hy1 = b.hatPos.y + 20.0f;
  b.hatAge++;
}

void Board::hatHit(const Ball& owner, int oi) {
  Ball h;
  h.pos = h.prev = owner.hatPos;
  h.r = 20.0f;
  h.slot = 1000000;
  h.hat = true;
  hatCur_ = &h;
  if (!objs[(size_t)oi].lit) pegHit(oi);
  hatCur_ = nullptr;
}

void Board::collideHat(const Ball& owner, int bi, const std::vector<int>& cand) {
  Ball h;
  h.pos = h.prev = owner.hatPos;
  h.vel = {0.0f, 0.0f};
  h.r = 20.0f;
  h.fire = true;
  const size_t n0 = notes_.size();
  collide(h, bi, &cand);
  for (size_t i = n0; i < notes_.size(); i++) notes_[i].hat = true;
}

void Board::polyNotify(Obj& o) {
  if (!o.toggle || o.canMove || !o.hasMover) return;
  const Mover& m = o.mv;
  const int period = m.pause1 > 0 ? m.time + m.pause2 + m.pause1 : m.time;
  if (period <= 0) return;
  const int cur = o.mtick % period;
  float frac;
  if (cur < 10) {
    if (o.mirror) {
      o.mirror = false;
      o.imgDx = o.imgDx * -1.0f;
    }
    frac = 0.5f;
  } else {
    if (!o.mirror) {
      o.mirror = true;
      o.imgDx = o.imgDx * -1.0f;
    }
    frac = 0.0f;
  }
  const int n = phaseImpl(m, o.mtick, frac);
  if (n > 0) {
    o.canMove = true;
    o.moveCount = n;
  }
  addBubbles(o, 0);
}

void Board::polyFlip(Obj& o) {
  if (!o.toggle || o.moveCount != 40) return;
  o.mirror = !o.mirror;
  o.imgDx = o.imgDx * -1.0f;
  addBubbles(o, 1);
}

void Board::addBubbles(const Obj& o, int kind) {
  ev(Event::Toggle, kind);
  const int x = (int)(o.mirror ? o.x1 : o.x0);
  const int y = (int)o.pos.y;
  int n = 2 + (int)(rng.rand() % 3u);
  for (; n > 0; n--) {
    const int ox = (int)(rng.rand() % 21u) - 10;
    const int oy = (int)(rng.rand() % 21u) - 10;
    const int rise = (int)(rng.rand() % 6u);
    int cel = (int)(rng.rand() & 3u);
    if (cel == 3) cel = (int)(rng.rand() & 3u);
    const int age = -((int)rng.rand()) % 20;
    ev(Event::Bubble, x + ox, y + oy, rise | (cel << 4) | ((-age) << 8));
  }
}

namespace {

struct ChainCand {
  int obj;
  float r, c1, c2, f10, f14;
};

inline float F32(double v) { return (float)v; }

}

int Board::bucketIndex() const {
  for (size_t i = 0; i < objs.size(); i++)
    if (!objs[i].gone && objs[i].id == "freeball") return (int)i;
  return -1;
}

bool Board::electroBall() const {
  if (!cfg.nights) return false;
  for (const Ball& b : balls)
    if (b.alive && b.chain) return true;
  return phase == Phase::Aim && ballsLeft[cur] > 0 && pturn[cur][P_CHAIN] > 0;
}

float Board::rodTargetX(const Obj& bk) const {
  float v = 0.0f;
  if (bk.hasMover && bk.mv.time > 0) {
    const int period = bk.mv.time;
    const float ph = F32((double)(bk.mtick % period) / (double)period);
    const float a = F32(0.25 - (double)ph), b = F32(0.75 - (double)ph);
    const float fa = std::fabs(a);
    if (!(fa > 0.25f)) {
      v = (float)((1.0 - (double)fa * 4.0) * -1.0);
    } else {
      const float fb = std::fabs(b);
      v = !(fb > 0.25f) ? (float)(1.0 - (double)fb * 4.0) : 0.0f;
    }
  }
  return F32((double)bk.pos.x + (double)v * 32.0);
}

void Board::rodMoveX() {
  const int bi = bucketIndex();
  if (bi < 0) return;
  rod.x = (int)rodTargetX(objs[(size_t)bi]);
}

void Board::rodUpdate() {
  if (!chainUsed) return;
  const int bi = bucketIndex();
  if (bi < 0) return;
  const Obj& bk = objs[(size_t)bi];
  Rod& r = rod;
  const bool electro = electroBall();
  const float top = 512.0f, bottom = 586.0f;
  auto wave = [](int a, int b) {
    int m = a % (2 * b);
    if (m > b) m = 2 * b - m;
    return F32((double)m / (double)b);
  };
  if (r.state == 1 || r.state == 2) r.age++;
  switch (r.state) {
    case 0:
      if (electro) {
        r.timer++;
        r.age = 0;
        r.state = 1;
        return;
      }
      break;
    case 1:
      if (electro) {
        r.vy = -1.0f;
        float ny = F32((double)r.y - 1.0);
        if (top > ny) ny = top;
        r.y = ny;
        if (ny == top) {
          r.vy = 0.0f;
          r.timer = 0;
          r.bob = 0;
          r.baseY = ny;
          r.state = 2;
        }
        rodMoveX();
        r.timer++;
        r.anchorX = r.x;
        return;
      }
      r.vy = 0.0f;
      r.state = 7;
      r.timer = 0;
      rodMoveX();
      r.timer++;
      r.anchorX = r.x;
      return;
    case 2:
      if (electro) {
        r.y = F32((double)wave(r.bob, 50) * 5.0 + (double)r.baseY);
        rodMoveX();
        r.bob++;
        r.timer++;
        r.anchorX = r.x;
        return;
      }
      r.vy = 0.0f;
      r.timer = 1;
      r.state = 7;
      r.anchorX = r.x;
      return;
    case 3:
      r.anchorX = r.x;
      r.y = F32((double)wave(r.bob, 50) * 5.0 + (double)r.baseY);
      r.bob++;
      if (r.timer > 30) {
        r.timer = 1;
        r.state = 4;
        return;
      }
      break;
    case 4: {
      const int rx = (int)(rng.rand() % 5u);
      r.x = r.anchorX + rx - 2;
      const int ry = (int)(rng.rand() % 5u);
      r.y = F32((double)ry + (double)r.baseY - 2.0);
      burnRand(9);
      if (r.timer > 50) {
        r.timer = 1;
        r.state = fever ? 9 : 5;
        return;
      }
      break;
    }
    case 5:
      if (fever) {
        r.timer = 1;
        r.state = 9;
        return;
      }
      {
        const float d1 = F32((double)bk.pos.x - (double)r.x);
        float fx = bk.pos.x, fy = bk.pos.y, fr = 0;
        if (bk.hasMover) bk.mv.calc(bk.mtick + 50, fx, fy, fr);
        const float d2 = F32((double)fx - (double)r.x);
        if (r.timer > 15 && (int)std::fabs(d1) > 100 && (int)std::fabs(d2) > 100) {
          r.timer = 1;
          r.state = 6;
          return;
        }
      }
      break;
    case 6:
      if (fever) {
        r.timer = 1;
        r.state = 9;
        return;
      }
      if (bottom <= r.y) {
        r.vy = 0.0f;
        r.timer++;
        r.state = 0;
        return;
      } else {
        r.vy = F32((double)r.vy + 0.03999999910593033);
        const double ny = (double)r.vy + (double)r.y;
        r.timer++;
        r.y = ny <= (double)bottom ? F32(ny) : bottom;
        return;
      }
    case 7:
      if (r.timer > 60) {
        r.timer = 0;
        r.state = 8;
      }
      rodMoveX();
      r.timer++;
      return;
    case 8:
      if (electro) {
        r.vy = 0.0f;
        r.state = 1;
        r.timer = 0;
        rodMoveX();
        r.timer++;
        return;
      }
      {
        r.vy = 1.0f;
        const double ny = (double)r.y + 1.0;
        r.y = (double)bottom >= ny ? F32(ny) : bottom;
        if (r.y == bottom) {
          r.vy = 0.0f;
          r.timer = 0;
          r.bob = 0;
          r.baseY = r.y;
          r.state = 0;
        }
        rodMoveX();
        r.timer++;
        return;
      }
    case 9:
      if (r.timer == 100) {
        r.y = bottom;
        r.state = 0;
      }
      break;
    default: break;
  }
  r.timer++;
}

void Board::chainLightning(int p0) {
  chainUsed = true;
  ev(Event::ChainStrike, p0);
  const Obj& P0 = objs[(size_t)p0];
  const float px = P0.pos.x, py = P0.pos.y;
  const float Tx = (float)rod.x, Ty = rod.y;
  constexpr double W = 7.0;
  const double dxd = (double)Tx - (double)px, dyd = (double)Ty - (double)py;
  const float dxf = F32(dxd), dyf = F32(dyd);
  const float d2tot = F32((double)dxf * dxf + (double)dyf * dyf);
  const float ln = F32(std::sqrt((double)d2tot));
  std::vector<ChainCand> A;
  std::vector<std::pair<int, float>> B;
  if (ln > 0.0f && std::isfinite(ln)) {
    const float e1x = F32((double)F32(dxd - W) * 10.0), e1y = F32((double)F32(dyd) * 10.0);
    const float e2x = F32((double)F32(dxd + W) * 10.0), e2y = e1y;
    const float k = F32(100.0 / (double)ln);
    const float nx = F32((double)k * (double)(-dyf)), ny = F32((double)k * (double)dxf);
    const float ux = F32((double)dxf / (double)ln), uy = F32((double)dyf / (double)ln);
    const float Qx = F32((double)F32((double)F32((double)ux * ln) * 10.0) + (double)px);
    const float Qy = F32((double)F32((double)F32((double)uy * ln) * 10.0) + (double)py);
    const float n1 = F32((double)e1x * e1x + (double)e1y * e1y);
    const float n2 = F32((double)e2x * e2x + (double)e2y * e2y);
    const float sq1 = F32(std::sqrt((double)n1)), sq2 = F32(std::sqrt((double)n2));
    const double m = (double)ny / (double)nx;
    const bool caseA = Ty >= py;
    for (int i = 0; i < (int)objs.size(); i++) {
      Obj& o = objs[(size_t)i];
      if (o.gone || !o.visible || o.lit || o.pegType < 1 || o.pegType > 4) continue;
      const bool brick = o.shape == Shape::Brick;
      const float X = o.pos.x, Y = o.pos.y;
      float tx = X, ty = Y;
      if (brick) {
        o.ppoly.ensureLines();
        if (o.ppoly.lines.empty()) continue;
        tx = o.ppoly.lines[0].x1;
        ty = o.ppoly.lines[0].y1;
      }
      const double t1 = ((double)tx - (double)px) * m + (double)py;
      if (caseA ? !(t1 <= (double)Y) : !(t1 >= (double)Y)) continue;
      const double t2 = ((double)ty - (double)Qx) * m + (double)Qy;
      if (caseA ? !(t2 >= (double)Y) : !(t2 <= (double)Y)) continue;
      const float a0 = F32((double)px - (double)X), a4 = F32((double)py - (double)Y);
      int r = o.pegType == 1 ? 10 : 12;
      if (brick) {
        float rr = 0.0f;
        const size_t nl = o.ppoly.lines.size();
        if ((size_t)o.brickIdx[0] < nl && (size_t)o.brickIdx[1] < nl && (size_t)o.brickIdx[2] < nl && (size_t)o.brickIdx[3] < nl) {
          const phys::Line& l0 = o.ppoly.lines[(size_t)o.brickIdx[0]];
          const float vx = F32((double)l0.x1 - (double)X);
          const float vy = F32((double)l0.y1 - (double)Y);
          rr = F32(std::sqrt((double)F32((double)vx * vx + (double)vy * vy)));
        }
        r = (int)((double)rr * 0.550000011920929 + (o.pegType != 1 ? 3.0 : 0.0));
      }
      const double c1 = (double)e1x * a4 - (double)e1y * a0;
      const float cc1 = F32(c1 / (double)n1);
      const float f10 = F32((double)sq1 * (double)std::fabs(cc1));
      const double c2 = (double)e2x * a4 - (double)e2y * a0;
      const float cc2 = F32(c2 / (double)n1);
      const float f14 = F32((double)sq1 * (double)std::fabs(cc2));
      if ((double)f10 <= 50.0 + r || (double)f14 <= 50.0 + r) A.push_back({i, (float)r, cc1, cc2, f10, f14});
    }
    float Pw = 0.0f;
    int prevLim = 0;
    for (;;) {
      int limit;
      if (!(Pw > 0.0f)) {
        limit = 100000;
        prevLim = 100000;
      } else {
        const float rr = F32(1.0 - (double)Pw / 50.0);
        const float v = F32(std::pow((double)rr, 2.5));
        limit = (int)std::ceil((double)F32((double)v * 15.0 + 2.0));
        if ((int)B.size() >= prevLim) break;
        prevLim = limit;
      }
      for (size_t kk = 0; kk < A.size();) {
        const ChainCand& c = A[kk];
        bool ok = (c.c1 > 0.0f && c.c2 < 0.0f) || (double)c.f10 <= (double)c.r + Pw || (double)c.f14 <= (double)c.r + Pw;
        Obj& o = objs[(size_t)c.obj];
        if (ok && o.shape == Shape::Brick) {
          ok = false;
          for (const phys::Line& ln2 : o.ppoly.lines) {
            const float la0 = F32((double)px - (double)ln2.x1), la4 = F32((double)py - (double)ln2.y1);
            const float g1 = F32(((double)e1x * la4 - (double)e1y * la0) / (double)n1);
            const float g2 = F32(((double)e2x * la4 - (double)e2y * la0) / (double)n2);
            const float h1 = F32((double)sq1 * (double)std::fabs(g1));
            const float h2 = F32((double)sq2 * (double)std::fabs(g2));
            if ((g1 > 0.0f && g2 < 0.0f) || (double)h1 <= (double)Pw + 5.0 || (double)h2 <= (double)Pw + 5.0) {
              ok = true;
              break;
            }
          }
        }
        if (!ok) {
          kk++;
          continue;
        }
        const float dx2 = F32((double)o.pos.x - (double)px), dy2 = F32((double)o.pos.y - (double)py);
        const float sqx = F32((double)dx2 * dx2);
        B.push_back({c.obj, F32((double)sqx + (double)dy2 * dy2)});
        A.erase(A.begin() + (long)kk);
      }
      if ((int)B.size() > limit) break;
      const float ratio = F32((double)Pw / 50.0);
      Pw = F32((double)Pw + (ratio < 0.25f ? 0.25 : ratio < 0.5f ? 0.5 : 1.0));
      if (!(Pw <= 50.0f)) break;
    }
  }
  if (killTimer < 0x28) killTimer = 0x28;
  if (B.empty()) return;
  std::stable_sort(B.begin(), B.end(), [](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.second < b.second; });
  const int n = (int)B.size();
  const bool fireBolt = curBall_ >= 0 && curBall_ < (int)balls.size() && balls[(size_t)curBall_].fire;
  int prev = p0;
  for (int i = 0; i < n; i++) {
    const int delay = 30 * i / n;
    const int hitsBefore = pegsHitShot;
    pegHit(B[(size_t)i].first, true);
    ev(Event::ChainBolt, prev, B[(size_t)i].first, delay | (fireBolt ? 0x100 : 0) | (hitsBefore << 16));
    burnRand(fireBolt ? 110 : 30);
    prev = B[(size_t)i].first;
  }
  ev(Event::ChainBolt, prev, -1, 30);
  burnRand(30);
  if (n >= 12) awardStyle(23);
  if (bucketIndex() >= 0) {
    rod.x = (int)objs[(size_t)bucketIndex()].pos.x;
    rod.timer = 0;
    rod.state = 4;
  }
}

}
