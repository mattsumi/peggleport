#include "mods/native.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <memory>
#include <string>
#include <filesystem>

#include "peggleport_mod.h"
#include "sim.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace pp::mods {

struct NativeMod::Impl {
  void* lib = nullptr;
  std::string id;
  HostServices* host = nullptr;
  PPModHooks hooks{};
  bool hooked = false;
  uint32_t presRng = 0x1234567u;
  std::chrono::steady_clock::time_point logWindow;
  int logCount = 0;
  bool logNoted = false;
};

namespace {
NativeMod::Impl* gCur = nullptr;
struct Cur {
  NativeMod::Impl* prev;
  explicit Cur(NativeMod::Impl* i) : prev(gCur) { gCur = i; }
  ~Cur() { gCur = prev; }
};
Board* B() { return gCur && gCur->host ? gCur->host->board : nullptr; }
bool inSim() { return gCur && gCur->host && gCur->host->ctx == Ctx::Sim && gCur->host->board; }
void cLog(const char* m) {
  if (!gCur || !gCur->host || !gCur->host->log || !m) return;
  NativeMod::Impl& I = *gCur;
  auto now = std::chrono::steady_clock::now();
  if (now - I.logWindow >= std::chrono::seconds(1)) {
    I.logWindow = now;
    I.logCount = 0;
    I.logNoted = false;
  }
  if (I.logCount >= 200) {
    if (!I.logNoted) {
      I.logNoted = true;
      I.host->log(I.id, pp::log::Level::Warn, "log output is limited to 200 lines per second; extra lines are dropped");
    }
    return;
  }
  I.logCount++;
  I.host->log(I.id, pp::log::Level::Info, std::string(m).substr(0, 800));
}
void cRegister(const PPModHooks* h) {
  if (!gCur || !h) return;
  gCur->hooks = PPModHooks{};
  std::memcpy(&gCur->hooks, h, std::min<size_t>(h->struct_size, sizeof(PPModHooks)));
  gCur->hooked = true;
}
int32_t cTick() { return B() ? B()->tick : -1; }
int32_t cScore(int32_t p) { return B() && p >= 0 && p < 4 ? B()->liveScore(p) : 0; }
int32_t cOrange() { return B() ? B()->orangeLeft : 0; }
int32_t cBallsLeft(int32_t p) { return B() && p >= 0 && p < 4 ? B()->ballsLeft[p] : 0; }
int32_t cObjCount() { return B() ? (int32_t)B()->objs.size() : 0; }
int32_t cPeg(int32_t id, PPPegInfo* o) {
  Board* b = B();
  if (!b || !o || id < 0 || id >= (int32_t)b->objs.size()) return 0;
  const Obj& ob = b->objs[(size_t)id];
  o->id = id; o->type = ob.pegType; o->lit = ob.lit; o->gone = ob.gone; o->x = ob.pos.x; o->y = ob.pos.y; o->radius = ob.radius;
  return 1;
}
int32_t cBallCount() { return B() ? (int32_t)B()->balls.size() : 0; }
int32_t cBall(int32_t i, PPBallInfo* o) {
  Board* b = B();
  if (!b || !o || i < 0 || i >= (int32_t)b->balls.size()) return 0;
  const Ball& bl = b->balls[(size_t)i];
  o->id = i; o->alive = bl.alive; o->x = bl.pos.x; o->y = bl.pos.y; o->vx = bl.vel.x; o->vy = bl.vel.y; o->r = bl.r;
  return 1;
}
uint32_t cRng() {
  if (inSim()) return B()->rng.rand();
  if (!gCur) return 0;
  uint32_t x = gCur->presRng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  gCur->presRng = x;
  return x & 0x7fffffffu;
}
void q(const ModCmd& c) { if (inSim() && gCur->host->queue) gCur->host->queue(c); }
void cAddScore(int32_t n) { q({ModCmd::AddScore, std::max(-1000000, std::min(1000000, n)), 0, 0, ""}); }
void cHit(int32_t id) { q({ModCmd::HitPeg, id, 0, 0, ""}); }
void cBalls(int32_t n) { q({ModCmd::AddBalls, std::max(-99, std::min(99, n)), 0, 0, ""}); }
void cSay(const char* t) { if (t) q({ModCmd::Say, 0, 0, 0, std::string(t).substr(0, 60)}); }
void cStateSet(const char* k, double v) {
  if (!inSim() || !k || std::strlen(k) > 48 || !(v == v)) return;
  Board* b = B();
  if (b->modState.size() >= 256 && !b->modState.count(k)) return;
  b->modState[k] = v;
}
int32_t cStateGet(const char* k, double* out) {
  Board* b = B();
  if (!b || !k || !out) return 0;
  auto it = b->modState.find(k);
  if (it == b->modState.end()) return 0;
  *out = it->second;
  return 1;
}
DrawSink* sink() { return gCur && gCur->host && gCur->host->ctx == Ctx::Draw ? gCur->host->sink : nullptr; }
void cRect(float x, float y, float w, float h, uint32_t c, int32_t f) { if (auto* s = sink()) s->rect(x, y, w, h, c, f != 0, false); }
void cCircle(float x, float y, float r, uint32_t c, int32_t f) { if (auto* s = sink()) s->circle(x, y, r, c, f != 0, false); }
void cText(float x, float y, const char* t, uint32_t c) { if (auto* s = sink(); s && t) s->text(x, y, t, c, 0, 1.0f, false); }

const PPModApi kApi = {PP_MOD_ABI_VERSION, sizeof(PPModApi), cLog, cRegister, cTick, cScore, cOrange, cBallsLeft, cObjCount, cPeg, cBallCount, cBall, cRng,
                       cAddScore, cHit, cBalls, cSay, cStateSet, cStateGet, cRect, cCircle, cText};
}

std::unique_ptr<NativeMod> NativeMod::load(const std::filesystem::path& lib, const Manifest& m, HostServices& host, std::string* err) {
  auto nm = std::unique_ptr<NativeMod>(new NativeMod());
  nm->im_ = std::make_unique<Impl>();
  Impl& I = *nm->im_;
  I.id = m.id;
  I.host = &host;
#ifdef _WIN32
  I.lib = (void*)LoadLibraryW(lib.wstring().c_str());
  auto sym = [&](const char* n) { return (void*)GetProcAddress((HMODULE)I.lib, n); };
  if (!I.lib) { if (err) *err = "LoadLibrary failed for " + lib.string(); return nullptr; }
#else
  I.lib = dlopen(lib.string().c_str(), RTLD_NOW | RTLD_LOCAL);
  auto sym = [&](const char* n) { return dlsym(I.lib, n); };
  if (!I.lib) { if (err) { const char* e = dlerror(); *err = std::string("dlopen: ") + (e ? e : "failed"); } return nullptr; }
#endif
  typedef int32_t (*InitFn)(const PPModApi*);
  auto init = (InitFn)sym("pp_mod_init");
  if (!init) { if (err) *err = "the library exports no pp_mod_init"; return nullptr; }
  Cur c(&I);
  int32_t rc = init(&kApi);
  if (rc != 0) { if (err) *err = "pp_mod_init returned " + std::to_string(rc); return nullptr; }
  if (!I.hooked) { if (err) *err = "pp_mod_init did not register hooks"; return nullptr; }
  return nm;
}

NativeMod::~NativeMod() {
  if (!im_) return;
  if (im_->hooked && im_->hooks.shutdown) { Cur c(im_.get()); im_->hooks.shutdown(); }
}

void NativeMod::levelStart() { if (im_->hooks.on_level_start) { Cur c(im_.get()); im_->hooks.on_level_start(); } }
void NativeMod::tick() { if (im_->hooks.on_tick) { Cur c(im_.get()); im_->hooks.on_tick(im_->host->board ? im_->host->board->tick : 0); } }
void NativeMod::event(int type, int a, int b, int c, int ball) {
  Cur cur(im_.get());
  PPModHooks& h = im_->hooks;
  switch (type) {
    case Event::Shot: if (h.on_shot) { float f; std::memcpy(&f, &a, sizeof f); h.on_shot(f); } break;
    case Event::PegLit: if (h.on_peg_hit) h.on_peg_hit(a, ball); break;
    case Event::BallLost: if (h.on_ball_lost) h.on_ball_lost(); break;
    case Event::LevelDone: if (h.on_level_end) h.on_level_end(a); break;
    case Event::Power: if (h.on_power && a >= 1 && a <= 14) h.on_power(a); break;
    default: break;
  }
  (void)b; (void)c;
}
bool NativeMod::power(int pegIdx, int power) {
  if (!im_->hooks.on_activate_power) return false;
  Cur c(im_.get());
  return im_->hooks.on_activate_power(power, pegIdx) != 0;
}
void NativeMod::frame() { if (im_->hooks.on_frame) { Cur c(im_.get()); im_->hooks.on_frame(); } }

}
