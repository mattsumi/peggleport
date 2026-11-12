#include <SDL3/SDL.h>
#include <utility>
#include <memory>
#include <vector>
#include <string>

#include <algorithm>
#include <ctime>
#include <filesystem>

#include "gamepaths.h"
#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/replay.h"
#include "ui/screens.h"

namespace pp::ui {

static bool haveReplays(App& a) { return !listReplays(a.userDir / "replays", a.game == Game::Nights ? 1 : 0).empty(); }

static std::string latin1ToUtf8(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (c < 0x80) {
      o += (char)c;
    } else {
      o += (char)(0xC0 | (c >> 6));
      o += (char)(0x80 | (c & 0x3F));
    }
  }
  return o;
}

static std::string utf8ToLatin1(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size();) {
    const unsigned char lead = (unsigned char)s[i++];
    unsigned cp = lead;
    size_t more = 0;
    if (lead >= 0xF0) { cp = lead & 0x07u; more = 3; }
    else if (lead >= 0xE0) { cp = lead & 0x0Fu; more = 2; }
    else if (lead >= 0xC0) { cp = lead & 0x1Fu; more = 1; }
    else if (lead >= 0x80) { o += '?'; continue; }
    for (; more > 0 && i < s.size(); more--) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3Fu);
    o += cp < 256 ? (char)cp : '?';
  }
  return o;
}

class SaveReplayDialog : public Dialog {
 public:
  explicit SaveReplayDialog(App& a, bool show = false) : Dialog(a, show ? D_Replay2 : D_SaveReplay, true, show ? "Show Replay" : "Save Replay", "", "", BUTTONS_NONE), showing(show) {
    rope = true;
    auto l = std::make_unique<ListBox>();
    l->gfx = &a.gfx;
    l->centered = false;
    list = add(std::move(l));
    auto e = std::make_unique<EditBox>();
    e->gfx = &a.gfx;
    e->maxChars = 40;
    e->widthFont = "PLAIN";
    e->maxWidth = 240;
    e->ink = 0x000000;
    e->paper = true;
    edit = add(std::move(e));
    bRename = patchButton(101, "Rename", "IMAGE_DLG_BUTTON3", 0, 0);
    bDelete = patchButton(102, "Delete", "IMAGE_DLG_BUTTON3", 0, 0);
    bFolder = patchButton(103, "Open Folder", "IMAGE_DLG_BUTTON3", 0, 0);
    bCancel = patchButton(104, "Cancel", "IMAGE_DLG_BUTTON3", 0, 0);
    bSave = patchButton(105, show ? "Play" : "Save", "IMAGE_DLG_BUTTON3", 0, 0);
    refresh();
    if (!show) {
      edit->value = "Replay" + std::to_string(files.size() + 1);
      edit->caret = (int)edit->value.size();
      edit->selectAll = true;
    }
    edit->focused = true;
    focusW = edit;
    edit->onEnter = [this] { onButton(105); };
    list->onClick = [this](int i, int clicks) {
      list->select = i;
      app.playSample("BUTTON1");
      if (showing && clicks >= 2) { play(); return; }
      if (i >= 0 && i < (int)files.size()) {
        edit->value = stemOf(files[(size_t)i]);
        edit->caret = (int)edit->value.size();
        edit->selectAll = false;
        edit->focused = true;
        focusW = edit;
      }
    };
  }
  ListBox* list;
  EditBox* edit;
  Button *bRename, *bDelete, *bFolder, *bCancel, *bSave;
  const bool showing;
  std::vector<ReplayFile> files;
  std::string pending;
  bool overwriteOk = false;
  std::filesystem::path dir() const { return app.userDir / "replays"; }
  static std::string stemOf(const ReplayFile& f) { return utf8ToLatin1(pathToUtf8(f.path.stem())); }
  const ReplayFile* selected() const { return list->select >= 0 && list->select < (int)files.size() ? &files[(size_t)list->select] : nullptr; }
  void refresh() {
    files = listReplays(dir(), app.game == Game::Nights ? 1 : 0);
    list->lines.clear();
    for (auto& f : files) list->lines.push_back(stemOf(f));
    if (list->select >= (int)files.size()) list->select = (int)files.size() - 1;
    list->reveal();
  }
  float preferredHeight(float) override { return 383; }
  void layout() {
    list->x = 46;
    list->y = 68;
    list->w = 314;
    list->h = 133;
    list->back = 0x000050;
    edit->x = 100;
    edit->y = 224;
    edit->w = 256;
    edit->h = 20;
    Button* const b[5] = {bRename, bDelete, bFolder, bCancel, bSave};
    const float bx[5] = {36, 196, 36, 197, 35};
    const float by[5] = {255, 255, 289, 289, 323};
    for (int i = 0; i < 5; i++) {
      b[i]->x = bx[i];
      b[i]->y = by[i];
      b[i]->w = i == 4 ? 322.0f : (i % 2 == 0 ? 159.0f : 160.0f);
      b[i]->h = buttonH;
    }
  }
  void drawLayer(Gfx& g) override {
    layout();
    Dialog::drawLayer(g);
    if (Tex* fr = g.img("IMAGE_DLG_BLUEFRAME")) {
      g.fill(edit->x, edit->y, edit->w, edit->h, 0xffffff);
      g.drawBox(fr, edit->x - 8, edit->y - 10, edit->w + 16, edit->h + 18);
    }
    g.drawString("PLAIN", 35, 241, "Name", 0xffffff);
  }
  bool input(const Input& in) override {
    if (in.type == Input::Wheel) { list->wheel(in.wheel); return true; }
    if (in.type == Input::KeyDown && in.key == SDLK_ESCAPE && !scrollActive && !scrollingOff) { onButton(104); return true; }
    return Dialog::input(in);
  }
  void onButton(int id) override {
    if (id == 101) {
      const ReplayFile* f = selected();
      std::string n = edit->value;
      while (!n.empty() && n.back() == ' ') n.pop_back();
      if (!f || n.empty()) return;
      const std::filesystem::path to = dir() / pathFromUtf8(latin1ToUtf8(n) + replayExt(app.game == Game::Nights ? 1 : 0));
      std::error_code ec;
      if (std::filesystem::exists(to, ec)) return;
      std::filesystem::rename(f->path, to, ec);
      refresh();
    } else if (id == 102) {
      if (const ReplayFile* f = selected()) app.doConfirmDeleteReplay(stemOf(*f));
    } else if (id == 103) {
      std::error_code ec;
      std::filesystem::create_directories(dir(), ec);
      std::string url = "file://" + pathToUtf8(dir());
      SDL_OpenURL(url.c_str());
    } else if (id == 104) {
      doScrollOff();
    } else if (id == 105) {
      if (showing) {
        play();
      } else {
        overwriteOk = false;
        save();
      }
    }
  }
  void play() {
    const ReplayFile* f = selected();
    if (!f) return;
    app.playReplayFile(f->path);
  }
  void confirmDelete() {
    const ReplayFile* f = selected();
    if (!f) return;
    std::error_code ec;
    std::filesystem::remove(f->path, ec);
    refresh();
  }
  void save() {
    std::string n = edit->value;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    if (n.empty()) return;
    pending = latin1ToUtf8(n);
    std::error_code ec;
    const std::filesystem::path to = dir() / pathFromUtf8(pending + replayExt(app.game == Game::Nights ? 1 : 0));
    if (!overwriteOk && std::filesystem::exists(to, ec)) {
      app.doDialog(D_OverwriteReplay, true, "Overwrite Replay?", n + " already exists.  Do you want to overwrite it?", "", BUTTONS_YES_NO);
      return;
    }
    auto* gs = dynamic_cast<GameScreen*>(app.screen());
    if (gs && gs->replayAllShots()) {
      Dialog* d = app.doDialog(D_SaveAll, true, "Save All?", "Would you like to save all the shots in the replay or just this individual shot?", "", BUTTONS_YES_NO);
      d->setYesNo("Save Single", "Save All");
      return;
    }
    commit(false);
  }
  void commit(bool all) {
    auto* gs = dynamic_cast<GameScreen*>(app.screen());
    std::filesystem::path path;
    if (gs && gs->saveReplay(all, &path, pending)) {
      doScrollOff();
    } else {
      app.doDialog(D_ReplayError, true, "File Error", "Unable to open replay file.", "OK", BUTTONS_FOOTER);
    }
  }
};

void App::doReplaysDialog() {
  if (!haveReplays(*this)) {
    Dialog* d = doDialog(D_Tip, true, "Replays",
                         "When you make a good shot you can save it using the ^00FF00^Instant Replay^oldclr^ button. You can then view them here or share them with a friend!\n\nYou don't have any saved replays, save one while playing the game!",
                         "OK", BUTTONS_FOOTER);
    (void)d;
    return;
  }
  killDialog(D_Replay2);
  auto d = std::make_unique<SaveReplayDialog>(*this, true);
  Dialog* raw = d.get();
  positionDialog(raw, 400);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::doConfirmDeleteReplay(const std::string& name) {
  Dialog* d = doDialog(D_DeleteReplay, true, "Delete Replay?", "Are you sure you want to delete " + name + "?", "", BUTTONS_YES_NO);
  d->setYesNo("Delete", "Cancel");
}

void App::doSaveReplayDialog() {
  killDialog(D_SaveReplay);
  auto d = std::make_unique<SaveReplayDialog>(*this);
  Dialog* raw = d.get();
  positionDialog(raw, 400);
  raw->x = 200;
  raw->y = 60;
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
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
    case D_SaveAll:
      off(dlg);
      if (auto* sd = dynamic_cast<SaveReplayDialog*>(getDialog(D_SaveReplay))) sd->commit(!yes);
      return;
    case D_OverwriteReplay:
      off(dlg);
      if (yes)
        if (auto* sd = dynamic_cast<SaveReplayDialog*>(getDialog(D_SaveReplay))) {
          sd->overwriteOk = true;
          sd->save();
        }
      return;
    case D_SaveReplay: off(dlg); return;
    case D_Replay2:
      if (yes) {
        if (auto* rd = dynamic_cast<SaveReplayDialog*>(getDialog(dlg))) rd->play();
      } else {
        off(dlg);
      }
      return;
    case D_DeleteReplay:
      off(dlg);
      if (yes) {
        if (auto* rd = dynamic_cast<SaveReplayDialog*>(getDialog(D_Replay2))) rd->confirmDelete();
        else if (auto* sd = dynamic_cast<SaveReplayDialog*>(getDialog(D_SaveReplay))) sd->confirmDelete();
      }
      return;
    case D_ReplayError: off(dlg); return;
    default: off(dlg); return;
  }
}

void App::playReplayFile(const std::filesystem::path& path) {
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
