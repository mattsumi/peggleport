# Level editor guide

The level editor is part of the game and uses the game's own renderer, so the board looks exactly like it does when you play. You can open any level of the game or of a mod, place pegs, bricks, polygons, lines, holes and movers, play the level right away, and save it as a mod that shows up in Quick Play.

Open it from **Options, then Level Editor**, or press **F9** on the main menu. It starts with a new level; use **Open** to load an existing one.

## The screen

| Area | What is there |
|---|---|
| Top bar | New, Open (a level browser with a filter), Save, To mod, Export and Import (JSON), Undo, Redo, Grid, Snap, Fit, Animate, Oranges, Check, Test play, Exit |
| Left palette | Tools (Select, Pan), pegs (blue, orange, green, purple, round with a radius, bumper), bricks (straight, curved), shapes (polygon, line, hole), effects (emitter) and arrange (flip, rotate, duplicate, delete, copy, paste, align, spread) |
| Canvas | The board, zoomed and panned. The playfield outline is white. On a wide board the original 650 pixel region is marked with a dashed gold guide |
| Right panel | **Object** (all properties of the selection), **Mover**, **Level** (name, title, stock or imported background, balls, orange target, physics, mod name), **Gen** (pattern generators) and **Checks** (click a line to jump to the object) |
| Bottom bar | Cursor position, zoom, grid, selection, saved or modified, and the last message |

## Keys and mouse

| Input | Action |
|---|---|
| V / H | Select tool / Pan tool (middle drag, or Space and drag, also pans) |
| 1 2 3 4 5 6 | Blue, orange, green, purple, round peg, bumper |
| B / K | Straight brick / curved brick |
| P / L / O / X | Polygon (click points, close with a click on the first point, Enter or a double click), line (drag), hole, hole exit (select a hole, then click where the ball should come out) |
| N | Emitter (click to place one) |
| J | Follow object: with the objects that should follow selected, click the object they should follow |
| Click, Shift-click, drag a box | Select, add to the selection, box select. Alt-drag copies |
| Drag | Move the selection. The round handle above it rotates (15 degree steps while Snap is on) |
| Drag a corner | With one polygon selected, its corners show as small squares. Drag one to move only that corner. It snaps to the grid like everything else |
| Double-click an edge | Adds a corner on that edge of the selected polygon |
| Delete or Backspace on a chosen corner | Removes the corner. A polygon keeps at least 3 |
| Arrow keys (Shift = one grid step) | Nudge. With a corner chosen it nudges just that corner |
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
| Esc | Cancel what you are doing, leave the tool, drop the chosen corner, clear the selection |
| Right click | Cancel. With nothing to cancel it returns to Select |

The `:` key opens a one line command box for typed commands, which some people prefer for exact placement. Everything it does is also available with the mouse.

## What things are

* **Pegs** are circles with a peg block. Blue and orange are saved as "variable" pegs like the shipped levels: the game picks the real oranges at the start of every game, and the colour you see in the editor is only a preview. Green and purple are fixed.
* **Mod peg types**: select a peg and type a name into the **Custom type** field of the Object tab, for example `ice` or `mymod:ice`. **To mod** saves it as a `peg` line in the level's `.ini`, and the game gives the peg that type when the mod that defines it is on. The editor still draws the peg as a normal one, and opening a level with `peg` lines keeps them. The modding guide has the details.
* **Round and bumper**: you can set the radius. A bumper has no peg properties.
* **Collision role** in the Object tab sets whether a supported selection is a **Scoring peg**, **Permanent bumper**, **Solid terrain** or **Decoration**. A scoring peg can be cleared; bumpers and solid terrain collide without counting toward the peg goal; decoration does not collide. The choices depend on the selected shapes.
* **Bricks** have all the brick settings of the game (type, subdivisions, radius or length, sweep, end angles, thickness, rotation) and can also be a peg.
* **Polygons** are collision shapes and are drawn as outlines. New ones are oriented like the shipped ones so "keep out" faces the right way. Select one and every corner gets a handle: drag a handle to move that corner, double-click an edge to add a corner, and press Delete on a chosen corner to remove it. The **Corners** part of the Object tab does the same with buttons and exact numbers (Corner X and Corner Y). A corner will not move onto another one or across an edge, and the polygon always keeps at least 3 corners. Every change is one undo step. **Lines** are single walls. **Holes** have a score, an out delay and a size.
* **Emitters** are the particle effects of Peggle Nights, like flames and sparks. Pick the Emitter tool (N) and click to place one. It shows as an orange frame, the area the particles come from, with a small square in the middle that holds the effect's picture. Click the square or the frame to select it and drag it to move it. The Object tab has all of its settings: the size and shape of the area, the picture, opacity, colour and size, how fast and how many particles, how long they live, speed, gravity and spin. An emitter can have a mover like any other object. The editor only shows where an emitter is and which picture it uses. The particles run when you play the level, and an emitter with a mover or a follower moves with it.
* **Following** is how the original makes one object ride on another one's mover: a ring of pegs that turns, a peg that sits on a moving platform. Select the objects that should follow, press J (or "Follow an object" in the Mover tab) and click the object they should follow. That object needs a mover. The followers keep their place and move along with it, on top of any mover of their own. Objects update in the order they were placed, so a follower that was placed before the object it follows trails it by one tick (a hundredth of a second), the same as in the original game. A followed object is outlined in purple with a line to each follower, and the orange path of a follower includes the motion it gets from the object it follows. "Follow start X" and "Follow start Y" in the Mover tab are the spot the followed object counted as home when you linked them. You normally leave them alone, they follow the followed object when you move it. "Stop following" lets go again. Copying a follower keeps it following, copying both together makes a new pair. An object that already follows something cannot be followed itself, and an object that is followed cannot follow.
* **Movers**: pick a type in the Mover tab. All 15 types are there with radius, period, time offset, two pauses, phase, path rotation, rotation and reverse. When you add a mover the object stays where it is.
* **New** creates a standard 4:3 level with a 650 by 560 playfield. Changing the display aspect ratio does not change the level. Opening an older custom wide level preserves its original width and object positions.

Properties the editor has no controls for are kept untouched, so editing one peg of a shipped level only changes that peg. Loading and saving a shipped level without changes writes the identical file.

## Saving and playing your level

* **Save** (Ctrl/Cmd+S) updates the writable mod or loose DAT you opened, or the last destination you saved to. An unsaved board or a board from a shipped archive or ZIP mod goes through **To mod** instead. Shipped archives and ZIPs are not rewritten.
* To edit one of two mods with the same level name, select the **mod `<folder>`: `<level>`** entry in **Open** rather than the by-name entry. The by-name entry opens the active copy. The editor preserves existing filename spelling and refuses ambiguous paths rather than guessing. An ordinary edit to geometry leaves the stage registration and settings intact.
* **To mod** (Ctrl/Cmd+Shift+S) uses the **Mod name** in the Level tab. It writes `userdata/mods/<modname>/levels/<Level>.dat`, a companion `.ini` for level settings, and the `levels/mod_levels.cfg` stage registration. Newly generated stages group up to five levels; authored stage settings are kept. It creates `mod.ini` if there is no manifest and renders a 200 × 177 TGA thumbnail into `levels/cached_thumbnails/`. If a destination has conflicting stage settings, choose a new mod folder.
* A board linked to another base keeps that link when you save or export. Keep the pack that supplies the base enabled when playing or editing the board. Do not rename or remove the generated `base`, `base_hash` and `base_provider` entries in its `.ini`.
* A loaded board keeps its DAT version. Use its original edition for testing and do not assume that a Nights board works in Deluxe. Level names may use letters, digits, `_` and `-`; an initial `_` marks a base layout.
* To play a saved board, enable its mod on the **Mods** screen. In Classic 4:3, use the **left arrow on the first Quick Play stage** to reach the mod's stages. In the console layout, use **Single Player → Custom Levels**. No restart is needed.

**Export** and **Import** write and read `userdata/exports/<Level>.json`. When sharing a level with an imported background, send the whole mod folder or send the generated image alongside the JSON. For loose DAT files, keep the DAT, INI and generated image together. A linked base pack is still required. See the [modding guide](modding.md#data-mods) for the sidecar fields.

## Test play

**T**, **F5** or the Test play button plays the level you are editing with the real rules and the normal HUD. Aim with the mouse and click to shoot, **R** restarts, **G** toggles the guide, and **Esc** goes back to the editor with everything as it was. Test play does not touch your profile or high scores.

## Checks

**F6**, **Check** and **Run checks** inspect the board. **To mod** reports check errors without necessarily blocking the export. Test play also runs the checks.

* **Errors**: no objects, no pegs, no peg that can turn orange, no bucket, an object completely outside the playfield, an invalid level name.
* **Warnings**: fewer possible orange pegs than the orange target (the original rules want 25), pegs that overlap each other or a wall, polygon or line, pegs in the side walls or in the bucket zone, pegs that stick out of the playfield, duplicate object ids, movers that do nothing, emitters without a picture or with a picture the game cannot find.
* **Info**: peg counts, emitters, where the bucket comes from, objects outside the original 650 pixel region on a wide board, and entries the editor keeps but does not show.

Markers on the canvas show the objects a message is about.

## Level art

A level background normally takes its name from the board and lives in `levels/`. In the Level tab you can choose a different image or import your own. Stock backgrounds are referenced by name. Keep the source pack enabled if the exported board still links to its base layout.

The editor can show and pick three kinds of pictures. Each one has the same chooser: a list of every picture in the game and in your mod folder with a big preview of the one under the mouse, a filter box (just type), and a button to type a name by hand.

* **Art of level** in the Level tab is the picture behind the board. Click the line to open the chooser. "The level's own picture" means the file called like the level. The board shows your choice at once. The level is saved with a `background` line in its .ini when it is not the level's own picture.
* **Import Background...** below **Art of level** opens an image file from your computer. Choose a PNG, JPEG, GIF, TGA, JP2 or J2K. The editor copies it into your mod's `levels/` folder and shows it on the board. You can move the original afterward. Canceling or choosing an unreadable image leaves the current background in place.
* **Image** in the Object tab is a picture drawn on an object (a bumper, a brick, a polygon). Select the objects first. Your choice is one undo step like any other change, and Image dx, Image dy, Image rot, Tile image and Mirror image next to it move and turn the picture.
* **Image** of an emitter is the picture of its particles, chosen the same way.

You can also put artwork in your mod folder yourself: `levels/` for backgrounds, `images/levels/` for object and emitter pictures. The chooser lists them as "mod folder". Picking one adds your mod folder to the running game so the picture shows on the board straight away, and the Checks tab warns about a picture that is picked but not found.

## Generators and arranging

The Gen tab makes a grid (columns, rows, spacing, origin), a circle, a spiral or a sine row of blue or orange pegs, as one undo step. Arrange can flip, rotate, align (left, centre, right, top, middle, bottom) and spread objects evenly.

## What it cannot do yet

* Bare physics objects (no shipped level has one) are kept in the file but neither drawn nor editable.
* Several objects sharing one mover path can only be made through an imported JSON file. Following covers the usual reason for wanting that.
* You cannot paint level art in the editor. Make the picture in your own program, then use **Level → Import Background...**.
