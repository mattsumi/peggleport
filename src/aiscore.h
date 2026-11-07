#pragma once
#include <functional>
#include <vector>
#include <cstdint>

#include "sim.h"

namespace pp {

class AIMgr {
 public:
  void begin(const Board& b, int skill, const std::function<uint32_t()>& rnd, int mode = 0, float aim = 0);
  bool update(int framesPerTick = 10);
  bool thinking() const { return thinking_; }
  float angle() const { return chosen_; }
  int candidates() const { return (int)cands_.size(); }
  int bestScore() const { return bestScore_; }
  int chosenGain() const { return chosenGain_; }
  int maxGain() const { return maxGain_; }
  struct BallInfo { bool valid = false; int left = 0, score = 0, sum = 0; };
  const BallInfo& best() const { return bestInfo_; }
  const BallInfo& aimed() const { return aimInfo_; }

 private:
  struct Cand {
    float angle = 0;
    Board sim;
    int frames = 0;
    bool done = false;
    int score = 0;
    int fColl = -1, nColl = 0;
    int pegF[3] = {-1, -1, -1};
    int orgF[2] = {-1, -1};
    int greenF = -1;
    int nPeg = 0, nOrg = 0;
    int left = 0, hits = 0;
    int sum = 0, aiScore = 0;
    std::vector<char> seen;
    bool f205 = false, f1f5 = false;
    bool fB = false, fC = false, fE = false;
  };
  void observe(Cand& c);
  std::vector<Cand> cands_;
  int base_ = 0, cur_ = 0, target_ = 500, skill_ = 1, oppScore_ = 0, ownScore_ = 0, master_ = 0;
  bool duel_ = false, guide_ = false, fireball_ = false;
  float defAngle_ = 4.712389f;
  bool thinking_ = false;
  int mode_ = 0, aimIdx_ = -1;
  bool computer_ = true, masterOn_ = false;
  BallInfo bestInfo_, aimInfo_;
  float chosen_ = 0;
  int bestScore_ = 0, chosenGain_ = 0, maxGain_ = 0;
  std::function<uint32_t()> rnd_;
  void finish();
};

}
