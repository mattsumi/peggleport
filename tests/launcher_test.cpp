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
  const auto copy = gameCopyFolder(store.parent_path(), Game::Deluxe);
  put(source / "main.pak", pak(campaign("Level1")));
  put(source / "levelpacks" / "addon.pak", pak(campaign("Bjorn1")));
  put(source / "music" / "old.ogg", "old synthetic music");
  Choice choice{source};
  ui::GameFolders folders(store, FindSources{}, choice.dialogs());
  require(folders.askForGame(Game::Deluxe) && folders.dir(Game::Deluxe) == copy, "seed Deluxe with add-on");
  const auto ini = bytes(store), manifest = bytes(copy / "manifest.txt"), original = bytes(copy / "main.pak");
  fs::remove_all(source);
  put(candidate / "main.pak", candidatePak);
  put(candidate / "properties" / "last.xml", "new synthetic properties");
  choice.path = candidate;
  if (lock == "saved-selection") fs::create_directory(store.parent_path() / "launcher.ini.tmp");
#ifdef _WIN32
  HANDLE held = INVALID_HANDLE_VALUE;
  if (lock == "managed-directory") {
    held = CreateFileW(copy.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
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
    require(folders.askForGame(Game::Deluxe) && folders.dir(Game::Deluxe) == copy && folders.installDir(Game::Deluxe) == candidate, "retry failed");
    require(checkGameCopy(copy).state == GameCopyCheck::State::Intact && bytes(copy / "main.pak") == candidatePak &&
            bytes(copy / "properties" / "last.xml") == "new synthetic properties" && !fs::exists(copy / "music" / "old.ogg"), "incomplete replacement");
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
