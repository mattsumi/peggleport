#include "ui/start.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

#include "gamepaths.h"
#include "log.h"
#include "ui/app.h"
#include "ui/dialog.h"

namespace pp::ui {

namespace {

namespace fs = std::filesystem;

const char* const kChoose = "Choose Folder...";

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

std::string folderKey(const fs::path& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  if (ec || c.empty()) c = p.lexically_normal();
  return pathToUtf8(c);
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
  for (char c : text) {
    shown += c;
    if (c == '^') shown += c;
  }
  auto answer = std::make_shared<BoxAnswer>();
  auto box = std::make_unique<FolderBox>(app, shown, buttons, answer);
  FolderBox* raw = box.get();
  const bool alone = !app.anyDialog();
  app.positionDialog(raw, alone ? 366 : 440);
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

GameFolders::GameFolders(fs::path storeFile, FindSources sources, StartDialogs dialogs) : storeFile_(std::move(storeFile)), sources_(std::move(sources)), dialogs_(std::move(dialogs)) {}

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

void GameFolders::detect() {
  store_.load(storeFile_);
  FindSources s = sources_;
  for (int i = 0; i < 2; i++) s.remembered[i] = store_.dir[i];
  FoundGames found = findGames(s);
  log::info("start", "Looking for Peggle Deluxe and Peggle Nights.");
  for (auto& line : searchReport(found)) log::info("start", line);
  lastSearch_ = found;
  bool changed = false;
  for (int i = 0; i < 2; i++) {
    dir_[i] = found.dir[i];
    if (!found.dir[i].empty() && found.dir[i] != store_.dir[i]) {
      store_.dir[i] = found.dir[i];
      changed = true;
    }
  }
  if (changed && !store_.save(storeFile_)) log::warn("start", "The game folders could not be written to " + pathToUtf8(storeFile_));
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
  if (has(otherGame(first))) {
    g = otherGame(first);
    return true;
  }
  return false;
}

void GameFolders::remember(Game g, const fs::path& dir) {
  int i = gameIndex(g);
  dir_[i] = dir;
  store_.dir[i] = dir;
  log::info("start", std::string(gameTitle(g)) + " is now read from " + pathToUtf8(dir));
  if (!store_.save(storeFile_)) log::warn("start", "The game folders could not be written to " + pathToUtf8(storeFile_));
}

void GameFolders::drop(Game g) {
  int i = gameIndex(g);
  dir_[i].clear();
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
       "), put your Peggle Deluxe or Peggle Nights folder into it and start PegglePort again.");
}

fs::path GameFolders::startFolder(Game g) const {
  for (Game c : {g, otherGame(g)}) {
    fs::path d = dir(c);
    if (d.empty()) d = store_.dir[gameIndex(c)];
    if (!d.empty() && d.has_parent_path()) return d.parent_path();
  }
  return {};
}

int GameFolders::adopt(const fs::path& chosen) {
  fs::path direct = resolveGameDir(chosen);
  if (!direct.empty()) {
    Game g = classifyGameDir(direct);
    if (g == Game::Deluxe) g = classifyGameDir(chosen);
    remember(g, direct);
    return 1;
  }
  FoundGames found = lookInside(chosen);
  int n = 0;
  for (int i = 0; i < 2; i++) {
    if (found.dir[i].empty()) continue;
    remember(gameAt(i), found.dir[i]);
    n++;
  }
  return n;
}

bool GameFolders::askForAnyGame(const std::string& problem) {
  if (!dialogs_.message || !dialogs_.folder) return false;
  std::string text = problem;
  if (text.empty()) {
    text = "PegglePort plays your own copy of Peggle Deluxe or Peggle Nights, and I could not find one on this computer.";
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
    FolderAnswer a = dialogs_.folder(window_, "Choose the folder Peggle Deluxe or Peggle Nights is installed in", startFolder(Game::Deluxe));
    if (a.status == FolderAnswer::Status::Unavailable) {
      tellNoChooser();
      return false;
    }
    if (a.status != FolderAnswer::Status::Chosen) return false;
    if (adopt(a.path) > 0) return true;
    text = "There is no Peggle game in " + pathToUtf8(a.path) + ".\n\nChoose the folder Peggle Deluxe or Peggle Nights is installed in. It is the folder that holds main.pak.";
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
    Game seen = g;
    if (!found.empty()) {
      seen = classifyGameDir(found);
      if (seen == Game::Deluxe) seen = classifyGameDir(a.path);
    } else {
      FoundGames inside = lookInside(a.path);
      found = inside.dir[gameIndex(g)];
      if (found.empty() && inside.has(otherGame(g))) seen = otherGame(g);
    }
    if (seen != g) {
      text = "That folder holds " + std::string(gameTitle(seen)) + ", not " + title + ".\n\nChoose the folder " + title + " is installed in.";
      continue;
    }
    if (found.empty()) {
      text = "There is no " + title + " in " + pathToUtf8(a.path) + ".\n\nChoose the folder " + title + " is installed in. It is the folder that holds main.pak.";
      continue;
    }
    remember(g, found);
    return true;
  }
}

bool GameFolders::chooseFolders(Game running) {
  if (!dialogs_.message) return false;
  std::string text;
  for (int i = 0; i < 2; i++) text += std::string(gameTitle(gameAt(i))) + ":\n" + (dir_[i].empty() ? std::string("not found") : pathToUtf8(dir_[i])) + "\n\n";
  text += "Which game do you want to choose a folder for?";
  int hit = ask("Game Folders", text, {gameTitle(Game::Deluxe), gameTitle(Game::Nights), "Close"}, false);
  if (hit != 0 && hit != 1) return false;
  Game g = gameAt(hit);
  std::string before = has(g) ? folderKey(dir(g)) : std::string();
  if (!askForGame(g)) return false;
  bool reopen = g == running && folderKey(dir(g)) != before;
  if (reopen) tell(std::string(gameTitle(g)) + " opens again now, from " + pathToUtf8(dir(g)) + ".", false);
  else tell(std::string(gameTitle(g)) + " is ready in " + pathToUtf8(dir(g)) + ".", false);
  return reopen;
}

bool GameFolders::readyFor(Game g) {
  if (has(g) && !resolveGameDir(dir(g)).empty()) return true;
  detect();
  if (has(g)) return true;
  tell(std::string(gameTitle(g)) + " is not installed on this computer, or I could not find it.\n\nYou can choose its folder with Game Folders... in Options.");
  return false;
}

}
