#include <map>
#include <vector>

#include "sim.h"

namespace pp {

const Mover* Board::subMover(const Mover& m) const {
  if (m.sub >= 0 && (size_t)m.sub < objs.size()) return &objs[(size_t)m.sub].mv;
  if (m.sub <= -2 && (size_t)(-2 - m.sub) < freeMovers_.size()) return &freeMovers_[(size_t)(-2 - m.sub)];
  return nullptr;
}

void Board::moverStep(Mover& m, int mtick, float& x, float& y, float& r) {
  if (!m.linked) {
    m.calc(mtick, x, y, r);
    return;
  }
  m.step(mtick, 0.0f, x, y, r, subMover(m));
}

void Board::moverStep(Obj& o, float& x, float& y, float& r) { moverStep(o.mv, o.mtick, x, y, r); }

void Board::drawCalc(const Mover& mv, int mtick, float frac, float& x, float& y, float& r) const {
  if (!mv.linked) {
    mv.calc(mtick - 1, frac, x, y, r);
    return;
  }
  Mover m = mv;
  m.step(mtick - 1, frac, x, y, r, subMover(m));
}

void Board::drawCalc(const Obj& o, float frac, float& x, float& y, float& r) const { drawCalc(o.mv, o.mtick, frac, x, y, r); }

void Board::emitDrawPos(const Emit& e, float frac, float& x, float& y) const {
  x = e.pos.x;
  y = e.pos.y;
  if (!e.hasMover || !e.canMove || e.mtick < 1) return;
  float r = 0.0f;
  drawCalc(e.mv, e.mtick, frac, x, y, r);
}

Emit Board::makeEmit(const Level& L, const Node& n, bool fromBase) {
  Emit e;
  e.node = &n;
  e.fromBase = fromBase;
  e.before = objs.size();
  e.canMove = n.flag("can_move");
  e.pos = {n.f("x"), n.f("y")};
  const Node* mn = nullptr;
  if (auto mv = n.val("mover")) mn = L.resolve(*mv);
  if (mn) {
    e.hasMover = true;
    e.mv = Mover::fromNode(*mn, cfg.fpBits == 24);
    float x = e.pos.x, y = e.pos.y, r = 0.0f;
    e.mv.calc(0, x, y, r);
    e.pos = {x, y};
  }
  return e;
}

void Board::updateEmit(Emit& e) {
  if (!e.hasMover || !(e.canMove || e.mv.sub != -1)) return;
  if (e.canMove) e.mtick++;
  float x = e.pos.x, y = e.pos.y, r = 0.0f;
  moverStep(e.mv, e.mtick, x, y, r);
  const float dx = x - e.pos.x, dy = y - e.pos.y;
  e.pos = {dx + e.pos.x, dy + e.pos.y};
}

void Board::linkFollowers(const Level& L, const std::vector<const Node*>& nodeOf, size_t first, const std::vector<const Node*>& emitNodeOf, size_t emitFirst) {
  const size_t nObj = nodeOf.size(), total = nObj + emitNodeOf.size();
  auto moverAt = [&](size_t i) -> Mover& { return i < nObj ? objs[first + i].mv : emitters[emitFirst + i - nObj].mv; };
  auto hasMoverAt = [&](size_t i) { return i < nObj ? objs[first + i].hasMover : emitters[emitFirst + i - nObj].hasMover; };
  auto nodeAt = [&](size_t i) { return i < nObj ? nodeOf[i] : emitNodeOf[i - nObj]; };
  std::vector<const Node*> moverNode(total, nullptr);
  bool any = false;
  for (size_t i = 0; i < total; i++) {
    if (!hasMoverAt(i)) continue;
    const Val* v = nodeAt(i)->val("mover");
    moverNode[i] = v ? L.resolve(*v) : nullptr;
    if (moverNode[i] && moverNode[i]->val("sub")) any = true;
  }
  if (!any) return;
  std::map<const Node*, size_t> owner;
  for (size_t i = 0; i < nObj; i++)
    if (moverNode[i]) owner.emplace(moverNode[i], first + i);
  std::map<const Node*, int> freeAt;
  for (size_t i = 0; i < total; i++) {
    const Node* mn = moverNode[i];
    if (!mn) continue;
    const Val* sv = mn->val("sub");
    const Node* sn = sv ? L.resolve(*sv) : nullptr;
    if (!sn || sn == mn) continue;
    Mover& f = moverAt(i);
    auto it = owner.find(sn);
    if (it != owner.end() && (i >= nObj || it->second != first + i)) {
      f.sub = (int)it->second;
      objs[it->second].mv.linked = true;
    } else {
      auto fr = freeAt.find(sn);
      if (fr == freeAt.end()) {
        freeMovers_.push_back(Mover::fromNode(*sn, cfg.fpBits == 24));
        fr = freeAt.emplace(sn, (int)freeMovers_.size() - 1).first;
      }
      f.sub = -2 - fr->second;
    }
    f.linked = true;
  }
  for (size_t i = 0; i < nObj; i++) {
    Obj& o = objs[first + i];
    if (!o.mv.linked || o.shape == Shape::Line || o.shape == Shape::None) continue;
    o.pos = {o.mv.outX, o.mv.outY};
    rebuild(o);
  }
  for (size_t i = nObj; i < total; i++) {
    Emit& e = emitters[emitFirst + i - nObj];
    if (e.mv.linked) e.pos = {e.mv.outX, e.mv.outY};
  }
}

}
