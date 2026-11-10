#include <algorithm>
#include <utility>
#include <vector>
#include <array>

#include "sim.h"
#include "physball.h"
#include "sim_internal.h"

namespace pp {

const float* Board::sapBox(int id) const {
  if (id < SAP_BALL) return &objs[(size_t)id].x0;
  for (const Ball& b : balls) {
    if (b.uid == id - SAP_BALL) return &b.bx0;
    if (b.hatUid == id - SAP_BALL) return &b.hx0;
  }
  return nullptr;
}

std::array<int, 4> Board::sapKey(int first, int second) const {
  if (second >= SAP_BALL) return {first - SAP_BALL, 2, second - SAP_BALL, second};
  const Shape sh = objs[(size_t)second].shape;
  int rank = sh == Shape::Hole ? 1 : sh == Shape::Circle ? 3 : sh == Shape::Line ? 4 : 5;
  return {first - SAP_BALL, rank, second, second};
}

bool Board::sapOverlap(int a, int b) const {
  const float* p = sapBox(a);
  const float* q = sapBox(b);
  if (!p || !q) return false;
  return p[0] <= q[2] && q[0] <= p[2] && p[1] <= q[3] && q[1] <= p[3];
}

void Board::sapAdd(int id) {
  const bool ball = id >= SAP_BALL;
  for (const SapEnd& e : sapX_) {
    if (!e.start) continue;
    int o = e.id;
    const bool oball = o >= SAP_BALL;
    if (ball) {
      if (!sapOverlap(id, o)) continue;
      if (!oball) sapInsert(id, o);
      else if (id < o) sapInsert(id, o);
      else sapInsert(o, id);
    } else if (oball && sapOverlap(id, o)) {
      sapInsert(o, id);
    }
  }
  const float* bx = sapBox(id);
  if (!bx) return;
  auto put = [&](std::vector<SapEnd>& list, int axis) {
    for (int k = 0; k < 2; k++) {
      const bool start = k == 0;
      float v = bx[axis + (start ? 0 : 2)];
      size_t lo = 0, hi = list.size();
      while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const float* mb = sapBox(list[mid].id);
        float mv = mb ? mb[axis + (list[mid].start ? 0 : 2)] : 0.0f;
        if (mv < v) lo = mid + 1;
        else hi = mid;
      }
      list.insert(list.begin() + (long)lo, SapEnd{id, start});
    }
  };
  put(sapX_, 0);
  put(sapY_, 1);
}

void Board::sapRemove(int id) {
  auto drop = [&](std::vector<SapEnd>& list) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const SapEnd& e) { return e.id == id; }), list.end());
  };
  drop(sapX_);
  drop(sapY_);
  for (auto it = sapPairs_.begin(); it != sapPairs_.end();) {
    if (SAP_BALL + (*it)[0] == id || (*it)[3] == id) it = sapPairs_.erase(it);
    else ++it;
  }
}

void Board::sapSortList(std::vector<SapEnd>& list, int axis) {
  auto val = [&](const SapEnd& e) {
    const float* b = sapBox(e.id);
    return b ? b[axis + (e.start ? 0 : 2)] : 0.0f;
  };
  for (size_t i = 1; i < list.size(); i++) {
    size_t j = i;
    while (j > 0) {
      if (val(list[j]) > val(list[j - 1])) break;
      const SapEnd A = list[j - 1], B = list[j];
      const bool aball = A.id >= SAP_BALL, bball = B.id >= SAP_BALL;
      if (A.start && !B.start) {
        if (aball) {
          if (!bball) sapErase(A.id, B.id);
          else if (A.id < B.id) sapErase(A.id, B.id);
          else sapErase(B.id, A.id);
        } else if (bball) {
          sapErase(B.id, A.id);
        }
      } else if (!A.start && B.start) {
        if (aball) {
          if (sapOverlap(A.id, B.id)) {
            if (!bball || A.id < B.id) sapInsert(A.id, B.id);
            else sapInsert(B.id, A.id);
          }
        } else if (bball && sapOverlap(A.id, B.id)) {
          sapInsert(B.id, A.id);
        }
      }
      std::swap(list[j - 1], list[j]);
      j--;
    }
  }
}

void Board::sapSync() {
  sapIn_.resize(objs.size(), 0);
  for (size_t i = 0; i < objs.size(); i++) {
    Obj& o = objs[i];
    const bool want = o.shape != Shape::None && (o.collidable || sapAll_) && !o.gone;
    if (want && !sapIn_[i]) { sapAdd((int)i); sapIn_[i] = 1; }
    else if (!want && sapIn_[i]) { sapRemove((int)i); sapIn_[i] = 0; }
  }
  std::vector<int> gone;
  for (const SapEnd& e : sapX_) {
    if (e.id < SAP_BALL || !e.start) continue;
    bool present = false;
    for (const Ball& b : balls) present |= b.uid == e.id - SAP_BALL || b.hatUid == e.id - SAP_BALL;
    if (!present) gone.push_back(e.id);
  }
  for (int id : gone) sapRemove(id);
  for (Ball& b : balls) {
    if (b.uid == 0) {
      b.uid = ++ballUid_;
      phys::ballSyncBounds(b);
      sapAdd(SAP_BALL + b.uid);
    }
    if (b.hat && b.hatUid == 0) {
      b.hatUid = ++ballUid_;
      sapAdd(SAP_BALL + b.hatUid);
    }
  }
}

void Board::sapSort() {
  sapSortList(sapX_, 0);
  sapSortList(sapY_, 1);
}

}
