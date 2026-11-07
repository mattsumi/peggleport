#pragma once
#include <filesystem>
#include <string>

#include "profile.h"

namespace pp::ui {

struct LauncherResult {
  bool play = false;
  Game game = Game::Deluxe;
  std::filesystem::path dir;
  std::string error;
};

LauncherResult runLauncher(bool allowAutoStart);

}
