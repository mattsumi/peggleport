#pragma once
#include <cstdint>
#include <string>

#include "profile.h"

namespace pp::ui {

enum class StartPlace { MainMenu, Adventure };

struct StartTarget {
  StartPlace place = StartPlace::MainMenu;
  bool handOver = false;
  std::string player;
  bool colorblind = false;
  uint64_t askedAt = 0;
};

struct GameRequest {
  Game game = Game::Deluxe;
  StartTarget start;
};

}
