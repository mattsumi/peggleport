#include <cmath>

#include "sim.h"
#include "physball.h"
#include "sim_internal.h"

namespace pp {

using namespace detail;

void Board::gunMouse(int mx, int my) {
  bool moved = mx != mouseX || my != mouseY;
  mouseX = mx;
  mouseY = my;
  if (moved && wheelLock == 0 && phase == Phase::Aim) mouseMoved = true;
}

void Board::gunWheel(int notches) {
  if (phase != Phase::Aim) return;
  const float step = 0.1f;
  float delta = (float)((double)step * (3.14159265358979323846 / 180.0));
  lmAim = gun::clampAim(lmAim + delta * (float)notches);
  wheelLock = 30;
}

void Board::gunClick() {
  if (phase == Phase::Aim && ballsLeft[cur] > 0) { if (lstate != 10) clickPending = true; }
  else if (phase == Phase::Shot) clickInShot();
}

void Board::gunLogic() {
  if (wheelLock != 0) wheelLock--;
  if (lstate == 10) return;
  if (mouseMoved) {
    mouseMoved = false;
    lmAim = gun::aimAngle(pivot(), mouseX, mouseY);
  }
  gun::setAngle(gunState, gunAngle, gunTarget, lmAim, false);

  if (fireCooldown < 1) {
    if (clickPending) {
      clickPending = false;
      fireHeld = true;
      if (balls.empty()) {
        fireCooldown = 8;
        return;
      }
    }
  } else {
    fireCooldown--;
    clickPending = false;
  }
  if (fireCooldown == 0 && fireHeld && phase == Phase::Aim && ballsLeft[cur] > 0) {
    if (balls.empty()) {
      fireHeld = clickPending = false;
      if (pturn[cur][P_ZEN] > 0) beginZen();
      else fire(gun::fireAngle(gunState, gunAngle, true));
    }
  }
  if (phase != Phase::Aim) fireHeld = false;
}

}
