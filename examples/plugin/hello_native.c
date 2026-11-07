#include <stdio.h>
#include "peggleport_mod.h"

static const PPModApi* api;

static void on_peg_hit(int32_t peg, int32_t ball) {
  double n = 0;
  (void)peg; (void)ball;
  api->state_get("native_hits", &n);
  n += 1;
  api->state_set("native_hits", n);
  if (((int)n) % 10 == 0) {
    api->add_score(250);
    api->say("Native bonus!");
  }
}

static void on_frame(void) {
  char buf[64];
  double n = 0;
  api->state_get("native_hits", &n);
  snprintf(buf, sizeof buf, "native plugin: %d pegs hit", (int)n);
  api->draw_text(20.0f, -8.0f, buf, 0xa0ffa0ffu);
}

static void on_level_start(void) { api->state_set("native_hits", 0); }

PP_MOD_EXPORT int32_t pp_mod_init(const PPModApi* a) {
  PPModHooks h = {0};
  if (a->abi_version != PP_MOD_ABI_VERSION) return 1;
  api = a;
  h.struct_size = sizeof h;
  h.on_level_start = on_level_start;
  h.on_peg_hit = on_peg_hit;
  h.on_frame = on_frame;
  api->log("hello_native loaded");
  api->register_hooks(&h);
  return 0;
}
