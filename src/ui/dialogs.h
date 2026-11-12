#pragma once
#include <functional>
#include <vector>
#include <string>
#include "ui/dialog.h"
#include "ui/game_result.h"

namespace pp::ui {

class ListBox : public Widget {
 public:
  std::vector<std::string> lines;
  int select = -1, hilite = -1, top = 0;
  uint32_t back = 0x000050;
  bool centered = true;
  std::string font = "PLAIN";
  float itemH = 16;
  std::function<void(int idx, int clicks)> onClick;
  Gfx* gfx = nullptr;
  int visibleItems() const { return (int)(h / itemH); }
  void draw(Gfx& g) override;
  bool mouseDown(float lx, float ly, int button, int clicks) override;
  void mouseMove(float lx, float ly) override;
  void mouseLeave() override { over = false; hilite = -1; }
  void wheel(float d);
  void reveal();
  int indexAt(float ly) const { int i = top + (int)(ly / itemH); return i >= 0 && i < (int)lines.size() ? i : -1; }
};

class UserDialog : public Dialog {
 public:
  explicit UserDialog(App& a);
  ListBox* list;
  Button *rename, *del, *create, *importBtn;
  void layout();
  float preferredHeight(float width) override;
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  std::string selName() const { return list->select >= 0 && list->select < (int)list->lines.size() ? list->lines[list->select] : ""; }
  void onButton(int id) override;
  void confirmDelete();
  void finish(bool accepted);
  void renamed(const std::string& to) { if (list->select >= 0) list->lines[list->select] = to; }
};

class NewUserDialog : public Dialog {
 public:
  NewUserDialog(App& a, bool isRename, bool certificate, bool canCancel);
  EditBox* edit;
  Button* okBtn = nullptr;
  Button* importBtn = nullptr;
  bool certificate;
  std::string dateText;
  int stamp = 0;
  void layout();
  void drawLayer(Gfx& g) override;
  void update() override;
  std::string name() const { return edit->value; }
  void setName(const std::string& n) { edit->value = n; edit->caret = (int)n.size(); }
  void onButton(int id) override;
  void finish(bool accepted);
  void doApproved();
  float preferredHeight(float width) override;
};

class OptionsDialog : public Dialog {
 public:
  OptionsDialog(App& a, bool fromGame);
  bool fromGame;
  Slider *music, *sfx, *fever;
  Checkbox *fullscreen, *cursors, *vsync, *colorblind;
  Button *help, *mainMenu, *newLevel, *newChallenge;
  Button* modsBtn = nullptr;
  Button *editor;
  Button* gamesBtn = nullptr;
  Button* visuals = nullptr;
  Button* widescreen;
  Button* homeBtn = nullptr;
  Button* demoBtn = nullptr;
  float extraH = 0;
  void updateWidescreenLabel();
  void updateHomeLabel();
  void layout();
  void drawLayer(Gfx& g) override;
  float preferredHeight(float width) override;
  void onButton(int id) override;
  void sliderChanged(int id, double v) override;
  void checkboxChanged(int id, bool on) override;
};

}
