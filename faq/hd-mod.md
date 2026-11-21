# Artwork guide

PegglePort has two separate artwork options. **Peggle Native HD** adapts iPad textures and backgrounds for Deluxe and Nights as a mod. The **PS3 Resource Pack** option loads artwork you import from your own PS3 copy for the built-in console layout. Neither changes the playable board or aiming coordinates. Neither includes a copy of the original artwork.

## Build the packs locally

Run the following from a source checkout. The regular game downloads do not ship the artwork tools. Configure the [PegglePort build](../README.md#building) with `-DPP_HD_TOOLS=ON`, then run `cmake --build build --config Release --target pp_build_hd_mod`. The exporter uses your installed edition's folder containing `main.pak`. Give it a new output folder outside the source install and any Git checkout. In these examples, `/external/peggle-art` is an absolute staging path you choose; use `build/pp_build_hd_mod` or `build/Release/pp_build_hd_mod.exe` if the tool is not on your `PATH`.

```sh
pp_build_hd_mod Deluxe "/path/to/Peggle Deluxe" "/external/peggle-art/pc-deluxe"
pp_build_hd_mod Nights "/path/to/Peggle Nights" "/external/peggle-art/pc-nights"
```

Use a Python virtual environment with `tools/requirements-hd-art.txt` installed. Your extracted `Content.HD` folder needs `en.lproj/properties/1024x768/resources.xml`, the JPEG backgrounds under `levels/1024x768/`, and decoded PNG atlases at the paths the manifest references. The importer does not download or include game artwork.

```sh
python -m pip install -r tools/requirements-hd-art.txt
python tools/import_hd_art.py "/external/peggle-art/pc-deluxe" "/path/to/Content.HD" "/external/peggle-art/Peggle-Deluxe-Native-HD"
python tools/import_hd_art.py "/external/peggle-art/pc-nights" "/path/to/Content.HD" "/external/peggle-art/Peggle-Nights-Native-HD"
```

The importer crops and repacks compatible HD sprites into the PC sprite grid. It aligns each level background to its PC landmarks and keeps the PC pixels where the iPad artwork is cropped or masked. It leaves unmatched textures untouched and lists what changed in `hd-art-report.json`. Allow about 1.6 GiB for the two completed packs, plus space for the sources and reference export.

## Install

In Classic 4:3, open **Options → Mods**; in widescreen, choose **Mods** directly from the main menu. Select **Open Folder** in the edition you want to mod. Copy in the matching `Peggle-Deluxe-Native-HD` or `Peggle-Nights-Native-HD` folder, then enable it. Do not stack it with another texture pack. Classic 4:3 keeps the HD textures without side art. Other layouts can use the panoramas; the centered 16:9 console frame uses its own gutters. Untick the mod to go back to stock textures.

With the tested sources, the mod replaces 72 textures per edition, including the ball, pegs, bricks, and colorblind variants. It replaces all 55 Deluxe backgrounds and 55 of 70 Nights backgrounds. Unmatched UI, bitmap fonts, and the 15 other Nights backgrounds stay stock.

## Custom wide art

A data mod can add an image at `widescreen/levels/<background>` with a supported image extension. The panorama's logical height must match the normal background and its width must cover the extended playfield. PegglePort centers it, clips it to the side regions and draws the standard image inside the original playfield. When no matching panorama exists it uses the normal widescreen fallback. A `mod.ini` with `image_alpha = baked` preserves the embedded alpha channel on that mod's color textures instead of applying the original game's sidecar masks.

## Console menus and 16:9 gameplay

The console layout is built into PegglePort. **Auto** uses the 16:9 presentation when the window is 16:10 or wider, and Classic 4:3 in narrower windows. It fits one frame to the window instead of mixing a widescreen menu with a different gameplay layout. You can still select an explicit aspect ratio. No mod is required.

The **PS3 Resource Pack** button sits next to **Open Folder** in **Mods**. In Classic 4:3, open **Options → Mods** first. You need your own compatible Peggle PS3 `.pkg` (retail package with the original artwork archives) or a decrypted, extracted copy containing `USRDIR/data/peggle.pak` and `nights.pak`. The importer does not run the PS3 game or download anything. You still need your own supported PC edition to play.

Install Python 3.11 or newer and the import dependencies, using the same Python that PegglePort will find on `PATH` (`python.exe` on Windows, `python3` on macOS and Linux). The Python command below is `python3` on macOS or Linux. The game itself does not need Python after the import.

```sh
python -m pip install -r tools/requirements-ps3-art.txt
```

On Windows, the in-game importer needs a real `python.exe` on `PATH`, not a WindowsApps launcher alias. If you use a virtual environment, start PegglePort from the terminal where that environment is active. You can also use the command-line import below with any Python installation that runs the script.

Select **Import PS3 .pkg** or **Import Extracted Folder**. For a folder, select the extracted game root or its `USRDIR/data` folder. If the package does not contain both archives in a readable form, use your decrypted, extracted copy instead. A successful import turns **PS3 Artwork** on. Close and restart PegglePort. Switch **PS3 Artwork** off and restart to return to PC art without deleting your import.

If the in-game picker or Python setup is unavailable, run `python tools/import_ps3_art.py "SOURCE" "DEST/presentation"` from the download or source checkout instead. `SOURCE` is the package or extracted folder and `DEST` is the PegglePort preferences directory (beside `launcher.ini`): `%APPDATA%/PegglePort/PegglePort` on Windows, `~/Library/Application Support/PegglePort/PegglePort` on macOS, or `${XDG_DATA_HOME:-$HOME/.local/share}/PegglePort/PegglePort` on Linux. For a DMG without the standalone tools folder, the scripts are also in `PegglePort.app/Contents/Resources/tools`. The destination `presentation` must not already exist; move an old import aside before replacing it. Enable **PS3 Artwork** and restart after a command-line import.

An existing `presentation` beside the Windows or Linux executable, or beside the macOS app or under its `Contents/Resources`, still works once **PS3 Artwork** is on. The preferences copy takes precedence. Without artwork, the console layout still works with PC art. Peggle Native HD is a separate texture mod and does not use this folder.

The widescreen menus keep Custom Levels, Join With Code, the two-column Pause and Options dialogs, and the Nights Lounge. Online rooms show the port's actual players, readiness and room code rather than PSN statistics.

At 16:9 the machine and HUD are centered at their Classic width. PegglePort tries a complete side-art pair under `console/gutters` for the level, then a shared default pair. Without a complete pair it uses dimmed level artwork outside the rails. The playable field, aim coordinates, and scoring are unchanged. Other ratios and authored wide boards still use the existing gameplay layouts.
