#include "online/invite.h"
#include "online/endpoint.h"

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
#include <shlwapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include "online/invite_desktop.h"
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
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

static std::string pctEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') out.push_back((char)c);
    else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
  }
  return out;
}

static const std::string kDiscordScheme = "discord-1571022918392225882://";

static bool discordInvite(const std::string& url, Invite* out);

bool isInviteLike(const std::string& s) {
  const std::string value = lower(s);
  return value.rfind("peggleport://", 0) == 0 || (value.rfind(kDiscordScheme, 0) == 0 && discordInvite(s, nullptr));
}

static bool discordInvite(const std::string& url, Invite* out) {
  if (url.size() > 2048 || lower(url).rfind(kDiscordScheme + "join?", 0) != 0) return false;
  for (size_t i = 0; i < url.size(); ++i) {
    const unsigned char c = url[i];
    if (c <= 32 || c >= 127 || c == '#' || c == '\x22' || c == '\x27' || c == '<' || c == '>' || c == '\\') return false;
    if (c == '%') {
      if (i + 2 >= url.size() || !std::isxdigit((unsigned char)url[i + 1]) || !std::isxdigit((unsigned char)url[i + 2])) return false;
      const int decoded = std::stoi(url.substr(i + 1, 2), nullptr, 16);
      if (decoded <= 32 || decoded >= 127 || decoded == '#' || decoded == 34 || decoded == 39 || decoded == '<' || decoded == '>' || decoded == '\\' || decoded == '%') return false;
      i += 2;
    }
  }
  const std::string query = url.substr(kDiscordScheme.size() + 5);
  bool lobby = false, code = false;
  for (size_t p = 0; p <= query.size();) {
    const size_t amp = query.find('&', p);
    const std::string field = query.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    const size_t eq = field.find('=');
    if (eq == std::string::npos || eq + 1 == field.size() || field.find('=', eq + 1) != std::string::npos) return false;
    const std::string key = field.substr(0, eq);
    if (key == "lobby" && !lobby) lobby = true;
    else if (key == "code" && !code) code = true;
    else return false;
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
  return lobby && code && parseInvite("peggleport://join?" + query, out);
}

bool parseInvite(const std::string& url0, Invite* out) {
  if (url0.size() > 4096) return false;
  for (unsigned char c : url0) if (c < 32 || c == 127) return false;
  if (lower(url0).rfind(kDiscordScheme, 0) == 0) return discordInvite(url0, out);
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
      if (k == "lobby") { if (!lobby.empty()) return false; lobby = v; }
      else if (k == "code") { if (!code.empty()) return false; code = v; }
    }
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
  for (auto& c : code) c = (char)std::toupper((unsigned char)c);
  if (code.size() != 5) return false;
  for (char c : code)
    if (!std::strchr(kAlphabet, c)) return false;
  LobbyEndpoint ep;
  if (!parseEndpoint(lobby, &ep)) return false;
  if (out) {
    out->host = ep.secure ? ep.address() : ep.host;
    out->port = ep.port;
    out->code = code;
  }
  return true;
}

std::string makeInvite(const std::string& host, int port, const std::string& code) {
  const std::string address = endpointAddress(host, port);
  if (address.empty()) return {};
  const bool escaped = address.rfind("wss://", 0) == 0 || address.find('%') != std::string::npos;
  return "peggleport://join?lobby=" + (escaped ? pctEncode(address) : address) + "&code=" + code;
}

bool findInviteArg(const std::vector<std::string>& args, Invite* out) {
  for (auto& a : args)
    if (isInviteLike(a) && parseInvite(a, out)) return true;
  return false;
}

bool extractInvite(const std::string& text, Invite* out) {
  std::string low = lower(text);
  if (low.rfind(kDiscordScheme, 0) == 0) return discordInvite(text, out);
  size_t from = 0;
  while (true) {
    size_t at = low.find("peggleport://", from);
    const size_t discord = low.find(kDiscordScheme, from);
    if (discord != std::string::npos && (at == std::string::npos || discord < at)) at = discord;
    if (at == std::string::npos) return false;
    size_t end = text.find_first_of(discord == at ? " " : " \t\r\n\"'<>", at);
    if (parseInvite(text.substr(at, end == std::string::npos ? std::string::npos : end - at), out)) return true;
    from = discord == at ? (end == std::string::npos ? text.size() : end) : at + 1;
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
  return argv0 && *argv0 ? fs::absolute(argv0, ec).string() : std::string();
}

std::string registrationCommandLine(const std::string& exePath) { return "\"" + exePath + "\" \"%1\""; }

std::string macInfoPlistSnippet() {
  return "Add this to the Info.plist of the .app bundle that wraps the peggleport binary (the engine itself is not a bundle):\n"
         "  <key>CFBundleURLTypes</key>\n"
         "  <array>\n"
         "    <dict>\n"
         "      <key>CFBundleURLName</key><string>PegglePort invite</string>\n"
         "      <key>CFBundleURLSchemes</key>\n"
         "      <array><string>peggleport</string><string>discord-1571022918392225882</string></array>\n"
         "    </dict>\n"
         "  </array>\n"
         "macOS then starts the app with the link as an Apple Event (SDL3 delivers it as SDL_EVENT_DROP_FILE / the first argument);\n"
         "run `/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f YourApp.app` once to register it.\n";
}

#ifdef _WIN32
static const std::string inviteSchemes[] = {"peggleport", "discord-1571022918392225882"};

bool urlHandlerRegistered(const std::string& exe, const std::atomic<bool>*) {
  if (exe.empty()) return false;
  const std::wstring expected = wideText(registrationCommandLine(exe));
  std::vector<wchar_t> value(expected.size() + 1);
  for (const auto& scheme : inviteSchemes) {
    const auto protocol = wideText(scheme);
    if (RegGetValueW(HKEY_CLASSES_ROOT, protocol.c_str(), L"URL Protocol", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) != ERROR_SUCCESS) return false;
    DWORD chars = static_cast<DWORD>(value.size());
    if (AssocQueryStringW(ASSOCF_IS_PROTOCOL | ASSOCF_NOTRUNCATE | ASSOCF_NOFIXUPS, ASSOCSTR_COMMAND, protocol.c_str(), nullptr,
                          value.data(), &chars) != S_OK ||
        CompareStringOrdinal(value.data(), -1, expected.c_str(), -1, TRUE) != CSTR_EQUAL) return false;
  }
  return true;
}

bool registerUrlHandler(const std::string& exe, std::string* report, const std::atomic<bool>*) {
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
  bool ok = true;
  for (const auto& scheme : inviteSchemes) {
    const std::string path = "Software\\Classes\\" + scheme;
    const bool written = set(path.c_str(), nullptr, "URL:PegglePort invite") && set(path.c_str(), "URL Protocol", "") && set((path + "\\shell\\open\\command").c_str(), nullptr, cmd);
    ok = written && ok;
  }
  if (report) *report = ok ? "registered PegglePort and Discord invite links for the current user\n  " + cmd : "could not write the registry";
  return ok;
}
bool unregisterUrlHandler(std::string* report) {
  LONG r = RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\peggleport");
  LONG discord = RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\discord-1571022918392225882");
  if (r == ERROR_FILE_NOT_FOUND || (r == ERROR_SUCCESS && discord != ERROR_SUCCESS && discord != ERROR_FILE_NOT_FOUND)) r = discord;
  if (report) *report = r == ERROR_SUCCESS ? "removed the invite link registrations" : "nothing to remove";
  return r == ERROR_SUCCESS;
}
#elif defined(__APPLE__)
bool urlHandlerRegistered(const std::string&, const std::atomic<bool>*) { return false; }
bool registerUrlHandler(const std::string&, std::string* report, const std::atomic<bool>*) {
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
  if (xdg && fs::path(xdg).is_absolute()) return fs::path(xdg) / "applications" / "peggleport-url.desktop";
  if (home && fs::path(home).is_absolute()) return fs::path(home) / ".local/share/applications/peggleport-url.desktop";
  return {};
}
using OwnerClock = std::chrono::steady_clock;

static bool desktopTool(const char* const args[], bool checkOwner, OwnerClock::time_point deadline, const std::atomic<bool>* stop) {
  if ((stop && *stop) || OwnerClock::now() >= deadline) return false;
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) != 0) return false;
  posix_spawn_file_actions_t io;
  posix_spawnattr_t attr;
  pid_t pid = -1;
  if (posix_spawn_file_actions_init(&io) == 0) {
    if (posix_spawnattr_init(&attr) == 0) {
      if (posix_spawn_file_actions_adddup2(&io, pipefd[1], STDOUT_FILENO) == 0 &&
          posix_spawn_file_actions_addopen(&io, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
          posix_spawnattr_setpgroup(&attr, 0) == 0 && posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP) == 0) {
        if (posix_spawnp(&pid, args[0], &io, &attr, const_cast<char* const*>(args), environ) != 0) pid = -1;
      }
      posix_spawnattr_destroy(&attr);
    }
    posix_spawn_file_actions_destroy(&io);
  }
  close(pipefd[1]);
  bool ok = false;
  if (pid > 0) {
    int status = 0;
    for (;;) {
      if ((stop && *stop) || OwnerClock::now() >= deadline) {
        kill(-pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        break;
      }
      const pid_t result = waitpid(pid, &status, WNOHANG);
      if (result == pid) {
        constexpr char expected[] = "peggleport-url.desktop\n";
        char reply[sizeof expected];
        ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (ok && checkOwner) ok = read(pipefd[0], reply, sizeof reply) == sizeof expected - 1 &&
                                   std::memcmp(reply, expected, sizeof expected - 1) == 0 && read(pipefd[0], reply, 1) == 0;
        break;
      }
      if (result < 0 && errno != EINTR) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  close(pipefd[0]);
  return ok;
}

bool urlHandlerRegistered(const std::string& exe, const std::atomic<bool>* stop) {
  if (exe.empty() || (stop && *stop)) return false;
  const auto deadline = OwnerClock::now() + std::chrono::seconds(2);
  int fd = open(desktopFile().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat info{};
  std::string entry;
  if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 && info.st_size <= 65536) {
    entry.resize(static_cast<size_t>(info.st_size) + 1);
    if (read(fd, entry.data(), entry.size()) != info.st_size) entry.clear();
    else entry.pop_back();
  }
  close(fd);
  std::istringstream desktop(entry);
  return desktopOwnsInvites(desktop, exe, [&](const char* mime) {
    const char* args[] = {"xdg-mime", "query", "default", mime, nullptr};
    return desktopTool(args, true, deadline, stop);
  });
}
bool registerUrlHandler(const std::string& exe, std::string* report, const std::atomic<bool>* stop) {
  fs::path f = desktopFile();
  std::error_code ec;
  fs::create_directories(f.parent_path(), ec);
  std::ofstream o(f);
  if (!o) { if (report) *report = "cannot write " + f.string(); return false; }
  o << "[Desktop Entry]\nType=Application\nName=PegglePort (invite handler)\nExec=\"" << exe << "\" %u\nNoDisplay=true\nTerminal=false\n"
    << "MimeType=x-scheme-handler/peggleport;x-scheme-handler/discord-1571022918392225882;\nCategories=Game;\n";
  o.close();
  if (!o) { if (report) *report = "cannot finish writing " + f.string(); return false; }
  const auto deadline = OwnerClock::now() + std::chrono::seconds(2);
  const char* setDefault[] = {"xdg-mime", "default", "peggleport-url.desktop", "x-scheme-handler/peggleport", "x-scheme-handler/discord-1571022918392225882", nullptr};
  const bool set = desktopTool(setDefault, false, deadline, stop);
  const std::string dir = f.parent_path().string();
  const char* refresh[] = {"update-desktop-database", dir.c_str(), nullptr};
  desktopTool(refresh, false, deadline, stop);
  if (report) *report = "wrote " + f.string() + (set ? " and requested both invite defaults" : "; xdg-mime did not set the defaults. Run: xdg-mime default peggleport-url.desktop x-scheme-handler/peggleport x-scheme-handler/discord-1571022918392225882");
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
