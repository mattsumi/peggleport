#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <utility>
#include <memory>
#include <type_traits>
#include <vector>
#include <string>
#include <array>

#include "dmath.h"
#include "brick.h"
#include "gun.h"
#include "pegtypes.h"
#include "physball.h"
#include "sim_internal.h"

namespace pp {

using namespace detail;

namespace {

uint64_t fnvMix(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}
template <class T>
uint64_t mix(uint64_t h, const T& v) {
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(v)) {
      const T canonical = std::numeric_limits<T>::quiet_NaN();
      return fnvMix(h, &canonical, sizeof canonical);
    }
  }
  return fnvMix(h, &v, sizeof v);
}

}

void MTRand::seed(uint32_t s) {
  mt[0] = s;
  for (int i = 1; i < 624; i++) mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + (uint32_t)i;
  idx = 624;
}
uint32_t MTRand::next() {
  drawCount++;
  if (idx >= 624) {
    if (idx == 625) seed(5489);
    for (int i = 0; i < 624; i++) {
      uint32_t y = (mt[i] & 0x80000000u) | (mt[(i + 1) % 624] & 0x7fffffffu);
      mt[i] = mt[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0u);
    }
    idx = 0;
  }
  uint32_t y = mt[idx++];
  y ^= y >> 11;
  y ^= (y << 7) & 0x9d2c5680u;
  y ^= (y << 15) & 0xefc60000u;
  y ^= y >> 18;
  return y;
}

static void buildBrickLocal(const Node& n, Obj& o) {
  BrickParams p;
  p.type = (int)n.i("brick_type", 0);
  p.sub = n.has("brick_subdiv") ? (int)n.i("brick_subdiv") : 2;
  p.radius = n.f("brick_radius", 35.0f);
  p.ext = n.has("brick_angle") ? n.f("brick_angle") : 30.0f;
  p.ang150 = n.f("brick_left_angle", 0.0f);
  p.ang154 = n.f("brick_right_angle", 0.0f);
  p.thick = n.has("brick_thickness") ? n.f("brick_thickness") : 20.0f;
  p.rot = n.f("brick_rotation", -90.0f);
  p.off = {n.f("u_0x160", 0.0f), n.f("u_0x164", 0.0f)};
  BrickShape s = brickInitFromParams(p);
  o.local = s.pts;
  o.brickInner.assign(s.pts.begin() + s.innerStart, s.pts.begin() + s.innerEnd + 1);
  o.brickIdx[0] = s.innerEnd;
  o.brickIdx[1] = s.outerStart;
  o.brickIdx[2] = s.innerStart;
  o.brickIdx[3] = s.outerEnd;
  o.brickOuter.assign(s.pts.begin() + s.outerStart, s.pts.begin() + s.outerEnd + 1);
  std::reverse(o.brickOuter.begin(), o.brickOuter.end());
}

bool Board::makeObj(const Level& L, const Node& n, Obj& o) {
  switch (n.type) {
    case 5: o.shape = Shape::Circle; break;
    case 2: o.shape = Shape::Line; break;
    case 3: o.shape = Shape::Poly; break;
    case 6: o.shape = Shape::Brick; break;
    case 8: o.shape = Shape::Hole; break;
    default: return false;
  }
  o.collidable = n.flag("collidable");
  o.visible = n.flag("visible");
  o.canMove = n.flag("can_move");
  o.background = n.flag("background");
  o.foreground = n.flag("foreground");
  o.foreground2 = n.flag("foreground2");
  o.stopReset = n.flag("ball_stop_reset");
  o.bounce = n.f("bouncy");
  o.slide = n.f("rolly");
  o.maxBounceVel = n.f("max_bounce_vel");
  o.id = n.str("id");
  o.image = n.str("image");
  o.imgDx = n.f("image_dx");
  o.imgDy = n.f("image_dy");
  o.sound = (int)n.i("sound");
  o.subId = (int)n.i("sub_id");
  o.logic = n.str("logic");
  o.toggle = o.logic == "movetoggle" && (o.shape == Shape::Poly || o.shape == Shape::Brick);
  if (auto pv = n.val("peg"); pv && pv->n) {
    o.pegType = (int)pv->n->i("type");
    o.canBeOrange = pv->n->flag("variable");
  }
  if (o.id == "flipper") o.collidable = false;
  const Node* mn = nullptr;
  if (auto mv = n.val("mover")) mn = L.resolve(*mv);
  if (mn) {
    o.hasMover = true;
    o.mv = Mover::fromNode(*mn, cfg.fpBits == 24);
  }
  float baseRot = 0;
  switch (o.shape) {
    case Shape::Circle:
      o.pos = {n.f("x"), n.f("y")};
      o.radius = n.f("radius");
      break;
    case Shape::Line: {
      V2 a(n.f("x1"), n.f("y1")), b(n.f("x2"), n.f("y2"));
      o.pos = a;
      o.local = {a, b};
      break;
    }
    case Shape::Poly: {
      o.pos = {n.f("x"), n.f("y")};
      if (auto v = n.val("vertices"))
        for (size_t i = 0; i + 1 < v->verts.size(); i += 2) o.local.push_back({v->verts[i], v->verts[i + 1]});
      if (o.local.size() < 2) return false;
      if (n.has("poly_normal_dir")) o.normalDir = (int)n.i("poly_normal_dir");
      baseRot = n.f("poly_rotation");
      break;
    }
    case Shape::Brick:
      o.pos = {n.f("x"), n.f("y")};
      baseRot = n.f("poly_rotation");
      buildBrickLocal(n, o);
      break;
    case Shape::Hole:
      o.hw = (float)n.i("width") * 0.5f;
      o.hh = (float)n.i("height") * 0.5f;
      o.circular = n.flag("circular");
      o.score = (int)n.i("score");
      o.outDelay = (int)n.i("out_delay");
      o.pos = {n.f("x"), n.f("y")};
      break;
    default: break;
  }
  o.rot = baseRot;
  if (o.hasMover) {
    float x = o.pos.x, y = o.pos.y, r = o.rot;
    o.mv.calc(0, x, y, r);
    o.pos = {x, y};
    if (o.mv.rotating()) o.rot = r;
  }
  rebuild(o);
  return true;
}

void Board::rebuild(Obj& o) {
  switch (o.shape) {
    case Shape::Circle:
      o.x0 = o.pos.x - o.radius; o.x1 = o.pos.x + o.radius;
      o.y0 = o.pos.y - o.radius; o.y1 = o.pos.y + o.radius;
      return;
    case Shape::Hole:
      o.x0 = o.pos.x - o.hw; o.x1 = o.pos.x + o.hw;
      o.y0 = o.pos.y - o.hh; o.y1 = o.pos.y + o.hh;
      return;
    case Shape::Line: {
      V2 a = o.local[0], b = o.local[1];
      o.pline = phys::Line();
      o.pline.init(a.x, a.y, b.x, b.y);
      o.pline.twoSided = true;
      o.x0 = o.pline.bx0; o.y0 = o.pline.by0; o.x1 = o.pline.bx1; o.y1 = o.pline.by1;
      return;
    }
    default: break;
  }
  phys::Poly& p = o.ppoly;
  p = phys::Poly();
  p.pts.reserve(o.local.size());
  for (const V2& v : o.local) p.pts.push_back({v.x * o.scale, v.y * o.scale});
  p.cx = o.pos.x;
  p.cy = o.pos.y;
  p.rot = p.prevRot = o.rot;
  p.scale = o.scale;
  p.normalDir = o.normalDir;
  p.initFromPoints();
  o.x0 = p.bx0; o.y0 = p.by0; o.x1 = p.bx1; o.y1 = p.by1;
}

void Board::addLevel(const Level& L, bool fromBase) {
  std::vector<const Node*> nodeOf;
  auto find = [&](const Node* n) {
    for (size_t i = 0; i < nodeOf.size(); i++)
      if (nodeOf[i] == n) return (int)i;
    return -1;
  };
  size_t first = objs.size();
  auto build = [&](const Node* n) -> int {
    int k = find(n);
    if (k >= 0) return k < 0 ? -1 : (int)(first + k);
    Obj o;
    if (!makeObj(L, *n, o)) return -1;
    o.fromBase = fromBase;
    o.sort = (int)objs.size();
    objs.push_back(std::move(o));
    nodeOf.push_back(n);
    return (int)objs.size() - 1;
  };
  std::vector<const Node*> emitNodes;
  const size_t emitFirst = emitters.size();
  for (auto& e : L.entries)
    if (auto n = L.resolve(e)) {
      if (n->type == 9) {
        emitters.push_back(makeEmit(L, *n, fromBase));
        emitNodes.push_back(n);
      } else {
        build(n);
      }
    }
  for (size_t i = 0; i < nodeOf.size(); i++) {
    Obj& o = objs[first + i];
    if (o.shape != Shape::Hole) continue;
    if (auto v = nodeOf[i]->val("entry"))
      if (auto en = L.resolve(*v)) {
        int ex = build(en);
        objs[first + i].exitIdx = ex;
      }
  }
  linkFollowers(L, nodeOf, first, emitNodes, emitFirst);
}

void Board::widen() {
  float dw = (float)(W - 650), k = W / 650.0f;
  float bumper = 0.0f;
  for (const auto& o : objs)
    if (o.fromBase && o.id == "fever" && o.shape == Shape::Circle) bumper = std::max(bumper, o.radius);
  for (auto& o : objs) {
    if (!o.fromBase) {
      if (o.hasMover) o.mv.ax += dw * 0.5f;
      else o.pos.x += dw * 0.5f;
      if (o.shape == Shape::Line) for (auto& p : o.local) p.x += dw * 0.5f;
    } else if (o.id == "wall") {
      if (o.pos.x > 600) o.pos.x += dw;
      else if (o.pos.x > 100) {
        o.pos.x = W * 0.5f;
        for (auto& p : o.local) p.x *= k;
      }
    } else if (o.id == "freeball" || o.id == "freeballcover" || o.id == "bumperhole") {
      if (o.hasMover) {
        o.mv.ax += dw * 0.5f;
        o.mv.rx += dw * 0.5f;
      } else o.pos.x += dw * 0.5f;
    } else if (o.id == "fever") {
      if (o.hasMover) o.mv.ax *= k;
      else o.pos.x *= k;
      if (o.shape == Shape::Hole) o.hw = (o.hw + bumper) * k - bumper;
    } else if (o.id == "flipper") {
      if (o.hasMover ? o.mv.ax > 300 : o.pos.x > 300) {
        if (o.hasMover) o.mv.ax += dw;
        else o.pos.x += dw;
      }
    }
    if (o.mv.linked) {
      if (!o.fromBase) {
        o.mv.outX += dw * 0.5f;
        if (o.mv.sub != -1) o.mv.lastX += dw * 0.5f;
      }
      if (o.shape != Shape::Line) o.pos = {o.mv.outX, o.mv.outY};
    } else if (o.hasMover) {
      float x = o.pos.x, y = o.pos.y, r = o.rot;
      o.mv.calc(o.mtick, x, y, r);
      o.pos = {x, y};
    }
    rebuild(o);
  }
  for (auto& f : freeMovers_) {
    f.ax += dw * 0.5f;
    f.outX += dw * 0.5f;
  }
  for (auto& e : emitters) {
    if (e.fromBase) continue;
    if (e.mv.linked) {
      e.mv.ax += dw * 0.5f;
      e.mv.outX += dw * 0.5f;
      if (e.mv.sub != -1) e.mv.lastX += dw * 0.5f;
      e.pos = {e.mv.outX, e.mv.outY};
    } else if (e.hasMover) {
      e.mv.ax += dw * 0.5f;
      float x = e.pos.x, y = e.pos.y, r = 0.0f;
      e.mv.calc(e.mtick, x, y, r);
      e.pos = {x, y};
    } else {
      e.pos.x += dw * 0.5f;
    }
  }
}

bool Board::load(std::shared_ptr<const Level> base, std::shared_ptr<const Level> lv, const BoardConfig& c, std::string* err) {
  *this = Board();
  if (!lv) {
    if (err) *err = "no level";
    return false;
  }
  cfg = c;
  modSig = g_modTypeSig;
  base_ = base;
  level_ = lv;
  if (g_simHooks) g_simHooks->basePhysics(physics);
  cfg.physics.applyTo(physics);
  physics.sanitize();
  derivePhysics();
  W = std::max(650, cfg.boardWidth);
  players = std::clamp(cfg.players, 1, 4);
  if (base) addLevel(*base, true);
  baseCount_ = objs.size();
  addLevel(*lv, false);
  if (W > 650) widen();
  rng.seed(cfg.seed);
  for (int p = 0; p < 4; p++) master[p] = cfg.master[p] >= masters::kFirstPower && !masters::byPower(cfg.master[p]) ? 0 : cfg.master[p];
  setupPegs();
  if (!cfg.pegTypes.empty()) applyPegTypes();
  int balls0 = cfg.balls > 0 ? cfg.balls : (cfg.duel ? 6 : 10);
  for (int p = 0; p < players; p++) ballsLeft[p] = balls0;
  score[0] = cfg.startScore;
  if (players > 1) score[1] = cfg.startScore2;
  if (cfg.duel && players > 1) {
    uint32_t r = rng.rand();
    if (players != 2) {
      cur = (int)(r % (uint32_t)players);
    } else {
      int v = (int)(r & 1u);
      firstPrev = cfg.prevFirst;
      firstRun = cfg.prevFirstRun;
      if (v == firstPrev) {
        if (++firstRun > 1) {
          v = 1 - v;
          firstRun = 1;
          firstPrev = v;
        }
      } else {
        firstRun = 1;
        firstPrev = v;
      }
      cur = v;
    }
  }
  if (cfg.ch.active) applyChallenge();
  phase = Phase::Aim;
  if (g_simHooks) g_simHooks->levelStart(*this);
  if (modMasterInUse()) modMasterStart();
  if (g_simHooks) g_simHooks->stepEnd(*this);
  return true;
}

void Board::updateObj(Obj& o) {
  if (o.popTimer > 0 && --o.popTimer == 0) {
    o.gone = true;
    o.collidable = false;
    o.visible = false;
  }
  if (o.lit) o.litAge++;
  if (o.flash > 0) o.flash--;
  if (o.hasMover && (o.canMove || o.mv.sub != -1)) {
    if (o.canMove) o.mtick++;
    V2 old = o.pos;
    float x = o.pos.x, y = o.pos.y, r = o.rot;
    moverStep(o, x, y, r);
    if (o.shape == Shape::Hole) {
      const float dx = x - old.x, dy = y - old.y;
      o.pos = {dx + old.x, dy + old.y};
      o.x0 = o.x0 + dx; o.x1 = o.x1 + dx;
      o.y0 = o.y0 + dy; o.y1 = dy + o.y1;
    } else {
      o.pos = {x, y};
    }
    o.vel = {x - old.x, y - old.y};
    switch (o.shape) {
      case Shape::Circle:
        o.x0 = x - o.radius; o.x1 = o.radius + x;
        o.y0 = y - o.radius; o.y1 = o.radius + y;
        break;
      case Shape::Line:
        o.pline.setPos(x, y);
        o.pline.setVelocity(o.vel.x, o.vel.y);
        break;
      case Shape::Poly:
      case Shape::Brick:
        o.ppoly.setPos(x, y);
        o.ppoly.setVelocity(o.vel.x, o.vel.y);
        o.ppoly.updateRotation(r);
        o.rot = o.ppoly.rot;
        o.x0 = o.ppoly.bx0; o.y0 = o.ppoly.by0; o.x1 = o.ppoly.bx1; o.y1 = o.ppoly.by1;
        break;
      default: break;
    }
    o.moving = true;
  } else if (o.moving) {
    o.moving = false;
    o.vel = {};
    if (o.shape == Shape::Poly || o.shape == Shape::Brick) {
      o.ppoly.setVelocity(0.0f, 0.0f);
      o.ppoly.angVel = 0.0f;
    } else if (o.shape == Shape::Line) {
      o.pline.setVelocity(0.0f, 0.0f);
    }
  }
  if (o.fadeT > 0) {
    o.fadeT--;
    if (o.fadeOut) o.visible = o.fadeT != 0;
  }
  if (o.moveCount != 0 && --o.moveCount == 0) o.canMove = false;
  if (o.toggle) polyFlip(o);
}

bool Board::updateBall(Ball& b) {
  b.age++;
  if (b.holeCooldown > 0) b.holeCooldown--;
  if (b.holeOut > 0 && b.holeExit >= 0 && (size_t)b.holeExit < objs.size()) b.holeTarget = holeExitPoint(objs[(size_t)b.holeExit]);
  if (b.hat) hatUpdate(b);
  return phys::ballUpdate(b, grav_, physics.maxSpeed, physics.windOrDrag() ? &physics : nullptr);
}

std::vector<int> Board::gather(const Ball& b) const {
  std::vector<int> out;
  for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i < (int)objs.size(); i++) {
      const Obj& o = objs[i];
      if (!o.collidable || o.gone) continue;
      int cls = o.shape == Shape::Circle ? 0 : o.shape == Shape::Line ? 1 : (o.shape == Shape::Poly || o.shape == Shape::Brick) ? 2 : -1;
      if (cls != pass) continue;
      if (b.bx0 <= o.x1 && o.x0 <= b.bx1 && b.by0 <= o.y1 && o.y0 <= b.by1) out.push_back(i);
    }
  return out;
}

bool Board::collide(Ball& b, int bi, const std::vector<int>* pre, V2* firstHit) {
  std::vector<int> local;
  if (!pre) {
    phys::ballSweptBounds(b);
    local = gather(b);
    pre = &local;
  }
  std::vector<phys::Target> tg;
  tg.reserve(pre->size());
  const bool scaledBounce = physics.bounceChanged();
  std::vector<phys::Target*> cands;
  for (int i : *pre) {
    const Obj& o = objs[(size_t)i];
    int cls = o.shape == Shape::Circle ? 0 : o.shape == Shape::Line ? 1 : 2;
    phys::Target t;
    t.user = i;
    t.bounce = o.bounce;
    if (o.custom) {
      const PegTypeDef* cd = pegTypeDef(o.custom);
      if (cd && cd->bounce != 1.0f) t.bounce = (o.bounce != 0.0f ? o.bounce : (cls == 0 ? 0.9f : 0.82f)) * cd->bounce;
    }
    t.slide = o.slide;
    t.maxVel = o.maxBounceVel;
    t.stopReset = o.stopReset;
    t.hasPegInfo = o.pegType != 0;
    if (scaledBounce) {
      const float scale = t.hasPegInfo ? physics.ballBounce * physics.pegBounce : physics.ballBounce;
      if (scale != 1.0f) {
        t.scaled = true;
        t.bounceScale = scale;
      }
    }
    if (cls == 0) {
      t.kind = 3;
      t.center = o.pos;
      t.radius = o.radius;
      t.vel = o.vel;
    } else if (cls == 1) {
      t.kind = 4;
      t.line = const_cast<phys::Line*>(&o.pline);
    } else {
      t.kind = 5;
      t.poly = const_cast<phys::Poly*>(&o.ppoly);
    }
    tg.push_back(t);
  }
  for (auto& t : tg) cands.push_back(&t);
  std::vector<const phys::Target*> notify;
  bool hit = phys::doCollideUpdate(b, cands, firstHit, &notify);
  for (const phys::Target* t : notify) notes_.push_back({bi, t->user});
  return hit;
}

void Board::stepBalls() {
  notes_.clear();
  sapSync();
  std::vector<char> live(balls.size(), 0);
  for (int bi = 0; bi < (int)balls.size(); bi++) live[(size_t)bi] = balls[(size_t)bi].alive && updateBall(balls[(size_t)bi]);
  sapSort();
  std::vector<char> done(balls.size(), 0);
  auto indexOf = [&](int uid) {
    for (int i = 0; i < (int)balls.size(); i++)
      if (balls[(size_t)i].uid == uid) return i;
    return -1;
  };
  auto hatIndexOf = [&](int uid) {
    for (int i = 0; i < (int)balls.size(); i++)
      if (balls[(size_t)i].hatUid == uid) return i;
    return -1;
  };
  auto litThisTick = [&](int oi) {
    for (const Note& n : notes_)
      if (n.obj == oi) return true;
    return false;
  };
  int curFirst = -1;
  std::vector<int> cand;
  auto flush = [&]() {
    if (curFirst < 0) return;
    int bi = indexOf(curFirst - SAP_BALL);
    if (bi >= 0) {
      collide(balls[(size_t)bi], bi, &cand);
      done[(size_t)bi] = 1;
    } else if ((bi = hatIndexOf(curFirst - SAP_BALL)) >= 0) {
      collideHat(balls[(size_t)bi], bi, cand);
    }
    cand.clear();
  };
  for (const auto& key : std::vector<std::array<int, 4>>(sapPairs_.begin(), sapPairs_.end())) {
    const std::pair<int, int> pr{SAP_BALL + key[0], key[3]};
    int fi = indexOf(pr.first - SAP_BALL);
    const int hi = fi < 0 ? hatIndexOf(pr.first - SAP_BALL) : -1;
    if (fi < 0 && hi < 0) continue;
    if (!live[(size_t)(fi < 0 ? hi : fi)]) continue;
    if (pr.first != curFirst) {
      flush();
      curFirst = pr.first;
    }
    if (hi >= 0) {
      if (pr.second < SAP_BALL && balls[(size_t)hi].hatAge > 10) {
        const Obj& ho = objs[(size_t)pr.second];
        if (ho.collidable && !ho.gone && ho.pegType && !ho.lit && !litThisTick(pr.second)) cand.push_back(pr.second);
      }
      continue;
    }
    if (pr.second >= SAP_BALL) {
      int si = indexOf(pr.second - SAP_BALL);
      if (si >= 0 && live[(size_t)si]) phys::checkBallCollision(balls[(size_t)fi], balls[(size_t)si]);
      continue;
    }
    const Obj& o = objs[(size_t)pr.second];
    if (!o.collidable || o.gone) continue;
    if (o.shape == Shape::Hole) {
      const Ball& b = balls[(size_t)fi];
      if (!phys::holeContains(b.pos.x, b.pos.y, o.pos.x, o.pos.y, o.hw, o.hh, o.circular)) continue;
      notes_.push_back({fi, pr.second});
    } else {
      cand.push_back(pr.second);
    }
  }
  flush();
  for (int bi = 0; bi < (int)balls.size(); bi++)
    if (live[(size_t)bi] && !done[(size_t)bi]) phys::ballTranslate(balls[(size_t)bi], balls[(size_t)bi].vel.x, balls[(size_t)bi].vel.y);
  for (auto& nt : notes_)
    if (!nt.hat && balls[(size_t)nt.ball].alive && objs[(size_t)nt.obj].toggle) polyNotify(objs[(size_t)nt.obj]);
  for (auto& nt : notes_) {
    curBall_ = nt.ball;
    Ball& b = balls[nt.ball];
    if (!b.alive) continue;
    Obj& o = objs[nt.obj];
    if (nt.hat) {
      hatHit(b, nt.obj);
      continue;
    }
    if (o.shape == Shape::Hole) holeHit(b, nt.obj);
    else {
      {
        const int sinceNow = cfg.nights ? b.sinceHit : b.since;
        if ((o.id == "freeballcover" || o.id == "freeball") && b.fire) ev(Event::Bump, 1);
        else if (o.sound == 4 && (sinceNow > 4 || o.shape == Shape::Circle)) ev(Event::Bump, b.fire ? 1 : 0);
        if (o.id.compare(0, 5, "lbump") == 0) ev(Event::LevelBump, nt.obj);
      }
      noteContact(b, o);
      if (o.pegType) pegHit(nt.obj);
    }
  }
  curBall_ = -1;
  if (cfg.nights)
    for (auto& b : balls)
      if (b.rimTick > 0 && b.pos.y > (float)b.rimMaxY) b.rimMaxY = (int)b.pos.y;
  for (int i = 0; i < (int)balls.size(); i++)
    if (balls[i].alive && balls[i].spooky && (float)H < balls[i].pos.y - balls[i].r) spookyRespawn(i);
  for (auto& b : balls)
    if ((float)((int)H + 10) < b.pos.y - b.r || (float)((int)W + 10) < b.pos.x - b.r || b.pos.x + b.r < -10.0f || b.pos.y + b.r < -10.0f) b.alive = false;
  balls.erase(std::remove_if(balls.begin(), balls.end(), [](const Ball& b) { return !b.alive; }), balls.end());
  shotSteps++;
  lookahead();
}

void Board::updateZoom() {
  if (!balls.empty()) {
    float zy = 40.0f + balls[0].pos.y;
    if (400.0f < zy) {
      zy = zy + (zy - 400.0f) * 1.0f;
      if (610.0f < zy) zy = 610.0f;
    }
    zoomPivot = {75.0f + balls[0].pos.x, zy};
  }
  if (slowOn && zoomReq) {
    if (balls.size() < 2) {
      zoom += 0.02f;
      if (zoom > 1.0f) {
        zoom = 1.0f;
        zoomHold = 10;
        zoomReq = false;
      }
    } else {
      zoomReq = false;
    }
  } else if (zoom > 0.0f) {
    if (!slowOn) {
      zoomHold = 0;
      zoom -= 0.01f;
    } else if (zoomHold < 0) {
      zoom -= fever ? 0.003f : 0.01f;
    } else {
      zoomHold--;
    }
    if (zoom < 0.0f) zoom = 0.0f;
  }
  bool active = !(zoom <= 0.1f && (zoom <= 0.0f || !zoomReq));
  if (!active) {
    if (zoomFade > 0) zoomFade--;
  } else if (zoomFade < 20) {
    zoomFade++;
  }
}

void Board::step() {
  if (modSig != g_modTypeSig) syncModTypes();
  const bool mh = g_simHooks && !noModHooks;
  if (mh) g_simHooks->stepBegin(*this);
  stepCore();
  if (mh) g_simHooks->stepEnd(*this);
}

void Board::stepCore() {
  frames++;
  updateZoom();
  if (updateSlow()) {
    gun::updateCommon(gunState, gunAngle);
    return;
  }
  updateScoreDisplay();
  if (chainUsed) rodUpdate();
  shotLogic();
  tick++;
  gunLogic();
  size_t ei = 0;
  for (size_t i = 0; i < objs.size(); i++) {
    while (ei < emitters.size() && emitters[ei].before <= i) updateEmit(emitters[ei++]);
    updateObj(objs[i]);
  }
  while (ei < emitters.size()) updateEmit(emitters[ei++]);
  gun::update(gunState, gunAngle, gunTarget);
  if (phase == Phase::Shot || phase == Phase::Popping) stepBalls();
  if (bannerTimer > 0) bannerTimer--;
}

bool Board::fire(float angle) {
  if (phase != Phase::Aim || ballsLeft[cur] <= 0) return false;
  angle = clampAngle(angle);
  ballsLeft[cur]--;
  shotRand[0] = rng.rand();
  shotRand[1] = rng.rand();
  startOrange = orangeLeft;
  orangeHit = bucketShot = false;
  orangeHitShot = 0;
  Ball b;
  b.fire = pturn[cur][P_FIREBALL] > 0;
  if (cfg.nights && pturn[cur][P_CHAIN] > 0) {
    b.chain = true;
    chainUsed = true;
  }
  b.hat = hatOn;
  if (cfg.nights && superOn) b.mode = ultraOn ? 2 : 1;
  b.slot = 0;
  b.r = ballRadiusFor(b.fire);
  V2 p = muzzle(angle, b.r);
  phys::ballSetPos(b, p.x, p.y);
  b.pos = b.prev = p;
  b.vel = gun::launchVelocity(angle, gunSpeed_);
  if (b.hat) {
    hatInit(b, true);
    b.hatAge = tick > hatBorn_ ? tick - hatBorn_ : 0;
  }
  balls.push_back(b);
  phase = Phase::Shot;
  { int bits; static_assert(sizeof(bits) == sizeof(angle)); std::memcpy(&bits, &angle, sizeof bits); ev(Event::Shot, bits); }
  beginShotRules();
  shotSteps = 0;
  if (master[cur] >= masters::kFirstPower) modMasterShot();
  if (g_simHooks && !noModHooks) g_simHooks->stepEnd(*this);
  return true;
}

float Board::armedBallRadius() const { return ballRadiusFor(pturn[cur][P_FIREBALL] > 0); }

float Board::ballRadiusFor(bool fire) const {
  const float r = fire ? (cfg.nights ? 11.0f : 10.0f) : BALL_R;
  if (physics.ballRadius == BALL_R) return r;
  return fire ? r * (physics.ballRadius / BALL_R) : physics.ballRadius;
}

void Board::derivePhysics() {
  grav_ = physics.gravityY;
  gunSpeed_ = physics.launchSpeed;
  if (!cfg.ch.active) return;
  if (cfg.ch.gravityMod != 1.0f) grav_ = cfg.ch.gravityMod * grav_;
  if (cfg.ch.projSpeedMod != 1.0f) gunSpeed_ = cfg.ch.projSpeedMod * gunSpeed_;
}

void Board::setPhysics(const PhysicsEdit& e) {
  e.applyTo(physics);
  physics.sanitize();
  derivePhysics();
}

V2 Board::muzzle(float a, float r) const { return gun::launchPoint(pivot(), a, r); }

float Board::clampAngle(float a) { return gun::clampAim(a); }

float Board::aimAt(V2 t) const { return gun::aimAngle(pivot(), (int)std::floor(t.x), (int)std::floor(t.y), gun::MAX_GUN_ANGLE, physics.launchSpeed, physics.gravityY, physics.gravityX, physics.ballRadius); }

std::vector<V2> Board::guide(float a, int steps) const {
  std::vector<V2> out;
  V2 p = muzzle(a, physics.ballRadius), v = gun::launchVelocity(a, gunSpeed_);
  const float rr = physics.ballRadius;
  for (int i = 0; i < steps; i++) {
    v.y += grav_;
    if (physics.windOrDrag()) {
      v.x += physics.gravityX;
      if (physics.drag != 0.0f) { v.x *= 1.0f - physics.drag; v.y *= 1.0f - physics.drag; }
    }
    p = p + v;
    out.push_back(p);
    if (p.y > H || p.x < 0 || p.x > W) break;
    bool hit = false;
    for (auto& o : objs)
      if (o.shape == Shape::Circle && o.collidable && !o.gone && (o.pos - p).len2() < (o.radius + rr) * (o.radius + rr)) {
        hit = true;
        break;
      }
    if (hit) break;
  }
  return out;
}

uint64_t Board::hash() const {
  uint64_t h = 1469598103934665603ull;
  int ph = (int)phase;
  h = mix(h, tick); h = mix(h, ph); h = mix(h, cur); h = mix(h, score); h = mix(h, ballsLeft);
  h = mix(h, orangeLeft); h = mix(h, shotScore); h = mix(h, pegsHitShot); h = mix(h, shotIndex); h = mix(h, fever);
  h = mix(h, rng.idx); h = fnvMix(h, rng.mt, sizeof rng.mt);
  h = mix(h, master); h = mix(h, turns); h = mix(h, spookyCount); h = mix(h, style);
  h = mix(h, lstate); h = mix(h, stateTick); h = mix(h, popCountdown); h = mix(h, popSkip); h = mix(h, popCount); h = mix(h, killTimer);
  h = mix(h, forceStop); h = mix(h, streak); h = mix(h, greenRepicks); h = mix(h, popIdx_); h = mix(h, purple_);
  h = mix(h, (int)hitList_.size()); h = mix(h, coin.on); h = mix(h, coin.angle); h = mix(h, coin.age);
  h = mix(h, slowOn); h = mix(h, slowAcc); h = mix(h, slowSpeed); h = mix(h, slowTarget); h = mix(h, slowHold); h = mix(h, slowC3);
  for (auto& o : objs) {
    h = mix(h, o.gone); h = mix(h, o.lit); h = mix(h, o.pegType); h = mix(h, o.popTimer); h = mix(h, o.mtick);
    h = mix(h, o.pos.x); h = mix(h, o.pos.y); h = mix(h, o.rot); h = mix(h, o.collidable);
    if (o.toggle) h = mix(h, o.mirror);
    if (o.moveCount) { h = mix(h, o.moveCount); h = mix(h, o.canMove); }
    if (o.custom) { h = mix(h, o.custom); h = mix(h, o.hits); h = mix(h, o.hitTick); h = mix(h, o.radius); }
    if (o.mv.linked) { h = mix(h, o.mv.ax); h = mix(h, o.mv.ay); h = mix(h, o.mv.lastX); h = mix(h, o.mv.lastY); h = mix(h, o.mv.outX); h = mix(h, o.mv.outY); }
  }
  for (auto& b : balls) {
    h = mix(h, b.pos.x); h = mix(h, b.pos.y); h = mix(h, b.vel.x); h = mix(h, b.vel.y); h = mix(h, b.stuck);
    h = mix(h, b.fire); h = mix(h, b.spooky);
    if (b.hat) { h = mix(h, b.hatAge); h = mix(h, b.disp.x); h = mix(h, b.disp.y); }
    if (chainUsed) h = mix(h, b.chain);
  }
  if (chainUsed) {
    h = mix(h, rod.state); h = mix(h, rod.timer); h = mix(h, rod.bob); h = mix(h, rod.age);
    h = mix(h, rod.x); h = mix(h, rod.anchorX); h = mix(h, rod.y); h = mix(h, rod.vy); h = mix(h, rod.baseY);
  }
  for (auto& kv : modState) { h = fnvMix(h, kv.first.data(), kv.first.size()); h = mix(h, kv.second); }
  if (!physics.isDefault())
    for (int k = 0; k < PhysicsParams::KeyCount; k++) h = mix(h, physics.at(k));
  if (modMasterInUse())
    for (int p = 0; p < 4; p++) h = fnvMix(h, &pturn[p][masters::kFirstPower], (size_t)(masters::kPowerSlots - masters::kFirstPower) * sizeof(int));
  return h;
}


std::vector<V2> Board::superGuide(float a, int maxSteps) const {
  Board c = *this;
  c.balls.clear();
  Ball b;
  b.r = physics.ballRadius;
  b.pos = b.prev = c.muzzle(a, b.r);
  b.vel = gun::launchVelocity(a, gunSpeed_);
  c.balls.push_back(b);
  std::vector<V2> out;
  for (int i = 0; i < maxSteps; i++) {
    c.notes_.clear();
    Ball& bb = c.balls[0];
    if (!c.updateBall(bb)) continue;
    c.collide(bb, 0);
    out.push_back(bb.pos);
    if (bb.pos.y - bb.r > H || bb.pos.x < -20 || bb.pos.x > W + 20) break;
  }
  return out;
}

Board::GuideTrace Board::guideTrace(float a, bool superG) const {
  GuideTrace r;
  Board c = *this;
  c.emit = false;
  c.fxlog.clear();
  c.balls.clear();
  Ball b;
  b.r = physics.ballRadius;
  b.pos = b.prev = c.muzzle(a, b.r);
  b.vel = gun::launchVelocity(a, gunSpeed_);
  c.balls.push_back(b);
  int contacts = 0, tail = 0;
  const int maxSteps = lstate == 10 ? 300 : 200;
  for (int step = 0; step < maxSteps; step++) {
    c.notes_.clear();
    Ball& bb = c.balls[0];
    c.updateBall(bb);
    V2 hp;
    if (c.collide(bb, -1, nullptr, &hp)) {
      if (contacts == 0) {
        r.hitStep = step;
        r.hit = hp;
        r.hitAng = std::atan2(bb.lastN.y, bb.lastN.x);
        for (auto& n : c.notes_)
          if (c.objs[(size_t)n.obj].pegType) r.hitPeg = true;
        if (r.hitPeg && (tail == 0 || 5 < tail)) tail = 5;
      }
      contacts++;
      if (contacts == 1 && tail == 0) tail = superG ? 100 : 1;
    }
    if (contacts > 1 || bb.pos.y > (float)H) break;
    r.pts.push_back(bb.pos);
    if ((tail != 0 && --tail == 0)) break;
  }
  return r;
}

}
