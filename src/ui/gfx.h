#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/res.h"
#include "ui/text.h"
#include "vfs.h"

namespace pp::ui {

struct Tex {
  SDL_Texture* t = nullptr;
  int w = 0, h = 0, cols = 1, rows = 1;
  float scale = 1;
  std::string path, mask;
  bool valid() const { return w > 0; }
  float lw() const { return w / scale; }
  float lh() const { return h / scale; }
  float cw() const { return lw() / cols; }
  float ch() const { return lh() / rows; }
};

struct Mode {
  float alpha = 1.0f;
  uint32_t tint = 0xffffff;
  bool add = false;
  Mode() = default;
  Mode(float a) : alpha(a) {}
  Mode(float a, uint32_t t, bool ad = false) : alpha(a), tint(t), add(ad) {}
};

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Gfx {
 public:
  Gfx(SDL_Renderer* r, const Vfs& vfs);
  ~Gfx();
  Gfx(const Gfx&) = delete;
  Gfx& operator=(const Gfx&) = delete;

  const Vfs& vfs() const { return vfs_; }
  const Manifest& man() const { return man_; }
  SDL_Renderer* renderer() const { return r_; }
  bool headless() const { return r_ == nullptr; }

  void refreshOutputSize();
  void begin(int canvasW = 800, int canvasH = 600);
  float scale() const { return s_; }
  float offX() const { return ox_; }
  float offY() const { return oy_; }
  int canvasW() const { return cw_; }
  int canvasH() const { return ch_; }
  void toCanvas(float wx, float wy, float& cx, float& cy) const { cx = (wx - ox_) / s_; cy = (wy - oy_) / s_; }
  void outputSize(int& w, int& h) const { w = ow_; h = oh_; }
  void setTransform(float scale, float ox, float oy) { s_ = scale; ox_ = ox; oy_ = oy; }
  void clear(uint32_t rgb = 0);

  bool beginBackdrop();
  void endBackdrop();
  void setBackdropEnabled(bool on) { bdEnabled_ = on; }

  void pushOrigin(float dx, float dy);
  void popOrigin();
  float originX() const { return orgX_; }
  float originY() const { return orgY_; }
  void pushClip(float x, float y, float w, float h);
  void clipToCanvas();
  void popClip();

  Tex* img(const std::string& idOrPath);
  void reloadAssets(const VfsChange& change);
  Tex* imgFile(const std::string& file);
  Tex* duelDim(const std::string& id);
  void preload(const std::string& id) { img(id); }
  void draw(Tex* t, float x, float y, Mode m = {});
  void drawCel(Tex* t, float x, float y, int col, int row, Mode m = {});
  void drawCelMirror(Tex* t, float x, float y, int col, int row, Mode m = {});
  void drawCel(Tex* t, float x, float y, int cel, Mode m = {}) { drawCel(t, x, y, cel % t->cols, cel / t->cols, m); }
  void drawSrc(Tex* t, float sx, float sy, float sw, float sh, float x, float y, Mode m = {});
  void drawStretch(Tex* t, float x, float y, float w, float h, Mode m = {});
  void drawStretchSrc(Tex* t, float sx, float sy, float sw, float sh, float x, float y, float w, float h, Mode m = {});
  void drawRot(Tex* t, float cx, float cy, float rad, float sx, float sy, Mode m = {});
  void drawRotCel(Tex* t, float cx, float cy, int col, int row, float rad, float sx, float sy, Mode m = {});
  void drawBox(Tex* t, float x, float y, float w, float h, Mode m = {});
  void drawBoxSrc(Tex* t, float sx, float sy, float sw, float sh, float x, float y, float w, float h, Mode m = {});
  void fill(float x, float y, float w, float h, uint32_t rgb, float alpha = 1.0f, bool add = false);
  void frame(float x, float y, float w, float h, uint32_t rgb, float alpha = 1.0f);
  void line(float x0, float y0, float x1, float y1, uint32_t rgb, float alpha = 1.0f);

  float textWidth(const std::string& font, const std::string& s);
  float fontHeight(const std::string& font) { return fp_->height(font); }
  float fontAscent(const std::string& font) { return fp_->ascent(font); }
  float fontAscentPadding(const std::string& font) { return fp_->ascentPadding(font); }
  float fontLineSpacing(const std::string& font) { return fp_->lineSpacing(font); }
  void drawString(const std::string& font, float x, float y, const std::string& s, uint32_t rgb = 0xffffff, float alpha = 1.0f, Align a = Align::Left);
  float drawWrapped(const std::string& font, Rect box, const std::string& s, uint32_t rgb = 0xffffff, float alpha = 1.0f, Align a = Align::Left, float lineSpacing = -1);
  float wrappedHeight(const std::string& font, float width, const std::string& s, float lineSpacing = -1);
  std::vector<std::string> wrap(const std::string& font, float width, const std::string& s);
  FontProvider& fontProvider() { return *fp_; }
  void setFontProvider(std::unique_ptr<FontProvider> p) { fp_ = std::move(p); }

 private:
  void blit(Tex* t, SDL_FRect src, float x, float y, float w, float h, Mode m, float rotDeg = 0, bool flipX = false);
  SDL_FRect dest(float x, float y, float w, float h, bool snap = true) const;
  void applyClip();

  SDL_Renderer* r_;
  const Vfs& vfs_;
  Manifest man_;
  bool haveMan_ = false;
  std::map<std::string, Tex> cache_;
  std::map<std::string, Tex> dimCache_;
  std::map<std::array<int, 8>, Tex> boxCache_;
  std::map<const Tex*, std::vector<uint8_t>> rgbaCache_;
  Tex* boxTexture(Tex* t, float sx, float sy, float sw, float sh, int W, int H);
  void fillTex(Tex& T, const std::string& idOrPath);
  std::unique_ptr<FontProvider> fp_;
  float s_ = 1, ox_ = 0, oy_ = 0, orgX_ = 0, orgY_ = 0;
  int cw_ = 800, ch_ = 600, ow_ = 800, oh_ = 600;
  std::vector<std::pair<float, float>> orgStack_;
  std::vector<SDL_Rect> clipStack_;
  bool bdEnabled_ = true, bdActive_ = false;
  SDL_Texture *bdRT_ = nullptr, *bdA_ = nullptr, *bdB_ = nullptr, *bdC_ = nullptr;
  int bdW_ = 0, bdH_ = 0;
  float bdOx_ = 0, bdOy_ = 0;
  SDL_Texture* bdPrev_ = nullptr;
  void bdFree();
  void gradient(float x0, float y0, float x1, float y1, float a0, float a1);
};

inline uint32_t hslToRgb(int h, int s, int l) {
  double v = l < 128 ? (l * (255 + s)) / 255.0 : (l + s - l * s / 255.0);
  int y = (int)(2 * l - v);
  int div = (6 * h) / 256;
  int x = (int)(y + (v - y) * ((h - (div * 256 / 6)) * 6) / 255);
  if (x > 255) x = 255;
  int z = (int)(v - (v - y) * ((h - (div * 256 / 6)) * 6) / 255);
  if (z < 0) z = 0;
  int r, g, b, V = (int)v;
  switch (div) {
    case 0: r = V; g = x; b = y; break;
    case 1: r = z; g = V; b = y; break;
    case 2: r = y; g = V; b = x; break;
    case 3: r = y; g = z; b = V; break;
    case 4: r = x; g = y; b = V; break;
    default: r = V; g = y; b = z; break;
  }
  auto c = [](int q) { return (uint32_t)(q < 0 ? 0 : q > 255 ? 255 : q); };
  return (c(r) << 16) | (c(g) << 8) | c(b);
}

}
