#pragma once

struct lua_State;

namespace pp::mods {

struct LuaWork {
  static constexpr long long kBig = 1000000;
  long long remaining = 0;
  bool timed = false;
  bool aborted = false;
  unsigned tick = 0;
  lua_State* cur = nullptr;
  void (*check)(lua_State*, LuaWork*) = nullptr;
  void (*refuse)(lua_State*, LuaWork*, long long) = nullptr;
  bool (*over)(LuaWork*) = nullptr;
  void (*nudge)(LuaWork*) = nullptr;
};

inline thread_local LuaWork* gWork = nullptr;

inline void workSpend(lua_State* L, long long units) {
  LuaWork* w = gWork;
  if (!w || units <= 0) return;
  if (units > LuaWork::kBig) w->refuse(L, w, units);
  w->remaining -= units;
  if (w->remaining <= 0 || (w->timed && (units >= 2048 || (++w->tick & 63) == 0))) w->check(L, w);
}

inline bool workTick() {
  LuaWork* w = gWork;
  if (!w) return false;
  if (--w->remaining > 0 && !(w->timed && (++w->tick & 127) == 0)) return false;
  return w->over(w);
}

inline void workQuiet(long long units) {
  LuaWork* w = gWork;
  if (w) w->remaining -= units;
}

inline void workNudge(long long units) {
  LuaWork* w = gWork;
  if (!w) return;
  w->remaining -= units;
  if (w->remaining <= 0) w->nudge(w);
}

}
