#include "ui/stages.h"

#include "log.h"

#include <fstream>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "ui/cfg.h"

namespace pp::ui {

static std::string lc(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
static bool ieq(const std::string& a, const char* b) { return lc(a) == lc(b); }
static std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}


void StageMgr::parseCfg(const std::string& text, bool pack, const std::string& label) {
  auto items = cfgParse(text);
  for (auto& it : items) {
    if (it.isGroup && ieq(it.key, "Stage")) {
      StageInfo st;
      st.pack = pack;
      st.packLabel = label;
      for (auto& k : it.kids) {
        if (k.isGroup) continue;
        auto v = cfgValues(k.label);
        if (ieq(k.key, "Level")) {
          if (v.empty()) continue;
          LevelInfo li;
          li.file = trim(v[0]);
          li.title = v.size() > 1 ? v[1] : li.file;
          if (v.size() > 2) li.ace = std::atoi(v[2].c_str());
          if (v.size() > 3) li.extra = std::atoi(v[3].c_str());
          li.index = (int)st.levels.size();
          st.levels.push_back(li);
        } else if (ieq(k.key, "Dialog")) {
          if (v.size() < 2) continue;
          int n = std::atoi(v[0].c_str()) - 1;
          if (n < 0 || n >= (int)st.levels.size()) continue;
          st.levels[n].dialog = v[1];
          if (v.size() > 2) st.levels[n].dialogTitle = v[2];
          if (v.size() > 3) st.levels[n].dialogChar = std::atoi(v[3].c_str());
        } else if (ieq(k.key, "StageDialog")) {
          if (v.size() < 2) continue;
          int n = std::atoi(v[0].c_str()) - 1;
          if (n < 0 || n >= 5) continue;
          StoryData sd;
          sd.text = v[1];
          if (v.size() > 2) sd.arg = std::atoi(v[2].c_str());
          st.story[n].push_back(sd);
        } else if (ieq(k.key, "Credit")) {
          if (v.size() < 2) continue;
          int n = std::atoi(v[0].c_str()) - 1;
          if (n < 0 || n >= 4) continue;
          StoryData sd;
          sd.text = v[1];
          if (v.size() > 2 && !trim(v[2]).empty()) sd.arg = std::atoi(v[2].c_str());
          st.credits[n].push_back(sd);
        } else if (ieq(k.key, "Name")) {
          if (!v.empty()) st.name = v[0];
        } else if (ieq(k.key, "Desc")) {
          if (!v.empty()) st.desc = v[0];
        } else if (ieq(k.key, "Decal")) {
          DecalInfo d;
          if (v.size() >= 10) {
            d.image = trim(v[0]);
            d.layer = std::atoi(v[1].c_str());
            d.emit = (float)std::atof(v[2].c_str());
            d.x = std::atoi(v[3].c_str());
            d.y = std::atoi(v[4].c_str());
            d.hx = std::atoi(v[5].c_str());
            d.hy = std::atoi(v[6].c_str());
            d.hw = std::atoi(v[7].c_str());
            d.hh = std::atoi(v[8].c_str());
            d.hover = v[9];
            st.decals.push_back(d);
          }
        }
      }
      if (!st.levels.empty()) stages_.push_back(std::move(st));
    } else if (!it.isGroup) {
      auto v = cfgValues(it.label);
      if (ieq(it.key, "Credit")) {
        if (v.size() == 1) credits_.push_back(v[0]);
      } else if (ieq(it.key, "Tip")) {
        if (!v.empty()) tips_.push_back(v[0]);
      } else if (ieq(it.key, "ExcludeRandStages") || ieq(it.key, "IncludeRandStages")) {
        bool ex = ieq(it.key, "ExcludeRandStages");
        for (auto& s : v) {
          int n = std::atoi(s.c_str()) - 1;
          if (auto* st = stage(n))
            for (auto& l : st->levels) l.excludeRand = ex;
        }
      } else if (ieq(it.key, "ExcludeRandLevels") || ieq(it.key, "IncludeRandLevels")) {
        bool ex = ieq(it.key, "ExcludeRandLevels");
        for (auto& s : v) pendingRand_.push_back({lc(trim(s)), ex});
      }
    }
  }
}

bool StageMgr::load(const Vfs& vfs) {
  stages_.clear();
  byName_.clear();
  tips_.clear();
  auto h = vfs.read("levels/stages.cfg");
  if (!h) {
    cfgError = "levels/stages.cfg not found";
    pp::log::error("game", cfgError);
    return false;
  }
  pendingRand_.clear();
  parseCfg(std::string(h->bytes.begin(), h->bytes.end()), false, "main");
  auto rebuild = [&] {
    byName_.clear();
    for (int s = 0; s < (int)stages_.size(); s++)
      for (int l = 0; l < (int)stages_[s].levels.size(); l++) {
        stages_[s].levels[l].stage = s;
        stages_[s].levels[l].index = l;
        byName_.insert({lc(stages_[s].levels[l].file), {s, l}});
      }
  };
  mainStages_ = (int)stages_.size();
  rebuild();
  mainLevels_ = byName_.size();
  for (auto& pr : pendingRand_) {
    auto r = byName_.equal_range(pr.first);
    for (auto i = r.first; i != r.second; ++i) stages_[i->second.first].levels[i->second.second].excludeRand = pr.second;
  }
  for (auto& L : vfs.layers()) {
    if (!L.isLevelPack) continue;
    for (auto& [k, idx] : L.pakIndex) {
      if (k.find('\\') != std::string::npos || k.size() < 4 || k.substr(k.size() - 4) != ".CFG") continue;
      auto& e = L.pak->entries[idx];
      parseCfg(std::string(L.pak->data.begin() + e.offset, L.pak->data.begin() + e.offset + e.size), true, L.label);
    }
  }
  for (auto& L : vfs.layers()) {
    if (!L.isMod) continue;
    auto it = L.files.find("LEVELS\\MOD_LEVELS.CFG");
    if (it == L.files.end()) continue;
    std::ifstream mf(it->second, std::ios::binary);
    parseCfg(std::string((std::istreambuf_iterator<char>(mf)), {}), true, L.label);
  }
  rebuild();
  return mainStages_ > 0;
}

const LevelInfo* StageMgr::levelByName(const std::string& name) const {
  auto it = byName_.find(lc(name));
  if (it == byName_.end()) return nullptr;
  return &stages_[it->second.first].levels[it->second.second];
}

std::string StageMgr::title(int i) const {
  static const char* kWord[12] = {"One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten", "Eleven", "Twelve"};
  const StageInfo* s = stage(i);
  if (s && s->pack && !s->name.empty()) return s->name;
  return i >= 0 && i <= 11 ? std::string("Stage ") + kWord[i] : "Stage " + std::to_string(i + 1);
}

const std::string& StageMgr::tip(unsigned i) const {
  static const std::string none;
  return tips_.empty() ? none : tips_[i % tips_.size()];
}

bool StageMgr::randomLevel(const PlayerInfo& p, int& stage, int& level, bool duel, uint32_t rnd) {
  struct C { int s, l, use; };
  std::vector<C> c;
  int bestUse = 1 << 30;
  for (int s = 0; s < mainStages_; s++)
    for (int l = 0; l < (int)stages_[s].levels.size(); l++) {
      const auto& li = stages_[s].levels[l];
      if (li.excludeRand || !p.isLevelUnlocked(s, l)) continue;
      c.push_back({s, l, li.lastUse});
      bestUse = std::min(bestUse, li.lastUse);
    }
  if (c.empty()) return false;
  std::vector<C> pool;
  for (auto& x : c)
    if (x.use <= bestUse + 5) pool.push_back(x);
  if (pool.empty()) pool = c;
  auto pick = pool[rnd % pool.size()];
  stage = pick.s;
  level = pick.l;
  return true;
}


bool TrophyMgr::load(const Vfs& vfs) {
  pages_.clear();
  byId_.clear();
  auto h = vfs.read("levels/trophy.cfg");
  if (!h) {
    cfgError = "levels/trophy.cfg not found";
    pp::log::error("game", cfgError);
    return false;
  }
  auto items = cfgParse(std::string(h->bytes.begin(), h->bytes.end()));
  for (auto& it : items) {
    if (!it.isGroup || !ieq(it.key, "Page")) continue;
    TrophyPage pg;
    pg.name = it.label;
    for (auto& k : it.kids) {
      if (!k.isGroup) {
        auto v = cfgValues(k.label);
        if (ieq(k.key, "Desc") && !v.empty()) pg.desc = v[0];
        else if (ieq(k.key, "SmallDesc") && !v.empty()) pg.smallDesc = v[0];
        continue;
      }
      if (!ieq(k.key, "Trophy")) continue;
      TrophyInfo t;
      t.name = k.label;
      for (auto& f : k.kids) {
        auto v = cfgValues(f.label);
        std::string key = lc(f.key);
        auto num = [&] { return v.empty() ? 0 : std::atoi(v[0].c_str()); };
        auto flag = [&] { return !v.empty() && ieq(trim(v[0]), "true"); };
        if (key == "id") t.id = num();
        else if (key == "desc") t.desc = v.empty() ? "" : v[0];
        else if (key == "smalldesc") t.smallDesc = v.empty() ? "" : v[0];
        else if (key == "ingamedesc") t.inGameDesc = v.empty() ? "" : v[0];
        else if (key == "balls") t.balls = num();
        else if (key == "numorange") t.numOrange = num();
        else if (key == "lives") t.lives = num();
        else if (key == "scorereq") { t.scoreReq = num(); if (t.scoreReq > 0) t.scoreReset = false; }
        else if (key == "scoreceil") { t.scoreCeil = num(); if (t.scoreCeil > 0) t.scoreReset = false; }
        else if (key == "scorereset") t.scoreReset = flag();
        else if (key == "clearlevel") t.clearLevel = flag();
        else if (key == "levels") { for (auto& s : v) t.levels.push_back(trim(s)); }
        else if (key == "opponents") { for (auto& s : v) t.opponents.push_back(s); }
        else if (key == "opponentdifficulty") { for (auto& s : v) t.opponentDifficulty.push_back(std::atoi(s.c_str())); }
        else t.raw[f.key] = trim(f.label);
      }
      pg.trophies.push_back(std::move(t));
    }
    pages_.push_back(std::move(pg));
  }
  for (int p = 0; p < (int)pages_.size(); p++)
    for (int s = 0; s < (int)pages_[p].trophies.size(); s++) {
      auto& t = pages_[p].trophies[s];
      t.page = p;
      t.slot = s;
      if (!byId_.emplace(t.id, &t).second) cfgError = "Id already exists: " + std::to_string(t.id);
    }
  return !pages_.empty();
}

std::vector<const TrophyInfo*> TrophyMgr::forClearedLevel(const std::string& level) const {
  std::vector<const TrophyInfo*> out;
  for (auto& pg : pages_)
    for (auto& t : pg.trophies)
      if (t.clearLevel && t.levels.size() == 1 && lc(t.levels[0]) == lc(level)) out.push_back(&t);
  return out;
}

}
