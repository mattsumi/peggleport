#include "ui/run.h"

#include "image.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "audio.h"
#include "gamefind.h"
#include "gamepaths.h"
#include "log.h"
#include "online/invite.h"
#include "online/online_ui.h"
#include "render.h"
#include "ui/screens.h"
#include "ui/game_screen.h"

namespace pp::ui {

namespace {

struct Pacer {
  uint64_t last = 0;
  double acc = 0;
};

struct Live {
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  App* app = nullptr;
  Gfx* gfx = nullptr;
  std::vector<SDL_Gamepad*> pads;
  std::set<std::pair<SDL_JoystickID, Uint8>> consolePadButtons;
  std::set<std::pair<SDL_JoystickID, Uint8>> helpAxes;
  bool hidden = false;
  GameWindow* window = nullptr;
  int leftTicks = 0;
  Pacer* pacer = nullptr;
  SDL_ThreadID thread = 0;
  int busy = 0;
  bool redrawing = false;
};

struct Busy {
  explicit Busy(Live& l) : live(l) { live.busy++; }
  ~Busy() { live.busy--; }
  Busy(const Busy&) = delete;
  Busy& operator=(const Busy&) = delete;
  Live& live;
};

static bool consoleHelp(const Live& L) {
  return L.gfx->canvasW() >= 960 && L.app->widescreenMode() != WidescreenMode::Classic4x3 &&
         dynamic_cast<HelpScreen*>(L.app->helpOverlay());
}

static bool handleGamepad(Live& L, const SDL_Event& e) {
  App& app = *L.app;
  Gfx& gfx = *L.gfx;
  auto key = [&](int k, bool down) {
    Input in;
    in.type = down ? Input::KeyDown : Input::KeyUp;
    in.key = k;
    app.input(in);
  };
  auto mouse = [&](int button, bool down) {
    Input in;
    in.type = down ? Input::MouseDown : Input::MouseUp;
    in.button = button;
    in.x = app.mouseX;
    in.y = app.mouseY;
    in.wx = gfx.offX() + in.x * gfx.scale();
    in.wy = gfx.offY() + in.y * gfx.scale();
    app.input(in);
  };
  static bool stickL = false, stickR = false, trig = false;
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) app.pad = readPad(SDL_GetGamepadFromID(e.gbutton.which));
  switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
      bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
      const auto press = std::pair{e.gbutton.which, e.gbutton.button};
      if (!down && L.consolePadButtons.erase(press)) return true;
      if (consoleHelp(L)) {
        if (!down || !L.consolePadButtons.insert(press).second) return true;
        switch (e.gbutton.button) {
          case SDL_GAMEPAD_BUTTON_DPAD_LEFT: key(SDLK_LEFT, true); break;
          case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: key(SDLK_RIGHT, true); break;
          case SDL_GAMEPAD_BUTTON_EAST:
          case SDL_GAMEPAD_BUTTON_START: key(SDLK_ESCAPE, true); break;
          default: break;
        }
        return true;
      }
      if (down && (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)) {
        auto* game = dynamic_cast<GameScreen*>(app.screen());
        if (game && game->consoleReplayPanel() && !app.helpOverlay()) {
          const bool save = e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
          const auto* dialog = app.topDialog();
          const bool saving = dialog && dialog->dialogId == D_SaveReplay && !dialog->scrollActive;
          if (!dialog || (!save && saving)) {
            L.consolePadButtons.insert(press);
            key(save ? SDLK_F2 : saving ? SDLK_RETURN : SDLK_ESCAPE, true);
            return true;
          }
        }
      }
      if (down) {
        if (const auto k = online::onlinePadKey(app, static_cast<SDL_GamepadButton>(e.gbutton.button))) {
          if (L.consolePadButtons.insert(press).second && !app.moving()) key(k, true);
          return true;
        }
      }
      if (auto* title = dynamic_cast<LoadingScreen*>(app.screen()); title && title->canvasWidth() > 800 &&
          !app.anyDialog() && (e.gbutton.button == SDL_GAMEPAD_BUTTON_START || e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)) {
        if (down) { L.consolePadButtons.insert(press); key(SDLK_RETURN, true); }
        return true;
      }
      if (auto* menu = dynamic_cast<MainMenu*>(app.screen()); menu && menu->canvasWidth() > 800 &&
          !app.anyDialog() && !app.helpOverlay() && !app.moving()) {
        int direction = 0;
        switch (e.gbutton.button) {
          case SDL_GAMEPAD_BUTTON_DPAD_UP: direction = SDLK_UP; break;
          case SDL_GAMEPAD_BUTTON_DPAD_DOWN: direction = SDLK_DOWN; break;
          case SDL_GAMEPAD_BUTTON_DPAD_LEFT: direction = SDLK_LEFT; break;
          case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: direction = SDLK_RIGHT; break;
          case SDL_GAMEPAD_BUTTON_SOUTH: direction = SDLK_RETURN; break;
          case SDL_GAMEPAD_BUTTON_EAST: direction = SDLK_ESCAPE; break;
          default: break;
        }
        if (direction && down) {
          L.consolePadButtons.insert(press);
          key(direction, true);
          return true;
        }
      }
      switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: key(SDLK_UP, down); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: key(SDLK_DOWN, down); break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: if (down) key(SDLK_LEFT, true); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: if (down) key(SDLK_RIGHT, true); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: mouse(1, down); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: mouse(3, down); break;
        case SDL_GAMEPAD_BUTTON_NORTH: if (down) key(SDLK_R, true); break;
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_START: if (down) key(SDLK_ESCAPE, true); break;
        default: break;
      }
      return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      int v = e.gaxis.value;
      const auto axis = std::pair{e.gaxis.which, e.gaxis.axis};
      if (consoleHelp(L) || L.helpAxes.contains(axis)) {
        if (std::abs(v) <= 12000) {
          L.helpAxes.erase(axis);
          if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) stickL = stickR = false;
          else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) trig = false;
        } else if (L.helpAxes.insert(axis).second) app.pad = readPad(SDL_GetGamepadFromID(e.gaxis.which));
        return true;
      }
      if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
        bool l = v < -12000, r = v > 12000;
        if (l != stickL) { stickL = l; key(SDLK_UP, l); }
        if (r != stickR) { stickR = r; key(SDLK_DOWN, r); }
      } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        bool t = v > 12000;
        if (t != trig) { trig = t; mouse(3, t); }
      }
      return true;
    }
    default: return false;
  }
}

static void handleEvent(Live& L, SDL_Event& e) {
  if (handleGamepad(L, e)) return;
  App& app = *L.app;
  Gfx& gfx = *L.gfx;
  SDL_ConvertEventToRenderCoordinates(L.ren, &e);
  Input in;
  bool have = false;
  switch (e.type) {
    case SDL_EVENT_QUIT: app.onWindowClose(); break;
    case SDL_EVENT_DROP_FILE:
      if (e.drop.data && pp::online::isInviteLike(e.drop.data)) pp::ui::online::setPendingInvite(e.drop.data);
      break;
    case SDL_EVENT_MOUSE_MOTION:
      if (consoleHelp(L) && (e.motion.xrel || e.motion.yrel)) app.pad = {};
      in.type = Input::MouseMove; in.wx = e.motion.x; in.wy = e.motion.y; have = true; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      app.pad = {};
      in.type = Input::MouseDown; in.wx = e.button.x; in.wy = e.button.y; in.button = e.button.button; in.clicks = e.button.clicks; have = true; break;
    case SDL_EVENT_MOUSE_BUTTON_UP: in.type = Input::MouseUp; in.wx = e.button.x; in.wy = e.button.y; in.button = e.button.button; have = true; break;
    case SDL_EVENT_MOUSE_WHEEL:
      if (consoleHelp(L) && (e.wheel.x || e.wheel.y)) app.pad = {};
      in.type = Input::Wheel; in.wheel = e.wheel.y; have = true; break;
    case SDL_EVENT_KEY_DOWN:
      app.pad = {};
      in.type = Input::KeyDown; in.key = (int)e.key.key; in.mod = (int)e.key.mod;
      have = !e.key.repeat || e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT || e.key.key == SDLK_BACKSPACE || e.key.key == SDLK_DELETE;
      break;
    case SDL_EVENT_KEY_UP: in.type = Input::KeyUp; in.key = (int)e.key.key; in.mod = (int)e.key.mod; have = true; break;
    case SDL_EVENT_TEXT_INPUT: in.type = Input::Text; in.text = e.text.text; have = true; break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: in.type = Input::FocusLost; have = true; break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: in.type = Input::FocusGained; have = true; break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
      L.hidden = true;
      in.type = Input::FocusLost;
      have = true;
      break;
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_EXPOSED: L.hidden = (SDL_GetWindowFlags(L.win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0; break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: in.type = Input::MouseLeave; have = true; break;
    case SDL_EVENT_GAMEPAD_ADDED: if (auto g = SDL_OpenGamepad(e.gdevice.which)) L.pads.push_back(g); break;
    default: break;
  }
  if (!have) return;
  if (in.type == Input::MouseMove || in.type == Input::MouseDown || in.type == Input::MouseUp) gfx.toCanvas(in.wx, in.wy, in.x, in.y);
  else if (in.type == Input::Wheel) { in.x = app.mouseX; in.y = app.mouseY; }
  app.input(in);
}

static void pumpEvents(Live& L) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    Busy busy(L);
    handleEvent(L, e);
  }
}

static void advance(Live& L, Pacer& P, uint64_t now) {
  if (!P.last) P.last = now;
  P.acc = std::min(P.acc + (double)(now - P.last) / 1e6, 200.0);
  P.last = now;
  while (P.acc >= 10.0 && !L.app->wantsQuit()) {
    P.acc -= 10.0;
    L.app->tick();
    if (L.leftTicks > 0) {
      if (Audio* left = L.window->leftSound()) left->tickMusic();
      if (--L.leftTicks == 0) L.window->dropLeftGame();
    }
  }
}

static void showFrame(Live& L, const Pacer& P) {
  L.app->renderAlpha = (float)(P.acc / 10.0);
  L.app->frame();
  SDL_RenderPresent(L.ren);
}

static void paceOnce(Live& L, Pacer& P) {
  pumpEvents(L);
  Busy busy(L);
  L.hidden = (SDL_GetWindowFlags(L.win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
  uint64_t now = SDL_GetTicksNS();
  advance(L, P, now);
  if (L.hidden) {
    SDL_Delay(16);
    return;
  }
  showFrame(L, P);
  double ms = (double)(SDL_GetTicksNS() - now) / 1e6;
  if (ms < 2.0) SDL_DelayNS((uint64_t)((2.0 - ms) * 1e6));
}

static bool redrawWhileResizing(void* userdata, SDL_Event* e) {
  if (e->type != SDL_EVENT_WINDOW_EXPOSED) return true;
  Live& L = *static_cast<Live*>(userdata);
  if (L.busy > 0 || L.redrawing || !L.app || !L.pacer || L.app->wantsQuit()) return true;
  if (SDL_GetCurrentThreadID() != L.thread || e->window.windowID != SDL_GetWindowID(L.win)) return true;
  int w = 0, h = 0;
  if ((SDL_GetWindowFlags(L.win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0) return true;
  if (!SDL_GetWindowSizeInPixels(L.win, &w, &h) || w <= 0 || h <= 0) return true;
  L.redrawing = true;
  advance(L, *L.pacer, SDL_GetTicksNS());
  showFrame(L, *L.pacer);
  L.redrawing = false;
  return true;
}

}

static SDL_Cursor* makeCursor(const Vfs& vfs, const char* path, int hx, int hy) {
  auto img = loadGameImage(vfs, path);
  if (!img || img->rgba.empty()) return nullptr;
  int x0 = img->w, y0 = img->h, x1 = -1, y1 = -1;
  for (int y = 0; y < img->h; y++)
    for (int x = 0; x < img->w; x++)
      if (img->rgba[((size_t)y * img->w + x) * 4 + 3] > 8) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
  if (x1 < 0) return nullptr;
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  std::vector<uint8_t> px((size_t)w * h * 4);
  for (int y = 0; y < h; y++) std::memcpy(&px[(size_t)y * w * 4], &img->rgba[((size_t)(y0 + y) * img->w + x0) * 4], (size_t)w * 4);
  SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
  if (!surf) return nullptr;
  SDL_Cursor* c = SDL_CreateColorCursor(surf, hx, hy);
  SDL_DestroySurface(surf);
  return c;
}

static SDL_Surface* makeWindowIcon(Game game) {
  const int N = 64;
  SDL_Surface* s = SDL_CreateSurface(N, N, SDL_PIXELFORMAT_RGBA32);
  if (!s) return nullptr;
  uint8_t* px = (uint8_t*)s->pixels;
  const float cr = game == Game::Deluxe ? 1.0f : 0.62f, cg = game == Game::Deluxe ? 0.5f : 0.3f, cb = game == Game::Deluxe ? 0.1f : 0.95f;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float dx = x + 0.5f - N / 2, dy = y + 0.5f - N / 2, d = std::sqrt(dx * dx + dy * dy);
      float r = 0, g = 0, b = 0, a = 0;
      if (d < 30.5f) {
        a = std::min(1.0f, 30.5f - d);
        r = 0.08f; g = 0.1f; b = 0.25f;
      }
      if (d < 21.5f) {
        float hx = dx + 7, hy = dy + 8, h = std::exp(-(hx * hx + hy * hy) / 90.0f);
        float k = 0.55f + 0.45f * (1.0f - d / 22.0f);
        r = std::min(1.0f, cr * k + h); g = std::min(1.0f, cg * k + h); b = std::min(1.0f, cb * k + h);
        a = std::min(1.0f, 21.5f - d + (a > 0 ? 1.0f : 0.0f));
      }
      uint8_t* p = px + y * s->pitch + x * 4;
      p[0] = (uint8_t)(r * 255); p[1] = (uint8_t)(g * 255); p[2] = (uint8_t)(b * 255); p[3] = (uint8_t)(std::clamp(a, 0.0f, 1.0f) * 255);
    }
  return s;
}

static bool bothInstalled(const RunOptions& o) { return o.hasGame && o.hasGame(o.game) && o.hasGame(otherGame(o.game)); }

static Game homeOf(const RunOptions& o) { return bothInstalled(o) && o.home ? o.home() : o.game; }

static RunResult playWindow(const RunOptions& o, Vfs& vfs, GameWindow& window, bool freshWindow, GameRequest& next) {
  SDL_Window* win = window.window();
  SDL_Renderer* ren = window.renderer();
  RunResult result = RunResult::Quit;
  SDL_Cursor* customCursor = nullptr;
  std::unique_ptr<Audio> audio = std::make_unique<Audio>(vfs);
  if (!audio->openDevice()) audio.reset();
  {
    Renderer board(ren, vfs);
    Gfx gfx(ren, vfs);
    App::Config cfg;
    cfg.game = o.game;
    cfg.start = o.start;
    cfg.bothGames = bothInstalled(o);
    cfg.home = homeOf(o);
    if (cfg.bothGames && o.gameDir) cfg.otherDir = o.gameDir(otherGame(o.game));
    cfg.seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
    App app(gfx, vfs, audio.get(), &board, cfg);
    app.homeHook = [&](Game g) {
      if (o.setHome) o.setHome(g);
    };
    app.setFullscreenHook = [&](bool on) { SDL_SetWindowFullscreen(win, on); };
    customCursor = makeCursor(vfs, "images/cursor_pointer", 0, 0);
    app.showCursorHook = [&](bool custom) { SDL_SetCursor(custom && customCursor ? customCursor : SDL_GetDefaultCursor()); };
    app.showCursorHook(app.settings.customCursors);
    window.dropCursor();
    app.modAssetsHook = [&] {
      SDL_Cursor* old = customCursor;
      customCursor = makeCursor(vfs, "images/cursor_pointer", 0, 0);
      app.showCursorHook(app.settings.customCursors);
      if (old) SDL_DestroyCursor(old);
    };
    if (!freshWindow) app.settings.fullscreen = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
    else if (app.settings.fullscreen) SDL_SetWindowFullscreen(win, true);
    bool pictureFillsWindow = false;
    SDL_Surface* picture = window.takePicture(pictureFillsWindow);
    if (o.start.handOver) {
      app.startAt(o.start, picture, pictureFillsWindow);
      app.frame();
    } else {
      app.showLoadingScreen();
    }
    if (picture) SDL_DestroySurface(picture);

    Live L;
    L.win = win;
    L.ren = ren;
    L.app = &app;
    L.gfx = &gfx;
    L.window = &window;
    L.leftTicks = window.leftSound() ? 110 : 0;
    if (!L.leftTicks) window.dropLeftGame();
    Pacer P;
    L.pacer = &P;
    L.thread = SDL_GetCurrentThreadID();
    SDL_AddEventWatch(redrawWhileResizing, &L);
    if (o.setGame) {
      o.setGame(&app, [&](const std::function<bool()>& done) {
        P.last = 0;
        while (!app.wantsQuit() && !done()) paceOnce(L, P);
        P.last = 0;
      });
    }
    bool told = !(o.start.handOver && o.start.askedAt);
    while (!app.wantsQuit()) {
      paceOnce(L, P);
      if (!told) {
        told = true;
        log::info("start", "Switched to " + app.titleText() + " in " + std::to_string((SDL_GetTicksNS() - o.start.askedAt) / 1000000) + " ms");
      }
      GameRequest wanted;
      if (app.takeGameSwitchRequest(wanted)) {
        if (!o.canOpen || o.canOpen(wanted.game)) {
          next = wanted;
          result = RunResult::SwitchGame;
          app.frame();
          window.keepPicture(SDL_RenderReadPixels(ren, nullptr), app.pictureFillsWindow());
          app.playMusic("fadeout");
          app.requestQuit();
        } else {
          app.gameSwitchRefused(wanted);
        }
        P.last = 0;
      }
      if (app.takeGameFoldersRequest() && o.chooseFolders) {
        if (o.chooseFolders(o.game)) {
          app.finishOptionsDialog(true, false);
          next = GameRequest();
          next.game = o.game;
          result = RunResult::SwitchGame;
          app.requestQuit();
        } else {
          app.setGames(bothInstalled(o), homeOf(o));
        }
        P.last = 0;
      }
    }

    SDL_RemoveEventWatch(redrawWhileResizing, &L);
    if (o.setGame) o.setGame(nullptr, nullptr);
    if (!(SDL_GetWindowFlags(win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED))) SDL_GetWindowSize(win, &app.settings.winW, &app.settings.winH);
    app.shutdown();
    for (SDL_Gamepad* g : L.pads) SDL_CloseGamepad(g);
  }
  window.dropLeftGame();
  if (result == RunResult::SwitchGame && next.start.handOver) {
    window.keepSound(std::move(audio));
    window.keepCursor(customCursor);
  } else {
    SDL_SetCursor(SDL_GetDefaultCursor());
    if (customCursor) SDL_DestroyCursor(customCursor);
  }
  return result;
}

static void keepPendingInvites() {
  SDL_PumpEvents();
  SDL_Event e;
  while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_TEXT) > 0)
    if (e.drop.data && pp::online::isInviteLike(e.drop.data)) pp::ui::online::setPendingInvite(e.drop.data);
}

bool GameWindow::open(Game game, int w, int h, bool keepPicture, std::string& error) {
  const char* title = game == Game::Deluxe ? "Peggle Deluxe (PegglePort)" : "Peggle Nights (PegglePort)";
  if (!win_) {
    SDL_Rect area;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area) && area.w > 0 && area.h > 0) {
      w = std::min(w, area.w);
      h = std::min(h, area.h);
    }
    if (!SDL_CreateWindowAndRenderer(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win_, &ren_)) {
      error = std::string("The game window could not be opened: ") + SDL_GetError();
      win_ = nullptr;
      ren_ = nullptr;
      return false;
    }
    SDL_SetRenderVSync(ren_, 1);
    SDL_StartTextInput(win_);
  } else {
    SDL_SetWindowTitle(win_, title);
    if (!keepPicture) {
      SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
      SDL_RenderClear(ren_);
      SDL_RenderPresent(ren_);
    }
  }
  if (SDL_Surface* icon = makeWindowIcon(game)) { SDL_SetWindowIcon(win_, icon); SDL_DestroySurface(icon); }
  return true;
}

void GameWindow::keepPicture(SDL_Surface* picture, bool fillsWindow) {
  if (picture_) SDL_DestroySurface(picture_);
  picture_ = picture;
  pictureFillsWindow_ = fillsWindow;
}

SDL_Surface* GameWindow::takePicture(bool& fillsWindow) {
  SDL_Surface* picture = picture_;
  fillsWindow = pictureFillsWindow_;
  picture_ = nullptr;
  return picture;
}

void GameWindow::keepSound(std::unique_ptr<Audio> sound) { leftSound_ = std::move(sound); }

void GameWindow::keepFiles(std::shared_ptr<Vfs> files) { leftFiles_ = std::move(files); }

void GameWindow::dropLeftGame() {
  leftSound_.reset();
  leftFiles_.reset();
}

void GameWindow::keepCursor(SDL_Cursor* cursor) {
  dropCursor();
  leftCursor_ = cursor;
}

void GameWindow::dropCursor() {
  if (!leftCursor_) return;
  if (SDL_GetCursor() == leftCursor_) SDL_SetCursor(SDL_GetDefaultCursor());
  SDL_DestroyCursor(leftCursor_);
  leftCursor_ = nullptr;
}

void GameWindow::close() {
  dropLeftGame();
  dropCursor();
  keepPicture(nullptr);
  if (ren_) SDL_DestroyRenderer(ren_);
  if (win_) SDL_DestroyWindow(win_);
  ren_ = nullptr;
  win_ = nullptr;
}

RunResult runApp(const RunOptions& o, Vfs& vfs, GameWindow& window, GameRequest& next, std::string& error) {
  int w = o.winW, h = o.winH;
  Settings remembered;
  if (remembered.load(defaultUserDir(o.game).parent_path() / "settings.ini") && remembered.winW >= 640 && remembered.winH >= 480) {
    w = remembered.winW;
    h = remembered.winH;
  }
  keepPendingInvites();
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  bool freshWindow = !window.isOpen();
  if (!window.open(homeOf(o), w, h, o.start.handOver, error)) return RunResult::Failed;
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  RunResult result = playWindow(o, vfs, window, freshWindow, next);
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  return result;
}

}
