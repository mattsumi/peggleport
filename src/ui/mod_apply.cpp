#include <memory>
#include <string>
#include <utility>

#include "audio.h"
#include "log.h"
#include "mods/manager.h"
#include "render.h"
#include "ui/app.h"

namespace pp::ui {

void App::requestModApply() { modApplyPending_ = true; }

bool App::modApplyBlocked(std::string* why) const {
  const std::string n = cur_ ? cur_->name() : "";
  const char* reason = nullptr;
  if (n == "onlinematch") reason = "An online match is running. Your mod change applies when it ends.";
  else if (n == "online") reason = "You are in the online lobby. Your mod change applies when you leave it.";
  else if (hasBoard()) reason = "A level is being played. Your mod change applies when you are back at the main menu.";
  if (reason && why) *why = reason;
  return reason != nullptr;
}

void App::serviceModApply() {
  if (!modApplyPending_ || moving()) return;
  std::string why;
  if (modApplyBlocked(&why)) {
    modApplyStatus_ = why;
    return;
  }
  modApplyPending_ = false;
  runModApply();
}

void App::runModApply() {
  auto* mm = mods::g_mods;
  modApplyCount_++;
  if (!mm) {
    modApplyStatus_ = "No mod manager is running.";
    return;
  }
  mods::ReloadResult r = mm->reload(vfs);
  if (r.assets.any) refreshAssets(r.assets);
  modApplyStatus_ = "Applied: " + mm->statusText();
}

void App::refreshAssets(const VfsChange& change) {
  gfx.reloadAssets(change);
  if (boardRenderer) boardRenderer->reloadAssets(change);
  if (audio) audio->reloadAssets(change);
  StageMgr newStages;
  if (newStages.load(vfs)) {
    stages = std::move(newStages);
    PlayerInfo::advStages = stages.numStages() > 0 ? stages.numStages() : 11;
  } else {
    log::warn("game", "the level list could not be read after the mod change; keeping the old one");
  }
  TrophyMgr newTrophies;
  if (newTrophies.load(vfs)) trophies = std::move(newTrophies);
  else log::warn("game", "the trophy list could not be read after the mod change; keeping the old one");
  if (curStage >= stages.numAllStages()) {
    curStage = 0;
    curLevel = 0;
  }
  if (packStage >= stages.numAllStages()) packStage = -1;
  if (modAssetsHook) modAssetsHook();
  log::info("ui", "art, sounds and level lists reloaded: " + std::to_string(stages.numAllStages()) + " stage(s), " + std::to_string(stages.numAllLevels()) + " level(s)");
}

}
