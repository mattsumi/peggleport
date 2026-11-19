#include "ui/start.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "gamecopy.h"
#include "gamepaths.h"
#include "log.h"
#include "mods/text.h"
#include "ui/app.h"
#include "ui/dialog.h"

namespace pp::ui {

namespace {

namespace fs = std::filesystem;

const char* const kChoose = "Choose Folder...";
const char* const kDonor = "Peggle World of Warcraft Edition is a source for the WoW level pack. Choose Deluxe, Nights, or Extreme to play.";

struct FolderWait {
  std::mutex m;
  bool done = false;
  FolderAnswer answer;
};

void SDLCALL folderChosen(void* userdata, const char* const* files, int) {
  std::unique_ptr<std::shared_ptr<FolderWait>> held(static_cast<std::shared_ptr<FolderWait>*>(userdata));
  FolderWait& w = **held;
  std::lock_guard<std::mutex> lock(w.m);
  if (!files) {
    w.answer.status = FolderAnswer::Status::Unavailable;
  } else if (!files[0]) {
    w.answer.status = FolderAnswer::Status::Cancelled;
  } else {
    w.answer.status = FolderAnswer::Status::Chosen;
    w.answer.path = pathFromUtf8(files[0]);
  }
  w.done = true;
}

bool quitWaiting() {
  SDL_Event e;
  return SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0;
}

FolderAnswer systemFolder(SDL_Window* parent, const std::string& title, const fs::path& startAt) {
  auto wait = std::make_shared<FolderWait>();
  auto* held = new std::shared_ptr<FolderWait>(wait);
  std::string start = pathToUtf8(startAt);
  SDL_PropertiesID props = SDL_CreateProperties();
  if (parent) SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, parent);
  if (!start.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, start.c_str());
  if (!title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
  SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, folderChosen, held, props);
  SDL_DestroyProperties(props);
  for (;;) {
    SDL_PumpEvents();
    {
      std::lock_guard<std::mutex> lock(wait->m);
      if (wait->done) return wait->answer;
    }
    if (quitWaiting()) return FolderAnswer();
    SDL_Delay(10);
  }
}

int systemMessage(SDL_Window* parent, const std::string& title, const std::string& text, const std::vector<std::string>& buttons, bool problem) {
  std::vector<SDL_MessageBoxButtonData> data(buttons.size());
  for (size_t i = 0; i < buttons.size(); i++) {
    SDL_MessageBoxButtonFlags flags = 0;
    if (i == 0) flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    if (i + 1 == buttons.size()) flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    data[i].flags = flags;
    data[i].buttonID = (int)i;
    data[i].text = buttons[i].c_str();
  }
  SDL_MessageBoxData box;
  SDL_zero(box);
  box.flags = (problem ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION) | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
  box.window = parent;
  box.title = title.c_str();
  box.message = text.c_str();
  box.numbuttons = (int)data.size();
  box.buttons = data.data();
  int hit = -1;
  if (!SDL_ShowMessageBox(&box, &hit)) {
    log::error("start", std::string("A message could not be shown (") + SDL_GetError() + "): " + text);
    return -1;
  }
  return hit;
}

FoundGames lookInside(const fs::path& chosen) {
  FindSources s;
  s.wholeComputer = false;
  s.steamRoots.push_back({chosen, "chosen folder", false});
  s.gamesDirs.push_back({chosen, "chosen folder", false});
  FoundGames found = findGames(s);
  log::info("start", "Looking in the chosen folder " + pathToUtf8(chosen) + ".");
  for (auto& line : searchReport(found)) log::info("start", line);
  return found;
}

std::string lowerText(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

const int kFolderDialog = 62;

struct BoxAnswer {
  int hit = -1;
  bool closed = false;
};

class FolderBox : public Dialog {
 public:
  FolderBox(App& a, const std::string& text, const std::vector<std::string>& buttons, std::shared_ptr<BoxAnswer> answer)
      : Dialog(a, kFolderDialog, true, "Game Folders", text, buttons.size() == 1 ? buttons[0] : std::string(), buttons.size() == 1 ? BUTTONS_FOOTER : BUTTONS_YES_NO), answer_(std::move(answer)) {
    if (buttons.size() > 1) setYesNo(buttons[0], buttons[1]);
    if (buttons.size() > 2) last_ = patchButton(ID_CLOSE, buttons[2], "IMAGE_DLG_BUTTON3", 0, 0);
    escapeButton = last_ ? ID_CLOSE : ID_NO;
  }
  ~FolderBox() override { answer_->closed = true; }

  float preferredHeight(float width) override { return Dialog::preferredHeight(width) + (last_ ? buttonH + 5 : 0); }

  void place() {
    if (!last_) return;
    layoutButtons();
    last_->x = yesBtn->x;
    last_->y = yesBtn->y;
    last_->w = noBtn->x + noBtn->w - yesBtn->x;
    last_->h = buttonH;
    yesBtn->y -= buttonH + 5;
    noBtn->y = yesBtn->y;
  }

  void drawLayer(Gfx& g) override {
    place();
    Dialog::drawLayer(g);
  }

  void onButton(int id) override {
    if (answer_->hit < 0) answer_->hit = id == ID_NO ? 1 : id == ID_CLOSE ? 2 : 0;
    doScrollOff();
  }

 private:
  std::shared_ptr<BoxAnswer> answer_;
  Button* last_ = nullptr;
};

int gameMessage(App& app, const GameWait& wait, const std::string& text, const std::vector<std::string>& buttons) {
  std::string shown;
  for (char c : mods::gameText(text)) {
    shown += c;
    if (c == '^') shown += c;
  }
  auto answer = std::make_shared<BoxAnswer>();
  auto box = std::make_unique<FolderBox>(app, shown, buttons, answer);
  FolderBox* raw = box.get();
  const bool alone = !app.anyDialog();
  app.positionDialog(raw, (float)(alone ? 366 : 440));
  if (alone && app.screenName() == "mainmenu") {
    raw->x = 410 - raw->w / 2;
    raw->y = 280;
  }
  raw->y = std::max(10.0f, std::min(raw->y, 590 - raw->h));
  raw->place();
  raw->doScroll(true);
  app.addDialog(kFolderDialog, std::move(box));
  app.pauseBoard(true);
  wait([&] { return answer->closed; });
  app.pauseBoard(false);
  return answer->hit;
}

}

StartDialogs systemDialogs() {
  StartDialogs d;
  d.message = systemMessage;
  d.folder = systemFolder;
  return d;
}

GameFolders::GameFolders(fs::path storeFile, FindSources sources, StartDialogs dialogs) : storeFile_(std::move(storeFile)), sources_([sources = std::move(sources)] { return sources; }), dialogs_(std::move(dialogs)) {}

GameFolders::GameFolders(fs::path storeFile, std::function<FindSources()> sources, StartDialogs dialogs) : storeFile_(std::move(storeFile)), sources_(std::move(sources)), dialogs_(std::move(dialogs)) {}

void GameFolders::setGame(App* app, GameWait wait) {
  app_ = app;
  wait_ = std::move(wait);
}

int GameFolders::ask(const std::string& title, const std::string& text, const std::vector<std::string>& buttons, bool problem) {
  if (app_ && wait_) return gameMessage(*app_, wait_, text, buttons);
  return dialogs_.message ? dialogs_.message(window_, title, text, buttons, problem) : -1;
}

fs::path GameFolders::defaultStoreFile() {
  char* p = SDL_GetPrefPath("PegglePort", "PegglePort");
  if (!p) return {};
  fs::path r = pathFromUtf8(p) / "launcher.ini";
  SDL_free(p);
  return r;
}

fs::path GameFolders::programDir() {
  const char* b = SDL_GetBasePath();
  if (!b || !*b) return {};
  fs::path dir = pathFromUtf8(b);
  if (!dir.has_filename()) dir = dir.parent_path();
  fs::path contents = dir.parent_path();
  if (lowerText(pathToUtf8(contents.filename())) == "contents" && lowerText(pathToUtf8(contents.parent_path().extension())) == ".app") return contents.parent_path().parent_path();
  return dir;
}

fs::path GameFolders::copyFolder(Game g) const { return storeFile_.empty() ? fs::path() : gameCopyFolder(storeFile_.parent_path(), g); }

void GameFolders::detect() {
  store_.load(storeFile_);
  GameCopyCheck have[kGameCount];
  bool search = false;
  for (int i = 0; i < kGameCount; i++) {
    const fs::path copy = copyFolder(gameAt(i));
    if (!copy.empty()) have[i] = checkGameCopy(copy);
    if (have[i].state == GameCopyCheck::State::Intact && classifyGameDir(copy) != gameAt(i)) have[i] = {};
    if (have[i].state != GameCopyCheck::State::Intact) search = true;
  }
  FoundGames found;
  if (search) {
    FindSources s = sources_ ? sources_() : FindSources();
    for (int i = 0; i < kGameCount; i++) {
      s.remembered[i] = store_.dir[i];
      s.skip[i] = have[i].state == GameCopyCheck::State::Intact;
    }
    found = findGames(s);
    for (int i = 0; i < kGameCount; i++)
      if (s.skip[i]) {
        found.dir[i] = copyFolder(gameAt(i));
        found.from[i] = "its own copy";
      }
    log::info("start", "Looking for Peggle editions.");
    for (auto& line : searchReport(found)) log::info("start", line);
  } else {
    log::info("start", "The installed editions are read from their own copies.");
  }
  lastSearch_ = found;
  for (int i = 0; i < kGameCount; i++) {
    const Game g = gameAt(i);
    if (have[i].state == GameCopyCheck::State::Intact) {
      dir_[i] = copyFolder(g);
      install_[i] = store_.dir[i];
      fromCopy_[i] = true;
      log::info("start", std::string(gameTitle(g)) + " is read from its own copy in " + pathToUtf8(dir_[i]) + ".");
      continue;
    }
    if (!have[i].why.empty()) log::info("start", std::string("The copy of ") + gameTitle(g) + " is not used: " + have[i].why + ".");
    if (!found.dir[i].empty()) {
      std::string problem;
      remember(g, found.dir[i], problem);
    } else {
      dir_[i].clear();
      install_[i].clear();
      fromCopy_[i] = false;
    }
  }
}

bool GameFolders::copyFailed(Game g) {
  const int i = gameIndex(g);
  if (!fromCopy_[i]) return false;
  log::warn("start", std::string("The copy of ") + gameTitle(g) + " in " + pathToUtf8(dir_[i]) + " could not be opened.");
  discardGameCopy(dir_[i]);
  fromCopy_[i] = false;
  dir_[i].clear();
  if (retried_[i]) return false;
  retried_[i] = true;
  detect();
  return has(g);
}

fs::path GameFolders::writeStartLog() const {
  if (storeFile_.empty()) return {};
  fs::path file = storeFile_.parent_path() / "start.log";
  return log::exportToFile(file) ? file : fs::path();
}

void GameFolders::setHome(Game g) {
  if (store_.home == g) return;
  store_.home = g;
  store_.save(storeFile_);
}

bool GameFolders::gameToOpen(Game& g) const {
  Game first = store_.home;
  if (has(first)) {
    g = first;
    return true;
  }
  for (int i = 0; i < kGameCount; ++i) {
    if (has(gameAt(i))) {
      g = gameAt(i);
      return true;
    }
  }
  return false;
}

bool GameFolders::recoverLaunch(Game& game, bool required, const std::string& problem) {
  drop(game);
  if (required) {
    tell(problem);
    return false;
  }
  if (gameToOpen(game)) {
    tell(problem);
    return true;
  }
  return askForAnyGame(problem + "\n\nChoose the folder that holds main.pak.") && gameToOpen(game);
}

bool GameFolders::remember(Game g, const fs::path& dir, std::string& problem) {
  const int i = gameIndex(g);
  const fs::path copy = copyFolder(g);
  GameFolderStore selected = store_;
  selected.dir[i] = dir;
  bool saved = false;
  auto saveSelection = [&] {
    saved = store_.dir[i] == dir || storeFile_.empty() || selected.save(storeFile_);
    return saved;
  };
  bool useCopy = false;
  if (!copy.empty()) {
    const auto have = checkGameCopy(copy);
    useCopy = have.state == GameCopyCheck::State::Intact && sameFolder(pathFromUtf8(have.source), dir) && classifyGameDir(copy) == g;
    if (!useCopy) {
      const auto result = collectGameFiles(dir, copy, g, saveSelection);
      useCopy = result.ok;
      if (!useCopy) {
        problem = "Could not keep a copy of " + std::string(gameTitle(g)) + ": " + result.why;
        log::warn("start", problem);
        if (has(g) || have.state == GameCopyCheck::State::Intact || (!store_.dir[i].empty() && !sameFolder(store_.dir[i], dir)) || classifyGameDir(dir) != g) return false;
      }
    }
  }
  if (!saved && !saveSelection()) {
    problem = "The game folders could not be written to " + pathToUtf8(storeFile_);
    log::warn("start", problem);
    if (has(g) || !store_.dir[i].empty()) return false;
  }
  store_ = std::move(selected);
  dir_[i] = useCopy ? copy : dir;
  install_[i] = dir;
  fromCopy_[i] = useCopy;
  log::info("start", std::string(gameTitle(g)) + " is now read from " + pathToUtf8(dir_[i]));
  return true;
}

void GameFolders::drop(Game g) {
  int i = gameIndex(g);
  dir_[i].clear();
  install_[i].clear();
  fromCopy_[i] = false;
  if (store_.dir[i].empty()) return;
  store_.dir[i].clear();
  store_.save(storeFile_);
}

void GameFolders::tell(const std::string& text, bool problem) {
  if (problem) log::warn("start", text);
  ask("PegglePort", text, {"OK"}, problem);
}

void GameFolders::tellNoChooser() {
  fs::path games = programDir() / "Games";
  tell("I could not open a folder chooser on this computer.\n\nMake a folder named Games next to PegglePort (" + pathToUtf8(games) +
       "), put your Peggle game folder into it and start PegglePort again.");
}

fs::path GameFolders::startFolder(Game g) const {
  for (Game c : {g, otherGame(g)}) {
    fs::path d = install_[gameIndex(c)];
    if (d.empty()) d = store_.dir[gameIndex(c)];
    if (!d.empty() && d.has_parent_path()) return d.parent_path();
  }
  return {};
}

int GameFolders::adopt(const fs::path& chosen, std::string& problem) {
  fs::path direct = resolveGameDir(chosen);
  if (!direct.empty()) {
    bool donor;
    const auto g = classifyGameDir(direct, &donor);
    if (!g) {
      if (donor) problem = kDonor;
      return 0;
    }
    return remember(*g, direct, problem) ? 1 : 0;
  }
  FoundGames found = lookInside(chosen);
  int n = 0;
  for (int i = 0; i < kGameCount; i++) {
    if (found.dir[i].empty()) continue;
    if (remember(gameAt(i), found.dir[i], problem)) n++;
  }
  return n;
}

bool GameFolders::askForAnyGame(const std::string& problem) {
  if (!dialogs_.message || !dialogs_.folder) return false;
  std::string text = problem;
  if (text.empty()) {
    text = "PegglePort could not find a supported Peggle installation on this computer.";
    std::vector<std::string> looked = searchSummary(lastSearch_);
    if (!looked.empty()) {
      text += "\n\nWhere I looked:";
      for (auto& line : looked) text += "\n" + line;
    }
    fs::path file = writeStartLog();
    if (!file.empty()) text += "\n\nThe full list is in " + pathToUtf8(file) + ".";
    text += "\n\nChoose the folder the game is installed in.";
  }
  bool warn = !problem.empty();
  for (;;) {
    if (ask("PegglePort", text, {kChoose, "Quit"}, warn) != 0) return false;
    FolderAnswer a = dialogs_.folder(window_, "Choose the Peggle folder holding main.pak", startFolder(Game::Deluxe));
    if (a.status == FolderAnswer::Status::Unavailable) {
      tellNoChooser();
      return false;
    }
    if (a.status != FolderAnswer::Status::Chosen) return false;
    text.clear();
    if (adopt(a.path, text) > 0) return true;
    if (text.empty()) text = "There is no supported Peggle game in " + pathToUtf8(a.path) + ".\n\nChoose the folder that holds main.pak.";
    warn = true;
    log::warn("start", "No game in the chosen folder " + pathToUtf8(a.path));
  }
}

bool GameFolders::askForGame(Game g) {
  if (!dialogs_.message || !dialogs_.folder) return false;
  const std::string title = gameTitle(g);
  std::string text;
  for (;;) {
    if (!text.empty() && ask("PegglePort", text, {kChoose, "Cancel"}, true) != 0) return false;
    FolderAnswer a = dialogs_.folder(window_, "Choose the folder " + title + " is installed in", startFolder(g));
    if (a.status == FolderAnswer::Status::Unavailable) {
      tellNoChooser();
      return false;
    }
    if (a.status != FolderAnswer::Status::Chosen) return false;
    fs::path found = resolveGameDir(a.path);
    std::optional<Game> seen = g;
    if (!found.empty()) {
      bool donor;
      seen = classifyGameDir(found, &donor);
      if (donor) { text = kDonor; continue; }
      if (!seen) found.clear();
    } else {
      FoundGames inside = lookInside(a.path);
      found = inside.dir[gameIndex(g)];
      if (found.empty())
        for (int i = 0; i < kGameCount; ++i)
          if (inside.has(gameAt(i))) { seen = gameAt(i); break; }
    }
    if (seen && seen != g) {
      text = "That folder holds " + std::string(gameTitle(*seen)) + ", not " + title + ".\n\nChoose the folder " + title + " is installed in.";
      continue;
    }
    if (found.empty()) {
      text = "There is no " + title + " in " + pathToUtf8(a.path) + ".\n\nChoose the folder " + title + " is installed in. It is the folder that holds main.pak.";
      continue;
    }
    if (remember(g, found, text)) return true;
  }
}

std::optional<Game> GameFolders::chooseFolders(Game running) {
  if (!dialogs_.message) return {};
  const int group = ask("Game Folders", "Choose a game to open or change its folder.", {"Deluxe / Nights", "Extreme", "Close"}, false);
  if (group != 0 && group != 1) return {};
  Game g = Game::Extreme;
  if (group == 0) {
    std::string text;
    for (Game edition : {Game::Deluxe, Game::Nights})
      text += std::string(gameTitle(edition)) + ":\n" + (has(edition) ? pathToUtf8(installDir(edition)) : "not found") + "\n\n";
    const int hit = ask("Game Folders", text, {"Deluxe", "Nights", "Close"}, false);
    if (hit != 0 && hit != 1) return {};
    g = gameAt(hit);
  }
  if (g != running && has(g)) {
    const int action = ask("Game Folders", gameTitle(g), {"Open", kChoose, "Cancel"}, false);
    if (action == 0) {
      if (combinedEdition(g)) setHome(g);
      return g;
    }
    if (action != 1) return {};
  }
  const fs::path before = has(g) ? installDir(g) : fs::path();
  if (!askForGame(g) || (g == running && sameFolder(before, installDir(g)))) return {};
  if (combinedEdition(g)) setHome(g);
  return g;
}

bool GameFolders::readyFor(Game g) {
  if (has(g) && !resolveGameDir(dir(g)).empty()) return true;
  detect();
  if (has(g)) return true;
  tell(std::string(gameTitle(g)) + " is not installed on this computer, or I could not find it.\n\nYou can choose its folder with Game Folders... in Options.");
  return false;
}

}
