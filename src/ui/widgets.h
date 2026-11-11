#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include "ui/gfx.h"

namespace pp::ui {

struct Input {
  enum Type { MouseMove, MouseDown, MouseUp, Wheel, KeyDown, KeyUp, Text, FocusLost, FocusGained, MouseLeave } type = MouseMove;
  float x = 0, y = 0;
  float wx = 0, wy = 0;
  int button = 1;
  int clicks = 1;
  int key = 0;
  int mod = 0;
  float wheel = 0;
  std::string text;
};

class App;
class Widget;

struct WidgetHost {
  virtual ~WidgetHost() = default;
  virtual void buttonDepress(int id) = 0;
  virtual void buttonPress(int id, int clicks) {}
  virtual void buttonEnter(int id) {}
  virtual void buttonLeave(int id) {}
  virtual void checkboxChanged(int id, bool checked) {}
  virtual void sliderChanged(int id, double v) {}
  virtual void playSound(const std::string& id, float pitchSemis = 0) {}
  virtual bool labelFullGradient() const { return false; }
};

class Widget {
 public:
  virtual ~Widget() = default;
  int id = 0;
  float x = 0, y = 0, w = 0, h = 0;
  bool visible = true, enabled = true, over = false, down = false, mouseVisible = true;
  float alpha = 1;
  WidgetHost* host = nullptr;
  Rect rect() const { return {x, y, w, h}; }
  bool hit(float px, float py) const { return visible && mouseVisible && px >= x && py >= y && px < x + w && py < y + h; }
  virtual void update() {}
  virtual void draw(Gfx& g) = 0;
  virtual bool mouseDown(float lx, float ly, int button, int clicks) { return false; }
  virtual bool mouseUp(float lx, float ly, int button) { return false; }
  virtual void mouseMove(float lx, float ly) {}
  virtual void mouseEnter() { over = true; }
  virtual void mouseLeave() { over = false; }
  virtual bool key(int sdlKey, int mod) { return false; }
  virtual bool text(const std::string& t) { return false; }
  virtual bool wantsFocus() const { return false; }
  bool focused = false;
};

class Button : public Widget {
 public:
  enum class Kind { Embedded, Patch, Simple, Link };
  Kind kind = Kind::Patch;
  Tex* image = nullptr;
  std::string label, font = "BUTTON";
  uint32_t labelColor = 0xffffff, labelOver = 0xffffff;
  float textOffX = 0, textOffY = 0;
  float overAlpha = 0;
  float fadeIn = 0.2f, fadeOut = 0.08f;
  bool idleImage = false;
  bool labelOnEmbedded = false;
  int labelStyle = -1;
  bool labelFullGradient = false;
  bool additiveDown = false;
  std::string hoverSound = "MOUSEOVER", clickSound = "BUTTON1";
  float hoverPitch = 0;
  int lastHoverTick = -1000, *tickRef = nullptr;
  int blink = 0;
  uint32_t colNormal = 0xffffff, colOver = 0xffff00, colDown = 0x88ff88;
  void update() override;
  void draw(Gfx& g) override;
  bool mouseDown(float lx, float ly, int button, int clicks) override;
  bool mouseUp(float lx, float ly, int button) override;
  void mouseEnter() override;
  void mouseLeave() override;
};

class Checkbox : public Widget {
 public:
  Tex* image = nullptr;
  int style = 0;
  std::string label, font = "BUTTON";
  bool checked = false;
  float labelX = 40, labelY = 2;
  std::string clickSound = "BUTTON2";
  void draw(Gfx& g) override;
  bool mouseDown(float lx, float ly, int button, int clicks) override;
  bool mouseUp(float lx, float ly, int button) override { return false; }
};

class Slider : public Widget {
 public:
  std::string label;
  double value = 0;
  bool dragging = false;
  Tex* thumb = nullptr;
  void draw(Gfx& g) override;
  bool mouseDown(float lx, float ly, int button, int clicks) override;
  bool mouseUp(float lx, float ly, int button) override;
  void mouseMove(float lx, float ly) override;
  void setFromX(float lx);
};

class EditBox : public Widget {
 public:
  std::string value, font = "PLAIN";
  size_t maxChars = 12;
  float maxWidth = 190;
  std::string widthFont = "OVERLOAD24";
  int caret = 0, tick = 0;
  float textPad = 4;
  bool clipText = false;
  bool paper = false;
  uint32_t ink = 0xffffff;
  bool wantsFocus() const override { return true; }
  void update() override { tick++; }
  void draw(Gfx& g) override;
  bool key(int sdlKey, int mod) override;
  bool text(const std::string& t) override;
  bool mouseDown(float lx, float ly, int button, int clicks) override { return true; }
  float textShift(Gfx& g) const;
  std::function<void()> onEnter;
  Gfx* gfx = nullptr;
};

}
