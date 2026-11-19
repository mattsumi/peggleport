#include "editor/editor.h"
#include "physball.h"

#include <cmath>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace pp;
using namespace pp::ed;
namespace fs = std::filesystem;

static int checks = 0, failures = 0;

static void check(bool pass, const std::string& name) {
  ++checks;
  if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}

static void datWidth(const fs::path& root) {
  Editor e;
  e.newLevel("Wide");
  e.meta.boardWidth = 917;
  e.place(Tool::PegOrange, {720, 250});
  std::string err;
  const fs::path dat = root / "Wide.dat";
  e.setArt(Editor::ArtSlot::Background, "Garden");
  check(e.saveDat(dat, &err), "save wide DAT: " + err);
  Editor loaded;
  check(loaded.loadDatFile(dat, &err), "load wide DAT: " + err);
  check(loaded.meta.background == "Garden", "standalone DAT restores the chosen background from its sidecar");
  check(loaded.meta.boardWidth == 917, "DAT sidecar preserves 917 field / 1067 full canvas");
  check(loaded.objects().size() == 1 && loaded.objects()[0]->f("x") == 720, "DAT preserves extended authored coordinate");
  {
    std::ofstream side(root / "Wide.ini");
    side << "title = Preserved\npeg = 0: custom_one\npeg = 1: custom_two\nboard_width = 800\nboard_width = 1000\n";
  }
  check(e.saveDat(dat, &err), "replace width without discarding unrelated sidecar keys");
  std::ifstream side(root / "Wide.ini");
  std::string text((std::istreambuf_iterator<char>(side)), {});
  side.close();
  check(text.find("title = Preserved") != std::string::npos && text.find("peg = 0: custom_one") != std::string::npos && text.find("peg = 1: custom_two") != std::string::npos, "DAT save preserves title and repeated custom peg definitions");
  check(text.find("board_width = 917") != std::string::npos && text.find("board_width = 800") == std::string::npos && text.find("board_width = 1000") == std::string::npos, "DAT save replaces stale duplicate widths");
  e.setMeta("board_width", "650");
  check(e.saveDat(dat, &err) && loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 650, "classic save clears stale wide metadata");
  fs::remove(root / "Wide.ini");
  loaded.setMeta("board_width", "917");
  check(loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 650, "DAT without sidecar restores classic width");
  fs::create_directories(root / "Wide.ini");
  auto previous = loaded.level;
  check(!loaded.loadDatFile(dat, &err) && loaded.level == previous, "unreadable sidecar fails without replacing the live level");
  fs::remove(root / "Wide.ini");
}

static std::string fileBytes(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  check(file.is_open(), "read fixture " + path.string());
  std::string bytes((std::istreambuf_iterator<char>(file)), {});
  check(!file.bad(), "read complete fixture " + path.string());
  return bytes;
}

static void backgroundFiles(const fs::path& root) {
  const fs::path folder = root / "art-import", library = folder / "working";
  fs::remove_all(folder);
  auto image = [&](const char* sub, uint8_t red) {
    auto path = folder / sub / "scene.tga";
    fs::create_directories(path.parent_path());
    const uint8_t tga[] = {0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,24,32,0,0,red};
    std::ofstream(path, std::ios::binary).write((const char*)tga, sizeof tga);
    return path;
  };
  auto first = image("one", 50), second = image("two", 200);
  const std::string one = fileBytes(first), two = fileBytes(second);
  Editor e;
  std::string err;
  e.newLevel("Travel");
  e.meta.boardWidth = 917;
  if (!e.importBackground(first, library, &err)) { check(false, "import first background: " + err); return; }
  const std::string a = e.meta.background;
  check(e.importBackground(second, library, &err), "import second background with same basename");
  const std::string b = e.meta.background;
  check(a != b && fileBytes(library / (a + ".tga")) == one && fileBytes(library / (b + ".tga")) == two, "both owned images retain exact source bytes");
  std::ofstream(folder / "bad.png") << "not an image";
  const int rev = e.revision;
  check(!e.importBackground(folder / "bad.png", library, &err) && e.revision == rev && e.meta.background == b, "invalid image does not change selection or history");
  check(e.importBackground(first, library, &err) && e.meta.background == a, "same image reuses the same asset");
  fs::rename(first, first.parent_path() / "moved.tga");
  fs::rename(second, second.parent_path() / "moved.tga");
  const fs::path exports = folder / "exports", dat = folder / "loose" / "Travel.dat";
  fs::create_directories(exports);
  std::ofstream(exports / (a + ".tga")) << "unrelated";
  check(e.saveJson(exports / "Travel.json", &err) && e.saveDat(dat, &err), "export JSON and DAT after moving originals");
  fs::remove_all(library);
  for (bool json : {false, true}) {
    Editor reopened;
    check(json ? reopened.loadJson(exports / "Travel.json", &err) : reopened.loadDatFile(dat, &err), "fresh editor loads portable export");
    const std::string bg = reopened.meta.background;
    check(fs::path(bg).filename() == bg && reopened.meta.boardWidth == 917 && fileBytes((json ? exports : dat.parent_path()) / (bg + ".tga")) == one, "relative image and authored width survive round trip");
    check(fileBytes(exports / (a + ".tga")) == "unrelated" && (!json || bg != a), "JSON export kept unrelated asset and used its own key");
    fs::path pack = folder / (json ? "FromJSON" : "FromDAT");
    fs::create_directories(pack / "levels");
    std::ofstream(pack / "levels" / (a + ".jpg")) << "other extension";
    check(reopened.saveToMod(pack, &err), "package reopened level without source or authoring library");
    Editor packed;
    check(packed.loadDatFile(pack / "levels/Travel.dat", &err) && packed.meta.boardWidth == 917 && packed.meta.background != a && fileBytes(pack / "levels" / (packed.meta.background + ".tga")) == one, "portable pack keeps width and selected pixels despite cross-format conflict");
    check(fileBytes(pack / "levels" / (a + ".jpg")) == "other extension", "pack collision did not overwrite existing art");
  }
}

static void datSavePreservation(const fs::path& root) {
  for (bool hadDat : {false, true}) for (bool hadIni : {false, true}) for (bool blockDat : {false, true}) {
    const std::string label = "DAT pair " + std::to_string(hadDat) + std::to_string(hadIni) + std::to_string(blockDat);
    const fs::path dir = root / label;
    fs::remove_all(dir);
    fs::create_directories(dir);
    Editor e;
    e.newLevel("Pair");
    e.place(Tool::PegOrange, {300, 200});
    std::string err;
    const fs::path prior = dir / "Prior.dat", dat = dir / "Pair.dat", ini = dir / "Pair.ini";
    check(e.saveDat(prior, &err), label + " establish prior save");
    const std::string oldDat = fileBytes(prior);
    const std::string oldIni = "title = Original\r\npeg = 0: custom_one\r\nboard_width = 650";
    if (hadDat) fs::copy_file(prior, dat, fs::copy_options::overwrite_existing);
    if (hadIni) {
      std::ofstream file(ini, std::ios::binary);
      file << oldIni;
      file.close();
      check((bool)file, label + " prepare original INI");
    }
    const fs::path blocked = blockDat ? dat : ini;
    const bool hadBlocked = blockDat ? hadDat : hadIni;
    fs::perms permissions = fs::perms::unknown;
    if (hadBlocked) {
      permissions = fs::status(blocked).permissions();
      fs::permissions(blocked, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
      std::ofstream probe(blocked, std::ios::binary | std::ios::app);
      check(!probe, label + " target really denies writes");
    } else {
      fs::create_directory(blocked);
    }
    e.setMeta("board_width", "917");
    e.setMeta("title", "Changed");
    e.place(Tool::PegBlue, {720, 250});
    const auto depth = e.undoDepth();
    const bool dirty = e.dirty(), metaDirty = e.metaDirty;
    err.clear();
    check(!e.saveDat(dat, &err) && !err.empty(), label + " reports blocked output");
    check(e.savedPath == prior && e.dirty() == dirty && e.metaDirty == metaDirty && e.undoDepth() == depth, label + " failure preserves target and dirty history");
    if (hadDat) check(fileBytes(dat) == oldDat, label + " failed save preserves DAT byte-for-byte");
    else check(blockDat ? fs::is_directory(dat) : !fs::exists(dat), label + " failed save creates no DAT");
    if (hadIni) check(fileBytes(ini) == oldIni, label + " failed save preserves INI byte-for-byte");
    else check(!blockDat ? fs::is_directory(ini) : !fs::exists(ini), label + " failed save creates no INI");
    if (hadBlocked) fs::permissions(blocked, permissions);
    else fs::remove(blocked);
    check(e.saveDat(dat, &err), label + " complete retry succeeds: " + err);
    check(e.savedPath == dat && !e.dirty() && !e.metaDirty, label + " successful pair publishes target and clean state");
    const auto expected = writeLevel(*e.level);
    check(fileBytes(dat) == std::string(expected.begin(), expected.end()), label + " successful DAT contains current level");
    Editor loaded;
    check(loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 917 && loaded.objects().size() == 2, label + " successful pair reloads together");
    if (hadIni) check(fileBytes(ini).find("peg = 0: custom_one") != std::string::npos, label + " successful pair preserves unrelated metadata");
  }
}

static void datCommitFailure(const fs::path& root) {
#ifdef _WIN32
  for (bool hadDat : {false, true}) {
    const std::string label = "DAT rollback " + std::to_string(hadDat);
    const fs::path dir = root / label;
    fs::remove_all(dir);
    fs::create_directories(dir);
    Editor e;
    e.newLevel("Rollback");
    e.place(Tool::PegOrange, {300, 200});
    std::string err;
    const fs::path prior = dir / "Prior.dat", dat = dir / "Rollback.dat", ini = dir / "Rollback.ini";
    check(e.saveDat(prior, &err), label + " establish prior save");
    if (hadDat) fs::copy_file(prior, dat, fs::copy_options::overwrite_existing);
    fs::copy_file(dir / "Prior.ini", ini, fs::copy_options::overwrite_existing);
    const std::string oldDat = fileBytes(prior), oldIni = fileBytes(ini);
    HANDLE lock = CreateFileW(ini.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock != INVALID_HANDLE_VALUE, label + " deny INI replacement but allow writes");
    if (lock == INVALID_HANDLE_VALUE) continue;
    {
      std::ofstream probe(ini, std::ios::binary | std::ios::app);
      check((bool)probe, label + " INI write preflight succeeds");
    }
    e.setMeta("board_width", "917");
    e.setMeta("title", "Changed");
    e.place(Tool::PegBlue, {720, 250});
    const auto depth = e.undoDepth();
    err.clear();
    check(!e.saveDat(dat, &err) && !err.empty(), label + " reports INI commit failure");
    check(e.savedPath == prior && e.dirty() && e.metaDirty && e.undoDepth() == depth, label + " preserves target and dirty history");
    if (hadDat) check(fileBytes(dat) == oldDat, label + " restores original DAT byte-for-byte");
    else check(!fs::exists(dat), label + " removes newly committed DAT");
    check(fileBytes(ini) == oldIni, label + " preserves original INI byte-for-byte");
    check(CloseHandle(lock) != 0, label + " releases obstruction");
    check(e.saveDat(dat, &err), label + " retry saves complete pair");
    check(!e.dirty() && !e.metaDirty && e.savedPath == dat, label + " publishes only successful retry");
    Editor loaded;
    check(loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 917 && loaded.objects().size() == 2, label + " retry reloads complete pair");
    size_t staged = 0;
    for (const auto& entry : fs::directory_iterator(dir)) staged += entry.is_directory();
    check(staged == 0, label + " no leftover staging directories");
  }
#endif
}

#ifdef _WIN32
static HANDLE lockFile(const fs::path& path, bool range) {
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | (range ? FILE_SHARE_DELETE : 0),
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  check(handle != INVALID_HANDLE_VALUE, "open fixture lock " + path.string());
  if (handle != INVALID_HANDLE_VALUE && range) {
    OVERLAPPED offset{};
    const bool locked = LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &offset) != 0;
    check(locked, "lock fixture bytes " + path.string());
    if (!locked) { CloseHandle(handle); return INVALID_HANDLE_VALUE; }
  }
  return handle;
}
#endif

static void datReadFailure(const fs::path& root) {
#ifdef _WIN32
  const fs::path dir = root / "backup_read", dat = dir / "Locked.dat", ini = dir / "Locked.ini";
  fs::remove_all(dir);
  fs::create_directories(dir);
  Editor e;
  e.newLevel("Locked");
  e.place(Tool::PegOrange, {300, 200});
  std::string err;
  check(e.saveDat(dat, &err), "backup read initial save");
  {
    std::ofstream file(ini, std::ios::binary);
    file << "#" << std::string(8 * 1024 * 1024, 'x') << "\nboard_width = 650\n";
    file.close();
    check((bool)file, "backup read seed sidecar");
  }
  const auto oldDat = fileBytes(dat), oldIni = fileBytes(ini);
  HANDLE datLock = lockFile(dat, true), iniLock = lockFile(ini, false);
  if (datLock == INVALID_HANDLE_VALUE || iniLock == INVALID_HANDLE_VALUE) {
    if (datLock != INVALID_HANDLE_VALUE) CloseHandle(datLock);
    if (iniLock != INVALID_HANDLE_VALUE) CloseHandle(iniLock);
    return;
  }
  std::atomic<bool> finished = false, backupObserved = false;
  std::thread release([&] {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!finished && std::chrono::steady_clock::now() < until) {
      std::error_code ec;
      if (fs::exists(dir / "Locked.dat.save-0/old.dat", ec)) { backupObserved = true; break; }
      std::this_thread::yield();
    }
    CloseHandle(datLock);
  });
  e.setMeta("board_width", "917");
  e.setMeta("title", "Changed");
  e.place(Tool::PegBlue, {720, 250});
  const auto depth = e.undoDepth();
  const bool saved = e.saveDat(dat, &err);
  finished = true;
  release.join();
  CloseHandle(iniLock);
  check(!saved && err.find("cannot read") != std::string::npos, "backup read rejects incomplete DAT before commit");
  check(!backupObserved, "backup read never stages an invalid backup");
  check(fileBytes(dat) == oldDat && fileBytes(ini) == oldIni, "backup read failure preserves both originals byte-for-byte");
  check(e.savedPath == dat && e.dirty() && e.metaDirty && e.undoDepth() == depth, "backup read preserves save target and dirty history");
  check(e.saveDat(dat, &err), "backup read retry succeeds");
  Editor loaded;
  check(loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 917 && loaded.objects().size() == 2, "backup read retry reloads current pair");
#endif
}

static void modIndexFailure(const fs::path& root) {
#ifdef _WIN32
  for (bool hadTarget : {false, true}) for (bool range : {false, true}) {
    const std::string label = "mod index " + std::to_string(hadTarget) + std::to_string(range);
    const fs::path mod = root / label, dat = mod / "levels/Existing.dat", ini = mod / "levels/Existing.ini";
    const fs::path index = mod / "levels/mod_levels.cfg", metadata = mod / "mod.ini";
    fs::remove_all(mod);
    Editor e;
    e.newLevel("First");
    e.place(Tool::PegOrange, {300, 200});
    std::string err;
    check(e.saveToMod(mod, &err), label + " seed prior registration");
    e.newLevel("Existing");
    e.place(Tool::PegOrange, {300, 200});
    if (hadTarget) check(e.saveToMod(mod, &err), label + " seed actual destination");
    const auto oldIndex = fileBytes(index), oldMetadata = fileBytes(metadata);
    const auto oldDat = hadTarget ? fileBytes(dat) : "", oldIni = hadTarget ? fileBytes(ini) : "";
    const auto savedPath = e.savedPath;
    HANDLE lock = lockFile(index, range);
    if (lock == INVALID_HANDLE_VALUE) continue;
    e.place(Tool::PegBlue, {720, 250});
    e.setMeta("board_width", "917");
    e.setMeta("title", "Changed");
    const auto depth = e.undoDepth();
    const bool saved = e.saveToMod(mod, &err);
    CloseHandle(lock);
    check(!saved && !err.empty(), label + " rejects unavailable index");
    check(fileBytes(index) == oldIndex, label + " preserves all registrations byte-for-byte");
    check(hadTarget ? fileBytes(dat) == oldDat : !fs::exists(dat), label + " preserves actual destination DAT or absence");
    check(hadTarget ? fileBytes(ini) == oldIni : !fs::exists(ini), label + " preserves actual destination INI or absence");
    check(fileBytes(metadata) == oldMetadata, label + " preserves existing mod metadata");
    check(e.savedPath == savedPath && e.dirty() && e.metaDirty && e.undoDepth() == depth, label + " preserves target and dirty history");
    check(e.saveToMod(mod, &err), label + " retry succeeds");
    check(e.savedPath == dat && !e.dirty() && !e.metaDirty, label + " publishes successful retry");
    const auto retried = fileBytes(index);
    for (const std::string name : {"First", "Existing"}) {
      const auto entry = "Level: " + name + ",";
      const auto pos = retried.find(entry);
      check(pos != std::string::npos && retried.find(entry, pos + 1) == std::string::npos, label + " retry retains exactly one " + name);
    }
    Editor loaded;
    check(loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 917 && loaded.objects().size() == 2, label + " retry reloads current destination");
    size_t staged = 0;
    for (const auto& entry : fs::directory_iterator(mod / "levels")) staged += entry.is_directory();
    check(staged == 0, label + " ordinary failure and retry remove staging");
  }
#endif
}

static void modRollbackRecovery(const fs::path& root) {
#ifdef _WIN32
  const fs::path mod = root / "mod_recovery", dat = mod / "levels/Existing.dat", ini = mod / "levels/Existing.ini";
  const fs::path index = mod / "levels/mod_levels.cfg", staging = dat.parent_path() / "Existing.dat.save-0";
  fs::remove_all(mod);
  Editor e;
  e.newLevel("Existing");
  e.place(Tool::PegOrange, {300, 200});
  std::string err;
  check(e.saveToMod(mod, &err), "recovery seed actual destination");
  {
    std::ofstream file(index, std::ios::binary | std::ios::app);
    file << "Level: Other, \"" << std::string(8 * 1024 * 1024, 'x') << "\", 100000, 3\n";
    file.close();
    check((bool)file, "recovery seed long registration");
  }
  const auto oldDat = fileBytes(dat), oldIni = fileBytes(ini), oldIndex = fileBytes(index);
  HANDLE indexLock = lockFile(index, false);
  if (indexLock == INVALID_HANDLE_VALUE) return;
  HANDLE backupLock = INVALID_HANDLE_VALUE;
  std::atomic<bool> finished = false;
  std::thread obstruction([&] {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!finished && std::chrono::steady_clock::now() < until) {
      backupLock = CreateFileW((staging / "old.dat").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (backupLock != INVALID_HANDLE_VALUE) break;
      std::this_thread::yield();
    }
  });
  e.place(Tool::PegBlue, {720, 250});
  e.setMeta("board_width", "917");
  e.setMeta("title", "Changed");
  const auto depth = e.undoDepth();
  const bool saved = e.saveToMod(mod, &err);
  finished = true;
  obstruction.join();
  CloseHandle(indexLock);
  check(backupLock != INVALID_HANDLE_VALUE, "recovery obstruction denies backup rename");
  if (backupLock != INVALID_HANDLE_VALUE) CloseHandle(backupLock);
  check(!saved && err.find("cannot restore") != std::string::npos && err.find("recovery files:") != std::string::npos, "recovery failure reports retained evidence");
  check(e.savedPath == dat && e.dirty() && e.metaDirty && e.undoDepth() == depth, "recovery failure preserves target and dirty history");
  check(fs::is_directory(staging) && err.find(staging.string()) != std::string::npos, "recovery error identifies retained staging path");
  check(fileBytes(staging / "old.dat") == oldDat, "recovery retains complete original DAT bytes");
  check(fileBytes(ini) == oldIni && fileBytes(index) == oldIndex, "recovery restores unblocked outputs and preserves index");
  const auto pending = writeLevel(*e.level);
  check(fileBytes(dat) == std::string(pending.begin(), pending.end()), "recovery does not claim failed DAT rollback succeeded");
  check(e.saveToMod(mod, &err), "recovery retry uses fresh staging");
  check(fileBytes(staging / "old.dat") == oldDat, "recovery retry does not erase retained evidence");
#endif
}

static void modMetadataFailure(const fs::path& root) {
  for (bool hadTarget : {false, true}) for (bool late : {false, true}) {
    const std::string label = "mod metadata " + std::to_string(hadTarget) + std::to_string(late);
    const fs::path mod = root / label, dat = mod / "levels/Existing.dat", ini = mod / "levels/Existing.ini";
    const fs::path index = mod / "levels/mod_levels.cfg", metadata = mod / "mod.ini";
    fs::remove_all(mod);
    Editor e;
    e.newLevel("First");
    e.place(Tool::PegOrange, {300, 200});
    std::string err;
    check(e.saveToMod(mod, &err), label + " seed prior registration");
    e.newLevel("Existing");
    e.place(Tool::PegOrange, {300, 200});
    if (hadTarget) check(e.saveToMod(mod, &err), label + " seed actual destination");
    fs::remove(metadata);
    if (late) {
      std::ofstream file(index, std::ios::binary | std::ios::app);
      file << "Level: Other, \"" << std::string(8 * 1024 * 1024, 'x') << "\", 100000, 3\n";
      file.close();
      check((bool)file, label + " seed long existing registration");
    } else fs::create_directory(metadata);
    const auto oldIndex = fileBytes(index);
    const auto oldDat = hadTarget ? fileBytes(dat) : "", oldIni = hadTarget ? fileBytes(ini) : "";
    const auto savedPath = e.savedPath;
    e.place(Tool::PegBlue, {720, 250});
    e.setMeta("board_width", "917");
    e.setMeta("title", "Changed");
    const auto depth = e.undoDepth();
    std::atomic<bool> finished = false, blocked = false;
    std::thread obstruction;
    if (late) obstruction = std::thread([&] {
      const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!finished && std::chrono::steady_clock::now() < until) {
        std::error_code ec;
        if (fs::exists(mod / "levels/Existing.dat.save-0/new.dat", ec)) {
          blocked = fs::create_directory(metadata, ec);
          break;
        }
        std::this_thread::yield();
      }
    });
    const bool saved = e.saveToMod(mod, &err);
    finished = true;
    if (obstruction.joinable()) obstruction.join();
    if (late) check(blocked, label + " obstructs metadata only after staging begins");
    check(!saved && !err.empty(), label + " rejects metadata directory");
    check(fileBytes(index) == oldIndex, label + " preserves registration byte-for-byte");
    check(hadTarget ? fileBytes(dat) == oldDat : !fs::exists(dat), label + " preserves actual destination DAT or absence");
    check(hadTarget ? fileBytes(ini) == oldIni : !fs::exists(ini), label + " preserves actual destination INI or absence");
    check(e.savedPath == savedPath && e.dirty() && e.metaDirty && e.undoDepth() == depth, label + " preserves target and dirty history");
    check(fs::is_directory(metadata), label + " leaves obstructing metadata untouched");
    fs::remove(metadata);
    check(e.saveToMod(mod, &err), label + " retry completes all required files");
    check(e.savedPath == dat && !e.dirty() && !e.metaDirty, label + " publishes successful retry");
    Editor loaded;
    check(loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 917 && loaded.objects().size() == 2, label + " retry reloads current destination");
    const std::string customMetadata = "id = custom\nname = Keep Me\nversion = 7\npriority = 23\n";
    {
      std::ofstream file(metadata, std::ios::binary);
      file << customMetadata;
      file.close();
      check((bool)file, label + " seed custom mod metadata");
    }
    const auto permissions = fs::status(metadata).permissions();
    fs::permissions(metadata, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
    e.setMeta("title", "Updated again");
    check(e.saveToMod(mod, &err), label + " accepts existing regular read-only metadata");
    check(fileBytes(metadata) == customMetadata, label + " preserves valid existing metadata byte-for-byte");
    fs::permissions(metadata, permissions);
  }
}

static void datIniAlias(const fs::path& root) {
  for (const std::string extension : {".ini", ".INI", ".InI"}) for (bool existed : {false, true}) {
    const std::string label = "case_alias" + extension + std::to_string(existed);
    const fs::path dir = root / label, prior = dir / "Prior.dat", target = dir / "output" / ("Alias" + extension);
    fs::remove_all(dir);
    Editor e;
    e.newLevel("Alias");
    e.place(Tool::PegOrange, {300, 200});
    std::string err;
    check(e.saveDat(prior, &err), label + " establish prior target");
    const auto oldDat = fileBytes(prior), oldIni = fileBytes(dir / "Prior.ini");
    const std::string original = "title = Preserve\nboard_width = 650\n";
    if (existed) {
      fs::create_directories(target.parent_path());
      std::ofstream file(target, std::ios::binary);
      file << original;
      file.close();
      check((bool)file, label + " seed alias destination");
    }
    e.setMeta("title", "Changed");
    e.setMeta("board_width", "917");
    e.place(Tool::PegBlue, {720, 250});
    const auto depth = e.undoDepth();
    const auto bytes = writeLevel(*e.level);
    bool ok = true;
    const auto message = e.exec("save " + target.string(), &ok);
    check(!ok && !message.empty(), label + " explicit save rejects INI extension");
    check(e.savedPath == prior && e.dirty() && e.metaDirty && e.undoDepth() == depth && writeLevel(*e.level) == bytes, label + " preserves save target and dirty content/history");
    check(existed ? fileBytes(target) == original : !fs::exists(target.parent_path()), label + " rejects before any destination writes");
    check(fileBytes(prior) == oldDat && fileBytes(dir / "Prior.ini") == oldIni, label + " preserves prior saved pair");
    e.exec("save", &ok);
    check(ok && !e.dirty() && e.savedPath == prior, label + " implicit retry still saves prior DAT");
    Editor loaded;
    check(loaded.loadDatFile(prior, &err) && loaded.meta.boardWidth == 917 && loaded.objects().size() == 2, label + " retry reloads current pair");
  }
}

static void modSaveFailure(const fs::path& root) {
  for (bool hadTarget : {false, true}) {
    const std::string label = "mod retry " + std::to_string(hadTarget);
    const fs::path dir = root / label, prior = dir / "Prior.dat", mod = dir / "BlockedMod";
    const fs::path dat = mod / "levels/Broken.dat", ini = mod / "levels/Broken.ini", index = mod / "levels/mod_levels.cfg";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Editor e;
    e.newLevel("Broken");
    e.place(Tool::PegOrange, {300, 200});
    std::string err, oldDat, oldIni;
    if (hadTarget) {
      check(e.saveDat(prior, &err), label + " establish old save target");
      oldDat = fileBytes(prior);
      oldIni = fileBytes(dir / "Prior.ini");
    }
    e.setMeta("board_width", "917");
    e.setMeta("title", "Complete Retry");
    e.setMeta("balls", "12");
    e.place(Tool::PegBlue, {720, 250});
    const fs::path savedPath = e.savedPath;
    const auto depth = e.undoDepth();
    const bool dirty = e.dirty(), metaDirty = e.metaDirty;
    fs::create_directories(index);
    check(!e.saveToMod(mod, &err) && !err.empty() && e.dirty(), label + " failed mod index write is reported and stays dirty");
    check(e.savedPath == savedPath && e.dirty() == dirty && e.metaDirty == metaDirty && e.undoDepth() == depth, label + " failed mod save preserves previous target and dirty history");
    check(fs::is_directory(index) && !fs::exists(mod / "mod.ini"), label + " failure has not completed mod registration");
    if (hadTarget) {
      check(fileBytes(prior) == oldDat && fileBytes(dir / "Prior.ini") == oldIni, label + " failed export leaves old target byte-identical");
      check(e.undo() && e.undo() && e.meta.boardWidth == 650 && e.metaDirty && e.dirty(), label + " failure preserves prior clean marker and metadata dirtiness");
      e.metaDirty = false;
      check(!e.dirty(), label + " original geometry save point remains clean");
      e.metaDirty = metaDirty;
      check(e.redo() && e.redo(), label + " restore pending changes for retry");
    }
    fs::remove(index);
    auto quickSave = [&] {
      if (!e.savedPath.empty() && e.savedPath.extension() == ".dat" && e.saveDat(e.savedPath, &err)) return true;
      return e.saveToMod(mod, &err);
    };
    check(quickSave(), label + " ordinary Save routing succeeds after obstruction removal");
    if (hadTarget) {
      check(e.savedPath == prior && !fs::exists(index) && !fs::exists(mod / "mod.ini"), label + " ordinary Save still addresses old target rather than incomplete export");
      Editor previous;
      check(previous.loadDatFile(prior, &err) && previous.meta.boardWidth == 917 && previous.objects().size() == 2, label + " ordinary Save updates preserved old target");
      e.setMeta("title", "Complete Retry");
      check(e.saveToMod(mod, &err), label + " explicit mod retry completes export from old target");
    }
    check(e.savedPath == dat && !e.dirty() && !e.metaDirty, label + " complete mod save publishes new target and clean state");
    check(fs::is_regular_file(dat) && fs::is_regular_file(ini) && fs::is_regular_file(index) && fs::is_regular_file(mod / "mod.ini"), label + " retry completes all four mod files");
    if (fs::is_regular_file(index)) {
      const std::string entries = fileBytes(index);
      const std::string entry = "Level: Broken, \"Complete Retry\", 100000, 3";
      const auto first = entries.find(entry);
      check(first != std::string::npos && entries.find(entry, first + 1) == std::string::npos, label + " retry indexes level exactly once");
    }
    const auto metadata = readIni(fileBytes(ini));
    check(metadata.count("title") && metadata.at("title") == "Complete Retry" && metadata.count("board_width") && metadata.at("board_width") == "917" && metadata.count("balls") && metadata.at("balls") == "12", label + " retry saves complete level metadata");
    if (fs::is_regular_file(mod / "mod.ini")) {
      const auto info = readIni(fileBytes(mod / "mod.ini"));
      check(info.count("id") && info.at("id") == "blockedmod" && info.count("name") && info.at("name") == "BlockedMod", label + " retry writes mod identity");
    }
    Editor loaded;
    check(loaded.loadDatFile(dat, &err) && loaded.meta.boardWidth == 917 && loaded.objects().size() == 2, label + " retry reloads complete authored level");
  }
}

static void widthUndo() {
  Editor e;
  NodeP n = e.place(Tool::PegBlue, {320, 200});
  const auto depth = e.undoDepth();
  e.setMeta("board_width", "917");
  check(e.meta.boardWidth == 917 && e.undoDepth() == depth + 1, "width edit is one undo step");
  check(e.undo() && e.meta.boardWidth == 650 && e.objects().size() == 1, "undo width without undoing placement");
  check(e.redo() && e.meta.boardWidth == 917 && n->f("x") == 320, "redo width leaves authored position unchanged");
  const auto nextDepth = e.undoDepth();
  e.setMeta("board_width", "invalid");
  check(e.meta.boardWidth == 917 && e.undoDepth() == nextDepth, "invalid width is rejected without history");
  e.setMeta("board_width", "917");
  check(e.undoDepth() == nextDepth, "unchanged width creates no undo step");
  e.exec("new Classic 917");
  check(e.meta.boardWidth == 650, "New always creates a classic level even after editing a wide level");
}

template<class E> static void roles() {
  if constexpr (requires(E e, NodeP n) { e.collisionRole(n); e.setCollisionRole("terrain"); }) {
    E e;
    NodeP peg = e.place(Tool::PegOrange, {200, 200});
    check(e.collisionRole(peg) == "peg", "orange peg role");
    check(e.setCollisionRole("terrain"), "assign terrain role");
    check(e.collisionRole(peg) == "terrain" && !pegOf(*peg) && peg->flag("collidable"), "terrain has no clearing peg and collides");
    check(e.undo() && e.collisionRole(peg) == "peg" && pegTypeOf(*peg) == 2, "undo role restores orange peg");
    check(e.redo() && e.collisionRole(peg) == "terrain", "redo role restores terrain");
  } else {
    check(false, "Editor exposes collisionRole and setCollisionRole");
  }
}

static void roleValidation() {
  Editor e;
  NodeP peg = e.place(Tool::PegBlue, {200, 200});
  NodeP hole = e.place(Tool::Hole, {300, 300});
  NodeP emitter = e.place(Tool::Emitter, {400, 200});
  NodeP outside = newCircle(500, 200, 10, 1);
  const auto before = writeLevel(*e.level);
  const auto depth = e.undoDepth();
  for (const auto& selection : std::vector<std::vector<NodeP>>{{}, {nullptr}, {peg, nullptr}, {peg, hole}, {peg, emitter}, {outside}}) {
    e.select(selection);
    check(!e.setCollisionRole("terrain"), "reject unsupported or stale selection atomically");
    check(writeLevel(*e.level) == before && e.undoDepth() == depth, "rejected role leaves content/history unchanged");
  }
  check(e.collisionRole(nullptr).empty() && e.collisionRole(hole).empty() && e.collisionRole(emitter).empty(), "unsupported objects have empty role");
  e.select({peg});
  check(!e.setCollisionRole("solid") && e.undoDepth() == depth, "invalid role rejected");
  check(e.setCollisionRole("peg") && e.undoDepth() == depth, "unchanged role creates no undo step");
  NodeP poly = newPoly(400, 200, {{-20, -10}, {-20, 10}, {20, 10}, {20, -10}});
  NodeP line = newLine(480, 200, 520, 200, true);
  e.addNodes({poly, line}, "Shapes");
  e.select({peg, poly, line});
  check(!e.setCollisionRole("peg") && e.collisionRole(peg) == "peg", "polygon/line cannot become scoring pegs");
  check(e.setCollisionRole("decoration"), "mixed supported selection converts together");
  check(e.collisionRole(peg) == "decoration" && e.collisionRole(poly) == "decoration" && e.collisionRole(line) == "decoration", "mixed selection has noncolliding decoration role");
  check(e.undo() && e.collisionRole(peg) == "peg" && e.collisionRole(poly) == "terrain" && e.collisionRole(line) == "terrain", "mixed role undo restores each original role");
  e.select({peg});
  check(e.addMover(3), "role fixture mover");
  Node* mover = moverOf(*e.level, *peg);
  const Geo beforeMove = geometry(*e.level, *peg);
  putStr(*peg, "image", "custom_picture");
  e.select({peg, peg});
  const auto beforeRole = e.undoDepth();
  check(e.setCollisionRole("bumper") && e.undoDepth() == beforeRole + 1, "duplicate selection creates one role transaction");
  const Geo afterMove = geometry(*e.level, *peg);
  check(moverOf(*e.level, *peg) == mover && beforeMove.c.x == afterMove.c.x && beforeMove.c.y == afterMove.c.y && peg->str("image") == "custom_picture", "role preserves art and mover geometry");
  check(e.undo() && e.collisionRole(peg) == "peg" && moverOf(*e.level, *peg) == mover, "role undo preserves mover identity");
  check(e.redo() && e.collisionRole(peg) == "bumper", "role redo preserves mover");
  check(e.setCollisionRole("peg") && pegTypeOf(*peg) == 1, "bumper converts back to scoring peg");
  e.tool = Tool::Select;
  const V2 pos = nodePos(*e.level, *peg);
  e.select({peg});
  e.mouseDown(pos, 1, {});
  e.mouseMove(pos + V2(30, 0), {});
  check(e.setCollisionRole("terrain") && !e.gestureActive(), "role edit safely cancels an in-progress drag");
  check(e.undo() && e.collisionRole(peg) == "peg" && nodePos(*e.level, *peg).x == pos.x, "undo role cannot resurrect an uncommitted drag");
}

static void boardContacts(Editor& e, const NodeP& target, const std::string& role, const std::string& label) {
  Board board;
  LevelBundle bundle;
  std::vector<NodeP> nodes;
  std::string err;
  if (!e.buildBoard(board, bundle, &nodes, 4357, true, &err)) { check(false, label + " buildBoard: " + err); return; }
  const auto it = std::find(nodes.begin(), nodes.end(), target);
  if (it == nodes.end()) { check(false, label + " maps built object to live node"); return; }
  int index = (int)(it - nodes.begin());
  const Obj& obj = board.objs[index];
  check(obj.collidable == (role != "decoration"), label + " built collidable flag");
  check(obj.visible == target->flag("visible"), label + " preserves authored visibility");
  check((obj.pegType != 0) == (role == "peg"), label + " built clearing flag");
  check(board.pivot().x == e.playfieldW() * 0.5f, label + " centered launcher");
  const Geo authored = geometry(*e.level, *target);
  check(std::fabs(obj.x0 - authored.x0 - e.wideShift()) < 0.01f && std::fabs(obj.y0 - authored.y0) < 0.01f, label + " translated not stretched");
  board.emit = false;
  check(board.fire(4.712389f), label + " test play fires");
  Ball ball;
  phys::ballSetPos(ball, (obj.x0 + obj.x1) * 0.5f, obj.y0 - 18);
  ball.vel = {0, 15 - board.physics.gravityY};
  board.balls[0] = ball;
  bool contacted = false, rebound = false;
  for (int tick = 0; tick < 8; ++tick) {
    board.step();
    for (auto [bi, oi] : board.contactNotes()) contacted |= oi == index;
    rebound |= !board.balls.empty() && board.balls[0].vel.y < 0;
  }
  check(contacted == (role != "decoration"), label + " actual contact");
  check(rebound == (role != "decoration"), label + " actual rebound");
  check((board.pegsHitShot > 0) == (role == "peg"), label + " scoring only for peg");
  board.balls.clear();
  for (int tick = 0; tick < 1500; ++tick) board.step();
  check(board.objs[index].gone == (role == "peg"), label + " clearing only for peg");
}

static void boardGeometry() {
  const auto base = Editor::syntheticBase();
  const auto originalBase = writeLevel(*base);
  for (int width : {650, 917, 1400}) {
    Editor e;
    e.newLevel("Geometry");
    e.meta.boardWidth = width;
    e.base = base;
    e.snap = false;
    const float shift = e.wideShift();
    e.tool = Tool::PegOrange;
    e.mouseDown({40 - shift, 200}, 1, {});
    e.mouseUp({40 - shift, 200}, 1, {});
    e.mouseDown({width - 40 - shift, 200}, 1, {});
    e.mouseUp({width - 40 - shift, 200}, 1, {});
    check(e.pegCount() == 2 && e.hitObject({width - 40 - shift, 200}) != nullptr, "place/select throughout field width=" + std::to_string(width));
    for (const Issue& issue : e.validate()) check(issue.sev != Issue::Error, "wide placements validate: " + issue.text);
    const auto originalLevel = writeLevel(*e.level);
    Board b;
    LevelBundle bundle;
    std::string err;
    check(e.buildBoard(b, bundle, nullptr, 4357, false, &err), "test-play board builds width=" + std::to_string(width));
    check(b.W == width && b.pivot().x == width * 0.5f, "board and launcher width=" + std::to_string(width));
    if (width == 917) check(width + 150 == 1067, "16:9 full canvas is 1067x600, not a 16:9 field");
    const int bucket = b.bucketIndex();
    check(bucket >= 0, "bucket built");
    if (bucket >= 0) {
      const Obj& o = b.objs[bucket];
      check(o.hw == 65 && o.hh == 10 && o.mv.ax == 326 + shift && o.mv.rx == 260 + shift, "bucket keeps size and widens travel");
      Board catchBoard = b;
      catchBoard.emit = false;
      catchBoard.fire(4.712389f);
      Ball ball;
      phys::ballSetPos(ball, o.pos.x, o.pos.y);
      ball.vel = {0, -catchBoard.physics.gravityY};
      catchBoard.balls[0] = ball;
      catchBoard.step();
      bool caught = false;
      for (auto [bi, oi] : catchBoard.contactNotes()) caught |= oi == bucket;
      check(caught, "bucket actual contact width=" + std::to_string(width));
    }
    for (int side : {-1, 1}) {
      int wall = -1;
      for (int i = 0; i < (int)b.objs.size(); ++i) {
        const Obj& o = b.objs[i];
        if (o.id == "wall" && (side < 0 ? o.pos.x < 0 : o.pos.x > width)) wall = i;
      }
      check(wall >= 0, "side wall exists");
      if (wall < 0) continue;
      check(side < 0 ? b.objs[wall].x1 == -0.5f : b.objs[wall].x0 == width + 0.5f, "side wall bounds follow width");
      Board contact = b;
      contact.emit = false;
      contact.fire(4.712389f);
      Ball ball;
      phys::ballSetPos(ball, side < 0 ? 18.0f : width - 18.0f, 350);
      ball.vel = {side * 15.0f, -contact.physics.gravityY};
      contact.balls[0] = ball;
      bool hit = false;
      for (int tick = 0; tick < 3; ++tick) {
        contact.step();
        for (auto [bi, oi] : contact.contactNotes()) hit |= oi == wall;
      }
      check(hit && !contact.balls.empty() && contact.balls[0].vel.x * side < 0, "side wall actual rebound width=" + std::to_string(width));
    }
    check(writeLevel(*e.level) == originalLevel && writeLevel(*base) == originalBase, "building wide board never changes authored level or base assets");
  }
}

static void roleRoundTrips(const fs::path& root, int side) {
  const fs::path mod = root / (side > 0 ? "WideRolesRight" : side < 0 ? "WideRolesLeft" : "Roles");
  fs::create_directories(mod / "levels");
  const auto base = Editor::syntheticBase();
  const auto bytes = writeLevel(*base);
  std::ofstream baseFile(mod / "levels/_base.dat", std::ios::binary);
  baseFile.write((const char*)bytes.data(), (std::streamsize)bytes.size());
  baseFile.close();
  for (int shape : {5, 6, 3, 2}) for (const std::string role : {"peg", "bumper", "terrain", "decoration"}) {
    if (role == "peg" && shape != 5 && shape != 6) continue;
    Editor e;
    const std::string label = std::to_string(shape) + "_" + role + (side > 0 ? "_wide_right" : side < 0 ? "_wide_left" : "_classic");
    e.newLevel("Role" + label);
    e.meta.boardWidth = side ? 917 : 650;
    e.base = base;
    const float x = side > 0 ? 720.0f : side < 0 ? -70.0f : 300.0f;
    NodeP target;
    if (shape == 5) target = newCircle(x, 250, 10, 1);
    if (shape == 6) target = newBrick(x, 250, e.brickStraight);
    if (shape == 3) target = newPoly(x, 250, {{-20, -10}, {-20, 10}, {20, 10}, {20, -10}});
    if (shape == 2) target = newLine(x - 20, 250, x + 20, 250, true);
    e.addNodes({newCircle(100, 100, 10, 2, false), target}, "Fixture");
    e.select({target});
    check(e.setCollisionRole(role) && e.collisionRole(target) == role, label + " role assignment");
    boardContacts(e, target, role, label);
    std::string err;
    check(e.saveJson(root / "Role.json", &err), label + " JSON save");
    Editor json;
    json.base = base;
    check(json.loadJson(root / "Role.json", &err), label + " JSON load");
    check(json.meta.boardWidth == e.meta.boardWidth && json.collisionRole(json.objects().back()) == role, label + " JSON width/role roundtrip");
    boardContacts(json, json.objects().back(), role, label + " JSON");
    check(e.saveDat(root / "Role.dat", &err), label + " DAT save");
    Editor dat;
    dat.base = base;
    check(dat.loadDatFile(root / "Role.dat", &err), label + " DAT load");
    check(dat.meta.boardWidth == e.meta.boardWidth && dat.collisionRole(dat.objects().back()) == role, label + " DAT width/role roundtrip");
    boardContacts(dat, dat.objects().back(), role, label + " DAT");
    check(e.saveToMod(mod, &err), label + " mod save");
    Vfs vfs;
    check(vfs.mountMod(mod, &err), label + " mod mount");
    Editor mounted;
    check(mounted.loadFromVfs(vfs, e.meta.name, &err), label + " mod load");
    check(mounted.meta.boardWidth == e.meta.boardWidth && mounted.collisionRole(mounted.objects().back()) == role, label + " mod width/role roundtrip");
    boardContacts(mounted, mounted.objects().back(), role, label + " mod");
  }
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  fs::path root = fs::path(argv[1]) / "editor_geometry_test";
  fs::create_directories(root);
  datWidth(root);
  backgroundFiles(root);
  datSavePreservation(root);
  datCommitFailure(root);
  datReadFailure(root);
  modIndexFailure(root);
  modRollbackRecovery(root);
  modMetadataFailure(root);
  datIniAlias(root);
  modSaveFailure(root);
  roles<Editor>();
  widthUndo();
  roleValidation();
  boardGeometry();
  roleRoundTrips(root, 0);
  if (argc < 3 || std::string(argv[2]) != "--editor-only") {
    roleRoundTrips(root, 1);
    roleRoundTrips(root, -1);
  }
  std::cout << "EDITOR_GEOMETRY checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
