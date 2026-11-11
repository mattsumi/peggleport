#include "ui/other_game.h"

#include <SDL3/SDL.h>
#include <algorithm>

#include "image.h"

namespace pp::ui {

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
