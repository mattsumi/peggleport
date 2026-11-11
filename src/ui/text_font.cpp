#ifdef PP_UI_HAVE_FONT
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

#include <algorithm>
#include <cmath>
#include <map>

#include "font.h"
#include "ui/gfx.h"
#include "ui/text.h"

namespace pp::ui {
namespace {

class PopcapFonts : public FontProvider {
 public:
  PopcapFonts(SDL_Renderer* r, Gfx* g) : r_(r), g_(g), fm_(g->vfs()) {}
  ~PopcapFonts() override {
    for (auto& [_, t] : tex_)
      if (t) SDL_DestroyTexture(t);
  }
  Font* get(const std::string& name) {
    Font* f = fm_.get("FONT_" + name);
    if (!f) f = fm_.get("FONT_PLAIN");
    return f;
  }
  float height(const std::string& n) override { Font* f = get(n); return f ? (float)f->height() : 12.0f; }
  float ascent(const std::string& n) override { Font* f = get(n); return f ? (float)f->ascent() : 10.0f; }
  float lineSpacing(const std::string& n) override { Font* f = get(n); return f ? (float)f->lineSpacing() : 14.0f; }
  float width(const std::string& n, const std::string& s) override { Font* f = get(n); return f ? (float)f->width(s) : (float)s.size() * 6; }
  bool layerColors(const std::string& n, size_t layer, uint32_t& mult, uint32_t& add) override { Font* f = get(n); return f && f->layerColors(layer, mult, add); }
  void setLayerColors(const std::string& n, size_t layer, uint32_t mult, uint32_t add) override { if (Font* f = get(n)) f->setLayerColors(layer, mult, add); }
  void draw(const std::string& n, float x, float y, const std::string& s, uint32_t rgb, float alpha) override {
    Font* f = get(n);
    if (!f || !r_ || s.empty()) return;
    uint32_t rgba = (rgb << 8) | (uint32_t)std::clamp((int)std::lround(alpha * 255.0f), 0, 255);
    quads_.clear();
    f->layout(s, rgba, quads_);
    float sc = g_->scale();
    for (const GlyphQuad& q : quads_) {
      SDL_Texture* t = texture(q.img);
      if (!t || q.sw <= 0 || q.sh <= 0) continue;
      SDL_FRect src{(float)q.sx, (float)q.sy, (float)q.sw, (float)q.sh};
      SDL_FRect dst{g_->offX() + (x + q.dx) * sc, g_->offY() + (y + q.dy) * sc, q.dw * sc, q.dh * sc};
      SDL_SetTextureColorMod(t, (q.rgba >> 24) & 255, (q.rgba >> 16) & 255, (q.rgba >> 8) & 255);
      SDL_SetTextureAlphaMod(t, q.rgba & 255);
      SDL_SetTextureBlendMode(t, q.mode == 1 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
      SDL_RenderTexture(r_, t, &src, &dst);
    }
  }

 private:
  SDL_Texture* texture(const Image* img) {
    if (!img) return nullptr;
    auto it = tex_.find(img);
    if (it != tex_.end()) return it->second;
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img->w, img->h);
    if (t) {
      SDL_UpdateTexture(t, nullptr, img->rgba.data(), img->w * 4);
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    tex_[img] = t;
    return t;
  }
  SDL_Renderer* r_;
  Gfx* g_;
  FontManager fm_;
  std::map<const Image*, SDL_Texture*> tex_;
  std::vector<GlyphQuad> quads_;
};

}

std::unique_ptr<FontProvider> makePopcapFontProvider(SDL_Renderer* r, Gfx* g) { return std::make_unique<PopcapFonts>(r, g); }

}
#endif
