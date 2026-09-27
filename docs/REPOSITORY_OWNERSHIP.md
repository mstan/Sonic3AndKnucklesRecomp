# Sonic 3 family repository ownership

Owner boundary (clarified 2026-09-22, first applied to Sonic 2 in engine
`c5d40a6` / SonicTheHedgehog2Recomp `bd40e97`): reusable opt-in engine
interfaces are allowed; game-specific implementation belongs here. Epic:
`beads-tdq.3`. This relocation moves the Sonic 3 family out of engine
`dcf77ffb3b5f185990a164077eb85668cf3460e2` without changing gameplay.

- `game/common/`: `sonic3_video.{c,h}`, `sonic3_blue_spheres.inc` (the
  opt-in family renderer linked by all three modes) and `CUSTOM-VIDEO.md`.
- `game/sonic3/`, `game/sonic3k/`, `game/sandk/`: per-mode `game.toml`,
  discovery inputs (`*.disasm_labels*.toml`, `*.disasm_jumptables*.toml`,
  `*.code_addrs.txt`, `annotations_from_disasm.csv`, S3K's
  `game_pure_heuristic.toml`), `<mode>_spec.c` and the owner ROM
  (`sonic3.bin`, `sonic3k.bin`, `sandk.bin`; ignored, never committed).
  `game/sonic3k/cosim.json` holds the optional cosim metadata formerly
  preset in the engine (`--game s3 --game-config game/sonic3k/cosim.json`).
- `game/skdisasm/`: same pinned upstream commit
  `1e1b5aff82c21175c593e42c966a6ff8b1586ff3`.
- `tests/`, `tools/`, `ghidra/annotations/`: renderer unit tests, runtime
  harnesses and regression inputs, `tools/sonic3_disassembly.py` and the
  reproducible annotation exports.
- Engine: runner, recompiler, generic tests and tooling only. Its
  `tools/sonic_disassembly.py` keeps the shared listing parser/exporter and a
  generic lock-on `COMPOSITES` hook; this repo supplies the skdisasm sources.

CMake passes this repo's absolute `game/<mode>` directory to the shared
generator and adds `game/common` to every mode's include path. Engine CTests
build under `<build>/engine-tests`, this repo's under `<build>/tests`.

Mechanical moves were blob-checked (34 files, identical Git blob IDs before
path edits). Subsequent edits are comments/paths only: `game.toml`
regeneration comments, the two `#include "../sonic3k/sonic3_video.h"` lines,
the renderer test's include, `run_sonic3_custom_video.py` engine-helper
lookup (`--engine`, `GENESIS_RECOMP_ROOT`, `engine-local`, submodule) and
the annotation JSON `provenance.source` (`game/skdisasm`).

## Validation (Windows, MSVC Release, stock settings)

- Generated C: 38 files per mode; 37 byte-identical to the pre-move build,
  `<prefix>_layout.c` differs only in its header comment naming the build
  directory.
- `--no-launcher --turbo --max-frames 3600 --hash-frames 60`: all 60
  framebuffer hashes identical per target; `dispatch_misses.toml` identical
  with empty `functions.extra`; 0 true-miss addresses.
- `--benchmark 3600`: identical machine-state and audio-state hashes per target.
- `tools/sonic3_disassembly.py`: all three builds byte-identical to the owner
  ROMs (1,817 / 2,369 / 4,186 code labels); CSV exports byte-identical to the
  moved discovery CSVs.
- CTest 17/17 with recomp-ui `b9ef2f5` (the `party_input` engine test needs
  its four logical Genesis binding slots; at the previous pin `47005fb` it
  failed identically before and after the move).

Remaining: bump the `segagenesisrecomp` submodule pin to the engine commit
that removes the legacy directories once it is available upstream.
