#include "online/online_ui.h"
#include "ui/screens.h"

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
  playBtn->hoverSound = "MOUSEOVER";
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

void LoadingScreen::drawLayer(Gfx& g) {
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
  if (Tex* sh = g.img("IMAGE_LOAD_SHADOW")) g.draw(sh, 162, 50 + 290, Mode(120 * visualProgress * visualProgress / 255.0f, 0x101010));
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
  if (nights) {
    Tex* fly = g.img("IMAGE_LOAD_FLY0");
    nights->drawOverlay(cmds, fly && fly->valid() ? (int)fly->lw() : 33);
    renderNights(g);
    return;
  }
  int cnt = updateCount;
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

bool LoadingScreen::input(const Input& in) {
  if (in.type == Input::KeyDown && loaded && (in.key == SDLK_RETURN || in.key == SDLK_SPACE)) {
    onButton(0);
    return true;
  }
  if (in.type == Input::MouseMove || in.type == Input::MouseDown || in.type == Input::MouseUp) {
    mouseX = (int)in.x;
    mouseY = (int)in.y;
  }
  bool used = Panel::input(in);
  if (nights && !used && in.type == Input::MouseDown && mouseVisible) {
    int popped = nights->click(mouseX, mouseY);
    for (int i = 0; i < popped; i++) app.playSample("FAIRY_POP");
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

MainMenu::MainMenu(App& a) : Screen(a) {
  isNights = a.game == Game::Nights;
  auto emb = [&](int id, const char* img, float pitch) {
    Button* b = embeddedButton(id, img, 0, 0);
    b->additiveDown = true;
    b->hoverPitch = pitch;
    return b;
  };
  bAdv = emb(0, "IMAGE_MM_ADVENTURE", 0);
  bSolo = emb(1, "IMAGE_MM_SOLO", 2);
  bDuel = emb(2, "IMAGE_MM_DUEL", isNights ? 4.0f : 5.0f);
  bChal = emb(3, "IMAGE_MM_CHALLENGE", isNights ? 5.0f : 4.0f);
  auto brown = [&](int id, const char* label, float pitch) {
    Button* b = patchButton(id, label, "IMAGE_DLG_BROWNBUTTON1", 0, 0);
    b->hoverPitch = pitch;
    return b;
  };
  bReplay = brown(4, "Replays", 7);
  bOptions = brown(5, "Options", 9);
  bDemo = isNights ? brown(9, "Stats", 11) : brown(6, "Demos", 11);
  bQuit = patchButton(7, "Quit", "IMAGE_DLG_PURPLEBUTTON", 7, 0x215);
  bQuit->hoverPitch = isNights ? 12.0f : 13.0f;
  online::addMainMenuButton(*this);
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
  app.playMusic("menu");
  placeButtons();
  syncPlayerInfo();
  for (Button* b : {bAdv, bSolo, bDuel, bChal, bReplay, bOptions, bDemo, bQuit}) b->clickSound = "BUTTON1";
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
      if ((int)p->trophies.size() >= (int)app.trophies.numTrophies()) {
        trophyTier = 2;
        if (p->clearedLevels.size() >= app.stages.numLevels()) trophyTier = 3;
      }
    }
    if (prev < trophyTier) {
      app.curProfile->trophyLevelSeen = (uint16_t)trophyTier;
      app.curProfile->dirty = true;
      if (trophyTier >= 1) { trophyAnim = 0; trophyAnimMax = 400; }
      app.saveProfile();
    }
  }
  bLink->visible = p != nullptr && bubble == 0;
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
  bool any = false;
  for (Button* b : {bAdv, bSolo, bDuel, bChal, bReplay, bOptions, bDemo, bQuit}) any = any || b->over || b->down;
  if (false) {} 
  bool unused_ = any; (void)unused_;
  updateBubbles();
  bLink->visible = app.curProfile != nullptr && bubble == 0;
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
  bLink->y = 0x88;
}

void MainMenu::updateBubbles() {
  bool any = false;
  for (Button* b : {bAdv, bSolo, bDuel, bChal, bReplay, bOptions, bDemo, bQuit}) any = any || b->over || b->down;
  if (app.getDialog(D_Locked) || app.hasModal()) {
  } else if (!any) {
    if (!isNights && trophyTier > 0 && app.mouseX >= 330 && app.mouseX < 490 && app.mouseY >= 200 && app.mouseY < 425) setBubble(trophyTier + 8, 50);
    else setBubble(0, 15);
  } else {
    if (bQuit->over) setBubble(8, 15);
    else if (bOptions->over) setBubble(7, 15);
    else if (bReplay->over) setBubble(6, 15);
    else if (bDemo->over) setBubble(isNights ? 9 : 5, 15);
    else if (bDuel->over) setBubble(3, 15);
    else if (bChal->over) setBubble(4, 15);
    else if (bSolo->over) setBubble(2, 15);
    else if (bAdv->over) setBubble(1, 15);
  }
}

void MainMenu::onButtonEnter(int id) {
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
      case 5: s = "Watch the Peggle Masters play and see if you can learn their tricks!"; break;
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
    g.drawString("PLAIN2BIG", bx + 0xfa / 2.0f + 10, ty + g.fontHeight("PLAIN2BIG") - 10, name + "!", 0x86065d, alpha, Align::Center);
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

void MainMenu::drawLayer(Gfx& g) {
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
  if (isNights && in.type == Input::MouseDown && in.button == 1 && !app.hasModal() && !app.moving()) {
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

void MainMenu::keyDown(int key, int) {
  if (app.hasModal()) return;
  if (key == SDLK_ESCAPE) app.doConfirmQuitDialog();
  else if (key == SDLK_F9) app.showEditor();
}

void MainMenu::onButton(int id) {
  if (online::handleMenuButton(app, id)) return;
  const PlayerInfo* p = app.curProfile;
  if (id >= 1 && id <= 3) app.menuNewLevels = false;
  if (id == 3) app.menuUnlocked = false;
  switch (id) {
    case 0:
      if (app.bothGames) app.doAdventureQuestion();
      else startAdventure();
      break;
    case 1:
      app.gameMode = GameMode::QuickPlay;
      startGame();
      break;
    case 2:
      app.gameMode = GameMode::Duel;
      startGame();
      break;
    case 3:
      app.gameMode = GameMode::Challenge;
      startGame();
      break;
    case 4: app.doReplaysDialog(); break;
    case 5: app.doOptionsDialog(); break;
    case 6:
      if (p && p->maxStage < 2) {
        app.doNotYet("^FFFF00^DEMO MODE LOCKED!^oldclr^\nThis mode shows how the Masters play.\n\n^FFFF00^To Unlock:^oldclr^\nFinish Stage 2 of Adventure Mode!");
        return;
      }
      app.gameMode = GameMode::Demo;
      app.curStage = (int)(app.rand() % (unsigned)(std::min(p ? p->maxStage : 0, app.numStages() - 1) + 1));
      app.curLevel = (int)(app.rand() % 5u);
      app.showBoard(true, true);
      break;
    case 7: app.doConfirmQuitDialog(); break;
    case 8:
      app.playSample("BUTTON1");
      app.doUserDialog();
      break;
    case 9: app.showAdventureScreen(); break;
  }
}

void MainMenu::startAdventure() {
  app.gameMode = GameMode::Adventure;
  app.curStage = advStage;
  app.curLevel = advLevel;
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
      if (app.gameMode == GameMode::Duel && p && p->maxStage < 1) {
        app.doNotYet("^FFFF00^DUEL MODE LOCKED!^oldclr^\nThis mode lets you compete against a friend or the computer.\n\n^FFFF00^To Unlock:^oldclr^\nFinish Stage 1 of Adventure Mode!");
        return;
      }
      app.packStage = -1;
      app.showLevelScreen(false);
      return;
    case GameMode::Challenge:
      if (p && (app.game == Game::Nights ? p->maxStage < 3 : p->advCompletions <= 0)) {
        app.doNotYet("^FFFF00^CHALLENGE MODE LOCKED!^oldclr^\nThis mode presents " + std::to_string(app.trophies.numTrophies()) + " challenges for advanced players.\n\n^FFFF00^To Unlock:^oldclr^\n" +
                     (app.game == Game::Nights ? "Finish Stage 3 of Adventure Mode!" : "Complete Adventure Mode!"));
        return;
      }
      app.showTrophyScreen();
      return;
    default: return;
  }
}

}
