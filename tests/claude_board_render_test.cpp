#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include "image.h"
#include "ui/game_screen.h"
#include "ui/run.cpp"

pp::ui::PadGlyphs pp::ui::readPad(SDL_Gamepad*) { return {Pad::PlayStation, {}}; }
using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;

static int run(const fs::path& path, const fs::path& art, const fs::path& captures, Game game, bool wide) {
  Vfs v;
  if (!v.mountGame(path, nullptr) || !v.mountPresentation(art)) return 1;
  const int w = wide ? 1920 : 800, h = wide ? 1080 : 600;
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  if (!surface) return 1;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> ren(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  if (!ren) return 1;
  Gfx g(ren.get(), v);
  Renderer draw(ren.get(), v);
  App::Config cfg;
  cfg.game = game;
  cfg.noRoomDiscovery = cfg.noSave = cfg.noAudio = true;
  cfg.userDir = fs::current_path() / ("claude-profile-" + std::to_string(SDL_GetPerformanceCounter()));
  cfg.seed = 1;
  cfg.widescreen = static_cast<int>(wide ? ui::WidescreenMode::W16x9 : ui::WidescreenMode::Classic4x3);
  PlayerInfo player;
  player.name = "Claude Test";
  App app(g, v, nullptr, &draw, cfg);
  app.curProfile = &player;
  app.gameMode = GameMode::Adventure;
  app.curLevel = 0;
  app.curStage = player.maxStage = game == Game::Nights ? 5 : 4;
  app.showBoard(true, true);
  auto* screen = dynamic_cast<GameScreen*>(app.screen());
  if (!screen || !screen->ok()) return 1;
  auto tick = [&](int n) { while (n-- > 0) app.tick(); };
  tick(35);
  if (auto* d = app.getDialog(D_Tip)) { app.dialogButton(d->dialogId, ID_YES); tick(35); }
  if (screen->paused()) return 1;
  const std::string tag = std::string(game == Game::Nights ? "nights-" : "deluxe-") + (wide ? "wide-" : "classic-");
  int errors = 0;
  auto check = [&](bool yes, const char* msg) { if (!yes) { ++errors; std::cerr << tag << msg << '\n'; } };
  auto take = [&](const std::string& name) {
    SDL_RenderPresent(ren.get());
    if (!captures.empty()) check(SDL_SaveBMP(surface.get(), (captures / (tag + name + ".bmp")).string().c_str()), "capture");
  };
  auto frame = [&](const char* name) { app.frame(); take(name); };
  Board& board = screen->board();
  Board pose = board;
  pose.gunAngle = 5.5f;
  auto portrait = [&](int power, const std::string& name) {
    pose.master[0] = power;
    draw.setFrameCanvas(screen->canvasWidth());
    draw.draw(pose, screen->bundle(), Hud{});
    take(name);
  };
  for (int power : {Board::P_MULTIBALL, Board::P_PYRAMID, Board::P_SPACEBLAST, Board::P_SPOOKY, Board::P_ZEN,
                    Board::P_FLOWER, Board::P_FIREBALL, Board::P_LUCKYSPIN, Board::P_CHAIN}) {
    if (power == Board::P_CHAIN && game != Game::Nights) continue;
    portrait(power, "control-" + std::to_string(power));
  }
  portrait(Board::P_GUIDE, "bjorn");
  auto win = [&](float bx, float by) {
    const V2 a = draw.toBoard(0, 0), b = draw.toBoard((float)w, (float)h);
    return SDL_Point{(int)std::floor((bx - a.x) * w / (b.x - a.x)), (int)std::floor((by - a.y) * h / (b.y - a.y))};
  };
  auto pixel = [&](SDL_Point p) { return (const Uint8*)surface->pixels + p.y * surface->pitch + 4 * p.x; };
  const auto atlas = loadGameImage(v, "characters/Lobster");
  if (!atlas || atlas->w != 62 || atlas->h != 240) return 1;
  for (int f = 0; f < 3; ++f) {
    pose.tick += 20;
    portrait(Board::P_FLIPPERS, "face" + std::to_string(f));
    const auto view = screen->consoleView();
    const int inset = Renderer::hudInset(screen->canvasWidth(), board.W);
    const float scale = std::min((float)w / screen->canvasWidth(), h / 600.f);
    const float k = scale * view.k;
    const SDL_FRect dst{(w - screen->canvasWidth() * scale) * .5f + view.x * scale + inset * k + (400 - atlas->w * .5f) * k,
                        (h - 600 * scale) * .5f + view.y * scale + 34 * k, atlas->w * k, 80 * k};
    std::vector<SDL_Point> points;
    std::vector<std::array<Uint8, 3>> observed;
    for (int y = 41; y < 65; ++y) for (int x = 17; x < 45; ++x) {
      const auto* color = &atlas->rgba[4 * ((f * 80 + y) * 62 + x)];
      if (color[3] < 250) continue;
      points.push_back(win(325 - 31 + x + .5f, 34 - 40 + y + .5f));
      const auto* c = pixel(points.back());
      observed.push_back({c[0], c[1], c[2]});
    }
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> ref(SDL_CreateTexture(ren.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 62, 80), SDL_DestroyTexture);
    if (!ref) return 1;
    SDL_UpdateTexture(ref.get(), nullptr, &atlas->rgba[4 * 62 * 80 * f], 4 * 62);
    SDL_SetTextureScaleMode(ref.get(), SDL_SCALEMODE_LINEAR);
    const SDL_Rect clip{(int)std::floor(dst.x - (400 - 31 - 352) * k), (int)std::floor(dst.y - 10 * k),
                        (int)std::ceil(91 * k), (int)std::ceil(88 * k)};
    SDL_SetRenderClipRect(ren.get(), &clip);
    SDL_RenderTexture(ren.get(), ref.get(), nullptr, &dst);
    SDL_SetRenderClipRect(ren.get(), nullptr);
    SDL_RenderPresent(ren.get());
    long sum = 0;
    for (size_t i = 0; i < points.size(); ++i) for (int k = 0; k < 3; ++k)
      sum += std::abs(observed[i][k] - pixel(points[i])[k]);
    const double error = (double)sum / (3 * points.size());
    std::cout << tag << "face=" << f << " RGB mean error=" << error << '\n';
    check(points.size() > 500 && sum == 0, "centered unscaled atlas cell behind orb");
  }
  check(board.master[0] == Board::P_FLIPPERS, "stage chooses Claude");
  auto green = std::find_if(board.objs.begin(), board.objs.end(), [](const Obj& o) { return o.visible && o.pegType == 4; });
  if (green == board.objs.end()) return 1;
  board.pegHit((int)(green - board.objs.begin()));
  if (board.turns[0] != 3) return 1;
  frame("active");
  Live live;
  live.app = &app; live.gfx = &g; live.ren = ren.get();
  const SDL_Point cursor = wide ? SDL_Point{1415, 865} : SDL_Point{665, 485};
  auto mouse = [&](bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = (float)cursor.x; e.button.y = (float)cursor.y;
    handleEvent(live, e);
  };
  auto pad = [&](bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    handleEvent(live, e);
  };
  const auto target = draw.toBoard((float)cursor.x, (float)cursor.y);
  const int start = board.frames;
  mouse(true); mouse(false);
  check(std::abs(board.mouseX - target.x) < 2 && std::abs(board.mouseY - target.y) < 2 && target.x > 550, "mouse targets right side of physical board");
  for (int n = 0; n < 100 && board.phase != Phase::Shot; ++n) tick(1);
  if (board.phase != Phase::Shot) return 1;
  auto clawInk = [&]() {
    std::array<int, 2> count{};
    for (int i = 0; i < 2; ++i) {
      for (int y = 418; y < 464; ++y) for (int x = 18; x < 79; ++x) {
        const auto* c = pixel(win((float)(i ? 650 - x : x), (float)y));
        count[i] += c[0] > 125 && c[0] > 1.7 * c[1] && c[0] > 2 * c[2];
      }
    }
    return count;
  };
  const int before = board.frames;
  pad(true); tick(18); frame("held");
  const auto held = clawInk();
  check(board.flipHeld && held[0] > 400 && held[1] > 400, "South down raises both visible claws");
  pad(false); tick(19); frame("released");
  const auto rest = clawInk();
  check(!board.flipHeld && rest[0] < held[0] / 2 && rest[1] < held[1] / 2, "South up lowers both visible claws");
  mouse(true); mouse(false); tick(34); frame("quick-tap");
  check(clawInk() == rest && !board.flipHeld, "same-frame mouse tap does not stick raised");
  for (int n = 0; n < 5000 && board.phase != Phase::Aim; ++n) tick(1);
  if (board.phase != Phase::Aim) return 1;
  screen->keyDown(SDLK_R, 0);
  if (!screen->replaying()) return 1;
  tick(before - start + 18); frame("replay-held");
  const auto seen = clawInk();
  check(seen == held, "instant replay reproduces both raised silhouettes");
  tick(19); frame("replay-released");
  check(clawInk() == rest, "instant replay reproduces both released silhouettes");
  std::cout << tag << "live=" << held[0] << ',' << held[1] << " replay=" << seen[0] << ',' << seen[1] << " rest=" << rest[0] << ',' << rest[1] << " errors=" << errors << '\n';
  return errors;
}

int main(int argc, char** argv) {
  if ((argc != 4 && argc != 5) || !SDL_Init(0)) return 2;
  int errors = 0;
  for (bool wide : {false, true}) for (int e = 0; e < 2; ++e)
    errors += run(argv[e + 1], argv[3], argc == 5 ? fs::path(argv[4]) : fs::path{}, e ? Game::Nights : Game::Deluxe, wide);
  SDL_Quit();
  std::cout << "CLAUDE_RENDER errors=" << errors << '\n';
  return errors ? 1 : 0;
}
