#pragma once
#include <cstdint>

#include "physics.h"

namespace pp {

class Board;

struct SimHooks {
  virtual ~SimHooks() = default;
  virtual void levelStart(Board& b) = 0;
  virtual void stepBegin(Board& b) = 0;
  virtual void stepEnd(Board& b) = 0;
  virtual void event(Board& b, int type, int a, int bb, int c, int ball) = 0;
  virtual bool power(Board& b, int pegIdx, int power, bool silent) = 0;
  virtual void basePhysics(PhysicsParams&) {}
  virtual void pegCustom(Board& b, int pegIdx, int what, int ball, int custom) { (void)b; (void)pegIdx; (void)what; (void)ball; (void)custom; }
  virtual void masterCall(Board& b, int player, int power, int what, int pegIdx) {}
};

inline SimHooks* g_simHooks = nullptr;
namespace mods_ { inline SimHooks* hooks() { return g_simHooks; } }
inline uint64_t g_modSetHash = 0;
inline uint64_t g_modTypeSig = 0;

}
