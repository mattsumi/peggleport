#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "gamepaths.h"
#include "ui/pad.h"

using namespace pp::ui;
using pp::pathToUtf8;
namespace fs = std::filesystem;

static bool fail(const std::string& what) {
  std::cerr << what << "\n";
  return false;
}

static bool padTypes() {
  struct Case { SDL_GamepadType type; Uint16 vendor, product; Pad pad; const char* name; };
  const Case cases[] = {
      {SDL_GAMEPAD_TYPE_XBOXONE, 0x045e, 0x0b12, Pad::Xbox, "Xbox Series controller"},
      {SDL_GAMEPAD_TYPE_XBOX360, 0x045e, 0x028e, Pad::Xbox, "Xbox 360 controller"},
      {SDL_GAMEPAD_TYPE_XBOX360, 0x28de, 0x11ff, Pad::Xbox, "Steam virtual gamepad"},
      {SDL_GAMEPAD_TYPE_PS3, 0x054c, 0x0268, Pad::PlayStation, "DualShock 3"},
      {SDL_GAMEPAD_TYPE_PS4, 0x054c, 0x09cc, Pad::PlayStation, "DualShock 4"},
      {SDL_GAMEPAD_TYPE_PS5, 0x054c, 0x0ce6, Pad::PlayStation, "DualSense"},
      {SDL_GAMEPAD_TYPE_STANDARD, 0x28de, 0x1205, Pad::SteamDeck, "Steam Deck"},
      {SDL_GAMEPAD_TYPE_STANDARD, 0x28de, 0x1102, Pad::SteamController, "wired Steam Controller"},
      {SDL_GAMEPAD_TYPE_STANDARD, 0x28de, 0x1142, Pad::SteamController, "wireless Steam Controller"},
      {SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2009, Pad::Nintendo, "Switch Pro controller"},
      {SDL_GAMEPAD_TYPE_STANDARD, 0x2dc8, 0x3106, Pad::Xbox, "generic gamepad"},
  };
  bool ok = true;
  for (const auto& c : cases)
    if (padFromSdl(c.type, c.vendor, c.product) != c.pad) ok = fail(std::string(c.name) + " was not recognised");
  return ok;
}

static bool glyphFiles(const fs::path& root) {
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path dark = root / "controller_base" / "images" / "api" / "dark";
  fs::create_directories(dark);
  for (const char* style : {"shared_button_", "shared_color_button_"})
    for (const char* face : {"a", "b", "x", "y"}) std::ofstream(dark / (std::string(style) + face + "_md.png")) << "png";
  const auto file = [&](const char* name) { return pathToUtf8(dark / (std::string(name) + "_md.png")); };
  struct Case { Pad pad; std::array<std::string, 4> art; const char* name; };
  const Case cases[] = {
      {Pad::Xbox, {file("shared_color_button_a"), file("shared_color_button_b"), file("shared_color_button_y"), file("shared_color_button_x")}, "Xbox"},
      {Pad::SteamController, {file("shared_color_button_a"), file("shared_color_button_b"), file("shared_color_button_y"), file("shared_color_button_x")}, "Steam Controller"},
      {Pad::SteamDeck, {file("shared_button_a"), file("shared_button_b"), file("shared_button_y"), file("shared_button_x")}, "Steam Deck"},
      {Pad::Nintendo, {file("shared_button_b"), file("shared_button_a"), file("shared_button_x"), file("shared_button_y")}, "Nintendo"},
      {Pad::PlayStation, {}, "PlayStation"},
      {Pad::Keys, {}, "keyboard"},
  };
  bool ok = true;
  for (const auto& c : cases)
    if (steamGlyphFiles(root, c.pad) != c.art) ok = fail(std::string(c.name) + " glyphs did not come from the Steam glyph folder");
  fs::remove(dark / "shared_color_button_y_md.png");
  if (steamGlyphFiles(root, Pad::Xbox) != std::array<std::string, 4>{}) ok = fail("a partial Steam glyph set was used");
  if (steamGlyphFiles(root / "missing", Pad::SteamDeck) != std::array<std::string, 4>{}) ok = fail("glyphs were found without a Steam folder");
  fs::remove_all(root, ec);
  return ok;
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const bool types = padTypes();
  const bool files = glyphFiles(argv[1]);
  return types && files ? 0 : 1;
}
