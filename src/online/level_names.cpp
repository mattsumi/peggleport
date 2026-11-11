#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "online/online_ui.h"
#include "ui/stages.h"
#include "ui/text.h"
#include "vfs.h"

namespace pp::ui::online {

namespace {

constexpr size_t kNameBytes = 64;

void dropBrokenTail(std::string& s) {
  size_t k = 0;
  while (k < s.size() && k < 3 && ((unsigned char)s[s.size() - 1 - k] & 0xc0) == 0x80) k++;
  if (k == s.size()) return;
  const unsigned char lead = (unsigned char)s[s.size() - 1 - k];
  const size_t need = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
  if (need > k) s.resize(s.size() - 1 - k);
}

void popChar(std::string& s) {
  while (s.size() > 1 && ((unsigned char)s.back() & 0xc0) == 0x80) s.pop_back();
  if (!s.empty()) s.pop_back();
}

std::string tidy(const std::string& s) {
  std::string t = cleanText(stripColorCodes(s), kNameBytes);
  const size_t a = t.find_first_not_of(' ');
  if (a == std::string::npos) return {};
  return t.substr(a, t.find_last_not_of(' ') - a + 1);
}

std::string ownTitle(const Vfs& vfs, const std::string& file) {
  if (file.empty() || file.size() > 128) return {};
  auto h = vfs.read("levels/" + file + ".ini");
  if (!h) return {};
  auto ini = readIni(std::string(h->bytes.begin(), h->bytes.end()));
  auto it = ini.find("title");
  return it == ini.end() ? std::string() : it->second;
}

std::string titleFor(const App& app, const std::string& file, const LevelInfo* li) {
  std::string t;
  if (li && li->title != li->file) t = tidy(li->title);
  if (t.empty()) t = tidy(ownTitle(app.vfs, file));
  if (t.empty()) t = cleanText(file, kNameBytes);
  return t;
}

}

std::string cleanText(const std::string& s, size_t maxBytes) {
  std::string o;
  bool cut = false;
  for (unsigned char c : s) {
    if (c < 32 || c == 127) continue;
    if (o.size() >= maxBytes) { cut = true; break; }
    o.push_back((char)c);
  }
  if (!cut) return o;
  dropBrokenTail(o);
  return o + "...";
}

std::string drawableText(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    o.push_back(c);
    if (c == '^') o.push_back('^');
  }
  return o;
}

std::string fitText(Gfx& g, const std::string& font, const std::string& s, float width) {
  FontProvider& fp = g.fontProvider();
  if (fp.width(font, s) <= width) return drawableText(s);
  std::string t = s;
  while (!t.empty()) {
    popChar(t);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (fp.width(font, t + "...") <= width) break;
  }
  return drawableText(t) + "...";
}

std::vector<LevelChoice> levelChoices(const App& app) {
  std::vector<LevelChoice> out;
  for (int s = 0; s < app.stages.numAllStages(); s++) {
    const StageInfo* st = app.stages.stage(s);
    if (!st) continue;
    const std::string group = tidy(app.stages.title(s));
    for (const LevelInfo& li : st->levels) out.push_back({li.file, titleFor(app, li.file, &li), group});
  }
  return out;
}

std::string levelTitle(const App& app, const std::string& file) { return titleFor(app, file, app.stages.levelByName(file)); }

bool levelInstalled(const App& app, const std::string& file) { return !file.empty() && file.size() <= 128 && app.vfs.exists("levels/" + file + ".dat"); }

}
