# FAQ

Longer guides: [Modding](faq/modding.md) · [Level editor](faq/editor.md) · [Online play and the lobby server](faq/online.md)

## The basics

**What is PegglePort?**
It is a program I wrote that runs Peggle Deluxe and Peggle Nights natively on Windows, macOS and Linux. It uses the data files of your own install, so the games look, sound and play like the originals, and it adds a few things on top (widescreen, a level editor, mods, online play).

**Do I need to own the games?**
Yes. PegglePort does not contain any of the game's code, art, music or levels, and the downloads on this page never will. Buy Peggle Deluxe and/or Peggle Nights from Steam, the EA app (Origin) or wherever you find them, install them, and PegglePort finds them by itself.

**Does it change my installed game?**
No. It only reads from the install. Profiles, high scores, settings, replays and mods are stored in a separate folder (see "Where are my saves?").

**Is this official?**
No. It is a fan project and has nothing to do with PopCap Games or Electronic Arts.

**Which systems does it run on?**
Windows, macOS and Linux.

## Starting the game

**What happens when I open it?**
PegglePort looks for your Peggle Deluxe and Peggle Nights and opens the game right away, with the normal loading screen and main menu. There is no extra window in between. If you have one of the two games, that game opens and everything is the way you know it.

**I have both games. What do I get?**
One game. It opens with the Peggle Deluxe main menu, and both adventures are behind it. When you go from one game to the other the window stays as it is and there is no second loading screen: the next screen slides in the way screens always do.

**How do I play the other adventure?**
Press **Adventure** on the main menu. The game asks **Peggle Deluxe** or **Peggle Nights**. Pick one and you are in that adventure, with its own story, levels, Masters and music. Press `Esc` if you did not mean to press Adventure.

**How do I get back?**
The same way as always: **Main Menu** in the level's menu, on the screen after a level or on the Adventure screen. You always come back to the menu you started from.

**Can I open with the Peggle Nights menu instead?**
Yes. Open **Options** on the main menu and press the row **Main Menu: Peggle Deluxe**. It changes to **Main Menu: Peggle Nights**. Press **Done** and the Peggle Nights menu comes up, and it is the one that opens from now on. Press the row again to go back. The row is only there when both games are installed.

**What about Quick Play, Duel and Challenge?**
They list the levels of both games. The list starts with the levels of the menu that is open. Keep pressing the right arrow and the other game's pages follow, and every page shows the name of its game. A level or a challenge of the other game is played in that game, with its own rules, Masters and music, and its scores and progress are kept there. In Quick Play and Duel, a level that is still locked in its game is locked in the list too. **New Level** and **New Challenge** bring you back to the list you picked it from.

**What do Random and Play New Level pick from?**
From the unlocked levels of both games. The **Random** button on the Quick Play and Duel lists and **Play New Level** on the screen after a level can give you a level of the other game, and it opens there the same way a level you picked yourself does. Inside each game the usual rules stay: the level you just played does not come up again, **Play New Level** does not repeat one of the last ten, and in Duel the levels the games keep out of random picks stay out. A game you have not played yet only offers what is unlocked in it. The random levels inside a challenge always come from that challenge's own game.

**Why does the Challenge list show only one game?**
Because the other game's Challenge mode is still locked, and the originals never show a challenge you cannot play. Its pages are left out until you unlock it (Peggle Deluxe: finish the Adventure, Peggle Nights: finish Stage 3 of the Adventure), and the page numbers only count the pages you can see. The **Challenge** button on the main menu opens as soon as one of the two games has Challenge unlocked. If neither has, you get the usual message of the menu that is open.

**What is "Your Best" in the Challenge list?**
Point at a challenge and the panel in the middle shows its name, what it asks of you and, once you have a result for it, a line **Your Best:** with your highest number. Before that there is no line, the same as in the originals. What the number is depends on the challenge. In Peggle Deluxe only the challenges with a score to reach keep one, your highest total score. In Peggle Nights it is the score of the level, the style points for the style point challenges, the number of different style shots, or the number of required style shots you made, and for the "stay under" challenges the lowest score counts. Challenges with a set number of orange pegs, with every peg to clear, duels, and challenges whose score carries on over several levels keep no best.

**Where does it look for my game?**
The first time, and again whenever its own copy of the game files is missing or damaged, it looks first in the folders it remembers from last time. Then it asks Steam: on Windows it reads the Steam folder from the registry, and on every system it reads Steam's list of library folders and Steam's own records of Peggle Deluxe and Peggle Nights, so a library on another drive or a folder with an unusual name is found too. After that it tries the usual Steam folders on every drive, the Origin, EA app and PopCap folders, and a `Games` folder next to PegglePort or in your home folder. A folder only counts when `main.pak` is in it. Once it has found a game it keeps its own copy of the files it needs (about 40 MB for both games) in its own folder next to `launcher.ini` and plays from that copy, so after that it does not need Steam, and you can close Steam, go offline or uninstall it.

**It cannot find my game.**
Then a message tells you so and lists the places it looked at with what it saw there, for example "folder not there" or "folder there, but no main.pak in it". Press **Choose Folder...** and pick the folder that holds `main.pak`. The folder that contains your game folders works too (for example `Games`, it picks up both games), and so does a Steam folder. If the folder is wrong, the message says so and asks again. The complete list is written to `start.log`, next to `launcher.ini` (see "Where are my saves?").
On Linux the folder window needs a desktop portal or `zenity`. If neither is there, the message tells you to put your game folder into a folder named `Games` next to PegglePort.

**Where did it find my game?**
Press `F10` in the game. The log starts with every place it looked at and ends with the folder each game is read from.

**How do I change a game folder, or add the other game?**
Use **Game Folders...** in the game's Options. A box shows both folders and asks which game you mean, then the folder window opens. If the folder you pick is the wrong one, the game says so and lets you choose again. Options stays open behind all of this, so when you are done you are back where you were. If you change the folder of the game you are playing, it makes a new copy from that folder and opens again from it.

**Do Deluxe and Nights share profiles?**
You are the same player in both. When you go to the other game it picks the player with your name, and if there is none yet it makes one for you without asking for your name again. Your progress is kept for each game on its own, so your Deluxe adventure and your Nights adventure do not mix. High scores, replays and mods are kept for each game too, the same way the originals do.

**Do I have to set my options twice?**
No. Sound and music volume, fullscreen, widescreen, custom cursors, vertical sync, the enhanced visuals and colorblind mode are the same in both games. Change them in either one.

**Can I use my existing profiles?**
Yes. In the game, click **If this is not you, click here.** on the main menu and then **Import Player**. If you are starting for the first time, **Import Player** is on the certificate where you would type your name. A file chooser opens: choose the `userdata` folder of your original Peggle Deluxe or Peggle Nights, or one player file such as `user1.dat`. PegglePort lists the players it finds with their progress. Pick one and press **Import**.

You get a new player with your adventure progress, challenge trophies and your best result in each challenge, cleared levels (and aced levels in Nights), adventure scores, the Masters you picked and the settings that are kept in the profile. The scores you put on the high score lists are added to the lists. If a player with the same name already exists, the imported one gets a number after its name. Nothing is ever overwritten and the original files are only read. If a file cannot be read, the window says so in plain words.

The original games keep their saves in `C:\ProgramData\PopCap Games\Peggle\userdata` (Deluxe) and `C:\ProgramData\PopCap Games\PeggleNights\userdata` (Nights) on Windows, or in a `userdata` folder next to the game's `.exe` on older Windows. The Steam versions use `C:\ProgramData\Steam\Peggle\userdata` and `C:\ProgramData\Steam\PeggleNights\userdata` instead. If you run the originals through Wine, CrossOver or Proton, it is the same path inside the Wine folder (`drive_c/ProgramData/PopCap Games/...` or `drive_c/ProgramData/Steam/...`), and the file chooser starts there when I find it. A player from the other game is added to that game's list. Change the **Main Menu** row in Options to that game and pick the player there with **If this is not you, click here.**

## Where are my saves?

PegglePort stores everything in one folder per game. Nothing is written into the game's install.

| System | Peggle Deluxe | Peggle Nights |
|---|---|---|
| Windows | `%APPDATA%\PopCap Games\Peggle` | `%APPDATA%\PopCap Games\PeggleNights` |
| macOS | `~/Library/Application Support/PopCap Games/Peggle` | `~/Library/Application Support/PopCap Games/PeggleNights` |
| Linux | `~/.local/share/PopCap Games/Peggle` | `~/.local/share/PopCap Games/PeggleNights` |

Inside you find `settings.ini` and a `userdata` folder with profiles (`users.dat`, `user*.dat`), saved games, `highscores.dat`, `replays/`, `mods/` and `logs/`. PegglePort remembers your game folders in a small `launcher.ini` of its own, in `%APPDATA%\PegglePort\PegglePort` on Windows, `~/Library/Application Support/PegglePort/PegglePort` on macOS and `~/.local/share/PegglePort/PegglePort` on Linux. The copy of the game files is in a `gamefiles` folder next to it.

## Playing

**What are the controls?**
Move the mouse to aim, left click to fire. The mouse wheel nudges the aim in small steps. Hold the right mouse button to fast forward when you play alone. `G` shows or hides the aim guide, `Esc` pauses. A game controller also works: the stick or the D-pad aims, the A button fires.

**What happens when I leave a level in the middle?**
It is kept for you, the way the original does it, in Adventure, Quick Play, Duel and Challenge. Fire a shot, open the level's menu and press **Main Menu**: the game asks **Leave Game?** and tells you the session will be saved. Press **Leave** and the level waits for you exactly as it was, with the pegs, your score, the balls, the ball in the air and your Master's power. It only asks the first three times for each player. After that it saves and leaves without a question. Closing the window in the middle of a level keeps it too. In Quick Play a level is only kept once you have scored something, in Duel once either player has, and in a Challenge once you have scored or finished a level of it.
A Challenge with several levels is also kept between its levels. If you leave from the end of a level, the end screen is waiting for you the next time, and the score, the balls and the lives you carry into the next level are still there. In a Duel against the computer, the computer's turn is kept as well: if it was in the middle of a shot, it goes on with it when you continue.
The next time you press **Adventure** (and pick the game, if you have both), **Quick Play**, **Duel** or **Challenge**, **Continue Game?** shows what you had: your score and the balls you have left in Adventure, the level's name and your score in Quick Play, both players' points in Duel, and the name of the challenge, the level you are on and your score in a Challenge. **Continue** goes on right where you stopped, and the level is running at once. **Restart Level** (**New Game** in the other modes) throws the kept level away and starts again; in Adventure that counts as another try. **Cancel** leaves everything as it is. In Peggle Nights, once you have finished the adventure, there is also **New Game**, which asks if you want to start the whole adventure over. Every player has one kept level for each game and each mode.

**Is the gameplay the same as the original?**
That is the goal and I have put most of the work into it: the physics, scoring, powers and level behaviour are meant to match. If you ever see the ball bounce differently from the original, or a score that does not add up, please open an issue with the level and what you did.

## Display

**How do I turn on widescreen?**
Options, then **Widescreen**. The default is **Auto**, which makes the picture as wide as your window. You can also lock it to 16:9, 16:10 or 21:9, or go back to the classic 4:3 picture. The board keeps the width it has in the original and stands in the middle, and the room at the sides is dimmed, so levels, scores, replays and high scores are exactly the same as in the original. Menus stay centred over a blurred backdrop.

**What are Enhanced Visuals?**
Options, then **Enhanced Visuals...**. They are extras that do not change how the game plays: smoother slow motion, edge smoothing, bloom, sparks and a frame that stretches to the corners of any wide window. They are all off by default so the game looks like the original.

**Fullscreen?**
Tick **Fullscreen** in Options. The window also remembers its size.

**The window is black or flickers.**
That is usually the graphics driver, in a virtual machine or over remote desktop. Start the game with the environment variable `SDL_RENDER_DRIVER=software` and it draws on the CPU instead. On Linux you can also force the window system with `SDL_VIDEO_DRIVER=x11` or `wayland`.

## Sound and music

**I hear sound effects but no music.**
The music comes from the `music` folder of your Peggle Deluxe or Peggle Nights install, next to `main.pak`, which PegglePort copies when it first finds the game, so that folder has to be there at that time. Then look at **Music Volume** in Options: all the way to the left is off. The fanfare at an Extreme Fever has its own slider, **Fever Volume**. If the folder is there and the music is still missing, the log (F10) names the file the game could not play.

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
Open **Online** on the main menu. Everybody enters the same lobby server address, one player creates a room and the others join it from the list, with the 5-character code, or by pasting an invite link. See [faq/online.md](faq/online.md), which also explains how to run a lobby server yourself.

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
