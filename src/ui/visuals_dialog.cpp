#include "ui/visuals_dialog.h"
#include <algorithm>
#include <utility>
#include <memory>

namespace pp::ui {

static const float kRowH = 46;

VisualsDialog::VisualsDialog(App& a) : Dialog(a, D_Visuals, true, "ENHANCED VISUALS", "", "Done", BUTTONS_FOOTER) {
  auto mkCheck = [&](int id, const char* label, bool v, const char* hint) {
    auto c = std::make_unique<Checkbox>();
    c->id = id;
    c->image = a.gfx.img("IMAGE_DLG_BUTTON2");
    c->style = 1;
    c->label = label;
    c->checked = v;
    c->labelX = 30;
    c->w = a.gfx.textWidth("BUTTON", label) + 30;
    c->h = c->image ? c->image->lh() / 3 : 24;
    Checkbox* raw = add(std::move(c));
    rows.push_back({raw, hint});
    return raw;
  };
  const VisualOptions& v = a.settings.vis;
  interp = mkCheck(0, "Smooth Motion", v.interpolate, "Draws balls and effects between the 10 ms steps.");
  smooth = mkCheck(1, "Smooth Scaling", v.smooth, "Better filtering of the art in very large or very small windows.");
  bloom = mkCheck(2, "Bloom Glow", v.bloom, "Soft glow around lit pegs, fireworks and rainbows.");
  sparks = mkCheck(3, "Peg Sparks", v.sparks, "Little sparks fly off every peg that is hit.");
  uwhud = mkCheck(4, "Ultra-Wide HUD", v.ultraWideHud, "Stretch the frame over the whole window, whatever its shape.");
  auto s = std::make_unique<Slider>();
  s->id = 6;
  s->label = "Bloom Strength";
  s->value = std::clamp(v.bloomStrength / 2.0f, 0.0f, 1.0f);
  s->w = 0xbe;
  s->h = 0x24;
  bloomStr = add(std::move(s));
  bloomStr->enabled = v.bloom;
  extraH = rows.size() * kRowH + 38 + 12;
}

float VisualsDialog::preferredHeight(float width) { return Dialog::preferredHeight(width) + kOwnLayoutExtra + extraH; }

void VisualsDialog::layout() {
  float y = contentTop(app.gfx) - 8;
  for (size_t i = 0; i < rows.size(); i++) {
    rows[i].box->x = insL;
    rows[i].box->y = y;
    y += kRowH;
    if (i == 2) {
      bloomStr->x = insL + 170;
      bloomStr->y = y - 4;
      y += 38;
    }
  }
}

void VisualsDialog::drawLayer(Gfx& g) {
  layout();
  Dialog::drawLayer(g);
  for (auto& r : rows)
    g.drawString("PLAIN", r.box->x + 30, r.box->y + r.box->h + 13, r.hint, r.box->enabled ? 0xbfd8ff : 0x808080, 1.0f);
}

void VisualsDialog::checkboxChanged(int id, bool on) {
  VisualOptions& v = app.settings.vis;
  switch (id) {
    case 0: v.interpolate = on; break;
    case 1: v.smooth = on; break;
    case 2: v.bloom = on; bloomStr->enabled = on; break;
    case 3: v.sparks = on; break;
    case 4: v.ultraWideHud = on; break;
  }
}

void VisualsDialog::sliderChanged(int id, double val) {
  if (id == 6) app.settings.vis.bloomStrength = (float)(val * 2.0);
}

void VisualsDialog::onButton(int id) {
  if (scrollActive && !scrollingOff) return;
  if (!app.cfg.noSave) app.saveAll();
  doScrollOff();
}

void App::doVisualsDialog() {
  if (getDialog(D_Visuals)) return;
  auto d = std::make_unique<VisualsDialog>(*this);
  Dialog* raw = d.get();
  positionDialog(raw, 540);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

}
