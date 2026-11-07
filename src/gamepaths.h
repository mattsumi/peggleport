#pragma once
#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "profile.h"

namespace pp {

struct GameCandidate {
  std::filesystem::path dir;
  bool found = false;
};

namespace gamepaths_detail {
namespace fs = std::filesystem;

inline std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

inline std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

inline std::string utf8(const fs::path& p) {
  auto u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

inline fs::path fromUtf8(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

inline std::string leafName(const fs::path& p) {
  fs::path q = p;
  if (!q.has_filename()) q = q.parent_path();
  return utf8(q.filename());
}

inline void steamLibraries(const fs::path& steamRoot, std::vector<fs::path>& out) {
  std::error_code ec;
  if (!fs::is_directory(steamRoot, ec)) return;
  out.push_back(steamRoot);
  std::ifstream f(steamRoot / "steamapps" / "libraryfolders.vdf");
  std::string line;
  while (std::getline(f, line)) {
    auto k = line.find("\"path\"");
    if (k == std::string::npos) continue;
    auto a = line.find('"', k + 6);
    if (a == std::string::npos) continue;
    std::string v;
    for (size_t i = a + 1; i < line.size() && line[i] != '"'; i++) {
      if (line[i] == '\\' && i + 1 < line.size()) i++;
      v += line[i];
    }
    if (!v.empty()) out.push_back(fromUtf8(v));
  }
}

inline std::vector<fs::path> searchRoots() {
  std::vector<fs::path> roots;
  for (const char* r : {"Games", "../Games", "../../Games"}) roots.push_back(r);
  if (const char* b = SDL_GetBasePath(); b && *b) {
    fs::path base = fromUtf8(b);
    for (const char* r : {"Games", "../Games", "../../Games"}) roots.push_back(base / r);
  }

  std::vector<fs::path> steam;
  std::vector<fs::path> prefix;
  std::string home = env("HOME");
#if defined(_WIN32)
  for (const char* v : {"ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"})
    if (auto p = env(v); !p.empty()) prefix.push_back(fs::path(p));
  if (prefix.empty()) prefix = {fs::path("C:/Program Files (x86)"), fs::path("C:/Program Files")};
  for (auto& p : prefix) steam.push_back(p / "Steam");
  if (auto l = env("LOCALAPPDATA"); !l.empty()) steam.push_back(fs::path(l) / "Steam");
  if (auto up = env("USERPROFILE"); !up.empty()) home = up;
#elif defined(__APPLE__)
  if (!home.empty()) steam.push_back(fs::path(home) / "Library/Application Support/Steam");
#else
  if (!home.empty()) {
    for (const char* s : {".steam/steam", ".local/share/Steam", ".var/app/com.valvesoftware.Steam/.local/share/Steam"}) steam.push_back(fs::path(home) / s);
    for (const char* w : {".wine/drive_c/Program Files (x86)", ".wine/drive_c/Program Files"}) prefix.push_back(fs::path(home) / w);
  }
#endif
  for (auto& s : steam) {
    std::vector<fs::path> libs;
    steamLibraries(s, libs);
    for (auto& l : libs) roots.push_back(l / "steamapps" / "common");
  }
  for (auto& p : prefix)
    for (const char* sub : {"Origin Games", "EA Games", "PopCap Games"}) roots.push_back(p / sub);
  if (!home.empty()) roots.push_back(fs::path(home) / "Games");
  return roots;
}
}

inline const char* gameTitle(Game g) { return g == Game::Nights ? "Peggle Nights" : "Peggle Deluxe"; }

inline bool hasMainPak(const std::filesystem::path& dir) {
  std::error_code ec;
  return !dir.empty() && std::filesystem::is_regular_file(dir / "main.pak", ec);
}

inline std::filesystem::path resolveGameDir(const std::filesystem::path& given) {
  namespace fs = std::filesystem;
  using namespace gamepaths_detail;
  if (given.empty()) return {};
  fs::path dir = given.has_filename() ? given : given.parent_path();
  if (hasMainPak(dir)) return dir;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return {};
  auto inBundle = [](const fs::path& app) { return app / "Contents" / "Resources"; };
  if (lower(utf8(dir.extension())) == ".app" && hasMainPak(inBundle(dir))) return inBundle(dir);
  std::vector<fs::path> apps;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_directory(e2) && lower(utf8(it->path().extension())) == ".app") apps.push_back(it->path());
  }
  std::sort(apps.begin(), apps.end());
  for (auto& a : apps)
    if (hasMainPak(inBundle(a))) return inBundle(a);
  return {};
}

inline Game classifyGameDir(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  using namespace gamepaths_detail;
  auto isNights = [](const std::string& n) { return lower(n).find("nights") != std::string::npos; };
  if (isNights(leafName(dir))) return Game::Nights;
  fs::path p = dir;
  if (!p.has_filename()) p = p.parent_path();
  if (lower(leafName(p)) == "resources" && lower(leafName(p.parent_path())) == "contents" && isNights(leafName(p.parent_path().parent_path()))) return Game::Nights;
  std::error_code ec;
  for (fs::directory_iterator it(dir / "levelpacks", ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2) && lower(utf8(it->path().extension())) == ".pak") return Game::Nights;
  }
  ec.clear();
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (isNights(utf8(it->path().filename()))) return Game::Nights;
  return Game::Deluxe;
}

inline std::vector<std::string> gameFolderNames(Game g) {
  if (g == Game::Nights) return {"Peggle Nights"};
  return {"Peggle Deluxe", "Peggle"};
}

inline std::filesystem::path findGameIn(const std::filesystem::path& parent, Game g) {
  namespace fs = std::filesystem;
  using namespace gamepaths_detail;
  std::error_code ec;
  if (!fs::is_directory(parent, ec)) return {};
  std::vector<fs::path> subs;
  for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_directory(e2)) subs.push_back(it->path());
  }
  std::sort(subs.begin(), subs.end());
  for (auto& name : gameFolderNames(g))
    for (auto& s : subs)
      if (lower(leafName(s)) == lower(name))
        if (fs::path r = resolveGameDir(s); !r.empty()) return r;
  return {};
}

inline std::vector<GameCandidate> gameCandidates(Game g) {
  namespace fs = std::filesystem;
  using namespace gamepaths_detail;
  std::vector<GameCandidate> out;
  std::error_code ec;
  auto add = [&](const fs::path& d) {
    fs::path abs = fs::absolute(d, ec).lexically_normal();
    fs::path r = resolveGameDir(abs);
    fs::path dir = r.empty() ? abs : r;
    for (auto& c : out)
      if (c.dir == dir) return;
    out.push_back({dir, !r.empty()});
  };
  for (auto& n : gameFolderNames(g))
    for (auto& r : searchRoots()) add(r / n);
  return out;
}

inline std::filesystem::path findGameDir(Game g) {
  for (auto& c : gameCandidates(g))
    if (c.found) return c.dir;
  return {};
}

inline std::string pathToUtf8(const std::filesystem::path& p) { return gamepaths_detail::utf8(p); }
inline std::filesystem::path pathFromUtf8(const std::string& s) { return gamepaths_detail::fromUtf8(s); }

}
