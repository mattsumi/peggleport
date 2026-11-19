#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "gamefind.h"
#include "profile.h"

struct SDL_Window;

namespace pp::ui {

class App;

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

using GameWait = std::function<void(const std::function<bool()>& done)>;

class GameFolders {
 public:
  GameFolders(std::filesystem::path storeFile, FindSources sources, StartDialogs dialogs);
  GameFolders(std::filesystem::path storeFile, std::function<FindSources()> sources, StartDialogs dialogs);

  static std::filesystem::path defaultStoreFile();
  static std::filesystem::path programDir();

  void setWindow(SDL_Window* window) { window_ = window; }
  void setGame(App* app, GameWait wait);
  void detect();
  bool has(Game g) const { return !dir_[gameIndex(g)].empty(); }
  const std::filesystem::path& dir(Game g) const { return dir_[gameIndex(g)]; }
  const std::filesystem::path& installDir(Game g) const { return install_[gameIndex(g)].empty() ? dir_[gameIndex(g)] : install_[gameIndex(g)]; }
  bool copyFailed(Game g);
  Game home() const { return store_.home; }
  void setHome(Game g);
  const FoundGames& lastSearch() const { return lastSearch_; }
  bool gameToOpen(Game& g) const;
  bool recoverLaunch(Game& game, bool required, const std::string& problem);
  bool askForAnyGame(const std::string& problem = std::string());
  bool askForGame(Game g);
  std::optional<Game> chooseFolders(Game running);
  bool readyFor(Game g);
  void tell(const std::string& text, bool problem = true);

 private:
  int ask(const std::string& title, const std::string& text, const std::vector<std::string>& buttons, bool problem);
  int adopt(const std::filesystem::path& chosen, std::string& problem);
  std::filesystem::path writeStartLog() const;
  std::filesystem::path copyFolder(Game g) const;
  bool remember(Game g, const std::filesystem::path& dir, std::string& problem);
  void exclude(Game g);
  bool useSource(Game g, const std::filesystem::path& source);
  void tellNoChooser();
  std::filesystem::path startFolder(Game g) const;

  std::filesystem::path storeFile_;
  std::function<FindSources()> sources_;
  StartDialogs dialogs_;
  GameFolderStore store_;
  bool selectionUnavailable_ = false;
  std::filesystem::path dir_[kGameCount];
  std::filesystem::path install_[kGameCount];
  bool fromCopy_[kGameCount] = {};
  FoundGames lastSearch_;
  SDL_Window* window_ = nullptr;
  App* app_ = nullptr;
  GameWait wait_;
};

}
