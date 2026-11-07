#include "ui/text.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <cstdlib>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

#include "ui/gfx.h"

namespace pp::ui {

std::vector<TextRun> parseColorCodes(const std::string& s) {
  std::vector<TextRun> out;
  TextRun cur;
  int32_t color = -1;
  auto push = [&] {
    if (!cur.text.empty()) out.push_back(cur);
    cur = TextRun();
    cur.rgb = color;
  };
  cur.rgb = -1;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '^') {
      if (i + 1 < s.size() && s[i + 1] == '^') {
        cur.text.push_back('^');
        i++;
        continue;
      }
      if (s.compare(i, 8, "^oldclr^") == 0) {
        push();
        color = -1;
        cur.rgb = -1;
        i += 7;
        continue;
      }
      if (i + 7 < s.size() + 0 && s[i + 7] == '^') {
        bool hex = true;
        for (int k = 1; k <= 6; k++) hex = hex && std::isxdigit((unsigned char)s[i + k]);
        if (hex) {
          push();
          color = (int32_t)std::strtoul(s.substr(i + 1, 6).c_str(), nullptr, 16);
          cur.rgb = color;
          i += 7;
          continue;
        }
      }
    }
    cur.text.push_back(s[i]);
  }
  push();
  return out;
}

std::string stripColorCodes(const std::string& s) {
  std::string r;
  for (auto& run : parseColorCodes(s)) r += run.text;
  return r;
}

namespace {
class DebugFonts : public FontProvider {
 public:
  DebugFonts(SDL_Renderer* r, Gfx* g) : r_(r), g_(g) {}
  float px(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    float v = 12;
    std::string id = "FONT_" + name;
    for (int guard = 0; guard < 6; guard++) {
      const ResFont* f = g_->man().font(id);
      if (!f) break;
      if (!f->ref.empty()) { id = f->ref; continue; }
      std::string p = f->path;
      std::string digits;
      for (size_t i = p.find('.') == std::string::npos ? p.size() : p.find('.'); i-- > 0;) {
        if (std::isdigit((unsigned char)p[i])) digits.insert(digits.begin(), p[i]);
        else if (!digits.empty()) break;
      }
      if (!digits.empty()) v = (float)std::atoi(digits.c_str());
      else if (p.find("tahoma") != std::string::npos || p.find("Tahoma") != std::string::npos) v = 8;
      break;
    }
    return cache_[name] = v;
  }
  float height(const std::string& f) override { return std::round(px(f) * 1.15f); }
  float ascent(const std::string& f) override { return std::round(px(f) * 0.95f); }
  float width(const std::string& f, const std::string& s) override { return std::round(px(f) * 0.56f * s.size()); }
  void draw(const std::string& f, float x, float y, const std::string& s, uint32_t rgb, float alpha) override {
    if (!r_ || s.empty()) return;
    float h = px(f);
    float sy = h * 0.8f / 8.0f * g_->scale(), sx = h * 0.56f / 8.0f * g_->scale();
    SDL_Rect clip{};
    bool clipped = SDL_RenderClipEnabled(r_);
    if (clipped) {
      SDL_GetRenderClipRect(r_, &clip);
      SDL_Rect c2{(int)(clip.x / sx), (int)(clip.y / sy), (int)(clip.w / sx) + 1, (int)(clip.h / sy) + 1};
      SDL_SetRenderClipRect(r_, &c2);
    }
    float X = g_->offX() + x * g_->scale(), Y = g_->offY() + (y - h * 0.78f) * g_->scale();
    SDL_SetRenderScale(r_, sx, sy);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    if (f.find("OUTLINE") != std::string::npos || f.find("DISP") != std::string::npos) {
      SDL_SetRenderDrawColor(r_, 0, 0, 0, (Uint8)(alpha * 230));
      for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
          if (dx || dy) SDL_RenderDebugText(r_, X / sx + dx * 0.6f, Y / sy + dy * 0.6f, s.c_str());
    }
    SDL_SetRenderDrawColor(r_, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (Uint8)(alpha * 255));
    SDL_RenderDebugText(r_, X / sx, Y / sy, s.c_str());
    SDL_SetRenderScale(r_, 1, 1);
    if (clipped) SDL_SetRenderClipRect(r_, &clip);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
  }

 private:
  SDL_Renderer* r_;
  Gfx* g_;
  std::map<std::string, float> cache_;
};
}

std::unique_ptr<FontProvider> makeDebugFontProvider(SDL_Renderer* r, Gfx* g) { return std::make_unique<DebugFonts>(r, g); }

std::unique_ptr<FontProvider> makeDefaultFontProvider(SDL_Renderer* r, Gfx* g) {
#ifdef PP_UI_HAVE_FONT
  return makePopcapFontProvider(r, g);
#else
  return makeDebugFontProvider(r, g);
#endif
}

}
