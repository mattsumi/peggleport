#pragma once
#if defined(__GNUC__) || defined(__clang__)
#define PP_UI_WEAK __attribute__((weak))
#else
#define PP_UI_WEAK
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#pragma comment(linker, "/include:pp_mods_available_default")
#pragma comment(linker, "/include:pp_mods_make_screen_default")
#pragma comment(linker, "/include:pp_editor_available_default")
#pragma comment(linker, "/include:pp_editor_make_screen_default")
#pragma comment(linker, "/include:pp_editor_run_default")
#pragma comment(linker, "/include:pp_editor_make_screen_level_default")
#endif

extern "C" {
int pp_mods_available(void);
void* pp_mods_make_screen(void* app);
int pp_editor_available(void);
void* pp_editor_make_screen(void* app);
int pp_editor_run(void* app);
void* pp_editor_make_screen_level(void* app, const char* level);
}

namespace pp::ui {
inline bool modsHookAvailable() { return pp_mods_available() != 0; }
inline bool editorHookAvailable() { return pp_editor_available() != 0; }
}
