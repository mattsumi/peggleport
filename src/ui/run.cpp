#include "ui/run.h"

#include "image.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "audio.h"
#include "online/invite.h"
#include "online/online_ui.h"
#include "render.h"

namespace pp::ui {

namespace {

struct Live {
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  App* app = nullptr;
  Gfx* gfx = nullptr;
  std::vector<SDL_Gamepad*> pads;
  bool hidden = false;
};

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
  switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
      bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
      switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: key(SDLK_UP, down); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: key(SDLK_DOWN, down); break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: if (down) key(SDLK_LEFT, true); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: if (down) key(SDLK_RIGHT, true); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: mouse(1, down); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: mouse(3, down); break;
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_START: if (down) key(SDLK_ESCAPE, true); break;
        default: break;
      }
      return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      int v = e.gaxis.value;
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
    case SDL_EVENT_MOUSE_MOTION: in.type = Input::MouseMove; in.wx = e.motion.x; in.wy = e.motion.y; have = true; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      in.type = Input::MouseDown; in.wx = e.button.x; in.wy = e.button.y; in.button = e.button.button; in.clicks = e.button.clicks; have = true; break;
    case SDL_EVENT_MOUSE_BUTTON_UP: in.type = Input::MouseUp; in.wx = e.button.x; in.wy = e.button.y; in.button = e.button.button; have = true; break;
    case SDL_EVENT_MOUSE_WHEEL: in.type = Input::Wheel; in.wheel = e.wheel.y; have = true; break;
    case SDL_EVENT_KEY_DOWN:
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
  while (SDL_PollEvent(&e)) handleEvent(L, e);
}

struct Pacer {
  uint64_t last = 0;
  double acc = 0;
};

static void paceOnce(Live& L, Pacer& P) {
  pumpEvents(L);
  L.hidden = (SDL_GetWindowFlags(L.win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
  uint64_t now = SDL_GetTicksNS();
  if (!P.last) P.last = now;
  P.acc = std::min(P.acc + (double)(now - P.last) / 1e6, 200.0);
  P.last = now;
  while (P.acc >= 10.0 && !L.app->wantsQuit()) {
    P.acc -= 10.0;
    L.app->tick();
  }
  if (L.hidden) {
    SDL_Delay(16);
    return;
  }
  L.app->renderAlpha = (float)(P.acc / 10.0);
  L.app->frame();
  SDL_RenderPresent(L.ren);
  double ms = (double)(SDL_GetTicksNS() - now) / 1e6;
  if (ms < 2.0) SDL_DelayNS((uint64_t)((2.0 - ms) * 1e6));
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

static RunResult playWindow(Vfs& vfs, Game game, SDL_Window* win, SDL_Renderer* ren) {
  RunResult result = RunResult::Quit;
  SDL_Cursor* customCursor = nullptr;
  {
    Renderer board(ren, vfs);
    Gfx gfx(ren, vfs);
    std::unique_ptr<Audio> audio = std::make_unique<Audio>(vfs);
    if (!audio->openDevice()) audio.reset();
    App::Config cfg;
    cfg.game = game;
    cfg.seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
    App app(gfx, vfs, audio.get(), &board, cfg);
    app.setFullscreenHook = [&](bool on) { SDL_SetWindowFullscreen(win, on); };
    customCursor = makeCursor(vfs, "images/cursor_pointer", 0, 0);
    app.showCursorHook = [&](bool custom) { SDL_SetCursor(custom && customCursor ? customCursor : SDL_GetDefaultCursor()); };
    app.showCursorHook(app.settings.customCursors);
    if (app.settings.fullscreen) SDL_SetWindowFullscreen(win, true);
    app.showLoadingScreen();

    Live L;
    L.win = win;
    L.ren = ren;
    L.app = &app;
    L.gfx = &gfx;
    Pacer P;
    while (!app.wantsQuit()) paceOnce(L, P);

    if (!(SDL_GetWindowFlags(win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED))) SDL_GetWindowSize(win, &app.settings.winW, &app.settings.winH);
    app.shutdown();
    if (app.gameSwitchRequested()) result = RunResult::SwitchGame;
    for (SDL_Gamepad* g : L.pads) SDL_CloseGamepad(g);
  }
  SDL_SetCursor(SDL_GetDefaultCursor());
  if (customCursor) SDL_DestroyCursor(customCursor);
  return result;
}

RunResult runApp(const RunOptions& o, Vfs& vfs, std::string& error) {
  Game game = o.game;
  const char* title = game == Game::Deluxe ? "Peggle Deluxe (PegglePort)" : "Peggle Nights (PegglePort)";
  int w = o.winW, h = o.winH;
  Settings remembered;
  if (remembered.load(defaultUserDir(game).parent_path() / "settings.ini") && remembered.winW >= 640 && remembered.winH >= 480) {
    w = remembered.winW;
    h = remembered.winH;
  }
  SDL_Rect area;
  if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area) && area.w > 0 && area.h > 0) {
    w = std::min(w, area.w);
    h = std::min(h, area.h);
  }
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  if (!SDL_CreateWindowAndRenderer(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
    error = std::string("The game window could not be opened: ") + SDL_GetError();
    return RunResult::Failed;
  }
  SDL_SetRenderVSync(ren, 1);
  if (SDL_Surface* icon = makeWindowIcon(game)) { SDL_SetWindowIcon(win, icon); SDL_DestroySurface(icon); }
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  RunResult result = playWindow(vfs, game, win, ren);
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  return result;
}

}
