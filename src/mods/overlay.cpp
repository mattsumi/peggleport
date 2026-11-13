#include "mods/overlay.h"

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string>

#include "mods/manager.h"
#include "mods/text.h"
#include "render.h"

namespace pp {

struct ModOverlayAccess {
  struct Sink final : mods::DrawSink {
    Renderer& R;
    explicit Sink(Renderer& r) : R(r) {}
    void canvas(float x, float y, bool screen, float& cx, float& cy) const {
      cx = screen ? R.BX - 75.0f + x : R.BX + x;
      cy = screen ? y : Renderer::BY + y;
    }
    static void color(SDL_Renderer* r, uint32_t c) { SDL_SetRenderDrawColor(r, (Uint8)(c >> 24), (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c); }
    void rect(float x, float y, float w, float h, uint32_t rgba, bool fill, bool screen) override {
      float cx, cy;
      canvas(x, y, screen, cx, cy);
      SDL_FRect r{R.px(cx), R.py(cy), w * R.s_, h * R.s_};
      SDL_SetRenderDrawBlendMode(R.r_, SDL_BLENDMODE_BLEND);
      color(R.r_, rgba);
      if (fill) SDL_RenderFillRect(R.r_, &r);
      else SDL_RenderRect(R.r_, &r);
    }
    void circle(float x, float y, float rad, uint32_t rgba, bool fill, bool screen) override {
      float cx, cy;
      canvas(x, y, screen, cx, cy);
      float px = R.px(cx), py = R.py(cy), pr = rad * R.s_;
      const int N = 28;
      SDL_SetRenderDrawBlendMode(R.r_, SDL_BLENDMODE_BLEND);
      if (fill) {
        SDL_FColor c{((rgba >> 24) & 255) / 255.0f, ((rgba >> 16) & 255) / 255.0f, ((rgba >> 8) & 255) / 255.0f, (rgba & 255) / 255.0f};
        SDL_Vertex v[N + 1];
        int idx[N * 3];
        v[0] = {{px, py}, c, {0, 0}};
        for (int i = 0; i < N; i++) {
          float a = 6.2831853f * (float)i / N;
          v[i + 1] = {{px + std::cos(a) * pr, py + std::sin(a) * pr}, c, {0, 0}};
          idx[i * 3] = 0;
          idx[i * 3 + 1] = i + 1;
          idx[i * 3 + 2] = (i + 1) % N + 1;
        }
        SDL_RenderGeometry(R.r_, nullptr, v, N + 1, idx, N * 3);
      } else {
        SDL_FPoint pts[N + 1];
        for (int i = 0; i <= N; i++) {
          float a = 6.2831853f * (float)i / N;
          pts[i] = {px + std::cos(a) * pr, py + std::sin(a) * pr};
        }
        color(R.r_, rgba);
        SDL_RenderLines(R.r_, pts, N + 1);
      }
    }
    void text(float x, float y, const std::string& s, uint32_t rgba, int align, float scale, bool screen) override {
      Font* f = R.font("FONT_PLAIN");
      if (!f) f = R.font("FONT_SMALL_DISP");
      if (!f) return;
      float cx, cy;
      canvas(x, y, screen, cx, cy);
      R.drawText(f, cx, cy, mods::gameText(s), rgba, align == 1 ? TextAlign::Center : align == 2 ? TextAlign::Right : TextAlign::Left, scale <= 0 ? 1.0f : scale);
    }
    void image(const std::string& path, float x, float y, float scale, float alpha, bool screen) override {
      Renderer::Tex* t = R.tex(path);
      if (!t) return;
      float cx, cy;
      canvas(x, y, screen, cx, cy);
      uint32_t a = (uint32_t)(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
      Paint p;
      p.rgba = 0xffffff00u | a;
      R.put(t, SDL_FRect{0, 0, t->lw(), t->lh()}, cx, cy, p, t->lw() * scale, t->lh() * scale);
    }
  };
  static void run(Renderer& R, const Board& b) {
    if (!mods::g_mods) return;
    Sink s(R);
    mods::g_mods->frame(s, b);
  }
};

namespace mods {
void installOverlay() { g_modOverlay = &ModOverlayAccess::run; }
}

}
