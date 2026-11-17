#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "profile.h"

namespace pp {

inline const char* gameTitle(Game g) { return g == Game::Nights ? "Peggle Nights" : "Peggle Deluxe"; }

std::string pathToUtf8(const std::filesystem::path& p);
std::filesystem::path pathFromUtf8(const std::string& s);

bool hasMainPak(const std::filesystem::path& dir);
std::filesystem::path resolveGameDir(const std::filesystem::path& given);
Game classifyGameDir(const std::filesystem::path& dir);
std::vector<std::string> gameFolderNames(Game g);
std::filesystem::path findGameIn(const std::filesystem::path& parent, Game g);

}
