#pragma once
#include "profile.h"

namespace pp::ui {

enum class StartPlace { MainMenu, Adventure };

struct StartTarget {
  StartPlace place = StartPlace::MainMenu;
};

struct GameRequest {
  Game game = Game::Deluxe;
  StartTarget start;
};

}
