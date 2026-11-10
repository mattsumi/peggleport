#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

#include "sim.h"

namespace pp {

class AIMgr {
 public:
  AIMgr();
  ~AIMgr();
  AIMgr(const AIMgr&) = delete;
  AIMgr& operator=(const AIMgr&) = delete;
  void begin(const Board& b, int skill, const std::function<uint32_t()>& rnd, int mode = 0, float aim = 0);
  bool update(int framesPerTick = 10);
  bool thinking() const;
  float angle() const { return chosen_; }
  int candidates() const { return candidates_; }
  int bestScore() const { return bestScore_; }
  struct BallInfo { bool valid = false; int left = 0, score = 0, sum = 0; };
  const BallInfo& best() const { return bestInfo_; }
  const BallInfo& aimed() const { return aimInfo_; }

 private:
  struct Run;
  void finish();
  std::unique_ptr<Run> run_;
  std::function<uint32_t()> rnd_;
  BallInfo bestInfo_, aimInfo_;
  float chosen_ = 0;
  int bestScore_ = 0, candidates_ = 0, mode_ = 0;
  bool thinking_ = false;
};

}
