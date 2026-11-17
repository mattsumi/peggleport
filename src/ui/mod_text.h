#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/gfx.h"

namespace pp::ui {

inline std::string plainText(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 4);
  for (char c : s) {
    unsigned char u = (unsigned char)c;
    if (u < 32 || u == 127) o += ' ';
    else if (c == '^') o += "^^";
    else o += c;
  }
  return o;
}

inline float plainWidth(Gfx& g, const std::string& font, const std::string& s) { return g.textWidth(font, plainText(s)); }

inline void drawPlain(Gfx& g, const std::string& font, float x, float y, const std::string& s, uint32_t rgb = 0xffffff, float alpha = 1.0f, Align a = Align::Left) {
  g.drawString(font, x, y, plainText(s), rgb, alpha, a);
}

inline size_t plainPrefix(Gfx& g, const std::string& font, const std::string& s, float width, const std::string& tail = "") {
  size_t lo = 0, hi = s.size();
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    if (plainWidth(g, font, s.substr(0, mid) + tail) <= width) lo = mid;
    else hi = mid - 1;
  }
  while (lo > 0 && lo < s.size() && ((unsigned char)s[lo] & 0xc0) == 0x80) lo--;
  return lo;
}

inline std::string dotted(Gfx& g, const std::string& font, const std::string& s, float width) {
  std::string cut = s.substr(0, plainPrefix(g, font, s, width, "..."));
  while (!cut.empty() && cut.back() == ' ') cut.pop_back();
  return cut + "...";
}

inline std::string fitPlain(Gfx& g, const std::string& font, const std::string& s, float width) {
  if (plainWidth(g, font, s) <= width) return s;
  return dotted(g, font, s, width);
}

inline std::vector<std::string> wrapPlain(Gfx& g, const std::string& font, float width, const std::string& s) {
  std::vector<std::string> lines;
  std::string cur, word;
  auto flush = [&] {
    lines.push_back(cur);
    cur.clear();
  };
  auto addWord = [&](std::string w) {
    while (w.size() > 1 && plainWidth(g, font, w) > width) {
      const std::string lead = cur.empty() ? "" : cur + " ";
      size_t fits = plainPrefix(g, font, lead + w, width);
      size_t cut = fits > lead.size() ? fits - lead.size() : 0;
      if (cut == 0) {
        if (!cur.empty()) {
          flush();
          continue;
        }
        cut = 1;
      }
      size_t slash = w.rfind('/', cut - 1);
      if (slash != std::string::npos && slash + 1 < cut && slash + 1 > cut / 2) cut = slash + 1;
      cur = lead + w.substr(0, cut);
      flush();
      w.erase(0, cut);
    }
    if (w.empty()) return;
    if (!cur.empty() && plainWidth(g, font, cur + " " + w) > width) flush();
    cur += (cur.empty() ? "" : " ") + w;
  };
  for (size_t i = 0; i <= s.size(); i++) {
    if (i == s.size() || s[i] == ' ' || s[i] == '\n') {
      if (!word.empty()) addWord(word);
      word.clear();
      if (i < s.size() && s[i] == '\n') flush();
    } else {
      word.push_back(s[i]);
    }
  }
  lines.push_back(cur);
  return lines;
}

inline std::vector<std::string> wrapPlainMax(Gfx& g, const std::string& font, float width, const std::string& s, size_t maxLines) {
  std::vector<std::string> lines = wrapPlain(g, font, width, s);
  if (maxLines == 0 || lines.size() <= maxLines) return lines;
  lines.resize(maxLines);
  lines.back() = dotted(g, font, lines.back(), width);
  return lines;
}

}
