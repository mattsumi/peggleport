#include "gamecopy.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "gamepaths.h"
#include "vfs.h"

namespace pp {

namespace {

namespace fs = std::filesystem;

const char* const kManifestName = "manifest.txt";
const char* const kManifestHead = "PegglePort game files 1";
constexpr std::size_t kChunk = 1u << 20;
constexpr int kMaxDepth = 8;
constexpr std::uintmax_t kMaxManifestBytes = 4u << 20;
constexpr std::uint64_t kReserveBytes = 4u << 20;
constexpr std::uint32_t kPakMagic = 0xBAC04AC0u;
constexpr int kRenameTries = 6;

struct Item {
  std::string rel;
  fs::path path;
  std::uint64_t size = 0;
  std::uint32_t crc = 0;
};

std::string lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

const std::array<std::uint32_t, 256>& crcTable() {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; i++) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  return table;
}

std::uint32_t crcUpdate(std::uint32_t crc, const unsigned char* p, std::size_t n) {
  const auto& t = crcTable();
  crc = ~crc;
  for (std::size_t i = 0; i < n; i++) crc = t[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}

std::string hex8(std::uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof b, "%08x", (unsigned)v);
  return b;
}

std::string megabytes(std::uint64_t bytes) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f MB", (double)bytes / 1048576.0);
  return b;
}

bool safeName(const std::string& n) {
  if (n.empty() || n.front() == '/') return false;
  for (char c : n)
    if (c == '\\' || c == ':' || (unsigned char)c < 32) return false;
  std::size_t at = 0;
  while (at <= n.size()) {
    std::size_t e = n.find('/', at);
    if (e == std::string::npos) e = n.size();
    const std::string part = n.substr(at, e - at);
    if (part.empty() || part == "." || part == "..") return false;
    at = e + 1;
  }
  return true;
}

bool pathHasNoLinks(const fs::path& path) {
  std::error_code ec;
  const fs::path absolute = fs::absolute(path, ec);
  if (ec) return false;
  fs::path p;
  for (const auto& part : absolute) {
    p /= part;
    const auto st = fs::symlink_status(p, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return false;
    if (fs::is_symlink(st)) return false;
  }
  return true;
}

bool copyHasNoLinks(const fs::path& copyDir) {
  if (!pathHasNoLinks(copyDir)) return false;
  std::error_code ec;
  if (!fs::exists(copyDir, ec)) return !ec;
  for (fs::recursive_directory_iterator walk(copyDir, ec), end; !ec && walk != end; walk.increment(ec)) {
    if (fs::is_symlink(walk->symlink_status(ec))) return false;
    if (ec) return false;
  }
  return !ec;
}

bool parseNumber(const std::string& s, std::uint64_t& out) {
  if (s.empty() || s.size() > 19) return false;
  std::uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (std::uint64_t)(c - '0');
  }
  out = v;
  return true;
}

bool parseHex(const std::string& s, std::uint32_t& out) {
  if (s.size() != 8) return false;
  std::uint32_t v = 0;
  for (char c : s) {
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | (std::uint32_t)d;
  }
  out = v;
  return true;
}

bool replaceFile(const fs::path& from, const fs::path& to) {
  for (int attempt = 0; attempt < kRenameTries; attempt++) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;
    fs::remove(to, ec);
    ec.clear();
    fs::rename(from, to, ec);
    if (!ec) return true;
#ifdef _WIN32
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
#else
    break;
#endif
  }
  return false;
}

bool listFolder(const fs::path& dir, const std::string& rel, int depth, std::vector<Item>& out, std::string& why) {
  std::error_code ec;
  std::vector<fs::path> entries;
  for (fs::directory_iterator walk(dir, ec), end; !ec && walk != end; walk.increment(ec)) entries.push_back(walk->path());
  if (ec) {
    why = "cannot list " + pathToUtf8(dir);
    return false;
  }
  std::sort(entries.begin(), entries.end());
  for (auto& p : entries) {
    const std::string name = pathToUtf8(p.filename());
    const std::string childRel = rel.empty() ? name : rel + "/" + name;
    fs::file_status st = fs::status(p, ec);
    if (ec) {
      why = "cannot look at " + pathToUtf8(p);
      return false;
    }
    if (fs::is_directory(st)) {
      if (depth >= kMaxDepth) continue;
      if (!listFolder(p, childRel, depth + 1, out, why)) return false;
    } else if (fs::is_regular_file(st)) {
      if (!safeName(childRel)) {
        why = "the name " + childRel + " cannot be copied";
        return false;
      }
      Item item;
      item.rel = childRel;
      item.path = p;
      const std::uintmax_t size = fs::file_size(p, ec);
      if (ec) {
        why = "cannot read the size of " + pathToUtf8(p);
        return false;
      }
      item.size = (std::uint64_t)size;
      out.push_back(std::move(item));
    }
  }
  return true;
}

bool listGameFiles(const fs::path& install, std::vector<Item>& out, std::string& why) {
  std::error_code ec;
  std::vector<fs::path> top;
  for (fs::directory_iterator walk(install, ec), end; !ec && walk != end; walk.increment(ec)) top.push_back(walk->path());
  if (ec) {
    why = "cannot list " + pathToUtf8(install);
    return false;
  }
  std::sort(top.begin(), top.end());
  bool pak = false;
  for (auto& p : top) {
    const std::string name = pathToUtf8(p.filename());
    const std::string key = lower(name);
    const bool isPak = key == "main.pak";
    const bool isFolder = key == "music" || key == "properties" || key == "levelpacks";
    if (!isPak && !isFolder) continue;
    fs::file_status st = fs::status(p, ec);
    if (ec) {
      why = "cannot look at " + pathToUtf8(p);
      return false;
    }
    if (isPak) {
      if (!fs::is_regular_file(st)) continue;
      Item item;
      item.rel = name;
      item.path = p;
      const std::uintmax_t size = fs::file_size(p, ec);
      if (ec) {
        why = "cannot read the size of " + pathToUtf8(p);
        return false;
      }
      item.size = (std::uint64_t)size;
      out.push_back(std::move(item));
      pak = true;
    } else if (fs::is_directory(st)) {
      if (!listFolder(p, name, 1, out, why)) return false;
    }
  }
  if (!pak) {
    why = "main.pak is not in " + pathToUtf8(install);
    return false;
  }
  return true;
}

bool copyOne(Item& item, const fs::path& copyDir, std::string& why) {
  const fs::path dest = copyDir / pathFromUtf8(item.rel);
  std::error_code ec;
  fs::create_directories(dest.parent_path(), ec);
  if (ec) {
    why = "cannot make the folder " + pathToUtf8(dest.parent_path());
    return false;
  }
  fs::path part = dest;
  part += ".part";
  std::uint64_t got = 0;
  std::uint32_t crc = 0;
  bool ok = true;
  {
    std::ifstream in(item.path, std::ios::binary);
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!in) {
      why = "cannot read " + pathToUtf8(item.path);
      ok = false;
    } else if (!out) {
      why = "cannot write " + pathToUtf8(part);
      ok = false;
    } else {
      std::vector<char> buf(kChunk);
      while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        const std::streamsize n = in.gcount();
        if (n <= 0) break;
        out.write(buf.data(), n);
        if (!out) {
          why = "cannot write " + pathToUtf8(part);
          ok = false;
          break;
        }
        crc = crcUpdate(crc, reinterpret_cast<const unsigned char*>(buf.data()), (std::size_t)n);
        got += (std::uint64_t)n;
      }
      if (ok && in.bad()) {
        why = "cannot read " + pathToUtf8(item.path);
        ok = false;
      }
      if (ok) {
        out.close();
        if (out.fail()) {
          why = "cannot write " + pathToUtf8(part);
          ok = false;
        }
      }
    }
  }
  if (ok && got != item.size) {
    why = pathToUtf8(item.path) + " changed while it was copied";
    ok = false;
  }
  if (!ok) {
    fs::remove(part, ec);
    return false;
  }
  if (!replaceFile(part, dest)) {
    fs::remove(part, ec);
    why = "cannot put " + pathToUtf8(dest) + " in place";
    return false;
  }
  const std::uintmax_t written = fs::file_size(dest, ec);
  if (ec || (std::uint64_t)written != got) {
    why = pathToUtf8(dest) + " was not written completely";
    return false;
  }
  item.crc = crc;
  return true;
}

bool writeManifest(const fs::path& copyDir, const fs::path& source, const std::vector<Item>& items, std::uint64_t total, std::string& why) {
  std::string text = std::string(kManifestHead) + "\n";
  text += "source=" + pathToUtf8(source) + "\n";
  for (auto& item : items) text += "file=" + std::to_string(item.size) + "\t" + hex8(item.crc) + "\t" + item.rel + "\n";
  text += "end=" + std::to_string(items.size()) + "\t" + std::to_string(total) + "\n";
  const fs::path part = copyDir / (std::string(kManifestName) + ".part");
  {
    std::ofstream o(part, std::ios::binary | std::ios::trunc);
    if (!o) {
      why = "cannot write " + pathToUtf8(part);
      return false;
    }
    o.write(text.data(), (std::streamsize)text.size());
    o.close();
    if (o.fail()) {
      why = "cannot write " + pathToUtf8(part);
      std::error_code ec;
      fs::remove(part, ec);
      return false;
    }
  }
  if (!replaceFile(part, copyDir / kManifestName)) {
    std::error_code ec;
    fs::remove(part, ec);
    why = "cannot put the manifest in place";
    return false;
  }
  return true;
}

void pruneCopy(const fs::path& copyDir, const std::set<std::string>& keep) {
  std::error_code ec;
  std::vector<fs::path> files, dirs;
  for (fs::recursive_directory_iterator walk(copyDir, ec), end; !ec && walk != end; walk.increment(ec)) {
    std::error_code e2;
    if (walk->is_directory(e2) && !walk->is_symlink(e2)) dirs.push_back(walk->path());
    else files.push_back(walk->path());
  }
  for (auto& f : files) {
    std::error_code e2;
    const std::string rel = genericUtf8(f.lexically_relative(copyDir));
    if (!keep.count(rel)) fs::remove(f, e2);
  }
  std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
  for (auto& d : dirs) {
    std::error_code e2;
    fs::remove(d, e2);
  }
}

bool pakStartsRight(const fs::path& pak) {
  std::ifstream f(pak, std::ios::binary);
  unsigned char h[8];
  if (!f.read(reinterpret_cast<char*>(h), 8)) return false;
  for (auto& b : h) b ^= 0xF7;
  const std::uint32_t magic = (std::uint32_t)h[0] | (std::uint32_t)h[1] << 8 | (std::uint32_t)h[2] << 16 | (std::uint32_t)h[3] << 24;
  const std::uint32_t version = (std::uint32_t)h[4] | (std::uint32_t)h[5] << 8 | (std::uint32_t)h[6] << 16 | (std::uint32_t)h[7] << 24;
  return magic == kPakMagic && version == 0;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t at = 0;
  while (at < text.size()) {
    std::size_t e = text.find('\n', at);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(at, e - at);
    while (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    at = e + 1;
  }
  return lines;
}

}

fs::path gameCopyFolder(const fs::path& dataDir, Game g) { return dataDir / "gamefiles" / gameTitle(g); }

bool sameFolder(const fs::path& a, const fs::path& b) {
  if (a.empty() || b.empty()) return false;
  std::error_code ec;
  if (fs::equivalent(a, b, ec)) return true;
  const fs::path ca = fs::weakly_canonical(a, ec);
  if (ec || ca.empty()) return false;
  const fs::path cb = fs::weakly_canonical(b, ec);
  return !ec && ca == cb;
}

bool foldersOverlap(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  const fs::path ca = fs::weakly_canonical(a, ec);
  if (ec || ca.empty()) return true;
  const fs::path cb = fs::weakly_canonical(b, ec);
  if (ec || cb.empty()) return true;
  for (fs::path p = ca; !p.empty(); p = p.parent_path()) {
    if (sameFolder(p, cb)) return true;
    if (p == p.parent_path()) break;
  }
  for (fs::path p = cb; !p.empty(); p = p.parent_path()) {
    if (sameFolder(p, ca)) return true;
    if (p == p.parent_path()) break;
  }
  return false;
}

void discardGameCopy(const fs::path& copyDir) {
  if (!pathHasNoLinks(copyDir)) return;
  std::error_code ec;
  fs::remove(copyDir / kManifestName, ec);
}

GameCopyCheck checkGameCopy(const fs::path& copyDir) {
  GameCopyCheck c;
  if (!copyHasNoLinks(copyDir)) {
    c.state = GameCopyCheck::State::Damaged;
    c.why = "the copy contains a link or cannot be read";
    return c;
  }
  std::error_code ec;
  const fs::path manifest = copyDir / kManifestName;
  if (!fs::is_regular_file(manifest, ec)) {
    if (fs::exists(copyDir, ec)) c.why = "there is no manifest";
    return c;
  }
  auto damaged = [&](const std::string& why) {
    c.state = GameCopyCheck::State::Damaged;
    c.why = why;
    return c;
  };
  const std::uintmax_t manifestSize = fs::file_size(manifest, ec);
  if (ec || manifestSize > kMaxManifestBytes) return damaged("the manifest cannot be read");
  std::string text;
  {
    std::ifstream f(manifest, std::ios::binary);
    if (!f) return damaged("the manifest cannot be read");
    text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  }
  const std::vector<std::string> lines = splitLines(text);
  if (lines.size() < 4 || lines[0] != kManifestHead) return damaged("the manifest is not one this version knows");
  if (lines[1].compare(0, 7, "source=") != 0) return damaged("the manifest has no source");
  c.source = lines[1].substr(7);
  struct Entry {
    std::string name;
    std::uint64_t size = 0;
  };
  std::vector<Entry> entries;
  std::uint64_t total = 0;
  bool ended = false;
  for (std::size_t i = 2; i < lines.size(); i++) {
    const std::string& line = lines[i];
    if (ended) return damaged("the manifest has lines after its end");
    if (line.compare(0, 5, "file=") == 0) {
      const std::size_t t1 = line.find('\t', 5);
      const std::size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
      Entry e;
      std::uint32_t crc = 0;
      if (t2 == std::string::npos || !parseNumber(line.substr(5, t1 - 5), e.size) || !parseHex(line.substr(t1 + 1, t2 - t1 - 1), crc)) return damaged("the manifest has a line it cannot read");
      e.name = line.substr(t2 + 1);
      if (!safeName(e.name)) return damaged("the manifest has a name it cannot use");
      total += e.size;
      entries.push_back(std::move(e));
    } else if (line.compare(0, 4, "end=") == 0) {
      const std::size_t t1 = line.find('\t', 4);
      std::uint64_t count = 0, sum = 0;
      if (t1 == std::string::npos || !parseNumber(line.substr(4, t1 - 4), count) || !parseNumber(line.substr(t1 + 1), sum)) return damaged("the manifest has a line it cannot read");
      if (count != entries.size() || sum != total) return damaged("the manifest does not add up");
      ended = true;
    } else {
      return damaged("the manifest has a line it cannot read");
    }
  }
  if (!ended) return damaged("the manifest is not complete");
  bool pak = false;
  for (auto& e : entries) {
    const fs::path file = copyDir / pathFromUtf8(e.name);
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec) return damaged(e.name + " is missing");
    if ((std::uint64_t)size != e.size) return damaged(e.name + " has " + std::to_string((std::uint64_t)size) + " bytes, not " + std::to_string(e.size));
    if (e.name == "main.pak") pak = true;
  }
  if (!pak) return damaged("main.pak is not in the manifest");
  if (!pakStartsRight(copyDir / "main.pak")) return damaged("main.pak does not start like a game file");
  c.state = GameCopyCheck::State::Intact;
  c.files = entries.size();
  c.bytes = total;
  return c;
}

GameCopyResult collectGameFiles(const fs::path& installDir, const fs::path& copyDir) {
  GameCopyResult r;
  if (foldersOverlap(installDir, copyDir)) {
    r.why = "the game folder and its copy overlap";
    return r;
  }
  if (!copyHasNoLinks(copyDir)) {
    r.why = "the copy contains a link or cannot be read";
    return r;
  }
  std::vector<Item> items;
  if (!listGameFiles(installDir, items, r.why)) return r;
  std::uint64_t total = 0;
  for (auto& item : items) {
    if (foldersOverlap(item.path, copyDir)) {
      r.why = "a game file and its copy overlap";
      return r;
    }
    total += item.size;
  }
  std::error_code ec;
  fs::create_directories(copyDir, ec);
  if (ec || !fs::is_directory(copyDir, ec)) {
    r.why = "cannot make the folder " + pathToUtf8(copyDir);
    return r;
  }
  const fs::space_info room = fs::space(copyDir, ec);
  if (!ec && room.available != static_cast<std::uintmax_t>(-1) && (std::uint64_t)room.available < total + kReserveBytes) {
    r.why = "there is no room: " + megabytes(total) + " are needed and " + megabytes((std::uint64_t)room.available) + " are free";
    return r;
  }
  const fs::path manifest = copyDir / kManifestName;
  fs::remove(manifest, ec);
  if (fs::exists(manifest, ec)) {
    r.why = "cannot update the copy in " + pathToUtf8(copyDir);
    return r;
  }
  std::set<std::string> keep;
  for (auto& item : items) {
    if (!copyOne(item, copyDir, r.why)) return r;
    keep.insert(item.rel);
  }
  pruneCopy(copyDir, keep);
  if (!writeManifest(copyDir, installDir, items, total, r.why)) return r;
  r.ok = true;
  r.files = items.size();
  r.bytes = total;
  return r;
}

}
