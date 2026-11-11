#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

#include "ui/character_dialog.h"
#include "ui/screens.h"

namespace pp::ui {

void drawSpeechBox(Gfx& g, float x, float y, float w, float h, float alpha, int pointer) {
  auto T = [&](const char* n) { return g.img(std::string("IMAGE_TROPHY_SPEECH_") + n); };
  Tex *tl = T("TL"), *t = T("T"), *tr = T("TR"), *l = T("L"), *c = T("C"), *r = T("R"), *bl = T("BL"), *b = T("B"), *br = T("BR");
  if (!tl || !t || !tr || !l || !c || !r || !bl || !b || !br) return;
  Mode m(alpha);
  float tw = tl->lw(), th = tl->lh();
  g.drawStretch(c, x + tw, y + th, w - 2 * tw, h - 2 * th, m);
  g.drawStretch(t, x + tw, y, w - 2 * tw, th, m);
  g.drawStretch(b, x + tw, y + h - th, w - 2 * tw, th, m);
  g.drawStretch(l, x, y + th, tw, h - 2 * th, m);
  g.drawStretch(r, x + w - tw, y + th, tw, h - 2 * th, m);
  g.draw(tl, x, y, m);
  g.draw(tr, x + w - tw, y, m);
  g.draw(bl, x, y + h - th, m);
  g.draw(br, x + w - tw, y + h - th, m);
  const char* pn[] = {nullptr, "BP", "LP", "RP", "TP"};
  if (pointer > 0 && pointer < 5) {
    if (Tex* p = T(pn[pointer])) {
      if (pointer == 1) g.draw(p, x + 30, y + h - th, m);
      else if (pointer == 2) g.draw(p, x - p->lw() + 8, y + 20, m);
      else if (pointer == 3) g.draw(p, x + w - 8, y + 20, m);
      else g.draw(p, x + 30, y - p->lh() + 8, m);
    }
  }
}

const DecalInfo* MainMenu::decalAt(App& app, float mx, float my, int* stageOut) {
  for (int i = app.stages.numStages(); i < app.stages.numAllStages(); i++) {
    const StageInfo* s = app.stages.stage(i);
    if (!s) continue;
    for (const DecalInfo& d : s->decals)
      if (d.hw > 0 && mx >= d.hx && my >= d.hy && mx < d.hx + d.hw && my < d.hy + d.hh) {
        if (stageOut) *stageOut = i;
        return &d;
      }
  }
  return nullptr;
}

static void drawLoungeScene(Gfx& g, int sunTick, float ox, float oy) {
  if (Tex* bg = g.img("IMAGE_MM_BACKGROUND")) g.draw(bg, ox, oy);
  const int flick = ((sunTick / 13) * 773 + (sunTick / 16) * 1321) % 42 + 174;
  if (Tex* fl = g.img("IMAGE_MM_FIRELIGHT")) g.draw(fl, ox + 64, oy + 224, Mode((float)flick / 255.0f, 0xffffff, true));
  static const char* kFlame[4] = {"IMAGE_MM_FLAME_01", "IMAGE_MM_FLAME_02", "IMAGE_MM_FLAME_03", "IMAGE_MM_FLAME_04"};
  int cel = (sunTick / 4) % 4;
  if (Tex* f = g.img(kFlame[cel])) g.draw(f, ox + 520 - f->lw() / 2, oy + 478 - f->lh(), Mode(0.9f));
  if (Tex* st = g.img("IMAGE_MM_STEAM")) {
    float ph = std::fmod(sunTick * 0.4f, 60.0f);
    g.draw(st, ox + 240 - st->lw() / 2 + 4 * std::sin(sunTick * 0.05f), oy + 352 - st->lh() + ph * 0.3f, Mode(0.18f + 0.1f * std::sin(sunTick * 0.04f)));
  }
}

struct MantelTrophy { const char* image; int rx, ry, rw, rh, sx, sy; const char* text; };
static const MantelTrophy kMantel[4] = {
    {"IMAGE_MM_TROPHY_CUP", 434, 145, 80, 100, 427, 126, "This trophy is awarded for completing Adventure Mode!"},
    {"IMAGE_MM_TROPHY_CROWN", 380, 108, 70, 130, 385, 105, "This trophy is awarded for clearing all of the levels!"},
    {"IMAGE_MM_TROPHY_ZEN", 494, 135, 90, 110, 496, 135, "This trophy is awarded for beating all of the challenges!"},
    {"IMAGE_MM_TROPHY_TORCH", 342, 142, 50, 95, 345, 138, "This trophy is awarded for acing all of the levels!"}};

bool MainMenu::mantelEarned(App& app, int id) {
  const PlayerInfo* p = app.curProfile;
  if (!p || id < 0 || id > 3) return false;
  if (id == 0) return p->advCompletions > 0;
  if (id == 2) return app.trophies.numTrophies() > 0 && p->trophies.size() >= app.trophies.numTrophies();
  size_t n = 0, total = 0;
  for (int i = 0; i < app.stages.numStages(); i++)
    if (const StageInfo* s = app.stages.stage(i))
      for (const LevelInfo& l : s->levels) {
        total++;
        n += (id == 1 ? p->clearedLevels.count(l.file) : p->acedLevels.count(l.file)) ? 1 : 0;
      }
  return total > 0 && n >= total;
}

int MainMenu::mantelAt(App& app, float mx, float my) {
  for (int id : {1, 3, 2, 0}) {
    const MantelTrophy& t = kMantel[id];
    if (mantelEarned(app, id) && mx >= t.rx && my >= t.ry && mx < t.rx + t.rw && my < t.ry + t.rh) return id;
  }
  return -1;
}

static std::string trophyName(int id) { return "Trophy:" + std::to_string(id); }
static std::string decalName(const DecalInfo& d) { return "Decal:" + d.image + std::to_string(d.layer); }

float MainMenu::fadeOf(const std::string& name) const {
  for (const Sparkle& s : sparkles)
    if (s.name == name) return (float)(int)(std::clamp(s.fade, 0.0f, 1.0f) * 255.0f) / 255.0f;
  return 1.0f;
}

void MainMenu::buildSparkles() {
  sparkles.clear();
  PlayerInfo* p = app.curProfile;
  if (!isNights || !p) return;
  struct Item {
    std::string name;
    int x, y, w, h;
    float emit;
    int group;
  };
  std::vector<Item> items;
  for (int id : {1, 3, 2, 0})
    if (mantelEarned(app, id)) {
      const MantelTrophy& t = kMantel[id];
      items.push_back({trophyName(id), t.rx, t.ry, t.rw, t.rh, 1.0f, -1});
    }
  std::vector<int> stages;
  for (int i = app.stages.numStages(); i < app.stages.numAllStages(); i++) {
    const StageInfo* s = app.stages.stage(i);
    if (!s) continue;
    for (const DecalInfo& d : s->decals) {
      if (!app.gfx.img(d.image)) continue;
      auto at = std::find(stages.begin(), stages.end(), i);
      if (at == stages.end()) at = stages.insert(stages.end(), i);
      items.push_back({decalName(d), d.hx, d.hy, d.hw, d.hh, d.emit, (int)(at - stages.begin())});
    }
  }
  bool changed = false;
  for (const Item& it : items) {
    if (!p->mantelSeen.insert(it.name).second) continue;
    changed = true;
    if (!p->mantelSeenKnown) continue;
    float scale = 0.2f;
    if (it.group >= 0) scale = (float)(int)((double)it.group * 0.800000011920929) + 0.2f;
    sparkles.push_back({it.name, SparkleField(it.x, it.y, it.w, it.h, it.emit, app.rand()), 0.0f - scale, it.w != 0 && it.h != 0});
  }
  if (!p->mantelSeenKnown) {
    p->mantelSeenKnown = true;
    changed = true;
  }
  if (changed) {
    p->dirty = true;
    app.saveProfile();
  }
}

void MainMenu::updateSparkles() {
  if (app.anyDialog()) return;
  for (Sparkle& s : sparkles) {
    s.fade = std::min(s.fade + 0.005f, 1.0f);
    if (s.live && s.fade > -0.2f && !s.field.update()) s.live = false;
  }
}

void MainMenu::drawSparkles(Gfx& g) {
  static const char* const kImage[SparkleField::kImages] = {"IMAGE_TROPHY_PARTICLE1", "IMAGE_TROPHY_PARTICLE2", "IMAGE_TROPHY_PARTICLE3",
                                                            "IMAGE_TROPHY_PARTICLE4", "IMAGE_TROPHY_PARTICLE5", "IMAGE_TROPHY_PARTICLE6"};
  for (const Sparkle& s : sparkles) {
    if (!s.live) continue;
    for (const SparkleField::Particle& q : s.field.particles()) {
      Tex* t = g.img(kImage[q.image]);
      if (!t) continue;
      float k = s.field.scale(q);
      g.drawRot(t, q.x, q.y, 0, k, k, Mode(s.field.alpha(q) / 255.0f, 0xffffff, true));
    }
  }
}

void MainMenu::drawNights(Gfx& g) {
  drawLoungeScene(g, sunTick, 0, 0);
  for (int id : {1, 3, 2, 0})
    if (mantelEarned(app, id))
      if (Tex* t = g.img(kMantel[id].image)) g.draw(t, (float)kMantel[id].sx, (float)kMantel[id].sy, Mode(fadeOf(trophyName(id))));
  {
    int st = -1;
    const DecalInfo* hov = decalAt(app, app.mouseX - x, app.mouseY - y, &st);
    for (int i = app.stages.numStages(); i < app.stages.numAllStages(); i++) {
      const StageInfo* s = app.stages.stage(i);
      if (!s) continue;
      for (const DecalInfo& d : s->decals) {
        if (Tex* t = g.img(d.image)) {
          float a = fadeOf(decalName(d));
          g.draw(t, (float)d.x, (float)d.y, Mode(a));
          if (&d == hov) g.draw(t, (float)d.x, (float)d.y, Mode((0.5f + 0.2f * std::sin(sunTick * 0.15f)) * a, 0xffffff, true));
        }
      }
    }
  }
  drawSparkles(g);
  if (bubbleFade > 0) drawSpeechBubble(g, bubbleOld, (float)bubbleFade / 15.0f, true);
  drawSpeechBubble(g, bubble, 1.0f - (bubbleFade > 0 ? (float)bubbleFade / 15.0f : 0.0f), bubbleFade == 0);
  {
    int st = -1;
    const DecalInfo* hov = decalAt(app, app.mouseX - x, app.mouseY - y, &st);
    if (hov && !hov->hover.empty() && !app.hasModal()) {
      float bw = 230, th = g.wrappedHeight("PLAIN2", bw - 40, hov->hover, 18), bh = th + 40;
      float bx = std::clamp((float)hov->x - bw / 2, 10.0f, 520.0f - bw), by = std::max(10.0f, (float)hov->hy - bh - 6);
      drawSpeechBox(g, bx, by, bw, bh, 0.95f, 1);
      g.drawWrapped("PLAIN2", {bx + 20, by + 18, bw - 40, th}, hov->hover, 0x303030, 1.0f, Align::Center, 18);
    }
    int mt = hov ? -1 : mantelAt(app, app.mouseX - x, app.mouseY - y);
    if (mt >= 0 && !app.hasModal()) {
      const MantelTrophy& t = kMantel[mt];
      float bw = 230, th = g.wrappedHeight("PLAIN2", bw - 40, t.text, 18), bh = th + 40;
      float bx = std::clamp(t.rx + t.rw / 2.0f - bw / 2, 10.0f, 520.0f - bw), by = std::max(10.0f, (float)t.ry - bh - 6);
      drawSpeechBox(g, bx, by, bw, bh, 0.95f, 1);
      g.drawWrapped("PLAIN2", {bx + 20, by + 18, bw - 40, th}, t.text, 0x303030, 1.0f, Align::Center, 18);
    }
  }
  if (Tex* l = g.img("IMAGE_MM_LEFT")) g.draw(l, 0, 0);
  if (Tex* t = g.img("IMAGE_MM_TOP")) g.draw(t, 0x2e, 0);
  if (Tex* r = g.img("IMAGE_MM_RIGHT")) g.draw(r, 800 - r->lw(), 0);
  if (Tex* b = g.img("IMAGE_MM_BOTTOM")) g.draw(b, 0x40, 600 - b->lh());
}

static const int kFrames[11][4] = {{574, 85, 54, 102}, {178, 94, 47, 89},  {77, 267, 53, 86},  {176, 254, 35, 68}, {251, 269, 49, 66}, {546, 251, 41, 83},
                                   {650, 254, 55, 88}, {90, 405, 42, 82},  {196, 396, 52, 67}, {545, 377, 61, 80}, {676, 402, 41, 81}};
static const int kMasterFrame[11] = {2, 9, 7, 1, 0, 3, 4, 5, 8, 6, 10};
static const int kHiddenAt[11][2] = {{101, 310}, {575, 418}, {114, 440}, {202, 136}, {603, 133}, {192, 287}, {276, 304}, {569, 288}, {224, 428}, {679, 296}, {699, 436}};

static int stageOfMaster(int master) {
  for (int s = 0; s < 11; s++)
    if (CharacterDialog::masterMetAt(Game::Nights, s) == master) return s;
  return master;
}

ProgressScreen::ProgressScreen(App& a, int m) : Screen(a), mode(m) {
  bContinue = patchButton(0, "Continue", "IMAGE_DLG_BUTTON3", 0, 0x215);
  if (Tex* pb = a.gfx.img("IMAGE_DLG_PLAYBUTTON1")) {
    bContinue->image = pb;
    bContinue->kind = Button::Kind::Embedded;
    bContinue->labelOnEmbedded = true;
    bContinue->w = pb->lw();
    bContinue->h = pb->lh() / 2;
    bContinue->textOffY = 0;
  }
  bContinue->x = (float)(400 - (int)bContinue->w / 2);
  const PlayerInfo* p = a.curProfile;
  int cleared = p ? std::min(p->maxStage, 11) : 0;
  if (p && p->advCompletions > 0) cleared = 11;
  if (m == 1 && cleared > 0) newest = cleared - 1;
  if (newest >= 0) {
    bContinue->visible = false;
  }
  a.playMusic("music/progress.ogg");
  if (newest >= 0 || m == 4) a.playSample("DRUMROLL");
  if (m == 4) {
    drumEnd = 350;
    if (a.audio)
      if (const Sample* s = a.audio->sample("DRUMROLL"))
        if (s->rate > 0) drumEnd = (int)((double)s->pcm.size() / 2 / s->rate * 100.0);
  }
}

void ProgressScreen::update() {
  Panel::update();
  t++;
  if (mode == 4) {
    if (t == drumEnd + 2) app.playSample("TROPHY_WIN_INTRO");
    else if (t == drumEnd + 270) app.playSample("AWARD");
    else if (t == drumEnd + 305) app.playSample("APPLAUSE");
    if (closing > 0 && ++closing == 21) {
      app.playSample("APPLAUSE_LONG");
      app.showCreditsScreen();
      return;
    }
  }
  if (newest >= 0 && t == 41) app.playSample("TROPHY_PHOTO_IN");
  if (newest >= 0 && !bContinue->visible && t > 140) {
    bContinue->visible = true;
    bContinue->x = -(100 + bContinue->w);
    bContinue->y = 0x215;
    app.playSample("TEXT_WOOSH");
  }
  if (bContinue->visible) {
    float tx = 400 - bContinue->w / 2;
    if (bContinue->x < tx) bContinue->x = std::min(tx, bContinue->x + 30);
  }
  hovered = -1;
  float mx = app.mouseX - x, my = app.mouseY - y;
  for (int i = 0; i < 11; i++) {
    const int* f = kFrames[kMasterFrame[i]];
    if (mx >= f[0] - 10 && mx < f[0] + f[2] + 10 && my >= f[1] - 10 && my < f[1] + f[3] + 10) hovered = i;
  }
  if (hovered != prevHovered && hovered >= 0 && t > 20) app.playSample("SPEECH_BUBBLE_POPUP");
  prevHovered = hovered;
}

void drawTrophyRoom(Gfx& g, App& app, int newest, int t) {
  g.fill(0, 0, 800, 600, 0x000000);
  const PlayerInfo* p = app.curProfile;
  int cleared = p ? std::min(p->maxStage, 11) : 0;
  if (p && p->advCompletions > 0) cleared = 11;
  for (int i = 0; i < 11; i++) {
    const int* f = kFrames[kMasterFrame[i]];
    float cx = f[0] + f[2] / 2.0f, cy = f[1] + f[3] / 2.0f;
    const int met = stageOfMaster(i);
    bool shown = met < cleared;
    float a = 1;
    if (met == newest) {
      shown = t > 40;
      a = std::clamp((t - 40) / 60.0f, 0.0f, 1.0f);
    }
    Tex* hid = g.img("IMAGE_TROPHY_PORTRAIT_HIDDEN_" + std::to_string(i + 1));
    Tex* pic = g.img("IMAGE_TROPHY_PORTRAIT_" + std::to_string(i + 1));
    if (hid && (!shown || a < 1)) g.draw(hid, (float)(kHiddenAt[i][0] - (int)hid->lw() / 2), (float)(kHiddenAt[i][1] - (int)hid->lh() / 2));
    if (pic && shown) g.draw(pic, cx - pic->lw() / 2, cy - pic->lh() / 2, Mode(a));
  }
  if (Tex* bg = g.img("IMAGE_TROPHY_TROPHYROOM_SCREEN")) g.draw(bg, 0, 0);
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
  for (int i = 0; i < 11; i++) {
    const int* f = kFrames[kMasterFrame[i]];
    float cx = f[0] + f[2] / 2.0f, cy = f[1] + f[3] / 2.0f;
    if (stageOfMaster(i) == newest && t > 10 && t < 160) {
      float k = t < 40 ? (t - 10) / 30.0f : t < 110 ? 1.0f : std::max(0.0f, (160 - t) / 50.0f);
      if (Tex* sp = g.img("IMAGE_TROPHY_PARTICLE_WHITE_MED_ROUND")) g.drawRot(sp, cx, cy, 0, 3.0f, 3.5f, Mode(0.55f * k, 0xfff0c0, true));
      if (Tex* sk = g.img("IMAGE_TROPHY_SPARKLE"))
        for (int j = 0; j < 6; j++) {
          float ang = t * 0.05f + j * 1.0472f, rr = 30 + 12 * std::sin(t * 0.1f + j);
          g.drawRot(sk, cx + rr * std::cos(ang), cy + rr * std::sin(ang) * 1.3f, t * 0.1f + j, 1, 1, Mode(0.9f * k, 0xffffff, true));
        }
    }
  }
  if (p && p->advCompletions > 0) {
    if (Tex* hole = g.img("IMAGE_TROPHY_HOLE")) g.draw(hole, 400 - hole->lw() / 2, 380, Mode(0.6f, 0xffffff, true));
    if (Tex* adv = g.img("IMAGE_TROPHY_ADV")) g.draw(adv, 400 - adv->lw() / 2, 392 - adv->lh() + 10);
  }
}

void ProgressScreen::drawLayer(Gfx& g) {
  drawTrophyRoom(g, app, newest, t);
  const PlayerInfo* p = app.curProfile;
  int cleared = p ? std::min(p->maxStage, 11) : 0;
  if (p && p->advCompletions > 0) cleared = 11;
  const int hoveredStage = hovered >= 0 ? stageOfMaster(hovered) : -1;
  if (hovered >= 0 && hoveredStage < cleared && !(hoveredStage == newest && t < 100)) {
    const StageInfo* st = app.stages.stage(hoveredStage);
    std::string txt;
    if (st)
      for (auto& d : st->story[0]) txt += d.text;
    if (!txt.empty()) {
      const int* f = kFrames[kMasterFrame[hovered]];
      float bw = 220, th = g.wrappedHeight("PLAIN2", bw - 40, txt, 18), bh = th + 40;
      float bx = std::clamp(f[0] + f[2] / 2.0f - bw / 2, 8.0f, 792.0f - bw), by = std::max(8.0f, f[1] - bh - 14);
      if (by + bh > f[1] - 4 && by < f[1] + f[3]) by = (float)(f[1] + f[3] + 14);
      drawSpeechBox(g, bx, by, bw, bh, 0.95f);
      g.drawWrapped("PLAIN2", {bx + 20, by + 20, bw - 40, th}, txt, 0x303030, 1.0f, Align::Center, 18);
    }
  }
  if (bContinue->visible)
    if (Tex* plate = g.img("IMAGE_TROPHY_EXIT_BUTTON")) g.draw(plate, bContinue->x - 32, bContinue->y - 16);
}

void ProgressScreen::finish() {
  if (mode == 4) {
    if (closing == 0) closing = 1;
    return;
  }
  if (mode == 1) { app.showStoryScreen(false, false); return; }
  if (mode == 2) { app.showMainMenu(); return; }
  app.showAdventureScreen();
}
void ProgressScreen::onButton(int) { finish(); }
bool ProgressScreen::input(const Input& in) { return Panel::input(in); }
void ProgressScreen::keyDown(int key, int) {
  if (key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_ESCAPE) {
    if (bContinue->visible || newest < 0) finish();
    else t = std::max(t, 141);
  }
}

CreditsScreen::CreditsScreen(App& a) : Screen(a) {
  std::vector<std::string> script = a.stages.credits();
  Page cur;
  std::string font = "PLAIN2";
  uint32_t color = 0xffffff;
  float offX = 0, curY = 120;
  bool any = false;
  int held = 0, forCount = -1;
  for (int i = 0; i < 4; i++) held += MainMenu::mantelEarned(a, i) ? 1 : 0;
  auto fontOk = [&](const std::string& f) { return a.gfx.fontHeight(f) > 0; };
  auto flush = [&]() {
    if (any && (forCount == -1 || forCount == held)) pages.push_back(cur);
    cur = Page();
    cur.hold = 200;
    curY = 120;
    offX = 0;
    any = false;
  };
  cur.hold = 200;
  for (const std::string& raw : script) {
    std::string s = raw;
    auto eq = s.find('=');
    if (s == "newPage") { flush(); continue; }
    bool directive = eq != std::string::npos && eq > 0 && std::all_of(s.begin(), s.begin() + (long)eq, [](char ch) { return std::isalnum((unsigned char)ch) != 0; });
    if (directive) {
      std::string k = s.substr(0, eq), v = s.substr(eq + 1);
      if (k == "font") {
        std::string up = v;
        for (auto& ch : up) ch = (char)std::toupper((unsigned char)ch);
        font = fontOk(up) ? up : "PLAIN2";
      } else if (k == "color") color = (uint32_t)std::strtoul(v.c_str(), nullptr, 0) & 0xffffff;
      else if (k == "offsetY") curY = 120 + (float)std::atof(v.c_str());
      else if (k == "addOffsetY") curY += (float)std::atof(v.c_str());
      else if (k == "offsetX") offX = (float)std::atof(v.c_str());
      else if (k == "blank") curY += (float)std::atof(v.c_str()) * 4.0f;
      else if (k == "timePause") cur.hold = std::max(30, std::atoi(v.c_str()) + 120);
      else if (k == "timeStartKeyframe") cur.start = std::atoi(v.c_str());
      else if (k == "trophyCount") { cur.trophy = std::atoi(v.c_str()); forCount = cur.trophy; }
      continue;
    }
    size_t at;
    while ((at = s.find("%user%")) != std::string::npos) s.replace(at, 6, a.curProfile ? a.curProfile->name : "Player");
    Line l;
    l.text = s;
    l.font = font;
    l.color = color;
    l.x = 474 + offX;
    l.y = curY;
    cur.lines.push_back(l);
    curY += a.gfx.fontHeight(font) + 6;
    any = true;
  }
  flush();
  int shown = -1;
  for (Page& p : pages) {
    if (p.trophy > 0) shown = std::min(3, p.trophy - 1);
    else if (p.start != 500) shown = -1;
    p.shown = shown;
  }
  a.playMusic("credits");
}

void CreditsScreen::update() {
  Panel::update();
  if (total == 80) app.playSample("TROPHY_CURTAINS");
  else if (total == 332) app.playSample("CREDITS_SPOTLIGHT_ON");
  else if (total == 410) app.playSample("FANFARE");
  total++;
  t++;
  if (page < pages.size() && t > pages[page].hold) {
    page++;
    t = 0;
  }
  if (page >= pages.size()) finish();
}

void CreditsScreen::drawLayer(Gfx& g) {
  g.fill(0, 0, 800, 600, 0x000000);
  if (Tex* room = g.img("IMAGE_CREDITS_ROOM")) g.draw(room, 0, 0);
  if (Tex* ped = g.img("IMAGE_CREDITS_PEDISTAL")) g.draw(ped, 84, 528);
  const int sh = page < pages.size() ? pages[page].shown : -1;
  static const char* kTrophy[4] = {"IMAGE_CREDITS_TROPHY_ADV", "IMAGE_CREDITS_TROPHY_ALLCLEAR", "IMAGE_CREDITS_TROPHY_CHAL", "IMAGE_CREDITS_TROPHY_ACE"};
  static const int kTx[4] = {100, 99, 91, 133}, kTy[4] = {405, 378, 388, 423};
  if (sh >= 0 && sh < 4) {
    if (Tex* tr = g.img(kTrophy[sh])) g.draw(tr, (float)kTx[sh], (float)kTy[sh]);
  } else if (Tex* bust = g.img("IMAGE_CREDITS_BUST")) g.draw(bust, 106, 333);
  if (page >= pages.size()) return;
  const Page& pg = pages[page];
  float a = std::min({1.0f, t / 30.0f, (pg.hold - t) / 30.0f});
  a = std::clamp(a, 0.0f, 1.0f);
  for (const Line& l : pg.lines) g.drawString(l.font, l.x, l.y, l.text, l.color, a, Align::Center);
}

void CreditsScreen::finish() { app.showMainMenu(); }
bool CreditsScreen::input(const Input& in) {
  if (in.type == Input::MouseDown) { page++; t = 0; if (page >= pages.size()) finish(); return true; }
  return Panel::input(in);
}
void CreditsScreen::keyDown(int key, int) {
  if (key == SDLK_ESCAPE) finish();
  else if (key == SDLK_RETURN || key == SDLK_SPACE) { page++; t = 0; if (page >= pages.size()) finish(); }
}

IntroScreen::IntroScreen(App& a) : Screen(a) {
  bContinue = patchButton(0, "Continue", "IMAGE_DLG_BUTTON3", 0, 0x21e);
  bContinue->x = 400 - bContinue->w / 2;
  a.playMusic("menu");
}
static const char* kIntroText[2] = {"Hi! Welcome to the Peggle Institute!\nWe don't often get visitors at this hour.",
                                    "The Peggle Masters are all asleep.\nIt's the perfect time to catch them dreaming!"};
void IntroScreen::update() {
  Panel::update();
  t++;
  int total = (int)std::string(kIntroText[pageNo - 1]).size();
  if (t > 15 && typed < total && t % 2 == 0) { typed++; if (typed % 3 == 0) app.playSample("TYPING"); }
}
void IntroScreen::drawLayer(Gfx& g) {
  drawLoungeScene(g, t, 15, 11);
  drawSpeechBox(g, 40, 50, 330, 120, std::min(1.0f, t / 15.0f), 1);
  std::string txt = kIntroText[pageNo - 1];
  txt = txt.substr(0, (size_t)typed);
  g.drawWrapped("PLAIN2", {58, 70, 294, 80}, txt, 0x303030, 1.0f, Align::Center, 20);
  if (Tex* l = g.img("IMAGE_MM_LEFT")) g.draw(l, 0, 0);
  if (Tex* tp = g.img("IMAGE_MM_TOP")) g.draw(tp, 0x2e, 0);
  if (Tex* r = g.img("IMAGE_MM_RIGHT")) g.draw(r, 800 - r->lw(), 0);
  if (Tex* b = g.img("IMAGE_MM_BOTTOM")) g.draw(b, 0x40, 600 - b->lh());
  g.drawString("OVERLOAD24GRAD1", 400, 0x32 + 24, "Welcome to the Institute", 0xffffff, std::min(1.0f, t / 20.0f), Align::Center);
}
void IntroScreen::advance() {
  int total = (int)std::string(kIntroText[pageNo - 1]).size();
  if (typed < total) { typed = total; return; }
  if (pageNo == 1) { pageNo = 2; typed = 0; t = 0; return; }
  app.showStoryScreen(false, false);
}
void IntroScreen::onButton(int) { advance(); }
bool IntroScreen::input(const Input& in) {
  if (in.type == Input::MouseDown && in.button == 1 && !app.moving() && in.x < 520 && in.y > 20 && in.y < 515) { advance(); return true; }
  return Panel::input(in);
}
void IntroScreen::keyDown(int key, int) {
  if (key == SDLK_RETURN || key == SDLK_SPACE) advance();
}

void App::showIntroScreen() { setScreen(std::make_unique<IntroScreen>(*this), Left); }
void App::showProgressScreen(int mode) { setScreen(std::make_unique<ProgressScreen>(*this, mode), Left); }
void App::showCreditsScreen() { setScreen(std::make_unique<CreditsScreen>(*this), Left); }

}
