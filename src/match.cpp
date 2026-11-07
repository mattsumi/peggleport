#include "match.h"
#include <algorithm>
#include <utility>
#include <memory>
#include <vector>
#include <string>

namespace pp {

bool Match::init(std::shared_ptr<const Level> base, std::shared_ptr<const Level> lv, BoardConfig cfg, int nPlayers, bool isParty, std::string* err) {
  players = std::max(1, std::min(4, nPlayers));
  party = isParty && players > 1;
  boards.clear();
  bs_.clear();
  resolved_.clear();
  rollbacks = dropped = 0;
  int nb = party ? players : 1;
  cfg.players = party ? 1 : players;
  cfg.duel = !party && players > 1;
  for (int i = 0; i < nb; i++) {
    Board b;
    if (!b.load(base, lv, cfg, err)) return false;
    boards.push_back(std::move(b));
    bs_.push_back({{}, boards.back()});
  }
  return true;
}

bool Match::canFire(int player) const {
  int bi = boardOf(player);
  if (bi < 0 || bi >= (int)boards.size()) return false;
  const Board& b = boards[bi];
  int slot = playerSlot(player);
  return b.phase == Phase::Aim && b.cur == slot && b.ballsLeft[slot] > 0 && bs_[bi].pending.empty();
}

void Match::submit(const ShotCmd& c) {
  if (c.board < 0 || c.board >= (int)boards.size()) return;
  bs_[c.board].pending.push_back(c);
  tryApply(c.board);
}

void Match::tryApply(int bi) {
  Board& B = boards[bi];
  BS& S = bs_[bi];
  while (!S.pending.empty()) {
    if (B.phase != Phase::Aim) return;
    ShotCmd c = S.pending.front();
    int slot = playerSlot(c.player);
    if (B.cur != slot || B.ballsLeft[slot] <= 0) {
      S.pending.pop_front();
      dropped++;
      continue;
    }
    if (c.tick > B.tick) return;
    if (c.tick < B.tick) {
      int now = B.tick;
      float ga = B.gunAngle;
      B = S.snap;
      B.events.clear();
      B.emit = false;
      rollbacks++;
      while (B.tick < c.tick) B.step();
      B.fire(c.angle);
      S.pending.pop_front();
      while (B.tick < now) stepBoard(bi);
      B.emit = true;
      (void)ga;
      continue;
    }
    B.fire(c.angle);
    S.pending.pop_front();
  }
}

void Match::stepBoard(int bi) {
  Board& B = boards[bi];
  Phase before = B.phase;
  B.step();
  if ((before == Phase::Shot || before == Phase::Popping) && (B.phase == Phase::Aim || B.phase == Phase::Over)) {
    bs_[bi].snap = B;
    resolved_.push_back({bi, B.shotIndex, B.hash()});
  }
}

void Match::step() {
  for (int bi = 0; bi < (int)boards.size(); bi++) {
    tryApply(bi);
    stepBoard(bi);
  }
}

std::vector<Resolved> Match::drainResolved() {
  std::vector<Resolved> r;
  r.swap(resolved_);
  return r;
}

bool Match::over() const {
  for (auto& b : boards)
    if (b.phase != Phase::Over) return false;
  return true;
}

}
