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
        g.drawString(font, (w - g.textWidth(font, label)) / 2, (h + g.fontAscent(font) - g.fontAscent(font) / 6 - 1) / 2 + textOffY + 4, label, over ? labelOver : labelColor, alpha);
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
        float asc = g.fontAscent(font);
        float ty = (h + asc - asc / 6 - 1) / 2 + textOffY;
        float tw = g.textWidth(font, label);
        g.drawString(font, (w - tw) / 2 + textOffX + dx, ty + dy, label, over ? labelOver : labelColor, alpha);
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
  if (!label.empty()) g.drawString(font, labelX, (h + g.fontAscent(font) * 0.8f) / 2 + labelY, label, 0xffffff, alpha);
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
  if (!label.empty()) g.drawString("BUTTON", bx + 18, by + 31, label, 0xffffff, alpha);
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
