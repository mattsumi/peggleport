#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "gamefind.h"
#include "profile.h"

struct SDL_Window;

namespace pp::ui {

struct FolderAnswer {
  enum class Status { Chosen, Cancelled, Unavailable };
  Status status = Status::Cancelled;
  std::filesystem::path path;
};

struct StartDialogs {
  std::function<int(SDL_Window* parent, const std::string& title, const std::string& text, const std::vector<std::string>& buttons, bool problem)> message;
  std::function<FolderAnswer(SDL_Window* parent, const std::string& title, const std::filesystem::path& startAt)> folder;
};

StartDialogs systemDialogs();

class GameFolders {
 public:
  GameFolders(std::filesystem::path storeFile, FindSources sources, StartDialogs dialogs);

  static std::filesystem::path defaultStoreFile();
  static std::filesystem::path programDir();

  void setWindow(SDL_Window* window) { window_ = window; }
  void detect();
  bool has(Game g) const { return !dir_[gameIndex(g)].empty(); }
  const std::filesystem::path& dir(Game g) const { return dir_[gameIndex(g)]; }
  Game home() const { return store_.home; }
  void setHome(Game g);
  const FoundGames& lastSearch() const { return lastSearch_; }
  bool gameToOpen(Game& g) const;
  bool askForAnyGame(const std::string& problem = std::string());
  bool askForGame(Game g);
  bool chooseFolders(Game running);
  bool readyFor(Game g);
  void drop(Game g);
  void tell(const std::string& text, bool problem = true);

 private:
  int adopt(const std::filesystem::path& chosen);
  std::filesystem::path writeStartLog() const;
  void remember(Game g, const std::filesystem::path& dir);
  void tellNoChooser();
  std::filesystem::path startFolder(Game g) const;

  std::filesystem::path storeFile_;
  FindSources sources_;
  StartDialogs dialogs_;
  GameFolderStore store_;
  std::filesystem::path dir_[2];
  FoundGames lastSearch_;
  SDL_Window* window_ = nullptr;
};

}
