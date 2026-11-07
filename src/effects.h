#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "vec2.h"

namespace pp {

struct FxText {
  int id = 0;
  std::vector<std::string> lines;
  int font = 0x2d;
  float x = 0, y = 0, vx = 0, vy = 0;
  float scale = 1, rot = 0, rotSpeed = 0;
  int alignX = 0, alignY = 0;
  uint32_t color = 0xffff00, shadow = 0;
  int life = 0, age = 0;
  int blink = 0, fadeIn = 0, fadeOut = 0;
  int layer = 1;
  int arrow = 0;
  int rotUntil = 0;
  int refObj = -1;
  float refX = 0, refY = 0;
  std::vector<std::pair<float, int>> scales;
  bool colorCodes = false;
};

struct FxEffect {
  int prio = 0;
  int type = 0;
  int age = 0, life = 100;
  float x = 0, y = 0;
  float vx = 0, vy = 0, ay = 0;
  float f28 = 0, f2c = 0;
  int i30 = 0, i34 = 0, i38 = 0, i3c = 0;
  bool b40 = false;
  float fa = 0, fb = 0, fc = 0;
};

struct FxSound {
  std::string id;
  float semis = 0, vol = 1;
  int throttle = 0;
};

struct FxState {
  std::vector<FxSound> sounds;
  void sound(const char* id, float semis = 0, float vol = 1, int throttle = 0) { sounds.push_back({id, semis, vol, throttle}); }
  std::vector<FxText> texts;
  std::vector<FxEffect> eff[5];
  int nextId = 0;
  uint32_t seen = 0;
  int lastTick = -1, lastFrame = -1;
  V2 lastHit{325.0f, 250.0f};
  std::map<int, float> trailAcc;
  int shake = 0;
  int frame = 0;
  int styleFrame = -100;
  float styleX = 0, styleY = 0;
  int styleStack = 0;
  bool rainbowWait = false;
  int rainbowAge = -1;
  int hintText = 0;
  int countText = 0, countCur = 0;
  uint32_t rng = 0x2545f491u;
  uint32_t rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng & 0x7fffffffu; }
  int rand(int n) { return n > 0 ? (int)(rand() % (uint32_t)n) : 0; }
  void clear() { texts.clear(); for (auto& e : eff) e.clear(); nextId = 0; hintText = 0; rainbowWait = false; rainbowAge = -1; countText = 0; countCur = 0; }
};

const char* fxFontName(int resId);

}
