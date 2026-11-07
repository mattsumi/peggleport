#include "ui/mods_screen.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "log.h"
#include "mods/manager.h"
#include "ui/consent_dialog.h"

namespace fs = std::filesystem;

namespace pp::ui {

using mods::ModInfo;

namespace {
std::string fileUrl(const fs::path& p) {
  std::error_code ec;
  fs::path abs = fs::absolute(p, ec);
  std::u8string u8 = (ec ? p : abs).generic_u8string();
  std::string s(u8.begin(), u8.end());
  std::string o = "file://";
  if (s.empty() || s[0] != '/') o += '/';
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
      o += (char)c;
    } else {
      char b[4];
      std::snprintf(b, sizeof b, "%%%02X", c);
      o += b;
    }
  }
  return o;
}
}

void App::showModsScreen() {
  if (overlay_) return;
  overlay_ = std::make_unique<ModsScreen>(*this);
  scrollDir = Left;
  scrollOn(overlay_.get());
}

ModsScreen::ModsScreen(App& a) : Screen(a), seen_(a.modApplyCount()), wasFocused_(a.windowFocused) {
  if (mods::g_mods) {
    status_ = mods::g_mods->statusText();
    if (mods::g_mods->folderChanged()) a.requestModApply();
  } else {
    status_ = "No mod manager is running.";
  }
}

void ModsScreen::changed(const std::string& id) {
  touched_ = id;
  app.requestModApply();
  dirty_ = true;
}

void ModsScreen::followApply() {
  auto* mm = mods::g_mods;
  const bool focused = app.windowFocused;
  if (focused && !wasFocused_ && mm && mm->folderChanged()) app.requestModApply();
  wasFocused_ = focused;
  std::string why;
  if (app.modApplyPending() && app.modApplyBlocked(&why)) {
    status_ = why;
    return;
  }
  if (seen_ == app.modApplyCount()) return;
  seen_ = app.modApplyCount();
  dirty_ = true;
  status_ = app.modApplyStatus();
  if (!mm || touched_.empty()) return;
  ModInfo* mi = mm->find(touched_);
  touched_.clear();
  if (!mi || !mi->enabled) return;
  if (mi->state == ModInfo::NeedsConsent) status_ = mi->m.name + " needs your OK before it loads. Press Allow...";
  else if (mi->state == ModInfo::Failed) status_ = mi->m.name + " could not load: " + mi->error;
}

int ModsScreen::signature() const {
  uint32_t s = (uint32_t)page_ * 7u;
  if (mods::g_mods)
    for (auto& m : mods::g_mods->mods()) s = s * 31u + (uint32_t)m->state * 3u + (m->enabled ? 1u : 0u) + (m->consented ? 5u : 0u) + (uint32_t)m->m.id.size();
  return (int)(s & 0x7fffffffu);
}

static std::string fit(Gfx& g, const std::string& font, std::string s, float w) {
  if (g.textWidth(font, s) <= w) return s;
  while (s.size() > 4 && g.textWidth(font, s + "...") > w) s.pop_back();
  return s + "...";
}

void ModsScreen::rebuild() {
  widgets.clear();
  capture = hover = focusW = nullptr;
  Gfx& g = app.gfx;
  patchButton(0, "Done", "IMAGE_DLG_BROWNBUTTON1", 340, 541);
  patchButton(6000, "Open Folder", "IMAGE_DLG_BUTTON3", 24, 10)->w = 140;
  patchButton(6001, "Log", "IMAGE_DLG_BUTTON3", 676, 10)->w = 100;
  auto* done = addButton(0, Button::Kind::Simple, "IMAGE_HELP_BUTTONOVERLAY");
  done->x = 738; done->y = 536; done->w = 62; done->h = 62; done->image = nullptr;
  auto* mm = mods::g_mods;
  if (!mm) return;
  int n = (int)mm->mods().size(), pages = std::max(1, (n + kPage - 1) / kPage);
  page_ = std::clamp(page_, 0, pages - 1);
  if (pages > 1) {
    patchButton(1, "Previous", "IMAGE_DLG_BROWNBUTTON1", 150, 541)->visible = page_ > 0;
    patchButton(2, "Next", "IMAGE_DLG_BROWNBUTTON1", 530, 541)->visible = page_ + 1 < pages;
  }
  for (int r = 0; r < kPage; r++) {
    int i = page_ * kPage + r;
    if (i >= n) break;
    ModInfo& mi = *mm->mods()[(size_t)i];
    float y = kTop + r * kRowH;
    auto c = std::make_unique<Checkbox>();
    c->id = 1000 + i;
    c->image = g.img("IMAGE_DLG_BUTTON2");
    c->style = 1;
    c->label = fit(g, "BUTTON", mi.m.name + "  " + mi.m.version, 380);
    c->checked = mi.enabled;
    c->enabled = !mi.discoveryFailed;
    c->labelX = 30;
    c->w = g.textWidth("BUTTON", c->label) + 30;
    c->h = c->image ? c->image->lh() / 3 : 24;
    c->x = 36;
    c->y = y;
    add(std::move(c));
    bool pending = mi.needsConsent() || mm->needsUnrestrictedStep(mi);
    Button* b = patchButton(2000 + i, pending ? "Allow..." : "Scan report", "IMAGE_DLG_BUTTON3", 540, y - 2);
    b->w = 112;
    Button* up = patchButton(3000 + i, "Up", "IMAGE_DLG_BUTTON3", 660, y - 2);
    up->w = 44;
    Button* dn = patchButton(4000 + i, "Dn", "IMAGE_DLG_BUTTON3", 708, y - 2);
    dn->w = 44;
  }
  int k = 0;
  for (auto& m : mm->menus()) {
    if (k >= 5) break;
    int idx = k++;
    Button* b = patchButton(5000 + idx, m.label, "IMAGE_DLG_BUTTON3", 36.0f + 150.0f * idx, 490);
    b->w = 144;
  }
}

void ModsScreen::openFolder() {
  fs::path dir = mods::g_mods ? mods::g_mods->modsDir() : app.userDir / "mods";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (SDL_OpenURL(fileUrl(dir).c_str())) {
    status_ = "Opened " + dir.string();
    log::info("ui", "opened the mods folder " + dir.string());
  } else {
    status_ = std::string("Could not open the mods folder: ") + SDL_GetError();
    log::warn("ui", status_);
  }
}

void ModsScreen::openConsent(const std::string& modId, bool ask) {
  dlg_ = std::make_unique<ConsentDialog>(app, modId, ask);
  dlg_->hosted = true;
  float hgt = std::min(dlg_->preferredHeight(560), 570.0f);
  dlg_->resize(120, std::max(10.0f, (600 - hgt) / 2), 560, hgt);
}
bool ModsScreen::input(const Input& in) {
  if (dlg_) { dlg_->input(in); return true; }
  return Panel::input(in);
}
void ModsScreen::drawOverlay(Gfx& g) {
  if (!dlg_) return;
  g.fill(0, 0, 800, 600, 0x000000, 0.5f);
  dlg_->draw(g);
}

void ModsScreen::update() {
  followApply();
  if (dlg_) {
    dlg_->update();
    if (dlg_->closed) {
      touched_ = dlg_->modId;
      std::string again = dlg_->followUp ? dlg_->modId : std::string();
      dlg_.reset();
      dirty_ = true;
      if (!again.empty()) openConsent(again, true);
    }
    return;
  }
  Panel::update();
  if (mods::g_mods) {
    mods::g_mods->pump();
    for (auto& m : mods::g_mods->takeMessages()) status_ = m;
  }
  int s = signature();
  if (dirty_ || s != sig_) {
    rebuild();
    sig_ = signature();
    dirty_ = false;
  }
}

void ModsScreen::checkboxChanged(int id, bool on) {
  if (id < 1000 || !mods::g_mods) return;
  size_t i = (size_t)(id - 1000);
  if (i < mods::g_mods->mods().size()) {
    const std::string mid = mods::g_mods->mods()[i]->m.id;
    mods::g_mods->setEnabled(mid, on);
    changed(mid);
  }
  dirty_ = true;
}

void ModsScreen::onButton(int id) {
  auto* mm = mods::g_mods;
  if (id == 0) { app.endHelpScreen(); return; }
  if (id == 1) { page_--; dirty_ = true; return; }
  if (id == 2) { page_++; dirty_ = true; return; }
  if (id == 6000) { openFolder(); return; }
  if (id == 6001) { app.showLogView(true); return; }
  if (!mm) return;
  if (id >= 5000) {
    auto menus = mm->menus();
    if ((size_t)(id - 5000) < menus.size() && !mm->runMenu(menus[(size_t)(id - 5000)].modId, menus[(size_t)(id - 5000)].index)) status_ = "That mod entry failed (see the log).";
    return;
  }
  size_t i = (size_t)(id % 1000);
  if (i >= mm->mods().size()) return;
  ModInfo& mi = *mm->mods()[i];
  if (id >= 4000) { const std::string mid = mi.m.id; mm->move(mid, +1); changed(mid); }
  else if (id >= 3000) { const std::string mid = mi.m.id; mm->move(mid, -1); changed(mid); }
  else if (id >= 2000) app.showModConsent(mi.m.id, mi.needsConsent() || mm->needsUnrestrictedStep(mi));
}

void ModsScreen::keyDown(int key, int) {
  if (dlg_) { if (key == SDLK_ESCAPE) dlg_->closed = true; return; }
  if (key == SDLK_ESCAPE || key == SDLK_RETURN) app.endHelpScreen();
}

void ModsScreen::drawLayer(Gfx& g) {
  g.fill(0, 0, 800, 600, 0x0a1428);
  if (Tex* bg = g.img("IMAGE_HELP_SCREEN")) g.draw(bg, 0, 0);
  g.fill(20, 56, 760, 470, 0x000018, 0.88f);
  g.drawString("OVERLOAD24GRAD1", 400, 42, "Mods", 0xb6ffff, 1, Align::Center);
  g.drawString("PLAIN2", 400, 76, "F10 shows the log anywhere", 0xa0b8d8, 1, Align::Center);
  auto* mm = mods::g_mods;
  if (!mm || mm->mods().empty()) {
    g.drawWrapped("PLAIN2", {60, 160, 680, 200}, mm ? "No mods found. Put a mod folder or .zip into:\n" + mm->modsDir().string() + "\n\nIt shows up here when you come back to the game." : "No mod manager.", 0xffffff, 1, Align::Center);
  } else {
    for (int r = 0; r < kPage; r++) {
      size_t i = (size_t)(page_ * kPage + r);
      if (i >= mm->mods().size()) break;
      ModInfo& mi = *mm->mods()[i];
      float y = kTop + r * kRowH;
      g.fill(28, y - 8, 744, kRowH - 6, 0xffffff, 0.06f);
      std::string who = (mi.m.author.empty() ? "" : "by " + mi.m.author + "   ") + mi.m.description;
      g.drawString("PLAIN2", 40, y + 38, fit(g, "PLAIN2", who, 480), 0xdddddd);
      uint32_t col = mi.state == ModInfo::Active ? 0x80ff80 : mi.state == ModInfo::NeedsConsent ? 0xffe060 : mi.state == ModInfo::Failed ? 0xff7070 : 0xa0a0a0;
      std::string st = std::string(mi.stateName()) + (mi.enabled && mi.state == ModInfo::Off && app.modApplyPending() ? " (applying)" : "") + (mi.error.empty() ? "" : ": " + mi.error);
      std::string pl = mi.protectionLine();
      g.drawString("PLAIN2", 40, y + 56, fit(g, "PLAIN2", st + "   |   " + mi.scan.summary() + (pl.empty() ? "" : "   |   " + pl), 700), col);
    }
  }
  g.drawString("PLAIN2", 400, 520, fit(g, "PLAIN2", status_, 740), 0xffffaa, 1, Align::Center);
}

}
