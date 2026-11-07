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
static void ppCheckStack(lua_State* L, int sz, const char* msg) {
  pp::mods::workSpend(L, sz);
  luaL_checkstack(L, sz, msg);
}

#define luaL_checkstack(L, sz, msg) ppCheckStack(L, sz, msg)
#define utflen utflen_orig
#define utfchar utfchar_orig
#define byteoffset byteoffset_orig
#define iter_codes iter_codes_orig
#define luaopen_utf8 luaopen_utf8_orig

#include "lutf8lib.c"

#undef luaL_checkstack
#undef utflen
#undef utfchar
#undef byteoffset
#undef iter_codes
#undef luaopen_utf8

static int utflen(lua_State* L) {
  size_t len;
  luaL_checklstring(L, 1, &len);
  lua_Integer posi = u_posrelat(luaL_optinteger(L, 2, 1), len);
  lua_Integer posj = u_posrelat(luaL_optinteger(L, 3, -1), len);
  lua_Integer span = posj >= posi ? posj - posi + 1 : 0;
  if (span > (lua_Integer)len) span = (lua_Integer)len;
  ppCharge(L, (long long)(span >> 1), "utf8.len");
  return utflen_orig(L);
}

static int utfchar(lua_State* L) {
  ppCharge(L, 8LL * lua_gettop(L), "utf8.char");
  return utfchar_orig(L);
}

static int byteoffset(lua_State* L) {
  size_t len;
  luaL_checklstring(L, 1, &len);
  ppCharge(L, (long long)(len >> 4), "utf8.offset");
  return byteoffset_orig(L);
}

static int ppIterAux(lua_State* L, int strict) {
  size_t len;
  const char* s = luaL_checklstring(L, 1, &len);
  lua_Unsigned n = (lua_Unsigned)lua_tointeger(L, 2);
  long long steps = 0;
  if (n < len) {
    while (iscontp(s + n)) {
      n++;
      if ((++steps & 1023) == 0) pp::mods::workSpend(L, 128);
    }
  }
  pp::mods::workSpend(L, 2 + (steps >> 3));
  return iter_aux(L, strict);
}
static int ppIterStrict(lua_State* L) { return ppIterAux(L, 1); }
static int ppIterLax(lua_State* L) { return ppIterAux(L, 0); }

static int iter_codes(lua_State* L) {
  int lax = lua_toboolean(L, 2);
  int r = iter_codes_orig(L);
  lua_pushcfunction(L, lax ? ppIterLax : ppIterStrict);
  lua_replace(L, -r - 1);
  return r;
}

LUAMOD_API int luaopen_utf8(lua_State* L) {
  luaopen_utf8_orig(L);
  static const luaL_Reg over[] = {{"len", utflen}, {"char", utfchar}, {"offset", byteoffset}, {"codes", iter_codes}, {nullptr, nullptr}};
  luaL_setfuncs(L, over, 0);
  return 1;
}
