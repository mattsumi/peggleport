#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include "online/discovery.h"
#include "online/invite_desktop.h"
#include "online/invite.h"
#include "online/online_ui.h"
#include "online/steam.h"
#include "ui/screens.h"
#include "ui/widescreen.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
static std::wstring protocolCommand, discordCommand;
static bool discordMarker;
static int otherChoice = -1, unknownChoice = -1, sameOwnerChoice = -1;
static HRESULT fixtureAssociation(ASSOCF flags, ASSOCSTR str, LPCWSTR scheme, LPCWSTR verb, LPWSTR out, DWORD* size) {
  if (!(flags & ASSOCF_IS_PROTOCOL) || (flags & ASSOCF_NOUSERSETTINGS) || str != ASSOCSTR_COMMAND || verb) return E_INVALIDARG;
  const int i = std::wstring(scheme) == L"peggleport" ? 0 : std::wstring(scheme) == L"discord-1571022918392225882" ? 1 : -1;
  if (i < 0 || i == unknownChoice) return E_FAIL;
  const std::wstring command = i == otherChoice ? LR"("C:\fixture\Other.exe" "%1")" : i == sameOwnerChoice ?
                               LR"("C:\fixture\PegglePort.exe" "%1")" : i ? discordCommand : protocolCommand;
  if (!out || command.empty() || command.size() >= *size) return E_FAIL;
  *size = static_cast<DWORD>(command.size() + 1);
  std::copy_n(command.c_str(), *size, out);
  return S_OK;
}
static LSTATUS fixtureRegistry(HKEY root, LPCWSTR path, LPCWSTR name, DWORD flags, DWORD*, void*, DWORD*) {
  if (root != HKEY_CLASSES_ROOT || flags != RRF_RT_REG_SZ) return ERROR_ACCESS_DENIED;
  for (bool discord : {false, true}) {
    std::wstring key = discord ? L"discord-1571022918392225882" : L"peggleport";
    const auto& command = discord ? discordCommand : protocolCommand;
    if (command.empty()) continue;
    if (path == key && name && std::wstring(name) == L"URL Protocol" && (!discord || discordMarker)) return ERROR_SUCCESS;
  }
  return ERROR_FILE_NOT_FOUND;
}
#define AssocQueryStringW fixtureAssociation
#define RegGetValueW fixtureRegistry
#include "online/invite.cpp"
#undef RegGetValueW
#undef AssocQueryStringW
#else
#include "online/invite.cpp"
#endif

namespace pp {
static bool reachable;
static int registration, attempts;
struct FixtureSession : Session {
  static bool listRooms(const std::string&, int, std::vector<std::string>*, std::string* err, const std::atomic<bool>*) {
    *err = reachable ? "" : "TLS fixture";
    return reachable;
  }
};
namespace online {
static std::string fixtureUdp(const std::string&, int port, bool* ok, LobbyProbe* probe) {
  *ok = true; *probe = {true, true, true, true, true, port}; return {};
}
static bool desktopMode = true;
static std::string entry, ids[2];
static bool queryOK[2];
static std::atomic<bool> delayNext{}, entered{}, releaseQuery{}, returned{}, sawStop{};
static std::string fixtureExe(const char*) {
  return desktopMode ? "/opt/fixture/peggleport" : "C:\\fixture\\PegglePort.exe";
}
static void seedRegistration(int state) {
  if (desktopMode) {
    entry = state ? "[Desktop Entry]\nType=Application\nExec=\"" + fixtureExe(nullptr) + "\" %u\n" : "";
    ids[0] = ids[1] = "peggleport-url.desktop";
    queryOK[0] = queryOK[1] = true;
    if (state == 1) queryOK[1] = false;
    if (state == 2) ids[1] = "other.desktop";
    return;
  }
#ifdef _WIN32
  protocolCommand = state ? LR"("C:\fixture\PegglePort.exe" "%1")" : L"";
  discordCommand = state == 2 ? LR"("C:\fixture\OtherGames.exe" "%1")" : protocolCommand;
  discordMarker = state != 1;
#endif
}
static bool fixtureInstalled(const std::string& exe, const std::atomic<bool>* stop) {
#ifdef _WIN32
  if (!desktopMode) return urlHandlerRegistered(exe, stop);
#endif
  std::istringstream file(entry);
  const bool owns = desktopOwnsInvites(file, exe, [](const std::string& mime) {
    int i = mime == "x-scheme-handler/peggleport" ? 0 : mime == "x-scheme-handler/discord-1571022918392225882" ? 1 : -1;
    return i >= 0 && queryOK[i] && ids[i] == "peggleport-url.desktop";
  });
  if (delayNext.exchange(false)) {
    entered = true;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!releaseQuery && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sawStop = stop && *stop;
    returned = true;
  }
  return owns;
}
static bool fixtureRegister(const std::string&, std::string* report, const std::atomic<bool>*) {
  ++attempts;
  seedRegistration(registration);
  *report = registration == 1 ? "xdg-mime did not set the defaults" : "fixture";
  return registration != 0;
}
}
}

#define Session FixtureSession
#define registerUrlHandler fixtureRegister
#define probeLobbyUdp fixtureUdp
#define selfExePath fixtureExe
#define urlHandlerRegistered fixtureInstalled
#define private public
#include "online/online_ui.cpp"
#undef private
#undef urlHandlerRegistered
#undef selfExePath
#undef probeLobbyUdp
#undef registerUrlHandler
#undef Session

using namespace pp;
using namespace pp::ui;
using namespace pp::ui::online;
namespace fs = std::filesystem;

static int failures;
static void check(bool ok, const char* what) {
  if (!ok) { ++failures; std::cerr << "FAIL " << what << '\n'; }
}

struct ScreenFixture : OnlineScreen {
  explicit ScreenFixture(App& app) : OnlineScreen(app, "", false, Flow::Join) {}
  int toggles = 0;
  void checkboxChanged(int id, bool) override { if (id == C_Discord) ++toggles; }
  void click(float x, float y) {
    Input in; in.x = x; in.y = y;
    for (auto type : {Input::MouseMove, Input::MouseDown, Input::MouseUp}) { in.type = type; input(in); }
  }
  void click(Widget& w) { click(w.x + w.w / 2, w.y + w.h / 2); }
  void settleOwner() {
    if (ownerCheck_) { ownerCheck_->th.join(); update(); }
  }
  void finish() {
    if (!job_ || !job_->th.joinable()) throw std::runtime_error("missing fixture job");
    job_->th.join(); update();
  }
};

static void exercise(App& app) {
  pp::online::seedRegistration(0); attempts = 0;
  Rect expectedRow{};
  const int openingWidth = app.gfx.canvasW();
  auto open = [&](bool settle = true) {
    app.cfg.noSave = false;
    auto s = std::make_unique<ScreenFixture>(app);
    app.cfg.noSave = true;
    if (settle) s->settleOwner();
    if (settle && expectedRow.w && app.gfx.canvasW() == openingWidth && !s->bRegister_->visible) {
      const auto& c = *s->discordCheck_;
      check(c.x == expectedRow.x && c.y == expectedRow.y && c.w == expectedRow.w, "reopened hit target ready before first draw");
    }
    s->draw(app.gfx);
    return s;
  };
  auto s = open();
  auto& reg = *s->bRegister_; auto& box = *s->discordCheck_;
  const Rect row = expectedRow = reg.rect(), old = box.rect();
  s->configuredLobby_ = "wss://fixture.invalid/lobby";
  for (bool ok : {false, true}) {
    reachable = ok;
    s->click(*s->bTest_); s->finish();
    check(ok ? s->status_ == "Secure lobby connection is working." && s->statusKind_ == 2 :
               s->status_.find("secure connection") != std::string::npos && s->statusKind_ == 1, "secure connection report");
    s->draw(app.gfx);
  }
  if (!kUrlRegistration) { check(!reg.visible, "unsupported platform"); return; }
  for (registration = 0; registration < 3; ++registration) {
    s->click(reg); s->finish();
    check(reg.visible && reg.enabled && box.x == old.x && box.y == old.y && attempts == registration + 1, "failure remains retryable");
    check(s->statusKind_ == 1 && s->held_ && s->status_.find("F10") != std::string::npos, "incomplete registration reports why");
  }
  s->setCode("ABCD"); s->click(reg);
  Input press; press.type = Input::MouseDown; press.x = row.x + row.w / 2; press.y = row.y + row.h / 2;
  s->input(press); s->finish();
  press.type = Input::MouseUp; s->input(press);
  check(s->status_.empty() && !s->held_, "no registration success banner");
  auto compact = [&](ScreenFixture& view) {
    auto& c = *view.discordCheck_;
    check(!view.bRegister_->visible && !view.bRegister_->hit(row.x + 1, row.y + 1), "registration removed");
    check(c.visible && c.enabled && c.x == row.x && c.y == row.y && c.w == row.w, "checkbox occupies registration row");
  };
  compact(*s);
  check(!s->capture && s->hover != &reg && s->focusW == s->codeEdit_, "usable focus and no hidden capture or hover");
  Input text; text.type = Input::Text; text.text = "E"; s->input(text);
  check(s->codeEdit_->value == "ABCDE", "code and caret survive registration");
  s->draw(app.gfx);
  s->click(row.x + row.w - 2, row.y + box.h / 2);
  s->click(old.x + old.w / 2, old.y + old.h / 2);
  check(s->toggles == 1 && attempts == 4 && !s->job_, "only replacement hit target activates");
  if (s->job_) s->finish();
  s->configuredLobby_ = "fixture.invalid:7770";
  s->click(*s->bTest_); s->finish();
  check(s->status_.starts_with("Direct links should work:") && s->statusKind_ == 2 && attempts == 4, "connection test still works in compact layout");
  s.reset(); s = open(); compact(*s);
  check(attempts == 4 && s->focusW == s->codeEdit_, "reopen already registered without registering");
  if (app.gfx.canvasW() > 800) {
    s->setCode("AB");
    std::string expectedCode = "AB";
    for (int width : {800, 1067}) {
      app.settings.widescreen = int(width == 800 ? pp::ui::WidescreenMode::Classic4x3 : pp::ui::WidescreenMode::Auto);
      app.gfx.begin(width);
      check(app.consoleCanvasWidth() == width, "fresh comparison uses resized canvas");
      auto fresh = open();
      const Rect expected = fresh->discordCheck_->rect();
      Input motion; motion.type = Input::MouseMove; motion.x = expected.x + expected.w - 1; motion.y = expected.y + expected.h / 2;
      s->input(motion);
      check(s->hover == s->discordCheck_, "resized row hit-tests before redraw");
      s->draw(app.gfx);
      const Rect got = s->discordCheck_->rect();
      check(got.x == expected.x && got.y == expected.y && got.w == expected.w && got.h == expected.h &&
            got.x >= 0 && got.x + got.w <= width, "resized Discord row matches fresh bounds");
      check(s->focusW == s->codeEdit_ && s->codeEdit_->focused, "resize preserves code focus");
      s->input(text);
      expectedCode += text.text;
      check(s->codeEdit_->value == expectedCode, "text entry survives resize");
      const int before = s->toggles;
      s->click(expected.x + expected.w - 1, expected.y + expected.h / 2);
      s->click(expected.x + expected.w + 1, expected.y + expected.h / 2);
      check(s->toggles == before + 1 && attempts == 4, "resized hit area matches fresh row");
      s->click(*s->codeEdit_);
    }
  }
  using namespace pp::online;
  if (desktopMode) {
    const auto original = entry;
    entry = "[Desktop Entry]\nType=Application\nExec=\"/opt/other\" %u\n[Desktop Action Peggle]\nExec=\"" + fixtureExe(nullptr) + "\" %u\n";
    s = open(); check(s->bRegister_->visible, "an action Exec is not the desktop launch command");
    entry = "[Desktop Entry]\nType=Application\n[Desktop Action Peggle]\nExec=\"" + fixtureExe(nullptr) + "\" %u\n";
    s = open(); check(s->bRegister_->visible, "missing main Exec remains unconfirmed");
    entry = original + "[Desktop Action Other]\nExec=\"/opt/other\" %u\n";
    for (int i : {0, 1}) {
      ids[i] = "other.desktop"; s = open(); check(s->bRegister_->visible, "both desktop IDs must match");
      ids[i] = "peggleport-url.desktop"; queryOK[i] = false; s = open(); check(s->bRegister_->visible, "failed default query is not ownership");
      queryOK[i] = true;
    }
    s = open(); check(!s->bRegister_->visible, "an unrelated action does not invalidate the main Exec");
    auto wait = [](const std::atomic<bool>& flag) {
      const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (!flag && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return flag.load();
    };
    for (bool close : {false, true}) {
      seedRegistration(3);
      entered = releaseQuery = returned = sawStop = false; delayNext = true;
      s = open(false);
      check(wait(entered) && !returned && s->bRegister_->visible, "Join opens before delayed owner query completes");
      std::weak_ptr<Job> oldCheck = s->ownerCheck_;
      s->setCode("AB"); s->input(text); s->update();
      check(!returned && s->codeEdit_->value == "ABE" && !s->job_, "pending detection does not block input or update");
      if (close) {
        s.reset();
        check(!returned, "closing Join retires rather than joins pending check");
        seedRegistration(0); s = open();
      } else {
        registration = 2; s->click(*s->bRegister_); s->finish();
        check(s->statusKind_ == 1 && s->status_.find("default handlers") != std::string::npos, "unconfirmed write explains retry");
      }
      releaseQuery = true;
      check(wait(returned) && sawStop, "retired owner query is cancelled and completes");
      const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (!oldCheck.expired() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
      s->update();
      check(oldCheck.expired() && s->bRegister_->visible, "retired success cannot replace current ownership");
    }
  }
#ifdef _WIN32
  else for (int scheme : {0, 1}) {
    otherChoice = scheme; s = open();
    check(s->bRegister_->visible, "another effective choice keeps Register visible");
    s->click(*s->bRegister_); s->finish();
    check(s->bRegister_->visible && s->statusKind_ == 1 && s->status_.find("default handlers") != std::string::npos,
          "successful write cannot bypass an effective choice");
    otherChoice = -1; unknownChoice = scheme; s = open();
    check(s->bRegister_->visible, "unresolved choice keeps Register visible");
    unknownChoice = -1;
    sameOwnerChoice = scheme;
    auto& localCommand = scheme ? discordCommand : protocolCommand;
    const auto saved = localCommand;
    localCommand = LR"("C:\fixture\Old.exe" "%1")";
    s = open(); check(!s->bRegister_->visible, "effective owner wins over stale installed command");
    localCommand = saved; sameOwnerChoice = -1;
  }
#endif
  s.reset(); pp::online::seedRegistration(0); s = open();
  check(s->bRegister_->visible && s->discordCheck_->x == old.x && s->discordCheck_->y == old.y, "registration removed externally can be retried");
}

#ifdef __linux__
static int fakeXdg(const char* mime) {
  const bool discord = std::string(mime) == "x-scheme-handler/discord-1571022918392225882";
  if (!discord && std::string(mime) != "x-scheme-handler/peggleport") return 2;
  fs::path dir = std::getenv("XDG_DATA_HOME");
  std::string mode;
  std::ifstream(dir / (discord ? "second" : "first")) >> mode;
  if (mode == "hang") {
    std::ofstream(dir / "pid") << getpid();
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }
  std::cout << (mode == "other" ? "other.desktop" : "peggleport-url.desktop") << '\n';
  return mode == "fail" ? 1 : 0;
}

static void nativeQueries(const fs::path& dir) {
  const auto bin = dir / "bin";
  fs::create_directories(bin);
  fs::create_symlink("/proc/self/exe", bin / "xdg-mime");
  const char* path = std::getenv("PATH"), *data = std::getenv("XDG_DATA_HOME");
  const std::string oldPath = path ? path : "", oldData = data ? data : "";
  const bool hadPath = path != nullptr, hadData = data != nullptr;
  if (setenv("XDG_DATA_HOME", dir.c_str(), 1) || setenv("PATH", bin.c_str(), 1)) throw std::runtime_error("cannot isolate xdg fixture");
  const std::string exe = "/opt/fixture/peggleport";
  const auto desktop = pp::online::desktopFile();
  fs::create_directories(desktop.parent_path());
  std::ofstream(desktop) << "[Desktop Entry]\nType=Application\nExec=\"" << exe << "\" %u\n";
  auto owns = [&] { return pp::online::urlHandlerRegistered(exe); };
  check(owns(), "native query accepts both selected fixture IDs");
  for (auto scheme : {"first", "second"}) {
    for (auto mode : {"other", "fail"}) {
      std::ofstream(dir / scheme) << mode;
      check(!owns(), "native non-owner or failed query stays unconfirmed");
    }
    fs::remove(dir / scheme);
  }
  std::ofstream(dir / "second") << "hang";
  const auto start = std::chrono::steady_clock::now();
  check(!owns() && std::chrono::steady_clock::now() - start < std::chrono::seconds(4), "hung query is bounded");
  pid_t pid = -1;
  std::ifstream(dir / "pid") >> pid;
  check(pid > 0 && kill(pid, 0) == -1 && errno == ESRCH, "timed-out fixture process was killed and reaped");
  fs::remove(dir / "pid");
  std::atomic<bool> stop{false};
  bool cancelled = false;
  std::thread worker([&] { cancelled = !pp::online::urlHandlerRegistered(exe, &stop); });
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  pid = -1;
  while (pid <= 0 && std::chrono::steady_clock::now() < until) {
    std::ifstream(dir / "pid") >> pid;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto stopping = std::chrono::steady_clock::now();
  stop = true; worker.join();
  check(cancelled && std::chrono::steady_clock::now() - stopping < std::chrono::seconds(1) &&
        pid > 0 && kill(pid, 0) == -1 && errno == ESRCH, "cancellation reaps a hung query without waiting for its deadline");
  fs::remove(dir / "second");
  check(owns(), "query works after timeout");
  if (hadPath) setenv("PATH", oldPath.c_str(), 1); else unsetenv("PATH");
  if (hadData) setenv("XDG_DATA_HOME", oldData.c_str(), 1); else unsetenv("XDG_DATA_HOME");
  fs::remove_all(dir);
}
#endif

int main(int argc, char** argv) {
#ifdef __linux__
  if (argc == 4 && std::string(argv[0]) == "xdg-mime" && std::string(argv[1]) == "query" && std::string(argv[2]) == "default") return fakeXdg(argv[3]);
#endif
  if (argc != 5 || !SDL_Init(0)) return 2;
  fs::path scratch = fs::path(argv[4]) / ("invite-" + std::to_string(SDL_GetPerformanceCounter()));
  if (!fs::create_directories(scratch)) return 2;
  try {
#ifdef __linux__
    nativeQueries(fs::absolute(scratch / "xdg-fixture"));
#endif
    for (auto game : {Game::Deluxe, Game::Nights}) {
      Vfs vfs;
      if (!vfs.mountGame(argv[game == Game::Deluxe ? 1 : 2], nullptr) || !vfs.mountPresentation(argv[3])) throw std::runtime_error("asset mount");
      for (int width : {800, 1067}) {
        std::cout << (game == Game::Deluxe ? "Deluxe " : "Nights ") << width << '\n' << std::flush;
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(width, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        Gfx gfx(renderer.get(), vfs); gfx.begin(width);
        App::Config cfg; cfg.game = game; cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = true; cfg.userDir = scratch / "userdata";
        PlayerInfo player; App app(gfx, vfs, nullptr, nullptr, cfg); app.curProfile = &player;
        check(app.consoleCanvasWidth() == width, "expected canvas");
        pp::online::desktopMode = true; exercise(app);
#ifdef _WIN32
        pp::online::desktopMode = false; exercise(app);
#endif
      }
    }
    check(fs::is_empty(scratch), "no settings or profile files");
  } catch (const std::exception& e) { check(false, e.what()); }
  fs::remove_all(scratch); SDL_Quit(); std::cout << "failures=" << failures << '\n'; return failures != 0;
}
