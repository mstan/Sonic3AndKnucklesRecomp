# Optional Megamix v3 stages and Mighty

`feature/megamix-after-s3k` extends the audited Sonic 1/2 pack with all
**19 Mega Drive Megamix v3 acts** and selectable **Mighty**. Campaign order:
**Sonic 1 → Sonic 2 → Sonic 3 & Knuckles → Megamix**. The game keeps its
existing name; the additions are disabled by default.

This child starts from `c2d9f37eaec2e1befd025d8fcb637b8057e24de9` on
`feature/imported-stage-packs`. Its engine adds an optional sprite bank at
`c17a6a48d82466ad1fbb5c2f31223d6b1652cfa3`. The audited parent still pins
`aeaff277c9838d07ec65512d12ea3adff8c119c9`.

## Enable and convert

On recomp-ui's Mods page, enable **Sonic Megamix + Mighty (Experimental)**.
Mighty and Mighty & Tails are always included with this mod. Sonic 1 and
Sonic 2 are separate independent mods; either, both or neither may be active.

No ROM or converted donor graphics/music is distributed. First build the
base bank with `tools/build_stage_pack.py`, then extend it:

```text
python tools/build_megamix_pack.py --base-bank sonic1-2-stages.bin --s1 sonic1.bin --s3k sonic3k.bin --megamix "sonic-1-megamix-v3.0 (1).zip" --out sonic-megamix-stages.bin
```

Export `mods/megamix-stages.pak` with
`python tools/split_campaign_packs.py --bank sonic-megamix-stages.bin --content megamix --out mods`
and select it as this mod's resource. Use `--content all` to export all three
self-contained packs from the extended bank. The converter accepts a ZIP or
raw BIN donor. The donor is 1,283,224 bytes, CRC32 `66ECB4F4`, SHA256
`85d9e9ba79f0035d5854618666ff198871abe1931558496e6563b1cd90e5cdbe`, matching
the [official v3 download](https://www.headcannon.com/TeamMegamix/builds/S1MM%20v30.zip).
Supported S3K CRC32: `63522553`. Inputs are never changed.

Existing SPK2 banks can retain all their converted stages and gain the new
Mighty menu/HUD data and original-ROM water/background resources without the
Sonic 1 input:

```text
python tools/build_megamix_pack.py --upgrade-presentation --base-bank sonic-megamix-stages.bin --s3k sonic3k.bin --megamix "sonic-1-megamix-v3.0 (1).zip" --out sonic-megamix-updated.bin
```

SPK2 checks length, CRC, complete directory, geometry, sprites and versioned
MDI/MDY metadata. SPK1 remains supported for the base campaign, but cannot
enable Megamix or Mighty on its own. Invalid/missing resources are skipped,
retaining their configuration and progress. The visual fixes require MDI
version 4 and MDY3; older extended banks must be upgraded before exporting.

## Gameplay and controls

Mighty and Mighty & Tails are additional Data Select choices. On a NEW or
NO SAVE slot, use Up/Down to cycle Sonic & Tails, Sonic, Tails, Knuckles,
Mighty & Tails and Mighty. Saved files remember their own character; Sonic
remains available with the mod enabled. This is the Mega
Drive v3 shield character with native S3K movement/collision and translated
v3 animations, mappings, DPLCs, palettes and shield behavior.

- **Up+A:** store the active shield, or rotate storage with none active.
- **A airborne:** retrieve/swap a stored shield for five rings.
- **B/C airborne:** elemental move or native Insta-Shield.
- **B+C airborne:** transform with 50 rings and seven Chaos Emeralds;
  press again to revert. The emerald prerequisite follows S3K.
- Super combines active and stored shields. Directions select fire,
  lightning and bubble; horizontal+Down combines fire and bubble.
- Super **A** with basic shield attacks visible enemies. Price: 16 rings
  per enemy, halved per element. Boss price is triple; remaining HP rounds
  up after halving. Hurt preserves storage; death clears it.

Mighty has a donor-derived standing menu portrait, a donor-derived life
head and a MIGHTY HUD label using the native capital height, two-pixel
strokes, white/yellow face and dark shadow. The 40px name uses split native
HUD sprites; the multiplication sign and live life digits keep their native
positions. Both the hardware renderer and adaptive renderer use this mapping.
`MDY3` banks include this artwork; rerun `--upgrade-presentation` for older
banks. His sprite palette is isolated from Tails,
Sonic, monitors, checkpoints and stage art, including Super and water colors.
Later CD abilities are outside this v3 port.
Blue Spheres remains the special-stage system. Corresponding native
S3K sound effects are reused.

Saves use native SRAM plus a recoverable campaign companion; see
[save compatibility](STAGE_PACKS.md#saves-and-local-play). Old profile SRAM
files migrate once without overwriting their originals. The native S3K
ending finishes before Sunny Shores loads. Final Fight's escape completes
the campaign. Stable journal codes 32–38 represent Megamix; 39 means
completion. Data Select presents a dense list of the currently available zones.
Quickstates, rewind and netplay remain unavailable until all additional
host state has a complete serialization contract.

## Implementation audit

`tools/megamix/v3_rom.py` verifies donor identity and actual 68000 table
loads. V3 has separate Megamix/Original headers and layouts, absolute
32-bit object pointers, 38 animation entries and 176 shared sprite frames.
Unused Sonic frames remain labeled shared data. The altered 2005
disassembly and later CD versions are not v3 gameplay oracles.

All 19 acts retain geometry, both collision paths and placements. City
Outskirts 2/3 exceed native chunk limits; bounded MGL1 readers retain full
16-bit layouts. Collision, native streaming and adaptive rendering share
that reader. Explicitly marked actor sprites use an immutable texture bank
in both renderers; unmarked sprites and planes retain VRAM. All paths use
portable C. All 25 retail hook sites are instruction/annotation-verified by
`tools/gen_megamix_geometry_sites.py`. Interior labels are not assumed
callable function entries.

Actual donor DLE instructions correct Sunny Shores 2, Dark Fortress 1–3
and Metallic Base bounds/arenas. Megamix dynamic water at `$5882–$5A24`
differs from the adjacent Original-mode routines; starts come from `$5872`.
The MB3 tunnel rectangle (`$5D32`) and changed slide entry (`$5E08`) also
follow v3; the retail S1 versions remain separate.
The earlier conversion incorrectly used Original-mode water. The presentation
upgrade corrects those four small water records while retaining all geometry,
placements and songs. MB3's separate level graphics override at `$5114`
(PLC `$28`) is also applied: its terrain is repacked with its native
level-art door mappings, retaining every layout/chunk/collision record.
Oscillator tables at
`$609E/$6138` equal S1's tables. Changed palette cycles and all 23 used
main/alternate songs come from v3. Sideways stomper `$45` is ported from
`$10D0C–$10FC2`.

The renderer publishes all 64 KiB of RAM to imported plane callbacks. Its
previous `$A800` snapshot omitted the zone and restart state, causing an
out-of-bounds read and missing backgrounds. Misty Maze initializes background
Y to `$20` and uses v3's five horizontal bands/wave table. Metallic Base 3
also initializes at `$20`, then takes its separately verified original-style
scrolling branch (`$9C46`). Native Sonic/Tails CRAM remains untouched by
Mighty; private sprite colors include the normal, water and Super palettes.

## Validation

Follow [STAGE_PACK_AUDIT.md](STAGE_PACK_AUDIT.md). Never write success flags
or HP to claim a result. Position, protection, ring/emerald and clock
prerequisites are recorded as bypasses. Automated runs use dummy audio and
isolated settings/saves/input. Owner builds retain normal audio/input.

Private evidence under `build/`:

| Check | Result / evidence directory |
|---|---|
| Survey | 19 acts, 2,003 locations, all 4,074 non-ring placements; `qa-megamix-final-survey` |
| Music | 19/19 non-silent muted PCM captures; `qa-mm-confirm-audio` |
| Signposts | 11/11 spins/tallies/fresh next starts; `qa-mm-final-signposts`, `qa-mm-final-signposts-2`, `qa-mm-final-mm2-sign` |
| Regular bosses | 5/5 defeats/capsules/next starts; `qa-mm-final-bosses`, `qa-mm-final-bosses-2` |
| Metallic Base | MB2 tally/switch/floor → MB3; MB3 → Final Fight; `qa-mm-confirm-metallic-2`, `qa-mm-final-routes` |
| Final Fight | Real 8→0 damage with Mighty, escape, title and saved completion code 39; `qa-mm-final-routes` |
| Checkpoint/death/retry | All 14 act-first posts and stable retries; `qa-mm-final-checkpoints` |
| Mighty | 29 input/monitor/area/water/death checks; `qa-mm-final-abilities-2` |
| Mighty hurt | Real pickups, enemy hit, native recovery, storage retrieval at five-ring cost; `qa-mm-mighty-hurt` |
| Sideways stomper | Four parts, motion, solid body, damaging spikes; `qa-mm-stomper-2` |
| S3K handoff | Actual final boss segment/defeat/chase/ending → Sunny Shores; `qa-mm-confirm-handoff-2`; earlier DEZ bosses bypassed |
| Save resume | All seven earned Megamix zone codes 32–38 reload through Data Select; completed campaign replays from Green Hill; `qa-mm-earned-save-resumes`, `qa-mm-final-fight-resume` |
| Native regression | All 49 PNG/RAM/VRAM artifacts identical; `qa-mm-confirm-mod-off` |
| Automated, original gameplay delivery | All 32 CTests; private animation run: 36 native states × four orientations × 600 ticks, 31,942 bounded DPLC uploads |

The owner playtest reopened visual acceptance. `run_megamix_visual_spots.py`
derives representative starts, enemies, monitors, checkpoints and route
positions from actual tables. The first corrected pass captured **140
locations across 19 acts** (`qa-mm-visual-spots`); subsequent Misty/water
reruns are kept separately. `run_megamix_reference_spots.py` captures the
same starts/late positions in the unmodified donor using an independently
supplied libretro core. Its 38 references retain canonical RAM, exact cheats
and core/donor hashes. Freeze/protection and direct selection are bypasses;
these captures do not establish interactions or exits. The first reference
iteration's incorrect Original-mode MB3 fixture is retained and superseded
by `qa-mm-original-reference-frozen`.

`check_megamix_water.py` executes the unchanged `$5882` routine in that
independent 68000 core through an explicit private test bootstrap, then
compares its water/trigger results with the compiled portable implementation.
`run_megamix_selection.py` verifies saved character identities through real
Data Select inputs; its completed SRAM fixtures are seeded, not earned.
Presentation-specific CTests check menu/save bounds, isolated sprite colors,
native HUD digit preservation, the mixed menu atlas and full-RAM published
frame stability in both renderer profiles. Retain capture/review/interaction
results separately; screenshots alone never certify a stage.

Enemy evidence is split across `qa-mm-confirm-enemies`,
`qa-mm-confirm-enemies-3`, `qa-mm-special-enemies` and `qa-mm-grounded-roller`.
Bomb's actual fuse produces four damaging fragments. Roller unrolls before
its real defeat. CO1's first Roller is over non-solid donor art and falls
below the map; its grounded `$AE8/$B8` placement supplies the interaction
check. Every original placement remains in the survey/bank.

Coverage screenshots do not prove every mechanic. These checks do not claim
pixel/cycle-perfect donor fidelity, every checkpoint, every special-stage
return, or an uncheated playthrough. Native S3K physics, Blue Spheres and
reused sound effects are deliberate integration choices. Earlier failed
fixtures remain beside corrected reruns; only corresponding passed evidence
belongs in the final matrix.

Presentation correction evidence (2026-10-19): all **34 CTests** pass;
the **29 Mighty input/ability checks** pass again; all **six independent
completed character files** reload Megamix through native Data Select;
all **49 native mod-off artifacts** remain byte-identical. The independent
68000 water oracle passes **524 cases** (`qa-mm-water-instructions`).
`qa-mm-water-visual` supersedes the initial Misty captures, and
`qa-mm-mb3-art-fixed` supersedes MB3. MB3's **616 terrain tile words and
216 active door tiles** reconstruct the actual independently emulated
donor VRAM exactly (`qa-mm-original-mb3-state/converted-art-check.json`).
The 140-location/38-reference review covers representative graphics and
palette anomalies; it does not certify all donor pixels or every camera
alignment. The initial failed reference/pacing fixtures remain retained.

Signpost correction (2026-10-19): the donor's late loader at `$621C`
selects PLC `$12` (Sonic) or `$24` (Mighty), with Nemesis art at
`$3D42E` / `$3D8F2`, 58 tiles at VRAM `$D000`. The previous binding
mistakenly selected PLC 1, the monitor graphics. The third donor choice,
PLC `$25`, belongs to Shadow and is outside this port's scope. MGS1 stores
the two verified graphics/palette sets, five mappings from `$146C2` and
animation from `$146A6`. A separate immutable sprite bank and palette line
keep the sign valid while the results objects load their own graphics.

Independent original-ROM captures (`qa-sign-source-3`) observe all five
frames and match all 1,856 sign-art bytes for each source variant. The
corrected Mighty runtime capture (`qa-sign-fixed-mighty`) observes all
five real frames, the results tally, and the actual next-act transition.
Only player placement/protection is cheated; sign state and completion
flags are not injected. Earlier failed reference fixtures are retained.

Tracking: central Beads `beads-tdq.3.20`, engine `beads-3vb.23`.
