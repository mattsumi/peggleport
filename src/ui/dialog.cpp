#include "ui/dialog.h"

#include <SDL3/SDL.h>
#include <vector>
#include <string>

#include <algorithm>
#include <cmath>

namespace pp::ui {

Dialog::Dialog(App& a, int id, bool mod, const std::string& hdr, const std::string& ln, const std::string& ftr, int bm)
    : Panel(a), dialogId(id), modal(mod), header(hdr), lines(ln), footer(ftr), buttonMode(bm) {
  Tex* bimg = a.gfx.img("IMAGE_DLG_BUTTON3");
  buttonH = bimg ? bimg->lh() / 3 : 30;
  auto mk = [&](int bid, const std::string& label) {
    auto* b = addButton(bid, Button::Kind::Patch, "IMAGE_DLG_BUTTON3", label);
    b->textOffY = 5;
    b->labelStyle = a.game == Game::Nights ? 0 : 9;
    b->labelFullGradient = a.game != Game::Nights;
    return b;
  };
  if (bm == BUTTONS_YES_NO) {
    yesBtn = mk(ID_YES, "YES");
    noBtn = mk(ID_NO, "NO");
  } else if (bm == BUTTONS_OK_CANCEL) {
    yesBtn = mk(ID_YES, "OK");
    noBtn = mk(ID_NO, "CANCEL");
  } else if (bm == BUTTONS_FOOTER) {
    yesBtn = mk(ID_FOOTER, ftr);
  }
}

void Dialog::setYesNo(const std::string& y, const std::string& n) {
  if (yesBtn) yesBtn->label = y;
  if (noBtn) noBtn->label = n;
}

float Dialog::preferredHeight(float width) {
  Gfx& g = app.gfx;
  float hgt = insT + insB;
  bool needSpace = false;
  if (!header.empty()) {
    hgt += g.fontHeight(headerFont);
    needSpace = true;
  }
  if (!lines.empty()) {
    if (needSpace) hgt += spaceAfterHeader;
    hgt += g.wrappedHeight(linesFont, width - insL - insR - 4, lines);
    needSpace = true;
  }
  if (!footer.empty() && buttonMode != BUTTONS_FOOTER) {
    if (needSpace) hgt += 8;
    hgt += g.fontLineSpacing(headerFont);
    needSpace = true;
  }
  if (yesBtn) {
    if (needSpace) hgt += 8;
    hgt += buttonH + 8;
  }
  return hgt;
}

void Dialog::layoutButtons() {
  if (yesBtn && noBtn) {
    float bw = (w - insL - insR - 8) / 2;
    yesBtn->x = insL;
    yesBtn->y = h - insB - buttonH;
    yesBtn->w = bw;
    yesBtn->h = buttonH;
    noBtn->x = yesBtn->x + bw + 8;
    noBtn->y = yesBtn->y;
    noBtn->w = bw;
    noBtn->h = buttonH;
  } else if (yesBtn) {
    yesBtn->x = insL;
    yesBtn->y = h - insB - buttonH;
    yesBtn->w = w - insL - insR;
    yesBtn->h = buttonH;
  }
}

void Dialog::resize(float nx, float ny, float nw, float nh) {
  x = nx;
  y = ny;
  w = nw;
  h = nh;
  layoutButtons();
}

float Dialog::contentTop(Gfx& g) const { return insT + g.fontHeight(headerFont) + spaceAfterHeader - 12; }

void Dialog::doScroll(bool) {
  app.playSample("DIALOG_MOVE");
  scrollTarget = y < 0 ? 100 : y;
  y = 600;
  scrollActive = true;
  scrollingOff = false;
  mouseVisible = true;
}

void Dialog::doScrollOff() {
  if (scrollingOff) return;
  app.playSample("DIALOG_MOVE");
  scrollingOff = true;
  scrollActive = true;
  scrollTarget = -h;
  mouseVisible = false;
}

void Dialog::update() {
  Panel::update();
  if (!scrollActive) return;
  float dist = std::fabs(y - scrollTarget);
  float step = scrollingOff ? 40.0f : std::clamp(std::floor(dist / 5), 10.0f, 40.0f);
  y -= step;
  if (y <= scrollTarget) {
    y = scrollTarget;
    scrollActive = false;
    if (scrollingOff) gone = true;
  }
}

void Dialog::drawLayer(Gfx& g) {
  if (rope) {
    Tex* con = g.img("IMAGE_DLG_CONNECTOR");
    Tex* seg = g.img("IMAGE_DLG_SEGMENT");
    if (con && seg) {
      float cy = 6 - con->lh();
      for (float ty = cy - 30; y + seg->lh() + ty >= 0; ty -= ropeLen) g.draw(seg, (w - seg->lw()) / 2, ty);
      g.draw(con, (w - con->lw()) / 2, cy);
    }
  }
  Tex* box = g.img("IMAGE_DLG_DIALOGBOX");
  if (box) g.drawBox(box, 0, 0, w, h);
  else g.fill(0, 0, w, h, 0x203050, 0.95f);
  float curY = insT;
  if (!header.empty()) {
    float asc = g.fontAscent(headerFont);
    g.drawString(headerFont, w / 2, curY + asc, header, headerColor, 1, Align::Center);
    curY += g.fontHeight(headerFont) + spaceAfterHeader;
  }
  if (!lines.empty()) {
    Align al = textAlign < 0 ? Align::Left : textAlign > 0 ? Align::Right : Align::Center;
    Rect r{insL + 2, curY, w - insL - insR - 4, 0};
    g.drawWrapped(linesFont, r, lines, linesColor, 1, al);
  }
  if (!footer.empty() && buttonMode != BUTTONS_FOOTER) {
    float fy = h - insB - (yesBtn ? buttonH + 8 : 0) - 4;
    g.drawString(headerFont, w / 2, fy, footer, 0xffffff, 1, Align::Center);
  }
  if (extraDraw) extraDraw(*this, g);
}

static bool navigable(const Widget* w) {
  if (!w->visible || !w->enabled || !w->mouseVisible) return false;
  return dynamic_cast<const Button*>(w) || dynamic_cast<const Checkbox*>(w) || dynamic_cast<const Slider*>(w);
}

void Dialog::cycleFocus(int dir) {
  std::vector<Widget*> list;
  for (auto& w : widgets)
    if (navigable(w.get()) || (w.get() == focusW && w->visible)) list.push_back(w.get());
  if (list.empty()) return;
  int cur = -1;
  for (size_t i = 0; i < list.size(); i++)
    if (list[i] == focusW) cur = (int)i;
  int n = (int)list.size();
  int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : ((cur + dir) % n + n) % n;
  if (focusW) focusW->focused = false;
  focusW = list[next];
  focusW->focused = true;
  app.playSample("MOUSEOVER");
}

bool Dialog::focusKey(int key) {
  if (!focusW || dynamic_cast<EditBox*>(focusW)) return false;
  if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
    if (auto* b = dynamic_cast<Button*>(focusW)) { app.playSample(b->clickSound); onButton(b->id); return true; }
    if (auto* c = dynamic_cast<Checkbox*>(focusW)) { c->mouseDown(0, 0, 1, 1); return true; }
    return false;
  }
  if (auto* s = dynamic_cast<Slider*>(focusW)) {
    if (key == SDLK_LEFT || key == SDLK_RIGHT) {
      s->value = std::clamp(s->value + (key == SDLK_LEFT ? -0.05 : 0.05), 0.0, 1.0);
      s->host->sliderChanged(s->id, s->value);
      return true;
    }
  }
  return false;
}

void Dialog::drawOverlay(Gfx& g) {
  if (!focusW || !focusW->visible || dynamic_cast<EditBox*>(focusW) || scrollingOff) return;
  const float x0 = focusW->x - 2, y0 = focusW->y - 1, w0 = focusW->w + 4, h0 = focusW->h + 2;
  g.fill(x0, y0, w0, 2, 0xffff66, 0.9f);
  g.fill(x0, y0 + h0 - 2, w0, 2, 0xffff66, 0.9f);
  g.fill(x0, y0, 2, h0, 0xffff66, 0.9f);
  g.fill(x0 + w0 - 2, y0, 2, h0, 0xffff66, 0.9f);
}

bool Dialog::input(const Input& in) {
  if (scrollingOff || (scrollActive && in.type == Input::MouseDown)) return true;
  if (in.type == Input::KeyDown) {
    if (in.key == SDLK_TAB && !scrollActive) { cycleFocus((in.mod & SDL_KMOD_SHIFT) ? -1 : 1); return true; }
    if (!scrollActive && focusKey(in.key)) return true;
    keyDown(in.key, in.mod);
    if (in.key == SDLK_ESCAPE && noBtn) { onButton(escapeButton); return true; }
    if (in.key == SDLK_ESCAPE && buttonMode == BUTTONS_FOOTER) { onButton(ID_FOOTER); return true; }
    if ((in.key == SDLK_RETURN || in.key == SDLK_KP_ENTER) && yesBtn && buttonMode != BUTTONS_NONE && !focusW) { onButton(returnButton); return true; }
  }
  return Panel::input(in);
}

void Dialog::onButton(int bid) { app.dialogButton(dialogId, bid); }

}
