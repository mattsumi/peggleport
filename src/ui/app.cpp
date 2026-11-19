#include "ui/app.h"
#include "ui/session.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>
#include <filesystem>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "log.h"
#include "online/invite.h"
#include "online/steam.h"
#include "online/online_ui.h"
#include "render.h"
#include "ui/dialog.h"
#include "ui/dialogs.h"
#include "ui/game_screen.h"
#include "ui/log_view.h"
#include "ui/other_game.h"
#include "ui/character_dialog.h"
#include "ui/screens.h"
#include "ui/visuals_dialog.h"
#include "gamefind.h"
#include "gamepaths.h"

namespace fs = std::filesystem;

namespace pp {
std::filesystem::path defaultUserDir(Game g) {
  char* p = SDL_GetPrefPath("PopCap Games", g == Game::Deluxe ? "Peggle" : "PeggleNights");
  fs::path r = p ? pathFromUtf8(p) / "userdata" : fs::path("userdata");
  if (p) SDL_free(p);
  return r;
}
}

namespace pp::ui {

const char* fontById(int id) {
  switch (id) {
    case 13: return "OVERLOAD16";
    case 14: return "OVERLOAD16OUTLINE";
    case 15: return "OVERLOAD16OUTLINE2";
    case 29: return "PLAIN";
    case 30: return "PLAIN2BIG";
    case 31: return "SMALL";
    case 32: return "SMALLMONO";
    case 33: return "SCORE_DISP";
    case 34: return "SMALL_DISP";
    case 35: return "BALLSLEFT";
    case 36: return "BUTTON";
    case 37: return "STAGE";
    case 38: return "BONK";
    case 39: return "OVERLOAD10";
    case 40: return "OVERLOAD12";
    case 41: return "OVERLOAD24";
    case 42: return "OVERLOADMONO10";
    case 43: return "OVERLOADMONO12";
    case 44: return "OVERLOAD10OUTLINE";
    case 45: return "OVERLOAD12OUTLINE";
    case 46: return "OVERLOAD24OUTLINE";
    case 47: return "OVERLOAD24GRAD1";
    case 48: return "OVERLOAD24GRAD2";
    case 49: return "PLAINOUTLINE";
    case 50: return "PLAIN2";
    case 51: return "PLAIN2OUTLINE";
    case 52: return "PLAIN2BIGOUTLINE";
  }
  return "PLAIN";
}


Button* Panel::addButton(int id, Button::Kind kind, const std::string& imageId, const std::string& label) {
  auto b = std::make_unique<Button>();
  b->id = id;
  b->kind = kind;
  b->image = app.gfx.img(imageId);
  b->label = label;
  b->tickRef = &updateCount;
  if (b->image) {
    switch (kind) {
      case Button::Kind::Embedded: b->w = b->image->cw(); b->h = b->image->ch(); break;
      case Button::Kind::Patch: b->w = b->image->lw(); b->h = b->image->lh() / 3; break;
      default: b->w = b->image->lw(); b->h = b->image->lh(); break;
    }
  }
  return add(std::move(b));
}

Button* Panel::embeddedButton(int id, const std::string& imageId, float bx, float by) {
  Button* b = addButton(id, Button::Kind::Embedded, imageId);
  b->x = bx;
  b->y = by;
  return b;
}

Button* Panel::patchButton(int id, const std::string& label, const std::string& imageId, float bx, float by) {
  Button* b = addButton(id, Button::Kind::Patch, imageId, label);
  b->x = bx;
  b->y = by;
  b->textOffY = 5;
  if (app.game == Game::Nights) {
    b->labelStyle = imageId == "IMAGE_DLG_PURPLEBUTTON" ? 16 : 0;
  } else {
    b->labelStyle = imageId == "IMAGE_DLG_PURPLEBUTTON" ? 16 : ((imageId == "IMAGE_DLG_BROWNBUTTON1" || imageId == "IMAGE_DLG_BROWNBUTTON2") ? 6 : 9);
    b->labelFullGradient = true;
  }
  return b;
}

Widget* Panel::findWidget(int id) {
  for (auto& w : widgets)
    if (w->id == id) return w.get();
  return nullptr;
}

void Panel::playSound(const std::string& id, float pitch) { app.playSample(id, pitch); }

bool Panel::labelFullGradient() const { return app.game != Game::Nights; }

void Panel::update() {
  updateCount++;
  for (auto& w : widgets) w->update();
}

void Panel::draw(Gfx& g) {
  g.pushOrigin(x, y);
  drawLayer(g);
  for (auto& wp : widgets) {
    if (!wp->visible) continue;
    g.pushOrigin(wp->x, wp->y);
    wp->draw(g);
    g.popOrigin();
  }
  drawOverlay(g);
  g.popOrigin();
}

bool Panel::input(const Input& in) {
  float lx = in.x - x, ly = in.y - y;
  auto topAt = [&](float px, float py) -> Widget* {
    for (size_t i = widgets.size(); i-- > 0;)
      if (widgets[i]->hit(px, py)) return widgets[i].get();
    return nullptr;
  };
  switch (in.type) {
    case Input::MouseMove: {
      if (!mouseVisible) return false;
      Widget* t = capture ? capture : topAt(lx, ly);
      if (capture) {
        bool inside = capture->hit(lx, ly);
        if (inside != capture->over) { if (inside) capture->mouseEnter(); else capture->mouseLeave(); }
        capture->down = capture->down;
        capture->mouseMove(lx - capture->x, ly - capture->y);
        return true;
      }
      if (t != hover) {
        if (hover) hover->mouseLeave();
        hover = t;
        if (hover) hover->mouseEnter();
      }
      if (hover) hover->mouseMove(lx - hover->x, ly - hover->y);
      return hover != nullptr;
    }
    case Input::MouseLeave:
      if (hover) { hover->mouseLeave(); hover = nullptr; }
      return false;
    case Input::MouseDown: {
      if (!mouseVisible) return false;
      Widget* t = topAt(lx, ly);
      if (focusW && focusW != t) { focusW->focused = false; focusW = nullptr; }
      if (!t) return false;
      if (t->wantsFocus()) { focusW = t; t->focused = true; }
      if (t->mouseDown(lx - t->x, ly - t->y, in.button, in.clicks)) { capture = t; return true; }
      return false;
    }
    case Input::MouseUp: {
      if (capture) {
        Widget* c = capture;
        capture = nullptr;
        c->mouseUp(lx - c->x, ly - c->y, in.button);
        if (!c->hit(lx, ly)) c->over = false;
        return true;
      }
      return false;
    }
    case Input::KeyDown:
      if (focusW && focusW->key(in.key, in.mod)) return true;
      keyDown(in.key, in.mod);
      return false;
    case Input::Text:
      if (focusW) return focusW->text(in.text);
      return false;
    default: break;
  }
  return false;
}


App::App(Gfx& g, Vfs& v, Audio* a, Renderer* br, Config c) : gfx(g), vfs(v), audio(a), boardRenderer(br), cfg(c), game(c.game), profiles(c.game), highScores(c.game) {
  rng_ ^= cfg.seed * 2654435761u;
  crt_.seed(cfg.seed ? cfg.seed : (uint32_t)SDL_GetTicks());
  userDir = cfg.userDir.empty() ? defaultUserDir(game) : cfg.userDir;
  log::info("game", titleText() + ": saved games are kept in " + pathToUtf8(userDir));
  stages.load(vfs);
  highScores.setTops([this](const std::string& key) { return stages.scoreListTop(key); });
  PlayerInfo::advStages = stages.numStages() > 0 ? stages.numStages() : 11;
  trophies.load(vfs);
  profiles.setDir(userDir);
  if (!cfg.noSave) {
    profiles.load();
    highScores.load(userDir / "highscores.dat");
    settings.load(userDir.parent_path() / "settings.ini");
    online::Prefs prefs;
    prefs.load(userDir.parent_path() / "online.ini");
    setDiscordEnabled(prefs.discord);
  }
  if (cfg.widescreen >= 0) settings.widescreen = std::clamp(cfg.widescreen, 0, kWidescreenModes - 1);
  maxStage = settings.maxStage;
  maxLevel = settings.maxLevel;
  if (!settings.curUser.empty()) curProfile = profiles.get(settings.curUser);
  if (!curProfile) curProfile = profiles.any();
  if (curProfile) colorblind = curProfile->colorblind;
  checkMaxStage();
  applySettings();
  bothGames = cfg.bothGames;
  homeGame = bothGames ? cfg.home : game;
}

App::~App() {
  pp::online::steam::shutdown();
  clearDiscord();
  if (discord_) discord_->stop();
}

void App::setDiscordEnabled(bool enabled) {
  if (enabled == discordEnabled_) return;
  clearDiscord();
  if (discord_) discord_->stop();
  discord_.reset();
  discordEnabled_ = enabled;
  if (enabled) discord_ = std::make_unique<pp::online::Discord>("1571022918392225882");
}

void App::clearDiscord() {
  discordInvite_.clear();
  discordInviteUntil_ = 0;
  if (discord_) discord_->clear();
}

std::string App::takeDiscordInvite() {
  if (!discordEnabled_ || SDL_GetTicks() >= discordInviteUntil_) discordInvite_.clear();
  std::string invite = std::move(discordInvite_);
  discordInvite_.clear();
  return invite;
}

namespace {

class LastPicture : public Screen {
 public:
  LastPicture(App& a, SDL_Surface* picture, bool fillsWindow) : Screen(a), fillsWindow_(fillsWindow) {
    SDL_Renderer* r = a.gfx.renderer();
    if (!r || !picture) return;
    SDL_Surface* opaque = SDL_ConvertSurface(picture, SDL_PIXELFORMAT_XRGB8888);
    if (!opaque) return;
    tex_.t = SDL_CreateTextureFromSurface(r, opaque);
    if (tex_.t) {
      tex_.w = opaque->w;
      tex_.h = opaque->h;
    }
    SDL_DestroySurface(opaque);
  }
  ~LastPicture() override {
    if (tex_.t) SDL_DestroyTexture(tex_.t);
  }
  const char* name() const override { return "lastpicture"; }
  void drawOverWindow() {
    SDL_Renderer* r = app.gfx.renderer();
    if (!fillsWindow_ || !tex_.t || !r) return;
    int ow = 0, oh = 0;
    app.gfx.outputSize(ow, oh);
    SDL_FRect to{std::round(x / 800.0f * (float)ow), std::round(y / 600.0f * (float)oh), (float)ow, (float)oh};
    SDL_SetTextureAlphaMod(tex_.t, 255);
    SDL_SetTextureColorMod(tex_.t, 255, 255, 255);
    SDL_SetTextureBlendMode(tex_.t, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(r, tex_.t, nullptr, &to);
  }
  void drawLayer(Gfx& g) override {
    if (fillsWindow_ && tex_.t) return;
    if (!tex_.t) {
      g.fill(0, 0, 800, 600, 0x000000);
      return;
    }
    int ow = 0, oh = 0;
    g.outputSize(ow, oh);
    const float k = g.scale();
    if (ow == tex_.w && oh == tex_.h && k > 0) {
      float left = g.offX(), top = g.offY();
      const float wide = std::round(800.0f * k);
      if (left == 0 && top == 0 && (float)tex_.w > wide) left = std::floor(((float)tex_.w - wide) * 0.5f);
      g.drawStretch(&tex_, -left / k, -top / k, (float)tex_.w / k, (float)tex_.h / k);
      return;
    }
    const float fit = std::min((float)tex_.w / 800.0f, (float)tex_.h / 600.0f);
    const float pw = std::round(800.0f * fit), ph = std::round(600.0f * fit);
    g.drawStretchSrc(&tex_, std::floor(((float)tex_.w - pw) * 0.5f), std::floor(((float)tex_.h - ph) * 0.5f), pw, ph, 0, 0, 800, 600);
  }

 private:
  Tex tex_;
  bool fillsWindow_ = false;
};

}

void App::requestGameSwitch(Game g, StartTarget start) {
  start.handOver = true;
  start.player = curProfile ? curProfile->name : std::string();
  start.colorblind = colorblind;
  start.askedAt = SDL_GetTicksNS();
  switchTo_.game = g;
  switchTo_.start = start;
  switchWanted_ = true;
  switchHome_ = false;
  switchWait_ = 0;
}

bool App::takeGameSwitchRequest(GameRequest& r) {
  if (!switchWanted_) return false;
  if (anyDialog() && switchWait_ < 40) return false;
  switchWanted_ = false;
  r = switchTo_;
  return true;
}

void App::gameSwitchRefused(const GameRequest& r) {
  if (r.start.place == StartPlace::Level && dynamic_cast<GameScreen*>(cur_.get())) {
    cfg.otherDir.clear();
    if (!playNewLevel()) static_cast<GameScreen*>(cur_.get())->restartLevel(false);
    return;
  }
  if (game == homeGame || r.game != homeGame) return;
  bothGames = false;
  homeGame = game;
  showMainMenu();
}

void App::setHomeGame(Game g) {
  homeGame = g;
  if (homeHook) homeHook(g);
}

bool App::goHome() {
  if (!bothGames || homeGame == game) return false;
  if (!switchWanted_) {
    requestGameSwitch(homeGame);
    switchHome_ = true;
  }
  return true;
}

bool App::goHomeList(StartPlace place) {
  if (!bothGames || homeGame == game) return false;
  if (switchWanted_) return true;
  StartTarget t;
  t.place = place;
  t.mode = gameMode;
  t.duel = duelSes;
  if (place == StartPlace::ChallengeList) {
    if (const TrophyInfo* c = trophies.byId(curChallenge)) t.page = c->page;
    t.newTrophies.assign(newTrophies.begin(), newTrophies.end());
  } else {
    t.page = curStage;
  }
  requestGameSwitch(homeGame, t);
  switchHome_ = true;
  return true;
}

OtherGame* App::otherLevels() { return game == homeGame ? otherFiles() : nullptr; }

OtherGame* App::otherFiles() {
  if (!bothGames || cfg.otherDir.empty()) return nullptr;
  if (!other_) {
    const Game theirs = otherGame(game);
    auto o = std::make_unique<OtherGame>(theirs);
    std::string err;
    if (!o->load(cfg.otherDir, cfg.otherUserDir.empty() ? defaultUserDir(theirs) : cfg.otherUserDir, gfx.renderer(), &err)) {
      log::warn("game", std::string(gameTitle(theirs)) + ": its levels cannot be listed: " + err);
      cfg.otherDir.clear();
      return nullptr;
    }
    for (int id : cfg.start.newTrophies) o->newTrophies.insert(id);
    other_ = std::move(o);
  }
  other_->choosePlayer(curProfile ? curProfile->name : std::string());
  return other_.get();
}

bool App::modeOpen(GameMode mode) const {
  if (!curProfile) return true;
  if (mode == GameMode::Duel) return curProfile->maxStage >= 1;
  if (mode == GameMode::Challenge) return challengesOpenFor(game, *curProfile);
  return true;
}

bool App::otherOpen(GameMode mode) {
  OtherGame* o = otherLevels();
  if (!o) return false;
  return mode == GameMode::Duel ? o->duelOpen() : mode == GameMode::Challenge ? o->challengesOpen() : true;
}

void App::startAt(const StartTarget& start, SDL_Surface* lastPicture, bool pictureFillsWindow) {
  if (!start.player.empty()) {
    PlayerInfo* p = profiles.get(start.player);
    if (!p) p = profiles.add(start.player);
    if (p) {
      if (curProfile && curProfile != p) saveProfile();
      curProfile = p;
      if (p->colorblind != start.colorblind) {
        p->colorblind = start.colorblind;
        p->dirty = true;
      }
      colorblind = p->colorblind;
      checkMaxStage();
      saveAll();
    }
  }
  stopMusic();
  cur_ = std::make_unique<LastPicture>(*this, lastPicture, pictureFillsWindow);
  scrollDir = 0;
  if (start.place == StartPlace::Adventure && curProfile) enterAdventure();
  else if (!startInList(start)) showMainMenu();
}

bool App::startInList(const StartTarget& start) {
  if (!curProfile || !bothGames) return false;
  const bool list = start.place == StartPlace::LevelList || start.place == StartPlace::ChallengeList;
  if (list != (game == homeGame) && start.place != StartPlace::Level) return false;
  if (start.place == StartPlace::Level) {
    const StageInfo* s = stages.stage(start.stage);
    if (!s || !s->level(start.level) || (start.mode != GameMode::QuickPlay && start.mode != GameMode::Duel)) return false;
    gameMode = start.mode;
    curStage = start.stage;
    curLevel = start.level;
    packStage = s->pack ? start.stage : -1;
    duelSes = start.duel;
    if (game == homeGame && !s->pack) {
      curProfile->levelSelectStage = start.stage;
      curProfile->dirty = true;
    }
    stages.markUsed(curStage, curLevel);
    showBoard(false, true);
    return true;
  }
  if (start.place == StartPlace::Challenge) {
    const TrophyInfo* t = trophies.byId(start.challenge);
    if (!t || t->levels.empty()) return false;
    gameMode = GameMode::Challenge;
    curChallenge = t->id;
    challengeStep = std::clamp(start.step, 0, (int)t->levels.size() - 1);
    challengeMore = false;
    challengeKeepLives = false;
    if (t->levels[(size_t)challengeStep] == "pick") {
      const LevelInfo* li = stages.levelByName(start.challengeLevel);
      if (!li) return false;
      curStage = li->stage;
      curLevel = li->index;
      challengeLevel = li->file;
    } else {
      challengeLevel = resolveChallengeLevel(t->levels[(size_t)challengeStep]);
    }
    showBoard(false, true);
    return true;
  }
  if (start.place == StartPlace::LevelList && (start.mode == GameMode::QuickPlay || start.mode == GameMode::Duel)) {
    if (!otherLevels()) return false;
    gameMode = start.mode;
    duelSes = start.duel;
    packStage = -1;
    listPage = start.page;
    showLevelScreen(false);
    return true;
  }
  if (start.place == StartPlace::ChallengeList) {
    if (!otherLevels()) return false;
    gameMode = GameMode::Challenge;
    listPage = start.page;
    showTrophyScreen();
    return true;
  }
  return false;
}

void App::enterAdventure() {
  PlayerInfo* p = curProfile;
  gameMode = GameMode::Adventure;
  curStage = p ? std::max(0, p->advStage) : 0;
  curLevel = p ? std::max(0, p->advLevel) : 0;
  if (sessionsOf(*this).askToContinue(true)) return;
  if (p && p->advCompletions > 0) {
    if (p->justBeatAdventure()) p->restartAdventure();
    if (game != Game::Nights) {
      showAdventureScreen();
      return;
    }
  }
  startAdventureGame();
}

std::string App::titleText() const { return game == Game::Deluxe ? "Peggle Deluxe" : "Peggle Nights"; }

const LevelInfo* App::curLevelInfo() const {
  const StageInfo* s = stages.stage(curStage);
  return s ? s->level(curLevel) : nullptr;
}

uint32_t App::rand() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

void App::applySettings() {
  if (audio) {
    audio->setVolumes(settings.muted ? 0 : settings.sfx, settings.muted ? 0 : settings.music);
    audio->setFeverVolume(settings.muted ? 0 : settings.fever);
  }
  if (showCursorHook) showCursorHook(settings.customCursors);
}

void App::applyColorblind(bool on) { colorblind = on; }

void App::playSample(const std::string& id, float pitch, float vol) {
  if (!audio) return;
  std::string key = id;
  if (key.rfind("SOUND_", 0) == 0) key = key.substr(6);
  audio->play(key, pitch, vol);
}

void App::checkMaxStage() {
  if (!curProfile) return;
  if (curProfile->maxStage > maxStage) {
    maxStage = curProfile->maxStage;
    maxLevel = curProfile->maxLevel;
  } else if (curProfile->maxStage == maxStage && curProfile->maxLevel > maxLevel) {
    maxLevel = curProfile->maxLevel;
  }
}

void App::saveAll() {
  if (cfg.noSave) return;
  saveProfile();
  profiles.save();
  highScores.save(userDir / "highscores.dat");
  settings.curUser = curProfile ? curProfile->name : "";
  settings.maxStage = maxStage;
  settings.maxLevel = maxLevel;
  settings.save(userDir.parent_path() / "settings.ini");
  if (bothGames && cfg.userDir.empty()) {
    const fs::path file = defaultUserDir(otherGame(game)).parent_path() / "settings.ini";
    Settings theirs;
    theirs.load(file);
    Settings shared = settings;
    shared.curUser = theirs.curUser;
    shared.maxStage = theirs.maxStage;
    shared.maxLevel = theirs.maxLevel;
    shared.save(file);
  }
}

void App::shutdown() {
  clearDiscord();
  if (discord_) discord_->stop();
  discord_.reset();
  if (cur_) cur_->onLeave();
  pp::online::steam::shutdown();
  if (audio) audio->stopLoops();
  saveAll();
}

void App::incLevel(int delta) {
  const StageInfo* st = stages.stage(curStage);
  if (!st || curStage >= numStages()) return;
  curLevel += delta;
  if (curProfile) curProfile->storyPending = false;
  int n = (int)st->levels.size();
  if (curLevel < n) {
    if (curLevel < 0) {
      if (curStage < 1) curLevel -= delta;
      else {
        curStage--;
        const StageInfo* prev = stages.stage(curStage);
        if (prev) curLevel = (int)prev->levels.size() - 1;
      }
    }
  } else {
    if (curStage < numStages()) {
      curLevel = 0;
      curStage++;
      if (curProfile) curProfile->storyPending = true;
    } else {
      curLevel -= delta;
    }
  }
  if (curProfile) {
    PlayerInfo& p = *curProfile;
    p.advLevelAttempts = 0;
    if (p.maxStage < curStage) {
      if (game == Game::Nights && p.maxStage < 3 && curStage >= 3) menuUnlocked = true;
      p.maxStage = curStage;
      p.maxLevel = 0;
      if (gameMode == GameMode::Adventure) p.char1 = CharacterDialog::masterMetAt(game, curStage);
    }
    if (numStages() <= curStage && p.advCompletions < 1000000) p.advCompletions++;
    if (curStage == p.maxStage && p.maxLevel < curLevel) p.maxLevel = curLevel;
    p.advLevel = curLevel;
    p.advStage = curStage;
    p.dirty = true;
    checkMaxStage();
    if (!cfg.noSave) profiles.saveDetails(p);
  }
}

bool App::tryShowNewTrophy() {
  if (!curProfile) return false;
  int old = curProfile->trophyLevelSeen, tier = 0;
  if (curProfile->advCompletions > 0) {
    tier = 1;
    if ((int)trophies.numHeld(curProfile->trophies) >= (int)trophies.numTrophies()) {
      tier = 2;
      if (stages.numClearedOwn(curProfile->clearedLevels) >= stages.numLevels()) tier = 3;
    }
  }
  if (old < tier) showStoryScreen(true, false);
  return old < tier;
}

void App::toggleLogView() {
  if (!logView_) logView_ = std::make_unique<LogView>(*this);
  logView_->toggleDocked();
}

void App::showLogView(bool large) {
  if (!logView_) logView_ = std::make_unique<LogView>(*this);
  logView_->setMode(large ? LogView::Mode::Full : LogView::Mode::Docked);
}

void App::syncProfile() {
  if (auto* mm = dynamic_cast<MainMenu*>(cur_.get())) mm->syncPlayerInfo();
}


App::Mover* App::mover(Panel* l) {
  for (auto& m : movers_)
    if (m.l == l) return &m;
  return nullptr;
}

void App::scrollOff(Panel* l, Dir d, bool remove) {
  Mover m;
  m.l = l;
  m.ticks = 20;
  m.sx = l->x;
  m.sy = l->y;
  m.remove = remove;
  switch (d) {
    case Stay: m.tx = l->x; m.ty = l->y; break;
    case Left: m.tx = -l->w; m.ty = l->y; break;
    case Right: m.tx = 800; m.ty = l->y; break;
    case Down: m.tx = l->x; m.ty = 600; break;
    case Up: m.tx = l->x; m.ty = -l->h; break;
  }
  l->mouseVisible = false;
  scrollDir = d;
  movers_.push_back(std::move(m));
}

void App::scrollOn(Panel* l) {
  if (scrollDir == 0) return;
  Mover m;
  m.l = l;
  m.ticks = 20;
  m.remove = false;
  m.tx = l->x;
  m.ty = l->y;
  m.sx = l->x;
  m.sy = l->y;
  switch (scrollDir) {
    case Left: m.sx = 800; break;
    case Right: m.sx = -l->w; break;
    case Down: m.sy = -l->h; break;
    case Up: m.sy = 600; break;
  }
  l->x = m.sx;
  l->y = m.sy;
  l->mouseVisible = false;
  movers_.push_back(std::move(m));
}

void App::setScreen(std::unique_ptr<Screen> s, Dir offDir) {
  clearDiscord();
  if (switchWanted_ && switchHome_) switchWanted_ = false;
  if (audio && game == Game::Nights && (dynamic_cast<LevelScreen*>(s.get()) || dynamic_cast<TrophyScreen*>(s.get()))) audio->stopSounds();
  scrollDir = 0;
  if (cur_) {
    Screen* old = cur_.get();
    old->onLeave();
    if (audio) audio->stopLoops();
    scrollOff(old, offDir, true);
    movers_.back().owned = std::move(cur_);
  } else if (offDir == Down) {
    scrollDir = Down;
  }
  cur_ = std::move(s);
  cur_->onShow();
  scrollOn(cur_.get());
}

void App::updateMovers() {
  for (size_t i = 0; i < movers_.size();) {
    Mover& m = movers_[i];
    m.ticks--;
    float t = m.ticks / 20.0f, it = 1 - t;
    m.l->x = m.sx * t + m.tx * it;
    m.l->y = m.sy * t + m.ty * it;
    if (m.ticks <= 0) {
      m.l->x = m.tx;
      m.l->y = m.ty;
      if (!m.remove) m.l->mouseVisible = true;
      movers_.erase(movers_.begin() + (long)i);
    } else {
      i++;
    }
  }
}

void App::showLoadingScreen() {
  stopMusic();
  setScreen(std::make_unique<LoadingScreen>(*this), Stay);
  scrollDir = 0;
}

void App::showMainMenu(bool nightsHub) {
  duelSes = DuelSession{};
  killDialog(D_Options);
  if (cur_ && std::string(cur_->name()) == "mainmenu") return;
  if (audio) audio->stopSounds();
  nightsHub = nightsHub && game == Game::Nights && consoleCanvasWidth() >= 960;
  if (!nightsHub && goHome()) return;
  playMusic("fadeout");
  Dir leave = Right;
  if (cur_ && std::string(cur_->name()) == "loading") leave = Left;
  Screen* old = cur_.get();
  (void)old;
  setScreen(std::make_unique<MainMenu>(*this, nightsHub), leave);
  if (leave == Right) {   }
  keepFaeries();
  if (!curProfile) doCreateUserDialog();
  else offerModConsent();
}

void App::keepFaeries() {
  if (!curProfile || faeriesCaught <= 0) return;
  curProfile->faeries = (int)((uint32_t)curProfile->faeries + (uint32_t)faeriesCaught);
  curProfile->dirty = true;
  faeriesCaught = 0;
  saveProfile();
}

void App::showAdventureScreen() {
  setScreen(std::make_unique<AdventureScreen>(*this), Left);
}
void App::showLevelScreen(bool fromTrophy) {
  if (goHomeList(fromTrophy || gameMode == GameMode::Challenge ? StartPlace::ChallengeList : StartPlace::LevelList)) return;
  setScreen(std::make_unique<LevelScreen>(*this), Left);
}
void App::showTrophyScreen() {
  if (goHomeList(StartPlace::ChallengeList)) return;
  setScreen(std::make_unique<TrophyScreen>(*this), Left);
}
void App::showStoryScreen(bool reveal, bool easter) {
  if (curProfile) {
    curProfile->storyPending = false;
    curProfile->dirty = true;
  }
  setScreen(std::make_unique<StoryScreen>(*this, reveal, easter), Right);
}

void App::showBoard(bool fromAdventure, bool newLevel) {
  if (curProfile) applyColorblind(curProfile->colorblind);
  setScreen(std::make_unique<GameScreen>(*this, fromAdventure, newLevel), Left);
}

void App::startAdventureGame() {
  if (curProfile && curProfile->justBeatAdventure()) {
    curProfile->restartAdventure();
    if (!cfg.noSave) profiles.saveDetails(*curProfile);
  }
  gameMode = GameMode::Adventure;
  if (!curProfile) {
    curStage = curLevel = 0;
  } else {
    curStage = std::clamp(curProfile->advStage, 0, std::max(0, numStages() - 1));
    curLevel = std::clamp(curProfile->advLevel, 0, 5);
  }
  if (curProfile && curProfile->storyPending && curLevel == 0) {
    if (game == Game::Nights && curStage == 0 && curProfile->advCompletions == 0) showIntroScreen();
    else showStoryScreen(false, false);
  }
  else showBoard(true, false);
}


Dialog* App::getDialog(int id) const {
  for (auto& d : dialogs_)
    if (d->dialogId == id && !d->gone) return d.get();
  return nullptr;
}

bool App::hasModal() const {
  for (auto& d : dialogs_)
    if (!d->gone && d->modal) return true;
  return false;
}

bool App::anyDialog() const {
  for (auto& d : dialogs_)
    if (!d->gone) return true;
  return false;
}

void App::releaseCovered() {
  bool covered = false;
  for (size_t i = dialogs_.size(); i-- > 0;) {
    Dialog* d = dialogs_[i].get();
    if (d->gone) continue;
    if (covered) d->releaseHover();
    else if (d->modal) covered = true;
  }
  if (covered && cur_ && !cur_->isGame()) cur_->releaseHover();
}

bool App::boardPaused() const {
  auto* gs = dynamic_cast<GameScreen*>(cur_.get());
  return gs && gs->paused();
}

void App::pauseBoard(bool on) {
  if (auto* gs = dynamic_cast<GameScreen*>(cur_.get())) gs->pause(on);
}

void App::killDialog(int id) {
  for (auto& d : dialogs_)
    if (d->dialogId == id) d->gone = true;
}

void App::addDialog(int id, std::unique_ptr<Dialog> d) {
  killDialog(id);
  d->dialogId = id;
  dialogs_.push_back(std::move(d));
}

Dialog* App::doDialog(int id, bool modal, const std::string& header, const std::string& lines, const std::string& footer, int mode) {
  killDialog(id);
  auto d = std::make_unique<Dialog>(*this, id, modal, header, lines, footer, mode);
  Dialog* raw = d.get();
  positionDialog(raw, 350);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
  return raw;
}

void App::positionDialog(Dialog* d, float width, bool fromMenu) {
  float minW = gfx.textWidth(d->headerFont, d->header) + d->insL + d->insR + 0x14;
  if (width < minW) width = minW;
  float hgt = d->preferredHeight(width);
  float y = 0x96;
  if (y + hgt > 0x244 && (y = 0x244 - hgt) < 10) y = 10;
  Dialog* other = nullptr;
  for (auto& o : dialogs_)
    if (!o->gone && !o->scrollingOff && o.get() != d) other = o.get();
  float x;
  if (!other) {
    bool menuUp = cur_ && std::string(cur_->name()) == "mainmenu";
    if (!menuUp || fromMenu || consoleCanvasWidth() >= 960) {
      x = 400 - width / 2;
    } else if (hgt >= 300) {
      x = 0x226 - width;
      y = std::min(50.0f, 600.0f - hgt);
    } else {
      x = 0x122 - width / 2;
    }
  } else {
    bool cert = other->dialogId == D_CreateUser && dynamic_cast<NewUserDialog*>(other) && static_cast<NewUserDialog*>(other)->certificate;
    y = (cert ? 0x8c : 0) + other->y;
    x = std::trunc((other->w - width) / 2) + other->x;
  }
  if (d->dialogId == D_Options && width > 440) y = std::min(y, 530 - hgt);
  d->resize(x, y, width, hgt);
}

void App::doToMenu() {
  killDialog(D_ToMenu);
  showMainMenu();
}

void App::doNotYet(const std::string& text) {
  Dialog* d = doDialog(D_Locked, true, "Locked", text, "OK", BUTTONS_FOOTER);
  d->rope = true;
  if (consoleCanvasWidth() >= 960) positionDialog(d, 490);
}

void App::doConfirmQuitDialog() {
  Dialog* d = doDialog(D_Quit, true, game == Game::Nights ? "Quit?" : "Quit Peggle?", "Are you sure you want to\nquit the game?", "", BUTTONS_YES_NO);
  d->x = 235;
  d->scrollTarget = 280;
}
void App::doConfirmNewChallengeDialog() {
  Dialog* d = doDialog(D_NewChallenge, true, "New Challenge?", "Are you sure that you want to choose a new challenge? Your progress on this challenge will be lost.", "", BUTTONS_YES_NO);
  d->setYesNo("New Challenge", "CANCEL");
  positionDialog(d, 400);
}
void App::doConfirmRestartAdventureDialog() {
  Dialog* d = doDialog(D_RestartAdventure, true, "Restart Adventure?",
                       "Are you sure that you want to ^00FF00^RESTART ADVENTURE?^oldclr^\n\nThis will put you all the way back to the first level and reset your score to zero.", "", BUTTONS_YES_NO);
  d->textAlign = -1;
  d->setYesNo("Restart Adventure", "CANCEL");
  positionDialog(d, 460);
}


void App::tick() {
  updates_++;
  if (switchWanted_) switchWait_++;
  if (audio) audio->tickMusic();
  serviceModApply();
  updateMovers();
  releaseCovered();
  for (auto& d : dialogs_) d->update();
  if (logView_ && logView_->visible()) logView_->update();
  if (cur_) cur_->update();
  if (overlay_) overlay_->update();
  if (audio) audio->setBoardLeaving(true);
  for (auto& m : movers_)
    if (m.owned) m.owned->update();
  if (audio) audio->setBoardLeaving(false);
  for (size_t i = 0; i < dialogs_.size();)
    if (dialogs_[i]->gone) dialogs_.erase(dialogs_.begin() + (long)i);
    else i++;
  pp::online::steam::ensureInit();
  pp::online::steam::pump();
  if (discord_) {
    pp::online::Discord::Presence presence;
    presence.details = titleText();
    presence.state = cur_ && cur_->isGame() ? "Playing a level" : "In menus";
    if (cur_) cur_->discordPresence(presence);
    if (!discord_->update(presence)) clearDiscord();
    discord_->pump();
    const auto status = discord_->status();
    if (status.state != pp::online::Discord::State::Ready || SDL_GetTicks() >= discordInviteUntil_) discordInvite_.clear();
    if (status.state == pp::online::Discord::State::Failed) {
      log::warn("net", "Discord activity stopped: " + std::to_string(status.errorCode) + " " + status.errorCommand + " " + status.error);
      discord_->stop();
    }
    std::string invite = discord_->takeJoinRequest();
    if (!invite.empty() && pp::online::parseInvite(invite, nullptr)) {
      discordInvite_ = std::move(invite);
      discordInviteUntil_ = SDL_GetTicks() + 5000;
    }
  }
}

void App::input(const Input& in) {
  if (in.type == Input::MouseMove || in.type == Input::MouseDown) { mouseX = in.x; mouseY = in.y; }
  if (in.type == Input::KeyDown && in.key == SDLK_F10) {
    toggleLogView();
    return;
  }
  if (logView_ && logView_->handle(in)) return;
  if (in.type == Input::FocusLost) {
    windowFocused = false;
    if (audio) audio->setFocusMuted(true);
    if (cur_) cur_->lostFocus();
    return;
  }
  if (in.type == Input::FocusGained) {
    windowFocused = true;
    if (audio) audio->setFocusMuted(false);
    return;
  }
  if (switchWanted_) return;
  Input din = in;
  din.x -= (canvasW_ - 800) * 0.5f;
  if (overlay_ && overlay_->mouseVisible) {
    overlay_->input(din);
    return;
  }
  if (canvasW_ >= 960 && (dynamic_cast<HelpScreen*>(overlay_.get()) ||
      std::any_of(movers_.begin(), movers_.end(), [](const Mover& m) {
        return dynamic_cast<HelpScreen*>(m.owned.get()) != nullptr;
      }))) return;
  for (size_t i = dialogs_.size(); i-- > 0;) {
    Dialog* d = dialogs_[i].get();
    if (d->gone) continue;
    d->input(din);
    if (d->modal) {
      if (d->dialogId == D_Character)
        if (auto* gs = dynamic_cast<GameScreen*>(cur_.get())) gs->menuThroughDialog(in);
      return;
    }
  }
  if (!cur_) return;
  if (in.type == Input::MouseDown && !cur_->mouseVisible) return;
  cur_->input(in);
}

WidescreenMode App::widescreenMode() const { return (WidescreenMode)std::clamp(settings.widescreen, 0, kWidescreenModes - 1); }

int App::consoleCanvasWidth() const {
  int ow = 800, oh = 600;
  if (auto* r = gfx.renderer()) SDL_GetRenderOutputSize(r, &ow, &oh);
  else gfx.outputSize(ow, oh);
  const int width = widescreenCanvasWidth(widescreenMode(), ow, oh);
  return width < 960 ? 800 : std::min(width, 1067);
}

int App::boardWidthForNewBoard() const { return cfg.boardWidth >= 650 ? cfg.boardWidth : 650; }

float App::pointerOnCanvas(int canvasW) const {
  if (canvasW == (int)gfx.canvasW()) return mouseX;
  int ow = 800, oh = 600;
  gfx.outputSize(ow, oh);
  const float wx = gfx.offX() + mouseX * gfx.scale();
  const float s = std::min((float)ow / (float)canvasW, (float)oh / 600.0f);
  return (wx - ((float)ow - (float)canvasW * s) * 0.5f) / s;
}

int App::frameCanvasWidth(int boardCanvas) const {
  const auto mode = widescreenMode();
  if (mode == WidescreenMode::Classic4x3) return boardCanvas;
  if (mode == WidescreenMode::W16x9 && boardCanvas == 800 && !settings.vis.ultraWideHud) return 1066;
  int ow = 800, oh = 600;
  gfx.outputSize(ow, oh);
  const int width = widescreenFrameWidth(boardCanvas, ow, oh, widescreenCanvasWidth(mode, ow, oh), settings.vis.ultraWideHud);
  return mode == WidescreenMode::W16x9 && boardCanvas == 800 ? std::max(1066, width) : width;
}

void App::frame(int canvasW) {
  gfx.begin(canvasW ? canvasW : (cur_ ? cur_->canvasWidth() : 800), cur_ ? cur_->canvasHeight() : 600);
  canvasW_ = (float)gfx.canvasW();
  if (auto* visuals = dynamic_cast<VisualsDialog*>(getDialog(D_Visuals))) visuals->fitConsoleFooter();
  gfx.setBackdropEnabled(widescreenMode() != WidescreenMode::Classic4x3);
  const bool backdrop = !(cur_ && cur_->isGame()) && gfx.beginBackdrop();
  gfx.clear(0);
  if (!backdrop) gfx.clipToCanvas();
  for (auto& m : movers_)
    if (m.owned && !(canvasW_ >= 960 && dynamic_cast<HelpScreen*>(m.owned.get()))) m.owned->draw(gfx);
  const bool clipGameplayUi = cur_ && cur_->isGame() && canvasW_ >= 960 &&
      widescreenMode() != WidescreenMode::Classic4x3;
  const auto drawScreen = [&](GameScreen* game) {
    const auto view = game ? game->consoleView() : Renderer::View{};
    const float s = gfx.scale(), x = gfx.offX(), y = gfx.offY();
    gfx.setTransform(s * view.k, x + view.x * s, y + view.y * s);
    cur_->draw(gfx);
    gfx.setTransform(s, x, y);
  };
  if (cur_) {
    auto* gameScreen = clipGameplayUi && boardRenderer ? dynamic_cast<GameScreen*>(cur_.get()) : nullptr;
    auto* pause = gameScreen && gameScreen->ok() && !gameScreen->viewing() ? dynamic_cast<OptionsDialog*>(getDialog(D_Options)) : nullptr;
    const bool menuModal = !cur_->isGame() && boardRenderer && canvasW_ >= 960 &&
        widescreenMode() != WidescreenMode::Classic4x3 && hasModal();
    if ((pause && pause->fromGame && pause->usesConsoleOptionsChrome()) || menuModal) {
      int ow = 0, oh = 0;
      gfx.outputSize(ow, oh);
      const int x = (int)std::round(gfx.offX()), y = (int)std::round(gfx.offY());
      auto* options = dynamic_cast<OptionsDialog*>(getDialog(D_Options));
      const float amount = pause ? pause->chromeOpacity() : options ? options->chromeOpacity() : 1.0f;
      boardRenderer->drawPauseBackdrop(amount, {x, y, ow - 2 * x, oh - 2 * y}, [&] { drawScreen(gameScreen); });
    } else drawScreen(gameScreen);
  }
  if (clipGameplayUi) gfx.clipToCanvas();
  gfx.pushOrigin((canvasW_ - 800) * 0.5f, 0);
  for (auto& d : dialogs_)
    if (!d->gone) d->draw(gfx);
  if (canvasW_ >= 960) for (auto& m : movers_)
    if (auto* help = dynamic_cast<HelpScreen*>(m.owned.get())) help->draw(gfx);
  if (overlay_) overlay_->draw(gfx);
  gfx.popOrigin();
  if (clipGameplayUi) gfx.popClip();
  if (logView_) logView_->drawView(gfx);
  if (backdrop) gfx.endBackdrop();
  else gfx.popClip();
  for (auto& m : movers_)
    if (auto* last = dynamic_cast<LastPicture*>(m.owned.get())) last->drawOverWindow();
  if (auto* last = dynamic_cast<LastPicture*>(cur_.get())) last->drawOverWindow();
}

}
