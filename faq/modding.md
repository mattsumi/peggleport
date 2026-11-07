# Modding guide

PegglePort has three kinds of mods. They all live in the same place and go through the same permission check.

| Kind | What it is | Where it runs | Asks for permission |
|---|---|---|---|
| Data | Replaces art, sounds and levels, tweaks a few rules with an ini file | it is not code | no |
| Lua | Scripts with hooks into the game, drawing on top of the board, menu buttons, custom powers, new peg types | sandbox | only when the scan finds network use |
| Native | A shared library (`.dll`, `.so`, `.dylib`) written against the C header `include/peggleport_mod.h` | not sandboxed | always |

The download has working examples in `examples/mods` (`hello_lua`, `rainbow_pegs`, `trace_hooks`, `ice_pegs`, and a prebuilt `hello_native` when your platform has one) and the source of the native example in `examples/plugin`.

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
| `permissions` | `net` allows the `net` Lua library, `fs` allows the `fs` library (a private data folder). Unknown words give a warning |
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
* A level can have an `.ini` next to it with `title`, `board_width`, `balls`, `orange`, `background` and `master`, and `peg` lines that give pegs a mod peg type (see Peg types below).

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

`board.pegs()` returns a list of `{id, x, y, type, lit, gone, shape, radius, rot}` where `type` is `blue`, `orange`, `purple` or `green`. A peg of the level file also has `index`, its number in the level file, and a peg that has a mod peg type has `custom` (the type id) and `hits` (see Peg types). `board.balls()` returns `{id, x, y, vx, vy, r, fire, alive}`. Coordinates are board pixels with the origin at the top left of the playfield. Object ids are indexes that stay the same for the whole level.

### Changing things (Sim only)

These calls are queued and applied at the end of the tick, in the order you made them.

* `board.add_score(n)` adds to the shot score while a shot is live, otherwise to the banked score.
* `board.hit_peg(id)` lights a peg the way a power would.
* `board.add_balls(n)` gives extra balls.
* `board.set_ball_velocity(index, vx, vy)` sets a ball's speed (clamped to plus or minus 40).
* `board.say(text)` shows a banner.
* `board.style_shot(name, points)` adds style points and shows the name.
* `board.set_peg_type(id, type)` and `board.set_peg_types{[id] = type, ...}` give pegs a peg type (see Peg types).

The events these cause call your hooks again, up to 8 rounds per tick.

### State and random numbers

* `state.get(key[, default])`, `state.set(key, number)` and `state.add(key, n)` keep numbers per board (up to 256 keys). This is where gameplay state belongs, because it is rolled back and compared between players with the rest of the board.
* `rng_next()` and `rng_range(lo, hi)` use the game's random generator inside gameplay hooks and a separate one elsewhere. There is no clock.

### Powers, menu and drawing

* `register_power{name=, replaces="Flower Power" | "Fireball" | "Tula" | 9, mode="replace" | "extra", on_activate=function(player, peg) end}` hooks a built-in Master power. Whenever it would fire, `on_activate` runs first, and with mode `"replace"` the built-in effect is skipped. To try it, pick that Master in Quick Play.
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

## Peg types

A peg type is a kind of peg you define: how it looks, what it scores, how many hits it takes, whether it counts as an orange peg, how bouncy it is. You register types while your mod loads and then give them to pegs, from Lua, from a level's `.ini` file or from a native plugin. A mod that defines peg types is a gameplay mod, so everybody in an online match needs it.

```lua
register_peg_type{
  id = "ice",
  name = "Ice Peg",
  stages = { "images/ice_0.png", "images/ice_1.png" },
  lit_image = "images/ice_lit.png",
  color = 0x9fd8ff,
  hits_to_clear = 2,
  score = 50,
  on_clear = function(peg) board.add_score(100) end,
}

function on_level_start(level)
  local plan = {}
  for _, p in ipairs(board.pegs()) do
    if p.type == "blue" and p.index % 5 == 0 then plan[p.id] = "ice" end
  end
  board.set_peg_types(plan)
end
```

`examples/mods/ice_pegs` is a complete mod with three types and four small images.

### What you can set

Only `id` is required.

| Field | Meaning | Default |
|---|---|---|
| `id` | letters, digits, `_` and `-`, up to 32 characters. Types of different mods never clash, the full name is `modid:id` | |
| `name` | a readable name, up to 60 characters | the id |
| `base` | `"blue"`, `"orange"`, `"purple"` or `"green"`: the kind it acts like. Green pegs give the Master's power, purple pegs give the point boost | `"blue"` |
| `score` | points for clearing it, before the multiplier. 0 means what the base kind gives (10, 100 or 500). A fixed score is never cut down at the x100 multiplier | 0 |
| `counts_as_orange` | `true`: it has to be cleared to win the level. `false`: it never counts. Left out: it counts when `base` is `"orange"`, and a peg that was already orange stays orange | left out |
| `hits_to_clear` | 1 to 100 hits before it lights up | 1 |
| `clears_on_hit` | `false` makes an indestructible peg that never lights up and scores on every hit | `true` |
| `bounce` | multiplies the bounce of the ball, 0.1 to 3 | 1 |
| `radius_scale` | multiplies the radius of a round peg, 0.5 to 3 | 1 |
| `image`, `lit_image` | PNG, JPEG or GIF files in your mod folder for the normal and the lit look. Without `image` the peg is drawn with the game's own peg art | |
| `stages` | a list of up to 16 images shown by number of hits taken, instead of `image`, while the peg is not lit yet | |
| `color` | `0xRRGGBB`. Tints the game's own peg art when you give no image, and colours the glow, the sparks and the pulse of the peg | |
| `on_hit` | `function(peg, ball)`, called for every counted hit | |
| `on_clear` | `function(peg)`, called when the peg lights up | |

`peg` is the same table that `board.peg(id)` returns and `ball` is the number of the ball, or -1 when a power or `board.hit_peg` did it. Both callbacks run like the gameplay hooks: you can read the board and `state.*`, and the changes you ask for (`board.add_score`, `board.set_peg_type` and so on) are applied at the end of the tick. They also run again when an online match replays old ticks, so keep your gameplay state in `state.*`.

A mod can define up to 64 types.

### How the pegs behave

* A peg counts at most one hit every 8 ticks (0.08 seconds), so a ball resting on it does not use up all its hits at once.
* A multi-hit peg shows its progress: with `stages` the image changes with every hit, without them the lit look fades in. Every counted hit that does not clear the peg plays the bumper sound.
* A power (Space Blast, Flower Power), a fireball and `board.hit_peg` clear a multi-hit peg at once. A power and `board.hit_peg` do nothing to an indestructible peg, a fireball flies through it and scores the hit.
* An indestructible peg adds `score` points to the shot on every hit (10 if you left it at 0), times the usual multiplier. It is never lit and is not part of the level goal.
* A level is won when all orange pegs are cleared, as always. Pegs that count as orange take part in that, indestructible pegs and pegs that are not orange do not hold the level up. What is left on the board is cleared when the level ends, indestructible pegs included.
* A level has at most 25 orange pegs. A type that counts as orange can go on a peg that is already orange, and on any other peg while fewer than 25 are orange. Otherwise the peg stays as it was.
* When the game picks a peg at random (the green and purple pegs, the new orange pegs the Flippers power chooses) it never picks one that has a mod type.

### Giving types to pegs

**From Lua**: `board.set_peg_type(id, "ice")` or `board.set_peg_types{[id] = "ice", ...}`, usually from `on_level_start`. A name without `:` is looked up in your own mod first, then in the others. Use `"othermod:ice"` to name the type of another mod. The calls are queued like the other changes. A peg that is lit or gone is left alone, and so is a name nobody defined. A peg can get a different type later, there is no way back to a plain peg.

**From the level file**: `peg` lines in the level's `.ini`.

```ini
title = Frozen Lake
peg = 3, 5, 8-12: ice
peg = 20: othermod:bomb
```

The number is the position of the object in the level file, counting from 0, not counting the objects every level shares (walls, bucket). `board.pegs()` shows it as `index`. The lines are applied when the level is set up, before `on_level_start` runs, and they are part of the level's content hash. If no mod provides the name, the level plays as a normal level and the log says so.

The level editor keeps these lines when you open and save a level, and the **Custom type** field of the Object tab sets them on the selected pegs. The editor itself shows such pegs as normal ones.

**From a native plugin**: `register_peg_type` takes a `PPPegTypeDef` (the same fields, plain numbers and strings, `indestructible` instead of `clears_on_hit`, and 0 meaning the default for `counts_as_orange`, `hits_to_clear`, `bounce` and `radius_scale`), `set_peg_type(peg, "ice")` gives a peg a type and `peg_custom(peg, buffer, size, &hits)` reads it back. Register in `pp_mod_init`. Plugins built against the older header still load, the new calls are at the end of `PPModApi`: check `PP_MOD_API_HAS(api, set_peg_type)` first when you want to run on older versions of the game.

### Online matches and replays

Every player computes every board, so the type table is built the same way on every computer: types are sorted by mod id and type id, not by the order mods loaded. The hits a peg has taken are part of the board state that is rolled back and compared between players. Peg Party and duels work the same way, every board has its own pegs.

## Native plugins

The header `include/peggleport_mod.h` is plain C and versioned (`PP_MOD_ABI_VERSION`). Your library exports `int32_t pp_mod_init(const PPModApi*)`, fills a `PPModHooks` structure and calls `api->register_hooks`. The hooks are `on_level_start`, `on_tick`, `on_shot`, `on_peg_hit`, `on_ball_lost`, `on_level_end`, `on_power`, `on_activate_power` (return 1 to replace the built-in power), `on_frame` and a final cleanup hook. The API offers `log`, board queries, the same queued changes as Lua, `state_set` and `state_get`, `rng_next` and `draw_*` calls (inside `on_frame` only).

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

* No new Master characters or physics.
* `https://` does not work.
* Changing the mod list or permissions needs a restart.
* The time limits do not interrupt one very long library call.
* Native plugins are scanned, never contained.
