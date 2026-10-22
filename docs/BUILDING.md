# Building from source

This guide is for contributors. For downloads and player setup, see the
[main README](../README.md). Run the commands below from the repository root.

**Prerequisites**

- A C compiler — Windows: Visual Studio 2022 (MSVC); macOS: Apple Clang (Xcode
  CLT); Linux: Clang/GCC. Plus CMake 3.16+ (and Ninja on macOS/Linux).
- SDL2 2.28+ — bundled on Windows; `brew install sdl2` (macOS); `libsdl2-dev` (Linux).
- Your own legally-obtained ROM(s); ROMs are gitignored and never committed.

**1. Clone.** The shared engine (`segagenesisrecomp`) is a git submodule, so a
recursive clone is self-contained:

```bash
git clone --recursive https://github.com/mstan/Sonic3AndKnucklesRecomp.git
cd Sonic3AndKnucklesRecomp
# (cloned without --recursive? run: git submodule update --init --recursive)
```

`Skipping submodule 'clownmdemu-core'` in that output is expected and correct.
That core is AGPL and dev-only; nothing you build needs it, and CMake skips the
dev-only `_oracle` targets automatically when it is absent.

**2. Supply your own ROM(s).** Either pass the ROM path on the command line, or
drop it into this repo's per-mode game dir so the build copies it next to the exe:

| Mode | Place ROM at | Identity |
|---|---|---|
| Sonic 3 alone | `game/sonic3/sonic3.bin` | CRC32 `9BC192CE` |
| Sonic & Knuckles alone | `game/sandk/sandk.bin` | MD5 `4ea493ea…` (2 MB) |
| S3 & Knuckles | `game/sonic3k/sonic3k.bin` | 4 MB combined |

**3. Build the mode you want, then run.** Generated C is ignored build output;
CMake builds the current recompiler and regenerates the selected mode whenever
its ROM, configuration/discovery inputs, or the recompiler changes.

Windows (MSVC):
```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Sonic3Recomp           :: Sonic 3 alone
cmake --build build --config Release --target SonicAndKnucklesRecomp  :: S&K alone
cmake --build build --config Release --target Sonic3KRecomp           :: S3&K combined
build\Release\Sonic3Recomp.exe          :: the build copies the ROM next to the exe
```

macOS / Linux (Ninja + Clang/GCC):
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build Sonic3Recomp Sonic3KRecomp
./build/Sonic3Recomp "path/to/Sonic 3.bin"
```

> **Local dev across games:** clone `segagenesisrecomp` once at the workspace root
> and run `scripts/link-engine.sh` (or `.bat`) to share ONE engine checkout; CMake
> prefers the gitignored `engine-local` symlink over the submodule.

Each mode also has an `_oracle` target (e.g. `Sonic3Recomp_oracle`) that runs
the clown68000 interpreter for native↔interpreter parity debugging; it needs
`debug.ini` next to the exe.

### Optional statically recompiled sound Z80

Native targets normally retain the embedded Z80 interpreter. For development,
the sound driver can instead use the flat static backend from
[`smsggrecomp`](https://github.com/mstan/smsggrecomp).
The generated code consumes the framework's pinned
[`z80-recomp-core`](https://github.com/mstan/z80-recomp-core) submodule.
Capture and generate the ROM-derived `<game>_step.c` locally as described in
[`docs/Z80_STATIC_RECOMP.md`](../segagenesisrecomp/docs/Z80_STATIC_RECOMP.md), then
configure with:

```bash
cmake -S . -B build-z80 \
  -DGENESIS_Z80_RECOMP=ON \
  -DGENESIS_Z80_AOT_SOURCE=/path/to/generated/s3kz80_step.c
cmake --build build-z80 --config Release --target Sonic3KRecomp
```

The capture and generated C contain ROM-derived data and remain uncommitted.
Opcode shapes are statically decoded; mutable driver operands are read live,
with one-instruction interpreter fallback only for unmatched code. Turbo mode
silences speaker playback while preserving headless WAV capture.

## Layout

This repo owns the Sonic 3 family implementation; only reusable framework
code comes from the pinned `segagenesisrecomp` submodule
(see [docs/REPOSITORY_OWNERSHIP.md](REPOSITORY_OWNERSHIP.md)):

```
Sonic3AndKnucklesRecomp/
├── CMakeLists.txt              ← three build modes
├── game/
│   ├── common/                 ← shared renderer (sonic3_video.c, Blue Spheres)
│   ├── sonic3/ sonic3k/ sandk/ ← game.toml, discovery inputs, <mode>_spec.c, ROM
│   └── skdisasm/               ← pinned source disassembly submodule
├── ghidra/annotations/         ← reproducible annotation exports
├── tests/, tools/, docs/       ← game-owned validation, tooling, documentation
└── segagenesisrecomp/          ← submodule (shared engine)
```

## Manually regenerate the C from a ROM

Use the configured build's generation targets (a normal build also runs them
automatically):

```
cmake --build build --config Release --target genesisrecomp_generate_sonic3
cmake --build build --config Release --target genesisrecomp_generate_sandk
cmake --build build --config Release --target genesisrecomp_generate_sonic3k
```

> Sonic & Knuckles alone shares the S&K master boot path with the combined cart,
> so its disasm data is regenerated from the same byte-perfect `skdisasm`
> `sonic3k.lst` at offset 0 — see `game/sandk/game.toml`.
