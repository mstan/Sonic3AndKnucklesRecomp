Sonic3KRecomp — native static recompilation of Sonic 3 & Knuckles (lock-on)
===========================================================================

A native Windows port produced by statically recompiling the Sega Genesis
68000 code to C. No emulator core: recompiled CPU code, clean-room
VDP/bus/scheduler, ymfm FM synthesis, clean-room SN76489 PSG.

This is the 4 MB "lock-on" combination: Sonic & Knuckles with Sonic 3 mapped
in, so you play the full combined game (Angel Island through Death Egg) with
the Sonic 3 & Knuckles data-select save slots.

BRING YOUR OWN ROM
------------------
This package contains NO game data. Place your own legally obtained
Sonic 3 & Knuckles (4 MB combined / lock-on) ROM next to the exe, named:

    sonic3k.bin

then run Sonic3KRecomp.exe. Battery saves persist next to the exe
(sonic3k.srm) — the data-select slots survive between runs.

CONTROLS
--------
Arrow keys = D-pad, A/S/D = A/B/C, Enter = Start. Gamepads supported (SDL2).
On the DATA SELECT screen, C confirms the highlighted slot.

LICENSE
-------
This software: PolyForm Noncommercial 1.0.0 — see LICENSE.
Third-party components (ymfm BSD-3-Clause, superzazu Z80 MIT, clowncommon
ISC, SDL2 zlib): see THIRD-PARTY-LICENSES.md.

Sonic the Hedgehog 3 and Sonic & Knuckles are trademarks of SEGA. This
project is not affiliated with or endorsed by SEGA. No SEGA assets are
distributed.

SOURCE
------
https://github.com/mstan/Sonic3AndKnucklesRecomp
