# Modding guide

PegglePort has three kinds of mods. They all live in the same place and go through the same permission check.

| Kind | What it is | Where it runs | Asks for permission |
|---|---|---|---|
| Data | Replaces art, sounds and levels, tweaks a few rules with an ini file | it is not code | no |
| Lua | Scripts with hooks into the game, drawing on top of the board, menu buttons, custom powers | sandbox | only when the scan finds network use |
| Native | A shared library (`.dll`, `.so`, `.dylib`) written against the C header `include/peggleport_mod.h` | not sandboxed | always |

The download has working examples in `examples/mods` (`hello_lua`, `rainbow_pegs`, `trace_hooks`, `moon_shots`, and a prebuilt `hello_native` when your platform has one) and the source of the native example in `examples/plugin`.

## Installing and turning mods on

1. Put the mod folder, or the `.zip`, into the `mods` folder of the game. The **Mods** screen has an **Open Folder** button that opens it. The folder sits next to your profiles: `<save folder>/userdata/mods` (see the FAQ for the save folders).
   A zip may hold the mod at its top level or inside one folder. It is unpacked into `mods/.cache` the first time.
2. Open **Options, then Mods**. Every mod is listed with a tick box, its state, the result of the scan and a **Scan report** or **Allow...** button. Tick the mods you want. Up and Down change the load order.
3. That is all. Ticking a mod, Up and Down, and **Allow...** take effect the moment you press them. You do not leave the game or restart it. The line at the bottom of the Mods screen says what happened, for example `Applied: 3 of 4 mods active`.

The choices are saved in `userdata/mods.json`.

### What happens when you change something

I switch every mod off, take its files out of the game, and load the ticked ones again in the new order. A Lua mod starts from a fresh script each time, so anything it kept in globals or local variables is gone; what belongs to a match lives in `state.*` on the board. A native plugin gets its cleanup hook and is then unloaded from the game, and the next time it is ticked the library is loaded again from disk.

Art, sounds and level lists are read again too. Levels that a mod adds show up in Quick Play as soon as the tick is set, and they disappear again when you untick it. Your profiles, scores and trophies are never touched by this.

* A mod that cannot load shows its error in its row and in the log. It is tried again when you untick and tick it, or when its files change.
* A mod that needs your OK stays off until you press **Allow...** and allow it. It loads right then.
* A change is only made while no level, online lobby or online match is on screen. If one is, the change waits and is made once you are back at the main menu.
* If you put a mod into the `mods` folder, or edit one, while the game is running, switch back to the game window while the Mods screen is open and it picks up the change. A mod with new code asks for your OK again, because it is a new version.

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
| `permissions` | `net` allows the `net` Lua library, `fs` allows the `fs` library (a private data folder). Unknown words give a warning |
| `scripts` | Lua files, run in this order inside one sandbox. Defaults to `main.lua` when it exists |
| `native` | the plugin, as a file name or a base name (`hello_native.dll`, `libhello_native.so`, `.dylib`) |
| `determinism` | `sim` (default) means the mod takes part in the gameplay and every player in an online match needs it. `cosmetic` means it only draws and can never change a result |
| `memory_mb` | Lua memory limit, 4 to 64 |
| `instruction_budget` | work allowed per hook call, 1000 to 5000000. Lua instructions and library calls both count |
| `priority` | for data mods, which one wins when two replace the same file. The higher number wins |

Paths to scripts and plugins must stay inside the mod folder.

Each mod gets a content hash (a fingerprint over all its files). The folder and the zip of the same mod have the same hash. Permissions you grant are stored per hash, so an update to a mod asks you again. Online, the hashes of all active gameplay mods are compared when a player joins.

## Data mods

Files in a mod folder are looked up before the game's own files, using the same relative path they have in the game. Put a file at the same path and yours is used instead of the original. A mod can also add new files, such as extra levels.

* `image_scale = 2` in `mod.ini` tells the game that your art is twice the size of the original and should be drawn at the original size.
* Levels go in `levels/` as `.dat` files. Add a `mod_levels.cfg` there to list them as extra Quick Play stages, or let the level editor write all of this for you.
* A level can have an `.ini` next to it with `title`, `board_width`, `balls`, `orange`, `background` and `master`. The same file can also change how the ball moves with `gravity`, `wind`, `launch_speed`, `bounce`, `peg_bounce`, `drag`, `max_speed` and `ball_radius`, see [Physics](#physics).

## The Lua API

### What is in the sandbox

You get `base`, `table`, `string`, `math`, `utf8` and `coroutine`. There is no `io`, `os`, `package` or `debug`, they are not even compiled in. `dofile`, `loadfile`, `collectgarbage` and `string.dump` are gone, `print` is the same as `log`, and `require("name")` loads `name.lua` from your mod folder only. `load` accepts text, never compiled chunks.

`math.sin`, `math.cos` and `math.atan` are special versions that return the same numbers on every system. `math.tan`, `asin`, `acos`, `exp`, `log` and friends are removed for the same reason. `math.random` is the game's random generator.

Limits: every hook and callback gets a work budget (`instruction_budget`, 200000 by default). Lua instructions use it, and so do library calls: `string.rep`, `string.format`, `table.sort`, `table.concat`, pattern matching, `load` and the rest are charged by the size of what they handle, and so is memory your script allocates. The charge depends on sizes only, never on the clock, so a hook uses exactly the same budget on every computer and online matches stay in sync. There is also a memory limit (`memory_mb`), and drawing and menu callbacks have a time limit (25 ms for `on_frame`, 250 ms for menu and network callbacks) that is checked inside library calls and while memory is allocated too. String patterns that could run away are refused.

When a hook runs out it is stopped with an error: `instruction budget exceeded` for Lua code, `work budget exceeded` for library calls and memory, `time limit exceeded` for the time limits. `pcall`, `xpcall` and `coroutine.resume` cannot catch these. A single call that is far too big to run at all, like a `string.rep` with a multi-megabyte result, fails at once with an ordinary error ending in `too large for the sandbox (split the work into smaller steps)`, which `pcall` can catch. Catching an error costs some of the budget, so a loop of failing `pcall`s ends quickly. A hook that raises an error is logged and skipped, and the mod keeps running. Drawing hooks are switched off after 20 errors. A string longer than 200 characters is never read as a number.

### Where code runs

| Context | Where | You may |
|---|---|---|
| Load | the top of your script and `on_load` | define globals and hooks, `register_power`, `ui.menu`, `physics.set` |
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
* `physics.set{...}` changes gravity, bounce and so on, see [Physics](#physics).

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

With `permissions = net` you get `net.get(url, cb[, {headers = {...}}])`, `net.post(url, body, cb[, opts])` with `cb(status, body, err)`, and `net.tcp(host, port, {on_open=, on_data=, on_close=})`. Bodies are limited to 1 MB, requests to 10 seconds and 4 at a time, and redirects are not followed. Callbacks arrive between frames, never inside the simulation, and `net.*` raises an error if you call it from a gameplay hook. Do not feed data from the network into the game: every player must get the same result from a gameplay hook, and network data differs.

Both `http://` and `https://` work. For `https://` the certificate of the server is always checked, and there is no setting, option or manifest entry that turns the check off. It has to chain to one of the trusted root certificates of your operating system (the system roots on macOS, the Windows certificate store, the CA bundle in `/etc/ssl` and similar places on Linux), it has to be valid today and for the host name in the URL, and the connection has to use TLS 1.2 or newer. I do not check revocation lists. When something is wrong, `status` is 0, `body` is nil and `err` says what: `TLS certificate check failed: the certificate has expired`, `... is not valid for example.org`, `... is not signed by a trusted authority`, or `TLS handshake failed: the server does not offer TLS 1.2 or newer`. The same text goes to the log. If your computer has no trusted root certificates at all, every `https://` request fails with `TLS is unavailable: no trusted root certificates were found on this computer`.

`net.tcp` takes `tls = true` in the same table when the server speaks TLS from the first byte, for example `net.tcp("mail.example.org", 465, {tls = true, on_open = ..., on_data = ...})`. It gets the same checks. `on_open` runs after the handshake, and when the check fails you only get `on_close(conn, err)`. If the server drops the connection without sending the TLS goodbye (close_notify), `on_close` also gets an `err`, because the data may have been cut short.

With `permissions = fs` you get `fs.read`, `fs.write`, `fs.exists`, `fs.remove` and `fs.list` on plain file names inside `mod-data/<your mod id>` (1 MB per file, 64 files).

### Keeping online matches in sync

Gameplay hooks run on every player's computer and must give identical results.

* Keep state in `state.*` and use `rng_*` for randomness.
* Use `+ - * /` and `math.sin`, `cos`, `atan`. Avoid `x ^ y` with fractional exponents.
* Globals are frozen while a gameplay hook runs (assigning one raises an error), so set them at load time. Local variables of your script are not protected: if you keep gameplay state in one, players will drift apart after a rollback.
* `pairs()` order is stable for string keys, but not for tables or functions used as keys.
* A mod with `determinism = cosmetic` gets no gameplay hooks and cannot change state. It only has `on_load`, `on_frame`, menu and network callbacks, and it does not count in the match comparison.

## Physics

A mod or a level can change how the ball moves. Distances are in board pixels and time is in the game's fixed 10 ms steps, the same units the original game uses.

| Setting | Level ini key | Original | Range | What it does |
|---|---|---|---|---|
| `gravity_y` | `gravity` | 0.05 | -0.5 to 0.5 | Speed added downwards on every step. A negative number lifts the ball |
| `gravity_x` | `wind` | 0 | -0.2 to 0.2 | Speed added sideways on every step. Positive blows to the right |
| `launch_speed` | `launch_speed` | 4 | 0.5 to 40 | Speed of the ball when it leaves the cannon |
| `ball_bounce` | `bounce` | 1 | 0 to 2 | Multiplies how much speed the ball keeps when it bounces off pegs, bricks, walls, lines and polygons. 1 is the original, 0 kills the bounce |
| `peg_bounce` | `peg_bounce` | 1 | 0 to 2 | The same, for pegs only, on top of `ball_bounce` |
| `drag` | `drag` | 0 | 0 to 0.5 | Share of its speed the ball loses on every step |
| `max_speed` | `max_speed` | 15 | 1 to 40 | The speed the ball can never exceed |
| `ball_radius` | `ball_radius` | 6 | 2 to 20 | Size of the ball. It is used for hitting pegs, for the other balls of a multiball, for the cannon and for drawing. The fireball grows or shrinks by the same factor |

A value outside its range is moved to the nearest end of the range and the log says so. Gravity, wind, bounce, drag and the speed limit change the balls that are already in the air. The launch speed and the ball size are read when a ball is launched, so they apply from the next ball on. The speed limit works from the first step, so a launch speed above `max_speed` is cut back to it. The guide line and the aiming help use the same gravity, wind, launch speed and ball size, so they still show where the ball goes.

### From Lua

`physics.set{gravity_y = 0.03, wind = 0.01}` changes only the settings you name. The short names of the ini keys (`gravity`, `wind`, `bounce`) work too. `physics.get()` returns a table with all eight settings.

* While the mod loads (the top of a script or `on_load`) the call sets the physics of every level. If several mods do this, the one that is later in the load order wins for each setting.
* In a gameplay hook the call is queued like `board.add_score`. A change made in `on_level_start` is in effect before the first step, one made in `on_tick` before the balls move in that step and one made in any other hook from the next step on. Hooks run again when a late shot rolls an online match back, so they put the same values in place again.
* It raises an error, and changes nothing, for a field it does not know, a value that is not a number or not finite, or the same setting twice (`gravity` and `gravity_y`). A mod with `determinism = cosmetic` cannot use it. From menu and drawing callbacks it is not available.

```lua
physics.set{gravity_y = 0.02, ball_bounce = 1.1}

function on_level_start(level)
  if level.pegs > 100 then physics.set{drag = 0.004} end
end

function on_tick(tick)
  if tick == 3000 then physics.set{wind = 0.01} end
end
```

### In a level ini

Put the keys in the level's `.ini` next to its `.dat`, for example `levels/MyLevel.ini` in your mod:

```ini
title = Moon Shot
gravity = 0.02
wind = 0.004
bounce = 1.1
ball_radius = 8
```

A number that cannot be read is ignored and the log says so. For one setting the order is: the original value, then what mods set while loading, then the level's ini, then what a mod sets in a hook. The level editor shows these keys in the Level tab, keeps them when it opens and saves a level, and uses them when you play the level from the editor.

### From a native plugin

The plugin header has `physics_set(const PPPhysics*)` and `physics_get(PPPhysics*)` at the end of the API table. Fill `struct_size`, set a bit of `mask` (`PP_PHYS_GRAVITY_Y`, `PP_PHYS_GRAVITY_X`, `PP_PHYS_LAUNCH_SPEED`, `PP_PHYS_BALL_BOUNCE`, `PP_PHYS_PEG_BOUNCE`, `PP_PHYS_DRAG`, `PP_PHYS_MAX_SPEED`, `PP_PHYS_BALL_RADIUS`) for every field you want to change and fill those fields. `physics_set` returns 1 when it took the change and 0 when it did not, for a value that is not finite, an unknown bit, or a call outside `pp_mod_init` and the gameplay hooks. Called from `pp_mod_init` it sets the physics of every level, from a hook it is queued like the other changes. `physics_get` fills all fields.

These two functions came with minor version 1 (`PP_MOD_ABI_MINOR`). `PP_MOD_ABI_VERSION` stays 1, so plugins built earlier keep loading. A plugin that wants to run on older builds as well checks `PP_MOD_API_HAS(api, physics_set)` before it calls the function.

### Keeping it in sync online

The physics values are part of the board, they are copied when the game keeps a snapshot for a rollback, and they go into the state hash that players compare after every shot as soon as they differ from the original values. A game that does not use any of this hashes and plays exactly as before. The level ini is part of the level's fingerprint and the scripts of a mod are part of the mod's fingerprint, so everyone in a match gets the same values. The same rules as for any gameplay hook apply: use `state.*` and the game's random numbers, nothing that differs from one computer to the next.

## Native plugins

The header `include/peggleport_mod.h` is plain C and versioned (`PP_MOD_ABI_VERSION`). Your library exports `int32_t pp_mod_init(const PPModApi*)`, fills a `PPModHooks` structure and calls `api->register_hooks`. The hooks are `on_level_start`, `on_tick`, `on_shot`, `on_peg_hit`, `on_ball_lost`, `on_level_end`, `on_power`, `on_activate_power` (return 1 to replace the built-in power), `on_frame` and a final cleanup hook. The API offers `log`, board queries, the same queued changes as Lua, `physics_set` and `physics_get`, `state_set` and `state_get`, `rng_next` and `draw_*` calls (inside `on_frame` only).

When a plugin is turned off, the game calls your final cleanup hook and then unloads the library, so stop any thread you started and release what you own in that hook. The same library is loaded again from disk, and `pp_mod_init` runs again, the next time it is ticked.

The same sync rules as for Lua apply. There is no sandbox. The plugin runs with all the rights of the game and is loaded only after you allowed this exact version of it.

To build the example yourself: `cmake --build build --target hello_native` puts the library and its `mod.ini` in `build/examples/mods/hello_native/`. Copy that folder to your `mods` folder.

## Network use: the scan and your OK

Before a mod loads, PegglePort reads its files:

* Lua: the `net` library, URLs, words such as `socket`, `tcp`, `curl` and `http`, and tricks like `load`, `_G[...]`, escaped or concatenated names (`"so" .. "cket"`).
* Native libraries (Windows, Linux and macOS formats): network libraries, imported network functions and embedded URLs.
* A manifest with `permissions = net`.

If it finds something, or the mod has native code, the game shows the findings and the mod does not load until you press **Allow...**. Until then even its data files stay unmounted.

This is a best-effort check. A native plugin can hide what it does, and the dialog says so. For Lua the sandbox is what protects you: without `permissions = net` a Lua mod has no network at all.

## Limits

* No new Master characters or peg types.
* Native plugins are scanned, never contained.
