#include "lprefix.h"

#define LUA_LIB
#include <string.h>

#include "luawork.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static void ppSeti(lua_State* L, int idx, lua_Integer n) {
  long long cost = 1;
  if (lua_getmetatable(L, idx)) {
    lua_pop(L, 1);
    cost = 5;
  }
  pp::mods::workSpend(L, cost);
  lua_seti(L, idx, n);
}
static int ppCompare(lua_State* L, int a, int b, int op) {
  pp::mods::workSpend(L, (lua_type(L, a) == LUA_TSTRING || lua_type(L, b) == LUA_TSTRING) ? 5 : 1);
  return lua_compare(L, a, b, op);
}
static int ppCheckStack(lua_State* L, int n) {
  pp::mods::workSpend(L, n);
  return lua_checkstack(L, n);
}
static void ppCall(lua_State* L, int nargs, int nresults) {
  pp::mods::workSpend(L, 1);
  lua_callk(L, nargs, nresults, 0, NULL);
}
static void ppAddLString(luaL_Buffer* B, const char* s, size_t l) {
  pp::mods::workSpend(B->L, (long long)(l >> 5) + 1);
  luaL_addlstring(B, s, l);
}
static void ppAddValue(luaL_Buffer* B) {
  int t = lua_type(B->L, -1);
  long long cost = t == LUA_TNUMBER ? (lua_isinteger(B->L, -1) ? 10 : 24) : (long long)(lua_rawlen(B->L, -1) >> 5) + 1;
  pp::mods::workSpend(B->L, cost);
  luaL_addvalue(B);
}

#undef lua_call
#define lua_seti(L, idx, n) ppSeti(L, idx, n)
#define lua_compare(L, a, b, op) ppCompare(L, a, b, op)
#define lua_checkstack(L, n) ppCheckStack(L, n)
#define lua_call(L, n, r) ppCall(L, n, r)
#define luaL_addlstring(B, s, l) ppAddLString(B, s, l)
#define luaL_addvalue(B) ppAddValue(B)
#define l_randomizePivot() 0x9e3779b9u

#include "ltablib.c"
