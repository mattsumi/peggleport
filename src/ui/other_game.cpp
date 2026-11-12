#include "ui/other_game.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <vector>

#include "image.h"
#include "ui/app.h"

namespace pp::ui {

namespace {

struct PlayedLevel {
  Game game;
  int stage, level;
};

const size_t kRemembered = 10;
std::vector<PlayedLevel> playedLately;

bool wasPlayedLately(Game g, int stage, int level) {
  for (const PlayedLevel& p : playedLately)
    if (p.game == g && p.stage == stage && p.level == level) return true;
  return false;
}

}

void notePlayed(Game g, int stage, int level) {
  playedLately.push_back({g, stage, level});
  if (playedLately.size() > kRemembered) playedLately.erase(playedLately.begin());
}

bool challengesOpenFor(Game g, const PlayerInfo& p) { return g == Game::Nights ? p.maxStage >= 3 : p.advCompletions > 0; }

std::string challengesLockedText(Game g, size_t challenges) {
  return "^FFFF00^CHALLENGE MODE LOCKED!^oldclr^\nThis mode presents " + std::to_string(challenges) + " challenges for advanced players.\n\n^FFFF00^To Unlock:^oldclr^\n" +
         (g == Game::Nights ? "Finish Stage 3 of Adventure Mode!" : "Complete Adventure Mode!");
}

OtherGame::~OtherGame() {
  for (auto& [_, t] : pictures_)
    if (t.t) SDL_DestroyTexture(t.t);
}

bool OtherGame::load(const std::filesystem::path& gameDir, const std::filesystem::path& userDir, SDL_Renderer* r, std::string* err) {
  r_ = r;
  if (!vfs.mountGame(gameDir, err)) return false;
  art.load(vfs);
  if (!stages.load(vfs)) {
    if (err) *err = stages.cfgError;
    return false;
  }
  trophies.load(vfs);
  profiles.setDir(userDir);
  profiles.load();
  highScores.setTops([this](const std::string& key) { return stages.scoreListTop(key); });
  highScores.load(userDir / "highscores.dat");
  blank.reset(false);
  player = &blank;
  return true;
}

void OtherGame::choosePlayer(const std::string& name) {
  const PlayerInfo* p = name.empty() ? nullptr : profiles.get(name);
  player = p ? p : &blank;
}

Tex* OtherGame::picture(const std::string& idOrPath) {
  auto it = pictures_.find(idOrPath);
  if (it != pictures_.end()) return it->second.valid() ? &it->second : nullptr;
  Tex& T = pictures_[idOrPath];
  std::string path = idOrPath;
  if (const ResImage* ri = art.image(idOrPath)) {
    path = ri->path;
    T.cols = ri->cols;
    T.rows = ri->rows;
  }
  if (auto im = loadGameImage(vfs, path)) {
    T.w = im->w;
    T.h = im->h;
    T.scale = im->scale;
    T.path = path;
    if (r_) {
      T.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
      if (T.t) {
        SDL_UpdateTexture(T.t, nullptr, im->rgba.data(), im->w * 4);
        SDL_SetTextureBlendMode(T.t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(T.t, SDL_SCALEMODE_LINEAR);
      } else {
        T.w = 0;
      }
    }
  }
  return T.valid() ? &T : nullptr;
}

bool OtherGame::drawLevel(App& app, bool afterLevel, LevelDraw& out) const {
  if (!app.curProfile) return false;
  const uint32_t rnd = app.rand();
  const bool duel = app.gameMode == GameMode::Duel;
  auto choices = [&](bool leaveOutLately) {
    std::vector<LevelDraw> c;
    auto add = [&](bool theirs, const StageMgr& from, const PlayerInfo& who, Game g) {
      for (int s = 0; s < from.numStages(); s++) {
        const StageInfo* st = from.stage(s);
        for (int l = 0; st && l < (int)st->levels.size(); l++) {
          if (!theirs && afterLevel && s == app.curStage && l == app.curLevel) continue;
          if (!who.isLevelUnlocked(s, l)) continue;
          if (duel && st->levels[(size_t)l].excludeRand) continue;
          if (leaveOutLately && wasPlayedLately(g, s, l)) continue;
          c.push_back({theirs, s, l});
        }
      }
    };
    if (!duel || app.modeOpen(GameMode::Duel)) add(false, app.stages, *app.curProfile, app.game);
    if (!duel || duelOpen()) add(true, stages, *player, game);
    return c;
  };
  std::vector<LevelDraw> c = choices(afterLevel);
  if (c.empty() && afterLevel) {
    playedLately.clear();
    c = choices(false);
  }
  if (c.empty()) return false;
  out = c[rnd % c.size()];
  return true;
}

int OtherGame::lastOpenStage() const { return std::clamp(player->maxStage, 0, std::max(0, stages.numStages() - 1)); }

int OtherGame::nextStage(int stage) const {
  if (stage < lastOpenStage()) return stage + 1;
  if (stage < stages.numStages()) return stages.numAllStages() > stages.numStages() ? stages.numStages() : -1;
  return stage + 1 < stages.numAllStages() ? stage + 1 : -1;
}

int OtherGame::prevStage(int stage) const {
  if (stage > stages.numStages()) return stage - 1;
  if (stage == stages.numStages()) return lastOpenStage();
  return stage > 0 ? stage - 1 : -1;
}

int OtherGame::listStage(int stage) const {
  if (stage >= stages.numStages() && stage < stages.numAllStages()) return stage;
  return std::clamp(stage, 0, lastOpenStage());
}

}
