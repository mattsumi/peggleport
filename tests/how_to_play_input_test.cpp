#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include "ui/dialogs.h"
#include "ui/run.cpp"

using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;

static PadGlyphs device;
PadGlyphs pp::ui::readPad(SDL_Gamepad*) { return device; }

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

struct HelpFont : FontProvider {
  std::unique_ptr<FontProvider> font;
  std::string words;
  struct Token { float cx, y; std::string word; };
  std::vector<Token> tokens;
  explicit HelpFont(Gfx& g) : font(makeDefaultFontProvider(g.renderer(), &g)) {}
  float height(const std::string& n) override { return font->height(n); }
  float ascent(const std::string& n) override { return font->ascent(n); }
  float ascentPadding(const std::string& n) override { return font->ascentPadding(n); }
  float lineSpacing(const std::string& n) override { return font->lineSpacing(n); }
  float width(const std::string& n, const std::string& s) override { return font->width(n,s); }
  bool layerColors(const std::string& n, size_t i, uint32_t& m, uint32_t& a) override { return font->layerColors(n,i,m,a); }
  void setLayerColors(const std::string& n, size_t i, uint32_t m, uint32_t a) override { font->setLayerColors(n,i,m,a); }
  void draw(const std::string& n, float x, float y, const std::string& s, uint32_t rgb, float alpha) override {
    words += s + " ";
    if (n == "OVERLOAD10OUTLINE" || n == "OVERLOAD10") tokens.push_back({x + font->width(n,s)/2, y, s});
    font->draw(n,x,y,s,rgb,alpha);
  }
};

static void exercise(Game edition, const fs::path& gameDir, const fs::path& art, const fs::path& scratch,
                     int width, int height, bool classic, bool boardOpener) {
  Vfs vfs;
  check(vfs.mountGame(gameDir,nullptr), "game mount");
  if (!art.empty()) check(vfs.mountPresentation(art), "presentation mount");
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
  check(renderer != nullptr, "software renderer");
  Gfx gfx(renderer.get(),vfs);
  auto font = std::make_unique<HelpFont>(gfx);
  auto& text = font->words;
  auto& tokens = font->tokens;
  gfx.setFontProvider(std::move(font));
  Renderer view(renderer.get(),vfs);
  PlayerInfo player;
  player.name = "Help Test";
  App::Config cfg;
  cfg.game = edition;
  cfg.userDir = scratch / "isolated-profile";
  cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = cfg.fastBoot = true;
  cfg.widescreen = static_cast<int>(classic ? pp::ui::WidescreenMode::Classic4x3 : pp::ui::WidescreenMode::W16x9);
  cfg.seed = 19;
  App app(gfx,vfs,nullptr,&view,cfg);
  app.curProfile = &player;
  Live live;
  live.ren = renderer.get(); live.app = &app; live.gfx = &gfx;
  const auto settle = [&] { for (int i = 0; i < 110; ++i) app.tick(); };
  const auto frame = [&](const std::string& tag = {}) {
    text.clear(); tokens.clear(); app.frame(); SDL_RenderPresent(renderer.get());
    if (const char* path = SDL_getenv("PP_HELP_CAPTURE"); path && !tag.empty()) {
      std::string name = (edition == Game::Deluxe ? "deluxe-" : "nights-") + std::to_string(width) + "x" + std::to_string(height) +
                         (classic ? "-classic-" : art.empty() ? "-fallback-" : boardOpener ? "-board-" : "-menu-") + tag + ".bmp";
      check(SDL_SaveBMP(surface.get(),(fs::path(path)/name).string().c_str()), "capture");
    }
  };
  const auto mouse = [&](Uint32 type, float x, float y) {
    SDL_Event e{}; e.type = type;
    const float wx = gfx.offX() + (x + (gfx.canvasW()-800)*0.5f)*gfx.scale(), wy = gfx.offY() + y*gfx.scale();
    if (type == SDL_EVENT_MOUSE_MOTION) { e.motion.x = wx; e.motion.y = wy; e.motion.xrel = 1; }
    else { e.button.button = 1; e.button.x = wx; e.button.y = wy; }
    handleEvent(live,e);
  };
  const auto click = [&](float x, float y) { mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,x,y); mouse(SDL_EVENT_MOUSE_BUTTON_UP,x,y); };
  const auto key = [&](SDL_Keycode k) { SDL_Event e{}; e.key.key = k; e.type = SDL_EVENT_KEY_DOWN; handleEvent(live,e); e.type = SDL_EVENT_KEY_UP; handleEvent(live,e); };
  const auto pad = [&](SDL_GamepadButton b, bool down) { SDL_Event e{}; e.gbutton.button = b; e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP; handleEvent(live,e); };
  const auto axis = [&](SDL_GamepadAxis a, Sint16 value) { SDL_Event e{}; e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION; e.gaxis.axis = a; e.gaxis.value = value; handleEvent(live,e); };
  const auto press = [&](SDL_GamepadButton b) { pad(b,true); pad(b,false); };
  app.showMainMenu(); settle(); frame();
  if (boardOpener) {
    player.storyPending = false; app.startAdventureGame(); settle();
    if (app.getDialog(D_Tip)) { app.killDialog(D_Tip); settle(); }
    auto* game = dynamic_cast<GameScreen*>(app.screen());
    check(game && game->ok() && !app.anyDialog() && !game->paused(), "Adventure opener");
    key(SDLK_ESCAPE);
  } else app.doOptionsDialog();
  settle(); frame();
  auto* parent = dynamic_cast<OptionsDialog*>(app.topDialog());
  auto* opener = app.screen();
  check(parent && parent->fromGame == boardOpener, "Options opener");
  const auto open = [&] {
    auto& w = *parent->help; click(parent->x+w.x+w.w/2,parent->y+w.y+w.h/2); settle(); frame();
    auto* help = dynamic_cast<HelpScreen*>(app.helpOverlay()); check(help != nullptr,"open via visible Help button"); return help;
  };
  HelpScreen* help = open();
  const int pages = edition == Game::Nights ? 13 : 8;
  const char* names[] = {"Controls", "Pegs", "Scoring", "Free Balls", "Game Modes", "Duel Mode", "Instant Replays", "Extra Tidbits", "Style Shots", "More Style Shots", "Still More Style Shots", "Master-Specific Style Shots", "Even More Master-Specific Style Shots"};
  for (int i=0; i<pages; ++i) {
    frame("page-" + std::to_string(i+1));
    check(help->page == i && text.find(names[i]) != std::string::npos && text.find("Page " + std::to_string(i+1) + " of " + std::to_string(pages)) != std::string::npos, "page title and count");
    if (i == 6) check(text.find("skip the replay") != std::string::npos && text.find("slow motion") == std::string::npos, "Replay page advertises slow motion instead of click to skip");
    auto& w = *help->findWidget(2); click(w.x+w.w-2,w.y+w.h/2);
  }
  check(help->page == 0,"next wrap");
  key(SDLK_LEFT); check(help->page == pages-1,"previous wrap"); key(SDLK_RIGHT);
  if (!classic) {
    device = {Pad::PlayStation,{}};
    press(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    check(help->page == pages-1,"horizontal D-pad ignored in Help");
    press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    for (int i = 0; i < 6; ++i) press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    frame("replay-pad");
    check(help->page == 6 && text.find("skip the replay") != std::string::npos && text.find("Save") != std::string::npos && text.find("click") == std::string::npos && text.find("mouse") == std::string::npos, "Replay page shows mouse instructions on controller");
    for (int i = 0; i < 6; ++i) press(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    key(SDLK_F1);
    const auto inside = [&](int id, bool right) { auto& w = *help->findWidget(id); click(w.x+(right ? w.w-2 : 2),w.y+w.h/2); };
    auto& next = *help->findWidget(2);
    click(next.x+next.w+2,next.y+next.h/2); check(help->page == 0,"dead rail");
    mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,next.x+4,next.y+8); frame(); mouse(SDL_EVENT_MOUSE_BUTTON_UP,next.x+next.w+8,next.y+8);
    check(help->page == 0,"drag off Next");
    inside(1,false); check(help->page == pages-1,"previous left edge"); inside(2,true);
    for (auto [kind,name] : {std::pair{Pad::PlayStation,"PlayStation"}, {Pad::Xbox,"Xbox"}, {Pad::SteamDeck,"Steam Deck"}, {Pad::SteamController,"Steam Controller"}, {Pad::Nintendo,"Switch"}}) {
      device = {kind,{}};
      press(SDL_GAMEPAD_BUTTON_WEST); frame(name);
      check(text.find("Change Page") != std::string::npos && text.find(name) != std::string::npos && text.find("mouse cursor") == std::string::npos, "active pad Controls and grouped footer");
      const auto glyph = [&](float cx, float cy) {
        for (auto it = tokens.rbegin(); it != tokens.rend(); ++it)
          if (std::abs(it->cx-cx) < 2 && std::abs(it->y-cy) < 14) return it->word;
        return std::string{};
      };
      const bool sw = kind == Pad::Nintendo, ps = kind == Pad::PlayStation;
      const float cx = (gfx.canvasW()-800)*.5f + 168;
      check(glyph(cx,298) == (ps ? "R1" : sw ? "R" : "RB"),"shoulder row");
      if (!ps || art.empty()) {
        check(glyph(cx,217) == (ps ? "X" : sw ? "B" : "A"),"shoot row face");
        check(glyph(cx,379) == (ps ? "Tri" : sw ? "X" : "Y"),"replay row face");
        const char* east = ps ? "O" : sw ? "A" : "B";
        check(glyph(cx,460) == east && glyph(96,555) == east,"east face in Controls and Back");
      }
      const float socketX = help->findWidget(1)->x + 30;
      const float socketY = help->findWidget(1)->y + 24;
      mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,socketX+8,socketY); frame(); mouse(SDL_EVENT_MOUSE_BUTTON_UP,socketX+8,socketY);
      check(help->page == 1,"right half of the painted D-pad socket must advance");
      press(SDL_GAMEPAD_BUTTON_WEST); frame(); click(socketX-8,socketY);
      check(help->page == 0,"left half of the painted D-pad socket must go back");
      pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT,true); check(help->page == pages-1,"pad left at first page");
      pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT,true); pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT,false); check(help->page == pages-1,"one page per pad press");
      press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT); check(help->page == 0,"pad right wrap");
      press(SDL_GAMEPAD_BUTTON_DPAD_DOWN); press(SDL_GAMEPAD_BUTTON_SOUTH); check(help->page == 0 && app.helpOverlay() == help,"unbound pad press clicked a footer");
      mouse(SDL_EVENT_MOUSE_MOTION,400,200); frame();
      check(app.pad.pad == Pad::Keys && text.find("Previous") != std::string::npos && text.find("mouse") != std::string::npos,"mouse switch");
    }
    axis(SDL_GAMEPAD_AXIS_LEFTX,3000); check(app.pad.pad == Pad::Keys,"stick drift stole prompt");
    axis(SDL_GAMEPAD_AXIS_LEFTX,-18000); frame(); check(app.pad.pad == device.pad && help->page == 0,"intentional axis switch");
    axis(SDL_GAMEPAD_AXIS_LEFTX,0);
    SDL_Event wheel{}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = 1; handleEvent(live,wheel);
    check(app.pad.pad == Pad::Keys && help->page == 0,"wheel switch");
    if (!art.empty()) {
      const std::string file = (art / "console/menu/pad_glyphs.png").string();
      device = {Pad::Xbox,{file,file,file,file}};
      press(SDL_GAMEPAD_BUTTON_WEST); frame();
      auto* tile = gfx.imgFile(file);
      check(tile && tile->lw() > 0,"offline external glyph fixture");
      check(std::none_of(tokens.begin(),tokens.end(),[&](const HelpFont::Token& t) {
        return t.y > 195 && t.y < 480 && std::abs(t.cx-((gfx.canvasW()-800)*.5f+168)) < 2 && (t.word == "A" || t.word == "B" || t.word == "Y");
      }),"external glyphs fell through to letters");
    }
    press(SDL_GAMEPAD_BUTTON_WEST); key(SDLK_RIGHT); check(app.pad.pad == Pad::Keys && help->page == 1,"keyboard switch");
    pad(SDL_GAMEPAD_BUTTON_EAST,true); settle(); pad(SDL_GAMEPAD_BUTTON_EAST,false); frame();
    check(!app.helpOverlay() && app.topDialog() == parent && !parent->scrollingOff && app.screen() == opener,"pad back to opener");
    help = open();
    if (gfx.offY() > 1) {
      Uint8 r,g,b,a;
      check(SDL_ReadSurfacePixel(surface.get(),width/2,height-1,&r,&g,&b,&a) && (r|g|b) == 0,"bottom letterbox painted");
      auto& w = *help->findWidget(2); click(w.x+w.w/2,600 + 1/gfx.scale()); check(help->page == 0,"letterbox click");
    }
  }
  auto& back = *help->findWidget(0); click(back.x+back.w/2,back.y+back.h/2); settle(); frame();
  if (classic) {
    check(!app.helpOverlay() && !app.anyDialog() && app.screen() == opener,"Classic Back to menu");
    std::cout << (edition == Game::Deluxe ? "Deluxe" : "Nights") << " Classic pages=" << pages << " PASS\n";
    return;
  }
  check(!app.helpOverlay() && app.topDialog() == parent && app.screen() == opener && !parent->scrollingOff,"click Back to opener");
  open(); key(SDLK_ESCAPE); settle();
  check(!app.helpOverlay() && app.topDialog() == parent && app.screen() == opener && !parent->scrollingOff,"Escape to opener");
  if (boardOpener) {
    app.finishOptionsDialog(true,true); settle(); frame();
    auto* game = dynamic_cast<GameScreen*>(app.screen());
    check(game == opener && !game->paused(),"resume original board");
    const Sint16 turn = edition == Game::Deluxe ? 24000 : -24000;
    mouse(SDL_EVENT_MOUSE_MOTION,400,320); app.tick();
    axis(SDL_GAMEPAD_AXIS_LEFTX,turn); axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,24000);
    app.tick(); check(game->fastForwardHeld(),"trigger active before entering Help");
    key(SDLK_ESCAPE); settle(); frame();
    parent = dynamic_cast<OptionsDialog*>(app.topDialog());
    check(parent && parent->fromGame,"open Pause with axes held");
    help = open();
    axis(SDL_GAMEPAD_AXIS_LEFTX,0); axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,0);
    check(help->page == 0,"neutral axes changed Help page");
    key(SDLK_ESCAPE); settle();
    check(!app.helpOverlay() && app.topDialog() == parent,"return to Pause after releasing axes");
    app.finishOptionsDialog(true,true); settle(); frame();
    check(app.screen() == game && !game->paused() && !game->fastForwardHeld(),"resume without held controls");
    mouse(SDL_EVENT_MOUSE_MOTION,400,320); app.tick();
    const float beforeTurn = game->board().lmAim;
    axis(SDL_GAMEPAD_AXIS_LEFTX,turn); app.tick();
    const bool turned = (game->board().lmAim - beforeTurn) * turn > 0;
    axis(SDL_GAMEPAD_AXIS_LEFTX,0);
    axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,24000);
    const bool spedUp = game->fastForwardHeld() && game->stepsThisTick() > 1;
    axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,0);
    if (!turned || !spedUp) std::cerr << "First press after Help: aim=" << turned << " fast-forward=" << spedUp << '\n';
    check(turned && spedUp,"releasing axes in Help swallowed the next gameplay press");
    mouse(SDL_EVENT_MOUSE_MOTION,400,320); app.tick();
    auto& b = game->board();
    const float aim = b.lmAim;
    press(SDL_GAMEPAD_BUTTON_DPAD_UP); check(b.lmAim < aim,"pad fine left");
    press(SDL_GAMEPAD_BUTTON_DPAD_DOWN); check(std::abs(b.lmAim-aim) < .00001f,"pad fine right");
    pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT,true); app.tick(); pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT,false);
    check(b.lmAim < aim,"held pad left aim");
    axis(SDL_GAMEPAD_AXIS_LEFTX,20000); app.tick(); axis(SDL_GAMEPAD_AXIS_LEFTX,0);
    check(std::abs(b.lmAim-aim) < .00001f,"horizontal stick aim");
    pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true); check(game->stepsThisTick() > 1,"shoulder speed"); pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,false);
    axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,20000); check(game->stepsThisTick() > 1,"trigger speed"); axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,0);
    check(!game->fastForwardHeld(),"speed released");
    press(SDL_GAMEPAD_BUTTON_SOUTH); for (int i=0; i<12; ++i) app.tick();
    check(b.phase == Phase::Shot && !b.balls.empty(),"south shoot in playfield");
    for (int i=0; i<4000 && b.phase != Phase::Aim; ++i) app.tick();
    press(SDL_GAMEPAD_BUTTON_NORTH); check(game->replaying(),"north replay after shot");
    press(SDL_GAMEPAD_BUTTON_EAST); check(!game->replaying(),"east exits replay");
    key(SDLK_R); check(game->replaying(),"R opens the last shot");
    click(400,320); check(!game->replaying() && b.phase == Phase::Aim,"left click skips the replay without firing");
    key(SDLK_RETURN); for (int i=0; i<12; ++i) app.tick();
    check(b.phase == Phase::Shot && !b.balls.empty(),"Enter shoot");
  }
  std::cout << (edition == Game::Deluxe ? "Deluxe" : "Nights") << ' ' << width << 'x' << height << " pages=" << pages << (boardOpener ? " Pause" : " Options") << " PASS\n";
}

int main(int argc, char** argv) {
  if (argc != 5 || !SDL_Init(0)) return 2;
  bool ok = true;
  for (Game g : {Game::Deluxe, Game::Nights}) for (int layout = 0; layout < 4; ++layout) {
    try { exercise(g, argv[g == Game::Deluxe ? 1 : 2], layout == 3 ? "" : argv[3], argv[4], layout == 1 ? 1572 : layout == 2 ? 800 : 1280,
                   layout == 1 ? 1067 : layout == 2 ? 600 : 720, layout == 2, layout == 1); }
    catch (const std::exception& e) { ok = false; std::cerr << int(g) << " layout=" << layout << ": " << e.what() << '\n'; }
  }
  SDL_Quit(); return ok ? 0 : 1;
}
