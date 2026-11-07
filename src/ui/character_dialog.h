#pragma once
#include <vector>
#include <string>
#include "ui/dialog.h"

namespace pp::ui {

class CharacterDialog : public Dialog {
 public:
  explicit CharacterDialog(App& a);
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  float preferredHeight(float width) override;
  void accept();
  struct Info { std::string name, power, desc; };
  std::vector<Info> info;
  int hover = -1;
  bool cellAt(float lx, float ly, int* player, int* idx) const;
  void cellRect(int player, int idx, float* x, float* y, float* w, float* h) const;
  int sel[2] = {0, 1};
  int count = 10;
  bool computer = true;
  int skill = 1;
  Checkbox *cbFriend = nullptr, *cbComputer = nullptr, *cbNormal = nullptr, *cbMaster = nullptr;
  void checkboxChanged(int id, bool on) override;
  void layoutBoxes();
};

}
