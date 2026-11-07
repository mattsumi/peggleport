#include "lprefix.h"

#define LUA_LIB
#include <string.h>

#include "luawork.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static void ppCharge(lua_State* L, long long units, const char* what) {
  if (!pp::mods::gWork) return;
  if (units > pp::mods::LuaWork::kBig) luaL_error(L, "%s: too large for the sandbox (split the work into smaller steps)", what);
  pp::mods::workSpend(L, units);
}

#define luaB_tonumber luaB_tonumber_orig
#define luaB_rawequal luaB_rawequal_orig
#define luaopen_base luaopen_base_orig

#include "lbaselib.c"

#undef luaB_tonumber
#undef luaB_rawequal
#undef luaopen_base

static int luaB_tonumber(lua_State* L) {
  if (lua_type(L, 1) == LUA_TSTRING && !lua_isnoneornil(L, 2)) ppCharge(L, (long long)(lua_rawlen(L, 1) >> 2), "tonumber");
  return luaB_tonumber_orig(L);
}

static int luaB_rawequal(lua_State* L) {
  if (lua_type(L, 1) == LUA_TSTRING && lua_type(L, 2) == LUA_TSTRING && lua_rawlen(L, 1) == lua_rawlen(L, 2))
    ppCharge(L, (long long)(lua_rawlen(L, 1) >> 6), "rawequal");
  return luaB_rawequal_orig(L);
}

LUAMOD_API int luaopen_base(lua_State* L) {
  luaopen_base_orig(L);
  static const luaL_Reg over[] = {{"tonumber", luaB_tonumber}, {"rawequal", luaB_rawequal}, {nullptr, nullptr}};
  luaL_setfuncs(L, over, 0);
  return 1;
}
