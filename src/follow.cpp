#include <map>
#include <vector>

#include "sim.h"

namespace pp {

const Mover* Board::subMover(const Mover& m) const {
  if (m.sub >= 0 && (size_t)m.sub < objs.size()) return &objs[(size_t)m.sub].mv;
  if (m.sub <= -2 && (size_t)(-2 - m.sub) < freeMovers_.size()) return &freeMovers_[(size_t)(-2 - m.sub)];
  return nullptr;
}

void Board::moverStep(Obj& o, float& x, float& y, float& r) {
  Mover& m = o.mv;
  if (!m.linked) {
    m.calc(o.mtick, x, y, r);
    return;
  }
  m.step(o.mtick, 0.0f, x, y, r, subMover(m));
}

void Board::drawCalc(const Obj& o, float frac, float& x, float& y, float& r) const {
  if (!o.mv.linked) {
    o.mv.calc(o.mtick - 1, frac, x, y, r);
    return;
  }
  Mover m = o.mv;
  m.step(o.mtick - 1, frac, x, y, r, subMover(m));
}

void Board::linkFollowers(const Level& L, const std::vector<const Node*>& nodeOf, size_t first) {
  std::vector<const Node*> moverNode(nodeOf.size(), nullptr);
  bool any = false;
  for (size_t i = 0; i < nodeOf.size(); i++) {
    if (!objs[first + i].hasMover) continue;
    const Val* v = nodeOf[i]->val("mover");
    moverNode[i] = v ? L.resolve(*v) : nullptr;
    if (moverNode[i] && moverNode[i]->val("sub")) any = true;
  }
  if (!any) return;
  std::map<const Node*, size_t> owner;
  for (size_t i = 0; i < nodeOf.size(); i++)
    if (moverNode[i]) owner.emplace(moverNode[i], first + i);
  std::map<const Node*, int> freeAt;
  for (size_t i = 0; i < nodeOf.size(); i++) {
    const Node* mn = moverNode[i];
    if (!mn) continue;
    const Val* sv = mn->val("sub");
    const Node* sn = sv ? L.resolve(*sv) : nullptr;
    if (!sn || sn == mn) continue;
    Obj& f = objs[first + i];
    auto it = owner.find(sn);
    if (it != owner.end() && it->second != first + i) {
      f.mv.sub = (int)it->second;
      objs[it->second].mv.linked = true;
    } else {
      auto fr = freeAt.find(sn);
      if (fr == freeAt.end()) {
        freeMovers_.push_back(Mover::fromNode(*sn, cfg.fpBits == 24));
        fr = freeAt.emplace(sn, (int)freeMovers_.size() - 1).first;
      }
      f.mv.sub = -2 - fr->second;
    }
    f.mv.linked = true;
  }
  for (size_t i = 0; i < nodeOf.size(); i++) {
    Obj& o = objs[first + i];
    if (!o.mv.linked || o.shape == Shape::Line || o.shape == Shape::None) continue;
    o.pos = {o.mv.outX, o.mv.outY};
    rebuild(o);
  }
}

}
