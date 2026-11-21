#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <functional>
#include <utility>

#include "visopts.h"

namespace pp {

enum class Game { Deluxe, Nights, Extreme };
enum class GameMode { None = 0, Adventure = 1, QuickPlay = 2, Duel = 3, Challenge = 4, Replay = 5, Demo = 6 };

class DataWriter {
 public:
  void u32(uint32_t v);
  void i32(int32_t v) { u32((uint32_t)v); }
  void u16(uint16_t v);
  void u8(uint8_t v);
  void str(const std::string& s);
  void bit(bool v);
  void endBit() { bitPos_ = 0; }
  const std::vector<uint8_t>& bytes() const { return buf_; }

 private:
  std::vector<uint8_t> buf_;
  int bitPos_ = 0;
};
class DataReader {
 public:
  DataReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  explicit DataReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}
  uint32_t u32();
  int32_t i32() { return (int32_t)u32(); }
  uint16_t u16();
  uint8_t u8();
  std::string str();
  bool bit();
  void endBit() { bitPos_ = 0; }
  bool ok() const { return !fail_; }
  size_t pos() const { return pos_; }
  size_t remaining() const { return n_ - pos_; }

 private:
  bool take(size_t k);
  const uint8_t* p_;
  size_t n_, pos_ = 0;
  int bitPos_ = 0;
  uint8_t cur_ = 0;
  bool fail_ = false;
};

struct GameStats {
  uint8_t version = 1;
  int32_t shots = 0, bestShot = 0, bestFever = 0;
  int32_t freeBalls[4]{};
  int32_t styleShots[24]{};
  void reset() { *this = GameStats(); }
  void add(const GameStats& o);
  int totalFreeBalls() const { return freeBalls[0] + freeBalls[1] + freeBalls[2] + freeBalls[3]; }
  int totalStyleShots() const;
  void write(DataWriter& w) const;
  bool read(DataReader& r);
};

struct CaseLess {
  bool operator()(const std::string& a, const std::string& b) const;
};

struct PlayerInfo {
  std::string name;
  uint32_t useSeq = 0, id = 0;
  int maxStage = 0, maxLevel = 0, levelSelectStage = 0, advStage = -1, advLevel = 0, char1 = 0, char2 = 1;
  std::string strB4, strD0;
  int i38 = 0;
  std::set<int> trophies;
  std::multiset<int> advScores;
  std::map<int, int> trophyTop;
  bool fEd = true, computerPlayer = true, fEe = true, colorblind = false, storyPending = true, fEf = false;
  int computerSkill = -1;
  uint8_t b54 = 0, b124 = 0, b128 = 0;
  std::set<std::string> clearedLevels;
  int advCompletions = 0;
  uint8_t b58 = 0;
  int tipsMask = 0, advScore = 0, advLevelAttempts = 0, advRetries = 0;
  GameStats statsA, statsB;
  uint16_t tipCount = 0;
  uint8_t tipCounts[10]{};
  uint16_t tipOther = 0, trophyLevelSeen = 0;
  std::set<std::string> acedLevels;
  int faeries = 0;
  std::set<std::string> mantelSeen;
  bool mantelSeenKnown = false;
  bool dirty = false;

  static constexpr int kWriterVersion = 23, kMinVersion = 19;
  void reset(bool keepLists = false);
  std::vector<uint8_t> serialize(int version = kWriterVersion) const;
  bool deserialize(const std::vector<uint8_t>& data);
  bool isLevelUnlocked(int stage, int level) const;
  int maxLevelIndex() const { return maxStage * 5 + maxLevel; }
  static inline int advStages = 11;
  bool justBeatAdventure() const { return advCompletions > 0 && advStage == advStages && advLevel == 0 && advLevelAttempts == 0; }
  bool justStartingAdventure() const { return advStage <= 0 && advLevel == 0 && advLevelAttempts == 0; }
  void restartAdventure();
  void countLevelStart();
  void uncountLevelStart();
  void countFailedTry();
  int tryNumber(bool nights) const { return nights ? advLevelAttempts + 1 : (advLevelAttempts < 1 ? 1 : advLevelAttempts); }
  int efficiencyBonus() const;
  void awardTrophy(int id) { if (trophies.insert(id).second) dirty = true; }
  void checkTrophyTopScore(int id, int score, bool lowerIsBetter = false);
  int trophyTopScore(int id) const { auto it = trophyTop.find(id); return it == trophyTop.end() ? 0 : it->second; }
  void submitAdventureScore(int score);
  void addTotalStats(const GameStats& s, bool current);
  bool tipSet(int id) const { return (tipsMask & (1 << (id & 31))) != 0; }
  void setTip(int id) { tipsMask |= 1 << (id & 31); dirty = true; }
  void incTip(int id);
};

struct HighScoreEntry {
  std::string name;
  int score = 0;
};

class HighScoreMgr {
 public:
  explicit HighScoreMgr(Game g = Game::Deluxe) : game_(g) {}
  bool load(const std::filesystem::path& file);
  bool save(const std::filesystem::path& file) const;
  const std::vector<HighScoreEntry>& scores(const std::string& key);
  int submit(const std::string& key, const std::string& name, int score);
  bool wouldRank(const std::string& key, int score);
  void setSeed(uint32_t s) { rng_ = s; }
  void setTops(std::function<int(const std::string&)> f) { tops_ = std::move(f); }
  std::vector<uint8_t> serialize() const;
  bool deserialize(const std::vector<uint8_t>& d);
  const std::map<std::string, std::vector<HighScoreEntry>>& all() const { return tables_; }

 private:
  Game game_;
  std::map<std::string, std::vector<HighScoreEntry>> tables_;
  std::function<int(const std::string&)> tops_;
  uint32_t rng_ = 0x1234567;
};

class ProfileMgr {
 public:
  explicit ProfileMgr(Game g = Game::Deluxe) : game_(g) {}
  void setDir(const std::filesystem::path& userdata) { dir_ = userdata; }
  const std::filesystem::path& dir() const { return dir_; }
  bool load();
  bool save();
  PlayerInfo* add(const std::string& name);
  bool remove(const std::string& name);
  bool rename(const std::string& from, const std::string& to);
  PlayerInfo* get(const std::string& name);
  PlayerInfo* any();
  std::vector<std::string> names() const;
  size_t count() const { return profiles_.size(); }
  bool saveDetails(PlayerInfo& p);
  void saveIfDirty(PlayerInfo* p) { if (p && p->dirty) saveDetails(*p); }
  std::filesystem::path detailsPath(const PlayerInfo& p) const;
  std::filesystem::path savedGamePath(const PlayerInfo& p, GameMode m) const;
  std::vector<uint8_t> serializeUsers() const;
  bool deserializeUsers(const std::vector<uint8_t>& d);
  static bool validName(const std::string& n);
  std::string modMaster(const PlayerInfo& p, int slot) const;
  void setModMaster(const PlayerInfo& p, int slot, const std::string& key);
  bool saveModMasters();
  std::filesystem::path modMastersPath() const;

 private:
  PlayerInfo* find(const std::string& name);
  void loadDetails(PlayerInfo& p);
  Game game_;
  std::filesystem::path dir_;
  std::vector<std::unique_ptr<PlayerInfo>> profiles_;
  uint32_t nextId_ = 1, nextSeq_ = 1;
  std::map<uint32_t, std::array<std::string, 2>> modChars_;
  bool modDirty_ = false;
  void loadModMasters();
};

struct Settings {
  VisualOptions vis;
  float music = 0.85f, sfx = 0.85f, fever = 0.85f;
  bool muted = false, fullscreen = false, customCursors = true, vsync = true;
  std::string curUser;
  int maxStage = 0, maxLevel = 0;
  int winW = 0, winH = 0;
  int widescreen = 0;
  bool ps3ResourcePack = false;
  bool load(const std::filesystem::path& file);
  bool save(const std::filesystem::path& file) const;
};

std::filesystem::path defaultUserDir(Game g);

}
