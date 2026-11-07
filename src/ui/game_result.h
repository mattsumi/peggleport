#pragma once
#include <string>
#include <vector>

namespace pp::ui {

struct LevelOutcome {
  bool finished = false, won = false, cleared = false;
  int totalScore = 0, shots = 0, pegsHit = 0, bestShot = 0;
  int shotPoints = 0, feverPoints = 0, stylePoints = 0, freeBalls = 0, ballsLeft = 0;
  int orangeTotal = 0, orangeHit = 0, pegsTotal = 0, pegsCleared = 0, ticks = 0;
  int styleCount[24]{};
  int tryBonus = 0, effBonus = 0, tryNumber = 1, prevTotal = 0;
  int percentCleared() const { return pegsTotal ? pegsCleared * 100 / pegsTotal : 0; }
};

}
