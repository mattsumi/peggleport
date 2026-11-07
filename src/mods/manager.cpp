#include "mods/manager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <filesystem>

#include "mods/json.h"
#include "sim.h"
#include "vfs.h"

#include "lua.h"

namespace fs = std::filesystem;
namespace pp::mods {

const char* ModInfo::stateName() const {
  switch (state) {
    case Off: return "off";
    case NeedsConsent: return "needs consent";
    case Active: return "active";
    case Failed: return "failed";
  }
  return "?";
}

namespace {
void failMod(std::vector<std::unique_ptr<ModInfo>>& mods, std::unique_ptr<ModInfo> m, const std::string& id, const std::string& name, const std::string& err) {
  m->m.id = id;
  m->m.name = name;
  m->state = ModInfo::Failed;
  m->error = err;
  pp::log::error("mod:" + id, "cannot load: " + err);
  mods.push_back(std::move(m));
}
}

ModManager::ModManager() {
  host_.log = [](const std::string& id, pp::log::Level level, const std::string& line) { pp::log::write(level, "mod:" + id, line); };
  host_.message = [this](const std::string& id, const std::string& text) {
    pp::log::info("mod:" + id, "message: " + text);
    if (messages_.size() < 20) messages_.push_back(id + ": " + text);
  };
  host_.dataDir = [this](const std::string& id) {
    std::error_code ec;
    fs::path d = (userDir_.empty() ? fs::temp_directory_path() : userDir_) / "mod-data" / sanitizeId(id);
    fs::create_directories(d, ec);
    return d.string();
  };
  host_.queue = [this](const ModCmd& c) {
    if (queue_.size() < 4096) {
      queue_.push_back(c);
    } else if (!queueFull_) {
      queueFull_ = true;
      pp::log::warn("mods", "too many queued mod commands in one tick; the extra commands are dropped");
    }
  };
}
ModManager::~ModManager() { deactivate(); }

void ModManager::setUserDir(const fs::path& d) { userDir_ = d; }

ModInfo* ModManager::find(const std::string& id) {
  for (auto& m : mods_) if (m->m.id == id) return m.get();
  return nullptr;
}

void ModManager::loadConfig(std::vector<std::string>& enabled) {
  if (userDir_.empty()) return;
  std::string text;
  Json j;
  std::error_code ec;
  if (!fs::exists(userDir_ / "mods.json", ec)) return;
  if (!readFile(userDir_ / "mods.json", text, 1 << 20) || !Json::parse(text, j, nullptr) || j.t != Json::Obj) {
    pp::log::warn("mods", "cannot read " + (userDir_ / "mods.json").string() + "; no mods are enabled");
    return;
  }
  if (auto* e = j.get("enabled")) for (auto& x : e->a) enabled.push_back(x.asStr());
  if (auto* o = j.get("order")) for (auto& x : o->a) order_.push_back(x.asStr());
  if (auto* c = j.get("consent")) for (auto& [k, v] : c->o) consent_[k] = v.asStr();
}

bool ModManager::save() const {
  if (userDir_.empty()) return false;
  Json j = Json::object();
  j.set("version", Json::num(1));
  Json en = Json::array(), ord = Json::array(), con = Json::object();
  for (auto& m : mods_) {
    ord.a.push_back(Json::str(m->m.id));
    if (m->enabled && !m->fromCli) en.a.push_back(Json::str(m->m.id));
  }
  for (auto& [h, id] : consent_) con.set(h, Json::str(id));
  j.set("enabled", en);
  j.set("order", ord);
  j.set("consent", con);
  std::error_code ec;
  fs::create_directories(userDir_, ec);
  std::ofstream f(userDir_ / "mods.json", std::ios::binary);
  if (!f) {
    pp::log::error("mods", "cannot save " + (userDir_ / "mods.json").string());
    return false;
  }
  f << j.dump(2) << "\n";
  return true;
}

void ModManager::addMod(const fs::path& srcIn, bool cli) {
  auto m = std::make_unique<ModInfo>();
  m->fromCli = cli;
  m->source = srcIn;
  std::error_code ec;
  fs::path dir = srcIn;
  if (fs::is_regular_file(srcIn, ec)) {
    std::string ext = srcIn.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext != ".zip") {
      pp::log::info("mods", "ignored " + srcIn.filename().string() + " (a mod is a folder or a .zip file)");
      return;
    }
    m->zip = true;
    const std::string zid = sanitizeId(srcIn.stem().string());
    std::string bytes;
    if (!readFile(srcIn, bytes, 256u << 20)) {
      failMod(mods_, std::move(m), zid, srcIn.stem().string(), "cannot read " + srcIn.filename().string());
      return;
    }
    uint64_t zh = fnvMix(1469598103934665603ull, bytes.data(), bytes.size());
    fs::path cache = (userDir_.empty() ? fs::temp_directory_path() : modsDir()) / ".cache" / (zid + "-" + hex64(zh));
    if (!fs::exists(cache / ".ok", ec)) {
      fs::remove_all(cache, ec);
      std::string e;
      bool unpacked = extractZip(srcIn, cache, &e);
      if (!unpacked && !fs::exists(cache, ec)) {
        failMod(mods_, std::move(m), zid, srcIn.stem().string(), "zip: " + e);
        return;
      }
      if (!e.empty()) pp::log::warn("mod:" + zid, "zip: " + e);
      std::ofstream(cache / ".ok") << "ok\n";
    }
    dir = cache;
    if (!fs::exists(dir / "mod.ini", ec) && !fs::exists(dir / "mod.json", ec)) {
      std::vector<fs::path> subs;
      for (auto& e : fs::directory_iterator(dir, ec)) if (e.is_directory(ec) && e.path().filename().string()[0] != '.') subs.push_back(e.path());
      if (subs.size() == 1 && (fs::exists(subs[0] / "mod.ini", ec) || fs::exists(subs[0] / "mod.json", ec))) dir = subs[0];
    }
  } else if (!fs::is_directory(srcIn, ec)) {
    pp::log::warn("mods", "cannot find mod " + srcIn.string());
    return;
  }
  m->dir = dir;
  std::string err;
  if (!loadManifest(dir, m->m, &err)) {
    failMod(mods_, std::move(m), sanitizeId(dir.filename().string()), dir.filename().string(), err);
    return;
  }
  if (m->zip) m->m.id = m->m.id.empty() ? sanitizeId(srcIn.stem().string()) : m->m.id;
  m->hash = hashModDir(dir);
  m->hashHex = hex64(m->hash);
  m->scan = scanMod(dir, m->m);
  for (auto it = mods_.begin(); it != mods_.end(); ++it)
    if ((*it)->m.id == m->m.id) {
      if (!cli) {
        pp::log::warn("mod:" + m->m.id, "ignored " + srcIn.string() + ": another mod already uses this id");
        return;
      }
      mods_.erase(it);
      break;
    }
  ModInfo* raw = m.get();
  mods_.push_back(std::move(m));
  logDiscovered(*raw);
}

void ModManager::logDiscovered(const ModInfo& mi) {
  const std::string src = "mod:" + mi.m.id;
  pp::log::info(src, "found " + mi.m.name + " " + mi.m.version + (mi.m.author.empty() ? "" : " by " + mi.m.author) + " in " + mi.source.string() + " (content hash " + mi.hashHex + ")");
  for (auto& w : mi.m.warnings) pp::log::warn(src, "manifest: " + w);
  pp::log::info(src, "scan: " + mi.scan.summary() + " (" + std::to_string(mi.scan.filesScanned) + " file(s))");
  size_t shown = 0;
  for (auto& f : mi.scan.findings) {
    if (shown == 100) {
      pp::log::info(src, "scan: " + std::to_string(mi.scan.findings.size() - shown) + " more finding(s), see the scan report");
      break;
    }
    pp::log::info(src, "scan " + f.describe());
    shown++;
  }
}

void ModManager::discover(const std::vector<std::string>& cliMods) {
  mods_.clear();
  order_.clear();
  consent_.clear();
  std::vector<std::string> enabled;
  loadConfig(enabled);
  std::error_code ec;
  if (!userDir_.empty() && !fs::is_directory(modsDir(), ec)) pp::log::info("mods", "no mods folder yet: " + modsDir().string());
  if (!userDir_.empty() && fs::is_directory(modsDir(), ec)) {
    pp::log::info("mods", "looking for mods in " + modsDir().string());
    std::vector<fs::path> items;
    for (auto& e : fs::directory_iterator(modsDir(), ec)) {
      std::string nm = e.path().filename().string();
      if (nm.empty() || nm[0] == '.') continue;
      items.push_back(e.path());
    }
    std::sort(items.begin(), items.end());
    for (auto& p : items) addMod(p, false);
  }
  std::set<std::string> en(enabled.begin(), enabled.end());
  for (auto& m : mods_) m->enabled = en.count(m->m.id) > 0 && m->state != ModInfo::Failed;
  for (auto& c : cliMods) {
    size_t before = mods_.size();
    addMod(fs::path(c), true);
    if (mods_.size() > before || (!mods_.empty() && mods_.back()->fromCli)) {
      ModInfo* mi = mods_.back().get();
      mi->enabled = mi->state != ModInfo::Failed;
    }
  }
  for (auto& m : mods_) m->consented = consent_.count(m->hashHex) > 0;
  std::vector<std::unique_ptr<ModInfo>> sorted;
  for (auto& id : order_)
    for (auto& m : mods_) if (m && m->m.id == id) { sorted.push_back(std::move(m)); break; }
  std::vector<std::unique_ptr<ModInfo>> rest;
  for (auto& m : mods_) if (m) rest.push_back(std::move(m));
  std::sort(rest.begin(), rest.end(), [](auto& a, auto& b) { return a->m.priority != b->m.priority ? a->m.priority > b->m.priority : a->m.id < b->m.id; });
  for (auto& m : rest) sorted.push_back(std::move(m));
  mods_ = std::move(sorted);
  pp::log::info("mods", std::to_string(mods_.size()) + " mod(s) found");
}

bool ModManager::setEnabled(const std::string& id, bool on) {
  ModInfo* m = find(id);
  if (!m || m->state == ModInfo::Failed) return false;
  m->enabled = on;
  pp::log::info("mod:" + id, on ? "enabled (loads the next time the game starts)" : "disabled (unloads the next time the game starts)");
  return m->fromCli ? true : save();
}
bool ModManager::setConsent(const std::string& id, bool grant) {
  ModInfo* m = find(id);
  if (!m) return false;
  m->consented = grant;
  if (grant) consent_[m->hashHex] = id;
  else consent_.erase(m->hashHex);
  pp::log::info("mod:" + id, grant ? "consent given for this exact version (content hash " + m->hashHex + ")" : "consent withdrawn");
  return save();
}
void ModManager::move(const std::string& id, int delta) {
  for (size_t i = 0; i < mods_.size(); i++)
    if (mods_[i]->m.id == id) {
      long j = std::clamp<long>((long)i + delta, 0, (long)mods_.size() - 1);
      if ((size_t)j != i) {
        std::swap(mods_[i], mods_[(size_t)j]);
        pp::log::info("mod:" + id, "load order changed (applies the next time the game starts)");
      }
      save();
      return;
    }
}

uint64_t ModManager::computeSetHash() const {
  uint64_t h = 1469598103934665603ull;
  bool any = false;
  for (auto& m : mods_) {
    if (m->state != ModInfo::Active || m->m.cosmetic) continue;
    any = true;
    h = fnvMix(h, m->m.id.data(), m->m.id.size());
    h = fnvMix(h, m->m.version.data(), m->m.version.size());
    h = fnvMix(h, &m->hash, sizeof m->hash);
  }
  return any ? h : 0;
}

int ModManager::activeCount() const {
  int n = 0;
  for (auto& m : mods_) if (m->state == ModInfo::Active) n++;
  return n;
}
bool ModManager::anyPendingConsent() const {
  for (auto& m : mods_) if (m->enabled && m->state == ModInfo::NeedsConsent) return true;
  return false;
}

void ModManager::activate(Vfs* vfs) {
  deactivate();
  for (auto& mp : mods_) {
    ModInfo& m = *mp;
    const std::string src = "mod:" + m.m.id;
    if (m.state == ModInfo::Failed) continue;
    m.state = ModInfo::Off;
    if (!m.enabled) continue;
    if (m.needsConsent()) {
      m.state = ModInfo::NeedsConsent;
      pp::log::warn(src, "not loaded: it uses the network or native code and needs your consent first");
      continue;
    }
    if (m.m.api > kModApi) {
      m.state = ModInfo::Failed;
      m.error = "needs a newer mod API";
      pp::log::error(src, "not loaded: " + m.error);
      continue;
    }
    std::string err;
    if (vfs && !vfs->mountMod(m.dir, &err)) {
      m.state = ModInfo::Failed;
      m.error = err;
      host_.log(m.m.id, pp::log::Level::Error, "not loaded: " + err);
      continue;
    }
    if (!m.m.scripts.empty()) {
      m.lua = std::make_unique<LuaMod>(m.m, m.dir, host_);
      host_.board = nullptr;
      if (!m.lua->open(&err)) {
        m.state = ModInfo::Failed;
        m.error = err;
        host_.log(m.m.id, pp::log::Level::Error, "failed to load: " + err);
        m.lua.reset();
        continue;
      }
    }
    if (!m.m.native.empty()) {
      fs::path lib = resolveNative(m.dir, m.m);
      if (lib.empty()) {
        m.state = ModInfo::Failed;
        m.error = "native library not found for this platform: " + m.m.native;
        host_.log(m.m.id, pp::log::Level::Error, "not loaded: " + m.error);
        continue;
      }
      m.native = NativeMod::load(lib, m.m, host_, &err);
      if (!m.native) {
        m.state = ModInfo::Failed;
        m.error = err;
        host_.log(m.m.id, pp::log::Level::Error, "native plugin failed: " + err);
        continue;
      }
    }
    m.state = ModInfo::Active;
    std::string what;
    if (!m.m.scripts.empty()) what += std::to_string(m.m.scripts.size()) + " script(s)";
    if (!m.m.native.empty()) what += std::string(what.empty() ? "" : ", ") + "native plugin";
    if (what.empty()) what = "data only";
    host_.log(m.m.id, pp::log::Level::Info, "loaded " + m.m.name + " " + m.m.version + " (" + what + (m.m.cosmetic ? ", cosmetic" : "") + ")");
  }
  setHash_ = computeSetHash();
  bool code = false;
  for (auto& m : mods_) if (m->state == ModInfo::Active && m->hasCode()) code = true;
  g_modSetHash = setHash_;
  if (code) { g_simHooks = this; installed_ = true; }
  g_mods = this;
  pp::log::info("mods", std::to_string(activeCount()) + " of " + std::to_string(mods_.size()) + " mod(s) active");
}

void ModManager::deactivate() {
  if (installed_ && g_simHooks == this) g_simHooks = nullptr;
  installed_ = false;
  if (g_modSetHash == setHash_) g_modSetHash = 0;
  if (g_mods == this) g_mods = nullptr;
  for (auto& m : mods_) {
    m->lua.reset();
    m->native.reset();
    if (m->state == ModInfo::Active) {
      pp::log::info("mod:" + m->m.id, "unloaded");
      m->state = ModInfo::Off;
    }
  }
  queue_.clear();
}

std::string ModManager::report() const {
  std::string o;
  for (auto& m : mods_) {
    o += m->m.id + "  " + m->m.name + " " + m->m.version + (m->m.author.empty() ? "" : " by " + m->m.author) + "  [" + m->stateName() + (m->enabled ? ", enabled" : "") + "]  hash " + m->hashHex + "\n";
    if (!m->error.empty()) o += "    error: " + m->error + "\n";
    o += "    " + m->scan.summary() + (m->consented ? " (consent given)" : "") + "\n";
  }
  if (o.empty()) o = "no mods found in " + modsDir().string() + "\n";
  return o;
}

struct ModManager::Scope {
  ModManager& M;
  Board* pb;
  Ctx pc;
  DrawSink* ps;
  Scope(ModManager& m, Board* b, Ctx c, DrawSink* s = nullptr) : M(m), pb(m.host_.board), pc(m.host_.ctx), ps(m.host_.sink) {
    M.host_.board = b;
    M.host_.ctx = c;
    M.host_.sink = s;
  }
  ~Scope() { M.host_.board = pb; M.host_.ctx = pc; M.host_.sink = ps; }
};

template <class F> void ModManager::eachSim(F&& f) {
  for (auto& m : mods_)
    if (m->state == ModInfo::Active && !m->m.cosmetic) f(*m);
}

void ModManager::levelStart(Board& b) {
  Scope sc(*this, &b, Ctx::Sim);
  eachSim([&](ModInfo& m) {
    if (m.lua) m.lua->callSim("on_level_start", 1, [&](lua_State* L) {
      lua_createtable(L, 0, 6);
      lua_pushinteger(L, b.W); lua_setfield(L, -2, "width");
      lua_pushinteger(L, b.orangeTotal); lua_setfield(L, -2, "orange");
      lua_pushinteger(L, b.players); lua_setfield(L, -2, "players");
      lua_pushboolean(L, b.cfg.nights); lua_setfield(L, -2, "nights");
      lua_pushboolean(L, b.cfg.duel); lua_setfield(L, -2, "duel");
      int n = 0;
      for (auto& o : b.objs) if (o.pegType > 0) n++;
      lua_pushinteger(L, n); lua_setfield(L, -2, "pegs");
    });
    if (m.native) m.native->levelStart();
  });
  flush(b);
}

void ModManager::stepBegin(Board& b) {
  flush(b);
  Scope sc(*this, &b, Ctx::Sim);
  eachSim([&](ModInfo& m) {
    if (m.lua) m.lua->callSim("on_tick", 1, [&](lua_State* L) { lua_pushinteger(L, b.tick); });
    if (m.native) m.native->tick();
  });
  flush(b);
}
void ModManager::stepEnd(Board& b) { flush(b); }

void ModManager::event(Board& b, int type, int a, int bb, int c, int ball) {
  const char* hook = nullptr;
  switch (type) {
    case Event::Shot: hook = "on_shot"; break;
    case Event::PegLit: hook = "on_peg_hit"; break;
    case Event::BallLost: hook = "on_ball_lost"; break;
    case Event::LevelDone: hook = "on_level_end"; break;
    case Event::Power: if (a >= 1 && a <= 14) hook = "on_power"; break;
    case Event::StyleShot: hook = "on_style_shot"; break;
    case Event::ShotEnd: hook = "on_shot_end"; break;
    case Event::Fever: hook = "on_fever"; break;
    default: break;
  }
  Scope sc(*this, &b, Ctx::Sim);
  eachSim([&](ModInfo& m) {
    if (m.lua && hook) {
      auto push = [&](lua_State* L) {
        switch (type) {
          case Event::Shot: { float ang; std::memcpy(&ang, &a, sizeof ang); lua_pushnumber(L, ang); break; }
          case Event::PegLit: lua_pushinteger(L, a); lua_pushinteger(L, ball); break;
          case Event::LevelDone:
            lua_createtable(L, 0, 6);
            lua_pushboolean(L, a == 1); lua_setfield(L, -2, "won");
            lua_pushinteger(L, b.score[std::clamp(b.cur, 0, 3)]); lua_setfield(L, -2, "score");
            lua_pushinteger(L, b.shotIndex); lua_setfield(L, -2, "shots");
            lua_pushinteger(L, b.orangeLeft); lua_setfield(L, -2, "orange_left");
            lua_pushinteger(L, b.tick); lua_setfield(L, -2, "ticks");
            break;
          case Event::Power: lua_pushinteger(L, a); lua_pushstring(L, Board::powerName(a)); break;
          case Event::StyleShot: lua_pushinteger(L, a); lua_pushinteger(L, bb); break;
          case Event::ShotEnd: lua_pushinteger(L, a); lua_pushinteger(L, bb); break;
          default: break;
        }
      };
      int n = type == Event::PegLit || type == Event::Power || type == Event::StyleShot || type == Event::ShotEnd ? 2 : (type == Event::Shot || type == Event::LevelDone ? 1 : 0);
      m.lua->callSim(hook, n, push);
    }
    if (m.native) m.native->event(type, a, bb, c, ball);
  });
}

bool ModManager::power(Board& b, int pegIdx, int pw, bool silent) {
  bool replaced = false;
  Scope sc(*this, &b, Ctx::Sim);
  eachSim([&](ModInfo& m) {
    if (m.lua)
      for (auto& p : m.lua->powers)
        if (p.replaces == pw) {
          m.lua->activatePower(p, b.cur, pegIdx);
          if (p.replace) replaced = true;
        }
    if (m.native && m.native->power(pegIdx, pw)) replaced = true;
  });
  return replaced;
}

void ModManager::apply(Board& b, const ModCmd& c) {
  const bool shotLive = b.phase == Phase::Shot || b.phase == Phase::Popping;
  const int cur = std::clamp(b.cur, 0, 3);
  switch (c.k) {
    case ModCmd::AddScore:
      if (shotLive && !b.shotBanked) b.addShotScore(c.i);
      else b.score[cur] += c.i;
      break;
    case ModCmd::HitPeg:
      if (shotLive && c.i >= 0 && c.i < (int)b.objs.size()) {
        const Obj& o = b.objs[(size_t)c.i];
        if (o.pegType > 0 && !o.gone && !o.lit) b.pegHit(c.i, true);
      }
      break;
    case ModCmd::AddBalls:
      b.ballsLeft[cur] = std::clamp(b.ballsLeft[cur] + c.i, 0, 99);
      if (c.i > 0) b.ev(Event::ExtraBall, cur);
      break;
    case ModCmd::SetBallVel:
      if (c.i >= 0 && c.i < (int)b.balls.size()) {
        double vx = std::clamp(c.a, -40.0, 40.0), vy = std::clamp(c.b, -40.0, 40.0);
        b.balls[(size_t)c.i].vel.x = (float)vx;
        b.balls[(size_t)c.i].vel.y = (float)vy;
      }
      break;
    case ModCmd::Say:
      b.banner = c.s;
      b.bannerTimer = 150;
      break;
    case ModCmd::StylePoints:
      b.style[cur] += c.i;
      b.result.stylePoints += c.i;
      b.banner = c.s + "! " + std::to_string(c.i);
      b.bannerTimer = 150;
      break;
  }
}

void ModManager::flush(Board& b) {
  for (int round = 0; round < 8 && !queue_.empty(); round++) {
    std::vector<ModCmd> cmds;
    cmds.swap(queue_);
    for (auto& c : cmds) apply(b, c);
  }
  queue_.clear();
  queueFull_ = false;
}

void ModManager::frame(DrawSink& sink, const Board& b) {
  pump();
  Scope sc(*this, const_cast<Board*>(&b), Ctx::Draw, &sink);
  for (auto& m : mods_) {
    if (m->state != ModInfo::Active) continue;
    if (m->lua) m->lua->callLoose("on_frame", Ctx::Draw, 1, [&](lua_State* L) {
      lua_createtable(L, 0, 6);
      lua_pushinteger(L, b.tick); lua_setfield(L, -2, "tick");
      lua_pushinteger(L, b.W); lua_setfield(L, -2, "width");
      lua_pushinteger(L, b.orangeLeft); lua_setfield(L, -2, "orange_left");
      lua_pushinteger(L, b.score[std::clamp(b.cur, 0, 3)]); lua_setfield(L, -2, "score");
      lua_pushinteger(L, b.frames); lua_setfield(L, -2, "frame");
    });
    if (m->native) m->native->frame();
  }
}
void ModManager::pump() {
  for (auto& m : mods_)
    if (m->state == ModInfo::Active && m->lua && (m->m.net || m->m.fs)) {
      Scope sc(*this, nullptr, Ctx::Ui);
      pumpNet(m->lua->state());
    }
}
std::vector<ModManager::MenuRef> ModManager::menus() const {
  std::vector<MenuRef> v;
  for (auto& m : mods_)
    if (m->state == ModInfo::Active && m->lua)
      for (size_t i = 0; i < m->lua->menus.size(); i++) v.push_back({m->m.id, m->lua->menus[i].label, (int)i});
  return v;
}
bool ModManager::runMenu(const std::string& id, int index) {
  ModInfo* m = find(id);
  if (!m || m->state != ModInfo::Active || !m->lua) return false;
  Scope sc(*this, nullptr, Ctx::Ui);
  return m->lua->runMenu(index);
}
std::vector<std::string> ModManager::takeMessages() {
  std::vector<std::string> v;
  v.swap(messages_);
  return v;
}

}
