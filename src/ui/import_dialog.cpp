#include "ui/import_dialog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>

#include "gamepaths.h"

namespace pp::ui {

namespace {

struct Chosen {
  std::mutex m;
  bool ready = false;
  int status = 0;
  std::string path;
  uint64_t ticket = 0;
};

Chosen& chosen() {
  static Chosen c;
  return c;
}

std::atomic<uint64_t> g_ticket{0};

void SDLCALL onChosen(void* userdata, const char* const* files, int) {
  Chosen& c = chosen();
  std::lock_guard<std::mutex> lock(c.m);
  c.ticket = (uint64_t)(intptr_t)userdata;
  c.status = !files ? 2 : !files[0] ? 1 : 0;
  c.path = c.status == 0 ? files[0] : std::string();
  c.ready = true;
}

std::string gameName(Game g) { return g == Game::Nights ? "Peggle Nights" : "Peggle Deluxe"; }

}

ImportDialog::ImportDialog(App& a) : Dialog(a, D_ImportProfile, true, "Import Players", "", "", BUTTONS_YES_NO) {
  rope = true;
  auto l = std::make_unique<ListBox>();
  l->gfx = &a.gfx;
  list = add(std::move(l));
  fileBtn = patchButton(101, "Choose File", "IMAGE_DLG_BUTTON3", 0, 0);
  folderBtn = patchButton(102, "Choose Folder", "IMAGE_DLG_BUTTON3", 0, 0);
  setYesNo("Import", "Close");
  yesBtn->enabled = false;
  list->onClick = [this](int i, int) {
    list->select = i;
    status.clear();
  };
}

void ImportDialog::layout() {
  list->x = insL + 10;
  list->y = contentTop(app.gfx);
  list->w = w - insL - insR - 20;
  list->h = 0x60;
  fileBtn->x = yesBtn->x;
  fileBtn->y = yesBtn->y - 5 - fileBtn->h;
  fileBtn->w = yesBtn->w;
  folderBtn->x = noBtn->x;
  folderBtn->y = noBtn->y - 5 - folderBtn->h;
  folderBtn->w = noBtn->w;
}

std::vector<std::string> ImportDialog::detailLines() const {
  std::vector<std::string> v;
  int i = selected();
  if (!haveScan || i < 0 || i >= (int)scan.profiles.size()) return v;
  const ImportedProfile& ip = scan.profiles[(size_t)i];
  v = describeImported(ip);
  if (ip.readable) {
    std::string target = uniqueProfileName(app.profiles, ip.name);
    if (ip.game == app.game && target != ip.name) v.push_back("A player called " + ip.name + " already exists, so this one will be called " + target + ".");
  }
  return v;
}

void ImportDialog::drawLayer(Gfx& g) {
  layout();
  yesBtn->enabled = haveScan && selected() >= 0 && selected() < (int)scan.profiles.size() && scan.profiles[(size_t)selected()].readable;
  Dialog::drawLayer(g);
  if (Tex* fr = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(fr, list->x - 8, list->y - 8, list->w + 16, list->h + 16);
  float ty = list->y + list->h + 14;
  float th = fileBtn->y - ty - 6;
  std::string text;
  if (!status.empty()) {
    text = status;
  } else if (!haveScan) {
    text = "Bring in your players from the original Peggle Deluxe or Peggle Nights. Choose the folder named userdata where the original game keeps its saves, or one player file such as user1.dat. The original files are only read, never changed.";
    std::string usual;
    for (auto& w : originalSaveFolders(app.game)) {
      std::error_code ec;
      if (std::filesystem::equivalent(w, app.userDir, ec)) continue;
      usual = pathToUtf8(w);
      break;
    }
    text += usual.empty() ? "\n\nNo saves of the original game were found in the usual places on this computer." : "\n\nUsual place on this computer:\n" + usual;
  } else {
    for (auto& s : detailLines()) text += (text.empty() ? "" : "\n") + s;
    for (size_t k = 0; k < scan.notes.size(); k++) text += (text.empty() ? "" : k == 0 ? "\n\n" : "\n") + scan.notes[k];
    if (scan.profiles.empty() && text.empty()) text = "No players were found there.";
  }
  g.pushClip(list->x, ty, list->w, th);
  g.drawWrapped("PLAIN", {list->x, ty, list->w, th}, text, 0xffffff, 1, Align::Left);
  g.popClip();
}

void ImportDialog::update() {
  Dialog::update();
  if (!waiting) return;
  Chosen& c = chosen();
  std::string path;
  int st = 0;
  {
    std::lock_guard<std::mutex> lock(c.m);
    if (!c.ready || c.ticket != ticket) return;
    c.ready = false;
    st = c.status;
    path = c.path;
  }
  waiting = false;
  if (st == 2) status = "The file chooser is not available on this computer.";
  else if (st == 0 && !path.empty()) load(pathFromUtf8(path));
}

bool ImportDialog::input(const Input& in) {
  if (in.type == Input::Wheel) {
    list->wheel(in.wheel);
    return true;
  }
  if (in.type == Input::KeyDown) {
    if (in.key == SDLK_UP && list->select > 0) { list->select--; status.clear(); return true; }
    if (in.key == SDLK_DOWN && list->select + 1 < (int)list->lines.size()) { list->select++; status.clear(); return true; }
  }
  return Dialog::input(in);
}

void ImportDialog::choose(bool folder) {
  if (waiting) return;
  std::string start;
  auto where = originalSaveFolders(app.game);
  if (where.empty()) where = originalSaveFolders(app.game == Game::Nights ? Game::Deluxe : Game::Nights);
  if (!where.empty()) start = pathToUtf8(where.front());
  ticket = ++g_ticket;
  waiting = true;
  status = folder ? "Choose the userdata folder of the original game." : "Choose a player file such as user1.dat.";
  SDL_Window* win = SDL_GetKeyboardFocus();
  const char* loc = start.empty() ? nullptr : start.c_str();
  if (folder) {
    SDL_ShowOpenFolderDialog(onChosen, (void*)(intptr_t)ticket, win, loc, false);
  } else {
    static const SDL_DialogFileFilter filters[] = {{"Player files", "dat"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(onChosen, (void*)(intptr_t)ticket, win, filters, 2, loc, false);
  }
}

void ImportDialog::refreshList() {
  list->lines.clear();
  for (auto& p : scan.profiles) {
    std::string line = p.name;
    if (!p.readable) line += "  (cannot be read)";
    list->lines.push_back(line);
  }
  list->select = list->lines.empty() ? -1 : 0;
  list->top = 0;
  for (size_t i = 0; i < scan.profiles.size(); i++)
    if (scan.profiles[i].readable) {
      list->select = (int)i;
      break;
    }
}

void ImportDialog::load(const std::filesystem::path& p) {
  scan = scanOriginalSaves(p);
  std::error_code ec;
  if (!scan.folder.empty() && std::filesystem::equivalent(scan.folder, app.userDir, ec)) {
    scan = ImportScan();
    scan.notes.push_back("That is PegglePort's own save folder. Choose the folder of the original game instead.");
  }
  haveScan = true;
  status.clear();
  refreshList();
  if (!scan.profiles.empty()) {
    std::string t = "Found " + std::to_string(scan.profiles.size()) + (scan.profiles.size() == 1 ? " player" : " players");
    if (scan.haveScores) t += " and the high scores";
    if (scan.replayCount > 0) t += ". The folder also holds " + std::to_string(scan.replayCount) + (scan.replayCount == 1 ? " replay" : " replays");
    scan.notes.insert(scan.notes.begin(), t + ".");
  }
}

void ImportDialog::doImport() {
  int i = selected();
  if (!haveScan || i < 0 || i >= (int)scan.profiles.size()) return;
  ImportedProfile ip = scan.profiles[(size_t)i];
  if (!ip.readable) {
    status = "This player cannot be read: " + ip.problem + ".";
    return;
  }
  if (ip.game != app.game) {
    if (!app.cfg.userDir.empty()) {
      status = "This is a " + gameName(ip.game) + " player. Start " + gameName(ip.game) + " in PegglePort to import it.";
      return;
    }
    ProfileMgr pm(ip.game);
    std::filesystem::path dir = defaultUserDir(ip.game);
    pm.setDir(dir);
    pm.load();
    HighScoreMgr hs(ip.game);
    hs.load(dir / "highscores.dat");
    ImportOutcome o = importOriginalProfile(pm, &hs, ip, scan, uniqueProfileName(pm, ip.name));
    if (!o.ok) {
      status = o.message;
      return;
    }
    if (!app.cfg.noSave) hs.save(dir / "highscores.dat");
    status = "Imported " + o.name + " into " + gameName(ip.game) + ". Start " + gameName(ip.game) + " in PegglePort to play as this player.";
    scan.profiles.erase(scan.profiles.begin() + i);
    refreshList();
    return;
  }
  ImportOutcome o = importOriginalProfile(app.profiles, &app.highScores, ip, scan, uniqueProfileName(app.profiles, ip.name));
  if (!o.ok) {
    status = o.message;
    return;
  }
  if (!app.cfg.noSave) app.highScores.save(app.userDir / "highscores.dat");
  PlayerInfo* p = app.profiles.get(o.name);
  if (p) {
    if (app.curProfile) app.saveProfile();
    app.curProfile = p;
    app.colorblind = p->colorblind;
    app.checkMaxStage();
    app.syncProfile();
    if (!app.cfg.noSave) {
      app.settings.curUser = p->name;
      app.profiles.save();
    }
  }
  std::string done = "Imported " + o.name + " from " + gameName(ip.game) + ". You are now playing as " + o.name + ".";
  if (o.scoresAdded > 0) done += " " + std::to_string(o.scoresAdded) + (o.scoresAdded == 1 ? " high score was" : " high scores were") + " added to the lists.";
  if (auto* u = app.getDialog(D_UserList)) u->doScrollOff();
  if (auto* c = dynamic_cast<NewUserDialog*>(app.getDialog(D_CreateUser))) {
    if (c->certificate) c->doApproved();
    else c->doScrollOff();
  }
  doScrollOff();
  app.doDialog(D_Tip, true, "Player Imported", done, "OK", BUTTONS_FOOTER);
}

void ImportDialog::onButton(int id) {
  if (id == 101) choose(false);
  else if (id == 102) choose(true);
  else if (id == ID_YES) doImport();
  else Dialog::onButton(id);
}

void App::doImportProfileDialog() {
  killDialog(D_ImportProfile);
  auto d = std::make_unique<ImportDialog>(*this);
  Dialog* raw = d.get();
  positionDialog(raw, 400);
  raw->x = 0x96;
  raw->y = 0x28;
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::importDialogButton(int dlg, int btn) {
  auto* d = dynamic_cast<ImportDialog*>(getDialog(dlg));
  if (!d) return;
  if (btn == ID_YES) d->doImport();
  else d->doScrollOff();
}

}
