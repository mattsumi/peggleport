#ifndef PEGGLEPORT_MOD_H
#define PEGGLEPORT_MOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PP_MOD_ABI_VERSION 1
#define PP_MOD_ABI_MINOR 1

#define PP_MASTER_TRIGGER_GREEN_PEG 0
#define PP_MASTER_TRIGGER_EVERY_SHOT 1
#define PP_MASTER_TRIGGER_LEVEL_START 2

#if defined(_WIN32)
#define PP_MOD_EXPORT __declspec(dllexport)
#else
#define PP_MOD_EXPORT __attribute__((visibility("default")))
#endif

typedef struct PPPegInfo {
  int32_t id;
  int32_t type;
  int32_t lit;
  int32_t gone;
  float x, y, radius;
} PPPegInfo;

typedef struct PPBallInfo {
  int32_t id, alive;
  float x, y, vx, vy, r;
} PPBallInfo;

typedef struct PPModHooks {
  uint32_t struct_size;
  void (*on_level_start)(void);
  void (*on_tick)(int32_t tick);
  void (*on_shot)(float angle);
  void (*on_peg_hit)(int32_t peg, int32_t ball);
  void (*on_ball_lost)(void);
  void (*on_level_end)(int32_t won);
  void (*on_power)(int32_t power);
  int32_t (*on_activate_power)(int32_t power, int32_t peg);
  void (*on_frame)(void);
  void (*shutdown)(void);
} PPModHooks;

typedef struct PPMasterDef {
  uint32_t struct_size;
  const char* id;
  const char* name;
  const char* description;
  const char* power_name;
  const char* power_description;
  const char* portrait;
  const char* banner;
  int32_t turns;
  int32_t trigger;
  uint32_t color;
  void (*on_activate)(int32_t player, int32_t peg);
  void (*on_shot)(int32_t player);
  void (*on_end)(int32_t player);
} PPMasterDef;

typedef struct PPModApi {
  uint32_t abi_version;
  uint32_t struct_size;
  void (*log)(const char* msg);
  void (*register_hooks)(const PPModHooks* hooks);
  int32_t (*tick)(void);
  int32_t (*score)(int32_t player);
  int32_t (*orange_left)(void);
  int32_t (*balls_left)(int32_t player);
  int32_t (*object_count)(void);
  int32_t (*peg_info)(int32_t id, PPPegInfo* out);
  int32_t (*ball_count)(void);
  int32_t (*ball_info)(int32_t index, PPBallInfo* out);
  uint32_t (*rng_next)(void);
  void (*add_score)(int32_t n);
  void (*hit_peg)(int32_t id);
  void (*add_balls)(int32_t n);
  void (*say)(const char* text);
  void (*state_set)(const char* key, double value);
  int32_t (*state_get)(const char* key, double* out);
  void (*draw_rect)(float x, float y, float w, float h, uint32_t rgba, int32_t fill);
  void (*draw_circle)(float x, float y, float r, uint32_t rgba, int32_t fill);
  void (*draw_text)(float x, float y, const char* text, uint32_t rgba);
  uint32_t abi_minor;
  int32_t (*register_master)(const PPMasterDef* def);
} PPModApi;

#ifdef __cplusplus
}
#endif
#endif
