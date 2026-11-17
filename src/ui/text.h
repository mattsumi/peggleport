#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Renderer;

namespace pp::ui {

enum class Align { Left, Center, Right };

struct TextRun {
  std::string text;
  int32_t rgb = -1;
};

std::vector<TextRun> parseColorCodes(const std::string& s);
std::string stripColorCodes(const std::string& s);

class FontProvider {
 public:
  virtual ~FontProvider() = default;
  virtual float height(const std::string& font) = 0;
  virtual float ascent(const std::string& font) = 0;
  virtual float ascentPadding(const std::string&) { return 0; }
  virtual float lineSpacing(const std::string& font) { return height(font); }
  virtual float width(const std::string& font, const std::string& plain) = 0;
  virtual void draw(const std::string& font, float x, float y, const std::string& plain, uint32_t rgb, float alpha) = 0;
  virtual bool layerColors(const std::string&, size_t, uint32_t&, uint32_t&) { return false; }
  virtual void setLayerColors(const std::string&, size_t, uint32_t, uint32_t) {}
};

std::unique_ptr<FontProvider> makeDebugFontProvider(SDL_Renderer* r, class Gfx* g);
std::unique_ptr<FontProvider> makeDefaultFontProvider(SDL_Renderer* r, class Gfx* g);
#ifdef PP_UI_HAVE_FONT
std::unique_ptr<FontProvider> makePopcapFontProvider(SDL_Renderer* r, class Gfx* g);
#endif

}
