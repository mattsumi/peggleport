#include "mods/native.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "mods/hostproc.h"
#include "mods/hostproto.h"
#include "mods/hostreg.h"
#include "pegtypes.h"
#include "sim.h"

namespace fs = std::filesystem;
namespace mh = pp::modhost;
namespace pp::mods {

namespace {
constexpr int kSimMs = 1000;
constexpr int kFrameMs = 250;
constexpr int kStartMs = 10000;
constexpr int kInitMs = 5000;
constexpr int kByeMs = 300;
const char* kHookNames[] = {"on_level_start", "on_tick", "on_shot", "on_peg_hit", "on_ball_lost", "on_level_end", "on_power", "on_activate_power", "on_frame", "cleanup"};
const char* kCallNames[] = {"on_hit of a peg type", "on_clear of a peg type", "on_activate of a Master", "on_shot of a Master", "on_end of a Master"};
}

bool NativeStatus::full() const { return protections == mh::kAllProtections; }

struct NativeMod::Impl {
  std::string id;
  HostServices* host = nullptr;
  std::unique_ptr<HostProcess> proc;
  std::mutex mu;
  uint32_t mask = 0;
  uint32_t prot = 0;
  bool dead = false;
  bool loaded = false;
  PhysicsEdit loadPhysics;
  std::vector<NativeMod::PegTypeReg> pegTypes;
  std::vector<NativeMasterDef> masters;
  std::string why;
  std::vector<uint8_t> tx, rx;
  std::vector<std::pair<int, uint16_t>> keyOf;
  std::vector<std::string> keys;
  std::vector<uint32_t> customPeg;
  std::vector<uint16_t> customKey;
  std::vector<int32_t> customHits;
  std::chrono::steady_clock::time_point logWindow;
  int logCount = 0;
  bool logNoted = false;
};

namespace {

void emitLog(NativeMod::Impl& I, const std::string& m) {
  if (!I.host || !I.host->log) return;
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
  I.host->log(I.id, pp::log::Level::Info, m.substr(0, 800));
}

void fail(NativeMod::Impl& I, const std::string& why) {
  if (I.dead) return;
  I.dead = true;
  I.why = why;
  if (I.host && I.host->log) I.host->log(I.id, pp::log::Level::Error, why + "; the plugin is switched off");
  if (I.proc) I.proc->stop(true);
}

mh::Context contextOf(const NativeMod::Impl& I) {
  if (!I.host || !I.host->board) return mh::CtxNone;
  if (I.host->ctx == Ctx::Sim) return mh::CtxSim;
  if (I.host->ctx == Ctx::Draw) return mh::CtxDraw;
  return mh::CtxNone;
}

void writeSnapshot(mh::Writer& w, const Board& b, NativeMod::Impl& I) {
  w.i32(b.tick);
  for (int p = 0; p < 4; p++) w.i32(b.liveScore(p));
  w.i32(b.orangeLeft);
  for (int p = 0; p < 4; p++) w.i32(b.ballsLeft[p]);
  const size_t no = b.objs.size();
  w.u32((uint32_t)no);
  uint8_t* d = w.grow(no * mh::kPegWire);
  I.keyOf.clear();
  I.keys.clear();
  I.customPeg.clear();
  I.customKey.clear();
  I.customHits.clear();
  for (size_t i = 0; i < no; i++) {
    const Obj& o = b.objs[i];
    mh::PegRec r;
    r.type = (int16_t)o.pegType;
    r.lit = o.lit ? 1 : 0;
    r.gone = o.gone ? 1 : 0;
    r.x = o.pos.x;
    r.y = o.pos.y;
    r.radius = o.radius;
    std::memcpy(d, &r, sizeof r);
    d += sizeof r;
    if (o.custom <= 0) continue;
    const PegTypeDef* def = pegTypeDef(o.custom);
    if (!def) continue;
    size_t at = 0;
    while (at < I.keyOf.size() && I.keyOf[at].first != o.custom) at++;
    if (at == I.keyOf.size()) {
      if (I.keys.size() >= mh::kMaxSnapKeys) continue;
      I.keyOf.push_back({o.custom, (uint16_t)I.keys.size()});
      I.keys.push_back(def->mod == I.id ? def->id : def->key());
    }
    I.customPeg.push_back((uint32_t)i);
    I.customKey.push_back(I.keyOf[at].second);
    I.customHits.push_back((int32_t)o.hits);
  }
  const size_t nb = b.balls.size();
  w.u32((uint32_t)nb);
  d = w.grow(nb * mh::kBallWire);
  for (const Ball& bl : b.balls) {
    mh::BallRec r;
    r.alive = bl.alive ? 1 : 0;
    r.x = bl.pos.x;
    r.y = bl.pos.y;
    r.vx = bl.vel.x;
    r.vy = bl.vel.y;
    r.r = bl.r;
    std::memcpy(d, &r, sizeof r);
    d += sizeof r;
  }
  uint32_t ns = 0;
  for (auto& kv : b.modState) if (kv.first.size() <= 65535 && ns < mh::kMaxStateWire) ns++;
  w.u32(ns);
  uint32_t put = 0;
  for (auto& kv : b.modState) {
    if (kv.first.size() > 65535 || put >= ns) continue;
    w.str(kv.first);
    w.f64(kv.second);
    put++;
  }
  const bool phys = !b.physics.isDefault();
  const size_t nc = I.customPeg.size();
  w.u8((uint8_t)((phys ? mh::SnapPhysics : 0) | (nc ? mh::SnapCustom : 0)));
  if (phys)
    for (int k = 0; k < PhysicsParams::KeyCount; k++) w.f32(b.physics.at(k));
  if (nc) {
    w.u32((uint32_t)nc);
    d = w.grow(nc * mh::kCustomWire);
    for (size_t i = 0; i < nc; i++) {
      std::memcpy(d, &I.customPeg[i], 4);
      std::memcpy(d + 4, &I.customKey[i], 2);
      std::memcpy(d + 6, &I.customHits[i], 4);
      d += mh::kCustomWire;
    }
    w.u32((uint32_t)I.keys.size());
    for (const std::string& k : I.keys) w.str(k);
  }
}

bool applyEffects(NativeMod::Impl& I, mh::Reader& r, mh::Context ctx) {
  uint32_t n = r.u32();
  HostServices& hs = *I.host;
  for (uint32_t i = 0; i < n; i++) {
    uint8_t k = r.u8();
    if (!r.ok()) return false;
    switch (k) {
      case mh::ELog: {
        std::string s = r.str(800);
        if (!r.ok()) return false;
        emitLog(I, s);
        break;
      }
      case mh::EAddScore: {
        int32_t v = r.i32();
        if (ctx == mh::CtxSim && hs.queue) hs.queue({ModCmd::AddScore, std::max(-1000000, std::min(1000000, v)), 0, 0, ""});
        break;
      }
      case mh::EHitPeg: {
        int32_t v = r.i32();
        if (ctx == mh::CtxSim && hs.queue) hs.queue({ModCmd::HitPeg, v, 0, 0, ""});
        break;
      }
      case mh::EAddBalls: {
        int32_t v = r.i32();
        if (ctx == mh::CtxSim && hs.queue) hs.queue({ModCmd::AddBalls, std::max(-99, std::min(99, v)), 0, 0, ""});
        break;
      }
      case mh::ESay: {
        std::string s = r.str(60);
        if (!r.ok()) return false;
        if (ctx == mh::CtxSim && hs.queue) hs.queue({ModCmd::Say, 0, 0, 0, s});
        break;
      }
      case mh::EState: {
        std::string key = r.str(mh::kMaxStateKey);
        double v = r.f64();
        if (!r.ok()) return false;
        if (ctx == mh::CtxSim && hs.board && mh::stateSetAllowed(hs.board->modState, key.c_str(), v)) hs.board->modState[key.c_str()] = v;
        break;
      }
      case mh::ERect: {
        float x = r.f32(), y = r.f32(), w = r.f32(), h = r.f32();
        uint32_t c = r.u32();
        uint8_t f = r.u8();
        if (!r.ok()) return false;
        if (ctx == mh::CtxDraw && hs.sink) hs.sink->rect(x, y, w, h, c, f != 0, false);
        break;
      }
      case mh::ECircle: {
        float x = r.f32(), y = r.f32(), rad = r.f32();
        uint32_t c = r.u32();
        uint8_t f = r.u8();
        if (!r.ok()) return false;
        if (ctx == mh::CtxDraw && hs.sink) hs.sink->circle(x, y, rad, c, f != 0, false);
        break;
      }
      case mh::ESetPhysics: {
        PhysicsEdit e;
        if (!mh::getPhysics(r, e)) return false;
        if (ctx == mh::CtxSim && hs.queue && e.any()) {
          ModCmd c;
          c.k = ModCmd::SetPhysics;
          c.pe = e;
          hs.queue(c);
        }
        break;
      }
      case mh::ESetPegType: {
        int32_t peg = r.i32();
        std::string t = r.str(80);
        if (!r.ok()) return false;
        if (ctx == mh::CtxSim && hs.queue) {
          if (t.find(':') == std::string::npos)
            for (auto& reg : I.pegTypes)
              if (reg.def.id == t) {
                t = reg.def.mod + ":" + t;
                break;
              }
          hs.queue({ModCmd::SetPegType, peg, 0, 0, t});
        }
        break;
      }
      case mh::EText: {
        float x = r.f32(), y = r.f32();
        std::string s = r.str(1000);
        uint32_t c = r.u32();
        if (!r.ok()) return false;
        if (ctx == mh::CtxDraw && hs.sink) hs.sink->text(x, y, s.c_str(), c, 0, 1.0f, false);
        break;
      }
      default:
        return false;
    }
  }
  return r.ok();
}

bool exchange(NativeMod::Impl& I, const std::string& name, int timeoutMs, mh::Context ctx, int32_t* ret) {
  auto start = std::chrono::steady_clock::now();
  IoResult io = I.proc->send(I.tx, timeoutMs);
  if (io != IoResult::Ok) {
    fail(I, io == IoResult::Timeout ? "the native plugin did not take the call to " + name + " within " + std::to_string(timeoutMs) + " ms" : "the native plugin helper is gone (" + I.proc->exitText() + ")");
    return false;
  }
  for (;;) {
    auto used = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    int left = (int)std::max<long long>(0, timeoutMs - used);
    io = I.proc->receive(I.rx, left);
    if (io == IoResult::Timeout) {
      fail(I, "the native plugin did not return from " + name + " within " + std::to_string(timeoutMs) + " ms");
      return false;
    }
    if (io == IoResult::Closed) {
      std::string p = I.proc->problem();
      fail(I, p.empty() ? "the native plugin helper stopped during " + name + " (" + I.proc->exitText() + ")" : "the native plugin helper broke the protocol: " + p);
      return false;
    }
    if (I.rx[0] == mh::MRngReq) {
      uint32_t v = ctx == mh::CtxSim && I.host->board ? I.host->board->rng.rand() : 0;
      I.tx.clear();
      mh::Writer rw(I.tx);
      rw.beginFrame(mh::MRngVal);
      rw.u32(v);
      rw.endFrame();
      io = I.proc->send(I.tx, kSimMs);
      if (io != IoResult::Ok) {
        fail(I, "the native plugin helper is gone (" + I.proc->exitText() + ")");
        return false;
      }
      continue;
    }
    if (I.rx[0] != mh::MReply) {
      fail(I, "the native plugin helper sent an unexpected message");
      return false;
    }
    mh::Reader r(I.rx.data() + 1, I.rx.size() - 1);
    int32_t rv = r.i32();
    uint32_t mask = r.u32();
    if (!r.ok() || !applyEffects(I, r, ctx)) {
      fail(I, "the native plugin helper sent a malformed reply");
      return false;
    }
    I.mask = mask & ((1u << mh::HookCount) - 1);
    if (ret) *ret = rv;
    return true;
  }
}

bool callHook(NativeMod::Impl& I, mh::Hook h, int32_t a, int32_t b, int32_t* ret) {
  std::lock_guard<std::mutex> lock(I.mu);
  if (I.dead || !I.proc) return false;
  const mh::Context ctx = contextOf(I);
  const int timeoutMs = h == mh::HFrame ? kFrameMs : kSimMs;
  I.tx.clear();
  mh::Writer w(I.tx);
  w.beginFrame(mh::MHook);
  w.u8(h);
  w.i32(a);
  w.i32(b);
  w.u8(ctx);
  if (ctx != mh::CtxNone) writeSnapshot(w, *I.host->board, I);
  w.endFrame();
  return exchange(I, kHookNames[h], timeoutMs, ctx, ret);
}

bool callBack(NativeMod::Impl& I, mh::Call kind, uint32_t handle, int32_t a, int32_t b) {
  std::lock_guard<std::mutex> lock(I.mu);
  if (I.dead || !I.proc) return false;
  const mh::Context ctx = contextOf(I);
  I.tx.clear();
  mh::Writer w(I.tx);
  w.beginFrame(mh::MCall);
  w.u8(kind);
  w.u32(handle);
  w.i32(a);
  w.i32(b);
  w.u8(ctx);
  if (ctx != mh::CtxNone) writeSnapshot(w, *I.host->board, I);
  w.endFrame();
  return exchange(I, kCallNames[kind], kSimMs, ctx, nullptr);
}

bool readRegistrations(mh::Reader& r, const std::string& modId, PhysicsEdit& phys, std::vector<NativeMod::PegTypeReg>& pegs, std::vector<NativeMasterDef>& masters) {
  if (!mh::getPhysics(r, phys)) return false;
  const uint32_t np = r.u32();
  if (!r.ok() || np > mh::kMaxPegTypes) return false;
  std::vector<PegTypeDef> defs;
  for (uint32_t i = 0; i < np; i++) {
    mh::RawPegType raw;
    PegTypeDef d;
    if (!mh::getPegType(r, raw) || !mh::buildPegType(raw, modId, defs, d)) return false;
    defs.push_back(d);
    NativeMod::PegTypeReg reg;
    reg.def = d;
    reg.handle = i;
    pegs.push_back(reg);
  }
  const uint32_t nm = r.u32();
  if (!r.ok() || nm > mh::kMaxMasters) return false;
  std::vector<mh::MasterSpec> specs;
  for (uint32_t i = 0; i < nm; i++) {
    mh::RawMaster raw;
    mh::MasterSpec sp;
    if (!mh::getMaster(r, raw) || mh::buildMaster(raw, specs, sp) != 0) return false;
    specs.push_back(sp);
    NativeMasterDef def;
    static_cast<mh::MasterSpec&>(def) = sp;
    def.handle = i;
    masters.push_back(def);
  }
  return r.ok();
}

std::string join(const std::vector<std::string>& v) {
  std::string o;
  for (size_t i = 0; i < v.size(); i++) o += (i ? ", " : "") + v[i];
  return o;
}

bool readReady(HostProcess& p, NativeStatus& st, std::string* err) {
  std::vector<uint8_t> rx;
  IoResult io = p.receive(rx, kStartMs);
  if (io != IoResult::Ok || rx[0] != mh::MReady) {
    if (err) *err = io == IoResult::Timeout ? "the plugin helper did not start in time" : "the plugin helper stopped at start-up (" + p.exitText() + ")";
    return false;
  }
  mh::Reader r(rx.data() + 1, rx.size() - 1);
  uint32_t version = r.u32();
  uint32_t prot = r.u32();
  std::string detail = r.str(400);
  if (!r.ok() || version != mh::kVersion) {
    if (err) *err = "the plugin helper speaks a different protocol";
    return false;
  }
  st.protections = prot & mh::kAllProtections;
  st.detail = detail;
  st.probed = true;
  return true;
}

void sendInit(HostProcess& p, const std::string& lib, const std::string& dir, const std::string& id, bool probeOnly, bool cosmetic, std::vector<uint8_t>& tx, std::string* err) {
  tx.clear();
  mh::Writer w(tx);
  w.beginFrame(mh::MInit);
  w.u32(mh::kVersion);
  w.str(lib);
  w.str(dir);
  w.str(id);
  w.u8(probeOnly ? 1 : 0);
  w.u8(cosmetic ? 1 : 0);
  w.endFrame();
  if (p.send(tx, 5000) != IoResult::Ok && err) *err = "cannot talk to the plugin helper (" + p.exitText() + ")";
}

}

std::string NativeMod::protectionText(uint32_t p) {
  std::vector<std::string> v;
  if (p & mh::kNoNetwork) v.push_back("no network");
  if (p & mh::kNoFileWrite) v.push_back("cannot write files");
  if (p & mh::kNoSpawn) v.push_back("cannot start programs");
  if (p & mh::kReadRestricted) v.push_back("can only read its own folder");
  return join(v);
}

std::string NativeMod::missingText(uint32_t p) {
  std::vector<std::string> v;
  if (!(p & mh::kNoNetwork)) v.push_back("the network is not blocked");
  if (!(p & mh::kNoFileWrite)) v.push_back("file writes are not blocked");
  if (!(p & mh::kNoSpawn)) v.push_back("starting programs is not blocked");
  if (!(p & mh::kReadRestricted)) v.push_back("reading other files is not blocked");
  return join(v);
}

NativeStatus NativeMod::probe() {
  static NativeStatus cached;
  static bool done = false;
  if (done) return cached;
  done = true;
  fs::path exe = findModHost();
  if (exe.empty()) {
    cached.helperMissing = true;
    cached.detail = "the plugin helper (pp_modhost) was not found next to the game";
    return cached;
  }
  std::string err;
  auto proc = HostProcess::start(exe, &err);
  if (!proc) {
    cached.detail = err;
    return cached;
  }
  std::vector<uint8_t> tx;
  sendInit(*proc, "", "", "", true, false, tx, &err);
  if (!readReady(*proc, cached, &err)) cached.detail = err;
  proc->stop();
  return cached;
}

std::unique_ptr<NativeMod> NativeMod::load(const fs::path& lib, const Manifest& m, HostServices& host, std::string* err, const NativeOptions& opt) {
  NativeStatus localStatus;
  NativeStatus& st = opt.status ? *opt.status : localStatus;
  st = NativeStatus();
  auto setErr = [&](const std::string& e) { if (err) *err = e; };
  fs::path exe = findModHost();
  if (exe.empty()) {
    st.helperMissing = true;
    setErr("the plugin helper (pp_modhost) was not found next to the game");
    return nullptr;
  }
  std::string herr;
  auto proc = HostProcess::start(exe, &herr);
  if (!proc) {
    setErr(herr);
    return nullptr;
  }
  auto nm = std::unique_ptr<NativeMod>(new NativeMod());
  nm->im_ = std::make_unique<Impl>();
  Impl& I = *nm->im_;
  I.id = m.id;
  I.host = &host;
  I.proc = std::move(proc);
  std::error_code ec;
  fs::path dir = opt.dir.empty() ? lib.parent_path() : opt.dir;
  fs::path canon = fs::canonical(dir, ec);
  if (!ec) dir = canon;
  fs::path libPath = fs::canonical(lib, ec);
  if (ec) libPath = lib;
  sendInit(*I.proc, libPath.string(), dir.string(), m.id, false, m.cosmetic, I.tx, &herr);
  if (!herr.empty()) {
    setErr(herr);
    return nullptr;
  }
  if (!readReady(*I.proc, st, &herr)) {
    setErr(herr);
    return nullptr;
  }
  I.prot = st.protections;
  if (!st.full() && !opt.allowUnrestricted) {
    st.needsUnrestricted = true;
    setErr("this plugin could not be restricted on this system (" + missingText(st.protections) + ")");
    return nullptr;
  }
  I.tx.clear();
  {
    mh::Writer w(I.tx);
    w.beginFrame(mh::MGo);
    w.endFrame();
  }
  if (I.proc->send(I.tx, 5000) != IoResult::Ok) {
    setErr("cannot talk to the plugin helper (" + I.proc->exitText() + ")");
    return nullptr;
  }
  IoResult io = I.proc->receive(I.rx, kInitMs);
  if (io != IoResult::Ok || I.rx[0] != mh::MInitDone) {
    setErr(io == IoResult::Timeout ? "pp_mod_init did not return within " + std::to_string(kInitMs / 1000) + " seconds" : "the native plugin stopped while it was loading (" + I.proc->exitText() + ")");
    return nullptr;
  }
  mh::Reader r(I.rx.data() + 1, I.rx.size() - 1);
  int32_t rc = r.i32();
  std::string perr = r.str(1200);
  uint32_t mask = r.u32();
  PhysicsEdit phys;
  std::vector<PegTypeReg> pegs;
  std::vector<NativeMasterDef> masters;
  if (!r.ok() || !readRegistrations(r, m.id, phys, pegs, masters) || !applyEffects(I, r, mh::CtxNone)) {
    setErr("the plugin helper sent a malformed reply");
    return nullptr;
  }
  if (rc != 0) {
    setErr(perr.empty() ? "the plugin failed to load" : perr);
    return nullptr;
  }
  I.loadPhysics = phys;
  I.pegTypes = std::move(pegs);
  I.masters = std::move(masters);
  I.mask = mask & ((1u << mh::HookCount) - 1);
  I.loaded = true;
  return nm;
}

NativeMod::~NativeMod() {
  if (!im_) return;
  Impl& I = *im_;
  bool polite = I.loaded;
  if (!I.dead && I.proc && I.proc->running() && (I.mask & mh::hookBit(mh::HShutdown))) {
    I.tx.clear();
    mh::Writer w(I.tx);
    w.beginFrame(mh::MShutdown);
    w.endFrame();
    polite = false;
    if (I.proc->send(I.tx, 250) == IoResult::Ok && I.proc->receive(I.rx, kByeMs) == IoResult::Ok && I.rx[0] == mh::MBye) {
      mh::Reader r(I.rx.data() + 1, I.rx.size() - 1);
      polite = applyEffects(I, r, mh::CtxNone);
    }
  }
  if (I.proc) I.proc->stop(!polite);
}

bool NativeMod::dead() const { return im_->dead; }
const std::string& NativeMod::deadReason() const { return im_->why; }
uint32_t NativeMod::protections() const { return im_->prot; }

const PhysicsEdit& NativeMod::loadPhysics() const { return im_->loadPhysics; }
const std::vector<NativeMod::PegTypeReg>& NativeMod::pegTypes() const { return im_->pegTypes; }
void NativeMod::pegHook(const PegTypeReg& r, bool clear, int peg, int ball) {
  if (clear ? !r.def.onClear : !r.def.onHit) return;
  callBack(*im_, clear ? mh::CPegClear : mh::CPegHit, r.handle, peg, ball);
}
const std::vector<NativeMasterDef>& NativeMod::masters() const { return im_->masters; }
void NativeMod::masterCall(const NativeMasterDef& m, int which, int player, int peg) {
  if (which == 0 && m.hasActivate) callBack(*im_, mh::CMasterActivate, m.handle, player, peg);
  else if (which == 1 && m.hasShot) callBack(*im_, mh::CMasterShot, m.handle, player, 0);
  else if (which == 2 && m.hasEnd) callBack(*im_, mh::CMasterEnd, m.handle, player, 0);
}

void NativeMod::levelStart() {
  if (im_->mask & mh::hookBit(mh::HLevelStart)) callHook(*im_, mh::HLevelStart, 0, 0, nullptr);
}
void NativeMod::tick() {
  if (im_->mask & mh::hookBit(mh::HTick)) callHook(*im_, mh::HTick, im_->host->board ? im_->host->board->tick : 0, 0, nullptr);
}
void NativeMod::event(int type, int a, int b, int c, int ball) {
  const uint32_t mask = im_->mask;
  switch (type) {
    case Event::Shot: if (mask & mh::hookBit(mh::HShot)) callHook(*im_, mh::HShot, a, 0, nullptr); break;
    case Event::PegLit: if (mask & mh::hookBit(mh::HPegHit)) callHook(*im_, mh::HPegHit, a, ball, nullptr); break;
    case Event::BallLost: if (mask & mh::hookBit(mh::HBallLost)) callHook(*im_, mh::HBallLost, 0, 0, nullptr); break;
    case Event::LevelDone: if (mask & mh::hookBit(mh::HLevelEnd)) callHook(*im_, mh::HLevelEnd, a, 0, nullptr); break;
    case Event::Power: if ((mask & mh::hookBit(mh::HPower)) && a >= 1 && a <= 14) callHook(*im_, mh::HPower, a, 0, nullptr); break;
    default: break;
  }
  (void)b; (void)c;
}
bool NativeMod::power(int pegIdx, int power) {
  if (!(im_->mask & mh::hookBit(mh::HActivate))) return false;
  int32_t ret = 0;
  if (!callHook(*im_, mh::HActivate, power, pegIdx, &ret)) return false;
  return ret != 0;
}
void NativeMod::frame() {
  if (im_->mask & mh::hookBit(mh::HFrame)) callHook(*im_, mh::HFrame, 0, 0, nullptr);
}

}
