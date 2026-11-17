#pragma once
#include "ui/dialog.h"

namespace pp::ui {

enum { D_Slot = 23 };

struct SpinWheel {
  float angle = 0, speed = 0.15f;
  int light = 0, phase = 0, lit = 0, dings = 0, timer = 0, hold = 40;
  int bright[40] = {};
  void calcLight();
  bool updatePointer(bool& ding);
};

class SlotMachineDialog : public Dialog {
 public:
  SlotMachineDialog(App& a, int outcome, int power, int spinCode = 0);
  void update() override;
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  SpinWheel wheel;
  bool exiting = false;
  int result = 0;
  int cell = 0;
  bool prizeDone = false;
  void skip();
  void playPrize();
};

}
