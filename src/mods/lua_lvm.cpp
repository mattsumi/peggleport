#include "lprefix.h"

#include <string.h>

#include "luawork.h"

static int ppStrcoll(const char* a, const char* b) {
  if (a[0] == b[0]) pp::mods::workNudge((long long)(strlen(a) >> 6));
  return strcoll(a, b);
}

#define strcoll ppStrcoll

#include "lvm.c"
