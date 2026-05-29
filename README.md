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

## Build

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Sonic3Recomp          # native
cmake --build build --config Release --target Sonic3Recomp_oracle   # interpreter oracle
```

Native debug server: port 4384. Oracle: 4385 (needs `debug.ini` next to the exe).

## ROM

The ROM is **not** committed (copyright). Supply a Sonic 3 (USA) 2 MB image,
CRC32 `9BC192CE`, as `sonic3.bin`.
