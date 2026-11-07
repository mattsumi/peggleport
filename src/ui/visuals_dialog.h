#pragma once
#include <vector>
#include "ui/dialog.h"

namespace pp::ui {

enum { D_Visuals = 61 };

class VisualsDialog : public Dialog {
 public:
  explicit VisualsDialog(App& a);
  Checkbox *interp, *smooth, *bloom, *sparks, *uwhud, *sepWide;
  Slider* bloomStr;
  float extraH = 0;
  struct Row { Checkbox* box; const char* hint; };
  std::vector<Row> rows;
  void layout();
  float preferredHeight(float width) override;
  void drawLayer(Gfx& g) override;
  void onButton(int id) override;
  void checkboxChanged(int id, bool on) override;
  void sliderChanged(int id, double v) override;
};

}
