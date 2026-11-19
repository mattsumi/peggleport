# Modding guide

PegglePort has three kinds of mods. They all live in the same place and go through the same permission check.

| Kind | What it is | Where it runs | Asks for permission |
|---|---|---|---|
| Data | Replaces art, sounds and levels, tweaks a few rules with an ini file | it is not code | no |
| Lua | Scripts with hooks into the game, drawing on top of the board, menu buttons, custom powers, new Masters, new peg types, physics | sandbox | only when the scan finds network use |
| Native | A shared library (`.dll`, `.so`, `.dylib`) written against the C header `include/peggleport_mod.h` | separate restricted helper process | always |

The download has working examples in `examples/mods` (`hello_lua`, `rainbow_pegs`, `trace_hooks`, `moon_shots`, `ice_pegs`, `frost_queen`, and a prebuilt `hello_native` when your platform has one) and the source of the native example in `examples/plugin`.

## Installing and turning mods on

1. Put the mod folder, or the `.zip`, into the `mods` folder of the game. The **Mods** screen has an **Open Folder** button that opens it. The folder sits next to your profiles: `<save folder>/userdata/mods` (see the FAQ for the save folders).
   A zip may hold the mod at its top level or inside one folder. It is unpacked into `mods/.cache` the first time.
2. From the main menu, select **Options → Mods** in Classic 4:3 or **Mods** in widescreen. Every mod is listed with a tick box, its state, the result of the scan and a **Scan report** or **Allow...** button. Tick the mods you want. Up and Down change the load order.
3. That is all. Ticking a mod, Up and Down, and **Allow...** take effect the moment you press them. You do not leave the game or restart it. The line at the bottom of the Mods screen says what happened, for example `Applied: 3 of 4 mods active`.

The choices are saved in `userdata/mods.json`.

### What happens when you change something

I switch every mod off, take its files out of the game, and load the ticked ones again in the new order. A Lua mod starts from a fresh script each time, so anything it kept in globals or local variables is gone; what belongs to a match lives in `state.*` on the board. A native plugin gets its cleanup hook, its helper process ends, and the next time it is ticked a new helper loads the library again from disk.

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
| `permissions` | `net` allows the `net` Lua library, `fs` allows the `fs` library (a private data folder). They apply to Lua only: a native plugin never gets the network or file access, whatever this line says. Unknown words give a warning |
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
* `image_alpha = baked` in `mod.ini` tells the game to use the replacement image's own alpha instead of loading separate masks. See the [HD mod guide](hd-mod.md) for the local pack generator and widescreen artwork.
* Levels go in `levels/` as `.dat` files. Add a `mod_levels.cfg` there to list them as extra Quick Play stages, or let the level editor write all of this for you.
* A level can have an `.ini` next to it with `title`, `board_width` (650 is the classic width, a larger number makes that level's board wider), `balls`, `orange`, `background` and `master`. The same file can also change how the ball moves with `gravity`, `wind`, `launch_speed`, `bounce`, `peg_bounce`, `drag`, `max_speed` and `ball_radius`, see [Physics](#physics), and give pegs a mod peg type with `peg` lines, see [Peg types](#peg-types).

### Pego prototype pack

Run `python examples/mods/build_pego_pack.py "path/to/Pego Deluxe" "path/to/pego_prototype"` with your own August 2006 prototype files. Put the generated folder into Deluxe's Mods folder and enable **Pego Prototype**. Its 55 boards appear as extra Quick Play stages. The pack keeps their original base geometry and uses prototype backgrounds and thumbnails where supplied. Deluxe supplies the remaining art, interface and rules; this is not the full prototype campaign.

A level sidecar can use `base = _pack_base` to load `levels/_pack_base.dat` instead of `levels/_base.dat`. The base name must start with an underscore and contain only letters, digits, underscores or hyphens.

### World of Warcraft level pack

Build the `pp_wow_pack` target, then run `pp_wow_pack "path/to/Peggle World of Warcraft Edition" "path/to/wow_levels"`. The tool reads your installed copy and creates a data-only mod. Put the folder into Deluxe's or Nights' Mods folder and enable **Peggle WoW Levels**. From the first Quick Play page, use the back arrow to jump to mod stages, then page forward until you see **WoW - Stage 1**.

The pack has the ten campaign boards and the **1-Shot Kill** challenge layout on a bonus page. The bonus is played with normal Quick Play rules. The pack does not install the original campaign, music, or challenge rules into the host game.

## The Lua API

### What is in the sandbox

You get `base`, `table`, `string`, `math`, `utf8` and `coroutine`. There is no `io`, `os`, `package` or `debug`, they are not even compiled in. `dofile`, `loadfile`, `collectgarbage` and `string.dump` are gone, `print` is the same as `log`, and `require("name")` loads `name.lua` from your mod folder only. `load` accepts text, never compiled chunks.

`math.sin`, `math.cos` and `math.atan` are special versions that return the same numbers on every system. `math.tan`, `asin`, `acos`, `exp`, `log` and friends are removed for the same reason. `math.random` is the game's random generator.

Limits: every hook and callback gets a work budget (`instruction_budget`, 200000 by default). Lua instructions use it, and so do library calls: `string.rep`, `string.format`, `table.sort`, `table.concat`, pattern matching, `load` and the rest are charged by the size of what they handle, and so is memory your script allocates. The charge depends on sizes only, never on the clock, so a hook uses exactly the same budget on every computer and online matches stay in sync. There is also a memory limit (`memory_mb`), and drawing and menu callbacks have a time limit (25 ms for `on_frame`, 250 ms for menu and network callbacks) that is checked inside library calls and while memory is allocated too. String patterns that could run away are refused.

When a hook runs out it is stopped with an error: `instruction budget exceeded` for Lua code, `work budget exceeded` for library calls and memory, `time limit exceeded` for the time limits. `pcall`, `xpcall` and `coroutine.resume` cannot catch these. A single call that is far too big to run at all, like a `string.rep` with a multi-megabyte result, fails at once with an ordinary error ending in `too large for the sandbox (split the work into smaller steps)`, which `pcall` can catch. Catching an error costs some of the budget, so a loop of failing `pcall`s ends quickly. A hook that raises an error is logged and skipped, and the mod keeps running. Drawing hooks are switched off after 20 errors. A string longer than 200 characters is never read as a number.

### Where code runs

| Context | Where | You may |
|---|---|---|
| Load | the top of your script and `on_load` | define globals and hooks, `register_power`, `register_master`, `register_peg_type`, `ui.menu`, `physics.set` |
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

`board.tick()`, `board.score([player])`, `board.orange_left()`, `board.orange_total()`, `board.balls_left([player])`, `board.shot_score()`, `board.player()`, `board.fever()`, `board.width()`, `board.master([player])`, `board.master_key([player])`, `board.phase()` (`"aim"`, `"shot"`, `"popping"` or `"over"`), `board.peg_count()`, `board.peg(id)`, `board.pegs()`, `board.bricks()`, `board.ball_count()` and `board.balls()`.

`board.pegs()` returns a list of `{id, x, y, type, lit, gone, shape, radius, rot}` where `type` is `blue`, `orange`, `purple` or `green`. A peg of the level file also has `index`, its number in the level file, and a peg that has a mod peg type has `custom` (the type id) and `hits` (see Peg types). `board.balls()` returns `{id, x, y, vx, vy, r, fire, alive}`. Coordinates are board pixels with the origin at the top left of the playfield. Object ids are indexes that stay the same for the whole level.

### Changing things (Sim only)

These calls are queued and applied at the end of the tick, in the order you made them.

* `board.add_score(n)` adds to the shot score while a shot is live, otherwise to the banked score.
* `board.hit_peg(id)` lights a peg the way a power would.
* `board.add_balls(n)` gives extra balls.
* `board.set_ball_velocity(index, vx, vy)` sets a ball's speed (clamped to plus or minus 40).
* `board.say(text)` shows a banner.
* `board.style_shot(name, points)` adds style points and shows the name.
* `physics.set{...}` changes gravity, bounce and so on, see [Physics](#physics).
* `board.set_peg_type(id, type)` and `board.set_peg_types{[id] = type, ...}` give pegs a peg type (see Peg types).

The events these cause call your hooks again, up to 8 rounds per tick.

### State and random numbers

* `state.get(key[, default])`, `state.set(key, number)` and `state.add(key, n)` keep numbers per board (up to 256 keys). This is where gameplay state belongs, because it is rolled back and compared between players with the rest of the board.
* `rng_next()` and `rng_range(lo, hi)` use the game's random generator inside gameplay hooks and a separate one elsewhere. There is no clock.

### Powers, menu and drawing

* `register_power{name=, replaces="Flower Power" | "Fireball" | "Tula" | 9, mode="replace" | "extra", on_activate=function(player, peg) end}` hooks a built-in Master power. Whenever it would fire, `on_activate` runs first, and with mode `"replace"` the built-in effect is skipped. To try it, pick that Master in Quick Play. It only changes the powers that are already in the game. To add a whole new Master with a name, a portrait and a power of its own, use `register_master` (see "Master characters" below).
* `ui.menu(label, fn)` adds a button to the Mods screen. `ui.message(text)` shows a line there.
* `draw.rect(x, y, w, h, rgba[, fill])`, `draw.circle(x, y, r, rgba[, fill])`, `draw.text(x, y, s, rgba[, align, scale])`, `draw.image(path, x, y[, scale, alpha])`, `draw.screen(true)` to switch to 800x600 screen coordinates (the classic picture, which stands in the middle of the frame in a wide window), `draw.rgb(r, g, b[, a])` and `draw.hsv(h, s, v[, a])`. Colours are `0xRRGGBBAA`.
* `log(text)` writes to the log. It is also what `print` does.

Drawing accepts finite positions from -32,768 to 32,768 and nonnegative widths, heights and circle diameters up to 8,192 canvas units. Text and image scale must be between 0 and 16; image opacity must be between 0 and 1. After window scaling, a drawing is limited to 8,192 pixels on either side and 16,777,216 pixels in area. A frame shares a maximum of 1,024 drawing calls and 8,192 text bytes across all mods, with at most 4,096 bytes in one text call. Missing images and invalid requests also count toward the call limit. Text scale 0 uses the default scale of 1. Image paths are limited to 4,096 bytes. Image sources and masks are limited to 2,048 pixels per side, 1,048,576 pixels total (4 MiB decoded RGBA), and 8 MiB per encoded file, checked before decoding. JPEG2000 images allow at most four components or palette channels, 16 bits per component, four channel definitions, 256 tiles and tile parts, 16 quality layers and eight resolution levels, with canvas and tile sides up to 2,048 pixels and precincts and code blocks at least 16 × 16 pixels; images with region, progression-change, packed-header or extended-transform markers are skipped. Upscaling can produce at most 4,096 pixels per side and 16 MiB of RGBA pixels. Individual PNG, JPEG, GIF and TGA decoder buffers are limited to 32 MiB. The mod image cache keeps at most 256 successful images and 64 MiB of texture pixels across frames, discarding entries as needed; missing images are not cached. Drawings outside these bounds are skipped.

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

**From a native plugin**: `register_peg_type` takes a `PPPegTypeDef` (the same fields, plain numbers and strings, `indestructible` instead of `clears_on_hit`, `base` as a number from 1 to 4 for blue, orange, purple and green, 0 meaning the default for `base`, `hits_to_clear`, `bounce` and `radius_scale`, and for `counts_as_orange` 1 for yes, -1 for no and 0 for left out), `set_peg_type(peg, "ice")` gives a peg a type (queued, and a name without `:` is looked up in your own plugin first) and `peg_custom(peg, buffer, size, &hits)` reads it back: it writes the type name the way `board.peg(id).custom` shows it, returns the length of the name (0 for a peg without a mod type) and sets `hits` to the hits the peg has taken. `on_hit(peg, ball)` and `on_clear(peg)` of the definition are called like the Lua callbacks. Set `struct_size` of the definition to `sizeof` it. `register_peg_type` returns 1 when the type was accepted and 0 when not: a wrong id, a path that leaves the mod folder, a repeated id, more than 64 types, or a call outside `pp_mod_init`. Check `PP_MOD_API_HAS(api, set_peg_type)` first when you want to run on older versions of the game.

### Online matches and replays

Every player computes every board, so the type table is built the same way on every computer: types are sorted by mod id and type id, not by the order mods loaded. The hits a peg has taken are part of the board state that is rolled back and compared between players. Peg Party and duels work the same way, every board has its own pegs.

## Master characters

A mod can add new Master characters. They are listed after the original ones in the character choice and in the online room, with their own name, portrait and power. The power is not one of the 14 built-in ones: it is made of your callbacks, which can use everything a gameplay hook can use (`board.add_score`, `board.hit_peg`, `board.add_balls`, `board.set_ball_velocity`, `board.say`, `board.style_shot`, `state.*`, `rng_*`). Use `register_power` when you only want to change what an original Master does.

Call `register_master` while the mod loads, at the top of a script or in `on_load`:

```lua
register_master {
  id = "frost", name = "Frost Queen", description = "She rules the winter board.",
  power_name = "Frost Snap", power_description = "For 3 shots every shot is worth 250 more.",
  portrait = "images/frost.png", turns = 3, trigger = "green_peg",
  banner = "Frost Snap!", color = 0x66ccff,
  on_activate = function(player, peg) board.add_balls(1) end,
  on_shot = function(player) board.add_score(250) end,
  on_end = function(player) log("over") end,
}
```

| Field | Meaning |
|---|---|
| `id` | Required. 1 to 32 letters, digits, `-` or `_` (kept in lower case). It has to be unique inside your mod and cannot be the name of an original Master. Together with your mod id it makes the key `modid:id`, which is how saves, level files and online rooms name the Master |
| `name` | Shown in the menus, up to 40 characters. Defaults to the id |
| `description` | A line or two about the character, shown in the character choice |
| `power_name`, `power_description` | The name of the power and what it does, shown below the description. The name defaults to `Master Power` |
| `portrait` | A picture file inside your mod folder, for example `images/frost.png` (PNG or JPEG, up to 4096 by 4096 pixels and 4 MB). It is shown in the character choice and in the round window above the cannon. Without one, the game draws a badge with the first letter of the name in your colour |
| `turns` | 1 to 99, default 3. How many shots the power lasts once it is granted |
| `trigger` | `"green_peg"` (default), `"every_shot"` or `"level_start"`, see below |
| `banner` | The text that pops up where the power is granted. Defaults to the power name with `!` |
| `color` | `0xRRGGBB` for the banner text and the badge. Default is a light green |
| `on_activate(player, peg)` | Runs when the power is granted. `peg` is the object id of the green peg, or -1 when there is none |
| `on_shot(player)` | Runs every time a shot is fired while the power has turns left |
| `on_end(player)` | Runs when the last turn of the power is over |

The three triggers:

* `green_peg` is how the original Masters work. Hitting a green peg grants the power for the next `turns` shots, shows the banner and runs `on_activate`. Hitting another green peg while it is on tops it up to `turns` again, it does not add up.
* `every_shot` means the power is always on. `on_activate` and then `on_shot` run each time a shot is fired. `turns` is not used, green pegs do nothing special and `on_end` never runs.
* `level_start` grants the power once when the level starts, for `turns` shots. `on_activate` runs right away. Green pegs do nothing special.

The callbacks run like the other gameplay hooks: changes are queued and applied at the end of the tick, global writes are not allowed, and anything you want to remember goes in `state.*`. `board.master(player)` gives the number of the Master, 16 or more for a Master from a mod, and `board.master_key(player)` gives `modid:id` (or `nil` for an original Master). `on_power` still reports only the 14 original powers.

One mod can add up to 16 Masters and all mods together up to 48. A mod with `determinism = cosmetic` cannot add Masters. The numbers from 16 on are handed out in order of mod id and then Master id, so players with the same mods always get the same numbers.

### Choosing a mod Master

* In the character choice the original Masters come first. When mods add Masters, a **Mods >** button appears under the grid, in Duel under each of the two grids. It pages through the mod Masters, ten to a page, and **< Back** returns to the originals. Mod Masters are always unlocked. In Duel, point at one to read its name, power and description. In the choice for one player, click it.
* Your pick is stored in `mod-masters.ini` in your `userdata` folder, next to your profile. The profile file itself is not touched and keeps the last original Master you picked. If the mod is removed or switched off, you play with that original Master again, and the pick comes back when the mod does. Picking an original Master clears it.
* Wherever you choose your Master, a mod Master works too: Quick Play, Duel, the Challenges that let you choose, and Adventure once you have reached the Master levels.
* A level can name a Master in its `.ini`: `master = frost` or `master = modid:frost`. It is used for single player when the level is played.
* The computer player can play a mod Master. When it plans a shot it treats the power as no power at all.

### Online

Mod Masters are in the **Master** choice when you create a room, after the original ones. The seats get consecutive Masters from your pick, as before. The host sends the Master by its key, so a guest who is missing the mod is told which Master it is and does not join. The usual comparison of the active gameplay mods still decides whether a guest can join, so everybody has the same Masters and powers.

### Example

`examples/mods/frost_queen` adds two Masters: Frost Queen with a portrait, a green peg power that lights a few more pegs and adds score on every shot, and Gleam, which is always on and has no portrait. Copy the folder into your `mods` folder and tick it in the Mods screen. The picture was made with a small script and does not come from the game.

## Native plugins

The header `include/peggleport_mod.h` is plain C and versioned (`PP_MOD_ABI_VERSION`). Your library exports `int32_t pp_mod_init(const PPModApi*)`, fills a `PPModHooks` structure and calls `api->register_hooks`. The hooks are `on_level_start`, `on_tick`, `on_shot`, `on_peg_hit`, `on_ball_lost`, `on_level_end`, `on_power`, `on_activate_power` (return 1 to replace the built-in power), `on_frame` and a final cleanup hook (`shutdown`).

What the `PPModApi` table gives you:

* `log`
* board queries: `tick`, `score`, `orange_left`, `balls_left`, `object_count`, `peg_info`, `ball_count`, `ball_info`
* the queued changes `add_score`, `hit_peg`, `add_balls` and `say`
* `state_set` and `state_get`, `rng_next`
* `draw_rect`, `draw_circle` and `draw_text` (inside `on_frame` only)
* `physics_set` and `physics_get` (see [Physics](#from-a-native-plugin))
* `register_peg_type`, `set_peg_type` and `peg_custom` (see [Peg types](#giving-types-to-pegs))
* `register_master` (see below)

`PP_MOD_ABI_VERSION` is 1 and stays 1. The table only grows at the end, and `PP_MOD_ABI_MINOR` (1 today, also stored in the table as `abi_minor`) counts the additions: the physics calls, `abi_minor` itself, the three peg type calls and `register_master`. A plugin built against an older header keeps loading and only looks at the part of the table it knows. A plugin that wants to run on older versions of the game as well checks `PP_MOD_API_HAS(api, member)` before it calls something newer.

When a plugin is turned off, the game asks the helper (see below) to call your final cleanup hook, which gets about 300 milliseconds, and then ends the helper. Turning the plugin on again starts a new helper that loads the library from disk and runs `pp_mod_init` again, so your static variables start fresh.

A plugin can add Masters too. The header has a `PPMasterDef` structure (the same fields as the Lua table, plus plain C callbacks `on_activate(player, peg)`, `on_shot(player)` and `on_end(player)`) and `api->register_master(&def)`, which you call from `pp_mod_init`. Set `struct_size` of the definition to `sizeof` it and use the `PP_MASTER_TRIGGER_*` values for `trigger`. It returns 0 when the Master was accepted and a number when not: 1 outside `pp_mod_init` or from a cosmetic plugin, 2 for a `struct_size` that is too small, 3 for an id that is empty, has other characters than letters, digits, `-` and `_`, is longer than 32 characters or is the name of an original Master, 4 for an id your plugin used already, 5 when you already registered 16 Masters and 6 for a `trigger` that does not exist. The id is kept in lower case, and longer texts are cut: names and banners to 40 characters, descriptions to 240.

The same sync rules as for Lua apply.

### Where a native plugin runs

A native plugin does not run inside the game. For every native plugin the game starts a small helper program, `pp_modhost`, which sits next to the game (inside `PegglePort.app/Contents/MacOS` on macOS). The helper loads your library and talks to the game over a private pipe on your own computer. The helper ends when the game ends, and it is only started when a native plugin is switched on.

Everything in the API works from the helper, and the game gets the same results as for a Lua mod: a hook sees the board as it was when the hook started, queued changes are applied at the end of the tick, `on_activate_power` hands its return value back to the game, and everything is replayed the same way after a rollback in an online match. The callbacks of your peg types (`on_hit`, `on_clear`) and of your Masters (`on_activate`, `on_shot`, `on_end`) are C functions in your library, so they run in the helper as well, at the point where a Lua callback would run, with the same board view and the same queued changes. Every call into your library costs a few microseconds.

Two things to keep in mind:

* Call the API only from inside `pp_mod_init`, your hooks and your callbacks, on the thread the game called you on. Calls from other threads, or outside those, are ignored. `register_peg_type` and `register_master` work in `pp_mod_init` only, and `physics_set` works there and in the gameplay hooks and callbacks.
* `rng_next()` inside a gameplay hook or callback asks the game for each number, so draw only as many as you need.

### What the helper is not allowed to do

The operating system locks the helper down before your library is even loaded, so static constructors are locked down as well. Your plugin cannot:

* use the network (no sockets, no name lookups),
* create, change or delete any file, not even in its own folder,
* start other programs or processes (threads are fine),
* read files outside its own mod folder and the system libraries, which keeps your profile, your settings and the other mods out of reach.

The `permissions` line of the manifest does not change any of this. A native plugin never gets the network or file access. If your plugin needs to remember something during a level, keep it with `state_set`, which is rolled back together with the board.

The lock-down uses what each system offers: a sandbox profile on macOS, Landlock and a seccomp filter on Linux (Landlock needs kernel 5.13 or newer), a restricted token, the lowest integrity level and a job object on Windows. When the helper starts it checks which limits are really in force and reports them to the game, and the game never shows more than that.

### What the player sees

The permission dialog for a native plugin says what the helper blocks, for example: "It runs in a restricted helper process: no network, cannot write files, cannot start programs, can only read its own folder." The Mods screen shows the same for every active plugin.

If your system cannot enforce all four limits, the dialog says "This plugin could not be restricted on this system" and names what is missing. The plugin then stays off until the player also presses **Run anyway** in a second, separate question. That answer is stored in `mods.json` for the exact content hash of the plugin, so an update asks again. On Windows the helper blocks file writes and new processes but cannot block the network or keep a plugin from reading other files, so Windows players are asked this second question.

### When a plugin misbehaves

If a gameplay hook or a peg type or Master callback takes longer than 1 second, or `on_frame` longer than 250 milliseconds, if the library crashes, or if the helper ends for any other reason, the game stops the helper, switches that plugin off and writes an error with the reason to the log (**F10**). The game keeps running. There is no second try.

To build the example yourself: `cmake --build build --target hello_native` puts the library and its `mod.ini` in `build/examples/mods/hello_native/`. Copy that folder to your `mods` folder.

## Network use: the scan and your OK

Before a mod loads, PegglePort reads its files:

* Lua: the `net` library, URLs, words such as `socket`, `tcp`, `curl` and `http`, and tricks like `load`, `_G[...]`, escaped or concatenated names (`"so" .. "cket"`).
* Native libraries (Windows, Linux and macOS formats): network libraries, imported network functions and embedded URLs.
* A manifest with `permissions = net`.

If it finds something, or the mod has native code, the game shows the findings and the mod does not load until you press **Allow...**. Until then even its data files stay unmounted.

This is a best-effort check. A native plugin can hide what it does from the scan, and the dialog says so. What protects you is the sandbox: without `permissions = net` a Lua mod has no network at all, and a native plugin runs in the restricted helper process that has no network whatever the scan found.
