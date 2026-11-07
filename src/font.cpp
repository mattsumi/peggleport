#include "font.h"

#include "log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <regex>
#include <set>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <map>

namespace pp {

namespace {

std::string upper(std::string s) {
  for (auto& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}
bool ieq(const std::string& a, const char* b) {
  size_t n = std::strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; i++)
    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  return true;
}

bool toInt(const std::string& s, int* out) {
  *out = 0;
  if (s.empty()) return false;
  int radix = 10;
  bool neg = false;
  size_t i = 0;
  if (s[0] == '-') { neg = true; i++; }
  for (; i < s.size(); i++) {
    char c = s[i];
    if (radix == 10 && c >= '0' && c <= '9') *out = *out * 10 + (c - '0');
    else if (radix == 16 && std::isxdigit((unsigned char)c)) *out = *out * 16 + (c <= '9' ? c - '0' : (std::toupper((unsigned char)c) - 'A') + 10);
    else if ((c == 'x' || c == 'X') && i == 1 && *out == 0) radix = 16;
    else { *out = 0; return false; }
  }
  if (neg) *out = -*out;
  return true;
}
bool toDouble(const std::string& s, double* out) {
  *out = 0;
  if (s.empty()) return false;
  size_t i = 0;
  bool neg = false;
  if (s[0] == '-') { neg = true; i++; }
  for (; i < s.size(); i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') *out = *out * 10 + (c - '0');
    else if (c == '.') { i++; break; }
    else { *out = 0; return false; }
  }
  double f = 0.1;
  for (; i < s.size(); i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') { *out += (c - '0') * f; f /= 10; }
    else { *out = 0; return false; }
  }
  if (neg) *out = -*out;
  return true;
}

struct DE {
  bool isList = false;
  std::string s;
  std::vector<DE> v;
};

bool isImmediate(const std::string& s) {
  if (s.empty()) return false;
  char c = s[0];
  return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '\'' || c == '"';
}
std::string unquote(const std::string& q) {
  if (q.empty() || (q[0] != '\'' && q[0] != '"')) return q;
  char qc = q[0];
  std::string out;
  bool last = true, esc = false;
  for (size_t i = 1; i + 1 < q.size(); i++) {
    char c = q[i];
    if (esc) { out += c; esc = false; }
    else if (c == qc) {
      if (last) out += qc;
      last = true;
    } else {
      if (c == '\\') esc = true;
      else out += c;
      last = false;
    }
  }
  return out;
}

std::vector<std::string> splitCommands(const std::vector<uint8_t>& bytes) {
  std::vector<std::string> lines;
  size_t pos = 0, n = bytes.size();
  std::string cur;
  while (pos < n) {
    bool skipLine = false, atLineStart = true, inS = false, inD = false, escaped = false;
    for (;;) {
      if (pos >= n) break;
      int c = bytes[pos++];
      if (c == '\r') continue;
      if (!atLineStart || (c != ' ' && c != '\t' && c != '\n')) {
        if (atLineStart) {
          if (c == '#') skipLine = true;
          atLineStart = false;
        }
        if (c == '\n') atLineStart = true;
        if (c == '\n' && skipLine) skipLine = false;
        else if (!skipLine) {
          if (c == '\\' && (inS || inD) && !escaped) escaped = true;
          else {
            if (c == '\'' && !inD && !escaped) inS = !inS;
            if (c == '"' && !inS && !escaped) inD = !inD;
            if (c == ';' && !inS && !inD) break;
            if (escaped) { cur += '\\'; escaped = false; }
            cur += (char)c;
          }
        }
      }
    }
    if (!cur.empty()) {
      lines.push_back(cur);
      cur.clear();
    }
  }
  return lines;
}

bool parseToList(const std::string& s, DE& list, bool expectEnd, size_t& pos, std::string& err) {
  bool inS = false, inD = false, escaped = false;
  size_t curIdx = (size_t)-1;
  while (pos < s.size()) {
    bool add = false;
    char c = s[pos++];
    bool sep = c == ' ' || c == '\t' || c == '\n' || c == ',';
    bool wasEscaped = false;
    if (escaped) { add = true; escaped = false; wasEscaped = true; }
    else {
      if (c == '\'' && !inD) inS = !inS;
      else if (c == '"' && !inS) inD = !inD;
      if (c == '\\') escaped = true;
      else if (!inS && !inD) {
        if (c == ')') {
          if (expectEnd) return true;
          err = "Unexpected List End";
          return false;
        } else if (c == '(') {
          if (curIdx != (size_t)-1) { err = "Unexpected List Start"; return false; }
          DE child;
          child.isList = true;
          if (!parseToList(s, child, true, pos, err)) return false;
          list.v.push_back(std::move(child));
        } else if (sep) curIdx = (size_t)-1;
        else add = true;
      } else add = true;
    }
    if (add) {
      if (curIdx == (size_t)-1) {
        list.v.emplace_back();
        curIdx = list.v.size() - 1;
      }
      if (wasEscaped) list.v[curIdx].s += '\\';
      list.v[curIdx].s += c;
    }
  }
  if (inS) { err = "Unterminated Single Quotes"; return false; }
  if (inD) { err = "Unterminated Double Quotes"; return false; }
  if (expectEnd) { err = "Unterminated List"; return false; }
  return true;
}

}

struct CharData {
  int rx = 0, ry = 0, rw = 0, rh = 0;
  int ox = 0, oy = 0;
  int8_t kern[256]{};
  int width = 0, order = 0;
};
struct RGBAi { int r = 255, g = 255, b = 255, a = 255; };

struct FontLayer {
  std::vector<std::string> required, excluded;
  CharData ch[256];
  RGBAi mult{255, 255, 255, 255}, add{0, 0, 0, 0};
  std::shared_ptr<Image> image;
  int drawMode = -1;
  int offX = 0, offY = 0;
  int spacing = 0, minPt = -1, maxPt = -1, pointSize = 0;
  int ascent = 0, ascentPadding = 0, height = 0, defaultHeight = 0, lineSpacingOffset = 0, baseOrder = 0;
};

struct FontData {
  const Vfs* vfs = nullptr;
  bool ok = false;
  std::string source, error;
  int defaultPointSize = 0;
  uint8_t charMap[256];
  std::list<FontLayer> layers;
  std::map<std::string, FontLayer*> layerMap;
  std::map<std::string, DE> defines;
  std::map<std::string, std::shared_ptr<Image>> images;

  FontData() { for (int i = 0; i < 256; i++) charMap[i] = (uint8_t)i; }
  bool fail(const std::string& e) { if (error.empty()) error = e; return false; }

  const DE* deref(const std::string& n) const {
    auto it = defines.find(upper(n));
    return it == defines.end() ? nullptr : &it->second;
  }
  bool getValues(const DE& src, DE& out) {
    out.v.clear();
    out.isList = true;
    for (auto& e : src.v) {
      if (e.isList) {
        DE child;
        if (!getValues(e, child)) return false;
        out.v.push_back(std::move(child));
      } else if (!e.s.empty()) {
        DE d;
        if (e.s[0] == '\'' || e.s[0] == '"') d.s = unquote(e.s);
        else if (isImmediate(e.s)) d.s = e.s;
        else {
          auto* r = deref(e.s);
          if (!r) return fail("Unable to Dereference \"" + e.s + "\"");
          d = *r;
        }
        out.v.push_back(std::move(d));
      }
    }
    return true;
  }
  bool toStrings(const DE& src, std::vector<std::string>& out) {
    out.clear();
    DE tmp;
    const DE* vals;
    if (src.isList) {
      if (!getValues(src, tmp)) return false;
      vals = &tmp;
    } else {
      auto* r = deref(src.s);
      if (!r) return fail("Unable to Dereference \"" + src.s + "\"");
      if (!r->isList) return false;
      vals = r;
    }
    for (auto& e : vals->v) {
      if (e.isList) { out.clear(); return false; }
      out.push_back(e.s);
    }
    return true;
  }
  bool toInts(const DE& src, std::vector<int>& out) {
    out.clear();
    std::vector<std::string> sv;
    if (!toStrings(src, sv)) return false;
    for (auto& s : sv) {
      int v;
      if (!toInt(s, &v)) return false;
      out.push_back(v);
    }
    return true;
  }
  bool toDoubles(const DE& src, std::vector<double>& out) {
    out.clear();
    std::vector<std::string> sv;
    if (!toStrings(src, sv)) return false;
    for (auto& s : sv) {
      double v;
      if (!toDouble(s, &v)) return false;
      out.push_back(v);
    }
    return true;
  }
  bool toList(const DE& src, DE& out) {
    if (src.isList) return getValues(src, out);
    auto* r = deref(src.s);
    if (!r || !r->isList) return false;
    out = *r;
    return true;
  }
  bool toString(const DE& src, std::string& out) {
    out.clear();
    if (src.isList) return false;
    if (auto* r = deref(src.s)) {
      if (r->isList) return false;
      out = unquote(r->s);
    } else out = unquote(src.s);
    return true;
  }
  bool toLayer(const DE& src, FontLayer** out) {
    *out = nullptr;
    if (src.isList) return false;
    auto it = layerMap.find(upper(src.s));
    if (it == layerMap.end()) return fail("Undefined Layer");
    *out = it->second;
    return true;
  }
  bool toColor(const DE& e, RGBAi& c) {
    if (e.isList) {
      std::vector<double> f;
      if (!toDoubles(e, f) || f.size() != 4) return false;
      c = {(int)(f[0] * 255), (int)(f[1] * 255), (int)(f[2] * 255), (int)(f[3] * 255)};
      return true;
    }
    int v = 0;
    if (!toInt(e.s, &v)) return false;
    c = {(v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255};
    if (c.a == 0) c.a = 255;
    return true;
  }
  std::shared_ptr<Image> loadImage(const std::string& name) {
    std::string dir;
    auto sl = source.find_last_of("/\\");
    if (sl != std::string::npos) dir = source.substr(0, sl + 1);
    std::string path = dir + name;
    std::string key = upper(path);
    auto it = images.find(key);
    if (it != images.end()) return it->second;
    std::shared_ptr<Image> img;
    if (vfs)
      if (auto im = loadGameImage(*vfs, path)) img = std::make_shared<Image>(std::move(*im));
    images[key] = img;
    return img;
  }

  bool handle(const DE& cmdList);
  bool loadFile(const Vfs& v, const std::string& path);
};

bool FontData::handle(const DE& P) {
  const auto& p = P.v;
  std::string cmd = p[0].s;
  bool badNum = false, badFmt = false, sizeMismatch = false;
  auto single = [&](size_t i) { return !p[i].isList; };

  if (ieq(cmd, "Define")) {
    if (p.size() == 3) {
      if (single(1)) {
        std::string name = upper(p[1].s);
        if (!isImmediate(name)) {
          defines.erase(name);
          if (p[2].isList) {
            DE vals;
            if (!getValues(p[2], vals)) return false;
            defines[name] = std::move(vals);
          } else {
            if (auto* d = deref(p[2].s)) { DE copy = *d; defines[name] = std::move(copy); }
            else defines[name] = p[2];
          }
        } else badFmt = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "CreateHorzSpanRectList")) {
    if (p.size() == 4) {
      std::vector<int> r, w;
      if (single(1) && toInts(p[2], r) && r.size() == 4 && toInts(p[3], w)) {
        DE list;
        list.isList = true;
        int x = 0;
        for (int wv : w) {
          DE rect;
          rect.isList = true;
          for (int val : {r[0] + x, r[1], wv, r[3]}) {
            DE e;
            e.s = std::to_string(val);
            rect.v.push_back(e);
          }
          list.v.push_back(std::move(rect));
          x += wv;
        }
        defines[upper(p[1].s)] = std::move(list);
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "SetDefaultPointSize")) {
    if (p.size() == 2) {
      int v;
      if (single(1) && toInt(p[1].s, &v)) defaultPointSize = v;
      else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "SetCharMap")) {
    if (p.size() == 3) {
      std::vector<std::string> from, to;
      if (toStrings(p[1], from) && toStrings(p[2], to)) {
        if (from.size() == to.size()) {
          for (size_t i = 0; i < from.size(); i++) {
            if (from[i].size() == 1 && to[i].size() == 1) charMap[(uint8_t)from[i][0]] = (uint8_t)to[i][0];
            else badFmt = true;
          }
        } else sizeMismatch = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "CreateLayer")) {
    if (p.size() == 2) {
      if (single(1)) {
        layers.emplace_back();
        if (!layerMap.insert({upper(p[1].s), &layers.back()}).second) return fail("Layer Already Exists");
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "CreateLayerFrom")) {
    if (p.size() == 3) {
      FontLayer* src;
      if (single(1) && toLayer(p[2], &src)) {
        FontLayer copy = *src;
        layers.push_back(std::move(copy));
        if (!layerMap.insert({upper(p[1].s), &layers.back()}).second) return fail("Layer Already Exists");
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerRequireTags") || ieq(cmd, "LayerExcludeTags")) {
    if (p.size() == 3) {
      FontLayer* L;
      std::vector<std::string> tags;
      if (toLayer(p[1], &L) && toStrings(p[2], tags)) {
        for (auto& t : tags) (ieq(cmd, "LayerRequireTags") ? L->required : L->excluded).push_back(upper(t));
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerPointRange")) {
    if (p.size() == 4) {
      FontLayer* L;
      int a, b;
      if (toLayer(p[1], &L) && single(2) && single(3)) {
        if (toInt(p[2].s, &a) && toInt(p[3].s, &b)) { L->minPt = a; L->maxPt = b; }
        else badFmt = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetPointSize") || ieq(cmd, "LayerSetHeight") || ieq(cmd, "LayerSetAscent") ||
             ieq(cmd, "LayerSetAscentPadding") || ieq(cmd, "LayerSetLineSpacingOffset") || ieq(cmd, "LayerSetSpacing") ||
             ieq(cmd, "LayerSetBaseOrder")) {
    if (p.size() == 3) {
      FontLayer* L;
      int v;
      if (toLayer(p[1], &L) && single(2)) {
        if (toInt(p[2].s, &v)) {
          if (ieq(cmd, "LayerSetPointSize")) L->pointSize = v;
          else if (ieq(cmd, "LayerSetHeight")) L->height = v;
          else if (ieq(cmd, "LayerSetAscent")) L->ascent = v;
          else if (ieq(cmd, "LayerSetAscentPadding")) L->ascentPadding = v;
          else if (ieq(cmd, "LayerSetLineSpacingOffset")) L->lineSpacingOffset = v;
          else if (ieq(cmd, "LayerSetSpacing")) L->spacing = v;
          else L->baseOrder = v;
        } else badFmt = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetImage")) {
    if (p.size() == 3) {
      FontLayer* L;
      std::string name;
      if (toLayer(p[1], &L) && toString(p[2], name)) {
        L->image = loadImage(name);
        if (!L->image) pp::log::writeOnce(pp::log::Level::Warn, "game", "font " + source + ": image '" + name + "' not found");
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetDrawMode")) {
    if (p.size() == 3) {
      FontLayer* L;
      int m;
      if (toLayer(p[1], &L) && single(2) && toInt(p[2].s, &m) && m >= 0 && m <= 1) L->drawMode = m;
      else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetColorMult") || ieq(cmd, "LayerSetColorAdd")) {
    if (p.size() == 3) {
      FontLayer* L;
      if (toLayer(p[1], &L)) {
        if (!toColor(p[2], ieq(cmd, "LayerSetColorMult") ? L->mult : L->add)) badFmt = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetOffset")) {
    if (p.size() == 3) {
      FontLayer* L;
      std::vector<int> o;
      if (toLayer(p[1], &L) && toInts(p[2], o) && o.size() == 2) { L->offX = o[0]; L->offY = o[1]; }
      else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetCharWidths")) {
    if (p.size() == 4) {
      FontLayer* L;
      std::vector<std::string> chars;
      std::vector<int> w;
      if (toLayer(p[1], &L) && toStrings(p[2], chars) && toInts(p[3], w)) {
        if (chars.size() == w.size()) {
          for (size_t i = 0; i < chars.size(); i++) {
            if (chars[i].size() == 1) L->ch[(uint8_t)chars[i][0]].width = w[i];
            else badFmt = true;
          }
        } else sizeMismatch = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetImageMap")) {
    if (p.size() == 4) {
      FontLayer* L;
      std::vector<std::string> chars;
      DE rects;
      if (toLayer(p[1], &L) && toStrings(p[2], chars) && toList(p[3], rects)) {
        if (chars.size() == rects.v.size()) {
          int iw = L->image ? L->image->w : 0, ih = L->image ? L->image->h : 0;
          for (size_t i = 0; i < chars.size(); i++) {
            std::vector<int> r;
            if (chars[i].size() == 1 && toInts(rects.v[i], r) && r.size() == 4) {
              if (L->image && (r[0] < 0 || r[1] < 0 || r[0] + r[2] > iw || r[1] + r[3] > ih)) {
                pp::log::writeOnce(pp::log::Level::Warn, "game", "font " + source + ": image rectangle out of bounds for '" + chars[i] + "'");
                r[0] = std::max(0, std::min(r[0], iw));
                r[1] = std::max(0, std::min(r[1], ih));
                r[2] = std::max(0, std::min(r[2], iw - r[0]));
                r[3] = std::max(0, std::min(r[3], ih - r[1]));
              }
              auto& cd = L->ch[(uint8_t)chars[i][0]];
              cd.rx = r[0]; cd.ry = r[1]; cd.rw = r[2]; cd.rh = r[3];
            } else badFmt = true;
          }
          L->defaultHeight = 0;
          for (int c = 0; c < 256; c++)
            if (L->ch[c].rh + L->ch[c].oy > L->defaultHeight) L->defaultHeight = L->ch[c].rh + L->ch[c].oy;
        } else sizeMismatch = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetCharOffsets")) {
    if (p.size() == 4) {
      FontLayer* L;
      std::vector<std::string> chars;
      DE offs;
      if (toLayer(p[1], &L) && toStrings(p[2], chars) && toList(p[3], offs)) {
        if (chars.size() == offs.v.size()) {
          for (size_t i = 0; i < chars.size(); i++) {
            std::vector<int> o;
            if (chars[i].size() == 1 && toInts(offs.v[i], o) && o.size() == 2) {
              L->ch[(uint8_t)chars[i][0]].ox = o[0];
              L->ch[(uint8_t)chars[i][0]].oy = o[1];
            } else badFmt = true;
          }
        } else sizeMismatch = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetKerningPairs")) {
    if (p.size() == 4) {
      FontLayer* L;
      std::vector<std::string> pairs;
      std::vector<int> offs;
      if (toLayer(p[1], &L) && toStrings(p[2], pairs) && toInts(p[3], offs)) {
        if (pairs.size() == offs.size()) {
          for (size_t i = 0; i < pairs.size(); i++) {
            if (pairs[i].size() == 2) L->ch[(uint8_t)pairs[i][0]].kern[(uint8_t)pairs[i][1]] = (int8_t)offs[i];
            else badFmt = true;
          }
        } else sizeMismatch = true;
      } else badFmt = true;
    } else badNum = true;
  } else if (ieq(cmd, "LayerSetExInfo")) {
    if (p.size() != 4) badNum = true;
  } else if (ieq(cmd, "LayerSetAlphaCorrection")) {
    if (p.size() != 3) badNum = true;
  } else if (ieq(cmd, "LayerSetCharOrders")) {
    if (p.size() == 4) {
      FontLayer* L;
      std::vector<std::string> chars;
      std::vector<int> ord;
      if (toLayer(p[1], &L) && toStrings(p[2], chars) && toInts(p[3], ord)) {
        if (chars.size() == ord.size()) {
          for (size_t i = 0; i < chars.size(); i++) {
            if (chars[i].size() == 1) L->ch[(uint8_t)chars[i][0]].order = ord[i];
            else badFmt = true;
          }
        } else sizeMismatch = true;
      } else badFmt = true;
    } else badNum = true;
  } else {
    return fail("Unknown Command " + cmd);
  }
  if (badNum) return fail("Invalid Number of Parameters (" + cmd + ")");
  if (badFmt) return fail("Invalid Paramater Type (" + cmd + ")");
  if (sizeMismatch) return fail("List Size Mismatch (" + cmd + ")");
  return true;
}

bool FontData::loadFile(const Vfs& v, const std::string& path) {
  vfs = &v;
  source = path;
  auto hit = v.read(path);
  if (!hit) return fail("font descriptor not found: " + path);
  bool bad = false;
  for (auto& line : splitCommands(hit->bytes)) {
    DE params;
    params.isList = true;
    size_t pos = 0;
    std::string err;
    if (!parseToList(line, params, false, pos, err)) { fail(err + " in " + path + ": " + line.substr(0, 60)); bad = true; break; }
    if (params.v.empty()) continue;
    if (params.v[0].isList) { fail("Missing Command"); bad = true; break; }
    if (!handle(params)) { error = path + ": " + error + "  [" + line.substr(0, 60) + "]"; bad = true; break; }
  }
  ok = !bad;
  return ok;
}

std::shared_ptr<Font> Font::load(const Vfs& vfs, const std::string& path, std::string* err) {
  auto data = std::make_shared<FontData>();
  if (!data->loadFile(vfs, path)) {
    if (err) *err = data->error;
    return nullptr;
  }
  auto f = std::make_shared<Font>();
  f->data_ = data;
  f->pointSize_ = data->defaultPointSize;
  return f;
}

size_t Font::layerCount() const { return data_ ? data_->layers.size() : 0; }
size_t Font::activeLayerCount() const { prepare(); return active_.size(); }
int Font::defaultPointSize() const { return data_ ? data_->defaultPointSize : 0; }

void Font::prepare() const {
  if (activeValid_) return;
  activeValid_ = true;
  active_.clear();
  ascent_ = height_ = ascentPadding_ = lineSpacingOffset_ = 0;
  if (!data_ || !data_->ok) return;
  bool first = true;
  for (auto& L : data_->layers) {
    if (!(pointSize_ >= L.minPt && (pointSize_ <= L.maxPt || L.maxPt == -1))) continue;
    bool act = true;
    for (auto& t : L.required)
      if (std::find(tags_.begin(), tags_.end(), t) == tags_.end()) act = false;
    for (auto& t : tags_)
      if (std::find(L.excluded.begin(), L.excluded.end(), t) != L.excluded.end()) act = false;
    if (!act) continue;
    active_.push_back(&L);
    double layerPt = 1, pt = scale_;
    if (!(scale_ == 1.0 && (L.pointSize == 0 || pointSize_ == L.pointSize))) {
      if (L.pointSize != 0) {
        layerPt = L.pointSize;
        pt = pointSize_ * scale_;
      }
    }
    int a = (int)((L.ascent * pt) / layerPt);
    if (a > ascent_) ascent_ = a;
    int h = (int)(((L.height != 0 ? L.height : L.defaultHeight) * pt) / layerPt);
    if (h > height_) height_ = h;
    int ap = (int)((L.ascentPadding * pt) / layerPt);
    if (first || ap < ascentPadding_) ascentPadding_ = ap;
    int ls = (int)((L.lineSpacingOffset * pt) / layerPt);
    if (first || ls > lineSpacingOffset_) lineSpacingOffset_ = ls;
    first = false;
  }
}

int Font::height() const { prepare(); return height_; }
int Font::ascent() const { prepare(); return ascent_; }
int Font::ascentPadding() const { prepare(); return ascentPadding_; }
int Font::lineSpacing() const { prepare(); return height_ + lineSpacingOffset_; }

int Font::charWidth(unsigned char c, unsigned char prev) const {
  prepare();
  if (!data_) return 0;
  c = data_->charMap[c];
  if (prev != 0) prev = data_->charMap[prev];
  int maxX = 0;
  double pt = pointSize_ * scale_;
  for (auto* L : active_) {
    int lpt = L->pointSize, cw, sp;
    if (lpt == 0) {
      cw = (int)(L->ch[c].width * scale_);
      sp = prev != 0 ? (int)((L->spacing + L->ch[prev].kern[c]) * scale_) : 0;
    } else {
      cw = (int)(L->ch[c].width * pt / lpt);
      sp = prev != 0 ? (int)((L->spacing + L->ch[prev].kern[c]) * pt / lpt) : 0;
    }
    maxX = std::max(maxX, cw + sp);
  }
  return maxX;
}

int Font::width(std::string_view s) const {
  int w = 0;
  unsigned char prev = 0;
  for (unsigned char c : s) {
    w += charWidth(c, prev);
    prev = c;
  }
  return w;
}

int Font::widthColored(std::string_view s) const {
  int total = 0;
  std::string seg;
  size_t n = s.size();
  for (size_t i = 0; i < n; i++) {
    if (s[i] == '^') {
      if (i + 1 < n && s[i + 1] == '^') { seg += '^'; i++; }
      else if ((long)i > (long)n - 8) break;
      else {
        i += 7;
        total += width(seg);
        seg.clear();
      }
    } else seg += s[i];
  }
  return total + width(seg);
}

std::shared_ptr<Font> Font::withTags(const std::vector<std::string>& tags) const {
  auto f = std::make_shared<Font>(*this);
  for (auto& t : tags) {
    std::string u = upper(t);
    if (std::find(f->tags_.begin(), f->tags_.end(), t) == f->tags_.end()) f->tags_.push_back(u);
  }
  f->activeValid_ = false;
  return f;
}
std::shared_ptr<Font> Font::withPointSize(int pt) const {
  auto f = std::make_shared<Font>(*this);
  f->pointSize_ = pt;
  f->activeValid_ = false;
  return f;
}
std::shared_ptr<Font> Font::withScale(double s) const {
  auto f = std::make_shared<Font>(*this);
  f->scale_ = s;
  f->activeValid_ = false;
  return f;
}
bool Font::hasTag(const std::string& t) const { return std::find(tags_.begin(), tags_.end(), t) != tags_.end(); }
std::string Font::define(const std::string& name) const {
  if (!data_) return "";
  auto* d = data_->deref(name);
  if (!d) return "";
  std::function<std::string(const DE&)> str = [&](const DE& e) -> std::string {
    if (!e.isList) return e.s;
    std::string r = "(";
    for (size_t i = 0; i < e.v.size(); i++) r += (i ? ", " : "") + str(e.v[i]);
    return r + ")";
  };
  return str(*d);
}

void Font::layout(std::string_view s, uint32_t rgba, std::vector<GlyphQuad>& out, int* outWidth) const {
  out.clear();
  if (outWidth) *outWidth = 0;
  if (!data_ || !data_->ok) return;
  prepare();
  const int cr = (rgba >> 24) & 255, cg = (rgba >> 16) & 255, cb = (rgba >> 8) & 255, ca = rgba & 255;
  struct Cmd { GlyphQuad q; int order; };
  std::vector<Cmd> cmds;
  int curX = 0;
  for (size_t i = 0; i < s.size(); i++) {
    uint8_t ch = data_->charMap[(uint8_t)s[i]];
    uint8_t next = 0;
    if (i + 1 < s.size()) next = data_->charMap[(uint8_t)s[i + 1]];
    int maxX = curX;
    for (auto* L : active_) {
      int layerX = curX, imgX, imgY, cw, sp;
      int lpt = L->pointSize;
      double aScale = scale_;
      if (lpt != 0) aScale *= (double)(pointSize_ / lpt);
      const CharData& cd = L->ch[ch];
      if (aScale == 1.0) {
        imgX = layerX + L->offX + cd.ox;
        imgY = 0 - (L->ascent - L->offY - cd.oy);
        cw = cd.width;
        sp = next != 0 ? L->spacing + L->ch[ch].kern[next] : 0;
      } else {
        imgX = layerX + (int)((L->offX + cd.ox) * aScale);
        imgY = 0 - (int)((L->ascent - L->offY - cd.oy) * aScale);
        cw = (int)(cd.width * aScale);
        sp = next != 0 ? (int)((L->spacing + L->ch[ch].kern[next]) * aScale) : 0;
      }
      int r = std::min(cr * L->mult.r / 255 + L->add.r, 255);
      int g = std::min(cg * L->mult.g / 255 + L->add.g, 255);
      int b = std::min(cb * L->mult.b / 255 + L->add.b, 255);
      int a = std::min(ca * L->mult.a / 255 + L->add.a, 255);
      Cmd c;
      c.order = L->baseOrder + cd.order;
      c.q.img = L->image.get();
      c.q.sx = cd.rx; c.q.sy = cd.ry; c.q.sw = cd.rw; c.q.sh = cd.rh;
      c.q.dx = imgX; c.q.dy = imgY;
      c.q.rgba = RGBA(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), std::clamp(a, 0, 255));
      c.q.mode = L->drawMode;
      if (!(scale_ == 1.0 && (lpt == 0 || pointSize_ == lpt))) {
        double lp = 1, pt = scale_;
        if (lpt != 0) { lp = lpt; pt = pointSize_ * scale_; }
        c.q.sw = cd.rw; c.q.sh = cd.rh;
        c.q.dw = (int)((cd.rw * pt) / lp);
        c.q.dh = (int)((cd.rh * pt) / lp);
      } else {
        c.q.dw = cd.rw;
        c.q.dh = cd.rh;
      }
      cmds.push_back(c);
      layerX += cw + sp;
      if (layerX > maxX) maxX = layerX;
    }
    curX = maxX;
  }
  if (outWidth) *outWidth = curX;
  std::stable_sort(cmds.begin(), cmds.end(), [](const Cmd& a, const Cmd& b) {
    return std::min(std::max(a.order + 128, 0), 255) < std::min(std::max(b.order + 128, 0), 255);
  });
  out.reserve(cmds.size());
  for (auto& c : cmds)
    if (c.q.img && c.q.sw > 0 && c.q.sh > 0) out.push_back(c.q);
}

void Font::layoutColored(std::string_view s, uint32_t rgba, std::vector<GlyphQuad>& out, int* outWidth, uint32_t* state) const {
  out.clear();
  uint32_t orig = rgba, cur = state ? *state : rgba;
  std::string seg;
  int xoff = 0;
  std::vector<GlyphQuad> part;
  auto flush = [&]() {
    if (seg.empty()) return;
    int w = 0;
    layout(seg, cur, part, &w);
    for (auto q : part) { q.dx += xoff; out.push_back(q); }
    xoff += width(seg);
    seg.clear();
  };
  size_t n = s.size();
  for (size_t i = 0; i < n; i++) {
    if (s[i] == '^') {
      if (i + 1 < n && s[i + 1] == '^') { seg += '^'; i++; }
      else if ((long)i > (long)n - 8) break;
      else {
        uint32_t col = 0;
        if (s[i + 1] == 'o') {
          if (s.substr(i + 1, 6) == "oldclr") col = orig >> 8;
        } else {
          for (int d = 0; d < 6; d++) {
            char c = s[i + d + 1];
            int v = 0;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            col += (uint32_t)v << ((5 - d) * 4);
          }
        }
        flush();
        cur = (col << 8) | (cur & 255);
        i += 7;
      }
    } else seg += s[i];
  }
  flush();
  if (state) *state = cur;
  if (outWidth) *outWidth = xoff;
}

int Font::wrap(std::string_view line, int wrapWidth, int lineSpacing, std::vector<Line>& lines, int* maxWidthOut) const {
  lines.clear();
  prepare();
  int yOff = ascent() - ascentPadding();
  int yOff0 = yOff;
  if (lineSpacing == -1) lineSpacing = this->lineSpacing();
  size_t curPos = 0;
  int lineStart = 0, curWidth = 0, spacePos = -1, maxWidth = 0;
  unsigned char curChar = 0, prevChar = 0;
  const size_t n = line.size();
  auto emit = [&](int start, int len) { lines.push_back({start, len, yOff - yOff0}); };
  while (curPos < n) {
    curChar = (unsigned char)line[curPos];
    if (curChar == '^') {
      if (curPos + 1 < n) {
        if (line[curPos + 1] == '^') curPos++;
        else { curPos += 8; continue; }
      }
    } else if (curChar == ' ') spacePos = (int)curPos;
    else if (curChar == '\n') {
      curWidth = wrapWidth + 1;
      spacePos = (int)curPos;
      curPos++;
    }
    curWidth += charWidth(curChar, prevChar);
    prevChar = curChar;
    if (curWidth > wrapWidth) {
      int written;
      if (spacePos != -1) {
        emit(lineStart, spacePos - lineStart);
        written = curWidth;
        curPos = (size_t)spacePos + 1;
        if (curChar != '\n')
          while (curPos < n && line[curPos] == ' ') curPos++;
        lineStart = (int)curPos;
      } else {
        if ((int)curPos < lineStart + 1) curPos++;
        emit(lineStart, (int)curPos - lineStart);
        written = widthColored(line.substr(lineStart, curPos - lineStart));
      }
      if (written > maxWidth) maxWidth = written;
      lineStart = (int)curPos;
      spacePos = -1;
      curWidth = 0;
      prevChar = 0;
      yOff += lineSpacing;
    } else curPos++;
  }
  if (lineStart < (int)n) {
    emit(lineStart, (int)n - lineStart);
    int w = widthColored(line.substr(lineStart));
    if (w > maxWidth) maxWidth = w;
    yOff += lineSpacing;
  } else if (curChar == '\n') yOff += lineSpacing;
  if (maxWidthOut) *maxWidthOut = maxWidth;
  return yOff + descent() - lineSpacing;
}

void FontManager::reset() {
  manifest_ = false;
  res_.clear();
  fonts_.clear();
}

void FontManager::loadManifest() {
  if (manifest_) return;
  manifest_ = true;
  auto h = vfs_.read("properties/resources.xml");
  if (!h) return;
  std::string xml(h->bytes.begin(), h->bytes.end());
  std::string idprefix, defPath;
  std::regex tagRe("<(\\w+)([^>]*?)/?>");
  std::regex attrRe("(\\w+)\\s*=\\s*\"([^\"]*)\"");
  for (size_t a; (a = xml.find("<!--")) != std::string::npos;) {
    size_t b = xml.find("-->", a);
    xml.erase(a, b == std::string::npos ? std::string::npos : b + 3 - a);
  }
  for (auto it = std::sregex_iterator(xml.begin(), xml.end(), tagRe); it != std::sregex_iterator(); ++it) {
    std::string name = (*it)[1], attrs = (*it)[2];
    std::map<std::string, std::string> A;
    for (auto a = std::sregex_iterator(attrs.begin(), attrs.end(), attrRe); a != std::sregex_iterator(); ++a) A[upper((*a)[1])] = (*a)[2];
    if (ieq(name, "SetDefaults")) {
      if (A.count("PATH")) {
        defPath = A["PATH"];
        while (!defPath.empty() && (defPath.back() == '/' || defPath.back() == '\\')) defPath.pop_back();
        defPath += "/";
      }
      if (A.count("IDPREFIX")) idprefix = A["IDPREFIX"];
    } else if (ieq(name, "Font")) {
      Res r;
      r.id = idprefix + A["ID"];
      std::string p = A["PATH"];
      r.path = (p.rfind("!ref:", 0) == 0 || p.rfind("!sys:", 0) == 0) ? p : defPath + p;
      if (A.count("IMAGE")) r.path += "";
      r.tags = A.count("TAGS") ? A["TAGS"] : "";
      res_[upper(r.id)] = r;
    }
  }
}

bool FontManager::has(const std::string& id) {
  loadManifest();
  std::string u = upper(id);
  return res_.count(u) || res_.count("FONT_" + u);
}
std::vector<std::string> FontManager::ids() {
  loadManifest();
  std::vector<std::string> v;
  for (auto& [k, r] : res_) v.push_back(r.id);
  return v;
}

Font* FontManager::get(const std::string& id0) {
  loadManifest();
  std::string id = upper(id0);
  if (!res_.count(id) && res_.count("FONT_" + id)) id = "FONT_" + id;
  auto it = fonts_.find(id);
  if (it != fonts_.end()) return it->second.get();
  auto r = res_.find(id);
  if (r == res_.end()) {
    if (id0.find('.') != std::string::npos) {
      auto f = Font::load(vfs_, id0);
      fonts_[id] = f;
      return f.get();
    }
    return nullptr;
  }
  fonts_[id] = nullptr;
  std::shared_ptr<Font> f;
  const std::string& path = r->second.path;
  std::string err;
  if (path.rfind("!ref:", 0) == 0) {
    Font* base = get(path.substr(5));
    if (base) f = std::make_shared<Font>(*base);
  } else if (path.rfind("!sys:", 0) == 0) {
    Font* base = get("FONT_SMALL");
    if (base) f = std::make_shared<Font>(*base);
  } else {
    f = Font::load(vfs_, path, &err);
    if (!f) pp::log::writeOnce(pp::log::Level::Warn, "game", "font " + id + ": " + err);
  }
  if (f && !r->second.tags.empty()) {
    std::vector<std::string> tags;
    std::string cur;
    for (char c : r->second.tags + ",") {
      if (c == ',' || c == ' ' || c == '\r' || c == '\n' || c == '\t') {
        if (!cur.empty()) tags.push_back(cur);
        cur.clear();
      } else cur += c;
    }
    f = f->withTags(tags);
  }
  fonts_[id] = f;
  return f.get();
}

}
