#include "mods/luamod.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <chrono>
#include <filesystem>

#include "dmath.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "mods/luawork.h"
#include "sim.h"

namespace fs = std::filesystem;
namespace pp::mods {

constexpr int kStride = 1000;

struct LuaMod::Impl : LuaWork {
  LuaMod* mod = nullptr;
  size_t used = 0, cap = 0, peak = 0;
  bool denied = false;
  long long total = 0;
  bool metered = false;
  unsigned allocTick = 0;
  const char* note = nullptr;
  std::chrono::steady_clock::time_point deadline;
  Ctx ctx = Ctx::None;
  int shadowRef = LUA_NOREF;
  uint32_t presRng = 0x2545f491u;
  int errLogged = 0;
  bool errNoted = false;
  std::string lastErr;
  long repeats = 0;
  int logCount = 0;
  bool logNoted = false;
  std::chrono::steady_clock::time_point logWindow;
  bool screenMode = false;
  uint32_t clampWarned = 0;
};

namespace {
using Impl = LuaMod::Impl;

bool pastDeadline(Impl* I) { return std::chrono::steady_clock::now() > I->deadline; }
void stopWith(Impl* I, const char* note) {
  I->aborted = true;
  if (!I->note) I->note = note;
}
bool allocBudget(Impl* I, size_t nsize) {
  if (!I->aborted) {
    I->remaining -= 6 + (long long)(nsize >> 5);
    if (I->remaining <= 0) stopWith(I, "work budget exceeded");
    else if (I->timed && ((++I->allocTick & 31) == 0 || nsize >= 65536) && pastDeadline(I)) stopWith(I, "time limit exceeded");
  }
  return !I->aborted || nsize < 1024;
}
void* allocf(void* ud, void* ptr, size_t osize, size_t nsize) {
  Impl* I = (Impl*)ud;
  size_t old = ptr ? osize : 0;
  if (nsize == 0) {
    if (ptr) I->used -= old;
    std::free(ptr);
    return nullptr;
  }
  if (nsize > old && I->used + (nsize - old) > I->cap) { I->denied = true; return nullptr; }
  if (I->metered && nsize > old && !allocBudget(I, nsize)) return nullptr;
  void* p = std::realloc(ptr, nsize);
  if (!p) return nullptr;
  I->used = I->used - old + nsize;
  if (I->used > I->peak) I->peak = I->used;
  return p;
}
Impl* IM(lua_State* L) { void* ud; lua_getallocf(L, &ud); return (Impl*)ud; }
HostServices& HS(lua_State* L);

void emit(HostServices& h, const std::string& id, pp::log::Level level, const std::string& text) {
  if (h.log) h.log(id, level, text);
}
bool allowPrint(Impl* I, HostServices& h, const std::string& id) {
  auto now = std::chrono::steady_clock::now();
  if (now - I->logWindow >= std::chrono::seconds(1)) {
    I->logWindow = now;
    I->logCount = 0;
    I->logNoted = false;
  }
  if (I->logCount < 200) {
    I->logCount++;
    return true;
  }
  if (!I->logNoted) {
    I->logNoted = true;
    emit(h, id, pp::log::Level::Warn, "log output is limited to 200 lines per second; extra lines are dropped");
  }
  return false;
}
void reportError(Impl* I, HostServices& h, const std::string& id, const std::string& text) {
  if (text == I->lastErr) {
    I->repeats++;
    long n = I->repeats + 1;
    if (n == 10 || n == 100 || n == 1000 || n == 10000 || n == 100000)
      emit(h, id, pp::log::Level::Error, "the error above has now happened " + std::to_string(n) + " times");
    return;
  }
  I->lastErr = text;
  I->repeats = 0;
  if (I->errLogged < 20) {
    I->errLogged++;
    emit(h, id, pp::log::Level::Error, text);
    return;
  }
  if (!I->errNoted) {
    I->errNoted = true;
    emit(h, id, pp::log::Level::Warn, "too many errors from this mod; further errors are not logged");
  }
}

void countHook(lua_State* L, lua_Debug*) {
  Impl* I = IM(L);
  long long stride = lua_gethookcount(L);
  const bool pushed = stride != kStride;
  if (pushed) lua_sethook(L, countHook, LUA_MASKCOUNT, kStride);
  I->remaining -= stride;
  if (I->remaining <= 0) {
    const char* why = pushed ? "work budget exceeded" : "instruction budget exceeded";
    stopWith(I, why);
    luaL_error(L, "%s", why);
  }
  if (I->timed && pastDeadline(I)) { stopWith(I, "time limit exceeded"); luaL_error(L, "time limit exceeded"); }
}
void workNudge(LuaWork* w) {
  if (w->cur && lua_gethookcount(w->cur) != 1) lua_sethook(w->cur, countHook, LUA_MASKCOUNT, 1);
}
void workCheck(lua_State* L, LuaWork* w) {
  Impl* I = static_cast<Impl*>(w);
  if (I->remaining <= 0) { stopWith(I, "work budget exceeded"); luaL_error(L, "work budget exceeded"); }
  if (I->timed && pastDeadline(I)) { stopWith(I, "time limit exceeded"); luaL_error(L, "time limit exceeded"); }
}
void workRefuse(lua_State* L, LuaWork*, long long) {
  luaL_error(L, "operation too large for the sandbox (split the work into smaller steps)");
}
bool workOver(LuaWork* w) {
  Impl* I = static_cast<Impl*>(w);
  if (I->remaining <= 0) { stopWith(I, "work budget exceeded"); return true; }
  if (I->timed && pastDeadline(I)) { stopWith(I, "time limit exceeded"); return true; }
  return false;
}

}

HostServices& LuaModHost(lua_State* L);

namespace {
HostServices& HS(lua_State* L) { return LuaModHost(L); }
Board& needBoard(lua_State* L) {
  Board* b = HS(L).board;
  if (!b) luaL_error(L, "no board in this context");
  return *b;
}
Board& needSim(lua_State* L) {
  Impl* I = IM(L);
  if (I->ctx != Ctx::Sim || !HS(L).board || I->mod->manifest().cosmetic)
    luaL_error(L, "sim mutators and state.set can only be used from gameplay hooks (on_*) of a non-cosmetic mod");
  return *HS(L).board;
}
const char* pegTypeName(int t) {
  static const char* n[] = {"none", "blue", "orange", "purple", "green"};
  return t >= 0 && t < 5 ? n[t] : "none";
}
const char* shapeName(Shape s) {
  switch (s) {
    case Shape::Circle: return "circle";
    case Shape::Line: return "line";
    case Shape::Poly: return "poly";
    case Shape::Brick: return "brick";
    case Shape::Hole: return "hole";
    default: return "none";
  }
}
int playerArg(lua_State* L, int idx, const Board& b) {
  int p = (int)luaL_optinteger(L, idx, b.cur);
  if (p < 0 || p > 3) luaL_error(L, "player must be 0..3");
  return p;
}

int l_log(lua_State* L) {
  Impl* I = IM(L);
  std::string line;
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    size_t len;
    const char* s = luaL_tolstring(L, i, &len);
    workSpend(L, 2);
    if (i > 1) line += '\t';
    line.append(s, std::min<size_t>(len, 400));
    lua_pop(L, 1);
  }
  if (allowPrint(I, HS(L), I->mod->manifest().id)) emit(HS(L), I->mod->manifest().id, pp::log::Level::Info, line.substr(0, 800));
  return 0;
}
int l_tostring(lua_State* L) {
  luaL_checkany(L, 1);
  int t = lua_type(L, 1);
  if (t == LUA_TTABLE || t == LUA_TFUNCTION || t == LUA_TTHREAD || t == LUA_TUSERDATA) {
    if (luaL_getmetafield(L, 1, "__tostring") == LUA_TNIL) {
      lua_pushstring(L, lua_typename(L, t));
      return 1;
    }
    lua_pop(L, 1);
  }
  luaL_tolstring(L, 1, nullptr);
  return 1;
}
constexpr long long kErrorCost = 1500;
void chargeParse(lua_State* L, size_t n) {
  long long units = 40 + (long long)(n + (n >> 1));
  if (units > LuaWork::kBig) luaL_error(L, "load: too large for the sandbox (split the work into smaller steps)");
  workSpend(L, units);
}
int l_load(lua_State* L) {
  size_t n;
  if (lua_type(L, 1) != LUA_TSTRING) luaL_error(L, "load: only string chunks are allowed in the sandbox");
  const char* s = lua_tolstring(L, 1, &n);
  chargeParse(L, n);
  const char* name = luaL_optstring(L, 2, s);
  int st = luaL_loadbufferx(L, s, n, name, "t");
  if (st != LUA_OK) workSpend(L, kErrorCost);
  if (st == LUA_OK) {
    if (!lua_isnone(L, 4)) {
      lua_pushvalue(L, 4);
      if (!lua_setupvalue(L, -2, 1)) lua_pop(L, 1);
    }
    return 1;
  }
  lua_pushnil(L);
  lua_insert(L, -2);
  return 2;
}
int l_burn(lua_State* L) {
  Impl* I = IM(L);
  I->remaining -= luaL_checkinteger(L, 1);
  const char* why = I->remaining <= 0 ? "work budget exceeded" : (I->timed && pastDeadline(I)) ? "time limit exceeded" : nullptr;
  if (why) {
    stopWith(I, why);
    luaL_where(L, 2);
    lua_pushstring(L, why);
    lua_concat(L, 2);
    return lua_error(L);
  }
  return 0;
}
void warnCharge(void*, const char*, int tocont) {
  if (!tocont) workQuiet(kErrorCost);
}
int l_aborted(lua_State* L) {
  lua_pushboolean(L, IM(L)->aborted);
  return 1;
}
int l_require(lua_State* L) {
  Impl* I = IM(L);
  std::string name = luaL_checkstring(L, 1);
  lua_getfield(L, LUA_REGISTRYINDEX, "pp_loaded");
  lua_getfield(L, -1, name.c_str());
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 1);
  std::string rel;
  for (char c : name) {
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '/' || c == '-')) luaL_error(L, "require: bad module name '%s'", name.c_str());
    rel += c == '.' ? '/' : c;
  }
  if (name.find("..") != std::string::npos || !safeRelPath(rel)) luaL_error(L, "require: bad module name '%s'", name.c_str());
  std::string text;
  if (!readFile(I->mod->dir() / (rel + ".lua"), text, 4u << 20)) luaL_error(L, "module '%s' not found in the mod folder", name.c_str());
  workSpend(L, (long long)(text.size() >> 3));
  if (luaL_loadbufferx(L, text.data(), text.size(), ("@" + I->mod->manifest().id + "/" + rel + ".lua").c_str(), "t") != LUA_OK) lua_error(L);
  lua_pushstring(L, name.c_str());
  lua_call(L, 1, 1);
  if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, name.c_str());
  return 1;
}
int l_global_guard(lua_State* L) {
  Impl* I = IM(L);
  if (I->ctx == Ctx::Sim) {
    luaL_error(L, "cannot assign global '%s' inside a gameplay hook: keep per-match state in state.set(); globals are frozen while the simulation runs (see the modding guide in the FAQ)",
               lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : "?");
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, I->shadowRef);
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_rawset(L, -3);
  return 0;
}
int l_patguard(lua_State* L) {
  size_t sl = 0, pl = 0;
  luaL_checklstring(L, 1, &sl);
  const char* p = luaL_checklstring(L, 2, &pl);
  int q = 0;
  bool heavy = false;
  for (size_t i = 0; i < pl; i++) {
    if (p[i] == '%') {
      i++;
      if (i < pl && (p[i] == 'b' || (p[i] >= '1' && p[i] <= '9'))) heavy = true;
      continue;
    }
    if (p[i] == '*' || p[i] == '+' || p[i] == '-' || p[i] == '?') q++;
  }
  double cost = std::pow((double)std::max<size_t>(sl, 2), q + 1);
  double units = (double)(sl >> 5) + (double)(pl >> 2) + 1.0 + (double)sl * (double)pl / 64.0;
  if (heavy) units += (double)sl * (double)sl / 64.0;
  if (sl > (1u << 20) || cost > 3e8 || units > (double)LuaWork::kBig)
    luaL_error(L, "pattern too expensive for the sandbox (subject %d bytes, %d repetitions): split the work", (int)sl, q);
  workSpend(L, (long long)units);
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}
int presentationNext(Impl* I) {
  uint32_t x = I->presRng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  I->presRng = x;
  return (int)(x & 0x7fffffffu);
}
int nextRand(lua_State* L) {
  Impl* I = IM(L);
  if (I->ctx == Ctx::Sim && HS(L).board) return (int)HS(L).board->rng.rand();
  return presentationNext(I);
}
int l_rng_next(lua_State* L) { lua_pushinteger(L, nextRand(L)); return 1; }
int l_rng_range(lua_State* L) {
  lua_Integer lo = luaL_checkinteger(L, 1), hi = luaL_checkinteger(L, 2);
  if (hi < lo) luaL_error(L, "rng_range: empty interval");
  lua_Unsigned span = (lua_Unsigned)hi - (lua_Unsigned)lo + 1;
  if (span == 0 || span > 0x7fffffffu) luaL_error(L, "rng_range: interval too large");
  lua_pushinteger(L, lo + (lua_Integer)((lua_Unsigned)nextRand(L) % span));
  return 1;
}
int l_math_random(lua_State* L) {
  int n = lua_gettop(L);
  if (n == 0) { lua_pushnumber(L, nextRand(L) / 2147483648.0); return 1; }
  lua_Integer lo = 1, hi;
  if (n == 1) hi = luaL_checkinteger(L, 1);
  else { lo = luaL_checkinteger(L, 1); hi = luaL_checkinteger(L, 2); }
  if (hi < lo) luaL_error(L, "math.random: empty interval");
  lua_Unsigned span = (lua_Unsigned)hi - (lua_Unsigned)lo + 1;
  if (span == 0 || span > 0x7fffffffu) luaL_error(L, "math.random: interval too large");
  lua_pushinteger(L, lo + (lua_Integer)((lua_Unsigned)nextRand(L) % span));
  return 1;
}
int l_math_sin(lua_State* L) { lua_pushnumber(L, dm::sin(luaL_checknumber(L, 1))); return 1; }
int l_math_cos(lua_State* L) { lua_pushnumber(L, dm::cos(luaL_checknumber(L, 1))); return 1; }
int l_math_atan(lua_State* L) { lua_pushnumber(L, dm::atan2(luaL_checknumber(L, 1), luaL_optnumber(L, 2, 1.0))); return 1; }

void pushPeg(lua_State* L, const Board& b, int i) {
  const Obj& o = b.objs[(size_t)i];
  lua_createtable(L, 0, 12);
  if (const int li = b.levelIndexOf(i); li >= 0) { lua_pushinteger(L, li); lua_setfield(L, -2, "index"); }
  if (o.custom) {
    if (const PegTypeDef* d = pegTypeDef(o.custom)) {
      const std::string key = d->mod == IM(L)->mod->manifest().id ? d->id : d->key();
      lua_pushstring(L, key.c_str()); lua_setfield(L, -2, "custom");
      lua_pushinteger(L, o.hits); lua_setfield(L, -2, "hits");
    }
  }
  lua_pushinteger(L, i); lua_setfield(L, -2, "id");
  lua_pushnumber(L, o.pos.x); lua_setfield(L, -2, "x");
  lua_pushnumber(L, o.pos.y); lua_setfield(L, -2, "y");
  lua_pushstring(L, pegTypeName(o.pegType)); lua_setfield(L, -2, "type");
  lua_pushboolean(L, o.lit); lua_setfield(L, -2, "lit");
  lua_pushboolean(L, o.gone); lua_setfield(L, -2, "gone");
  lua_pushstring(L, shapeName(o.shape)); lua_setfield(L, -2, "shape");
  lua_pushnumber(L, o.radius); lua_setfield(L, -2, "radius");
  lua_pushnumber(L, o.rot); lua_setfield(L, -2, "rot");
}
int b_tick(lua_State* L) { lua_pushinteger(L, needBoard(L).tick); return 1; }
int b_score(lua_State* L) { Board& b = needBoard(L); lua_pushinteger(L, b.liveScore(playerArg(L, 1, b))); return 1; }
int b_orange_left(lua_State* L) { lua_pushinteger(L, needBoard(L).orangeLeft); return 1; }
int b_orange_total(lua_State* L) { lua_pushinteger(L, needBoard(L).orangeTotal); return 1; }
int b_balls_left(lua_State* L) { Board& b = needBoard(L); lua_pushinteger(L, b.ballsLeft[playerArg(L, 1, b)]); return 1; }
int b_shot_score(lua_State* L) { lua_pushinteger(L, needBoard(L).shotScore); return 1; }
int b_player(lua_State* L) { lua_pushinteger(L, needBoard(L).cur); return 1; }
int b_fever(lua_State* L) { lua_pushboolean(L, needBoard(L).fever); return 1; }
int b_width(lua_State* L) { lua_pushinteger(L, needBoard(L).W); return 1; }
int b_master(lua_State* L) { Board& b = needBoard(L); lua_pushinteger(L, b.master[playerArg(L, 1, b)]); return 1; }
int b_phase(lua_State* L) {
  static const char* n[] = {"aim", "shot", "popping", "over"};
  lua_pushstring(L, n[std::min(3, (int)needBoard(L).phase)]);
  return 1;
}
int b_peg(lua_State* L) {
  Board& b = needBoard(L);
  lua_Integer i = luaL_checkinteger(L, 1);
  if (i < 0 || i >= (lua_Integer)b.objs.size()) { lua_pushnil(L); return 1; }
  pushPeg(L, b, (int)i);
  return 1;
}
int b_pegs(lua_State* L) {
  Board& b = needBoard(L);
  lua_newtable(L);
  int n = 0;
  for (int i = 0; i < (int)b.objs.size(); i++) {
    const Obj& o = b.objs[(size_t)i];
    if (o.pegType <= 0 || o.gone) continue;
    pushPeg(L, b, i);
    lua_rawseti(L, -2, ++n);
  }
  return 1;
}
int b_peg_count(lua_State* L) {
  int n = 0;
  for (auto& o : needBoard(L).objs) if (o.pegType > 0 && !o.gone) n++;
  lua_pushinteger(L, n);
  return 1;
}
int b_bricks(lua_State* L) {
  Board& b = needBoard(L);
  lua_newtable(L);
  int n = 0;
  for (int i = 0; i < (int)b.objs.size(); i++) {
    const Obj& o = b.objs[(size_t)i];
    if ((o.shape != Shape::Brick && o.shape != Shape::Poly) || o.gone) continue;
    pushPeg(L, b, i);
    lua_rawseti(L, -2, ++n);
  }
  return 1;
}
int b_balls(lua_State* L) {
  Board& b = needBoard(L);
  lua_newtable(L);
  for (int i = 0; i < (int)b.balls.size(); i++) {
    const Ball& bl = b.balls[(size_t)i];
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, i); lua_setfield(L, -2, "id");
    lua_pushnumber(L, bl.pos.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, bl.pos.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, bl.vel.x); lua_setfield(L, -2, "vx");
    lua_pushnumber(L, bl.vel.y); lua_setfield(L, -2, "vy");
    lua_pushnumber(L, bl.r); lua_setfield(L, -2, "r");
    lua_pushboolean(L, bl.fire); lua_setfield(L, -2, "fire");
    lua_pushboolean(L, bl.alive); lua_setfield(L, -2, "alive");
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}
int b_ball_count(lua_State* L) { lua_pushinteger(L, (lua_Integer)needBoard(L).balls.size()); return 1; }
void queueCmd(lua_State* L, ModCmd c) {
  needSim(L);
  if (!HS(L).queue) luaL_error(L, "no command buffer");
  HS(L).queue(c);
}
int m_add_score(lua_State* L) { lua_Integer n = luaL_checkinteger(L, 1); queueCmd(L, {ModCmd::AddScore, (int)std::clamp<lua_Integer>(n, -1000000, 1000000), 0, 0, ""}); return 0; }
int m_hit_peg(lua_State* L) { queueCmd(L, {ModCmd::HitPeg, (int)luaL_checkinteger(L, 1), 0, 0, ""}); return 0; }
int m_add_balls(lua_State* L) { queueCmd(L, {ModCmd::AddBalls, (int)std::clamp<lua_Integer>(luaL_checkinteger(L, 1), -99, 99), 0, 0, ""}); return 0; }
int m_set_ball_velocity(lua_State* L) { queueCmd(L, {ModCmd::SetBallVel, (int)luaL_checkinteger(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), ""}); return 0; }
std::string qualifyPegType(lua_State* L, const std::string& t) {
  if (t.find(':') != std::string::npos) return t;
  Impl* I = IM(L);
  for (auto& r : I->mod->pegTypes)
    if (r.def.id == t) return r.def.mod + ":" + t;
  return t;
}
int m_set_peg_type(lua_State* L) {
  lua_Integer id = luaL_checkinteger(L, 1);
  std::string t = luaL_checkstring(L, 2);
  if (id < 0 || id > 100000) luaL_error(L, "set_peg_type: bad peg id");
  queueCmd(L, {ModCmd::SetPegType, (int)id, 0, 0, qualifyPegType(L, t.substr(0, 80))});
  return 0;
}
int m_set_peg_types(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  std::vector<std::pair<int, std::string>> list;
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    if (lua_isinteger(L, -2) && lua_type(L, -1) == LUA_TSTRING) {
      lua_Integer id = lua_tointeger(L, -2);
      if (id >= 0 && id <= 100000) list.push_back({(int)id, std::string(lua_tostring(L, -1)).substr(0, 80)});
    }
    lua_pop(L, 1);
    if (list.size() > 4000) luaL_error(L, "set_peg_types: too many entries");
  }
  std::sort(list.begin(), list.end());
  for (auto& [id, t] : list) queueCmd(L, {ModCmd::SetPegType, id, 0, 0, qualifyPegType(L, t)});
  return 0;
}
int m_say(lua_State* L) { queueCmd(L, {ModCmd::Say, 0, 0, 0, std::string(luaL_checkstring(L, 1)).substr(0, 60)}); return 0; }
int m_style_shot(lua_State* L) {
  std::string nm = std::string(luaL_checkstring(L, 1)).substr(0, 40);
  lua_Integer pts = luaL_checkinteger(L, 2);
  queueCmd(L, {ModCmd::StylePoints, (int)std::clamp<lua_Integer>(pts, -1000000, 1000000), 0, 0, nm});
  return 0;
}
PhysicsEdit readPhysics(lua_State* L, int idx, uint32_t& clamped, double* raw) {
  PhysicsEdit e;
  clamped = 0;
  lua_pushnil(L);
  while (lua_next(L, idx)) {
    if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "physics.set: the field names must be words such as gravity_y");
    const char* name = lua_tostring(L, -2);
    int k = 0;
    if (!physicsKeyByName(name, k))
      luaL_error(L, "physics.set: unknown field '%s' (use gravity_y, gravity_x, launch_speed, ball_bounce, peg_bounce, drag, max_speed or ball_radius)", name);
    if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "physics.set: '%s' must be a number", name);
    double v = lua_tonumber(L, -1);
    if (!std::isfinite(v)) luaL_error(L, "physics.set: '%s' must be a finite number", name);
    if (e.has(k)) luaL_error(L, "physics.set: '%s' is given twice (gravity and gravity_y are the same setting)", name);
    if (physicsClamp(k, v) != (float)v) { clamped |= 1u << k; if (raw) raw[k] = v; }
    e.set(k, v);
    lua_pop(L, 1);
  }
  return e;
}
int p_set(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  Impl* I = IM(L);
  if (I->mod->manifest().cosmetic) luaL_error(L, "physics.set cannot be used by a cosmetic mod, because it changes the game");
  const bool loading = I->ctx == Ctx::Load;
  const bool sim = I->ctx == Ctx::Sim && HS(L).board;
  if (!loading && !sim) luaL_error(L, "physics.set can only be used while the mod loads (top of a script or on_load) or from gameplay hooks (on_*)");
  uint32_t clamped = 0;
  double raw[PhysicsParams::KeyCount] = {};
  PhysicsEdit e = readPhysics(L, 1, clamped, raw);
  for (int k = 0; k < PhysicsParams::KeyCount; k++)
    if (((clamped & ~I->clampWarned) >> k) & 1u) {
      I->clampWarned |= 1u << k;
      char buf[200];
      std::snprintf(buf, sizeof buf, "physics.set: %s %g is outside %g to %g, using %g", physicsKey(k).name, raw[k], (double)physicsKey(k).lo, (double)physicsKey(k).hi, (double)e.v.at(k));
      emit(HS(L), I->mod->manifest().id, pp::log::Level::Warn, buf);
    }
  if (loading) {
    I->mod->loadPhysics.merge(e);
    return 0;
  }
  if (!HS(L).queue) luaL_error(L, "no command buffer");
  ModCmd c;
  c.k = ModCmd::SetPhysics;
  c.pe = e;
  HS(L).queue(c);
  return 0;
}
int p_get(lua_State* L) {
  Impl* I = IM(L);
  PhysicsParams p;
  if (I->ctx == Ctx::Load) I->mod->loadPhysics.applyTo(p);
  else p = needBoard(L).physics;
  lua_createtable(L, 0, PhysicsParams::KeyCount);
  for (int k = 0; k < PhysicsParams::KeyCount; k++) {
    const double v = std::round((double)p.at(k) * 1e6) / 1e6;
    if (v == std::floor(v)) lua_pushinteger(L, (lua_Integer)v);
    else lua_pushnumber(L, v);
    lua_setfield(L, -2, physicsKey(k).name);
  }
  return 1;
}
void pushNum(lua_State* L, double v) {
  if (v == std::floor(v) && std::fabs(v) < 9e15) lua_pushinteger(L, (lua_Integer)v);
  else lua_pushnumber(L, v);
}
int s_get(lua_State* L) {
  Board& b = needBoard(L);
  auto it = b.modState.find(luaL_checkstring(L, 1));
  if (it == b.modState.end()) { if (lua_isnone(L, 2)) lua_pushnil(L); else lua_pushvalue(L, 2); }
  else pushNum(L, it->second);
  return 1;
}
int s_set(lua_State* L) {
  Board& b = needSim(L);
  std::string k = luaL_checkstring(L, 1);
  if (k.size() > 48) luaL_error(L, "state key too long");
  if (lua_isnil(L, 2)) { b.modState.erase(k); return 0; }
  double v = luaL_checknumber(L, 2);
  if (!std::isfinite(v)) luaL_error(L, "state values must be finite numbers");
  if (b.modState.size() >= 256 && !b.modState.count(k)) luaL_error(L, "too many state keys (256)");
  b.modState[k] = v;
  return 0;
}
int s_add(lua_State* L) {
  Board& b = needSim(L);
  std::string k = luaL_checkstring(L, 1);
  if (b.modState.size() >= 256 && !b.modState.count(k)) luaL_error(L, "too many state keys (256)");
  b.modState[k] += luaL_checknumber(L, 2);
  pushNum(L, b.modState[k]);
  return 1;
}
int u_menu(lua_State* L) {
  luaL_checktype(L, 2, LUA_TFUNCTION);
  MenuEntry e;
  e.label = std::string(luaL_checkstring(L, 1)).substr(0, 60);
  lua_pushvalue(L, 2);
  e.ref = luaL_ref(L, LUA_REGISTRYINDEX);
  IM(L)->mod->menus.push_back(e);
  return 0;
}
int u_message(lua_State* L) {
  Impl* I = IM(L);
  if (HS(L).message) HS(L).message(I->mod->manifest().id, std::string(luaL_checkstring(L, 1)).substr(0, 200));
  return 0;
}
DrawSink& needDraw(lua_State* L) {
  Impl* I = IM(L);
  if (I->ctx != Ctx::Draw || !HS(L).sink) luaL_error(L, "draw.* can only be used from on_frame");
  return *HS(L).sink;
}
uint32_t colorArg(lua_State* L, int i) { return (uint32_t)(lua_Unsigned)luaL_optinteger(L, i, (lua_Integer)0xffffffffu); }
int d_rect(lua_State* L) {
  needDraw(L).rect((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4), colorArg(L, 5),
                   lua_isnone(L, 6) ? true : lua_toboolean(L, 6), IM(L)->screenMode);
  return 0;
}
int d_circle(lua_State* L) {
  needDraw(L).circle((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), colorArg(L, 4), lua_isnone(L, 5) ? true : lua_toboolean(L, 5),
                     IM(L)->screenMode);
  return 0;
}
int d_text(lua_State* L) {
  needDraw(L).text((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), luaL_checkstring(L, 3), colorArg(L, 4), (int)luaL_optinteger(L, 5, 0),
                   (float)luaL_optnumber(L, 6, 1.0), IM(L)->screenMode);
  return 0;
}
int d_image(lua_State* L) {
  needDraw(L).image(luaL_checkstring(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_optnumber(L, 4, 1.0), (float)luaL_optnumber(L, 5, 1.0),
                    IM(L)->screenMode);
  return 0;
}
int d_screen(lua_State* L) { IM(L)->screenMode = lua_toboolean(L, 1); return 0; }
int d_rgb(lua_State* L) {
  auto c = [&](int i, int d) { return (uint32_t)std::clamp<lua_Integer>(luaL_optinteger(L, i, d), 0, 255); };
  lua_pushinteger(L, (lua_Integer)((c(1, 255) << 24) | (c(2, 255) << 16) | (c(3, 255) << 8) | c(4, 255)));
  return 1;
}
int d_hsv(lua_State* L) {
  double h = luaL_checknumber(L, 1), s = luaL_optnumber(L, 2, 1.0), v = luaL_optnumber(L, 3, 1.0);
  h = (h - std::floor(h)) * 6.0;
  int i = (int)h;
  double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  double r, g, b;
  switch (i % 6) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                   case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; break; }
  uint32_t a = (uint32_t)std::clamp<lua_Integer>(luaL_optinteger(L, 4, 255), 0, 255);
  lua_pushinteger(L, (lua_Integer)(((uint32_t)(r * 255 + 0.5) << 24) | ((uint32_t)(g * 255 + 0.5) << 16) | ((uint32_t)(b * 255 + 0.5) << 8) | a));
  return 1;
}
int l_register_power(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  PowerDef p;
  lua_getfield(L, 1, "name"); p.name = luaL_optstring(L, -1, "Mod power"); lua_pop(L, 1);
  lua_getfield(L, 1, "description"); p.description = luaL_optstring(L, -1, ""); lua_pop(L, 1);
  lua_getfield(L, 1, "replaces");
  std::string rep = luaL_optstring(L, -1, "");
  lua_pop(L, 1);
  auto norm = [](std::string x) {
    std::string o;
    for (char c : x) if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
    return o;
  };
  for (int i = 1; i <= 14; i++) {
    std::string a = norm(Board::powerName(i)), r = norm(rep);
    if (!r.empty() && (a == r || (r.size() >= 4 && a.find(r) != std::string::npos))) p.replaces = i;
  }
  if (!p.replaces) {
    int m = Board::masterByName(rep);
    p.replaces = m;
  }
  if (p.replaces < 1 || p.replaces > 14) luaL_error(L, "register_power: 'replaces' must name a built-in power (e.g. \"Fireball\", \"Flower Power\", or a Master name)");
  lua_getfield(L, 1, "mode");
  p.replace = std::string(luaL_optstring(L, -1, "replace")) != "extra";
  lua_pop(L, 1);
  lua_getfield(L, 1, "on_activate");
  if (!lua_isfunction(L, -1)) luaL_error(L, "register_power: on_activate must be a function");
  p.ref = luaL_ref(L, LUA_REGISTRYINDEX);
  IM(L)->mod->powers.push_back(p);
  return 0;
}

int l_register_peg_type(lua_State* L) {
  Impl* I = IM(L);
  if (I->ctx != Ctx::Load) luaL_error(L, "register_peg_type can only be used while the mod loads (at the top of a script or in on_load)");
  if (I->mod->manifest().cosmetic) luaL_error(L, "a cosmetic mod cannot define peg types");
  luaL_checktype(L, 1, LUA_TTABLE);
  if (I->mod->pegTypes.size() >= 64) luaL_error(L, "register_peg_type: a mod can define up to 64 peg types");
  PegTypeReg r;
  PegTypeDef& d = r.def;
  d.mod = I->mod->manifest().id;
  auto field = [&](const char* k) { lua_getfield(L, 1, k); return lua_type(L, -1); };
  auto text = [&](const char* k, std::string& out, size_t max, bool path) {
    if (field(k) != LUA_TNIL) {
      if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "register_peg_type: '%s' must be a string", k);
      out = lua_tostring(L, -1);
      if (out.size() > max) luaL_error(L, "register_peg_type: '%s' is too long", k);
      if (path && !safeRelPath(out)) luaL_error(L, "register_peg_type: '%s' must be a path inside the mod folder", k);
    }
    lua_pop(L, 1);
  };
  auto number = [&](const char* k, double& out) {
    bool set = false;
    if (field(k) != LUA_TNIL) {
      if (!lua_isnumber(L, -1)) luaL_error(L, "register_peg_type: '%s' must be a number", k);
      out = lua_tonumber(L, -1);
      if (!std::isfinite(out)) luaL_error(L, "register_peg_type: '%s' must be a finite number", k);
      set = true;
    }
    lua_pop(L, 1);
    return set;
  };
  text("id", d.id, 32, false);
  if (d.id.empty()) luaL_error(L, "register_peg_type: 'id' is required");
  for (char c : d.id)
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) luaL_error(L, "register_peg_type: 'id' may only use letters, digits, '_' and '-'");
  for (auto& o : I->mod->pegTypes)
    if (o.def.id == d.id) luaL_error(L, "register_peg_type: the id '%s' is already used by this mod", d.id.c_str());
  d.name = d.id;
  text("name", d.name, 60, false);
  text("image", d.image, 200, true);
  text("lit_image", d.litImage, 200, true);
  if (field("stages") != LUA_TNIL) {
    if (!lua_istable(L, -1)) luaL_error(L, "register_peg_type: 'stages' must be a list of image paths");
    for (lua_Integer i = 1; i <= 16; i++) {
      lua_rawgeti(L, -1, i);
      if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
      if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "register_peg_type: 'stages' must be a list of image paths");
      std::string sp = lua_tostring(L, -1);
      if (sp.size() > 200 || !safeRelPath(sp)) luaL_error(L, "register_peg_type: every stage must be a path inside the mod folder");
      d.stages.push_back(sp);
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  double v = 0;
  if (number("color", v)) { d.color = (uint32_t)((lua_Unsigned)(lua_Integer)v & 0xffffffu); d.hasColor = true; }
  if (field("base") != LUA_TNIL) {
    if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "register_peg_type: 'base' must be \"blue\", \"orange\", \"purple\" or \"green\"");
    std::string b = lua_tostring(L, -1);
    if (b == "blue") d.base = 1;
    else if (b == "orange") d.base = 2;
    else if (b == "purple") d.base = 3;
    else if (b == "green") d.base = 4;
    else luaL_error(L, "register_peg_type: 'base' must be \"blue\", \"orange\", \"purple\" or \"green\"");
  }
  lua_pop(L, 1);
  if (number("score", v)) d.score = (int)std::clamp(v, 0.0, 1000000.0);
  if (field("counts_as_orange") != LUA_TNIL) d.orange = lua_toboolean(L, -1) ? 1 : 0;
  lua_pop(L, 1);
  if (number("hits_to_clear", v)) d.hits = (int)std::clamp(v, 1.0, 100.0);
  if (field("clears_on_hit") != LUA_TNIL) d.clears = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (number("bounce", v)) d.bounce = (float)std::clamp(v, 0.1, 3.0);
  if (number("radius_scale", v)) d.radiusScale = (float)std::clamp(v, 0.5, 3.0);
  if (!d.clears && d.orange == 1) {
    emit(HS(L), d.mod, pp::log::Level::Warn, "peg type '" + d.id + "' never clears, so it cannot count as an orange peg; counts_as_orange is ignored");
    d.orange = 0;
  }
  if (!d.clears) d.hits = 1;
  for (const char* k : {"on_hit", "on_clear"}) {
    int t = field(k);
    if (t == LUA_TNIL) { lua_pop(L, 1); continue; }
    if (t != LUA_TFUNCTION) luaL_error(L, "register_peg_type: '%s' must be a function", k);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (k[3] == 'h') { r.hitRef = ref; d.onHit = true; }
    else { r.clearRef = ref; d.onClear = true; }
  }
  I->mod->pegTypes.push_back(r);
  return 0;
}
std::string clipText(const std::string& s, size_t n) {
  if (s.size() <= n) return s;
  size_t k = n;
  while (k > 0 && ((unsigned char)s[k] & 0xc0) == 0x80) k--;
  return s.substr(0, k);
}
bool optStringField(lua_State* L, const char* key, std::string& out, size_t maxLen) {
  lua_getfield(L, 1, key);
  bool had = false;
  if (lua_type(L, -1) == LUA_TSTRING) {
    out = clipText(lua_tostring(L, -1), maxLen);
    had = true;
  } else if (!lua_isnil(L, -1)) {
    luaL_error(L, "register_master: '%s' must be a string", key);
  }
  lua_pop(L, 1);
  return had;
}
int optFunctionField(lua_State* L, const char* key) {
  lua_getfield(L, 1, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return -1;
  }
  if (!lua_isfunction(L, -1)) luaL_error(L, "register_master: '%s' must be a function", key);
  return luaL_ref(L, LUA_REGISTRYINDEX);
}
int l_register_master(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  Impl* I = IM(L);
  if (I->ctx != Ctx::Load) luaL_error(L, "register_master can only be used while the mod loads (at the top of a script or in on_load)");
  if (I->mod->manifest().cosmetic) luaL_error(L, "register_master needs a gameplay mod: a cosmetic mod cannot add Masters");
  MasterDef d;
  std::string id;
  if (!optStringField(L, "id", id, 64) || id.empty()) luaL_error(L, "register_master: 'id' is required");
  for (char& c : id) {
    c = (char)std::tolower((unsigned char)c);
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) luaL_error(L, "register_master: 'id' may only hold letters, digits, - and _");
  }
  if (id.size() > 32) luaL_error(L, "register_master: 'id' is longer than 32 characters");
  if (pp::masters::reservedName(id)) luaL_error(L, "register_master: '%s' is the name of a built-in Master, pick another id", id.c_str());
  for (auto& m : I->mod->masters)
    if (m.id == id) luaL_error(L, "register_master: this mod already has a Master with the id '%s'", id.c_str());
  if (I->mod->masters.size() >= 16) luaL_error(L, "register_master: one mod can add at most 16 Masters");
  d.id = id;
  d.name = id;
  optStringField(L, "name", d.name, 40);
  if (d.name.empty()) d.name = id;
  optStringField(L, "description", d.description, 240);
  d.powerName = "Master Power";
  optStringField(L, "power_name", d.powerName, 40);
  optStringField(L, "power_description", d.powerDescription, 240);
  optStringField(L, "portrait", d.portrait, 200);
  d.banner = d.powerName + "!";
  optStringField(L, "banner", d.banner, 40);
  lua_getfield(L, 1, "turns");
  if (!lua_isnil(L, -1)) {
    if (!lua_isinteger(L, -1)) luaL_error(L, "register_master: 'turns' must be a whole number from 1 to 99");
    lua_Integer t = lua_tointeger(L, -1);
    if (t < 1 || t > 99) luaL_error(L, "register_master: 'turns' must be a whole number from 1 to 99");
    d.turns = (int)t;
  }
  lua_pop(L, 1);
  std::string trig = "green_peg";
  optStringField(L, "trigger", trig, 16);
  if (trig == "green_peg") d.trigger = 0;
  else if (trig == "every_shot") d.trigger = 1;
  else if (trig == "level_start") d.trigger = 2;
  else luaL_error(L, "register_master: 'trigger' must be \"green_peg\", \"every_shot\" or \"level_start\"");
  lua_getfield(L, 1, "color");
  if (!lua_isnil(L, -1)) {
    if (!lua_isinteger(L, -1)) luaL_error(L, "register_master: 'color' must be a number like 0xRRGGBB");
    d.color = (uint32_t)((lua_Unsigned)lua_tointeger(L, -1) & 0xffffffu);
  }
  lua_pop(L, 1);
  d.onActivate = optFunctionField(L, "on_activate");
  d.onShot = optFunctionField(L, "on_shot");
  d.onEnd = optFunctionField(L, "on_end");
  I->mod->masters.push_back(d);
  return 0;
}
int b_master_key(lua_State* L) {
  Board& b = needBoard(L);
  if (const ModMaster* m = pp::masters::byPower(b.master[playerArg(L, 1, b)])) lua_pushstring(L, m->key().c_str());
  else lua_pushnil(L);
  return 1;
}

void setFuncs(lua_State* L, const char* table, const luaL_Reg* fns) {
  lua_newtable(L);
  luaL_setfuncs(L, fns, 0);
  lua_setglobal(L, table);
}

struct LuaPanic {};
int panicThrow(lua_State*) { throw LuaPanic{}; }

const char* kPrelude = R"LUA(
local pcall0, xpcall0, resume0, aborted, error0, burn = pcall, xpcall, coroutine.resume, __aborted, error, __burn
local function chk(ok, ...)
  if aborted() then error0("aborted by the host (budget)", 0) end
  if not ok then burn(1500) end
  return ok, ...
end
function pcall(f, ...) return chk(pcall0(f, ...)) end
function xpcall(f, h, ...) return chk(xpcall0(f, h, ...)) end
function coroutine.resume(c, ...) return chk(resume0(c, ...)) end
__aborted = nil
__burn = nil
)LUA";
}

HostServices& LuaModHost(lua_State* L) { return IM(L)->mod->host(); }

LuaMod::LuaMod(const Manifest& m, const fs::path& dir, HostServices& host) : man_(m), dir_(dir), host_(host), im_(std::make_unique<Impl>()) {
  im_->mod = this;
  im_->cap = (size_t)m.memoryMb << 20;
}
LuaMod::~LuaMod() {
  if (L_) { closeNet(L_); lua_close(L_); }
}
Ctx LuaMod::ctx() const { return im_->ctx; }
size_t LuaMod::memoryUsed() const { return im_->used; }
size_t LuaMod::memoryPeak() const { return im_->peak; }
bool LuaMod::memoryDenied() const { return im_->denied; }

bool LuaMod::open(std::string* err) {
  L_ = lua_newstate(allocf, im_.get());
  if (!L_) { if (err) *err = "cannot create a Lua state"; return false; }
  lua_State* L = L_;
  lua_atpanic(L, panicThrow);
  lua_setwarnf(L, warnCharge, nullptr);
  static const luaL_Reg libs[] = {{"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                                  {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine}, {nullptr, nullptr}};
  for (const luaL_Reg* l = libs; l->func; l++) { luaL_requiref(L, l->name, l->func, 1); lua_pop(L, 1); }
  auto nilGlobal = [&](const char* n) { lua_pushnil(L); lua_setglobal(L, n); };
  auto nilField = [&](const char* t, const char* n) { lua_getglobal(L, t); lua_pushnil(L); lua_setfield(L, -2, n); lua_pop(L, 1); };
  for (const char* n : {"dofile", "loadfile", "collectgarbage", "print"}) nilGlobal(n);
  nilField("string", "dump");
  for (const char* n : {"tan", "asin", "acos", "exp", "log", "random", "randomseed", "pow", "sinh", "cosh", "tanh", "log10", "ldexp", "frexp"}) nilField("math", n);
  auto setG = [&](const char* n, lua_CFunction f) { lua_pushcfunction(L, f); lua_setglobal(L, n); };
  setG("print", l_log); setG("log", l_log); setG("tostring", l_tostring); setG("load", l_load); setG("require", l_require); setG("__aborted", l_aborted); setG("__burn", l_burn);
  setG("rng_next", l_rng_next); setG("rng_range", l_rng_range); setG("register_power", l_register_power); setG("register_peg_type", l_register_peg_type); setG("register_master", l_register_master);
  lua_getglobal(L, "math");
  lua_pushcfunction(L, l_math_random); lua_setfield(L, -2, "random");
  lua_pushcfunction(L, l_math_sin); lua_setfield(L, -2, "sin");
  lua_pushcfunction(L, l_math_cos); lua_setfield(L, -2, "cos");
  lua_pushcfunction(L, l_math_atan); lua_setfield(L, -2, "atan");
  lua_pop(L, 1);
  lua_getglobal(L, "string");
  for (const char* n : {"find", "match", "gmatch", "gsub"}) {
    lua_getfield(L, -1, n);
    lua_pushcclosure(L, l_patguard, 1);
    lua_setfield(L, -2, n);
  }
  lua_pop(L, 1);
  lua_newtable(L); lua_setfield(L, LUA_REGISTRYINDEX, "pp_loaded");
  {
    lua_newtable(L);
    lua_pushstring(L, man_.id.c_str()); lua_setfield(L, -2, "id");
    lua_pushstring(L, man_.name.c_str()); lua_setfield(L, -2, "name");
    lua_pushstring(L, man_.version.c_str()); lua_setfield(L, -2, "version");
    lua_pushinteger(L, kModApi); lua_setfield(L, -2, "api");
    lua_setglobal(L, "mod");
  }
  static const luaL_Reg board[] = {{"tick", b_tick}, {"score", b_score}, {"orange_left", b_orange_left}, {"orange_total", b_orange_total}, {"balls_left", b_balls_left},
                                   {"shot_score", b_shot_score}, {"player", b_player}, {"fever", b_fever}, {"width", b_width}, {"master", b_master}, {"master_key", b_master_key}, {"phase", b_phase},
                                   {"peg", b_peg}, {"pegs", b_pegs}, {"peg_count", b_peg_count}, {"bricks", b_bricks}, {"balls", b_balls}, {"ball_count", b_ball_count},
                                   {"add_score", m_add_score}, {"hit_peg", m_hit_peg}, {"add_balls", m_add_balls}, {"set_ball_velocity", m_set_ball_velocity},
                                   {"say", m_say}, {"style_shot", m_style_shot}, {"set_peg_type", m_set_peg_type}, {"set_peg_types", m_set_peg_types}, {nullptr, nullptr}};
  static const luaL_Reg physics[] = {{"set", p_set}, {"get", p_get}, {nullptr, nullptr}};
  static const luaL_Reg state[] = {{"get", s_get}, {"set", s_set}, {"add", s_add}, {nullptr, nullptr}};
  static const luaL_Reg ui[] = {{"menu", u_menu}, {"message", u_message}, {nullptr, nullptr}};
  static const luaL_Reg draw[] = {{"rect", d_rect}, {"circle", d_circle}, {"text", d_text}, {"image", d_image}, {"screen", d_screen}, {"rgb", d_rgb}, {"hsv", d_hsv}, {nullptr, nullptr}};
  setFuncs(L, "board", board); setFuncs(L, "state", state); setFuncs(L, "ui", ui); setFuncs(L, "draw", draw); setFuncs(L, "physics", physics);
  if (man_.net || man_.fs) openNetLib(L, this, host_);
  if (luaL_loadbufferx(L, kPrelude, std::strlen(kPrelude), "=prelude", "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    if (err) *err = std::string("prelude: ") + lua_tostring(L, -1);
    return false;
  }
  lua_pushglobaltable(L);
  lua_newtable(L);
  lua_pushnil(L);
  std::vector<std::string> keys;
  while (lua_next(L, -3)) {
    lua_pushvalue(L, -2);
    lua_insert(L, -2);
    lua_rawset(L, -4);
    keys.push_back(lua_tostring(L, -1));
  }
  for (auto& k : keys) { lua_pushnil(L); lua_setfield(L, -3, k.c_str()); }
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, "_G");
  lua_pushvalue(L, -1);
  im_->shadowRef = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_newtable(L);
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, l_global_guard);
  lua_setfield(L, -2, "__newindex");
  lua_pushstring(L, "locked");
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -3);
  lua_pop(L, 2);
  im_->check = workCheck;
  im_->refuse = workRefuse;
  im_->over = workOver;
  im_->nudge = workNudge;
  lua_sethook(L, countHook, LUA_MASKCOUNT, kStride);
  for (auto& s : man_.scripts) {
    std::string text;
    if (!readFile(dir_ / s, text, 8u << 20)) { if (err) *err = "script not found: " + s; return false; }
    if (luaL_loadbufferx(L, text.data(), text.size(), ("@" + man_.id + "/" + s).c_str(), "t") != LUA_OK) {
      if (err) *err = lua_tostring(L, -1);
      lua_pop(L, 1);
      return false;
    }
    std::string e;
    if (!pcallBudgeted(0, 0, true, Ctx::Load, &e)) { if (err) *err = e; return false; }
  }
  std::string e;
  if (!call("on_load", Ctx::Load, 0, nullptr, true)) { if (err) *err = "on_load: " + lastError; return false; }
  return true;
}

bool LuaMod::pcallBudgeted(int nargs, int nres, bool timed, Ctx ctx, std::string* err) {
  Impl* I = im_.get();
  Ctx prev = I->ctx;
  I->ctx = ctx;
  I->total = I->remaining = (ctx == Ctx::Load ? 5LL : 1LL) * man_.budget;
  I->timed = timed && ctx != Ctx::Sim;
  I->aborted = false;
  I->note = nullptr;
  I->screenMode = false;
  I->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ctx == Ctx::Load ? 2000 : ctx == Ctx::Draw ? 25 : 250);
  lua_sethook(L_, countHook, LUA_MASKCOUNT, kStride);
  LuaWork* prevWork = gWork;
  lua_State* prevCur = I->cur;
  I->cur = L_;
  const bool prevMetered = I->metered;
  gWork = I;
  I->metered = true;
  int st = LUA_OK;
  try {
    st = lua_pcall(L_, nargs, nres, 0);
  } catch (...) {
    st = LUA_ERRRUN;
    lua_pushstring(L_, "native exception");
  }
  I->metered = prevMetered;
  I->cur = prevCur;
  gWork = prevWork;
  I->ctx = prev;
  lastInstructions = I->total - I->remaining;
  if (st != LUA_OK) {
    size_t len = 0;
    const char* msg = lua_isstring(L_, -1) ? lua_tolstring(L_, -1, &len) : nullptr;
    lastError = msg ? std::string(msg, std::min<size_t>(len, 600)) + (len > 600 ? "..." : "") : "(error object)";
    if (I->aborted && I->note && lastError.find(I->note) == std::string::npos) lastError = I->note;
    if (err) *err = lastError;
    lua_pop(L_, 1);
    return false;
  }
  return true;
}

bool LuaMod::hasHook(const char* name) const {
  lua_rawgeti(L_, LUA_REGISTRYINDEX, im_->shadowRef);
  lua_pushstring(L_, name);
  lua_rawget(L_, -2);
  bool f = lua_isfunction(L_, -1);
  lua_pop(L_, 2);
  return f;
}

bool LuaMod::call(const char* hook, Ctx ctx, int nargs, const std::function<void(lua_State*)>& push, bool timed) {
  if (!L_) return true;
  const int base = lua_gettop(L_);
  try {
    lua_rawgeti(L_, LUA_REGISTRYINDEX, im_->shadowRef);
    lua_pushstring(L_, hook);
    lua_rawget(L_, -2);
    lua_remove(L_, -2);
    if (!lua_isfunction(L_, -1)) { lua_settop(L_, base); return true; }
    if (push) push(L_);
  } catch (...) {
    lua_settop(L_, base);
    errors++;
    reportError(im_.get(), host_, man_.id, std::string("out of memory before ") + hook);
    return false;
  }
  std::string e;
  if (pcallBudgeted(nargs, 0, timed, ctx, &e)) return true;
  errors++;
  reportError(im_.get(), host_, man_.id, std::string("error in ") + hook + ": " + e);
  return false;
}

bool LuaMod::callSim(const char* hook, int nargs, const std::function<void(lua_State*)>& push) { return call(hook, Ctx::Sim, nargs, push, false); }
bool LuaMod::callLoose(const char* hook, Ctx ctx, int nargs, const std::function<void(lua_State*)>& push) {
  if (disabled) return true;
  bool ok = call(hook, ctx, nargs, push, true);
  if (!ok && errors >= 20) { disabled = true; emit(host_, man_.id, pp::log::Level::Warn, std::string(hook) + " failed repeatedly: drawing, menu and network callbacks of this mod are switched off"); }
  return ok;
}
bool LuaMod::runMenu(int index) {
  if (index < 0 || index >= (int)menus.size()) return false;
  lua_rawgeti(L_, LUA_REGISTRYINDEX, menus[(size_t)index].ref);
  std::string e;
  if (pcallBudgeted(0, 0, true, Ctx::Ui, &e)) return true;
  emit(host_, man_.id, pp::log::Level::Error, "menu error: " + e);
  return false;
}
bool LuaMod::callRef(int ref, int nargs, const std::function<void(lua_State*)>& push) {
  if (!L_ || disabled) return false;
  const int base = lua_gettop(L_);
  try {
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L_, -1)) { lua_settop(L_, base); return false; }
    if (push) push(L_);
  } catch (...) {
    lua_settop(L_, base);
    errors++;
    reportError(im_.get(), host_, man_.id, "out of memory before a callback");
    return false;
  }
  std::string e;
  if (pcallBudgeted(nargs, 0, true, Ctx::Ui, &e)) return true;
  errors++;
  reportError(im_.get(), host_, man_.id, "callback error: " + e);
  if (errors >= 20 && !disabled) {
    disabled = true;
    emit(host_, man_.id, pp::log::Level::Warn, "callbacks failed repeatedly: drawing, menu and network callbacks of this mod are switched off");
  }
  return false;
}

bool LuaMod::activatePower(const PowerDef& p, int player, int peg) {
  lua_rawgeti(L_, LUA_REGISTRYINDEX, p.ref);
  lua_pushinteger(L_, player);
  lua_pushinteger(L_, peg);
  std::string e;
  if (pcallBudgeted(2, 0, false, Ctx::Sim, &e)) return true;
  reportError(im_.get(), host_, man_.id, "error in power '" + p.name + "': " + e);
  return false;
}

bool LuaMod::callPegHook(int ref, int peg, int ball) {
  if (!L_ || !host_.board || peg < 0 || peg >= (int)host_.board->objs.size()) return false;
  lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
  if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return false; }
  pushPeg(L_, *host_.board, peg);
  lua_pushinteger(L_, ball);
  std::string e;
  if (pcallBudgeted(2, 0, false, Ctx::Sim, &e)) return true;
  errors++;
  reportError(im_.get(), host_, man_.id, "error in a peg type callback: " + e);
  return false;
}

bool LuaMod::callMaster(const MasterDef& m, int which, int player, int peg) {
  const int ref = which == 0 ? m.onActivate : which == 1 ? m.onShot : m.onEnd;
  if (ref < 0 || !L_) return true;
  lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
  lua_pushinteger(L_, player);
  int n = 1;
  if (which == 0) {
    lua_pushinteger(L_, peg);
    n = 2;
  }
  std::string e;
  if (pcallBudgeted(n, 0, false, Ctx::Sim, &e)) return true;
  static const char* names[] = {"on_activate", "on_shot", "on_end"};
  reportError(im_.get(), host_, man_.id, "error in " + std::string(names[which]) + " of Master '" + m.id + "': " + e);
  return false;
}

std::string LuaMod::eval(const std::string& code, std::string* result, bool sim) {
  lua_State* L = L_;
  if (luaL_loadbufferx(L, code.data(), code.size(), "=eval", "t") != LUA_OK) {
    std::string e = lua_tostring(L, -1);
    lua_pop(L, 1);
    return e;
  }
  std::string e;
  if (!pcallBudgeted(0, 1, !sim, sim ? Ctx::Sim : Ctx::Ui, &e)) return e;
  if (result) {
    size_t n;
    const char* s = luaL_tolstring(L, -1, &n);
    *result = std::string(s, n);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return "";
}

}
