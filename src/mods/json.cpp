#include "mods/json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <string>
#include <string_view>

namespace pp::mods {
namespace {
struct P {
  std::string_view s;
  size_t i = 0;
  std::string err;
  int depth = 0;
  void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
  bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at byte " + std::to_string(i); return false; }
  bool lit(const char* w) {
    size_t n = std::char_traits<char>::length(w);
    if (s.substr(i, n) != w) return false;
    i += n;
    return true;
  }
  static void utf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  }
  bool string(std::string& out) {
    if (i >= s.size() || s[i] != '"') return fail("expected string");
    i++;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) return fail("bad escape");
      char e = s[i++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (i + 4 > s.size()) return fail("bad \\u");
          unsigned cp = (unsigned)std::strtoul(std::string(s.substr(i, 4)).c_str(), nullptr, 16);
          i += 4;
          utf8(out, cp);
          break;
        }
        default: out += e;
      }
    }
    if (i >= s.size()) return fail("unterminated string");
    i++;
    return true;
  }
  bool value(Json& j) {
    ws();
    if (i >= s.size()) return fail("unexpected end");
    if (++depth > 64) return fail("too deep");
    struct D { int& d; ~D() { d--; } } dd{depth};
    char c = s[i];
    if (c == '{') {
      i++;
      j = Json::object();
      ws();
      if (i < s.size() && s[i] == '}') { i++; return true; }
      for (;;) {
        ws();
        std::string k;
        if (!string(k)) return false;
        ws();
        if (i >= s.size() || s[i] != ':') return fail("expected ':'");
        i++;
        Json v;
        if (!value(v)) return false;
        j.o.emplace_back(std::move(k), std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == '}') { i++; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      i++;
      j = Json::array();
      ws();
      if (i < s.size() && s[i] == ']') { i++; return true; }
      for (;;) {
        Json v;
        if (!value(v)) return false;
        j.a.push_back(std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == ']') { i++; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') { j.t = Json::Str; return string(j.s); }
    if (lit("true")) { j = Json::boolean(true); return true; }
    if (lit("false")) { j = Json::boolean(false); return true; }
    if (lit("null")) { j = Json(); return true; }
    size_t st = i;
    while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
    if (st == i) return fail("unexpected character");
    j = Json::num(std::strtod(std::string(s.substr(st, i - st)).c_str(), nullptr));
    return true;
  }
};
void esc(std::string& o, const std::string& s) {
  o += '"';
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\t') o += "\\t";
    else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  o += '"';
}
void dumpTo(const Json& j, std::string& o, int ind, int lvl) {
  auto nl = [&](int l) { if (ind) { o += '\n'; o.append((size_t)(l * ind), ' '); } };
  switch (j.t) {
    case Json::Null: o += "null"; break;
    case Json::Bool: o += j.b ? "true" : "false"; break;
    case Json::Num: {
      char b[40];
      if (j.n == std::floor(j.n) && std::fabs(j.n) < 1e15) std::snprintf(b, sizeof b, "%lld", (long long)j.n);
      else std::snprintf(b, sizeof b, "%.17g", j.n);
      o += b;
      break;
    }
    case Json::Str: esc(o, j.s); break;
    case Json::Arr:
      o += '[';
      for (size_t k = 0; k < j.a.size(); k++) { if (k) o += ','; nl(lvl + 1); dumpTo(j.a[k], o, ind, lvl + 1); }
      if (!j.a.empty()) nl(lvl);
      o += ']';
      break;
    case Json::Obj:
      o += '{';
      for (size_t k = 0; k < j.o.size(); k++) { if (k) o += ','; nl(lvl + 1); esc(o, j.o[k].first); o += ind ? ": " : ":"; dumpTo(j.o[k].second, o, ind, lvl + 1); }
      if (!j.o.empty()) nl(lvl);
      o += '}';
      break;
  }
}
}

bool Json::parse(std::string_view text, Json& out, std::string* err) {
  P p;
  p.s = text;
  if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) p.i = 3;
  if (!p.value(out)) { if (err) *err = p.err; return false; }
  p.ws();
  if (p.i != text.size()) { if (err) *err = "trailing characters at byte " + std::to_string(p.i); return false; }
  return true;
}

std::string Json::dump(int indent) const {
  std::string o;
  dumpTo(*this, o, indent, 0);
  return o;
}

}
