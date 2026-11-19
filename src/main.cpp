#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "gamefind.h"
#include "gamepaths.h"
#include "log.h"
#include "mods/manager.h"
#include "mods/overlay.h"
#include "online/invite.h"
#include "online/online_ui.h"
#include "profile.h"
#include "ui/run.h"
#include "ui/start.h"
#include "vfs.h"

namespace {

void showProblem(const std::string& text) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PegglePort", text.c_str(), nullptr); }

pp::ui::RunResult playGame(const pp::ui::GameRequest& request, pp::Vfs& vfs, pp::ui::GameFolders& folders, pp::ui::GameWindow& window, pp::ui::GameRequest& next, std::string& error) {
  pp::mods::ModManager mods;
  mods.setUserDir(pp::defaultUserDir(request.game));
  mods.discover();
  mods.activate(&vfs);
  pp::mods::installOverlay();
  pp::ui::RunOptions options;
  options.game = request.game;
  options.start = request.start;
  options.canOpen = [&](pp::Game g) {
    folders.setWindow(window.window());
    return folders.readyFor(g);
  };
  options.chooseFolders = [&](pp::Game running) {
    folders.setWindow(window.window());
    return folders.chooseFolders(running);
  };
  options.hasGame = [&](pp::Game g) { return folders.has(g); };
  options.gameDir = [&](pp::Game g) { return folders.dir(g); };
  options.home = [&] { return folders.home(); };
  options.setHome = [&](pp::Game g) { folders.setHome(g); };
  options.setGame = [&](pp::ui::App* app, pp::ui::GameWait wait) { folders.setGame(app, std::move(wait)); };
  return pp::ui::runApp(options, vfs, window, next, error);
}

}

int main(int argc, char** argv) {
  std::string invite;
  std::optional<pp::Game> edition;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--game") {
      if (i + 1 >= argc) return 2;
      const std::string key = argv[++i];
      if (key == "deluxe") edition = pp::Game::Deluxe;
      else if (key == "nights") edition = pp::Game::Nights;
      else if (key == "extreme") edition = pp::Game::Extreme;
      else return 2;
    } else if (arg == "+connect" && i + 1 < argc) {
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
  {
    pp::ui::GameFolders folders(
        pp::ui::GameFolders::defaultStoreFile(), [] { return pp::systemFindSources(pp::ui::GameFolders::programDir()); }, pp::ui::systemDialogs());
    folders.detect();
    pp::ui::GameRequest request;
    bool play;
    if (edition) {
      request.game = *edition;
      play = folders.has(*edition) || folders.askForGame(*edition);
      if (!play) exitCode = 1;
    } else {
      play = folders.gameToOpen(request.game) || (folders.askForAnyGame() && folders.gameToOpen(request.game));
    }
    pp::ui::GameWindow window;
    const auto base = pp::ui::GameFolders::programDir();
    while (play) {
      folders.setWindow(window.window());
      const std::filesystem::path dir = folders.dir(request.game);
      std::shared_ptr<pp::Vfs> vfs = std::make_shared<pp::Vfs>();
      std::string reason;
      if (!vfs->mountGame(dir, &reason)) {
        window.dropLeftGame();
        if (folders.copyFailed(request.game)) continue;
        std::string problem = std::string("The game files in ") + pp::pathToUtf8(dir) + " could not be loaded: " + reason;
        if (folders.recoverLaunch(request.game, edition.has_value(), problem)) {
          request.start = {};
          continue;
        }
        exitCode = 1;
        break;
      }
      if (edition && pp::combinedEdition(*edition)) folders.setHome(*edition);
      if (!base.empty()) vfs->mountPresentation(base / "presentation");
      pp::log::info("start", std::string("Opening ") + pp::gameTitle(request.game) + " from " + pp::pathToUtf8(dir));
      std::string error;
      pp::ui::GameRequest next;
      pp::ui::RunResult result = playGame(request, *vfs, folders, window, next, error);
      if (result == pp::ui::RunResult::Failed) {
        showProblem(error);
        exitCode = 1;
        break;
      }
      if (result != pp::ui::RunResult::SwitchGame) break;
      window.keepFiles(vfs);
      edition.reset();
      request = next;
    }
  }
  SDL_Quit();
  return exitCode;
}
