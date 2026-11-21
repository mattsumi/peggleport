#include "mods/manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <map>
#include <filesystem>

#include "mods/hostreg.h"
#include "gamepaths.h"
#include "mods/json.h"
#include "mods/text.h"
#include "vfs.h"

namespace fs = std::filesystem;
namespace pp::mods {

uint64_t fnvMix(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
std::string hex64(uint64_t v) {
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
  return b;
}
bool readFile(const fs::path& p, std::string& out, size_t maxBytes) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const auto n = f.tellg();
  if (n < 0 || (uint64_t)n > maxBytes) return false;
  f.seekg(0);
  out.resize((size_t)n);
  if (f.read(out.data(), (std::streamsize)n) && f.gcount() == n) return true;
  out.clear();
  return false;
}
bool safeRelPath(const std::string& rel) { return modhost::relPathOk(rel); }

static std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> v;
  std::string cur;
  for (char c : s + ",") {
    if (c == ',' || c == ';' || c == '|' || c == ' ') {
      if (!cur.empty()) v.push_back(cur);
      cur.clear();
    } else cur += c;
  }
  return v;
}
static std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool parseManifest(const std::string& text, bool json, const std::string& folderName, Manifest& m, std::string* err) {
  std::map<std::string, std::string> kv;
  std::vector<std::string> scripts, perms;
  if (json) {
    Json j;
    std::string e;
    if (!Json::parse(text, j, &e)) { if (err) *err = "mod.json: " + e; return false; }
    if (j.t != Json::Obj) { if (err) *err = "mod.json: top level must be an object"; return false; }
    for (auto& [k, v] : j.o) {
      if (v.t == Json::Arr) {
        std::vector<std::string> items;
        for (auto& x : v.a) items.push_back(x.asStr());
        if (lower(k) == "scripts") scripts = items;
        else if (lower(k) == "permissions") perms = items;
      } else if (v.t == Json::Bool) kv[lower(k)] = v.b ? "true" : "false";
      else kv[lower(k)] = v.asStr();
    }
  } else {
    for (auto& [k, v] : readIni(text)) kv[lower(k)] = v;
    if (kv.count("scripts")) scripts = splitList(kv["scripts"]);
    if (kv.count("script") && scripts.empty()) scripts = splitList(kv["script"]);
  }
  if (kv.count("permissions") && perms.empty()) perms = splitList(kv["permissions"]);
  auto get = [&](const char* k, const std::string& d = "") { auto it = kv.find(k); return it == kv.end() ? d : it->second; };
  m.name = get("name", folderName);
  m.id = sanitizeId(get("id", m.name == folderName ? folderName : (kv.count("name") ? folderName : folderName)));
  m.version = get("version", "0");
  m.author = get("author");
  m.description = get("description");
  m.name = gameText(m.name);
  m.author = gameText(m.author);
  m.description = gameText(m.description);
  m.api = std::atoi(get("api", get("api_level", std::to_string(kModApi))).c_str());
  m.scripts = scripts;
  m.native = get("native", get("plugin"));
  m.priority = std::atoi(get("priority", "0").c_str());
  std::string det = lower(get("determinism", "sim"));
  if (det == "cosmetic" || det == "presentation") m.cosmetic = true;
  else if (det != "sim" && det != "deterministic" && !det.empty()) m.warnings.push_back("unknown determinism '" + det + "' (use sim or cosmetic): treated as sim");
  for (auto& p : perms) {
    std::string q = lower(p);
    if (q == "net") m.net = true;
    else if (q == "fs") m.fs = true;
    else if (q == "control") m.control = true;
    else m.warnings.push_back("unknown permission '" + p + "' ignored");
  }
  int mem = std::atoi(get("memory_mb", "16").c_str());
  m.memoryMb = std::clamp(mem, 4, 64);
  if (mem != m.memoryMb) m.warnings.push_back("memory_mb clamped to " + std::to_string(m.memoryMb));
  int bud = std::atoi(get("instruction_budget", "200000").c_str());
  m.budget = std::clamp(bud, 1000, 5000000);
  if (bud != m.budget) m.warnings.push_back("instruction_budget clamped to " + std::to_string(m.budget));
  for (auto& s : m.scripts)
    if (!safeRelPath(s)) { if (err) *err = "script path escapes the mod folder: " + s; return false; }
  if (!m.native.empty() && !safeRelPath(m.native)) { if (err) *err = "native path escapes the mod folder: " + m.native; return false; }
  if (m.api > kModApi) { if (err) *err = "needs mod API " + std::to_string(m.api) + " (this build has " + std::to_string(kModApi) + ")"; return false; }
  if (m.api < 1) m.api = 1;
  m.format = json ? "json" : "ini";
  return true;
}

bool loadManifest(const fs::path& dir, Manifest& m, std::string* err) {
  std::string folder = pathToUtf8(dir.filename());
  if (folder.empty()) folder = pathToUtf8(dir.parent_path().filename());
  std::string text;
  m = Manifest();
  fs::path jsonPath, iniPath;
  if (!resolveDiskSpelling(dir / "mod.json", jsonPath, err) || !resolveDiskSpelling(dir / "mod.ini", iniPath, err)) return false;
  for (bool json : {true, false}) {
    const auto& path = json ? jsonPath : iniPath;
    std::error_code ec;
    if (fs::symlink_status(path, ec).type() == fs::file_type::not_found) continue;
    if (ec || !fs::is_regular_file(path, ec) || !readFile(path, text, 1 << 20)) {
      if (err) *err = "cannot read " + pathToUtf8(path);
      return false;
    }
    if (!parseManifest(text, json, folder, m, err)) return false;
    break;
  }
  if (m.format.empty()) {
    m.name = gameText(folder);
    m.id = sanitizeId(folder);
    m.format = "none";
  }
  if (m.scripts.empty() && m.format != "none") {
    std::error_code ec;
    if (fs::exists(dir / "main.lua", ec)) m.scripts.push_back("main.lua");
  }
  return true;
}

fs::path resolveNative(const fs::path& dir, const Manifest& m) {
  if (m.native.empty()) return {};
  std::vector<std::string> cand{m.native};
#if defined(_WIN32)
  cand.push_back(m.native + ".dll");
#elif defined(__APPLE__)
  cand.push_back(m.native + ".dylib");
  cand.push_back("lib" + m.native + ".dylib");
#else
  cand.push_back(m.native + ".so");
  cand.push_back("lib" + m.native + ".so");
#endif
  std::error_code ec;
  for (auto& c : cand) {
    fs::path p = dir / c;
    if (fs::is_regular_file(p, ec)) return p;
  }
  return {};
}

uint64_t hashModDir(const fs::path& dir, std::vector<std::string>* files) {
  std::vector<std::pair<std::string, fs::path>> v;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    std::string rel = genericUtf8(it->path().lexically_relative(dir));
    std::string nm = pathToUtf8(it->path().filename());
    if (!nm.empty() && nm[0] == '.') { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
    if (rel.rfind("mod-data", 0) == 0) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
    if (it->is_regular_file(ec)) v.emplace_back(rel, it->path());
  }
  std::sort(v.begin(), v.end());
  uint64_t h = 1469598103934665603ull;
  for (auto& [rel, p] : v) {
    std::string bytes;
    readFile(p, bytes, 1ull << 31);
    uint64_t n = bytes.size();
    h = fnvMix(h, rel.data(), rel.size());
    h = fnvMix(h, &n, sizeof n);
    h = fnvMix(h, bytes.data(), bytes.size());
    if (files) files->push_back(rel);
  }
  return h;
}

}
