#include "lprefix.h"

#define LUA_LIB
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "luawork.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#undef l_unlikely
#define l_unlikely(x) (pp::mods::workTick() ? 1 : ((x) != 0))

static void ppCharge(lua_State* L, long long units, const char* what) {
  if (!pp::mods::gWork) return;
  if (units > pp::mods::LuaWork::kBig) luaL_error(L, "%s: too large for the sandbox (split the work into smaller steps)", what);
  pp::mods::workSpend(L, units);
}

static long long ppFmtCost(const char* f, double v) {
  long long width = 0, prec = 0;
  const char* p = f;
  while (*p && *p != '%') p++;
  if (!*p) return 8;
  p++;
  while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
  while (*p >= '0' && *p <= '9') { if (width < 1000) width = width * 10 + (*p - '0'); p++; }
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') { if (prec < 1000) prec = prec * 10 + (*p - '0'); p++; }
  }
  if (width > 200) width = 200;
  if (prec > 200) prec = 200;
  long long cost = 8 + (width >> 1) + prec;
  while (*p && !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) p++;
  while (*p == 'l' || *p == 'L' || *p == 'h') p++;
  if ((*p == 'f' || *p == 'F') && v - v == 0.0 && v != 0.0) {
    int e = 0;
    frexp(v, &e);
    if (e > 0) cost += (long long)e;
  }
  return cost;
}
static long long ppFmtCost(const char* f, long long) { return ppFmtCost(f, 0.0); }
static long long ppFmtCost(const char* f, int) { return ppFmtCost(f, 0.0); }
static long long ppFmtCost(const char* f, const void*) { return ppFmtCost(f, 0.0); }

static char* ppPrep(luaL_Buffer* B, size_t sz) {
  long long cost = (sz == 1 && B->n >= B->size) ? (long long)(B->size >> 3) + 4 : (long long)(sz >> 7) + 1;
  pp::mods::workSpend(B->L, cost);
  return luaL_prepbuffsize(B, sz);
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
static void ppCheckStack(lua_State* L, int sz, const char* msg) {
  pp::mods::workSpend(L, sz);
  luaL_checkstack(L, sz, msg);
}

#undef l_sprintf
#define l_sprintf(s, sz, f, i) (pp::mods::workQuiet(ppFmtCost(f, i)), snprintf(s, sz, f, i))
#define luaL_prepbuffsize(B, sz) ppPrep(B, sz)
#define luaL_addlstring(B, s, l) ppAddLString(B, s, l)
#define luaL_addvalue(B) ppAddValue(B)
#define luaL_checkstack(L, sz, msg) ppCheckStack(L, sz, msg)

#define str_rep str_rep_orig
#define str_lower str_lower_orig
#define str_upper str_upper_orig
#define str_reverse str_reverse_orig
#define str_pack str_pack_orig
#define str_unpack str_unpack_orig
#define str_packsize str_packsize_orig
#define luaopen_string luaopen_string_orig

#include "lstrlib.c"

#undef str_rep
#undef str_lower
#undef str_upper
#undef str_reverse
#undef str_pack
#undef str_unpack
#undef str_packsize
#undef luaopen_string
#undef luaL_prepbuffsize
#undef luaL_addlstring
#undef luaL_addvalue
#undef luaL_checkstack

static int str_rep(lua_State* L) {
  size_t l, lsep;
  luaL_checklstring(L, 1, &l);
  lua_Integer n = luaL_checkinteger(L, 2);
  luaL_optlstring(L, 3, "", &lsep);
  if (n > 0 && pp::mods::gWork) {
    unsigned long long unit = (unsigned long long)l + lsep, total;
    const unsigned long long cap = 1ull << 32;
    if (unit != 0 && (unsigned long long)n > cap / unit) total = cap;
    else total = unit * (unsigned long long)n;
    unsigned long long cost = ((unsigned long long)n >> 3) + (total >> 6);
    if (cost > (unsigned long long)pp::mods::LuaWork::kBig) luaL_error(L, "string.rep: result too large for the sandbox (split the work into smaller steps)");
    pp::mods::workSpend(L, (long long)cost);
  }
  return str_rep_orig(L);
}
static int str_upper(lua_State* L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  ppCharge(L, (long long)(l >> 3), "string.upper");
  return str_upper_orig(L);
}
static int str_lower(lua_State* L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  ppCharge(L, (long long)(l >> 3), "string.lower");
  return str_lower_orig(L);
}
static int str_reverse(lua_State* L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  ppCharge(L, (long long)(l >> 4), "string.reverse");
  return str_reverse_orig(L);
}
static int str_pack(lua_State* L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  ppCharge(L, (long long)(l >> 1), "string.pack");
  return str_pack_orig(L);
}
static int str_unpack(lua_State* L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  ppCharge(L, (long long)(l >> 1), "string.unpack");
  return str_unpack_orig(L);
}
static int str_packsize(lua_State* L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  ppCharge(L, (long long)(l >> 1), "string.packsize");
  return str_packsize_orig(L);
}

LUAMOD_API int luaopen_string(lua_State* L) {
  luaopen_string_orig(L);
  static const luaL_Reg over[] = {{"rep", str_rep}, {"upper", str_upper}, {"lower", str_lower}, {"reverse", str_reverse},
                                  {"pack", str_pack}, {"unpack", str_unpack}, {"packsize", str_packsize}, {nullptr, nullptr}};
  luaL_setfuncs(L, over, 0);
  return 1;
}
