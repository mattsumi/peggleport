#pragma once
#include <deque>
#include <memory>
#include <vector>
#include <cstdint>
#include <string>

#include "sim.h"

namespace pp {

struct ShotCmd {
  uint32_t seq = 0;
  int player = 0, board = 0;
  float angle = 0;
  int32_t tick = 0;
};
struct Resolved {
  int board, shotIndex;
  uint64_t hash;
};

class Match {
 public:
  bool init(std::shared_ptr<const Level> base, std::shared_ptr<const Level> lv, BoardConfig cfg, int players, bool party, std::string* err);
  void submit(const ShotCmd& c);
  void step();
  bool canFire(int player) const;
  int boardOf(int player) const { return party ? player : 0; }
  int playerSlot(int player) const { return party ? 0 : player; }
  std::vector<Resolved> drainResolved();
  bool over() const;

  std::vector<Board> boards;
  bool party = false;
  int players = 1, rollbacks = 0, dropped = 0;

 private:
  struct BS {
    std::deque<ShotCmd> pending;
    Board snap;
  };
  std::vector<BS> bs_;
  std::vector<Resolved> resolved_;
  void tryApply(int bi);
  void stepBoard(int bi);
};

}
