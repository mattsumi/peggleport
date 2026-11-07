#include "lprefix.h"

#include "luawork.h"

#define luaT_gettm luaT_gettm_orig
#define luaT_getvarargs luaT_getvarargs_orig

#include "ltm.c"

#undef luaT_gettm
#undef luaT_getvarargs

const TValue* luaT_gettm(Table* events, TMS event, TString* ename) {
  pp::mods::workNudge(1);
  return luaT_gettm_orig(events, event, ename);
}

void luaT_getvarargs(lua_State* L, CallInfo* ci, StkId where, int wanted) {
  pp::mods::workNudge((long long)(ci->u.l.nextraargs >> 2));
  luaT_getvarargs_orig(L, ci, where, wanted);
}
