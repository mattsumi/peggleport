# Level editor guide

The level editor is part of the game and uses the game's own renderer, so the board looks exactly like it does when you play. You can open any level of the game or of a mod, place pegs, bricks, polygons, lines, holes and movers, play the level right away, and save it as a mod that shows up in Quick Play.

Open it from **Options, then Level Editor**, or press **F9** on the main menu. It starts with a new level; use **Open** to load an existing one.

## The screen

| Area | What is there |
|---|---|
| Top bar | New, Open (a level browser with a filter), Save, To mod, Export and Import (JSON), Undo, Redo, Grid, Snap, Fit, Animate, Oranges, Check, Test play, Exit |
| Left palette | Tools (Select, Pan), pegs (blue, orange, green, purple, round with a radius, bumper), bricks (straight, curved), shapes (polygon, line, hole) and arrange (flip, rotate, duplicate, delete, copy, paste, align, spread) |
| Canvas | The board, zoomed and panned. The playfield outline is white. On a wide board the original 650 pixel region is marked with a dashed gold guide |
| Right panel | **Object** (all properties of the selection), **Mover**, **Level** (name, title, art, board width, balls, orange target, mod name), **Gen** (pattern generators) and **Checks** (click a line to jump to the object) |
| Bottom bar | Cursor position, zoom, grid, selection, saved or modified, and the last message |

## Keys and mouse

| Input | Action |
|---|---|
| V / H | Select tool / Pan tool (middle drag, or Space and drag, also pans) |
| 1 2 3 4 5 6 | Blue, orange, green, purple, round peg, bumper |
| B / K | Straight brick / curved brick |
| P / L / O / X | Polygon (click points, close with a click on the first point, Enter or a double click), line (drag), hole, hole exit (select a hole, then click where the ball should come out) |
| Click, Shift-click, drag a box | Select, add to the selection, box select. Alt-drag copies |
| Drag | Move the selection. The round handle above it rotates (15 degree steps while Snap is on) |
| Arrow keys (Shift = one grid step) | Nudge |
| Delete or Backspace | Delete |
| Ctrl/Cmd + Z, Shift+Z, Y | Undo, redo. Every operation is one step, a drag is one step |
| Ctrl/Cmd + C, X, V, D, A | Copy, cut, paste, duplicate, select all |
| Ctrl/Cmd + S, Shift+S, N, O | Save, save to mod, new, open |
| M / Shift+M | Mirror horizontally / vertically (the selection, or the whole level when nothing is selected) |
| R / Shift+R | Rotate by +15 / -15 degrees |
| G, S | Show the grid, snap to the grid |
| F, + and -, wheel | Fit the view, zoom |
| A | Animate: runs the movers, the orange line is the path of the selected object |
| E | Random orange preview: shows the pegs the game would pick with a seed. Press again for another seed |
| T or F5 | Test play |
| F6 | Run the checks |
| Esc | Cancel what you are doing, leave the tool, clear the selection |
| Right click | Cancel. With nothing to cancel it returns to Select |

The `:` key opens a one line command box for typed commands, which some people prefer for exact placement. Everything it does is also available with the mouse.

## What things are

* **Pegs** are circles with a peg block. Blue and orange are saved as "variable" pegs like the shipped levels: the game picks the real oranges at the start of every game, and the colour you see in the editor is only a preview. Green and purple are fixed.
* **Mod peg types**: select a peg and type a name into the **Custom type** field of the Object tab, for example `ice` or `mymod:ice`. **To mod** saves it as a `peg` line in the level's `.ini`, and the game gives the peg that type when the mod that defines it is on. The editor still draws the peg as a normal one, and opening a level with `peg` lines keeps them. The modding guide has the details.
* **Round and bumper**: any radius. A bumper has no peg properties.
* **Bricks** have all the brick settings of the game (type, subdivisions, radius or length, sweep, end angles, thickness, rotation) and can also be a peg.
* **Polygons** are collision shapes and are drawn as outlines. New ones are oriented like the shipped ones so "keep out" faces the right way. **Lines** are single walls. **Holes** have a score, an out delay and a size.
* **Movers**: pick a type in the Mover tab. All 15 types are there with radius, period, time offset, two pauses, phase, path rotation, rotation and reverse. When you add a mover the object stays where it is.
* The level files use the classic 650 by 560 coordinates. On a wide board the game centres the level, so the playfield reaches past 0 and 650 on both sides. The editor draws exactly that.

Properties the editor has no controls for are kept untouched, so editing one peg of a shipped level only changes that peg. Loading and saving a shipped level without changes writes the identical file.

## Saving and playing your level

* **Save** writes the level to where it came from, or into your mod folder when it is new.
* **To mod** (Ctrl/Cmd+Shift+S) writes
  * `mods/<modname>/levels/<Level>.dat`,
  * `<Level>.ini` next to it with `title`, `board_width`, `balls`, `orange`, `background` and `master`, and a `peg` line for every mod peg type you set (see Mod peg types),
  * `levels/mod_levels.cfg`, which makes a Quick Play stage for every five levels,
  * a `mod.ini` when the folder has none, and a 200 by 177 thumbnail in `levels/cached_thumbnails/` for the Quick Play tile.
* New levels use the file version that both games load. A loaded level keeps its own version, so a Nights-only level only loads in Nights.
* Level names may use letters, digits, `_` and `-`. A name that starts with `_` is treated as a base layout without walls and a bucket, and the checker tells you about it.
* A new level uses the background art of an existing one (the editor does not draw art). Choose it in the Level tab.
* To play it: tick the mod in **Options, then Mods** and restart. In **Quick Play**, press the **left arrow on the first stage** to reach the mod's stages. The editor also mounts the mod into the running game, so levels you save in a session show up without a restart.

**Export and Import** write and read `exports/<Level>.json` in your save folder: a readable file of the whole level. Numbers keep their exact value, so exporting and importing changes nothing.

## Test play

**T**, **F5** or the Test play button plays the level you are editing with the real rules and the normal HUD. Aim with the mouse and click to shoot, **R** restarts, **G** toggles the guide, and **Esc** goes back to the editor with everything as it was. Test play does not touch your profile or high scores.

## Checks

**F6**, the Check button and every save run the checks.

* **Errors**: no objects, no pegs, no peg that can turn orange, no bucket, an object completely outside the playfield, an invalid level name.
* **Warnings**: fewer possible orange pegs than the orange target (the original rules want 25), pegs that overlap each other or a wall, polygon or line, pegs in the side walls or in the bucket zone, pegs that stick out of the playfield, duplicate object ids, movers that do nothing.
* **Info**: peg counts, where the bucket comes from, objects outside the original 650 pixel region on a wide board, and entries the editor keeps but does not show.

Markers on the canvas show the objects a message is about.

## Generators and arranging

The Gen tab makes a grid (columns, rows, spacing, origin), a circle, a spiral or a sine row of blue or orange pegs, as one undo step. Arrange can flip, rotate, align (left, centre, right, top, middle, bottom) and spread objects evenly.

## What it cannot do yet

* Peggle Nights emitters and bare physics objects are kept in the file but neither drawn nor editable.
* Movers that move another mover's anchor are kept but not editable. Several objects sharing one mover path can only be made through an imported JSON file.
* You cannot draw level art.
* After a polygon is placed you can move and rotate it as a whole, but not edit single corners.
