#include "lprefix.h"

#define LUA_LIB
#include "luawork.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static int ppResume(lua_State* co, lua_State* from, int narg, int* nres) {
  pp::mods::LuaWork* w = pp::mods::gWork;
  lua_State* prev = nullptr;
  if (w) {
    prev = w->cur;
    w->cur = co;
  }
  int st = lua_resume(co, from, narg, nres);
  if (w) w->cur = prev;
  return st;
}

#define lua_resume(co, from, narg, nres) ppResume(co, from, narg, nres)

#include "lcorolib.c"
