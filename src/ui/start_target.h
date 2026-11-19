#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "profile.h"

namespace pp::ui {

enum class StartPlace { MainMenu, Adventure, Level, Challenge, LevelList, ChallengeList, SinglePlayer, NightsHub };

struct DuelTally {
  int wins[2]{}, fevers[2]{}, points[2]{}, streakOwner = -1, streak = 0;
};

struct StartTarget {
  StartPlace place = StartPlace::MainMenu;
  bool handOver = false;
  std::string player;
  bool colorblind = false;
  uint64_t askedAt = 0;
  GameMode mode = GameMode::QuickPlay;
  int stage = 0, level = 0;
  int challenge = 0, step = 0;
  std::string challengeLevel;
  int page = -1;
  DuelTally duel;
  std::vector<int> newTrophies;
};

struct GameRequest {
  Game game = Game::Deluxe;
  StartTarget start;
};

}
