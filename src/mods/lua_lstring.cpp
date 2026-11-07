#include "lprefix.h"

#include "luawork.h"

#define luaS_eqlngstr luaS_eqlngstr_orig

#include "lstring.c"

#undef luaS_eqlngstr

int luaS_eqlngstr(TString* a, TString* b) {
  if (a != b && a->u.lnglen == b->u.lnglen) pp::mods::workNudge((long long)(a->u.lnglen >> 6));
  return luaS_eqlngstr_orig(a, b);
}
