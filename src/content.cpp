#include "content.h"

#include "gamepaths.h"
#include "log.h"
#include "ui/cfg.h"

#include <filesystem>
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

static void parseStages(const std::string& text, const std::string& pack, std::vector<StageLevel>& out) {
  std::istringstream in(ui::cfgStripComments(text));
  std::string line;
  int stage = -1;
  while (std::getline(in, line)) {
    auto k = line.find_first_not_of(" \t");
    if (k != std::string::npos && line.compare(k, 5, "Stage") == 0 && (k + 5 >= line.size() || !std::isalpha((unsigned char)line[k + 5]) || line[k + 5] == ' ')) stage++;
    if (k == std::string::npos || line.compare(k, 6, "Level:") != 0) continue;
    const auto values = ui::cfgValues(line.substr(k + 6));
    if (!values.empty() && !values[0].empty()) out.push_back({values[0], values.size() > 1 ? values[1] : values[0], pack, std::max(stage, 0)});
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
    if (readFileBytes(it->second, b)) parseStages(std::string(b.begin(), b.end()), L.label, out);
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

bool baseLinkFromIni(const std::map<std::string, std::string>& ini, const Layer* owner, BaseLink& link, std::string* err) {
  auto get = [&](const char* k) { auto it = ini.find(k); return it == ini.end() ? std::string() : it->second; };
  link = {get("base"), get("base_hash"), get("base_provider")};
  if ((ini.count("base") && link.name.empty()) || (ini.count("base_hash") && (link.name.empty() || link.hash.empty())) ||
      (ini.count("base_provider") && (link.provider.empty() || link.hash.empty() || normKey(link.name) == "_BASE"))) {
    if (err) *err = "incomplete base link";
    return false;
  }
  if (!link.name.empty() && normKey(link.name) != "_BASE" && link.provider.empty() && owner) link.provider = owner->provider;
  return true;
}

std::optional<VfsHit> readBaseFile(const Vfs& vfs, const BaseLink& link, uint64_t& hash, std::string* err) {
  auto fail = [&](const std::string& message) { if (err) *err = message; return std::nullopt; };
  if (link.name.empty() || link.name[0] != '_' || link.name.find_first_not_of("_0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-") != std::string::npos)
    return fail("invalid base level name");
  const auto path = "levels/" + link.name + ".dat";
  const Layer* owner = nullptr;
  if (normKey(link.name) == "_BASE") {
    if (!link.provider.empty()) return fail(path + ": unexpected linked provider");
    owner = vfs.owner(path);
  } else {
    if (link.provider.empty() || link.provider.find_first_of("\r\n") != std::string::npos) return fail(path + ": cannot identify the original provider");
    for (const auto& layer : vfs.layers()) {
      if (layer.provider != link.provider) continue;
      if (owner && (layer.root.empty() || layer.root != owner->root)) return fail(path + ": ambiguous provider " + link.provider);
      owner = &layer;
    }
  }
  auto hit = owner ? vfs.readFrom(*owner, path) : std::nullopt;
  if (!hit) return fail(path + " missing or unreadable" + (link.provider.empty() ? "" : " in " + link.provider));
  hash = fnv1a(hit->bytes.data(), hit->bytes.size());
  if (!link.hash.empty() && link.hash != std::to_string(hash)) return fail(path + " differs from the linked base");
  return hit;
}

bool readBaseLevel(const Vfs& vfs, BaseLink& link, std::shared_ptr<const Level>& level, uint64_t& hash, std::string* err) {
  auto hit = readBaseFile(vfs, link, hash, err);
  if (!hit) return false;
  std::string e;
  auto parsed = parseLevel(hit->bytes, &e);
  if (!parsed) { if (err) *err = link.name + ": " + e; return false; }
  level = std::make_shared<Level>(std::move(*parsed));
  if (normKey(link.name) == "_BASE") link = {};
  else link.hash = std::to_string(hash);
  return true;
}

static bool loadLevelBundleImpl(const Vfs& vfs, const std::string& nameOrPath, LevelBundle& out, std::string* err) {
  out = LevelBundle();
  std::vector<uint8_t> lvBytes, sideBytes;
  const Layer* linkOwner = nullptr;
  fs::path p = pathFromUtf8(nameOrPath);
  std::error_code ec;
  if (p.extension() == ".dat" && fs::is_regular_file(p, ec)) {
    if (!readFileBytes(p, lvBytes)) {
      if (err) *err = "cannot read " + pathToUtf8(p);
      return false;
    }
    out.name = pathToUtf8(p.stem());
    out.source = pathToUtf8(p);
    fs::path side = p;
    side.replace_extension(".ini");
    linkOwner = vfs.owner("levels/" + out.name + ".dat", p);
    if ((fs::exists(side, ec) && (!fs::is_regular_file(side, ec) || !readFileBytes(side, sideBytes))) || ec) {
      if (err) *err = "cannot read " + pathToUtf8(side);
      return false;
    }
  } else {
    out.name = nameOrPath;
    const auto key = "levels/" + nameOrPath;
    const auto* boardOwner = vfs.owner(key + ".dat");
    auto h = boardOwner ? vfs.readFrom(*boardOwner, key + ".dat") : std::nullopt;
    if (!h) {
      if (err) *err = "level not found: levels/" + nameOrPath + ".dat";
      return false;
    }
    lvBytes = std::move(h->bytes);
    out.source = h->layer ? h->layer->label : "";
    const auto side = key + ".ini";
    const auto* sideOwner = vfs.owner(side);
    if (sideOwner && sideOwner != boardOwner) {
      const auto k = normKey(side);
      if (boardOwner->files.count(k) || boardOwner->pakIndex.count(k)) {
        auto own = vfs.readFrom(*boardOwner, side);
        if (!own) { if (err) *err = "cannot read " + side; return false; }
        auto declared = readIni(std::string(own->bytes.begin(), own->bytes.end()));
        if (declared.count("base") && normKey(declared.at("base")) != "_BASE") {
          if (err) *err = side + ": board's linked sidecar is shadowed by " + sideOwner->label;
          return false;
        }
      }
    }
    if (sideOwner) {
      auto s = vfs.readFrom(*sideOwner, side);
      if (!s) { if (err) *err = "cannot read " + side; return false; }
      sideBytes = std::move(s->bytes);
      linkOwner = sideOwner;
    }
  }
  auto ini = readIni(std::string(sideBytes.begin(), sideBytes.end()));
  std::string e2;
  auto lv = parseLevel(lvBytes, &e2);
  if (!lv) {
    if (err) *err = out.name + ": " + e2;
    return false;
  }
  out.level = std::make_shared<Level>(std::move(*lv));
  uint64_t h = fnv1a(nullptr, 0);
  if (out.name.empty() || out.name[0] != '_') {
    if (!baseLinkFromIni(ini, linkOwner, out.baseLink, err)) return false;
    if (out.baseLink.name.empty()) out.baseLink.name = "_base";
    if (!readBaseLevel(vfs, out.baseLink, out.base, h, err)) return false;
  }
  if (ini.count("board_width")) out.cfg.boardWidth = std::clamp(std::atoi(ini["board_width"].c_str()), 650, 1400);
  out.cfg.authoredWidth = out.cfg.boardWidth;
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
