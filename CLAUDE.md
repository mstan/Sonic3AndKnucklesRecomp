# CLAUDE.md — Sonic3AndKnucklesRecomp

This repository owns the Sonic 3 family implementation and release: Sonic 3
alone (`Sonic3Recomp`), Sonic 3 & Knuckles lock-on (`Sonic3KRecomp`) and
Sonic & Knuckles alone (`SonicAndKnucklesRecomp`). The shared
`segagenesisrecomp` framework is a pinned submodule, optionally replaced for
development by `engine-local` or explicit `GENESIS_RECOMP_ROOT`.

→ **`segagenesisrecomp/CLAUDE.md`** — read this first.
→ **`segagenesisrecomp/PRINCIPLES.md`** — the 25 rules.
→ **`segagenesisrecomp/DEBUG.md`** — always-on ring inventory + TCP commands.

## What's in this repo

- `CMakeLists.txt` — shared runner from the engine, per-mode files from `game/`.
- `game/common/` — the family renderer (`sonic3_video.{c,h}`,
  `sonic3_blue_spheres.inc`) shared by all three modes, and `CUSTOM-VIDEO.md`.
- `game/sonic3/`, `game/sonic3k/`, `game/sandk/` — per-mode `game.toml`,
  discovery inputs (`*.disasm_labels*.toml`, `*.disasm_jumptables*.toml`,
  `*.code_addrs.txt`, `annotations_from_disasm.csv`), `<mode>_spec.c` and the
  owner ROM (`sonic3.bin`, `sonic3k.bin`, `sandk.bin`; gitignored).
- `game/skdisasm/` — pinned Sonic Retro source disassembly (submodule).
- `ghidra/annotations/` — reproducible annotation exports + provenance.
- `tests/`, `tools/`, `docs/` — game-owned validation, tooling and ledgers.
  `tools/sonic3_disassembly.py --install` rebuilds and verifies the pinned
  disassembly and re-exports annotations.

## Workspace layout

Game-specific implementation belongs HERE, never in the engine repository.
The shared engine must expose reusable opt-in contracts only. Legacy engine
game directories are not precedent for new game-specific framework code.
See `docs/REPOSITORY_OWNERSHIP.md`. The Sonic 3 family does not consume
Sonic 1 or Sonic 2 game code. Generated C belongs under the build directory.
Never commit ROMs, extracted artwork or local Ghidra databases.

## Engine commit order (PRINCIPLES.md #20)

1. Commit + push engine changes in the top-level `segagenesisrecomp/` checkout
   first.
2. Bump this repo's engine submodule pointer only after the engine commit is
   available upstream. Commit Sonic 3 family implementation changes here.
