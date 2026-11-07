#pragma once
#include <string>

#include "ui/app.h"

namespace pp::ui {

struct RunOptions {
  Game game = Game::Deluxe;
  int winW = 1024, winH = 768;
};

enum class RunResult { Quit, SwitchGame, Failed };

RunResult runApp(const RunOptions& o, Vfs& vfs, std::string& error);

}
