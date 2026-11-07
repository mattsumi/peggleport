#include "lprefix.h"

#define LUA_CORE
#include "luawork.h"
#include "lcode.h"

static int ppJumpStep(FuncState* fs) {
  pp::mods::LuaWork* w = pp::mods::gWork;
  if (w && (++w->aux & 7) == 0) pp::mods::workSpend(fs->ls->L, 1);
  return -1;
}

#undef NO_JUMP
#define NO_JUMP (ppJumpStep(fs))

#include "lcode.c"
