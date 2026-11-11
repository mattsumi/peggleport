#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "profile.h"

namespace pp {

constexpr int kSteamAppDeluxe = 3480;
constexpr int kSteamAppNights = 3540;

inline int gameIndex(Game g) { return g == Game::Nights ? 1 : 0; }
inline Game gameAt(int i) { return i == 1 ? Game::Nights : Game::Deluxe; }
inline Game otherGame(Game g) { return g == Game::Nights ? Game::Deluxe : Game::Nights; }
inline int steamAppId(Game g) { return g == Game::Nights ? kSteamAppNights : kSteamAppDeluxe; }

struct FindPlace {
  std::filesystem::path path;
  std::string from;
  bool guessed = false;
};

struct FindSources {
  std::filesystem::path remembered[2];
  std::vector<FindPlace> steamRoots;
  std::vector<FindPlace> installs[2];
  std::vector<FindPlace> steamLibraries;
  std::vector<FindPlace> publisherDirs;
  std::vector<FindPlace> gamesDirs;
  std::vector<std::string> notes;
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
  std::filesystem::path dir[2];
  std::string from[2];
  std::vector<std::string> notes;
  std::vector<FindLook> looks;
  bool has(Game g) const { return !dir[gameIndex(g)].empty(); }
  bool any() const { return !dir[0].empty() || !dir[1].empty(); }
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
  std::filesystem::path dir[2];
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
