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
  static inline std::atomic<int> joins, hosts;
  bool hostViaLobby(const std::string&, int, const std::string&, const MatchInfo&, std::string*, const std::atomic<bool>*) {
    ++hosts;
    return false;
  }
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
#include "ui/run.cpp"

pp::ui::PadGlyphs pp::ui::readPad(SDL_Gamepad*) { return {Pad::PlayStation, {}}; }

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

static void resizeWindow(SDL_Window* window, int width, int height = 600) {
  require(SDL_SetWindowSize(window, width, height) && SDL_SyncWindow(window), "resize failed");
  SDL_PumpEvents();
}

static void mouse(App& app, SDL_Window* window, Uint32 type, float x, float y) {
  Live live{window, app.gfx.renderer(), &app, &app.gfx};
  SDL_Event e{};
  e.type = type;
  if (type == SDL_EVENT_MOUSE_MOTION) {
    e.motion.windowID = SDL_GetWindowID(window);
    e.motion.x = x; e.motion.y = y;
  } else {
    e.button.windowID = SDL_GetWindowID(window);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.clicks = 1;
    e.button.x = x; e.button.y = y;
  }
  handleEvent(live, e);
}

static void south(Live& live, Uint32 type) {
  SDL_Event e{};
  e.type = type;
  e.gbutton.which = 1;
  e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
  handleEvent(live, e);
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
    resizeWindow(window, width); app.frame();
    require(app.gfx.canvasW() == width && app.screen() == view && view->sess_.get() == session && view->seated_ == 2, "resize lost room");
  };
  resize(800);
  const char* action = host ? "Start Match" : "Ready";
  std::cout << "Classic seated=" << view->seated_ << " copy=" << bool(button(app, "Copy Invite")) << ' ' << action << '=' << bool(button(app, action)) << " leave=" << bool(button(app, "Leave Room")) << '\n';
  require(button(app, "Copy Invite") && button(app, "Leave Room") && !button(app, host ? "Ready" : "Start Match"), "Classic room controls");
  SeatedSession::ready = SeatedSession::starts = SeatedSession::leaves = 0;
  press(app, action); app.tick();
  require(host ? SeatedSession::starts == 1 : SeatedSession::ready == 1 && button(app, "Ready - waiting") && !button(app, "Ready - waiting")->enabled, "Classic primary action missed");
  Live live{window, app.gfx.renderer(), &app, &app.gfx};
  for (bool pad : {false, true}) {
    resize(800);
    mouse(app, window, SDL_EVENT_MOUSE_MOTION, 670, 535);
    if (pad) south(live, SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    else mouse(app, window, SDL_EVENT_MOUSE_BUTTON_DOWN, 670, 535);
    require(view->capture == view->bLeave_ && view->bLeave_->down, "Classic Leave did not capture press");
    resizeWindow(window, 1067);
    if (!pad && !host) {
      mouse(app, window, SDL_EVENT_MOUSE_MOTION, 670, 535);
      require(!view->capture && !view->bLeave_->down, "hidden Leave still captured after motion");
    }
    if (pad) south(live, SDL_EVENT_GAMEPAD_BUTTON_UP);
    else mouse(app, window, SDL_EVENT_MOUSE_BUTTON_UP, 670, 535);
    const std::string source = pad ? "South" : "mouse";
    require(app.screen() == view && view->sess_.get() == session && view->seated_ == 2 && !SeatedSession::leaves,
            source + " pre-draw release at old Leave rectangle lost session");
    require(app.gfx.canvasW() == 1067 && !view->capture && !view->bLeave_->down, "hidden Leave retained capture");
    std::cout << source << " pre-draw Leave cancelled; same " << (host ? "host" : "guest") << " session\n";
  }
  app.frame();
  require(!button(app, "Leave Room") && !button(app, "Copy Invite") && !view->bReady_->visible && !view->bStart_->visible &&
          onlinePadKey(app, SDL_GAMEPAD_BUTTON_EAST) == SDLK_ESCAPE, "wide room actions not restored");
  if (host) {
    const auto quit = std::find_if(view->prompts_.begin(), view->prompts_.end(), [](auto p) { return p.key == SDLK_ESCAPE; });
    require(quit != view->prompts_.end(), "no wide Quit prompt");
    click(app, quit->x + quit->w / 2, 565);
  } else {
    south(live, SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    resizeWindow(window, 800);
    mouse(app, window, SDL_EVENT_MOUSE_BUTTON_DOWN, 670, 535);
    south(live, SDL_EVENT_GAMEPAD_BUTTON_UP);
    require(view->capture == view->bLeave_ && !SeatedSession::leaves, "console South release stole Classic mouse press");
    mouse(app, window, SDL_EVENT_MOUSE_BUTTON_UP, 670, 535);
  }
  std::cout << "room click counts: ready=" << SeatedSession::ready << " start=" << SeatedSession::starts << " leave=" << SeatedSession::leaves << '\n';
  require(SeatedSession::leaves == 1 && app.screenName() == "mainmenu" && !app.anyDialog(), "Leave missed");
  settle(app);
}

static void emptyCustom(App& app, SDL_Window* window) {
  showOnline(app, Flow::Custom, false);
  settle(app);
  auto* view = dynamic_cast<OnlineScreen*>(app.screen());
  require(view && view->console() && view->rooms_.empty() && !button(app, "Main Menu") && !button(app, "Refresh"), "wide Custom setup");
  for (int i = 0; i <= 200; ++i) app.tick();
  resizeWindow(window, 800); app.frame();
  require(view->flow_ == Flow::Custom && view->prompts_.empty() && button(app, "Main Menu") && button(app, "Refresh"),
          "empty narrowed Custom list has no Classic navigation");
  view->hold("Waiting", 0, true);
  mouse(app, window, SDL_EVENT_MOUSE_BUTTON_DOWN, 650, 95);
  mouse(app, window, SDL_EVENT_MOUSE_BUTTON_UP, 650, 95);
  require(!view->held_ && app.screen() == view && view->rooms_.empty(), "empty Custom Refresh missed");
  resizeWindow(window, 600, 450);
  mouse(app, window, SDL_EVENT_MOUSE_BUTTON_DOWN, 90, 405);
  require(view->capture == view->bBack_, "scaled pre-draw press missed Main Menu");
  mouse(app, window, SDL_EVENT_MOUSE_BUTTON_UP, 90, 405);
  require(app.screenName() == "mainmenu" && !app.anyDialog(), "empty Custom mouse exit missed");
  std::cout << "empty Custom: Refresh and scaled Main Menu clicks passed\n";
}

static bool narrowExit(App& app, SDL_Window* window, OnlineScreen& view) {
  const auto flow = view.flow_;
  const bool host = flow == Flow::Host;
  resizeWindow(window, 800);
  const float x = host ? 480.0f : 130.0f, y = host ? 405.0f : 535.0f;
  mouse(app, window, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
  const auto* exit = host ? view.bCancel_ : view.bBack_;
  const bool captured = view.flow_ == flow && view.capture == exit && exit->visible && !view.bRefresh_->visible;
  mouse(app, window, SDL_EVENT_MOUSE_BUTTON_UP, x, y);
  return captured && app.screenName() == "mainmenu" && !app.anyDialog();
}

static void formExit(App& app, SDL_Window* window, Flow flow) {
  const bool host = flow == Flow::Host;
  SeatedSession::hosts = SeatedSession::joins = 0;
  showOnline(app, flow, false);
  settle(app);
  auto* view = dynamic_cast<OnlineScreen*>(app.screen());
  require(view && view->console() && view->create_ == host && view->codeEdit_->value.empty() &&
          !view->bBack_->visible && !view->bCancel_->visible, "wide form setup");
  if (host) {
    const auto level = view->levelIdx_;
    const float x = view->bLvlPrev_->x + view->bLvlPrev_->w / 2, y = view->bLvlPrev_->y + view->bLvlPrev_->h / 2;
    resizeWindow(window, 600, 450);
    mouse(app, window, SDL_EVENT_MOUSE_MOTION, x, y);
    resizeWindow(window, 1067);
    Live live{window, app.gfx.renderer(), &app, &app.gfx};
    south(live, SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    south(live, SDL_EVENT_GAMEPAD_BUTTON_UP);
    require(view->levelIdx_ != level && !view->job_, "pre-draw wide South missed Host level arrow");
  }
  require(narrowExit(app, window, *view), host ? "narrow Host Cancel missed" : "narrow empty Join Main Menu missed");
  settle(app);
  require(!SeatedSession::hosts && !SeatedSession::joins, "exiting form submitted it");
  std::cout << (host ? "Host Cancel" : "empty Join Main Menu") << ": no host or join submitted\n";
}

static void quick(App& app, SDL_Window* window, int exitWidth) {
  const bool back = exitWidth != 0;
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
  bool exited = true;
  if (exitWidth == 800) {
    require(view->flow_ == Flow::Quick && view->browse_.busy(), "Quick listing not pending");
    exited = narrowExit(app, window, *view);
  } else if (back) {
    click(app, 80, 565);
    require(app.screenName() == "mainmenu" && app.moving() && !app.anyDialog(), "Back did not start menu slide");
  }
  const bool cancelled = view->browser_.cancelled;
  finish.set_value(); listing.get();
  app.tick();
  if (view->job_ && view->job_->th.joinable()) view->job_->th.join();
  settle(app);
  std::cout << "Quick " << (back ? (exitWidth == 800 ? "Classic Main Menu" : "Back") : "active") << " cancelled=" << cancelled << " join calls=" << SeatedSession::joins << '\n';
  require(exited && SeatedSession::joins == (back ? 0 : 1) && cancelled == back &&
          app.screenName() == (back ? "mainmenu" : "online") && !app.anyDialog(), "Quick Match list completion");
}

static int navigation(char** argv, const fs::path& scratch) {
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
        resizeWindow(window.get(), width); app.frame();
        roomResize(app, window.get(), host);
      });
      if (width > 800) {
        resizeWindow(window.get(), width); app.frame();
        check([&] { emptyCustom(app, window.get()); });
        resizeWindow(window.get(), width); app.frame();
        for (int exitWidth : {0, 1067, 800}) check([&] { quick(app, window.get(), exitWidth); });
        for (auto flow : {Flow::Join, Flow::Host}) check([&] {
          resizeWindow(window.get(), width); app.frame();
          formExit(app, window.get(), flow);
        });
      }
    }
  }
  std::cout << "failures=" << failures << '\n';
  return failures != 0;
}

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
  if (!SDL_Init(SDL_INIT_VIDEO)) return 2;
  const fs::path parent = argv[4], scratch = parent / "navigation";
  std::error_code ec;
  fs::create_directories(parent, ec);
  int result = 1;
  if (ec || !fs::create_directory(scratch, ec)) {
    std::cerr << "Need a fresh navigation fixture: " << scratch << '\n';
  } else {
    try { result = navigation(argv, scratch); }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; }
    fs::remove_all(scratch, ec);
    if (ec) { std::cerr << ec.message() << '\n'; result = 1; }
  }
  SDL_Quit();
  return result;
}
