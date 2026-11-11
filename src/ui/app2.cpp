#include <SDL3/SDL.h>
#include <utility>
#include <memory>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <cctype>
#include "ui/hooks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render.h"
#include "ui/dialog.h"
#include "ui/dialogs.h"
#include "ui/character_dialog.h"
#include "ui/game_screen.h"
#include "ui/screens.h"

namespace pp::ui {

bool App::hasBoard() const { return dynamic_cast<GameScreen*>(cur_.get()) != nullptr || stashed_ != nullptr; }

void App::playMusic(const std::string& which) {
  if (!audio) return;
  std::string path;
  if (which == "fadeout") {
    curMusic_.clear();
    audio->fadeOutMusic();
    return;
  } else if (which.rfind("beats:", 0) == 0) {
    if (game != Game::Deluxe || curMusic_ == which) return;
    curMusic_ = which;
    audio->playMusic("music/PeggleBeats.mo3", true, std::atoi(which.c_str() + 6));
    return;
  } else if (which == "menu") path = game == Game::Deluxe ? "music/opening.ogg" : "music/Menu_Amb_2.ogg";
  else if (which == "credits") path = "music/credits.ogg";
  else if (which == "level") {
    curMusic_ = "level";
    audio->playLevelMusic(game != Game::Deluxe);
    return;
  } else path = which;
  if (path == curMusic_) return;
  curMusic_ = path;
  audio->playMusic(path, true);
}

void App::stopMusic() {
  curMusic_.clear();
  if (audio) audio->stopMusic();
}


void App::doUserDialog() {
  killDialog(D_UserList);
  auto d = std::make_unique<UserDialog>(*this);
  Dialog* raw = d.get();
  positionDialog(raw, 400);
  raw->x = 0x96;
  raw->y = 0x3c;
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::doCreateUserDialog() {
  killDialog(D_CreateUser);
  auto d = std::make_unique<NewUserDialog>(*this, false, true, curProfile != nullptr);
  Dialog* raw = d.get();
  float h = raw->preferredHeight(raw->w);
  raw->resize((800 - raw->w) / 2, (600 - h) / 2, raw->w, h);
  if (dynamic_cast<MainMenu*>(cur_.get())) { raw->x = 0x1e; raw->y = 0x1e; }
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::doRenameUserDialog(const std::string& name) {
  killDialog(D_RenameUser);
  auto d = std::make_unique<NewUserDialog>(*this, true, false, true);
  NewUserDialog* raw = d.get();
  positionDialog(raw, raw->w);
  raw->setName(name);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::doConfirmDeleteUserDialog(const std::string& name) {
  killDialog(D_DeleteUser);
  Dialog* d = doDialog(D_DeleteUser, true, "Delete User?", "This will permanently remove '" + name + "' from the player roster!", "", BUTTONS_YES_NO);
  d->setYesNo("Delete", "Cancel");
}

void App::doDialogScroll(int id, bool modal, const std::string& header, const std::string& lines, const std::string& footer, int mode) {
  doDialog(id, modal, header, lines, footer, mode);
}

void App::finishUserDialog(bool accepted) {
  auto* d = dynamic_cast<UserDialog*>(getDialog(D_UserList));
  if (!d) return;
  if (accepted) {
    if (curProfile) saveProfile();
    PlayerInfo* p = profiles.get(d->selName());
    if (p) {
      curProfile = p;
      colorblind = p->colorblind;
      syncProfile();
      checkMaxStage();
      if (!cfg.noSave) { settings.curUser = p->name; profiles.save(); }
    }
  }
  d->doScrollOff();
}

void App::finishCreateUserDialog(bool accepted) {
  auto* d = dynamic_cast<NewUserDialog*>(getDialog(D_CreateUser));
  if (!d) return;
  std::string name = d->name();
  const char* needName = "Please enter your name to create a new user profile for storing high score data and game progress.";
  if (!accepted || name.empty()) {
    if (!curProfile && (!accepted || name.empty())) { doDialogScroll(D_NameError, true, "Enter Your Name", needName, "OK", BUTTONS_FOOTER); return; }
    if (!accepted) { d->doScrollOff(); return; }
    doDialogScroll(D_NameError, true, "Enter Your Name", needName, "OK", BUTTONS_FOOTER);
    return;
  }
  PlayerInfo* p = profiles.add(name);
  if (!p) {
    doDialogScroll(D_NameError, true, "Name Conflict", "The name you entered is already being used.  Please enter a unique player name.", "OK", BUTTONS_FOOTER);
    return;
  }
  if (!cfg.noSave) profiles.save();
  if (curProfile) saveProfile();
  curProfile = p;
  keepFaeries();
  colorblind = p->colorblind;
  checkMaxStage();
  if (!d->certificate) {
    if (auto* u = getDialog(D_UserList)) u->doScrollOff();
    d->doScrollOff();
  } else {
    d->doApproved();
  }
  syncProfile();
  if (!cfg.noSave) { settings.curUser = p->name; profiles.save(); }
}

void App::onProfileCreated() {
  if (auto* ls = dynamic_cast<LoadingScreen*>(cur_.get())) ls->adventureStartDelay = 20;
  syncProfile();
}

void App::finishRenameUserDialog(bool accepted) {
  auto* nd = dynamic_cast<NewUserDialog*>(getDialog(D_RenameUser));
  if (!nd) return;
  if (!accepted) { nd->doScrollOff(); return; }
  auto* ud = dynamic_cast<UserDialog*>(getDialog(D_UserList));
  if (!ud) return;
  std::string oldName = ud->selName(), newName = nd->name();
  if (newName.empty()) return;
  if (curProfile) saveProfile();
  bool wasCur = curProfile && curProfile->name == oldName;
  if (!profiles.rename(oldName, newName)) {
    doDialogScroll(D_NameErrorRename, true, "Name Conflict", "The name you entered is already being used.  Please enter a unique player name.", "OK", BUTTONS_FOOTER);
    return;
  }
  if (!cfg.noSave) profiles.save();
  if (wasCur) curProfile = profiles.get(newName);
  ud->renamed(newName);
  nd->doScrollOff();
  syncProfile();
}

void App::finishNameErrorDialog(int id) {
  if (auto* d = getDialog(id)) d->doScrollOff();
  if (auto* nd = getDialog(id == D_NameError ? D_CreateUser : D_RenameUser)) { nd->focusW = nd->widgets.empty() ? nullptr : nd->focusW; }
}

void App::onWindowClose() {
  if (quit_ || getDialog(D_Quit) || !cur_ || std::string(cur_->name()) == "loading" || gfx.headless()) { requestQuit(); return; }
  if (hasBoard() && !anyDialog()) { pauseBoard(true); quitPaused = true; }
  doConfirmQuitDialog();
}

void App::doOptionsDialog() {
  if (getDialog(D_Options)) return;
  pauseBoard(true);
  auto d = std::make_unique<OptionsDialog>(*this, hasBoard());
  OptionsDialog* raw = d.get();
  positionDialog(raw, 0x1b8);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::finishOptionsDialog(bool apply, bool scrollOffIt) {
  auto* d = dynamic_cast<OptionsDialog*>(getDialog(D_Options));
  if (!d) return;
  if (apply) {
    settings.fullscreen = d->fullscreen->checked;
    settings.customCursors = d->cursors->checked;
    settings.vsync = d->vsync->checked;
    if (curProfile) { curProfile->colorblind = d->colorblind->checked; curProfile->dirty = true; }
    colorblind = d->colorblind->checked;
    if (setFullscreenHook) setFullscreenHook(settings.fullscreen);
    applySettings();
    if (!cfg.noSave) { saveAll(); }
  }
  if (scrollOffIt) d->doScrollOff();
  else killDialog(D_Options);
  pauseBoard(false);
}


void App::dialogButton(int dlg, int btn) {
  bool yes = btn == ID_YES;
  auto off = [&](int id) { if (auto* d = getDialog(id)) d->doScrollOff(); };
  switch (dlg) {
    case D_DeleteUser:
      off(dlg);
      if (yes)
        if (auto* ud = dynamic_cast<UserDialog*>(getDialog(D_UserList))) ud->confirmDelete();
      return;
    case D_ReturnToMenu:
      off(dlg);
      if (yes) {
        if (curProfile && curProfile->b58 < 255) { curProfile->b58++; curProfile->dirty = true; }
        finishOptionsDialog(false, false);
        doToMenu();
      }
      return;
    case D_Quit:
      if (yes) requestQuit();
      else {
        off(dlg);
        if (quitPaused) { quitPaused = false; pauseBoard(false); }
      }
      return;
    case D_RestartLevel:
      off(dlg);
      if (yes) {
        finishOptionsDialog(true, true);
        if (gameMode == GameMode::QuickPlay || gameMode == GameMode::Duel) showLevelScreen(false);
        else if (gameMode == GameMode::Challenge) restartChallenge();
        else if (auto* gs = dynamic_cast<GameScreen*>(cur_.get())) gs->restartLevel();
      }
      return;
    case D_NewChallenge:
      off(dlg);
      if (yes) {
        finishOptionsDialog(true, true);
        showTrophyScreen();
      }
      return;
    case D_RestartAdventure:
      off(dlg);
      if (yes && curProfile) {
        curProfile->restartAdventure();
        if (!cfg.noSave) {
          profiles.saveDetails(*curProfile);
          std::error_code ec;
          std::filesystem::remove(profiles.savedGamePath(*curProfile, GameMode::Adventure), ec);
        }
        if (auto* as = dynamic_cast<AdventureScreen*>(cur_.get())) as->startGame();
      }
      return;
    case D_WhichAdventure: {
      off(dlg);
      if (btn != ID_YES && btn != ID_NO) return;
      const Game wanted = yes ? Game::Deluxe : Game::Nights;
      if (wanted != game) {
        StartTarget target;
        target.place = StartPlace::Adventure;
        requestGameSwitch(wanted, target);
      } else if (auto* mm = dynamic_cast<MainMenu*>(cur_.get())) {
        mm->startAdventure();
      }
      return;
    }
    case D_CreateUser: finishCreateUserDialog(yes); return;
    case D_NameError: case D_NameErrorRename: finishNameErrorDialog(dlg); return;
    case D_ToMenu: off(dlg); return;
    case D_Tip: off(dlg); return;
    case D_Character:
      if (auto* cd = dynamic_cast<CharacterDialog*>(getDialog(dlg))) {
        cd->accept();
        if (auto* gs = dynamic_cast<GameScreen*>(cur_.get())) gs->masterChosen(cd->picked[0]);
      }
      off(dlg);
      return;
    case D_Options: finishOptionsDialog(yes, true); return;
    case D_Replay1: case D_Replay2: case D_SaveAll: case D_DeleteReplay: case D_ReplayError: replayDialogButton(dlg, btn); return;
    case D_RenameUser: finishRenameUserDialog(yes); return;
    case D_UserList: finishUserDialog(yes); return;
    case D_ImportProfile: importDialogButton(dlg, btn); return;
    default: off(dlg); return;
  }
}

void App::alignQuestionWithMenu(Dialog* d) {
  auto* o = dynamic_cast<OptionsDialog*>(getDialog(D_Options));
  if (!o || !o->fromGame) return;
  o->layout();
  d->scrollTarget = o->y + o->mainMenu->y + o->mainMenu->h - d->h;
}

void App::doAdventureQuestion() {
  Dialog* d = doDialog(D_WhichAdventure, true, "Adventure", "Which adventure do you want to play?", "", BUTTONS_YES_NO);
  d->setYesNo("Peggle Deluxe", "Peggle Nights");
  d->escapeButton = ID_CLOSE;
  d->returnButton = game == Game::Nights ? ID_NO : ID_YES;
  positionDialog(d, 400);
}

void App::doConfirmMainMenuDialog() {
  if (const auto* gs = dynamic_cast<GameScreen*>(cur_.get()); gs && !gs->hasProgress()) {
    finishOptionsDialog(false, false);
    doToMenu();
    return;
  }
  Dialog* d = doDialog(D_ReturnToMenu, true, "Return to Main Menu?", "Are you sure you want to quit this game and return to the main menu?", "", BUTTONS_YES_NO);
  alignQuestionWithMenu(d);
}
void App::doConfirmRestartLevelDialog() {
  const char* title = "New Level?";
  const char* text = "Are you sure that you want to pick a new level?";
  const char* yes = "New Level";
  if (gameMode == GameMode::Challenge) {
    title = "Restart Challenge?";
    text = "Are you sure that you want to restart the challenge?";
    yes = "Restart";
  } else if (gameMode == GameMode::Adventure) {
    title = "Restart Level?";
    text = "Are you sure that you want to restart the level?";
    yes = "Restart";
  } else if (const auto* gs = dynamic_cast<GameScreen*>(cur_.get()); gs && !gs->hasProgress()) {
    dialogButton(D_RestartLevel, ID_YES);
    return;
  }
  Dialog* d = doDialog(D_RestartLevel, true, title, text, "", BUTTONS_YES_NO);
  d->setYesNo(yes, "NO");
  alignQuestionWithMenu(d);
}

void App::restartChallenge() {
  auto* gs = dynamic_cast<GameScreen*>(cur_.get());
  if (!gs) return;
  const TrophyInfo* t = trophies.byId(curChallenge);
  challengeStep = 0;
  challengeMore = false;
  challengeKeepLives = false;
  if (t && !t->levels.empty() && t->levels[0] != "pick") challengeLevel = resolveChallengeLevel(t->levels[0]);
  gs->startChallengeLevel();
}


void App::showEndLevel(int mode, const LevelOutcome& o, const std::string& level, const std::string& title, int rank, bool newBest) {
  killDialog(D_EndLevel);
  auto d = std::make_unique<EndLevelDialog>(*this, (EndLevelDialog::Mode)mode, o, level, title, rank, newBest);
  EndLevelDialog* raw = d.get();
  float h = raw->preferredHeight(raw->w);
  raw->resize((800 - raw->w) / 2, std::max(55.0f, std::min((600 - h) / 2, 590 - h)), raw->w, h);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::endLevelButton(int id, int mode) {
  if (auto* d = getDialog(D_EndLevel)) d->doScrollOff();
  auto* gs = dynamic_cast<GameScreen*>(cur_.get());
  auto m = (EndLevelDialog::Mode)mode;
  if (id == 1000 && gs && game == Game::Nights && (m == EndLevelDialog::ChallengeLevelDone || m == EndLevelDialog::ChallengeLives)) {
    restartChallenge();
    return;
  }
  if (id == 1001 && newMantel && game == Game::Nights && m != EndLevelDialog::AdventureComplete) {
    newMantel = false;
    challengeMore = false;
    showMainMenu();
    return;
  }
  if (id == 1000) {
    if (game == Game::Nights && m == EndLevelDialog::ChallengeLose) showTrophyScreen();
    else doToMenu();
    return;
  }
  if (id == 0 && gs && curProfile && (m == EndLevelDialog::QuickWin || m == EndLevelDialog::QuickLose)) {
    int s = -1, l = -1;
    if (stages.randomLevel(*curProfile, s, l, gameMode == GameMode::Duel, rand()) && s >= 0) {
      curStage = s;
      curLevel = l;
      curProfile->levelSelectStage = s;
      curProfile->dirty = true;
      stages.markUsed(s, l);
      gs->nextLevel();
      return;
    }
  }
  bool win = m == EndLevelDialog::AdventureWin || m == EndLevelDialog::QuickWin || m == EndLevelDialog::ChallengeWin || m == EndLevelDialog::AdventureComplete;
  if (m == EndLevelDialog::AdventureComplete) {
    if (game == Game::Nights) {
      playSample("APPLAUSE_LONG");
      showProgressScreen(4);
      return;
    }
    if (!tryShowNewTrophy()) showMainMenu();
    return;
  }
  if (!gs) return;
  if (m == EndLevelDialog::AdventureWin) {
    if (curProfile && curProfile->storyPending) {
      if (game == Game::Nights) showProgressScreen(1);
      else showStoryScreen(false, false);
    }
    else gs->nextLevel();
  } else if (m == EndLevelDialog::ChallengeLives) {
    challengeKeepLives = true;
    gs->startChallengeLevel();
  } else if (win || m == EndLevelDialog::ChallengeLevelDone) {
    if (m == EndLevelDialog::QuickWin) gs->restartLevel(false);
    else if (challengeMore) {
      challengeMore = false;
      const TrophyInfo* t = trophies.byId(curChallenge);
      challengeStep++;
      if (t && challengeStep < (int)t->levels.size()) challengeLevel = resolveChallengeLevel(t->levels[challengeStep]);
      gs->startChallengeLevel();
    } else showTrophyScreen();
  } else if (m == EndLevelDialog::ChallengeLose) {
    const TrophyInfo* t = trophies.byId(curChallenge);
    challengeStep = 0;
    if (t && !t->levels.empty() && t->levels[0] != "pick") challengeLevel = resolveChallengeLevel(t->levels[0]);
    gs->startChallengeLevel();
  } else {
    gs->restartLevel(false);
  }
}

std::string App::resolveChallengeLevel(const std::string& tok) {
  std::string l = tok;
  for (auto& c : l) c = (char)std::tolower((unsigned char)c);
  if (l.rfind("rand", 0) == 0 && curProfile) {
    int s = -1, lv = -1;
    if (stages.randomLevel(*curProfile, s, lv, false, rand()) && s >= 0)
      if (const StageInfo* st = stages.stage(s))
        if (const LevelInfo* li = st->level(lv)) { curStage = s; curLevel = lv; return li->file; }
  }
  if (const LevelInfo* li = stages.levelByName(tok)) { curStage = li->stage; curLevel = li->index; }
  return tok;
}


void App::showEditor(const std::string& level) {
  if (!editorHookAvailable()) return;
  if (auto* s = static_cast<Screen*>(pp_editor_make_screen_level(this, level.c_str()))) setScreen(std::unique_ptr<Screen>(s), Stay);
  else pp_editor_run(this);
}

void App::showHelpScreen(bool fromBoard) {
  if (overlay_) return;
  overlay_ = std::make_unique<HelpScreen>(*this, fromBoard);
  scrollDir = Left;
  scrollOn(overlay_.get());
}

void App::endHelpScreen() {
  if (!overlay_) return;
  Screen* s = overlay_.get();
  scrollOff(s, Right, true);
  movers_.back().owned = std::move(overlay_);
  (void)s;
}

void App::showScreen(const std::string& n) {
  if (n == "loading") showLoadingScreen();
  else if (n == "menu" || n == "mainmenu") showMainMenu();
  else if (n == "adventure") showAdventureScreen();
  else if (n == "levels") { gameMode = GameMode::QuickPlay; showLevelScreen(false); }
  else if (n == "trophies") { gameMode = GameMode::Challenge; showTrophyScreen(); }
  else if (n == "help") showHelpScreen(false);
  else if (n == "mods") showModsScreen();
  else if (n == "story") showStoryScreen(false, false);
  else if (n == "board") { gameMode = GameMode::Adventure; showBoard(true, true); }
  else if (n == "duel") { gameMode = GameMode::Duel; showBoard(true, true); }
  else if (n == "options") doOptionsDialog();
  else if (n == "visuals") doVisualsDialog();
  else if (n == "users") doUserDialog();
  else if (n == "newuser") doCreateUserDialog();
  else if (n == "quit") doConfirmQuitDialog();
  else if (n == "intro") showIntroScreen();
  else if (n == "progress") showProgressScreen(2);
  else if (n == "progress1") showProgressScreen(1);
  else if (n == "progress4") showProgressScreen(4);
  else if (n == "credits") showCreditsScreen();
  else if (n.rfind("editor", 0) == 0) showEditor(n.size() > 7 ? n.substr(7) : "");
}

}
