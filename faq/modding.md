# Modding guide

PegglePort has three kinds of mods. They all live in the same place and go through the same permission check.

| Kind | What it is | Where it runs | Asks for permission |
|---|---|---|---|
| Data | Replaces art, sounds and levels, tweaks a few rules with an ini file | it is not code | no |
| Lua | Scripts with hooks into the game, drawing on top of the board, menu buttons, custom powers | sandbox | only when the scan finds network use |
| Native | A shared library (`.dll`, `.so`, `.dylib`) written against the C header `include/peggleport_mod.h` | separate restricted helper process | always |

The download has working examples in `examples/mods` (`hello_lua`, `rainbow_pegs`, `trace_hooks`, and a prebuilt `hello_native` when your platform has one) and the source of the native example in `examples/plugin`.

## Installing and turning mods on

1. Put the mod folder, or the `.zip`, into the `mods` folder of the game. The **Mods** screen has an **Open Folder** button that opens it. The folder sits next to your profiles: `<save folder>/userdata/mods` (see the FAQ for the save folders).
   A zip may hold the mod at its top level or inside one folder. It is unpacked into `mods/.cache` the first time.
2. Open **Options, then Mods**. Every mod is listed with a tick box, its state, the result of the scan and a **Scan report** or **Allow...** button. Tick the mods you want. Up and Down change the load order.
3. Restart the game. Changes to the list, the order and the permissions apply on the next start, because the files are mounted once at start-up.

The choices are saved in `userdata/mods.json`.

### Watching the log

Press **F10** on any screen to open the live log, also in the middle of a level. Everything your Lua `log()` calls print, every error from a hook, every load message and every scan finding shows up there as it happens. The Mods screen has a **Log** button that opens the same view larger. You can filter by level and by mod, copy lines, clear the view and save the whole log to a file.

## The manifest

Every mod has a `mod.ini` (or a `mod.json` with the same keys, where `permissions` and `scripts` are arrays) in its top folder.

```ini
name = Hello Lua
id = hello_lua
version = 1.0
author = you
description = One line that the Mods screen shows
api = 1
permissions = net, fs
scripts = main.lua, util.lua
native = hello_native
determinism = sim
memory_mb = 16
instruction_budget = 200000
priority = 0
```

| Key | Meaning |
|---|---|
| `name`, `version`, `author`, `description` | shown in the Mods screen |
| `id` | unique name, defaults to the folder name |
| `api` | the mod API level you wrote against. This build is level 1, a higher number will not load |
| `permissions` | `net` allows the `net` Lua library, `fs` allows the `fs` library (a private data folder). They apply to Lua only: a native plugin never gets the network or file access, whatever this line says. Unknown words give a warning |
| `scripts` | Lua files, run in this order inside one sandbox. Defaults to `main.lua` when it exists |
| `native` | the plugin, as a file name or a base name (`hello_native.dll`, `libhello_native.so`, `.dylib`) |
| `determinism` | `sim` (default) means the mod takes part in the gameplay and every player in an online match needs it. `cosmetic` means it only draws and can never change a result |
| `memory_mb` | Lua memory limit, 4 to 64 |
| `instruction_budget` | Lua instructions allowed per gameplay hook call, 1000 to 5000000 |
| `priority` | for data mods, which one wins when two replace the same file. The higher number wins |

Paths to scripts and plugins must stay inside the mod folder.

Each mod gets a content hash (a fingerprint over all its files). The folder and the zip of the same mod have the same hash. Permissions you grant are stored per hash, so an update to a mod asks you again. Online, the hashes of all active gameplay mods are compared when a player joins.

## Data mods

Files in a mod folder are looked up before the game's own files, using the same relative path they have in the game. Put a file at the same path and yours is used instead of the original. A mod can also add new files, such as extra levels.

* `image_scale = 2` in `mod.ini` tells the game that your art is twice the size of the original and should be drawn at the original size.
* Levels go in `levels/` as `.dat` files. Add a `mod_levels.cfg` there to list them as extra Quick Play stages, or let the level editor write all of this for you.
* A level can have an `.ini` next to it with `title`, `board_width`, `balls`, `orange`, `background` and `master`.

## The Lua API

### What is in the sandbox

You get `base`, `table`, `string`, `math`, `utf8` and `coroutine`. There is no `io`, `os`, `package` or `debug`, they are not even compiled in. `dofile`, `loadfile`, `collectgarbage` and `string.dump` are gone, `print` is the same as `log`, and `require("name")` loads `name.lua` from your mod folder only. `load` accepts text, never compiled chunks.

`math.sin`, `math.cos` and `math.atan` are special versions that return the same numbers on every system. `math.tan`, `asin`, `acos`, `exp`, `log` and friends are removed for the same reason. `math.random` is the game's random generator.

Limits: every gameplay hook gets an instruction budget, there is a memory limit, drawing and menu callbacks have a time limit (25 ms for `on_frame`, 250 ms for menu and network callbacks), and string patterns that could run away are refused. A hook that raises an error is logged and skipped. Drawing hooks are switched off after 20 errors. A single huge call such as `string.rep` on a gigantic input is only limited by memory.

### Where code runs

| Context | Where | You may |
|---|---|---|
| Load | the top of your script and `on_load` | define globals and hooks, `register_power`, `ui.menu` |
| Sim | the `on_*` gameplay hooks and power callbacks | read the board, use `state.*` and `rng_*`, queue changes. No global writes, no `net` |
| Draw | `on_frame` | read the board, use `draw.*` and globals |
| Ui | `ui.menu` callbacks and `net` callbacks | globals, `ui.message`, `net` and `fs` |

### Hooks

Define these as global functions.

| Hook | Arguments | When |
|---|---|---|
| `on_load()` | | after your scripts ran |
| `on_level_start(level)` | `{width, orange, players, nights, duel, pegs}` | a level is loaded |
| `on_tick(tick)` | the tick number | at the start of every 10 ms step |
| `on_shot(angle)` | radians | a shot is fired |
| `on_peg_hit(peg, ball)` | object id, ball index (-1 when a power did it) | every peg that lights up |
| `on_ball_lost()` | | a ball leaves the board |
| `on_shot_end(score, style)` | | the shot has been scored |
| `on_style_shot(id, points)` | | a style shot was awarded |
| `on_power(power, name)` | 1 to 14 | a Master power fired |
| `on_fever()` | | Extreme Fever started |
| `on_level_end(result)` | `{won, score, shots, orange_left, ticks}` | the level is over |
| `on_frame(info)` | `{tick, width, orange_left, score, frame}` | after every drawn frame |

Hooks run in load order, for every board (a Peg Party match has one per player), and also while an online match replays old ticks after a late shot.

### Reading the board (Sim and Draw)

`board.tick()`, `board.score([player])`, `board.orange_left()`, `board.orange_total()`, `board.balls_left([player])`, `board.shot_score()`, `board.player()`, `board.fever()`, `board.width()`, `board.master([player])`, `board.phase()` (`"aim"`, `"shot"`, `"popping"` or `"over"`), `board.peg_count()`, `board.peg(id)`, `board.pegs()`, `board.bricks()`, `board.ball_count()` and `board.balls()`.

`board.pegs()` returns a list of `{id, x, y, type, lit, gone, shape, radius, rot}` where `type` is `blue`, `orange`, `purple` or `green`. `board.balls()` returns `{id, x, y, vx, vy, r, fire, alive}`. Coordinates are board pixels with the origin at the top left of the playfield. Object ids are indexes that stay the same for the whole level.

### Changing things (Sim only)

These calls are queued and applied at the end of the tick, in the order you made them.

* `board.add_score(n)` adds to the shot score while a shot is live, otherwise to the banked score.
* `board.hit_peg(id)` lights a peg the way a power would.
* `board.add_balls(n)` gives extra balls.
* `board.set_ball_velocity(index, vx, vy)` sets a ball's speed (clamped to plus or minus 40).
* `board.say(text)` shows a banner.
* `board.style_shot(name, points)` adds style points and shows the name.

The events these cause call your hooks again, up to 8 rounds per tick.

### State and random numbers

* `state.get(key[, default])`, `state.set(key, number)` and `state.add(key, n)` keep numbers per board (up to 256 keys). This is where gameplay state belongs, because it is rolled back and compared between players with the rest of the board.
* `rng_next()` and `rng_range(lo, hi)` use the game's random generator inside gameplay hooks and a separate one elsewhere. There is no clock.

### Powers, menu and drawing

* `register_power{name=, replaces="Flower Power" | "Fireball" | "Tula" | 9, mode="replace" | "extra", on_activate=function(player, peg) end}` hooks a built-in Master power. Whenever it would fire, `on_activate` runs first, and with mode `"replace"` the built-in effect is skipped. To try it, pick that Master in Quick Play. New Master characters are not supported.
* `ui.menu(label, fn)` adds a button to the Mods screen. `ui.message(text)` shows a line there.
* `draw.rect(x, y, w, h, rgba[, fill])`, `draw.circle(x, y, r, rgba[, fill])`, `draw.text(x, y, s, rgba[, align, scale])`, `draw.image(path, x, y[, scale, alpha])`, `draw.screen(true)` to switch to 800x600 screen coordinates, `draw.rgb(r, g, b[, a])` and `draw.hsv(h, s, v[, a])`. Colours are `0xRRGGBBAA`.
* `log(text)` writes to the log. It is also what `print` does.

### Network and files

With `permissions = net` you get `net.get(url, cb[, {headers = {...}}])`, `net.post(url, body, cb[, opts])` with `cb(status, body, err)`, and `net.tcp(host, port, {on_open=, on_data=, on_close=})`. Only plain `http://` works, there is no TLS. Bodies are limited to 1 MB, requests to 10 seconds and 4 at a time, and redirects are not followed. Callbacks arrive between frames, never inside the simulation, and `net.*` raises an error if you call it from a gameplay hook. Do not feed data from the network into the game: every player must get the same result from a gameplay hook, and network data differs.

With `permissions = fs` you get `fs.read`, `fs.write`, `fs.exists`, `fs.remove` and `fs.list` on plain file names inside `mod-data/<your mod id>` (1 MB per file, 64 files).

### Keeping online matches in sync

Gameplay hooks run on every player's computer and must give identical results.

* Keep state in `state.*` and use `rng_*` for randomness.
* Use `+ - * /` and `math.sin`, `cos`, `atan`. Avoid `x ^ y` with fractional exponents.
* Globals are frozen while a gameplay hook runs (assigning one raises an error), so set them at load time. Local variables of your script are not protected: if you keep gameplay state in one, players will drift apart after a rollback.
* `pairs()` order is stable for string keys, but not for tables or functions used as keys.
* A mod with `determinism = cosmetic` gets no gameplay hooks and cannot change state. It only has `on_load`, `on_frame`, menu and network callbacks, and it does not count in the match comparison.

## Native plugins

The header `include/peggleport_mod.h` is plain C and versioned (`PP_MOD_ABI_VERSION`). Your library exports `int32_t pp_mod_init(const PPModApi*)`, fills a `PPModHooks` structure and calls `api->register_hooks`. The hooks are `on_level_start`, `on_tick`, `on_shot`, `on_peg_hit`, `on_ball_lost`, `on_level_end`, `on_power`, `on_activate_power` (return 1 to replace the built-in power), `on_frame` and a final cleanup hook. The API offers `log`, board queries, the same queued changes as Lua, `state_set` and `state_get`, `rng_next` and `draw_*` calls (inside `on_frame` only).

The same sync rules as for Lua apply.

### Where a native plugin runs

A native plugin does not run inside the game. For every native plugin the game starts a small helper program, `pp_modhost`, which sits next to the game (inside `PegglePort.app/Contents/MacOS` on macOS). The helper loads your library and talks to the game over a private pipe on your own computer. The helper ends when the game ends, and it is only started when a native plugin is switched on.

You write the same code as before. `PPModApi` and `PPModHooks` did not change, so a plugin built earlier still loads, and the game still gets the same results: a hook sees the board as it was when the hook started, queued changes are applied at the end of the tick, `on_activate_power` hands its return value back to the game, and everything is replayed the same way after a rollback in an online match. A hook call costs a few microseconds more than it would inside the game.

Two things to keep in mind:

* Call the API only from inside your hooks, on the thread the game called you on. Calls from other threads, or outside a hook, are ignored.
* `rng_next()` inside a gameplay hook asks the game for each number, so draw only as many as you need.

### What the helper is not allowed to do

The operating system locks the helper down before your library is even loaded, so static constructors are locked down as well. Your plugin cannot:

* use the network (no sockets, no name lookups),
* create, change or delete any file, not even in its own folder,
* start other programs or processes (threads are fine),
* read files outside its own mod folder and the system libraries, which keeps your profile, your settings and the other mods out of reach.

The `permissions` line of the manifest does not change any of this. A native plugin never gets the network or file access. If your plugin needs to remember something during a level, keep it with `state_set`, which is rolled back together with the board.

The lock-down uses what each system offers: a sandbox profile on macOS, Landlock and a seccomp filter on Linux (Landlock needs kernel 5.13 or newer), a restricted token, low integrity level and a job object on Windows. When the helper starts it checks which limits are really in force and reports them to the game, and the game never shows more than that.

### What the player sees

The permission dialog for a native plugin says what the helper blocks, for example: "It runs in a restricted helper process: no network, cannot write files, cannot start programs, can only read its own folder." The Mods screen shows the same for every active plugin.

If your system cannot enforce all four limits, the dialog says "This plugin could not be restricted on this system" and names what is missing. The plugin then stays off until the player also presses **Run anyway** in a second, separate question. That answer is stored in `mods.json` for the exact content hash of the plugin, so an update asks again. On Windows the helper limits file writes and new processes but cannot block the network, so Windows players are asked this second question.

### When a plugin misbehaves

If a gameplay hook takes longer than 1 second, or `on_frame` longer than 250 milliseconds, if the library crashes, or if the helper ends for any other reason, the game stops the helper, switches that plugin off and writes an error with the reason to the log (**F10**). The game keeps running. There is no second try.

To build the example yourself: `cmake --build build --target hello_native` puts the library and its `mod.ini` in `build/examples/mods/hello_native/`. Copy that folder to your `mods` folder.

## Network use: the scan and your OK

Before a mod loads, PegglePort reads its files:

* Lua: the `net` library, URLs, words such as `socket`, `tcp`, `curl` and `http`, and tricks like `load`, `_G[...]`, escaped or concatenated names (`"so" .. "cket"`).
* Native libraries (Windows, Linux and macOS formats): network libraries, imported network functions and embedded URLs.
* A manifest with `permissions = net`.

If it finds something, or the mod has native code, the game shows the findings and the mod does not load until you press **Allow...**. Until then even its data files stay unmounted.

This is a best-effort check. A native plugin can hide what it does from the scan, and the dialog says so. What protects you is the sandbox: without `permissions = net` a Lua mod has no network at all, and a native plugin runs in the restricted helper process that has no network whatever the scan found.

## Limits

* No new Master characters, peg types or physics.
* `https://` does not work.
* Changing the mod list or permissions needs a restart.
* The Lua time limits do not interrupt one very long library call.
