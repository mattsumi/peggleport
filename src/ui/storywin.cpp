#include <algorithm>
#include <cmath>

#include "ui/screens.h"

namespace pp::ui {

namespace {

int centred(float width) {
  int w = (int)width;
  return w / 2;
}

int cut(double v) { return (int)v; }

}

void StoryScreen::initWin() {
  const StageInfo* info = app.stages.stage(std::max(0, app.numStages() - 1));
  tier = 0;
  if (info) {
    const PlayerInfo* p = app.curProfile;
    if (p && (int)app.trophies.numHeld(p->trophies) >= (int)app.trophies.numTrophies()) {
      tier = 1;
      if (app.stages.numClearedOwn(p->clearedLevels) >= app.stages.numLevels()) tier = 2;
    }
    if (easter) tier = 3;
    if (info->story[tier].empty() && info->credits[tier].empty()) tier = 0;
  }
  bContinue->visible = false;
  auto lk = std::make_unique<Button>();
  lk->id = 1;
  lk->kind = Button::Kind::Link;
  lk->label = "Continue";
  lk->font = "OVERLOAD24OUTLINE";
  lk->colNormal = 0xffff88;
  lk->colOver = 0x88ff88;
  lk->w = app.gfx.textWidth(lk->font, lk->label);
  lk->h = 18 + app.gfx.fontHeight(lk->font);
  lk->x = (float)(400 - centred(lk->w));
  lk->y = 600 - 35;
  lk->tickRef = &updateCount;
  bLink = add(std::move(lk));
  fw = std::make_unique<StoryFireworks>(app.crtRand(), 800, 600);
  app.playMusic("beats:62");
}

void StoryScreen::updateWin() {
  if (fw && fw->update(updateCount)) app.playSample("FIREWORKPOP");
}

void StoryScreen::drawWin(Gfx& g) {
  g.fill(0, 0, 800, 600, 0x000000);
  if (Tex* bg = g.img("IMAGE_STORY_WIN")) g.draw(bg, (float)((800 - (int)bg->lw()) / 2), 0);
  g.fill(283, 118, 262, 294, 0x6b31ad);
  g.fill(313, 412, 232, 20, 0x6b31ad);
  const StageInfo* info = app.stages.stage(std::max(0, app.numStages() - 1));
  if (!info) return;
  g.pushClip(280, 117, 266, 316);
  const int baseY = cut(455.0 - (double)(fw ? fw->scroll() : 0.0f));
  if (tier < 3) {
    int off = tier == 1 ? 60 : tier == 2 ? 125 : 0;
    if (Tex* tr = g.img("IMAGE_MM_TROPHIES")) g.drawCel(tr, 305, (float)(160 + off + baseY), tier);
  }
  int y = baseY;
  const int cx = 280 + 266 / 2;
  for (const StoryData& d : info->credits[tier]) {
    std::string font;
    uint32_t color;
    if (d.arg == 2) {
      y += 20;
      continue;
    } else if (d.arg == 0) {
      font = "OVERLOAD16OUTLINE";
      color = 0xffff88;
    } else if (d.arg == 1) {
      font = "OVERLOAD24GRAD2";
      color = 0xffff00;
    } else {
      font = "PLAIN2OUTLINE";
      color = 0xffffaa;
    }
    y += (int)g.fontAscent(font);
    g.drawString(font, (float)(cx - centred(g.textWidth(font, d.text))), (float)y, d.text, color);
  }
  g.popClip();
  if (Tex* claws = g.img("IMAGE_STORY_CLAWS")) g.draw(claws, 522, 386);
  drawFireworks(g);
}

void StoryScreen::drawFireworks(Gfx& g) {
  if (!fw) return;
  Tex* fire = g.img("IMAGE_FIREBALLFIRE");
  Tex* sparkle = g.img("IMAGE_STORY_BIGSPARKLE");
  Tex* streak = g.img("IMAGE_STREAK");
  Tex* nuke = g.img("IMAGE_NUKESTREAK");
  Tex* rocket = g.img("IMAGE_STORY_ROCKET");
  for (const StoryFireworks::Particle& p : fw->particles()) {
    const float ratio = (float)p.age / (float)p.life;
    const int fade = cut((double)255 - (double)(ratio * 255.0f));
    const uint32_t rgb = p.color & 0xffffff;
    if (p.type == 1) {
      if (!fire) continue;
      int n = cut((double)(ratio * 30.0f));
      int a = 40 - n;
      float cw = fire->cw(), ch = fire->ch();
      g.drawStretchSrc(fire, 0, ch * n, cw, ch, (float)cut((double)p.x - 4), (float)cut((double)p.y - 4), 9, 9, Mode(a / 255.0f, 0x000000));
    } else if (p.type == 0) {
      if (!sparkle) continue;
      int n = cut((double)(ratio * 14.0f));
      float cw = sparkle->cw(), ch = sparkle->ch();
      g.drawStretchSrc(sparkle, cw * n, 0, cw, ch, (float)cut((double)p.x - 15), (float)cut((double)p.y - 15), 30, 30, Mode(120 / 255.0f, rgb, true));
    } else if (p.type == 2 || p.type == 4) {
      if (!nuke) continue;
      g.drawStretch(nuke, (float)cut((double)p.x - 6), (float)cut((double)p.y - 6), 13, 13, Mode(fade / 255.0f, rgb, true));
    } else if (p.type == 3) {
      if (!streak) continue;
      double rad = std::atan2((double)p.vx, (double)p.vy) + 3.1415;
      g.drawRot(streak, p.x, p.y, (float)rad, 0.75f, 1.0f, Mode(fade / 255.0f, rgb, true));
    }
  }
  if (fw->rocketFlying() && rocket) {
    double rad = std::atan2((double)fw->rocketVX(), (double)fw->rocketVY()) + 3.1415;
    g.drawRot(rocket, fw->rocketX(), fw->rocketY(), (float)rad, 0.75f, 0.5f, Mode(1.0f));
  }
}

}
