# Mods

[Back to README](README.md) · [Modding guide](faq/modding.md) · [HD artwork guide](faq/hd-mod.md)

This catalog covers the mods provided by this repository: **1 official PegglePort mod, 7 bundled examples, and 4 locally generated packs**. The two HD editions are separate packs. Generated packs require your own source assets; the repository does not include copyrighted game artwork or level packs.

## Install and enable

1. Open **Options → Mods** in Classic 4:3, or **Mods** in widescreen, then select **Open Folder**. This opens the current game's `<save folder>/userdata/mods` directory.
2. Copy an example folder, a generated pack folder, or a mod `.zip` into that directory. Keep its `mod.ini` at the top of the mod folder.
3. Tick the mod in the Mods screen. If it needs permission, read the scan report and use **Allow...** only if you trust it. Changes apply at the main menu; return there if a level or online session is open.

The [installation guide](faq/modding.md#installing-and-turning-mods-on) explains load order, live reload and errors. Lua mods run in a sandbox. Native plugins always require approval and run in a separate helper; platforms that cannot enforce all restrictions require a second confirmation. The scan is not a guarantee of safety.

## Official PegglePort mods

### MCP Agent

| Mod / ID | Status | Purpose | Source / guide |
| --- | --- | --- | --- |
| **MCP Agent** (`mcp_agent`) | Official PegglePort mod; bundled; Python 3.10+ required for the companion | Opt-in AI control through the Model Context Protocol (MCP): inspect the board, aim, fire and operate flippers. | [Mod and bridge](examples/mods/mcp_agent) · [MCP setup guide](faq/mcp.md) |

MCP Agent uses local files and a stdio companion, not a network listener. Enable it, approve its player-control permission, open a single-player level and press **F8** to hand over control. Press F8 again to stop. The agent cannot approve itself, navigate menus, dismiss dialogs or control online play, Duel, Demo mode or replays. Normal pause and focus checks remain in force. Follow the setup guide to connect your MCP client; no Python packages are required.

## Bundled examples

These examples include their scripts and any example images they need. They still require a supported base game. Copy them from `examples/mods`; **Hello Native** must be built first unless your download includes its prebuilt folder.

| Mod / ID | Kind | What it does | Source |
| --- | --- | --- | --- |
| **Hello Lua** (`hello_lua`) | Lua gameplay | Demonstrates hooks, per-board state, random score bonuses, a combo style shot, an extra Flower Power effect, a menu button and a draw overlay. | [Folder](examples/mods/hello_lua) · [Manifest](examples/mods/hello_lua/mod.ini) |
| **Rainbow Pegs** (`rainbow_pegs`) | Lua cosmetic | Draws cycling rainbow rings around pegs without changing gameplay. | [Folder](examples/mods/rainbow_pegs) · [Manifest](examples/mods/rainbow_pegs/mod.ini) |
| **Trace Hooks** (`trace_hooks`) | Lua development aid | Logs gameplay hooks and their arguments without changing the board. Uses the default gameplay determinism mode, not cosmetic mode. | [Folder](examples/mods/trace_hooks) · [Manifest](examples/mods/trace_hooks/mod.ini) |
| **Moon Shots** (`moon_shots`) | Lua gameplay | Lowers gravity and adds a livelier bounce with slight drag. | [Folder](examples/mods/moon_shots) · [Manifest](examples/mods/moon_shots/mod.ini) |
| **Ice Pegs** (`ice_pegs`) | Lua gameplay | Adds two-hit ice pegs, indestructible iron pegs that score on hits, and gold pegs worth 200 base points. Applies them to selected blue pegs. | [Folder](examples/mods/ice_pegs) · [Manifest](examples/mods/ice_pegs/mod.ini) |
| **Frost Queen** (`frost_queen`) | Lua gameplay | Adds two Masters: Frost Queen with a portrait and green-peg power, and Gleam with an always-on score bonus. | [Folder](examples/mods/frost_queen) · [Manifest](examples/mods/frost_queen/mod.ini) |
| **Hello Native** (`hello_native`) | Native gameplay | C ABI example that counts peg hits, awards 250 bonus points every tenth hit, and draws a counter. Requires native-code approval. | [C source](examples/plugin/hello_native.c) · [Manifest template](examples/plugin/hello_native.ini) |

Build Hello Native with `cmake --build build --target hello_native`, then copy `build/examples/mods/hello_native` into the Mods folder. See [native plugins](faq/modding.md#native-plugins) for the API and platform restrictions.

For online play, all players need matching active gameplay mods, including Trace Hooks. Rainbow Pegs is cosmetic and is excluded from the gameplay-mod comparison. See [online determinism](faq/modding.md#keeping-online-matches-in-sync).

## Generated packs — bring your own assets

These are **not bundled ready-to-use mods**. The repository supplies generators, not the original game content. Generate each pack locally from your own legally acquired files, using a new output directory outside the source installation; HD staging and output must also be outside Git checkouts. Install the resulting mod folder, not the generator.

| Mod / ID | Host game | Contents and required assets | Generator / instructions |
| --- | --- | --- | --- |
| **Peggle Deluxe Native HD** (`peggle_native_hd_deluxe`) | Deluxe | Cosmetic data pack adapting higher-resolution iPad artwork to the PC sprite layout, with matched backgrounds and side art. Requires your Deluxe files and extracted iPad `Content.HD` artwork. | [HD importer](tools/import_hd_art.py) · [Reference exporter](tools/build_hd_mod.cpp) · [Build and install](faq/hd-mod.md#build-the-packs-locally) |
| **Peggle Nights Native HD** (`peggle_native_hd_nights`) | Nights | Separate cosmetic HD data pack for Nights, using your Nights files and extracted iPad `Content.HD` artwork. Unmatched textures and backgrounds stay stock. | [HD importer](tools/import_hd_art.py) · [Reference exporter](tools/build_hd_mod.cpp) · [Build and install](faq/hd-mod.md#build-the-packs-locally) |
| **Pego Prototype** (`pego_prototype`) | Deluxe | Adds 55 Quick Play boards from your August 2006 Pego prototype, retaining its base geometry and available backgrounds/thumbnails. Not the full prototype campaign. | [Python generator](examples/mods/build_pego_pack.py) · [Pego instructions](faq/modding.md#pego-prototype-pack) |
| **Peggle WoW Levels** (`wow_levels`) | Deluxe or Nights | Adds ten campaign boards plus the **1-Shot Kill** bonus layout from your World of Warcraft Edition files. Uses normal Quick Play rules; does not add the original campaign, music or challenge rules. | [Generator source](examples/mods/build_wow_pack.cpp) · [WoW instructions](faq/modding.md#world-of-warcraft-level-pack) |

For HD, enable `-DPP_HD_TOOLS=ON`, build `pp_build_hd_mod` and follow the artwork guide to export a PC reference and run the importer. Install only the matching edition's HD pack, and do not stack it with another texture pack.

For Pego, run `python examples/mods/build_pego_pack.py "path/to/Pego Deluxe" "path/to/pego_prototype"`. For WoW, build `pp_wow_pack` and run `pp_wow_pack "path/to/Peggle World of Warcraft Edition" "path/to/wow_levels"`.

**Widescreen is built in, not a mod.** The console menus and centered 16:9 gameplay layout do not belong in this catalog. Use the built-in **Mods → PS3 Resource Pack** option to import artwork locally from your own PS3 copy. See the [artwork guide](faq/hd-mod.md#console-menus-and-169-gameplay).

## Make your own

Use the [modding guide](faq/modding.md) for manifests, Lua hooks, native plugins, custom peg types and Masters. The [level editor](faq/editor.md) can export your own boards as mods; those user-created packs are not separate bundled catalog entries.
