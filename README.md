# Sonic3Recomp

Static recompilation of **Sonic the Hedgehog 3** (USA, 1994) for the Sega
Genesis / Mega Drive, built on the shared
[`segagenesisrecomp`](https://github.com/mstan/segagenesisrecomp) recompiler +
runner.

This is the **Sonic 3 standalone** target — the first of three planned modes:

| Mode | ROM | Status |
|------|-----|--------|
| **Sonic 3 alone** | Sonic 3 (USA), 2 MB | boots to title; bring-up in progress |
| Sonic & Knuckles alone | S&K, 2 MB | planned |
| Sonic 3 & Knuckles (locked-on) | combined 4 MB | separate target (`Sonic3KRecomp`) |

Standalone Sonic 3 is the cleanest target: a single 2 MB ROM at `$000000`,
identity-mapped, so file offset == runtime address == disassembly address — no
lock-on banking, no address offsets.

## Layout

- `CMakeLists.txt` — build config. Reaches the shared engine through a sibling
  checkout of `SonicTheHedgehogRecomp` (which carries the `segagenesisrecomp`
  submodule): `../SonicTheHedgehogRecomp/segagenesisrecomp/`.
- Per-game files (`game.toml`, `sonic3_spec.c`, generated C, discovery data)
  live in `segagenesisrecomp/sonic3/`.

## Building from source

No prebuilt binaries are distributed — build it yourself and bring your own ROM.

**Prerequisites**
- Visual Studio 2022 (MSVC) and CMake 3.16+
- A Sonic the Hedgehog 3 (USA) ROM, 2 MB, CRC32 `9BC192CE`

**1. Clone, with the shared engine alongside.** This repo reaches the engine
through a *sibling* checkout of `SonicTheHedgehogRecomp` (which carries the
`segagenesisrecomp` submodule), so clone both side by side:

```
git clone --recursive https://github.com/mstan/SonicTheHedgehogRecomp.git
git clone https://github.com/mstan/Sonic3Recomp.git
# layout:  <parent>/SonicTheHedgehogRecomp/   and   <parent>/Sonic3Recomp/
```

(Cloned without `--recursive`? Run
`git -C SonicTheHedgehogRecomp submodule update --init --recursive`.)

**2. Supply your own ROM.** Place your Sonic 3 (USA) image (CRC32 `9BC192CE`)
as `sonic3.bin` at
`SonicTheHedgehogRecomp/segagenesisrecomp/sonic3/sonic3.bin`. ROMs are
copyrighted and are never committed or distributed.

**3. Build and run.**

```
cd Sonic3Recomp
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Sonic3Recomp
build\Release\Sonic3Recomp.exe          # the build copies sonic3.bin next to the exe
```

The interpreter **oracle** build (used for native↔interpreter parity
debugging) is a separate target and needs `debug.ini` next to the exe:

```
cmake --build build --config Release --target Sonic3Recomp_oracle
```

Native debug server: port 4384. Oracle: 4385.
