#include <algorithm>
#include <cmath>
#include <cstdint>

#include "aiscore.h"
#include "sim.h"
#include "sim_internal.h"

namespace pp {

using namespace detail;

namespace {
constexpr int kDivineTick = 1, kAchievedTick = 130, kFireTick = 180;
}

float Board::zenSearch() {
  AIMgr ai;
  ai.begin(*this, 10, [this] { return (uint32_t)rng.rand(); }, 1, clampAngle(lmAim));
  while (!ai.update(10000)) {
  }
  const float best = ai.angle();
  zenNote = 0;
  zenPct = 0;
  const AIMgr::BallInfo& A = ai.aimed();
  const AIMgr::BallInfo& B = ai.best();
  if (A.valid) {
    if (!B.valid || A.left == 0 || B.score <= A.score) {
      zenNote = 1;
    } else if (A.sum < B.sum && A.sum != 0) {
      int pct = B.sum * 100 / A.sum - 100;
      if (pct % 10 != 0) pct += 10 - pct % 10;
      zenPct = pct;
      zenNote = pct < 500 ? 2 : 3;
    }
  }
  return best;
}

void Board::beginZen() {
  fireHeld = clickPending = false;
  fireCooldown = 0;
  zenAngle = lmAim;
  setLState(10);
  ev(Event::Zen, 0);
}

void Board::updateZen() {
  if (stateTick == kDivineTick) {
    say("Divining Best Shot...", 150);
    ev(Event::Zen, 1);
  } else if (stateTick == kAchievedTick) {
    zenAngle = zenSearch();
    lmAim = zenAngle;
    gun::setAngle(gunState, gunAngle, gunTarget, zenAngle, true);
    say("Zen Achieved!", 150);
    ev(Event::Zen, 2, zenNote, zenPct);
  } else if (stateTick == kFireTick) {
    ev(Event::Zen, 3);
    phase = Phase::Aim;
    fire(gun::fireAngle(gunState, gunAngle, true));
  }
}

}
