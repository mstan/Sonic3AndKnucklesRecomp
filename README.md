# Sonic 3 & Knuckles — Recompiled

Native x64 static recompilation of **Sonic the Hedgehog 3** and **Sonic &
Knuckles** for the Sega Genesis / Mega Drive, built on the shared
[`segagenesisrecomp`](https://github.com/mstan/segagenesisrecomp) recompiler +
runner.

Sonic 3 and Sonic & Knuckles shipped as two separate ~2 MB cartridges; **"Sonic
3 & Knuckles"** is the 4 MB **lock-on** combination of the two (S&K boots at
`$000000`, Sonic 3 maps in at `$200000`). They share one engine, so this repo
hosts all three as **build modes** — each a native target plus a paired
`_oracle` (interpreter parity-reference) target:

| Target | Game / ROM | Ports (native/oracle) | Status |
|---|---|---|---|
| `Sonic3Recomp` | Sonic 3 alone, 2 MB (CRC32 `9BC192CE`) | 4384 / 4385 | **Playable bring-up** (Angel Island, saves) |
| `Sonic3KRecomp` | Sonic 3 & Knuckles combined, 4 MB | 4386 / 4387 | Scaffold / early bring-up |
| *Sonic & Knuckles alone* | S&K, 2 MB | — | Planned (no `sandk/` data yet) |

> **Prebuilt Sonic 3 binaries are on the
> [Releases](https://github.com/mstan/Sonic3AndKnucklesRecomp/releases) page —
> supply your own ROM.** Release binaries are AGPL-free clean-room builds
> (PolyForm Noncommercial 1.0.0 + permissive third-party notices). The AGPL
> clownmdemu core is used only by unshipped development/oracle targets; see
> `segagenesisrecomp/RELEASING.md`.

## Building from source

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

**2. Supply your own ROM(s).** Either pass the ROM path on the command line, or
drop it into the engine's per-mode data dir so the build copies it next to the exe:

| Mode | Place ROM at | CRC32 |
|---|---|---|
| Sonic 3 alone | `segagenesisrecomp/sonic3/sonic3.bin` | `9BC192CE` |
| S3 & Knuckles | `segagenesisrecomp/sonic3k/sonic3k.bin` | 4 MB combined |

**3. Build the mode you want, then run.**

Windows (MSVC):
```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Sonic3Recomp     :: Sonic 3 alone
cmake --build build --config Release --target Sonic3KRecomp    :: S3&K combined
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

## Regenerate the C from a ROM

```
cd segagenesisrecomp\sonic3     &&  ..\recompiler\build\Release\GenesisRecomp.exe sonic3.bin  --game game.toml
cd segagenesisrecomp\sonic3k    &&  ..\recompiler\build\Release\GenesisRecomp.exe sonic3k.bin --game game.toml
```

## Sonic 3 & Knuckles (combined) mode — why it's harder

The combined 4 MB target is early bring-up. Beyond the standalone Sonic 3 work,
it has to handle:

1. **RAM-installed IRQ handlers.** VBlank/HBlank vectors point into 68K RAM
   (`$FFFFFFF0` / `$FFFFFFF6`); boot code copies trampolines into RAM at
   runtime, which a static call-graph walk can't follow without a hook/directive.
2. **Sega lock-on memory map.** S&K at `$000000–$1FFFFF`, Sonic 3 at
   `$200000–$3FFFFF`; S&K code JSRs into the Sonic 3 bank. The runtime must
   honor the lock-on mapping.
3. **Battery SRAM** (`$200001–$203FFF`, odd bytes) for combined save data.
4. **Header quirks**: SSP = `$00000000` (boot sets it), header checksum covers
   only the original 2 MB, and one `NBCD` (`$4300`) opcode appears at `$3000EE`.

See `ISSUES.md` / `FEATURES.md` (currently focused on the Sonic-3-alone mode).

## License & ROM

This project's own code: [PolyForm Noncommercial](LICENSE.md) (the recompiler /
build wiring). **Third-party engine components are separately licensed and
mostly AGPL-3.0** — see
`../SonicTheHedgehogRecomp/segagenesisrecomp/THIRD-PARTY-LICENSES.md`.

ROM files are **not** included — supply your own legally-obtained copy.
