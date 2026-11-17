# Third-party software

PegglePort links the libraries below. The versions are the ones `CMakeLists.txt` fetches with `FetchContent` (pinned git tags or a pinned source archive, built as static
libraries). Each project keeps its licence text in its own source tree, which CMake puts under `<build>/_deps/<name>-src/`. The downloads carry a copy of
those texts in their `licenses` folder.

Nothing from PopCap Games or Electronic Arts is in this repository or in the release archives. The game data (`main.pak`, `levelpacks/*.pak`,
music and sound) is read from your own installation at run time.

## Linked into the program

| Component | Version | Used for | Licence |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | release-3.2.24 | window, renderer, input, audio device, folder dialogs | zlib |
| [OpenJPEG](https://github.com/uclouvain/openjpeg) | v2.5.3 | the JPEG 2000 images in the game data | BSD-2-Clause |
| [libogg](https://github.com/xiph/ogg) | v1.3.5 | container for the Vorbis sound effects | BSD-3-Clause |
| [libvorbis](https://github.com/xiph/vorbis) | v1.3.7 | decoding the sound effects | BSD-3-Clause |
| [Lua](https://github.com/lua/lua) | v5.4.8 | mod scripting (only the base, table, string, math, utf8 and coroutine libraries are compiled in) | MIT |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | v3.6.7 | TLS for mods that use https (the trusted root certificates come from the operating system) | Apache-2.0 |
| [stb_image](https://github.com/nothings/stb) | v2.30, vendored in `third_party/` | PNG, JPEG, TGA and GIF decoding | public domain or MIT |
| [libopenmpt](https://lib.openmpt.org/) | 0.8.10, source archive from lib.openmpt.org (SHA-256 checked) | the `.mo3` music of the original games | BSD-3-Clause |
| [minimp3](https://github.com/lieff/minimp3) | the copy inside the libopenmpt archive | the MP3 compressed samples that a `.mo3` file can hold | CC0 |
| [stb_vorbis](https://github.com/nothings/stb) | v1.22, the copy inside the libopenmpt archive | the Ogg Vorbis compressed samples that a `.mo3` file can hold | public domain or MIT |
| [miniz](https://github.com/richgel999/miniz) | the copy inside the libopenmpt archive | packed tracker files (`.mdz`, `.s3z`, `.xmz`, `.itz`) | MIT |

libopenmpt reads the `.mo3` files of Peggle Deluxe and Peggle Nights itself. The Ogg Vorbis and MP3 samples inside them are decoded by stb_vorbis and
minimp3 from the same archive, so no system library is needed. libopenmpt is Copyright (c) 2004-2026 the OpenMPT Project Developers and Contributors and
Copyright (c) 1997-2003 Olivier Lapicque; its licence text, like the one of the other libraries, is in the `licenses` folder of the download. The source is used as
downloaded, nothing of it is changed.

Mbed TLS is offered under Apache-2.0 or GPL-2.0-or-later; PegglePort uses it under Apache-2.0. Its sample programs are switched off, like OpenJPEG's codec tools.

SDL3 carries a few small parts of its own (stb_image and stb_truetype for its helpers, dlmalloc, FreeBSD libm routines, HIDAPI). OpenJPEG's
optional codec tools are switched off. Their notices are in the fetched source trees.

## Optional, separately licensed

| Component | Used for | Licence |
|---|---|---|
| Steamworks SDK | Steam presence, friend invitations, lobbies and peer networking, only with `-DPP_STEAM=ON` | [Valve's SDK Access Agreement](https://partner.steamgames.com/documentation/sdk_access_agreement/). SDK headers and tools stay separate; permitted runtime files come from `redistributable_bin`. |

## Written for this project

* `src/mods/zip.cpp` is a small zip reader. Its inflate follows the structure of Mark Adler's `puff.c` (zlib licence), no code is copied.
* `src/dmath.h` has deterministic `sin`, `cos`, `atan2` and `sqrt` so that every player in an online match computes the same numbers.
