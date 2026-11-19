#include "content.h"
#include "gamecopy.h"
#include "level.h"
#include "ui/start.h"
#include "vfs.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using namespace pp;
using Files = std::vector<std::pair<std::string, std::string>>;

void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}

void put(const fs::path& file, const std::string& data) {
  fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out.write(data.data(), data.size());
  out.close();
  require(!out.fail(), "writing " + pathToUtf8(file));
}

std::string bytes(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  require(bool(in), "reading " + pathToUtf8(file));
  return {std::istreambuf_iterator<char>(in), {}};
}

std::string pak(const Files& files) {
  std::string out;
  auto word = [&](uint32_t n) {
    for (int i = 0; i < 4; ++i) out += char(n >> (8 * i));
  };
  word(0xbac04ac0);
  word(0);
  for (const auto& [name, data] : files) {
    out += '\0';
    out += char(name.size());
    out += name;
    word(uint32_t(data.size()));
    word(0);
    word(0);
  }
  out += char(0x80);
  for (const auto& [name, data] : files) out += data;
  for (char& c : out) c ^= char(0xf7);
  return out;
}

Files campaign(const std::string& board) {
  Level lv;
  lv.version = 0x52;
  auto dat = writeLevel(lv);
  std::string geometry(dat.begin(), dat.end());
  return {{"Levels/" + board + ".dat", geometry}, {"levels\\_base.dat", geometry},
          {"levels/stages.cfg", "Stage\n{\nLevel: " + board + ", Fixture\n}\n"}};
}

struct Choice {
  fs::path path;
  std::string warning;
  ui::StartDialogs dialogs() {
    return {[this](SDL_Window*, const std::string&, const std::string& text, const auto&, bool problem) {
              if (problem) warning = text;
              return problem ? -1 : 0;
            },
            [this](SDL_Window*, const std::string&, const fs::path&) { return ui::FolderAnswer{ui::FolderAnswer::Status::Chosen, path}; }};
  }
};

void replacement(const fs::path& root, const std::string& candidatePak, bool any, const std::string& message, const std::string& lock = {}) {
  const auto source = root / "original", candidate = root / "Peggle Deluxe", store = root / "profile" / "launcher.ini";
  put(source / "main.pak", pak(campaign("Level1")));
  put(source / "levelpacks" / "addon.pak", pak(campaign("Bjorn1")));
  put(source / "music" / "old.ogg", "old synthetic music");
  Choice choice{source};
  ui::GameFolders folders(store, FindSources{}, choice.dialogs());
  require(folders.askForGame(Game::Deluxe), "seed Deluxe with add-on");
  const auto copy = folders.dir(Game::Deluxe);
  require(copy != source, "seed has no managed copy");
  const auto ini = bytes(store), manifest = bytes(copy / "manifest.txt"), original = bytes(copy / "main.pak");
  fs::remove_all(source);
  put(candidate / "main.pak", candidatePak);
  put(candidate / "properties" / "last.xml", "new synthetic properties");
  choice.path = candidate;
  if (lock == "saved-selection") fs::create_directory(store.parent_path() / "launcher.ini.tmp");
#ifdef _WIN32
  HANDLE held = INVALID_HANDLE_VALUE;
  if (lock == "managed-directory") {
    GameFolderStore selected;
    require(selected.load(store), "load committed slot");
    const auto target = gameCopyFolder(store.parent_path(), Game::Deluxe, !selected.alternate[gameIndex(Game::Deluxe)]);
    fs::create_directories(target);
    held = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    require(held != INVALID_HANDLE_VALUE, "cannot hold managed copy");
  } else if (!lock.empty() && lock != "saved-selection") {
    held = CreateFileW((candidate / lock).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(held != INVALID_HANDLE_VALUE, "cannot lock fixture");
  }
#endif
  const bool accepted = any ? folders.askForAnyGame() : folders.askForGame(Game::Deluxe);
#ifdef _WIN32
  if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
#endif
  require(!accepted, "candidate accepted; saved path " + std::string(bytes(store) == ini ? "unchanged" : "changed") +
                     ", managed manifest " + (fs::exists(copy / "manifest.txt") && bytes(copy / "manifest.txt") == manifest ? "unchanged" : "changed/missing"));
  require(choice.warning.find(message) != std::string::npos, "wrong rejection: " + choice.warning);
  require(folders.dir(Game::Deluxe) == copy && folders.installDir(Game::Deluxe) == source, "active selection changed");
  require(bytes(store) == ini && bytes(copy / "manifest.txt") == manifest && bytes(copy / "main.pak") == original &&
          bytes(copy / "music" / "old.ogg") == "old synthetic music" && !fs::exists(copy / "properties" / "last.xml"), "saved selection or old bytes changed");
  ui::GameFolders restart(store, FindSources{}, {});
  restart.detect();
  require(restart.dir(Game::Deluxe) == copy && restart.installDir(Game::Deluxe) == source && !restart.has(Game::Nights), "copied-only restart failed");
  if (lock == "properties/last.xml") {
    require(folders.askForGame(Game::Deluxe) && folders.installDir(Game::Deluxe) == candidate, "retry failed");
    const auto updated = folders.dir(Game::Deluxe);
    require(updated != candidate, "retry has no managed copy");
    require(checkGameCopy(updated).state == GameCopyCheck::State::Intact && bytes(updated / "main.pak") == candidatePak &&
            bytes(updated / "properties" / "last.xml") == "new synthetic properties" && !fs::exists(updated / "music" / "old.ogg"), "incomplete replacement");
    GameFolderStore saved;
    require(saved.load(store) && saved.dir[gameIndex(Game::Deluxe)] == candidate, "new source not saved");
  }
}

}

int launcherTest(const fs::path& scratch) {
  if (!scratch.is_absolute() || !fs::create_directory(scratch)) return 2;
  int failed = 0;
  auto test = [&](const char* name, auto body) {
    try {
      body(scratch / name);
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception& e) {
      std::cerr << "FAIL " << name << ": " << e.what() << '\n';
      ++failed;
    }
  };
  auto reject = [&](const char* name, const std::string& data, bool any, const char* message, const std::string& lock = {}) {
    test(name, [&](const auto& root) { replacement(root, data, any, message, lock); });
  };
  reject("wrong-edition", pak(campaign("Bjorn1")), false, "Nights");
  reject("known-donor", pak(campaign("hogger")), true, "level pack");
  reject("unknown-pak", pak(campaign("unrelated")), true, "no supported");
  reject("truncated-pak", pak(campaign("level1")).substr(0, 8), false, "no Peggle Deluxe");
  auto mixed = campaign("level1");
  mixed.emplace_back("levels/BjornNHeavy.dat", mixed[0].second);
  reject("mixed", pak(mixed), true, "no supported");
  std::reverse(mixed.begin(), mixed.end());
  reject("mixed-reversed", pak(mixed), true, "no supported");
  auto files = campaign("level1");
  files.back().second = "Stage\n{\n}\n";
  reject("no-stage-level", pak(files), true, "no supported");
  files = campaign("level1");
  files[0].second = "bad";
  reject("bad-board", pak(files), true, "no supported");
  files = campaign("level1");
  files.back().second = "Stage\n{\nLevel: level1\nLevel: extra\n}\n";
  files.emplace_back("levels/extra.dat", "bad");
  reject("bad-listed-board", pak(files), true, "no supported");
  files = campaign("level1");
  files.emplace_back("extra.txt", "new copy");
  reject("selection-write-failure", pak(files), false, "folders", "saved-selection");
#ifdef _WIN32
  reject("late-publish-failure", pak(files), false, "copy", "managed-directory");
  files = campaign("level1");
  files.emplace_back("extra.txt", "different bytes");
  reject("unreadable-pak", pak(files), true, "no supported", "main.pak");
  reject("late-read-failure", pak(files), false, "copy", "properties/last.xml");
#endif
  for (bool wrongHash : {false, true}) {
    auto linked = campaign("level1");
    linked.back().second = "Stage\n{\nLevel: level1\nlevel: extra\n}\n";
    linked.emplace_back("levels/extra.dat", linked.front().second);
    linked.emplace_back("levels/extra.ini", "base=_alt\nbase_hash=0\n");
    if (wrongHash) linked.emplace_back("levels/_alt.dat", linked.front().second);
    reject(wrongHash ? "linked-hash" : "linked-missing", pak(linked), false, wrongHash ? "differs" : "missing");
  }
  test("occupied-slot", [&](const auto& root) {
    const auto store = root / "profile" / "launcher.ini";
    Choice choice{root / "original"};
    put(choice.path / "main.pak", pak(campaign("Level1")));
    ui::GameFolders folders(store, FindSources{}, choice.dialogs());
    require(folders.askForGame(Game::Deluxe), "seed previous copy");
    const auto ini = bytes(store);
    const auto copy = folders.dir(Game::Deluxe);
    GameFolderStore saved;
    require(saved.load(store), "read selected slot");
    const auto target = gameCopyFolder(store.parent_path(), Game::Deluxe, !saved.alternate[gameIndex(Game::Deluxe)]);
    put(target / "manifest.txt", "not a managed copy");
    choice.path = root / "replacement";
    put(choice.path / "main.pak", pak(campaign("Level1")));
    require(!folders.askForGame(Game::Deluxe) && bytes(store) == ini && folders.dir(Game::Deluxe) == copy && bytes(target / "manifest.txt") == "not a managed copy", "unowned slot overwritten");
  });
  test("linked-present", [&](const auto& root) {
    auto files = campaign("Level1");
    const auto data = files.front().second;
    const auto hash = std::to_string(fnv1a(data.data(), data.size()));
    files.emplace_back("levels/Level1.ini", "base=_alt\nbase_hash=" + hash + "\n");
    Choice choice{root / "install"};
    put(choice.path / "main.pak", pak(files));
    require(classifyGameDir(choice.path) == Game::Deluxe, "recognition depended on external base");
    put(choice.path / "levelpacks" / "linked.pak", pak({{"levels/_alt.dat", data}}));
    ui::GameFolders folders(root / "profile" / "launcher.ini", FindSources{}, choice.dialogs());
    require(folders.askForGame(Game::Deluxe), choice.warning);
    Vfs vfs;
    LevelBundle board;
    std::string error;
    require(vfs.mountGame(folders.dir(Game::Deluxe), &error) && loadLevelBundle(vfs, "Level1", board, &error) && board.baseLink.hash == hash, "linked board from staged pack: " + error);
    require(board.baseLink.provider == "levelpacks/linked.pak", "wrong base provider: " + board.baseLink.provider);
  });
  for (bool named : {false, true}) {
    test(named ? "linked-named-missing" : "linked-pack-hash", [&](const auto& root) {
      auto files = campaign("Level1");
      const auto data = files.front().second;
      const auto hash = std::to_string(fnv1a(data.data(), data.size()));
      files.emplace_back("levels/Level1.ini", "base=_alt\nbase_hash=" + hash + (named ? "\nbase_provider=main.pak" : ""));
      const auto packs = root / "Peggle Deluxe" / "levelpacks";
      put(packs / "a.pak", pak({{"levels/_alt.dat", named ? data : "wrong bytes"}}));
      put(packs / "b.pak", pak({{"levels/_alt.dat", data}}));
      replacement(root, pak(files), false, named ? "in main.pak" : "differs");
    });
  }
  test("linked-owners", [&](const auto& root) {
    auto files = campaign("Level1");
    const auto data = files.front().second;
    const auto hash = std::to_string(fnv1a(data.data(), data.size()));
    const std::string pack = "levelpacks/donor.pak";
    files.emplace_back("levels/_private.dat", "not the donor");
    files.emplace_back("levels/Level1.ini", "base=_private\nbase_hash=" + hash + "\nbase_provider=" + pack);
    put(root / "game/main.pak", pak(files));
    put(root / "game" / pack, pak({{"levels/_private.dat", data}, {"levels/_shared.dat", data},
                                    {"levels/Packed.dat", data}, {"levels/Packed.ini", "base=_private"}}));
    put(root / "game/levels/Loose.dat", data);
    put(root / "game/levels/Loose.ini", "base=_shared");
    put(root / "mod/levels/_shared.dat", "not edition content");
    Vfs vfs;
    std::string error;
    require(vfs.mountGame(root / "game", &error) && vfs.mountMod(root / "mod", &error), error);
    for (const std::string name : {std::string("Level1"), std::string("Packed"), pathToUtf8(root / "game/levels/Loose.dat")}) {
      LevelBundle board;
      require(loadLevelBundle(vfs, name, board, &error), error);
      require(board.baseLink.provider == pack && board.baseLink.hash == hash, "wrong base owner or bytes: " + name);
    }
  });
  test("supported", [&](const auto& root) {
    for (const auto& [game, board] : {std::pair{Game::Deluxe, "Level1"}, {Game::Nights, "Bjorn1"}, {Game::Extreme, "BjornNHeavy"}}) {
      const auto folder = root / board;
      put(folder / "main.pak", pak(campaign(board)));
      require(classifyGameDir(folder) == game, board);
    }
  });
  for (bool explicitGame : {true, false}) {
    test(explicitGame ? "explicit-extreme" : "automatic-fallback", [&](const auto& root) {
      const auto profile = root / "profile", original = root / "requested";
      const Game broken = explicitGame ? Game::Extreme : Game::Nights;
      Choice choice;
      ui::GameFolders folders(profile / "launcher.ini", FindSources{}, choice.dialogs());
      choice.path = root / "deluxe";
      put(choice.path / "main.pak", pak(campaign("level1")));
      require(folders.askForGame(Game::Deluxe), "seed healthy Deluxe");
      choice.path = original;
      put(original / "main.pak", pak(campaign(explicitGame ? "BjornNHeavy" : "Bjorn1")));
      require(folders.askForGame(broken), "seed requested edition");
      if (!explicitGame) folders.setHome(broken);
      fs::remove_all(original);
      fs::remove(folders.dir(broken) / "main.pak");
      Game selected = explicitGame ? Game::Extreme : folders.home();
      Vfs vfs;
      std::string reason;
      require(!vfs.mountGame(folders.dir(selected), &reason) && !folders.copyFailed(selected), "expected unrecoverable mount failure");
      require(folders.has(Game::Deluxe), "missing healthy alternative");
      const bool retry = folders.recoverLaunch(selected, explicitGame, reason);
      if (explicitGame) require(!retry && selected == Game::Extreme, "explicit launch fell back");
      else require(retry && selected == Game::Deluxe && vfs.mountGame(folders.dir(selected), &reason), "automatic fallback failed");
    });
  }
#ifdef _WIN32
  for (bool explicitGame : {true, false}) {
    test(explicitGame ? "locked-explicit" : "locked-automatic", [&](const auto& root) {
      const auto store = root / "profile" / "launcher.ini", source = root / "requested";
      Choice choice{root / "alternative"};
      ui::GameFolders folders(store, FindSources{}, choice.dialogs());
      put(choice.path / "main.pak", pak(campaign("Level1")));
      require(folders.askForGame(Game::Deluxe), "seed alternative");
      choice.path = source;
      put(source / "main.pak", pak(campaign("Bjorn1")));
      require(folders.askForGame(Game::Nights), "seed requested edition");
      folders.setHome(Game::Nights);
      const auto copy = folders.dir(Game::Nights);
      const auto ini = bytes(store), manifest = bytes(copy / "manifest.txt"), archive = bytes(copy / "main.pak");
      fs::remove_all(source);
      HANDLE held = CreateFileW((copy / "main.pak").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      require(held != INVALID_HANDLE_VALUE, "lock copied-only archive");
      Vfs vfs;
      Game selected = Game::Nights;
      std::string error;
      const bool failed = !vfs.mountGame(copy, &error) && !folders.copyFailed(selected);
      const bool retry = failed && folders.recoverLaunch(selected, explicitGame, error);
      CloseHandle(held);
      require(failed && !folders.has(Game::Nights), "failed copy not excluded");
      require(explicitGame ? !retry && selected == Game::Nights : retry && selected == Game::Deluxe && vfs.mountGame(folders.dir(selected), &error), "wrong recovery edition");
      require(bytes(store) == ini && fs::exists(copy / "manifest.txt") && bytes(copy / "manifest.txt") == manifest, "temporary failure changed saved selection or manifest");
      ui::GameFolders restart(store, FindSources{}, {});
      restart.detect();
      Vfs after;
      require(restart.dir(Game::Nights) == copy && restart.installDir(Game::Nights) == source && restart.home() == Game::Nights &&
              bytes(copy / "main.pak") == archive && after.mountGame(restart.dir(Game::Nights), &error), "unlocked copy did not resume");
    });
  }
  for (bool valid : {true, false}) {
    test(valid ? "retry-readable-source" : "retry-broken-source", [&](const auto& root) {
      const auto store = root / "profile" / "launcher.ini", source = root / "install";
      Choice choice{source};
      put(source / "main.pak", pak(campaign("Level1")));
      ui::GameFolders folders(store, FindSources{}, choice.dialogs());
      require(folders.askForGame(Game::Deluxe), "seed managed copy");
      const auto copy = folders.dir(Game::Deluxe);
      const auto ini = bytes(store), manifest = bytes(copy / "manifest.txt"), original = bytes(copy / "main.pak");
      auto modified = campaign("Level1");
      modified.emplace_back("levels/Level1.ini", valid ? "title=Changed" : "base=_absent");
      put(source / "main.pak", pak(modified));
      HANDLE held = CreateFileW((copy / "main.pak").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      require(held != INVALID_HANDLE_VALUE, "lock archive");
      ui::GameFolders cold(store, FindSources{}, {});
      cold.detect();
      Vfs vfs;
      std::string error;
      const bool failed = !vfs.mountGame(copy, &error);
      const bool retry = folders.copyFailed(Game::Deluxe);
      CloseHandle(held);
      require(failed && retry == valid && cold.has(Game::Deluxe) == valid, "unexpected same-edition retry");
      if (valid) require(cold.dir(Game::Deluxe) == source && folders.dir(Game::Deluxe) == source && vfs.mountGame(source, &error), "retry did not use source directly");
      ui::GameFolders after(store, FindSources{}, {});
      after.detect();
      require(bytes(store) == ini && after.dir(Game::Deluxe) == copy && bytes(copy / "main.pak") == original && bytes(copy / "manifest.txt") == manifest, "retry replaced committed files");
    });
  }
  test("unreadable-selection", [&](const auto& root) {
    const auto store = root / "profile" / "launcher.ini", source = root / "original";
    Choice choice{source};
    put(source / "main.pak", pak(campaign("Level1")));
    ui::GameFolders seed(store, FindSources{}, choice.dialogs());
    require(seed.askForGame(Game::Deluxe), "seed selection");
    const auto copy = seed.dir(Game::Deluxe);
    const auto archive = bytes(copy / "main.pak"), manifest = bytes(copy / "manifest.txt"), ini = bytes(store);
    fs::remove_all(source);
    choice.path = root / "replacement";
    auto replacement = campaign("Level1");
    replacement.emplace_back("other", "different");
    put(choice.path / "main.pak", pak(replacement));
    HANDLE held = CreateFileW(store.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(held != INVALID_HANDLE_VALUE, "lock saved selection");
    ui::GameFolders cold(store, FindSources{}, choice.dialogs());
    cold.detect();
    const bool refused = !cold.askForGame(Game::Deluxe);
    CloseHandle(held);
    ui::GameFolders restart(store, FindSources{}, {});
    restart.detect();
    require(refused && bytes(store) == ini && restart.dir(Game::Deluxe) == copy && bytes(copy / "manifest.txt") == manifest && bytes(copy / "main.pak") == archive, "unreadable selector allowed replacement of committed bytes");
  });
  test("blocked-rollback", [&](const auto& root) {
    const auto store = root / "profile" / "launcher.ini", source = root / "original", candidate = root / "replacement";
    Choice choice{source};
    ui::GameFolders folders(store, FindSources{}, choice.dialogs());
    const auto oldPak = pak(campaign("Level1"));
    auto changed = campaign("Level1");
    changed.emplace_back("version.txt", "replacement");
    const auto newPak = pak(changed);
    put(source / "main.pak", oldPak);
    require(folders.askForGame(Game::Deluxe), "seed committed selection");
    const auto copy = folders.dir(Game::Deluxe);
    const auto ini = bytes(store), manifest = bytes(copy / "manifest.txt");
    fs::remove_all(source);
    put(candidate / "main.pak", newPak);
    const auto block = store.parent_path() / "launcher.ini.tmp";
    fs::create_directory(block);
    HANDLE held = INVALID_HANDLE_VALUE;
    GameFolderStore next;
    require(next.load(store), "load selection");
    next.dir[gameIndex(Game::Deluxe)] = candidate;
    const bool active = next.alternate[gameIndex(Game::Deluxe)];
    next.alternate[gameIndex(Game::Deluxe)] = !active;
    const auto published = gameCopyFolder(store.parent_path(), Game::Deluxe, !active);
    const auto result = collectGameFiles(candidate, store.parent_path(), Game::Deluxe, active, [&] {
      require(bytes(published / "main.pak") == newPak, "callback before publication");
      held = CreateFileW(published.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
      return next.save(store);
    });
    require(held != INVALID_HANDLE_VALUE && !result.ok && bytes(store) == ini && bytes(published / "main.pak") == newPak, "did not block rollback after publication");
    choice.path = candidate;
    ui::GameFolders restart(store, FindSources{}, choice.dialogs());
    restart.detect();
    auto preserved = [&] {
      return restart.has(Game::Deluxe) && restart.installDir(Game::Deluxe) == source && bytes(restart.dir(Game::Deluxe) / "main.pak") == oldPak &&
             bytes(restart.dir(Game::Deluxe) / "manifest.txt") == manifest && bytes(store) == ini;
    };
    const bool oldActive = preserved() && bytes(folders.dir(Game::Deluxe) / "main.pak") == oldPak;
    fs::remove(block);
    const bool rejected = !restart.askForGame(Game::Deluxe);
    const bool stillOld = preserved();
    CloseHandle(held);
    require(oldActive, "restart selected rejected bytes");
    require(rejected && stillOld, "failed retry displaced committed selection");
    require(restart.askForGame(Game::Deluxe), "unblocked replacement failed");
    fs::remove_all(candidate);
    ui::GameFolders after(store, FindSources{}, {});
    after.detect();
    require(after.installDir(Game::Deluxe) == candidate && bytes(after.dir(Game::Deluxe) / "main.pak") == newPak, "retry not committed");
  });
#endif
  test("remembered-invalid", [&](const auto& root) {
    const auto candidate = root / "Peggle Nights";
    put(candidate / "main.pak", pak(campaign("unrelated")));
    const auto profile = root / "profile" / "launcher.ini";
    GameFolderStore store;
    store.dir[gameIndex(Game::Nights)] = candidate;
    require(store.save(profile), "seed saved path");
    ui::GameFolders folders(profile, FindSources{}, {});
    folders.detect();
    require(!folders.has(Game::Nights) && !fs::exists(gameCopyFolder(profile.parent_path(), Game::Nights)), "remembered invalid source was adopted");
    for (const auto& look : folders.lastSearch().looks)
      require(look.seen.find("Warcraft") == std::string::npos, "unknown archive reported as WoW");
  });
  if (!failed) fs::remove_all(scratch);
  return failed ? 1 : 0;
}
