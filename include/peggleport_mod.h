#ifndef PEGGLEPORT_MOD_H
#define PEGGLEPORT_MOD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PP_MOD_ABI_VERSION 1
#define PP_MOD_ABI_MINOR 1

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

#define PP_PHYS_GRAVITY_Y (1u << 0)
#define PP_PHYS_GRAVITY_X (1u << 1)
#define PP_PHYS_LAUNCH_SPEED (1u << 2)
#define PP_PHYS_BALL_BOUNCE (1u << 3)
#define PP_PHYS_PEG_BOUNCE (1u << 4)
#define PP_PHYS_DRAG (1u << 5)
#define PP_PHYS_MAX_SPEED (1u << 6)
#define PP_PHYS_BALL_RADIUS (1u << 7)
#define PP_PHYS_ALL 0xffu

typedef struct PPPhysics {
  uint32_t struct_size;
  uint32_t mask;
  float gravity_y, gravity_x, launch_speed, ball_bounce, peg_bounce, drag, max_speed, ball_radius;
} PPPhysics;

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

typedef struct PPPegTypeDef {
  uint32_t struct_size;
  const char* id;
  const char* name;
  const char* image;
  const char* lit_image;
  const char* const* stages;
  int32_t stage_count;
  uint32_t color;
  int32_t base;
  int32_t score;
  int32_t counts_as_orange;
  int32_t hits_to_clear;
  int32_t indestructible;
  float bounce;
  float radius_scale;
  void (*on_hit)(int32_t peg, int32_t ball);
  void (*on_clear)(int32_t peg);
} PPPegTypeDef;

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
  int32_t (*physics_set)(const PPPhysics* p);
  int32_t (*physics_get)(PPPhysics* out);
  uint32_t abi_minor;
  int32_t (*register_peg_type)(const PPPegTypeDef* def);
  void (*set_peg_type)(int32_t peg, const char* type);
  int32_t (*peg_custom)(int32_t peg, char* out, int32_t cap, int32_t* hits);
} PPModApi;

#define PP_MOD_API_HAS(api, member) ((api)->struct_size >= offsetof(PPModApi, member) + sizeof((api)->member))

#ifdef __cplusplus
}
#endif
#endif
