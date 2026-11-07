#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <filesystem>
#include <string>

#include "gamepaths.h"
#include "mods/manager.h"
#include "mods/overlay.h"
#include "online/invite.h"
#include "online/online_ui.h"
#include "profile.h"
#include "ui/launcher.h"
#include "ui/run.h"
#include "vfs.h"

namespace {

void showProblem(const std::string& text) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PegglePort", text.c_str(), nullptr); }

pp::ui::RunResult playGame(pp::Game game, const std::filesystem::path& dir, std::string& error) {
  pp::Vfs vfs;
  std::string reason;
  if (!vfs.mountGame(dir, &reason)) {
    error = std::string("The game files in ") + pp::pathToUtf8(dir) + " could not be loaded: " + reason;
    return pp::ui::RunResult::Failed;
  }
  pp::mods::ModManager mods;
  mods.setUserDir(pp::defaultUserDir(game));
  mods.discover();
  mods.activate(&vfs);
  pp::mods::installOverlay();
  pp::ui::RunOptions options;
  options.game = game;
  return pp::ui::runApp(options, vfs, error);
}

}

int main(int argc, char** argv) {
  std::string invite;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "+connect" && i + 1 < argc) {
      std::string link = argv[++i];
      if (pp::online::isInviteLike(link)) invite = link;
    } else if (pp::online::isInviteLike(arg)) {
      invite = arg;
    }
  }

  SDL_SetAppMetadata("PegglePort", nullptr, nullptr);
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    showProblem(std::string("PegglePort could not start: ") + SDL_GetError());
    return 1;
  }
  if (!invite.empty()) pp::ui::online::setPendingInvite(invite);

  int exitCode = 0;
  bool allowAutoStart = true;
  for (;;) {
    pp::ui::LauncherResult pick = pp::ui::runLauncher(allowAutoStart);
    allowAutoStart = false;
    if (!pick.error.empty()) {
      showProblem(pick.error);
      exitCode = 1;
      break;
    }
    if (!pick.play) break;
    std::string error;
    pp::ui::RunResult result = playGame(pick.game, pick.dir, error);
    if (result == pp::ui::RunResult::Failed) {
      showProblem(error);
      continue;
    }
    if (result != pp::ui::RunResult::SwitchGame) break;
  }
  SDL_Quit();
  return exitCode;
}
