#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

#include "profile.h"
#include "vfs.h"

namespace pp::ui {

struct LevelInfo {
  std::string file, title;
  int ace = 0;
  int extra = 0;
  int stage = 0, index = 0;
  std::string dialog, dialogTitle;
  int dialogChar = -1;
  bool excludeRand = false;
  int lastUse = -1000;
};

struct StoryData {
  std::string text;
  int arg = -1;
};

struct DecalInfo {
  std::string image;
  int layer = 0;
  float emit = 1;
  int x = 0, y = 0, hx = 0, hy = 0, hw = 0, hh = 0;
  std::string hover;
};

struct StageInfo {
  std::string name, desc;
  std::vector<LevelInfo> levels;
  std::vector<StoryData> story[5];
  std::vector<DecalInfo> decals;
  bool pack = false;
  std::string packLabel;
  const LevelInfo* level(int i) const { return i >= 0 && i < (int)levels.size() ? &levels[i] : nullptr; }
};

class StageMgr {
 public:
  bool load(const Vfs& vfs);
  int numStages() const { return mainStages_; }
  int numAllStages() const { return (int)stages_.size(); }
  const StageInfo* stage(int i) const { return i >= 0 && i < (int)stages_.size() ? &stages_[i] : nullptr; }
  StageInfo* stage(int i) { return i >= 0 && i < (int)stages_.size() ? &stages_[i] : nullptr; }
  const LevelInfo* levelByName(const std::string& name) const;
  size_t numLevels() const { return mainLevels_; }
  size_t numAllLevels() const { return byName_.size(); }
  const std::string& tip(unsigned i) const;
  size_t numTips() const { return tips_.size(); }
  const std::vector<std::string>& credits() const { return credits_; }
  bool randomLevel(const PlayerInfo& p, int& stage, int& level, bool duel, uint32_t rnd);
  void markUsed(int stage, int level) { if (auto* s = stage_(stage)) if (level >= 0 && level < (int)s->levels.size()) s->levels[level].lastUse = ++useCounter_; }
  std::string cfgError;

 private:
  StageInfo* stage_(int i) { return stage(i); }
  void parseCfg(const std::string& text, bool pack, const std::string& label);
  std::vector<StageInfo> stages_;
  std::multimap<std::string, std::pair<int, int>> byName_;
  std::vector<std::string> tips_;
  std::vector<std::string> credits_;
  std::vector<std::pair<std::string, bool>> pendingRand_;
  int mainStages_ = 0, useCounter_ = 0;
  size_t mainLevels_ = 0;
};

struct TrophyInfo {
  int id = 0;
  std::string name, desc, smallDesc, inGameDesc;
  int balls = 0, numOrange = 0, lives = 0, scoreReq = 0, scoreCeil = 0;
  bool scoreReset = true, clearLevel = false;
  std::vector<std::string> levels;
  std::vector<std::string> opponents;
  std::vector<int> opponentDifficulty;
  std::map<std::string, std::string> raw;
  int page = 0, slot = 0;
};
struct TrophyPage {
  std::string name, desc, smallDesc;
  std::vector<TrophyInfo> trophies;
};

class TrophyMgr {
 public:
  bool load(const Vfs& vfs);
  const std::vector<TrophyPage>& pages() const { return pages_; }
  size_t numTrophies() const { return byId_.size(); }
  const TrophyInfo* byId(int id) const { auto it = byId_.find(id); return it == byId_.end() ? nullptr : it->second; }
  std::vector<const TrophyInfo*> forClearedLevel(const std::string& level) const;
  std::string cfgError;

 private:
  std::vector<TrophyPage> pages_;
  std::map<int, const TrophyInfo*> byId_;
};

}
