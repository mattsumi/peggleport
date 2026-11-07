#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "log.h"
#include "physics.h"

namespace pp {
class Board;
}

namespace pp::mods {

enum class Ctx { None, Load, Sim, Draw, Ui };

struct ModCmd {
  enum K { AddScore, HitPeg, AddBalls, SetBallVel, Say, StylePoints, SetPhysics } k = Say;
  int i = 0;
  double a = 0, b = 0;
  std::string s;
  PhysicsEdit pe{};
};

struct DrawSink {
  virtual ~DrawSink() = default;
  virtual void rect(float x, float y, float w, float h, uint32_t rgba, bool fill, bool screen) = 0;
  virtual void circle(float cx, float cy, float r, uint32_t rgba, bool fill, bool screen) = 0;
  virtual void text(float x, float y, const std::string& s, uint32_t rgba, int align, float scale, bool screen) = 0;
  virtual void image(const std::string& path, float x, float y, float scale, float alpha, bool screen) = 0;
};

struct HostServices {
  std::function<void(const std::string& modId, pp::log::Level level, const std::string& line)> log;
  std::function<void(const std::string& modId, const std::string& text)> message;
  Board* board = nullptr;
  Ctx ctx = Ctx::None;
  DrawSink* sink = nullptr;
  std::function<void(const ModCmd&)> queue;
  std::function<std::string(const std::string& modId)> dataDir;
};

}
