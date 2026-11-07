#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "image.h"
#include "vfs.h"

namespace pp {

enum class TextAlign { Left, Center, Right };

constexpr uint32_t RGBA(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) { return (r << 24) | (g << 16) | (b << 8) | a; }

struct FontLayer;
struct FontData;

struct GlyphQuad {
  const Image* img = nullptr;
  int sx = 0, sy = 0, sw = 0, sh = 0;
  int dx = 0, dy = 0;
  int dw = 0, dh = 0;
  uint32_t rgba = 0xffffffff;
  int mode = -1;
};

class Font {
 public:
  Font() = default;
  static std::shared_ptr<Font> load(const Vfs& vfs, const std::string& descriptorPath, std::string* err = nullptr);

  bool valid() const { return data_ != nullptr; }
  int height() const;
  int ascent() const;
  int ascentPadding() const;
  int descent() const { return height() - ascent(); }
  int lineSpacing() const;
  int pointSize() const { return pointSize_; }
  int defaultPointSize() const;

  int charWidth(unsigned char c, unsigned char prev = 0) const;
  int width(std::string_view s) const;
  int widthColored(std::string_view s) const;

  std::shared_ptr<Font> withTags(const std::vector<std::string>& tags) const;
  std::shared_ptr<Font> withPointSize(int pt) const;
  std::shared_ptr<Font> withScale(double s) const;
  bool hasTag(const std::string& t) const;
  std::string define(const std::string& name) const;

  void layout(std::string_view s, uint32_t rgba, std::vector<GlyphQuad>& out, int* outWidth = nullptr) const;
  void layoutColored(std::string_view s, uint32_t rgba, std::vector<GlyphQuad>& out, int* outWidth = nullptr, uint32_t* state = nullptr) const;

  struct Line { int start = 0, len = 0, y = 0; };
  int wrap(std::string_view s, int wrapWidth, int lineSpacing, std::vector<Line>& lines, int* maxWidth = nullptr) const;

  size_t layerCount() const;
  size_t activeLayerCount() const;
  const FontData* fontData() const { return data_.get(); }

 private:
  void prepare() const;
  std::shared_ptr<FontData> data_;
  int pointSize_ = 0;
  double scale_ = 1.0;
  std::vector<std::string> tags_;
  mutable bool activeValid_ = false;
  mutable std::vector<const FontLayer*> active_;
  mutable int ascent_ = 0, height_ = 0, ascentPadding_ = 0, lineSpacingOffset_ = 0;
  friend struct FontData;
};

class FontManager {
 public:
  explicit FontManager(const Vfs& v) : vfs_(v) {}
  Font* get(const std::string& id);
  bool has(const std::string& id);
  std::vector<std::string> ids();
  void reset();

 private:
  struct Res { std::string id, path, tags; };
  void loadManifest();
  const Vfs& vfs_;
  bool manifest_ = false;
  std::map<std::string, Res> res_;
  std::map<std::string, std::shared_ptr<Font>> fonts_;
};

}
