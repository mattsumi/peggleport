# Online play guide

PegglePort plays Duel (2 players) and Peg Party (3 or 4 players, one board each with the same level and seed) over the internet or a local network. Each computer simulates every board. The game sends player inputs and compares board-state hashes after each shot to detect when the simulations disagree.

## Playing

1. In Classic 4:3, choose **Online** from the main menu. In the console layout, choose **Multiplayer → Online Duel** or **Online Peg Party**. Choose **Custom Match** to browse, **Quick Match** to try joining an open room on a level you have, or **Host Duel** / **Host Peg Party** to create one. If Quick Match finds no suitable room, it opens the custom room list. The **Multiplayer** menu also has **Join With Code**.
2. LAN/local and global rooms appear together, with a source label on each row. **Refresh** checks both again. Back cancels a search, and local rooms remain usable if the global lobby is unavailable. There is no server-address field to fill in.
3. **To join**, select a room and confirm (or double-click it), enter its five-character code, or use **Paste Invite** with the invite on your clipboard. **To host**, pick the mode, level and Master, then confirm with **Open Room** or **Host Match**. In Classic 4:3, start with **Create Room**. The hosting destination defaults to the global lobby or your configured custom lobby. If a local lobby has answered, **Destination** lets you choose it. Masters added by mods are listed after the original ones.
4. Guests press **Ready**. Once all seats are filled and ready, the host presses **Start Match** or **Start Game**. **Copy Invite** puts the room link on the clipboard.
5. Playing: aim with the mouse and click (or press Space or Enter) on your turn. Esc opens the leave menu. In a Peg Party the two boxes at the top list everybody with their scores, yours first and marked YOU. The log (F10) says who you are, whether you are on a direct link (`Direct UDP link (23 ms)`) or relayed through the lobby, how many shots the host has verified, whose turn it is in a Duel, and rollbacks.
6. When the match ends, **Rematch** needs everybody to ask (the host picks a new seed), or choose **Leave**.

A disconnect or a mismatch puts a clear message on the screen instead of silently carrying on. Losing the lobby ends the match for everybody, even with a working direct link. The message names the player or host whose connection was lost when that is known; an unknown network or server failure is not blamed on a player. A player who is no longer reachable may only see a connection-loss message.

Use the same PegglePort version as the host. A guest whose level files or active gameplay mods differ is turned away in the waiting room with a message that says so. If the host picked a Master added by a mod that a guest does not have, the message names the missing Master.

## Invite links

An invite link includes the lobby address and room code. Native links such as `peggleport://join?lobby=HOST:PORT&code=XXXXX` still work, including IPv6 addresses in brackets. Secure links carry the whole `wss://` address with its punctuation escaped. Use **Copy Invite** and **Paste Invite** to keep it intact. Opening a link selects that exact destination on the existing Online screen. Press **Join** to enter the room. Browsing an invite does not change your saved hosting destination. Clicking a room also retains its server, even if another server has the same five-character room code. A manually typed code searches the discovered sources, including the public lobby and the local computer; use an invite or select a row to disambiguate duplicate codes.

### Custom and private lobbies

The global lobby is `wss://peggleport-lobby.konekurosawa.org/lobby`, using encrypted WebSockets on port 443. Advanced users can set `lobby=host:port` and `lobbyset=1` in `online.ini`, beside the game's `userdata` directory, or launch the game with the `PP_LOBBY` environment variable. Native `host`, `host:port` and `[IPv6]:port` addresses and secure `wss://` addresses are accepted. This sets the default hosting destination and adds the custom server to the browser; it never replaces the global query. An invite can reach a private server without changing either setting.

To make links open the game:

* **Windows and Linux**: press **Register invite links** on **Online** in Classic 4:3 or **Multiplayer → Join With Code** in the console layout. It registers the handler for your account only, in the Windows registry or with a Linux desktop entry. No administrator rights are needed. You can still paste an invite without registering it.
* **macOS**: the app already announces the link type. Open `PegglePort.app` once and macOS remembers it.

## Direct links and the relay

From **Online** in Classic 4:3 or **Multiplayer → Join With Code** in the console layout, **Test connection** checks the configured lobby, not the currently selected room. For the public secure lobby it requests the room list. That lobby relays match traffic rather than using native direct UDP; no router changes are needed.

On native custom servers, players first talk through the TCP lobby. Their computers also try to open a direct UDP connection with each other, which has less delay and spares the server. If that fails for any reason, you stay on the relay and the match is not interrupted. The waiting room tells you why a direct link did not happen, and the log (F10) keeps the full reason.

**Test connection** for a native custom server checks that the lobby answers over UDP and tells you what kind of router you are behind. Routers that give every destination a different port ("symmetric") can not do direct links, and those players stay on the relay.

## Running a lobby server

`pp_lobbyd` is in the download. It does not need the game files. Use the same current version of PegglePort and pp_lobbyd on every computer; older versions cannot join these matches.

```
pp_lobbyd [port] [--no-udp] [--aux-offset N] [--proxy-local]
```

The port defaults to 7770. It uses:

* **TCP on the port**: the room list and the relay. Always needed.
* **UDP on the same port**: LAN discovery and direct-link rendezvous. Use `--no-udp` to run a TCP-only relay.
* **UDP on port+1**: the optional router type check. `--aux-offset N` changes the offset.

The browser sends one IPv4 broadcast and one loopback discovery request to UDP 7770 per refresh, listens briefly, then queries the responding servers over TCP. Allow UDP and TCP 7770 through the server's local firewall for automatic LAN discovery. Broadcasts normally stay on the current subnet; a lobby on another subnet or a nondefault port can be added through the custom setting or an invite. Discovered replies carry the server's actual listening port. If UDP is disabled or blocked, the browser still checks TCP `127.0.0.1:7770`, and configured servers and invites still work through the relay.

**On a Linux server with systemd** (as a normal user):

```sh
mkdir -p ~/peggleport-lobby ~/.config/systemd/user
cp pp_lobbyd ~/peggleport-lobby/
cp deploy/pp-lobbyd.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now pp-lobbyd
loginctl enable-linger $USER
```

The service file uses port 7770. Edit `ExecStart` to change it. `journalctl --user -u pp-lobbyd` shows the log, including a counter line every 10 minutes.

**To make it reachable from the internet:**

1. Open or forward **TCP 7770** for the room list and relay.
2. For direct UDP, also open or forward **UDP 7770**. Open or forward **UDP 7771** if you want the default router type check. Adjust these numbers if you changed the listening port or `--aux-offset`.
3. From another network, open a room invite or look for a room from the configured server. **Test connection** on a native server checks UDP, not TCP-only relay reachability.

### Secure relay bridge

For an HTTP tunnel, I use the bridge in `server/lobby_bridge.py` with Python 3.9 or newer and the pinned package in `server/requirements.txt`:

```sh
python3 -m venv lobby-env
lobby-env/bin/python -m pip install -r server/requirements.txt
pp_lobbyd 7770 --proxy-local
lobby-env/bin/python server/lobby_bridge.py
```

The bridge only listens on `127.0.0.1:7772`, accepts WebSockets at `/lobby`, and forwards to the fixed TCP backend `127.0.0.1:7770`. Put the HTTPS tunnel in front of that loopback HTTP origin. Keep the origin port private. The tunnel must supply the real `CF-Connecting-IP` header; missing, duplicate or invalid source addresses are refused. Do not rewrite that header or enable a setting that replaces IPv6 visitor addresses with synthetic IPv4 addresses. Connections through the bridge use the relay without UDP setup.

`--proxy-local` is off by default. Enable it only on the backend used by the local bridge. It trusts a single initial PROXY-v1 header from actual loopback peers, so other local programs share that trust. The resolved source address keeps the native connection and room limits, including the existing IPv6 prefix buckets. Native remote connections cannot supply this header.

`GET /health` reports only that the bridge is ready. Its response explicitly says the backend was not checked. A successful WebSocket room list checks the backend too.

### Notes on safety

* Every UDP request is at least 64 bytes and every answer is shorter than its request, so the server cannot be used to amplify traffic.
* LAN discovery replies echo the request nonce and share the rendezvous rate limits. Each browse pass caps discovery packets and queried endpoints; it does not continuously retry broadcasts.
* A player's address is only recorded after a cookie tied to the source address proves it, so nobody can plant a fake address for another player.
* Direct links need a secret that the lobby hands over TCP to exactly those two players. Packets are authenticated with it, and packets from anybody else are dropped.
* Request rates are limited per address and overall, and the number of tracked addresses and live pairs is capped.
* Hash checks accept reports only from assigned players. Each player can report once per board and shot, in order. Up to 4,096 outstanding local checks and 4,096 outstanding remote check keys are retained, with at most one report per remote seat at each key. Completed checks are released and rematches reset the counters. This bounds a stalled or flooding peer without limiting the total shots in a match. Invalid or excessive reports end the session with the affected player’s name.
* Native lobby traffic over TCP is not encrypted. Somebody who can read it learns the pair secret. Secure lobby connections verify the server certificate against this computer’s trusted roots and encrypt traffic as far as the HTTPS tunnel endpoint. The lobby and its bridge can still read relayed traffic.

## Discord (optional)

**Discord activity** is off by default. Find it on **Online** in Classic 4:3 or **Multiplayer → Join With Code** in the console layout. Turn it on to share your current activity with the Discord desktop app. Turning it off clears the activity and closes the connection.

A host waiting in a room with an open seat can share that room through Discord. Full rooms and matches have no join link. While PegglePort is running, an incoming invitation fills in the existing Online screen; press **Join** to connect. It does not interrupt a level or another room. An unanswered Discord invitation expires after five seconds. Very long lobby addresses can still use **Copy Invite** when they do not fit Discord’s limit.

## Steam (optional)

I keep Steam optional. Builds made with the Steamworks SDK (`cmake -DPP_STEAM=ON -DSTEAMWORKS_SDK_DIR=/path/to/sdk ...`) can use Steam presence, friend invitations, lobbies and peer networking when Steam is available. Without Steam, singleplayer and the usual online connections still work. PegglePort does not start Steam.

An open host room can share its existing invitation through Steam. Full rooms and matches have no join link. Incoming friend invitations and lobby requests fill in the existing Online screen; press **Join** to connect. They do not interrupt a level or another room, replace a destination you selected, or override a Discord invitation already on screen. A Steam request waiting behind a level, room or dialog expires after five seconds.

Compatible players can use Steam for their match connection after confirming their identities through the room connection and joining the same PegglePort lobby. If this cannot be set up, the usual direct or lobby connection remains available. The public lobby connection must stay healthy throughout the match, including when match traffic uses Steam.

Development uses App ID 480 (Spacewar). A `steam_appid.txt` containing `480` belongs next to the program for that setup. Steam may identify it as Spacewar; I do not rely on custom Steam presence translations for this ID. The SDK stays separate from this project. Steam-enabled builds need the appropriate Steam API runtime from the SDK's `redistributable_bin` directory under Valve's licence.
