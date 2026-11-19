#include "online/online_ui.h"
#include "ui/screens.h"
#include "ui/session.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/dialog.h"
#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/loader_tray.h"
#include "ui/secondary_art.h"

namespace pp::ui {

static const char* const kNightsFont = "OVERLOAD16OUTLINE";

static float interp(float v, float a, float b, float lo, float hi) {
  if (b == a) return hi;
  float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
  return lo + (hi - lo) * t;
}


LoadingScreen::LoadingScreen(App& a) : Screen(a) {
  const bool nightsGame = a.game == Game::Nights;
  if (nightsGame) playBtn = embeddedButton(0, "IMAGE_LOAD_PLAYNOW", NightsLoading::kPlayX, NightsLoading::kPlayY);
  else playBtn = embeddedButton(0, "IMAGE_LOAD_PLAYNOW", 0xe4, 0x1be);
  playBtn->visible = false;
  if (playBtn->image && playBtn->image->rows == 1) {
    playBtn->image->rows = 2;
    playBtn->h = playBtn->image->ch();
  }
  playBtn->idleImage = true;
  classicPlayRect = {playBtn->x, playBtn->y, playBtn->w, playBtn->h};
  for (const char* g : {"Init", "Common", "MainMenu", "LevelSelect", "Dialog", "Interface", "Loading"})
    for (auto& id : a.gfx.man().groupImages(g)) resList.push_back(id);
  if (resList.size() > 400) resList.resize(400);
  fastBoot = a.cfg.fastBoot;
  if (nightsGame) nights = std::make_unique<NightsLoading>(a.crtRand(), fastBoot);
  else if (fastBoot) updateCount = 400;
  a.gfx.img("IMAGE_LOAD_POPCAP");
  if (nightsGame) {
    a.gfx.img("IMAGE_LOAD_FLY0");
    a.gfx.img("IMAGE_LOAD_FLY1");
  }
}

void LoadingScreen::spawnParticles(float, float, int) {
  const double kPi = 3.14159265358979323846;
  for (int i = -7; i <= 7; i++) {
    P p;
    p.angle = (float)std::atan2(ballVY, ballVX);
    double cp = std::cos((double)p.angle + kPi / 2), sp = std::sin((double)p.angle + kPi / 2);
    p.x = (float)(4.3 * cp * i + ballX + (double)(app.rand() % 30) - 15);
    p.y = (float)(4.3 * sp * i + ballY + (double)(app.rand() % 30) - 15);
    p.vx = (float)(-0.071 * cp * i);
    p.vy = (float)(-0.071 * sp * i);
    p.alpha = 255;
    p.flip = i > 0;
    particles.push_back(p);
  }
}

void LoadingScreen::doBallBounce() {
  ballState = 1;
  ballX = 0xcd;
  ballY = -0x14;
  ballVX = 4.5f;
  ballVY = 0;
  logoBounce = 0;
}

void LoadingScreen::updateBall() {
  if (ballState > 0 && ballState <= 2) {
    ballVY += 0.2f;
    ballX += ballVX;
    ballY += ballVY;
    const float rightX = 0x348, groundY = 0xaa;
    if (ballY > groundY && ballState == 1) {
      logoBounce = 1;
      ballState = 2;
      ballVY *= -0.9f;
      ballVX *= 1.0f;
      app.playSample("PEGHIT_LOW");
    } else if (ballX > rightX) {
      if (ballX > rightX + 0x14) ballState = 3;
    }
    if (ballX < rightX) spawnParticles(ballX, ballY, 15);
  }
}

void LoadingScreen::update() {
  Panel::update();
  int cnt = updateCount;
  if (!nights && cnt == 1) app.playSample("MORNING");
  if (cnt > 20 && nextRes < resList.size()) {
    uint64_t t0 = SDL_GetTicksNS();
    while (nextRes < resList.size() && (SDL_GetTicksNS() - t0) < 6000000) app.gfx.img(resList[nextRes++]);
  }
  float loadTime = resList.empty() ? 1.0f : (float)nextRes / (float)resList.size();
  if (nights) updateNights();
  else updateDeluxe(cnt, loadTime);
  if (adventureStartDelay > 0 && --adventureStartDelay == 0) app.startAdventureGame();
}

void LoadingScreen::updateNights() {
  NightsLoading::Env env;
  env.mouseX = mouseX;
  env.mouseY = mouseY;
  env.loadFraction = resList.empty() ? 1.0f : (float)nextRes / (float)resList.size();
  env.loadDone = nextRes >= resList.size();
  unsigned snd = nights->update(env);
  if (snd & NightsLoading::kMoonlight) app.playSample("MOONLIGHT");
  if (snd & NightsLoading::kPegHit) app.playSample("PEGHIT_LOW");
  if (snd & NightsLoading::kLogoBuzz) app.playSample("LOGO_BUZZ_ON");
  if (snd & NightsLoading::kMusic) app.playMusic("music/opening.ogg");
  loaded = nights->ready();
  playBtn->visible = loaded;
}

void LoadingScreen::updateDeluxe(int cnt, float loadTime) {
  if (playPulse) playPulse++;
  const int start = fastBoot ? 0 : 250;
  const float rate = fastBoot ? 0.01f : 0.001f;
  updateBall();
  for (size_t i = 0; i < particles.size();) {
    auto& p = particles[i];
    p.alpha -= 6;
    if (p.alpha <= 0) { particles[i] = particles.back(); particles.pop_back(); continue; }
    float sp = 0.25f;
    p.y += p.vy + sp * 3.0f * (float)std::sin(p.angle);
    p.x += p.vx + sp * 3.0f * (float)std::cos(p.angle);
    p.angle += p.flip ? 0.015f : -0.015f;
    i++;
  }
  if (logoBounce > 0) { logoBounce++; if (logoBounce > 30) logoBounce = 0; }
  if (cnt > start - 50 && loadTime > 0) logoFrames++;
  float disp = (cnt - start) * rate;
  if (loadTime > disp) loadTime = disp;
  loadTime = std::max(0.0f, loadTime);
  if (loadProgress < loadTime) {
    loadProgress += 0.01f;
    if (loadProgress > loadTime) loadProgress = loadTime;
    beams = (int)(loadProgress * 25.0f);
    if (loadProgress >= 1.0f) {
      playPulse = 1;
      playBtn->visible = true;
      showDeluxe = true;
      loaded = true;
    }
  }
  if (visualProgress < loadProgress) visualProgress = loadProgress;
  if (visualProgress >= 1.0f && cnt > (fastBoot ? 100 : 550) && ballState == 0) doBallBounce();
  if (visualProgress >= 1.0f) {
    sunAngle += 0.2f;
    doneCnt++;
  }
}

void LoadingScreen::placePlayButton() {
  w = static_cast<float>(canvasWidth());
  const bool wide = w > 800;
  const Rect r = wide ? Rect{w * .513f - 180, 408, 360, 90} : classicPlayRect;
  playBtn->x = r.x; playBtn->y = r.y; playBtn->w = r.w; playBtn->h = r.h;
  playBtn->alpha = wide ? 0.0f : 1.0f;
}

void LoadingScreen::drawLayer(Gfx& g) {
  placePlayButton();
  if (w > 800) { drawConsoleTitle(g); return; }
  if (nights) {
    nights->drawScene(cmds, (int)g.textWidth(kNightsFont, NightsLoading::kLoadingText));
    renderNights(g);
    return;
  }
  g.fill(0, 0, 800, 600, 0x000000);
  int cnt = updateCount;
  if (cnt < 200 && !fastBoot) return;
  if (Tex* t = g.img("IMAGE_LOAD_BACK")) g.draw(t, 0, 0);
  float sunY = (1.0f - visualProgress) * 500 - 214.0f;
  if (Tex* sg = g.img("IMAGE_SUNGLOW")) {
    if (sunAngle != 0) g.drawRot(sg, -0x24 + sg->lw() / 2, sunY + sg->lh() / 2, sunAngle * 3.14159265f / 180.0f, 1, 1);
    else g.draw(sg, -0x24, sunY);
  }
  if (sunAngle != 0)
    if (Tex* sh = g.img("IMAGE_LOAD_SUNHEAD")) g.draw(sh, 283, 307 + sunY);
  float finalY = 140;
  float logoX = 170;
  float logoY = finalY;
  {
    const float initialY = 200, startY = -60, hill = 50, rise = 70;
    float y0 = initialY + finalY;
    float r1 = startY + finalY - y0, r2 = finalY - y0;
    float det = hill * hill * rise - hill * rise * rise;
    float A = (r1 * rise - hill * r2) / det, B = (hill * hill * r2 - rise * rise * r1) / det;
    float f = (float)logoFrames;
    logoY = B * f * f + A * f + y0;
    if (logoFrames >= rise) logoY = finalY;
  }
  Tex* logo = g.img("IMAGE_LOAD_LOGO");
  if (logo && logoFrames < 50) g.draw(logo, logoX, (float)(int)logoY);
  if (Tex* hill = g.img("IMAGE_LOAD_HILL")) g.draw(hill, 0, 0x11d);
  int shadowY = 290 + (logoBounce > 0 && logoBounce < 10 ? (int)(8 - 1.5f * (float)logoBounce) : 0);
  if (Tex* sh = g.img("IMAGE_LOAD_SHADOW")) g.draw(sh, 162, (float)shadowY, Mode(120 * visualProgress * visualProgress / 255.0f, 0x101010));
  if (Tex* ov = g.img("IMAGE_LOAD_OVERLAY")) g.draw(ov, 201, 420);
  if (logo) {
    if (logoFrames >= 50 && logoFrames < 70) g.draw(logo, logoX, (float)(int)logoY);
    else if (logoFrames >= 70) {
      float fy = finalY;
      if (logoBounce > 0 && logoBounce < 10) fy += 8 - 1.5f * logoBounce;
      g.draw(logo, logoX, fy);
      if (logoBounce != 0) g.draw(logo, logoX, fy, Mode(0.5f * interp((float)logoBounce, 5, 25, 0, 1), 0xffffff, true));
    }
  }
  if (Tex* lights = g.img("IMAGE_LOAD_LIGHTS")) {
    for (int i = 0; i < beams; i++) {
      int cel = i == 0 ? 4 : i == 1 ? 3 : i == 23 ? 1 : i == 24 ? 0 : 2;
      g.drawCel(lights, (float)(int)(13.9f * (float)i + 224.0f), 0x1bf, cel, 0);
    }
  }
  if (loadProgress < 1.0f) g.drawString("OVERLOAD16OUTLINE", 400, 0x22b, "Loading...", 0xffec91, 1, Align::Center);
  for (auto& p : particles) {
    uint32_t col = ((uint32_t)(app.rand() % 0x80) << 16) | ((uint32_t)(app.rand() % 0x80) << 8) | (uint32_t)(app.rand() % 0x80);
    g.fill((float)(int)p.x, (float)(int)p.y, (float)(app.rand() % 2 + 1), (float)(app.rand() % 2 + 1), col, p.alpha / 255.0f, true);
    if ((int)(app.rand() % 0xff) < p.alpha) g.fill((float)(int)(p.x - 4), (float)(int)(p.y - 4), 8, 8, 0x020201, p.alpha / 255.0f, true);
  }
  if (ballState > 0)
    if (Tex* b = g.img("IMAGE_LOAD_BALL")) g.draw(b, ballX - b->lw() / 2, ballY - b->lh() / 2);
  if (showDeluxe) {
    if (Tex* d = g.img("IMAGE_LOAD_DELUXE")) g.draw(d, 300, 334);
    if (Tex* tm = g.img("IMAGE_LOAD_TM")) g.draw(tm, 574, 310, Mode(1, 0x000000));
  }
}

void LoadingScreen::drawOverlay(Gfx& g) {
  if (canvasWidth() > 800) return;
  if (nights) {
    Tex* fly = g.img("IMAGE_LOAD_FLY0");
    nights->drawOverlay(cmds, fly && fly->valid() ? (int)fly->lw() : 33);
    renderNights(g);
    return;
  }
  int cnt = updateCount;
  Tex* frameL = g.img("IMAGE_SCREENFRAMELEFT");
  Tex* frameT = g.img("IMAGE_SCREENFRAMETOP");
  Tex* frameB = g.img("IMAGE_SCREENFRAMEBOTTOM");
  Tex* frameR = g.img("IMAGE_SCREENFRAMERIGHT");
  if (frameL && frameT && frameB && frameR) {
    g.draw(frameL, 0, 0);
    g.draw(frameT, frameL->lw(), 0);
    g.draw(frameB, frameL->lw(), frameL->lh() - frameB->lh());
    g.draw(frameR, frameL->lw() + frameT->lw(), 0);
  }
  if (playPulse > 0 && playPulse < 50 && playBtn->visible) {
    float v = playPulse < 25 ? (float)playPulse : (float)(50 - playPulse);
    float a = v / 25.0f * 100.0f / 255.0f;
    if (playBtn->image) g.drawCel(playBtn->image, playBtn->x, playBtn->y, 0, 1, Mode(a, 0xffffff, true));
  }
  if (!fastBoot) {
    if (cnt < 300) {
      int alpha = 255;
      if (cnt > 200) alpha = (300 - cnt) * 255 / 100;
      if (alpha > 0) g.fill(0, 0, 800, 600, 0x000000, alpha / 255.0f);
    }
    if (cnt < 200) {
      int alpha = 255;
      if (cnt < 50) alpha = cnt * 255 / 50;
      else if (cnt > 150) alpha = (200 - cnt) * 255 / 50;
      if (Tex* pc = g.img("IMAGE_LOAD_POPCAP")) g.draw(pc, (800 - pc->lw()) / 2, (600 - pc->lh()) / 2, Mode(alpha / 255.0f));
    }
  }
}

static uint32_t packRgb(const NightsCmd& c) { return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b; }

static Mode nightsMode(const NightsCmd& c) {
  if (!c.colorize) return Mode(1.0f, 0xffffff, c.add);
  return Mode(c.a / 255.0f, packRgb(c), c.add);
}

void LoadingScreen::renderNights(Gfx& g) {
  for (const NightsCmd& c : cmds) {
    Mode m = nightsMode(c);
    switch (c.op) {
      case NightsCmd::Fill: g.fill(c.x, c.y, (float)c.w, (float)c.h, packRgb(c), c.a / 255.0f, c.add); break;
      case NightsCmd::Text: g.drawString(kNightsFont, c.x, c.y, c.img, packRgb(c), c.a / 255.0f); break;
      case NightsCmd::Highlight: {
        Tex* t = playBtn->image;
        if (!t) break;
        Mode lit(1.0f, packRgb(c), true);
        g.drawCel(t, c.x, c.y, 0, 0, lit);
        if (playBtn->down && playBtn->over) g.drawCel(t, c.x, c.y, 0, 1, lit);
        else if (playBtn->overAlpha > 0) g.drawCel(t, c.x, c.y, 0, 1, Mode(playBtn->overAlpha, packRgb(c), true));
        break;
      }
      default: {
        Tex* t = g.img(c.img);
        if (!t || !t->valid()) break;
        if (c.op == NightsCmd::Stretch) g.drawStretch(t, c.x, c.y, (float)c.w, (float)c.h, m);
        else if (c.op == NightsCmd::Cel) g.drawSrc(t, (float)c.sx, (float)c.sy, (float)c.sw, (float)c.sh, c.x, c.y, m);
        else if (c.op == NightsCmd::Centered) g.draw(t, c.x - t->lw() / 2, c.y - t->lh() / 2, m);
        else if (c.op == NightsCmd::Rotated) g.drawRot(t, c.x + (float)c.w, c.y + (float)c.h, c.angle, 1, 1, m);
        else g.draw(t, c.x, c.y, m);
        break;
      }
    }
  }
}

void LoadingScreen::lostFocus() {
  if (canvasWidth() <= 800) return;
  releaseHover();
  playBtn->down = playBtn->over = false;
  capture = nullptr;
}

bool LoadingScreen::input(const Input& in) {
  placePlayButton();
  if (w > 800 && capture == playBtn && in.type == Input::MouseUp && in.button != 1) return true;
  if (in.type == Input::KeyDown && loaded && (in.key == SDLK_RETURN || in.key == SDLK_SPACE)) {
    onButton(0);
    return true;
  }
  if (in.type == Input::MouseMove || in.type == Input::MouseDown || in.type == Input::MouseUp) {
    mouseX = (int)in.x;
    mouseY = (int)in.y;
  }
  bool used = Panel::input(in);
  if (nights && canvasWidth() == 800 && !used && in.type == Input::MouseDown && mouseVisible) {
    int popped = nights->click(mouseX, mouseY);
    for (int i = 0; i < popped; i++) app.playSample("FAIRY_POP");
    app.faeriesCaught += popped;
  }
  return used;
}

void LoadingScreen::onButton(int id) {
  if (id != 0) return;
  if (!app.curProfile) {
    app.doCreateUserDialog();
    return;
  }
  app.showMainMenu();
}


static const int kSecret[15] = {2, 2, 3, 4, 4, 3, 2, 1, 0, 0, 1, 2, 2, 1, 1};

namespace {

constexpr int kConsoleRow = 100;
constexpr int kConsoleFooter = 110;
constexpr int kClassicAction = 120;
constexpr int kMenuSlide = 14;

Tex* menuArt(Gfx& g, const char* name) {
  const auto path = std::string("console/menu/") + name + ".png";
  return g.vfs().exists(path) ? g.img(path) : nullptr;
}

void menuFit(Gfx& g, Tex* t, float x, float y, float w, float h) {
  if (!t) return;
  const float s = std::min(w / t->lw(), h / t->lh());
  g.drawStretch(t, x + (w - t->lw() * s) / 2, y + (h - t->lh() * s) / 2, t->lw() * s, t->lh() * s);
}

constexpr std::array<uint32_t, 6> kPipe{0x376b95, 0xb9f3ff, 0x66b6f4, 0x28488d, 0x47317d, 0x260c4f};
constexpr std::array<uint32_t, 6> kIdle{0xb9c9db, 0x6c90ca, 0x335096, 0x121d58, 0x242d73, 0x12193e};
constexpr std::array<uint32_t, 6> kFocus{0xe3fdff, 0xa0ebff, 0x528db7, 0x225474, 0x4e8eaf, 0xa1e6ee};
constexpr std::array<uint32_t, 6> kDown{0xd2ffed, 0x97edbb, 0x4a9d76, 0x245d45, 0x328964, 0xafe4bb};
constexpr std::array<uint32_t, 6> kLocked{0xeae7df, 0xb8b5b6, 0xa19ca6, 0x5e596b, 0x81798b, 0x4c495c};
constexpr std::array<uint32_t, 6> kGold{0x7b4b2c, 0xffe3a4, 0xaf773e, 0x805333, 0xc39860, 0x5b3221};
constexpr std::array<uint32_t, 6> kSign{0x174735, 0x196244, 0x146247, 0x0b583a, 0x0b382b, 0x061c17};

void menuBand(Gfx& g, Rect r, float radius, const std::array<uint32_t, 6>& colors, float alpha = 1) {
  for (int y = 0; y < static_cast<int>(r.h); ++y) {
    const float d = std::max({0.0f, radius - y - 0.5f, y + 0.5f - r.h + radius});
    const float edge = radius - std::sqrt(std::max(0.0f, radius * radius - d * d));
    const float p = 5 * y / (r.h - 1);
    const int i = std::min(4, static_cast<int>(p));
    uint32_t rgb = 0;
    for (int shift : {0, 8, 16}) {
      const int a = (colors[i] >> shift) & 255, b = (colors[i + 1] >> shift) & 255;
      rgb |= static_cast<uint32_t>(a + (b - a) * (p - i)) << shift;
    }
    g.fill(r.x + edge, r.y + y, r.w - 2 * edge, 1, rgb, alpha);
  }
}

void menuText(Gfx& g, const char* font, float cx, float cy, const std::string& label, uint32_t color, float alpha = 1) {
  g.drawString(font, cx, cy + g.fontAscent(font) - g.fontHeight(font) * 0.5f, label, color, alpha, Align::Center);
}

void menuTextFit(Gfx& g, const char* font, float cx, float cy, const std::string& label, uint32_t color, float room, float aspect = 1) {
  const float k = std::min(aspect, room / std::max(1.0f, g.textWidth(font, label)));
  SDL_Renderer* r = g.renderer();
  if (!r || k >= 1) { menuText(g, font, cx, cy, label, color); return; }
  float sx, sy;
  SDL_GetRenderScale(r, &sx, &sy);
  SDL_Rect clip;
  const bool clipped = SDL_RenderClipEnabled(r) && SDL_GetRenderClipRect(r, &clip);
  const float s = g.scale(), ox = g.offX(), oy = g.offY();
  SDL_SetRenderScale(r, sx * k, sy);
  if (clipped) {
    const int left = static_cast<int>(std::floor(clip.x / k));
    SDL_Rect scaled{left, clip.y, static_cast<int>(std::ceil((clip.x + clip.w) / k)) - left, clip.h};
    SDL_SetRenderClipRect(r, &scaled);
  }
  g.setTransform(s, ox + (ox + (g.originX() + cx) * s) * (1 / k - 1), oy);
  menuText(g, font, cx, cy, label, color);
  g.setTransform(s, ox, oy);
  SDL_SetRenderScale(r, sx, sy);
  if (clipped) SDL_SetRenderClipRect(r, &clip);
}

void menuMirror(Gfx& g, Tex* art, float x, float y, float k) {
  const float s = g.scale(), ox = g.offX(), oy = g.offY();
  g.setTransform(s * k, ox + (g.originX() + x) * s * (1 - k), oy + (g.originY() + y) * s * (1 - k));
  g.drawCelMirror(art, x, y, 0, 0);
  g.setTransform(s, ox, oy);
}

}

void drawPromptGlyph(Gfx& g, float cx, float cy, int glyph, const char* key, const PadGlyphs& pad, float alpha) {
  const bool face = pad.pad != Pad::Keys && glyph >= 0 && glyph < 4;
  Tex* art = face && !pad.art[glyph].empty() ? g.imgFile(pad.art[glyph]) : nullptr;
  Tex* glyphs = !art && face && pad.pad == Pad::PlayStation ? menuArt(g, "pad_glyphs") : nullptr;
  const std::string text = face && pad.pad != Pad::PlayStation ? std::string(1, (pad.pad == Pad::Nintendo ? "BAXY" : "ABYX")[glyph]) : key;
  if (art) {
    g.drawStretch(art, cx - 15.5f, cy - 15.5f, 31, 31, Mode(alpha));
  } else if (glyphs) {
    const float gw = glyphs->lw() / 4;
    g.drawStretchSrc(glyphs, glyph * gw, 0, gw, glyphs->lh(), cx - 14, cy - 14, 28, 28, Mode(alpha));
  } else if (text == "ENTER") {
    g.fill(cx + 4, cy - 8, 2.5f, 10.5f, 0xffffff, alpha);
    g.fill(cx - 5, cy, 11.5f, 2.5f, 0xffffff, alpha);
    for (int i = 0; i < 5; ++i) g.fill(cx - 8 + i * 1.2f, cy + 1.25f - (5 - i) * 1.1f, 1.2f, (5 - i) * 2.2f, 0xffffff, alpha);
  } else {
    const char* keyFont = g.textWidth("OVERLOAD10OUTLINE", text) <= 25 ? "OVERLOAD10OUTLINE" : "OVERLOAD10";
    g.drawString(keyFont, cx, cy + g.fontAscent(keyFont) - g.fontHeight(keyFont) * 0.5f, text, 0xffffff, alpha, Align::Center);
  }
}

float drawPromptSleeve(Gfx& g, float x, float y, int glyph, const char* key, const char* label, const PadGlyphs& pad, float alpha) {
  Tex* sleeve = menuArt(g, "footer_sleeve");
  if (!sleeve || sleeve->lw() <= 80 || sleeve->lh() <= 0) return 0;
  constexpr const char* font = "OVERLOAD16OUTLINE";
  const float sw = sleeve->lw(), sh = sleeve->lh();
  const float tagW = std::max(112.0f, 82 + g.textWidth(font, label));
  g.drawStretchSrc(sleeve, 0, 0, 61, sh, x, y, 54, 64, Mode(alpha));
  g.drawStretchSrc(sleeve, 61, 0, sw - 78, sh, x + 54, y, tagW - 69, 64, Mode(alpha));
  g.drawStretchSrc(sleeve, sw - 17, 0, 17, sh, x + tagW - 15, y, 15, 64, Mode(alpha));
  const float cx = x + 29.7f, cy = y + 25.3f;
  drawPromptGlyph(g, cx, cy, glyph, key, pad, alpha);
  g.drawString(font, x + 58, cy + g.fontAscent(font) - g.fontHeight(font) * 0.5f, label, 0xffffff, alpha);
  return tagW;
}

bool drawConsoleTitle(Gfx& g, float cx, float width, const char* title) {
  auto* header = menuArt(g, "header");
  if (!header) return false;
  if (auto* rail = menuArt(g, "header_rail")) {
    g.drawStretch(rail, 0, 0, cx - 198, 54);
    g.drawStretch(rail, cx + 198, 0, width - cx - 198, 54);
  }
  g.drawStretch(header, cx - 233, -5, 466, 158);
  menuTextFit(g, "OVERLOAD24", cx, 68, title, 0x153d27, 226, .80f);
  menuTextFit(g, "OVERLOAD24", cx, 66, title, 0xd6fc5b, 226, .80f);
  return true;
}

void drawConsoleRail(Gfx& g, float width) {
  if (auto* rail = menuArt(g, "footer_rail")) g.drawStretch(rail, 0, 535, width, 72);
  else if (auto* footer = menuArt(g, "footer")) g.drawStretch(footer, 0, 535, width, 65);
  else {
    menuBand(g, {0, 536, width, 64}, 0, kPipe);
    for (float seam : {width * 0.48f, width * 0.82f}) {
      g.line(seam, 538, seam + 5, 598, 0x1a255d, 0.75f);
      for (int y : {544, 566, 589}) g.fill(seam + 5, static_cast<float>(y), 3, 3, 0x142952);
    }
  }
}

float drawConsolePrompt(Gfx& g, float x, int glyph, const char* key, const char* label, const PadGlyphs& pad) {
  return drawConsolePrompt(g, x, glyph, key, label, pad, 1);
}

float drawConsolePrompt(Gfx& g, float x, int glyph, const char* key, const char* label, const PadGlyphs& pad, float alpha) {
  if (const float w = drawPromptSleeve(g, x, 529, glyph, key, label, pad, alpha)) return x + w;
  const float keyW = std::max(36.0f, g.textWidth("OVERLOAD16", key) + 13);
  const float tagW = keyW + g.textWidth("OVERLOAD16OUTLINE", label) + 20;
  menuBand(g, {x, 541, tagW, 47}, 14, kGold, alpha);
  menuBand(g, {x + 4, 544, tagW - 8, 41}, 11, kIdle, alpha);
  menuBand(g, {x + 7, 551, keyW - 6, 28}, 6, kPipe, alpha);
  menuText(g, "OVERLOAD16", x + 4 + keyW / 2, 565, key, 0xffffff, alpha);
  menuText(g, "OVERLOAD16OUTLINE", x + (tagW + keyW) / 2 + 2, 565, label, 0xffffff, alpha);
  return x + tagW + 8;
}

namespace {

void menuButtonArt(Gfx& g, Tex* art, int row, int rows, float w, float h) {
  const float srcW = art->lw(), srcH = art->lh() / rows;
  const float sx = srcW / 3, cap = sx * h / srcH;
  for (int i = 0; i < 3; ++i) {
    const float x = i == 0 ? 0 : i == 1 ? cap : w - cap;
    g.drawStretchSrc(art, i * sx, row * srcH, sx, srcH, x, 0, i == 1 ? w - 2 * cap : cap, h);
  }
}

class ConsoleMenuButton : public Button {
 public:
  bool locked = false;
  void draw(Gfx& g) override {
    const bool small = id >= kConsoleFooter;
    const bool hot = focused || over;
    auto* art = menuArt(g, locked ? "button_locked" : "buttons");
    if (hot) {
      if (auto* glow = menuArt(g, "button_glow"))
        g.drawStretch(glow, -22, -20, w + 44, h + 40, Mode(0.85f, locked ? 0xffe9ad : 0x9be6ff, true));
      else menuBand(g, {-3, -3, w + 6, h + 6}, h * 0.5f + 3, kFocus, 0.63f);
    }
    if (art) {
      const float grow = hot && !small && !down ? 0.035f : 0;
      g.pushOrigin(-w * grow / 2, -h * grow / 2);
      menuButtonArt(g, art, locked ? 0 : down ? 2 : hot ? 1 : 0, locked ? 1 : 3, w * (1 + grow), h * (1 + grow));
      g.popOrigin();
    }
    else
      menuBand(g, {0, 0, w, h}, h * 0.5f, locked ? kLocked : down ? kDown : hot ? kFocus : kIdle);
    if (locked) {
      if (auto* lock = menuArt(g, "lock")) menuFit(g, lock, 1, -9, h + 36, h + 18);
      else {
        menuBand(g, {15, 5, 26, 34}, 12, kGold);
        menuBand(g, {20, 10, 16, 24}, 7, kLocked);
        g.fill(26, 19, 5, 10, 0x181128);
      }
    }
    const char* font = small ? "OVERLOAD16OUTLINE" : "OVERLOAD24OUTLINE";
    const float left = locked ? 72.0f : small ? 7.0f : 13.0f, right = small ? 7.0f : 13.0f;
    const uint32_t edge = locked ? 0x77747c : hot ? 0x669cb8 : 0x192955;
    auto& fp = g.fontProvider();
    uint32_t mult, add;
    const bool outline = fp.layerColors(font, 1, mult, add);
    if (outline) fp.setLayerColors(font, 1, (edge << 8) | 255, add);
    menuTextFit(g, font, (w + left - right) / 2, h / 2, label, locked || hot ? 0xffffff : 0xdfe9ff, w - left - right, 0.80f);
    if (outline) fp.setLayerColors(font, 1, mult, add);
  }
};

}

void LoadingScreen::drawConsoleTitle(Gfx& g) {
  const float cx = w / 2;
  auto art = [&](const char* name, const char* fallback) {
    if (auto* t = menuArt(g, name)) return t;
    return g.img(fallback);
  };
  g.fill(0, 0, w, 600, 0x694ab7);
  auto* bg = art("home", nights ? "IMAGE_LOAD_BACKBRIGHT" : "IMAGE_LOAD_BACK");
  const float sceneWidth = bg ? std::max(w, bg->lw() * 601 / bg->lh()) : w;
  if (bg) g.drawStretch(bg, cx - sceneWidth / 2, -.5f, sceneWidth, 601);
  if (auto* glow = art("sun_glow", "IMAGE_SUNGLOW"))
    g.drawRot(glow, cx - 30, 164, 3.165f, 718 / glow->lw(), 718 / glow->lh());
  menuFit(g, art("title_sun", "IMAGE_LOAD_SUNHEAD"), cx - 93, 113, 135, 135);
  if (auto* front = menuArt(g, "home_front")) g.drawStretch(front, cx - sceneWidth / 2, -.5f, sceneWidth, 601);
  else menuFit(g, g.img(nights ? "IMAGE_LOAD_HILLBRIGHT" : "IMAGE_LOAD_HILL"), 0, 287, w, 313);
  if (auto* shadow = art("title_shadow", "IMAGE_LOAD_SHADOW"))
    g.drawStretch(shadow, cx - 201.5f, 291, 414, 212, Mode(120 / 255.f, 0x101010));
  menuFit(g, art("logo", "IMAGE_LOAD_LOGO"), cx - 194, 155.7f, 383.4f, 190.8f);
  if (auto* tm = g.img("IMAGE_LOAD_TM")) g.drawStretch(tm, cx + 169.4f, 308, 12.6f, 5.4f, Mode(1, 0x000000));

  const Rect tray{playBtn->x, playBtn->y, playBtn->w, playBtn->h};
  const float center = tray.x + tray.w / 2;
  auto* plate = menuArt(g, "title_plate");
  if (plate) g.drawStretch(plate, tray.x, tray.y, tray.w, tray.h);
  else {
    menuBand(g, tray, 32, kPipe);
    menuBand(g, {tray.x + 4, tray.y + 4, tray.w - 8, tray.h - 8}, 28, kGold);
    menuBand(g, {tray.x + 23, tray.y + 21, 314, 49}, 23, kLocked);
  }
  if (loaded) {
    if (auto* faces = menuArt(g, "title_buttons")) {
      const float row = faces->lh() / 2;
      g.drawStretchSrc(faces, 0, playBtn->down && playBtn->over ? 0 : row, faces->lw(), row,
                       tray.x + 24.3f, tray.y + 23.4f, 309.6f, 45);
    } else {
      constexpr std::array<uint32_t, 6> green{0xc7ff4e, 0x88eb27, 0x4ab500, 0x66e300, 0x76f300, 0x367e00};
      menuBand(g, {tray.x + 24, tray.y + 23, 312, 45}, 21, playBtn->down ? kGold : green);
    }
    constexpr const char* font = "OVERLOAD24OUTLINE";
    auto& fp = g.fontProvider();
    uint32_t mult, add;
    const bool outline = fp.layerColors(font, 1, mult, add);
    if (outline) fp.setLayerColors(font, 1, 0x547812ff, add);
    menuTextFit(g, font, center, tray.y + 47.5f, "Press Start", 0xffffec, 140);
    if (outline) fp.setLayerColors(font, 1, mult, add);
  } else {
    const int cells = nights ? nights->beams() : beams;
    auto* lights = art("loader_lights", "IMAGE_LOAD_LIGHTS");
    for (int i = 0; i < cells; ++i) {
      if (lights && lights->lw() == 90 && lights->lh() == 49) drawLoaderCell(g, lights, tray, i);
      else if (lights) {
        const float sw = lights->lw() / 5;
        const int col = i < 2 ? 4 - i : i > 22 ? 24 - i : 2;
        g.drawStretchSrc(lights, sw * col, 0, sw, lights->lh(), tray.x + 24 + 12.45f * i, tray.y + 24, 15, 43);
      }
    }
    menuTextFit(g, "OVERLOAD16OUTLINE", center, tray.y - 13, "Loading...", 0xffefb7, 140);
  }
  const float s = g.scale(), ox = g.offX(), oy = g.offY();
  g.setTransform(s * 1.14f, ox - (g.originX() + center) * s * .14f, oy - (g.originY() + 514) * s * .14f);
  menuTextFit(g, "OVERLOAD16OUTLINE", center + 10, 515, "2007, 2009 PopCap Games, Inc. All rights reserved.", 0xffe299, 389);
  menuText(g, "PLAINOUTLINE", center - 194, 513, "\xa9", 0xffe299);
  g.setTransform(s, ox, oy);
}

MainMenu::MainMenu(App& a, bool nightsHub) : Screen(a) {
  isNights = a.game == Game::Nights;
  auto emb = [&](int id, const char* img, float pitch) {
    Button* b = embeddedButton(id, img, 0, 0);
    b->additiveDown = true;
    b->hoverSound = "MOUSEOVER";
    b->hoverPitch = pitch;
    return b;
  };
  bAdv = emb(0, "IMAGE_MM_ADVENTURE", 0);
  bSolo = emb(1, "IMAGE_MM_SOLO", 2);
  bDuel = emb(2, "IMAGE_MM_DUEL", 4);
  bChal = emb(3, "IMAGE_MM_CHALLENGE", 5);
  auto brown = [&](int id, const char* label, float pitch) {
    Button* b = patchButton(id, label, "IMAGE_DLG_BROWNBUTTON1", 0, 0);
    b->hoverSound = "MOUSEOVER";
    b->hoverPitch = pitch;
    return b;
  };
  bReplay = isNights ? brown(online::kMenuButtonId, "Online", 7) : brown(4, "Replays", 7);
  bOptions = brown(5, "Options", 9);
  bDemo = isNights ? brown(9, "Stats", 11) : brown(online::kMenuButtonId, "Online", 11);
  bQuit = patchButton(7, "Quit", "IMAGE_DLG_PURPLEBUTTON", 7, 0x215);
  bQuit->hoverSound = "MOUSEOVER";
  bQuit->hoverPitch = 12;
  auto lk = std::make_unique<Button>();
  lk->id = 8;
  lk->kind = Button::Kind::Link;
  lk->label = "If this is not you, click here.";
  lk->font = "PLAIN2";
  lk->colNormal = 0x000064;
  lk->colOver = 0x0000ff;
  lk->w = a.gfx.textWidth(lk->font, lk->label);
  lk->h = a.gfx.fontHeight(lk->font) + 8;
  lk->x = 0x13b;
  lk->y = 0x88;
  lk->tickRef = &updateCount;
  lk->hoverSound = "";
  lk->clickSound = "";
  bLink = add(std::move(lk));
  bLink->visible = false;
  bUpsell = nullptr;
  auto console = [&](int id) {
    auto b = std::make_unique<ConsoleMenuButton>();
    b->id = id;
    b->visible = false;
    b->hoverSound = "MOUSEOVER";
    b->tickRef = &updateCount;
    return add(std::move(b));
  };
  for (int i = 0; i < kMenuRows; i++) consoleRows_[i] = console(kConsoleRow + i);
  for (int i = 0; i < kFooterButtons; i++) consoleFooter_[i] = console(kConsoleFooter + i);
  classicAction_ = patchButton(kClassicAction, "Back", "IMAGE_DLG_PURPLEBUTTON", 7, 494);
  app.playMusic("menu");
  placeButtons();
  syncPlayerInfo();
  if (nightsHub) { page_ = Page::Nights; consoleFocus_ = 2; }
  if (app.menuPage) {
    page_ = static_cast<Page>(app.menuPage);
    consoleFocus_ = app.menuRow;
    app.menuPage = 0;
  }
  placeConsole(canvasWidth());
  for (Button* b : {bAdv, bSolo, bDuel, bChal, bReplay, bOptions, bDemo, bQuit}) b->clickSound = "BUTTON1";
}

int MainMenu::consoleWidth() const {
  return app.consoleCanvasWidth();
}

int MainMenu::canvasWidth() const { return page_ == Page::Lounge ? 800 : consoleWidth(); }

bool MainMenu::consoleButton(int id) const { return id >= kConsoleRow && id < kConsoleFooter + kFooterButtons; }

void MainMenu::placeConsole(int width) {
  const bool wide = width >= 960;
  if (width != consoleLayoutWidth_) lostFocus();
  if (!wide && consoleLayoutWidth_ >= 960 && page_ != Page::Lounge) {
    page_ = Page::Home;
    consoleFocus_ = 0;
    slideTicks_ = 0;
  }
  consoleLayoutWidth_ = width;
  w = static_cast<float>(width);
  for (Button* b : {bAdv, bSolo, bDuel, bChal, bReplay, bOptions, bDemo, bQuit}) b->visible = !wide;
  if (auto* online = findWidget(online::kMenuButtonId)) online->visible = !wide;
  bLink->visible = !wide && app.curProfile && bubble == 0 && !bubbleDimmed;
  classicAction_->visible = !wide && page_ == Page::Lounge;

  static const char* const home[] = {"Single Player", "Multiplayer", "Replays", "Trophies", "Help & Options"};
  static const char* const single[] = {"Adventure", "Quick Play", "Custom Levels", "Master Duel", "Challenge"};
  static const char* const nights[] = {"Adventure", "Challenge", "Trophy Room", "Stats", "Lounge"};
  static const char* const multi[] = {"Online Peg Party", "Online Duel", "Same Console Duel", "Join With Code", ""};
  static const char* const party[] = {"Quick Match", "Custom Match", "Host Peg Party", "", ""};
  static const char* const duel[] = {"Quick Match", "Custom Match", "Host Duel", "", ""};
  const bool online = page_ >= Page::Multiplayer;
  const auto& labels = page_ == Page::Home ? home : page_ == Page::Nights ? nights : page_ == Page::Multiplayer ? multi
                     : page_ == Page::Party ? party : page_ == Page::Duel ? duel : single;
  const bool homePage = page_ == Page::Home;
  const bool actions = wide && !app.getDialog(D_Locked);
  const float phase = static_cast<float>(slideTicks_) / kMenuSlide;
  const float slide = 78 * phase * phase * slideDirection_;
  const bool duelLocked = wide && !app.modeOpen(GameMode::Duel) && !app.otherOpen(GameMode::Duel);
  const bool challengeLocked = wide && !homePage && !app.modeOpen(GameMode::Challenge) && !app.otherOpen(GameMode::Challenge);
  for (int i = 0; i < kMenuRows; ++i) {
    auto* b = static_cast<ConsoleMenuButton*>(consoleRows_[i]);
    b->label = wide ? labels[i] : "";
    b->x = (online ? width * 0.5f - 115.5f : static_cast<float>(width - (homePage ? 392 : 356))) + slide;
    b->y = online ? 139.0f + i * 59.0f : (homePage ? 81.0f : 194.0f) + i * 63.0f;
    b->w = 285;
    b->h = 50;
    b->visible = actions && *labels[i];
    b->mouseVisible = !slideTicks_;
    b->focused = consoleFocus_ == i;
    b->locked = (page_ == Page::Single && ((i == 3 && duelLocked) || (i == 4 && challengeLocked))) ||
                (page_ == Page::Nights && i == 1 && challengeLocked) || (page_ == Page::Multiplayer && i == 2 && duelLocked);
  }
  const char* const footer[] = {"Profile", homePage ? "Mods" : "Back", "Quit"};
  for (int i = 0; i < kFooterButtons; ++i) {
    auto* b = consoleFooter_[i];
    b->label = footer[i];
    b->x = static_cast<float>(width - 316 + i * 98);
    b->y = 548;
    b->w = 90;
    b->h = 36;
    b->visible = actions && *footer[i] && !app.helpOverlay();
    b->focused = b->visible && consoleFocus_ == kMenuRows + i;
  }
}

void MainMenu::syncPlayerInfo() {
  const PlayerInfo* p = app.curProfile;
  advStage = advLevel = trophyTier = 0;
  if (p) {
    advStage = std::max(0, p->advStage);
    advLevel = std::max(0, p->advLevel);
    int prev = p->trophyLevelSeen;
    if (p->advCompletions > 0) {
      trophyTier = 1;
      if ((int)app.trophies.numHeld(p->trophies) >= (int)app.trophies.numTrophies()) {
        trophyTier = 2;
        if (app.stages.numClearedOwn(p->clearedLevels) >= app.stages.numLevels()) trophyTier = 3;
      }
    }
    if (prev < trophyTier) {
      app.curProfile->trophyLevelSeen = (uint16_t)trophyTier;
      app.curProfile->dirty = true;
      if (trophyTier >= 1) { trophyAnim = 0; trophyAnimMax = 400; }
      app.saveProfile();
    }
  }
  bLink->visible = consoleLayoutWidth_ == 800 && p != nullptr && bubble == 0 && !bubbleDimmed;
  bAdv->blink = 0;
  buildSparkles();
}

void MainMenu::setBubble(int which, int delay) {
  if (bubble != which) {
    int c = bubbleCount++;
    if (c >= delay) {
      bubbleFade = fadeTicks();
      bubbleOld = bubble;
      bubble = which;
      bubbleCount = 0;
    }
  }
}

void MainMenu::update() {
  Panel::update();
  online::tickMainMenu(app);
  sunTick++;
  if (bubbleFade > 0) bubbleFade--;
  if (!bubbleDimmed) {}
  if (trophyAnim < trophyAnimMax) {
    trophyAnim++;
    if (!isNights) {
      if (trophyAnim == 160) app.playSample("AWARD");
      else if (trophyAnim == 10) app.playSample("APPLAUSE");
    }
  }
  updateSparkles();
  placeButtons();
  if (slideTicks_ > 0) --slideTicks_;
  placeConsole(canvasWidth());
  bool any = false;
  for (Button* b : {bAdv, bSolo, bDuel, bChal, bReplay, bOptions, bDemo, bQuit}) any = any || b->over || b->down;
  if (false) {} 
  bool unused_ = any; (void)unused_;
  if (consoleLayoutWidth_ == 800) {
    updateBubbles();
    bLink->visible = app.curProfile != nullptr && bubble == 0 && !bubbleDimmed;
  }
  if (page_ == Page::Home && app.bothGames && app.homeGame != app.game && !app.anyDialog() && !app.helpOverlay() && !app.moving()) app.goHome();
}

void MainMenu::placeButtons() {
  bAdv->x = 0x24c; bAdv->y = 0x1b;
  bSolo->x = 0x241; bSolo->y = 0xac;
  bDuel->x = 0x241; bDuel->y = 0x13a;
  bChal->x = 0x24c; bChal->y = 0x1c8;
  bQuit->x = 7; bQuit->y = 0x21e;
  bDemo->x = 0x6c; bDemo->y = 0x22a;
  bOptions->x = 0x108; bOptions->y = 0x22a;
  bReplay->x = 0x1a1; bReplay->y = 0x22a;
  bLink->x = 0x13b;
  bLink->y = (float)(isNights ? 0x88 : 0x8e);
}

void MainMenu::updateBubbles() {
  const DecalInfo* decal = isNights ? decalAt(app, app.mouseX - x, app.mouseY - y, nullptr) : nullptr;
  bubbleDimmed = isNights && !app.hasModal() && (decal ? !decal->hover.empty() : mantelAt(app, app.mouseX - x, app.mouseY - y) >= 0);
  bool any = false;
  for (Button* b : {bAdv, bSolo, bDuel, bChal, bReplay, bOptions, bDemo, bQuit}) any = any || b->over || b->down;
  if (app.getDialog(D_Locked) || app.hasModal()) {
  } else if (!any) {
    if (!isNights && trophyTier > 0 && app.mouseX >= 330 && app.mouseX < 490 && app.mouseY >= 200 && app.mouseY < 425) setBubble(trophyTier + 8, 50);
    else setBubble(0, 15);
  } else {
    if (bQuit->over) setBubble(8, 15);
    else if (bOptions->over) setBubble(7, 15);
    else if (bReplay->over) setBubble(isNights ? 5 : 6, 15);
    else if (bDemo->over) setBubble(isNights ? 9 : 5, 15);
    else if (bDuel->over) setBubble(3, 15);
    else if (bChal->over) setBubble(4, 15);
    else if (bSolo->over) setBubble(2, 15);
    else if (bAdv->over) setBubble(1, 15);
  }
}

void MainMenu::onButtonEnter(int id) {
  if (consoleButton(id)) {
    onButtonPress(id, 1);
    return;
  }
  if (isNights && secretDone) return;
  if (id == kSecret[hoverSeq]) hoverSeq++;
  else if (!isNights || (id >= 0 && id <= 4)) hoverSeq = 0;
  if (hoverSeq >= 15) {
    hoverSeq = 0;
    if (isNights) {
      secretDone = true;
      app.playSample("APPLAUSE_LONG");
    } else {
      app.showStoryScreen(true, true);
    }
  }
}

void MainMenu::drawSpeechBubble(Gfx& g, int which, float alpha, bool fading) {
  if (bubbleDimmed) return;
  const PlayerInfo* p = app.curProfile;
  std::string name;
  int adv = -1, lvl = 0;
  bool done = false;
  if (p) { name = p->name; adv = p->advStage; lvl = p->advLevel; done = p->advCompletions > 0; }
  float bx = 0x113, by = 0x38;
  if (fading && (!name.empty() || which > 0)) {
    float k = 1.0f;
    if (bubbleFade > 0) {
      float d = (float)fadeTicks() / 2.0f - std::fabs((float)fadeTicks() / 2.0f - (float)bubbleFade);
      k -= 0.0025f * d;
    }
    if (isNights) drawSpeechBox(g, bx - 5, by + 4, 280, 126, k);
    else if (Tex* sb = g.img("IMAGE_MM_SPEECHBUBBLE")) g.drawRot(sb, sb->lw() / 2 + bx, sb->lh() / 2 + 10 + by, 0, k, k);
  }
  float tH = 0x50, tW = 0xeb, tY = 0x14 + by, tX = 0xa + bx;
  uint32_t col = 0x000064;
  if (which > 0) {
    std::string s;
    switch (which) {
      case 1:
        if (done == false && p && p->advStage >= 0 && lvl > 0 && p->advStage * 5 + lvl > 0) s = "Continue your adventure on\nStage " + std::to_string(adv + 1) + "-" + std::to_string(lvl + 1) + "!";
        else if (adv == -1 && !done) s = isNights ? "New to Peggle Nights?\nClick here to start your adventure!" : "New to Peggle?\nClick here to start your adventure!";
        else s = "Start a new Adventure!\nCan you get a high score?";
        if (done) s = "Start a new Adventure!\nCan you get a high score?";
        break;
      case 2: s = "Play individual levels that you have unlocked in Adventure!"; break;
      case 3: s = "Play against a friend or the computer in this two player mode!"; break;
      case 4: s = isNights ? "Complete all of these extra\ndifficult challenges to boost\nyour Peggle Master rank!" : "Complete all of these extra\ndifficult challenges to become a\nPeggle Grand Master!"; break;
      case 5: s = "Play Online against other players!"; break;
      case 6: s = "View instant replays\nthat you've saved!"; break;
      case 7: s = "Set options like sound volume\nand colorblind mode.\nHelp is also available here."; break;
      case 8: s = "See you again soon!"; break;
      case 9: s = isNights ? "Check your progress\nthrough the game." : "This trophy is awarded for completing Adventure Mode!"; break;
      case 10: s = "This trophy is awarded for beating all of the challenges!"; break;
      case 11: s = "This trophy is awarded for beating all of the challenges and clearing all of the levels!"; break;
    }
    float hh = g.wrappedHeight("PLAIN2", tW, s, 20);
    g.drawWrapped("PLAIN2", {tX, tY + (tH - hh) / 2, tW, tH}, s, col, alpha, Align::Center, 20);
  } else if (!name.empty()) {
    std::string s = adv < 0 ? (isNights ? "Welcome to Peggle Nights," : "Welcome to Peggle,") : "Welcome Back,";
    float ty = tY - 10 + g.fontHeight("PLAIN2BIG");
    g.drawString("PLAIN2BIG", bx + tW / 2 + 10, ty, s, 0x86065d, alpha, Align::Center);
    g.drawString("PLAIN2BIG", bx + 0xfa / 2.0f + (isNights ? 10 : 0), ty + g.fontHeight("PLAIN2BIG") - 10, name + "!", 0x86065d, alpha, Align::Center);
  }
  if (which == 8) {
    float k = 1.0f;
    if (bubbleFade > 0) {
      k = (float)bubbleFade / (float)fadeTicks();
      if (bubble == 8) k = 1.0f - k;
    }
    if (Tex* cry = g.img("IMAGE_MM_CRY")) g.draw(cry, 0xc2, 0xb2, Mode(k));
  }
}

void MainMenu::onButtonPress(int id, int) {
  if (!consoleButton(id)) return;
  consoleFocus_ = id < kConsoleFooter ? id - kConsoleRow : kMenuRows + id - kConsoleFooter;
  placeConsole(consoleLayoutWidth_);
}

void MainMenu::lostFocus() {
  releaseHover();
  if (capture) { capture->mouseLeave(); capture = nullptr; }
  for (Button* b : consoleRows_) if (b) { b->over = false; b->down = false; }
  for (Button* b : consoleFooter_) if (b) { b->over = false; b->down = false; }
}

void MainMenu::setConsolePage(Page page, int focus) {
  slideDirection_ = static_cast<int>(page) < static_cast<int>(page_) ? -1.0f : 1.0f;
  lostFocus();
  page_ = page;
  consoleFocus_ = focus;
  slideTicks_ = page == Page::Lounge ? 0 : kMenuSlide;
  placeConsole(canvasWidth());
}

void MainMenu::consoleAction(int id) {
  if (id == kConsoleFooter) { onButton(8); return; }
  if (id == kConsoleFooter + 2) { onButton(7); return; }
  if (id == kConsoleFooter + 1) {
    if (page_ == Page::Party || page_ == Page::Duel) setConsolePage(Page::Multiplayer, page_ == Page::Party ? 0 : 1);
    else if (page_ == Page::Multiplayer) setConsolePage(Page::Home, 1);
    else if (page_ == Page::Home) app.showModsScreen();
    else if (page_ == Page::Single) setConsolePage(Page::Home, 0);
    else if (page_ == Page::Nights && app.bothGames && app.game != app.homeGame) {
      StartTarget target;
      target.place = StartPlace::SinglePlayer;
      app.requestGameSwitch(app.homeGame, target);
    } else setConsolePage(Page::Single, 0);
    return;
  }
  const int row = id - kConsoleRow;
  if (row < 0 || row >= kMenuRows || slideTicks_) return;
  auto trophies = [&]() {
    if (!app.modeOpen(GameMode::Challenge) && !app.otherOpen(GameMode::Challenge)) onButton(3);
    else {
      app.gameMode = GameMode::Challenge;
      app.showTrophyScreen();
    }
  };
  if (page_ == Page::Home) {
    if (row == 0) setConsolePage(Page::Single, 0);
    else if (row == 1) setConsolePage(Page::Multiplayer, 0);
    else if (row == 3) trophies();
    else onButton(row == 2 ? 4 : 5);
  } else if (page_ == Page::Multiplayer) {
    if (row == 0) setConsolePage(Page::Party, 0);
    else if (row == 1) setConsolePage(Page::Duel, 0);
    else if (row == 2) onButton(2);
    else {
      app.menuPage = static_cast<int>(page_);
      app.menuRow = row;
      online::showOnline(app, online::Flow::Join, false);
    }
  } else if (page_ == Page::Party || page_ == Page::Duel) {
    app.menuPage = static_cast<int>(page_);
    app.menuRow = row;
    online::showOnline(app, row == 0 ? online::Flow::Quick : row == 1 ? online::Flow::Custom : online::Flow::Host, page_ == Page::Party);
  } else if (page_ == Page::Single) {
    if (row == 2) {
      if (app.stages.numAllStages() <= app.stages.numStages()) {
        app.doNotYet("No custom levels are installed. Add a level pack in Mods or make a level in the Level Editor.");
        return;
      }
      app.gameMode = GameMode::QuickPlay;
      app.packStage = app.stages.numStages();
      app.showLevelScreen(false);
    } else onButton(row < 2 ? row : row - 1);
  } else if (page_ == Page::Nights) {
    if (row == 0) startAdventure();
    else if (row == 1) onButton(3);
    else if (row == 2) app.showProgressScreen(2, true);
    else if (row == 3) onButton(9);
    else setConsolePage(Page::Lounge, 0);
  }
}

void MainMenu::drawConsole(Gfx& g) {
  const float width = static_cast<float>(consoleLayoutWidth_);
  const float crop = (width - 1067) * 0.5f;
  const bool home = page_ == Page::Home, online = page_ >= Page::Multiplayer, night = !online && (isNights || page_ == Page::Nights);
  const char* backing = online ? "multiplayer" : night ? "nights" : home ? "home" : "single";
  const char* front = online ? "multiplayer_front" : night ? "nights_front" : home ? "home_front" : "single_front";
  auto* base = menuArt(g, backing);
  const bool original = base != nullptr;
  if (!base) base = g.img("IMAGE_MM_BACKGROUND");
  const float scale = base ? std::max(width / base->lw(), 600 / base->lh()) : 1.0f;
  const float muralW = base ? base->lw() * scale : width, muralH = base ? base->lh() * scale : 600;
  const float muralX = (width - muralW) * 0.5f, muralY = original ? (600 - muralH) * 0.5f : 0;
  g.pushClip(0, 0, width, 600);
  if (base) g.drawStretch(base, muralX, muralY, muralW, muralH);
  else g.fill(0, 0, width, 600, 0x141b3c);
  if (night) menuFit(g, menuArt(g, "moon"), 217 + crop, 84, 181, 181);
  else if (!online || !original) {
    const float sunX = (home ? 513.0f : 303.0f) + crop;
    const float sunY = home ? 168.0f : 196.0f;
    auto* glow = menuArt(g, "sun_glow");
    if (!glow) glow = g.img("IMAGE_SUNGLOW");
    if (glow) {
      const float scale = (home ? 650.0f : 610.0f) / glow->lw();
      g.drawRot(glow, sunX, sunY, sunTick * 0.0008f, scale, scale);
    }
    auto* sun = menuArt(g, "sun");
    if (!sun && g.man().image("IMAGE_MM_BLANKSUN")) sun = g.img("IMAGE_MM_BLANKSUN");
    menuFit(g, sun, sunX - 67, sunY - 67, 134, 134);
  }
  if (auto* foreground = menuArt(g, front); original && foreground) g.drawStretch(foreground, muralX, muralY, muralW, muralH);
  else if (!original && !isNights) {
    if (auto* hill = g.img("IMAGE_MM_FOREGROUND")) {
      const float s = width / hill->lw();
      g.drawStretch(hill, 0, 600 - hill->lh() * s, width, hill->lh() * s);
    }
    if (!home) menuFit(g, g.img("IMAGE_MM_BJORN"), 48 + crop, 100, 442, 436);
  }
  if (home) {
    auto* logo = menuArt(g, night ? "nights_logo" : "logo");
    if (!logo) logo = g.img("IMAGE_LOAD_LOGO");
    if (night && original && logo) {
      const float left = 412 + crop;
      menuFit(g, logo, left, 65, width - 406 - left, 142);
    } else {
      menuFit(g, logo, 185 + crop, 73, 385, 196);
      if (isNights) menuFit(g, g.img("IMAGE_LOAD_NIGHTS"), 235 + crop, 195, 290, 112);
    }
  } else {
    static const char* const singleTips[] = {"Learn from each Master in turn!", "Pick a level. Then play it!", "Play installed level packs.",
      "Beat the Masters at their own game!", "Test your skills with these tough challenges!"};
    static const char* const nightsTips[] = {"Embark on a Peggle Nights adventure!", "Try the Peggle Nights challenges!",
      "Visit the Trophy Room.", "Check your Peggle Nights progress.", "Explore the original Nights lounge."};
    static const char* const multiTips[] = {"Party with up to 4 players!", "A remote two-player battle!", "A local two-player funfest!",
      "Join a friend's room with a code or invite link.", ""};
    static const char* const partyTips[] = {"Hop right into a Peg Party!", "See a list of available games!", "Host your own match.", "", ""};
    static const char* const duelTips[] = {"Hop right into a duel!", partyTips[1], "Host your own duel!", "", ""};
    const int index = consoleFocus_ < kMenuRows ? consoleFocus_ : 0;
    const bool hub = page_ == Page::Nights;
    const bool wide169 = consoleLayoutWidth_ == 1067;
    const char* tip = hub ? nightsTips[index] : page_ == Page::Multiplayer ? multiTips[index] : page_ == Page::Party ? partyTips[index]
                    : page_ == Page::Duel ? duelTips[index] : singleTips[index];
    if (!app.getDialog(D_Locked)) {
      int rows = 0;
      for (Button* b : consoleRows_) rows += b->visible;
      const float bx = (page_ == Page::Multiplayer ? 450 : online ? 441 : 404) + crop;
      const float by = online ? 140.0f + rows * 59 : 102.0f;
      if (auto* bubbleArt = menuArt(g, "speech")) {
        if (online) menuMirror(g, bubbleArt, bx, by, 232 / bubbleArt->lw());
        else g.drawStretch(bubbleArt, bx, by, 232, 120);
      }
      else if (!isNights && g.man().image("IMAGE_MM_SPEECHBUBBLE")) g.drawStretch(g.img("IMAGE_MM_SPEECHBUBBLE"), bx, by, 231, 116);
      else {
        constexpr std::array<uint32_t, 6> cream{0xfff7e4, 0xfffbe9, 0xfffbe9, 0xfffbe9, 0xfff5da, 0xe5bbd0};
        menuBand(g, {bx, by, 231, 100}, 22, cream);
      }
      const char* font = "OVERLOAD16";
      const float textH = g.wrappedHeight(font, 199, tip, 21);
      g.drawWrapped(font, {bx + 18, by + 49 - textH * 0.5f, 199, 82}, tip, 0x251c23, 1, Align::Left, 21);
    }

    const float cx = online ? 560 + crop : width - (wide169 ? 220 : 215);
    const char* title = hub ? "Peggle Nights" : page_ == Page::Multiplayer ? "Multiplayer" : page_ == Page::Party ? "Online Peg Party"
                      : page_ == Page::Duel ? "Online Duel" : "Single Player";
    if (auto* header = menuArt(g, "header")) {
      auto* rail = menuArt(g, "header_rail");
      if (online && rail) {
        g.drawStretch(rail, 0, 0, cx - 198, 54);
        auto* cap = page_ == Page::Multiplayer ? menuArt(g, "header_cap") : nullptr;
        g.drawStretch(rail, cx + 198, 0, cap ? 102 : width - cx - 198, 54);
        if (cap) menuMirror(g, cap, cx + 300, -7, .9f);
      } else if (hub && rail) {
        g.drawStretch(rail, 0, 0, cx - (wide169 ? 198 : 146), 54);
      } else if (auto* cap = menuArt(g, "header_cap")) {
        if (wide169 && rail) g.drawStretchSrc(rail, 300, 0, 100, rail->lh(), cx - 275, 0, 77, 54);
        g.drawStretch(cap, cx - (wide169 ? 402 : 333), wide169 ? -7.0f : -18.0f, 153, 89);
      }
      const float headerW = wide169 || online ? 466.0f : 396.0f;
      g.drawStretch(header, cx - headerW / 2, wide169 || online ? -5.0f : 0.0f, headerW, wide169 || online ? 158.0f : 143.0f);
    } else {
      menuBand(g, {cx - 333, -3, 578, 54}, 24, kPipe);
      menuBand(g, {cx - 159, 1, 318, 118}, 33, kPipe);
      menuBand(g, {cx - 137, 18, 274, 91}, 24, kGold);
      menuBand(g, {cx - 131, 23, 262, 80}, 21, kSign);
    }
    menuTextFit(g, "OVERLOAD24", cx, 68, title, 0x153d27, 226, .80f);
    menuTextFit(g, "OVERLOAD24", cx, 66, title, 0xd6fc5b, 226, .80f);
  }

  if (app.getDialog(D_Locked)) { g.popClip(); return; }
  drawConsoleRail(g, width);
  if (!app.helpOverlay()) {
    const float nextPrompt = drawConsolePrompt(g, 66, 0, "ENTER", "Select", app.pad);
    if (!home) drawConsolePrompt(g, nextPrompt, 1, "ESC", "Back", app.pad);
  }
  g.popClip();
}

void MainMenu::drawLayer(Gfx& g) {
  placeConsole(canvasWidth());
  if (consoleLayoutWidth_ >= 960) { drawConsole(g); return; }
  if (isNights) { drawNights(g); return; }
  float dim = bubbleFade > 0 && false ? 0.5f : 1.0f;
  (void)dim;
  if (Tex* bg = g.img("IMAGE_MM_BACKGROUND")) g.draw(bg, 15, 11);
  float sunX = -170, sunY = -190;
  if (Tex* glow = g.img("IMAGE_SUNGLOW")) g.drawRot(glow, sunX + 0x18a, sunY + 0x17f, 0.05f * (float)sunTick * 3.14159265f / 180.0f, 1, 1);
  if (Tex* bs = g.img("IMAGE_MM_BLANKSUN")) g.draw(bs, sunX + 0x13f, sunY + 0x133);
  if (Tex* fg = g.img("IMAGE_MM_FOREGROUND")) g.draw(fg, 15, 600 - fg->lh() - 0x3c);
  if (Tex* bj = g.img("IMAGE_MM_BJORN")) g.draw(bj, 15, 0x82);
  const float fade = (float)bubbleFade / (float)fadeTicks();
  if (bubbleFade > 0) drawSpeechBubble(g, bubbleOld, fade, true);
  drawSpeechBubble(g, bubble, 1.0f - fade, bubbleFade == 0);
  if (trophyTier > 0) {
    if (Tex* tr = g.img("IMAGE_MM_TROPHIES")) {
      int cel = std::clamp(trophyTier - 1, 0, 2);
      if (trophyAnim < trophyAnimMax) {
        float k = (float)trophyAnim / (float)trophyAnimMax;
        float hgt = tr->lh() * 0.75f * (1.0f - k);
        g.pushClip(0x138, hgt + 0xb5, tr->cw(), tr->lh() - hgt);
        g.drawCel(tr, 0x138, 0xb5, cel, 0);
        g.popClip();
      } else {
        g.drawCel(tr, 0x138, 0xb5, cel, 0);
      }
    }
  }
  if (Tex* l = g.img("IMAGE_MM_LEFT")) g.draw(l, 0, 0);
  if (Tex* t = g.img("IMAGE_MM_TOP")) g.draw(t, 0x2e, 0);
  if (Tex* r = g.img("IMAGE_MM_RIGHT")) g.draw(r, 800 - r->lw(), 0);
  if (Tex* b = g.img("IMAGE_MM_BOTTOM")) g.draw(b, 0x40, 600 - b->lh());
}

void MainMenu::drawOverlay(Gfx& g) {
  if (consoleLayoutWidth_ >= 960) {
    if (isNights && page_ == Page::Single && (app.menuNewLevels || app.menuUnlocked)) {
      for (int i : {1, 3, 4}) {
        if (i != 4 && !app.menuNewLevels) continue;
        if (i == 4 && !app.menuUnlocked && !app.menuNewLevels) continue;
        const auto* b = consoleRows_[i];
        menuText(g, "OVERLOAD16OUTLINE", b->x + b->w - 39, b->y + b->h, i == 4 && app.menuUnlocked ? "UNLOCKED" : "NEW LEVELS", 0xffe370);
      }
    }
    return;
  }
  if (!isNights || !(app.menuNewLevels || app.menuUnlocked)) return;
  const char* font = "OVERLOAD16OUTLINE";
  if (app.menuNewLevels) {
    float x = bSolo->x + 0x3d;
    g.drawString(font, x, bSolo->y + 0x61, "NEW LEVELS");
    g.drawString(font, x, bDuel->y + 0x61, "NEW LEVELS");
    float below = app.menuUnlocked ? g.fontHeight(font) + 0x61 : 0x61;
    g.drawString(font, x, bChal->y - 0xb + below, "NEW LEVELS");
  }
  if (app.menuUnlocked) g.drawString(font, bSolo->x + 0x42, bChal->y + 0x56, "UNLOCKED");
}

bool MainMenu::input(const Input& in) {
  if (consoleLayoutWidth_ == 800 && isNights && in.type == Input::MouseDown && in.button == 1 && !app.hasModal() && !app.moving()) {
    int st = -1;
    if (decalAt(app, in.x - x, in.y - y, &st)) {
      app.gameMode = GameMode::QuickPlay;
      app.packStage = st;
      app.playSample("BUTTON1");
      app.showLevelScreen(false);
      return true;
    }
  }
  return Panel::input(in);
}

void MainMenu::keyDown(int key, int mod) {
  if (app.hasModal()) return;
  if (page_ == Page::Lounge && key == SDLK_ESCAPE) { onButton(kClassicAction); return; }
  if (consoleLayoutWidth_ >= 960 && mouseVisible && !app.moving()) {
    placeConsole(consoleLayoutWidth_);
    const int total = kMenuRows + kFooterButtons;
    const int old = consoleFocus_;
    auto shown = [&](int i) { return (i < kMenuRows ? consoleRows_[i] : consoleFooter_[i - kMenuRows])->visible; };
    auto next = [&](int from, int start, int count, int step) {
      int i = from - start;
      do {
        i = (i + count + step) % count;
      } while (!shown(start + i));
      return start + i;
    };
    if (key == SDLK_UP) consoleFocus_ = next(old < kMenuRows ? old : 0, 0, kMenuRows, -1);
    else if (key == SDLK_DOWN) consoleFocus_ = old < kMenuRows ? next(old, 0, kMenuRows, 1) : 0;
    else if (key == SDLK_TAB) consoleFocus_ = next(old, 0, total, mod & SDL_KMOD_SHIFT ? -1 : 1);
    else if (key == SDLK_LEFT || key == SDLK_RIGHT)
      consoleFocus_ = old < kMenuRows ? kMenuRows : next(old, kMenuRows, kFooterButtons, key == SDLK_LEFT ? -1 : 1);
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
      if (slideTicks_) return;
      app.playSample("BUTTON1");
      consoleAction(old < kMenuRows ? kConsoleRow + old : kConsoleFooter + old - kMenuRows);
      return;
    } else if (key == SDLK_ESCAPE && page_ != Page::Home) { consoleAction(kConsoleFooter + 1); return; }
    if (old != consoleFocus_) { lostFocus(); placeConsole(consoleLayoutWidth_); app.playSample("MOUSEOVER"); return; }
  }
  if (key == SDLK_ESCAPE) app.doConfirmQuitDialog();
  else if (key == SDLK_F9) app.showEditor();
}

void MainMenu::onButton(int id) {
  if (consoleButton(id)) { consoleAction(id); return; }
  if (id == kClassicAction) {
    if (page_ == Page::Lounge) setConsolePage(Page::Nights, 4);
    return;
  }
  if (online::handleMenuButton(app, id)) return;
  if (id >= 1 && id <= 3) app.menuNewLevels = false;
  if (id == 3) app.menuUnlocked = false;
  switch (id) {
    case 0:
      if (app.bothGames) app.doAdventureQuestion();
      else chooseAdventure(app.game);
      break;
    case 1:
      app.gameMode = GameMode::QuickPlay;
      if (sessionsOf(app).askToContinue(true)) break;
      startGame();
      break;
    case 2:
      app.gameMode = GameMode::Duel;
      if (sessionsOf(app).askToContinue(true)) break;
      startGame();
      break;
    case 3:
      app.gameMode = GameMode::Challenge;
      if (sessionsOf(app).askToContinue(true)) break;
      startGame();
      break;
    case 4: app.doReplaysDialog(); break;
    case 5: app.doOptionsDialog(); break;

    case 7: app.doConfirmQuitDialog(); break;
    case 8:
      app.playSample("BUTTON1");
      app.doUserDialog();
      break;
    case 9: app.showAdventureScreen(); break;
  }
}

void MainMenu::chooseAdventure(Game edition) {
  const bool hub = edition == Game::Nights && page_ == Page::Single && consoleWidth() >= 960;
  if (edition != app.game) {
    StartTarget target;
    target.place = hub ? StartPlace::NightsHub : StartPlace::Adventure;
    app.requestGameSwitch(edition, target);
  } else if (hub) setConsolePage(Page::Nights, 0);
  else startAdventure();
}

void MainMenu::restoreAdventurePage(App& app, StartPlace place) {
  const bool hub = place == StartPlace::NightsHub && app.game == Game::Nights;
  if (app.consoleCanvasWidth() >= 960) {
    app.menuPage = static_cast<int>(hub ? Page::Nights : Page::Single);
    app.menuRow = 0;
  }
  app.showMainMenu(hub);
}

void MainMenu::startAdventure() {
  app.gameMode = GameMode::Adventure;
  app.curStage = advStage;
  app.curLevel = advLevel;
  if (sessionsOf(app).askToContinue(true)) return;
  startGame();
}

void MainMenu::startGame() {
  PlayerInfo* p = app.curProfile;
  switch (app.gameMode) {
    case GameMode::Adventure:
      if (p && p->advCompletions > 0) {
        if (p->justBeatAdventure()) p->restartAdventure();
        if (!isNights) {
          app.showAdventureScreen();
          return;
        }
      }
      app.startAdventureGame();
      return;
    case GameMode::QuickPlay:
    case GameMode::Duel:
      if (app.gameMode == GameMode::Duel && p && p->maxStage < 1 && !app.otherOpen(GameMode::Duel)) {
        app.doNotYet(consoleLayoutWidth_ >= 960 ? "You'll need to complete the first stage of Adventure Mode before this option becomes available." :
                     "^FFFF00^DUEL MODE LOCKED!^oldclr^\nThis mode lets you compete against a friend or the computer.\n\n^FFFF00^To Unlock:^oldclr^\nFinish Stage 1 of Adventure Mode!");
        return;
      }
      app.packStage = -1;
      app.showLevelScreen(false);
      return;
    case GameMode::Challenge:
      if (p && (app.game == Game::Nights ? p->maxStage < 3 : p->advCompletions <= 0) && !app.otherOpen(GameMode::Challenge)) {
        app.doNotYet(consoleLayoutWidth_ >= 960 ? (isNights ? "You'll need to complete the third stage of Adventure Mode before this option becomes available." :
                     "You'll need to complete Adventure Mode before this option becomes available.") : "^FFFF00^CHALLENGE MODE LOCKED!^oldclr^\nThis mode presents " + std::to_string(app.trophies.numTrophies()) + " challenges for advanced players.\n\n^FFFF00^To Unlock:^oldclr^\n" +
                     (app.game == Game::Nights ? "Finish Stage 3 of Adventure Mode!" : "Complete Adventure Mode!"));
        return;
      }
      app.showTrophyScreen();
      return;
    default: return;
  }
}

}
