#include "mods/scanner.h"
#include "gamepaths.h"
#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <cstdint>
#include <string>
#include <string_view>
#include <filesystem>

namespace fs = std::filesystem;
namespace pp::mods {

const char* kScanDisclaimer =
    "This scan is best effort. It looks for network API use, URLs and library imports in the files of the mod, but it cannot prove "
    "a mod never goes online: a determined author can hide network use in the code (packed or encrypted code, names built at run "
    "time). Lua mods without the net permission run in a sandbox that has no sockets at all, and native plugins run in a separate "
    "restricted helper process (the permission dialog says what your system can enforce). Only enable mods you trust.";

bool ScanReport::usesNet() const {
  if (declaredNet) return true;
  for (auto& f : findings) if (f.isNet()) return true;
  return false;
}
std::string ScanReport::summary() const {
  int net = 0, dyn = 0;
  for (auto& f : findings) { if (f.isNet()) net++; else if (f.kind == Finding::Dynamic) dyn++; }
  std::string s;
  if (usesNet()) s = "uses the network (" + std::to_string(net) + " finding" + (net == 1 ? "" : "s") + (declaredNet ? ", permission declared" : "") + ")";
  else s = "no network use found";
  if (hasNative) s += "; native code";
  if (dyn) s += "; dynamic code";
  return s;
}
std::string Finding::describe() const {
  static const char* tag[] = {"NET", "URL", "NET?", "NET", "DYN", "NATIVE", "FS", "INFO"};
  std::string o = std::string("[") + tag[(int)kind] + "] " + file;
  if (line) o += ":" + std::to_string(line);
  o += "  " + what;
  return o;
}
std::string ScanReport::text() const {
  std::string o = "scan: " + summary() + " (" + std::to_string(filesScanned) + " file(s))\n";
  for (auto& f : findings) o += "  " + f.describe() + "\n";
  o += std::string("note: ") + kScanDisclaimer + "\n";
  return o;
}

namespace {
bool isId(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
std::string lowerS(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
void add(ScanReport& r, Finding::Kind k, const std::string& file, int line, const std::string& what) {
  for (auto& f : r.findings) if (f.kind == k && f.file == file && f.line == line && f.what == what) return;
  if (r.findings.size() < 400) r.findings.push_back({k, file, line, what});
}

std::string normaliseLua(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      if (std::isdigit((unsigned char)s[i + 1])) {
        int v = 0, k = 0;
        size_t j = i + 1;
        while (j < s.size() && k < 3 && std::isdigit((unsigned char)s[j])) { v = v * 10 + (s[j] - '0'); j++; k++; }
        if (v > 31 && v < 127) { o += (char)v; i = j - 1; continue; }
      } else if (s[i + 1] == 'x' && i + 3 < s.size() && std::isxdigit((unsigned char)s[i + 2]) && std::isxdigit((unsigned char)s[i + 3])) {
        int v = (int)std::strtol(std::string(s.substr(i + 2, 2)).c_str(), nullptr, 16);
        if (v > 31 && v < 127) { o += (char)v; i += 3; continue; }
      }
    }
    o += s[i];
  }
  std::string f;
  for (size_t i = 0; i < o.size(); i++) {
    if ((o[i] == '"' || o[i] == '\'')) {
      char q = o[i];
      size_t j = i + 1;
      while (j < o.size() && (o[j] == ' ' || o[j] == '\t')) j++;
      if (j + 1 < o.size() && o[j] == '.' && o[j + 1] == '.') {
        size_t k = j + 2;
        while (k < o.size() && (o[k] == ' ' || o[k] == '\t')) k++;
        if (k < o.size() && o[k] == q && !f.empty() && f.back() != '\\') { i = k; continue; }
      }
    }
    f += o[i];
  }
  return f;
}

void findUrls(const std::string& file, int line, std::string_view text, ScanReport& r) {
  size_t p = 0;
  while ((p = text.find("://", p)) != std::string_view::npos) {
    size_t a = p;
    while (a > 0 && (std::isalnum((unsigned char)text[a - 1]) || text[a - 1] == '+' || text[a - 1] == '.' || text[a - 1] == '-')) a--;
    size_t b = p + 3;
    while (b < text.size() && !std::isspace((unsigned char)text[b]) && text[b] != '"' && text[b] != '\'' && text[b] != ')' && text[b] != '<' && text[b] != '\0' && std::isprint((unsigned char)text[b])) b++;
    std::string scheme = lowerS(std::string(text.substr(a, p - a)));
    if (!scheme.empty() && b > p + 3 && scheme != "file" && scheme.size() <= 10) add(r, Finding::Url, file, line, "URL " + std::string(text.substr(a, std::min<size_t>(b - a, 100))) + (b - a > 100 ? "..." : ""));
    p += 3;
  }
}
}

void scanLuaSource(const std::string& file, std::string_view srcIn, ScanReport& r) {
  std::string src = normaliseLua(srcIn);
  r.filesScanned++;
  int line = 1;
  size_t ls = 0;
  auto handle = [&](std::string_view L) {
    std::string low = lowerS(std::string(L));
    findUrls(file, line, L, r);
    for (size_t p = low.find("net"); p != std::string::npos; p = low.find("net", p + 1)) {
      bool lhs = p == 0 || !(isId(low[p - 1]) || low[p - 1] == '.');
      size_t q = p + 3;
      if (!lhs) continue;
      while (q < low.size() && low[q] == ' ') q++;
      if (q < low.size() && (low[q] == '.' || low[q] == '[' || low[q] == ':' || low[q] == '=' || low[q] == ',' || low[q] == ')' || low[q] == '\0')) {
        if (q < low.size() && low[q] == '=' && q + 1 < low.size() && low[q + 1] == '=') continue;
        add(r, Finding::NetApi, file, line, "use of the `net` API: " + std::string(L.substr(p, std::min<size_t>(40, L.size() - p))) + (L.size() - p > 40 ? "..." : ""));
        break;
      }
    }
    for (const char* w : {"socket", "tcp", "udp", "winsock", "curl", "wininet", "winhttp", "getaddrinfo", "gethostby"})
      if (low.find(w) != std::string::npos) { add(r, Finding::SocketWord, file, line, std::string("mentions '") + w + "'"); break; }
    if (low.find("http") != std::string::npos && low.find("://") == std::string::npos) add(r, Finding::SocketWord, file, line, "mentions 'http'");
    for (const char* w : {"loadstring", "loadfile", "dofile", "string.dump", "getfenv", "setfenv", "debug.", "package.", "io.", "os.execute", "os.getenv", "ffi"})
      if (low.find(w) != std::string::npos) { add(r, Finding::Dynamic, file, line, std::string("uses '") + w + "' (runtime-built code can hide anything)"); break; }
    if (low.find("load(") != std::string::npos && (low.find("load(") == 0 || !isId(low[low.find("load(") - 1])))
      add(r, Finding::Dynamic, file, line, "uses load(...) (runtime-built code can hide anything)");
    if (low.find("_g[") != std::string::npos || low.find("_env[") != std::string::npos || low.find("getmetatable(_g") != std::string::npos)
      add(r, Finding::Dynamic, file, line, "dynamic global access (_G[...] / _ENV[...])");
    if (low.find("require") != std::string::npos && low.find("require") < low.size() && (low.find("require(") != std::string::npos || low.find("require ") != std::string::npos))
      add(r, Finding::Info, file, line, "require of another mod file");
  };
  for (size_t i = 0; i <= src.size(); i++) {
    if (i == src.size() || src[i] == '\n') {
      handle(std::string_view(src).substr(ls, i - ls));
      line++;
      ls = i + 1;
    }
  }
}

namespace {
const char* kNetLibs[] = {"ws2_32", "wsock32", "winhttp", "wininet", "urlmon", "libcurl", "libssl", "libnghttp2", "cfnetwork", "sdl3_net", "sdl2_net", "libsoup", "enet", "mswsock", "iphlpapi", "dnsapi", "websocket"};
const char* kNetFns[] = {"socket", "connect", "getaddrinfo", "gethostbyname", "gethostbyaddr", "WSAStartup", "WSASocketA", "WSASocketW", "WSAConnect", "InternetOpenA", "InternetOpenW", "InternetOpenUrlA", "InternetConnectA",
                         "WinHttpOpen", "WinHttpConnect", "URLDownloadToFileA", "URLDownloadToFileW", "curl_easy_init", "curl_easy_perform", "curl_global_init", "curl_easy_setopt", "NSURLSession", "NSURLConnection",
                         "CFSocketCreate", "CFStreamCreatePairWithSocketToHost", "CFReadStreamCreateForHTTPRequest", "SDLNet_Init", "enet_host_create", "enet_host_connect", "nw_connection_create", "nw_connection_start",
                         "nw_endpoint_create_host", "sendto", "recvfrom", "inet_pton", "inet_addr", "freeaddrinfo", "gethostname", "SSL_connect", "SSL_new", "SSL_CTX_new", "BIO_new_connect"};
}

void scanNativeBytes(const std::string& file, std::string_view b, ScanReport& r) {
  r.filesScanned++;
  r.hasNative = true;
  const char* fmt = "unknown binary";
  if (b.size() > 4 && b[0] == 'M' && b[1] == 'Z') fmt = "PE (Windows)";
  else if (b.size() > 4 && b.substr(0, 4) == std::string_view("\x7f""ELF", 4)) fmt = "ELF (Linux)";
  else if (b.size() > 4) {
    uint32_t m;
    std::memcpy(&m, b.data(), 4);
    if (m == 0xfeedfaceu || m == 0xfeedfacfu || m == 0xcefaedfeu || m == 0xcffaedfeu || m == 0xcafebabeu || m == 0xbebafecau) fmt = "Mach-O (macOS)";
  }
  add(r, Finding::Native, file, 0, std::string("native code, format: ") + fmt);
  std::set<std::string> strs;
  std::string cur;
  auto flush = [&] {
    if (cur.size() >= 4 && cur.size() < 512) strs.insert(cur);
    cur.clear();
  };
  for (unsigned char c : b) {
    if (c >= 32 && c < 127) cur += (char)c;
    else flush();
  }
  flush();
  std::set<std::string> lowStrs;
  for (auto& s : strs) lowStrs.insert(lowerS(s));
  for (auto& s : strs) findUrls(file, 0, s, r);
  for (const char* l : kNetLibs)
    for (auto& s : lowStrs) {
      size_t p = s.find(l);
      if (p != std::string::npos && (p == 0 || !isId(s[p - 1]) ) && s.size() < 64) { add(r, Finding::NativeNet, file, 0, std::string("references network library '") + s + "'"); break; }
    }
  for (const char* fn : kNetFns) {
    std::string f = fn, us = "_" + f;
    for (auto& s : strs) {
      bool hit = s == f || s == us || (f.size() >= 6 && s.size() > f.size() && s.size() <= f.size() + 2 && s.compare(s.size() - f.size(), f.size(), f) == 0);
      if (hit) { add(r, Finding::NativeNet, file, 0, "imports/uses network function '" + f + "'"); break; }
    }
  }
}

ScanReport scanMod(const fs::path& dir, const Manifest& m) {
  ScanReport r;
  r.declaredNet = m.net;
  if (m.net) add(r, Finding::NetApi, "mod manifest", 0, "declares permissions = net (gets the `net` API)");
  if (m.fs) add(r, Finding::Fs, "mod manifest", 0, "declares permissions = fs (private data folder)");
  std::error_code ec;
  std::set<std::string> done;
  auto scanFile = [&](const fs::path& p) {
    std::string rel = genericUtf8(p.lexically_relative(dir));
    if (!done.insert(rel).second) return;
    std::string bytes;
    if (!readFile(p, bytes, 128u << 20)) return;
    std::string ext = lowerS(pathToUtf8(p.extension()));
    if (ext == ".lua") scanLuaSource(rel, bytes, r);
    else if (ext == ".dll" || ext == ".so" || ext == ".dylib" || ext == ".exe" || ext == ".node" || (bytes.size() > 4 && (bytes.substr(0, 2) == "MZ" || bytes.substr(0, 4) == std::string("\x7f""ELF", 4)))) scanNativeBytes(rel, bytes, r);
  };
  for (auto& s : m.scripts) scanFile(dir / s);
  fs::path nat = resolveNative(dir, m);
  if (!m.native.empty()) {
    if (nat.empty()) add(r, Finding::Info, "mod manifest", 0, "native = " + m.native + " but no library for this platform was found");
    else scanFile(nat);
    r.hasNative = true;
  }
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    std::string nm = pathToUtf8(it->path().filename());
    if (!nm.empty() && nm[0] == '.') { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
    if (!it->is_regular_file(ec)) continue;
    std::string ext = lowerS(pathToUtf8(it->path().extension()));
    if (ext == ".lua" || ext == ".dll" || ext == ".so" || ext == ".dylib" || ext == ".exe" || ext == ".node") {
      bool declared = done.count(genericUtf8(it->path().lexically_relative(dir))) > 0;
      scanFile(it->path());
      if (!declared && ext != ".lua") add(r, Finding::Info, genericUtf8(it->path().lexically_relative(dir)), 0, "native library present but not declared in the manifest (it will not be loaded)");
    }
  }
  return r;
}

}
