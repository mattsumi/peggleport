#include "ui/dialogs.h"
#include "ui/hooks.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>
#include <cstdio>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "ui/game_screen.h"
#include "ui/screens.h"

namespace pp::ui {


void ListBox::draw(Gfx& g) {
  g.fill(0, 0, w, h, back);
  g.pushClip(0, 0, w, h);
  int n = visibleItems() + 1;
  for (int i = 0; i < n; i++) {
    int idx = top + i;
    if (idx >= (int)lines.size()) break;
    float y = i * itemH;
    uint32_t col = 0xffff50;
    if (idx == select) {
      g.fill(0, y, w, itemH, 0xf45d02);
      col = 0xffffff;
    } else if (idx == hilite) {
      col = 0xffffff;
    }
    g.drawString(font, w / 2, y + g.fontAscent(font), lines[idx], col, 1, Align::Center);
  }
  g.popClip();
}
bool ListBox::mouseDown(float lx, float ly, int button, int clicks) {
  if (button != 1) return false;
  int i = indexAt(ly);
  if (i >= 0 && onClick) onClick(i, clicks);
  return true;
}
void ListBox::mouseMove(float lx, float ly) { hilite = indexAt(ly); }
void ListBox::reveal() {
  const int vis = std::max(1, visibleItems());
  if (select < 0) return;
  if (select < top) top = select;
  else if (select >= top + vis) top = select - vis + 1;
  top = std::clamp(top, 0, std::max(0, (int)lines.size() - vis));
}
void ListBox::wheel(float d) {
  int maxTop = std::max(0, (int)lines.size() - visibleItems());
  top = std::clamp(top - (int)d, 0, maxTop);
}


UserDialog::UserDialog(App& a) : Dialog(a, D_UserList, true, "WHO ARE YOU?", "", "", BUTTONS_YES_NO) {
  rope = true;
  auto l = std::make_unique<ListBox>();
  l->gfx = &a.gfx;
  list = add(std::move(l));
  rename = patchButton(100, "Rename", "IMAGE_DLG_BUTTON3", 0, 0);
  del = patchButton(101, "Delete", "IMAGE_DLG_BUTTON3", 0, 0);
  create = patchButton(102, "Create New User", "IMAGE_DLG_BUTTON3", 0, 0);
  importBtn = patchButton(103, "Import Player", "IMAGE_DLG_BUTTON3", 0, 0);
  if (a.curProfile) {
    list->lines.push_back(a.curProfile->name);
    list->select = 0;
  }
  for (auto& n : a.profiles.names())
    if (!a.curProfile || n != a.curProfile->name) list->lines.push_back(n);
  list->onClick = [this](int i, int clicks) {
    list->select = i;
    if (clicks >= 2) app.dialogButton(D_UserList, ID_YES);
  };
}

float UserDialog::preferredHeight(float width) { return Dialog::preferredHeight(width) + kOwnLayoutExtra + 0xff + buttonH + 5; }

void UserDialog::layout() {
  float left = insL, top = contentTop(app.gfx);
  list->x = left + 10;
  list->y = top;
  list->w = w - insL - insR - 20;
  list->h = 0x9b;
  rename->x = yesBtn->x;
  rename->y = yesBtn->y - 5 - rename->h;
  rename->w = yesBtn->w;
  del->x = noBtn->x;
  del->y = noBtn->y - 5 - del->h;
  del->w = noBtn->w;
  create->x = rename->x;
  create->y = rename->y - 5 - create->h;
  create->w = del->x + del->w - create->x;
  importBtn->x = create->x;
  importBtn->y = create->y - 5 - importBtn->h;
  importBtn->w = create->w;
}

void UserDialog::drawLayer(Gfx& g) {
  layout();
  Dialog::drawLayer(g);
  Tex* fr = g.img("IMAGE_DLG_BLUEFRAME");
  if (fr) g.drawBox(fr, list->x - 8, list->y - 8, list->w + 16, list->h + 16);
}

bool UserDialog::input(const Input& in) {
  if (in.type == Input::Wheel) { list->wheel(in.wheel); return true; }
  if (in.type == Input::KeyDown) {
    if (in.key == SDLK_UP && list->select > 0) { list->select--; list->reveal(); return true; }
    if (in.key == SDLK_DOWN && list->select + 1 < (int)list->lines.size()) { list->select++; list->reveal(); return true; }
  }
  return Dialog::input(in);
}

void UserDialog::onButton(int id) {
  std::string n = selName();
  switch (id) {
    case 100: if (!n.empty()) app.doRenameUserDialog(n); break;
    case 101: if (!n.empty()) app.doConfirmDeleteUserDialog(n); break;
    case 102: app.doCreateUserDialog(); break;
    case 103: app.doImportProfileDialog(); break;
    default: Dialog::onButton(id);
  }
}

void UserDialog::confirmDelete() {
  std::string name = selName();
  if (name.empty()) return;
  bool wasCur = app.curProfile && app.curProfile->name == name;
  if (wasCur) app.curProfile = nullptr;
  app.profiles.remove(name);
  int idx = list->select;
  list->lines.erase(list->lines.begin() + idx);
  idx = std::max(0, idx - 1);
  if (!list->lines.empty()) list->select = std::min(idx, (int)list->lines.size() - 1);
  else list->select = -1;
  list->reveal();
  if (!app.curProfile) {
    app.curProfile = app.profiles.get(selName());
    if (!app.curProfile) app.curProfile = app.profiles.any();
  }
  if (!app.cfg.noSave) app.profiles.save();
  if (!app.curProfile) app.doCreateUserDialog();
  app.syncProfile();
}

void UserDialog::finish(bool accepted) { app.finishUserDialog(accepted); }


NewUserDialog::NewUserDialog(App& a, bool isRename, bool cert, bool canCancel)
    : Dialog(a, isRename ? D_RenameUser : D_CreateUser, true, isRename ? "Rename User" : "New User", "Please enter your name.", "", BUTTONS_OK_CANCEL), certificate(cert) {
  auto e = std::make_unique<EditBox>();
  e->gfx = &a.gfx;
  e->maxChars = 12;
  e->widthFont = "OVERLOAD24";
  e->maxWidth = 190;
  edit = add(std::move(e));
  edit->focused = true;
  focusW = edit;
  edit->onEnter = [this] { app.dialogButton(dialogId, ID_YES); };
  if (cert) {
    Tex* c = a.gfx.img("IMAGE_MM_CERTIFICATE");
    w = c ? c->lw() : 516;
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d / %d / %d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
    dateText = buf;
    edit->font = "PLAIN2BIG";
    edit->paper = true;
    edit->ink = 0x606060;
    if (!canCancel) {
      noBtn->visible = false;
      importBtn = patchButton(103, "Import Player", "IMAGE_DLG_BUTTON3", 0, 0);
    }
    yesBtn->visible = false;
    okBtn = embeddedButton(1000, "IMAGE_MM_CERTIFICATEOK", 0xd2, 0x107);
    okBtn->additiveDown = true;
    okBtn->hoverSound = "";
    header.clear();
    lines.clear();
    rope = false;
  } else {
    w = 400;
  }
}

float NewUserDialog::preferredHeight(float width) {
  if (certificate) {
    Tex* c = app.gfx.img("IMAGE_MM_CERTIFICATE");
    return c ? c->lh() : 461;
  }
  return Dialog::preferredHeight(width) + kOwnLayoutExtra + 0x28;
}

void NewUserDialog::layout() {
  if (certificate) {
    edit->x = 145;
    edit->y = 180;
    edit->w = 300;
    edit->h = 32;
    noBtn->x = 0x136;
    noBtn->y = 0x16d;
    noBtn->w = 0x96;
    layoutButtons();
    noBtn->x = 0x136;
    noBtn->y = 0x16d;
    noBtn->w = 0x96;
    if (importBtn) {
      importBtn->x = 0x136;
      importBtn->y = 0x16d;
      importBtn->w = 0x96;
    }
  } else {
    layoutButtons();
    edit->x = 0x30;
    edit->y = 0x69;
    edit->w = w - 96;
    edit->h = 0x18;
  }
}

void NewUserDialog::drawLayer(Gfx& g) {
  layout();
  if (!certificate) {
    Dialog::drawLayer(g);
    return;
  }
  Tex* c = g.img("IMAGE_MM_CERTIFICATE");
  if (c) g.draw(c, 0, 0);
  g.drawString("PLAIN", 0x87 - g.textWidth("PLAIN", dateText) / 2, 0x186, dateText, 0x808080);
  if (noBtn->visible || importBtn) g.fill(0x133, 0x164, 0x88, 0x32, 0xd6cfc0);
  if (stamp > 0) {
    Tex* st = g.img("IMAGE_MM_APPROVEDSTAMP");
    if (st) {
      float k = stamp >= 20 ? 1.0f : 1.0f + (20 - stamp) / 20.0f;
      float cx = 0xfa + st->lw() / 2, cy = 0x131 + st->lh() / 2;
      g.drawRot(st, cx, cy, 0, k, k, Mode(stamp >= 20 ? 1.0f : 0.4f + 0.6f * stamp / 20.0f));
    }
  }
}

void NewUserDialog::update() {
  Dialog::update();
  if (stamp != 0) {
    stamp++;
    if (stamp == 0x4b) {
      if (!app.curProfile) { stamp = 0; return; }
      app.killDialog(D_UserList);
      app.onProfileCreated();
      doScrollOff();
    }
  }
}

void NewUserDialog::doApproved() {
  if (okBtn) okBtn->enabled = false;
  if (importBtn) importBtn->enabled = false;
  noBtn->enabled = false;
  stamp = 1;
  app.playSample("STAMP");
}

void NewUserDialog::onButton(int id) {
  if (stamp) return;
  if (id == 103) {
    app.doImportProfileDialog();
    return;
  }
  if (id == 1000 || id == ID_YES) app.dialogButton(dialogId, ID_YES);
  else app.dialogButton(dialogId, ID_NO);
}


OptionsDialog::OptionsDialog(App& a, bool fg) : Dialog(a, D_Options, true, fg ? "MENU" : "OPTIONS", "", fg ? "Back To Game" : "Done", BUTTONS_FOOTER), fromGame(fg) {
  rope = true;
  auto mkSlider = [&](int id, const char* label, float v) {
    auto s = std::make_unique<Slider>();
    s->id = id;
    s->label = label;
    s->value = v;
    s->w = 0xa2;
    s->h = 0x24;
    return add(std::move(s));
  };
  music = mkSlider(0, "Music Volume", a.settings.music);
  sfx = mkSlider(1, "Sound Effects", a.settings.sfx);
  fever = mkSlider(2, "Fever Volume", a.settings.fever);
  auto mkCheck = [&](int id, const char* label, bool v) {
    auto c = std::make_unique<Checkbox>();
    c->id = id;
    c->image = a.gfx.img("IMAGE_DLG_BUTTON1");
    c->style = 0;
    c->label = label;
    c->checked = v;
    c->labelX = 40;
    c->labelY = 2;
    c->w = 0xb6;
    c->h = c->image ? c->image->lh() / 4 : 34;
    return add(std::move(c));
  };
  fullscreen = mkCheck(3, "Fullscreen", a.settings.fullscreen);
  cursors = mkCheck(4, "Custom Cursors", a.settings.customCursors);
  vsync = mkCheck(5, "Vertical Sync", a.settings.vsync);
  colorblind = mkCheck(6, "Colorblind Mode", a.curProfile ? a.curProfile->colorblind : false);
  help = patchButton(7, "Help", "IMAGE_DLG_BUTTON3", 0, 0);
  mainMenu = patchButton(8, "Main Menu", "IMAGE_DLG_BUTTON3", 0, 0);
  newLevel = patchButton(9, "New Level", "IMAGE_DLG_BUTTON3", 0, 0);
  newChallenge = patchButton(10, "New Challenge", "IMAGE_DLG_BUTTON3", 0, 0);
  modsBtn = patchButton(12, "Mods", "IMAGE_DLG_BUTTON3", 0, 0);
  modsBtn->visible = !fg;
  editor = patchButton(13, "Level Editor", "IMAGE_DLG_BUTTON3", 0, 0);
  editor->visible = !fg;
  gamesBtn = patchButton(15, "Game Folders...", "IMAGE_DLG_BUTTON3", 0, 0);
  gamesBtn->visible = !fg;
  widescreen = patchButton(11, "Widescreen", "IMAGE_DLG_BUTTON3", 0, 0);
  visuals = patchButton(14, "Enhanced Visuals...", "IMAGE_DLG_BUTTON3", 0, 0);
  homeBtn = patchButton(16, "Main Menu", "IMAGE_DLG_BUTTON3", 0, 0);
  homeBtn->visible = !fg && a.bothGames;
  demoBtn = patchButton(17, "Demo", "IMAGE_DLG_BUTTON3", 0, 0);
  demoBtn->visible = !fg && a.game == Game::Nights;
  updateWidescreenLabel();
  updateHomeLabel();
  if (Tex* pb = a.gfx.img("IMAGE_DLG_PLAYBUTTON1")) {
    yesBtn->image = pb;
    yesBtn->kind = Button::Kind::Embedded;
    yesBtn->w = pb->lw();
    yesBtn->h = pb->lh() / 2;
    yesBtn->labelOnEmbedded = true;
    yesBtn->font = "BUTTON";
  }
  bool adv = a.gameMode == GameMode::Adventure, chal = a.gameMode == GameMode::Challenge;
  const auto* board = dynamic_cast<GameScreen*>(a.screen());
  const bool watching = a.gameMode == GameMode::Demo || (board && board->viewing());
  if (fg && chal) newLevel->label = "Restart Challenge";
  else if (adv) newLevel->label = "Restart Level";
  mainMenu->visible = fg;
  newLevel->visible = fg;
  newChallenge->visible = fg && chal;
  help->visible = true;
  if (fg && watching) { newLevel->visible = false; help->visible = false; }
  widescreen->visible = !fg;
  extraH = (float)(0xdc + (fg ? 0x24 : 0x50));
  if (editor->visible) extraH += 0x20;
  if (homeBtn->visible) extraH += 0x20;
  if (demoBtn->visible) extraH += 0x20;
  if (mainMenu->visible) extraH += 0x20;
  if (help->visible) extraH += 0x20;
  if (fg && chal) extraH += 0x20;
}

void OptionsDialog::updateHomeLabel() { homeBtn->label = std::string("Main Menu: ") + (app.homeGame == Game::Nights ? "Peggle Nights" : "Peggle Deluxe"); }

void OptionsDialog::updateWidescreenLabel() { widescreen->label = std::string("Widescreen: ") + widescreenName(app.widescreenMode()); }

float OptionsDialog::preferredHeight(float width) { return Dialog::preferredHeight(width) + kOwnLayoutExtra + extraH; }

void OptionsDialog::layout() {
  Gfx& g = app.gfx;
  float left = insL, top = contentTop(g) - 10, W = w - insL - insR;
  float sx = left + W / 2 - 2;
  music->x = sx;  music->y = top;
  fever->x = sx;  fever->y = music->y + music->h;
  sfx->x = sx;    sfx->y = fever->y + fever->h;
  fullscreen->x = left;   fullscreen->y = sfx->y + sfx->h + 8;   fullscreen->w = 0xb6;
  cursors->x = left;      cursors->y = fullscreen->y + fullscreen->h;  cursors->w = 0xb6;
  vsync->x = fullscreen->x + fullscreen->w + 5;  vsync->y = fullscreen->y;  vsync->w = 0xb6;
  colorblind->x = vsync->x;  colorblind->y = cursors->y;  colorblind->w = 0xb6;
  widescreen->x = left;   widescreen->y = cursors->y + cursors->h + 6;   widescreen->w = W;   widescreen->h = help->h;
  visuals->x = left;      visuals->y = fromGame ? widescreen->y : widescreen->y + help->h + 2;   visuals->w = W;   visuals->h = help->h;
  homeBtn->x = left;      homeBtn->y = visuals->y + help->h + 2;   homeBtn->w = W;   homeBtn->h = help->h;
  demoBtn->x = left;      demoBtn->y = (homeBtn->visible ? homeBtn->y : visuals->y) + help->h + 2;   demoBtn->w = W;   demoBtn->h = help->h;
  if (yesBtn) {
    yesBtn->x = (w - yesBtn->w) / 2;
    yesBtn->y = h - insB - yesBtn->h - 6;
  }
  float bw = W / 2 - 2, bh = help->h;
  if (!fromGame) {
    help->x = yesBtn->x; help->y = yesBtn->y - 0x1e - bh + 0; help->w = yesBtn->w;
    help->y = yesBtn->y - bh - 24;
    help->w = bw;
    modsBtn->x = help->x + bw; modsBtn->y = help->y; modsBtn->w = bw; modsBtn->h = bh;
    if (editor->visible) { editor->x = help->x; editor->w = bw; editor->y = help->y - bh; }
    if (gamesBtn->visible) { gamesBtn->x = help->x + bw; gamesBtn->w = bw; gamesBtn->y = help->y - bh; }
  } else {
    mainMenu->x = left; mainMenu->w = W; mainMenu->y = h - insB - bh - 0x1e - bh;
    if (app.gameMode == GameMode::Challenge) {
      newLevel->x = mainMenu->x; newLevel->w = mainMenu->w; newLevel->y = mainMenu->y - bh;
      help->x = mainMenu->x; help->w = bw; help->y = newLevel->y - bh;
      newChallenge->x = help->x + help->w + 5; newChallenge->w = bw; newChallenge->y = help->y;
    } else if (newLevel->visible) {
      help->x = mainMenu->x; help->w = bw; help->y = mainMenu->y - bh;
      newLevel->x = help->x + help->w + 5; newLevel->w = bw; newLevel->y = help->y;
    } else {
      help->x = mainMenu->x; help->w = mainMenu->w; help->y = mainMenu->y - bh;
    }
  }
  for (Button* b : {help, mainMenu, newLevel, newChallenge, modsBtn, editor, gamesBtn}) b->h = bh;
}

void OptionsDialog::drawLayer(Gfx& g) {
  layout();
  Dialog::drawLayer(g);
  if (Tex* pw = g.img("IMAGE_DLG_PLAYWIDGET1")) g.drawBox(pw, 0x24, h - pw->lh() - 6, 0x172, pw->lh());
}

void OptionsDialog::sliderChanged(int id, double v) {
  if (id == 0) app.settings.music = (float)v;
  else if (id == 1) app.settings.sfx = (float)v;
  else app.settings.fever = (float)v;
  app.applySettings();
  if (id == 1 && !sfx->dragging) app.playSample("BUTTON1");
}

void OptionsDialog::checkboxChanged(int id, bool on) {
  if (id == 4) app.settings.customCursors = on;
  if (id == 5) app.settings.vsync = on;
  app.applySettings();
}

void OptionsDialog::onButton(int id) {
  if (scrollActive && !scrollingOff) return;
  switch (id) {
    case 7: app.showHelpScreen(app.hasBoard()); if (!app.hasBoard()) app.finishOptionsDialog(true, true); break;
    case 12: app.showModsScreen(); app.finishOptionsDialog(true, true); break;
    case 8: app.doConfirmMainMenuDialog(); break;
    case 9: app.doConfirmRestartLevelDialog(); break;
    case 10: app.doConfirmNewChallengeDialog(); break;
    case 13: app.finishOptionsDialog(true, true); app.showEditor(); break;
    case 14: app.doVisualsDialog(); break;
    case 15: app.requestGameFolders(); break;
    case 17: app.startDemo(); break;
    case 16:
      app.setHomeGame(app.homeGame == Game::Nights ? Game::Deluxe : Game::Nights);
      updateHomeLabel();
      break;
    case 11:
      app.settings.widescreen = (app.settings.widescreen + 1) % kWidescreenModes;
      updateWidescreenLabel();
      break;
    default: app.dialogButton(D_Options, ID_YES);
  }
}

}
