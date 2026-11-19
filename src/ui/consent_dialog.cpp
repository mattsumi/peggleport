#include "ui/consent_dialog.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "log.h"
#include "mods/manager.h"
#include "ui/mod_text.h"
#include "ui/mods_screen.h"

namespace pp::ui {

namespace {
const char* const kFont = "PLAIN2";
const float kLine = 24, kBase = 18, kPara = 10;
const float kRow = 22, kRowBase = 16, kPadX = 10, kPadY = 6, kBullet = 12;
const float kBar = 6, kBarGap = 8, kFrame = 8;
const int kMinRows = 2;

std::string head(const mods::ModInfo& mi) {
  std::string t = mi.m.name;
  if (!mi.m.version.empty() && mi.m.version != "0") t += " " + mi.m.version;
  if (!mi.m.author.empty()) t += " by " + mi.m.author;
  return t;
}

std::string helperLine() {
  mods::NativeStatus ps = mods::NativeMod::probe();
  if (ps.helperMissing) return "The helper program that runs native plugins (pp_modhost) is missing, so this plugin cannot load.";
  if (!ps.probed) return "The helper process could not be started: " + ps.detail;
  if (ps.full()) return "It runs in a restricted helper process: " + mods::NativeMod::protectionText(ps.protections) + ".";
  return "This plugin could not be restricted on this system (" + mods::NativeMod::missingText(ps.protections) + "). It only loads if you also allow it to run without restrictions. You are asked about that separately.";
}
}

int ConsentList::rowsFor(float height) { return std::max(0, (int)std::floor((height - 2 * kPadY) / kRow)); }
float ConsentList::heightFor(int count) { return 2 * kPadY + (float)count * kRow; }
int ConsentList::shown() const { return std::min((int)rows.size(), rowsFor(h)); }
int ConsentList::maxTop() const { return std::max(0, (int)rows.size() - rowsFor(h)); }
void ConsentList::scrollTo(int row) { top = std::clamp(row, 0, maxTop()); }

void ConsentList::wheel(float d) {
  wheelAcc += d * 3.0f;
  int n = (int)std::trunc(wheelAcc);
  wheelAcc -= (float)n;
  if (n != 0) scrollBy(-n);
}

bool ConsentList::pressKey(int k) {
  if (!scrolls()) return false;
  const int page = std::max(1, rowsFor(h) - 1);
  switch (k) {
    case SDLK_UP: scrollBy(-1); return true;
    case SDLK_DOWN: scrollBy(1); return true;
    case SDLK_PAGEUP: scrollBy(-page); return true;
    case SDLK_PAGEDOWN: scrollBy(page); return true;
    case SDLK_HOME: scrollTo(0); return true;
    case SDLK_END: scrollTo(maxTop()); return true;
    default: break;
  }
  return false;
}

float ConsentList::thumbHeight() const {
  const float track = h - 2 * kPadY;
  return std::min(track, std::max(18.0f, track * (float)rowsFor(h) / (float)std::max<size_t>(1, rows.size())));
}

void ConsentList::dragTo(float ly) {
  const float track = h - 2 * kPadY, th = thumbHeight();
  const float f = std::clamp((ly - kPadY - th / 2) / std::max(1.0f, track - th), 0.0f, 1.0f);
  scrollTo((int)std::lround(f * (float)maxTop()));
}

void ConsentList::draw(Gfx& g) {
  g.fill(0, 0, w, h, 0x000018, 0.45f);
  if (Tex* fr = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(fr, -kFrame, -kFrame, w + 2 * kFrame, h + 2 * kFrame);
  g.pushClip(0, 0, w, h);
  const int n = rowsFor(h);
  for (int i = 0; i < n; i++) {
    const size_t idx = (size_t)(top + i);
    if (idx >= rows.size()) break;
    const Row& r = rows[idx];
    const float base = kPadY + (float)i * kRow + kRowBase;
    if (r.bullet && r.first) drawPlain(g, kFont, kPadX, base, "-", 0xffffff, alpha);
    drawPlain(g, kFont, kPadX + (r.bullet ? kBullet : 0), base, r.text, 0xffffff, alpha);
  }
  g.popClip();
  if (!scrolls()) return;
  const float tx = w - kPadX - kBar + 4, track = h - 2 * kPadY, th = thumbHeight();
  g.fill(tx, kPadY, kBar, track, 0xffffff, 0.12f);
  g.fill(tx, kPadY + (track - th) * (float)top / (float)maxTop(), kBar, th, 0xb0c8ff, 0.85f);
}

bool ConsentList::mouseDown(float lx, float ly, int button, int) {
  if (button != 1 || !scrolls() || lx < w - kPadX - kBar - kBarGap) return false;
  dragging = true;
  dragTo(ly);
  return true;
}

bool ConsentList::mouseUp(float, float, int) {
  if (!dragging) return false;
  dragging = false;
  return true;
}

void ConsentList::mouseMove(float, float ly) {
  if (dragging) dragTo(ly);
}

int ConsentDialog::stepFor(const std::string& id, bool ask) {
  auto* mm = mods::g_mods;
  mods::ModInfo* mi = mm ? mm->find(id) : nullptr;
  if (!mi || !ask) return 0;
  if (mi->needsConsent()) return 1;
  if (mm->needsUnrestrictedStep(*mi)) return 2;
  return 0;
}

ConsentDialog::Body ConsentDialog::describe(const std::string& id, int step) {
  Body b;
  auto* mm = mods::g_mods;
  mods::ModInfo* mi = mm ? mm->find(id) : nullptr;
  if (!mi) {
    b.head = "Unknown mod.";
    return b;
  }
  b.head = head(*mi);
  if (step == 2) {
    mods::NativeStatus ps = mods::NativeMod::probe();
    b.before.push_back("This plugin could not be restricted on this system: " + mods::NativeMod::missingText(ps.protections) + ".");
    b.before.push_back("If you run it without these restrictions, its native code can do those things with your rights, like any program you start yourself. It stays switched off unless you allow this.");
    b.before.push_back("This choice is saved for this exact version of the plugin.");
    return b;
  }
  std::string what;
  if (mi->scan.usesNet()) what += (mi->scan.declaredNet ? "asks for the network permission" : "contains code that looks like network access");
  if (mi->scan.hasNative) what += std::string(what.empty() ? "" : " and ") + "ships native code";
  if (what.empty()) what = "has nothing that needs your OK";
  b.before.push_back("This mod " + what + ".");
  for (auto& f : mi->scan.findings) {
    if (!(f.isNet() || f.kind == mods::Finding::Native)) continue;
    b.findings.push_back(f.file + (f.line ? ":" + std::to_string(f.line) : "") + ": " + f.what);
  }
  bool nativeFull = false;
  if (mi->scan.hasNative || !mi->m.native.empty()) {
    b.after.push_back(helperLine());
    nativeFull = mods::NativeMod::probe().full();
  }
  if (step == 1) b.after.push_back(nativeFull && !mi->scan.declaredNet && mi->m.scripts.empty() ? "The scan is best effort. Allow only mods you trust." : "This scan is best effort: a determined author can hide network use in the code. Allow only mods you trust.");
  b.notesMayScroll = step != 1;
  return b;
}

ConsentDialog::ConsentDialog(App& a, const std::string& id, bool askConsent)
    : Dialog(a, D_ModConsent, true, stepFor(id, askConsent) == 2 ? "RUN WITHOUT LIMITS?" : askConsent ? "TRUST THIS MOD?" : "MOD SCAN", "", askConsent ? "" : "OK", askConsent ? BUTTONS_YES_NO : BUTTONS_FOOTER), modId(id), ask(askConsent) {
  step = stepFor(id, askConsent);
  if (step == 0) ask = false;
  if (mods::ModInfo* mi = mods::g_mods ? mods::g_mods->find(id) : nullptr) hash = mi->hashHex;
  if (step == 2) setYesNo("Run anyway", "Keep off");
  else if (askConsent) setYesNo("Allow", "Not now");
  body = describe(id, step);
  list = add(std::make_unique<ConsentList>());
  list->visible = false;
}

bool ConsentDialog::build(float width, float limit, bool notesInList, float& height) {
  Gfx& g = app.gfx;
  const float cw = width - insL - insR - 4, x0 = insL + 2;
  fixed.clear();
  hint.clear();
  auto para = [&](const std::string& s, size_t maxLines, std::vector<Line>& out, float& y) {
    for (auto& l : wrapPlainMax(g, kFont, cw, s, maxLines)) {
      out.push_back({y, l});
      y += kLine;
    }
  };
  float y = insT + (header.empty() ? 0.0f : g.fontHeight(headerFont) + spaceAfterHeader);
  para(body.head, 2, fixed, y);
  for (auto& p : body.before) {
    y += kPara;
    para(p, 0, fixed, y);
  }
  std::vector<Line> tail;
  float tailH = 0;
  if (!notesInList)
    for (auto& p : body.after) {
      tailH += kPara;
      para(p, 0, tail, tailH);
    }
  const float bottom = (yesBtn ? buttonH + 14 : 0.0f) + insB;
  bool all = true;
  list->visible = !body.findings.empty();
  list->rows.clear();
  if (list->visible) {
    const float listTop = y + kPara + kFrame, listW = cw - 2 * kFrame;
    auto fill = [&](float textW) {
      list->rows.clear();
      for (auto& f : body.findings) {
        bool first = true;
        for (auto& l : wrapPlain(g, kFont, textW - kBullet, f)) {
          list->rows.push_back({l, true, first});
          first = false;
        }
      }
      if (!notesInList) return;
      for (auto& p : body.after) {
        list->rows.push_back({"", false, false});
        for (auto& l : wrapPlain(g, kFont, textW, p)) list->rows.push_back({l, false, false});
      }
    };
    fill(listW - 2 * kPadX);
    float room = limit - listTop - kFrame - tailH - bottom;
    int count = (int)list->rows.size();
    all = count <= ConsentList::rowsFor(room);
    if (!all) {
      room -= kLine;
      fill(listW - 2 * kPadX - kBar - kBarGap);
      count = std::max(kMinRows, ConsentList::rowsFor(room));
      hint = notesInList ? "Scroll the list to read the whole report." : "Scroll the list to read all " + std::to_string(body.findings.size()) + " findings.";
    }
    list->x = x0 + kFrame;
    list->y = listTop;
    list->w = listW;
    list->h = ConsentList::heightFor(count);
    list->scrollTo(list->top);
    y = listTop + list->h + kFrame;
    if (!all) {
      hintY_ = y;
      y += kLine;
    }
  }
  for (auto& l : tail) fixed.push_back({y + l.y, l.text});
  height = y + tailH + bottom;
  return all;
}

float ConsentDialog::arrange(float width, float limit) {
  float hgt = 0;
  if (!build(width, limit, false, hgt) && body.notesMayScroll) build(width, limit, true, hgt);
  laidW_ = width;
  laidLimit_ = limit;
  laidH_ = hgt;
  return hgt;
}

float ConsentDialog::preferredHeight(float width) { return arrange(width, maxHeight); }

void ConsentDialog::drawLayer(Gfx& g) {
  if (w != laidW_ || (h != laidLimit_ && h != laidH_)) arrange(w, h);
  Dialog::drawLayer(g);
  for (auto& l : fixed) drawPlain(g, kFont, insL + 2, l.y + kBase, l.text, linesColor);
  if (!hint.empty()) drawPlain(g, kFont, insL + 2, hintY_ + kBase, hint, 0xffffaa);
}

bool ConsentDialog::input(const Input& in) {
  if (list->visible && list->scrolls() && !scrollingOff) {
    if (in.type == Input::Wheel) {
      list->wheel(in.wheel);
      return true;
    }
    if (in.type == Input::KeyDown && list->pressKey(in.key)) return true;
  }
  return Dialog::input(in);
}

void ConsentDialog::onButton(int id) {
  if (scrollActive && !scrollingOff) return;
  if (mods::g_mods && step == 1 && id == ID_YES) {
    mods::ModInfo* mi = mods::g_mods->find(modId);
    if (mi && mi->hashHex == hash) {
      mods::g_mods->setConsent(modId, true);
      followUp = mods::g_mods->needsUnrestrictedStep(*mi);
      app.requestModApply();
    } else {
      log::warn("mod:" + modId, "the mod changed while its permission dialog was open, so nothing was allowed; press Allow... again");
    }
  } else if (mods::g_mods && step == 2) {
    mods::ModInfo* mi = mods::g_mods->find(modId);
    if (mi && mi->hashHex == hash && mi->consented) {
      mods::g_mods->setUnrestricted(modId, id == ID_YES);
      app.requestModApply();
    } else if (id == ID_YES) {
      log::warn("mod:" + modId, "the mod changed while its permission dialog was open, so nothing was allowed; press Allow... again");
    }
  }
  closed = true;
  if (!hosted) doScrollOff();
}

void App::showModConsent(const std::string& modId, bool ask) {
  if (auto* ms = dynamic_cast<ModsScreen*>(overlay_.get())) { ms->openConsent(modId, ask); return; }
  killDialog(D_ModConsent);
  auto d = std::make_unique<ConsentDialog>(*this, modId, ask);
  d->maxHeight = 540;
  Dialog* raw = d.get();
  positionDialog(raw, 520);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::offerModConsent() {
  static bool offered = false;
  if (offered || !mods::g_mods) return;
  for (auto& m : mods::g_mods->mods())
    if (m->enabled && m->state == mods::ModInfo::NeedsConsent) {
      offered = true;
      showModConsent(m->m.id, true);
      return;
    }
}

}
