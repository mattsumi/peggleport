# Peggle HD

Peggle HD adapts higher-resolution artwork from the iPad edition to Deluxe and Nights. It keeps the original sprite sizes and aiming coordinates. Matched levels get scene-derived side art for center-extended playfields. The built-in 16:9 layout uses a separate surround outside the machine.

## Build the packs locally

Configure the [PegglePort build](../README.md) with `-DPP_HD_TOOLS=ON` and build `pp_build_hd_mod` and `peggleport`. `pp_build_hd_mod` exports the unmodified PC artwork and sprite layouts as a reference for the importer. Give it your installed edition's folder containing `main.pak` and a **new** output folder outside the game install.

```sh
pp_build_hd_mod Deluxe "/path/to/Peggle Deluxe" "pc-deluxe"
pp_build_hd_mod Nights "/path/to/Peggle Nights" "pc-nights"
```

Use a Python virtual environment with `tools/requirements-hd-art.txt` installed. Supply your local copy of the extracted `Content.HD` artwork with its English resource manifest, background JPEGs, and decoded PNG atlases. The importer does not download or include game artwork.

```sh
python -m pip install -r tools/requirements-hd-art.txt
python tools/import_hd_art.py pc-deluxe "/path/to/Content.HD" Peggle-Deluxe-Native-HD
python tools/import_hd_art.py pc-nights "/path/to/Content.HD" Peggle-Nights-Native-HD
```

The importer crops and repacks compatible HD sprites into the PC sprite grid. It aligns each level background to its PC landmarks and keeps the PC pixels where the iPad artwork is cropped or masked. It leaves unmatched textures untouched and lists what changed in `hd-art-report.json`. Allow about 1.6 GiB for the two completed packs, plus space for the sources and reference export.

## Install

Open **Options → Mods → Open Folder** in the edition you want to mod. Copy the matching `Peggle-Deluxe-Native-HD` or `Peggle-Nights-Native-HD` folder there, then enable it on the Mods screen. Do not stack it with another texture pack. In **Options → Widescreen**, choose **Auto** or the matching aspect ratio. **Classic 4:3** keeps the HD textures without adding side art. Untick the mod to return to stock rendering.

With the tested sources, the mod replaces 72 textures per edition, including the ball, pegs, bricks, and colorblind variants. It replaces all 55 Deluxe backgrounds and 55 of 70 Nights backgrounds. Unmatched UI, bitmap fonts, and the 15 other Nights backgrounds stay stock.

## Custom wide art

A data mod can add an image at `widescreen/levels/<background>` with a supported image extension. The panorama's logical height must match the normal background and its width must cover the extended playfield. PegglePort centers it, clips it to the side regions and draws the standard image inside the original playfield. When no matching panorama exists it uses the normal widescreen fallback. A `mod.ini` with `image_alpha = baked` preserves the embedded alpha channel on that mod's color textures instead of applying the original game's sidecar masks.

## Console menus and 16:9 gameplay

The console layout is built into PegglePort. **Auto** selects it for widescreen menus, and the centered gameplay frame is used at 16:9. **Classic 4:3** keeps the PC layout. No mod is required.

To use the original PS3 art, run `python tools/import_ps3_console_art.py "/path/to/NPUA30005/USRDIR/data" "/path/to/peggleport-folder/presentation"` with Pillow installed and a new output folder. Place `presentation` beside the executable. Both editions pick it up automatically. Without those files the layouts still work with PC artwork. The original assets are not included in the source repository. Peggle Native HD remains a separate optional texture mod.

The console layout covers Home, Single Player, and the Nights hub. Other screens, including in-game Options and the online lobby, still use the existing PegglePort UI. The Nights lounge remains available from the hub.

At 16:9 the machine and HUD are centered at their Classic width. PegglePort tries a complete side-art pair under `console/gutters` for the level, then a shared default pair. Without a complete pair it uses dimmed level artwork outside the rails. The playable field, aim coordinates, and scoring are unchanged. Other ratios and authored wide boards still use the existing gameplay layouts.
