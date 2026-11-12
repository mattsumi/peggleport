#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "gamepaths.h"
#include "profile.h"
#include "ui/gfx.h"
#include "ui/res.h"
#include "ui/stages.h"
#include "vfs.h"

struct SDL_Renderer;

namespace pp::ui {

bool challengesOpenFor(Game g, const PlayerInfo& p);
std::string challengesLockedText(Game g, size_t challenges);

class App;

struct LevelDraw {
  bool theirs = false;
  int stage = -1, level = -1;
};

void notePlayed(Game g, int stage, int level);

struct OtherGame {
  explicit OtherGame(Game g) : game(g) {}
  OtherGame(const OtherGame&) = delete;
  OtherGame& operator=(const OtherGame&) = delete;
  ~OtherGame();

  bool load(const std::filesystem::path& gameDir, const std::filesystem::path& userDir, SDL_Renderer* r, std::string* err);
  void choosePlayer(const std::string& name);
  Tex* picture(const std::string& idOrPath);
  const char* title() const { return gameTitle(game); }
  int lastOpenStage() const;
  int nextStage(int stage) const;
  int prevStage(int stage) const;
  int listStage(int stage) const;
  bool duelOpen() const { return player->maxStage >= 1; }
  bool challengesOpen() const { return challengesOpenFor(game, *player); }
  bool drawLevel(App& app, bool afterLevel, LevelDraw& out) const;

  Game game;
  Vfs vfs;
  Manifest art;
  StageMgr stages;
  TrophyMgr trophies;
  ProfileMgr profiles{game};
  HighScoreMgr highScores{game};
  PlayerInfo blank;
  const PlayerInfo* player = &blank;
  std::set<int> newTrophies;

 private:
  SDL_Renderer* r_ = nullptr;
  std::map<std::string, Tex> pictures_;
};

}
