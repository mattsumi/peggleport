#include "ui/hooks.h"

#if defined(_MSC_VER) && !defined(__clang__)
#define PP_HOOK(name) name##_default
#pragma comment(linker, "/alternatename:pp_mods_available=pp_mods_available_default")
#pragma comment(linker, "/alternatename:pp_mods_make_screen=pp_mods_make_screen_default")
#pragma comment(linker, "/alternatename:pp_editor_available=pp_editor_available_default")
#pragma comment(linker, "/alternatename:pp_editor_make_screen=pp_editor_make_screen_default")
#pragma comment(linker, "/alternatename:pp_editor_run=pp_editor_run_default")
#pragma comment(linker, "/alternatename:pp_editor_make_screen_level=pp_editor_make_screen_level_default")
#else
#define PP_HOOK(name) name
#endif
extern "C" {
PP_UI_WEAK int PP_HOOK(pp_mods_available)(void) { return 0; }
PP_UI_WEAK void* PP_HOOK(pp_mods_make_screen)(void*) { return nullptr; }
PP_UI_WEAK int PP_HOOK(pp_editor_available)(void) { return 0; }
PP_UI_WEAK void* PP_HOOK(pp_editor_make_screen)(void*) { return nullptr; }
PP_UI_WEAK int PP_HOOK(pp_editor_run)(void*) { return -1; }
PP_UI_WEAK void* PP_HOOK(pp_editor_make_screen_level)(void*, const char*) { return nullptr; }
}
