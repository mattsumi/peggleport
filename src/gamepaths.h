#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "profile.h"

namespace pp {

inline constexpr int kGameCount = 3;
inline constexpr int gameIndex(Game g) { return static_cast<int>(g); }
inline constexpr Game gameAt(int i) { return static_cast<Game>(i); }
inline constexpr bool combinedEdition(Game g) { return g == Game::Deluxe || g == Game::Nights; }
inline const char* gameTitle(Game g) {
  switch (g) {
    case Game::Nights: return "Peggle Nights";
    case Game::Extreme: return "Peggle Extreme";
    default: return "Peggle Deluxe";
  }
}
inline const char* gameKey(Game g) {
  switch (g) {
    case Game::Nights: return "Nights";
    case Game::Extreme: return "Extreme";
    default: return "Deluxe";
  }
}

std::string pathToUtf8(const std::filesystem::path& p);
std::filesystem::path pathFromUtf8(const std::string& s);

bool hasMainPak(const std::filesystem::path& dir);
std::filesystem::path resolveGameDir(const std::filesystem::path& given);
std::optional<Game> classifyGameDir(const std::filesystem::path& dir, bool* wowDonor = nullptr);
std::vector<std::string> gameFolderNames(Game g);
std::filesystem::path findGameIn(const std::filesystem::path& parent, Game g);

}
