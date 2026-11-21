# Third-party software

The versions below are pinned in [CMakeLists.txt](CMakeLists.txt) and [cmake/openmpt.cmake](cmake/openmpt.cmake). SDL3, OpenJPEG, Ogg and Vorbis use system copies unless you configure with `-DPP_FETCH_DEPS=ON`. Lua, Mbed TLS and libopenmpt are built from pinned sources; stb_image is vendored. Look for licence texts in the download's `licenses/` directory, in the [vendored header](third_party/stb_image.h), or in the dependency source trees. CMake normally places fetched source trees under `<build>/_deps/<name>-src/`.

The original game data (`main.pak`, `levelpacks/*.pak`, music and sound) is not bundled. PegglePort copies the files it needs from your own installation.

## Linked into the program

| Component | Version | Used for | Licence |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | release-3.2.24 | window, renderer, input, audio device, folder dialogs | zlib |
| [OpenJPEG](https://github.com/uclouvain/openjpeg) | v2.5.3 | the JPEG 2000 images in the game data | BSD-2-Clause |
| [libogg](https://github.com/xiph/ogg) | v1.3.5 | container for the Vorbis sound effects | BSD-3-Clause |
| [libvorbis](https://github.com/xiph/vorbis) | v1.3.7 | decoding the sound effects | BSD-3-Clause |
| [Lua](https://github.com/lua/lua) | v5.4.8 | mod scripting (only the base, table, string, math, utf8 and coroutine libraries are compiled in) | MIT |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | v3.6.7 | TLS for secure lobbies and mods; trusted roots come from the operating system | Apache-2.0 |
| [stb_image](https://github.com/nothings/stb) | v2.30, vendored in `third_party/` | PNG, JPEG, TGA and GIF decoding | public domain or MIT |
| [libopenmpt](https://lib.openmpt.org/) | 0.8.10, source archive from lib.openmpt.org (SHA-256 checked) | the `.mo3` music of the original games | BSD-3-Clause |
| [minimp3](https://github.com/lieff/minimp3) | the copy inside the libopenmpt archive | the MP3 compressed samples that a `.mo3` file can hold | CC0 |
| [stb_vorbis](https://github.com/nothings/stb) | v1.22, the copy inside the libopenmpt archive | the Ogg Vorbis compressed samples that a `.mo3` file can hold | public domain or MIT |
| [miniz](https://github.com/richgel999/miniz) | the copy inside the libopenmpt archive | packed tracker files (`.mdz`, `.s3z`, `.xmz`, `.itz`) | MIT |

libopenmpt reads the original `.mo3` music files with the bundled stb_vorbis and minimp3 decoders. It is copyright (c) 2004–2026 the OpenMPT Project Developers and Contributors and (c) 1997–2003 Olivier Lapicque. The build applies the decoder fixes in [cmake/openmpt-mo3.cmake](cmake/openmpt-mo3.cmake) when fetching that source archive. The [Lua licence](https://github.com/lua/lua/blob/v5.4.8/lua.h) is at the end of `lua.h`.

Mbed TLS is offered under Apache-2.0 or GPL-2.0-or-later; PegglePort uses it under Apache-2.0. CMake disables the Mbed TLS sample programs and OpenJPEG's optional codec tools.

SDL3 includes notices for its bundled code, including stb_image, stb_truetype, dlmalloc, FreeBSD libm routines and HIDAPI. See `LICENSE.txt` and the component sources in the SDL3 tree.

## Separate tools

The game and native `pp_lobbyd` server do not require Python packages. The optional secure lobby bridge uses [websockets](https://pypi.org/project/websockets/15.0.1/) 15.0.1 (BSD-3-Clause, pinned in `server/requirements.txt`). The local artwork importers use Pillow; the iPad importer also uses NumPy and OpenCV (`tools/requirements-hd-art.txt`). Those Python packages are installed separately, not linked into PegglePort. The MCP Agent companion uses only the Python standard library.

## Optional, separately licensed

| Component | Used for | Licence |
|---|---|---|
| Steamworks SDK | Steam presence, friend invitations, lobbies and peer networking, only with `-DPP_STEAM=ON` | [Valve's SDK Access Agreement](https://partner.steamgames.com/documentation/sdk_access_agreement/). SDK headers and tools stay separate; permitted runtime files come from `redistributable_bin`. |

## Written for this project

* `src/mods/zip.cpp` is a small zip reader. Its inflate follows the structure of Mark Adler's `puff.c` (zlib licence), no code is copied.
* `src/dmath.h` has deterministic `sin`, `cos`, `atan2` and `sqrt` so that every player in an online match computes the same numbers.
