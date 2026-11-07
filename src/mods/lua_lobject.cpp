#include "lprefix.h"

#include <string.h>

#define luaO_str2num luaO_str2num_orig

#include "lobject.c"

#undef luaO_str2num

size_t luaO_str2num(const char* s, TValue* o) {
  if (memchr(s, 0, 201) == NULL) return 0;
  return luaO_str2num_orig(s, o);
}
