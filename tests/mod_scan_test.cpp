#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include "mods/manager.h"
#include "ui/consent_dialog.h"

using namespace pp;
using namespace pp::ui;

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  const char* tmp = std::getenv("TMPDIR");
  if (!tmp || !*tmp || !SDL_Init(0)) return 2;
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const char* name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  mods::ModManager manager;
  manager.setUserDir(std::filesystem::path(tmp) / "peggle-mod-scan-test");
  mods::g_mods = &manager;
  auto info = std::make_unique<mods::ModInfo>();
  info->m.id = "scan-test";
  info->m.name = "Scan Test";
  info->m.version = "1";
  auto* mod = info.get();
  manager.mods().push_back(std::move(info));
  for (int arg = 1; arg < argc; ++arg) {
    Vfs vfs;
    std::string error;
    if (!vfs.mountGame(argv[arg], &error)) { std::cerr << error << '\n'; return 2; }
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    if (!surface || !renderer) return 2;
    Gfx gfx(renderer.get(), vfs);
    App::Config cfg;
    cfg.game = cfg.home = arg == 1 ? Game::Deluxe : Game::Nights;
    cfg.userDir = std::filesystem::path(tmp) / "peggle-mod-scan-test";
    cfg.noSave = cfg.noAudio = true;
    App app(gfx, vfs, nullptr, nullptr, cfg);
    mod->scan = {};
    mod->m.native.clear();
    {
      ConsentDialog dialog(app, mod->m.id, false);
      check(dialog.body.before.size() == 1 && dialog.body.before.front() == "This mod has nothing that needs your OK.", "clean scan has exact concise message");
      check(dialog.body.findings.empty() && dialog.body.after.empty(), "clean scan has no generic explanation");
      check(dialog.preferredHeight(520) < 250, "clean scan uses a compact dialog");
      check(!dialog.ask && dialog.step == 0, "clean scan does not ask for consent");
    }
    mod->scan.declaredNet = true;
    mod->scan.findings.push_back({mods::Finding::NetApi, "main.lua", 12, "net.request"});
    {
      ConsentDialog dialog(app, mod->m.id, false);
      check(dialog.body.before.front() == "This mod asks for the network permission.", "network request is explicit");
      check(dialog.body.findings.size() == 1 && dialog.body.findings.front() == "main.lua:12: net.request", "network finding retains file and line");
      check(dialog.body.after.empty(), "network scan omits generic explanation");
      check(mod->needsConsent() && !mod->consented && !mod->enabled, "viewing network scan grants no permission");
    }
    {
      ConsentDialog dialog(app, mod->m.id, true);
      check(dialog.ask && dialog.step == 1, "network consent is still required");
      check(!dialog.body.after.empty() && dialog.body.after.back().find("Allow only mods you trust.") != std::string::npos, "permission prompt retains concise caution");
      check(dialog.yesBtn && dialog.noBtn, "permission prompt retains accept and decline");
    }
    mod->scan.declaredNet = false;
    {
      auto body = ConsentDialog::describe(mod->m.id, 0);
      check(body.before.front() == "This mod contains code that looks like network access.", "undeclared network code is not described as safe");
      check(body.findings.size() == 1 && body.after.empty(), "undeclared network finding is retained without generic text");
    }
    mod->scan = {};
    mod->scan.playerControl = true;
    mod->scan.findings.push_back({mods::Finding::Control, "mod manifest", 0, "declares permissions = control"});
    {
      ConsentDialog dialog(app, mod->m.id, true);
      check(dialog.ask && dialog.step == 1, "player control always asks for consent");
      check(dialog.body.before.front() == "This mod asks to control your player.", "control permission is explicit without claiming network access");
      check(dialog.body.findings.size() == 1, "control finding is retained");
      check(!dialog.body.after.empty() && dialog.body.after.front().find("F8") != std::string::npos, "control consent explains the local approval and stop key");
    }
    mod->scan = {};
    mod->scan.hasNative = true;
    mod->scan.findings.push_back({mods::Finding::Native, "plugin.dll", 0, "native plugin"});
    {
      auto body = ConsentDialog::describe(mod->m.id, 0);
      check(body.before.front() == "This mod ships native code.", "native code risk stays explicit");
      check(body.findings.size() == 1 && body.findings.front() == "plugin.dll: native plugin", "native finding is retained");
      check(body.after.size() == 1, "native scan retains only relevant helper restrictions");
      check(mod->needsConsent() && !mod->consented && !mod->enabled, "viewing native scan grants no permission");
      auto unrestricted = ConsentDialog::describe(mod->m.id, 2);
      check(unrestricted.before.size() == 3 && unrestricted.before[1].find("stays switched off unless you allow") != std::string::npos, "unrestricted execution warning remains intact");
    }
  }
  mods::g_mods = nullptr;
  std::cout << "MOD_SCAN checks=" << checks << " failures=" << failures << '\n';
  SDL_Quit();
  return failures ? 1 : 0;
}
