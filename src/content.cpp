#include "content.h"

#include "log.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <cstdlib>
#include <cctype>

namespace fs = std::filesystem;

namespace pp {

uint64_t fnv1a(const void* p, size_t n, uint64_t h) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

static bool slurp(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), {});
  return true;
}

static void parseStages(const std::string& text, const std::string& pack, std::vector<StageLevel>& out) {
  std::istringstream in(text);
  std::string line;
  int stage = -1;
  while (std::getline(in, line)) {
    auto c = line.find("//");
    if (c != std::string::npos) line = line.substr(0, c);
    auto k = line.find_first_not_of(" \t");
    if (k != std::string::npos && line.compare(k, 5, "Stage") == 0 && (k + 5 >= line.size() || !std::isalpha((unsigned char)line[k + 5]) || line[k + 5] == ' ')) stage++;
    if (k == std::string::npos || line.compare(k, 6, "Level:") != 0) continue;
    std::string rest = line.substr(k + 6);
    auto comma = rest.find(',');
    std::string name = rest.substr(0, comma);
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t\r") + 1);
    std::string title;
    auto q1 = rest.find('"');
    if (q1 != std::string::npos) {
      auto q2 = rest.find('"', q1 + 1);
      title = rest.substr(q1 + 1, q2 - q1 - 1);
    }
    if (!name.empty()) out.push_back({name, title, pack, std::max(stage, 0)});
  }
}

std::vector<StageLevel> listStageLevels(const Vfs& vfs) {
  std::vector<StageLevel> out;
  if (auto h = vfs.read("levels/stages.cfg")) parseStages(std::string(h->bytes.begin(), h->bytes.end()), "main", out);
  for (auto& L : vfs.layers()) {
    if (!L.isLevelPack) continue;
    for (auto& [k, idx] : L.pakIndex) {
      if (k.find('\\') != std::string::npos || k.size() < 4 || k.substr(k.size() - 4) != ".CFG") continue;
      auto& e = L.pak->entries[idx];
      parseStages(std::string(L.pak->data.begin() + e.offset, L.pak->data.begin() + e.offset + e.size), L.label, out);
    }
  }
  for (auto& L : vfs.layers()) {
    if (!L.isMod) continue;
    auto it = L.files.find("LEVELS\\MOD_LEVELS.CFG");
    if (it == L.files.end()) continue;
    std::vector<uint8_t> b;
    if (slurp(it->second, b)) parseStages(std::string(b.begin(), b.end()), L.label, out);
    else pp::log::warn("game", "cannot read the level list of " + L.label);
  }
  return out;
}

static void parsePegTypeLines(const std::string& text, std::vector<std::pair<int, std::string>>& out) {
  auto trim = [](std::string t) {
    size_t a = t.find_first_not_of(" \t\r\n"), b = t.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
  };
  auto number = [](const std::string& t, int& v) {
    if (t.empty() || t.size() > 6 || t.find_first_not_of("0123456789") != std::string::npos) return false;
    v = std::atoi(t.c_str());
    return true;
  };
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos || trim(line.substr(0, eq)) != "peg") continue;
    const std::string val = trim(line.substr(eq + 1));
    const size_t colon = val.find(':');
    if (colon == std::string::npos) continue;
    const std::string type = trim(val.substr(colon + 1));
    if (type.empty()) continue;
    const std::string ids = val.substr(0, colon) + ",";
    size_t pos = 0;
    while (pos < ids.size() && out.size() < 4096) {
      const size_t e = ids.find(',', pos);
      const std::string tok = trim(ids.substr(pos, e - pos));
      pos = e + 1;
      int a = 0, b = 0;
      const size_t dash = tok.find('-');
      if (dash != std::string::npos) {
        if (!number(trim(tok.substr(0, dash)), a) || !number(trim(tok.substr(dash + 1)), b) || b < a) continue;
        for (int k = a; k <= b && out.size() < 4096; k++) out.push_back({k, type});
      } else if (number(tok, a)) {
        out.push_back({a, type});
      }
    }
  }
}

static bool loadLevelBundleImpl(const Vfs& vfs, const std::string& nameOrPath, LevelBundle& out, std::string* err) {
  out = LevelBundle();
  std::vector<uint8_t> lvBytes, sideBytes, baseBytes;
  fs::path p(nameOrPath);
  std::error_code ec;
  if (p.extension() == ".dat" && fs::is_regular_file(p, ec)) {
    if (!slurp(p, lvBytes)) {
      if (err) *err = "cannot read " + p.string();
      return false;
    }
    out.name = p.stem().string();
    out.source = p.string();
    fs::path side = p;
    side.replace_extension(".ini");
    slurp(side, sideBytes);
  } else {
    out.name = nameOrPath;
    auto h = vfs.read("levels/" + nameOrPath + ".dat");
    if (!h) {
      if (err) *err = "level not found: levels/" + nameOrPath + ".dat";
      return false;
    }
    lvBytes = std::move(h->bytes);
    out.source = h->layer ? h->layer->label : "";
    if (auto s = vfs.read("levels/" + nameOrPath + ".ini")) sideBytes = std::move(s->bytes);
  }
  auto ini = readIni(std::string(sideBytes.begin(), sideBytes.end()));
  std::string e2;
  auto lv = parseLevel(lvBytes, &e2);
  if (!lv) {
    if (err) *err = out.name + ": " + e2;
    return false;
  }
  out.level = std::make_shared<Level>(std::move(*lv));
  if (out.name.empty() || out.name[0] != '_') {
    auto b = vfs.read("levels/_base.dat");
    if (!b) {
      if (err) *err = "levels/_base.dat missing";
      return false;
    }
    baseBytes = std::move(b->bytes);
    auto bl = parseLevel(baseBytes, &e2);
    if (!bl) {
      if (err) *err = "_base.dat: " + e2;
      return false;
    }
    out.base = std::make_shared<Level>(std::move(*bl));
  }
  if (ini.count("board_width")) out.cfg.boardWidth = std::clamp(std::atoi(ini["board_width"].c_str()), 650, 1400);
  if (ini.count("balls")) out.cfg.balls = std::max(1, std::atoi(ini["balls"].c_str()));
  if (ini.count("orange")) out.cfg.orange = std::max(0, std::atoi(ini["orange"].c_str()));
  {
    std::string pre;
    for (char c : out.name)
      if (std::isalpha((unsigned char)c)) pre.push_back(c);
      else break;
    int m = pre == "Master" ? 0 : Board::masterByName(pre, false);
    if (ini.count("master")) {
      m = Board::masterByName(ini["master"]);
      out.master = ini["master"];
    }
    for (int i = 0; i < 4; i++) out.cfg.master[i] = m;
  }
  for (const auto& [key, text] : ini) {
    int pk = 0;
    if (!physicsKeyByName(key, pk)) continue;
    double d = 0;
    if (!physicsParseNumber(text, d)) {
      pp::log::warn("game", out.name + ": the level ini key " + key + " is not a number and is ignored");
      continue;
    }
    if (physicsClamp(pk, d) != (float)d) pp::log::warn("game", out.name + ": " + key + " = " + text + " is outside " + std::to_string(physicsKey(pk).lo) + " to " + std::to_string(physicsKey(pk).hi) + " and is limited");
    out.cfg.physics.set(pk, d);
  }
  parsePegTypeLines(std::string(sideBytes.begin(), sideBytes.end()), out.cfg.pegTypes);
  out.background = ini.count("background") ? ini["background"] : out.name;
  out.title = ini.count("title") ? ini["title"] : out.name;
  if (!ini.count("title"))
    for (auto& s : listStageLevels(vfs))
      if (s.name == out.name && !s.title.empty()) out.title = s.title;
  uint64_t h = fnv1a(baseBytes.data(), baseBytes.size());
  h = fnv1a(lvBytes.data(), lvBytes.size(), h);
  h = fnv1a(sideBytes.data(), sideBytes.size(), h);
  h = fnv1a(&out.cfg.boardWidth, sizeof out.cfg.boardWidth, h);
  if (g_modSetHash) h = fnv1a(&g_modSetHash, sizeof g_modSetHash, h);
  out.contentHash = h;
  return true;
}

bool loadLevelBundle(const Vfs& vfs, const std::string& nameOrPath, LevelBundle& out, std::string* err) {
  std::string e;
  if (!loadLevelBundleImpl(vfs, nameOrPath, out, &e)) {
    pp::log::error("game", "cannot load level " + nameOrPath + ": " + e);
    if (err) *err = e;
    return false;
  }
  pp::log::info("game", "loaded level " + out.name + (out.source.empty() ? std::string() : " from " + out.source));
  return true;
}

}

namespace pp {
namespace {
std::string upperS(std::string s) {
  for (auto& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}
}

ImageManifest::ImageManifest(const Vfs& v) {
  auto h = v.read("properties/resources.xml");
  if (!h) return;
  std::string xml(h->bytes.begin(), h->bytes.end());
  for (size_t a; (a = xml.find("<!--")) != std::string::npos;) {
    size_t b = xml.find("-->", a);
    xml.erase(a, b == std::string::npos ? std::string::npos : b + 3 - a);
  }
  std::string idprefix, defPath;
  size_t pos = 0;
  while ((pos = xml.find('<', pos)) != std::string::npos) {
    size_t end = xml.find('>', pos);
    if (end == std::string::npos) break;
    std::string tag = xml.substr(pos + 1, end - pos - 1);
    pos = end + 1;
    if (tag.empty() || tag[0] == '/' || tag[0] == '?') continue;
    size_t sp = tag.find_first_of(" \t\r\n/");
    std::string name = tag.substr(0, sp);
    std::map<std::string, std::string> A;
    size_t i = sp == std::string::npos ? tag.size() : sp;
    while (i < tag.size()) {
      while (i < tag.size() && (std::isspace((unsigned char)tag[i]) || tag[i] == '/')) i++;
      size_t k0 = i;
      while (i < tag.size() && (std::isalnum((unsigned char)tag[i]) || tag[i] == '_')) i++;
      if (i == k0) { i++; continue; }
      std::string key = upperS(tag.substr(k0, i - k0));
      size_t j = i;
      while (j < tag.size() && std::isspace((unsigned char)tag[j])) j++;
      if (j < tag.size() && tag[j] == '=') {
        j++;
        while (j < tag.size() && std::isspace((unsigned char)tag[j])) j++;
        if (j < tag.size() && (tag[j] == '"' || tag[j] == '\'')) {
          char q = tag[j++];
          size_t e = tag.find(q, j);
          if (e == std::string::npos) e = tag.size();
          A[key] = tag.substr(j, e - j);
          i = e + 1;
        } else { A[key] = ""; i = j; }
      } else A[key] = "";
    }
    if (upperS(name) == "SETDEFAULTS") {
      if (A.count("PATH")) {
        defPath = A["PATH"];
        while (!defPath.empty() && (defPath.back() == '/' || defPath.back() == '\\')) defPath.pop_back();
        defPath += "/";
      }
      if (A.count("IDPREFIX")) idprefix = A["IDPREFIX"];
    } else if (upperS(name) == "IMAGE") {
      ImageRes r;
      r.id = idprefix + A["ID"];
      r.path = defPath + A["PATH"];
      r.alphaImage = A.count("ALPHAIMAGE") ? A["ALPHAIMAGE"] : (A.count("ALPHAGRID") ? A["ALPHAGRID"] : "");
      if (!r.alphaImage.empty()) r.alphaImage = defPath + r.alphaImage;
      if (A.count("ROWS")) r.rows = std::max(1, std::atoi(A["ROWS"].c_str()));
      if (A.count("COLS")) r.cols = std::max(1, std::atoi(A["COLS"].c_str()));
      if (A.count("ALPHACOLOR")) { r.alphaColor = true; r.alphaColorRGB = (uint32_t)std::strtoul(A["ALPHACOLOR"].c_str(), nullptr, 0); }
      if (A.count("ANIM")) r.animOnce = upperS(A["ANIM"]) == "ONCE";
      if (A.count("FRAMEDELAY")) r.frameDelay = std::atoi(A["FRAMEDELAY"].c_str());
      res_[upperS(r.id)] = r;
    }
  }
}

const ImageRes* ImageManifest::find(const std::string& id) const {
  auto it = res_.find(upperS(id));
  return it == res_.end() ? nullptr : &it->second;
}

int stageOfLevel(const Vfs& vfs, const std::string& levelName) {
  for (auto& s : listStageLevels(vfs))
    if (upperS(s.name) == upperS(levelName)) return s.stage;
  return 0;
}

std::vector<CharInfo> loadCharacters(const Vfs& vfs) {
  std::vector<CharInfo> out;
  auto h = vfs.read("characters/characters.cfg");
  if (!h) return out;
  std::string text(h->bytes.begin(), h->bytes.end());
  std::istringstream in(text);
  std::string line;
  CharInfo cur;
  bool open = false;
  auto trim = [](std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  };
  auto ints = [](const std::string& s) {
    std::vector<int> v;
    std::string t;
    for (char ch : s + ",") {
      if (ch == ',') { if (!t.empty()) v.push_back(std::atoi(t.c_str())); t.clear(); }
      else if (!std::isspace((unsigned char)ch)) t += ch;
    }
    return v;
  };
  while (std::getline(in, line)) {
    auto c2 = line.find("//");
    if (c2 != std::string::npos) line = line.substr(0, c2);
    line = trim(line);
    if (line.empty()) continue;
    if (line.compare(0, 9, "Character") == 0 && !open) {
      cur = CharInfo();
      cur.name = trim(line.substr(9));
      if (cur.name.size() >= 2 && cur.name.front() == '"') cur.name = cur.name.substr(1, cur.name.size() - 2);
      continue;
    }
    if (line == "{") { open = true; continue; }
    if (line == "}") { if (open) out.push_back(cur); open = false; continue; }
    if (!open) continue;
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = trim(line.substr(0, colon)), val = trim(line.substr(colon + 1));
    for (auto& ch : key) ch = (char)std::tolower((unsigned char)ch);
    if (key == "powerup") cur.power = val;
    else if (key == "image") {
      auto comma = val.find(',');
      cur.image = trim(val.substr(0, comma));
      if (comma != std::string::npos) cur.frames = std::atoi(val.c_str() + comma + 1);
    } else if (key == "eyeimage") cur.eyeImage = val;
    else if (key == "eyes") { auto v = ints(val); if (v.size() >= 4) cur.eyes.push_back({v[0], v[1], v[2], v[3]}); }
    else if (key == "pos") { auto v = ints(val); if (v.size() >= 2) { cur.posX = v[0]; cur.posY = v[1]; } }
    else if (key == "tweenspeed") cur.tween = std::atoi(val.c_str());
  }
  return out;
}

}
