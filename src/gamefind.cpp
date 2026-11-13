#include "gamefind.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "gamepaths.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pp {

namespace {

namespace fs = std::filesystem;

constexpr std::uintmax_t kMaxRecordBytes = 8u << 20;
constexpr int kMaxVdfDepth = 64;

std::string lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

std::string leafName(const fs::path& p) {
  fs::path q = p;
  if (!q.has_filename()) q = q.parent_path();
  return pathToUtf8(q.filename());
}

fs::path pathFromRecord(const std::string& s) {
#ifdef _WIN32
  if (s.empty()) return {};
  UINT page = CP_UTF8;
  int n = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) {
    page = CP_ACP;
    n = MultiByteToWideChar(page, 0, s.data(), (int)s.size(), nullptr, 0);
  }
  if (n <= 0) return {};
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(page, 0, s.data(), (int)s.size(), w.data(), n);
  return fs::path(w);
#else
  return fs::path(s);
#endif
}

bool readText(const fs::path& file, std::string& out) {
  if (pathKind(file) != PathKind::File) return false;
  std::error_code ec;
  std::uintmax_t size = fs::file_size(file, ec);
  if (ec || size > kMaxRecordBytes) return false;
  std::ifstream f(file, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

struct VdfReader {
  enum Kind { End, Open, Close, Text };
  const std::string& s;
  size_t i = 0;

  Kind next(std::string& text) {
    for (;;) {
      while (i < s.size() && (unsigned char)s[i] <= ' ') i++;
      if (i >= s.size()) return End;
      char c = s[i];
      if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
        while (i < s.size() && s[i] != '\n') i++;
        continue;
      }
      if (c == '{') {
        i++;
        return Open;
      }
      if (c == '}') {
        i++;
        return Close;
      }
      text.clear();
      if (c == '"') {
        i++;
        while (i < s.size() && s[i] != '"') {
          if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '\\' || s[i + 1] == '"')) i++;
          text += s[i++];
        }
        if (i < s.size()) i++;
        return Text;
      }
      while (i < s.size() && (unsigned char)s[i] > ' ' && s[i] != '{' && s[i] != '}' && s[i] != '"') text += s[i++];
      if (text.size() >= 2 && text.front() == '[' && text.back() == ']') continue;
      return Text;
    }
  }
};

bool parseBlock(VdfReader& r, VdfNode& into, int depth) {
  if (depth > kMaxVdfDepth) return false;
  std::string key, value;
  for (;;) {
    VdfReader::Kind k = r.next(key);
    if (k == VdfReader::End) return depth == 0;
    if (k == VdfReader::Close) return depth > 0;
    if (k == VdfReader::Open) return false;
    VdfNode n;
    n.key = key;
    VdfReader::Kind v = r.next(value);
    if (v == VdfReader::Open) {
      n.block = true;
      bool ok = parseBlock(r, n, depth + 1);
      into.kids.push_back(std::move(n));
      if (!ok) return false;
    } else if (v == VdfReader::Text) {
      n.value = value;
      into.kids.push_back(std::move(n));
    } else {
      return false;
    }
  }
}

bool allDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

void collectBaseFolders(const VdfNode& n, std::vector<std::string>& out, int depth) {
  if (depth > kMaxVdfDepth) return;
  for (auto& k : n.kids) {
    if (k.block) collectBaseFolders(k, out, depth + 1);
    else if (lower(k.key).rfind("baseinstallfolder_", 0) == 0 && !k.value.empty()) out.push_back(k.value);
  }
}

fs::path absolutePath(const fs::path& p) {
  std::error_code ec;
  fs::path a = fs::absolute(p, ec);
  if (ec) a = p;
  return a.lexically_normal();
}

std::string placeKey(const fs::path& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  if (ec || c.empty()) c = absolutePath(p);
  std::string k = pathToUtf8(c);
  while (k.size() > 1 && (k.back() == '/' || k.back() == '\\')) k.pop_back();
#if defined(_WIN32) || defined(__APPLE__)
  k = lower(k);
#endif
  return k;
}

std::vector<fs::path> subFolders(const fs::path& parent, std::string* why = nullptr) {
  std::vector<fs::path> subs;
  std::error_code ec;
  fs::directory_iterator it(parent, ec), end;
  if (ec && why) *why = "error " + std::to_string(ec.value());
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_directory(e2)) subs.push_back(it->path());
  }
  std::sort(subs.begin(), subs.end());
  return subs;
}

bool namedLike(const fs::path& folder, Game g) {
  std::string leaf = lower(leafName(folder));
  for (auto& name : gameFolderNames(g))
    if (leaf == lower(name)) return true;
  return false;
}

std::string manifestName(Game g) { return "appmanifest_" + std::to_string(steamAppId(g)) + ".acf"; }

const char* const kNotThere = "folder not there";
const char* const kNotAFolder = "a file, not a folder";

std::string unreadable(const std::string& why) { return why.empty() ? std::string("could not be read") : "could not be read (" + why + ")"; }

std::string seenFor(PathKind k, const std::string& why) {
  if (k == PathKind::Missing) return kNotThere;
  if (k == PathKind::Unreadable) return unreadable(why);
  if (k == PathKind::File) return kNotAFolder;
  return "folder there";
}

struct Spot {
  fs::path game;
  std::string seen;
  bool there = false;
};

Spot lookAtGameFolder(const fs::path& dir) {
  Spot s;
  std::string why;
  PathKind k = pathKind(dir, &why);
  if (k != PathKind::Folder) {
    s.seen = seenFor(k, why);
    return s;
  }
  s.there = true;
  s.game = resolveGameDir(dir);
  if (!s.game.empty()) s.seen = "holds the game";
  else if (pathKind(dir / "main.pak", &why) == PathKind::Unreadable) s.seen = "folder there, but main.pak " + unreadable(why);
  else s.seen = "folder there, but no main.pak in it";
  return s;
}

struct Finder {
  const FindSources& src;
  FoundGames out;
  std::vector<std::string> seenRoots;
  std::vector<std::string> seenLibraries;

  explicit Finder(const FindSources& s) : src(s) { out.notes = s.notes; }

  bool wanted(int i) const { return !src.skip[i] && out.dir[i].empty(); }

  bool done() const { return !wanted(0) && !wanted(1); }

  static bool firstVisit(std::vector<std::string>& seen, const fs::path& p) {
    std::string k = placeKey(p);
    if (std::find(seen.begin(), seen.end(), k) != seen.end()) return false;
    seen.push_back(k);
    return true;
  }

  size_t add(FindLook::Kind kind, const fs::path& path, const std::string& from, const std::string& seen, bool guessed = false, int game = -1) {
    FindLook l;
    l.kind = kind;
    l.path = path;
    l.from = from;
    l.seen = seen;
    l.guessed = guessed;
    l.game = game;
    out.looks.push_back(std::move(l));
    return out.looks.size() - 1;
  }

  void lookAtFolder(FindLook::Kind kind, int i, const fs::path& given, const std::string& from, const std::string& source, bool guessed) {
    fs::path dir = absolutePath(given);
    Spot spot = lookAtGameFolder(dir);
    FindLook& l = out.looks[add(kind, dir, from, spot.seen, guessed, i)];
    l.there = spot.there;
    if (!wanted(i) || spot.game.empty()) return;
    out.dir[i] = spot.game;
    out.from[i] = source;
    l.found = true;
  }

  void remembered() {
    for (int i = 0; i < 2; i++) {
      if (src.skip[i]) add(FindLook::Kind::Remembered, {}, "", "its own copy is used", false, i);
      else if (src.remembered[i].empty()) add(FindLook::Kind::Remembered, {}, "", "no folder is remembered", false, i);
      else lookAtFolder(FindLook::Kind::Remembered, i, src.remembered[i], "remembered from an earlier start", "remembered from an earlier start", false);
    }
  }

  void library(const fs::path& lib, FindLook::Kind kind, const std::string& from, bool guessed) {
    std::string why;
    PathKind k = pathKind(lib, &why);
    size_t top = add(kind, lib, from, seenFor(k, why), guessed);
    if (k != PathKind::Folder) return;
    out.looks[top].there = true;
    fs::path apps;
    for (const char* name : {"steamapps", "SteamApps"})
      if (apps.empty() && pathKind(lib / name) == PathKind::Folder) apps = lib / name;
    if (apps.empty()) {
      out.looks[top].seen = "folder there, but no steamapps folder in it";
      return;
    }
    const std::string source = "Steam library " + pathToUtf8(lib);
    for (int i = 0; i < 2; i++) {
      if (!wanted(i)) continue;
      Game g = gameAt(i);
      const std::string record = manifestName(g);
      PathKind rk = pathKind(apps / record, &why);
      fs::path named;
      bool said = false;
      if (rk == PathKind::File) {
        std::string dirName = steamInstallDir(apps / record);
        if (dirName.empty()) {
          add(FindLook::Kind::Game, {}, "", "Steam's record " + record + " names no folder", false, i);
        } else {
          fs::path given = pathFromRecord(dirName);
          named = absolutePath(given.is_absolute() ? given : apps / "common" / given);
          lookAtFolder(FindLook::Kind::Game, i, named, "named by Steam's record " + record, source, false);
        }
        said = true;
      } else if (rk == PathKind::Unreadable) {
        add(FindLook::Kind::Game, {}, "", "Steam's record " + record + " " + unreadable(why), false, i);
        said = true;
      }
      if (!out.dir[i].empty()) continue;
      std::string listWhy;
      for (auto& sub : subFolders(apps / "common", &listWhy)) {
        if (!namedLike(sub, g)) continue;
        if (!named.empty() && placeKey(sub) == placeKey(named)) continue;
        lookAtFolder(FindLook::Kind::Game, i, sub, "found by its folder name", source, false);
        said = true;
        if (!out.dir[i].empty()) break;
      }
      if (said) continue;
      if (listWhy.empty()) add(FindLook::Kind::Game, {}, "", "no Steam record " + record + " and no folder with its name", false, i);
      else add(FindLook::Kind::Game, {}, "", "no Steam record " + record + ", and the common folder " + unreadable(listWhy), false, i);
    }
  }

  void steamRoot(const FindPlace& root) {
    if (root.path.empty()) {
      add(FindLook::Kind::Steam, {}, root.from, "names no Steam folder", false);
      return;
    }
    if (!firstVisit(seenRoots, root.path)) return;
    if (firstVisit(seenLibraries, root.path)) library(root.path, FindLook::Kind::Steam, root.from, root.guessed);
    if (pathKind(root.path) != PathKind::Folder) return;
    for (auto& lib : steamLibraryPaths(root.path)) {
      if (done()) return;
      if (!firstVisit(seenLibraries, lib.path)) continue;
      library(lib.path, FindLook::Kind::Library, lib.from, false);
    }
  }

  void steam() {
    for (auto& root : src.steamRoots) {
      if (done()) break;
      steamRoot(root);
    }
  }

  void installs() {
    for (int i = 0; i < 2; i++)
      for (auto& place : src.installs[i]) {
        if (!wanted(i)) break;
        lookAtFolder(FindLook::Kind::Install, i, place.path, place.from, place.from, place.guessed);
      }
  }

  void extraLibraries() {
    for (auto& lib : src.steamLibraries) {
      if (done()) break;
      if (!firstVisit(seenLibraries, lib.path)) continue;
      library(lib.path, FindLook::Kind::Library, lib.from, lib.guessed);
    }
  }

  void folders(const std::vector<FindPlace>& places) {
    std::vector<std::string> seen;
    for (auto& place : places) {
      if (done()) break;
      if (!firstVisit(seen, place.path)) continue;
      fs::path dir = absolutePath(place.path);
      std::string why;
      PathKind k = pathKind(dir, &why);
      size_t top = add(FindLook::Kind::Other, dir, place.from, seenFor(k, why), place.guessed);
      if (k != PathKind::Folder) continue;
      out.looks[top].there = true;
      std::string listWhy;
      std::vector<fs::path> subs = subFolders(dir, &listWhy);
      bool any = false;
      for (int i = 0; i < 2; i++) {
        if (!wanted(i)) continue;
        for (auto& sub : subs) {
          if (!namedLike(sub, gameAt(i))) continue;
          any = true;
          lookAtFolder(FindLook::Kind::Game, i, sub, "found by its folder name", place.from, false);
          if (!out.dir[i].empty()) break;
        }
      }
      if (!listWhy.empty()) out.looks[top].seen = unreadable(listWhy);
      else if (!any) out.looks[top].seen = "folder there, but no game folder in it";
    }
  }

  template <class Step>
  void guarded(const char* what, Step step) {
    try {
      step();
    } catch (const std::exception& e) {
      out.notes.push_back(std::string("Looking at ") + what + " stopped early: " + e.what());
    } catch (...) {
      out.notes.push_back(std::string("Looking at ") + what + " stopped early.");
    }
  }

  void run() {
    if (src.wholeComputer) guarded("the remembered folders", [&] { remembered(); });
    if (!done()) guarded("the Steam folders", [&] { steam(); });
    if (!done()) guarded("the uninstall entries", [&] { installs(); });
    if (!done()) guarded("the Steam folders on other drives", [&] { extraLibraries(); });
    if (!done()) guarded("the publisher folders", [&] { folders(src.publisherDirs); });
    if (!done()) guarded("the Games folders", [&] { folders(src.gamesDirs); });
  }
};

std::string lookLine(const FindLook& l, bool brief) {
  const std::string title = l.game >= 0 ? gameTitle(gameAt(l.game)) : "";
  const std::string path = pathToUtf8(l.path);
  const std::string source = brief || l.from.empty() ? std::string() : " (" + l.from + ")";
  switch (l.kind) {
    case FindLook::Kind::Remembered:
      if (l.path.empty()) return title + ": " + l.seen;
      return title + ", remembered folder " + path + ": " + l.seen;
    case FindLook::Kind::Steam:
      if (l.path.empty()) return "Steam: " + l.from + " " + l.seen;
      return "Steam folder " + path + source + ": " + l.seen;
    case FindLook::Kind::Library:
      return "Steam library " + path + source + ": " + l.seen;
    case FindLook::Kind::Game:
      if (l.path.empty()) return "  " + title + ": " + l.seen;
      return "  " + title + ", " + path + source + ": " + l.seen;
    case FindLook::Kind::Install:
      return title + ", " + path + " (" + l.from + "): " + l.seen;
    case FindLook::Kind::Other:
      break;
  }
  return path + " (" + l.from + "): " + l.seen;
}

#ifdef _WIN32

std::wstring envText(const wchar_t* name) {
  DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
  if (need == 0) return {};
  std::wstring s((size_t)need, L'\0');
  DWORD got = GetEnvironmentVariableW(name, s.data(), need);
  if (got == 0 || got >= need) return {};
  s.resize((size_t)got);
  return s;
}

bool registryText(HKEY root, const wchar_t* key, const wchar_t* value, REGSAM view, std::wstring& out) {
  HKEY h = nullptr;
  if (RegOpenKeyExW(root, key, 0, (REGSAM)(KEY_QUERY_VALUE | view), &h) != ERROR_SUCCESS) return false;
  bool ok = false;
  DWORD type = 0, bytes = 0;
  if (RegQueryValueExW(h, value, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= (DWORD)sizeof(wchar_t)) {
    std::vector<wchar_t> buf((size_t)bytes / sizeof(wchar_t) + 2, L'\0');
    DWORD room = (DWORD)((buf.size() - 1) * sizeof(wchar_t));
    if (RegQueryValueExW(h, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &room) == ERROR_SUCCESS) {
      std::wstring text(buf.data());
      if (type == REG_EXPAND_SZ) {
        DWORD need = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
        if (need > 0) {
          std::vector<wchar_t> wide((size_t)need + 1, L'\0');
          DWORD got = ExpandEnvironmentStringsW(text.c_str(), wide.data(), need);
          if (got > 0 && got <= need) text.assign(wide.data());
        }
      }
      if (!text.empty()) {
        out = text;
        ok = true;
      }
    }
  }
  RegCloseKey(h);
  return ok;
}

fs::path systemSpelling(const std::wstring& text) {
  std::wstring w = text;
  std::replace(w.begin(), w.end(), L'/', L'\\');
  DWORD need = GetLongPathNameW(w.c_str(), nullptr, 0);
  if (need > 0) {
    std::vector<wchar_t> buf((size_t)need + 1, L'\0');
    DWORD got = GetLongPathNameW(w.c_str(), buf.data(), need + 1);
    if (got > 0 && got <= need) w.assign(buf.data(), got);
  }
  if (w.size() >= 3 && w[1] == L':' && w[2] == L'\\') {
    std::wstring walked = w.substr(0, 2);
    size_t at = 3;
    while (at <= w.size()) {
      size_t end = w.find(L'\\', at);
      if (end == std::wstring::npos) end = w.size();
      std::wstring part = w.substr(at, end - at);
      if (!part.empty()) {
        WIN32_FIND_DATAW found;
        HANDLE probe = FindFirstFileW((walked + L"\\" + part).c_str(), &found);
        if (probe != INVALID_HANDLE_VALUE) {
          part = found.cFileName;
          FindClose(probe);
        }
        walked += L"\\" + part;
      }
      at = end + 1;
    }
    w = walked.size() == 2 ? walked + L"\\" : walked;
  }
  if (w.size() >= 2 && w[1] == L':' && w[0] >= L'a' && w[0] <= L'z') w[0] = (wchar_t)(w[0] - L'a' + L'A');
  return fs::path(w);
}

struct RegistrySpot {
  HKEY root;
  const wchar_t* key;
  const wchar_t* value;
  const char* name;
};

std::vector<FindPlace> registryPlaces(const RegistrySpot* spots, size_t count, std::vector<std::string>* notes) {
  std::vector<FindPlace> out;
  for (size_t i = 0; i < count; i++) {
    const RegistrySpot& spot = spots[i];
    std::string line = std::string("Registry ") + spot.name + ":";
    bool any = false;
    const REGSAM views[2] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    const char* viewNames[2] = {"64-bit view", "32-bit view"};
    for (int v = 0; v < 2; v++) {
      std::wstring text;
      if (!registryText(spot.root, spot.key, spot.value, views[v], text)) continue;
      fs::path p = systemSpelling(text);
      line += std::string(any ? ", " : " ") + pathToUtf8(p) + " (" + viewNames[v] + ")";
      any = true;
      out.push_back({p, std::string("registry ") + spot.name + ", " + viewNames[v], false});
    }
    if (!any) line += " not set";
    if (notes) notes->push_back(line + ".");
  }
  return out;
}

#endif

}

std::string pathToUtf8(const fs::path& p) {
#ifdef _WIN32
  const std::wstring& w = p.native();
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
#else
  return p.string();
#endif
}

fs::path pathFromUtf8(const std::string& s) {
#ifdef _WIN32
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return fs::path(w);
#else
  return fs::path(s);
#endif
}

PathKind pathKind(const fs::path& p, std::string* why) {
  if (why) why->clear();
  if (p.empty()) return PathKind::Missing;
#ifdef _WIN32
  DWORD attributes = GetFileAttributesW(p.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
    switch (error) {
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
      case ERROR_INVALID_NAME:
      case ERROR_INVALID_DRIVE:
      case ERROR_NOT_READY:
      case ERROR_BAD_NETPATH:
      case ERROR_BAD_NET_NAME:
      case ERROR_BAD_PATHNAME:
        return PathKind::Missing;
      default:
        break;
    }
    if (why) *why = "Windows error " + std::to_string((unsigned long)error);
    return PathKind::Unreadable;
  }
  return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? PathKind::Folder : PathKind::File;
#else
  std::error_code ec;
  fs::file_status st = fs::status(p, ec);
  if (st.type() == fs::file_type::not_found) return PathKind::Missing;
  if (ec || st.type() == fs::file_type::none || st.type() == fs::file_type::unknown) {
    if (why && ec) *why = "error " + std::to_string(ec.value());
    return PathKind::Unreadable;
  }
  return st.type() == fs::file_type::directory ? PathKind::Folder : PathKind::File;
#endif
}

bool hasMainPak(const fs::path& dir) { return !dir.empty() && pathKind(dir / "main.pak") == PathKind::File; }

fs::path resolveGameDir(const fs::path& given) {
  if (given.empty()) return {};
  fs::path dir = given.has_filename() ? given : given.parent_path();
  if (hasMainPak(dir)) return dir;
  if (pathKind(dir) != PathKind::Folder) return {};
  auto inBundle = [](const fs::path& app) { return app / "Contents" / "Resources"; };
  if (lower(pathToUtf8(dir.extension())) == ".app" && hasMainPak(inBundle(dir))) return inBundle(dir);
  for (auto& sub : subFolders(dir))
    if (lower(pathToUtf8(sub.extension())) == ".app" && hasMainPak(inBundle(sub))) return inBundle(sub);
  return {};
}

Game classifyGameDir(const fs::path& dir) {
  auto isNights = [](const std::string& n) { return lower(n).find("nights") != std::string::npos; };
  if (isNights(leafName(dir))) return Game::Nights;
  fs::path p = dir;
  if (!p.has_filename()) p = p.parent_path();
  if (lower(leafName(p)) == "resources" && lower(leafName(p.parent_path())) == "contents" && isNights(leafName(p.parent_path().parent_path()))) return Game::Nights;
  std::error_code ec;
  for (fs::directory_iterator it(dir / "levelpacks", ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2) && lower(pathToUtf8(it->path().extension())) == ".pak") return Game::Nights;
  }
  ec.clear();
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (isNights(pathToUtf8(it->path().filename()))) return Game::Nights;
  return Game::Deluxe;
}

std::vector<std::string> gameFolderNames(Game g) {
  if (g == Game::Nights) return {"Peggle Nights"};
  return {"Peggle Deluxe", "Peggle"};
}

fs::path findGameIn(const fs::path& parent, Game g) {
  if (pathKind(parent) != PathKind::Folder) return {};
  std::vector<fs::path> subs = subFolders(parent);
  for (auto& name : gameFolderNames(g))
    for (auto& s : subs)
      if (lower(leafName(s)) == lower(name))
        if (fs::path r = resolveGameDir(s); !r.empty()) return r;
  return {};
}

const VdfNode* VdfNode::child(const std::string& name) const {
  std::string want = lower(name);
  for (auto& k : kids)
    if (lower(k.key) == want) return &k;
  return nullptr;
}

bool parseVdf(const std::string& text, VdfNode& root) {
  root = VdfNode();
  root.block = true;
  VdfReader r{text, 0};
  if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) r.i = 3;
  return parseBlock(r, root, 0);
}

std::vector<FindPlace> steamLibraryPaths(const fs::path& steamRoot) {
  std::vector<FindPlace> out;
  std::vector<fs::path> lists = {steamRoot / "steamapps" / "libraryfolders.vdf", steamRoot / "SteamApps" / "libraryfolders.vdf", steamRoot / "config" / "libraryfolders.vdf"};
  for (auto& file : lists) {
    std::string text;
    if (!readText(file, text)) continue;
    VdfNode root;
    parseVdf(text, root);
    for (auto& top : root.kids) {
      if (!top.block || lower(top.key) != "libraryfolders") continue;
      for (auto& entry : top.kids) {
        if (!allDigits(entry.key)) continue;
        std::string path = entry.value;
        if (entry.block) {
          const VdfNode* p = entry.child("path");
          path = p ? p->value : std::string();
        }
        if (!path.empty()) out.push_back({pathFromRecord(path), "listed in " + pathToUtf8(file), false});
      }
    }
  }
  fs::path config = steamRoot / "config" / "config.vdf";
  std::string text;
  if (readText(config, text)) {
    VdfNode root;
    parseVdf(text, root);
    std::vector<std::string> base;
    collectBaseFolders(root, base, 0);
    for (auto& b : base) out.push_back({pathFromRecord(b), "listed in " + pathToUtf8(config), false});
  }
  return out;
}

std::string steamInstallDir(const fs::path& manifest) {
  std::string text;
  if (!readText(manifest, text)) return {};
  VdfNode root;
  parseVdf(text, root);
  for (auto& top : root.kids) {
    if (!top.block) continue;
    if (const VdfNode* d = top.child("installdir"); d && !d->block && !d->value.empty()) return d->value;
    if (const VdfNode* d = top.child("appinstalldir"); d && !d->block && !d->value.empty()) return d->value;
  }
  return {};
}

FoundGames findGames(const FindSources& sources) {
  Finder f(sources);
  f.run();
  return f.out;
}

std::vector<std::string> searchReport(const FoundGames& found) {
  std::vector<std::string> lines;
  for (auto& n : found.notes) lines.push_back(n);
  for (auto& l : found.looks) lines.push_back(lookLine(l, false));
  for (int i = 0; i < 2; i++) {
    if (found.dir[i].empty()) lines.push_back(std::string(gameTitle(gameAt(i))) + " was not found.");
    else lines.push_back(std::string(gameTitle(gameAt(i))) + " is in " + pathToUtf8(found.dir[i]) + " (" + found.from[i] + ").");
  }
  return lines;
}

std::vector<std::string> searchSummary(const FoundGames& found, size_t maxLines) {
  std::vector<std::string> lines;
  std::vector<std::string> rest;
  bool under = false;
  for (auto& l : found.looks) {
    if (l.kind == FindLook::Kind::Game) {
      if (under) lines.push_back(lookLine(l, true));
      continue;
    }
    under = false;
    if (l.kind == FindLook::Kind::Remembered && l.path.empty()) continue;
    bool own = !l.guessed || (l.there && l.kind != FindLook::Kind::Other) || (l.kind == FindLook::Kind::Other && l.there && l.seen == "folder there");
    if (own) {
      lines.push_back(lookLine(l, true));
      under = l.kind != FindLook::Kind::Remembered && l.kind != FindLook::Kind::Install;
    } else if (l.seen == kNotThere) {
      rest.push_back(pathToUtf8(l.path));
    } else {
      rest.push_back(pathToUtf8(l.path) + " (" + l.seen + ")");
    }
  }
  if (maxLines > 2 && lines.size() + 1 > maxLines) {
    size_t cut = lines.size() - (maxLines - 2);
    lines.resize(maxLines - 2);
    lines.push_back("(" + std::to_string(cut) + " more lines are in the log)");
  }
  if (!rest.empty()) {
    const size_t shown = std::min<size_t>(rest.size(), 4);
    std::string line = "Nothing in the other usual places:";
    for (size_t i = 0; i < shown; i++) line += (i ? "; " : " ") + rest[i];
    if (rest.size() > shown) line += "; and " + std::to_string(rest.size() - shown) + " more";
    lines.push_back(line);
  }
  return lines;
}

FindSources systemFindSources(const fs::path& programDir) {
  FindSources s;
  std::vector<fs::path> prefix;
  fs::path home;
#if defined(_WIN32)
  s.steamRoots = registrySteamRoots(&s.notes);
  if (s.steamRoots.empty()) s.steamRoots.push_back({fs::path(), "the registry", false});
  for (const wchar_t* v : {L"ProgramFiles(x86)", L"ProgramFiles", L"ProgramW6432"})
    if (std::wstring p = envText(v); !p.empty()) prefix.push_back(fs::path(p));
  for (const wchar_t* fixed : {L"C:\\Program Files (x86)", L"C:\\Program Files"}) prefix.push_back(fs::path(fixed));
  for (auto& p : prefix) s.steamRoots.push_back({p / "Steam", "usual place", true});
  if (std::wstring l = envText(L"LOCALAPPDATA"); !l.empty()) s.steamRoots.push_back({fs::path(l) / "Steam", "usual place", true});
  for (int i = 0; i < 2; i++) s.installs[i] = registryInstallDirs(gameAt(i), &s.notes);
  std::string drives;
  for (auto& d : fixedDriveRoots()) {
    drives += (drives.empty() ? " " : ", ") + pathToUtf8(d);
    s.steamLibraries.push_back({d / "SteamLibrary", "top of a drive", true});
    s.steamLibraries.push_back({d / "Steam", "top of a drive", true});
    s.steamLibraries.push_back({d / "Program Files (x86)" / "Steam", "usual place on a drive", true});
  }
  s.notes.push_back("Fixed drives:" + (drives.empty() ? std::string(" none") : drives) + ".");
  if (std::wstring up = envText(L"USERPROFILE"); !up.empty()) home = fs::path(up);
#else
  if (const char* h = std::getenv("HOME"); h && *h) home = fs::path(h);
#if defined(__APPLE__)
  if (!home.empty()) s.steamRoots.push_back({home / "Library" / "Application Support" / "Steam", "usual place", true});
#else
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) s.steamRoots.push_back({fs::path(x) / "Steam", "usual place", true});
  if (!home.empty()) {
    for (const char* r : {".steam/steam", ".steam/root", ".steam/debian-installation", ".local/share/Steam"}) s.steamRoots.push_back({home / r, "usual place", true});
    for (const char* r : {".var/app/com.valvesoftware.Steam/.local/share/Steam", ".var/app/com.valvesoftware.Steam/.steam/steam", ".var/app/com.valvesoftware.Steam/data/Steam"})
      s.steamRoots.push_back({home / r, "Flatpak", true});
    for (const char* r : {"snap/steam/common/.local/share/Steam", "snap/steam/common/.steam/steam"}) s.steamRoots.push_back({home / r, "Snap", true});
    for (const char* w : {".wine/drive_c/Program Files (x86)", ".wine/drive_c/Program Files"}) prefix.push_back(home / w);
  }
#endif
#endif
  for (auto& p : prefix)
    for (const char* sub : {"PopCap Games", "Origin Games", "EA Games"}) s.publisherDirs.push_back({p / sub, "publisher folder", true});
  if (!programDir.empty())
    for (const char* r : {"Games", "../Games", "../../Games"}) s.gamesDirs.push_back({(programDir / r).lexically_normal(), "Games folder near PegglePort", true});
  std::error_code ec;
  if (fs::path here = fs::current_path(ec); !ec && !here.empty())
    for (const char* r : {"Games", "../Games", "../../Games"}) s.gamesDirs.push_back({(here / r).lexically_normal(), "Games folder", true});
  if (!home.empty()) s.gamesDirs.push_back({home / "Games", "Games folder in your home folder", true});
  return s;
}

bool GameFolderStore::load(const fs::path& file) {
  *this = GameFolderStore();
  if (file.empty()) return false;
  std::ifstream f(file);
  if (!f) return false;
  std::string line;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = lower(line.substr(0, eq)), v = line.substr(eq + 1);
    if (k == "deluxe") dir[0] = pathFromUtf8(v);
    else if (k == "nights") dir[1] = pathFromUtf8(v);
    else if (k == "home") home = lower(v) == "nights" ? Game::Nights : Game::Deluxe;
  }
  return true;
}

bool GameFolderStore::save(const fs::path& file) const {
  if (file.empty()) return false;
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream o(tmp, std::ios::trunc);
    if (!o) return false;
    o << "Deluxe=" << pathToUtf8(dir[0]) << "\nNights=" << pathToUtf8(dir[1]) << "\nHome=" << (home == Game::Nights ? "Nights" : "Deluxe") << "\n";
    if (!o) return false;
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

#ifdef _WIN32

std::vector<FindPlace> registrySteamRoots(std::vector<std::string>* notes) {
  static const RegistrySpot spots[] = {
      {HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", "HKCU\\Software\\Valve\\Steam SteamPath"},
      {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", "HKLM\\SOFTWARE\\WOW6432Node\\Valve\\Steam InstallPath"},
      {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", "HKLM\\SOFTWARE\\Valve\\Steam InstallPath"},
  };
  return registryPlaces(spots, sizeof(spots) / sizeof(spots[0]), notes);
}

std::vector<FindPlace> registryInstallDirs(Game g, std::vector<std::string>* notes) {
  static const RegistrySpot deluxe[] = {
      {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 3480", L"InstallLocation", "HKLM uninstall entry Steam App 3480"},
      {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 3480", L"InstallLocation", "HKCU uninstall entry Steam App 3480"},
  };
  static const RegistrySpot nights[] = {
      {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 3540", L"InstallLocation", "HKLM uninstall entry Steam App 3540"},
      {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 3540", L"InstallLocation", "HKCU uninstall entry Steam App 3540"},
  };
  return registryPlaces(g == Game::Nights ? nights : deluxe, 2, notes);
}

std::vector<fs::path> fixedDriveRoots() {
  std::vector<fs::path> out;
  DWORD mask = GetLogicalDrives();
  for (int i = 0; i < 26; i++) {
    if (!(mask & ((DWORD)1 << i))) continue;
    wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', L'\0'};
    if (GetDriveTypeW(root) == DRIVE_FIXED) out.push_back(fs::path(root));
  }
  return out;
}

#endif

}
