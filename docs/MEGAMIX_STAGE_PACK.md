# Megamix continuation branch

`feature/megamix-after-s3k` starts at the audited stage-pack commit
`c2d9f37eaec2e1befd025d8fcb637b8057e24de9`. The parent branch remains
`feature/imported-stage-packs`; its engine pin remains
`aeaff277c9838d07ec65512d12ea3adff8c119c9`.

The requested campaign order is **Sonic 1 → Sonic 2 → Sonic 3 & Knuckles →
Sonic Megamix**. Megamix stages and Mighty are elective additions. Existing
native and stage-pack saves must remain intact, with an isolated Megamix
campaign profile when the new campaign is enrolled.

## Exact donor and source-data work

The owner ZIP contains the exact Mega Drive v3 cartridge:

- 1,283,224 bytes; CRC32 `66ECB4F4`.
- SHA256 `85d9e9ba79f0035d5854618666ff198871abe1931558496e6563b1cd90e5cdbe`.
- Byte-identical to v3 from the [Team Megamix downloads page](https://www.headcannon.com/TeamMegamix/index.php?p=mm_downloads).

The public 2005 disassembly describes an altered first release, so its offsets
are not v3 bindings. `tools/megamix/v3_rom.py` reads the actual owner's input
and checks the v3 68000 instructions that select each table. The two copies of
the stage headers/layouts belong to Megamix mode and Original mode. The
Megamix branch must select the first set. Object layouts use absolute 32-bit
pointers in this mode; reusing retail S1's relative-word reader is incorrect.

`tools/inspect_megamix.py` validates all **19 acts and 5,855 placements**,
including Metallic Base 3's special blocks/chunks and The Final Fight. It
decodes every act's art, blocks and chunks and bounds-checks the layout/block
references. It also locates the actual Mighty art bank, palette, mappings and
dynamic PLCs. The shared mapping/PLC tables contain 176 frames, including
unused Sonic-form frames; this is not a claim that all are active Mighty poses.

```text
python tools/inspect_megamix.py --rom "Sonic 1 Megamix v3.0.bin" --out build/megamix-v3-inventory.json --preview-dir build/megamix-previews
```

The ZIP itself can be supplied as `--rom`. Previews require Pillow; captures,
inventories, ROMs and converted assets remain private under `build/`.

`--check-conversion` additionally converts 256px source chunks into S3K's
128px format, compacts reachable blocks with equal visuals/collision, and
checks the resulting visuals, orientations, primary/secondary solidity and
collision shapes. Background cells reuse visible foreground blocks where
possible; background loop collision paths are unused. The v3 loop routine
itself restricts behind-loop selection to Sunny Shores and Starry Night.

Two acts exceed S3K's original 256-chunk format even after equivalent-block
compaction: **City Outskirts 2 uses 279 chunks (260 foreground)** and **City
Outskirts 3 uses 258 (239 foreground)**. The converter preserves complete
16-bit `MGL1` layouts for runtime integration. It never truncates IDs or
silently omits a route. `--export-geometry-dir build/megamix-geometry` exports
private converted payloads; `layout.bin` is empty for these two acts, and
`layout16.bin` holds their complete layouts. These are source-data artifacts,
not a playable stage bank.

## Extended geometry runtime milestone

The child branch now has a portable, bounded `MGL1` reader and an `MGI1`
bank descriptor adapter for directory entries with `TD_GAME=3`. Both retail
collision lookups and the native row streamer return full expansion-bank
cell pointers. The custom background writer and adaptive renderer use the
same geometry. The renderer checks the published frame's stage identity.
The parent campaign remains on its existing RAM path.

`tools/gen_megamix_geometry_sites.py --rom game/sonic3k/sonic3k.bin --check`
verifies all six hook sites against complete retail instruction sequences
and the pinned annotation labels. No engine fork or assembly is required.

Validation on 2026-10-19:

- Both new C tests and both existing cartridge renderer tests passed.
- Every exported act passed the compiled reader and hook adapters; native
  collision, tile streaming and the published renderer agree on every
  converted cell, including both oversized City Outskirts acts.
- The combined game built successfully. With the pack off, all 49 native
  PNG/RAM/VRAM captures match v0.5.2 exactly.
- Existing pack signpost/audio probes passed GHZ1, SYZ2 and MTZ2; each loaded
  the expected next act at its actual start. These are regression probes,
  not Megamix gameplay coverage.

Reproduce the private corpus check after exporting geometry:

```text
python tests/runtime/check_megamix_geometry.py --inventory build/megamix-v3-conversion.json --geometry-dir build/megamix-geometry --reader build/tests/Release/megamix_geometry_test.exe --hooks build/tests/Release/megamix_hooks_test.exe --out build/megamix-runtime-geometry-checks.json
```

The descriptor reader does not enroll a stage or expose an enabled package.
Actual Megamix terrain interactions, stage events and exits remain unverified.

## Acceptance still in progress

The reader and source previews do not constitute a playable stage/character
port. Remaining work is S3K bank conversion and enrollment, donor-specific
events/object/music/rendering behavior, Mighty's native sprite/ability/UI
integration, the real S3K-ending transition into Megamix, save semantics and
the full cheat-assisted audit. Do not expose an enabled package or announce
Megamix/Mighty as landed before those paths are implemented and verified.

Use the same audit policy as `STAGE_PACK_AUDIT.md`: derive every location from
actual tables, record prerequisite bypasses, exercise native interactions and
real next-stage starts, and keep passed/failed/unverified checks separate.
Tracking: central Beads `beads-tdq.3.20`.
