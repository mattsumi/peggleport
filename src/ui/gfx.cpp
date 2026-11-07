#include "ui/gfx.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>

#include "image.h"

namespace pp::ui {

Gfx::Gfx(SDL_Renderer* r, const Vfs& vfs) : r_(r), vfs_(vfs) {
  haveMan_ = man_.load(vfs_);
  fp_ = makeDefaultFontProvider(r_, this);
}

Gfx::~Gfx() {
  bdFree();
  for (auto& [_, t] : cache_)
    if (t.t) SDL_DestroyTexture(t.t);
  for (auto& [_, t] : boxCache_)
    if (t.t) SDL_DestroyTexture(t.t);
}

void Gfx::begin(int canvasW, int canvasH) {
  cw_ = canvasW;
  ch_ = canvasH;
  if (r_) {
    SDL_GetCurrentRenderOutputSize(r_, &ow_, &oh_);
  } else {
    ow_ = canvasW;
    oh_ = canvasH;
  }
  s_ = std::min(ow_ / (float)cw_, oh_ / (float)ch_);
  ox_ = (ow_ - cw_ * s_) * 0.5f;
  oy_ = (oh_ - ch_ * s_) * 0.5f;
  orgX_ = orgY_ = 0;
  orgStack_.clear();
  clipStack_.clear();
  if (r_) SDL_SetRenderClipRect(r_, nullptr);
}

void Gfx::bdFree() {
  for (SDL_Texture** t : {&bdRT_, &bdA_, &bdB_, &bdC_})
    if (*t) { SDL_DestroyTexture(*t); *t = nullptr; }
  bdW_ = bdH_ = 0;
}

bool Gfx::beginBackdrop() {
  bdActive_ = false;
  if (!r_ || !bdEnabled_ || cw_ != 800 || ch_ != 600) return false;
  const float s = oh_ / 600.0f;
  const int W = (int)std::lround(800 * s), H = oh_;
  if (W + 8 > ow_ || W < 16) return false;
  if (!bdRT_ || bdW_ != W || bdH_ != H) {
    bdFree();
    auto mk = [&](int w, int h) {
      SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, std::max(w, 2), std::max(h, 2));
      if (t) { SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE); }
      return t;
    };
    bdRT_ = mk(W, H);
    bdA_ = mk(W / 4, H / 4);
    bdB_ = mk(W / 20, H / 20);
    bdC_ = mk(W / 8, H / 8);
    bdW_ = W;
    bdH_ = H;
    if (!bdRT_ || !bdA_ || !bdB_ || !bdC_) { bdFree(); return false; }
  }
  bdPrev_ = SDL_GetRenderTarget(r_);
  bdOx_ = std::floor((ow_ - W) * 0.5f);
  bdOy_ = 0;
  s_ = s;
  ox_ = oy_ = 0;
  SDL_SetRenderTarget(r_, bdRT_);
  SDL_SetRenderClipRect(r_, nullptr);
  bdActive_ = true;
  return true;
}

void Gfx::gradient(float x0, float y0, float x1, float y1, float a0, float a1) {
  auto c = [](float a) { return SDL_FColor{0, 0, 0, a}; };
  SDL_Vertex v[4] = {{{x0, y0}, c(a0), {0, 0}}, {{x1, y0}, c(a1), {0, 0}}, {{x1, y1}, c(a1), {0, 0}}, {{x0, y1}, c(a0), {0, 0}}};
  int idx[6] = {0, 1, 2, 0, 2, 3};
  SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

void Gfx::endBackdrop() {
  if (!bdActive_) return;
  bdActive_ = false;
  SDL_SetRenderClipRect(r_, nullptr);
  SDL_SetRenderTarget(r_, bdA_);
  SDL_RenderTexture(r_, bdRT_, nullptr, nullptr);
  SDL_SetRenderTarget(r_, bdB_);
  SDL_RenderTexture(r_, bdA_, nullptr, nullptr);
  SDL_SetRenderTarget(r_, bdC_);
  SDL_RenderTexture(r_, bdB_, nullptr, nullptr);
  SDL_SetRenderTarget(r_, bdA_);
  SDL_RenderTexture(r_, bdC_, nullptr, nullptr);
  SDL_SetRenderTarget(r_, bdPrev_);
  SDL_SetRenderClipRect(r_, nullptr);
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
  SDL_RenderClear(r_);
  const float W = (float)bdW_, H = (float)bdH_;
  SDL_SetTextureColorMod(bdA_, 150, 150, 150);
  for (int k = -3; k <= 3; k++) {
    if (k == 0) continue;
    float x = bdOx_ + k * W;
    if (x + W <= 0 || x >= ow_) continue;
    SDL_FRect d{x, 0, W, H};
    SDL_RenderTextureRotated(r_, bdA_, nullptr, &d, 0, nullptr, (k & 1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
  }
  SDL_SetTextureColorMod(bdA_, 255, 255, 255);
  const float sh = std::max(8.0f, 0.04f * W);
  gradient(bdOx_ - sh, 0, bdOx_, H, 0.0f, 0.55f);
  gradient(bdOx_ + W, 0, bdOx_ + W + sh, H, 0.55f, 0.0f);
  SDL_FRect d{bdOx_, 0, W, H};
  SDL_RenderTexture(r_, bdRT_, nullptr, &d);
  ox_ = bdOx_;
  oy_ = bdOy_;
}

void Gfx::clear(uint32_t rgb) {
  if (!r_) return;
  SDL_SetRenderDrawColor(r_, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
  SDL_RenderClear(r_);
}

void Gfx::pushOrigin(float dx, float dy) {
  orgStack_.push_back({orgX_, orgY_});
  orgX_ += dx;
  orgY_ += dy;
}
void Gfx::popOrigin() {
  if (orgStack_.empty()) return;
  orgX_ = orgStack_.back().first;
  orgY_ = orgStack_.back().second;
  orgStack_.pop_back();
}

void Gfx::applyClip() {
  if (!r_) return;
  if (clipStack_.empty()) SDL_SetRenderClipRect(r_, nullptr);
  else SDL_SetRenderClipRect(r_, &clipStack_.back());
}
void Gfx::pushClip(float x, float y, float w, float h) {
  int x0 = (int)std::floor(ox_ + (orgX_ + x) * s_), y0 = (int)std::floor(oy_ + (orgY_ + y) * s_);
  int x1 = (int)std::ceil(ox_ + (orgX_ + x + w) * s_), y1 = (int)std::ceil(oy_ + (orgY_ + y + h) * s_);
  if (!clipStack_.empty()) {
    const SDL_Rect& c = clipStack_.back();
    x0 = std::max(x0, c.x);
    y0 = std::max(y0, c.y);
    x1 = std::min(x1, c.x + c.w);
    y1 = std::min(y1, c.y + c.h);
  }
  SDL_Rect rc{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
  clipStack_.push_back(rc);
  applyClip();
}
void Gfx::popClip() {
  if (clipStack_.empty()) return;
  clipStack_.pop_back();
  applyClip();
}

Tex* Gfx::img(const std::string& idOrPath) {
  auto it = cache_.find(idOrPath);
  if (it != cache_.end()) return it->second.valid() ? &it->second : nullptr;
  Tex T;
  std::string path = idOrPath;
  if (const ResImage* ri = man_.image(idOrPath)) {
    path = ri->path;
    T.cols = ri->cols;
    T.rows = ri->rows;
  }
  if (auto im = loadGameImage(vfs_, path)) {
    T.w = im->w;
    T.h = im->h;
    T.scale = im->scale;
    T.path = path;
    if (r_) {
      T.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
      if (T.t) {
        SDL_UpdateTexture(T.t, nullptr, im->rgba.data(), im->w * 4);
        SDL_SetTextureBlendMode(T.t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(T.t, SDL_SCALEMODE_LINEAR);
      } else {
        T.w = 0;
      }
    }
  }
  auto& slot = cache_[idOrPath] = T;
  if (!slot.valid()) vfsLogMiss("image", idOrPath == path ? idOrPath : idOrPath + " (" + path + ")");
  return slot.valid() ? &slot : nullptr;
}

Tex* Gfx::imgFile(const std::string& file) {
  const std::string key = "file:" + file;
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second.valid() ? &it->second : nullptr;
  Tex T;
  std::ifstream f(file, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  Image im;
  if (!bytes.empty() && bytes.size() <= (4u << 20) && decodeImage(bytes, im) && im.w > 0 && im.h > 0 && im.w <= 4096 && im.h <= 4096) {
    T.w = im.w;
    T.h = im.h;
    T.path = file;
    if (r_) {
      T.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im.w, im.h);
      if (T.t) {
        SDL_UpdateTexture(T.t, nullptr, im.rgba.data(), im.w * 4);
        SDL_SetTextureBlendMode(T.t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(T.t, SDL_SCALEMODE_LINEAR);
      } else {
        T.w = 0;
      }
    }
  }
  auto& slot = cache_[key] = T;
  return slot.valid() ? &slot : nullptr;
}

SDL_FRect Gfx::dest(float x, float y, float w, float h, bool snap) const {
  float x0 = ox_ + (orgX_ + x) * s_, y0 = oy_ + (orgY_ + y) * s_, x1 = ox_ + (orgX_ + x + w) * s_, y1 = oy_ + (orgY_ + y + h) * s_;
  if (!snap) return {x0, y0, x1 - x0, y1 - y0};
  float rx0 = std::round(x0), ry0 = std::round(y0), rx1 = std::round(x1), ry1 = std::round(y1);
  if (rx1 - rx0 < 1 && w > 0) rx1 = rx0 + 1;
  if (ry1 - ry0 < 1 && h > 0) ry1 = ry0 + 1;
  return {rx0, ry0, rx1 - rx0, ry1 - ry0};
}

void Gfx::blit(Tex* t, SDL_FRect src, float x, float y, float w, float h, Mode m, float rotDeg, bool flipX) {
  if (!r_ || !t || !t->t) return;
  SDL_FRect d = dest(x, y, w, h, rotDeg == 0);
  SDL_SetTextureAlphaMod(t->t, (Uint8)std::clamp((int)std::lround(m.alpha * 255.0f), 0, 255));
  SDL_SetTextureColorMod(t->t, (m.tint >> 16) & 255, (m.tint >> 8) & 255, m.tint & 255);
  SDL_SetTextureBlendMode(t->t, m.add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
  if (rotDeg == 0 && !flipX) SDL_RenderTexture(r_, t->t, &src, &d);
  else SDL_RenderTextureRotated(r_, t->t, &src, &d, rotDeg, nullptr, flipX ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void Gfx::draw(Tex* t, float x, float y, Mode m) {
  if (!t) return;
  blit(t, {0, 0, (float)t->w, (float)t->h}, x, y, t->lw(), t->lh(), m);
}
void Gfx::drawCel(Tex* t, float x, float y, int col, int row, Mode m) {
  if (!t || col < 0 || row < 0 || col >= t->cols || row >= t->rows) return;
  float cw = (float)t->w / t->cols, ch = (float)t->h / t->rows;
  blit(t, {cw * col, ch * row, cw, ch}, x, y, t->cw(), t->ch(), m);
}
void Gfx::drawCelMirror(Tex* t, float x, float y, int col, int row, Mode m) {
  if (!t || col < 0 || row < 0 || col >= t->cols || row >= t->rows) return;
  float cw = (float)t->w / t->cols, ch = (float)t->h / t->rows;
  blit(t, {cw * col, ch * row, cw, ch}, x, y, t->cw(), t->ch(), m, 0, true);
}
void Gfx::drawSrc(Tex* t, float sx, float sy, float sw, float sh, float x, float y, Mode m) {
  if (!t) return;
  float k = t->scale;
  blit(t, {sx * k, sy * k, sw * k, sh * k}, x, y, sw, sh, m);
}
void Gfx::drawStretch(Tex* t, float x, float y, float w, float h, Mode m) {
  if (!t) return;
  blit(t, {0, 0, (float)t->w, (float)t->h}, x, y, w, h, m);
}
void Gfx::drawStretchSrc(Tex* t, float sx, float sy, float sw, float sh, float x, float y, float w, float h, Mode m) {
  if (!t) return;
  float k = t->scale;
  blit(t, {sx * k, sy * k, sw * k, sh * k}, x, y, w, h, m);
}
void Gfx::drawRot(Tex* t, float cx, float cy, float rad, float sx, float sy, Mode m) {
  if (!t) return;
  float w = t->lw() * sx, h = t->lh() * sy;
  blit(t, {0, 0, (float)t->w, (float)t->h}, cx - w / 2, cy - h / 2, w, h, m, -rad * 57.29577951f);
}
void Gfx::drawRotCel(Tex* t, float cx, float cy, int col, int row, float rad, float sx, float sy, Mode m) {
  if (!t || col < 0 || row < 0 || col >= t->cols || row >= t->rows) return;
  float cw = (float)t->w / t->cols, ch = (float)t->h / t->rows;
  float w = t->cw() * sx, h = t->ch() * sy;
  blit(t, {cw * col, ch * row, cw, ch}, cx - w / 2, cy - h / 2, w, h, m, -rad * 57.29577951f);
}

void Gfx::drawBox(Tex* t, float x, float y, float w, float h, Mode m) {
  if (t) drawBoxSrc(t, 0, 0, t->lw(), t->lh(), x, y, w, h, m);
}
Tex* Gfx::boxTexture(Tex* t, float sx, float sy, float sw, float sh, int W, int H) {
  if (!r_ || !t || t->path.empty()) return nullptr;
  std::array<int, 8> key{(int)(uint32_t)(uintptr_t)t, (int)(uint32_t)((uint64_t)(uintptr_t)t >> 32), (int)std::lround(sx * 4) * 4096 + (int)std::lround(sy * 4), (int)std::lround(sw * 4) * 4096 + (int)std::lround(sh * 4), W, H, 0, 0};
  auto it = boxCache_.find(key);
  if (it != boxCache_.end()) return it->second.valid() ? &it->second : nullptr;
  auto rit = rgbaCache_.find(t);
  if (rit == rgbaCache_.end()) {
    auto im = loadGameImage(vfs_, t->path);
    if (!im || im->w != t->w || im->h != t->h) return nullptr;
    rit = rgbaCache_.emplace(t, std::vector<uint8_t>(im->rgba.begin(), im->rgba.end())).first;
  }
  const uint8_t* src = rit->second.data();
  float k = t->scale;
  int spx = (int)std::lround(sx * k), spy = (int)std::lround(sy * k), swp = (int)std::lround(sw * k), shp = (int)std::lround(sh * k);
  int cwp = (int)std::lround((int)(sw / 3) * k), chp = (int)std::lround((int)(sh / 3) * k);
  int cmw = swp - cwp * 2, cmh = shp - chp * 2;
  int Wp = (int)std::lround(W * k), Hp = (int)std::lround(H * k);
  if (cmw <= 0 || cmh <= 0 || Wp <= 0 || Hp <= 0 || spx < 0 || spy < 0 || spx + swp > t->w || spy + shp > t->h) return nullptr;
  std::vector<uint8_t> out((size_t)Wp * Hp * 4);
  std::vector<int> mapX(Wp), mapY(Hp);
  for (int x = 0; x < Wp; x++) mapX[x] = spx + (x >= Wp - cwp ? swp - (Wp - x) : x < cwp ? x : cwp + (x - cwp) % cmw);
  for (int y = 0; y < Hp; y++) mapY[y] = spy + (y >= Hp - chp ? shp - (Hp - y) : y < chp ? y : chp + (y - chp) % cmh);
  for (int y = 0; y < Hp; y++) {
    const uint8_t* row = src + (size_t)mapY[y] * t->w * 4;
    uint8_t* o = out.data() + (size_t)y * Wp * 4;
    for (int x = 0; x < Wp; x++) std::memcpy(o + x * 4, row + (size_t)mapX[x] * 4, 4);
  }
  if (boxCache_.size() > 40) {
    for (auto& [_, e] : boxCache_)
      if (e.t) SDL_DestroyTexture(e.t);
    boxCache_.clear();
  }
  Tex B;
  B.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, Wp, Hp);
  if (B.t) {
    B.w = Wp;
    B.h = Hp;
    B.scale = k;
    SDL_UpdateTexture(B.t, nullptr, out.data(), Wp * 4);
    SDL_SetTextureBlendMode(B.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(B.t, SDL_SCALEMODE_LINEAR);
  }
  auto& slot = boxCache_[key] = B;
  return slot.valid() ? &slot : nullptr;
}

void Gfx::drawBoxSrc(Tex* t, float sx, float sy, float sw, float sh, float x, float y, float w, float h, Mode m) {
  if (!t || sw <= 0 || sh <= 0) return;
  int cw = (int)(sw / 3), ch = (int)(sh / 3);
  int cmw = (int)sw - cw * 2, cmh = (int)sh - ch * 2;
  if (cmw <= 0 || cmh <= 0) return;
  int W = (int)std::lround(w), H = (int)std::lround(h);
  if (W > 0 && H > 0) {
    if (Tex* B = boxTexture(t, sx, sy, sw, sh, W, H)) {
      blit(B, {0, 0, (float)B->w, (float)B->h}, x, y, w, h, m);
      return;
    }
  }
  drawSrc(t, sx, sy, cw, ch, x, y, m);
  drawSrc(t, sx + cw + cmw, sy, cw, ch, x + w - cw, y, m);
  drawSrc(t, sx, sy + ch + cmh, cw, ch, x, y + h - ch, m);
  drawSrc(t, sx + cw + cmw, sy + ch + cmh, cw, ch, x + w - cw, y + h - ch, m);
  pushClip(x + cw, y, w - cw * 2, h);
  for (int c = 0; c < (int)((w - cw * 2 + cmw - 1) / cmw); c++) {
    drawSrc(t, sx + cw, sy, cmw, ch, x + cw + c * cmw, y, m);
    drawSrc(t, sx + cw, sy + ch + cmh, cmw, ch, x + cw + c * cmw, y + h - ch, m);
  }
  popClip();
  pushClip(x, y + ch, w, h - ch * 2);
  for (int rr = 0; rr < (int)((h - ch * 2 + cmh - 1) / cmh); rr++) {
    drawSrc(t, sx, sy + ch, cw, cmh, x, y + ch + rr * cmh, m);
    drawSrc(t, sx + cw + cmw, sy + ch, cw, cmh, x + w - cw, y + ch + rr * cmh, m);
  }
  popClip();
  pushClip(x + cw, y + ch, w - cw * 2, h - ch * 2);
  for (int c = 0; c < (int)((w - cw * 2 + cmw - 1) / cmw); c++)
    for (int rr = 0; rr < (int)((h - ch * 2 + cmh - 1) / cmh); rr++) drawSrc(t, sx + cw, sy + ch, cmw, cmh, x + cw + c * cmw, y + ch + rr * cmh, m);
  popClip();
}

void Gfx::fill(float x, float y, float w, float h, uint32_t rgb, float alpha, bool add) {
  if (!r_) return;
  SDL_SetRenderDrawBlendMode(r_, add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r_, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (Uint8)std::clamp((int)std::lround(alpha * 255), 0, 255));
  SDL_FRect d = dest(x, y, w, h);
  SDL_RenderFillRect(r_, &d);
  SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
}
void Gfx::frame(float x, float y, float w, float h, uint32_t rgb, float alpha) {
  fill(x, y, w, 1, rgb, alpha);
  fill(x, y + h - 1, w, 1, rgb, alpha);
  fill(x, y + 1, 1, h - 2, rgb, alpha);
  fill(x + w - 1, y + 1, 1, h - 2, rgb, alpha);
}
void Gfx::line(float x0, float y0, float x1, float y1, uint32_t rgb, float alpha) {
  if (!r_) return;
  SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r_, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (Uint8)(alpha * 255));
  SDL_RenderLine(r_, ox_ + (orgX_ + x0) * s_, oy_ + (orgY_ + y0) * s_, ox_ + (orgX_ + x1) * s_, oy_ + (orgY_ + y1) * s_);
  SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
}


float Gfx::textWidth(const std::string& font, const std::string& s) {
  float best = 0;
  std::string plain = stripColorCodes(s);
  size_t p = 0;
  while (true) {
    size_t e = plain.find('\n', p);
    best = std::max(best, fp_->width(font, plain.substr(p, e == std::string::npos ? std::string::npos : e - p)));
    if (e == std::string::npos) break;
    p = e + 1;
  }
  return best;
}

void Gfx::drawString(const std::string& font, float x, float y, const std::string& s, uint32_t rgb, float alpha, Align a) {
  if (!r_) return;
  auto runs = parseColorCodes(s);
  float total = 0;
  for (auto& r : runs) total += fp_->width(font, r.text);
  float cx = x;
  if (a == Align::Center) cx = x - total / 2;
  else if (a == Align::Right) cx = x - total;
  for (auto& r : runs) {
    uint32_t c = r.rgb < 0 ? rgb : (uint32_t)r.rgb;
    fp_->draw(font, orgX_ + cx, orgY_ + y, r.text, c, alpha);
    cx += fp_->width(font, r.text);
  }
}

std::vector<std::string> Gfx::wrap(const std::string& font, float width, const std::string& s) {
  std::vector<std::string> lines;
  std::string cur, carry;
  float curW = 0;
  size_t i = 0;
  auto flush = [&] {
    lines.push_back(cur);
    cur = carry;
    curW = 0;
  };
  std::string word, active;
  auto wordWidth = [&](const std::string& w) { return fp_->width(font, stripColorCodes(w)); };
  auto trackColor = [&](const std::string& w) {
    for (size_t k = 0; k + 7 < w.size() + 1; k++) {
      if (w[k] == '^') {
        if (k + 7 < w.size() + 0 && w[k + 7] == '^' && std::isxdigit((unsigned char)w[k + 1])) { active = w.substr(k, 8); carry = active; k += 7; }
        else if (w.compare(k, 8, "^oldclr^") == 0) { active.clear(); carry.clear(); k += 7; }
        else if (k + 1 < w.size() && w[k + 1] == '^') k++;
      }
    }
  };
  auto addWord = [&](const std::string& w) {
    float ww = wordWidth(w), sp = fp_->width(font, " ");
    if (curW > 0 && curW + sp + ww > width) {
      flush();
    }
    if (curW > 0 || (!cur.empty() && stripColorCodes(cur).size() > 0)) { cur += " "; curW += sp; }
    cur += w;
    curW += ww;
    trackColor(w);
  };
  while (i <= s.size()) {
    if (i == s.size() || s[i] == ' ' || s[i] == '\n') {
      if (!word.empty()) addWord(word);
      word.clear();
      if (i < s.size() && s[i] == '\n') flush();
    } else {
      word.push_back(s[i]);
    }
    i++;
  }
  lines.push_back(cur);
  return lines;
}

float Gfx::wrappedHeight(const std::string& font, float width, const std::string& s, float lineSpacing) {
  float ls = lineSpacing < 0 ? fp_->lineSpacing(font) : lineSpacing;
  return ls * wrap(font, width, s).size();
}

float Gfx::drawWrapped(const std::string& font, Rect box, const std::string& s, uint32_t rgb, float alpha, Align a, float lineSpacing) {
  float ls = lineSpacing < 0 ? fp_->lineSpacing(font) : lineSpacing;
  auto lines = wrap(font, box.w, s);
  float y = box.y + fp_->ascent(font);
  for (auto& l : lines) {
    float x = a == Align::Left ? box.x : a == Align::Center ? box.x + box.w / 2 : box.x + box.w;
    drawString(font, x, y, l, rgb, alpha, a);
    y += ls;
  }
  return ls * lines.size();
}

}
