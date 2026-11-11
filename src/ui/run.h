#pragma once
#include <functional>
#include <string>

#include "ui/app.h"
#include "ui/start_target.h"

struct SDL_Window;
struct SDL_Renderer;

namespace pp::ui {

struct RunOptions {
  Game game = Game::Deluxe;
  StartTarget start;
  int winW = 1024, winH = 768;
  std::function<bool(Game)> canOpen;
  std::function<bool(Game)> chooseFolders;
  std::function<bool(Game)> hasGame;
  std::function<Game()> home;
  std::function<void(Game)> setHome;
};

enum class RunResult { Quit, SwitchGame, Failed };

class GameWindow {
 public:
  GameWindow() = default;
  GameWindow(const GameWindow&) = delete;
  GameWindow& operator=(const GameWindow&) = delete;
  ~GameWindow() { close(); }

  bool open(Game game, int w, int h, std::string& error);
  void close();
  bool isOpen() const { return win_ != nullptr; }
  SDL_Window* window() const { return win_; }
  SDL_Renderer* renderer() const { return ren_; }

 private:
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
};

RunResult runApp(const RunOptions& o, Vfs& vfs, GameWindow& window, GameRequest& next, std::string& error);

}
