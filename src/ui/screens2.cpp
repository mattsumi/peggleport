#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <filesystem>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/character_dialog.h"
#include "ui/session.h"
#include "ui/dialog.h"
#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/other_game.h"
#include "ui/screens.h"

namespace pp::ui {

static std::string commas(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v), r;
  for (size_t i = 0; i < s.size(); i++) {
    if (i && (s.size() - i) % 3 == 0) r += ',';
    r += s[i];
  }
  return v < 0 ? "-" + r : r;
}

static uint32_t rankColor(int rank, bool nights) {
  uint32_t hue = ((nights ? 0xdc : 0x46) + rank * 0xf5) & 0xff;
  return hslToRgb((int)hue, 0xff, 0x80);
}


AdventureScreen::AdventureScreen(App& a) : Screen(a) {
  nights = a.game == Game::Nights;
  if (nights) {
    a.playMusic("music/progress.ogg");
    bContinue = patchButton(0, "Trophies", "IMAGE_DLG_BROWNBUTTON1", 0x1fc, 0x227);
    bRestart = nullptr;
    bMenu = patchButton(2, "Main Menu", "IMAGE_DLG_BROWNBUTTON1", 199, 0x227);
    return;
  }
  a.playMusic("fadeout");
  PlayerInfo* p = a.curProfile;
  bContinue = patchButton(0, "Continue", "IMAGE_DLG_BROWNBUTTON1", 0x1fc, 0x227);
  bRestart = patchButton(1, "Restart", "IMAGE_DLG_BROWNBUTTON1", 0x162, 0x227);
  bMenu = patchButton(2, "Main Menu", "IMAGE_DLG_BROWNBUTTON1", 199, 0x227);
  if (p && p->justStartingAdventure()) bContinue->label = "Start";
  if (p) {
    auto path = a.profiles.savedGamePath(*p, GameMode::Adventure);
    hasSave = !path.empty() && std::filesystem::exists(path);
    if (p->advStage == 0 && p->advLevel == 0 && !hasSave) bRestart->visible = false;
  }
}

void AdventureScreen::drawFrame(Gfx& g, float x, float y, float w, float h, const std::string& title) {
  g.fill(x, y, w, h, 0x000000, 128 / 255.0f);
  g.fill(x, y - 20, w, 20, 0x000000, 128 / 255.0f);
  const float tw = g.textWidth("OVERLOAD16OUTLINE", title);
  g.drawString("OVERLOAD16OUTLINE", x + std::floor((w - tw) / 2), y + 5, title, app.game == Game::Nights ? 0xd375ffu : 0xffd305u);
  if (Tex* fr = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(fr, x - 8, y - 28, w + 16, h + 36);
}

void AdventureScreen::drawHighScores(Gfx& g, float x, float y) {
  drawFrame(g, x, y, 0x118, 0xbe, "High Scores");
  const auto& hs = app.highScores.scores("_adventure");
  float rowH = g.fontHeight("OVERLOAD12OUTLINE"), ay = y + 0x19;
  float ax = x + 10, nameX = ax + 0x28, scoreR = nameX + 0xdc;
  for (int r = 1; r <= 10; r++) {
    uint32_t col = 0xffffff;
    if (r <= (int)hs.size()) col = rankColor(r, nights);
    g.drawString("OVERLOAD12OUTLINE", ax, ay, std::to_string(r) + ".", col);
    if (r <= (int)hs.size()) {
      g.drawString("OVERLOAD12OUTLINE", nameX, ay, hs[r - 1].name, col);
      std::string sc = commas(hs[r - 1].score);
      g.drawString("OVERLOADMONO12", scoreR - g.textWidth("OVERLOADMONO12", sc), ay, sc, col);
    }
    ay += rowH;
  }
}

void AdventureScreen::drawPersonalHighScores(Gfx& g, float x, float y) {
  drawFrame(g, x, y, 0x118, 0xbe, "Your Best Scores");
  PlayerInfo* p = app.curProfile;
  float rowH = g.fontHeight("OVERLOAD12OUTLINE"), ay = y + 0x19;
  float ax = x + 10, nameX = ax + 0x28, scoreR = nameX + 0xdc;
  std::vector<int> sc;
  if (p) for (auto it = p->advScores.rbegin(); it != p->advScores.rend(); ++it) sc.push_back(*it);
  for (int r = 1; r <= 10; r++) {
    uint32_t col = r <= (int)sc.size() ? rankColor(r, nights) : 0xffffff;
    g.drawString("OVERLOAD12OUTLINE", ax, ay, std::to_string(r) + ".", col);
    if (r <= (int)sc.size()) {
      g.drawString("OVERLOAD12OUTLINE", nameX, ay, p->name, col);
      std::string s = commas(sc[r - 1]);
      g.drawString("OVERLOADMONO12", scoreR - g.textWidth("OVERLOADMONO12", s), ay, s, col);
    }
    ay += rowH;
  }
}

void AdventureScreen::drawStats(Gfx& g, float x, float y, bool current) {
  const bool nights = app.game == Game::Nights;
  const float w = 280, h = nights ? 150.0f : 140.0f;
  drawFrame(g, x, y, w, h, !current ? "Your All-Time Stats" : nights ? "Current Adventure Stats" : "Your Current Stats");
  PlayerInfo* p = app.curProfile;
  if (!p) return;
  const GameStats& s = current ? p->statsA : p->statsB;
  const char* font = "OVERLOAD12OUTLINE";
  const float step = g.fontHeight(font);
  float ay = y + 25;
  auto row = [&](const char* label, long long value, long long total = 0) {
    std::string v = commas(value);
    if (total > 0) v += " / " + std::to_string(total);
    g.drawString(font, x + 10, ay, label, nights ? 0x00ffffu : 0xffff00u);
    g.drawString(font, x + w - 10 - g.textWidth(font, v), ay, v, 0x88ff88);
    ay += step;
  };
  row("Shots:", s.shots);
  row("Free Balls:", s.totalFreeBalls());
  row("Style Shots:", s.totalStyleShots());
  row("Best Shot:", s.bestShot);
  row("Best Fever:", s.bestFever);
  if (current) {
    row("Total Score:", p->advScore);
    row("Retries:", p->advRetries);
    if (nights) row("Faeries Captured:", p->faeries);
  } else {
    const long long levels = (long long)app.stages.numAllLevels();
    row("Challenges Finished:", (long long)p->trophies.size(), (long long)app.trophies.numAllTrophies());
    row("Levels 100% Cleared:", (long long)p->clearedLevels.size(), levels);
    if (nights) row("Levels Aced:", (long long)p->acedLevels.size(), levels);
  }
}

void AdventureScreen::drawLayer(Gfx& g) {
  PlayerInfo* p = app.curProfile;
  std::string title;
  if (nights) {
    drawTrophyRoom(g, app, -1, 0);
    title = (p ? p->name : std::string()) + "'s Stats";
  } else {
    int st = p ? std::clamp(p->advStage, 0, std::max(0, app.numStages() - 1)) : 0, lv = p ? std::clamp(p->advLevel, 0, 5) : 0;
    const StageInfo* si = app.stages.stage(st);
    const LevelInfo* li = si ? si->level(std::min(lv, (int)si->levels.size() - 1)) : nullptr;
    g.fill(0, 0, 800, 600, 0x000020);
    if (li)
      if (Tex* t = g.img("levels/" + li->file)) g.draw(t, (float)((800 - (int)t->lw()) / 2), (float)((600 - (int)t->lh()) / 2));
    int stg = p ? std::clamp(p->advStage + 1, 1, 11) : 1, lvl = p ? std::clamp(p->advLevel + 1, 1, 5) : 1;
    title = "Adventure Stage " + std::to_string(stg) + "-" + std::to_string(lvl);
  }
  if (Tex* fr = g.img("IMAGE_ADV_ADVENTURE")) g.draw(fr, 0, 0);
  g.drawString("OVERLOAD24GRAD1", 400, 0x30, title, 0xaaffff, 1, Align::Center);
  drawHighScores(g, 100, 0x78);
  drawPersonalHighScores(g, 0x1a4, 0x78);
  drawStats(g, 100, 0x168, false);
  drawStats(g, 0x1a4, 0x168, true);
}

void AdventureScreen::drawOverlay(Gfx& g) {
  if (bRestart && !bRestart->visible)
    if (Tex* c = g.img("IMAGE_ADV_RESTART_COVER")) g.drawStretch(c, 0x131, 0x21b, 0xaa, c->lh());
}

void AdventureScreen::startGame() {
  app.gameMode = GameMode::Adventure;
  if (sessionsOf(app).askToContinue(false)) return;
  app.startAdventureGame();
}

void AdventureScreen::onButton(int id) {
  if (id == 0 && nights) app.showProgressScreen(0);
  else if (id == 0) startGame();
  else if (id == 1) app.doConfirmRestartAdventureDialog();
  else if (id == 2) app.showMainMenu();
}

void AdventureScreen::keyDown(int key, int) {
  if (app.hasModal()) return;
  if (key == SDLK_ESCAPE) app.showMainMenu();
}


static const float kSlot[5][2] = {{43, 85}, {304, 85}, {568, 85}, {43, 334}, {568, 334}};
static const float kSlotW = 190, kSlotH = 175;
static const float kTileHit[5][2] = {{14, 62}, {276, 62}, {540, 62}, {13, 312}, {539, 312}};
static const float kTileHitW = 250, kTileHitH = 215;

LevelScreen::LevelScreen(App& a) : Screen(a) {
  a.playMusic(a.game == Game::Deluxe ? "beats:60" : "music/menu.ogg");
  stage = std::clamp(a.curProfile ? a.curProfile->levelSelectStage : 0, 0, std::max(0, a.stages.numStages() - 1));
  if (a.curProfile && a.curProfile->maxStage < stage) stage = a.curProfile->maxStage;
  if (a.packStage >= a.stages.numStages() && a.packStage < a.stages.numAllStages() && a.gameMode != GameMode::Challenge) stage = a.packStage;
  pick = a.pickOther && a.gameMode == GameMode::Challenge;
  a.pickOther = false;
  if (a.gameMode != GameMode::Challenge || pick) og = a.otherLevels();
  if (og && pick) {
    other = true;
    stage = og->listStage(og->player->levelSelectStage);
  } else if (og && a.listPage >= 0) {
    other = true;
    stage = og->listStage(a.listPage);
  }
  if (!pick) a.listPage = -1;
  for (int i = 0; i < 5; i++) {
    auto b = std::make_unique<Button>();
    b->id = i;
    b->kind = Button::Kind::Simple;
    b->x = kTileHit[i][0]; b->y = kTileHit[i][1]; b->w = kTileHitW; b->h = kTileHitH;
    b->hoverSound = "";
    b->clickSound = "BUTTON1";
    b->tickRef = &updateCount;
    bLevel[i] = add(std::move(b));
    bLevel[i]->image = nullptr;
    bLevel[i]->fadeIn = 0.2f; bLevel[i]->fadeOut = 0.04f;
  }
  if (a.gameMode == GameMode::Challenge) {
    bMenu = patchButton(100, "Back", "IMAGE_DLG_PURPLEBUTTON", 6, 541);
  } else {
    bMenu = embeddedButton(100, "IMAGE_LS_MAINMENUBUTTON", 6, 541);
  }
  bBack = embeddedButton(101, "IMAGE_LS_BACKBUTTON", 284, 546);
  bNext = embeddedButton(102, "IMAGE_LS_NEXTBUTTON", 399, 546);
  bRandom = embeddedButton(103, "IMAGE_LS_RANDOMBUTTON", 740, 539);
  if (a.gameMode == GameMode::Challenge || stage >= a.stages.numStages()) bRandom->visible = false;
  syncStageNum();
}

const StageInfo* LevelScreen::pageStage() const { return other ? og->stages.stage(stage) : app.stages.stage(stage); }

bool LevelScreen::groupOpen() const {
  if (!og || app.gameMode != GameMode::Duel) return true;
  return other ? og->duelOpen() : app.modeOpen(GameMode::Duel);
}

bool LevelScreen::canSelect(int id) const {
  if (id < 0 || id >= 5) return false;
  if (other) {
    const StageInfo* s = og->stages.stage(stage);
    if (!s || id >= (int)s->levels.size() || !groupOpen()) return false;
    return s->pack || og->player->isLevelUnlocked(stage, id);
  }
  if (!app.curProfile) return true;
  if (!groupOpen()) return false;
  const StageInfo* s = app.stages.stage(stage);
  if (!s || id >= (int)s->levels.size()) return false;
  if (s->pack) return true;
  return app.curProfile->isLevelUnlocked(stage, id);
}

static int modFirstStage(const App& a) {
  for (int i = a.stages.numStages(); i < a.stages.numAllStages(); i++)
    if (const StageInfo* s = a.stages.stage(i); s && s->packLabel.rfind("mod:", 0) == 0) return i;
  return -1;
}

void LevelScreen::syncStageNum() {
  if (other) {
    bBack->visible = !pick || og->prevStage(stage) >= 0;
    bNext->visible = og->nextStage(stage) >= 0;
    bRandom->visible = !pick && stage < og->stages.numStages();
    return;
  }
  if (og && stage < app.stages.numStages()) bRandom->visible = true;
  int mf = modFirstStage(app);
  if (stage >= app.stages.numStages()) {
    bBack->visible = stage > app.stages.numStages() || stage == mf;
    bNext->visible = stage < app.stages.numAllStages() - 1;
    return;
  }
  bBack->visible = stage > 0 || mf >= 0;
  int maxS = app.curProfile ? std::min(app.curProfile->maxStage, app.stages.numStages() - 1) : app.stages.numStages() - 1;
  bNext->visible = stage < maxS || og != nullptr;
  if (app.curProfile) { app.curProfile->levelSelectStage = stage; app.curProfile->dirty = true; }
}

void LevelScreen::update() {
  Panel::update();
  if (hoverTicks > 0 && --hoverTicks == 0) hovered = -1;
}
void LevelScreen::onButtonEnter(int id) { if (id >= 0 && id < 5) { hovered = id; hoverTicks = 0; } else if (id == 103) { hovered = 5; hoverTicks = 0; } }
void LevelScreen::onButtonLeave(int id) { if (id >= 0 && id < 5) { hovered = id; hoverTicks = 10; } else if (id == 103) { hovered = -1; } }

void LevelScreen::doPlay(int id) {
  int lvl = id;
  if (og && id >= 5) {
    LevelDraw d;
    if (!og->drawLevel(app, false, d)) return;
    other = d.theirs;
    stage = d.stage;
    lvl = d.level;
  }
  if (other) {
    const StageInfo* st = og->stages.stage(stage);
    const LevelInfo* li = st ? st->level(lvl) : nullptr;
    if (!li) return;
    StartTarget t;
    t.place = pick ? StartPlace::Challenge : StartPlace::Level;
    t.mode = app.gameMode;
    t.stage = stage;
    t.level = lvl;
    t.duel = app.duelSes;
    t.challenge = app.curChallenge;
    t.challengeLevel = li->file;
    notePlayed(og->game, stage, lvl);
    app.requestGameSwitch(og->game, t);
    return;
  }
  if (id >= 5 && !og) {
    if (!app.curProfile || !groupOpen()) return;
    int s = -1, l = -1;
    if (!app.stages.randomLevel(*app.curProfile, s, l, app.gameMode == GameMode::Duel, 0, app.rand()) || s < 0) return;
    stage = s;
    lvl = l;
  }
  if (og) notePlayed(app.game, stage, lvl);
  app.curLevel = lvl;
  app.curStage = stage;
  if (app.gameMode == GameMode::Challenge)
    if (const StageInfo* st = app.stages.stage(stage))
      if (const LevelInfo* li = st->level(lvl)) app.challengeLevel = li->file;
  app.stages.markUsed(stage, lvl);
  app.showBoard(false, true);
}

void LevelScreen::onButton(int id) {
  if (app.moving()) return;
  if (id >= 0 && id < 5) { if (canSelect(id)) doPlay(id); }
  else if (id == 100) {
    if (app.gameMode == GameMode::Challenge) app.showTrophyScreen();
    else app.showMainMenu();
  } else if (other && (id == 101 || id == 102)) {
    const int to = id == 101 ? og->prevStage(stage) : og->nextStage(stage);
    if (to >= 0) {
      stage = to;
    } else if (id == 101 && !pick) {
      other = false;
      stage = app.curProfile ? std::clamp(app.curProfile->maxStage, 0, std::max(0, app.stages.numStages() - 1)) : std::max(0, app.stages.numStages() - 1);
      app.packStage = -1;
    }
    syncStageNum();
  } else if (og && id == 102 && stage < app.stages.numStages() && !(app.curProfile ? stage < std::min(app.curProfile->maxStage, app.stages.numStages() - 1) : stage < app.stages.numStages() - 1)) {
    other = true;
    stage = 0;
    syncStageNum();
  } else if (id == 101 && modFirstStage(app) >= 0 && (stage == 0 || stage == modFirstStage(app))) {
    stage = stage == 0 ? modFirstStage(app) : 0;
    app.packStage = stage == 0 ? -1 : stage;
    if (stage == 0 && app.curProfile) { app.curProfile->levelSelectStage = 0; app.curProfile->dirty = true; }
    syncStageNum();
  } else if (id == 101) { if (stage > 0) { stage--; if (stage >= app.stages.numStages()) app.packStage = stage; syncStageNum(); } }
  else if (id == 102) { stage++; if (stage >= app.stages.numStages()) app.packStage = stage; syncStageNum(); }
  else if (id == 103) doPlay(103);
}

void LevelScreen::keyDown(int key, int) {
  if (app.hasModal()) return;
  if (key == SDLK_ESCAPE) onButton(100);
  else if (key == SDLK_LEFT && bBack->visible) onButton(101);
  else if (key == SDLK_RIGHT && bNext->visible) onButton(102);
}

void LevelScreen::drawLevelInfo(Gfx& g, float, float) {
  const StageInfo* s = pageStage();
  const Game pageGame = other ? og->game : app.game;
  const bool duel = app.gameMode == GameMode::Duel, chal = app.gameMode == GameMode::Challenge, normal = !duel && !chal;
  const float panelX = 300, panelW = 190;
  const std::string modeHint = chal ? "Pick a level for the challenge!" : duel ? "Face off against a friend or the computer in this two player game mode!" : "";
  if (hovered >= 0 && hovered < 5 && s && hovered < (int)s->levels.size() && canSelect(hovered)) {
    const LevelInfo& li = s->levels[hovered];
    g.drawString("OVERLOAD16OUTLINE", 400 - g.textWidth("OVERLOAD16OUTLINE", li.title) / 2, 365, li.title, 0xffff00);
    if (normal) {
      const auto& hs = (other ? og->highScores : app.highScores).scores(li.file);
      if (pageGame == Game::Nights) {
        const char* af = "PLAINOUTLINE";
        const std::string ace = "Ace Score: " + commas(li.ace);
        g.drawString(af, 400.0f - (float)((int)g.textWidth(af, ace) / 2), 388, ace, 0xffffff);
      }
      const char* hf = "OVERLOAD12OUTLINE";
      const float top = pageGame == Game::Nights ? 395.0f + 21.0f : 395.0f;
      g.drawString(hf, 400 - g.textWidth(hf, "Top Five Scores") / 2, top, "Top Five Scores", pageGame == Game::Nights ? 0xffffff : 0x88ff88);
      const char* nf = "OVERLOAD10OUTLINE";
      float y = top + (pageGame == Game::Nights ? 20.0f : 25.0f), step = g.fontLineSpacing(nf);
      for (int i = 0; i < 5 && i < (int)hs.size(); i++) {
        int hue = (10 * (i + 1)) & 0x800000ff;
        if (hue > 0x80) hue = 0x100 - hue;
        uint32_t col = hslToRgb(hue, 0xff, 0x80);
        g.drawString(nf, 310, y, std::to_string(i + 1) + ".", col);
        g.drawString(nf, 330, y, hs[i].name, col);
        std::string v = commas(hs[i].score);
        g.drawString("OVERLOADMONO10", 490 - g.textWidth("OVERLOADMONO10", v), y + 4, v, col);
        y += step;
      }
    } else {
      if (pageGame == Game::Deluxe) g.drawWrapped("OVERLOAD12OUTLINE", {panelX, 410, panelW, 60}, modeHint, 0x88ff88, 1, Align::Center);
      else g.drawWrapped("PLAINOUTLINE", {panelX, 395, panelW, 60}, modeHint, 0xffffff, 1, Align::Center);
    }
    return;
  }
  if (pageGame == Game::Nights && hovered >= 0 && hovered < 5 && s && hovered < (int)s->levels.size() && !canSelect(hovered)) {
    std::string t = "Level locked!", body = "Complete this level\nin adventure mode to unlock!";
    g.drawString("OVERLOAD16OUTLINE", 400 - g.textWidth("OVERLOAD16OUTLINE", t) / 2, 365, t, 0xffff00);
    g.drawWrapped("PLAINOUTLINE", {303, 395, 190, 80}, body, 0xff4444, 1, Align::Center);
    return;
  }
  bool randomOver = hovered == 5;
  std::string title = randomOver ? "Play Random Level" : other ? og->stages.title(stage) : app.stages.title(stage);
  float titleY = 365;
  if (og && !randomOver) {
    const std::string name = gameTitle(pageGame);
    g.drawString("OVERLOAD16OUTLINE", 400 - g.textWidth("OVERLOAD16OUTLINE", name) / 2, titleY, name, 0xffff00);
    titleY += g.fontLineSpacing("OVERLOAD16OUTLINE");
  }
  g.drawString("OVERLOAD16OUTLINE", 400 - g.textWidth("OVERLOAD16OUTLINE", title) / 2, titleY, title, 0xffff00);
  if (normal || chal || duel) {
    std::string hint = !modeHint.empty() ? modeHint
                       : randomOver ? "Can't decide on a level?  Select one randomly!"
                                    : "Play any level that\nyou have unlocked in Adventure Mode!";
    if (pageGame == Game::Nights && !modeHint.empty()) g.drawWrapped("PLAINOUTLINE", {panelX, 395, panelW, 60}, hint, 0xffffff, 1, Align::Center);
    else g.drawWrapped("PLAIN", {panelX, 365 + 40 + 10, panelW, 60}, hint, 0x88ff88, 1, Align::Center);
  }
}

void LevelScreen::drawLayer(Gfx& g) {
  g.fill(0, 0, 800, 600, 0x000000);
  const StageInfo* s = pageStage();
  const bool normal = app.gameMode != GameMode::Duel && app.gameMode != GameMode::Challenge;
  const PlayerInfo* marks = other ? og->player : app.curProfile;
  for (int i = 0; i < 5; i++) {
    float sx = kSlot[i][0], sy = kSlot[i][1];
    g.fill(sx - 10, sy - 10, kSlotW + 20, kSlotH + 20, 0x000000);
    if (!s || i >= (int)s->levels.size()) continue;
    float cx = kTileHit[i][0] + (float)((int)(kTileHitW - 200) / 2), cy = kTileHit[i][1] + (float)((int)(kTileHitH - 177) / 2);
    Tex* t = other ? og->picture("levels/cached_thumbnails/" + s->levels[i].file) : g.img("levels/cached_thumbnails/" + s->levels[i].file);
    if (!t) t = other ? og->picture("levels/" + s->levels[i].file) : g.img("levels/" + s->levels[i].file);
    bool ok = canSelect(i);
    if (t) {
      g.drawStretch(t, cx, cy, 200, 177, Mode(1, 0x969696));
      float a = hovered == 5 ? 1.0f : bLevel[i]->overAlpha;
      if (ok && a > 0) g.drawStretch(t, cx, cy, 200, 177, Mode(a));
    } else {
      g.fill(sx, sy, kSlotW, kSlotH, 0x303060);
    }
    if (!ok)
      if (Tex* lk = g.img("IMAGE_LS_LOCK")) g.draw(lk, kTileHit[i][0] + (float)((int)(kTileHitW - lk->lw()) / 2), kTileHit[i][1] + (float)((int)(kTileHitH - lk->lh()) / 2));
  }
  if (Tex* bg = g.img("IMAGE_LS_LEVELSCREEN")) g.draw(bg, 0, 0);
  for (int i = 0; i < 5; i++) {
    if (!s || i >= (int)s->levels.size()) continue;
    float sx = kSlot[i][0], sy = kSlot[i][1];
    bool ok = canSelect(i);
    const char* tf = "OVERLOAD16OUTLINE2";
    std::string name = s->levels[i].title;
    const float nameW = g.textWidth(tf, name);
    g.drawString(tf, kTileHit[i][0] + (float)((int)(kTileHitW - nameW) / 2), kTileHit[i][1] + 26 + g.fontAscent(tf), name, hovered == 5 || bLevel[i]->over ? 0xfff600 : 0x93c814);
    if (bLevel[i]->overAlpha > 0)
      if (Tex* sel = g.img("IMAGE_LS_LEVELSELECTOR")) {
        const float a = bLevel[i]->overAlpha;
        const bool dimmed = !ok && (other ? og->game : app.game) == Game::Nights;
        g.draw(sel, kTileHit[i][0] + 4, kTileHit[i][1], dimmed ? Mode(a * 250.0f / 255.0f, 0x666666) : Mode(a));
      }
    if (ok && normal && marks && marks->clearedLevels.count(s->levels[i].file))
      if (Tex* rb = g.img("IMAGE_LS_TINYBLUERIBBON")) g.draw(rb, sx + kSlotW - 60, sy + 177);
    if (ok && normal && (other ? og->game : app.game) == Game::Nights && marks && marks->acedLevels.count(s->levels[i].file))
      if (Tex* ar = other ? og->picture("IMAGE_LS_TINYACED") : g.img("IMAGE_LS_TINYACED")) g.draw(ar, sx + kSlotW - 225, sy + 177);
  }
  if (Tex* l = g.img("IMAGE_LS_LEVELSCREENLEFT")) g.draw(l, 15, 524);
  if (bRandom->visible)
    if (Tex* r = g.img("IMAGE_LS_LEVELSCREENRIGHT")) g.draw(r, 703, 508);
  const TrophyInfo* pickedChallenge = app.gameMode == GameMode::Challenge ? (pick && og ? og->trophies : app.trophies).byId(app.curChallenge) : nullptr;
  std::string banner = app.gameMode == GameMode::Duel ? "Two Player Duel" : app.gameMode == GameMode::Challenge ? (pickedChallenge && !pickedChallenge->name.empty() ? pickedChallenge->name : "Pick a Level") : "Quick Play";
  g.drawString("OVERLOAD24GRAD1", 400 - g.textWidth("OVERLOAD24GRAD1", banner) / 2, 48, banner, 0xb6ffff);
  drawLevelInfo(g, 304, 335);
  const Game pageGame = other ? og->game : app.game;
  const StageMgr& pageStages = other ? og->stages : app.stages;
  if (pageGame == Game::Nights && normal && marks && stage < pageStages.numStages()) {
    int clearedOwn = 0, acedOwn = 0;
    for (int i = 0; i < pageStages.numStages(); i++)
      if (const StageInfo* st = pageStages.stage(i))
        for (const LevelInfo& l : st->levels) {
          clearedOwn += (int)marks->clearedLevels.count(l.file);
          acedOwn += (int)marks->acedLevels.count(l.file);
        }
    const char* nf = "OVERLOAD16OUTLINE2";
    const std::string total = std::to_string(pageStages.numLevels());
    const std::string acedText = std::to_string(acedOwn) + " / " + total, clearedText = std::to_string(clearedOwn) + " / " + total;
    if (Tex* ar = other ? og->picture("IMAGE_LS_TINYACED") : g.img("IMAGE_LS_TINYACED")) g.draw(ar, 111, 550);
    if (Tex* rb = other ? og->picture("IMAGE_LS_TINYBLUERIBBON") : g.img("IMAGE_LS_TINYBLUERIBBON")) g.draw(rb, 581, 550);
    g.drawString(nf, 210 - g.textWidth(nf, acedText), 583, acedText, 0xffffff);
    g.drawString(nf, 680 - g.textWidth(nf, clearedText), 583, clearedText, 0xffffff);
  }
}


StoryScreen::StoryScreen(App& a, bool rev, bool eg) : Screen(a), reveal(rev), easter(eg) {
  stage = a.curProfile && a.curProfile->advStage >= 0 && a.gameMode == GameMode::Adventure ? a.curStage : (a.curProfile ? std::max(0, a.curProfile->advStage) : a.curStage);
  stage = std::clamp(a.curStage, 0, std::max(0, a.numStages() - 1));
  bContinue = patchButton(0, "Continue", "IMAGE_DLG_BUTTON3", 0, 0x21e);
  if (Tex* pb = a.gfx.img("IMAGE_DLG_PLAYBUTTON1")) {
    bContinue->image = pb;
    bContinue->kind = Button::Kind::Embedded;
    bContinue->labelOnEmbedded = true;
    bContinue->w = pb->lw();
    bContinue->h = pb->lh() / 2;
    bContinue->textOffY = 0;
  }
  bContinue->x = (float)(400 - (int)bContinue->w / 2);
  bContinue->visible = false;
  const int reached = a.curProfile ? std::max(0, a.curProfile->advStage) : 0;
  win = a.game != Game::Nights && (rev || (a.numStages() > 0 && reached >= a.numStages()));
  if (win) initWin();
  else initText();
}

void StoryScreen::initText() {
  const StageInfo* s = app.stages.stage(stage);
  page = 1;
  if (app.curProfile) page = app.curProfile->advCompletions < 4 ? app.curProfile->advCompletions + 1 : 1 + (int)(app.rand() % 3) + 1;
  if (!s || page > 4 || s->story[page].empty()) page = 1;
  text.clear();
  if (s) {
    for (auto& d : s->story[0]) text += d.text;
    headVisible = (int)stripColorCodes(text).size();
    if (page != 1) text += "\n^ffffff^A few choice words of\nPeggle Wisdom^oldclr^\n\n";
    else text += "\n";
    for (auto& d : s->story[page]) text += d.text;
  }
  head.clear();
  text2.clear();
  if (s && app.game != Game::Nights) {
    if (page != 1) head = "^ffffff^A few choice words of\nPeggle Wisdom^oldclr^";
    else for (auto& d : s->story[0]) head += d.text;
    while (!head.empty() && head.back() == '\n') head.pop_back();
    text.clear();
    for (auto& d : s->story[page]) text += d.text;
    headVisible = 0;
  } else if (s) {
    for (auto& d : s->story[0]) head += d.text;
    while (!head.empty() && head.back() == '\n') head.pop_back();
    text.clear();
    for (auto& d : s->story[1]) text += d.text;
    for (auto& d : s->story[2]) text2 += d.text;
    headVisible = (int)text.size();
  }
  totalVisible = (int)stripColorCodes(text).size() + (int)stripColorCodes(text2).size();
  start = 20;
  app.playMusic(app.game == Game::Deluxe || reveal ? "beats:48" : "music/09.mo3");
}

void StoryScreen::update() {
  Panel::update();
  if (win) {
    updateWin();
    return;
  }
  if (bContinue->visible) {
    float tx = (float)(400 - (int)bContinue->w / 2);
    if (bContinue->x < tx) bContinue->x = std::min(tx, bContinue->x + 30);
    if (bContinue->y > 0x21e) bContinue->y = std::max(0x21e * 1.0f, bContinue->y - 15);
  }
  if (done) return;
  if (start != 0) {
    if (--start == 0) {
      boxes++;
      boxFade = 1;
      wait = 20;
      if (boxes == 2) dreamIn();
    }
    return;
  }
  if (boxFade != 0 && ++boxFade == 20) boxFade = 0;
  if (wait != 0) { wait--; return; }
  if (typed < totalVisible) {
    typed++;
    app.playSample("TYPING");
    std::string vis = stripColorCodes(text) + stripColorCodes(text2);
    int off = typed - 1;
    if (off >= 0 && off < (int)vis.size()) {
      switch (vis[off]) {
        case '!': case '?': wait = 20; break;
        case '.': case ',': wait = 10; break;
        default: wait = 2; break;
      }
      if (app.game == Game::Nights) {
        if (off == headVisible - 1 || off == (int)vis.size() - 1) start = 50;
      } else if (off == (int)vis.size() - 1) {
        wait = 50;
      }
    }
    return;
  }
  done = true;
  bContinue->visible = true;
  bContinue->x = -(100 + bContinue->w);
  bContinue->y = 0x21e + 60;
  app.playSample("DIALOG_MOVE");
}
void StoryScreen::finish() {
  if (win || reveal) { app.showMainMenu(); return; }
  if (app.game == Game::Nights) app.playSample("STAGE_DREAMY_OUT");
  app.showBoard(true, true);
}
void StoryScreen::onButton(int) { finish(); }
void StoryScreen::dreamIn() {
  if (dreamy || app.game != Game::Nights || stage == 10) return;
  dreamy = true;
  app.playSample("STAGE_DREAMY_IN");
}
void StoryScreen::skipTyping() {
  typed = 1000000; wait = 0; start = 0; boxFade = 0;
  if (app.game == Game::Nights) {
    if (boxes < 2) dreamIn();
    boxes = 2;
  } else {
    boxes = 1;
  }
}
bool StoryScreen::input(const Input& in) {
  if (!win && in.type == Input::MouseDown && !done && in.button > 0) {
    skipTyping();
    return true;
  }
  return Panel::input(in);
}
void StoryScreen::keyDown(int key, int) {
  if (win) return;
  if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_SPACE) {
    if (done) finish();
    else skipTyping();
  }
}

static std::string firstVisible(const std::string& s, int n) {
  std::string out;
  int cnt = 0;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '^') {
      auto e = s.find('^', i + 1);
      if (e != std::string::npos && (e - i == 7 || s.compare(i + 1, e - i - 1, "oldclr") == 0)) { out += s.substr(i, e - i + 1); i = e + 1; continue; }
    }
    if (cnt >= n) break;
    out += s[i++];
    cnt++;
  }
  return out;
}

static uint32_t charColor(int i) {
  static const uint32_t c[10] = {0x88ffff, 0x88ff88, 0xffff88, 0xa080ff, 0x88ccff, 0xffaa44, 0xff88ff, 0xff4444, 0x00ff00, 0xb6bcd6};
  return i >= 0 && i < 10 ? c[i] : 0xffffff;
}

void StoryScreen::drawNights(Gfx& g) {
  const char* font = "STAGE";
  const int ci = std::clamp(CharacterDialog::masterMetAt(app.game, stage), 0, 11);
  const uint32_t rgb = charColor(stage);
  auto fadeOf = [&](int index) { return boxes > index ? (boxFade > 0 && index == boxes - 1 ? boxFade / 20.0f : 1.0f) : 0.0f; };
  auto portraitAt = [&](const std::string& name, float fade) {
    if (Tex* pic = g.img(name)) {
      g.draw(pic, 70, 120, Mode(fade));
      if (Tex* bf = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(bf, 62, 112, pic->lw() + 16, pic->lh() + 16, Mode(fade));
    }
  };
  Tex* tr = g.img("IMAGE_DLG_TEXTRECT");
  if (boxes > 0) {
    if (tr) g.drawBox(tr, 264, 120, 466, 190, Mode(0.5f * fadeOf(0), 0x000000));
    portraitAt("IMAGE_CHR_CHARACTER_ADV" + std::to_string(ci + 1), fadeOf(0));
  }
  if (boxes > 1) {
    if (tr) g.drawBox(tr, 70, 325, 660, 170, Mode(0.5f * fadeOf(1), 0x000000));
    portraitAt("IMAGE_CHR_CHARACTER_NEW" + std::to_string(ci + 1), fadeOf(1));
  }
  auto typedLines = [&](float x, float y, float w, const std::string& block, int shown) {
    const float ls = g.fontLineSpacing(font);
    float base = y + g.fontAscent(font);
    size_t at = 0;
    for (const std::string& line : g.wrap(font, w, block)) {
      size_t pos = line.empty() ? at : block.find(line, at);
      if (pos == std::string::npos) pos = at;
      const int n = std::clamp(shown - (int)pos, 0, (int)line.size());
      if (n > 0) g.drawString(font, x, base, line.substr(0, (size_t)n), rgb);
      at = pos + line.size();
      base += ls;
    }
  };
  const int first = (int)stripColorCodes(text).size();
  typedLines(274, 130, 446, text, typed);
  typedLines(90, 335, 620, text2, typed - first);
  if (!head.empty()) g.drawWrapped("OVERLOAD24OUTLINE", {80, 53, 640, 0}, head, rgb, 1, Align::Center, 30);
}

void StoryScreen::drawLayer(Gfx& g) {
  if (win) {
    drawWin(g);
    return;
  }
  g.fill(0, 0, 800, 600, 0x000000);
  g.fill(0, 0, 800, 20, 0x000080);
  g.fill(0, 0, 20, 600, 0x000080);
  g.fill(780, 0, 20, 600, 0x000080);
  g.fill(0, 580, 800, 20, 0x000080);
  if (Tex* bx = g.img("IMAGE_DLG_DIALOGBOX")) g.drawBox(bx, -2, -4, 804, 608);
  if (bContinue->visible)
    if (Tex* plate = g.img("IMAGE_DLG_PLAYWIDGET1")) g.draw(plate, bContinue->x - 32, bContinue->y - 20);
  const bool first = !app.curProfile || stage == 0 || app.curProfile->advCompletions != 0;
  static const char* kWord[12] = {"One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten", "Eleven", "Twelve"};
  g.fill(0, 0, 0, 0, 0);
  std::string head = stage >= 0 && stage <= 11 ? std::string("Stage ") + kWord[stage] : "Stage " + std::to_string(stage + 1);
  g.drawString("OVERLOAD24OUTLINE", 400 - g.textWidth("OVERLOAD24OUTLINE", head) / 2, 45.0f, head, 0xddddff);
  if (app.game == Game::Nights) {
    drawNights(g);
    return;
  }
  const float aX = 53, aY = first ? 200.0f : 135.0f;
  int ci = stage % 10;
  Tex* portrait = g.img(std::string("IMAGE_CHR_CHARACTER") + std::to_string(ci + 1));
  float pw = portrait ? portrait->lw() : 184, ph = portrait ? portrait->lh() : 184;
  float fade = boxes == 0 ? 0.0f : (boxFade > 0 ? boxFade / 20.0f : 1.0f);
  if (boxes > 0) {
    float tx = 20 + pw + aX, ty = 10.0f + aY, tw = 800 - tx - 50;
    if (Tex* tr = g.img("IMAGE_DLG_TEXTRECT")) g.drawBox(tr, tx - 10, ty - 10, tw + 20, 190.0f, Mode(0.5f * fade, 0x000000));
    if (portrait) {
      g.draw(portrait, aX, aY + 4, Mode(fade));
      if (Tex* bf = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(bf, aX - 8, aY + 4 - 8, pw + 16, ph + 16, Mode(fade));
    }
    std::string shown = firstVisible(text, typed);
    g.drawWrapped("STAGE", {tx, ty, tw, 180}, shown, charColor(ci), 1, Align::Left);
    g.drawWrapped("OVERLOAD24OUTLINE", {80, page != 1 ? 105.0f : 65.0f, 640, 0}, this->head, charColor(ci), fade, Align::Center, 30);
  }
  if (stage != 0 && app.curProfile && app.curProfile->advCompletions == 0 && done) {
    const float bx = 45, bw = 64 * 11 + 14;
    if (Tex* tr = g.img("IMAGE_DLG_TEXTRECT")) {
      g.drawBox(tr, bx, 342, bw, 95, Mode(50 / 255.0f, 0x000000));
      g.drawBox(tr, bx, 440, bw, 75, Mode(50 / 255.0f, 0x000000));
    }
    std::string adv = "Adventure: ^00FF00^" + std::to_string(app.curProfile->maxLevelIndex()) + " of 55^oldclr^ Complete";
    std::string chl = "Challenge: ^00FF00^" + std::to_string((int)app.trophies.numHeld(app.curProfile->trophies)) + " of " + std::to_string(app.trophies.numTrophies()) + "^oldclr^ Complete";
    auto line = [&](const std::string& s, float base) {
      std::string bare;
      int carets = 0;
      for (char c : s) {
        if (c == '^') carets++;
        else bare += c;
      }
      const int raw = (int)(g.textWidth("OVERLOAD16OUTLINE", bare) + (float)carets * g.textWidth("OVERLOAD16OUTLINE", "^"));
      float x = (float)(80 + (800 - raw) / 2);
      for (auto& run : parseColorCodes(s)) {
        g.drawString("OVERLOAD16OUTLINE", x, base, run.text, run.rgb < 0 ? 0xffffff : (uint32_t)run.rgb);
        x += g.textWidth("OVERLOAD16OUTLINE", run.text);
      }
    };
    line(adv, 342 + 25);
    line(chl, 440 + 25);
    Tex* chars = g.img("IMAGE_CHR_CHARACTERS");
    Tex* shadows = g.img("IMAGE_CHR_CHARACTERSHADOWS");
    Tex* frame = g.img("IMAGE_DLG_BLUEFRAME");
    Tex* sel = g.img("IMAGE_DLG_SELECTORFRAME");
    const int unlocked = app.curProfile->maxStage;
    for (int i = 0; i < 11; i++) {
      float cx = (float)(53 + 64 * i), cy = 342 + 25;
      bool open = i <= unlocked && i < 10;
      if (i < 10) {
        if (Tex* t = open ? chars : shadows) g.drawCel(t, cx, cy, i, 0);
        if (!open) {
          if (Tex* lk = g.img("IMAGE_LS_LOCK_SMALL")) g.draw(lk, cx, cy, Mode(120 / 255.0f));
          if (chars) g.fill(cx, cy, chars->cw(), chars->ch(), 0x000000, 110 / 255.0f);
        }
      } else if (Tex* q = g.img("IMAGE_DLG_QUESTIONMARK")) {
        g.draw(q, cx, cy);
      }
      float cw = chars ? chars->cw() : 52, chh = chars ? chars->ch() : 52;
      if (Tex* fr = i == stage % 10 ? sel : frame) g.drawBox(fr, cx - 5, cy - 8, cw + 10 + 1, chh + 10 + 5);
    }
  }
}


struct HelpPage {
  const char* name;
  const char* head[2];
  const char* row[4][3];
  bool style = false;
};
static const HelpPage kHelp[8] = {
    {"Controls", {"Move your mouse cursor to where you want the ball to go.", "The Launcher will automatically aim so that the ball heads to that point."},
     {{"Click the left mouse button to fire the ball.", "The ball will head right to where your mouse was when you clicked.", ""},
      {"Hold down the right mouse button to speed up time.", "This is helpful when waiting for moving objects.", ""},
      {"Use the mouse wheel to make small angle adjustments.", "This is useful for getting the exact shot that you want.", ""},
      {"The left and right arrow keys will also make small angle adjustments.", "Hitting the enter key will fire the ball.", ""}}},
    {"Pegs", {"There are four types of pegs in Peggle.", ""},
     {{"Pegs can be both round and rectangular.", "", ""},
      {"Orange pegs are worth 10x the value of blue pegs.", "Clear all of the orange pegs on a level to beat the level.", ""},
      {"The purple peg is worth 50x the value of the blue pegs.", "This peg changes location on every shot.", ""},
      {"The green peg activates your Magic Power.", "There are two green pegs per level.", ""}}},
    {"Scoring", {"The number of points you get for a shot is the total value of all of the pegs", "multiplied by the number of pegs you hit."},
     {{"Every peg that you hit on a shot adds to a subtotal score.", "The shot score is the subtotal score multiplied by the number of pegs hit.", ""},
      {"The value of the pegs goes up as you eliminate orange pegs.", "The Fevermeter on the right shows the current score multiplier.", ""},
      {"Certain cool shots give you extra style points.  These points add directly", "to your score and don't factor into getting a 25,000 point Free Ball.", ""},
      {"After beating a level you earn extra points from the bonus holes.", "You also earn 10,000 points per extra ball left over.", ""}}},
    {"Free Balls", {"There are a few different ways to get a Free Ball.", ""},
     {{"You get a Free Ball if your ball lands in the Free Ball Bucket.", "", ""},
      {"Scoring 25K, 75K, and 125K on a shot also gives you a Free Ball.", "", ""},
      {"If you don't hit any pegs on a shot then you may be awarded", "a Free Ball by winning a coin flip!", ""},
      {"In Duel Mode, you get style points instead of Free Balls.", "Also, there are no coin flip Free Balls in Duel Mode.", ""}}},
    {"Game Modes", {"There are four game modes in Peggle.", ""},
     {{"Adventure Mode allows you to train with all the Peggle Masters.", "Progress through all the levels to become a Peggle Master yourself.", ""},
      {"Quick Play Mode lets you select an individual level to play.", "You may only play levels that you've completed in Adventure Mode.", ""},
      {"Duel Mode allows you to compete in a two player version of Peggle.", "You can play against the computer or another person.", ""},
      {"Challenge Mode presents you with extra difficult Peggle feats.", "Beat all of the challenges to become a Peggle Grand Master.", ""}}},
    {"Duel Mode", {"Duel Mode has slightly different rules than the single player modes.", ""},
     {{"In Duel Mode, two players take turns shooting the ball.", "The player with the most points at the end of the game wins.", ""},
      {"You must hit at least one orange peg on a shot.", "If you don't then you are penalized by losing 25% of your total score.", ""},
      {"In Duel Mode, you get style points instead of Free Balls.", "Also, there are no coin flip Free Balls in Duel Mode.", ""},
      {"The game ends either when a player eliminates the last orange peg or", "when both players run out of balls.", ""}}},
    {"Instant Replays", {"Instant replays are useful for reliving your favorite shots and showing them", "to your friends and family over and over again."},
     {{"Click the Instant Replay button on the bottom right of the game to", "see a replay of the last shot.", ""},
      {"Right click the Instant Replay button to enable an advanced mode in which", "you can replay all shots for a game.", ""},
      {"Hold down the left mouse button in the play area to activate slow motion.", "Hold down the right button to activate fast forward.", ""},
      {"You can save replays and view them later!", "Click the Replays button on the main menu to view saved replays.", ""}}},
    {"Extra Tidbits", {"What else, what else, what else?", ""},
     {{"After beating Adventure Mode, you can replay it again using any Peggle", "Master on any level!  Can you top your high score?", ""},
      {"Clearing all of the pegs on a level gives you a bonus blue ribbon!", "", ""},
      {"It's rumored that there's a level of Peggle Mastery beyond Grand Master.", "Supposedly, clearing every level can help you attain this rank!", ""},
      {"Click the Open Folder button in the Show Replays dialog to locate replays.", "Send replays to friends from here, or drop friends' replays in here to view.", ""}}}};

static const HelpPage kHelpNights[13] = {
    {"Controls", {"Move your mouse cursor to where you want the ball to go.", "The Launcher will automatically aim so that the ball heads to that point."},
     {{"Click the left mouse button to fire the ball.", "The ball will head right to where your mouse pointer was when you clicked.", ""},
      {"Hold down the right mouse button to speed up time.", "This is helpful when waiting for moving objects.", ""},
      {"Use the mouse wheel to make small angle adjustments.", "This is useful for getting the exact shot that you want.", ""},
      {"The left and right arrow keys will also make small angle adjustments.", "Hitting the enter key will fire the ball.", ""}}},
    {"Pegs", {"There are four types of pegs in Peggle Nights.", ""},
     {{"Pegs can be both round and rectangular.", "", ""},
      {"Orange pegs are worth 10x the value of blue pegs.", "Clear all of the orange pegs on a level to beat the level.", ""},
      {"The purple peg is worth 50x the value of the blue pegs.", "This peg changes location on every shot.", ""},
      {"The green peg activates your Magic Power.", "There are two green pegs per level.", ""}}},
    {"Scoring", {"The number of points you get for a shot is the total value of all of the pegs", "multiplied by the number of pegs you hit."},
     {{"Every peg that you hit on a shot adds to a subtotal score.", "The shot score is the subtotal score multiplied by the number of pegs hit.", ""},
      {"The value of the pegs goes up as you eliminate orange pegs.", "The Fevermeter on the right shows the current score multiplier.", ""},
      {"Certain cool shots give you extra style points.  These points add directly", "to your score and don't factor into getting a 25,000 point Free Ball.", ""},
      {"After beating a level you earn extra points from the bonus holes.", "You also earn 10,000 points per extra ball left over.", ""}}},
    {"Free Balls", {"There are a few different ways to get a Free Ball.", ""},
     {{"You get a Free Ball if your ball lands in the Free Ball Bucket.", "", ""},
      {"Scoring 25K, 75K, and 125K on a shot also gives you a Free Ball.", "", ""},
      {"If you don't hit any pegs on a shot then you may be awarded", "a Free Ball by winning a coin flip!", ""},
      {"In Duel Mode, you get style points instead of Free Balls.", "Also, there are no coin flip Free Balls in Duel Mode.", ""}}},
    {"Game Modes", {"There are four game modes in Peggle Nights.", ""},
     {{"Adventure Mode allows you to train with all the Peggle Masters.", "Progress through all the levels to become a Peggle Master yourself.", ""},
      {"Quick Play Mode lets you select an individual level to play.", "You may only play levels that you've completed in Adventure Mode.", ""},
      {"Duel Mode allows you to compete in a two player version of Peggle Nights.", "You can play against the computer or another person.", ""},
      {"Challenge Mode presents you with extra difficult Peggle Nights feats.", "Beat all of the challenges to advance your Peggle Master rank.", ""}}},
    {"Duel Mode", {"Duel Mode has slightly different rules than the single player modes.", ""},
     {{"In Duel Mode, two players take turns shooting the ball.", "The player with the most points at the end of the game wins.", ""},
      {"You must hit at least one orange peg on a shot.", "If you don't then you are penalized by losing 25% of your total score.", ""},
      {"In Duel Mode, you get style points instead of Free Balls.", "Also, there are no coin flip Free Balls in Duel Mode.", ""},
      {"The game ends either when a player eliminates the last orange peg or", "when both players run out of balls.", ""}}},
    {"Instant Replays", {"Instant replays are useful for reliving your favorite shots and showing them", "to your friends and family over and over again."},
     {{"Click the Instant Replay button on the bottom right of the game to", "see a replay of the last shot.", ""},
      {"Right click the Instant Replay button to enable an advanced mode in which", "you can replay all shots for a game.", ""},
      {"Hold down the left mouse button in the play area to activate slow motion.", "Hold down the right button to activate fast forward.", ""},
      {"You can save replays and view them later!", "Click the Replays button on the main menu to view saved replays.", ""}}},
    {"Extra Tidbits", {"What else, what else, what else?", ""},
     {{"After beating Adventure Mode, you can replay it again using any Peggle", "Master on any level!  Can you top your high score?", ""},
      {"Clearing all of the pegs on a level gives you a bonus blue ribbon!", "", ""},
      {"Each level has an Ace Score. Beat that score for a bonus red ribbon!", "Get Ace Scores on all levels for an even fancier trophy!", ""},
      {"Open replays by double-clicking the replay files. You can also", "drag-and-drop replay files into an open Peggle Nights window.", ""}}},
    {"Style Shots", {"Certain kinds of shots earn special recognition and bonus points!", "The second score listed is specific to duel mode."},
     {{"FREE BALL SKILLS", "5,000 / 2,500 points", "Bounce the ball directly from one (and only one) peg into the\nFree Ball Bucket."},
      {"LONG SHOT", "25,000 / 5,000 points", "Hit a non-blue peg first, travel 1/3 the width of the screen and\nhit another non-blue peg."},
      {"DOUBLE LONG SHOT", "25,000 / 10,000 points", "Get a LONG SHOT, then travel 1/3 the width of the screen and hit\na third non-blue peg."},
      {"SUPER LONG SHOT", "50,000 / 10,000 points", "Hit a non-blue peg first, travel 2/3 the width of the screen and\nhit another non-blue peg."}},
     true},
    {"More Style Shots", {"Certain kinds of shots earn special recognition and bonus points!", "The second score listed is specific to duel mode."},
     {{"EXTREME SLIDE", "50,000 / 5,000 points", "Slide above a certain speed for 12 pegs."},
      {"OFF THE WALL", "25,000 / 5,000 points", "Bounce the ball off the wall, travel 1/5 the width of the screen\nand hit a non-blue peg."},
      {"KICK THE BUCKET", "25,000 / 5,000 points", "Bounce the ball off the Free Ball Bucket to hit the final orange\npeg for Fever."},
      {"LUCKY BOUNCE", "25,000 / 2,500 points", "Bounce the ball off the Free Ball Bucket, then catch it again."}},
     true},
    {"Still More Style Shots", {"Certain kinds of shots earn special recognition and bonus points!", "The second score listed is specific to duel mode."},
     {{"MAD SKILLZ", "25,000 / 5,000 points", "Have 5 balls in a row fall in the Free Ball Bucket."},
      {"CRAZY MAD SKILLZ", "100,000 /   N/A   points", "Have 10 balls in a row fall in the Free Ball Bucket."},
      {"ORANGE ATTACK", "50,000 / 5,000 points", "Hit a large percentage of the remaining orange pegs in one shot."},
      {"COOL CLEAR", "50,000 /   N/A   points", "Hit the final orange peg for Fever, then hit the final remaining\npeg for an All Clear."}},
     true},
    {"Master-Specific Style Shots", {"All Masters have a Style Shot awarded for hitting both green pegs in the same shot.", "Some Masters have additional Style Shots specific to their powers."},
     {{"MULTIBALL MADNESS", "25,000 / 5,000 points", "Get the Multiball after hitting the final orange peg for Fever."},
      {"FREE BALL FRENZY", "100,000 / 10,000 points", "Have more than one ball go into the Free Ball Bucket in one shot."},
      {"FLIPPER MANIAC", "25,000 / 5,000 points", "Bounce off a Flipper and hit at least one peg; do this four times."},
      {"SHOCK IT TO ME", "25,000 / 5,000 points", "Zap a dozen or more pegs with Electrobolt."}},
     true},
    {"Even More Master-Specific Style Shots", {"All Masters have a Style Shot awarded for hitting both green pegs in the same shot.", "Some Masters have additional Style Shots specific to their powers."},
     {{"EYE OF THE PYRAMID", "10,000 / 5,000 points", "Get a ball into the Free Ball Bucket without hitting the Pyramid\nbumpers."},
      {"PYRAMID PICKUP", "10,000 / 5,000 points", "Bounce a ball off the Pyramid bumpers and into the Free Ball Bucket."},
      {"HAT TRICK", "10,000 / 2,500 points", "Have a ball wearing the Magic Hat go into the Free Ball Bucket."},
      {"HOT STUFF", "25,000 / 5,000 points", "Bounce a Fireball off the cap on the Free Ball Bucket, then have the\nFireball land in the Free Ball Bucket."}},
     true},
};

int HelpScreen::numPages() const { return app.game == Game::Nights ? 13 : 8; }

HelpScreen::HelpScreen(App& a, bool fb) : Screen(a), fromBoard(fb) {
  Button* prev = patchButton(1, "Previous", "IMAGE_DLG_BROWNBUTTON2", 266, 553);
  patchButton(2, "Next", "IMAGE_DLG_BROWNBUTTON2", prev->x + prev->w + 22, 553);
  auto* done = addButton(0, Button::Kind::Simple, "IMAGE_HELP_BUTTONOVERLAY");
  done->x = 738; done->y = 536;
  done->w = 62; done->h = 62;
  done->image = nullptr;
}
void HelpScreen::onButton(int id) {
  if (id == 0) { app.endHelpScreen(); }
  else if (id == 1) page = page > 0 ? page - 1 : numPages() - 1;
  else if (id == 2) page = page + 1 < numPages() ? page + 1 : 0;
}
void HelpScreen::keyDown(int key, int) {
  if (key == SDLK_ESCAPE || key == SDLK_RETURN) app.endHelpScreen();
  else if (key == SDLK_LEFT) onButton(1);
  else if (key == SDLK_RIGHT) onButton(2);
}
void HelpScreen::drawLayer(Gfx& g) {
  g.fill(0, 0, 800, 600, 0x000020);
  if (Tex* bg = g.img("IMAGE_HELP_SCREEN")) g.draw(bg, 0, 0);
  if (Tex* ov = g.img("IMAGE_HELP_BUTTONOVERLAY")) g.draw(ov, 224, 541);
  const bool nights = app.game == Game::Nights;
  const HelpPage& hp = nights ? kHelpNights[page] : kHelp[page];
  g.drawString("OVERLOAD24GRAD1", 100, 50, "How to Play", 0xb6ffff);
  g.drawString("OVERLOAD24OUTLINE", 400, 110, hp.name, 0xffffff, 1, Align::Center);
  char pg[32];
  std::snprintf(pg, sizeof pg, "Page %d of %d", page + 1, numPages());
  g.drawString("OVERLOAD24GRAD1", nights ? 700 - g.textWidth("OVERLOAD24GRAD1", pg) : 540, 50, pg, 0xb6ffff);
  for (int i = 0; i < 2; i++)
    if (hp.head[i][0]) g.drawWrapped("PLAINOUTLINE", {60, 110.0f + 18 * i, 680, 20}, hp.head[i], 0xffffaa, 1, Align::Center);
  Tex* icons = g.img("IMAGE_HELP_ICONS");
  Tex* frame = g.img("IMAGE_DLG_BLUEFRAME");
  for (int r = 0; r < 4; r++) {
    float y = 170.0f + 90 * r;
    int icol = page < 4 ? page : page == 4 ? -1 : page - 1;
    if (page == 4 && r == 0)
      if (Tex* mi = g.img("IMAGE_HELP_MODES")) {
        g.draw(mi, 32, 170);
        if (frame) g.drawBox(frame, 24, 162, mi->lw() + 14, mi->lh() + 14);
      }
    if (icons && icol >= 0 && icol < icons->cols && (nights || hp.row[r][0][0])) {
      g.drawCel(icons, 72.0f, y, icol, r);
      if (frame) g.drawBox(frame, 64, y - 8, icons->cw() + 14, icons->ch() + 14);
    }
    if (hp.style) {
      if (!hp.row[r][0][0]) continue;
      g.drawString("PLAINOUTLINE", 170, y + 19, hp.row[r][0], 0xffffaa);
      g.drawString("PLAINOUTLINE", 690, y + 19, hp.row[r][1], 0xffffaa, 1, Align::Right);
      std::string d = hp.row[r][2];
      float ly = y + (d.find('\n') == std::string::npos ? 46.0f : 40.0f);
      size_t p = 0;
      while (p <= d.size()) {
        size_t e = d.find('\n', p);
        g.drawString("PLAINOUTLINE", 200, ly, d.substr(p, e == std::string::npos ? std::string::npos : e - p), 0xffffff);
        ly += 18;
        if (e == std::string::npos) break;
        p = e + 1;
      }
      continue;
    }
    int n = 0;
    for (int k = 0; k < 3; k++) if (hp.row[r][k][0]) n++;
    float off = n <= 1 ? 16.0f : n == 2 ? 8.0f : 0.0f;
    for (int k = 0; k < 3; k++)
      if (hp.row[r][k][0]) g.drawString("PLAINOUTLINE", 170, y + 23 + off + 18.0f * k, hp.row[r][k], 0xffffff);
  }
}


TrophyScreen::TrophyScreen(App& a) : Screen(a) {
  a.playMusic(a.game == Game::Deluxe ? "fadeout" : "music/menu.ogg");
  og = a.otherLevels();
  a.pickOther = false;
  page = a.curProfile && ownPages() > 0 ? std::clamp(a.curProfile->i38, 0, ownPages() - 1) : 0;
  if (theirPages() > 0 && a.listPage >= 0) page = ownPages() + std::min(a.listPage, theirPages() - 1);
  a.listPage = -1;
  bBack = embeddedButton(100, "IMAGE_LS_MAINMENUBUTTON", 6, 541);
  bPrev = embeddedButton(101, "IMAGE_LS_BACKBUTTON", 284, 546);
  bNext = embeddedButton(102, "IMAGE_LS_NEXTBUTTON", 399, 546);
  for (int i = 0; i < 5; i++) {
    auto b = std::make_unique<Button>();
    b->id = i;
    b->kind = Button::Kind::Simple;
    b->x = kTileHit[i][0]; b->y = kTileHit[i][1]; b->w = kTileHitW; b->h = kTileHitH;
    b->hoverSound = ""; b->clickSound = "BUTTON1"; b->tickRef = &updateCount;
    bSlot[i] = add(std::move(b));
  }
  syncPage();
}
int TrophyScreen::ownPages() const {
  const bool shown = !og || app.modeOpen(GameMode::Challenge) || !og->challengesOpen();
  return shown ? (int)app.trophies.pages().size() : 0;
}
int TrophyScreen::theirPages() const { return og && og->challengesOpen() ? (int)og->trophies.pages().size() : 0; }
const TrophyPage* TrophyScreen::pageShown(bool& theirs) const {
  const auto& mine = app.trophies.pages();
  theirs = false;
  if (page >= 0 && page < ownPages()) return &mine[(size_t)page];
  if (!og) return nullptr;
  const auto& more = og->trophies.pages();
  const int at = page - ownPages();
  theirs = true;
  return at >= 0 && at < theirPages() ? &more[(size_t)at] : nullptr;
}
void TrophyScreen::syncPage() {
  bPrev->visible = bNext->visible = ownPages() + theirPages() > 0;
  badgeTicks = applauseTicks = 0;
  if (page >= ownPages()) {
    bool theirs = false;
    if (const TrophyPage* pg = pageShown(theirs))
      for (int i = 0; i < 5 && i < (int)pg->trophies.size(); i++)
        if (og->newTrophies.erase(pg->trophies[(size_t)i].id)) badgeTicks = 100;
    return;
  }
  if (app.curProfile) { app.curProfile->i38 = page; app.curProfile->dirty = true; }
  const auto& pages = app.trophies.pages();
  if (page >= 0 && page < (int)pages.size())
    for (int i = 0; i < 5 && i < (int)pages[page].trophies.size(); i++)
      if (app.newTrophies.erase(pages[page].trophies[i].id)) badgeTicks = 100;
}
void TrophyScreen::update() {
  Panel::update();
  if (badgeTicks > 0 && --badgeTicks == 0) {
    applauseTicks = 10;
    app.playSample("AWARD");
  }
  if (applauseTicks > 0 && --applauseTicks == 0) app.playSample("APPLAUSE");
  if (hoverTicks > 0 && --hoverTicks == 0) hovered = -1;
}
void TrophyScreen::onButtonEnter(int id) { if (id >= 0 && id < 5) { hovered = id; hoverTicks = 0; } }
void TrophyScreen::onButtonLeave(int id) { if (id >= 0 && id < 5) { hovered = id; hoverTicks = 10; } }
void TrophyScreen::doPlay(int slot) {
  const auto& pages = app.trophies.pages();
  if (og) {
    bool theirs = false;
    const TrophyPage* pg = pageShown(theirs);
    if (!pg || slot >= (int)pg->trophies.size()) return;
    if (!(theirs ? og->challengesOpen() : app.modeOpen(GameMode::Challenge))) {
      app.doNotYet(challengesLockedText(theirs ? og->game : app.game, theirs ? og->trophies.numTrophies() : app.trophies.numTrophies()));
      return;
    }
    if (theirs) {
      const TrophyInfo& t = pg->trophies[(size_t)slot];
      if (t.levels.empty()) return;
      app.curChallenge = t.id;
      app.gameMode = GameMode::Challenge;
      app.challengeStep = 0;
      if (t.levels[0] == "pick") {
        app.pickOther = true;
        app.listPage = page - ownPages();
        app.showLevelScreen(true);
        return;
      }
      StartTarget target;
      target.place = StartPlace::Challenge;
      target.mode = GameMode::Challenge;
      target.challenge = t.id;
      app.requestGameSwitch(og->game, target);
      return;
    }
  }
  if (page >= (int)pages.size() || slot >= (int)pages[page].trophies.size()) return;
  const TrophyInfo& t = pages[page].trophies[slot];
  app.curChallenge = t.id;
  app.gameMode = GameMode::Challenge;
  if (!t.levels.empty() && t.levels[0] != "pick" && t.levels[0].rfind("rand", 0) != 0 && t.levels[0].rfind("Rand", 0) != 0) {
    app.challengeStep = 0;
    app.challengeLevel = app.resolveChallengeLevel(t.levels[0]);
    app.showBoard(false, true);
  } else if (!t.levels.empty() && (t.levels[0].rfind("rand", 0) == 0 || t.levels[0].rfind("Rand", 0) == 0)) {
    app.challengeStep = 0;
    app.challengeLevel = app.resolveChallengeLevel(t.levels[0]);
    app.showBoard(false, true);
  } else {
    app.challengeStep = 0;
    app.showLevelScreen(true);
  }
}
void TrophyScreen::onButton(int id) {
  if (app.moving()) return;
  if (id >= 0 && id < 5) doPlay(id);
  else if (id == 100) app.showMainMenu();
  else if (id == 101 || id == 102) {
    const int total = ownPages() + theirPages();
    if (total <= 0) return;
    page = id == 101 ? (page > 0 ? page - 1 : total - 1) : (page + 1 < total ? page + 1 : 0);
    syncPage();
  }
}
void TrophyScreen::keyDown(int key, int) {
  if (app.hasModal()) return;
  if (key == SDLK_ESCAPE) onButton(100);
  else if (key == SDLK_LEFT && bPrev->visible) onButton(101);
  else if (key == SDLK_RIGHT && bNext->visible) onButton(102);
}
static void drawTrophyInfo(Gfx& g, const std::string& name, const std::string& desc, int best, bool nights, float dy = 0) {
  const std::string head = "OVERLOAD16OUTLINE", small = "OVERLOAD12OUTLINE", plain = "PLAINOUTLINE";
  const float space = g.fontLineSpacing(head) - 4.0f, pad = g.fontAscentPadding(head);
  const int lines = std::max(1, (int)(g.wrappedHeight(head, 200, name, space) / space + 0.5f));
  const float headH = g.fontHeight(head) - pad + (float)(lines - 1) * space;
  float top = 340.0f, below = 340.0f + headH + (best > 0 ? 15.0f : 30.0f);
  if (nights) {
    const bool tall = headH > 20.0f;
    top = tall ? 340.0f : 348.0f;
    below = (tall ? 328.0f : 348.0f) + headH + 15.0f;
  }
  top += dy;
  below += dy;
  g.drawWrapped(head, {300, top - pad, 200, 0}, name, 0xffff00, 1, Align::Center, space);
  if (best > 0) {
    const std::string line = "Your Best: " + commas(best);
    if (nights) g.drawString(small, 400.0f - (float)((int)g.textWidth(small, line) / 2), 485, line, 0x88ff88);
    else g.drawString(small, 302, 465, line, 0xffff00);
  }
  g.drawWrapped(plain, {300, below, 198, 0}, desc, 0xffffff, 1, Align::Center);
}

void TrophyScreen::drawLayer(Gfx& g) {
  g.fill(0, 0, 800, 600, 0x000020);
  bool theirs = false;
  const TrophyPage* shown = pageShown(theirs);
  if (!shown) {
    if (Tex* bg = g.img("IMAGE_LS_LEVELSCREEN")) g.draw(bg, 0, 0);
    return;
  }
  const TrophyPage& pg = *shown;
  const PlayerInfo* marks = theirs ? og->player : app.curProfile;
  for (int i = 0; i < 5 && i < (int)pg.trophies.size(); i++) {
    const TrophyInfo& t = pg.trophies[i];
    float sx = kSlot[i][0], sy = kSlot[i][1];
    const bool drawn = !t.levels.empty() && (t.levels[0] == "pick" || t.levels[0].rfind("Rand", 0) == 0 || t.levels[0].rfind("rand", 0) == 0);
    Tex* th = t.levels.empty() || drawn ? nullptr : theirs ? og->picture("levels/cached_thumbnails/" + t.levels[0]) : g.img("levels/cached_thumbnails/" + t.levels[0]);
    float cx = kTileHit[i][0] + (float)((int)(kTileHitW - 200) / 2), cy = kTileHit[i][1] + (float)((int)(kTileHitH - 177) / 2);
    if (drawn) {
      static const int kRow[10] = {0, 1, 2, 3, 4, 5, -1, -1, -1, 6};
      Tex* rl = theirs ? og->picture("IMAGE_LS_RANDOMLEVEL") : g.img("IMAGE_LS_RANDOMLEVEL");
      const size_t n = t.levels.size();
      const int row = n >= 1 && n <= 10 ? kRow[n - 1] : -1;
      if (rl && row >= 0) g.drawStretchSrc(rl, 0, (float)row * rl->ch(), rl->cw(), rl->ch(), cx, cy, 200, 177);
    } else if (t.levels.size() == 3) {
      for (int row = 0; row < 177; row++) g.fill(cx, cy + (float)row, 200, 1, (uint32_t)(0x60 + row));
      static const float kPart[3][2] = {{49, 30}, {-1, 98}, {100, 98}};
      g.pushClip(cx, cy, 200, 177);
      for (int k = 2; k >= 0; k--) {
        const std::string file = "levels/cached_thumbnails/" + t.levels[(size_t)k];
        Tex* part = theirs ? og->picture(file) : g.img(file);
        const float px = cx + kPart[k][0], py = cy + kPart[k][1];
        if (part && part->valid()) g.drawStretch(part, px, py, 102, 86);
        g.fill(px, py, 102, 1, 0x808080);
        g.fill(px, py + 85, 102, 1, 0x808080);
        g.fill(px, py, 1, 86, 0x808080);
        g.fill(px + 101, py, 1, 86, 0x808080);
      }
      g.popClip();
    } else if (th && th->valid()) {
      g.drawStretch(th, cx, cy, 200, 177);
    } else {
      g.fill(sx, sy, kSlotW, kSlotH, 0x303060);
    }
    if (marks && marks->trophies.count(t.id))
      if (Tex* bd = theirs ? og->picture("IMAGE_LS_CHALLENGEBADGE") : g.img("IMAGE_LS_CHALLENGEBADGE"))
        g.draw(bd, kTileHit[i][0] + (float)((int)kTileHitW / 2) - (float)((int)bd->lw() / 2), kTileHit[i][1] + (float)((int)kTileHitH / 2) + 10 - (float)((int)bd->lh() / 2));
  }
  if (Tex* bg = g.img("IMAGE_LS_LEVELSCREEN")) g.draw(bg, 0, 0);
  const Game pageGame = theirs ? og->game : app.game;
  const bool firstOwnPage = (theirs ? page - ownPages() : page) == 0;
  const char* header = pageGame == Game::Nights && firstOwnPage ? "Initiate Challenges" : "Master Challenges";
  g.drawString("OVERLOAD24GRAD1", 400, 48, header, 0xb7ffff, 1, Align::Center);
  g.drawString("OVERLOAD12OUTLINE", 0x1b8, 0x1f9, "Page " + std::to_string(page + 1), 0xffff00, 1, Align::Left);
  const TrophyMgr& pageTrophies = theirs ? og->trophies : app.trophies;
  if (pageGame == Game::Nights && marks && (theirs ? page - ownPages() : page) < pageTrophies.numMainPages()) {
    const char* nf = "OVERLOAD16OUTLINE2";
    const std::string text = std::to_string(pageTrophies.numHeld(marks->trophies)) + " / " + std::to_string(pageTrophies.numTrophies());
    if (Tex* st = theirs ? og->picture("IMAGE_LS_TINYCHALLENGE") : g.img("IMAGE_LS_TINYCHALLENGE")) g.draw(st, 613, 545);
    g.drawString(nf, 710 - g.textWidth(nf, text), 583, text, 0xffffff);
  }
  for (int i = 0; i < 5 && i < (int)pg.trophies.size(); i++) {
    const TrophyInfo& t = pg.trophies[i];
    const char* tf = "OVERLOAD16OUTLINE2";
    const float nameW = g.textWidth(tf, t.name);
    g.drawString(tf, kTileHit[i][0] + (float)((int)(kTileHitW - nameW) / 2), kTileHit[i][1] + 26 + g.fontAscent(tf), t.name, bSlot[i]->over ? 0xfff600 : 0x93c814);
    if (bSlot[i]->overAlpha > 0)
      if (Tex* sel = g.img("IMAGE_LS_LEVELSELECTOR")) g.draw(sel, kTileHit[i][0] + 4, kTileHit[i][1], Mode(bSlot[i]->overAlpha));
  }
  if (hovered >= 0 && hovered < 5 && hovered < (int)pg.trophies.size()) {
    const TrophyInfo& t = pg.trophies[hovered];
    drawTrophyInfo(g, t.name.empty() ? pg.name : t.name, t.desc.empty() ? pg.desc : t.desc, marks ? marks->trophyTopScore(t.id) : 0, (theirs ? og->game : app.game) == Game::Nights);
  } else {
    float dy = 0;
    if (og) {
      const std::string name = gameTitle(pageGame);
      g.drawString("OVERLOAD16OUTLINE", 400 - g.textWidth("OVERLOAD16OUTLINE", name) / 2, 365, name, 0xffff00);
      dy = g.fontLineSpacing("OVERLOAD16OUTLINE");
    }
    drawTrophyInfo(g, pg.name, pg.desc, 0, pageGame == Game::Nights, dy);
  }
}

}
