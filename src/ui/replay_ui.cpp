#include <SDL3/SDL.h>
#include <utility>
#include <memory>
#include <vector>
#include <string>

#include <algorithm>
#include <ctime>
#include <filesystem>

#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/replay.h"
#include "ui/screens.h"

namespace pp::ui {

static std::string commas(int v) {
  std::string s = std::to_string(v < 0 ? -(long long)v : (long long)v), o;
  for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += ','; o += s[i]; }
  return v < 0 ? "-" + o : o;
}

class ReplayFileDialog : public Dialog {
 public:
  explicit ReplayFileDialog(App& a) : Dialog(a, D_Replay2, true, "Replays", "", "", BUTTONS_YES_NO) {
    rope = true;
    auto l = std::make_unique<ListBox>();
    l->gfx = &a.gfx;
    list = add(std::move(l));
    del = patchButton(101, "Delete", "IMAGE_DLG_BUTTON3", 0, 0);
    folder = patchButton(102, "Open Folder", "IMAGE_DLG_BUTTON3", 0, 0);
    setYesNo("Play", "Close");
    files = listReplays(dir(), a.game == Game::Nights ? 1 : 0);
    for (auto& f : files) list->lines.push_back(label(f));
    list->select = files.empty() ? -1 : 0;
    list->onClick = [this](int i, int clicks) {
      list->select = i;
      if (clicks >= 2) app.dialogButton(D_Replay2, ID_YES);
    };
  }
  ListBox* list;
  Button *del, *folder;
  std::vector<ReplayFile> files;
  std::string dir() const { return (app.userDir / "replays").string(); }
  static std::string label(const ReplayFile& f) {
    const ReplayData& h = f.head;
    std::string t = h.title.empty() ? h.level : h.title;
    return t + "  -  " + h.player + "  -  " + commas(h.score);
  }
  const ReplayFile* selected() const { return list->select >= 0 && list->select < (int)files.size() ? &files[list->select] : nullptr; }
  float preferredHeight(float width) override { return Dialog::preferredHeight(width) + 0xff; }
  void layout() {
    list->x = insL + 10;
    list->y = contentTop(app.gfx);
    list->w = w - insL - insR - 20;
    list->h = 0x9b;
    del->x = yesBtn->x;
    del->y = yesBtn->y - 5 - del->h;
    del->w = yesBtn->w;
    folder->x = noBtn->x;
    folder->y = noBtn->y - 5 - folder->h;
    folder->w = noBtn->w;
  }
  void drawLayer(Gfx& g) override {
    layout();
    Dialog::drawLayer(g);
    if (Tex* fr = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(fr, list->x - 8, list->y - 8, list->w + 16, list->h + 16);
    g.drawWrapped("PLAIN", {insL, list->y + list->h + 14, w - insL - insR, 30}, "Send replays to friends from here, or drop friends' replays in here to view", 0xffffff, 1, Align::Center);
  }
  bool input(const Input& in) override {
    if (in.type == Input::Wheel) { list->wheel(in.wheel); return true; }
    if (in.type == Input::KeyDown) {
      if (in.key == SDLK_UP && list->select > 0) { list->select--; return true; }
      if (in.key == SDLK_DOWN && list->select + 1 < (int)list->lines.size()) { list->select++; return true; }
    }
    return Dialog::input(in);
  }
  void onButton(int id) override {
    if (id == 101) {
      if (const ReplayFile* f = selected()) app.doConfirmDeleteReplay(f->name);
    } else if (id == 102) {
      std::error_code ec;
      std::filesystem::create_directories(dir(), ec);
      std::string url = "file://" + dir();
      SDL_OpenURL(url.c_str());
    } else {
      Dialog::onButton(id);
    }
  }
  void confirmDelete() {
    const ReplayFile* f = selected();
    if (!f) return;
    std::error_code ec;
    std::filesystem::remove(f->path, ec);
    int idx = list->select;
    files.erase(files.begin() + idx);
    list->lines.erase(list->lines.begin() + idx);
    list->select = files.empty() ? -1 : std::min(idx, (int)files.size() - 1);
  }
  void play() {
    const ReplayFile* f = selected();
    if (!f) return;
    app.playReplayFile(f->path);
  }
};

static bool haveReplays(App& a) { return !listReplays((a.userDir / "replays").string(), a.game == Game::Nights ? 1 : 0).empty(); }

void App::doReplaysDialog() {
  if (!haveReplays(*this)) {
    Dialog* d = doDialog(D_Tip, true, "Replays",
                         "When you make a good shot you can save it using the ^00FF00^Instant Replay^oldclr^ button. You can then view them here or share them with a friend!\n\nYou don't have any saved replays, save one while playing the game!",
                         "OK", BUTTONS_FOOTER);
    (void)d;
    return;
  }
  killDialog(D_Replay2);
  auto d = std::make_unique<ReplayFileDialog>(*this);
  Dialog* raw = d.get();
  positionDialog(raw, 400);
  raw->x = 0x96;
  raw->y = 0x3c;
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::doConfirmDeleteReplay(const std::string& name) {
  Dialog* d = doDialog(D_DeleteReplay, true, "Delete Replay?", "Are you sure you want to delete " + name + "?", "", BUTTONS_YES_NO);
  d->setYesNo("Delete", "NO");
}

void App::doSaveReplayDialog() {
  Dialog* d = doDialog(D_SaveAll, true, "Save All?", "Would you like to save all the shots in the replay or just this individual shot?", "", BUTTONS_YES_NO);
  d->setYesNo("Save Single", "Save All");
}

void App::replayDialogButton(int dlg, int btn) {
  bool yes = btn == ID_YES;
  auto off = [&](int id) { if (auto* d = getDialog(id)) d->doScrollOff(); };
  auto* gs = dynamic_cast<GameScreen*>(cur_.get());
  switch (dlg) {
    case D_Replay1:
      off(dlg);
      if (gs && gs->viewing()) doToMenu();
      else if (!yes && gs && gs->canSaveReplay()) doSaveReplayDialog();
      return;
    case D_SaveAll: {
      off(dlg);
      std::string path;
      bool ok = gs && gs->saveReplay(!yes, &path);
      Dialog* d = doDialog(D_Tip, true, ok ? "Replay Saved" : "Save Replay",
                           ok ? "Your replay was saved as\n" + std::filesystem::path(path).filename().string() + "\n\nView it with the Replays button on the main menu." : "The replay could not be saved.", "OK", BUTTONS_FOOTER);
      (void)d;
      return;
    }
    case D_Replay2:
      if (yes) {
        if (auto* rd = dynamic_cast<ReplayFileDialog*>(getDialog(dlg))) rd->play();
      } else {
        off(dlg);
      }
      return;
    case D_DeleteReplay:
      off(dlg);
      if (yes)
        if (auto* rd = dynamic_cast<ReplayFileDialog*>(getDialog(D_Replay2))) rd->confirmDelete();
      return;
    case D_ReplayError: off(dlg); return;
    default: off(dlg); return;
  }
}

void App::playReplayFile(const std::string& path) {
  ReplayData d;
  if (!ReplayData::load(path, d) || d.game != (game == Game::Nights ? 1 : 0)) {
    doDialog(D_ReplayError, true, "Replay Load Error", "There was an error loading the replay.", "OK", BUTTONS_FOOTER);
    return;
  }
  if (auto* rd = getDialog(D_Replay2)) rd->doScrollOff();
  auto gs = std::make_unique<GameScreen>(*this, d);
  if (!gs->ok()) {
    doDialog(D_ReplayError, true, "Replay Load Error", "There was an error loading the replay.", "OK", BUTTONS_FOOTER);
    return;
  }
  gameMode = GameMode::QuickPlay;
  setScreen(std::move(gs), Left);
}

}
