#pragma once
#include <cctype>
#include <string>
#include <vector>

namespace pp::ui {

struct CfgItem {
  bool isGroup = false;
  std::string key;
  std::string label;
  std::vector<CfgItem> kids;
  int line = 0;
};

inline std::vector<std::string> cfgValues(const std::string& v) {
  std::vector<std::string> out;
  size_t i = 0, n = v.size();
  auto skip = [&] { while (i < n && (v[i] == ' ' || v[i] == '\t' || v[i] == '\r')) i++; };
  while (true) {
    skip();
    if (i >= n) break;
    std::string cur;
    bool any = false;
    while (true) {
      skip();
      if (i < n && v[i] == '"') {
        i++;
        any = true;
        while (i < n && v[i] != '"') {
          if (v[i] == '\\' && i + 1 < n) {
            char c = v[i + 1];
            cur.push_back(c == 'n' ? '\n' : c == 't' ? '\t' : c);
            i += 2;
          } else {
            cur.push_back(v[i++]);
          }
        }
        if (i < n) i++;
      } else {
        size_t s = i;
        while (i < n && v[i] != ',' && v[i] != '+') i++;
        std::string t = v.substr(s, i - s);
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
        cur += t;
        if (!t.empty()) any = true;
      }
      skip();
      if (i < n && v[i] == '+') { i++; continue; }
      break;
    }
    if (any) out.push_back(cur);
    skip();
    if (i < n && v[i] == ',') i++;
    else if (i >= n) break;
  }
  return out;
}

inline std::string cfgStripComments(const std::string& t) {
  std::string out;
  out.reserve(t.size());
  bool inStr = false;
  for (size_t i = 0; i < t.size(); i++) {
    char c = t[i];
    if (inStr) {
      out.push_back(c);
      if (c == '\\' && i + 1 < t.size()) out.push_back(t[++i]);
      else if (c == '"' || c == '\n') inStr = false;
      continue;
    }
    if (c == '"') { inStr = true; out.push_back(c); continue; }
    if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') {
      while (i < t.size() && t[i] != '\n') i++;
      out.push_back('\n');
      continue;
    }
    if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
      i += 2;
      while (i + 1 < t.size() && !(t[i] == '*' && t[i + 1] == '/')) { if (t[i] == '\n') out.push_back('\n'); i++; }
      i++;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

inline std::vector<CfgItem> cfgParse(const std::string& raw) {
  std::string t = cfgStripComments(raw);
  std::vector<CfgItem> root;
  std::vector<std::vector<CfgItem>*> stack{&root};
  std::vector<CfgItem> pending;
  size_t i = 0, n = t.size();
  int line = 1;
  CfgItem hdr;
  bool haveHdr = false;
  while (i < n) {
    while (i < n && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) { if (t[i] == '\n') line++; i++; }
    if (i >= n) break;
    if (t[i] == '{') {
      i++;
      if (!haveHdr) { hdr = CfgItem(); hdr.isGroup = true; hdr.key = "?"; }
      hdr.isGroup = true;
      stack.back()->push_back(hdr);
      stack.push_back(&stack.back()->back().kids);
      haveHdr = false;
      continue;
    }
    if (t[i] == '}') {
      i++;
      if (stack.size() > 1) stack.pop_back();
      continue;
    }
    size_t s = i;
    while (i < n && (std::isalnum((unsigned char)t[i]) || t[i] == '_')) i++;
    if (i == s) { i++; continue; }
    std::string word = t.substr(s, i - s);
    size_t j = i;
    while (j < n && (t[j] == ' ' || t[j] == '\t')) j++;
    if (j < n && t[j] == ':') {
      size_t e = t.find('\n', j);
      if (e == std::string::npos) e = n;
      CfgItem it;
      it.key = word;
      it.label = t.substr(j + 1, e - j - 1);
      it.line = line;
      stack.back()->push_back(it);
      i = e;
      continue;
    }
    hdr = CfgItem();
    hdr.key = word;
    hdr.line = line;
    if (j < n && t[j] == '"') {
      size_t e = j + 1;
      while (e < n && t[e] != '"') e++;
      hdr.label = t.substr(j + 1, e - j - 1);
      j = e + 1;
    } else if (j > i) {
      size_t e = j;
      while (e < n && (std::isalnum((unsigned char)t[e]) || t[e] == '_')) e++;
      const size_t brace = t.find_first_not_of(" \t\r\n", e);
      if (e > j && brace < n && t[brace] == '{') {
        hdr.label = t.substr(j, e - j);
        j = e;
      }
    }
    haveHdr = true;
    i = j;
  }
  return root;
}

}
