#include "editor/editor.h"
#include "content.h"
#include "gamepaths.h"
#include "log.h"
#include "image.h"
#include "mods/manifest.h"
#include "ui/cfg.h"
#include "vfs.h"

#include <algorithm>
#include <cstdio>
#include <climits>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

namespace pp::ed {

static const struct { const char* name; Tool t; } kTools[] = {
    {"select", Tool::Select}, {"pan", Tool::Pan}, {"blue", Tool::PegBlue}, {"orange", Tool::PegOrange}, {"green", Tool::PegGreen},
    {"purple", Tool::PegPurple}, {"round", Tool::PegRound}, {"bumper", Tool::Bumper}, {"brick_straight", Tool::BrickStraight},
    {"brick_curved", Tool::BrickCurved}, {"poly", Tool::Poly}, {"line", Tool::Line}, {"hole", Tool::Hole}, {"hole_exit", Tool::HoleExit}, {"emitter", Tool::Emitter}, {"follow", Tool::Follow}};
const char* toolName(Tool t) {
  for (auto& e : kTools)
    if (e.t == t) return e.name;
  return "select";
}
bool toolFromName(const std::string& s, Tool& t) {
  for (auto& e : kTools)
    if (s == e.name) { t = e.t; return true; }
  return false;
}

Editor::Editor() {
  brick.type = 0; brick.sub = 4; brick.radius = 80; brick.ext = 30; brick.thick = 20; brick.rot = -90;
  brickStraight.type = 5; brickStraight.sub = 2; brickStraight.radius = 60; brickStraight.thick = 20; brickStraight.rot = 90;
  newLevel("NewLevel");
}

static bool slurp(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) return false;
  const auto size = f.tellg();
  if (size < 0) return false;
  out.resize((size_t)size);
  f.seekg(0);
  f.read((char*)out.data(), (std::streamsize)out.size());
  return (bool)f && f.gcount() == (std::streamsize)out.size();
}
static bool spit(const fs::path& p, const void* data, size_t n) {
  std::error_code ec;
  if (!p.parent_path().empty()) fs::create_directories(p.parent_path(), ec);
  if (ec) return false;
  std::ofstream f(p, std::ios::binary);
  if (!f) return false;
  f.write((const char*)data, (std::streamsize)n);
  f.flush();
  f.close();
  return (bool)f;
}
static bool spit(const fs::path& p, const std::string& s) { return spit(p, s.data(), s.size()); }

static bool saveLevelFiles(const fs::path& dat, const std::vector<uint8_t>& bytes, const fs::path& ini, const std::string& text, std::string* err, const fs::path& indexPath = {}, const std::string* index = nullptr, const std::string* metadata = nullptr) {
  auto fail = [&](const std::string& message) { if (err) *err = message; return false; };
  if (dat == ini) return fail("DAT and INI paths must differ");
  struct Output {
    fs::path path, staged, backup;
    const void* data;
    size_t size;
    bool existed = false;
    std::vector<uint8_t> old;
  };
  std::vector<Output> outputs = {{dat, "new.dat", "old.dat", bytes.data(), bytes.size()}, {ini, "new.ini", "old.ini", text.data(), text.size()}};
  if (index) outputs.push_back({indexPath, "new.cfg", "old.cfg", index->data(), index->size()});
  if (metadata) outputs.push_back({dat.parent_path().parent_path() / "mod.ini", "new.mod.ini", "old.mod.ini", metadata->data(), metadata->size()});
  std::error_code ec;
  for (auto& output : outputs) {
    output.existed = fs::exists(output.path, ec);
    if (ec) return fail("cannot inspect " + pathToUtf8(output.path) + ": " + ec.message());
    if (!output.existed) continue;
    if (!fs::is_regular_file(output.path, ec) || ec) return fail("not a regular file: " + pathToUtf8(output.path));
    std::fstream probe(output.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!probe) return fail("cannot write " + pathToUtf8(output.path));
    probe.flush();
    probe.close();
    if (!probe) return fail("cannot close " + pathToUtf8(output.path));
    if (!slurp(output.path, output.old)) return fail("cannot read " + pathToUtf8(output.path));
  }
  const fs::path parent = dat.parent_path().empty() ? fs::path(".") : dat.parent_path();
  fs::create_directories(parent, ec);
  if (ec) return fail("cannot create " + pathToUtf8(parent) + ": " + ec.message());
  struct Staging {
    fs::path path;
    bool keep = false;
    ~Staging() { std::error_code ec; if (!keep && !path.empty()) fs::remove_all(path, ec); }
  } staging;
  for (size_t i = 0;; ++i) {
    const fs::path candidate = parent / (pathToUtf8(dat.filename()) + ".save-" + std::to_string(i));
    if (fs::create_directory(candidate, ec)) { staging.path = candidate; break; }
    if (ec) return fail("cannot stage " + pathToUtf8(dat) + ": " + ec.message());
  }
  for (auto& output : outputs) {
    output.staged = staging.path / output.staged;
    output.backup = staging.path / output.backup;
    if (!spit(output.staged, output.data, output.size)) return fail("cannot stage " + pathToUtf8(output.path));
    if (output.existed && !spit(output.backup, output.old.data(), output.old.size())) return fail("cannot back up " + pathToUtf8(output.path));
  }
  for (size_t i = 0; i < outputs.size(); ++i) {
    fs::rename(outputs[i].staged, outputs[i].path, ec);
    if (!ec) continue;
    std::string message = "cannot replace " + pathToUtf8(outputs[i].path) + ": " + ec.message();
    for (size_t j = i; j > 0; --j) {
      auto& output = outputs[j - 1];
      if (output.existed) fs::rename(output.backup, output.path, ec);
      else fs::remove(output.path, ec);
      if (ec) {
        staging.keep = true;
        message += "; cannot restore " + pathToUtf8(output.path) + ": " + ec.message() + "; recovery files: " + pathToUtf8(staging.path);
      }
    }
    return fail(message);
  }
  return true;
}

static std::vector<const Node*> builtNodes(const Level& L);

std::string Editor::customType(const NodeP& n) const {
  auto it = customTypes.find(n.get());
  return it == customTypes.end() ? std::string() : it->second;
}

void Editor::setCustomType(const std::string& key) {
  std::string k;
  for (char c : key)
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != ',' && c != '=') k.push_back(c);
  int n = 0;
  for (auto& node : sel) {
    if (!pegOf(*node)) continue;
    if (k.empty()) customTypes.erase(node.get());
    else customTypes[node.get()] = k;
    n++;
  }
  if (n) {
    metaDirty = true;
    status = k.empty() ? "custom type removed" : "custom type " + k + " on " + std::to_string(n) + " peg(s)";
    changed();
  }
}

std::string Editor::collisionRole(const NodeP& n) const {
  if (!n || (n->type != 2 && n->type != 3 && n->type != 5 && n->type != 6) || n->str("id") == "flipper") return "";
  if (!n->flag("collidable")) return "decoration";
  if (pegTypeOf(*n) > 0) return "peg";
  if (n->f("bouncy") > 1.0f || (n->type == 5 && !n->has("bouncy"))) return "bumper";
  return "terrain";
}

bool Editor::setCollisionRole(const std::string& role) {
  if (role != "peg" && role != "bumper" && role != "terrain" && role != "decoration") {
    status = "unknown collision role";
    return false;
  }
  if (sel.empty()) { status = "select an object first"; return false; }
  const auto live = objects();
  std::vector<NodeP> touch;
  for (const auto& n : sel) {
    const std::string old = collisionRole(n);
    if (old.empty() || std::find(live.begin(), live.end(), n) == live.end() || (role == "peg" && n->type != 5 && n->type != 6)) {
      status = "collision role is not supported by every selected object";
      return false;
    }
    if (old != role || (role != "peg" && pegOf(*n)))
      if (std::find(touch.begin(), touch.end(), n) == touch.end()) touch.push_back(n);
  }
  if (touch.empty()) return true;
  cancelGesture();
  begin("Collision role: " + role, touch);
  for (auto& n : touch) {
    n->flags["collidable"] = role != "decoration";
    setPegType(*n, role == "peg" ? std::max(1, pegTypeOf(*n)) : 0);
    if (role == "bumper") {
      putNum(*n, "bouncy", Prim::F32, 1.2);
      putNum(*n, "sound", Prim::U8, 4);
    } else if (role == "terrain" || role == "peg") {
      if (role == "terrain") putNum(*n, "bouncy", Prim::F32, 0.75);
      else eraseField(*n, "bouncy");
      if (n->i("sound") == 4) eraseField(*n, "sound");
    }
  }
  commit();
  status = "collision role: " + role;
  return true;
}

void Editor::setVfs(const Vfs* v) {
  vfs = v;
  emitterImage = imageExists("images/levels/flame4") ? "images/levels/flame4" : "";
}
bool Editor::imageExists(const std::string& path) const {
  if (!vfs || path.empty()) return false;
  for (const char* ext : {".jp2", ".jpg", ".png", ".gif", ".tga", ".j2k"})
    if (vfs->exists(path + ext)) return true;
  return false;
}

static const char* const kArtExt[] = {".tga", ".jpg", ".png", ".gif", ".j2k", ".jp2"};
static bool isArtExt(std::string e) {
  for (auto& c : e) c = (char)std::tolower((unsigned char)c);
  for (const char* x : kArtExt)
    if (e == x) return true;
  return false;
}
static std::string lowerS(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
static std::string slashes(std::string s) {
  for (auto& c : s)
    if (c == '\\') c = '/';
  return s;
}
static std::vector<std::string> artPrefixes(Editor::ArtSlot s) {
  if (s == Editor::ArtSlot::Background) return {"levels/"};
  return {"", "images/levels/", "images/game/", "levels/"};
}
std::string Editor::artPath(ArtSlot slot, const std::string& value) const {
  if (!vfs || value.empty()) return "";
  for (auto& pre : artPrefixes(slot))
    if (imageExists(pre + value)) return pre + value;
  return "";
}
bool Editor::hasImportedBackground() const {
  std::string_view name(meta.background);
  if (!name.starts_with("imported_")) return false;
  name.remove_prefix(9);
  auto canonical = [](std::string_view s, auto value) {
    if (s.empty() || (s.size() > 1 && s.front() == '0')) return false;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    return ec == std::errc{} && p == s.data() + s.size();
  };
  const auto split = name.find('_');
  if (!canonical(name.substr(0, split), uint64_t{})) return false;
  if (split == name.npos) return true;
  const auto suffix = name.substr(split + 1);
  return suffix != "0" && canonical(suffix, unsigned{});
}

static std::string backgroundKey(const std::vector<uint8_t>& bytes) {
  return "imported_" + std::to_string(fnv1a(bytes.data(), bytes.size()));
}

static bool storeBackground(const fs::path& folder, const Vfs* vfs, const std::vector<uint8_t>& bytes, const std::string& ext, std::string& name, std::string* err, bool referenceOnly = false) {
  auto fail = [&] { if (err) *err = "Cannot store the background in " + pathToUtf8(folder); return false; };
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) return fail();
  const std::string base = backgroundKey(bytes);
  for (unsigned i = 0;; ++i) {
    const std::string stem = referenceOnly ? name : base + (i ? "_" + std::to_string(i) : "");
    const std::string leaf = stem + ext;
    bool conflict = false, reusable = false;
    for (const auto& pre : {stem, "_" + stem, stem + "_"}) for (const char* suffix : kArtExt) {
      const auto file = pre + suffix;
      fs::path actual;
      if (!resolveDiskSpelling(folder / file, actual, err)) return false;
      if (fs::exists(actual, ec)) {
        std::vector<uint8_t> existing;
        if (file == leaf && fs::is_regular_file(fs::symlink_status(actual, ec)) && fs::file_size(actual, ec) == bytes.size() && slurp(actual, existing) && existing == bytes) reusable = true;
        else conflict = true;
      }
      if (vfs && vfs->exists("levels/" + file)) {
        auto hit = file == leaf ? vfs->read("levels/" + file, bytes.size()) : std::nullopt;
        if (!hit || hit->bytes != bytes) conflict = true;
      }
    }
    if (referenceOnly) return !conflict || fail();
    if (conflict) continue;
    if (!reusable) {
      const fs::path target = folder / leaf;
#ifdef _WIN32
      FILE* f = _wfopen(target.c_str(), L"wbx");
#else
      FILE* f = std::fopen(target.c_str(), "wbx");
#endif
      if (!f) return fail();
      const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
      if (std::fclose(f) != 0 || !written) { fs::remove(target, ec); return fail(); }
    }
    name = stem;
    return true;
  }
}

bool Editor::importBackground(const fs::path& requested, const fs::path& folder, std::string* err) {
  fs::path file;
  if (!resolveDiskSpelling(requested, file, err)) return false;
  std::string ext = lowerS(pathToUtf8(file.extension()));
  if (ext == ".jpeg") ext = ".jpg";
  std::vector<uint8_t> bytes;
  Image decoded;
  std::error_code ec;
  if (!isArtExt(ext) || !fs::is_regular_file(file, ec) || fs::file_size(file, ec) > INT_MAX || !slurp(file, bytes) || bytes.empty() || !decodeImage(bytes, decoded)) {
    if (err) *err = "Cannot read this image. Choose PNG, JPEG, GIF, TGA, JP2 or J2K.";
    return false;
  }
  std::string name;
  if (!storeBackground(folder, vfs, bytes, ext, name, err)) return false;
  backgroundDir_ = folder;
  return setArt(ArtSlot::Background, name);
}

bool Editor::prepareBackground(const fs::path& folder, std::string& name, std::string* err) const {
  if (!hasImportedBackground()) return true;
  const auto key = name.substr(0, name.find('_', 9));
  if (!meta.baseLink.name.empty()) {
    for (const char* ext : kArtExt) {
      const auto path = "levels/" + name + ext;
      const auto* owner = vfs ? vfs->owner(path) : nullptr;
      if (!owner) continue;
      auto hit = vfs->readFrom(*owner, path);
      if (owner->provider == meta.baseLink.provider && hit && backgroundKey(hit->bytes) == key)
        return storeBackground(folder, vfs, hit->bytes, ext, name, err, true);
      break;
    }
    if (err) *err = "Linked background " + name + " must be available from " + meta.baseLink.provider;
    return false;
  }
  if (!backgroundDir_.empty()) for (const char* ext : kArtExt) {
    std::vector<uint8_t> bytes;
    fs::path file;
    if (!resolveDiskSpelling(backgroundDir_ / (name + ext), file, err)) return false;
    if (slurp(file, bytes) && backgroundKey(bytes) == key)
      return storeBackground(folder, vfs, bytes, ext, name, err);
  }
  if (vfs) for (const char* ext : kArtExt) {
    auto hit = vfs->read("levels/" + name + ext);
    if (hit && backgroundKey(hit->bytes) == key) return storeBackground(folder, vfs, hit->bytes, ext, name, err);
  }
  if (err) *err = "Cannot find imported background " + name;
  return false;
}

std::vector<Editor::ArtItem> Editor::artChoices(ArtSlot slot, const fs::path& modDir) const {
  std::vector<ArtItem> out;
  std::set<std::string> seen;
  std::map<std::string, std::string> levelCase;
  if (vfs)
    for (auto& s : listStageLevels(*vfs)) levelCase[normKey(s.name)] = s.name;
  auto add = [&](const std::string& key, const fs::path* real, const std::string& src) {
    size_t dot = key.rfind('.');
    if (dot == std::string::npos || !isArtExt(key.substr(dot))) return;
    std::string stem = key.substr(0, dot), value, last = real ? pathToUtf8(real->stem()) : std::string();
    if (slot == ArtSlot::Background) {
      if (stem.rfind("LEVELS\\", 0) != 0 || stem.find('\\', 7) != std::string::npos) return;
      std::string nm = stem.substr(7);
      if (nm.empty() || nm[0] == '_') return;
      auto lc = levelCase.find(nm);
      value = !last.empty() ? last : lc != levelCase.end() ? lc->second : lowerS(nm);
    } else {
      std::string pre;
      if (stem.rfind("IMAGES\\LEVELS\\", 0) == 0) { stem = stem.substr(14); pre = slot == ArtSlot::Emitter ? "images/levels/" : ""; }
      else if (stem.rfind("IMAGES\\GAME\\", 0) == 0) { stem = stem.substr(12); pre = "images/game/"; }
      else if (stem.rfind("IMAGES\\", 0) == 0) { stem = stem.substr(7); pre = "images/"; }
      else return;
      if (stem.empty()) return;
      size_t sl = stem.rfind('\\');
      std::string dir = sl == std::string::npos ? std::string() : slashes(lowerS(stem.substr(0, sl + 1)));
      std::string leaf = !last.empty() ? last : lowerS(sl == std::string::npos ? stem : stem.substr(sl + 1));
      value = pre + dir + leaf;
    }
    if (!seen.insert(normKey(value)).second) return;
    out.push_back({value, src, real ? pathToUtf8(*real) : std::string()});
  };
  if (vfs)
    for (auto& L : vfs->layers()) {
      std::string src = L.isMod ? L.label : "game";
      for (auto& [k, p] : L.files) add(k, &p, src);
      for (auto& [k, _] : L.pakIndex) add(k, nullptr, src);
    }
  std::error_code ec;
  if (!modDir.empty() && fs::is_directory(modDir, ec))
    for (const char* sub : {"levels", "images"})
      for (fs::recursive_directory_iterator it(modDir / sub, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) {
          fs::path rel = fs::relative(it->path(), modDir, ec);
          fs::path real = it->path();
          add(normKey(genericUtf8(rel)), &real, "mod folder " + pathToUtf8(modDir.filename()));
        }
  std::sort(out.begin(), out.end(), [](const ArtItem& a, const ArtItem& b) { return lowerS(a.value) < lowerS(b.value); });
  return out;
}
bool Editor::setArt(ArtSlot slot, const std::string& value) {
  if (slot == ArtSlot::Background) {
    setMeta("background", value);
    status = value.empty() ? "level art: the level's own picture" : "level art: " + value;
    return true;
  }
  const char* key = slot == ArtSlot::Emitter ? "emitter_image" : "image";
  if (sel.empty()) { status = "select an object first"; return false; }
  bool ok = setProp(key, value);
  status = ok ? (value.empty() ? "picture removed" : "picture: " + value) : "the selected objects have no picture setting";
  return ok;
}
bool Editor::planArtCopies(const fs::path& modDir, std::map<fs::path, fs::path>& copies, std::string* err) const {
  if (!vfs) return true;
  struct Ref { ArtSlot slot; std::string value; };
  std::vector<Ref> refs;
  if (!hasImportedBackground()) refs.push_back({ArtSlot::Background, meta.background.empty() ? meta.name : meta.background});
  for (auto& n : objects()) {
    if (n->type == 9) { if (!n->str("emitter_image").empty()) refs.push_back({ArtSlot::Emitter, n->str("emitter_image")}); }
    else if (!n->str("image").empty()) refs.push_back({ArtSlot::Object, n->str("image")});
  }
  std::set<std::string> done;
  std::error_code ec;
  for (auto& r : refs) {
    std::string base = artPath(r.slot, r.value);
    if (base.empty()) continue;
    for (const char* ext : kArtExt) {
      std::string key = normKey(base + ext);
      if (!done.insert(key).second) continue;
      for (auto& L : vfs->layers()) {
        auto f = L.files.find(key);
        if (f == L.files.end() && !L.pakIndex.count(key)) continue;
        if (L.isMod && f != L.files.end()) {
          fs::path src, dst;
          if (!resolveDiskSpelling(f->second, src, err)) return false;
          const auto rel = src.lexically_relative(L.root);
          fs::path dir = modDir / rel.parent_path();
          if (rel.has_parent_path() && !resolveDiskSpelling(dir, dir, err)) return false;
          if (!resolveDiskSpelling(dir / rel.filename(), dst, err)) return false;
          if (L.root != modDir && !fs::exists(dst, ec)) copies.emplace(dst, src);
        }
        break;
      }
    }
  }
  return true;
}

void Editor::resetHistory() {
  savedMod_ = false;
  customTypes.clear();
  undo_.clear();
  redo_.clear();
  open_ = false;
  clean_ = 0;
  metaDirty = false;
  sel.clear();
  polyPts_.clear();
  cornerPoly_.reset();
  cornerIdx_ = -1;
  g_ = G();
  issues.clear();
  changed();
}

bool Editor::resolveBase(BaseLink& link, std::string* err) {
  uint64_t hash;
  if (!link.name.empty()) {
    if (vfs) return readBaseLevel(*vfs, link, base, hash, err);
    if (err) *err = "Mount the pack containing " + link.name + " before opening this level";
    return false;
  }
  if (!meta.baseLink.name.empty()) {
    BaseLink stock{"_base", ""};
    if (!vfs || !readBaseLevel(*vfs, stock, base, hash, nullptr)) base = syntheticBase();
  }
  return true;
}

void Editor::newLevel(const std::string& name) {
  stageError_.clear();
  BaseLink stock;
  resolveBase(stock, nullptr);
  level = std::make_shared<Level>();
  level->version = 0x42;
  level->mode = 1;
  meta = LevelMeta();
  meta.name = name;
  meta.title = name;
  backgroundDir_.clear();
  savedPath.clear();
  resetHistory();
  status = "new level " + name;
}

std::shared_ptr<Level> Editor::syntheticBase() {
  auto L = std::make_shared<Level>();
  L->version = 0x42;
  L->mode = 1;
  auto wall = [&](float x, float y, float hw, float hh) {
    NodeP n = newPoly(x, y, {{-hw, -hh}, {-hw, hh}, {hw, hh}, {hw, -hh}});
    putStr(*n, "id", "wall");
    putNum(*n, "bouncy", Prim::F32, 0.75);
    Val v; v.k = VK::Ptr; v.n = n;
    L->entries.push_back(v);
  };
  wall(-20, 240, 19.5f, 999.5f);
  wall(670, 240, 19.5f, 999.5f);
  wall(325, -20, 999.5f, 19.5f);
  NodeP h = newHole(326, 556, 130, 20, false, 0);
  putStr(*h, "id", "freeball");
  attachMover(*L, *h, 2);
  Node* m = moverOf(*L, *h);
  m->setF("anchor_x", 326);
  m->setF("anchor_y", 556);
  putNum(*m, "radius", Prim::I16, 260);
  putNum(*m, "time", Prim::U16, 600);
  putNum(*m, "phase", Prim::F32, 0.25);
  Val v; v.k = VK::Ptr; v.n = h;
  L->entries.push_back(v);
  auto bytes = writeLevel(*L);
  auto back = parseLevel(bytes);
  return std::make_shared<Level>(std::move(*back));
}

std::shared_ptr<Level> Editor::bake() const {
  try {
    auto bytes = writeLevel(*level);
    auto lv = parseLevel(bytes);
    if (!lv) return nullptr;
    return std::make_shared<Level>(std::move(*lv));
  } catch (const std::exception&) {
    return nullptr;
  }
}

struct StageSelection {
  ui::CfgItem level;
  int matches = 0;
  std::vector<std::vector<std::string>> rows;
};

static StageSelection selectStage(const std::string& text, const std::string& name) {
  StageSelection result;
  for (const auto& stage : ui::cfgParse(text)) {
    if (!stage.isGroup || lowerS(stage.key) != "stage") continue;
    int count = 0, selected = 0;
    std::vector<std::vector<std::string>> rows;
    for (const auto& row : stage.kids) {
      if (row.isGroup) continue;
      auto values = ui::cfgValues(row.label);
      if (values.empty()) continue;
      const auto key = lowerS(row.key);
      if (key == "level") {
        ++count;
        auto& file = values.front();
        file.erase(file.find_last_not_of(" \t\r\n") + 1);
        file.erase(0, file.find_first_not_of(" \t\r\n"));
        if (lowerS(file) != lowerS(name)) continue;
        ++result.matches;
        result.level = row;
        selected = count;
      } else if (key == "dialog") {
        if (!selected || values.size() < 2 || std::atoi(values[0].c_str()) != selected) continue;
        values[0] = "1";
      }
      values.insert(values.begin(), row.key);
      rows.push_back(std::move(values));
    }
    if (selected) result.rows = std::move(rows);
  }
  return result;
}

static bool boardFiles(const fs::path& requested, fs::path& dat, fs::path& ini, std::string* err) {
  auto dir = requested.parent_path();
  if (normKey(genericUtf8(dir.filename())) == "LEVELS" && !resolveDiskSpelling(dir, dir, err)) return false;
  if (!resolveDiskSpelling(dir / requested.filename(), dat, err)) return false;
  return resolveDiskSpelling(fs::path(dat).replace_extension(".ini"), ini, err);
}

static bool readSourceStage(const fs::path& file, std::string& text, std::string* err) {
  if (file.empty()) return true;
  fs::path index;
  if (!resolveDiskSpelling(file.parent_path() / "mod_levels.cfg", index, err)) return false;
  std::error_code ec;
  if (!fs::exists(index, ec) && !ec) return true;
  std::vector<uint8_t> bytes;
  if (ec || !slurp(index, bytes)) { if (err) *err = "cannot read stage list: " + pathToUtf8(index); return false; }
  text.assign(bytes.begin(), bytes.end());
  return true;
}

void Editor::collectSourceStage(const std::string& text) {
  auto selection = selectStage(text, meta.name);
  if (!selection.matches) return;
  if (selection.matches > 1 || !meta.stageRows.empty()) stageError_ = "ambiguous stage entry for " + meta.name;
  else meta.stageRows = std::move(selection.rows);
}

void Editor::finishLoad(const LevelBundle& lb, const Layer* owner, const fs::path& file, const std::string& stage) {
  linkRefs(*level);
  resetHistory();
  meta = LevelMeta();
  meta.baseLink = lb.baseLink;
  meta.name = lb.name;
  meta.title = lb.title;
  meta.background = lb.background;
  meta.master = lb.master;
  meta.boardWidth = lb.cfg.boardWidth;
  meta.balls = lb.cfg.balls;
  meta.orange = lb.cfg.orange;
  meta.physics = lb.cfg.physics;
  stageError_.clear();
  savedPath.clear();
  if (!file.empty()) meta.name = pathToUtf8(file.stem());
  backgroundDir_ = file.parent_path();
  if (!file.empty()) {
    if (backgroundDir_.empty()) backgroundDir_ = ".";
    collectSourceStage(stage);
    if (!owner || (owner->isMod && !owner->fromZip)) {
      savedPath = file;
      savedMod_ = owner || (normKey(pathToUtf8(file.parent_path().filename())) == "LEVELS" && (!meta.stageRows.empty() || !stageError_.empty()));
    }
  }
  auto nodes = builtNodes(*level);
  for (auto& [idx, key] : lb.cfg.pegTypes)
    if (idx >= 0 && (size_t)idx < nodes.size() && pegOf(*nodes[(size_t)idx])) customTypes[nodes[(size_t)idx]] = key;
}

bool Editor::loadFromVfs(const Vfs& vfs, const std::string& nameOrPath, std::string* err) {
  LevelBundle lb;
  std::string e;
  if (!loadLevelBundle(vfs, nameOrPath, lb, &e)) {
    if (err) *err = e;
    return false;
  }
  std::shared_ptr<Level> copy;
  try {
    auto bytes = writeLevel(*lb.level);
    auto lv = parseLevel(bytes, &e);
    if (!lv) { if (err) *err = e; return false; }
    copy = std::make_shared<Level>(std::move(*lv));
  } catch (const std::exception& ex) {
    if (err) *err = ex.what();
    return false;
  }
  fs::path file;
  const bool disk = lb.name != nameOrPath;
  if (disk) file = pathFromUtf8(lb.source);
  const auto key = "levels/" + lb.name;
  const auto* owner = vfs.owner(key + ".dat", file);
  if (file.empty() && owner && owner->kind == Layer::Dir) file = owner->files.at(normKey(key + ".dat"));
  fs::path side;
  if (!file.empty() && !boardFiles(file, file, side, err)) return false;
  std::string stage;
  if ((disk || (owner && owner->isMod)) && !readSourceStage(file, stage, err)) return false;
  level = copy;
  base = lb.base;
  finishLoad(lb, owner, file, stage);
  if (!disk && owner && !owner->isMod) {
    if (owner->isLevelPack) {
      for (const auto& [key, i] : owner->pakIndex) {
        if (!key.ends_with(".CFG") || key.find('\\') != key.npos) continue;
        const auto& e = owner->pak->entries[i];
        collectSourceStage(std::string(owner->pak->data.begin() + e.offset, owner->pak->data.begin() + e.offset + e.size));
      }
    } else if (auto stages = vfs.read("levels/stages.cfg")) {
      collectSourceStage(std::string(stages->bytes.begin(), stages->bytes.end()));
    }
  }
  status = "opened " + lb.name + " (" + std::to_string(objects().size()) + " objects)";
  return true;
}

bool Editor::loadDatFile(const fs::path& requested, std::string* err) {
  fs::path p, side;
  std::string stage;
  if (!boardFiles(requested, p, side, err) || !readSourceStage(p, stage, err)) return false;
  std::vector<uint8_t> bytes;
  if (!slurp(p, bytes)) { if (err) *err = "cannot read " + pathToUtf8(p); return false; }
  std::string e;
  auto lv = parseLevel(bytes, &e);
  if (!lv) { if (err) *err = e; return false; }
  LevelBundle lb;
  lb.name = lb.title = pathToUtf8(p.stem());
  const auto* owner = vfs ? vfs->owner("levels/" + lb.name + ".dat", p) : nullptr;
  std::error_code ec;
  if (fs::exists(side, ec)) {
    if (!slurp(side, bytes)) { if (err) *err = "cannot read " + pathToUtf8(side); return false; }
    const std::string sidecar(bytes.begin(), bytes.end());
    auto ini = readIni(sidecar);
    if (!baseLinkFromIni(ini, vfs, owner, lb.baseLink, err)) return false;
    readLevelSettings(lb, ini, sidecar);
  }
  if (ec) { if (err) *err = "cannot read " + pathToUtf8(side) + ": " + ec.message(); return false; }
  if (!resolveBase(lb.baseLink, err)) return false;
  level = std::make_shared<Level>(std::move(*lv));
  finishLoad(lb, owner, p, stage);
  status = "opened " + pathToUtf8(p);
  return true;
}

static bool exportBaseLink(const BaseLink& link, const std::string& levelName, const Vfs* vfs, const fs::path& dir, std::string& text, std::string* err, const Layer** donor = nullptr) {
  if (link.name.empty()) return true;
  auto fail = [&](const std::string& why) { if (err) *err = link.name + ": " + why; return false; };
  if (levelName.starts_with('_')) return fail("a base level cannot itself link another base");
  const auto leaf = link.name + ".dat";
  if (!vfs || link.hash.empty()) return fail("mount the original base pack to export this level");
  uint64_t hash;
  auto hit = readBaseFile(*vfs, link, hash, err);
  if (!hit) return false;
  std::error_code ec;
  if (donor) *donor = hit->layer;
  fs::path localFile;
  if (!resolveDiskSpelling(dir / leaf, localFile, err)) return false;
  if (fs::exists(localFile, ec)) {
    std::vector<uint8_t> local;
    if (!fs::is_regular_file(fs::symlink_status(localFile, ec)) || !slurp(localFile, local) || local != hit->bytes) return fail("destination base differs or cannot be read");
  }
  if (ec) return fail("cannot inspect the destination base");
  text = "base = " + link.name + "\nbase_hash = " + link.hash + "\nbase_provider = " + link.provider + "\n";
  return true;
}

bool Editor::saveDat(const fs::path& requested, std::string* err) {
  fs::path p, side;
  if ((requested != savedPath && !checkSavedSpelling(err)) || !boardFiles(requested, p, side, err)) return false;
  try {
    if (lowerS(pathToUtf8(p.extension())) == ".ini") { if (err) *err = "DAT target must not use the INI extension"; return false; }
    auto bytes = writeLevel(*level);
    std::vector<uint8_t> old;
    if (fs::exists(side) && !slurp(side, old)) { if (err) *err = "cannot read " + pathToUtf8(side); return false; }
    std::istringstream in(std::string(old.begin(), old.end()));
    std::string line, text;
    if (!exportBaseLink(meta.baseLink, pathToUtf8(p.stem()), vfs, p.parent_path(), text, err)) return false;
    std::string background = meta.background;
    if (!prepareBackground(p.has_parent_path() ? p.parent_path() : fs::path("."), background, err)) return false;
    while (std::getline(in, line)) {
      auto entry = readIni(line);
      if (entry.count("base") || entry.count("base_hash") || entry.count("base_provider")) continue;
      if (!entry.count("board_width") && !entry.count("background")) text += line + "\n";
    }
    if (!background.empty()) text += "background = " + background + "\n";
    text += "board_width = " + std::to_string(meta.boardWidth) + "\n";
    if (!saveLevelFiles(p, bytes, side, text, err)) return false;
  } catch (const std::exception& ex) {
    if (err) *err = ex.what();
    return false;
  }
  savedPath = p;
  savedMod_ = false;
  clean_ = (int)undo_.size();
  metaDirty = false;
  status = "saved " + pathToUtf8(p);
  return true;
}

bool Editor::checkSavedSpelling(std::string* err) const {
  if (savedPath.empty()) return true;
  fs::path dat, ini, index;
  return boardFiles(savedPath, dat, ini, err) && (!savedMod_ || resolveDiskSpelling(dat.parent_path() / "mod_levels.cfg", index, err));
}

bool Editor::saveCurrent(std::string* err) {
  if (savedPath.empty()) { if (err) *err = "save needs a path (or use savemod)"; return false; }
  if (!savedMod_) return saveDat(savedPath, err);
  if (normKey(genericUtf8(savedPath.stem())) != normKey(meta.name)) { if (err) *err = "level name changed; choose a new mod destination"; return false; }
  return saveToMod(savedPath.parent_path().parent_path(), err);
}

bool Editor::saveJson(const fs::path& requested, std::string* err) {
  fs::path p;
  if (!checkSavedSpelling(err) || !resolveDiskSpelling(requested, p, err)) return false;
  if (!stageError_.empty()) { if (err) *err = stageError_; return false; }
  auto exported = meta;
  std::string link;
  if (!exportBaseLink(meta.baseLink, meta.name, vfs, p.parent_path(), link, err)) return false;
  if (!prepareBackground(p.has_parent_path() ? p.parent_path() : fs::path("."), exported.background, err)) return false;
  std::string text = levelToJson(*level, exported).dump(2) + "\n";
  if (!spit(p, text)) { if (err) *err = "cannot write " + pathToUtf8(p); return false; }
  status = "exported " + pathToUtf8(p);
  return true;
}

bool Editor::loadJson(const fs::path& requested, std::string* err) {
  fs::path p;
  if (!resolveDiskSpelling(requested, p, err)) return false;
  std::vector<uint8_t> bytes;
  if (!slurp(p, bytes)) { if (err) *err = "cannot read " + pathToUtf8(p); return false; }
  J j;
  std::string e;
  if (!parseJson(std::string(bytes.begin(), bytes.end()), j, &e)) { if (err) *err = pathToUtf8(p) + ": " + e; return false; }
  Level lv;
  LevelMeta m;
  if (!levelFromJson(j, lv, m, &e)) { if (err) *err = e; return false; }
  if (!resolveBase(m.baseLink, err)) return false;
  level = std::make_shared<Level>(std::move(lv));
  meta = std::move(m);
  stageError_.clear();
  backgroundDir_ = p.has_parent_path() ? p.parent_path() : fs::path(".");
  savedPath.clear();
  resetHistory();
  status = "imported " + pathToUtf8(p);
  return true;
}

using IndexEntries = std::vector<std::string>;

static std::string editorIndex(const fs::path& modDir, const IndexEntries& levels) {
  std::string out;
  for (size_t i = 0; i < levels.size(); i += 5) {
    std::string stageName = pathToUtf8(modDir.filename()) + (i ? " " + std::to_string(i / 5 + 1) : "");
    out += "Stage\n{\nName: \"" + stageName + "\"\nDesc: \"Levels made with the PegglePort level editor\"\n";
    for (size_t j = i; j < std::min(levels.size(), i + 5); j++) out += "\t" + levels[j] + "\n";
    out += "}\n";
  }
  return out;
}

static std::string cfgQuote(const std::string& field) {
  std::string out = "\"";
  for (char c : field) {
    if (c == '\n') out += "\\n";
    else {
      if (c == '\\' || c == '"') out += '\\';
      out += c;
    }
  }
  return out + '"';
}

static bool replaceStageTitle(std::string& text, const ui::CfgItem& row, const LevelMeta& meta) {
  auto values = ui::cfgValues(row.label);
  if (values.size() < 2) return false;
  if (values[1] == meta.title) return true;
  auto comma = row.label.find(',');
  if (comma == text.npos || ui::cfgValues(row.label.substr(0, comma)) != std::vector<std::string>{values[0]}) return false;
  auto pos = row.label.find_first_not_of(" \t", comma + 1);
  if (pos == text.npos) return false;
  size_t len = values[1].size();
  if (row.label[pos] == '"') {
    std::istringstream in(row.label.substr(pos));
    std::string title;
    if (!(in >> std::quoted(title)) || in.tellg() < 0) return false;
    len = (size_t)in.tellg();
  } else if (row.label.compare(pos, len, values[1]) != 0) return false;
  auto next = row.label.find_first_not_of(" \t\r", pos + len);
  if (next != text.npos && row.label[next] != ',') return false;
  const auto quoted = cfgQuote(meta.title);
  auto changed = row.label;
  changed.replace(pos, len, quoted);
  values[1] = meta.title;
  if (ui::cfgValues(changed) != values) return false;
  size_t start = 0;
  for (int i = 1; i < row.line; ++i) {
    start = text.find('\n', start);
    if (start == text.npos) return false;
    ++start;
  }
  const auto line = text.substr(start, text.find('\n', start) - start);
  const auto label = line.rfind(row.label);
  if (label == text.npos) return false;
  auto probe = text;
  probe.insert(start + label + pos, 1, 'X');
  auto live = row.label;
  live.insert(pos, 1, 'X');
  if (selectStage(probe, meta.name).level.label != live) return false;
  text.replace(start + label + pos, len, quoted);
  return true;
}

static std::string singleBoardStage(const LevelMeta& meta) {
  std::string out = "Stage\n{\n";
  for (auto row : meta.stageRows) {
    const bool level = lowerS(row[0]) == "level";
    if (level) {
      row[1] = meta.name;
      if (meta.stageTitleEdited) { if (row.size() < 3) row.resize(3); row[2] = meta.title; }
    }
    out += row[0] + ": ";
    for (size_t i = 1; i < row.size(); ++i) {
      if (i > 1) out += ", ";
      out += level && i == 1 ? row[i] : cfgQuote(row[i]);
    }
    out += '\n';
  }
  return out + "}\n";
}

static constexpr int kEditorPackPriority = 10;

static bool checkModDestination(const Vfs& vfs, const Layer& donor, const fs::path& dir, const mods::Manifest& dest, const std::string& name, std::string* err) {
  auto refuse = [&](const std::string& why) { if (err) *err = why; return false; };
  std::error_code ec;
  const bool samePack = fs::equivalent(dir, donor.root, ec);
  if (samePack != ("mod:" + dest.id == donor.provider))
    return refuse("destination pack ID conflicts with the required donor " + donor.provider);
  const auto key = normKey("levels/" + name);
  if (samePack || (!donor.files.contains(key + ".DAT") && !donor.files.contains(key + ".INI"))) return true;
  int priority = dest.format == "none" ? kEditorPackPriority : 0;
  fs::path ini;
  if (!resolveDiskSpelling(dir / "mod.ini", ini, err)) return false;
  if (fs::exists(ini, ec)) {
    std::string bytes;
    if (!mods::readFile(ini, bytes, 1 << 20)) return refuse("cannot read " + pathToUtf8(ini));
    priority = std::atoi(readIni(bytes)["priority"].c_str());
  }
  if (ec) return refuse("cannot inspect " + pathToUtf8(ini));
  if (priority > donor.priority) return true;
  if (priority == donor.priority) {
    for (const auto& layer : vfs.layers()) {
      if (&layer == &donor) break;
      if (layer.priority == priority && layer.provider == "mod:" + dest.id && fs::equivalent(dir, layer.root, ec)) return true;
    }
  }
  return refuse("choose a distinct level name; " + donor.provider + " can mask " + name + " in Quick Play");
}

bool Editor::saveToMod(const fs::path& modDir, std::string* err) {
  static const std::string okChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
  if (meta.name.empty() || meta.name.find_first_not_of(okChars) != std::string::npos) {
    if (err) *err = "level names may only use letters, digits, '_' and '-': '" + meta.name + "'";
    return false;
  }
  const bool original = savedMod_ && modDir == savedPath.parent_path().parent_path() && normKey(meta.name) == normKey(genericUtf8(savedPath.stem()));
  if (!original && !checkSavedSpelling(err)) return false;
  fs::path lv, target, side, path;
  if (!resolveDiskSpelling(modDir / "levels", lv, err) || !boardFiles(lv / (original ? savedPath.filename() : fs::path(meta.name + ".dat")), target, side, err) ||
      !resolveDiskSpelling(lv / "mod_levels.cfg", path, err)) return false;
  auto refuse = [&](const std::string& message) { if (err) *err = message; return false; };
  if (meta.title.find_first_of("\r\n") != std::string::npos || readIni("title=" + meta.title)["title"] != meta.title)
    return refuse("title cannot be represented in the level sidecar");
  std::string out;
  std::vector<uint8_t> index;
  std::error_code ec;
  const bool exists = fs::exists(path, ec);
  if (ec || (exists && !slurp(path, index))) return refuse("cannot read " + pathToUtf8(path));
  out.assign(index.begin(), index.end());
  const auto selected = selectStage(out, meta.name);
  if (!stageError_.empty() && (!original || !selected.matches || meta.stageTitleEdited)) return refuse(stageError_);
  if (selected.matches) {
    if (!meta.stageRows.empty() && selected.rows != meta.stageRows)
      return refuse("stage settings conflict with the destination entry for " + meta.name + "; choose a new mod folder");
    if (meta.stageTitleEdited && (selected.matches != 1 || !replaceStageTitle(out, selected.level, meta)))
      return refuse("cannot safely update the selected stage title for " + meta.name);
  } else {
    if (original && !meta.stageRows.empty()) return refuse("saved stage entry is missing: " + pathToUtf8(path));
    if (!meta.stageRows.empty()) {
      if (exists) return refuse("cannot add the selected stage to an existing index; choose a new mod folder");
      out = singleBoardStage(meta);
    } else {
      IndexEntries levels;
      std::istringstream in(out);
      std::string line;
      while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("\tLevel: ")) levels.push_back(line.substr(1));
      }
      auto formatted = [&](const IndexEntries& entries) {
        auto text = editorIndex(modDir, entries);
        if (out.find("\r\n") != out.npos) {
          for (size_t i = 0; (i = text.find('\n', i)) != text.npos; i += 2) text.insert(i, 1, '\r');
        }
        return text;
      };
      if (exists && (levels.empty() || out != formatted(levels)))
        return refuse("cannot add a level to this authored stage list; choose a new mod folder");
      levels.push_back("Level: " + meta.name + ", " + cfgQuote(meta.title) + ", 100000, 3");
      out = formatted(levels);
    }
  }
  std::string link;
  const Layer* donor = nullptr;
  if (!exportBaseLink(meta.baseLink, meta.name, vfs, lv, link, err, &donor)) return false;
  mods::Manifest dest;
  if (!mods::loadManifest(modDir, dest, err)) return false;
  if (donor && donor->isMod && !checkModDestination(*vfs, *donor, modDir, dest, meta.name, err)) return false;
  std::map<fs::path, fs::path> copies;
  if (meta.baseLink.name.empty() && !planArtCopies(modDir, copies, err)) return false;
  std::ostringstream ini;
  ini << "# written by the PegglePort level editor\n";
  ini << link;
  ini << "title = " << meta.title << "\n";
  if (meta.boardWidth > 650) ini << "board_width = " << meta.boardWidth << "\n";
  if (meta.balls > 0) ini << "balls = " << meta.balls << "\n";
  if (meta.orange != 25) ini << "orange = " << meta.orange << "\n";
  std::string background = meta.background;
  if (!prepareBackground(lv, background, err)) return false;
  if (!background.empty() && background != meta.name) ini << "background = " << background << "\n";
  if (!meta.master.empty()) ini << "master = " << meta.master << "\n";
  for (int k = 0; k < PhysicsParams::KeyCount; k++)
    if (meta.physics.has(k)) ini << physicsKey(k).alias << " = " << physicsFormat(meta.physics.v.at(k)) << "\n";
  if (!customTypes.empty()) {
    auto nodes = builtNodes(*level);
    std::map<std::string, std::string> byType;
    for (size_t i = 0; i < nodes.size(); i++) {
      auto it = customTypes.find(nodes[i]);
      if (it == customTypes.end()) continue;
      std::string& list = byType[it->second];
      list += (list.empty() ? "" : ", ") + std::to_string(i);
    }
    for (auto& [type, list] : byType) ini << "peg = " << list << ": " << type << "\n";
  }
  try {
    std::string metadata;
    if (dest.format == "none")
      metadata = "id = " + dest.id + "\nname = " + dest.name + "\nversion = 1\ndescription = Levels made with the PegglePort level editor\npriority = " + std::to_string(kEditorPackPriority) + "\n";
    const auto bytes = writeLevel(*level);
    if (!saveLevelFiles(target, bytes, side, ini.str(), err, path, &out, metadata.empty() ? nullptr : &metadata)) return false;
  } catch (const std::exception& ex) {
    if (err) *err = ex.what();
    return false;
  }
  int copied = 0;
  for (const auto& [dst, src] : copies) {
    fs::create_directories(dst.parent_path(), ec);
    if (!ec && fs::copy_file(src, dst, ec)) ++copied;
  }
  if (copied) pp::log::info("editor", "copied " + std::to_string(copied) + " picture file(s) from other mods into " + pathToUtf8(modDir));
  savedPath = target;
  savedMod_ = true;
  auto stage = selectStage(out, meta.name);
  meta.stageRows = std::move(stage.rows);
  stageError_ = stage.matches == 1 ? "" : "ambiguous stage entry for " + meta.name;
  meta.stageTitleEdited = false;
  clean_ = (int)undo_.size();
  metaDirty = false;
  status = "saved " + pathToUtf8(target);
  return true;
}

void Editor::begin(const std::string& label, const std::vector<NodeP>& touch) {
  if (open_) abort();
  cur_ = Step();
  cur_.label = label;
  cur_.widthBefore = meta.boardWidth;
  cur_.eb = level->entries;
  cur_.touched = touch;
  for (auto& n : touch)
    if (auto v = n->val("mover"); v && v->n && std::find(cur_.touched.begin(), cur_.touched.end(), v->n) == cur_.touched.end()) cur_.touched.push_back(v->n);
  for (auto& m : allMovers(*level))
    if (Node* sm = subOf(*level, *m); sm && sm != m.get()) {
      if (std::find(cur_.touched.begin(), cur_.touched.end(), m) == cur_.touched.end()) cur_.touched.push_back(m);
      cur_.subPose[m.get()] = moverRest(*sm);
    }
  for (auto& n : cur_.touched) cur_.sb.push_back(snapNode(n));
  open_ = true;
}
void Editor::keepFollowers() {
  if (!open_) return;
  for (auto& m : allMovers(*level)) {
    auto it = cur_.subPose.find(m.get());
    Node* sm = it == cur_.subPose.end() ? nullptr : subOf(*level, *m);
    if (!sm) continue;
    V2 now = moverRest(*sm), was = it->second;
    if (now.x == was.x && now.y == was.y) continue;
    putNum(*m, "sub_x", Prim::F32, m->f("sub_x") + (now.x - was.x));
    putNum(*m, "sub_y", Prim::F32, m->f("sub_y") + (now.y - was.y));
    it->second = now;
  }
}
void Editor::commit() {
  if (!open_) return;
  keepFollowers();
  cur_.ea = level->entries;
  cur_.widthAfter = meta.boardWidth;
  for (auto& n : cur_.touched) cur_.sa.push_back(snapNode(n));
  undo_.push_back(std::move(cur_));
  if (clean_ > (int)undo_.size() - 1 && !redo_.empty()) clean_ = -1;
  redo_.clear();
  cur_ = Step();
  open_ = false;
  changed();
}
void Editor::abort() {
  if (!open_) return;
  level->entries = cur_.eb;
  meta.boardWidth = cur_.widthBefore;
  for (auto& s : cur_.sb) restoreNode(s);
  cur_ = Step();
  open_ = false;
  prune();
  changed();
}
void Editor::prune() {
  std::set<const Node*> alive;
  for (auto& e : level->entries)
    if (e.n) alive.insert(e.n.get());
  sel.erase(std::remove_if(sel.begin(), sel.end(), [&](const NodeP& n) { return !alive.count(n.get()); }), sel.end());
}
bool Editor::undo() {
  if (open_) abort();
  if (undo_.empty()) return false;
  Step s = std::move(undo_.back());
  undo_.pop_back();
  level->entries = s.eb;
  meta.boardWidth = s.widthBefore;
  for (auto it = s.sb.rbegin(); it != s.sb.rend(); ++it) restoreNode(*it);
  status = "undo: " + s.label;
  redo_.push_back(std::move(s));
  prune();
  changed();
  return true;
}
bool Editor::redo() {
  if (open_) abort();
  if (redo_.empty()) return false;
  Step s = std::move(redo_.back());
  redo_.pop_back();
  level->entries = s.ea;
  meta.boardWidth = s.widthAfter;
  for (auto& x : s.sa) restoreNode(x);
  status = "redo: " + s.label;
  undo_.push_back(std::move(s));
  prune();
  changed();
  return true;
}

std::vector<NodeP> Editor::objects() const {
  std::vector<NodeP> v;
  for (auto& e : level->entries)
    if (e.k == VK::Ptr && e.n && isObjectType(e.n->type)) v.push_back(e.n);
  return v;
}
bool Editor::isSelected(const NodeP& n) const { return std::find(sel.begin(), sel.end(), n) != sel.end(); }
NodeP Editor::hitObject(V2 p) const {
  NodeP best;
  float bestArea = 1e30f;
  float tol = 3.0f / std::max(0.05f, zoom_);
  for (auto& n : objects()) {
    Geo g = geometry(*level, *n);
    if (!hitTest(g, p, tol)) continue;
    float area = std::max(1.0f, (g.x1 - g.x0) * (g.y1 - g.y0));
    if (g.kind == Geo::Line) area = 1;
    if (area <= bestArea) { bestArea = area; best = n; }
  }
  return best;
}
std::vector<NodeP> Editor::inBox(float x0, float y0, float x1, float y1) const {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  std::vector<NodeP> v;
  for (auto& n : objects())
    if (hitBox(geometry(*level, *n), x0, y0, x1, y1)) v.push_back(n);
  return v;
}
bool Editor::selBounds(float& x0, float& y0, float& x1, float& y1) const {
  bool any = false;
  for (auto& n : sel) {
    Geo g = geometry(*level, *n);
    if (g.kind == Geo::None) continue;
    if (!any) { x0 = g.x0; y0 = g.y0; x1 = g.x1; y1 = g.y1; any = true; }
    else { x0 = std::min(x0, g.x0); y0 = std::min(y0, g.y0); x1 = std::max(x1, g.x1); y1 = std::max(y1, g.y1); }
  }
  return any;
}
int Editor::pegCount(int type) const {
  int c = 0;
  for (auto& n : objects()) {
    int t = pegTypeOf(*n);
    if (t > 0 && (type < 0 || t == type)) c++;
  }
  return c;
}

void Editor::selectAll() { sel = objects(); }

NodeP Editor::makeFor(Tool t, V2 p) const {
  switch (t) {
    case Tool::PegBlue: return newCircle(p.x, p.y, 10, 1);
    case Tool::PegOrange: return newCircle(p.x, p.y, 10, 2);
    case Tool::PegGreen: return newCircle(p.x, p.y, 10, 4, false);
    case Tool::PegPurple: return newCircle(p.x, p.y, 10, 3, false);
    case Tool::PegRound: return newCircle(p.x, p.y, roundRadius, 1);
    case Tool::Bumper: return newCircle(p.x, p.y, roundRadius, 0);
    case Tool::BrickStraight: return newBrick(p.x, p.y, brickStraight, 1);
    case Tool::BrickCurved: { BrickParams b = brick; if (b.type == 5) b.type = 0; return newBrick(p.x, p.y, b, 1); }
    case Tool::Hole: return newHole(p.x, p.y, 40, 40, true, 0);
    case Tool::Emitter: return newEmitter(p.x, p.y, emitterImage);
    default: return nullptr;
  }
}
void Editor::addNodes(const std::vector<NodeP>& v, const std::string& label) {
  if (v.empty()) return;
  begin(label, {});
  for (auto& n : v) {
    Val e;
    e.k = VK::Ptr;
    e.n = n;
    level->entries.push_back(e);
  }
  commit();
  sel = v;
}
NodeP Editor::place(Tool t, V2 p) {
  NodeP n = makeFor(t, snapPt(p));
  if (!n) return nullptr;
  addNodes({n}, std::string("Place ") + toolName(t));
  return n;
}
void Editor::deleteSelection() {
  if (sel.empty()) return;
  begin("Delete", {});
  std::set<const Node*> del;
  for (auto& n : sel) del.insert(n.get());
  auto& es = level->entries;
  es.erase(std::remove_if(es.begin(), es.end(), [&](const Val& e) { return e.n && del.count(e.n.get()); }), es.end());
  commit();
  sel.clear();
}
void Editor::moveSelection(float dx, float dy) {
  if (sel.empty()) return;
  if (int ci = cornerIndex(); ci >= 0) {
    setCorner(ci, cornerWorld(ci) + V2(dx, dy));
    return;
  }
  begin("Move", sel);
  std::set<const Node*> done;
  for (auto& n : sel) translateNode(*n, dx, dy, &done);
  commit();
}
void Editor::rotateSelection(float degrees) {
  if (sel.empty()) return;
  float x0, y0, x1, y1;
  selBounds(x0, y0, x1, y1);
  V2 pivot((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
  begin("Rotate", sel);
  std::set<const Node*> done;
  for (auto& n : sel) rotateNode(*level, *n, pivot, deg2rad(degrees), &done);
  commit();
}
void Editor::mirrorSelection(bool horizontal) {
  std::vector<NodeP> v = sel.empty() ? objects() : sel;
  if (v.empty()) return;
  float axis;
  if (sel.empty()) axis = horizontal ? kBoardW * 0.5f : kBoardH * 0.5f;
  else {
    float x0, y0, x1, y1;
    selBounds(x0, y0, x1, y1);
    axis = horizontal ? (x0 + x1) * 0.5f : (y0 + y1) * 0.5f;
  }
  begin(horizontal ? "Mirror horizontally" : "Mirror vertically", v);
  std::set<const Node*> done;
  for (auto& n : v) mirrorNode(*level, *n, horizontal, axis, &done);
  commit();
}
static std::vector<NodeP> cloneAll(const Level& L, const std::vector<NodeP>& src) {
  std::map<const Node*, NodeP> remap;
  std::vector<NodeP> out;
  for (auto& n : src) out.push_back(cloneNode(n, &remap));
  std::set<const Node*> inSet, outside;
  for (auto& n : src) inSet.insert(n.get());
  for (auto& o : L.entries)
    if (o.n && !inSet.count(o.n.get()))
      if (const Node* m = moverOf(L, *o.n)) outside.insert(m);
  for (size_t i = 0; i < src.size(); i++) {
    const Node* mo = moverOf(L, *src[i]);
    Node* mc = moverOf(L, *out[i]);
    for (int depth = 0; mo && mc && depth < 8; depth++) {
      auto sv = mo->val("sub");
      if (!sv) break;
      const Node* so = subOf(L, *mo);
      auto it = so ? remap.find(so) : remap.end();
      Val link;
      link.k = VK::Ref;
      if (so && outside.count(so)) {
        link.n = sv->n;
        mc->fields["sub"] = link;
        break;
      }
      if (it != remap.end()) {
        link.n = it->second;
        mc->fields["sub"] = link;
        break;
      }
      mo = so;
      mc = subOf(L, *mc);
    }
  }
  return out;
}
void Editor::duplicateSelection(float dx, float dy) {
  if (sel.empty()) return;
  auto c = cloneAll(*level, sel);
  std::set<const Node*> done;
  for (auto& n : c) translateNode(*n, dx, dy, &done);
  addNodes(c, "Duplicate");
}
void Editor::copySelection() {
  if (sel.empty()) return;
  clip_ = cloneAll(*level, sel);
  float x0, y0, x1, y1;
  selBounds(x0, y0, x1, y1);
  clipOrigin_ = V2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
  status = "copied " + std::to_string(clip_.size()) + " object(s)";
}
void Editor::cutSelection() {
  if (sel.empty()) return;
  copySelection();
  deleteSelection();
}
void Editor::paste(V2 at, bool useAt) {
  if (clip_.empty()) { status = "nothing to paste"; return; }
  auto c = cloneAll(*level, clip_);
  V2 d = useAt ? snapPt(at) - clipOrigin_ : V2(grid > 1 ? grid : 10, grid > 1 ? grid : 10);
  if (useAt && snap) d = snapPt(clipOrigin_ + d) - clipOrigin_;
  std::set<const Node*> done;
  for (auto& n : c) translateNode(*n, d.x, d.y, &done);
  addNodes(c, "Paste");
  if (!useAt) clipOrigin_ = clipOrigin_ + d;
}
void Editor::align(const std::string& mode) {
  if (sel.size() < 2) return;
  std::vector<Geo> g;
  for (auto& n : sel) g.push_back(geometry(*level, *n));
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (auto& q : g) { x0 = std::min(x0, q.x0); y0 = std::min(y0, q.y0); x1 = std::max(x1, q.x1); y1 = std::max(y1, q.y1); }
  begin("Align " + mode, sel);
  std::set<const Node*> done;
  for (size_t i = 0; i < sel.size(); i++) {
    float dx = 0, dy = 0;
    if (mode == "left") dx = x0 - g[i].x0;
    else if (mode == "right") dx = x1 - g[i].x1;
    else if (mode == "top") dy = y0 - g[i].y0;
    else if (mode == "bottom") dy = y1 - g[i].y1;
    else if (mode == "hcenter") dx = (x0 + x1) * 0.5f - (g[i].x0 + g[i].x1) * 0.5f;
    else if (mode == "vcenter") dy = (y0 + y1) * 0.5f - (g[i].y0 + g[i].y1) * 0.5f;
    translateNode(*sel[i], dx, dy, &done);
  }
  commit();
}
void Editor::distribute(bool horizontal) {
  if (sel.size() < 3) return;
  struct It { NodeP n; float c; };
  std::vector<It> v;
  for (auto& n : sel) {
    Geo g = geometry(*level, *n);
    v.push_back({n, horizontal ? (g.x0 + g.x1) * 0.5f : (g.y0 + g.y1) * 0.5f});
  }
  std::sort(v.begin(), v.end(), [](const It& a, const It& b) { return a.c < b.c; });
  float lo = v.front().c, hi = v.back().c;
  begin(horizontal ? "Distribute horizontally" : "Distribute vertically", sel);
  std::set<const Node*> done;
  for (size_t i = 0; i < v.size(); i++) {
    float want = lo + (hi - lo) * (float)i / (float)(v.size() - 1);
    translateNode(*v[i].n, horizontal ? want - v[i].c : 0, horizontal ? 0 : want - v[i].c, &done);
  }
  commit();
}
bool Editor::setProp(const std::string& key, const std::string& text) {
  if (sel.empty()) return false;
  bool any = false;
  begin("Set " + key, sel);
  for (auto& n : sel)
    for (auto& d : propsFor(*n))
      if (key == d.key) { any |= setPropText(*level, *n, d, text); break; }
  if (any) commit(); else abort();
  return any;
}
bool Editor::setMoverProp(const std::string& key, const std::string& text) {
  std::vector<NodeP> movers;
  for (auto& n : sel)
    if (auto v = n->val("mover"); v && v->n) movers.push_back(v->n);
  if (movers.empty()) return false;
  bool any = false;
  begin("Set mover " + key, movers);
  for (auto& m : movers)
    for (auto& d : moverProps())
      if (key == d.key) {
        if (!std::strncmp(d.key, "sub_", 4) && !m->val("sub")) break;
        if (setPropText(*level, *m, d, text)) {
          any = true;
          if (std::strncmp(d.key, "sub_", 4)) syncRest(*m);
        }
        break;
      }
  if (any) commit(); else abort();
  return any;
}
bool Editor::addMover(int type) {
  if (sel.empty()) return false;
  begin("Add mover", sel);
  bool any = false;
  for (auto& n : sel) any |= attachMover(*level, *n, type);
  if (any) commit(); else abort();
  return any;
}
NodeP Editor::driverOf(const NodeP& follower) const {
  Node* m = moverOf(*level, *follower);
  Node* s = m ? subOf(*level, *m) : nullptr;
  if (!s) return nullptr;
  for (auto& o : objects())
    if (o != follower && moverOf(*level, *o) == s) return o;
  return nullptr;
}
std::vector<NodeP> Editor::followersOf(const NodeP& driver) const {
  std::vector<NodeP> out;
  Node* dm = moverOf(*level, *driver);
  if (!dm) return out;
  for (auto& o : objects()) {
    Node* m = moverOf(*level, *o);
    if (m && o != driver && subOf(*level, *m) == dm) out.push_back(o);
  }
  return out;
}
std::string Editor::describe(const NodeP& n) const {
  V2 p = nodePos(*level, *n);
  const char* t = typeName(n->type);
  if (n->type == 5 && pegOf(*n)) t = "peg";
  else if (n->type == 5) t = "round object";
  char b[96];
  std::snprintf(b, sizeof b, "%s at %.0f, %.0f", t, p.x, p.y);
  return b;
}
bool Editor::followObject(const NodeP& driver) {
  if (sel.empty()) { status = "select the objects that should follow first"; return false; }
  Node* dm = moverOf(*level, *driver);
  if (!dm) { status = "that object has no mover, so there is nothing to follow"; return false; }
  const Val* dv = driver->val("mover");
  NodeP dmp = dv ? dv->n : nullptr;
  if (!dmp) { status = "that object cannot be followed"; return false; }
  if (dm->val("sub")) { status = "that object follows another one itself, pick the one it follows"; return false; }
  std::vector<NodeP> who;
  for (auto& n : sel) {
    if (!isObjectType(n->type) || n->type == 2) continue;
    if (n == driver) { status = "an object cannot follow itself"; return false; }
    Node* fm = moverOf(*level, *n);
    if (fm == dm) { status = "those objects share one mover already"; return false; }
    if (fm && !followersOf(n).empty()) { status = "other objects follow " + describe(n) + ", so it cannot follow anything"; return false; }
    who.push_back(n);
  }
  if (who.empty()) { status = "these objects cannot follow anything"; return false; }
  begin("Follow object", who);
  V2 start = moverRest(*dm);
  for (auto& n : who) {
    if (!n->val("mover")) attachMover(*level, *n, 0);
    Node* fm = moverOf(*level, *n);
    if (!fm) continue;
    Val link;
    link.k = VK::Ref;
    link.n = dmp;
    fm->fields["sub"] = link;
    putNum(*fm, "sub_x", Prim::F32, start.x);
    putNum(*fm, "sub_y", Prim::F32, start.y);
  }
  commit();
  status = "now following " + describe(driver);
  return true;
}
bool Editor::stopFollowing() {
  std::vector<NodeP> who;
  for (auto& n : sel)
    if (Node* m = moverOf(*level, *n); m && m->val("sub")) who.push_back(n);
  if (who.empty()) { status = "nothing to stop"; return false; }
  begin("Stop following", who);
  for (auto& n : who) {
    Node* m = moverOf(*level, *n);
    m->fields.erase("sub");
    m->fields.erase("sub_x");
    m->fields.erase("sub_y");
  }
  commit();
  status = "stopped following";
  return true;
}
bool Editor::setHoleExit(V2 p) {
  std::vector<NodeP> holes;
  for (auto& n : sel)
    if (n->type == 8) holes.push_back(n);
  if (holes.empty()) { status = "select a hole first, then click where the ball should come out"; return false; }
  std::vector<NodeP> touch = holes;
  for (auto& h : holes)
    if (auto v = h->val("entry"); v && v->n) touch.push_back(v->n);
  begin("Set hole exit", touch);
  for (auto& h : holes) {
    auto v = h->val("entry");
    if (v && v->n) {
      V2 now = nodePos(*level, *v->n);
      translateNode(*v->n, p.x - now.x, p.y - now.y);
      continue;
    }
    NodeP ex = newPoly(p.x, p.y, {{-19.5f, -19.5f}, {-19.5f, 19.5f}, {19.5f, 19.5f}, {19.5f, -19.5f}});
    ex->flags["collidable"] = false;
    Val e;
    e.k = VK::Ptr;
    e.n = ex;
    h->fields["entry"] = e;
    if (!h->has("out_delay")) putNum(*h, "out_delay", Prim::I32, 20);
  }
  commit();
  status = "hole exit set";
  return true;
}
void Editor::removeMover() {
  if (sel.empty()) return;
  begin("Remove mover", sel);
  for (auto& n : sel) detachMover(*level, *n);
  commit();
}
bool Editor::cornersEditable() const {
  return sel.size() == 1 && sel.front()->type == 3 && sel.front()->val("vertices") && polyCorners(*sel.front()).size() >= 2;
}
int Editor::cornerCount() const { return cornersEditable() ? (int)polyCorners(*sel.front()).size() : 0; }
int Editor::cornerIndex() const {
  if (!cornersEditable() || sel.front() != cornerPoly_) return -1;
  return cornerIdx_ >= 0 && cornerIdx_ < cornerCount() ? cornerIdx_ : -1;
}
void Editor::selectCorner(int i) {
  if (!cornersEditable() || i < 0 || i >= cornerCount()) {
    cornerPoly_.reset();
    cornerIdx_ = -1;
    return;
  }
  cornerPoly_ = sel.front();
  cornerIdx_ = i;
}
V2 Editor::cornerWorld(int i) const { return cornersEditable() ? polyCornerWorld(*level, *sel.front(), i) : V2(); }
int Editor::cornerAt(V2 p) const { return cornersEditable() ? hitCorner(*level, *sel.front(), p, 7.0f / std::max(0.05f, zoom_)) : -1; }
int Editor::edgeAt(V2 p, V2* at) const { return cornersEditable() ? hitEdge(*level, *sel.front(), p, 6.0f / std::max(0.05f, zoom_), at) : -1; }
bool Editor::applyCorners(const NodeP& poly, const std::vector<V2>& corners, const std::string& label) {
  begin(label, {poly});
  bool ok = polySetCorners(*poly, corners);
  if (ok) commit(); else abort();
  return ok;
}
bool Editor::setCorner(int i, V2 world) {
  if (!cornersEditable()) return false;
  NodeP poly = sel.front();
  auto c = polyCorners(*poly);
  if (i < 0 || (size_t)i >= c.size()) { status = "no such corner"; return false; }
  auto next = c;
  next[(size_t)i] = polyWorldToLocal(*level, *poly, world);
  if (!polyShapeOk(c, next)) { status = "that corner would collapse or cross the shape"; return false; }
  if (!applyCorners(poly, next, "Move corner")) return false;
  cornerPoly_ = poly;
  cornerIdx_ = i;
  return true;
}
bool Editor::addCorner() {
  if (!cornersEditable()) return false;
  NodeP poly = sel.front();
  auto c = polyCorners(*poly);
  int edge = cornerIndex();
  if (edge < 0) {
    float best = -1;
    for (size_t i = 0; i < c.size(); i++) {
      float l = (c[(i + 1) % c.size()] - c[i]).len2();
      if (l > best) { best = l; edge = (int)i; }
    }
  }
  V2 mid = (c[(size_t)edge] + c[((size_t)edge + 1) % c.size()]) * 0.5f;
  auto next = c;
  next.insert(next.begin() + edge + 1, mid);
  if (!polyShapeOk(c, next)) { status = "no room for another corner there"; return false; }
  if (!applyCorners(poly, next, "Add corner")) return false;
  cornerPoly_ = poly;
  cornerIdx_ = edge + 1;
  status = "corner added";
  return true;
}
bool Editor::addCornerAt(V2 world) {
  V2 at;
  int edge = edgeAt(world, &at);
  if (edge < 0) return false;
  NodeP poly = sel.front();
  auto c = polyCorners(*poly);
  auto next = c;
  next.insert(next.begin() + edge + 1, polyWorldToLocal(*level, *poly, at));
  if (!polyShapeOk(c, next)) { status = "no room for another corner there"; return false; }
  if (!applyCorners(poly, next, "Add corner")) return false;
  cornerPoly_ = poly;
  cornerIdx_ = edge + 1;
  status = "corner added";
  return true;
}
bool Editor::removeCorner() {
  int i = cornerIndex();
  if (i < 0) { status = "select a corner first"; return false; }
  NodeP poly = sel.front();
  auto c = polyCorners(*poly);
  if (c.size() <= 3) { status = "a polygon needs at least 3 corners"; return false; }
  auto next = c;
  next.erase(next.begin() + i);
  if (!polyShapeOk(c, next)) { status = "removing that corner would cross the shape"; return false; }
  if (!applyCorners(poly, next, "Remove corner")) return false;
  cornerPoly_ = poly;
  cornerIdx_ = std::max(0, i - 1);
  status = "corner removed";
  return true;
}
bool Editor::deleteKey() {
  if (cornerIndex() >= 0) return removeCorner();
  deleteSelection();
  return true;
}
int Editor::generate(const std::string& kind, const std::vector<float>& a, int pegType) {
  std::vector<V2> pts;
  auto A = [&](size_t i, float d) { return i < a.size() ? a[i] : d; };
  if (kind == "grid") pts = gridPoints((int)A(0, 5), (int)A(1, 4), A(2, 40), A(3, 40), V2(A(4, 200), A(5, 150)));
  else if (kind == "circle") pts = circlePoints((int)A(0, 12), A(1, 80), V2(A(2, 325), A(3, 250)), A(4, -90));
  else if (kind == "spiral") pts = spiralPoints((int)A(0, 24), A(1, 20), A(2, 100), A(3, 2), V2(A(4, 325), A(5, 250)));
  else if (kind == "sine") pts = sinePoints((int)A(0, 12), A(1, 400), A(2, 40), A(3, 2), V2(A(4, 125), A(5, 300)));
  else return 0;
  if (pts.size() > 2000) pts.resize(2000);
  std::vector<NodeP> v;
  for (auto& p : pts) v.push_back(newCircle(std::round(p.x * 100) / 100, std::round(p.y * 100) / 100, roundRadius, pegType, pegType <= 2));
  addNodes(v, "Generate " + kind);
  return (int)v.size();
}
void Editor::setMeta(const std::string& k, const std::string& v) {
  if (k == "name") meta.name = v;
  else if (k == "title") { meta.title = v; meta.stageTitleEdited = true; }
  else if (k == "background") meta.background = v;
  else if (k == "master") meta.master = v;
  else if (k == "board_width") {
    double width = 0;
    if (!physicsParseNumber(v, width) || width != std::floor(width)) return;
    const int next = (int)std::clamp(width, 650.0, 1400.0);
    if (next == meta.boardWidth) return;
    cancelGesture();
    begin("Board width", {});
    meta.boardWidth = next;
    commit();
    return;
  }
  else if (k == "balls") meta.balls = std::clamp(std::atoi(v.c_str()), 0, 99);
  else if (k == "orange") meta.orange = std::clamp(std::atoi(v.c_str()), 0, 500);
  else if (int pk = 0; physicsKeyByName(k, pk)) {
    double d = 0;
    if (v.empty() || v == "-") meta.physics.clear(pk);
    else if (physicsParseNumber(v, d)) meta.physics.set(pk, d);
    else return;
  } else return;
  metaDirty = true;
  changed();
}

bool Editor::rotateHandle(V2& pos) const {
  float x0, y0, x1, y1;
  if (sel.empty() || !selBounds(x0, y0, x1, y1)) return false;
  pos = V2((x0 + x1) * 0.5f, y0 - 26.0f / std::max(0.05f, zoom_));
  return true;
}
bool Editor::overRotateHandle(V2 p) const {
  V2 h;
  return rotateHandle(h) && (p - h).len2() <= (9.0f / zoom_) * (9.0f / zoom_);
}
void Editor::cancelGesture() {
  if (g_.kind == G::Move || g_.kind == G::Rotate || g_.kind == G::Corner) abort();
  g_ = G();
  polyPts_.clear();
}
void Editor::mouseDown(V2 p, int button, Mods m, int clicks) {
  hover = p;
  if (button == 3) {
    if (g_.kind != G::None || !polyPts_.empty()) cancelGesture();
    else tool = Tool::Select;
    return;
  }
  if (button != 1 || tool == Tool::Pan) return;
  switch (tool) {
    case Tool::Select: {
      if (!sel.empty() && overRotateHandle(p)) {
        float x0, y0, x1, y1;
        selBounds(x0, y0, x1, y1);
        g_ = G();
        g_.kind = G::Rotate;
        g_.pivot = V2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        g_.ang0 = std::atan2(p.y - g_.pivot.y, p.x - g_.pivot.x);
        begin("Rotate", sel);
        return;
      }
      if (cornersEditable()) {
        NodeP poly = sel.front();
        int ci = cornerAt(p);
        if (ci >= 0) {
          cornerPoly_ = poly;
          cornerIdx_ = ci;
          g_ = G();
          g_.kind = G::Corner;
          g_.corner = ci;
          g_.start = p;
          g_.cornerStart = cornerWorld(ci);
          g_.cornersAtStart = polyCorners(*poly);
          g_.primary = poly;
          begin("Move corner", {poly});
          return;
        }
        if (clicks >= 2 && addCornerAt(p)) return;
      }
      NodeP hit = hitObject(p);
      if (hit) {
        if (m.shift) {
          auto it = std::find(sel.begin(), sel.end(), hit);
          if (it != sel.end()) { sel.erase(it); return; }
          sel.push_back(hit);
        } else if (!isSelected(hit)) {
          sel = {hit};
        }
        if (m.alt) {
          auto c = cloneAll(*level, sel);
          addNodes(c, "Duplicate");
          hit = c.empty() ? hit : c.front();
        }
        g_ = G();
        g_.kind = G::Move;
        g_.start = p;
        g_.primary = isSelected(hit) ? hit : sel.front();
        g_.primaryStart = nodePos(*level, *g_.primary);
        begin("Move", sel);
      } else {
        if (!m.shift) sel.clear();
        g_ = G();
        g_.kind = G::Box;
        g_.a = g_.b = p;
        g_.shiftBox = m.shift;
      }
      break;
    }
    case Tool::HoleExit: setHoleExit(snapPt(p)); break;
    case Tool::Follow: {
      NodeP hit = hitObject(p);
      if (!hit) status = "click the object to follow";
      else followObject(hit);
      tool = Tool::Select;
      break;
    }
    case Tool::Line:
      g_ = G();
      g_.kind = G::Line;
      g_.a = g_.b = snapPt(p);
      break;
    case Tool::Poly: {
      V2 q = snapPt(p);
      if (polyPts_.size() >= 3 && ((q - polyPts_[0]).len2() < (9 / zoom_) * (9 / zoom_) || clicks >= 2)) finishPoly();
      else polyPts_.push_back(q);
      break;
    }
    default: place(tool, p); break;
  }
}
void Editor::mouseMove(V2 p, Mods m) {
  hover = p;
  switch (g_.kind) {
    case G::Move: {
      V2 d = p - g_.start;
      if (snap) d = snapPt(g_.primaryStart + d) - g_.primaryStart;
      V2 step = d - g_.appliedD;
      if (step.x != 0 || step.y != 0) {
        std::set<const Node*> done;
        for (auto& n : sel) translateNode(*n, step.x, step.y, &done);
        g_.appliedD = d;
        g_.moved = true;
        keepFollowers();
        changed();
      }
      break;
    }
    case G::Rotate: {
      float ang = std::atan2(p.y - g_.pivot.y, p.x - g_.pivot.x) - g_.ang0;
      if (snap || m.shift) {
        float q = deg2rad(15.0f);
        ang = std::floor(ang / q + 0.5f) * q;
      }
      float step = ang - g_.applied;
      if (step != 0) {
        std::set<const Node*> done;
        for (auto& n : sel) rotateNode(*level, *n, g_.pivot, step, &done);
        g_.applied = ang;
        g_.moved = true;
        changed();
      }
      break;
    }
    case G::Corner: {
      V2 w = g_.cornerStart + (p - g_.start);
      if (snap) w = snapPt(w);
      if (!g_.moved && w.x == g_.cornerStart.x && w.y == g_.cornerStart.y) break;
      if (g_.moved && w.x == g_.appliedD.x && w.y == g_.appliedD.y) break;
      auto next = g_.cornersAtStart;
      next[(size_t)g_.corner] = polyWorldToLocal(*level, *g_.primary, w);
      if (!polyShapeOk(g_.cornersAtStart, next)) {
        status = "that corner would collapse or cross the shape";
        break;
      }
      polySetCorners(*g_.primary, next);
      g_.appliedD = w;
      g_.moved = true;
      changed();
      break;
    }
    case G::Box: g_.b = p; break;
    case G::Line: {
      V2 q = snapPt(p);
      if (m.shift) {
        V2 d = q - g_.a;
        float len = std::sqrt(d.len2()), a = std::floor(std::atan2(d.y, d.x) / deg2rad(15) + 0.5f) * deg2rad(15);
        q = g_.a + V2(std::cos(a), std::sin(a)) * len;
      }
      g_.b = q;
      break;
    }
    default: break;
  }
}
void Editor::mouseUp(V2 p, int button, Mods m) {
  hover = p;
  if (button != 1) return;
  switch (g_.kind) {
    case G::Move:
    case G::Rotate:
    case G::Corner:
      if (g_.moved) commit(); else abort();
      break;
    case G::Box: {
      auto in = inBox(g_.a.x, g_.a.y, g_.b.x, g_.b.y);
      if (g_.shiftBox) {
        for (auto& n : in)
          if (!isSelected(n)) sel.push_back(n);
      } else sel = in;
      break;
    }
    case G::Line:
      if ((g_.b - g_.a).len2() > 4) addNodes({newLine(g_.a.x, g_.a.y, g_.b.x, g_.b.y)}, "Place line");
      break;
    default: break;
  }
  g_ = G();
}
void Editor::keyEnter() {
  if (tool == Tool::Poly && polyPts_.size() >= 3) finishPoly();
}
void Editor::finishPoly() {
  if (polyPts_.size() < 3) { polyPts_.clear(); return; }
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (auto& p : polyPts_) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
  V2 o = snapPt(V2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f));
  std::vector<V2> local;
  for (auto& p : polyPts_) local.push_back(p - o);
  double area = 0;
  for (size_t i = 0; i < local.size(); i++) {
    const V2 &a = local[i], &b = local[(i + 1) % local.size()];
    area += (double)a.x * b.y - (double)b.x * a.y;
  }
  if (area > 0) std::reverse(local.begin(), local.end());
  polyPts_.clear();
  addNodes({newPoly(o.x, o.y, local)}, "Place polygon");
}

BoardConfig Editor::boardConfig(uint32_t seed) const {
  BoardConfig c;
  c.boardWidth = std::max(650, meta.boardWidth);
  c.authoredWidth = c.boardWidth;
  c.balls = meta.balls;
  c.orange = meta.orange;
  c.physics = meta.physics;
  c.seed = seed;
  int m = meta.master.empty() ? 0 : Board::masterByName(meta.master);
  for (int i = 0; i < 4; i++) c.master[i] = m;
  return c;
}
static bool buildable(const Node& n) {
  if (n.type == 3) {
    auto v = n.val("vertices");
    return v && v->verts.size() >= 4;
  }
  return n.type == 2 || n.type == 5 || n.type == 6 || n.type == 8;
}
static std::vector<const Node*> builtNodes(const Level& L) {
  std::vector<const Node*> out;
  auto has = [&](const Node* n) { return std::find(out.begin(), out.end(), n) != out.end(); };
  for (auto& e : L.entries)
    if (auto n = L.resolve(e))
      if (buildable(*n) && !has(n)) out.push_back(n);
  for (size_t i = 0; i < out.size(); i++) {
    if (out[i]->type != 8) continue;
    if (auto v = out[i]->val("entry"))
      if (auto en = L.resolve(*v))
        if (buildable(*en) && !has(en)) out.push_back(en);
  }
  return out;
}
bool Editor::buildBoard(Board& b, LevelBundle& lb, std::vector<NodeP>* objNode, uint32_t seed, bool stored, std::string* err) const {
  auto baked = bake();
  if (!baked) {
    if (err) *err = "the level cannot be written (a required field is missing)";
    return false;
  }
  lb = LevelBundle();
  lb.base = base;
  lb.baseLink = meta.baseLink;
  lb.level = baked;
  lb.name = meta.name;
  lb.title = meta.title;
  lb.background = meta.background.empty() ? meta.name : meta.background;
  lb.source = "editor";
  lb.cfg = boardConfig(seed);
  if (!b.load(base, baked, lb.cfg, err)) return false;
  size_t baseCount = base ? builtNodes(*base).size() : 0;
  auto nodes = builtNodes(*baked);
  if (objNode) objNode->assign(b.objs.size(), nullptr);
  if (b.objs.size() != baseCount + nodes.size()) return true;
  std::map<const Node*, NodeP> toLive;
  for (size_t i = 0; i < baked->entries.size() && i < level->entries.size(); i++)
    if (baked->entries[i].n && level->entries[i].n) toLive[baked->entries[i].n.get()] = level->entries[i].n;
  for (size_t i = 0; i < nodes.size(); i++) {
    auto it = toLive.find(nodes[i]);
    if (it == toLive.end()) continue;
    if (objNode) (*objNode)[baseCount + i] = it->second;
    if (stored) b.objs[baseCount + i].pegType = pegTypeOf(*it->second);
  }
  return true;
}

std::vector<Issue> Editor::validate() {
  std::vector<Issue> out;
  auto add = [&](Issue::Sev s, const std::string& t, NodeP n = nullptr) { out.push_back({s, t, n}); };
  auto objs = objects();
  float sh = wideShift(), W = playfieldW();
  if (objs.empty()) add(Issue::Error, "the level has no objects");

  int pegs = 0, cand = 0, fixedOrange = 0, storedOrange = 0, purple = 0, green = 0;
  for (auto& n : objs) {
    Node* p = pegOf(*n);
    if (!p) continue;
    pegs++;
    int t = (int)p->i("type");
    bool var = p->flag("variable");
    if (t == 2) storedOrange++;
    if (var && (t == 1 || t == 2)) cand++;
    else if (!var && t == 2) fixedOrange++;
    if (t == 3) purple++;
    if (t == 4) green++;
  }
  if (pegs == 0) add(Issue::Error, "no pegs: the level cannot be played");
  else if (cand + fixedOrange == 0) add(Issue::Error, "no peg can become orange (needs pegs with 'variable colour' on)");
  else if (cand + fixedOrange < meta.orange) add(Issue::Warn, std::to_string(cand + fixedOrange) + " pegs can be orange but the target is " + std::to_string(meta.orange) + " (the original rules want 25)");
  if (pegs > 0) add(Issue::Info, std::to_string(pegs) + " pegs: " + std::to_string(storedOrange) + " orange stored, " + std::to_string(purple) + " purple, " + std::to_string(green) + " green, " + std::to_string(cand) + " random-colour candidates");

  bool ownBucket = false;
  for (auto& n : objs)
    if (n->str("id") == "freeball") ownBucket = true;
  bool baseBucket = false;
  if (base)
    for (auto& e : base->entries)
      if (e.n && e.n->str("id") == "freeball") baseBucket = true;
  bool underscore = !meta.name.empty() && meta.name[0] == '_';
  if (ownBucket || (baseBucket && !underscore)) add(Issue::Info, ownBucket ? "bucket: defined by the level" : "bucket and walls come from _base.dat");
  else if (underscore) add(Issue::Error, "no bucket: names starting with '_' do not merge _base.dat and the level has no 'freeball' hole");
  else add(Issue::Warn, "no bucket found: no _base.dat is loaded (open the editor with the game data)");

  std::vector<std::pair<NodeP, Geo>> solid;
  std::vector<std::pair<NodeP, Geo>> circles;
  for (auto& n : objs) {
    Geo g = geometry(*level, *n);
    if (g.kind == Geo::Circle) circles.push_back({n, g});
    else if (n->flag("collidable") && (g.kind == Geo::Poly || g.kind == Geo::Line)) solid.push_back({n, g});
  }
  int wide = 0;
  for (auto& n : objs) {
    Geo g = geometry(*level, *n);
    if (g.kind == Geo::None || g.kind == Geo::Emitter) continue;
    float bx0 = g.x0 + sh, bx1 = g.x1 + sh;
    bool bg = n->flag("background") || !n->flag("collidable");
    if (bx1 < 0 || bx0 > W || g.y1 < 0 || g.y0 > kBoardH + 40) {
      if (!bg) add(Issue::Error, std::string(typeName(n->type)) + " is completely outside the playfield", n);
    } else if (bx0 < 0 || bx1 > W || g.y0 < 0 || g.y1 > kBoardH) {
      if (!bg && pegOf(*n)) add(Issue::Warn, std::string(typeName(n->type)) + " peg sticks out of the playfield", n);
    }
    if (W > 650 && (g.x0 < 0 || g.x1 > 650) && !bg) wide++;
  }
  if (vfs) {
    std::string bg = meta.background.empty() ? meta.name : meta.background;
    if (!artExists(ArtSlot::Background, bg)) {
      if (!meta.background.empty() && meta.background != meta.name) add(Issue::Warn, "level art '" + meta.background + "' was not found in the game or mod files");
      else add(Issue::Info, "the level has no picture (levels/" + bg + " is missing), it plays without art");
    }
    for (auto& n : objs) {
      std::string img = n->type == 9 ? std::string() : n->str("image");
      if (!img.empty() && !artExists(ArtSlot::Object, img)) add(Issue::Warn, std::string(typeName(n->type)) + " picture '" + img + "' was not found in the game or mod files", n);
    }
  }
  int emitters = 0;
  for (auto& n : objs) {
    if (n->type != 9) continue;
    emitters++;
    std::string img = n->str("emitter_image");
    if (img.empty()) add(Issue::Warn, "emitter has no image", n);
    else if (vfs && !artExists(ArtSlot::Emitter, img)) add(Issue::Warn, "emitter image '" + img + "' was not found in the game or mod files", n);
    if (n->i("width") < 1 || n->i("height") < 1) add(Issue::Warn, "emitter area is empty", n);
  }
  if (emitters) add(Issue::Info, std::to_string(emitters) + " emitter(s): particle effects of Peggle Nights");
  if (W > 650 && wide) add(Issue::Info, std::to_string(wide) + " object(s) lie outside the original 650 px region (this level needs the wide board)");
  if (W == 650) {
    int beyond = 0;
    for (auto& n : objs) {
      Geo g = geometry(*level, *n);
      if (pegOf(*n) && g.kind != Geo::None && (g.x0 < 0 || g.x1 > 650)) beyond++;
    }
    (void)beyond;
  }
  int overlapWall = 0, overlapPeg = 0, overlapBucket = 0;
  for (size_t i = 0; i < circles.size(); i++) {
    auto& [n, g] = circles[i];
    if (!pegOf(*n)) continue;
    for (auto& [sn, sg] : solid) {
      if (sn.get() == n.get()) continue;
      if (distToGeo(sg, g.c) < g.r - 0.5f) {
        if (++overlapWall <= 6) add(Issue::Warn, "peg overlaps a " + std::string(typeName(sn->type)), n);
        break;
      }
    }
    if (g.c.y + sh * 0 > kBoardH - 40 && g.c.y < kBoardH + 40) {
      if (++overlapBucket <= 3) add(Issue::Warn, "peg in the bucket zone (y > " + std::to_string((int)(kBoardH - 40)) + ")", n);
    }
    if (g.c.x - g.r + sh < 0 || g.c.x + g.r + sh > W) {
      if (++overlapWall <= 6) add(Issue::Warn, "peg overlaps the side wall", n);
    }
    for (size_t j = i + 1; j < circles.size(); j++) {
      auto& [m2, g2] = circles[j];
      if (!pegOf(*m2)) continue;
      float rr = g.r + g2.r - 1.0f;
      if ((g.c - g2.c).len2() < rr * rr) {
        if (++overlapPeg <= 6) add(Issue::Warn, (g.c - g2.c).len2() < 1 ? "two pegs sit on the same spot" : "pegs overlap", n);
      }
    }
  }
  if (overlapWall > 6) add(Issue::Warn, std::to_string(overlapWall - 6) + " more peg/wall overlaps");
  if (overlapPeg > 6) add(Issue::Warn, std::to_string(overlapPeg - 6) + " more peg/peg overlaps");

  std::map<std::string, std::vector<NodeP>> ids;
  for (auto& n : objs) {
    std::string id = n->str("id");
    if (!id.empty()) ids[id].push_back(n);
  }
  for (auto& [id, v] : ids) {
    if (v.size() > 1) add(Issue::Warn, "duplicate id '" + id + "' on " + std::to_string(v.size()) + " objects", v[0]);
    if (base)
      for (auto& e : base->entries)
        if (e.n && e.n->str("id") == id) { add(Issue::Warn, "id '" + id + "' is used by _base.dat (walls/bucket logic)", v[0]); break; }
  }
  for (auto& n : objs) {
    Node* m = moverOf(*level, *n);
    if (!m) continue;
    int t = std::abs((int)m->i("move_type"));
    if (t == 10 || t > 15) add(Issue::Warn, "mover type " + std::to_string(t) + " does nothing", n);
    if (t != 0 && m->i("time", 200) < 1) add(Issue::Warn, "mover period is below 1 tick", n);
    if ((t >= 1 && t <= 7 && t != 6 && t != 7) && m->i("radius") == 0 && m->i("radius2") == 0 && !m->val("sub")) add(Issue::Info, "mover with radius 0 does not move", n);
    if (Node* s = subOf(*level, *m)) {
      bool owned = false;
      for (auto& o : objs)
        if (o != n && moverOf(*level, *o) == s) owned = true;
      if (!owned) add(Issue::Warn, "follows a mover that no object uses, so it never moves", n);
    }
  }
  if (level->version > 0x42) add(Issue::Info, "file version " + std::to_string(level->version) + ": loads in Peggle Nights only (Deluxe refuses versions above 66)");
  int skipped = 0;
  for (auto& e : level->entries)
    if (e.n && !isObjectType(e.n->type)) skipped++;
  if (skipped) add(Issue::Info, std::to_string(skipped) + " bare object entries are kept but not shown");
  static const std::string okChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
  if (meta.name.empty() || meta.name.find_first_not_of(okChars) != std::string::npos) add(Issue::Error, "level name '" + meta.name + "' must be letters, digits, '_' or '-'");
  bool hasErr = false, hasWarn = false;
  for (auto& i : out) { hasErr |= i.sev == Issue::Error; hasWarn |= i.sev == Issue::Warn; }
  if (!hasErr && !hasWarn) add(Issue::Info, "no problems found");
  std::stable_sort(out.begin(), out.end(), [](const Issue& a, const Issue& b) { return a.sev > b.sev; });
  issues = out;
  return out;
}

static std::vector<std::string> words(const std::string& line) {
  std::vector<std::string> w;
  std::istringstream in(line);
  std::string t;
  while (in >> t) w.push_back(t);
  return w;
}
static std::vector<float> nums(const std::vector<std::string>& w, size_t from) {
  std::vector<float> v;
  for (size_t i = from; i < w.size(); i++) {
    char* e = nullptr;
    float f = std::strtof(w[i].c_str(), &e);
    if (e != w[i].c_str()) v.push_back(f);
  }
  return v;
}
std::string Editor::exec(const std::string& line, bool* okOut) {
  bool ok = true;
  std::string msg;
  auto w = words(line);
  Mods mods;
  for (auto it = w.begin(); it != w.end();) {
    if (*it == "shift") { mods.shift = true; it = w.erase(it); }
    else if (*it == "alt") { mods.alt = true; it = w.erase(it); }
    else if (*it == "ctrl") { mods.ctrl = true; it = w.erase(it); }
    else ++it;
  }
  if (w.empty()) { if (okOut) *okOut = true; return ""; }
  const std::string& c = w[0];
  auto n = nums(w, 1);
  auto need = [&](size_t k) { if (n.size() < k) { ok = false; msg = "'" + c + "' needs " + std::to_string(k) + " number(s)"; return false; } return true; };
  std::string rest;
  for (size_t i = 2; i < w.size(); i++) rest += (i > 2 ? " " : "") + w[i];
  std::string err;
  if (c == "new") {
    newLevel(w.size() > 1 ? w[1] : "NewLevel");
    msg = status;
  } else if (c == "save") {
    ok = w.size() > 1 ? saveDat(w[1], &err) : saveCurrent(&err);
    msg = ok ? status : err;
  } else if (c == "export") {
    ok = w.size() > 1 && saveJson(w[1], &err);
    msg = ok ? status : (w.size() > 1 ? err : "export needs a path");
  } else if (c == "import") {
    ok = w.size() > 1 && loadJson(w[1], &err);
    msg = ok ? status : (w.size() > 1 ? err : "import needs a path");
  } else if (c == "loaddat") {
    ok = w.size() > 1 && loadDatFile(w[1], &err);
    msg = ok ? status : (w.size() > 1 ? err : "loaddat needs a path");
  } else if (c == "tool") {
    Tool t;
    if (w.size() > 1 && toolFromName(w[1], t)) { tool = t; polyPts_.clear(); msg = "tool " + w[1]; } else { ok = false; msg = "unknown tool"; }
  } else if (c == "place") {
    Tool t;
    if (w.size() > 3 && toolFromName(w[1], t) && t != Tool::Select && t != Tool::Pan && t != Tool::Poly && t != Tool::Line) {
      auto q = nums(w, 2);
      if (t == Tool::PegRound || t == Tool::Bumper) { if (q.size() > 2) roundRadius = q[2]; }
      bool sv = snap; snap = false;
      NodeP node = place(t, V2(q[0], q[1]));
      snap = sv;
      msg = node ? std::string("placed ") + w[1] : "nothing placed";
    } else { ok = false; msg = "usage: place TOOL x y [radius]"; }
  } else if (c == "exit") {
    if (need(2)) { ok = setHoleExit(V2(n[0], n[1])); msg = ok ? "hole exit set" : status; }
  } else if (c == "line") {
    if (need(4)) { addNodes({newLine(n[0], n[1], n[2], n[3])}, "Place line"); msg = "line placed"; }
  } else if (c == "poly") {
    if (n.size() >= 6) {
      polyPts_.clear();
      for (size_t i = 0; i + 1 < n.size(); i += 2) polyPts_.push_back(V2(n[i], n[i + 1]));
      finishPoly();
      msg = "polygon placed";
    } else { ok = false; msg = "poly needs at least 3 points"; }
  } else if (c == "click" || c == "down" || c == "up" || c == "move") {
    if (need(2)) {
      V2 p(n[0], n[1]);
      if (c == "click") { mouseDown(p, 1, mods); mouseUp(p, 1, mods); }
      else if (c == "down") mouseDown(p, 1, mods);
      else if (c == "up") mouseUp(p, 1, mods);
      else mouseMove(p, mods);
      msg = c;
    }
  } else if (c == "rclick") {
    if (need(2)) { mouseDown(V2(n[0], n[1]), 3, mods); msg = "rclick"; }
  } else if (c == "enter") {
    keyEnter();
  } else if (c == "drag") {
    if (need(4)) {
      V2 a(n[0], n[1]), b(n[2], n[3]);
      mouseDown(a, 1, mods);
      for (int i = 1; i <= 5; i++) mouseMove(a + (b - a) * (i / 5.0f), mods);
      mouseUp(b, 1, mods);
      msg = "dragged";
    }
  } else if (c == "select") {
    if (w.size() > 1 && w[1] == "all") selectAll();
    else if (w.size() > 1 && w[1] == "none") sel.clear();
    else if (w.size() > 1 && w[1] == "box") { if (need(4)) sel = inBox(n[0], n[1], n[2], n[3]); }
    else if (n.size() >= 2) { NodeP h = hitObject(V2(n[0], n[1])); sel = h ? std::vector<NodeP>{h} : std::vector<NodeP>{}; }
    else if (w.size() > 1 && w[1] == "orange") { sel.clear(); for (auto& o : objects()) if (pegTypeOf(*o) == 2) sel.push_back(o); }
    else { ok = false; msg = "usage: select all|none|orange|x y|box x0 y0 x1 y1"; }
    if (ok) msg = std::to_string(sel.size()) + " selected";
  } else if (c == "delete") { deleteSelection(); msg = "deleted"; }
  else if (c == "undo") { ok = undo(); msg = ok ? status : "nothing to undo"; }
  else if (c == "redo") { ok = redo(); msg = ok ? status : "nothing to redo"; }
  else if (c == "copy") { copySelection(); msg = status; }
  else if (c == "cut") { cutSelection(); msg = "cut"; }
  else if (c == "paste") { paste(n.size() >= 2 ? V2(n[0], n[1]) : V2(), n.size() >= 2); msg = "pasted"; }
  else if (c == "dup") { duplicateSelection(n.size() > 0 ? n[0] : 20, n.size() > 1 ? n[1] : 20); msg = "duplicated"; }
  else if (c == "nudge") { if (need(2)) { moveSelection(n[0], n[1]); msg = "moved"; } }
  else if (c == "rotate") { if (need(1)) { rotateSelection(n[0]); msg = "rotated"; } }
  else if (c == "mirror") { mirrorSelection(w.size() < 2 || w[1] != "v"); msg = "mirrored"; }
  else if (c == "align") { if (w.size() > 1) { align(w[1]); msg = "aligned"; } else { ok = false; msg = "align left|right|top|bottom|hcenter|vcenter"; } }
  else if (c == "distribute") { distribute(w.size() < 2 || w[1] != "v"); msg = "distributed"; }
  else if (c == "set") { if (w.size() > 2) { ok = setProp(w[1], rest); msg = ok ? "set " + w[1] : "cannot set " + w[1]; } else { ok = false; msg = "usage: set KEY VALUE"; } }
  else if (c == "mset") { if (w.size() > 2) { ok = setMoverProp(w[1], rest); msg = ok ? "mover " + w[1] + " set" : "no mover or unknown key"; } else { ok = false; msg = "usage: mset KEY VALUE"; } }
  else if (c == "follow") {
    if (n.size() < 2) { ok = false; msg = "usage: follow x y"; }
    else if (NodeP h = hitObject(V2(n[0], n[1]))) { ok = followObject(h); msg = status; }
    else { ok = false; msg = "no object there"; }
  }
  else if (c == "unfollow") { ok = stopFollowing(); msg = status; }
  else if (c == "mover") {
    if (w.size() > 1 && w[1] == "remove") { removeMover(); msg = "mover removed"; }
    else if (w.size() > 2 && w[1] == "add") { ok = addMover(std::atoi(w[2].c_str())); msg = ok ? "mover added" : "no object can take a mover"; }
    else { ok = false; msg = "usage: mover add TYPE | mover remove"; }
  } else if (c == "corner") {
    if (w.size() > 1 && w[1] == "add") { ok = addCorner(); msg = ok ? "corner added" : status; }
    else if (w.size() > 1 && w[1] == "remove") { ok = removeCorner(); msg = ok ? "corner removed" : status; }
    else if (w.size() > 1 && w[1] == "move") {
      auto q = nums(w, 2);
      ok = q.size() >= 3 && setCorner((int)q[0], V2(q[1], q[2]));
      msg = ok ? "corner moved" : (q.size() >= 3 ? status : "usage: corner move N x y");
    } else if (!n.empty()) {
      selectCorner((int)n[0]);
      ok = cornerIndex() >= 0;
      msg = ok ? "corner " + std::to_string(cornerIndex()) : "no such corner";
    } else { ok = false; msg = "usage: corner N | corner add | corner remove | corner move N x y"; }
  } else if (c == "gen") {
    if (w.size() > 1) {
      int type = 1;
      std::vector<std::string> ww(w.begin() + 1, w.end());
      if (!ww.empty() && ww.back() == "orange") { type = 2; ww.pop_back(); }
      int made = generate(ww[0], nums(ww, 1), type);
      ok = made > 0;
      msg = ok ? "generated " + std::to_string(made) + " pegs" : "usage: gen grid|circle|spiral|sine numbers...";
    } else { ok = false; msg = "usage: gen grid|circle|spiral|sine numbers..."; }
  } else if (c == "grid") { if (need(1)) { grid = std::max(0.0f, n[0]); msg = "grid " + std::to_string((int)grid); } }
  else if (c == "snap") { snap = w.size() < 2 || w[1] != "off"; msg = snap ? "snap on" : "snap off"; }
  else if (c == "meta") { if (w.size() > 2) { setMeta(w[1], rest); msg = "meta " + w[1]; } else { ok = false; msg = "usage: meta name|title|background|master|board_width|balls|orange VALUE"; } }
  else if (c == "validate") {
    auto v = validate();
    int e = 0, wn = 0;
    for (auto& i : v) { e += i.sev == Issue::Error; wn += i.sev == Issue::Warn; }
    msg = std::to_string(e) + " error(s), " + std::to_string(wn) + " warning(s)";
    for (auto& i : v) msg += std::string("\n  ") + (i.sev == Issue::Error ? "ERROR " : i.sev == Issue::Warn ? "warn  " : "info  ") + i.text;
  } else if (c == "oranges") {
    previewOranges = w.size() < 2 || w[1] != "off";
    if (n.size() > 0) previewSeed = (uint32_t)n[0]; else if (previewOranges) previewSeed++;
    changed();
    msg = previewOranges ? "random orange preview, seed " + std::to_string(previewSeed) : "stored colours";
  } else if (c == "info") {
    msg = meta.name + ": " + std::to_string(objects().size()) + " objects, " + std::to_string(pegCount()) + " pegs (" + std::to_string(pegCount(2)) + " orange), " +
          std::to_string(sel.size()) + " selected, undo " + std::to_string(undo_.size()) + ", " + (dirty() ? "modified" : "saved");
  } else {
    ok = false;
    msg = "unknown editor command '" + c + "'";
  }
  status = msg.substr(0, msg.find('\n'));
  if (okOut) *okOut = ok;
  return msg;
}

}
