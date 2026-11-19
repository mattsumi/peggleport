#pragma once

#include "ui/gfx.h"

namespace pp::ui {

inline constexpr int kLoaderCells = 25;

inline void drawLoaderCell(Gfx& g, Tex* lights, Rect plate, int cell, float alpha = 1) {
  if (cell < 0 || cell >= kLoaderCells) return;
  const float kx = plate.w / 400, ky = plate.h / 100;
  const auto piece = [&](int sx, int sw, float x, float y, float w, float h) {
    g.drawStretchSrc(lights, sx, 0, sw, 49, plate.x + x * kx, plate.y + y * ky, w * kx, h * ky, alpha);
  };
  if (cell == 0) piece(76, 14, 28, 29, 12, 43);
  else if (cell == 1) piece(55, 17, 38, 29, 16, 44);
  else if (cell == kLoaderCells - 1) piece(0, 18, 355, 29, 16, 44);
  else piece(cell % 2 ? 19 : 37, 17, 37 + (cell - 1) * 13.85f, 27, 17, 48);
}

}
