#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "profile.h"

namespace pp {

struct GameCopyCheck {
  enum class State { Missing, Damaged, Intact };
  State state = State::Missing;
  std::string why;
  std::string source;
  size_t files = 0;
  uint64_t bytes = 0;
};

struct GameCopyResult {
  bool ok = false;
  std::string why;
  size_t files = 0;
  uint64_t bytes = 0;
};

std::filesystem::path gameCopyFolder(const std::filesystem::path& dataDir, Game g);
GameCopyCheck checkGameCopy(const std::filesystem::path& copyDir);
GameCopyResult collectGameFiles(const std::filesystem::path& installDir, const std::filesystem::path& copyDir);
void discardGameCopy(const std::filesystem::path& copyDir);
bool sameFolder(const std::filesystem::path& a, const std::filesystem::path& b);

}
