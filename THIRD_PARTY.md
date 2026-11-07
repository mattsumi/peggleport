# Third-party software

PegglePort links the libraries below. The versions are the ones `CMakeLists.txt` fetches with `FetchContent` (pinned git tags, built as static
libraries). Each project keeps its licence text in its own source tree, which CMake puts under `<build>/_deps/<name>-src/`.

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

Mbed TLS is offered under Apache-2.0 or GPL-2.0-or-later; PegglePort uses it under Apache-2.0. Its sample programs are switched off, like OpenJPEG's codec tools.

SDL3 carries a few small parts of its own (stb_image and stb_truetype for its helpers, dlmalloc, FreeBSD libm routines, HIDAPI). OpenJPEG's
optional codec tools are switched off. Their notices are in the fetched source trees.

## Optional, never shipped by this project

| Component | Used for | Licence |
|---|---|---|
| [libopenmpt](https://lib.openmpt.org/) | the `.mo3` music of the original games | BSD-3-Clause (its own optional libraries have their own licences) |
| Steamworks SDK | Steam presence and friend invites, only with `-DPP_STEAM=ON` | Valve's SDK licence, not redistributable |

libopenmpt is picked up from the system at configure time. The release archives are built without it, so they play no music. Build from source
with libopenmpt installed if you want the music.

## Written for this project

* `src/mods/zip.cpp` is a small zip reader. Its inflate follows the structure of Mark Adler's `puff.c` (zlib licence), no code is copied.
* `src/dmath.h` has deterministic `sin`, `cos`, `atan2` and `sqrt` so that every player in an online match computes the same numbers.
