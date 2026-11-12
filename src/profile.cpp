#include "profile.h"

#include "log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace pp {


void DataWriter::u32(uint32_t v) {
  for (int i = 0; i < 4; i++) buf_.push_back((uint8_t)(v >> (8 * i)));
  bitPos_ = 0;
}
void DataWriter::u16(uint16_t v) {
  buf_.push_back((uint8_t)v);
  buf_.push_back((uint8_t)(v >> 8));
  bitPos_ = 0;
}
void DataWriter::u8(uint8_t v) {
  buf_.push_back(v);
  bitPos_ = 0;
}
void DataWriter::str(const std::string& s) {
  u16((uint16_t)std::min<size_t>(s.size(), 0xffff));
  buf_.insert(buf_.end(), s.begin(), s.begin() + std::min<size_t>(s.size(), 0xffff));
  bitPos_ = 0;
}
void DataWriter::bit(bool v) {
  if (bitPos_ == 0) buf_.push_back(0);
  if (v) buf_.back() |= (uint8_t)(1u << bitPos_);
  if (++bitPos_ > 7) bitPos_ = 0;
}

bool DataReader::take(size_t k) {
  bitPos_ = 0;
  if (fail_ || pos_ + k > n_) {
    fail_ = true;
    return false;
  }
  return true;
}
uint32_t DataReader::u32() {
  if (!take(4)) return 0;
  uint32_t v = p_[pos_] | (p_[pos_ + 1] << 8) | (p_[pos_ + 2] << 16) | ((uint32_t)p_[pos_ + 3] << 24);
  pos_ += 4;
  return v;
}
uint16_t DataReader::u16() {
  if (!take(2)) return 0;
  uint16_t v = (uint16_t)(p_[pos_] | (p_[pos_ + 1] << 8));
  pos_ += 2;
  return v;
}
uint8_t DataReader::u8() {
  if (!take(1)) return 0;
  return p_[pos_++];
}
std::string DataReader::str() {
  uint16_t n = u16();
  if (!take(n)) return {};
  std::string s((const char*)p_ + pos_, n);
  pos_ += n;
  return s;
}
bool DataReader::bit() {
  if (bitPos_ == 0) {
    if (fail_ || pos_ + 1 > n_) {
      fail_ = true;
      return false;
    }
    cur_ = p_[pos_++];
  }
  bool v = (cur_ >> bitPos_) & 1;
  if (++bitPos_ >= 8) bitPos_ = 0;
  return v;
}

bool CaseLess::operator()(const std::string& a, const std::string& b) const {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) {
    int x = std::tolower((unsigned char)a[i]), y = std::tolower((unsigned char)b[i]);
    if (x != y) return x < y;
  }
  return a.size() < b.size();
}

static bool ieq(const std::string& a, const std::string& b) {
  return a.size() == b.size() && !CaseLess()(a, b) && !CaseLess()(b, a);
}


void GameStats::add(const GameStats& o) {
  shots += o.shots;
  bestShot = std::max(bestShot, o.bestShot);
  bestFever = std::max(bestFever, o.bestFever);
  for (int i = 0; i < 4; i++) freeBalls[i] += o.freeBalls[i];
  for (int i = 0; i < 24; i++) styleShots[i] += o.styleShots[i];
}
int GameStats::totalStyleShots() const {
  int n = 0;
  for (int v : styleShots) n += v;
  return n;
}
void GameStats::write(DataWriter& w) const {
  w.u8(version);
  w.i32(shots);
  w.i32(bestShot);
  w.i32(bestFever);
  for (int v : freeBalls) w.i32(v);
  w.i32(20);
  for (int i = 0; i < 20; i++) w.i32(styleShots[i]);
}
bool GameStats::read(DataReader& r) {
  version = r.u8();
  shots = r.i32();
  bestShot = r.i32();
  bestFever = r.i32();
  for (int& v : freeBalls) v = r.i32();
  int count = r.i32();
  if (count != 20) return false;
  for (int i = 0; i < 20; i++) styleShots[i] = r.i32();
  for (int i = 20; i < 24; i++) styleShots[i] = 0;
  return r.ok();
}


void PlayerInfo::reset(bool keepLists) {
  dirty = true;
  fEd = fEe = computerPlayer = true;
  b54 = 0;
  colorblind = false;
  storyPending = true;
  fEf = false;
  tipsMask = 0;
  faeries = 0;
  tipCount = 0;
  tipOther = 0;
  computerSkill = -1;
  std::memset(tipCounts, 0, sizeof tipCounts);
  advStage = -1;
  maxStage = maxLevel = advLevel = 0;
  char1 = 0;
  char2 = 1;
  strD0.clear();
  strB4.clear();
  i38 = 0;
  b124 = b128 = 0;
  advCompletions = 0;
  b58 = 0;
  levelSelectStage = 0;
  advRetries = 0;
  statsA.reset();
  advScore = 0;
  advLevelAttempts = 0;
  if (!keepLists) {
    trophies.clear();
    trophyTop.clear();
    clearedLevels.clear();
    advScores.clear();
    acedLevels.clear();
    mantelSeen.clear();
    mantelSeenKnown = false;
    statsB.reset();
  }
  trophyLevelSeen = 0;
}

bool PlayerInfo::isLevelUnlocked(int s, int l) const {
  if (maxStage < s) return false;
  if (maxStage > s) return true;
  return maxLevel > l;
}

void PlayerInfo::restartAdventure() {
  advScore = 0;
  advLevelAttempts = 0;
  advRetries = 0;
  advStage = 0;
  advLevel = 0;
  storyPending = true;
  statsA.reset();
  dirty = true;
}

void PlayerInfo::countLevelStart() {
  advLevelAttempts++;
  if (advLevelAttempts > 1) advRetries++;
  dirty = true;
}

void PlayerInfo::uncountLevelStart() {
  if (advLevelAttempts > 1 && advRetries > 0) advRetries--;
  if (advLevelAttempts > 0) advLevelAttempts--;
  dirty = true;
}

void PlayerInfo::countFailedTry() {
  advLevelAttempts++;
  if (advLevelAttempts > 0) advRetries++;
  dirty = true;
}

void PlayerInfo::addTotalStats(const GameStats& s, bool current) {
  if (current) statsA.add(s);
  statsB.add(s);
  dirty = true;
}

int PlayerInfo::efficiencyBonus() const {
  int v = 10000000 - 200000 * advRetries;
  return v >= 100000 ? v : 100000;
}

void PlayerInfo::checkTrophyTopScore(int id, int score) {
  if (score <= 0) return;
  auto it = trophyTop.insert({id, score}).first;
  if (it->second < score) {
    it->second = score;
    dirty = true;
  } else if (it->second == score) {
    dirty = true;
  }
}

void PlayerInfo::submitAdventureScore(int score) {
  advScores.insert(score);
  if (advScores.size() > 10) {
    auto it = advScores.begin();
    if (*it != score) dirty = true;
    advScores.erase(it);
    return;
  }
  dirty = true;
}

void PlayerInfo::incTip(int id) {
  tipCount++;
  if ((unsigned)id <= 9u) tipCounts[id]++;
  else tipOther++;
  dirty = true;
}

std::vector<uint8_t> PlayerInfo::serialize(int version) const {
  DataWriter w;
  w.i32(version);
  w.i32(maxStage);
  w.i32(maxLevel);
  w.i32(levelSelectStage);
  w.i32(advStage);
  w.i32(advLevel);
  w.i32(char1);
  w.i32(char2);
  w.str(strB4);
  w.i32(i38);
  w.u32((uint32_t)trophies.size());
  for (int v : trophies) w.i32(v);
  w.u32((uint32_t)advScores.size());
  for (int v : advScores) w.i32(v);
  w.u32((uint32_t)trophyTop.size());
  for (auto& [k, v] : trophyTop) { w.i32(k); w.i32(v); }
  w.str(strD0);
  w.bit(fEd);
  w.bit(computerPlayer);
  w.bit(fEe);
  w.bit(colorblind);
  w.bit(storyPending);
  w.bit(fEf);
  w.endBit();
  w.i32(computerSkill);
  w.u8(b54);
  w.u8(b124);
  w.u8(b128);
  w.u32((uint32_t)clearedLevels.size());
  for (auto& s : clearedLevels) w.str(s);
  w.i32(advCompletions);
  w.u8(b58);
  w.i32(tipsMask);
  w.i32(advScore);
  w.i32(advLevelAttempts);
  w.i32(advRetries);
  statsA.write(w);
  statsB.write(w);
  if (version >= 21) {
    w.u16(tipCount);
    for (uint8_t v : tipCounts) w.u8(v);
  }
  if (version >= 22) w.u16(tipOther);
  if (version >= 23) w.u16(trophyLevelSeen);
  if (!acedLevels.empty()) {
    w.u32(0x314e5050);
    w.u32((uint32_t)acedLevels.size());
    for (auto& s : acedLevels) w.str(s);
  }
  if (mantelSeenKnown) {
    w.u32(0x314d5050);
    w.u32((uint32_t)mantelSeen.size());
    for (auto& s : mantelSeen) w.str(s);
  }
  bool moreStyles = false;
  for (int i = 20; i < 24; i++) moreStyles = moreStyles || statsA.styleShots[i] != 0 || statsB.styleShots[i] != 0;
  if (moreStyles) {
    w.u32(0x31535050);
    w.u32(8);
    for (int i = 20; i < 24; i++) w.i32(statsA.styleShots[i]);
    for (int i = 20; i < 24; i++) w.i32(statsB.styleShots[i]);
  }
  if (faeries != 0) {
    w.u32(0x31465050);
    w.i32(faeries);
  }
  return w.bytes();
}

bool PlayerInfo::deserialize(const std::vector<uint8_t>& data) {
  PlayerInfo keep = *this;
  DataReader r(data);
  int version = r.i32();
  reset(false);
  dirty = false;
  if (!r.ok() || version < kMinVersion || version > kWriterVersion) {
    std::string n = keep.name;
    uint32_t sq = keep.useSeq, i = keep.id;
    reset(false);
    name = n;
    useSeq = sq;
    id = i;
    return false;
  }
  maxStage = r.i32();
  maxLevel = r.i32();
  levelSelectStage = r.i32();
  advStage = r.i32();
  advLevel = r.i32();
  char1 = r.i32();
  char2 = r.i32();
  strB4 = r.str();
  i38 = r.i32();
  trophies.clear();
  for (uint32_t n = r.u32(), i = 0; i < n && r.ok(); i++) trophies.insert(r.i32());
  advScores.clear();
  for (uint32_t n = r.u32(), i = 0; i < n && r.ok(); i++) advScores.insert(r.i32());
  trophyTop.clear();
  for (uint32_t n = r.u32(), i = 0; i < n && r.ok(); i++) {
    int k = r.i32();
    trophyTop[k] = r.i32();
  }
  strD0 = r.str();
  fEd = r.bit();
  computerPlayer = r.bit();
  fEe = r.bit();
  colorblind = r.bit();
  storyPending = r.bit();
  fEf = r.bit();
  r.endBit();
  computerSkill = r.i32();
  b54 = r.u8();
  b124 = r.u8();
  b128 = r.u8();
  clearedLevels.clear();
  for (uint32_t n = r.u32(), i = 0; i < n && r.ok(); i++) clearedLevels.insert(r.str());
  advCompletions = std::clamp(r.i32(), 0, 1000000);
  b58 = r.u8();
  tipsMask = r.i32();
  advScore = r.i32();
  advLevelAttempts = r.i32();
  advRetries = r.i32();
  bool ok = statsA.read(r) && statsB.read(r);
  if (version >= 21) {
    tipCount = r.u16();
    for (uint8_t& v : tipCounts) v = r.u8();
  }
  if (version >= 22) tipOther = r.u16();
  if (version >= 23) trophyLevelSeen = r.u16();
  acedLevels.clear();
  while (r.ok() && r.remaining() >= 8) {
    uint32_t magic = r.u32();
    if (magic == 0x314e5050) {
      for (uint32_t n = r.u32(), i = 0; i < n && r.ok(); i++) acedLevels.insert(r.str());
    } else if (magic == 0x31465050) {
      faeries = r.i32();
    } else if (magic == 0x314d5050) {
      for (uint32_t n = r.u32(), i = 0; i < n && r.ok(); i++) mantelSeen.insert(r.str());
      mantelSeenKnown = true;
    } else if (magic == 0x31535050) {
      for (uint32_t n = r.u32(), i = 0; i < n && r.ok(); i++) {
        const int32_t v = r.i32();
        if (i < 4) statsA.styleShots[20 + i] = v;
        else if (i < 8) statsB.styleShots[16 + i] = v;
      }
    } else {
      break;
    }
  }
  if (!r.ok() || !ok) {
    std::string n = keep.name;
    uint32_t sq = keep.useSeq, i = keep.id;
    reset(false);
    name = n;
    useSeq = sq;
    id = i;
    dirty = false;
    return false;
  }
  name = keep.name;
  useSeq = keep.useSeq;
  id = keep.id;
  dirty = false;
  return true;
}


static bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), {});
  return true;
}
static bool writeFileAtomic(const fs::path& p, const std::vector<uint8_t>& data) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  fs::path tmp = p;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      pp::log::error("game", "cannot save " + p.string() + ": the file cannot be written");
      return false;
    }
    f.write((const char*)data.data(), (std::streamsize)data.size());
    if (!f) {
      pp::log::error("game", "cannot save " + p.string() + ": writing failed");
      return false;
    }
  }
  fs::rename(tmp, p, ec);
  if (ec) {
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
  }
  if (ec) pp::log::error("game", "cannot save " + p.string() + ": " + ec.message());
  return !ec;
}


static const char* kNames[32] = {"Andy", "Anthony", "Ben", "Bill", "Brian", "Ace", "Chad", "Dave", "Del", "Ed", "Jimmy Goggle",
                                 "Eric", "Hans", "Isaac", "Jason", "James", "Jeff", "Joe", "John", "Josh", "Juho", "Kathy",
                                 "Katrina", "Mark", "Matt", "Max", "Shawn", "Stephen", "Sukhbir", "Tysen", "Walter", "Wes"};

static const char* kNightsNames[36] = {"Adam", "Anthony", "Ben", "Bill", "Brian", "Carl", "Chad", "Chris", "DP", "Dan", "Dave", "Ed",
                                       "Eric", "Ethan", "George", "Heather", "Isaac", "Jason", "Jeff", "Jeremy", "Joe", "John", "Josh", "Kirk",
                                       "Kurt", "Marcia", "Matt", "Mike", "Rich", "Scott", "Shawn", "Stephen", "Sukhbir", "Tod", "Tysen", "Walter"};

const std::vector<HighScoreEntry>& HighScoreMgr::scores(const std::string& key) {
  auto& t = tables_[key];
  if (t.empty()) {
    bool adv = key.compare(0, 10, "_adventure") == 0;
    int s = adv ? 20000000 : 300000;
    for (int i = 0; i < 10; i++) {
      rng_ = rng_ * 1103515245u + 12345u;
      t.push_back({game_ == Game::Nights ? kNightsNames[(rng_ >> 16) % 36] : kNames[(rng_ >> 16) & 31], s});
      s -= adv ? (i == 0 ? 3000000 : 1500000) : 10000;
    }
  }
  return t;
}

bool HighScoreMgr::wouldRank(const std::string& key, int score) {
  if (score <= 0) return false;
  auto& t = scores(key);
  for (size_t i = 0; i < t.size(); i++)
    if (t[i].score < score) return true;
  return t.size() < 10;
}

int HighScoreMgr::submit(const std::string& key, const std::string& name, int score) {
  if (score <= 0) return 0;
  auto& t = const_cast<std::vector<HighScoreEntry>&>(scores(key));
  size_t pos = 0;
  while (pos < t.size() && t[pos].score >= score) pos++;
  if (pos >= t.size() && t.size() >= 10) return 0;
  t.insert(t.begin() + (long)pos, {name, score});
  while (t.size() > 10) t.pop_back();
  return (int)pos + 1;
}

std::vector<uint8_t> HighScoreMgr::serialize() const {
  DataWriter w;
  w.u32(game_ == Game::Deluxe ? 2 : 3);
  w.u32((uint32_t)tables_.size());
  for (auto& [k, v] : tables_) {
    w.str(k);
    w.u32((uint32_t)v.size());
    for (auto& e : v) {
      w.str(e.name);
      w.i32(e.score);
    }
  }
  return w.bytes();
}
bool HighScoreMgr::deserialize(const std::vector<uint8_t>& d) {
  DataReader r(d);
  uint32_t ver = r.u32();
  uint32_t need = game_ == Game::Deluxe ? 2 : 3;
  if (!r.ok() || ver < need) return false;
  std::map<std::string, std::vector<HighScoreEntry>> t;
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok(); i++) {
    std::string k = r.str();
    uint32_t m = r.u32();
    auto& v = t[k];
    for (uint32_t j = 0; j < m && r.ok(); j++) {
      HighScoreEntry e;
      e.name = r.str();
      e.score = r.i32();
      v.push_back(e);
    }
  }
  if (!r.ok()) return false;
  tables_ = std::move(t);
  return true;
}
bool HighScoreMgr::load(const fs::path& file) {
  std::vector<uint8_t> d;
  if (!readFile(file, d)) return false;
  if (!deserialize(d)) {
    pp::log::warn("game", "the high score file " + file.string() + " is damaged and was ignored");
    return false;
  }
  return true;
}
bool HighScoreMgr::save(const fs::path& file) const { return writeFileAtomic(file, serialize()); }


bool ProfileMgr::validName(const std::string& n) {
  if (n.empty() || n.size() > 40) return false;
  for (unsigned char c : n)
    if (c < 32) return false;
  return n.find_first_not_of(' ') != std::string::npos;
}

PlayerInfo* ProfileMgr::find(const std::string& name) {
  for (auto& p : profiles_)
    if (ieq(p->name, name)) return p.get();
  return nullptr;
}

std::vector<uint8_t> ProfileMgr::serializeUsers() const {
  DataWriter w;
  w.u32(game_ == Game::Deluxe ? 1 : 2);
  w.u16((uint16_t)profiles_.size());
  for (auto& p : profiles_) {
    w.str(p->name);
    w.u32(p->useSeq);
    w.u32(p->id);
  }
  return w.bytes();
}
bool ProfileMgr::deserializeUsers(const std::vector<uint8_t>& d) {
  DataReader r(d);
  uint32_t ver = r.u32();
  bool ok = game_ == Game::Deluxe ? ver <= 1 : ver == 2;
  if (!r.ok() || !ok) return false;
  std::vector<std::unique_ptr<PlayerInfo>> ps;
  uint16_t n = r.u16();
  uint32_t maxId = 0, maxSeq = 0;
  for (uint16_t i = 0; i < n && r.ok(); i++) {
    auto p = std::make_unique<PlayerInfo>();
    p->name = r.str();
    p->useSeq = r.u32();
    p->id = r.u32();
    maxId = std::max(maxId, p->id);
    maxSeq = std::max(maxSeq, p->useSeq);
    ps.push_back(std::move(p));
  }
  if (!r.ok()) return false;
  profiles_ = std::move(ps);
  std::stable_sort(profiles_.begin(), profiles_.end(), [](auto& a, auto& b) { return CaseLess()(a->name, b->name); });
  nextId_ = maxId + 1;
  nextSeq_ = maxSeq + 1;
  return true;
}

fs::path ProfileMgr::modMastersPath() const { return dir_ / (game_ == Game::Nights ? "mod-masters.pp.ini" : "mod-masters.ini"); }

void ProfileMgr::loadModMasters() {
  modChars_.clear();
  modDirty_ = false;
  std::vector<uint8_t> d;
  if (!readFile(modMastersPath(), d)) return;
  std::string text(d.begin(), d.end());
  size_t p = 0;
  while (p < text.size()) {
    size_t e = text.find('\n', p);
    std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
    p = e == std::string::npos ? text.size() : e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1);
    if (b == std::string::npos) continue;
    long id = std::atol(line.substr(0, a).c_str());
    long slot = std::atol(line.substr(a + 1, b - a - 1).c_str());
    if (id <= 0 || slot < 0 || slot > 1) continue;
    modChars_[(uint32_t)id][(size_t)slot] = line.substr(b + 1);
  }
}

std::string ProfileMgr::modMaster(const PlayerInfo& p, int slot) const {
  auto it = modChars_.find(p.id);
  return it != modChars_.end() && slot >= 0 && slot < 2 ? it->second[(size_t)slot] : std::string();
}

void ProfileMgr::setModMaster(const PlayerInfo& p, int slot, const std::string& key) {
  if (slot < 0 || slot > 1) return;
  if (key.empty() && !modChars_.count(p.id)) return;
  auto& row = modChars_[p.id];
  if (row[(size_t)slot] == key) return;
  row[(size_t)slot] = key;
  if (row[0].empty() && row[1].empty()) modChars_.erase(p.id);
  modDirty_ = true;
}

bool ProfileMgr::saveModMasters() {
  if (!modDirty_) return true;
  modDirty_ = false;
  std::string text;
  for (auto& [id, row] : modChars_)
    for (int s = 0; s < 2; s++)
      if (!row[(size_t)s].empty()) text += std::to_string(id) + " " + std::to_string(s) + " " + row[(size_t)s] + "\n";
  if (text.empty()) {
    std::error_code ec;
    fs::remove(modMastersPath(), ec);
    return true;
  }
  return writeFileAtomic(modMastersPath(), std::vector<uint8_t>(text.begin(), text.end()));
}

bool ProfileMgr::load() {
  profiles_.clear();
  nextId_ = nextSeq_ = 1;
  loadModMasters();
  std::vector<uint8_t> d;
  if (!readFile(dir_ / "users.dat", d)) return false;
  if (!deserializeUsers(d)) {
    pp::log::warn("game", "the player list " + (dir_ / "users.dat").string() + " is damaged or from another version and was ignored");
    return false;
  }
  pp::log::info("game", "loaded " + std::to_string(profiles_.size()) + " player profile(s) from " + dir_.string());
  return true;
}
bool ProfileMgr::save() { return writeFileAtomic(dir_ / "users.dat", serializeUsers()); }

PlayerInfo* ProfileMgr::add(const std::string& name) {
  std::string n = name;
  while (!n.empty() && n.front() == ' ') n.erase(n.begin());
  while (!n.empty() && n.back() == ' ') n.pop_back();
  if (!validName(n) || find(n)) return nullptr;
  auto p = std::make_unique<PlayerInfo>();
  p->reset(false);
  p->name = n;
  p->id = nextId_++;
  p->useSeq = nextSeq_++;
  p->dirty = true;
  PlayerInfo* raw = p.get();
  profiles_.push_back(std::move(p));
  std::stable_sort(profiles_.begin(), profiles_.end(), [](auto& a, auto& b) { return CaseLess()(a->name, b->name); });
  saveDetails(*raw);
  return raw;
}

fs::path ProfileMgr::detailsPath(const PlayerInfo& p) const { return dir_ / ("user" + std::to_string(p.id) + (game_ == Game::Nights ? ".pp.dat" : ".dat")); }
fs::path ProfileMgr::savedGamePath(const PlayerInfo& p, GameMode m) const {
  const char* pre = m == GameMode::Adventure ? "stage" : m == GameMode::QuickPlay ? "arcade" : m == GameMode::Duel ? "duel" : m == GameMode::Challenge ? "trophy" : nullptr;
  if (!pre) return {};
  return dir_ / (std::string(pre) + std::to_string(p.id) + ".sav");
}

bool ProfileMgr::remove(const std::string& name) {
  for (size_t i = 0; i < profiles_.size(); i++)
    if (ieq(profiles_[i]->name, name)) {
      std::error_code ec;
      if (modChars_.erase(profiles_[i]->id)) {
        modDirty_ = true;
        saveModMasters();
      }
      fs::remove(detailsPath(*profiles_[i]), ec);
      for (GameMode m : {GameMode::Adventure, GameMode::QuickPlay, GameMode::Duel, GameMode::Challenge}) fs::remove(savedGamePath(*profiles_[i], m), ec);
      profiles_.erase(profiles_.begin() + (long)i);
      return true;
    }
  return false;
}

bool ProfileMgr::rename(const std::string& from, const std::string& to) {
  PlayerInfo* p = find(from);
  if (!p || !validName(to)) return false;
  PlayerInfo* other = find(to);
  if (other && other != p) return false;
  p->name = to;
  std::stable_sort(profiles_.begin(), profiles_.end(), [](auto& a, auto& b) { return CaseLess()(a->name, b->name); });
  return true;
}

void ProfileMgr::loadDetails(PlayerInfo& p) {
  std::vector<uint8_t> d;
  if (readFile(detailsPath(p), d)) {
    if (!p.deserialize(d)) pp::log::warn("game", "the save data of player " + p.name + " is damaged and was ignored");
  } else {
    p.dirty = false;
  }
}

PlayerInfo* ProfileMgr::get(const std::string& name) {
  PlayerInfo* p = find(name);
  if (!p) return nullptr;
  p->useSeq = nextSeq_++;
  if (!p->dirty) {
    std::string n = p->name;
    uint32_t sq = p->useSeq, id = p->id;
    loadDetails(*p);
    p->name = n;
    p->useSeq = sq;
    p->id = id;
  }
  return p;
}
PlayerInfo* ProfileMgr::any() { return profiles_.empty() ? nullptr : get(profiles_.front()->name); }
std::vector<std::string> ProfileMgr::names() const {
  std::vector<std::string> v;
  for (auto& p : profiles_) v.push_back(p->name);
  return v;
}
bool ProfileMgr::saveDetails(PlayerInfo& p) {
  p.dirty = false;
  return writeFileAtomic(detailsPath(p), p.serialize());
}


bool Settings::load(const fs::path& file) {
  std::ifstream f(file);
  if (!f) return false;
  std::string line;
  while (std::getline(f, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
    auto fl = [&] { return (float)std::atof(v.c_str()); };
    auto bl = [&] { return v == "1" || v == "true"; };
    if (k == "MusicVolume") music = fl();
    else if (k == "SfxVolume") sfx = fl();
    else if (k == "FeverVolume") fever = fl();
    else if (k == "Muted") muted = bl();
    else if (k == "Fullscreen") fullscreen = bl();
    else if (k == "CustomCursors") customCursors = bl();
    else if (k == "WaitForVSync") vsync = bl();
    else if (k == "CurUser") curUser = v;
    else if (k == "MaxStage") maxStage = std::atoi(v.c_str());
    else if (k == "MaxLevel") maxLevel = std::atoi(v.c_str());
    else if (k == "WinW") winW = std::atoi(v.c_str());
    else if (k == "WinH") winH = std::atoi(v.c_str());
    else if (k == "Widescreen") widescreen = std::clamp(std::atoi(v.c_str()), 0, 4);
    else if (k == "SeparateWideScores") separateWideScores = bl();
    else if (k == "VisInterpolate") vis.interpolate = bl();
    else if (k == "VisSmooth") vis.smooth = bl();
    else if (k == "VisBloom") vis.bloom = bl();
    else if (k == "VisSparks") vis.sparks = bl();
    else if (k == "VisUltraWideHud") vis.ultraWideHud = bl();
    else if (k == "VisBloomStrength") vis.bloomStrength = std::clamp(fl(), 0.0f, 2.0f);
  }
  music = std::clamp(music, 0.0f, 1.0f);
  sfx = std::clamp(sfx, 0.0f, 1.0f);
  fever = std::clamp(fever, 0.0f, 1.0f);
  return true;
}
bool Settings::save(const fs::path& file) const {
  std::ostringstream o;
  o << "MusicVolume=" << music << "\nSfxVolume=" << sfx << "\nFeverVolume=" << fever << "\nMuted=" << (muted ? 1 : 0) << "\nFullscreen=" << (fullscreen ? 1 : 0)
    << "\nCustomCursors=" << (customCursors ? 1 : 0) << "\nWaitForVSync=" << (vsync ? 1 : 0) << "\nCurUser=" << curUser << "\nMaxStage=" << maxStage
    << "\nMaxLevel=" << maxLevel << "\nWinW=" << winW << "\nWinH=" << winH << "\nWidescreen=" << widescreen << "\nSeparateWideScores=" << (separateWideScores ? 1 : 0) << "\nVisInterpolate=" << (vis.interpolate ? 1 : 0)
    << "\nVisSmooth=" << (vis.smooth ? 1 : 0) << "\nVisBloom=" << (vis.bloom ? 1 : 0) << "\nVisSparks=" << (vis.sparks ? 1 : 0)
    << "\nVisUltraWideHud=" << (vis.ultraWideHud ? 1 : 0) << "\nVisBloomStrength=" << vis.bloomStrength << "\n";
  std::string s = o.str();
  return writeFileAtomic(file, std::vector<uint8_t>(s.begin(), s.end()));
}

}
