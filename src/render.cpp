#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "gamepaths.h"
#include "image.h"

namespace pp {

namespace {
constexpr float RAD2DEG = 57.29577951f;

V2 rot(V2 p, float a) {
  float c = std::cos(a), s = std::sin(a);
  return {p.x * c + p.y * s, p.y * c - p.x * s};
}

std::vector<V2> resample(const std::vector<V2>& v, int n) {
  std::vector<V2> out;
  if (v.empty()) return out;
  for (int i = 0; i <= n; i++) {
    float t = (float)i / n * (v.size() - 1);
    int k = std::min((int)t, (int)v.size() - 2);
    if (v.size() == 1) { out.push_back(v[0]); continue; }
    float f = t - k;
    out.push_back(v[k] + (v[k + 1] - v[k]) * f);
  }
  return out;
}
}

static void downscale2(Image& im) {
  const int W = im.w / 2, H = im.h / 2;
  std::vector<uint8_t> out((size_t)W * H * 4);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float a = 0, c[3] = {0, 0, 0};
      for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
          const uint8_t* p = &im.rgba[((size_t)(2 * y + dy) * im.w + 2 * x + dx) * 4];
          a += p[3];
          for (int k = 0; k < 3; k++) c[k] += p[k] * (float)p[3] / 255.0f;
        }
      uint8_t* o = &out[((size_t)y * W + x) * 4];
      for (int k = 0; k < 3; k++) o[k] = a > 0.5f ? (uint8_t)std::lround(std::clamp(c[k] * 255.0f / a, 0.0f, 255.0f)) : 0;
      o[3] = (uint8_t)std::lround(a / 4.0f);
    }
  im.rgba = std::move(out);
  im.w = W;
  im.h = H;
}

static void blurImage(std::vector<uint8_t>& pixels, int w, int h, int radius) {
  std::vector<uint8_t> tmp(pixels.size());
  auto box = [&](const auto& src, auto& dst, int span, int lines, int along, int across) {
    for (int line = 0; line < lines; ++line) {
      const size_t start = (size_t)line * across * 4;
      for (int c = 0; c < 4; ++c) {
        auto at = [&](int pos) { return src[start + (size_t)std::clamp(pos, 0, span - 1) * along * 4 + c]; };
        int sum = 0;
        for (int i = -radius; i <= radius; ++i) sum += at(i);
        for (int i = 0; i < span; ++i) {
          dst[start + (size_t)i * along * 4 + c] = (uint8_t)(sum / (2 * radius + 1));
          sum += at(i + radius + 1) - at(i - radius);
        }
      }
    }
  };
  for (int i = 0; i < 3; ++i) {
    box(pixels, tmp, w, h, 1, w);
    box(tmp, pixels, h, w, w, 1);
  }
}

Renderer::~Renderer() {
  if (bloomA_) SDL_DestroyTexture(bloomA_);
  if (bloomB_) SDL_DestroyTexture(bloomB_);
  for (auto* target : pauseTargets_) if (target) SDL_DestroyTexture(target);
  for (auto& [_, t] : cache_)
    if (t.t) SDL_DestroyTexture(t.t);
  for (auto& [_, t] : modCache_)
    if (t.t) SDL_DestroyTexture(t.t);
  for (auto& [_, t] : fontTex_)
    if (t) SDL_DestroyTexture(t);
}

void Renderer::reloadAssets(const VfsChange& change) {
  if (!change.any) return;
  for (auto& [_, t] : cache_)
    if (t.t) SDL_DestroyTexture(t.t);
  cache_.clear();
  for (auto& [_, t] : modCache_)
    if (t.t) SDL_DestroyTexture(t.t);
  for (auto& [_, t] : fontTex_)
    if (t) SDL_DestroyTexture(t);
  modCache_.clear();
  fontTex_.clear();
  fonts_.reset();
  man_ = ImageManifest(vfs_);
  chars_.clear();
  charsLoaded_ = false;
  if (portrait_.tex) SDL_DestroyTexture(portrait_.tex);
  portrait_ = Portrait();
}

SDL_Texture* Renderer::fontTexture(const Image* img) {
  auto it = fontTex_.find(img);
  if (it != fontTex_.end()) return it->second;
  SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img->w, img->h);
  if (t) {
    SDL_UpdateTexture(t, nullptr, img->rgba.data(), img->w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  }
  fontTex_[img] = t;
  return t;
}

void Renderer::blitGlyphs(const std::vector<GlyphQuad>& quads, float x, float y, float scale) {
  for (auto& q : quads) {
    SDL_Texture* t = fontTexture(q.img);
    if (!t) continue;
    SDL_FRect src{(float)q.sx, (float)q.sy, (float)q.sw, (float)q.sh};
    SDL_FRect dst{px(x + q.dx * scale), py(y + q.dy * scale), (q.dw ? q.dw : q.sw) * scale * s_, (q.dh ? q.dh : q.sh) * scale * s_};
    SDL_SetTextureColorMod(t, (q.rgba >> 24) & 255, (q.rgba >> 16) & 255, (q.rgba >> 8) & 255);
    SDL_SetTextureAlphaMod(t, q.rgba & 255);
    SDL_SetTextureBlendMode(t, q.mode == 1 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(r_, t, &src, &dst);
  }
}

void Renderer::drawText(Font* f, float x, float y, std::string_view s, uint32_t rgba, TextAlign al, float scale, bool colorCodes) {
  if (!f || !f->valid() || s.empty()) return;
  std::vector<GlyphQuad> q;
  int w = 0;
  if (colorCodes) f->layoutColored(s, rgba, q, &w);
  else f->layout(s, rgba, q, &w);
  if (al == TextAlign::Center) x -= (w / 2) * scale;
  else if (al == TextAlign::Right) x -= w * scale;
  blitGlyphs(q, x, y, scale);
}

int Renderer::wrappedHeight(Font* f, int wrapWidth, std::string_view s, int lineSpacing) const {
  if (!f || !f->valid()) return 0;
  std::vector<Font::Line> lines;
  return f->wrap(s, wrapWidth, lineSpacing, lines);
}

int Renderer::drawTextWrapped(Font* f, float x, float y, int wrapWidth, std::string_view s, uint32_t rgba, TextAlign al, int lineSpacing, float scale) {
  if (!f || !f->valid()) return 0;
  std::vector<Font::Line> lines;
  int h = f->wrap(s, wrapWidth, lineSpacing, lines);
  uint32_t cur = rgba;
  std::vector<GlyphQuad> q;
  for (auto& ln : lines) {
    std::string_view part = s.substr(ln.start, ln.len);
    int w = 0;
    uint32_t before = cur;
    f->layoutColored(part, rgba, q, &w, &cur);
    float ox = 0;
    if (al == TextAlign::Center) ox = (float)((wrapWidth - f->widthColored(part)) / 2);
    else if (al == TextAlign::Right) ox = (float)(wrapWidth - f->widthColored(part));
    (void)before;
    blitGlyphs(q, x + ox * scale, y + ln.y * scale, scale);
  }
  return h;
}

void Renderer::beginCanvas(int canvasW) {
  int ow = 0, oh = 0;
  SDL_GetCurrentRenderOutputSize(r_, &ow, &oh);
  canvasW_ = canvasW;
  s_ = oh / 600.0f;
  if (canvasW_ * s_ > ow) s_ = ow / (float)canvasW_;
  ox_ = (ow - canvasW_ * s_) * 0.5f;
  oy_ = (oh - 600 * s_) * 0.5f;
}

void Renderer::drawPauseBackdrop(float amount, const SDL_Rect& area, const std::function<void()>& drawScene) {
  const int opacity = (int)std::lround(std::clamp(amount, 0.0f, 1.0f) * 255);
  int ow = 0, oh = 0;
  if (!opacity || area.w < 16 || area.h < 16 || !SDL_GetCurrentRenderOutputSize(r_, &ow, &oh)) {
    drawScene();
    return;
  }
  const int passes = area.h >= 840 ? 3 : 2;
  for (int i = 0; i <= passes; ++i) {
    const int w = i ? area.w >> i : ow, h = i ? area.h >> i : oh;
    auto*& t = pauseTargets_[i];
    if (t && (t->w != w || t->h != h)) {
      SDL_DestroyTexture(t);
      t = nullptr;
    }
    if (!t) {
      t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
      if (!t) { drawScene(); return; }
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    }
  }

  auto* previous = SDL_GetRenderTarget(r_);
  if (!SDL_SetRenderTarget(r_, pauseTargets_[0])) { drawScene(); return; }
  SDL_SetRenderClipRect(r_, nullptr);
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
  SDL_RenderClear(r_);
  drawScene();

  const SDL_FRect bounds{(float)area.x, (float)area.y, (float)area.w, (float)area.h};
  bool softened = true;
  for (int i = 1; i <= passes && softened; ++i) {
    softened = SDL_SetRenderTarget(r_, pauseTargets_[i]) &&
        SDL_RenderTexture(r_, pauseTargets_[i - 1], i == 1 ? &bounds : nullptr, nullptr);
  }
  SDL_SetRenderTarget(r_, previous);
  SDL_RenderTexture(r_, pauseTargets_[0], nullptr, nullptr);
  if (softened) {
    auto* blurred = pauseTargets_[passes];
    SDL_SetTextureBlendMode(blurred, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(blurred, (Uint8)opacity);
    SDL_RenderTexture(r_, blurred, nullptr, &bounds);
    SDL_SetTextureAlphaMod(blurred, 255);
    SDL_SetTextureBlendMode(blurred, SDL_BLENDMODE_NONE);
  }
}

Renderer::Tex* Renderer::tex(const std::string& path, int rows, int cols, const std::string& mask, int alphaColor) { return tex(path, rows, cols, mask, alphaColor, false); }

Renderer::Tex* Renderer::tex(const std::string& path, int rows, int cols, const std::string& mask, int alphaColor, bool limited) {
  std::string key = path + "|" + std::to_string(rows) + "x" + std::to_string(cols) + "|" + mask + "|" + std::to_string(alphaColor) + "|q" + std::to_string(texQ_);
  auto& cache = limited ? modCache_ : cache_;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second.t ? &it->second : nullptr;
  Tex T;
  if (auto im = loadGameImage(vfs_, path, mask, limited)) {
    if (alphaColor >= 0)
      for (size_t i = 0; i + 3 < im->rgba.size(); i += 4) {
        im->rgba[i] = (alphaColor >> 16) & 255;
        im->rgba[i + 1] = (alphaColor >> 8) & 255;
        im->rgba[i + 2] = alphaColor & 255;
      }
    if (path.starts_with("console/gutters/")) {
      bool opaque = true;
      for (size_t i = 3; i < im->rgba.size() && opaque; i += 4) opaque = im->rgba[i] == 255;
      if (opaque) blurImage(im->rgba, im->w, im->h, std::max(1, (int)std::lround(im->h / 180.0f)));
    } else if (texQ_ == 1 && im->scale < 1.5f && im->w <= 2048 && im->h <= 2048) {
      upscale2(*im, rows, cols);
      im->scale *= 2;
    } else if (texQ_ == -1 && im->w % 2 == 0 && im->h % 2 == 0 && im->w >= 16 && im->h >= 16 && (cols <= 1 || (im->w / 2) % cols == 0) && (rows <= 1 || (im->h / 2) % rows == 0)) {
      downscale2(*im);
      im->scale *= 0.5f;
    }
    T.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
    if (T.t) {
      SDL_UpdateTexture(T.t, nullptr, im->rgba.data(), im->w * 4);
      SDL_SetTextureBlendMode(T.t, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(T.t, SDL_SCALEMODE_LINEAR);
      T.w = im->w;
      T.h = im->h;
      T.scale = im->scale;
      T.rows = std::max(1, rows);
      T.cols = std::max(1, cols);
    }
  }
  if (limited) {
    if (!T.t) return nullptr;
    size_t bytes = (size_t)T.w * T.h * 4;
    for (const auto& [_, t] : cache) bytes += (size_t)t.w * t.h * 4;
    while (!cache.empty() && (cache.size() >= 256 || bytes > 67108864)) {
      auto old = cache.begin();
      bytes -= (size_t)old->second.w * old->second.h * 4;
      SDL_DestroyTexture(old->second.t);
      cache.erase(old);
    }
  }
  auto& slot = cache[key] = T;
  return slot.t ? &slot : nullptr;
}

Renderer::Tex* Renderer::wideBackground(const std::string& path, int targetW, float* topY) {
  const std::string key = "wide|" + path + "|" + std::to_string(targetW) + "|q" + std::to_string(texQ_);
  if (auto it = cache_.find(key); it != cache_.end()) {
    if (topY) *topY = (float)it->second.rows - 1000.0f;
    return it->second.t ? &it->second : nullptr;
  }
  Tex T;
  if (auto im = loadGameImage(vfs_, path)) {
    if (texQ_ == 1 && im->scale < 1.5f) { upscale2(*im, 1, 1); im->scale *= 2; }
    else if (texQ_ == -1 && im->w % 2 == 0 && im->h % 2 == 0) { downscale2(*im); im->scale *= 0.5f; }
    int cropY = 0, cropX = 0;
    const int fullW = im->w;
    if (im->w >= 700 * im->scale) {
      std::vector<int> cc((size_t)im->w, 0), rc((size_t)im->h, 0);
      for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
          const uint8_t* p = &im->rgba[((size_t)y * im->w + x) * 4];
          if (p[3] > 16 && std::max({p[0], p[1], p[2]}) > 12) { cc[(size_t)x]++; rc[(size_t)y]++; }
        }
      int x0 = 0, x1 = im->w - 1, y0 = 0, y1 = im->h - 1;
      while (x0 < x1 && cc[(size_t)x0] < im->h * 35 / 100) x0++;
      while (x1 > x0 && cc[(size_t)x1] < im->h * 35 / 100) x1--;
      while (y0 < y1 && rc[(size_t)y0] < im->w * 35 / 100) y0++;
      while (y1 > y0 && rc[(size_t)y1] < im->w * 35 / 100) y1--;
      if (x1 > x0 && y1 > y0) {
        x1++; y1++;
        std::vector<uint8_t> c((size_t)(x1 - x0) * (y1 - y0) * 4);
        for (int y = y0; y < y1; y++) std::memcpy(&c[(size_t)(y - y0) * (x1 - x0) * 4], &im->rgba[((size_t)y * im->w + x0) * 4], (size_t)(x1 - x0) * 4);
        im->rgba = std::move(c);
        im->w = x1 - x0;
        im->h = y1 - y0;
        cropY = y0;
        cropX = x0;
      }
      {
        const int ym0 = im->h / 6, ym1 = im->h * 2 / 3;
        auto colMean = [&](int x) {
          long sum = 0;
          for (int y = ym0; y < ym1; y++)
            for (int c = 0; c < 3; c++) sum += im->rgba[((size_t)y * im->w + x) * 4 + c];
          return (float)sum / (float)((ym1 - ym0) * 3);
        };
        const int most = std::max(1, (int)std::lround(3 * im->scale));
        int tl = 0, tr = 0;
        while (tl < most && std::fabs(colMean(tl) - colMean(tl + 1)) > 3.0f) tl++;
        while (tr < most && std::fabs(colMean(im->w - 1 - tr) - colMean(im->w - 2 - tr)) > 3.0f) tr++;
        if (tl + tr > 0) {
          const int nw = im->w - tl - tr;
          std::vector<uint8_t> c((size_t)nw * im->h * 4);
          for (int y = 0; y < im->h; y++) std::memcpy(&c[(size_t)y * nw * 4], &im->rgba[((size_t)y * im->w + tl) * 4], (size_t)nw * 4);
          im->rgba = std::move(c);
          im->w = nw;
          cropX += tl;
        }
      }
    }
    const int w = im->w, h = im->h;
    const float sc = im->scale;
    const int tw = std::max(1, (int)std::lround(targetW * sc));
    const int off = (tw - fullW) / 2 + cropX;
    std::vector<uint8_t> out((size_t)tw * h * 4);
    std::vector<float> dist((size_t)tw, 0.0f);
    int gap = 0;
    for (int x = 0; x < tw; x++) {
      const int u = x - off;
      int sx = u, d = 0;
      if (u < 0 || u >= w) {
        d = u < 0 ? -u : u - w + 1;
        sx = std::clamp(u, 0, w - 1);
        gap = std::max(gap, d);
      }
      dist[(size_t)x] = (float)d;
      for (int y = 0; y < h; y++) std::memcpy(&out[((size_t)y * tw + x) * 4], &im->rgba[((size_t)y * w + sx) * 4], 4);
    }
    if (gap > 0) {
      std::vector<uint8_t> bl(out.size()), tmp(out.size());
      const float sampleScale = (float)w / (float)tw;
      for (int y = 0; y < h; y++) {
        const int sy = std::clamp((int)((y + 0.5f - h * 0.5f) * sampleScale + h * 0.5f), 0, h - 1);
        for (int x = 0; x < tw; x++) {
          const int sx = std::min(w - 1, (int)((x + 0.5f) * sampleScale));
          std::memcpy(&bl[((size_t)y * tw + x) * 4], &im->rgba[((size_t)sy * w + sx) * 4], 4);
        }
      }
      blurImage(bl, tw, h, std::max(2, (int)std::lround(18 * sc)));
      for (int x = 0; x < tw; x++) {
        if (dist[(size_t)x] <= 0) continue;
        float t = std::min(1.0f, dist[(size_t)x] / (24.0f * sc));
        t = t * t * (3 - 2 * t);
        float dark = 1.0f - 0.18f * t;
        for (int y = 0; y < h; y++) {
          uint8_t* o = &out[((size_t)y * tw + x) * 4];
          const uint8_t* b = &bl[((size_t)y * tw + x) * 4];
          for (int c = 0; c < 3; c++) o[c] = (uint8_t)std::lround((o[c] * (1 - t) + b[c] * t) * dark);
        }
      }
    }
    T.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, tw, h);
    if (T.t) {
      SDL_UpdateTexture(T.t, nullptr, out.data(), tw * 4);
      SDL_SetTextureBlendMode(T.t, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(T.t, SDL_SCALEMODE_LINEAR);
      T.w = tw;
      T.h = h;
      T.scale = sc;
      T.rows = 1000 + (int)std::lround(cropY / sc);
    }
  }
  auto& slot = cache_[key] = T;
  if (topY) *topY = (float)slot.rows - 1000.0f;
  return slot.t ? &slot : nullptr;
}

Renderer::Tex* Renderer::img(const std::string& id) {
  const ImageRes* r = man_.find(id);
  if (!r) return nullptr;
  Tex* t = tex(r->path, r->rows, r->cols, r->alphaImage, r->alphaColor ? (int)(r->alphaColorRGB & 0xffffff) : -1);
  if (t) t->frameDelay = r->animOnce ? std::max(1, r->frameDelay) : 0;
  return t;
}

Renderer::Tex* Renderer::grayImg(const std::string& id) {
  const std::string key = "gray|" + id;
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second.t ? &it->second : nullptr;
  Tex T;
  if (const ImageRes* r = man_.find(id)) {
    if (auto im = loadGameImage(vfs_, r->path, r->alphaImage)) {
      for (size_t i = 0; i + 3 < im->rgba.size(); i += 4) {
        const int lum = (im->rgba[i] * 299 + im->rgba[i + 1] * 587 + im->rgba[i + 2] * 114) / 1000;
        const uint8_t v = (uint8_t)std::min(255, lum * 5 / 2);
        im->rgba[i] = im->rgba[i + 1] = im->rgba[i + 2] = v;
      }
      T.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
      if (T.t) {
        SDL_UpdateTexture(T.t, nullptr, im->rgba.data(), im->w * 4);
        SDL_SetTextureBlendMode(T.t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(T.t, SDL_SCALEMODE_LINEAR);
        T.w = im->w;
        T.h = im->h;
        T.scale = im->scale;
        T.rows = std::max(1, r->rows);
        T.cols = std::max(1, r->cols);
      }
    }
  }
  auto& slot = cache_[key] = T;
  return slot.t ? &slot : nullptr;
}

Renderer::Tex* Renderer::objImage(const std::string& name) {
  if (name.empty()) return nullptr;
  for (const char* pre : {"", "images/levels/", "images/game/", "levels/"})
    if (auto t = tex(pre + name)) return t;
  return nullptr;
}

Renderer::Tex* Renderer::fileTex(const std::string& file) {
  const std::string key = "file:" + file;
  if (auto it = cache_.find(key); it != cache_.end()) return it->second.t ? &it->second : nullptr;
  Tex T;
  std::ifstream in(pathFromUtf8(file), std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Image im;
  if (!bytes.empty() && decodeImage(bytes, im, nullptr, true) && (T.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im.w, im.h))) {
    SDL_UpdateTexture(T.t, nullptr, im.rgba.data(), im.w * 4);
    SDL_SetTextureBlendMode(T.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(T.t, SDL_SCALEMODE_LINEAR);
    T.w = im.w;
    T.h = im.h;
  }
  auto& slot = cache_[key] = T;
  return slot.t ? &slot : nullptr;
}

void Renderer::put(Tex* t, SDL_FRect sl, float x, float y, Paint p, float w, float h, float rot, float pivx, float pivy) {
  if (!t || !t->t) return;
  SDL_FRect src{sl.x * t->scale, sl.y * t->scale, sl.w * t->scale, sl.h * t->scale};
  if (w < 0) w = sl.w;
  if (h < 0) h = sl.h;
  SDL_FRect dst{px(x), py(y), w * s_, h * s_};
  SDL_SetTextureColorMod(t->t, (p.rgba >> 24) & 255, (p.rgba >> 16) & 255, (p.rgba >> 8) & 255);
  SDL_SetTextureAlphaMod(t->t, p.rgba & 255);
  SDL_SetTextureBlendMode(t->t, p.mode == Blend::Add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
  if (bloomOn_ && p.mode == Blend::Add)
    bloom_.push_back({t->t, src, dst, rot != 0 ? -rot * RAD2DEG : 0.0, {pivx * s_, pivy * s_}, rot != 0, (uint8_t)(p.rgba >> 24), (uint8_t)(p.rgba >> 16), (uint8_t)(p.rgba >> 8), (uint8_t)p.rgba});
  if (rot != 0 || flipX_) {
    SDL_FPoint c{pivx * s_, pivy * s_};
    SDL_RenderTextureRotated(r_, t->t, &src, &dst, -rot * RAD2DEG, rot != 0 ? &c : nullptr, flipX_ ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
  } else {
    SDL_RenderTexture(r_, t->t, &src, &dst);
  }
  SDL_SetTextureColorMod(t->t, 255, 255, 255);
  SDL_SetTextureAlphaMod(t->t, 255);
  SDL_SetTextureBlendMode(t->t, SDL_BLENDMODE_BLEND);
}

void Renderer::putCelAt(Tex* t, int col, int row, float x, float y, Paint p) {
  if (!t) return;
  put(t, {col * t->cw(), row * t->ch(), t->cw(), t->ch()}, x, y, p);
}
void Renderer::putCel(Tex* t, int cel, float x, float y, Paint p) {
  if (!t) return;
  putCelAt(t, cel % t->cols, cel / t->cols, x, y, p);
}
void Renderer::putCentered(Tex* t, float cx, float cy, Paint p, float scale, float rot) {
  if (!t) return;
  float w = t->lw() * scale, h = t->lh() * scale;
  put(t, {0, 0, t->lw(), t->lh()}, cx - w / 2, cy - h / 2, p, w, h, rot, w / 2, h / 2);
}
void Renderer::putCelCentered(Tex* t, int cel, float cx, float cy, Paint p) {
  if (!t) return;
  put(t, {(cel % t->cols) * t->cw(), (cel / t->cols) * t->ch(), t->cw(), t->ch()}, cx - t->cw() / 2, cy - t->ch() / 2, p);
}

void Renderer::sprite(Tex* t, SDL_FRect src, float cx, float cy, float w, float h, float rotRad, Uint8 a, bool add, Uint32 tint, bool flip) {
  if (!t) return;
  SDL_FRect dst{px(cx - w / 2), py(cy - h / 2), w * s_, h * s_};
  SDL_SetTextureAlphaMod(t->t, a);
  SDL_SetTextureColorMod(t->t, (tint >> 16) & 255, (tint >> 8) & 255, tint & 255);
  SDL_SetTextureBlendMode(t->t, add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
  if (bloomOn_ && add) bloom_.push_back({t->t, src, dst, -rotRad * RAD2DEG, {dst.w * 0.5f, dst.h * 0.5f}, true, (uint8_t)(tint >> 16), (uint8_t)(tint >> 8), (uint8_t)tint, a});
  SDL_RenderTextureRotated(r_, t->t, &src, &dst, -rotRad * RAD2DEG, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
  SDL_SetTextureAlphaMod(t->t, 255);
  SDL_SetTextureColorMod(t->t, 255, 255, 255);
  SDL_SetTextureBlendMode(t->t, SDL_BLENDMODE_BLEND);
}

void Renderer::quad(Tex* t, SDL_FRect src, float x, float y, float w, float h) {
  if (!t) return;
  SDL_FRect dst{px(x), py(y), w * s_, h * s_};
  SDL_RenderTexture(r_, t->t, &src, &dst);
}

void Renderer::text(float x, float y, const std::string& s, float size, Uint32 rgb, bool centre) {
  float k = size * s_ / 8.0f;
  float X = px(x), Y = py(y);
  if (centre) X -= s.size() * 8 * k / 2;
  SDL_SetRenderScale(r_, k, k);
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 200);
  SDL_RenderDebugText(r_, X / k + 1, Y / k + 1, s.c_str());
  SDL_SetRenderDrawColor(r_, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
  SDL_RenderDebugText(r_, X / k, Y / k, s.c_str());
  SDL_SetRenderScale(r_, 1, 1);
}

V2 Renderer::toBoard(float x, float y) const { return {(x - ox_) / s_ - BX, (y - oy_) / s_ - BY}; }

Renderer::View Renderer::consoleView(int frameWidth, int boardWidth) const {
  const int inset = hudInset(frameWidth, boardWidth);
  if (!inset) return {};
  for (const char* part : {"console/trim/left.png", "console/trim/right.png", "console/trim/top.png", "console/trim/bottom.png"})
    if (!vfs_.exists(part)) return {};
  return {0.9f, frameWidth * 0.5f - 345.8f - 0.9f * inset, 30.8f};
}

void Renderer::observe(const Board& b) {
  bool fresh = b.tick < lastTick_ || b.objs.size() != lastObjs_ || viz_.size() != b.objs.size();
  if (fresh) {
    viz_.assign(b.objs.size(), ObjViz{});
    for (size_t i = 0; i < b.objs.size(); i++) {
      viz_[i].wasPopped = b.objs[i].gone || b.objs[i].popTimer > 0;
      viz_[i].pegType = b.objs[i].pegType;
    }
  } else {
    for (size_t i = 0; i < b.objs.size(); i++) {
      const Obj& o = b.objs[i];
      ObjViz& v = viz_[i];
      bool popped = o.gone || o.popTimer > 0;
      if (popped && !v.wasPopped && o.pegType) {
        v.pulseStart = b.tick - (o.popTimer > 0 ? std::max(0, 4 - o.popTimer) : 0);
        v.pulseKind = 1;
      }
      if (o.pegType == 3 && v.pegType != 3 && v.pegType != 0 && !popped) {
        v.pulseStart = b.tick;
        v.pulseKind = 2;
      }
      v.wasPopped = popped;
      if (o.pegType) v.pegType = o.pegType;
    }
  }
  if (blinkReq_.tick >= 0 && viz_.size() == b.objs.size()) {
    for (size_t i = 0; i < b.objs.size(); i++)
      if (b.objs[i].pegType == blinkReq_.type && b.objs[i].visible && !b.objs[i].gone && b.objs[i].popTimer <= 0) {
        viz_[i].blinkStart = blinkReq_.tick;
        viz_[i].blinkN = blinkReq_.n;
      }
    blinkReq_ = BlinkReq{};
  }
  if (hintTick_ >= 0 && viz_.size() == b.objs.size()) {
    for (size_t i = 0; i < b.objs.size(); i++)
      if (b.objs[i].pegType == 2 && !b.objs[i].lit && !b.objs[i].gone && b.objs[i].popTimer <= 0) {
        viz_[i].pulseStart = hintTick_;
        viz_[i].pulseKind = 1;
      }
    hintTick_ = -1;
  }
  lastTick_ = b.tick;
  lastObjs_ = b.objs.size();
}

void Renderer::drawPegGlow(const Obj& o, float cx, float cy) {
  if (!o.lit) return;
  uint32_t c;
  if (const PegTypeDef* d = o.custom ? pegTypeDef(o.custom) : nullptr; d && d->hasColor) {
    putCentered(img("IMAGE_PEGGLOW"), cx, cy, {(d->color << 8) | 0x9fu, Blend::Add});
    return;
  }
  switch (o.pegType) {
    case 2: c = 0xff8800'7f; break;
    case 3: c = 0xff55ff'9f; break;
    case 4: c = 0x00ff00'9f; break;
    default: return;
  }
  putCentered(img("IMAGE_PEGGLOW"), cx, cy, {c, Blend::Add});
}

void Renderer::drawBigPulse(int pegType, float cx, float cy, int age, int kind, uint32_t tint) {
  int t = pegType - 1;
  if (t < 0 || t > 3) return;
  static const char* names[] = {"IMAGE_PULSE_NORMAL", "IMAGE_PULSE_GOAL", "IMAGE_PULSE_SCORE", "IMAGE_PULSE_POWERUP", "IMAGE_PULSE2_SCORE", "IMAGE_PULSE2_POWERUP"};
  int idx = t + ((kind > 0 && (t == 2 || t == 3)) ? 2 : 0);
  Tex* im = img(names[idx]);
  if (!im) return;
  int delay = std::max(1, im->frameDelay);
  if (age > im->rows * delay) return;
  int cel = std::min(im->rows - 1, age / delay);
  if (kind == 2) cel = im->rows - cel - 1;
  float x = (float)(int)(cx - (int)(im->lw() / 2)), y = (float)(int)(cy - (int)(im->ch() / 2));
  putCelAt(im, 0, cel, x, y, {tint, Blend::Add});
}

void Renderer::drawCustomPeg(const Board& b, const Obj& o, const PegTypeDef& d, float cx, float cy) {
  const float r = o.radius;
  const float x = cx - r, y = cy - r;
  const int cel = std::clamp(o.pegType - 1, 0, 3);
  const int hitsNeeded = std::max(1, d.hits);
  const int shown = d.clears ? std::min(o.hits, hitsNeeded - 1) : 0;
  const bool stages = !d.stages.empty() && d.clears;
  const std::string& own = stages ? d.stages[(size_t)std::min<int>(shown, (int)d.stages.size() - 1)] : d.image;
  Tex* own0 = own.empty() ? nullptr : tex(own);
  Tex* litOwn = d.litImage.empty() ? nullptr : tex(d.litImage);
  const uint32_t tint = d.hasColor ? ((d.color << 8) | 0xffu) : 0xffffffffu;
  Tex* sheet = d.hasColor ? grayImg("IMAGE_BALLPEG") : img("IMAGE_BALLPEG");
  auto sprite0 = [&](bool lit, uint32_t rgba, Blend mode) {
    if (!lit && own0) {
      put(own0, {0, 0, own0->lw(), own0->lh()}, x, y, {0xffffff00u | (rgba & 255u), mode}, r * 2, r * 2);
    } else if (lit && litOwn) {
      put(litOwn, {0, 0, litOwn->lw(), litOwn->lh()}, x, y, {0xffffff00u | (rgba & 255u), mode}, r * 2, r * 2);
    } else if (lit && own0) {
      put(own0, {0, 0, own0->lw(), own0->lh()}, x, y, {0xffffff00u | (rgba & 255u), Blend::Add}, r * 2, r * 2);
    } else if (sheet) {
      const int c = cel + (lit ? 4 : 0);
      put(sheet, {0, c * sheet->ch(), sheet->cw(), sheet->ch()}, x, y, {rgba, mode}, r * 2, r * 2);
    }
  };
  if (o.lit) {
    if (own0 && !litOwn) sprite0(false, 0xffffffffu, Blend::Normal);
    sprite0(true, own0 || litOwn ? 0xffffffffu : tint, Blend::Normal);
    return;
  }
  sprite0(false, own0 ? 0xffffffffu : tint, Blend::Normal);
  const int age = b.tick - o.hitTick;
  int progress = d.clears && hitsNeeded > 1 && !stages ? 255 * std::min(o.hits, hitsNeeded - 1) / hitsNeeded : 0;
  if (progress > 0) sprite0(true, (own0 || litOwn ? 0xffffff00u : (tint & 0xffffff00u)) | (uint32_t)progress, Blend::Normal);
  if (age >= 0 && age < 12) {
    const int a = 200 * (12 - age) / 12;
    sprite0(true, 0xffffff00u | (uint32_t)a, Blend::Add);
  }
}

void Renderer::drawPeg(const Board& b, const Obj& o, int idx, Pass pass) {
  ObjViz& v = viz_[idx];
  float cx = BX + o.pos.x, cy = BY + o.pos.y;
  if (pass == Pass::Light) {
    if (v.pulseStart >= 0) {
      const PegTypeDef* cd = o.custom ? pegTypeDef(o.custom) : nullptr;
      drawBigPulse(o.pegType, cx, cy, b.tick - v.pulseStart + 1, v.pulseKind, cd && cd->hasColor ? ((cd->color << 8) | 0xffu) : 0xffffffffu);
    }
    return;
  }
  if (!o.visible || o.gone || o.popTimer > 0) return;
  float r = o.radius;
  bool mover = o.hasMover;
  if (pass == Pass::Shadow) {
    float x = cx - 3.0f - r, y = cy + 3.0f - r;
    Paint sh{0x00000090u, Blend::Normal};
    if (r == 10.0f) {
      Tex* t = img("IMAGE_PEGSHADOW");
      if (t) {
        if (!mover) put(t, {1, 1, t->lw() - 2, t->lh() - 2}, (float)(int)x, (float)(int)y, sh);
        else put(t, {0, 0, t->lw(), t->lh()}, x - 1, y - 1, sh);
      }
    } else if (Tex* t = img("IMAGE_BIGBALLSHADOW")) {
      put(t, {0, 0, t->lw(), t->lh()}, cx - 3.0f - r, cy + 3.0f - r, sh, r * 2, r * 2);
    }
    drawPegGlow(o, cx, cy);
    return;
  }
  if (const PegTypeDef* cd = o.custom ? pegTypeDef(o.custom) : nullptr) {
    drawCustomPeg(b, o, *cd, cx, cy);
    return;
  }
  Tex* t = img("IMAGE_BALLPEG");
  if (!t) return;
  int cel = std::clamp(o.pegType - 1, 0, 3) + (o.lit ? 4 : 0);
  float x = cx - r, y = cy - r;
  if (r != 10.0f) {
    put(t, {0, cel * t->ch(), t->cw(), t->ch()}, x, y, {}, r * 2, r * 2);
  } else if (mover) {
    putCelAt(t, 0, cel, x - 1, y - 1);
  } else {
    put(t, {1, cel * t->ch() + 1, t->cw() - 2, t->ch() - 2}, (float)(int)x, (float)(int)y);
  }
  if (v.blinkStart >= 0 && !o.lit && r == 10.0f) {
    int age = 50 * v.blinkN - 1 - (b.tick - v.blinkStart);
    if (age > 0) {
      int e = age % 50;
      if (e >= 10) {
        e -= 10;
        if (e > 20) e = 40 - e;
        int a = std::clamp((int)(200.0f * (float)e / 20.0f), 0, 255);
        int litCel = std::clamp(o.pegType - 1, 0, 3) + 4;
        if (a > 0) {
          Paint pw{0xffffff00u | (uint32_t)a, Blend::Add};
          if (mover) putCelAt(t, 0, litCel, x - 1, y - 1, pw);
          else put(t, {1, litCel * t->ch() + 1, t->cw() - 2, t->ch() - 2}, (float)(int)x, (float)(int)y, pw);
        }
      }
    }
  }
}

void Renderer::brickMesh(const Obj& o, Tex* t, int row, float dx, float dy, uint32_t rgba, Blend mode, float inflate) {
  if (!t || o.brickInner.size() < 2 || o.brickOuter.size() < 2) return;
  const int N = 8;
  auto in = resample(o.brickInner, N), out = resample(o.brickOuter, N);
  float ch = (float)t->h / t->rows;
  SDL_FColor col{((rgba >> 24) & 255) / 255.0f, ((rgba >> 16) & 255) / 255.0f, ((rgba >> 8) & 255) / 255.0f, (rgba & 255) / 255.0f};
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  float k = inflate > 0 ? inflate : 1.0f;
  for (int i = 0; i <= N; i++) {
    V2 wi = o.pos + rot(in[i] * (o.scale * k), o.rot), wo = o.pos + rot(out[i] * (o.scale * k), o.rot);
    float u = (0.5f + (t->w - 1.0f) * i / N) / t->w;
    v.push_back({{px(BX + wi.x + dx), py(BY + wi.y + dy)}, col, {u, (row * ch + 0.5f) / t->h}});
    v.push_back({{px(BX + wo.x + dx), py(BY + wo.y + dy)}, col, {u, ((row + 1) * ch - 0.5f) / t->h}});
  }
  for (int i = 0; i < N; i++) {
    int q = i * 2;
    idx.insert(idx.end(), {q, q + 1, q + 2, q + 1, q + 3, q + 2});
  }
  SDL_SetTextureBlendMode(t->t, mode == Blend::Add ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(r_, t->t, v.data(), (int)v.size(), idx.data(), (int)idx.size());
  SDL_SetTextureBlendMode(t->t, SDL_BLENDMODE_BLEND);
}

void Renderer::drawBrickPeg(const Board& b, const Obj& o, int idx, Pass pass) {
  ObjViz& v = viz_[idx];
  if (pass == Pass::Light) {
    if (v.pulseStart >= 0) {
      V2 c = o.pos;
      if (o.brickInner.size() > 1 && o.brickOuter.size() > 1) {
        V2 a = (o.brickInner[o.brickInner.size() / 2] + o.brickOuter[o.brickOuter.size() / 2]) * 0.5f;
        c = o.pos + rot(a * o.scale, o.rot);
      }
      const PegTypeDef* cd = o.custom ? pegTypeDef(o.custom) : nullptr;
      drawBigPulse(o.pegType, BX + c.x, BY + c.y, b.tick - v.pulseStart + 1, v.pulseKind, cd && cd->hasColor ? ((cd->color << 8) | 0xffu) : 0xffffffffu);
    }
    return;
  }
  if (!o.visible || o.gone || o.popTimer > 0) return;
  const PegTypeDef* cd = o.custom ? pegTypeDef(o.custom) : nullptr;
  Tex* t = cd && cd->hasColor ? grayImg("IMAGE_BRICK") : img("IMAGE_BRICK");
  if (!t) return;
  int cel = std::clamp(o.pegType - 1, 0, 3) + (o.lit ? 4 : 0);
  const uint32_t brickTint = cd && cd->hasColor ? ((cd->color << 8) | 0xffu) : 0xffffffffu;
  if (pass == Pass::Shadow) {
    brickMesh(o, t, cel, -3, 3, 0x00000090u, Blend::Normal);
    if (o.lit && o.pegType != 1) {
      static const uint32_t gc[] = {0, 0xff8800ff, 0xff00ffff, 0x00ff00ff};
      brickMesh(o, img("IMAGE_BRICKGLOW"), 0, 0, 0, gc[o.pegType - 1], Blend::Add, 2.0f);
    }
    return;
  }
  brickMesh(o, t, cel, 0, 0, brickTint, Blend::Normal);
  if (cd && !o.lit) {
    const int need = std::max(1, cd->hits);
    const int progress = cd->clears && need > 1 ? 255 * std::min(o.hits, need - 1) / need : 0;
    if (progress > 0) brickMesh(o, t, cel + 4, 0, 0, (brickTint & 0xffffff00u) | (uint32_t)progress, Blend::Normal);
    const int age = b.tick - o.hitTick;
    if (age >= 0 && age < 12) brickMesh(o, t, cel + 4, 0, 0, 0xffffff00u | (uint32_t)(200 * (12 - age) / 12), Blend::Add);
  }
}

void Renderer::drawObj(const Board& b, const Obj& o, int idx, Pass pass) {
  if (idx >= (int)viz_.size()) return;
  if (o.shape == Shape::Circle && o.pegType) {
    drawPeg(b, o, idx, pass);
    return;
  }
  if (o.shape == Shape::Brick && (o.pegType || o.image.empty())) {
    if (o.pegType) drawBrickPeg(b, o, idx, pass);
    return;
  }
  if (pass != Pass::Main || !o.visible || o.gone) return;
  float popA = o.popTimer > 0 ? o.popTimer / 4.0f : 1.0f;
  Uint8 a = (Uint8)(255 * popA);
  const bool feverSlot = o.shape == Shape::Hole && o.id == "fever" && o.subId != 0;
  Tex* lightImg = nullptr;
  int lift = 0;
  if (feverSlot) {
    lift = o.subId == 3 ? 25 : 20;
    int k = o.score == 5000 ? 0 : o.score == 10000 ? 1 : o.score == 25000 ? 2 : o.score == 50000 ? 3 : o.score == 100000 ? 4 : 1;
    static const char* prize[] = {"IMAGE_BONUS5000", "IMAGE_BONUS10000", "IMAGE_BONUS25000", "IMAGE_BONUS50000", "IMAGE_BONUS100000"};
    static const char* light[] = {"IMAGE_BONUS5000LIGHT", "IMAGE_BONUS10000LIGHT", "IMAGE_BONUS25000LIGHT", "IMAGE_BONUS50000LIGHT", "IMAGE_BONUS100000LIGHT"};
    float hx = BX + o.pos.x, hy = BY + o.pos.y;
    if (b.cfg.nights) {
      const bool duel = b.cfg.duel;
      const char* bucket = "IMAGE_RBUCKET";
      int dy1 = -10, dy2 = -5;
      switch (o.score) {
        case 5000:
        case 10000: bucket = "IMAGE_BBUCKET"; dy1 = 0; dy2 = 0; break;
        case 25000: bucket = "IMAGE_GBUCKET"; dy1 = -7; dy2 = -5; break;
        case 50000: bucket = duel ? "IMAGE_RBUCKET" : "IMAGE_GBUCKET"; dy1 = -7; dy2 = duel ? -7 : -5; break;
        default: break;
      }
      const int row = o.flash > 0 && (o.flash / 15) % 2 == 0 ? 1 : 0;
      if (Tex* d = img(bucket)) putCelAt(d, 0, row, (float)(int)(hx - (int)(d->cw() / 2)), (float)(int)(hy - lift - 25 + dy1));
      if (Tex* t = img(prize[k])) putCelAt(t, 0, row, (float)(int)(hx - (int)(t->cw() / 2)), (float)(int)(hy - lift - 35 + dy1 + dy2));
    } else {
      if (Tex* h = img("IMAGE_BONUSHALO")) put(h, {0, 0, h->lw(), h->lh()}, (float)(int)(hx - (int)(h->lw() / 2)), (float)(int)(hy - lift - 40));
      if (Tex* t = img(prize[k])) put(t, {0, 0, t->lw(), t->lh()}, (float)(int)(hx - (int)(t->lw() / 2)), (float)(int)(hy - lift - 25));
    }
    lightImg = img(light[k]);
  }
  Tex* objTex = objImage(o.image);
  if (objTex && b.W > 650 && b.cfg.authoredWidth <= 650 && !o.fromBase) {
    const float half = (float)(b.W - 650) * 0.5f, cx = o.pos.x + rot({o.imgDx, o.imgDy}, o.rot).x, reach = std::max(objTex->lw(), objTex->lh()) * 0.5f;
    if (cx - reach > half + 650.0f || cx + reach < half) objTex = nullptr;
  }
  if (auto t = objTex) {
    V2 off = rot({o.imgDx, o.imgDy}, o.rot);
    float lw = t->lw(), lh = t->lh();
    sprite(t, {0, 0, (float)t->w, (float)t->h}, BX + o.pos.x + off.x, BY + o.pos.y + off.y + (b.cfg.nights && o.shape == Shape::Hole ? 2.0f : 0.0f), lw, lh, o.rot, a, false, 0xffffff, o.mirror);
  }
  if (feverSlot && o.flash > 0) {
    float hx = BX + o.pos.x, hy = BY + o.pos.y;
    const char* gl = o.subId == 1 || o.subId == 5 ? "IMAGE_BONUSGLOW_BLUE" : o.subId == 2 || o.subId == 4 ? "IMAGE_BONUSGLOW_GREEN" : "IMAGE_BONUSGLOW";
    if (Tex* g = img(gl)) put(g, {0, 0, g->lw(), g->lh()}, (float)(int)(hx - (int)(g->lw() / 2)), (float)(int)(hy - 55), {0xffffffffu, Blend::Add});
    if (lightImg && (o.flash / 15) % 2 == 0)
      put(lightImg, {0, 0, lightImg->lw(), lightImg->lh()}, (float)(int)(hx - (int)(lightImg->lw() / 2)), (float)(int)(hy - lift - 40), {0x808080ffu, Blend::Add});
  }
}

void Renderer::drawBall(const Board& b, const Ball& bl, bool inGun) {
  (void)inGun;
  if (bl.holeOut > 0) return;
  float cx = BX + bl.pos.x, cy = BY + bl.pos.y;
  float r = bl.r;
  struct EyesOnExit {
    Renderer* R; float x, y; bool on;
    ~EyesOnExit() { if (on) if (Tex* t = R->img("IMAGE_SPOOKYEYES")) R->putCentered(t, x + 2, y + 2, {}, 1.2f); }
  } eyes{this, cx, cy, bl.spooky || bl.spookyUsed};
  struct HatOnExit {
    Renderer* R; float x, y, vx; bool on;
    ~HatOnExit() {
      if (!on) return;
      Tex* t = R->img("IMAGE_MAGICHAT");
      if (!t) return;
      float tilt = std::clamp(vx / 3.0f, -1.0f, 1.0f) * 0.7853982f;
      R->putCentered(t, x, y - t->lh() / 2 - 2.0f, {}, 1.0f, -tilt);
    }
  } hatDraw{this, cx + (bl.hat && bl.hatAge > 0 ? bl.hatPos.x - bl.pos.x : 0.0f), cy + (bl.hat && bl.hatAge > 0 ? bl.hatPos.y - (bl.pos.y - 21.0f) : 0.0f), bl.vel.x, bl.hat};
  if (bl.fire) {
    const float fs = b.physics.ballRadius == 6.0f ? 1.0f : r / (b.cfg.nights ? 11.0f : 10.0f);
    if (Tex* t = img("IMAGE_FIREBALL")) putCentered(t, cx, cy, {}, fs, 0.05f * (float)b.tick);
    return;
  }
  if (r == 6.0f) {
    float x = cx - r, y = cy - r;
    put(img("IMAGE_BALLSHADOW"), {0, 0, 14, 14}, x - 2, y + 2, {0x00000060u, Blend::Normal});
    if (Tex* t = img("IMAGE_BALL")) put(t, {0, 0, t->lw(), t->lh()}, x, y);
    return;
  }
  if (Tex* t = img("IMAGE_BIGBALLSHADOW")) put(t, {0, 0, t->lw(), t->lh()}, cx - r - 2, cy - r + 2, {0x00000090u, Blend::Normal}, r * 2, r * 2);
  if (Tex* t = img("IMAGE_BIGBALL")) put(t, {0, 0, t->lw(), t->lh()}, cx - r, cy - r, {}, r * 2, r * 2);
}

void Renderer::drawGuide(const Board& b) {
  const int age = guideAge_;
  if (age && age < 11) return;
  const bool powered = b.guideOn();
  const auto tr = b.guideTrace(b.gunAngle, powered);
  const int count = (int)tr.pts.size();
  const bool rebound = powered && tr.hitStep >= 0;
  const int full = rebound ? std::max(tr.hitStep + 1, count) : powered ? 80 : 40;
  const int len = age ? std::min(full, rebound ? (age - 10) * full / 20 : age * 4 - 40) : full;
  const bool fire = b.master[b.cur] == Board::P_FIREBALL && b.turns[b.cur] > 0;
  const uint32_t color = fire ? 0x903010ffu : 0x109090ffu;
  const Paint add{color, Blend::Add};
  const V2 muzzle = b.muzzle(b.gunAngle, b.physics.ballRadius);
  const V2 velocity{std::cos(b.gunAngle) * b.physics.launchSpeed, -std::sin(b.gunAngle) * b.physics.launchSpeed};
  const V2 accel{b.physics.gravityX * .5f, b.physics.gravityY * .5f};
  auto ballistic = [&](int n) { return muzzle + velocity * (float)n + accel * (float)(n * n); };
  V2 prev = ballistic(2);
  Tex* rect = img("IMAGE_GUIDERECT");
  for (int n = 6; n - 4 < len; n += 4) {
    const int i = n - 3;
    if (powered && (i >= count || (rebound && i >= std::min(len, tr.hitStep)))) break;
    const V2 at = i < count ? tr.pts[i] : ballistic(n);
    const V2 next = powered ? at : ballistic(n);
    const float rot = std::atan2(prev.y - next.y, next.x - prev.x);
    const bool arrow = !rebound && n >= len && len == full;
    if (!powered && tr.hitStep >= 0 && n > tr.hitStep + (arrow ? 2 : 0)) {
      if (auto* hit = img("IMAGE_GUIDEHIT")) {
        putCentered(hit, BX + tr.hit.x, BY + tr.hit.y, {b.cfg.nights ? color : 0xffffffffu, Blend::Normal}, 1, rot);
        if (b.cfg.nights) putCentered(hit, BX + tr.hit.x, BY + tr.hit.y, add, 1, rot);
      }
      break;
    }
    if (arrow) putCentered(img("IMAGE_GUIDEARROW"), BX + (int)at.x, BY + (int)at.y, add, 1, rot);
    else putCentered(rect, BX + at.x, BY + at.y, add, 1, rot);
    prev = next;
  }
  if (!rebound || tr.hitStep >= len) return;
  const int end = std::min(count, len);
  if (Tex* line = img("IMAGE_GUIDERECT2"); line && tr.hitStep < end) {
    std::vector<SDL_Vertex> strip;
    std::vector<int> indices;
    const SDL_FColor tint{(color >> 24) / 255.f, ((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f, 1};
    strip.reserve(2 * (end - tr.hitStep + 1));
    indices.reserve(6 * (end - tr.hitStep));
    auto point = [&](int i) { return i < tr.hitStep ? tr.hit : tr.pts[i]; };
    for (int i = tr.hitStep - 1; i < end; ++i) {
      const V2 p = point(i), tangent = point(std::min(i + 1, end - 1)) - point(i - 1);
      const float length = std::sqrt(tangent.len2());
      if (length == 0) continue;
      const V2 side = V2{-tangent.y, tangent.x} * (5.f / length);
      const int k = (int)strip.size();
      const float u = (k & 2) ? 1.f : 0.f;
      strip.push_back({{px(BX + p.x + side.x), py(BY + p.y + side.y)}, tint, {u, 0}});
      strip.push_back({{px(BX + p.x - side.x), py(BY + p.y - side.y)}, tint, {u, 1}});
      if (k) indices.insert(indices.end(), {k - 2, k - 1, k, k, k - 1, k + 1});
    }
    if (!indices.empty()) {
      SDL_SetTextureBlendMode(line->t, SDL_BLENDMODE_ADD);
      SDL_RenderGeometry(r_, line->t, strip.data(), (int)strip.size(), indices.data(), (int)indices.size());
      SDL_SetTextureBlendMode(line->t, SDL_BLENDMODE_BLEND);
    }
  }
  if (Tex* marker = img("IMAGE_ALPHABALL")) {
    const float rot = 4.712389f - tr.hitAng;
    putCentered(marker, BX + tr.hit.x, BY + tr.hit.y, add, 1, rot);
    putCentered(marker, BX + tr.hit.x, BY + tr.hit.y, add, 1, rot);
  }
}

void Renderer::drawBoardShadow() {
  Tex* t = img("IMAGE_INT_BACKSHADOW");
  if (!t) return;
  float W = t->lw(), H = t->lh(), h2 = H - 25;
  const float X = 76, Y = 49;
  const float dw = (float)(canvasW_ - 800);
  struct R { float x, y, w, h, dx, dw; };
  const R pieces[] = {{0, 0, 30, h2, 0, 0}, {30, 0, 205, 25, 0, dw / 2}, {235, 0, 173, 108, dw / 2, 0}, {408, 0, 207, 25, dw / 2, dw / 2}, {615, 0, W - 615, h2, dw, 0}};
  for (auto& p : pieces) put(t, {p.x, p.y, p.w, p.h}, X + p.x + p.dx, Y + p.y, {}, p.w + p.dw, p.h);
}

void Renderer::drawWallMarks(float padL) {
  if (padL <= 0) return;
  const float x0 = 75.0f, x1 = BX, x2 = BX + (float)boardW_, x3 = 75.0f + (float)boardW_ + 2.0f * padL;
  SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 80);
  SDL_FRect l{px(x0), py(BY), px(x1) - px(x0), 560.0f * s_}, rr{px(x2), py(BY), px(x3) - px(x2), 560.0f * s_};
  SDL_RenderFillRect(r_, &l);
  SDL_RenderFillRect(r_, &rr);
}

void Renderer::drawBoardShadowBottom(float rows, float top) {
  Tex* t = img("IMAGE_INT_BACKSHADOW");
  if (!t) return;
  const float W = t->lw(), y = t->lh() - rows;
  const float dw = (float)(canvasW_ - 800), e = 0.01f;
  auto sx = [&](float c) { return (std::round(px(c)) - ox_) / s_ + e; };
  auto sy = [&](float c) { return (std::round(py(c)) - oy_) / s_ + e; };
  const float x0 = sx(76), x1 = sx(106), x2 = sx(691 + dw), x3 = sx(76 + W + dw);
  const float y0 = sy(top + y), h = sy(top + y + rows) - y0 + e;
  put(t, {0, y, 30, rows}, x0, y0, {}, x1 - x0 + e, h);
  put(t, {30, y, 585, rows}, x1, y0, {}, x2 - x1 + e, h);
  put(t, {615, y, W - 615, rows}, x2, y0, {}, x3 - x2 + e, h);
}

void Renderer::drawZoomFrame(const Board& b) {
  float k = 0.1f * (float)(20 - b.zoomFade) / 20.0f + 1.0f;
  float s0 = s_, ox0 = ox_, oy0 = oy_;
  if (k > 1.0f) {
    float cx = (float)canvasW_ * 0.5f, cy = 300.0f;
    ox_ += cx * s0 * (1 - k);
    oy_ += cy * s0 * (1 - k);
    s_ *= k;
  }
  Tex* l = img("IMAGE_SCREENFRAMELEFT");
  Tex* t = img("IMAGE_SCREENFRAMETOP");
  Tex* bt = img("IMAGE_SCREENFRAMEBOTTOM");
  Tex* r = img("IMAGE_SCREENFRAMERIGHT");
  float dw = (float)(canvasW_ - 800);
  if (!trimClip_ && l && t && bt && r) {
    put(l, {0, 0, l->lw(), l->lh()}, 0, 0);
    put(t, {0, 0, t->lw(), t->lh()}, l->lw(), 0, {}, t->lw() + dw, t->lh());
    put(bt, {0, 0, bt->lw(), bt->lh()}, l->lw(), l->lh() - bt->lh(), {}, bt->lw() + dw, bt->lh());
    put(r, {0, 0, r->lw(), r->lh()}, l->lw() + t->lw() + dw, 0);
  }
  if (Tex* c = img("IMAGE_FEVERCAM")) put(c, {0, 0, c->lw(), c->lh()}, 0, 0);
  s_ = s0;
  ox_ = ox0;
  oy_ = oy0;
}

namespace {
struct FxInterp {
  FxState& s;
  std::vector<float> saved;
  FxInterp(FxState& st, float alpha) : s(st) {
    if (alpha <= 0) return;
    for (auto& L : s.eff)
      for (auto& e : L) {
        saved.push_back(e.x);
        saved.push_back(e.y);
        if (e.age >= 0 && !e.b40) { e.x += alpha * e.vx; e.y += alpha * e.vy; }
      }
    for (auto& t : s.texts) {
      saved.push_back(t.x);
      saved.push_back(t.y);
      if (t.age >= 0) { t.x += alpha * t.vx; t.y += alpha * t.vy; }
    }
  }
  ~FxInterp() {
    size_t k = 0;
    for (auto& L : s.eff)
      for (auto& e : L) if (k + 1 < saved.size()) { e.x = saved[k++]; e.y = saved[k++]; }
    for (auto& t : s.texts) if (k + 1 < saved.size()) { t.x = saved[k++]; t.y = saved[k++]; }
  }
};
struct SlowMoInterp {
  Board& b;
  std::vector<std::pair<size_t, std::pair<V2, float>>> objs;
  std::vector<std::pair<size_t, V2>> emits;
  std::vector<V2> balls;
  explicit SlowMoInterp(const Board& cb, float fReal = -1.0f) : b(const_cast<Board&>(cb)) {
    if (!b.slowOn && fReal < 0) return;
    const float f = b.slowOn ? (float)b.slowAcc / 1000.0f : std::min(fReal, 1.0f);
    for (Ball& bl : b.balls) {
      balls.push_back(bl.pos);
      bl.pos = bl.prev * (1.0f - f) + bl.pos * f;
    }
    for (size_t i = 0; i < b.objs.size(); i++) {
      Obj& o = b.objs[i];
      if (!o.hasMover || !o.canMove || o.gone || o.mtick < 1) continue;
      float x = o.pos.x, y = o.pos.y, r = o.rot;
      b.drawCalc(o, f, x, y, r);
      objs.push_back({i, {o.pos, o.rot}});
      o.pos = {x, y};
      if (o.shape == Shape::Poly || o.shape == Shape::Brick) o.rot = r;
    }
    for (size_t i = 0; i < b.emitters.size(); i++) {
      Emit& e = b.emitters[i];
      if (!e.hasMover || !e.canMove || e.mtick < 1) continue;
      float x = e.pos.x, y = e.pos.y;
      b.emitDrawPos(e, f, x, y);
      emits.push_back({i, e.pos});
      e.pos = {x, y};
    }
  }
  ~SlowMoInterp() {
    for (size_t k = 0; k < balls.size(); k++) b.balls[k].pos = balls[k];
    for (auto& s : objs) {
      b.objs[s.first].pos = s.second.first;
      b.objs[s.first].rot = s.second.second;
    }
    for (auto& s : emits) b.emitters[s.first].pos = s.second;
  }
};
}

void Renderer::bloomPass(int ow, int oh, const SDL_Rect& clip) {
  if (bloom_.empty() || ow < 16 || oh < 16) return;
  const float D = 4.0f;
  const int bw = std::max(8, (int)(ow / D)), bh = std::max(8, (int)(oh / D));
  if (!bloomA_ || bw != bloomW_ || bh != bloomH_) {
    if (bloomA_) SDL_DestroyTexture(bloomA_);
    if (bloomB_) SDL_DestroyTexture(bloomB_);
    bloomA_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, bw, bh);
    bloomB_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, bw, bh);
    bloomW_ = bw;
    bloomH_ = bh;
    for (SDL_Texture* t : {bloomA_, bloomB_})
      if (t) { SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD); }
  }
  if (!bloomA_ || !bloomB_) return;
  SDL_Texture* old = SDL_GetRenderTarget(r_);
  SDL_SetRenderTarget(r_, bloomA_);
  SDL_SetRenderClipRect(r_, nullptr);
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
  SDL_RenderClear(r_);
  for (const BloomRec& q : bloom_) {
    SDL_FRect d{q.dst.x / D, q.dst.y / D, q.dst.w / D, q.dst.h / D};
    SDL_FPoint c{q.c.x / D, q.c.y / D};
    SDL_SetTextureBlendMode(q.t, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(q.t, q.r, q.g, q.b);
    SDL_SetTextureAlphaMod(q.t, q.a);
    if (q.rot) SDL_RenderTextureRotated(r_, q.t, &q.src, &d, q.ang, &c, SDL_FLIP_NONE);
    else SDL_RenderTexture(r_, q.t, &q.src, &d);
    SDL_SetTextureColorMod(q.t, 255, 255, 255);
    SDL_SetTextureAlphaMod(q.t, 255);
    SDL_SetTextureBlendMode(q.t, SDL_BLENDMODE_BLEND);
  }
  static const float wt[9] = {0.028f, 0.067f, 0.124f, 0.179f, 0.204f, 0.179f, 0.124f, 0.067f, 0.028f};
  auto blur = [&](SDL_Texture* src, SDL_Texture* dst, bool horiz) {
    SDL_SetRenderTarget(r_, dst);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    for (int k = -4; k <= 4; k++) {
      SDL_SetTextureAlphaMod(src, (uint8_t)std::lround(wt[k + 4] * 255.0f));
      SDL_FRect d{horiz ? k * 1.6f : 0.0f, horiz ? 0.0f : k * 1.6f, (float)bw, (float)bh};
      SDL_RenderTexture(r_, src, nullptr, &d);
    }
    SDL_SetTextureAlphaMod(src, 255);
  };
  blur(bloomA_, bloomB_, true);
  blur(bloomB_, bloomA_, false);
  blur(bloomA_, bloomB_, true);
  blur(bloomB_, bloomA_, false);
  SDL_SetRenderTarget(r_, old);
  SDL_SetRenderClipRect(r_, &clip);
  const float st = std::clamp(vis_.bloomStrength, 0.0f, 2.0f);
  SDL_FRect full{0, 0, (float)ow, (float)oh};
  SDL_SetTextureAlphaMod(bloomA_, (uint8_t)std::lround(std::min(1.0f, 0.5f * st) * 255.0f));
  SDL_RenderTexture(r_, bloomA_, nullptr, &full);
  SDL_SetTextureAlphaMod(bloomA_, 255);
}

std::pair<Renderer::Tex*, Renderer::Tex*> Renderer::consoleGutters(int frameWidth, int boardWidth, const std::string& background) {
  if (!hudInset(frameWidth, boardWidth)) return {};
  const auto find = [&](const std::string& stem) {
    const std::string prefix = "console/gutters/" + stem;
    return std::pair{tex(prefix + "_left"), tex(prefix + "_right")};
  };
  const std::string key = normKey(background);
  const auto separator = key.rfind('\\');
  std::string stem = key.substr(separator == std::string::npos ? 0 : separator + 1);
  if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
  if (!stem.empty() && stem != "DEFAULT") {
    auto art = find(stem);
    if (art.first && art.second) return art;
  }
  auto art = find("default");
  return art.first && art.second ? art : std::pair<Tex*, Tex*>{};
}

void Renderer::draw(const Board& b, const LevelBundle& lb, const Hud& hud) {
  int ow = 0, oh = 0;
  SDL_GetCurrentRenderOutputSize(r_, &ow, &oh);
  observe(b);
  advance(b);
  const bool interp = vis_.interpolate && !b.slowOn && hud.alpha < 1.0f && hud.alpha >= 0.0f;
  SlowMoInterp slowMo(b, interp ? hud.alpha : -1.0f);
  FxInterp fxInterp(fx_, interp ? hud.alpha : 0.0f);
  if (stageFor_ != lb.name) {
    for (auto it = cache_.lower_bound("widescreen/levels/");
         it != cache_.end() && it->first.starts_with("widescreen/levels/");) {
      if (it->second.t) SDL_DestroyTexture(it->second.t);
      it = cache_.erase(it);
    }
    stageFor_ = lb.name;
    stageCel_ = 0;
    stageNo_ = levelNo_ = 0;
    int k = 0, last = -1;
    for (auto& s : listStageLevels(vfs_)) {
      if (s.stage != last) { last = s.stage; k = 0; }
      if (s.name == lb.name) { stageNo_ = s.stage; levelNo_ = k; stageCel_ = std::min(s.stage, 9); break; }
      k++;
    }
  }
  boardW_ = b.W;
  canvasW_ = 800 + (b.W - 650);
  const float padL = frameCanvas_ > canvasW_ ? (float)((frameCanvas_ - canvasW_) / 2) : 0.0f;
  frameCanvas_ = 0;
  canvasW_ += (int)(2 * padL);
  BX = 75.0f + padL;
  {
    float s0 = std::min(oh / 600.0f, ow / (float)canvasW_);
    texS_ = s0;
    texQ_ = !vis_.smooth ? 0 : s0 >= 1.45f ? 1 : s0 <= 0.7f ? -1 : 0;
  }
  const auto [leftGutter, rightGutter] = consoleGutters(canvasW_, b.W, lb.background);
  const float inset = (float)hudInset(canvasW_, b.W);
  const float innerPad = padL - inset;
  const auto centeredHud = [&](auto draw) {
    const float x = ox_, board = BX;
    const int w = canvasW_;
    ox_ += inset * s_;
    BX -= inset;
    canvasW_ -= (int)(2 * inset);
    draw();
    ox_ = x;
    BX = board;
    canvasW_ = w;
  };
  bloomOn_ = vis_.bloom;
  bloom_.clear();
  s_ = oh / 600.0f;
  if (canvasW_ * s_ > ow) s_ = ow / (float)canvasW_;
  ox_ = (ow - canvasW_ * s_) * 0.5f;
  oy_ = (oh - 600 * s_) * 0.5f;
  const float frameS = s_, frameOx = ox_, frameOy = oy_;
  const View view = consoleView(canvasW_, b.W);
  ox_ += view.x * s_;
  oy_ += view.y * s_;
  s_ *= view.k;
  if (fx_.shake > 0) {
    ox_ += (float)(fx_.rand(5) - 2) * s_;
    oy_ += (float)(fx_.rand(5) - 2) * s_;
  }
  const float baseS = s_, baseOx = ox_, baseOy = oy_;
  const bool zoomed = b.zoom > 0;
  V2 zp = b.zoomPivot;
  zp.x += padL;
  if (b.slowOn && !b.balls.empty()) {
    float zy = 40.0f + b.balls[0].pos.y;
    if (400.0f < zy) {
      zy = zy + (zy - 400.0f) * 1.0f;
      if (610.0f < zy) zy = 610.0f;
    }
    zp = {BX + b.balls[0].pos.x, zy};
  }
  if (zoomed) {
    float Z = 1.0f + b.cfg.zoomK * b.zoom;
    ox_ += zp.x * s_ * (1 - Z);
    oy_ += zp.y * s_ * (1 - Z);
    s_ *= Z;
  }
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
  SDL_RenderClear(r_);

  const std::string background = "levels/" + lb.background;
  Tex* bg = tex(background);
  if (bg) {
    float k = std::max(ow / (float)bg->w, oh / (float)bg->h);
    SDL_FRect d{(ow - bg->w * k) / 2, (oh - bg->h * k) / 2, bg->w * k, bg->h * k};
    SDL_SetTextureColorMod(bg->t, 70, 70, 85);
    SDL_RenderTexture(r_, bg->t, nullptr, &d);
    SDL_SetTextureColorMod(bg->t, 255, 255, 255);
  }
  const int canvasL = (int)std::floor(frameOx), canvasT = (int)std::floor(frameOy);
  SDL_Rect canvasClip{canvasL, canvasT, (int)std::ceil(frameOx + canvasW_ * frameS) - canvasL, (int)std::ceil(frameOy + 600 * frameS) - canvasT};
  SDL_SetRenderClipRect(r_, &canvasClip);
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
  SDL_FRect cv{px(inset), py(0), (canvasW_ - 2 * inset) * s_, 600 * s_};
  SDL_RenderFillRect(r_, &cv);
  if (leftGutter && rightGutter) {
    const int x0 = canvasClip.x, x1 = (int)std::round(px(inset));
    const int x2 = (int)std::round(px(canvasW_ - inset)), x3 = canvasClip.x + canvasClip.w;
    const int y0 = (int)std::round(frameOy), y1 = (int)std::round(frameOy + 600 * frameS);
    const auto gutter = [&](Tex* t, const char* bezel, int l, int r, bool left) {
      const SDL_Rect clip{l, y0, r - l, y1 - y0};
      SDL_SetRenderClipRect(r_, &clip);
      if (view.k < 1) {
        const float gw = t->lw() * 600.0f / t->lh(), gx = left ? canvasW_ * 0.5f - 400 - gw : canvasW_ * 0.5f + 400;
        const SDL_FRect dst{frameOx + gx * frameS, frameOy, gw * frameS, 600 * frameS};
        SDL_RenderTexture(r_, t->t, nullptr, &dst);
        const SDL_FRect edge{left ? (float)t->w - 1 : 0.0f, 0, 1, (float)t->h};
        const SDL_FRect rest = left ? SDL_FRect{dst.x + dst.w, dst.y, r - dst.x - dst.w, dst.h} : SDL_FRect{(float)l, dst.y, dst.x - l, dst.h};
        if (rest.w > 0) SDL_RenderTexture(r_, t->t, &edge, &rest);
        return;
      }
      const float k = std::max(inset / t->lw(), 600.0f / t->lh());
      const float sw = t->w * (inset / (k * t->lw())), sh = t->h * (600.0f / (k * t->lh()));
      const SDL_FRect src{left ? t->w - sw : 0.0f, (t->h - sh) / 2, sw, sh};
      const SDL_FRect dst{(float)l, (float)y0, (float)clip.w, (float)clip.h};
      SDL_RenderTexture(r_, t->t, &src, &dst);
      if (Tex* trim = tex(bezel)) {
        const float scale = 600.0f / 720.0f;
        const float height = trim->lh() * (1.08f * scale);
        const float width = trim->lw() * (0.7f * scale);
        const float margin = 70.0f * scale;
        const float x = left ? margin : canvasW_ - margin - width;
        quad(trim, {0, 0, (float)trim->w, (float)trim->h}, x, (600 - height) / 2, width, height);
      }
    };
    gutter(leftGutter, "console/trim/left", x0, x1, true);
    gutter(rightGutter, "console/trim/right", x2, x3, false);
  }
  if (view.k < 1) {
    SDL_SetRenderClipRect(r_, &canvasClip);
    const auto piece = [&](const char* path, float u, float v) {
      if (Tex* t = tex(path)) quad(t, {0, 0, (float)t->w, (float)t->h}, inset + u - 1, v, t->lw() + 2, t->lh());
    };
    piece("console/trim/top", 94, -45);
    piece("console/trim/bottom", 94, 582);
  }
  trimClip_ = view.k < 1 ? std::optional<SDL_Rect>(canvasClip) : std::nullopt;
  SDL_Rect field{(int)px(BX), (int)py(BY), (int)std::ceil(b.W * s_), (int)std::ceil(560 * s_)};
  SDL_Rect interior{(int)px(75.0f + inset), (int)py(BY), (int)std::ceil((b.W + 2 * innerPad) * s_), (int)std::ceil(560 * s_)};
  SDL_Rect objClip = zoomed ? canvasClip : field;
  SDL_SetRenderClipRect(r_, &objClip);
  if (bg) {
    bool small = bg->lw() == 646 && bg->lh() == 543;
    float cx = BX + b.W * 0.5f, cy = small ? BY + 284 : 300;
    if (!small) cx = canvasW_ * 0.5f;
    float topY = 0;
    const float wbW = b.W + 2 * innerPad;
    if (innerPad > 0) SDL_SetRenderClipRect(r_, &interior);
    Tex* panorama = b.W == 650 && innerPad > 0 ? tex("widescreen/" + background) : nullptr;
    if (panorama && panorama->lw() >= wbW && panorama->lh() == bg->lh()) {
      const float y = cy - bg->lh() / 2;
      const int right = field.x + field.w;
      const SDL_Rect wings[]{{interior.x, interior.y, field.x - interior.x, interior.h},
                             {right, interior.y, interior.x + interior.w - right, interior.h}};
      for (const auto& wing : wings) {
        if (wing.w <= 0) continue;
        SDL_SetRenderClipRect(r_, &wing);
        quad(panorama, {0, 0, (float)panorama->w, (float)panorama->h}, cx - panorama->lw() / 2, y, panorama->lw(), panorama->lh());
      }
      SDL_SetRenderClipRect(r_, &field);
      quad(bg, {0, 0, (float)bg->w, (float)bg->h}, cx - bg->lw() / 2, y, bg->lw(), bg->lh());
    } else if (Tex* wb = (b.W > 650 || innerPad > 0) ? wideBackground(background, (int)wbW, &topY) : nullptr) {
      quad(wb, {0, 0, (float)wb->w, (float)wb->h}, 75.0f, small ? cy - wb->lh() / 2 : topY, wb->lw(), wb->lh());
    } else {
      float w = bg->lw() * (small ? b.W / 650.0f : (canvasW_ - 2 * inset) / 800.0f), h = bg->lh();
      quad(bg, {0, 0, (float)bg->w, (float)bg->h}, cx - w / 2, cy - h / 2, w, h);
    }
    if (padL > 0) SDL_SetRenderClipRect(r_, &objClip);
    centeredHud([&] { drawBoardShadowBottom(b.cfg.nights ? 26.0f : 25.0f, ((int)bg->lh() & 1) ? 49.0f : 50.0f); });
  }
  for (size_t i = 0; i <= b.objs.size(); i++) {
    emitDraw(b, 0, i);
    if (i < b.objs.size() && b.objs[i].background) drawObj(b, b.objs[i], (int)i, Pass::Main);
  }
  fxDrawEffects(b, -1);
  for (size_t i = 0; i < b.objs.size(); i++)
    if (!b.objs[i].background) drawObj(b, b.objs[i], (int)i, Pass::Shadow);
  for (size_t i = 0; i <= b.objs.size(); i++) {
    emitDraw(b, 1, i);
    if (i == b.objs.size()) break;
    const Obj& o = b.objs[i];
    if (!o.background && !o.foreground && !o.foreground2 && o.shape != Shape::Line) drawObj(b, o, (int)i, Pass::Main);
  }
  SDL_SetRenderClipRect(r_, &canvasClip);
  fxDrawEffects(b, 0);
  drawRod(b);
  fxDrawTexts(b, -1);
  SDL_SetRenderClipRect(r_, &objClip);
  for (auto& bl : b.balls) drawBall(b, bl, false);
  if (b.phase == Phase::Aim) {
    Ball g;
    g.r = b.physics.ballRadius;
    g.pos = b.muzzleArmed(b.gunAngle);
    drawBall(b, g, true);
    if (hud.showGuide) drawGuide(b);
  }
  for (size_t i = 0; i <= b.objs.size(); i++) {
    emitDraw(b, 2, i);
    if (i < b.objs.size() && b.objs[i].foreground && !b.objs[i].foreground2 && !b.objs[i].background) drawObj(b, b.objs[i], (int)i, Pass::Main);
  }
  for (size_t i = 0; i < b.objs.size(); i++) drawObj(b, b.objs[i], (int)i, Pass::Light);
  SDL_SetRenderClipRect(r_, &canvasClip);
  fxDrawEffects(b, 1);
  centeredHud([&] {
    drawBoardShadow();
    drawWallMarks(innerPad);
    drawHud(b, hud);
  });
  SDL_SetRenderClipRect(r_, &objClip);
  for (size_t i = 0; i <= b.objs.size(); i++) {
    emitDraw(b, 3, i);
    if (i < b.objs.size() && b.objs[i].foreground2) drawObj(b, b.objs[i], (int)i, Pass::Main);
  }
  SDL_SetRenderClipRect(r_, &canvasClip);
  fxDrawTexts(b, 0);
  fxDrawTexts(b, 1);
  if (b.phase == Phase::Aim) zenOn_ = b.turns[b.cur] > 0 && b.master[b.cur] == Board::P_ZEN;
  if (zenOn_ && !b.balls.empty()) {
    SDL_SetRenderClipRect(r_, &canvasClip);
    if (Tex* yy = img("IMAGE_YINYANG"))
      for (auto& bl : b.balls) putCentered(yy, BX + bl.pos.x, BY + bl.pos.y, {0xffffffffu, Blend::Add}, 1.0f, 0.0628f * (float)(b.frames % 100));
  }
  SDL_SetRenderClipRect(r_, &canvasClip);
  fxDrawEffects(b, 2);
  fxDrawTexts(b, 2);
  s_ = baseS;
  ox_ = baseOx;
  oy_ = baseOy;
  fxDrawEffects(b, 3);
  fxDrawTexts(b, 3);
  if (b.zoomFade > 0) {
    if (inset > 0) {
      s_ = frameS;
      ox_ = frameOx;
      oy_ = frameOy;
    }
    drawZoomFrame(b);
    s_ = baseS;
    ox_ = baseOx;
    oy_ = baseOy;
  }
  if (g_modOverlay) {
    SDL_SetRenderClipRect(r_, &canvasClip);
    g_modOverlay(*this, b);
  }
  if (bloomOn_) bloomPass(ow, oh, canvasClip);
  SDL_SetRenderClipRect(r_, nullptr);
  if (inset > 0) {
    const float x = std::floor((ow - canvasW_ * frameS) * 0.5f);
    const float y = std::floor((oh - 600 * frameS) * 0.5f);
    if (x > 0 || y > 0) {
      const SDL_FRect bars[]{{0, 0, (float)ow, y}, {0, oh - y, (float)ow, y},
                             {0, y, x, oh - 2 * y}, {ow - x, y, x, oh - 2 * y}};
      SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
      if (y > 0) SDL_RenderFillRects(r_, bars, 2);
      if (x > 0) SDL_RenderFillRects(r_, bars + 2, 2);
    }
  }
}

}
