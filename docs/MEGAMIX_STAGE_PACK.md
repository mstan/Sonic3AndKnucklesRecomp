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
