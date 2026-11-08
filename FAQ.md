# FAQ

Longer guides: [Modding](faq/modding.md) · [Level editor](faq/editor.md) · [Online play and the lobby server](faq/online.md)

## The basics

**What is PegglePort?**
It is a program I wrote that runs Peggle Deluxe and Peggle Nights natively on Windows, macOS and Linux. It uses the data files of your own install, so the games look, sound and play like the originals, and it adds a few things on top (widescreen, a level editor, mods, online play).

**Do I need to own the games?**
Yes. PegglePort does not contain any of the game's code, art, music or levels, and the downloads on this page never will. Buy Peggle Deluxe and/or Peggle Nights from Steam, the EA app (Origin) or wherever you find them, install them, and point PegglePort at the install.

**Does it change my installed game?**
No. It only reads from the install. Profiles, high scores, settings, replays and mods are stored in a separate folder (see "Where are my saves?").

**Is this official?**
No. It is a fan project and has nothing to do with PopCap Games or Electronic Arts.

**Which systems does it run on?**
Windows, macOS and Linux.

## Starting the game

**What happens when I open it?**
A small window lists Peggle Deluxe and Peggle Nights. Each one shows the folder it found, or "Not found". Press **Play** on the one you want. The game opens in its own window with the normal loading screen and main menu.

**It says "Not found" but I have the game installed.**
It looks in the usual places for Steam (including extra Steam library folders), Origin, the EA app, the PopCap installer, and in a `Games` folder in your home folder. If your copy lives somewhere else, press **Locate...** and choose the folder that holds `main.pak`. You can also drag that folder onto the window. If you choose a folder that contains the game folders (for example `Games`), it picks up both games.
It is the folder with `main.pak` in it, not its parent and not `levelpacks`. The window tells you when a folder is wrong.

**Can I skip the first window?**
Tick **Open the last game directly next time**. To get back to the folder window later, use **Game Folders...** in the game's Options.

**Do Deluxe and Nights share profiles?**
No. Each game has its own profiles, high scores, settings and mods folder, the same way the originals do.

**Can I use my existing profiles?**
Yes. In the game, click **If this is not you, click here.** on the main menu and then **Import Player**. If you are starting for the first time, **Import Player** is on the certificate where you would type your name. A file chooser opens: choose the `userdata` folder of your original Peggle Deluxe or Peggle Nights, or one player file such as `user1.dat`. PegglePort lists the players it finds with their progress. Pick one and press **Import**.

You get a new player with your adventure progress, challenge trophies, cleared levels (and aced levels in Nights), adventure scores, the Masters you picked and the settings that are kept in the profile. The scores you put on the high score lists are added to the lists. If a player with the same name already exists, the imported one gets a number after its name. Nothing is ever overwritten and the original files are only read. If a file cannot be read, the window says so in plain words.

The original games keep their saves in `C:\ProgramData\PopCap Games\Peggle\userdata` (Deluxe) and `C:\ProgramData\PopCap Games\PeggleNights\userdata` (Nights) on Windows, or in a `userdata` folder next to the game's `.exe` on older Windows. If you run the originals through Wine, CrossOver or Proton, it is the same path inside the Wine folder (`drive_c/ProgramData/PopCap Games/...`), and the file chooser starts there when I find it. A player from the other game is added to that game's list, so start that game in PegglePort to find the player there.

## Where are my saves?

PegglePort stores everything in one folder per game. Nothing is written into the game's install.

| System | Peggle Deluxe | Peggle Nights |
|---|---|---|
| Windows | `%APPDATA%\PopCap Games\Peggle` | `%APPDATA%\PopCap Games\PeggleNights` |
| macOS | `~/Library/Application Support/PopCap Games/Peggle` | `~/Library/Application Support/PopCap Games/PeggleNights` |
| Linux | `~/.local/share/PopCap Games/Peggle` | `~/.local/share/PopCap Games/PeggleNights` |

Inside you find `settings.ini` and a `userdata` folder with profiles (`users.dat`, `user*.dat`), saved games, `highscores.dat`, `replays/`, `mods/` and `logs/`. The launcher remembers your game folders in a small `launcher.ini` of its own, in `%APPDATA%\PegglePort\PegglePort` on Windows, `~/Library/Application Support/PegglePort/PegglePort` on macOS and `~/.local/share/PegglePort/PegglePort` on Linux.

## Playing

**What are the controls?**
Move the mouse to aim, left click to fire. The mouse wheel nudges the aim in small steps. Hold the right mouse button to fast forward when you play alone. `G` shows or hides the aim guide, `Esc` pauses. A game controller also works: the stick or the D-pad aims, the A button fires.

**Is the gameplay the same as the original?**
That is the goal and I have put most of the work into it: the physics, scoring, powers and level behaviour are meant to match. If you ever see the ball bounce differently from the original, or a score that does not add up, please open an issue with the level and what you did.

## Display

**How do I turn on widescreen?**
Options, then **Widescreen**. The default is **Auto**, which makes the board as wide as your window needs it. You can also lock it to 16:9, 16:10 or 21:9, or go back to the classic 4:3 board. Menus stay centred over a blurred backdrop.
High scores set on a wide board go in the same list as the classic board. If you would rather keep them apart, open **Enhanced Visuals...** and tick **Separate Wide Scores**.

**What are Enhanced Visuals?**
Options, then **Enhanced Visuals...**. They are extras that do not change how the game plays: smoother slow motion, edge smoothing, bloom, sparks and a wider HUD for ultra wide windows. They are all off by default so the game looks like the original.

**Fullscreen?**
Tick **Fullscreen** in Options. The window also remembers its size.

**The window is black or flickers.**
That is usually the graphics driver, in a virtual machine or over remote desktop. Start the game with the environment variable `SDL_RENDER_DRIVER=software` and it draws on the CPU instead. On Linux you can also force the window system with `SDL_VIDEO_DRIVER=x11` or `wayland`.

## Sound and music

**I hear sound effects but no music.**
The music is read from the `music` folder of your Peggle Deluxe or Peggle Nights install, next to `main.pak`, so that folder has to be there. Then look at **Music Volume** in Options: all the way to the left is off. The fanfare at an Extreme Fever has its own slider, **Fever Volume**. If the folder is there and the music is still missing, the log (F10) names the file the game could not play.

**There is no sound at all.**
The game keeps running when it cannot open an audio device. Check the output device of your system. On Linux you can force a driver with `SDL_AUDIODRIVER=pulseaudio`, `alsa` or `pipewire`. The log (F10) says why the device could not be opened.

## The log

**Where can I see what the game is doing?**
Press **F10** on any screen, also while you play. A panel opens at the bottom with the live log of the game and of your mods. The Mods screen has a **Log** button for a larger view. You can filter by level and by source, copy the lines, clear them, or save them to a file in the `logs` folder. If you report a problem, attach a saved log.

## Mods

**How do I install a mod?**
Put the mod folder or `.zip` into the `mods` folder (the Mods screen has an **Open Folder** button for it), open **Options, then Mods** and tick the mod. It loads right away, the game does not need a restart. The guide in [faq/modding.md](faq/modding.md) covers writing your own mods with Lua or native plugins.

**Can a mod do something nasty?**
A mod can only touch the network if it says so in its manifest, and PegglePort scans every mod before it loads. If the scan finds network use, or the mod contains native code, the game shows you what it found and waits for your OK. This is a best effort check, not a guarantee, so only install mods from people you trust. Lua mods run in a sandbox without file or network access. Native plugins run in a separate helper process that the operating system locks down: no network, no writing files, no starting programs, and it can only read its own folder. If your system cannot apply all of that to a plugin, the game says so and the plugin stays off until you also allow it to run without restrictions.

**What is pp_modhost?**
The helper program for native mod plugins. It only runs while a native plugin is switched on and it ends when the game ends. Keep it next to the game.

**Why does the other player have to use the same mods online?**
Gameplay mods change the result of every shot, so everybody in a match needs the same ones. The game compares them when you join and tells you what is different.

## Level editor

**Is there a level editor?**
Yes, inside the game. Open **Options, then Level Editor**, or press **F9** on the main menu. It can open any level of the game, place pegs, bricks, polygons, lines, holes and movers, test play the level, and save it as a mod. See [faq/editor.md](faq/editor.md).

## Online play

**How do I play with a friend?**
Open **Online** on the main menu. Everybody enters the same lobby server address, one player creates a room and the others join it from the list, with the 5-letter code, or by pasting an invite link. See [faq/online.md](faq/online.md), which also explains how to run a lobby server yourself.

**Is there a public lobby server?**
You need the address of a lobby server somebody runs. `pp_lobbyd` in the download is the server, and the guide shows how to start it.

**What does "Test connection" do?**
It checks that the lobby answers over UDP and what kind of router you sit behind. UDP is what allows a direct link between players; if it does not work, the game uses the slower relay through the lobby automatically.

## Problems

**macOS says the app is damaged or from an unidentified developer.**
The app is not signed with a paid developer certificate. Right-click it and choose Open the first time. If macOS still refuses, run `xattr -dr com.apple.quarantine PegglePort.app` in Terminal once.

**Windows shows "Windows protected your PC".**
Same reason. Click "More info", then "Run anyway".

**The window is tiny or huge on my high resolution screen.**
The game draws at your screen's real resolution and scales its 800x600 picture to fit. Resize the window once and it remembers the size. On Linux with X11 you can set `SDL_VIDEO_X11_SCALING_FACTOR=1.5` or similar if the scale is off.

**The game closes on start or a level does not load.**
Open the game again, press F10 and look for red lines. The most common cause is a mod; disable it in Options, Mods. If that does not help, open an issue and attach the saved log.
