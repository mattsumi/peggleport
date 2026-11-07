#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <functional>
#include <vector>
#include <string>
#include <cstdlib>
#include <filesystem>

#include "lauxlib.h"
#include "log.h"
#include "lua.h"
#include "mods/luamod.h"
#include "sockcompat.h"

namespace fs = std::filesystem;
namespace pp::mods {
namespace {

constexpr size_t kMaxBody = 1u << 20;
constexpr int kTimeoutMs = 10000;
constexpr int kMaxThreads = 4;

struct Ev {
  enum K { Http, Open, Data, Close } k = Http;
  int ref = 0;
  int status = 0;
  std::string body, err;
};
struct Conn {
  int id = 0;
  std::mutex m;
  std::string out;
  bool closeReq = false, started = false;
  std::atomic<bool> done{false};
};
struct Shared {
  std::mutex m;
  std::deque<Ev> events;
  std::atomic<int> threads{0};
  std::atomic<bool> stop{false};
  std::string source;
  std::mutex logMu;
  std::chrono::steady_clock::time_point logWindow;
  int logCount = 0;
  bool logNoted = false;
};
struct NetState {
  LuaMod* mod = nullptr;
  HostServices* host = nullptr;
  std::shared_ptr<Shared> sh = std::make_shared<Shared>();
  struct Handlers { int onOpen = LUA_NOREF, onData = LUA_NOREF, onClose = LUA_NOREF, obj = LUA_NOREF; std::shared_ptr<Conn> conn; };
  std::map<int, Handlers> conns;
  int nextConn = 1;
};
std::mutex gMu;
std::map<lua_State*, std::unique_ptr<NetState>> gStates;

void netLog(const std::shared_ptr<Shared>& sh, pp::log::Level level, const std::string& text) {
  bool say = false, note = false;
  {
    std::lock_guard<std::mutex> g(sh->logMu);
    auto now = std::chrono::steady_clock::now();
    if (now - sh->logWindow >= std::chrono::seconds(1)) {
      sh->logWindow = now;
      sh->logCount = 0;
      sh->logNoted = false;
    }
    if (sh->logCount < 50) {
      sh->logCount++;
      say = true;
    } else if (!sh->logNoted) {
      sh->logNoted = true;
      note = true;
    }
  }
  if (say) pp::log::write(level, sh->source, text);
  if (note) pp::log::write(pp::log::Level::Warn, sh->source, "network and file log lines are limited to 50 per second; extra lines are dropped");
}
std::string shortUrl(const std::string& u) { return u.size() > 200 ? u.substr(0, 200) + "..." : u; }

NetState* NS(lua_State* L) {
  std::lock_guard<std::mutex> g(gMu);
  auto it = gStates.find(L);
  return it == gStates.end() ? nullptr : it->second.get();
}

sock_t dial(const std::string& host, int port, std::string* err) {
  netStartup();
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) { *err = "cannot resolve " + host; return BAD_SOCK; }
  sock_t out = BAD_SOCK;
  for (addrinfo* ai = res; ai && out == BAD_SOCK; ai = ai->ai_next) {
    sock_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == BAD_SOCK) continue;
    nonBlock(s);
    int r = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
    if (r != 0 && !wouldBlock()) { closeSock(s); continue; }
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval tv{kTimeoutMs / 1000, (kTimeoutMs % 1000) * 1000};
    if (select((int)s + 1, nullptr, &w, nullptr, &tv) > 0) {
      int e = 0;
      socklen_t l = sizeof e;
      getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&e, &l);
      if (e == 0) { out = s; tuneSock(s); break; }
    }
    closeSock(s);
  }
  freeaddrinfo(res);
  if (out == BAD_SOCK) *err = "cannot connect to " + host + ":" + std::to_string(port);
  return out;
}
bool waitFd(sock_t s, bool write, int ms) {
  fd_set f;
  FD_ZERO(&f);
  FD_SET(s, &f);
  timeval tv{ms / 1000, (ms % 1000) * 1000};
  return select((int)s + 1, write ? nullptr : &f, write ? &f : nullptr, nullptr, &tv) > 0;
}
bool sendAll(sock_t s, const std::string& d, std::atomic<bool>* stop) {
  size_t off = 0;
  int idle = 0;
  while (off < d.size()) {
    int n = (int)send(s, d.data() + off, (int)std::min<size_t>(d.size() - off, 16384), MSG_NOSIGNAL);
    if (n > 0) { off += (size_t)n; idle = 0; continue; }
    if (!wouldBlock() || (stop && *stop) || ++idle > 500) return false;
    waitFd(s, true, 20);
  }
  return true;
}
bool parseUrl(const std::string& u, std::string& host, int& port, std::string& path, std::string* err) {
  if (u.rfind("https://", 0) == 0) { *err = "https is not supported (the port has no TLS library): use http:// or net.tcp"; return false; }
  if (u.rfind("http://", 0) != 0) { *err = "only http:// URLs are supported"; return false; }
  std::string rest = u.substr(7);
  size_t sl = rest.find('/');
  std::string hp = sl == std::string::npos ? rest : rest.substr(0, sl);
  path = sl == std::string::npos ? "/" : rest.substr(sl);
  size_t c = hp.rfind(':');
  port = 80;
  host = hp;
  if (c != std::string::npos) { host = hp.substr(0, c); port = std::atoi(hp.c_str() + c + 1); }
  if (host.empty() || port <= 0 || port > 65535 || host.find_first_of(" \r\n\t") != std::string::npos || path.find_first_of(" \r\n") != std::string::npos) { *err = "bad URL"; return false; }
  return true;
}
void post(const std::shared_ptr<Shared>& sh, Ev e) {
  std::lock_guard<std::mutex> g(sh->m);
  if (sh->events.size() < 2048) sh->events.push_back(std::move(e));
}
void httpWorker(std::shared_ptr<Shared> sh, int ref, std::string method, std::string url, std::string body, std::vector<std::pair<std::string, std::string>> hdr) {
  Ev ev;
  ev.k = Ev::Http;
  ev.ref = ref;
  std::string host, path, err;
  int port;
  sock_t s = BAD_SOCK;
  netLog(sh, pp::log::Level::Info, "HTTP " + method + " " + shortUrl(url));
  if (!parseUrl(url, host, port, path, &err)) { ev.err = err; goto done; }
  s = dial(host, port, &err);
  if (s == BAD_SOCK) { ev.err = err; goto done; }
  {
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: PegglePort-mod/1\r\nAccept: */*\r\nConnection: close\r\n";
    bool hasLen = false;
    for (auto& h : hdr) { req += h.first + ": " + h.second + "\r\n"; if (h.first == "Content-Length") hasLen = true; }
    if (!hasLen && (method == "POST" || !body.empty())) req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    if (!sendAll(s, req, &sh->stop)) { ev.err = "send failed"; goto done; }
    std::string resp;
    int idle = 0;
    char buf[8192];
    while (resp.size() < kMaxBody + 65536 && !sh->stop) {
      int n = (int)recv(s, buf, sizeof buf, 0);
      if (n > 0) { resp.append(buf, (size_t)n); idle = 0; continue; }
      if (n == 0) break;
      if (!wouldBlock()) break;
      if (++idle > kTimeoutMs / 20) { ev.err = "timeout"; break; }
      waitFd(s, false, 20);
    }
    size_t he = resp.find("\r\n\r\n");
    if (he == std::string::npos) { if (ev.err.empty()) ev.err = "bad response"; goto done; }
    ev.err.clear();
    ev.status = std::atoi(resp.c_str() + resp.find(' ') + 1);
    std::string head = resp.substr(0, he), b = resp.substr(he + 4);
    std::string lh = head;
    for (auto& c : lh) c = (char)std::tolower((unsigned char)c);
    if (lh.find("transfer-encoding: chunked") != std::string::npos) {
      std::string dec;
      size_t p = 0;
      while (p < b.size()) {
        size_t e = b.find("\r\n", p);
        if (e == std::string::npos) break;
        size_t n = (size_t)std::strtoul(b.substr(p, e - p).c_str(), nullptr, 16);
        if (n == 0) break;
        dec.append(b, e + 2, n);
        p = e + 2 + n + 2;
      }
      b = dec;
    }
    if (b.size() > kMaxBody) b.resize(kMaxBody);
    ev.body = std::move(b);
  }
done:
  if (s != BAD_SOCK) closeSock(s);
  if (!ev.err.empty()) netLog(sh, pp::log::Level::Warn, "HTTP " + method + " " + shortUrl(url) + " failed: " + ev.err);
  else netLog(sh, pp::log::Level::Info, "HTTP " + method + " " + shortUrl(url) + " answered " + std::to_string(ev.status) + " (" + std::to_string(ev.body.size()) + " bytes)");
  post(sh, std::move(ev));
  sh->threads--;
}
void tcpWorker(std::shared_ptr<Shared> sh, std::shared_ptr<Conn> c, std::string host, int port) {
  std::string err;
  const std::string peer = host + ":" + std::to_string(port);
  netLog(sh, pp::log::Level::Info, "TCP connecting to " + peer);
  sock_t s = dial(host, port, &err);
  if (s == BAD_SOCK) {
    netLog(sh, pp::log::Level::Warn, "TCP " + peer + " failed: " + err);
    post(sh, {Ev::Close, c->id, 0, "", err});
  } else {
    netLog(sh, pp::log::Level::Info, "TCP connected to " + peer);
    post(sh, {Ev::Open, c->id, 0, "", ""});
    char buf[16384];
    std::string cause;
    while (!sh->stop) {
      std::string out;
      bool closeReq;
      {
        std::lock_guard<std::mutex> g(c->m);
        out.swap(c->out);
        closeReq = c->closeReq;
      }
      if (!out.empty() && !sendAll(s, out, &sh->stop)) { cause = "send failed"; break; }
      if (closeReq) break;
      if (!waitFd(s, false, 20)) continue;
      int n = (int)recv(s, buf, sizeof buf, 0);
      if (n > 0) { post(sh, {Ev::Data, c->id, 0, std::string(buf, (size_t)n), ""}); continue; }
      if (n == 0) break;
      if (!wouldBlock()) { cause = "connection error"; break; }
    }
    closeSock(s);
    if (cause.empty()) netLog(sh, pp::log::Level::Info, "TCP " + peer + " closed");
    else netLog(sh, pp::log::Level::Warn, "TCP " + peer + " closed: " + cause);
    post(sh, {Ev::Close, c->id, 0, "", cause});
  }
  c->done = true;
  sh->threads--;
}

bool guard(lua_State* L, NetState*& ns) {
  ns = NS(L);
  if (!ns) luaL_error(L, "net is not available");
  if (ns->mod->ctx() == Ctx::Sim) luaL_error(L, "net.* cannot be used inside a gameplay hook (the simulation must not depend on the network): call it from on_load, on_frame or a menu callback");
  return true;
}
std::vector<std::pair<std::string, std::string>> headersArg(lua_State* L, int idx) {
  std::vector<std::pair<std::string, std::string>> v;
  if (lua_istable(L, idx)) {
    lua_getfield(L, idx, "headers");
    if (lua_istable(L, -1)) {
      lua_pushnil(L);
      while (lua_next(L, -2)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING) {
          std::string k = lua_tostring(L, -2), val = lua_tostring(L, -1);
          if (k.find_first_of("\r\n:") == std::string::npos && val.find_first_of("\r\n") == std::string::npos && v.size() < 16) v.emplace_back(k, val);
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
  }
  return v;
}
int startHttp(lua_State* L, const char* method, int urlIdx, const std::string& body, int cbIdx, int optIdx) {
  NetState* ns;
  guard(L, ns);
  std::string url = luaL_checkstring(L, urlIdx);
  luaL_checktype(L, cbIdx, LUA_TFUNCTION);
  if (ns->sh->threads >= kMaxThreads) luaL_error(L, "too many network requests in flight");
  lua_pushvalue(L, cbIdx);
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  auto hdr = headersArg(L, optIdx);
  ns->sh->threads++;
  try {
    std::thread(httpWorker, ns->sh, ref, std::string(method), url, body.substr(0, kMaxBody), hdr).detach();
  } catch (...) {
    ns->sh->threads--;
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    luaL_error(L, "cannot start a network thread");
  }
  return 0;
}
int n_get(lua_State* L) { return startHttp(L, "GET", 1, "", 2, 3); }
int n_post(lua_State* L) { size_t n; const char* b = luaL_checklstring(L, 2, &n); return startHttp(L, "POST", 1, std::string(b, n), 3, 4); }
Conn* connOf(lua_State* L, NetState*& ns) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, 1, "id");
  int id = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  ns = NS(L);
  if (!ns || !ns->conns.count(id)) return nullptr;
  return ns->conns[id].conn.get();
}
int c_send(lua_State* L) {
  NetState* ns;
  Conn* c = connOf(L, ns);
  if (!c) return luaL_error(L, "connection is closed");
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  std::lock_guard<std::mutex> g(c->m);
  if (c->out.size() + n > (256u << 10)) return luaL_error(L, "send queue full");
  c->out.append(d, n);
  return 0;
}
int c_close(lua_State* L) {
  NetState* ns;
  Conn* c = connOf(L, ns);
  if (c) { std::lock_guard<std::mutex> g(c->m); c->closeReq = true; }
  return 0;
}
int n_tcp(lua_State* L) {
  NetState* ns;
  guard(L, ns);
  std::string host = luaL_checkstring(L, 1);
  int port = (int)luaL_checkinteger(L, 2);
  luaL_checktype(L, 3, LUA_TTABLE);
  if (port <= 0 || port > 65535) luaL_error(L, "bad port");
  if (ns->conns.size() >= 4 || ns->sh->threads >= kMaxThreads) luaL_error(L, "too many connections");
  NetState::Handlers h;
  auto ref = [&](const char* k) {
    lua_getfield(L, 3, k);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return (int)LUA_NOREF; }
    return luaL_ref(L, LUA_REGISTRYINDEX);
  };
  h.onOpen = ref("on_open"); h.onData = ref("on_data"); h.onClose = ref("on_close");
  h.conn = std::make_shared<Conn>();
  h.conn->id = ns->nextConn++;
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, h.conn->id); lua_setfield(L, -2, "id");
  lua_pushcfunction(L, c_send); lua_setfield(L, -2, "send");
  lua_pushcfunction(L, c_close); lua_setfield(L, -2, "close");
  lua_pushvalue(L, -1);
  h.obj = luaL_ref(L, LUA_REGISTRYINDEX);
  ns->conns[h.conn->id] = h;
  ns->sh->threads++;
  try {
    std::thread(tcpWorker, ns->sh, h.conn, host, port).detach();
  } catch (...) {
    ns->sh->threads--;
    luaL_error(L, "cannot start a network thread");
  }
  return 1;
}

std::string fsDir(lua_State* L) {
  NetState* ns = NS(L);
  return ns ? ns->host->dataDir(ns->mod->manifest().id) : "";
}
bool okName(const std::string& n) {
  if (n.empty() || n.size() > 64 || n[0] == '.') return false;
  for (char c : n) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-')) return false;
  return true;
}
fs::path fsPath(lua_State* L, int i) {
  std::string n = luaL_checkstring(L, i);
  if (!okName(n)) luaL_error(L, "bad file name (use A-Z a-z 0-9 _ . - , at most 64 characters)");
  std::string d = fsDir(L);
  if (d.empty()) luaL_error(L, "fs is not available");
  return fs::path(d) / n;
}
int f_read(lua_State* L) {
  std::string t;
  if (!readFile(fsPath(L, 1), t, 1u << 20)) { lua_pushnil(L); return 1; }
  lua_pushlstring(L, t.data(), t.size());
  return 1;
}
int f_write(lua_State* L) {
  fs::path p = fsPath(L, 1);
  size_t n;
  const char* d = luaL_checklstring(L, 2, &n);
  if (n > (1u << 20)) luaL_error(L, "file too large (1 MB)");
  std::error_code ec;
  int count = 0;
  for (auto& e : fs::directory_iterator(p.parent_path(), ec)) { (void)e; count++; }
  if (count >= 64 && !fs::exists(p, ec)) luaL_error(L, "too many files (64)");
  std::ofstream f(p, std::ios::binary);
  f.write(d, (std::streamsize)n);
  if (NetState* ns = NS(L)) {
    if (f) netLog(ns->sh, pp::log::Level::Info, "fs.write " + p.filename().string() + " (" + std::to_string(n) + " bytes)");
    else netLog(ns->sh, pp::log::Level::Warn, "fs.write " + p.filename().string() + " failed");
  }
  lua_pushboolean(L, (bool)f);
  return 1;
}
int f_exists(lua_State* L) { std::error_code ec; lua_pushboolean(L, fs::exists(fsPath(L, 1), ec)); return 1; }
int f_remove(lua_State* L) {
  std::error_code ec;
  fs::path p = fsPath(L, 1);
  bool removed = fs::remove(p, ec);
  if (removed) {
    if (NetState* ns = NS(L)) netLog(ns->sh, pp::log::Level::Info, "fs.remove " + p.filename().string());
  }
  lua_pushboolean(L, removed);
  return 1;
}
int f_list(lua_State* L) {
  lua_newtable(L);
  std::error_code ec;
  int i = 0;
  for (auto& e : fs::directory_iterator(fsDir(L), ec)) { lua_pushstring(L, e.path().filename().string().c_str()); lua_rawseti(L, -2, ++i); }
  return 1;
}
}

void openNetLib(lua_State* L, LuaMod* mod, HostServices& host) {
  {
    std::lock_guard<std::mutex> g(gMu);
    auto ns = std::make_unique<NetState>();
    ns->mod = mod;
    ns->host = &host;
    ns->sh->source = "mod:" + mod->manifest().id;
    gStates[L] = std::move(ns);
  }
  if (mod->manifest().net) {
    static const luaL_Reg fns[] = {{"get", n_get}, {"post", n_post}, {"tcp", n_tcp}, {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, fns, 0);
    lua_setglobal(L, "net");
  }
  if (mod->manifest().fs) {
    static const luaL_Reg fns[] = {{"read", f_read}, {"write", f_write}, {"exists", f_exists}, {"remove", f_remove}, {"list", f_list}, {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, fns, 0);
    lua_setglobal(L, "fs");
  }
}

void pumpNet(lua_State* L) {
  NetState* ns = NS(L);
  if (!ns) return;
  for (int n = 0; n < 16; n++) {
    Ev e;
    {
      std::lock_guard<std::mutex> g(ns->sh->m);
      if (ns->sh->events.empty()) return;
      e = std::move(ns->sh->events.front());
      ns->sh->events.pop_front();
    }
    if (e.k == Ev::Http) {
      ns->mod->callRef(e.ref, 3, [&](lua_State* l) {
        lua_pushinteger(l, e.status);
        if (e.err.empty()) lua_pushlstring(l, e.body.data(), e.body.size()); else lua_pushnil(l);
        if (e.err.empty()) lua_pushnil(l); else lua_pushstring(l, e.err.c_str());
      });
      luaL_unref(L, LUA_REGISTRYINDEX, e.ref);
      continue;
    }
    auto it = ns->conns.find(e.ref);
    if (it == ns->conns.end()) continue;
    NetState::Handlers h = it->second;
    auto withObj = [&](int fn, int extra, const std::function<void(lua_State*)>& push) {
      if (fn == LUA_NOREF) return;
      ns->mod->callRef(fn, 1 + extra, [&](lua_State* l) {
        lua_rawgeti(l, LUA_REGISTRYINDEX, h.obj);
        if (push) push(l);
      });
    };
    if (e.k == Ev::Open) withObj(h.onOpen, 0, nullptr);
    else if (e.k == Ev::Data) withObj(h.onData, 1, [&](lua_State* l) { lua_pushlstring(l, e.body.data(), e.body.size()); });
    else {
      withObj(h.onClose, 1, [&](lua_State* l) { if (e.err.empty()) lua_pushnil(l); else lua_pushstring(l, e.err.c_str()); });
      for (int r : {h.onOpen, h.onData, h.onClose, h.obj}) if (r != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, r);
      ns->conns.erase(it);
    }
  }
}

void closeNet(lua_State* L) {
  std::unique_ptr<NetState> ns;
  {
    std::lock_guard<std::mutex> g(gMu);
    auto it = gStates.find(L);
    if (it == gStates.end()) return;
    ns = std::move(it->second);
    gStates.erase(it);
  }
  ns->sh->stop = true;
}

}
