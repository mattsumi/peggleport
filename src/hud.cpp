#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

#include "render.h"

namespace pp {

namespace {

std::string commaSep(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v), out;
  for (size_t i = 0; i < s.size(); i++) {
    if (i && (s.size() - i) % 3 == 0) out += ',';
    out += s[i];
  }
  return v < 0 ? "-" + out : out;
}
}

void Renderer::drawFrame(int dw) {
  struct P { const char* id; int x, y; int anchor; int stretch; bool whole; };
  const P pieces[] = {
      {"IMAGE_IP_TLC", 0, 0, 0, 0, false},      {"IMAGE_IP_TL1", 130, 42, 0, dw / 2, false}, {"IMAGE_IP_TL2", 292, 0, 1, 0, false},
      {"IMAGE_IP_TR2", 477, 0, 1, 0, false},    {"IMAGE_IP_TR1", 511, 42, 1, dw / 2, false}, {"IMAGE_IP_TRC", 673, 0, 2, 0, false},
      {"IMAGE_IP_L1", 0, 147, 0, 0, false},     {"IMAGE_IP_L2", 66, 147, 0, 0, false},       {"IMAGE_IP_R1", 722, 147, 2, 0, false},
      {"IMAGE_IP_R2", 795, 147, 2, 0, false},   {"IMAGE_IP_BLC", 0, 499, 0, 0, false},       {"IMAGE_IP_B", 99, 591, 0, dw, false},
      {"IMAGE_IP_BRC", 707, 499, 2, 0, false},  {"IMAGE_IP_BALLOTRON", 0, 62, 0, 0, true},   {"IMAGE_IP_FEVERMETER", 714, 62, 2, 0, true},
      {"IMAGE_IP_LWINDOW", 110, 0, 0, dw / 2, true}, {"IMAGE_IP_RWINDOW", 488, 0, 1, dw / 2, true},    {"IMAGE_IP_SHOOTERORB", 312, 0, 1, 0, true},
  };
  struct R { int x, y, w, h; };
  const R regions[] = {{88, 0, 624 + dw, 74}, {345 + dw / 2, 74, 110, 70}, {0, 0, 88, 600}, {712 + dw, 0, 88, 600}, {88, 586, 624 + dw, 14}};
  for (auto& rg : regions) {
    SDL_Rect clip{(int)std::floor(px((float)rg.x)), (int)std::floor(py((float)rg.y)), (int)std::ceil(rg.w * s_), (int)std::ceil(rg.h * s_)};
    SDL_SetRenderClipRect(r_, &clip);
    for (auto& p : pieces) {
      Tex* t = img(p.id);
      if (!t) continue;
      float x = (float)p.x + (p.anchor == 2 ? (float)dw : p.anchor == 1 ? dw / 2.0f : 0.0f);
      if (p.whole && p.stretch > 0) {
        const float cap = 18, w = t->lw(), h = t->lh(), ex = (float)p.stretch;
        put(t, {0, 0, cap, h}, x, (float)p.y);
        put(t, {cap, 0, w - 2 * cap, h}, x + cap, (float)p.y, {}, w - 2 * cap + ex, h);
        put(t, {w - cap, 0, cap, h}, x + w - cap + ex, (float)p.y);
      } else if (p.whole) {
        put(t, {0, 0, t->lw(), t->lh()}, x, (float)p.y);
      } else {
        int cel = std::clamp(stageCel_, 0, std::max(t->rows, t->cols) - 1);
        int col = t->cols > 1 ? cel : 0, row = t->cols > 1 ? 0 : cel;
        put(t, {col * t->cw(), row * t->ch(), t->cw(), t->ch()}, x, (float)p.y, {}, t->cw() + p.stretch, t->ch());
      }
    }
  }
  SDL_Rect all{(int)std::floor(px(0)), (int)std::floor(py(0)), (int)std::ceil((800 + dw) * s_), (int)std::ceil(600 * s_)};
  SDL_SetRenderClipRect(r_, &all);
}

void Renderer::drawBallColumn(const Board& b) {
  Tex* back = img("IMAGE_INT_BALLBACK");
  Tex* ball = img("IMAGE_INT_BALL");
  Tex* glass = img("IMAGE_INT_BALLGLASS");
  if (back) put(back, {0, 0, back->lw(), back->lh()}, 24, 180);
  int n = b.ballsLeft[b.cur];
  if (b.fever && b.feverBallsPaid > 0) n = std::max(0, n - b.feverBallsPaid);
  int shown = std::min(n, 15);
  int spacing = ball ? (int)ball->lh() : 20;
  int bottom = 410 + 3 * shown;
  if (back) put(back, {0, 0, back->lw(), back->lh()}, 28, (float)(17 + bottom));
  int vis = std::min(n, 25);
  for (int i = 0; i < vis; i++) put(ball, {0, 0, ball->lw(), ball->lh()}, 30, (float)(bottom - i * spacing));
  if (glass) put(glass, {0, 0, glass->lw(), glass->lh()}, 3, 92);
}

void Renderer::drawOrbBack(const Board& b, const Hud& hud, int dw) {
  float cxs = (float)dw / 2;
  if (Tex* t = img("IMAGE_IP_SHOOTERBACK")) {
    int cel = std::clamp(stageCel_, 0, t->rows - 1);
    put(t, {0, cel * t->ch(), t->cw(), t->ch() - 1}, 333 + cxs, 25);
  }
  Tex* sh = img("IMAGE_IP_SHOOTER");
  Tex* seams = img("IMAGE_IP_SEAMS");
  if (sh) {
    int row = 0;
    if (b.turns[b.cur] > 0 && b.master[b.cur]) row = 1;
    if (b.phase == Phase::Shot && b.shotSteps < 4) row = 2;
    V2 pv = b.pivot();
    float px0 = BX + pv.x, py0 = BY + pv.y;
    float rot = b.gunAngle + 1.5707964f - 6.2831855f;
    float fw = 82, fh = 64;
    float rx = fw / 2, ry = fh / 2 - 72;
    float ox = px0 - rx + 0.5f, oy = py0 - ry + 0.5f;
    float pivx = rx, pivy = ry;
    if (seams) put(seams, {0, std::clamp(stageCel_, 0, seams->rows - 1) * seams->ch(), seams->cw(), seams->ch()}, ox, oy + 1, {}, -1, -1, rot, pivx, pivy - 1);
    put(sh, {0, row * sh->ch(), sh->cw(), sh->ch()}, ox + 10, oy + 3, {}, -1, -1, rot, pivx - 10, pivy - 3);
  }
  if (b.phase == Phase::Aim) {
    Ball g;
    g.r = b.physics.ballRadius;
    g.pos = b.muzzleArmed(b.gunAngle);
    drawBall(b, g, true);
  }
  (void)hud;
}


void Renderer::drawPortrait(const Board& b, int dw) {
  if (!charsLoaded_) {
    chars_ = loadCharacters(vfs_);
    charsLoaded_ = true;
  }
  static const char* powerNames[] = {"None", "Guide", "Flippers", "Multiball", "Pyramid", "SpaceBlast", "SpookyBall", "ZenBall",
                                     "ShotExtender", "FlowerPower", "Nudge", "TimeBomb", "Fireball", "LuckySpin", "ChainLightning"};
  int pw = b.master[b.cur];
  if (pw < 0 || pw > 14) pw = 0;
  const CharInfo* ci = nullptr;
  for (auto& c : chars_)
    if (c.power == powerNames[pw]) ci = &c;
  if (!ci && !chars_.empty() && pw == 0) ci = &chars_[0];
  if (!ci) return;
  Portrait& P = portrait_;
  if (P.key != ci->image) {
    if (P.tex) SDL_DestroyTexture(P.tex);
    P = Portrait();
    P.key = ci->image;
    P.info = *ci;
    auto f = loadGameImage(vfs_, "characters/" + ci->image);
    auto m = loadGameImage(vfs_, "characters/" + ci->image + "MASK");
    auto e = loadGameImage(vfs_, "characters/" + ci->eyeImage);
    if (f) {
      P.frames = *f;
      if (m) P.mask = *m;
      if (e) P.eye = *e;
      P.tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, P.frames.w, P.frames.h / std::max(1, ci->frames));
      if (P.tex) {
        SDL_SetTextureBlendMode(P.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(P.tex, SDL_SCALEMODE_LINEAR);
        P.ok = true;
      }
    }
  }
  if (!P.ok) return;
  const int fw = P.frames.w, fh = P.frames.h / std::max(1, ci->frames);
  const float x0 = 355.0f + dw / 2.0f + ci->posX, y0 = 24.0f + ci->posY;
  V2 pv = b.pivot();
  V2 look = V2(BX + pv.x, BY + pv.y) + V2(std::cos(b.gunAngle), -std::sin(b.gunAngle)) * 200.0f;
  if (!b.balls.empty()) look = V2(BX + b.balls[0].pos.x, BY + b.balls[0].pos.y);
  if (P.lastTick < 0 || b.tick < P.lastTick) P.ticks = 0;
  if (P.lastTick >= 0 && b.tick > P.lastTick) P.ticks += std::min(b.tick - P.lastTick, 200);
  P.lastTick = b.tick;
  while (P.ticks >= 20) {
    P.ticks -= 20;
    if (look.x >= x0 + fw / 2.0f) P.frame = std::min(P.frame + 1, ci->frames - 1);
    else P.frame = std::max(P.frame - 1, 0);
  }
  int f = std::clamp(P.frame, 0, ci->frames - 1);
  int pup[4] = {0, 0, 0, 0};
  bool have[2] = {false, false};
  if (f < (int)ci->eyes.size()) {
    for (int e = 0; e < 2; e++) {
      int ex = ci->eyes[f][e * 2], ey = ci->eyes[f][e * 2 + 1];
      if (ex < 0) continue;
      have[e] = true;
      float lx = x0 + ex, ly = y0 + (ey - f * fh);
      float dx = look.x - lx, dy = look.y - ly, len = std::sqrt(dx * dx + dy * dy);
      if (len < 4.0f) continue;
      dx /= len; dy /= len;
      int n = 0;
      while (n < 60) {
        int sx = (int)(ex + dx * (n + 1)), sy = (int)(ey + dy * (n + 1));
        if (sx < 0 || sy < 0 || sx >= P.mask.w || sy >= P.mask.h || P.mask.rgba[((size_t)sy * P.mask.w + sx) * 4 + 3] >= 0x64) break;
        n++;
      }
      int r = std::max(0, n - 1 - P.eye.w / 2);
      pup[e * 2] = (int)(dx * r);
      pup[e * 2 + 1] = (int)(dy * r);
    }
  }
  uint64_t sig = (uint64_t)f | ((uint64_t)(pup[0] + 128) << 8) | ((uint64_t)(pup[1] + 128) << 16) | ((uint64_t)(pup[2] + 128) << 24) | ((uint64_t)(pup[3] + 128) << 32);
  if (sig != P.sig) {
    P.sig = sig;
    std::vector<uint8_t> px((size_t)fw * fh * 4);
    for (int y = 0; y < fh; y++) std::memcpy(&px[(size_t)y * fw * 4], &P.frames.rgba[((size_t)(f * fh + y) * fw) * 4], (size_t)fw * 4);
    for (int e = 0; e < 2; e++) {
      if (!have[e] || P.eye.w == 0) continue;
      int cx = ci->eyes[f][e * 2] + pup[e * 2] - P.eye.w / 2, cy = ci->eyes[f][e * 2 + 1] + pup[e * 2 + 1] - P.eye.h / 2;
      for (int yy = 0; yy < P.eye.h; yy++)
        for (int xx = 0; xx < P.eye.w; xx++) {
          int X = cx + xx, Y = cy + yy;
          if (X < 0 || X >= fw || Y < f * fh || Y >= (f + 1) * fh) continue;
          if (P.mask.w && P.mask.rgba[((size_t)Y * P.mask.w + X) * 4 + 3] >= 0x64) continue;
          const uint8_t* s = &P.eye.rgba[((size_t)yy * P.eye.w + xx) * 4];
          uint8_t* d = &px[((size_t)(Y - f * fh) * fw + X) * 4];
          int a = s[3];
          for (int k = 0; k < 3; k++) d[k] = (uint8_t)((s[k] * a + d[k] * (255 - a)) / 255);
          d[3] = (uint8_t)std::max<int>(d[3], a);
        }
    }
    SDL_UpdateTexture(P.tex, nullptr, px.data(), fw * 4);
  }
  SDL_Rect clip{(int)std::floor(px(352.0f + dw / 2.0f)), (int)std::floor(py(24)), (int)std::ceil(91 * s_), (int)std::ceil(88 * s_)};
  SDL_SetRenderClipRect(r_, &clip);
  SDL_FRect dst{px(x0), py(y0), fw * s_, fh * s_};
  SDL_RenderTexture(r_, P.tex, nullptr, &dst);
  SDL_Rect all{(int)std::floor(px(0)), (int)std::floor(py(0)), (int)std::ceil((800 + dw) * s_), (int)std::ceil(600 * s_)};
  SDL_SetRenderClipRect(r_, &all);
}

void Renderer::drawOrbFront(const Board& b, int dw) {
  float cx = dw / 2.0f;
  bool active = b.master[b.cur] && b.turns[b.cur] > 0;
  bool blinkOff = chPowerBlink_ != 0 && (chPowerBlink_ / 20) % 2 == 0;
  if (b.master[b.cur] == 12 || b.master[b.cur] == 7) blinkOff = (chTick_ / 30) % 2 == 0;
  bool hideCount = chCountBlink_ != 0 && (chCountBlink_ / 30) % 2 == 0;
  if (Tex* t = img("IMAGE_IP_POWERUPBANNER")) {
    int row = std::clamp(stageCel_, 0, t->rows - 1);
    putCelAt(t, (active && !blinkOff) ? 1 : 0, row, 343 + cx, 1);
  }
  if (Tex* t = img("IMAGE_INT_COUNTER")) put(t, {0, 0, t->lw(), t->lh()}, 382 + cx, 17);
  if (Font* f = hideCount ? nullptr : font("FONT_SMALL_DISP")) {
    std::string s = std::to_string(std::clamp(b.turns[b.cur], 0, 99));
    drawText(f, 400 + cx - f->width(s) / 2.0f, 32, s, 0xffffffffu);
  }
}

void Renderer::drawOrangeMeter(const Board& b, int dw) {
  int hits = std::clamp(25 - b.orangeLeft, 0, 25);
  float X = 737 + dw;
  Tex* mid = img("IMAGE_INT_FEVERMETER");
  Tex* top = img("IMAGE_INT_FEVERMETERTOP");
  Tex* bot = img("IMAGE_INT_FEVERMETERBOTTOM");
  float y = 509;
  for (int i = 0; i < hits && i < 25; i++, y -= 14) {
    Tex* t = i == 0 ? bot : i == 24 ? top : mid;
    if (t) put(t, {0, 0, t->lw(), t->lh()}, X, y);
  }
  Tex* bars = img("IMAGE_INT_FEVERBARS");
  Tex* lit = img("IMAGE_INT_FEVERBARLITES");
  const int by[4] = {371, 301, 244, 203}, th[4] = {10, 15, 19, 22};
  for (int i = 0; i < 4; i++) {
    if (bars) putCelAt(bars, 0, i, 734.0f + dw, (float)by[i]);
    if (lit && hits >= th[i] && (i == 3 || hits < th[i + 1])) putCelAt(lit, 0, i, 746.0f + dw, (float)(by[i] + 2));
  }
}

void Renderer::drawHud(const Board& b, const Hud& hud) {
  int dw = canvasW_ - 800;
  drawOrbBack(b, hud, dw);
  drawPortrait(b, dw);
  drawBallColumn(b);
  drawFrame(dw);
  drawOrbFront(b, dw);
  if (Tex* t = tex("images/dialog/menubutton", 2, 1, "roundbutton_")) putCelAt(t, 0, hud.menuOver ? 1 : 0, 12.0f, 540.0f);
  if (b.shotIndex == 0) {
    if (Tex* t = img("IMAGE_INT_BUTTONCOVER")) put(t, {0, 0, t->lw(), t->lh()}, 735.0f + dw, 538);
  } else if (Tex* t = tex("images/dialog/replaybutton", 2, 1, "roundbutton_")) {
    putCelAt(t, 0, hud.replayOver ? 1 : 0, 736.0f + dw, 540.0f);
  }
  drawOrangeMeter(b, dw);
  if (hud.clearedBefore)
    if (Tex* t = img("IMAGE_TINYBLUERIBBON")) put(t, {0, 0, t->lw(), t->lh()}, 740.0f + dw, 4.0f);

  int p = b.cur;
  Font* score = font("FONT_SCORE_DISP");
  Font* small = font("FONT_SMALL_DISP");
  Font* left = font("FONT_BALLSLEFT");
  if (small) {
    auto mode = [&](const std::string& t, bool leftSide) {
      float x = leftSide ? 128.0f : 508.0f + dw;
      if (small->width(t) > 75) x -= 6;
      drawText(small, x, 20, t, 0xffffffff);
    };
    auto detail = [&](const std::string& t, bool leftSide, float off) {
      float x = leftSide ? 128.0f : 508.0f + dw;
      if (small->width(t) > 75) x -= 6;
      drawText(small, x + off, 35, t, 0xffffffff);
    };
    if (hud.mode == Hud::ModeStage) {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%d-%d", (hud.stageNo >= 0 ? hud.stageNo : stageNo_) + 1, (hud.levelNo >= 0 ? hud.levelNo : levelNo_) + 1);
      mode("Stage", true);
      detail(buf, true, 12);
      if (hud.totalScore > 0) detail("Total", false, 0);
    } else if (hud.mode == Hud::ModeDuel) {
      mode(hud.challengeText, true);
      detail("Score", true, 0);
      mode(hud.topName, false);
      detail("Score", false, 0);
    } else if (hud.mode == Hud::ModeQuick || hud.mode == Hud::ModeDemo) {
      if (hud.mode == Hud::ModeQuick) mode("Quick", true);
      detail("Score", true, 0);
      mode("Top Score", false);
      if (hud.topScore > 0) detail(hud.topName, false, 0);
    } else {
      detail("Score", true, 0);
      mode(hud.challengeText.empty() ? "Challenge" : hud.challengeText, true);
      if (!hud.challengeRight.empty()) mode(hud.challengeRight, false);
    }
  }
  if (Font* sc = font("FONT_SCORE_DISP")) {
    int v = hud.mode == Hud::ModeStage ? hud.totalScore : hud.topScore;
    if ((v > 0 || hud.mode == Hud::ModeDuel) && hud.mode != Hud::ModeChallenge) {
      std::string t = commaSep(v);
      int w = sc->width(t);
      float right = 670.0f + dw + (w > 100 ? 5 : 0);
      drawText(sc, right - w, 28, t, 0xffffffff);
    }
  }
  if (score) {
    std::string s = commaSep(b.dispScore[p < 2 ? p : 0]);
    int w = score->width(s);
    float right = 295.0f + (w > 100 ? 5 : 0) + dw / 2.0f;
    drawText(score, right - w, 28, s, 0xffffffff);
  }
  if (left) {
    std::string s = std::to_string(b.ballsLeft[p]);
    drawText(left, 39.0f - left->width(s) / 2, 140, s, 0xffffffff);
  }
  Font* ui = font("FONT_PLAIN");
  if (ui) {
    float y = 70;
    for (auto& line : hud.players) {
      drawText(ui, 100, y, line, 0xffffffff);
      y += 14;
    }
    if (!hud.footer.empty()) drawText(ui, 400.0f + dw / 2, 596, hud.footer, 0xffddddddu, TextAlign::Center);
  }
}

}
