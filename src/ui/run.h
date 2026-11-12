#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "ui/app.h"
#include "ui/start_target.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Cursor;

namespace pp::ui {

struct RunOptions {
  Game game = Game::Deluxe;
  StartTarget start;
  int winW = 1024, winH = 768;
  std::function<bool(Game)> canOpen;
  std::function<bool(Game)> chooseFolders;
  std::function<bool(Game)> hasGame;
  std::function<std::filesystem::path(Game)> gameDir;
  std::function<Game()> home;
  std::function<void(Game)> setHome;
  std::function<void(App*, std::function<void(const std::function<bool()>&)>)> setGame;
};

enum class RunResult { Quit, SwitchGame, Failed };

class GameWindow {
 public:
  GameWindow() = default;
  GameWindow(const GameWindow&) = delete;
  GameWindow& operator=(const GameWindow&) = delete;
  ~GameWindow() { close(); }

  bool open(Game game, int w, int h, bool keepPicture, std::string& error);
  void close();
  void keepPicture(SDL_Surface* picture, bool fillsWindow = false);
  SDL_Surface* takePicture(bool& fillsWindow);
  void keepSound(std::unique_ptr<Audio> sound);
  void keepFiles(std::shared_ptr<Vfs> files);
  Audio* leftSound() const { return leftSound_.get(); }
  void dropLeftGame();
  void keepCursor(SDL_Cursor* cursor);
  void dropCursor();
  bool isOpen() const { return win_ != nullptr; }
  SDL_Window* window() const { return win_; }
  SDL_Renderer* renderer() const { return ren_; }

 private:
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Surface* picture_ = nullptr;
  bool pictureFillsWindow_ = false;
  SDL_Cursor* leftCursor_ = nullptr;
  std::shared_ptr<Vfs> leftFiles_;
  std::unique_ptr<Audio> leftSound_;
};

RunResult runApp(const RunOptions& o, Vfs& vfs, GameWindow& window, GameRequest& next, std::string& error);

}
