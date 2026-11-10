#include "profile_import.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace pp {

namespace {

constexpr size_t kMaxFile = 16u * 1024u * 1024u;
constexpr uint32_t kMaxItems = 1000000;
constexpr int kDeluxeFirst = 19, kDeluxeLast = 23, kNightsVersion = 28;
constexpr int kNightsStyleCount = 24;

std::string lowerStr(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string utf8(const fs::path& p) {
  auto u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

bool readSmall(const fs::path& p, std::vector<uint8_t>& out) {
  std::error_code ec;
  if (!fs::is_regular_file(p, ec)) return false;
  auto sz = fs::file_size(p, ec);
  if (ec || sz > kMaxFile) return false;
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), {});
  return true;
}

bool fits(const DataReader& r, uint32_t n, size_t each) {
  return r.ok() && n <= kMaxItems && (size_t)n <= r.remaining() / each;
}

bool readSetInt(DataReader& r, std::vector<int>& out) {
  uint32_t n = r.u32();
  if (!fits(r, n, 4)) return false;
  out.clear();
  out.reserve(n);
  for (uint32_t i = 0; i < n; i++) out.push_back(r.i32());
  return r.ok();
}

bool readMapInt(DataReader& r, std::vector<std::pair<int, int>>& out) {
  uint32_t n = r.u32();
  if (!fits(r, n, 8)) return false;
  out.clear();
  for (uint32_t i = 0; i < n; i++) {
    int k = r.i32();
    int v = r.i32();
    out.push_back({k, v});
  }
  return r.ok();
}

bool readSetStr(DataReader& r, std::vector<std::string>& out) {
  uint32_t n = r.u32();
  if (!fits(r, n, 2)) return false;
  out.clear();
  for (uint32_t i = 0; i < n; i++) out.push_back(r.str());
  return r.ok();
}

bool readVecInt(DataReader& r) {
  uint32_t n = r.u32();
  if (!fits(r, n, 4) || n > 100000) return false;
  for (uint32_t i = 0; i < n; i++) r.i32();
  return r.ok();
}

bool readStats(DataReader& r, GameStats& g, uint32_t styleCount) {
  g.version = r.u8();
  g.a0 = r.i32();
  g.bestShot = r.i32();
  g.a8 = r.i32();
  for (int& v : g.freeBalls) v = r.i32();
  uint32_t count = r.u32();
  if (!r.ok() || count != styleCount) return false;
  for (uint32_t i = 0; i < count; i++) {
    int v = r.i32();
    if (i < 20) g.styleShots[i] = v;
  }
  return r.ok();
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void tidy(PlayerInfo& p) {
  p.maxStage = clampInt(p.maxStage, 0, 99);
  p.maxLevel = clampInt(p.maxLevel, 0, 4);
  p.levelSelectStage = clampInt(p.levelSelectStage, 0, 99);
  p.advStage = clampInt(p.advStage, -1, 99);
  p.advLevel = clampInt(p.advLevel, 0, 4);
  p.char1 = clampInt(p.char1, 0, 99);
  p.char2 = clampInt(p.char2, 0, 99);
  p.computerSkill = clampInt(p.computerSkill, -1, 10);
  p.advCompletions = clampInt(p.advCompletions, 0, 1000000);
  p.advScore = std::max(p.advScore, 0);
  p.advLevelAttempts = std::max(p.advLevelAttempts, 0);
  p.advRetries = std::max(p.advRetries, 0);
  for (auto it = p.advScores.begin(); it != p.advScores.end();) it = *it < 0 ? p.advScores.erase(it) : std::next(it);
  while (p.advScores.size() > 10) p.advScores.erase(p.advScores.begin());
  for (auto it = p.trophyTop.begin(); it != p.trophyTop.end();) it = it->second < 0 ? p.trophyTop.erase(it) : std::next(it);
  p.dirty = true;
}

bool parseDeluxe(DataReader& r, int version, PlayerInfo& p, std::string& why) {
  p.maxStage = r.i32();
  p.maxLevel = r.i32();
  p.levelSelectStage = r.i32();
  p.advStage = r.i32();
  p.advLevel = r.i32();
  p.char1 = r.i32();
  p.char2 = r.i32();
  p.strB4 = r.str();
  p.i38 = r.i32();
  std::vector<int> ints;
  if (!readSetInt(r, ints)) { why = "the list of trophies is cut short"; return false; }
  p.trophies.insert(ints.begin(), ints.end());
  if (!readSetInt(r, ints)) { why = "the list of adventure scores is cut short"; return false; }
  p.advScores.insert(ints.begin(), ints.end());
  std::vector<std::pair<int, int>> pairs;
  if (!readMapInt(r, pairs)) { why = "the list of trophy scores is cut short"; return false; }
  for (auto& kv : pairs) p.trophyTop[kv.first] = kv.second;
  p.strD0 = r.str();
  p.fEd = r.bit();
  p.computerPlayer = r.bit();
  p.fEe = r.bit();
  p.colorblind = r.bit();
  p.storyPending = r.bit();
  p.fEf = r.bit();
  r.endBit();
  p.computerSkill = r.i32();
  p.b54 = r.u8();
  p.b124 = r.u8();
  p.b128 = r.u8();
  std::vector<std::string> strs;
  if (!readSetStr(r, strs)) { why = "the list of cleared levels is cut short"; return false; }
  p.clearedLevels.insert(strs.begin(), strs.end());
  p.advCompletions = r.i32();
  p.b58 = r.u8();
  p.tipsMask = r.i32();
  p.advScore = r.i32();
  p.advLevelAttempts = r.i32();
  p.advRetries = r.i32();
  if (!r.ok() || !readStats(r, p.statsA, 20) || !readStats(r, p.statsB, 20)) { why = "the statistics are cut short"; return false; }
  if (version >= 21) {
    p.tipCount = r.u16();
    for (uint8_t& v : p.tipCounts) v = r.u8();
  }
  if (version >= 22) p.tipOther = r.u16();
  if (version >= 23) p.trophyLevelSeen = r.u16();
  if (!r.ok()) { why = "the file ends too early"; return false; }
  return true;
}

bool parseNights(DataReader& r, PlayerInfo& p, std::string& why) {
  p.maxStage = r.i32();
  p.maxLevel = r.i32();
  p.levelSelectStage = r.i32();
  p.advStage = r.i32();
  p.advLevel = r.i32();
  p.char1 = r.i32();
  p.char2 = r.i32();
  p.strB4 = r.str();
  p.i38 = r.i32();
  std::vector<int> ints;
  if (!readSetInt(r, ints)) { why = "the list of trophies is cut short"; return false; }
  p.trophies.insert(ints.begin(), ints.end());
  if (!readSetInt(r, ints)) { why = "the list of adventure scores is cut short"; return false; }
  p.advScores.insert(ints.begin(), ints.end());
  std::vector<std::pair<int, int>> pairs;
  if (!readMapInt(r, pairs)) { why = "the list of trophy scores is cut short"; return false; }
  for (auto& kv : pairs) p.trophyTop[kv.first] = kv.second;
  p.strD0 = r.str();
  p.fEd = r.bit();
  p.computerPlayer = r.bit();
  p.fEe = r.bit();
  p.colorblind = r.bit();
  p.storyPending = r.bit();
  p.fEf = r.bit();
  r.bit();
  r.bit();
  r.endBit();
  p.computerSkill = r.i32();
  p.b54 = r.u8();
  p.b124 = r.u8();
  p.b128 = r.u8();
  std::vector<std::string> strs;
  if (!readSetStr(r, strs)) { why = "the list of cleared levels is cut short"; return false; }
  p.clearedLevels.insert(strs.begin(), strs.end());
  p.advCompletions = r.i32();
  p.b58 = r.u8();
  p.tipsMask = r.i32();
  r.i32();
  p.advScore = r.i32();
  p.advLevelAttempts = r.i32();
  p.advRetries = r.i32();
  if (!r.ok() || !readStats(r, p.statsA, kNightsStyleCount) || !readStats(r, p.statsB, kNightsStyleCount)) { why = "the statistics are cut short"; return false; }
  p.tipCount = r.u16();
  for (uint8_t& v : p.tipCounts) v = r.u8();
  p.tipOther = r.u16();
  r.u16();
  if (!readSetStr(r, strs)) { why = "the list of aced levels is cut short"; return false; }
  p.acedLevels.insert(strs.begin(), strs.end());
  r.i32();
  if (!readVecInt(r) || !readVecInt(r)) { why = "a list of numbers is cut short"; return false; }
  if (!readSetStr(r, strs) || !readSetStr(r, strs)) { why = "a list of levels is cut short"; return false; }
  if (!readSetInt(r, ints)) { why = "a list of numbers is cut short"; return false; }
  if (!readSetStr(r, strs)) { why = "a list of names is cut short"; return false; }
  p.mantelSeen.insert(strs.begin(), strs.end());
  p.mantelSeenKnown = true;
  if (!r.ok()) { why = "the file ends too early"; return false; }
  return true;
}

std::string cleanName(const std::string& in) {
  std::string s;
  for (unsigned char c : in)
    if (c >= 32 && c != 127) s += (char)c;
  size_t a = s.find_first_not_of(' ');
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(' ');
  s = s.substr(a, b - a + 1);
  if (s.size() > 40) s.resize(40);
  return s;
}

bool sameName(const std::string& a, const std::string& b) { return lowerStr(a) == lowerStr(b); }

bool userFileId(const std::string& lowerName, uint32_t& id) {
  if (lowerName.size() < 9 || lowerName.compare(0, 4, "user") != 0) return false;
  if (lowerName.compare(lowerName.size() - 4, 4, ".dat") != 0) return false;
  std::string mid = lowerName.substr(4, lowerName.size() - 8);
  if (mid.empty() || mid.size() > 9) return false;
  for (char c : mid)
    if (c < '0' || c > '9') return false;
  id = (uint32_t)std::strtoul(mid.c_str(), nullptr, 10);
  return true;
}

bool findFile(const fs::path& dir, const std::string& lowerName, fs::path& out) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    if (lowerStr(utf8(it->path().filename())) == lowerName) {
      out = it->path();
      return true;
    }
  }
  return false;
}

bool hasProfileFiles(const fs::path& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return false;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    std::string n = lowerStr(utf8(it->path().filename()));
    uint32_t id;
    if (n == "users.dat" || userFileId(n, id)) return true;
  }
  return false;
}

fs::path locateFolder(const fs::path& p) {
  std::vector<fs::path> tries = {p, p / "userdata", p / "Peggle" / "userdata", p / "PeggleNights" / "userdata", p / "PopCap Games" / "Peggle" / "userdata",
                                 p / "PopCap Games" / "PeggleNights" / "userdata"};
  for (auto& t : tries)
    if (hasProfileFiles(t)) return t;
  return {};
}

int countReplays(const fs::path& dir) {
  std::error_code ec;
  int n = 0;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    std::string e = lowerStr(utf8(it->path().extension()));
    if (e == ".pego" || e == ".pegn" || e == ".peg") n++;
  }
  return n;
}

}

bool parseOriginalProfile(const std::vector<uint8_t>& data, ImportedProfile& out) {
  out.readable = false;
  out.problem.clear();
  DataReader r(data);
  int version = r.i32();
  if (!r.ok()) {
    out.problem = "the file is empty or too short";
    return false;
  }
  out.fileVersion = version;
  PlayerInfo p;
  p.reset(false);
  std::string why;
  bool ok;
  if (version >= kDeluxeFirst && version <= kDeluxeLast) {
    out.game = Game::Deluxe;
    ok = parseDeluxe(r, version, p, why);
  } else if (version == kNightsVersion) {
    out.game = Game::Nights;
    ok = parseNights(r, p, why);
  } else {
    out.problem = "it was saved by a version of the game I do not know (file version " + std::to_string(version) + ")";
    return false;
  }
  if (!ok) {
    out.problem = why.empty() ? "the file is damaged" : why;
    return false;
  }
  out.trailing = r.remaining();
  tidy(p);
  p.name = out.name;
  out.info = std::move(p);
  out.readable = true;
  return true;
}

bool parseOriginalUsers(const std::vector<uint8_t>& data, Game& game, std::vector<std::pair<std::string, uint32_t>>& list, std::string& why) {
  DataReader r(data);
  uint32_t version = r.u32();
  if (!r.ok()) {
    why = "the file is empty or too short";
    return false;
  }
  if (version <= 1) game = Game::Deluxe;
  else if (version == 2) game = Game::Nights;
  else {
    why = "it was saved by a version of the game I do not know (file version " + std::to_string(version) + ")";
    return false;
  }
  uint16_t n = r.u16();
  list.clear();
  for (uint16_t i = 0; i < n && r.ok(); i++) {
    std::string name = r.str();
    r.u32();
    uint32_t id = r.u32();
    if (r.ok()) list.push_back({name, id});
  }
  if (!r.ok()) {
    why = "the list of players is cut short";
    return false;
  }
  return true;
}

bool parseOriginalHighScores(const std::vector<uint8_t>& data, Game& game, std::map<std::string, std::vector<HighScoreEntry>>& out, std::string& why) {
  DataReader r(data);
  uint32_t version = r.u32();
  if (!r.ok()) {
    why = "the file is empty or too short";
    return false;
  }
  if (version == 2) game = Game::Deluxe;
  else if (version == 3) game = Game::Nights;
  else {
    why = "it was saved by a version of the game I do not know (file version " + std::to_string(version) + ")";
    return false;
  }
  uint32_t tables = r.u32();
  if (!fits(r, tables, 6)) {
    why = "the file is damaged";
    return false;
  }
  out.clear();
  for (uint32_t t = 0; t < tables; t++) {
    std::string key = r.str();
    uint32_t n = r.u32();
    if (!fits(r, n, 6)) {
      why = "the file is damaged";
      return false;
    }
    auto& v = out[key];
    for (uint32_t i = 0; i < n; i++) {
      HighScoreEntry e;
      e.name = r.str();
      e.score = r.i32();
      v.push_back(e);
    }
    if (!r.ok()) {
      why = "the file ends too early";
      return false;
    }
  }
  return true;
}

ImportScan scanOriginalSaves(const fs::path& chosen) {
  ImportScan s;
  std::error_code ec;
  fs::path onlyFile;
  fs::path base = chosen;
  if (fs::is_regular_file(chosen, ec)) {
    onlyFile = chosen;
    base = chosen.parent_path();
  } else if (!fs::is_directory(chosen, ec)) {
    s.notes.push_back("I could not open " + utf8(chosen) + ".");
    return s;
  }
  fs::path folder = onlyFile.empty() ? locateFolder(base) : base;
  if (folder.empty()) {
    s.notes.push_back("There are no Peggle player files in " + utf8(chosen) + ". Choose the userdata folder of the original game, or one of its player files (user1.dat and so on).");
    return s;
  }
  s.folder = folder;

  std::vector<std::pair<std::string, uint32_t>> list;
  fs::path usersPath;
  if (findFile(folder, "users.dat", usersPath)) {
    std::vector<uint8_t> d;
    std::string why;
    Game g;
    if (!readSmall(usersPath, d)) {
      s.notes.push_back("I could not read users.dat, so the player names are missing.");
    } else if (!parseOriginalUsers(d, g, list, why)) {
      s.notes.push_back("users.dat could not be used: " + why + ".");
      list.clear();
    } else {
      s.gameKnown = true;
      s.game = g;
    }
  }

  std::vector<std::pair<uint32_t, fs::path>> files;
  {
    std::error_code e1;
    for (fs::directory_iterator it(folder, e1), end; !e1 && it != end; it.increment(e1)) {
      std::error_code e2;
      if (!it->is_regular_file(e2)) continue;
      uint32_t id;
      if (userFileId(lowerStr(utf8(it->path().filename())), id)) files.push_back({id, it->path()});
    }
  }
  std::sort(files.begin(), files.end());

  std::set<uint32_t> listed;
  auto addFile = [&](uint32_t id, const fs::path& path, const std::string& name, bool known) {
    ImportedProfile ip;
    ip.originalId = id;
    ip.fileName = utf8(path.filename());
    ip.name = known ? cleanName(name) : std::string();
    ip.nameKnown = known && !ip.name.empty();
    if (!ip.nameKnown) ip.name = "Player " + std::to_string(id);
    std::vector<uint8_t> d;
    if (!readSmall(path, d)) {
      ip.problem = "I could not read the file";
    } else {
      parseOriginalProfile(d, ip);
    }
    s.profiles.push_back(std::move(ip));
  };

  if (!onlyFile.empty()) {
    uint32_t id = 0;
    if (!userFileId(lowerStr(utf8(onlyFile.filename())), id)) {
      s.notes.push_back(utf8(onlyFile.filename()) + " is not a player file. Player files are called user1.dat, user2.dat and so on.");
      return s;
    }
    std::string name;
    bool known = false;
    for (auto& e : list)
      if (e.second == id) {
        name = e.first;
        known = true;
      }
    addFile(id, onlyFile, name, known);
  } else {
    for (auto& e : list) {
      listed.insert(e.second);
      fs::path path;
      bool found = false;
      for (auto& f : files)
        if (f.first == e.second) {
          path = f.second;
          found = true;
        }
      if (!found) {
        s.notes.push_back("The player " + e.first + " is in the list, but user" + std::to_string(e.second) + ".dat is missing.");
        continue;
      }
      addFile(e.second, path, e.first, true);
    }
    for (auto& f : files)
      if (!listed.count(f.first)) addFile(f.first, f.second, "", false);
  }

  for (auto& p : s.profiles)
    if (p.readable) {
      s.gameKnown = true;
      s.game = p.game;
      break;
    }

  fs::path hsPath;
  if (onlyFile.empty() && findFile(folder, "highscores.dat", hsPath)) {
    std::vector<uint8_t> d;
    std::string why;
    Game g = s.game;
    if (!readSmall(hsPath, d)) {
      s.notes.push_back("I could not read highscores.dat.");
    } else if (!parseOriginalHighScores(d, g, s.scores, why)) {
      s.notes.push_back("highscores.dat could not be used: " + why + ".");
      s.scores.clear();
    } else {
      s.haveScores = true;
      if (!s.gameKnown) {
        s.gameKnown = true;
        s.game = g;
      }
    }
  }

  fs::path rep = folder / "replays";
  if (fs::is_directory(rep, ec)) {
    s.replayFolder = rep;
    s.replayCount = countReplays(rep);
  }
  return s;
}

std::vector<fs::path> originalSaveFolders(Game g) {
  std::vector<fs::path> cand;
  const char* product = g == Game::Nights ? "PeggleNights" : "Peggle";
  const char* steamId = g == Game::Nights ? "3540" : "3480";
  auto env = [](const char* n) -> std::string {
    const char* v = std::getenv(n);
    return v ? v : "";
  };
  auto tail = [&](const fs::path& root) { return root / "PopCap Games" / product / "userdata"; };
  auto shared = [&](const fs::path& root) {
    cand.push_back(tail(root));
    cand.push_back(root / "Steam" / product / "userdata");
  };
#ifdef _WIN32
  for (const char* v : {"ProgramData", "ALLUSERSPROFILE", "PUBLIC"})
    if (auto e = env(v); !e.empty()) shared(fs::path(e));
  shared(fs::path("C:/ProgramData"));
  if (auto e = env("APPDATA"); !e.empty()) cand.push_back(tail(fs::path(e)));
#else
  std::string home = env("HOME");
  if (!home.empty()) {
    fs::path h(home);
    auto prefix = [&](const fs::path& drive) { shared(drive / "ProgramData"); };
    prefix(h / ".wine" / "drive_c");
    std::vector<fs::path> steam = {h / ".steam" / "steam", h / ".local" / "share" / "Steam", h / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam",
                                   h / "Library" / "Application Support" / "Steam"};
    for (auto& s : steam) prefix(s / "steamapps" / "compatdata" / steamId / "pfx" / "drive_c");
    std::error_code ec;
    fs::path bottles = h / "Library" / "Application Support" / "CrossOver" / "Bottles";
    for (fs::directory_iterator it(bottles, ec), end; !ec && it != end; it.increment(ec)) prefix(it->path() / "drive_c");
    cand.push_back(h / "Library" / "Application Support" / "PopCap Games" / product / "userdata");
  }
#endif
  std::vector<fs::path> out;
  for (auto& c : cand) {
    std::error_code ec;
    if (!fs::is_directory(c, ec)) continue;
    bool dup = false;
    for (auto& o : out)
      if (o == c) dup = true;
    if (!dup) out.push_back(c);
  }
  return out;
}

std::string uniqueProfileName(const ProfileMgr& pm, const std::string& wanted) {
  std::string base = cleanName(wanted);
  if (base.empty()) base = "Player";
  auto taken = [&](const std::string& n) {
    for (auto& e : pm.names())
      if (sameName(e, n)) return true;
    return false;
  };
  if (!taken(base)) return base;
  for (int i = 2; i < 1000; i++) {
    std::string suffix = " " + std::to_string(i);
    std::string b = base;
    if (b.size() + suffix.size() > 40) b.resize(40 - suffix.size());
    if (!taken(b + suffix)) return b + suffix;
  }
  return base;
}

ImportOutcome importOriginalProfile(ProfileMgr& pm, HighScoreMgr* scores, const ImportedProfile& src, const ImportScan& scan, const std::string& asName) {
  ImportOutcome o;
  o.game = src.game;
  if (!src.readable) {
    o.message = "This player could not be read: " + src.problem + ".";
    return o;
  }
  std::string name = cleanName(asName.empty() ? src.name : asName);
  if (name.empty() || !ProfileMgr::validName(name)) {
    o.message = "That name cannot be used.";
    return o;
  }
  for (auto& e : pm.names())
    if (sameName(e, name)) {
      o.message = "A player called " + name + " already exists. Nothing was changed.";
      return o;
    }
  PlayerInfo* p = pm.add(name);
  if (!p) {
    o.message = "The player could not be created.";
    return o;
  }
  PlayerInfo n = src.info;
  n.name = p->name;
  n.id = p->id;
  n.useSeq = p->useSeq;
  n.dirty = true;
  *p = std::move(n);
  pm.saveDetails(*p);
  pm.save();
  o.ok = true;
  o.name = p->name;
  if (scores && scan.haveScores) {
    for (auto& [key, entries] : scan.scores) {
      for (auto& e : entries) {
        if (!sameName(e.name, src.name) || e.score <= 0) continue;
        bool have = false;
        for (auto& cur : scores->scores(key))
          if (cur.score == e.score && sameName(cur.name, p->name)) have = true;
        if (have) continue;
        if (scores->submit(key, p->name, e.score) > 0) o.scoresAdded++;
      }
    }
  }
  o.message = "Imported " + o.name + ".";
  return o;
}

static std::string withCommas(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v), o;
  for (size_t i = 0; i < s.size(); i++) {
    if (i && (s.size() - i) % 3 == 0) o += ',';
    o += s[i];
  }
  return v < 0 ? "-" + o : o;
}

std::vector<std::string> describeImported(const ImportedProfile& ip) {
  std::vector<std::string> v;
  if (!ip.readable) {
    v.push_back("Cannot be read: " + ip.problem + ".");
    return v;
  }
  const PlayerInfo& p = ip.info;
  v.push_back(std::string(ip.game == Game::Nights ? "Peggle Nights" : "Peggle Deluxe") + " player");
  if (p.advCompletions > 0) v.push_back("Adventure finished " + std::to_string(p.advCompletions) + (p.advCompletions == 1 ? " time" : " times"));
  else v.push_back("Adventure: stage " + std::to_string(p.maxStage + 1) + ", level " + std::to_string(p.maxLevel + 1));
  v.push_back(std::to_string(p.trophies.size()) + " challenge trophies");
  v.push_back(std::to_string(p.clearedLevels.size()) + " levels cleared");
  if (ip.game == Game::Nights) v.push_back(std::to_string(p.acedLevels.size()) + " levels aced");
  if (!p.advScores.empty()) v.push_back("Best adventure score " + withCommas(*p.advScores.rbegin()));
  return v;
}

}
