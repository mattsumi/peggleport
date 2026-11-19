#include "ui/pad.h"

#include "gamefind.h"
#include "gamepaths.h"
#include "online/steam.h"

namespace pp::ui {

namespace fs = std::filesystem;

Pad padFromSdl(SDL_GamepadType type, Uint16 vendor, Uint16 product) {
  if (vendor == 0x28de && product == 0x1205) return Pad::SteamDeck;
  if (vendor == 0x28de && product != 0x11ff) return Pad::SteamController;
  switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return Pad::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return Pad::Nintendo;
    default: return Pad::Xbox;
  }
}

std::array<std::string, 4> steamGlyphFiles(const fs::path& steamRoot, Pad pad) {
  if (steamRoot.empty() || pad == Pad::Keys || pad == Pad::PlayStation) return {};
  const std::string style = pad == Pad::Xbox || pad == Pad::SteamController ? "shared_color_button_" : "shared_button_";
  const char* faces = pad == Pad::Nintendo ? "baxy" : "abyx";
  std::array<std::string, 4> art;
  for (int i = 0; i < 4; ++i) {
    const fs::path file = steamRoot / "controller_base" / "images" / "api" / "dark" / (style + faces[i] + "_md.png");
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return {};
    art[i] = pathToUtf8(file);
  }
  return art;
}

PadGlyphs readPad(SDL_Gamepad* gamepad) {
  PadGlyphs out{padFromSdl(SDL_GetGamepadType(gamepad), SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad)), {}};
  if (online::steam::padGlyphs(SDL_GetGamepadSteamHandle(gamepad), &out.art)) return out;
  static const fs::path steamRoot = [] {
    for (const auto& place : systemFindSources({}).steamRoots) {
      std::error_code ec;
      if (!place.path.empty() && fs::is_directory(place.path / "controller_base" / "images" / "api", ec)) return place.path;
    }
    return fs::path();
  }();
  out.art = steamGlyphFiles(steamRoot, out.pad);
  return out;
}

}
