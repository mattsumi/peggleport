#pragma once

#include <algorithm>
#include "ui/gfx.h"
#include "ui/pad.h"

namespace pp::ui {

inline Tex* secondaryArt(Gfx& g, const char* name) {
  const auto path = std::string("console/secondary/") + name + ".png";
  return g.vfs().exists(path) ? g.img(path) : nullptr;
}

inline Rect secondaryCover(Tex& t, float w, float h = 600) {
  const float scale = std::max(w / t.lw(), h / t.lh());
  return {(w - scale * t.lw()) / 2, (h - scale * t.lh()) / 2, scale * t.lw(), scale * t.lh()};
}

float drawInstituteScene(Gfx& g, float width, int tick);
float drawPromptSleeve(Gfx& g, float x, float y, int glyph, const char* key, const char* label, const PadGlyphs& pad, float alpha = 1);
bool drawConsoleTitle(Gfx& g, float cx, float width, const char* title);
void drawConsoleRail(Gfx& g, float width);
float drawConsolePrompt(Gfx& g, float x, int glyph, const char* key, const char* label, const PadGlyphs& pad);

inline void secondaryFooter(Gfx& g, float width) {
  constexpr float y = 550;
  g.fill(0, y, width, 50, 0x201140, 0.97f);
  g.fill(0, y + 2, width, 2, 0x73c9ff);
  g.fill(0, y + 5, width, 2, 0x8052a4);
}

inline void secondaryPanel(Gfx& g, float x, float y, float w, float h, float heading = 0) {
  g.fill(x + 3, y + 4, w, h, 0x090512, 0.8f);
  g.fill(x, y, w, h, 0x19103b, 0.97f);
  if (heading > 0) g.fill(x + 2, y + 2, w - 4, heading, 0x25386d);
  g.frame(x, y, w, h, 0xdeb076);
  g.frame(x + 2, y + 2, w - 4, h - 4, 0x605292);
}

}
