#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <string>

namespace pp::ui {

enum class Pad { Keys, PlayStation, Xbox, SteamDeck, SteamController, Nintendo };

struct PadGlyphs {
  Pad pad = Pad::Keys;
  std::array<std::string, 4> art;
};

Pad padFromSdl(SDL_GamepadType type, Uint16 vendor, Uint16 product);
std::array<std::string, 4> steamGlyphFiles(const std::filesystem::path& steamRoot, Pad pad);
PadGlyphs readPad(SDL_Gamepad* gamepad);

}
