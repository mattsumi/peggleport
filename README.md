<p align="center">
    <img src="packaging/peggleport.png" width="160" alt="PegglePort"/>
</p>

<h1 align="center">PegglePort</h1>

<p align="center">
    <a href="https://github.com/mattsumi/peggleport/releases"><b>Download</b></a>
    ·
    <a href="#minimum-system-requirements"><b>Requirements</b></a>
    ·
    <a href="#installing"><b>Installing</b></a>
    ·
    <a href="#features"><b>Features</b></a>
    ·
    <a href="#known-issues"><b>Known issues</b></a>
    ·
    <a href="#controls"><b>Controls</b></a>
    ·
    <a href="#faq"><b>FAQ</b></a>
    ·
    <a href="#building"><b>Building</b></a>
    ·
    <a href="#credits"><b>Credits</b></a>
</p>

---

PegglePort is an unofficial port of Peggle Deluxe and Peggle Nights, written in C++20 with SDL3.
The port supports Windows, macOS and Linux.

**This project does not include any game assets. You must provide the files from your own legally acquired copy of Peggle Deluxe, Peggle Nights or both to play.**

PegglePort reads the data files of your own install, so the games look, sound and play like the originals. It never changes that install. It is a fan project and has nothing to do with PopCap Games or Electronic Arts; "Peggle" and "PopCap" belong to their owners.

## Minimum system requirements

- Your own copy of Peggle Deluxe, Peggle Nights or both (Steam, the EA app, Origin or a PopCap install)
- Operating system:
    - Windows 10 or newer, 64-bit
    - macOS 12 or newer, Apple silicon or Intel
    - A modern Linux distribution with X11 or Wayland
- Graphics: anything SDL3 can draw on. There is a software renderer for machines without a working driver.
- Storage:
    - About 50 MB for the program and its copy of both games' files

## Installing

1. Download the archive for your system from the [Releases][releases] page and unpack it anywhere. On macOS you can also open the disk image and drag PegglePort to Applications.
2. Start `peggleport.exe` (Windows), `PegglePort.app` (macOS) or `peggleport` (Linux).
3. PegglePort looks for Peggle Deluxe and Peggle Nights by itself and opens straight into the game.
4. If it finds no game, a message shows where it looked, and a folder window lets you pick the folder that contains `main.pak`. It remembers the folder.

There is no installer and nothing else to install. Everything runs from a window.

## Features

### Both games in one

With both games installed you get one game. It opens with the Peggle Deluxe main menu, **Adventure** asks which of the two adventures you want, and Quick Play, Duel and Challenge list the levels of both games. Every level plays in its own game, with its own rules, Masters and music. The row **Main Menu** in Options opens with the Peggle Nights menu instead.

### Finds your games and keeps its own copy

PegglePort finds Steam, Origin, EA app and PopCap installs, including Steam libraries on other drives. Once it has found a game it copies the files it needs (`main.pak`, the music, the level packs) into its own folder and plays from that copy. After that it does not need Steam or the original install.

### Widescreen and ultrawide support

On 16:9, 16:10 and 21:9 windows the picture fills the window instead of showing black bars. The board keeps its original width in the middle, so every level plays exactly as in the original. The classic 4:3 picture is still there.

### Enhanced visuals

Smooth motion between the game's 10 ms steps, smooth scaling, bloom glow, peg sparks and a frame that stretches over an ultrawide window. They change nothing about how the game plays, and they are all off by default so the game looks like the original.

### Saves and profiles

Leave a level in the middle and it waits for you exactly as it was, in Adventure, Quick Play, Duel and Challenge. **Import Player** brings over a player from the original games with adventure progress, trophies and scores. The original files are only read.

### Level editor

A level editor inside the game. It opens any level, places pegs, bricks, polygons, holes and movers, test plays the result and saves it as a mod. See the [level editor guide](faq/editor.md).

### Mod support

Mods replace art, sound and levels, or add behaviour with Lua scripts and native plugins. They load from the Mods screen without a restart. See the [modding guide](faq/modding.md).

> [!NOTE]
> PegglePort scans every mod before it loads. A mod that uses the network or contains native code is shown to you first and waits for your OK. Lua mods run in a sandbox, and native plugins run in a separate locked down helper process. The scan is a best effort check, so only install mods from people you trust.

### Online play

Duels for two players and Peg Party for up to four. A public lobby is filled in when you open **Online**, so one player creates a room and the others join from the list, with a 5-character code or with an invite link. You can also run your own lobby with `pp_lobbyd`. See the [online guide](faq/online.md).

### Discord and Steam

Both are optional. **Discord activity** on the Online screen shares what you are playing and lets a friend join an open room. It is off by default. Builds made with the Steamworks SDK add Steam presence, friend invitations, lobbies and peer networking.

> [!NOTE]
> Steam builds use the development App ID 480, so Steam may show the game as Spacewar. Single player and the public lobby work without Steam and without Discord.

## Known issues

Before reporting an issue, check if it is listed [here][issues].

## Controls

| Input | Action |
| --- | --- |
| Mouse | Aim |
| Left click or Enter | Fire |
| Mouse wheel, Left and Right | Nudge the aim in small steps |
| Up and Down (hold) | Move the aim |
| Right mouse button (hold) | Fast forward when you play alone |
| G | Show or hide the aim guide |
| Esc | Pause |

A game controller works too.

| Controller | Action |
| --- | --- |
| Left stick, D-pad left and right | Move the aim |
| D-pad up and down | Nudge the aim in small steps |
| A | Fire |
| Right bumper or right trigger (hold) | Fast forward |
| B or Start | Pause |

| Hotkey | Action |
| --- | --- |
| F9 | Open the level editor from the main menu |
| F10 | Show or hide the log |
| Ctrl+V | Paste an invite link on the Online screen (Cmd+V on macOS) |

## FAQ

### Do you have a website?

PegglePort does not have a website. Builds are published on the [Releases][releases] page of this repository.

**Please link here when directing anyone to the project.**

> [!CAUTION]
> Do not download builds of PegglePort from anywhere but the [Releases][releases] page.

### Which versions of the games can I use?

The PC versions of Peggle Deluxe and Peggle Nights. A folder only counts when `main.pak` is in it. PegglePort looks for Steam, EA app, Origin and PopCap installs, and one game is enough.

### Do I still need Steam or the original install afterwards?

No. Once PegglePort has made its own copy of the files you can close Steam, go offline or uninstall the game. If the copy is ever missing or damaged, PegglePort collects it again from the install.

### Where are save data and settings?

| System | Peggle Deluxe | Peggle Nights |
| --- | --- | --- |
| Windows | `%APPDATA%\PopCap Games\Peggle` | `%APPDATA%\PopCap Games\PeggleNights` |
| macOS | `~/Library/Application Support/PopCap Games/Peggle` | `~/Library/Application Support/PopCap Games/PeggleNights` |
| Linux | `~/.local/share/PopCap Games/Peggle` | `~/.local/share/PopCap Games/PeggleNights` |

Inside, settings are in `settings.ini` and everything else is in `userdata/`: profiles, saved games, high scores, replays, mods and logs. Nothing is written into the game's install.

### How do I update?

Unpack a new [release][releases] over your existing folder, or replace the app on macOS.

- Saves and settings are kept
- The copy of the game files is kept, so the game opens right away

### Can I use my saves from the original games?

Yes. Click **If this is not you, click here.** on the main menu and then **Import Player**, and pick the `userdata` folder of your original game. You get a new player with your progress. Nothing is overwritten.

### Is the gameplay the same as the original?

That is the goal and I have put most of the work into it: the physics, scoring, powers and level behaviour are meant to match. If the ball ever bounces differently from the original, or a score does not add up, please open an issue with the level and what you did.

### Why does Steam show Spacewar?

Steam builds use App ID 480, the ID of Valve's Spacewar example that development builds run under. This is not a separate Steam store release.

### Where is everything else?

In [FAQ.md](FAQ.md): starting the game, display, sound, the log, mods, the editor, online play and problems.

## Building

You need CMake 3.25 or newer and a C++20 compiler.

```sh
cmake -S . -B build -DPP_FETCH_DEPS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

`-DPP_FETCH_DEPS=ON` downloads and builds SDL3, OpenJPEG, Ogg and Vorbis. Without it the build uses the copies installed on your system. Lua, Mbed TLS and libopenmpt are always downloaded. libopenmpt is built into the game, so the music of your Peggle installs plays without any extra library.

On Linux install the SDL build dependencies first:

```sh
sudo apt-get install build-essential cmake pkg-config libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev libdrm-dev libgbm-dev \
  libgl1-mesa-dev libegl1-mesa-dev libdbus-1-dev libudev-dev
```

On Windows use Visual Studio 2022 with the C++ workload and run the commands in a Developer PowerShell. On macOS run `xcode-select --install` and install CMake with Homebrew.

The program ends up in `build/peggleport` (`build\Release\peggleport.exe` with Visual Studio). `pp_lobbyd` is the lobby server for online play. Steam support is off unless you build with the Steamworks SDK; the [online guide](faq/online.md) has the flags.

## Credits

### PegglePort

- [mattsumi](https://github.com/mattsumi): creator and developer.

### Special thanks

- PopCap Games: made Peggle Deluxe and Peggle Nights.
- [SDL](https://github.com/libsdl-org/SDL): window, renderer, input and audio on every system.
- [libopenmpt](https://lib.openmpt.org/): plays the music of the original games.
- [OpenJPEG](https://github.com/uclouvain/openjpeg), [libogg](https://github.com/xiph/ogg), [libvorbis](https://github.com/xiph/vorbis) and [stb](https://github.com/nothings/stb): read the images and sounds in the game data.
- [Lua](https://github.com/lua/lua): mod scripting.
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls): secure connections.

Versions and licences are in [THIRD_PARTY.md](THIRD_PARTY.md).

## License

PegglePort is released under the MIT license, see `LICENSE`. The libraries it builds on keep their own licenses, listed in `THIRD_PARTY.md`. It contains no Peggle files, you need your own copy of the game.

[releases]: https://github.com/mattsumi/peggleport/releases
[issues]: https://github.com/mattsumi/peggleport/issues
