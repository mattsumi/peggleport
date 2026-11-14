# Online play guide

PegglePort plays Duel (2 players) and Peg Party (3 or 4 players, one board each, same level and same seed) over the internet. Every player's computer simulates every board, and the only thing that travels over the network is the shot: the angle and the moment it was fired. After every shot the host compares the state of all boards and tells everybody if the games ever drift apart.

All of it is done from the game. The only thing that needs a command line is the lobby server, because it is a separate program for people who host.

## Playing

1. On the main menu press **Online** (bottom right).
2. Type the address of a lobby server (`host` or `host:port`, the default port is 7770). The game remembers it. The open rooms show up in the list.
3. **To join**: click a room (a double click joins it), or type its 5-character code, or press **Paste Invite** with an invite link on your clipboard.
   **To host**: press **Create Room**, choose Duel or Peg Party with 3 or 4 players, the level and your Master, then **Open Room**. Masters added by mods are listed after the original ones.
4. In the waiting room everybody presses **Ready** and the host presses **Start Match**. **Copy Invite** puts a link on your clipboard that your friends can open.
5. Playing: aim with the mouse and click (or press Space or Enter) on your turn. Esc opens the leave menu. In a Peg Party the two boxes at the top list everybody with their scores, yours first and marked YOU. The log (F10) says who you are, whether you are on a direct link (`Direct UDP link (23 ms)`) or relayed through the lobby, how many shots the host has verified, whose turn it is in a Duel, and rollbacks.
6. When the match ends, **Rematch** needs everybody to ask (the host picks a new seed), or choose **Leave**.

A disconnect or a mismatch puts a clear message on the screen instead of silently carrying on. Losing the lobby ends the match for everybody, even with a working direct link. The message names the player or host whose connection was lost when that is known; an unknown network or server failure is not blamed on a player. A player who is no longer reachable may only see a connection-loss message.

Everybody needs the same game data. A guest whose level files or active gameplay mods differ is turned away in the waiting room with a message that says so. A guest who does not have the mod that adds the Master you picked is told which Master is missing.

## Invite links

An invite link looks like `peggleport://join?lobby=HOST:PORT&code=XXXXX`. When you open one, PegglePort starts (or comes to the front) with the Online screen filled in. Nothing connects until you press **Join**, so a link from a stranger cannot make your game talk to some server on its own.

To make links open the game:

* **Windows and Linux**: press **Register invite links** on the Online screen. On Windows this writes a key in the current user's registry, on Linux a desktop entry in `~/.local/share/applications`. No administrator rights are needed.
* **macOS**: the app already announces the link type. Open `PegglePort.app` once and macOS remembers it.

## Direct links and the relay

Players first talk through the lobby, which always works and needs no router settings. At the same time their computers try to open a direct UDP connection with each other, which has less delay and spares the server. If that fails for any reason, you stay on the relay and the match is not interrupted. The waiting room tells you why a direct link did not happen, and the log (F10) keeps the full reason.

**Test connection** on the Online screen checks that the lobby answers over UDP and tells you what kind of router you are behind. Routers that give every destination a different port ("symmetric") can not do direct links, and those players stay on the relay.

## Running a lobby server

`pp_lobbyd` is in the download. It does not need the game files. Use the same current version of PegglePort and pp_lobbyd on every computer; older versions cannot join these matches.

```
pp_lobbyd [port] [--no-udp] [--aux-offset N]
```

The port defaults to 7770. It uses:

* **TCP on the port**: the room list and the relay. Always needed.
* **UDP on the same port number**: lets players find each other for direct links.
* **UDP on port+1**: the router type check. Optional.

If UDP is blocked, rooms still work through the relay.

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

1. Open the ports in the server's firewall, for example `sudo ufw allow 7770/tcp && sudo ufw allow 7770/udp && sudo ufw allow 7771/udp`.
2. Forward TCP 7770, UDP 7770 and UDP 7771 on your router to the server.
3. From another network, enter the server's address on the Online screen and press **Test connection**.

### Notes on safety

* Every UDP request is at least 64 bytes and every answer is shorter than its request, so the server cannot be used to amplify traffic.
* A player's address is only recorded after a cookie tied to the source address proves it, so nobody can plant a fake address for another player.
* Direct links need a secret that the lobby hands over TCP to exactly those two players. Packets are authenticated with it, and packets from anybody else are dropped.
* Request rates are limited per address and overall, and the number of tracked addresses and live pairs is capped.
* The lobby traffic over TCP is not encrypted. Somebody who can read it learns the pair secret. The lobby is a convenience relay, not a private channel.

## Steam (optional)

If you build PegglePort yourself with the Steamworks SDK (`cmake -DPP_STEAM=ON -DSTEAMWORKS_SDK_DIR=/path/to/sdk ...`), your Steam status shows "In a PegglePort room" with a **Join Game** entry, and a click from a friend arrives as the same invite link. The SDK is not part of this project and can not be redistributed, so the downloads do not have this. For development the code uses Steam's test App ID 480 (Spacewar): put a `steam_appid.txt` containing `480` next to the program with Steam running.
