#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "ui/app.h"

namespace pp::ui {

enum ButtonMode { BUTTONS_NONE = 0, BUTTONS_OK_CANCEL = 1, BUTTONS_YES_NO = 2, BUTTONS_FOOTER = 3 };
enum { ID_YES = 1000, ID_NO = 1001, ID_FOOTER = 1000, ID_CLOSE = 1002 };

class Dialog : public Panel {
 public:
  Dialog(App& a, int dialogId, bool modal, const std::string& header, const std::string& lines, const std::string& footer, int buttonMode);
  int dialogId;
  bool modal;
  std::string header, lines, footer;
  int buttonMode;
  Button *yesBtn = nullptr, *noBtn = nullptr;
  int escapeButton = ID_NO, returnButton = ID_YES;
  float insL = 35, insT = 20, insR = 35, insB = 26;
  float spaceAfterHeader = 32;
  std::string headerFont = "OVERLOAD24GRAD1", linesFont = "PLAINOUTLINE";
  uint32_t headerColor = 0x63f7ff, linesColor = 0xffffff;
  int textAlign = 0;
  float buttonH = 0;
  bool rope = false;
  bool scrollingOff = false, removeWhenGone = false, gone = false;
  float scrollTarget = -1e9f;
  bool scrollActive = false;
  int scrollDir = 0;
  int goneTicks = 0;
  float ropeLen = 35;
  std::function<void(Dialog&, Gfx&)> extraDraw;

  void doScroll(bool fromTop = true);
  void doScrollOff();
  void killNow() { gone = true; }
  virtual float preferredHeight(float width);
  void layoutButtons();
  void resize(float nx, float ny, float nw, float nh);
  void update() override;
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  void onButton(int id) override;
  void cycleFocus(int dir);
  bool focusKey(int key);
  void drawOverlay(Gfx& g) override;
  float contentLeft() const { return insL; }
  float contentWidth() const { return w - insL - insR; }
  float contentTop(Gfx& g) const;
  void setYesNo(const std::string& yes, const std::string& no);
};

}
