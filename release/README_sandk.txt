SonicAndKnucklesRecomp — native static recompilation of Sonic & Knuckles
========================================================================

A native Windows port produced by statically recompiling the Sega Genesis
68000 code to C. No emulator core: recompiled CPU code, clean-room
VDP/bus/scheduler, ymfm FM synthesis, clean-room SN76489 PSG.

BRING YOUR OWN ROM
------------------
This package contains NO game data. Place your own legally obtained
Sonic & Knuckles (2 MB) ROM next to the exe, named:

    sandk.bin

then run SonicAndKnucklesRecomp.exe. (Standalone Sonic & Knuckles has no
data-select save game, so it writes no .srm — that's correct for this cart.)

CONTROLS
--------
Arrow keys = D-pad, A/S/D = A/B/C, Enter = Start. Gamepads supported (SDL2).

LICENSE
-------
This software: PolyForm Noncommercial 1.0.0 — see LICENSE.
Third-party components (ymfm BSD-3-Clause, superzazu Z80 MIT, clowncommon
ISC, SDL2 zlib): see THIRD-PARTY-LICENSES.md.

Sonic & Knuckles is a trademark of SEGA. This project is not affiliated
with or endorsed by SEGA. No SEGA assets are distributed.

SOURCE
------
https://github.com/mstan/Sonic3AndKnucklesRecomp
