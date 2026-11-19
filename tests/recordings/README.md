# Historical recording fixtures

`Stroke.dat` is a synthetic four-circle Deluxe board; the test loads `_base` from the supplied game data. The circles at (324, 138.25) and (324, 105) put a second collider just outside the ball's initial sweep. At frame 59 classic reports object 27; HD reports 27, 28, 27. The green hit enables Claude's flippers.

These checkpoints were written by `SavedSession::save` linked to the simulation and serializer at the indicated revision, not by the compatibility reader. Load `Stroke` at classic width, use seed 1, Claude and live spin, wheel 0 at frame 0, click/release at 50 to launch and click/release at 59 without a step between the edges. Save at frame 63. The test also checks continuation to frame 71.

| Fixture | Producer | PPSS | Frame 63 board hash |
|---|---|---:|---:|
| classic | `33a524a` | 2 | 3388911711294902990 |
| hd | `50015d1` | 2 | 4645744355899930655 |
| full-stroke | `76ed979`, continued legacy coin | 2 | 12727845725280150023 |
| fair-coin | `76ed979`, new attempt | 3 | 12727845725280150023 |

The first three producers' `ReplayData::save` returned byte-identical `untagged.pego` (v1). With an explicit default gravity override they returned byte-identical `untagged-physics.pego` (v2). Neither format has a terminal hash. `fair-coin.pego` is the fourth producer's v3 output. Do not regenerate these files with a current build.

A successful PPSS v2 reconstruction identifies compatible rules at the checkpoint, not necessarily its producer. If multiple candidates match, the reader keeps the previous playback behavior first (full stroke, legacy coin), then tries HD and classic. New v4 files identify the rules actually used to continue. PPPR v1/v2 playback keeps the previous behavior with a warning; its origin cannot be recovered from the file.
