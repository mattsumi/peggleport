#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "mods/hostproto.h"
#include "modhost/sandbox.h"
#include "peggleport_mod.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

using namespace pp::modhost;

static_assert(sizeof(PegRec) == kPegWire, "peg record layout");
static_assert(sizeof(BallRec) == kBallWire, "ball record layout");

namespace {

struct Channel {
#ifdef _WIN32
  HANDLE in = nullptr, out = nullptr;
  bool readAll(void* p, size_t n) {
    uint8_t* b = (uint8_t*)p;
    while (n) {
      DWORD got = 0;
      if (!ReadFile(in, b, (DWORD)std::min<size_t>(n, 1u << 20), &got, nullptr) || got == 0) return false;
      b += got;
      n -= got;
    }
    return true;
  }
  bool writeAll(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n) {
      DWORD put = 0;
      if (!WriteFile(out, b, (DWORD)std::min<size_t>(n, 1u << 20), &put, nullptr) || put == 0) return false;
      b += put;
      n -= put;
    }
    return true;
  }
#else
  int fd = 3;
  bool readAll(void* p, size_t n) {
    uint8_t* b = (uint8_t*)p;
    while (n) {
      ssize_t got = read(fd, b, n);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) return false;
      b += got;
      n -= (size_t)got;
    }
    return true;
  }
  bool writeAll(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n) {
      ssize_t put = write(fd, b, n);
      if (put < 0 && errno == EINTR) continue;
      if (put <= 0) return false;
      b += put;
      n -= (size_t)put;
    }
    return true;
  }
#endif
};

struct Snapshot {
  bool have = false;
  int32_t tick = 0;
  int32_t score[4]{};
  int32_t orange = 0;
  int32_t balls[4]{};
  std::vector<PegRec> pegs;
  std::vector<BallRec> ballv;
  std::map<std::string, double> state;
};

struct Host {
  Channel ch;
  std::vector<uint8_t> rx, tx, eff;
  uint32_t effCount = 0;
  bool effFull = false;
  int logCount = 0;
  PPModHooks hooks{};
  bool hooked = false;
  uint32_t hookMask = 0;
  uint32_t presRng = 0x1234567u;
  Context ctx = CtxNone;
  Snapshot snap;
  std::thread::id mainThread;
};

Host H;

[[noreturn]] void die(int code) {
#ifdef _WIN32
  ExitProcess((UINT)code);
#else
  _exit(code);
#endif
}

bool readFrame(std::vector<uint8_t>& out) {
  uint32_t len = 0;
  if (!H.ch.readAll(&len, 4)) return false;
  if (len < 1 || len > kMaxFrame) return false;
  out.resize(len);
  return H.ch.readAll(out.data(), len);
}

void sendFrame() {
  if (!H.ch.writeAll(H.tx.data(), H.tx.size())) die(3);
}

bool onMain() { return std::this_thread::get_id() == H.mainThread; }

bool effRoom(size_t n) {
  if (H.eff.size() + n > kMaxEffectBytes) {
    H.effFull = true;
    return false;
  }
  return true;
}

void aLog(const char* m) {
  if (!m || !onMain() || H.logCount >= 200 || !effRoom(900)) return;
  H.logCount++;
  Writer w(H.eff);
  w.u8(ELog);
  w.str(m, std::strlen(m), 800);
  H.effCount++;
}

void aRegister(const PPModHooks* h) {
  if (!h || !onMain()) return;
  H.hooks = PPModHooks{};
  std::memcpy(&H.hooks, h, std::min<size_t>(h->struct_size, sizeof(PPModHooks)));
  H.hooked = true;
  uint32_t m = 0;
  if (H.hooks.on_level_start) m |= hookBit(HLevelStart);
  if (H.hooks.on_tick) m |= hookBit(HTick);
  if (H.hooks.on_shot) m |= hookBit(HShot);
  if (H.hooks.on_peg_hit) m |= hookBit(HPegHit);
  if (H.hooks.on_ball_lost) m |= hookBit(HBallLost);
  if (H.hooks.on_level_end) m |= hookBit(HLevelEnd);
  if (H.hooks.on_power) m |= hookBit(HPower);
  if (H.hooks.on_activate_power) m |= hookBit(HActivate);
  if (H.hooks.on_frame) m |= hookBit(HFrame);
  if (H.hooks.shutdown) m |= hookBit(HShutdown);
  H.hookMask = m;
}

bool have() { return H.snap.have && onMain(); }
bool inSim() { return have() && H.ctx == CtxSim; }

int32_t aTick() { return have() ? H.snap.tick : -1; }
int32_t aScore(int32_t p) { return have() && p >= 0 && p < 4 ? H.snap.score[p] : 0; }
int32_t aOrange() { return have() ? H.snap.orange : 0; }
int32_t aBallsLeft(int32_t p) { return have() && p >= 0 && p < 4 ? H.snap.balls[p] : 0; }
int32_t aObjCount() { return have() ? (int32_t)H.snap.pegs.size() : 0; }
int32_t aPeg(int32_t id, PPPegInfo* o) {
  if (!have() || !o || id < 0 || id >= (int32_t)H.snap.pegs.size()) return 0;
  const PegRec& p = H.snap.pegs[(size_t)id];
  o->id = id;
  o->type = p.type;
  o->lit = p.lit;
  o->gone = p.gone;
  o->x = p.x;
  o->y = p.y;
  o->radius = p.radius;
  return 1;
}
int32_t aBallCount() { return have() ? (int32_t)H.snap.ballv.size() : 0; }
int32_t aBall(int32_t i, PPBallInfo* o) {
  if (!have() || !o || i < 0 || i >= (int32_t)H.snap.ballv.size()) return 0;
  const BallRec& b = H.snap.ballv[(size_t)i];
  o->id = i;
  o->alive = b.alive;
  o->x = b.x;
  o->y = b.y;
  o->vx = b.vx;
  o->vy = b.vy;
  o->r = b.r;
  return 1;
}

uint32_t aRng() {
  if (inSim()) {
    H.tx.clear();
    Writer w(H.tx);
    w.beginFrame(MRngReq);
    w.endFrame();
    sendFrame();
    std::vector<uint8_t> in;
    if (!readFrame(in) || in[0] != MRngVal || in.size() < 5) die(3);
    uint32_t v;
    std::memcpy(&v, in.data() + 1, 4);
    return v;
  }
  if (!onMain()) return 0;
  uint32_t x = H.presRng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  H.presRng = x;
  return x & 0x7fffffffu;
}

void putInt(Effect e, int32_t v) {
  if (!inSim() || !effRoom(8)) return;
  Writer w(H.eff);
  w.u8(e);
  w.i32(v);
  H.effCount++;
}
void aAddScore(int32_t n) { putInt(EAddScore, std::max(-1000000, std::min(1000000, n))); }
void aHit(int32_t id) { putInt(EHitPeg, id); }
void aBalls(int32_t n) { putInt(EAddBalls, std::max(-99, std::min(99, n))); }
void aSay(const char* t) {
  if (!t || !inSim() || !effRoom(80)) return;
  Writer w(H.eff);
  w.u8(ESay);
  w.str(t, std::strlen(t), 60);
  H.effCount++;
}
void aStateSet(const char* k, double v) {
  if (!inSim() || !stateSetAllowed(H.snap.state, k, v) || !effRoom(80)) return;
  H.snap.state[k] = v;
  Writer w(H.eff);
  w.u8(EState);
  w.str(k, std::strlen(k), kMaxStateKey);
  w.f64(v);
  H.effCount++;
}
int32_t aStateGet(const char* k, double* out) {
  if (!have() || !k || !out) return 0;
  auto it = H.snap.state.find(k);
  if (it == H.snap.state.end()) return 0;
  *out = it->second;
  return 1;
}
bool drawing() { return have() && H.ctx == CtxDraw; }
void aRect(float x, float y, float w, float h, uint32_t c, int32_t f) {
  if (!drawing() || !effRoom(32)) return;
  Writer wr(H.eff);
  wr.u8(ERect);
  wr.f32(x);
  wr.f32(y);
  wr.f32(w);
  wr.f32(h);
  wr.u32(c);
  wr.u8(f != 0);
  H.effCount++;
}
void aCircle(float x, float y, float r, uint32_t c, int32_t f) {
  if (!drawing() || !effRoom(32)) return;
  Writer wr(H.eff);
  wr.u8(ECircle);
  wr.f32(x);
  wr.f32(y);
  wr.f32(r);
  wr.u32(c);
  wr.u8(f != 0);
  H.effCount++;
}
void aText(float x, float y, const char* t, uint32_t c) {
  if (!drawing() || !t || !effRoom(1100)) return;
  Writer wr(H.eff);
  wr.u8(EText);
  wr.f32(x);
  wr.f32(y);
  wr.str(t, std::strlen(t), 1000);
  wr.u32(c);
  H.effCount++;
}

const PPModApi kApi = {PP_MOD_ABI_VERSION, sizeof(PPModApi), aLog, aRegister, aTick, aScore, aOrange, aBallsLeft, aObjCount, aPeg, aBallCount, aBall,
                       aRng, aAddScore, aHit, aBalls, aSay, aStateSet, aStateGet, aRect, aCircle, aText};

void resetEffects() {
  H.eff.clear();
  H.effCount = 0;
  H.effFull = false;
  H.logCount = 0;
}

void appendEffects(Writer& w) {
  w.u32(H.effCount);
  w.raw(H.eff.data(), H.eff.size());
}

bool parseSnapshot(Reader& r) {
  Snapshot& s = H.snap;
  s.tick = r.i32();
  for (int i = 0; i < 4; i++) s.score[i] = r.i32();
  s.orange = r.i32();
  for (int i = 0; i < 4; i++) s.balls[i] = r.i32();
  uint32_t np = r.u32();
  if (!r.ok() || (uint64_t)np * kPegWire > r.left()) return false;
  s.pegs.resize(np);
  if (np) std::memcpy(s.pegs.data(), r.take((size_t)np * kPegWire), (size_t)np * kPegWire);
  uint32_t nb = r.u32();
  if (!r.ok() || (uint64_t)nb * kBallWire > r.left()) return false;
  s.ballv.resize(nb);
  if (nb) std::memcpy(s.ballv.data(), r.take((size_t)nb * kBallWire), (size_t)nb * kBallWire);
  uint32_t ns = r.u32();
  s.state.clear();
  if (ns > kMaxStateWire) return false;
  for (uint32_t i = 0; i < ns; i++) {
    std::string k = r.str();
    double v = r.f64();
    if (!r.ok()) return false;
    s.state.emplace_hint(s.state.end(), std::move(k), v);
  }
  s.have = true;
  return r.ok();
}

void handleHook(Reader& r) {
  uint8_t hook = r.u8();
  int32_t a = r.i32();
  int32_t b = r.i32();
  uint8_t ctx = r.u8();
  H.snap.have = false;
  H.ctx = CtxNone;
  if (!r.ok() || hook >= HookCount || ctx > CtxDraw) die(4);
  if (ctx != CtxNone) {
    if (!parseSnapshot(r)) die(4);
    H.ctx = (Context)ctx;
  }
  resetEffects();
  int32_t ret = 0;
  PPModHooks& h = H.hooks;
  switch (hook) {
    case HLevelStart: if (h.on_level_start) h.on_level_start(); break;
    case HTick: if (h.on_tick) h.on_tick(a); break;
    case HShot: if (h.on_shot) { float f; std::memcpy(&f, &a, sizeof f); h.on_shot(f); } break;
    case HPegHit: if (h.on_peg_hit) h.on_peg_hit(a, b); break;
    case HBallLost: if (h.on_ball_lost) h.on_ball_lost(); break;
    case HLevelEnd: if (h.on_level_end) h.on_level_end(a); break;
    case HPower: if (h.on_power) h.on_power(a); break;
    case HActivate: if (h.on_activate_power) ret = h.on_activate_power(a, b); break;
    case HFrame: if (h.on_frame) h.on_frame(); break;
    default: break;
  }
  H.snap.have = false;
  H.ctx = CtxNone;
  H.tx.clear();
  Writer w(H.tx);
  w.beginFrame(MReply);
  w.i32(ret);
  w.u32(H.hookMask);
  appendEffects(w);
  w.endFrame();
  sendFrame();
}

#ifdef _WIN32
std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)std::max(n, 0), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
#endif

void* openLibrary(const std::string& path, std::string* err) {
#ifdef _WIN32
  void* lib = (void*)LoadLibraryW(widen(path).c_str());
  if (!lib) *err = "LoadLibrary failed for " + path;
  return lib;
#else
  void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    const char* e = dlerror();
    *err = std::string("dlopen: ") + (e ? e : "failed");
  }
  return lib;
#endif
}

void* findSymbol(void* lib, const char* name) {
#ifdef _WIN32
  return (void*)GetProcAddress((HMODULE)lib, name);
#else
  return dlsym(lib, name);
#endif
}

void replyInit(int32_t rc, const std::string& err) {
  H.tx.clear();
  Writer w(H.tx);
  w.beginFrame(MInitDone);
  w.i32(rc);
  w.str(err, 1000);
  w.u32(H.hookMask);
  appendEffects(w);
  w.endFrame();
  sendFrame();
}

#ifndef _WIN32
void startMonitor(pid_t parent) {
  std::thread([parent] {
    pollfd p;
    p.fd = 3;
    p.events = 0;
    p.revents = 0;
    for (;;) {
      int rc = poll(&p, 1, 200);
      if (rc > 0 && (p.revents & (POLLHUP | POLLERR | POLLNVAL))) _exit(1);
      if (parent > 0 && getppid() != parent) _exit(1);
    }
  }).detach();
}

void earlyHardening(pid_t parent) {
  signal(SIGPIPE, SIG_IGN);
  rlimit rl;
  rl.rlim_cur = 0;
  rl.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &rl);
  long lim = sysconf(_SC_OPEN_MAX);
  if (lim < 0 || lim > 4096) lim = 4096;
  for (long fd = 4; fd < lim; fd++) close((int)fd);
#ifdef __linux__
  prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
  if (parent > 0 && getppid() != parent) _exit(1);
}
#endif

int serve(long parent) {
  H.mainThread = std::this_thread::get_id();
#ifndef _WIN32
  earlyHardening((pid_t)parent);
#else
  (void)parent;
#endif
  std::vector<uint8_t> in;
  if (!readFrame(in) || in[0] != MInit) return 2;
  Reader r(in.data() + 1, in.size() - 1);
  uint32_t version = r.u32();
  std::string libPath = r.str(), pluginDir = r.str(), modId = r.str();
  uint8_t probeOnly = r.u8();
  if (!r.ok() || version != kVersion) return 2;
#ifndef _WIN32
  char real[4096];
  if (!libPath.empty() && realpath(libPath.c_str(), real)) libPath = real;
  if (!pluginDir.empty() && realpath(pluginDir.c_str(), real)) pluginDir = real;
#endif

  SandboxReport rep = applySandbox(pluginDir);
#ifndef _WIN32
  startMonitor((pid_t)parent);
#endif

  H.tx.clear();
  {
    Writer w(H.tx);
    w.beginFrame(MReady);
    w.u32(kVersion);
    w.u32(rep.protections);
    w.str(rep.detail, 400);
    w.endFrame();
  }
  sendFrame();
  if (probeOnly) return 0;

  if (!readFrame(in) || in[0] != MGo) return 0;

  std::string err;
  void* lib = openLibrary(libPath, &err);
  if (!lib) {
    replyInit(-1, err);
    return 0;
  }
  typedef int32_t (*InitFn)(const PPModApi*);
  InitFn init = (InitFn)findSymbol(lib, "pp_mod_init");
  if (!init) {
    replyInit(-1, "the library exports no pp_mod_init");
    return 0;
  }
  resetEffects();
  int32_t rc = init(&kApi);
  if (rc != 0) {
    replyInit(-1, "pp_mod_init returned " + std::to_string(rc));
    return 0;
  }
  if (!H.hooked) {
    replyInit(-1, "pp_mod_init did not register hooks");
    return 0;
  }
  replyInit(0, "");

  for (;;) {
    if (!readFrame(in)) return 0;
    Reader q(in.data() + 1, in.size() - 1);
    if (in[0] == MHook) {
      handleHook(q);
    } else if (in[0] == MShutdown) {
      H.snap.have = false;
      H.ctx = CtxNone;
      resetEffects();
      if (H.hooked && H.hooks.shutdown) H.hooks.shutdown();
      H.tx.clear();
      Writer w(H.tx);
      w.beginFrame(MBye);
      appendEffects(w);
      w.endFrame();
      sendFrame();
      return 0;
    } else {
      return 4;
    }
  }
}

}

int main(int argc, char** argv) {
  if (argc < 4 || std::strcmp(argv[1], "--serve") != 0) {
    std::fputs("This program is started by PegglePort to run native mod plugins.\n", stderr);
    return 1;
  }
#ifdef _WIN32
  H.ch.in = (HANDLE)(uintptr_t)std::strtoull(argv[2], nullptr, 16);
  H.ch.out = (HANDLE)(uintptr_t)std::strtoull(argv[3], nullptr, 16);
  int code = serve(0);
  ExitProcess((UINT)code);
#else
  H.ch.fd = std::atoi(argv[2]);
  int code = serve(std::atol(argv[3]));
  _exit(code);
#endif
}
