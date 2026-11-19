#include "editor/editor.h"
#include "gamepaths.h"
#include "mods/manager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace pp;
using namespace pp::ed;
namespace fs = std::filesystem;

static int failures;
static void check(bool ok, const std::string& name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
  if (!ok) ++failures;
}
static void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  out.exceptions(std::ios::badbit | std::ios::failbit);
  out << text;
}
static std::string get(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read fixture");
  return {std::istreambuf_iterator<char>(in), {}};
}
static std::string geometry(float x) {
  Editor e;
  e.place(Tool::PegBlue, {x, 180});
  auto bytes = writeLevel(*e.level);
  return {bytes.begin(), bytes.end()};
}
static bool isBase(const std::shared_ptr<const Level>& base, const std::string& expected) {
  if (!base) return false;
  auto bytes = writeLevel(*base);
  return std::string(bytes.begin(), bytes.end()) == expected;
}
static auto files(const fs::path& root) {
  std::map<fs::path, std::string> out;
  for (const auto& f : fs::recursive_directory_iterator(root))
    if (f.is_regular_file()) out.emplace(f.path().lexically_relative(root), get(f.path()));
  return out;
}

static Vfs discover(const fs::path& user) {
  mods::ModManager manager;
  manager.setUserDir(user);
  manager.discover();
  Vfs v;
  manager.activate(&v);
  return v;
}

static void manifestReads(const fs::path& user) {
  const auto pack = user / "mods/Garden", ini = pack / "mod.ini", json = pack / "mod.json";
  put(user / "mods.json", R"({"enabled":["garden","flowers"]})");
  put(pack / "levels/Board.dat", geometry(180));
  auto provider = [&] {
    auto v = discover(user);
    auto owner = v.owner("levels/Board.dat");
    return owner ? owner->provider : std::string();
  };
  check(provider() == "mod:garden", "absent manifests retain the legacy folder identity");
  for (auto path : {ini, json}) {
    const auto name = pathToUtf8(path.filename());
    const std::string text = path == ini ? "id = flowers\n" : R"({"id":"flowers"})";
    put(path, text + std::string(1 << 20, ' '));
    check(provider().empty(), "over-limit " + name + " does not fall back at managed discovery");
    put(path, text + std::string(16000, ' '));
    check(provider() == "mod:flowers", "readable " + name + " supplies the declared ID");
#ifdef _WIN32
    for (bool partial : {false, true}) {
      HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, partial ? FILE_SHARE_READ | FILE_SHARE_WRITE : 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold " + name);
      OVERLAPPED range{};
      range.Offset = 12000;
      if (partial && !LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range)) throw std::runtime_error("cannot lock " + name);
      const auto id = provider();
      CloseHandle(h);
      check(id.empty(), (partial ? "incomplete " : "unreadable ") + name + " is not mounted");
    }
    check(provider() == "mod:flowers", "managed discovery retries " + name + " after unlock");
#endif
    put(ini, "id = garden\n");
  }
  put(json, std::string((1 << 20) + 1, ' '));
  const auto valid = user / "mods/Spring";
  put(valid / "mod.ini", "id = garden\n");
  put(valid / "levels/Spring.dat", geometry(230));
  auto fresh = discover(user);
  const auto owner = fresh.owner("levels/Spring.dat");
  check(!fresh.exists("levels/Board.dat") && owner && owner->provider == "mod:garden", "failed discovery does not reserve the folder ID against a valid pack");
}

static void linkedExports(const fs::path& user) {
  const auto mods = user / "mods", donor = mods / "Garden", dest = mods / "My Edits";
  put(user / "mods.json", R"({"enabled":["flower_bed","my_edits"]})");
  put(donor / "mod.json", R"({"id":"Flower Bed"})");
  put(donor / "mod.ini", "name = Garden\npriority = 20\n");
  put(donor / "levels/Board.dat", geometry(180));
  put(donor / "levels/_petals.dat", geometry(260));
  put(donor / "levels/Board.ini", "base = _petals\ntitle = Garden\n");
  auto v = discover(user);
  Editor e;
  e.setVfs(&v);
  std::string err;
  if (!e.loadFromVfs(v, "Board", &err) || !e.saveDat(user / "Previous.dat", &err)) throw std::runtime_error(err);
  e.place(Tool::PegOrange, {350, 200});
  e.setMeta("title", "Edited");
  e.setMeta("name", "Copy");
  auto refused = [&](const fs::path& dir, const std::string& reason) {
    const bool existed = fs::exists(dir);
    const auto before = files(user);
    const auto edited = writeLevel(*e.level);
    const auto depth = e.undoDepth();
    err.clear();
    check(!e.saveToMod(dir, &err) && err.find(reason) != err.npos && fs::exists(dir) == existed && files(user) == before &&
          writeLevel(*e.level) == edited && e.dirty() && e.undoDepth() == depth && e.savedPath == user / "Previous.dat",
          "refused export preserves all files and unsaved edit: " + pathToUtf8(dir.filename()) + " / " + reason);
  };
  refused(mods / "Flower Bed", "flower_bed");
  put(dest / "levels/Copy.dat", geometry(510));
  put(dest / "levels/Copy.ini", "title = Earlier edit\n");
  put(dest / "levels/mod_levels.cfg", "Stage\n{\nLevel: Copy, Earlier edit\n}\n");
  put(dest / "mod.ini", "id = Flower Bed\n");
  refused(dest, "flower_bed");
  put(dest / "mod.ini", "id = my_edits\n");
  put(dest / "mod.json", R"({"id":"Flower Bed"})");
  refused(dest, "flower_bed");
  fs::remove_all(dest);
  e.setMeta("name", "Board");
  refused(dest, "level name");
  put(dest / "mod.ini", "priority = 20\n");
  refused(dest, "level name");
  fs::remove_all(dest);
  auto plays = [&](const fs::path& expected) {
    auto fresh = discover(user);
    for (const auto& row : listStageLevels(fresh)) {
      if (row.name != e.meta.name || row.title != "Edited") continue;
      LevelBundle lb;
      auto owner = fresh.owner("levels/" + row.name + ".dat");
      if (owner && owner->root == expected && loadLevelBundle(fresh, row.name, lb, &err) && lb.title == row.title &&
          writeLevel(*lb.level) == writeLevel(*e.level) && lb.baseLink.provider == "mod:flower_bed" && isBase(lb.base, geometry(260))) return true;
    }
    return false;
  };
  e.setMeta("name", "Copy");
  check(e.saveToMod(dest / "", &err) && plays(dest), "distinct level name plays the edit by its exported row with the donor enabled");
#ifdef _WIN32
  const auto before = files(dest);
  const auto previous = e.savedPath;
  e.place(Tool::PegBlue, {410, 180});
  const auto depth = e.undoDepth();
  HANDLE held = CreateFileW((donor / "mod.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold donor manifest");
  auto missing = discover(user);
  CloseHandle(held);
  LevelBundle unavailable;
  e.setVfs(&missing);
  check(!loadLevelBundle(missing, "Copy", unavailable, &err) && !e.saveToMod(dest, &err) && files(dest) == before &&
        e.dirty() && e.undoDepth() == depth && e.savedPath == previous, "failed donor discovery refuses fresh play and preserves the saved export and current edit");
  v = discover(user);
  e.setVfs(&v);
  check(e.saveToMod(dest, &err) && plays(dest), "rediscovering the readable donor restores save and by-name play");
#endif
  e.setMeta("name", "Board");
  fs::remove_all(dest);
  put(dest / "mod.ini", "id = Flower Bed\npriority = 30\n");
  put(dest / "mod.json", R"({"id":"my_edits"})");
  check(e.saveToMod(dest, &err) && plays(dest), "distinct JSON identity and winning INI priority play the same-name edit by row");
  put(dest / "mod.ini", "priority = 20\n");
  put(user / "mods.json", R"({"enabled":["flower_bed","my_edits"],"order":["my_edits","flower_bed"]})");
  v = discover(user);
  e.place(Tool::PegOrange, {440, 220});
  check(e.saveToMod(dest, &err) && plays(dest), "an active equal-priority export already ahead of its donor saves in place");
  fs::remove_all(dest);
  check(e.saveToMod(donor, &err) && plays(donor), "saving back into the donor plays the edit in place");
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto root = fs::path(argv[1]) / "editor_provider_fixture";
  fs::remove_all(root);
  manifestReads(root / "manifest-user");
  linkedExports(root / "export-user");
  const auto host = root / "host", donor = root / "source-folder", other = root / "other", output = root / "out";
  const auto base = "levels/_private.dat", board = "levels/Board.dat", ini = "levels/Board.ini", art = "levels/Scene.tga";
  const auto stock = geometry(100), linked = geometry(310), different = geometry(520);
  const std::string hash = std::to_string(fnv1a(linked.data(), linked.size()));
  put(host / "levels/_base.dat", stock);
  put(host / ini, "board_width = 800\n");
  put(donor / "mod.ini", "id = original\nname = Shared label\npriority = 2\n");
  put(other / "mod.ini", "id = impostor\nname = Shared label\npriority = 10\n");
  put(donor / base, linked);
  put(other / base, different);
  put(donor / board, geometry(220));
  put(donor / ini, std::string(12000, ' ') + "\nbase = _private\nbackground = Scene\n");
  const std::string pixels = "donor-only art sentinel";
  const auto imported = "imported_" + std::to_string(fnv1a(pixels.data(), pixels.size()));
  put(donor / art, pixels);
  put(donor / "levels" / (imported + ".tga"), pixels);
  put(host / "levels/NoSide.dat", geometry(150));
  Vfs v;
  Editor e;
  e.setVfs(&v);
  std::string err;
  auto mount = [&](const fs::path& p) { if (!v.mountMod(p, &err)) throw std::runtime_error(err); };
  mount(host); mount(donor); mount(other);
  const std::string disk = pathToUtf8(donor / board);
  for (const std::string source : {std::string("Board"), disk}) {
    check(e.loadFromVfs(v, source, &err) && isBase(e.base, linked), "name-only Open uses board owner's geometry: " + source);
  }
  check(e.loadDatFile(donor / board, &err) && isBase(e.base, linked), "direct DAT import uses board owner");
  v.unmountMod(other);
  if (!e.loadFromVfs(v, "Board", &err)) return 2;
  e.setMeta("name", "Copy");
  const auto dat = output / "Board.dat", json = output / "Board.json", mod = output / "export";
  fs::create_directories(output);
  auto save = [&](int n) { return n == 0 ? e.saveDat(dat, &err) : n == 1 ? e.saveJson(json, &err) : e.saveToMod(mod, &err); };
  for (int n : {0, 1, 2}) if (!save(n)) throw std::runtime_error(err);
  for (const auto& p : {output / "Board.ini", mod / "levels/Copy.ini"}) {
    auto fields = readIni(get(p));
    check(fields["base"] == "_private" && fields["base_hash"] == hash && fields["base_provider"] == "mod:original", "sidecar records provider ID, key and hash: " + pathToUtf8(p.filename()));
  }
  const auto reserved = root / "reserved";
  fs::create_directory(reserved);
  e.setArt(Editor::ArtSlot::Background, imported);
  check(e.saveDat(reserved / "Board.dat", &err) && e.saveJson(reserved / "Board.json", &err) && e.saveToMod(reserved / "pack", &err) && files(reserved).size() == 7,
        "donor background with an imported key stays linked, not copied");
  const auto conflict = root / "conflict";
  put(conflict / (imported + ".tga"), "wrong pixels");
  check(!e.saveDat(conflict / "Board.dat", &err) && files(conflict).size() == 1, "linked generated background cannot be shadowed in the destination");
  e.setArt(Editor::ArtSlot::Background, "Scene");
  check(files(output).size() == 7 && !fs::exists(mod / art) && !fs::exists(mod / base), "exports contain only DAT, JSON and metadata; no donor assets");
  mount(mod);
  e.place(Tool::PegOrange, {450, 200});
  const auto oldPath = e.savedPath;
  const auto depth = e.undoDepth();
  const auto savedFiles = files(output);
  v.unmountMod(donor);
  put(other / base, linked);
  mount(other);
  check(!v.exists(art), "other ID supplies identical geometry but not the artwork");
  for (int n : {0, 1, 2}) {
    err.clear();
    check(!save(n) && err.find("_private") != err.npos && files(output) == savedFiles && e.dirty() && e.undoDepth() == depth && e.savedPath == oldPath,
          "missing provider refuses save without changing files or edit: " + std::to_string(n));
  }
  Editor opened;
  opened.setVfs(&v);
  auto reload = [&](int n) { return n == 0 ? opened.loadDatFile(dat, &err) : n == 1 ? opened.loadJson(json, &err) : opened.loadFromVfs(v, "Copy", &err); };
  for (int n : {0, 1, 2}) check(!reload(n), "fresh load rejects identical wrong-ID base: " + std::to_string(n));
  const auto moved = root / "moved";
  fs::rename(donor, moved);
  mount(moved);
  put(other / base, different);
  for (int n : {0, 1, 2}) check(reload(n) && isBase(opened.base, linked) && opened.meta.background == "Scene", "relocated donor with same ID restores linked level: " + std::to_string(n));
  const auto duplicate = root / "duplicate";
  put(duplicate / "mod.ini", "id = original\n");
  put(duplicate / base, linked);
  mount(duplicate);
  check(!reload(2), "ambiguous mounted provider ID refuses reload");
  v.unmountMod(duplicate);
  put(moved / base, different);
  for (int n : {0, 1, 2}) check(!reload(n), "same ID with changed bytes fails fingerprint: " + std::to_string(n));
  put(moved / base, linked);
  v.unmountMod(other);
  check(opened.loadFromVfs(v, "NoSide", &err) && isBase(opened.base, stock), "missing sidecar defaults to stock");
#ifdef _WIN32
  const auto side = moved / ini;
  for (bool partial : {false, true}) {
    HANDLE h = CreateFileW(side.c_str(), GENERIC_READ | GENERIC_WRITE, partial ? FILE_SHARE_READ | FILE_SHARE_WRITE : 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 2;
    OVERLAPPED range{};
    range.Offset = 12001;
    if (partial && !LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range)) return 2;
    for (const std::string source : {pathToUtf8(moved / board), std::string("Board")}) {
      Editor live;
      live.setVfs(&v);
      if (!live.loadDatFile(dat, &err)) return 2;
      live.place(Tool::PegOrange, {430, 250});
      auto level = live.level;
      auto history = live.undoDepth();
      err.clear();
      check(!live.loadFromVfs(v, source, &err) && err.find(".ini") != err.npos && live.level == level && isBase(live.base, linked) && live.dirty() && live.undoDepth() == history && live.savedPath == dat,
            std::string(partial ? "partial" : "unreadable") + " selected sidecar refuses Open, preserves edit: " + source);
    }
    CloseHandle(h);
  }
  check(opened.loadFromVfs(v, pathToUtf8(moved / board), &err) && isBase(opened.base, linked), "Open retries after sidecar unlock");
  put(other / ini, "background = Override\n");
  mount(other);
  HANDLE overlay = CreateFileW((other / ini).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (overlay == INVALID_HANDLE_VALUE) return 2;
  const auto prior = opened.level;
  check(!opened.loadFromVfs(v, "Board", &err) && opened.level == prior, "unreadable sidecar-only overlay never falls through to the lower board");
  CloseHandle(overlay);
#endif
  return failures ? 1 : 0;
}
