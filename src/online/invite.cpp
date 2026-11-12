#include "online/invite.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace pp::online {

namespace fs = std::filesystem;

static const char* kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

static std::string pctDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
      o.push_back((char)std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else if (s[i] == '+') o.push_back(' ');
    else o.push_back(s[i]);
  }
  return o;
}

static bool hostOk(const std::string& h) {
  if (h.empty() || h.size() > 253) return false;
  for (unsigned char c : h)
    if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':')) return false;
  return true;
}

bool isInviteLike(const std::string& s) { return lower(s).rfind("peggleport://", 0) == 0; }

bool parseInvite(const std::string& url0, Invite* out) {
  size_t b = url0.find_first_not_of(" \t\r\n\"'<");
  size_t e = url0.find_last_not_of(" \t\r\n\"'>");
  if (b == std::string::npos) return false;
  std::string url = url0.substr(b, e - b + 1);
  if (!isInviteLike(url)) return false;
  std::string rest = url.substr(13);
  size_t q = rest.find('?');
  if (q == std::string::npos) return false;
  std::string action = rest.substr(0, q);
  while (!action.empty() && action.back() == '/') action.pop_back();
  if (lower(action) != "join") return false;
  std::string lobby, code;
  std::string query = rest.substr(q + 1);
  size_t p = 0;
  while (p <= query.size()) {
    size_t amp = query.find('&', p);
    std::string kv = query.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    size_t eq = kv.find('=');
    if (eq != std::string::npos) {
      std::string k = lower(kv.substr(0, eq)), v = pctDecode(kv.substr(eq + 1));
      if (k == "lobby") lobby = v;
      else if (k == "code") code = v;
    }
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
  for (auto& c : code) c = (char)std::toupper((unsigned char)c);
  if (code.size() != 5) return false;
  for (char c : code)
    if (!std::strchr(kAlphabet, c)) return false;
  std::string host;
  int port = 7770;
  if (lobby.empty()) return false;
  if (lobby[0] == '[') {
    size_t rb = lobby.find(']');
    if (rb == std::string::npos) return false;
    host = lobby.substr(1, rb - 1);
    if (rb + 1 < lobby.size()) {
      if (lobby[rb + 1] != ':') return false;
      port = std::atoi(lobby.c_str() + rb + 2);
      if (lobby.size() == rb + 2) return false;
    }
  } else if (std::count(lobby.begin(), lobby.end(), ':') == 1) {
    size_t c = lobby.find(':');
    host = lobby.substr(0, c);
    if (c + 1 >= lobby.size()) return false;
    for (size_t i = c + 1; i < lobby.size(); i++)
      if (!std::isdigit((unsigned char)lobby[i])) return false;
    port = std::atoi(lobby.c_str() + c + 1);
  } else {
    host = lobby;
  }
  if (!hostOk(host) || port < 1 || port > 65535) return false;
  if (out) {
    out->host = host;
    out->port = port;
    out->code = code;
  }
  return true;
}

std::string makeInvite(const std::string& host, int port, const std::string& code) {
  std::string h = host.find(':') != std::string::npos ? "[" + host + "]" : host;
  return "peggleport://join?lobby=" + h + ":" + std::to_string(port) + "&code=" + code;
}

bool findInviteArg(const std::vector<std::string>& args, Invite* out) {
  for (auto& a : args)
    if (isInviteLike(a) && parseInvite(a, out)) return true;
  return false;
}

bool extractInvite(const std::string& text, Invite* out) {
  std::string low = lower(text);
  size_t from = 0;
  while (true) {
    size_t at = low.find("peggleport://", from);
    if (at == std::string::npos) return false;
    size_t end = text.find_first_of(" \t\r\n\"'<>", at);
    if (parseInvite(text.substr(at, end == std::string::npos ? std::string::npos : end - at), out)) return true;
    from = at + 1;
  }
}

#ifdef _WIN32
static std::string utf8Text(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
static std::wstring wideText(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
#endif

std::string selfExePath(const char* argv0) {
  std::error_code ec;
#ifdef _WIN32
  wchar_t b[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
  if (n > 0 && n < MAX_PATH * 2) return utf8Text(std::wstring(b, n));
#elif defined(__APPLE__)
  char b[4096];
  uint32_t n = sizeof b;
  if (_NSGetExecutablePath(b, &n) == 0) return fs::weakly_canonical(b, ec).string();
#else
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return p.string();
#endif
  return fs::absolute(argv0 ? argv0 : "peggleport", ec).string();
}

std::string registrationCommandLine(const std::string& exePath) { return "\"" + exePath + "\" \"%1\""; }

std::string macInfoPlistSnippet() {
  return "Add this to the Info.plist of the .app bundle that wraps the peggleport binary (the engine itself is not a bundle):\n"
         "  <key>CFBundleURLTypes</key>\n"
         "  <array>\n"
         "    <dict>\n"
         "      <key>CFBundleURLName</key><string>PegglePort invite</string>\n"
         "      <key>CFBundleURLSchemes</key>\n"
         "      <array><string>peggleport</string></array>\n"
         "    </dict>\n"
         "  </array>\n"
         "macOS then starts the app with the link as an Apple Event (SDL3 delivers it as SDL_EVENT_DROP_FILE / the first argument);\n"
         "run `/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f YourApp.app` once to register it.\n";
}

#ifdef _WIN32
bool registerUrlHandler(const std::string& exe, std::string* report) {
  HKEY k = nullptr;
  std::string cmd = registrationCommandLine(exe);
  auto set = [&](const char* path, const char* name, const std::string& v) {
    HKEY h;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    const std::wstring wn = name ? wideText(name) : std::wstring();
    const std::wstring wv = wideText(v);
    LONG r = RegSetValueExW(h, name ? wn.c_str() : nullptr, 0, REG_SZ, (const BYTE*)wv.c_str(), (DWORD)((wv.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
  };
  (void)k;
  bool ok = set("Software\\Classes\\peggleport", nullptr, "URL:PegglePort invite") && set("Software\\Classes\\peggleport", "URL Protocol", "") &&
            set("Software\\Classes\\peggleport\\shell\\open\\command", nullptr, cmd);
  if (report) *report = ok ? "registered peggleport:// for the current user (HKCU\\Software\\Classes\\peggleport)\n  " + cmd : "could not write the registry";
  return ok;
}
bool unregisterUrlHandler(std::string* report) {
  LONG r = RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\peggleport");
  if (report) *report = r == ERROR_SUCCESS ? "removed the peggleport:// registration" : "nothing to remove";
  return r == ERROR_SUCCESS;
}
#elif defined(__APPLE__)
bool registerUrlHandler(const std::string&, std::string* report) {
  if (report) *report = macInfoPlistSnippet();
  return false;
}
bool unregisterUrlHandler(std::string* report) {
  if (report) *report = macInfoPlistSnippet();
  return false;
}
#else
static fs::path desktopFile() {
  const char* xdg = std::getenv("XDG_DATA_HOME");
  const char* home = std::getenv("HOME");
  fs::path base = xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : ".") / ".local" / "share";
  return base / "applications" / "peggleport-url.desktop";
}
bool registerUrlHandler(const std::string& exe, std::string* report) {
  fs::path f = desktopFile();
  std::error_code ec;
  fs::create_directories(f.parent_path(), ec);
  std::ofstream o(f);
  if (!o) { if (report) *report = "cannot write " + f.string(); return false; }
  o << "[Desktop Entry]\nType=Application\nName=PegglePort (invite handler)\nExec=\"" << exe << "\" %u\nNoDisplay=true\nTerminal=false\n"
    << "MimeType=x-scheme-handler/peggleport;\nCategories=Game;\n";
  o.close();
  int a = std::system(("xdg-mime default peggleport-url.desktop x-scheme-handler/peggleport >/dev/null 2>&1"));
  std::system(("update-desktop-database \"" + f.parent_path().string() + "\" >/dev/null 2>&1").c_str());
  if (report) *report = "wrote " + f.string() + (a == 0 ? " and set it as the peggleport:// handler (xdg-mime)" : "; xdg-mime was not available, run: xdg-mime default peggleport-url.desktop x-scheme-handler/peggleport");
  return true;
}
bool unregisterUrlHandler(std::string* report) {
  std::error_code ec;
  bool ok = fs::remove(desktopFile(), ec);
  if (report) *report = ok ? "removed " + desktopFile().string() : "nothing to remove";
  return ok;
}
#endif

}
