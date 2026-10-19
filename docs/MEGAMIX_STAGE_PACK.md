# Optional Megamix v3 stages and Mighty

`feature/megamix-after-s3k` extends the audited Sonic 1/2 pack with all
**19 Mega Drive Megamix v3 acts** and optional **Mighty**. Campaign order:
**Sonic 1 → Sonic 2 → Sonic 3 & Knuckles → Megamix**. The game keeps its
existing name; the additions are disabled by default.

This child starts from `c2d9f37eaec2e1befd025d8fcb637b8057e24de9` on
`feature/imported-stage-packs`. Its engine adds an optional sprite bank at
`c17a6a48d82466ad1fbb5c2f31223d6b1652cfa3`. The audited parent still pins
`aeaff277c9838d07ec65512d12ea3adff8c119c9`.

## Enable and convert

On recomp-ui's Mods page, enable **Sonic Megamix v3 stages** and optionally
**Mighty from Megamix v3** under Sonic Campaign Stage Packs. Either addition
enables the base feature. Disabling the base disables both additions.

No ROM or converted donor graphics/music is distributed. First build the
base bank with `tools/build_stage_pack.py`, then extend it:

```text
python tools/build_megamix_pack.py --base-bank sonic1-2-stages.bin --s1 sonic1.bin --s3k sonic3k.bin --megamix "sonic-1-megamix-v3.0 (1).zip" --out sonic-megamix-stages.bin
```

Select that extended bank as the mod's required resource. The ZIP or raw BIN
is accepted. The donor is 1,283,224 bytes, CRC32 `66ECB4F4`, SHA256
`85d9e9ba79f0035d5854618666ff198871abe1931558496e6563b1cd90e5cdbe`, matching
the [official v3 download](https://www.headcannon.com/TeamMegamix/builds/S1MM%20v30.zip).
Supported S3K CRC32: `63522553`. Inputs are never changed.

SPK2 checks length, CRC, complete directory, geometry, sprites and versioned
MDI/MDY metadata. SPK1 remains supported for the base campaign, but cannot
enable Megamix or Mighty. Invalid resources refuse launch.

## Gameplay and controls

Mighty replaces player-one Sonic in Sonic and Sonic & Tails modes across
the campaign. Tails and Knuckles retain their characters. This is the Mega
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

Later CD abilities are outside this v3 port. The native Sonic save portrait
and life label remain; Mighty is selected by the mod, not a new character
slot. Blue Spheres remains the special-stage system. Corresponding native
S3K sound effects are reused.

Megamix saves use **sonic3k-megamix.srm**; the base uses
`sonic3k-stage-pack.srm`. Native saves remain separate. The native S3K
ending finishes before Sunny Shores loads. Final Fight's escape completes
the campaign. Zone codes 32–38 represent Megamix; 39 means completion.
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
portable C. All 18 retail hook sites are instruction/annotation-verified by
`tools/gen_megamix_geometry_sites.py`. Interior labels are not assumed
callable function entries.

Actual donor DLE instructions correct Sunny Shores 2, Dark Fortress 1–3
and Metallic Base bounds/arenas. Dynamic water at `$5A62–$5C20` matches
the imported S1 logic; starts come from `$587A`. Oscillator tables at
`$609E/$6138` equal S1's tables. Changed palette cycles and all 23 used
main/alternate songs come from v3. Sideways stomper `$45` is ported from
`$10D0C–$10FC2`.

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
| Automated | All 32 CTests; private animation run: 36 native states × four orientations × 600 ticks, 31,942 bounded DPLC uploads |

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

Tracking: central Beads `beads-tdq.3.20`, engine `beads-3vb.23`.
