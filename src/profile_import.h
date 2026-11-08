#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "profile.h"

namespace pp {

struct ImportedProfile {
  Game game = Game::Deluxe;
  int fileVersion = 0;
  std::string name;
  bool nameKnown = false;
  uint32_t originalId = 0;
  std::string fileName;
  PlayerInfo info;
  bool readable = false;
  size_t trailing = 0;
  std::string problem;
};

struct ImportScan {
  std::filesystem::path folder;
  bool gameKnown = false;
  Game game = Game::Deluxe;
  std::vector<ImportedProfile> profiles;
  std::map<std::string, std::vector<HighScoreEntry>> scores;
  bool haveScores = false;
  std::vector<std::string> notes;
  std::filesystem::path replayFolder;
  int replayCount = 0;
};

struct ImportOutcome {
  bool ok = false;
  std::string name;
  std::string message;
  int scoresAdded = 0;
  Game game = Game::Deluxe;
};

bool parseOriginalProfile(const std::vector<uint8_t>& data, ImportedProfile& out);
bool parseOriginalUsers(const std::vector<uint8_t>& data, Game& game, std::vector<std::pair<std::string, uint32_t>>& list, std::string& why);
bool parseOriginalHighScores(const std::vector<uint8_t>& data, Game& game, std::map<std::string, std::vector<HighScoreEntry>>& out, std::string& why);

ImportScan scanOriginalSaves(const std::filesystem::path& chosen);

std::vector<std::filesystem::path> originalSaveFolders(Game g);

std::string uniqueProfileName(const ProfileMgr& pm, const std::string& wanted);

ImportOutcome importOriginalProfile(ProfileMgr& pm, HighScoreMgr* scores, const ImportedProfile& src, const ImportScan& scan, const std::string& asName);

std::vector<std::string> describeImported(const ImportedProfile& p);

}
