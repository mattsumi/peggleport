# PegglePort

PegglePort is an unofficial, open source engine that runs **Peggle Deluxe** and **Peggle Nights** on Windows, macOS and Linux. It is written in C++20 with SDL3.

You need your own copy of the games. PegglePort reads the files of your install (`main.pak` and the level packs) when it starts and never copies, changes or ships any of them. There is no PopCap or EA code or art in this repository.

PegglePort is a fan project. It has nothing to do with PopCap Games or Electronic Arts, and "Peggle" and "PopCap" belong to their owners. The libraries it uses are listed in [THIRD_PARTY.md](THIRD_PARTY.md).

## What you get

* Both games with their menus, adventure, quick play, challenges, duels, trophies, profiles and high scores.
* Real widescreen. The board gets wider on 16:9, 16:10 and 21:9 windows instead of showing black bars. The classic 4:3 board is still there.
* Optional nicer visuals (smoother slow motion, bloom, sparks). They are off by default so the game looks like the original.
* A level editor inside the game.
* Mods: replace art, sound and levels, or write Lua scripts and native plugins. A mod that wants the network has to ask you first.
* Online duels and Peg Party for up to four players, with invite links.
* Everything runs from a window. Nothing needs a terminal.

## Getting started

1. Download the archive for your system from the Releases page and unpack it anywhere.
2. Start `peggleport.exe` (Windows), `PegglePort.app` (macOS) or `peggleport` (Linux).
3. The first window asks where Peggle Deluxe and Peggle Nights are installed. It finds Steam, Origin, EA app and PopCap installs by itself. If it misses yours, press **Locate...** and pick the folder that contains `main.pak`, or drag the folder onto the window.
4. Press **Play**.

The FAQ has answers about settings, saves, mods, the editor, online play and problems: [FAQ.md](FAQ.md).

## Building from source

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

The program ends up in `build/peggleport` (`build\Release\peggleport.exe` with Visual Studio). `pp_lobbyd` is the lobby server for online play and is explained in the FAQ.

## License

PegglePort is released under the MIT license, see `LICENSE`. The libraries it builds on keep their own licenses, listed in `THIRD_PARTY.md`. It contains no Peggle files, you need your own copy of the game.
