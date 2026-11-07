#include "ui/res.h"

#include "log.h"

#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>
#include <map>

namespace pp::ui {

namespace {
std::map<std::string, std::string> attrs(const std::string& tag) {
  std::map<std::string, std::string> out;
  size_t i = 0;
  while (i < tag.size()) {
    while (i < tag.size() && (std::isspace((unsigned char)tag[i]) || tag[i] == '<' || tag[i] == '/' || tag[i] == '>')) i++;
    size_t k = i;
    while (i < tag.size() && (std::isalnum((unsigned char)tag[i]) || tag[i] == '_' || tag[i] == '-')) i++;
    if (i == k) { i++; continue; }
    std::string key = tag.substr(k, i - k);
    for (auto& c : key) c = (char)std::tolower((unsigned char)c);
    while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
    std::string val;
    if (i < tag.size() && tag[i] == '=') {
      i++;
      while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
      if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
        char q = tag[i++];
        size_t e = tag.find(q, i);
        if (e == std::string::npos) e = tag.size();
        val = tag.substr(i, e - i);
        i = e + 1;
      } else {
        size_t e = i;
        while (e < tag.size() && !std::isspace((unsigned char)tag[e]) && tag[e] != '/' && tag[e] != '>') e++;
        val = tag.substr(i, e - i);
        i = e;
      }
    }
    out[key] = val;
  }
  return out;
}
}

bool Manifest::load(const Vfs& vfs) {
  auto h = vfs.read("properties/resources.xml");
  if (!h) {
    pp::log::error("game", "properties/resources.xml not found: images and sounds cannot be looked up");
    return false;
  }
  std::string t(h->bytes.begin(), h->bytes.end());
  std::string curGroup, imgPath = "images", imgPrefix = "IMAGE_", sndPath = "sounds", sndPrefix = "SOUND_", fntPath = "fonts", fntPrefix = "FONT_";
  size_t p = 0;
  while ((p = t.find('<', p)) != std::string::npos) {
    if (t.compare(p, 4, "<!--") == 0) {
      size_t e = t.find("-->", p);
      p = e == std::string::npos ? t.size() : e + 3;
      continue;
    }
    size_t e = t.find('>', p);
    if (e == std::string::npos) break;
    std::string tag = t.substr(p, e - p + 1);
    p = e + 1;
    if (tag.compare(0, 11, "<Resources ") == 0) {
      curGroup = attrs(tag)["id"];
      groupOrder_.push_back(curGroup);
      continue;
    }
    if (tag.compare(0, 12, "<SetDefaults") == 0) {
      auto a = attrs(tag);
      std::string path = a["path"], pre = a.count("idprefix") ? a["idprefix"] : "";
      if (pre.rfind("SOUND_", 0) == 0) { sndPath = path; sndPrefix = pre; }
      else if (pre.rfind("FONT_", 0) == 0) { fntPath = path; fntPrefix = pre; }
      else { imgPath = path; imgPrefix = pre; }
      continue;
    }
    bool isImg = tag.compare(0, 6, "<Image") == 0, isSnd = tag.compare(0, 6, "<Sound") == 0, isFnt = tag.compare(0, 5, "<Font") == 0;
    if (!isImg && !isSnd && !isFnt) continue;
    auto a = attrs(tag);
    if (!a.count("id")) continue;
    if (isImg) {
      ResImage r;
      r.id = imgPrefix + a["id"];
      for (auto& c : r.id) c = (char)std::toupper((unsigned char)c);
      std::string pth = a["path"];
      r.path = imgPath.empty() ? pth : imgPath + "/" + pth;
      if (a.count("cols")) r.cols = std::max(1, std::atoi(a["cols"].c_str()));
      if (a.count("rows")) r.rows = std::max(1, std::atoi(a["rows"].c_str()));
      if (a.count("alphaimage")) r.alphaImage = a["alphaimage"];
      if (a.count("alphagrid")) r.alphaGrid = a["alphagrid"];
      images_[r.id] = r;
      groups_[curGroup].push_back(r.id);
    } else if (isSnd) {
      std::string id = sndPrefix + a["id"];
      for (auto& c : id) c = (char)std::toupper((unsigned char)c);
      sounds_[id] = sndPath + "/" + a["path"];
    } else {
      ResFont f;
      f.id = fntPrefix + a["id"];
      for (auto& c : f.id) c = (char)std::toupper((unsigned char)c);
      f.path = a["path"];
      f.tags = a["tags"];
      if (f.path.rfind("!ref:", 0) == 0) { f.ref = f.path.substr(5); f.path.clear(); }
      for (auto& c : f.ref) c = (char)std::toupper((unsigned char)c);
      fonts_[f.id] = f;
    }
  }
  return !images_.empty();
}

const ResImage* Manifest::image(const std::string& id) const {
  auto it = images_.find(id);
  return it == images_.end() ? nullptr : &it->second;
}
const std::string* Manifest::sound(const std::string& id) const {
  auto it = sounds_.find(id);
  return it == sounds_.end() ? nullptr : &it->second;
}
const ResFont* Manifest::font(const std::string& id) const {
  auto it = fonts_.find(id);
  return it == fonts_.end() ? nullptr : &it->second;
}
const std::vector<std::string>& Manifest::groupImages(const std::string& g) const {
  static const std::vector<std::string> none;
  auto it = groups_.find(g);
  return it == groups_.end() ? none : it->second;
}

}
