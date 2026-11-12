#include "ui/widgets.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

#include <algorithm>
#include <cmath>

namespace pp::ui {


void Button::update() {
  if (blink > 0) blink--;
  if (over && overAlpha < 1) overAlpha = std::min(1.0f, overAlpha + fadeIn);
  else if (!over && overAlpha > 0) overAlpha = std::max(0.0f, overAlpha - fadeOut);
}

void Button::mouseEnter() {
  over = true;
  if (host && enabled) {
    host->buttonEnter(id);
    if (!hoverSound.empty() && tickRef && *tickRef - lastHoverTick > 30) {
      host->playSound(hoverSound, hoverPitch);
      lastHoverTick = *tickRef;
    }
  }
}
void Button::mouseLeave() {
  over = false;
  down = false;
  if (host) host->buttonLeave(id);
}
bool Button::mouseDown(float lx, float ly, int button, int clicks) {
  if (!enabled || button != 1) return false;
  down = true;
  over = true;
  if (host && !clickSound.empty()) host->playSound(clickSound);
  if (host) host->buttonPress(id, clicks);
  return true;
}
bool Button::mouseUp(float lx, float ly, int button) {
  if (!down) return false;
  down = false;
  bool inside = lx >= 0 && ly >= 0 && lx < w && ly < h;
  if (inside && enabled && host) host->buttonDepress(id);
  return true;
}

static void drawStyledLabel(Gfx& g, const std::string& font, float x, float y, const std::string& text, uint32_t rgb, float alpha, float styleAlpha, uint32_t outline, uint32_t main, uint32_t gradient, bool fullGradient) {
  FontProvider& fp = g.fontProvider();
  uint32_t mult[3] = {0, 0, 0}, add[3] = {0, 0, 0};
  bool have = true;
  for (size_t i = 0; i < 3; i++) have = fp.layerColors(font, i, mult[i], add[i]) && have;
  if (have) {
    fp.setLayerColors(font, 0, outline, add[0]);
    fp.setLayerColors(font, 1, main, add[1]);
    if (fullGradient) fp.setLayerColors(font, 2, mult[2], gradient | 0xffu);
    else fp.setLayerColors(font, 2, mult[2] & 0xffffff00u, gradient | (uint32_t)std::clamp((int)(90.0f * styleAlpha), 0, 255));
  }
  g.drawString(font, x, y, text, rgb, alpha);
  if (have)
    for (size_t i = 0; i < 3; i++) fp.setLayerColors(font, i, mult[i], add[i]);
}

static void drawControlLabel(Gfx& g, const Widget& w, const std::string& font, float x, float y, const std::string& text, float styleAlpha) {
  if (font != "BUTTON") {
    g.drawString(font, x, y, text, 0xffffff, w.alpha);
    return;
  }
  drawStyledLabel(g, font, x, y, text, 0xffffff, w.alpha, styleAlpha, w.down ? 0x1a3bd6ffu : 0x1333d8ffu, w.down ? 0xffffffffu : 0x65f3f8ffu, w.down ? 0xd2f7ff00u : 0x4cc4f700u, w.host && w.host->labelFullGradient());
}

static void drawButtonLabel(Gfx& g, const Button& b, float x, float y) {
  const uint32_t rgb = b.over ? b.labelOver : b.labelColor;
  if ((b.labelStyle != 0 && b.labelStyle != 6 && b.labelStyle != 9 && b.labelStyle != 16) || b.font != "BUTTON") {
    g.drawString(b.font, x, y, b.label, rgb, b.alpha);
    return;
  }
  static const uint32_t kOutlines[4][3] = {{0x00264cccu, 0x003366ccu, 0x003f7fccu}, {0x5b1e54ffu, 0x600256ffu, 0xe533ccffu}, {0x560700ccu, 0x4c000cccu, 0xc41400ccu}, {0x821c01ffu, 0xb32702ffu, 0xe55e04ffu}};
  static const uint32_t kMains[4][3] = {{0xe7fff3ffu, 0xffff00ffu, 0xffff00ffu}, {0xfb54eaffu, 0xfe94e7ffu, 0xfdb6f7ffu}, {0xffba02ffu, 0xfac841ffu, 0xf6fd42ffu}, {0xfff3a3ffu, 0xfffadaffu, 0xffffffffu}};
  static const uint32_t kGradients[4][3] = {{0x19b24c00u, 0x33d86600u, 0x4cff7f00u}, {0xcc2bb700u, 0xe08cef00u, 0xff99ff00u}, {0xfc5e0000u, 0xffaf0000u, 0xf4b21400u}, {0xffcc0000u, 0xffe40000u, 0xfffeac00u}};
  const size_t set = b.labelStyle == 16 ? 1 : (b.labelStyle == 6 ? 2 : (b.labelStyle == 9 ? 3 : 0));
  const uint32_t* kOutline = kOutlines[set];
  const uint32_t* kMain = kMains[set];
  const uint32_t* kGradient = kGradients[set];
  const int state = (b.down && b.over) ? 2 : ((b.down || b.over) ? 1 : 0);
  drawStyledLabel(g, b.font, x, y, b.label, rgb, b.alpha, b.alpha, kOutline[state], kMain[state], kGradient[state], b.labelFullGradient);
}

void Button::draw(Gfx& g) {
  if (!visible || (!image && kind != Kind::Link)) return;
  Mode full(alpha);
  float dx = 0, dy = 0;
  bool pressed = down && over;
  switch (kind) {
    case Kind::Embedded: {
      float a = overAlpha;
      if (blink > 0 && !over && !down) a = std::max(a, 0.5f + 0.5f * std::sin(blink * 0.2f)) * 0.7f;
      if (idleImage) {
        g.drawCel(image, 0, 0, 0, 0, full);
        if (pressed) g.drawCel(image, 0, 0, 0, 1, full);
        else if (a > 0) g.drawCel(image, 0, 0, 0, 1, Mode(alpha * a));
      } else if (pressed) {
        g.drawCel(image, 0, 0, 0, additiveDown ? 0 : 1, Mode(alpha));
        if (additiveDown) g.drawCel(image, 0, 0, 0, 0, Mode(alpha * 0.25f, 0xffffff, true));
      } else if (a > 0) {
        g.drawCel(image, 0, 0, 0, 0, Mode(alpha * a));
      }
      if (labelOnEmbedded && !label.empty())
        drawButtonLabel(g, *this, (w - g.textWidth(font, label)) / 2, (h + g.fontAscent(font) - g.fontAscent(font) / 6 - 1) / 2 + textOffY + 4);
      return;
    }
    case Kind::Simple:
      g.draw(image, (w - image->lw()) / 2, (h - image->lh()) / 2, Mode(alpha, down || over ? (over ? colOver : colDown) : colNormal));
      return;
    case Kind::Link: {
      uint32_t c = over ? colOver : colNormal;
      float tw = g.textWidth(font, label);
      g.drawString(font, (w - tw) / 2, g.fontAscent(font), label, c, alpha);
      return;
    }
    case Kind::Patch: {
      float ch = image->lh() / 3;
      float row = pressed ? 2.0f : 0.0f;
      if (!pressed && overAlpha > 0) {
        if (overAlpha < 1) g.drawBoxSrc(image, 0, 0, image->lw(), ch, 0, 0, w, h, full);
        g.drawBoxSrc(image, 0, ch, image->lw(), ch, 0, 0, w, h, Mode(alpha * overAlpha));
      } else {
        g.drawBoxSrc(image, 0, ch * row, image->lw(), ch, dx, dy, w, h, full);
      }
      if (!label.empty()) {
        const int asc = (int)g.fontAscent(font);
        float ty = (float)(((int)h + asc - (int)g.fontAscentPadding(font) - asc / 6 - 1) / 2) + textOffY;
        float tw = g.textWidth(font, label);
        drawButtonLabel(g, *this, (w - tw) / 2 + textOffX + dx, ty + dy);
      }
      return;
    }
  }
}


void Checkbox::draw(Gfx& g) {
  if (!image) return;
  float rows = style == 0 ? 4.0f : 3.0f, ch = image->lh() / rows;
  int row;
  if (style == 0) row = checked ? (over ? 3 : 0) : (over ? 1 : 2);
  else row = checked ? 0 : (over ? 1 : 2);
  g.drawBoxSrc(image, 0, ch * row, image->lw(), ch, 0, 0, w, h, Mode(alpha));
  if (!label.empty()) drawControlLabel(g, *this, font, labelX, (h + g.fontAscent(font) * 0.8f) / 2 + labelY, label, alpha);
}
bool Checkbox::mouseDown(float lx, float ly, int button, int clicks) {
  if (!enabled || button != 1) return false;
  checked = !checked;
  if (host) {
    host->playSound(clickSound);
    host->checkboxChanged(id, checked);
  }
  return true;
}


void Slider::setFromX(float lx) {
  float tw = thumb ? thumb->lw() : 20;
  value = std::clamp((lx - tw / 2) / std::max(1.0f, w - tw), 0.0f, 1.0f);
  if (host) host->sliderChanged(id, value);
}
bool Slider::mouseDown(float lx, float ly, int button, int clicks) {
  if (button != 1) return false;
  dragging = true;
  setFromX(lx);
  return true;
}
bool Slider::mouseUp(float lx, float ly, int button) {
  if (!dragging) return false;
  dragging = false;
  return true;
}
void Slider::mouseMove(float lx, float ly) {
  if (dragging) setFromX(lx);
}
void Slider::draw(Gfx& g) {
  Tex* bar = g.img("IMAGE_DLG_SLIDER");
  Tex* hi = g.img("IMAGE_DLG_SLIDERHILIGHT");
  Tex* orb = thumb ? thumb : g.img("IMAGE_DLG_SLIDERORB");
  float bx = -170, by = -2;
  if (bar) g.draw(bar, bx, by);
  if (!label.empty()) drawControlLabel(g, *this, "BUTTON", bx + 18, by + 31, label, 1.0f);
  if (hi && orb) {
    float tw = orb->lw();
    float cw = std::clamp((float)((hi->lw() + tw) * value), 0.0f, hi->lw());
    g.drawSrc(hi, 0, 0, cw, hi->lh(), 0, 9);
    float ox = (float)(value * (w - tw));
    g.draw(orb, ox, (h - orb->lh()) / 2);
  }
}


bool EditBox::text(const std::string& t) {
  for (unsigned char c : t) {
    if (c < 32 || c == 127) continue;
    if (value.size() >= maxChars) break;
    std::string tryv = value;
    tryv.insert(tryv.begin() + caret, (char)c);
    if (gfx && gfx->textWidth(widthFont, tryv) > maxWidth) break;
    value = tryv;
    caret++;
  }
  return true;
}
bool EditBox::key(int k, int mod) {
  if (k == SDLK_BACKSPACE) {
    if (caret > 0) { value.erase(value.begin() + caret - 1); caret--; }
    return true;
  }
  if (k == SDLK_DELETE) {
    if (caret < (int)value.size()) value.erase(value.begin() + caret);
    return true;
  }
  if (k == SDLK_LEFT) { caret = std::max(0, caret - 1); return true; }
  if (k == SDLK_RIGHT) { caret = std::min((int)value.size(), caret + 1); return true; }
  if (k == SDLK_HOME) { caret = 0; return true; }
  if (k == SDLK_END) { caret = (int)value.size(); return true; }
  if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
    if (onEnter) onEnter();
    return true;
  }
  return false;
}
void EditBox::draw(Gfx& g) {
  if (!paper) {
    Tex* box = g.img("IMAGE_DLG_TEXTBOX");
    if (box) g.drawBox(box, 0, 0, w, h);
    else g.fill(0, 0, w, h, 0x000000, 0.6f);
  }
  float asc = g.fontAscent(font);
  float ty = (h + asc * 0.8f) / 2;
  const float shift = textShift(g);
  if (clipText) g.pushClip(textPad - 1, 0, w - 2 * textPad + 2, h);
  g.drawString(font, textPad - shift, ty, value, ink);
  if (focused && (tick / 40) % 2 == 0) {
    float cx = textPad - shift + g.textWidth(font, value.substr(0, caret));
    g.fill(cx, 3, 1, h - 6, ink);
  }
  if (clipText) g.popClip();
}
float EditBox::textShift(Gfx& g) const {
  if (!clipText) return 0;
  const float room = w - 2 * textPad, all = g.textWidth(font, value);
  if (all <= room) return 0;
  return std::clamp(g.textWidth(font, value.substr(0, (size_t)std::max(0, caret))) - room, 0.0f, all - room);
}

}
