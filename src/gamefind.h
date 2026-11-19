#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "gamepaths.h"

namespace pp {

constexpr int kSteamAppDeluxe = 3480;
constexpr int kSteamAppNights = 3540;
constexpr int kSteamAppExtreme = 3483;

inline Game otherGame(Game g) { return g == Game::Nights ? Game::Deluxe : Game::Nights; }
inline int steamAppId(Game g) {
  switch (g) {
    case Game::Deluxe: return kSteamAppDeluxe;
    case Game::Nights: return kSteamAppNights;
    case Game::Extreme: return kSteamAppExtreme;
    default: return 0;
  }
}

struct FindPlace {
  std::filesystem::path path;
  std::string from;
  bool guessed = false;
};

struct FindSources {
  std::filesystem::path remembered[kGameCount];
  std::vector<FindPlace> steamRoots;
  std::vector<FindPlace> installs[kGameCount];
  std::vector<FindPlace> steamLibraries;
  std::vector<FindPlace> publisherDirs;
  std::vector<FindPlace> gamesDirs;
  std::vector<std::string> notes;
  bool skip[kGameCount] = {};
  bool wholeComputer = true;
};

struct FindLook {
  enum class Kind { Remembered, Steam, Library, Game, Install, Other };
  Kind kind = Kind::Other;
  int game = -1;
  std::filesystem::path path;
  std::string from;
  std::string seen;
  bool guessed = false;
  bool there = false;
  bool found = false;
};

struct FoundGames {
  std::filesystem::path dir[kGameCount];
  std::string from[kGameCount];
  std::vector<std::string> notes;
  std::vector<FindLook> looks;
  bool has(Game g) const { return !dir[gameIndex(g)].empty(); }
  bool any() const { for (const auto& p : dir) if (!p.empty()) return true; return false; }
};

enum class PathKind { Missing, Folder, File, Unreadable };

PathKind pathKind(const std::filesystem::path& p, std::string* why = nullptr);

struct VdfNode {
  std::string key;
  std::string value;
  bool block = false;
  std::vector<VdfNode> kids;
  const VdfNode* child(const std::string& name) const;
};

bool parseVdf(const std::string& text, VdfNode& root);
std::vector<FindPlace> steamLibraryPaths(const std::filesystem::path& steamRoot);
std::string steamInstallDir(const std::filesystem::path& manifest);

FindSources systemFindSources(const std::filesystem::path& programDir);
FoundGames findGames(const FindSources& sources);
std::vector<std::string> searchReport(const FoundGames& found);
std::vector<std::string> searchSummary(const FoundGames& found, size_t maxLines = 14);

struct GameFolderStore {
  std::filesystem::path dir[kGameCount];
  Game home = Game::Deluxe;
  bool load(const std::filesystem::path& file);
  bool save(const std::filesystem::path& file) const;
};

#ifdef _WIN32
std::vector<FindPlace> registrySteamRoots(std::vector<std::string>* notes = nullptr);
std::vector<FindPlace> registryInstallDirs(Game g, std::vector<std::string>* notes = nullptr);
std::vector<std::filesystem::path> fixedDriveRoots();
#endif

}
