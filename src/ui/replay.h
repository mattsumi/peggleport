#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "sim.h"

namespace pp::ui {

struct ReplayEvent {
  int tick = 0;
  uint8_t kind = 0;
  int a = 0, b = 0;
  int frame = 0;
};

struct ReplayData {
  int game = 0;
  std::string level, title, player;
  int64_t when = 0;
  int score = 0, shots = 0, startTick = 0;
  BoardConfig cfg;
  std::vector<ReplayEvent> log;
  bool save(const std::string& path) const;
  static bool load(const std::string& path, ReplayData& out);
};

struct ReplayFile {
  std::string path, name;
  ReplayData head;
};

std::vector<ReplayFile> listReplays(const std::string& dir, int game);
std::string replayExt(int game);

}
