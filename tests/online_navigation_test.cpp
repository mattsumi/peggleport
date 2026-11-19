#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "online/discovery.h"
#include "online/online_ui.h"
#include "online/steam.h"
#include "ui/screens.h"
#include "ui/widescreen.h"

namespace pp {
struct SeatedSession : Session {
  static inline std::atomic<int> joins;
  static bool listRooms(const std::string&, int, std::vector<std::string>*, std::string*, const std::atomic<bool>*) { return false; }
  bool joinViaLobby(const std::string&, int, const std::string&, std::string*, const std::atomic<bool>*) {
    ++joins;
    return false;
  }
  MatchInfo match;
  const MatchInfo& info() const { return match; }
  const std::vector<Seat> seats{{"Host", true, true}, {"Guest", true, false}};
  static inline int ready, starts, leaves;
  const std::vector<Seat>& roster() const { return seats; }
  bool allReady() const { return true; }
  void poll() {}
  void sendReady(uint64_t) { ++ready; }
  void hostStart() { ++starts; }
  void leave() { ++leaves; }
};
}

namespace pp::online {
struct FixtureBrowser {
  explicit FixtureBrowser(RoomLister) {}
  std::shared_future<BrowseSnapshot> listing;
  bool cancelled = false;
  bool start(const BrowseOptions&) { throw std::logic_error("unexpected discovery"); }
  void cancel() { cancelled = true; }
  BrowseSnapshot snapshot() const {
    if (!listing.valid()) return {};
    if (listing.wait_for(std::chrono::seconds(0)) == std::future_status::ready) return listing.get();
    return {{}, true, 1};
  }
};
}

#define RoomBrowser FixtureBrowser
#define private public
#define Session SeatedSession
#include "online/online_ui.cpp"
#undef RoomBrowser
#undef Session
#undef private

using namespace pp;
using namespace pp::ui;
using namespace pp::ui::online;
namespace fs = std::filesystem;

static void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}

static void settle(App& app) {
  for (int i = 0; i < 25; ++i) app.tick();
  app.frame();
}

static void click(App& app, float x, float y) {
  Input in;
  in.x = x; in.y = y;
  for (auto type : {Input::MouseMove, Input::MouseDown, Input::MouseUp}) {
    in.type = type;
    app.input(in);
  }
}

static Button* button(App& app, const char* label) {
  for (const auto& widget : app.screen()->widgets)
    if (auto* b = dynamic_cast<Button*>(widget.get()); b && b->visible && b->label == label) return b;
  return nullptr;
}

static void press(App& app, const char* label) {
  auto* b = button(app, label);
  require(b && b->enabled, std::string("no available ") + label);
  click(app, b->x + b->w / 2, b->y + b->h / 2);
}

static void menu(App& app, int width) {
  app.showMainMenu(app.game == Game::Nights && width > 800);
  settle(app);
  if (width > 800 && app.game == Game::Nights) {
    press(app, "Lounge"); settle(app);
    require(app.gfx.canvasW() == 800 && app.consoleCanvasWidth() == width, "not in the fixed-width Lounge");
  }
  if (width > 800 && app.game == Game::Deluxe) {
    require(!button(app, "Online"), "hidden Classic footer exposed on console Home");
    press(app, "Multiplayer"); settle(app);
    press(app, "Join With Code"); settle(app);
    require(app.screenName() == "online" && !app.anyDialog() && button(app, "Join"), "console Join route");
    std::cout << "console Join destination=" << app.screenName() << '\n';
    return;
  }
  press(app, "Online"); settle(app);
  std::cout << "footer destination=" << app.screenName() << '\n';
  require(app.screenName() == "online" && !app.anyDialog() && button(app, "Create Room"), "Online footer did not open Browse");
}

static void resizeWindow(App& app, SDL_Window* window, int width) {
  require(SDL_SetWindowSize(window, width, 600) && SDL_SyncWindow(window), "resize failed");
  SDL_PumpEvents(); app.frame();
}

static void roomResize(App& app, SDL_Window* window, bool host) {
  auto screen = std::make_unique<OnlineScreen>(app, "", false, host ? Flow::Host : Flow::Join);
  auto* view = screen.get();
  view->sess_ = std::make_unique<SeatedSession>();
  auto* session = view->sess_.get();
  view->hostInfo_.level = session->match.level = levelChoices(app).at(0).file;
  view->create_ = false;
  view->isHost_ = host;
  view->enterRoom(); view->guestChecked_ = true;
  view->roomCode_ = "ABCDE"; view->lobbyHost_ = "fixture.invalid";
  app.setScreen(std::move(screen), App::Stay); settle(app);
  require(view->seated_ == 2 && !button(app, "Leave Room"), "wide room setup");
  auto resize = [&](int width) {
    resizeWindow(app, window, width);
    require(app.gfx.canvasW() == width && app.screen() == view && view->sess_.get() == session && view->seated_ == 2, "resize lost room");
  };
  resize(800);
  const char* action = host ? "Start Match" : "Ready";
  std::cout << "Classic seated=" << view->seated_ << " copy=" << bool(button(app, "Copy Invite")) << ' ' << action << '=' << bool(button(app, action)) << " leave=" << bool(button(app, "Leave Room")) << '\n';
  require(button(app, "Copy Invite") && button(app, "Leave Room") && !button(app, host ? "Ready" : "Start Match"), "Classic room controls");
  SeatedSession::ready = SeatedSession::starts = SeatedSession::leaves = 0;
  press(app, action); app.tick();
  require(host ? SeatedSession::starts == 1 : SeatedSession::ready == 1 && button(app, "Ready - waiting") && !button(app, "Ready - waiting")->enabled, "Classic primary action missed");
  Input held; held.type = Input::MouseDown; held.x = 670; held.y = 535; app.input(held);
  require(view->capture == view->bLeave_, "Classic Leave did not capture press");
  resize(1067);
  held.type = Input::MouseUp; held.x = 937; held.y = 570; app.input(held);
  require(app.screen() == view && !SeatedSession::leaves, "hidden Classic Leave consumed release");
  require(!button(app, "Leave Room") && !button(app, "Copy Invite") && !view->bReady_->visible && !view->bStart_->visible &&
          onlinePadKey(app, SDL_GAMEPAD_BUTTON_EAST) == SDLK_ESCAPE, "wide room actions not restored");
  if (host) {
    const auto quit = std::find_if(view->prompts_.begin(), view->prompts_.end(), [](auto p) { return p.key == SDLK_ESCAPE; });
    require(quit != view->prompts_.end(), "no wide Quit prompt");
    click(app, quit->x + quit->w / 2, 565);
  } else {
    resize(800); press(app, "Leave Room");
  }
  std::cout << "room click counts: ready=" << SeatedSession::ready << " start=" << SeatedSession::starts << " leave=" << SeatedSession::leaves << '\n';
  require(SeatedSession::leaves == 1 && app.screenName() == "mainmenu" && !app.anyDialog(), "Leave missed");
  settle(app);
}

static void quick(App& app, bool back) {
  SeatedSession::joins = 0;
  showOnline(app, Flow::Quick, false);
  auto* view = dynamic_cast<OnlineScreen*>(app.screen());
  require(view && view->canvasWidth() == 1067, "wide Quick Match setup");
  std::promise<void> finish;
  auto listing = std::async(std::launch::async, [gate = finish.get_future(), level = levelChoices(app).at(0).file] {
    if (gate.wait_for(std::chrono::seconds(5)) != std::future_status::ready) throw std::runtime_error("lister gate timed out");
    pp::online::LobbyListing row;
    pp::online::parseEndpoint("fixture.invalid:7770", &row.endpoint);
    row.ok = row.done = true;
    row.lines = {"ROOM ABCDE 2 1 0 " + level + " 0 Fixture"};
    return pp::online::BrowseSnapshot{{row}, false, 2};
  }).share();
  view->browser_.listing = listing;
  settle(app);
  if (back) {
    click(app, 80, 565);
    require(app.screenName() == "mainmenu" && app.moving() && !app.anyDialog(), "Back did not start menu slide");
  }
  const bool cancelled = view->browser_.cancelled;
  finish.set_value(); listing.get();
  app.tick();
  if (view->job_ && view->job_->th.joinable()) view->job_->th.join();
  settle(app);
  std::cout << "Quick " << (back ? "Back" : "active") << " cancelled=" << cancelled << " join calls=" << SeatedSession::joins << '\n';
  require(SeatedSession::joins == (back ? 0 : 1) && cancelled == back &&
          app.screenName() == (back ? "mainmenu" : "online") && !app.anyDialog(), "Quick Match list completion");
}

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
  if (!SDL_Init(SDL_INIT_VIDEO)) return 2;
  auto scratch = fs::path(argv[4]) / ("nav-" + std::to_string(SDL_GetPerformanceCounter()));
  int failures = 0;
  for (auto game : {Game::Deluxe, Game::Nights}) {
    const bool nights = game == Game::Nights;
    auto assets = scratch / (nights ? "Nights" : "Deluxe");
    fs::create_directories(assets);
    fs::copy_file(fs::path(argv[nights ? 2 : 1]) / "main.pak", assets / "main.pak");
    Vfs vfs;
    require(vfs.mountGame(assets, nullptr) && vfs.mountPresentation(argv[3]), "asset mount");
    for (int width : {800, 1067}) {
      std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Online navigation", width, 600, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
      std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(), "software"), SDL_DestroyRenderer);
      require(renderer != nullptr, SDL_GetError());
      Gfx gfx(renderer.get(), vfs); gfx.begin(width);
      PlayerInfo player;
      App::Config cfg; cfg.game = game; cfg.noRoomDiscovery = cfg.noSave = cfg.noAudio = true;
      cfg.widescreen = static_cast<int>(pp::ui::WidescreenMode::Auto);
      cfg.userDir = scratch / (nights ? "nights-profile" : "deluxe-profile") / std::to_string(width);
      App app(gfx, vfs, nullptr, nullptr, cfg); app.curProfile = &player;
      std::cout << (nights ? "Nights " : "Deluxe ") << width << '\n';
      auto check = [&](auto exercise) {
        try { exercise(); } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; ++failures; }
      };
      check([&] { menu(app, width); });
      if (width > 800) for (bool host : {true, false}) check([&] {
        resizeWindow(app, window.get(), width);
        roomResize(app, window.get(), host);
      });
      if (width > 800) {
        resizeWindow(app, window.get(), width);
        for (bool back : {false, true}) check([&] { quick(app, back); });
      }
    }
  }
  fs::remove_all(scratch);
  SDL_Quit();
  std::cout << "failures=" << failures << '\n';
  return failures != 0;
}
