#include "vfs.h"

#include "gamepaths.h"
#include "log.h"
#include "mods/manifest.h"

#include <cctype>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <optional>
#include <filesystem>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace pp {

std::string normKey(std::string_view path) {
  std::string s;
  s.reserve(path.size());
  for (char c : path) {
    if (c == '/') c = '\\';
    s.push_back((char)std::toupper((unsigned char)c));
  }
  while (s.rfind(".\\", 0) == 0) s.erase(0, 2);
  while (!s.empty() && s[0] == '\\') s.erase(0, 1);
  return s;
}

std::string genericUtf8(const fs::path& p) {
  auto u = p.generic_u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::map<std::string, std::string> readIni(const std::string& text) {
  std::map<std::string, std::string> out;
  std::istringstream in(text);
  std::string line;
  auto trim = [](std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  };
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    out[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  return out;
}

bool readFileBytes(const fs::path& p, std::vector<uint8_t>& out, size_t maxBytes) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  auto size = f.tellg();
  if (size < 0 || (uint64_t)size > maxBytes) return false;
  out.resize((size_t)size);
  f.seekg(0);
  f.read((char*)out.data(), (std::streamsize)out.size());
  return (bool)f && f.gcount() == (std::streamsize)out.size();
}

bool loadPak(const fs::path& p, Pak& out, std::string* err) {
  auto fail = [&](const std::string& m) {
    if (err) *err = pathToUtf8(p) + ": " + m;
    return false;
  };
  if (!readFileBytes(p, out.data)) return fail("cannot read");
  for (auto& b : out.data) b ^= 0xF7;
  const auto& d = out.data;
  auto u32 = [&](size_t o) { return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; };
  if (d.size() < 8) return fail("truncated header");
  if (u32(0) != 0xBAC04AC0u) return fail("bad magic");
  if (u32(4) != 0) return fail("unsupported version");
  size_t pos = 8;
  out.entries.clear();
  for (;;) {
    if (pos >= d.size()) return fail("directory runs past end");
    uint8_t flags = d[pos++];
    if (flags & 0x80) break;
    if (pos >= d.size()) return fail("truncated entry");
    uint8_t n = d[pos++];
    if (pos + n + 12 > d.size()) return fail("truncated entry");
    PakEntry e;
    e.name.assign((const char*)&d[pos], n);
    pos += n;
    e.size = u32(pos);
    e.filetime = (uint64_t)u32(pos + 4) | (uint64_t)u32(pos + 8) << 32;
    pos += 12;
    out.entries.push_back(std::move(e));
  }
  for (auto& e : out.entries) {
    e.offset = (uint32_t)pos;
    pos += e.size;
  }
  if (pos > d.size()) return fail("file data runs past end");
  return true;
}

void Vfs::indexDir(Layer& L, std::string_view prefix) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(L.root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file()) continue;
    auto rel = genericUtf8(fs::relative(it->path(), L.root, ec));
    if (rel.empty() || rel[0] == '.') continue;
    L.files.emplace(std::string(prefix) + normKey(rel), it->path());
  }
}

bool Vfs::mountGame(const fs::path& gameDir, std::string* err) {
  fs::path mainPak = gameDir / "main.pak";
  auto pak = std::make_shared<Pak>();
  if (!loadPak(mainPak, *pak, err)) {
    pp::log::error("game", "cannot open " + pathToUtf8(mainPak) + (err && !err->empty() ? ": " + *err : std::string()));
    return false;
  }
  pp::log::info("game", "game data: " + pathToUtf8(gameDir) + " (" + std::to_string(pak->entries.size()) + " files in main.pak)");
  Layer L;
  L.kind = Layer::PakFile;
  L.provider = L.label = "main.pak";
  L.pak = pak;
  for (uint32_t i = 0; i < pak->entries.size(); i++) L.pakIndex.emplace(normKey(pak->entries[i].name), i);
  layers_.push_back(std::move(L));

  std::vector<fs::path> packs;
  std::error_code ec;
  if (fs::is_directory(gameDir / "levelpacks", ec))
    for (auto& e : fs::directory_iterator(gameDir / "levelpacks", ec))
      if (e.is_regular_file() && normKey(pathToUtf8(e.path().extension())) == ".PAK") packs.push_back(e.path());
  std::sort(packs.begin(), packs.end());
  for (auto& p : packs) {
    auto lp = std::make_shared<Pak>();
    std::string e2;
    if (!loadPak(p, *lp, &e2)) {
      pp::log::warn("game", "skipped level pack " + pathToUtf8(p.filename()) + ": " + e2);
      continue;
    }
    pp::log::info("game", "level pack " + pathToUtf8(p.filename()) + " (" + std::to_string(lp->entries.size()) + " files)");
    Layer P;
    P.kind = Layer::PakFile;
    P.isLevelPack = true;
    P.provider = P.label = "levelpacks/" + pathToUtf8(p.filename());
    P.pak = lp;
    for (uint32_t i = 0; i < lp->entries.size(); i++) P.pakIndex.emplace(normKey(lp->entries[i].name), i);
    layers_.push_back(std::move(P));
  }
  Layer D;
  D.kind = Layer::Dir;
  D.provider = D.label = "game dir";
  D.root = gameDir;
  indexDir(D);
  layers_.push_back(std::move(D));
  return true;
}

bool Vfs::mountPresentation(const fs::path& dataDir) {
  if (dataDir.empty()) return false;
  Layer L;
  L.root = dataDir / "console";
  L.label = "console presentation";
  L.meta.emplace("image_alpha", "baked");
  indexDir(L, "CONSOLE\\");
  std::erase_if(L.files, [](const auto& image) { return !image.first.ends_with(".PNG"); });
  if (L.files.empty()) return false;
  auto where = std::find_if(layers_.begin(), layers_.end(), [](const Layer& layer) { return !layer.isMod; });
  pp::log::info("game", "console presentation: " + pathToUtf8(L.root) + " (" + std::to_string(L.files.size()) + " images)");
  layers_.insert(where, std::move(L));
  return true;
}

bool Vfs::mountMod(const fs::path& modDir, std::string* err, const std::string& modId) {
  std::error_code ec;
  if (!fs::is_directory(modDir, ec)) {
    if (err) *err = "mod directory not found: " + pathToUtf8(modDir);
    pp::log::error("game", "mod directory not found: " + pathToUtf8(modDir));
    return false;
  }
  Layer L;
  L.kind = Layer::Dir;
  L.isMod = true;
  L.root = modDir;
  L.label = "mod:" + pathToUtf8(modDir.filename());
  std::vector<uint8_t> ini;
  const bool readManifest = readFileBytes(modDir / "mod.ini", ini);
  if (readManifest) {
    L.meta = readIni(std::string(ini.begin(), ini.end()));
    if (L.meta.count("name")) L.label = "mod:" + L.meta["name"];
    if (L.meta.count("priority")) L.priority = std::atoi(L.meta["priority"].c_str());
    if (L.meta.count("image_scale")) L.imageScale = std::max(0.25f, (float)std::atof(L.meta["image_scale"].c_str()));
  }
  indexDir(L);
  if (!modId.empty()) L.provider = "mod:" + modId;
  else if (!L.files.count("MOD.JSON") && (!L.files.count("MOD.INI") || readManifest)) {
    std::string id = pathToUtf8(modDir.has_filename() ? modDir.filename() : modDir.parent_path().filename());
    for (const auto& [k, val] : L.meta) if (normKey(k) == "ID") id = val;
    L.provider = "mod:" + mods::sanitizeId(id);
  }
  L.files.erase("MOD.INI");
  pp::log::info("game", "mounted " + L.label + " (" + std::to_string(L.files.size()) + " files, priority " + std::to_string(L.priority) + ")");
  auto it = layers_.begin();
  while (it != layers_.end() && it->isMod && it->priority >= L.priority) ++it;
  layers_.insert(it, std::move(L));
  return true;
}

bool Vfs::unmountMod(const fs::path& modDir) {
  for (auto it = layers_.begin(); it != layers_.end(); ++it)
    if (it->isMod && it->root == modDir) {
      pp::log::info("game", "unmounted " + it->label);
      layers_.erase(it);
      return true;
    }
  return false;
}

size_t Vfs::unmountMods() {
  size_t n = 0;
  for (auto it = layers_.begin(); it != layers_.end();) {
    if (it->isMod) {
      pp::log::info("game", "unmounted " + it->label);
      it = layers_.erase(it);
      n++;
    } else {
      ++it;
    }
  }
  return n;
}

std::string Vfs::modSignature() const {
  std::string s;
  for (auto& L : layers_)
    if (L.isMod) s += genericUtf8(L.root) + "|" + std::to_string(L.priority) + "|" + std::to_string(L.imageScale) + "\n";
  return s;
}

void Vfs::modKeys(std::set<std::string>& out) const {
  for (auto& L : layers_)
    if (L.isMod)
      for (auto& [k, _] : L.files) out.insert(k);
}

bool VfsChange::touches(std::string_view path) const { return any && keys.count(normKey(path)) > 0; }

bool VfsChange::touchesFolder(std::string_view folder) const {
  if (!any) return false;
  std::string f = normKey(folder);
  if (!f.empty() && f.back() != '\\') f.push_back('\\');
  auto it = keys.lower_bound(f);
  return it != keys.end() && it->compare(0, f.size(), f) == 0;
}

bool VfsChange::touchesImage(std::string_view path) const {
  if (!any) return false;
  static const char* kExts[] = {".TGA", ".JPG", ".PNG", ".GIF", ".J2K", ".JP2"};
  std::string k = normKey(path);
  if (keys.count(k)) return true;
  std::string stem = k;
  size_t dot = stem.find_last_of('.'), sep = stem.find_last_of('\\');
  if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
    std::string e = stem.substr(dot);
    for (auto x : kExts)
      if (e == x) { stem.erase(dot); break; }
  }
  sep = stem.find_last_of('\\');
  std::string dir = sep == std::string::npos ? "" : stem.substr(0, sep + 1);
  std::string file = sep == std::string::npos ? stem : stem.substr(sep + 1);
  for (auto x : kExts)
    if (keys.count(stem + x) || keys.count(dir + "_" + file + x) || keys.count(stem + "_" + x)) return true;
  return false;
}

void vfsLogMiss(const char* kind, std::string_view path) {
  pp::log::writeOnce(pp::log::Level::Warn, "game", std::string("missing ") + kind + ": " + std::string(path));
}

std::optional<VfsHit> Vfs::read(std::string_view path) const { return read(path, std::numeric_limits<size_t>::max()); }

std::optional<VfsHit> Vfs::read(std::string_view path, size_t maxBytes) const { return readImpl(path, maxBytes, nullptr); }

std::optional<VfsHit> Vfs::readFrom(const Layer& layer, std::string_view path) const {
  return readImpl(path, std::numeric_limits<size_t>::max(), &layer);
}

std::optional<VfsHit> Vfs::readImpl(std::string_view path, size_t maxBytes, const Layer* only) const {
  std::string k = normKey(path);
  for (auto& L : layers_) {
    if (only && only != &L) continue;
    if (L.kind == Layer::PakFile) {
      auto it = L.pakIndex.find(k);
      if (it == L.pakIndex.end()) continue;
      auto& e = L.pak->entries[it->second];
      if (e.size > maxBytes) return std::nullopt;
      VfsHit h;
      h.layer = &L;
      h.bytes.assign(L.pak->data.begin() + e.offset, L.pak->data.begin() + e.offset + e.size);
      return h;
    }
    auto it = L.files.find(k);
    if (it == L.files.end()) continue;
    VfsHit h;
    h.layer = &L;
    if (!readFileBytes(it->second, h.bytes, maxBytes)) continue;
    return h;
  }
  return std::nullopt;
}

const Layer* Vfs::owner(std::string_view path, const fs::path& diskFile) const {
  const auto k = normKey(path);
  for (const auto& layer : layers_) {
    if (diskFile.empty()) {
      if (layer.kind == Layer::PakFile ? layer.pakIndex.count(k) : layer.files.count(k)) return &layer;
    } else if (auto it = layer.files.find(k); it != layer.files.end()) {
      std::error_code ec;
      if (fs::equivalent(diskFile, it->second, ec)) return &layer;
    }
  }
  return nullptr;
}

bool Vfs::exists(std::string_view path) const { return owner(path) != nullptr; }

std::vector<std::string> Vfs::keys() const {
  std::set<std::string> s;
  for (auto& L : layers_) {
    if (L.kind == Layer::PakFile)
      for (auto& [k, _] : L.pakIndex) s.insert(k);
    else
      for (auto& [k, _] : L.files) s.insert(k);
  }
  return {s.begin(), s.end()};
}

std::string Vfs::describe() const {
  std::string s;
  for (auto& L : layers_) {
    size_t n = L.kind == Layer::PakFile ? L.pakIndex.size() : L.files.size();
    s += "  " + L.label + " (" + std::to_string(n) + " files)\n";
  }
  return s;
}

}
