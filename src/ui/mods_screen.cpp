#include "ui/mods_screen.h"
#include "gamepaths.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "log.h"
#include "mods/manager.h"
#include "mods/text.h"
#include "ui/consent_dialog.h"
#include "ui/mod_text.h"

namespace fs = std::filesystem;

namespace pp::ui {

using mods::ModInfo;

namespace {
const char* const kText = "PLAIN2";
const float kRowX = 28, kRowW = 744, kBoxH = 71, kInnerL = 36, kInnerR = 766;
const float kPillLabelX = 30, kPillPad = 14, kVersionGap = 8, kVersionMax = 140, kNameGap = 10;
const float kButtonPad = 44, kButtonGap = 2;
const float kMenuY = 462, kStatusY = 516;

float patchMin(const Tex* t) { return t ? 2.0f * std::floor(t->lw() / 3.0f) : 0.0f; }

bool hasVersion(const ModInfo& mi) { return !mi.m.version.empty() && mi.m.version != "0"; }

std::string named(Gfx& g, const std::string& name, const char* rest, float width) {
  return fitPlain(g, kText, name, std::max(120.0f, width - plainWidth(g, kText, rest))) + rest;
}

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
  if (mi->state == ModInfo::NeedsConsent) status_ = named(app.gfx, mi->m.name, " needs your OK before it loads. Press Allow...", 740);
  else if (mi->state == ModInfo::Failed) status_ = named(app.gfx, mi->m.name, " could not load: ", 440) + mi->error;
}

int ModsScreen::signature() const {
  uint32_t s = (uint32_t)page_ * 7u;
  if (mods::g_mods)
    for (auto& m : mods::g_mods->mods()) s = s * 31u + (uint32_t)m->state * 3u + (m->enabled ? 1u : 0u) + (m->consented ? 5u : 0u) + (uint32_t)m->m.id.size();
  return (int)(s & 0x7fffffffu);
}

void ModsScreen::rebuild() {
  widgets.clear();
  capture = hover = focusW = nullptr;
  rows_.clear();
  Gfx& g = app.gfx;
  Tex* orange = g.img("IMAGE_DLG_BUTTON3");
  Tex* pill = g.img("IMAGE_DLG_BUTTON2");
  const float minW = patchMin(orange);
  auto sized = [&](Button* b, float want) {
    b->w = std::max(minW, want);
    return b;
  };
  auto labelled = [&](const std::string& label) { return g.textWidth("BUTTON", label) + kButtonPad; };
  patchButton(0, "Done", "IMAGE_DLG_BROWNBUTTON1", 340, 541);
  sized(patchButton(6000, "Open Folder", "IMAGE_DLG_BUTTON3", 24, 10), labelled("Open Folder"));
  Button* logBtn = sized(patchButton(6001, "Log", "IMAGE_DLG_BUTTON3", 0, 10), labelled("Log"));
  logBtn->x = 776 - logBtn->w;
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
  const float moveW = std::max(minW, std::max(labelled("Up"), labelled("Down")));
  const float scanW = std::max(minW, std::max(labelled("Scan report"), labelled("Allow...")));
  const float downX = kInnerR - moveW, upX = downX - kButtonGap - moveW, scanX = upX - kButtonGap - scanW;
  const float nameRoom = scanX - kNameGap - kInnerL;
  const float pillMin = patchMin(pill), pillH = pill ? pill->lh() / 3 : 24.0f;
  for (int r = 0; r < kPage; r++) {
    int i = page_ * kPage + r;
    if (i >= n) break;
    ModInfo& mi = *mm->mods()[(size_t)i];
    float top = (float)(kTop + r * kRowH);
    RowText rt;
    float versionW = 0;
    if (hasVersion(mi)) {
      rt.version = fitPlain(g, kText, "version " + mi.m.version, kVersionMax);
      versionW = plainWidth(g, kText, rt.version) + kVersionGap;
    }
    auto c = std::make_unique<Checkbox>();
    c->id = 1000 + i;
    c->image = pill;
    c->style = 1;
    c->label = plainText(fitPlain(g, "BUTTON", mi.m.name, nameRoom - versionW - kPillLabelX - kPillPad));
    c->checked = mi.enabled;
    c->enabled = !mi.discoveryFailed;
    c->labelX = kPillLabelX;
    c->w = std::max(pillMin, kPillLabelX + g.textWidth("BUTTON", c->label) + kPillPad);
    c->h = pillH;
    c->x = kInnerL;
    c->y = top + 7;
    rt.versionX = c->x + c->w + kVersionGap;
    rows_.push_back(rt);
    add(std::move(c));
    bool pending = mi.needsConsent() || mm->needsUnrestrictedStep(mi);
    patchButton(2000 + i, pending ? "Allow..." : "Scan report", "IMAGE_DLG_BUTTON3", scanX, top + 3)->w = scanW;
    patchButton(3000 + i, "Up", "IMAGE_DLG_BUTTON3", upX, top + 3)->w = moveW;
    patchButton(4000 + i, "Down", "IMAGE_DLG_BUTTON3", downX, top + 3)->w = moveW;
  }
  auto menus = mm->menus();
  const int count = std::min(5, (int)menus.size());
  if (count > 0) {
    std::vector<float> want((size_t)count), give((size_t)count, 0.0f);
    std::vector<int> order;
    for (int k = 0; k < count; k++) {
      want[(size_t)k] = std::min(360.0f, std::max(minW, labelled(plainText(menus[(size_t)k].label))));
      order.push_back(k);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return want[(size_t)a] < want[(size_t)b]; });
    float left = kInnerR - kInnerL - kButtonGap * (float)(count - 1);
    for (int rank = 0; rank < count; rank++) {
      const size_t k = (size_t)order[(size_t)rank];
      give[k] = std::max(minW, std::floor(std::min(want[k], left / (float)(count - rank))));
      left -= give[k];
    }
    float bx = kInnerL;
    for (int k = 0; k < count; k++) {
      const float bw = give[(size_t)k];
      patchButton(5000 + k, plainText(fitPlain(g, "BUTTON", menus[(size_t)k].label, bw - kButtonPad)), "IMAGE_DLG_BUTTON3", bx, kMenuY)->w = bw;
      bx += bw + kButtonGap;
    }
  }
}

void ModsScreen::openFolder() {
  fs::path dir = mods::g_mods ? mods::g_mods->modsDir() : app.userDir / "mods";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (SDL_OpenURL(fileUrl(dir).c_str())) {
    status_ = "Opened " + mods::gameText(pathToUtf8(dir));
    log::info("ui", "opened the mods folder " + pathToUtf8(dir));
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
  g.drawString(kText, 400, 74, "F10 shows the log anywhere", 0xa0b8d8, 1, Align::Center);
  auto* mm = mods::g_mods;
  if (!mm || mm->mods().empty()) {
    const std::string text = mm ? "No mods found. Put a mod folder or .zip into:\n" + mods::gameText(pathToUtf8(mm->modsDir())) + "\n\nIt shows up here when you come back to the game." : "No mod manager.";
    float y = 160 + g.fontAscent(kText);
    for (auto& l : wrapPlain(g, kText, 680, text)) {
      drawPlain(g, kText, 400, y, l, 0xffffff, 1, Align::Center);
      y += g.fontLineSpacing(kText);
    }
  } else {
    const float textW = kRowX + kRowW - 12 - 40;
    for (int r = 0; r < kPage; r++) {
      size_t i = (size_t)(page_ * kPage + r);
      if (i >= mm->mods().size()) break;
      ModInfo& mi = *mm->mods()[i];
      float top = (float)(kTop + r * kRowH);
      g.fill(kRowX, top, kRowW, kBoxH, 0xffffff, 0.06f);
      if ((size_t)r < rows_.size() && !rows_[(size_t)r].version.empty()) drawPlain(g, kText, rows_[(size_t)r].versionX, top + 26, rows_[(size_t)r].version, 0xa0b8d8);
      std::string who = (mi.m.author.empty() ? "" : "by " + mi.m.author + "   ") + mi.m.description;
      drawPlain(g, kText, 40, top + 47, fitPlain(g, kText, who, textW), 0xdddddd);
      uint32_t col = mi.state == ModInfo::Active ? 0x80ff80 : mi.state == ModInfo::NeedsConsent ? 0xffe060 : mi.state == ModInfo::Failed ? 0xff7070 : 0xa0a0a0;
      std::string st = std::string(mi.stateName()) + (mi.enabled && mi.state == ModInfo::Off && app.modApplyPending() ? " (applying)" : "") + (mi.error.empty() ? "" : ": " + mi.error);
      std::string pl = mi.protectionLine();
      drawPlain(g, kText, 40, top + 65, fitPlain(g, kText, st + "      " + mi.scan.summary() + (pl.empty() ? "" : "      " + pl), textW), col);
    }
  }
  drawPlain(g, kText, 400, kStatusY, fitPlain(g, kText, status_, 740), 0xffffaa, 1, Align::Center);
}

}
