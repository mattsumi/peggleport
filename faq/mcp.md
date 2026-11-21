# Let an AI agent play a local level

[Mod catalog](../MODS.md) · [Modding guide](modding.md)

MCP Agent is an official PegglePort mod that lets an MCP-capable AI client observe a board, aim, fire and operate flippers. The player chooses the level and explicitly hands over control. The companion is a Python 3.10+ script using only the standard library: no packages to install, no sockets, no HTTP endpoint and no network listener. The client launches it over stdio; the companion and game exchange local JSON files.

This is optional local single-player control, not an online service. It does not navigate menus, select a profile or level, dismiss dialogs, start the next level or operate replays. Online, multiplayer/duel, demo and replay/viewer control are unavailable. Pause, modal dialogs, transitions and loss of game-window focus block input. The game checks these conditions again when it receives each command.

## Install and grant permission

1. Use a PegglePort build with the `control` Lua API and `on_control` hook. Install Python 3.10 or newer separately if needed.
2. In the game's Mods screen, select **Open Folder**. Copy the entire `examples/mods/mcp_agent` folder into that `userdata/mods` directory, keeping `mod.ini`, `main.lua` and `server.py` together.
3. At the main menu, enable **MCP Agent**. Read its permission request and choose **Allow...** only if you trust the mod and the client. The manifest requests `permissions = control, fs`: control of eligible local gameplay and files in the mod's private data directory. It does not request network permission. Updating mod files can require approval again.
4. Manually open a single-player level. Keep the game focused, unpaused and free of dialogs. Press **F8** in the game to arm control; press **F8** again to revoke it. An AI tool cannot arm itself.
5. Configure one MCP client as below, connect it, and ask it to call `peggle_observe` first. A usable board reports `armed: true` and `available: true`. If not, inspect `reason` and fix the condition in the game.

Enabling the mod is not the same as arming control. Keep the game in the foreground while using a separately running AI client. Returning to a chat window can make control unavailable until you focus the game again. To stop, press F8, disable the mod, or close the MCP client.

## Configure your MCP client

Use your client's local **stdio server** configuration. The following is the common `mcpServers` JSON shape; individual clients may use a different wrapper or a setup form. Replace all three absolute paths with paths on your machine. Forward slashes work in Windows JSON paths and avoid backslash escaping.

```json
{
  "mcpServers": {
    "peggleport": {
      "command": "C:/Python312/python.exe",
      "args": [
        "-u",
        "C:/Games/PegglePort/userdata/mods/mcp_agent/server.py",
        "--data-dir",
        "C:/Games/PegglePort/userdata/mod-data/mcp_agent"
      ]
    }
  }
}
```

`command` is the absolute Python executable, not a shell command containing arguments. `server.py` is the installed script. `--data-dir` must be an explicit absolute path to the **same save folder's** `userdata/mod-data/mcp_agent`, not the mod's installation folder and not another edition's userdata. PegglePort's Mods **Open Folder** button identifies the matching `userdata/mods`; the data directory is its sibling under `userdata/mod-data/mcp_agent`. Save data need not live beside the executable.

On macOS or Linux, use absolute paths such as `/usr/bin/python3` and your actual save directory. The companion can create the specified data directory, but cannot produce game state until the enabled mod runs. Let the MCP client launch the script; do not also start a second copy in a terminal. No Hermes or other client configuration is changed by the script.

## Tools

| Tool | Arguments | Result / restrictions |
|---|---|---|
| `peggle_observe` | `{}` | Reads the latest board snapshot; does not require arming. |
| `peggle_aim` | `{"x": 320, "y": 400}` | Aims at integer board coordinates. Requires armed, available gameplay; the game must be in the aiming phase. |
| `peggle_fire` | `{}` or `{"x": 320, "y": 400}` | Fires once using the current aim, or aims and fires once. Supply both coordinates or neither. |
| `peggle_flippers` | `{"pressed": true}` or `{"pressed": false}` | Presses or releases flippers when that power is available during a live shot. |
| `peggle_wait` | `{"seconds": 0.5}` | Sleeps for 0–10 real seconds, then reads a fresh snapshot. Does not require arming, send an input, unpause or fast-forward the game. |

Coordinates are logical board pixels with the origin at the top left, not desktop/window pixels. Use the snapshot's `width` and `height`: `0 <= x <= width` and `0 <= y <= height`. Fractional, non-finite, boolean and out-of-bounds coordinates are rejected; no rounding or clamping occurs. Snapshot dimensions must be finite numbers between 1 and 32768. Unexpected or missing tool arguments are rejected.

Snapshots contain `session`, `armed`, `pending_fire`, `available`, `reason`, `width`, `height`, `tick`, `phase`, `score`, `balls_left`, `orange_left`, `aim_x`, `aim_y`, `pegs`, `balls` and game-provided details such as cannon or bucket positions. `phase` is `aim`, `shot`, `popping` or `over`. `pending_fire` identifies an accepted agent click waiting for launch. Without a loaded level there is no fresh snapshot. Read the actual returned data rather than assuming a fixed board layout or peg count.

Peg `type` is numeric: **1 blue, 2 orange, 3 purple, 4 green**. Clear the orange pegs to win. Each peg includes its `id`, position, `lit`, `gone`, shape, radius and rotation. Gone or lit pegs are not new targets. Ball positions and velocities use the same board coordinates.

Observe and wait return the snapshot object as JSON text. Write tools return the **actual game response**, not an acknowledgement that a file was written. Game rejection and local bridge failures use `isError: true`; their JSON text contains `ok: false` and an error. Protocol mistakes, unknown tools and non-object argument envelopes instead produce JSON-RPC errors. On supported protocol versions, `structuredContent` duplicates the same JSON object.

## Practical prompt

> Play the single-player PegglePort level I have opened and armed with F8. Use only the peggle tools. Observe first, check armed/available/reason and phase, then choose integer board coordinates from the current snapshot. Aim and fire only while aiming. After one shot, wait in short intervals and observe until the next aiming phase or the level ends. Use flippers only if available, releasing them after use. Do not spam commands, navigate menus, edit bridge files, change settings or attempt online play. If control is disarmed or blocked, ask me to resolve it. If a command times out, observe before making any decision and never automatically repeat the shot. Stop when the level is over and report the final score and remaining orange pegs.

A held flipper input has a short game-side lease; it is not an indefinite key hold. Use fresh observations and bounded tool calls rather than assuming a button remains held throughout a long wait.

A successful fire response means the normal click was recorded, not that a ball has already launched. The cannon has its usual short delay, so the immediate snapshot can still report `phase: "aim"`. Observe until the shot starts; do not send another fire to hurry it. F8 revocation or a blocked control state cancels an agent-owned shot that has not launched yet. A ball already in flight continues normally. Physical player clicks remain independent of AI control.

Cancellation is recorded too, so replays and saved sessions reconstruct the same result. Recordings containing a canceled AI shot use format 5 and require this build or newer to read. Recordings without cancellation keep their previous format.

## Local-file contract and safeguards

The companion exclusively creates `bridge.lock` in `--data-dir` and keeps ownership for its entire process lifetime, including observe/wait calls. A second companion for that directory exits with an error. Calls are processed serially, so there is at most one outstanding command. The owned lock is removed on orderly stdin shutdown or handled termination; a forced kill or crash can leave it behind.

`state.json` must be a UTF-8 JSON object no larger than 1 MiB. Its file modification time must be no more than two seconds old and not in the future. Freshness is based on **mtime**, not a timestamp claimed inside JSON. Snapshot reads retry for at most 50 ms to cover a concurrent refresh, Windows sharing conflicts or a submillisecond clock difference; no command has been sent at this stage. Data that remains missing, stale, malformed, duplicate-key, future-dated or non-finite fails closed. Every write action reads a fresh snapshot and requires literal boolean `armed: true`, `available: true` and a nonempty string session. The game remains the authority for permissions, session identity, request expiry and whether a particular action is legal now.

For a write, the companion writes a temporary sibling `request.tmp`, closes it, then atomically publishes it with `os.replace` as `request.json`. The request has this shape:

```json
{
  "id": "0123456789abcdef0123456789abcdef",
  "session": "the-session-from-state",
  "expires": 1800000002.0,
  "action": "aim",
  "x": 320,
  "y": 400
}
```

The example ID, session and expiry above are illustrative, not reusable values. Each real request gets a random UUID hex string, the freshly read session and `expires = time.time() + 2` in Unix seconds. Actions are `aim`, `fire` and `flippers`; fire may omit both `x` and `y`, and flippers uses boolean `pressed` instead of coordinates. Observe and wait never publish requests. An existing `request.json` is not overwritten, even if it appears expired.

The Lua `on_control` UI hook consumes `request.json`, sends its JSON through `control.command`, writes `response.json` and refreshes `state.json` through `control.state`. The companion polls for a response matching its random request ID, ignoring unrelated IDs and incomplete JSON while waiting. A matching response must be an object with boolean `ok`, optional string `error` and optional object `state`:

```json
{
  "id": "0123456789abcdef0123456789abcdef",
  "ok": false,
  "error": "not_aiming"
}
```

The response deadline is two seconds using a monotonic clock. On completion or timeout, the companion removes `request.json` only if its current ID still matches the companion's request. It never deletes a replacement request, and it never automatically retries a destructive action. **A timeout does not prove that a shot failed to fire.** Observe before deciding what to do next.

Local files are not an authentication boundary against other processes running as your user. Use a private save directory, trust the client and the installed script, and do not point the bridge at shared or untrusted data. The Python companion itself is an ordinary local process, not part of the Lua sandbox.

## Troubleshooting

- **`bridge.lock already exists`:** close every MCP client and companion using this data directory. Only after confirming they are closed, manually delete the stale `bridge.lock` and reconnect. Do not delete a live lock or run multiple clients against the same directory.
- **A request is already outstanding:** do not overwrite it to force another shot. Close the client, revoke control with F8 and let the game reject/consume any expired request. For manual recovery, close all clients and the game first, then remove stale `request.json`/`request.tmp` and the stale lock before restarting.
- **Missing/stale `state.json`:** verify the absolute data path, enabled mod and granted `control, fs` permissions. Open a local level and keep the game running. Do not change file timestamps to bypass freshness checks. A transient parse error during a state refresh can be handled by another observe call, never by replaying a fire command.
- **Disarmed/unavailable:** press F8 yourself in an eligible level, focus the game, unpause and dismiss dialogs. Online, duel, demo and replay modes cannot be enabled through these tools.
- **Game rejects aim/fire/flippers:** inspect the returned error and current phase. Fire only from `aim`; flippers require the power and a live shot. A valid coordinate is not a promise that a shot is currently legal.
- **Response timeout:** stop issuing actions and inspect fresh state. Confirm the game is processing UI ticks. A late response is not permission to fire again automatically.
- **No tools or handshake failure:** ensure the client uses stdio and supports one of the legacy versions below. An idle script waiting on stdin does not print a banner; stdout is reserved for protocol messages, and startup errors go to stderr.

## Protocol compatibility and verification

This companion implements the legacy newline-delimited JSON-RPC stdio lifecycle for **2025-11-25, 2025-06-18, 2025-03-26 and 2024-11-05**. Its method subset is `initialize`, `notifications/initialized`, `ping`, `tools/list` and `tools/call`. It advertises tools only, emits no list-change notifications, and does not implement HTTP, resources, prompts, sampling, tasks, JSON-RPC batches or newer protocol revisions. The fixed tool list is returned in one page; cursors are not accepted. Other notifications, including cancellation, are ignored without a response or game action; serial calls finish within their bounded wait/response deadline rather than being interrupted. Unknown requested versions are answered with `2025-11-25`; the client must accept that version or disconnect. This is not a claim to implement the latest MCP specification.

The implementation follows the official [stdio transport][1] and [initialization/version negotiation][2] descriptions. JSON tool text is always returned; `structuredContent` is included only for 2025-06-18 and 2025-11-25, following the [2025-06-18 tools format][3] rather than sending it to [older tools clients][4].

Run the dependency-free companion suite from the repository root with a scratch directory selected through `TMPDIR`:

```bash
TMPDIR="/absolute/path/to/scratch" python3 -m unittest discover -s tests -p test_mcp_server.py -v
```

Use an absolute writable scratch path appropriate to your machine. These tests launch real stdio subprocesses and exercise a temporary file bridge, including handshake versions, validation, expiry/timeout behavior, matching responses, serialization and lock ownership. They emulate game-side state/responses; they do not replace an in-game single-player playthrough or the C++/Lua control tests.

[1]: https://modelcontextprotocol.io/specification/2025-11-25/basic/transports
[2]: https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle
[3]: https://modelcontextprotocol.io/specification/2025-06-18/server/tools
[4]: https://modelcontextprotocol.io/specification/2025-03-26/server/tools
