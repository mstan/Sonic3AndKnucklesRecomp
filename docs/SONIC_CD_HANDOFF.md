# Sonic CD implementation handoff

Investigation: 2026-10-19. Owner explicitly requested investigation and this
handoff **before implementation**, for a fresh Codex session to implement the
feature end to end. No CD runtime, converter, save format, renderer or audio
changes have been made in our repositories during this investigation.

## Start the next session here

Open `F:/Projects/segagenesisrecomp/_wt-sonic-cd` and give Codex this task:

> Read `docs/SONIC_CD_HANDOFF.md`, the applicable AGENTS.md instructions, and
> central Beads issue `beads-ritf`. Implement Sonic CD end to end as a fourth,
> independent, default-off experimental campaign mod. Use the prepared game
> and engine worktrees and pinned SonicAnthology reference. The owner confirmed
> campaign order Sonic 1 -> Sonic 2 -> Sonic CD -> Sonic 3 & Knuckles -> Megamix.
> Preserve Mighty whenever Megamix is available, all existing packs and saves,
> and native S3K behavior. Complete conversion, runtime, launcher resources,
> save migration/recovery, all 16 mod permutations, systematic cheat-assisted
> mechanics QA, independent source comparisons, final screenshot review and a
> verified owner playtest build. Do not stop at stage loading or converter
> milestones. Record passed, failed, bypassed and unverified checks accurately.
> Work solo; the owner has not authorized subagents. Keep CD changes on the
> prepared feature branches unless the owner requests main/master integration.

The next session should continue implementation autonomously within this task.
Do not ask for permission again to do the work described here. If the required
owned CD image cannot be found, ask for its location while continuing useful
source/tooling work; do not invent disc data or claim gameplay verification.

## Verified starting state and worktrees

All preceding S1/S2/Megamix/Mighty/sign/save work is committed and pushed:

| Repository | Branch | Verified commit |
| --- | --- | --- |
| `Sonic3AndKnucklesRecomp` | `main` | `7558f26c2e0a8aa8f39c589f4f4f20beaf8c68b7` |
| `segagenesisrecomp` | `master` | `28eb58e41fc529feb2829e172193e7c04eb29771` |

Both primary checkouts and the prior Megamix game/engine feature worktrees
had clean own-source status. Remote main/master match those hashes. Game
PR11 and engine PR26 are merged. Do not reset or reuse those worktrees for CD.

| Purpose | Path | Branch / initial commit |
| --- | --- | --- |
| CD game implementation | `F:/Projects/segagenesisrecomp/_wt-sonic-cd` | `feature/sonic-cd-stage-pack`, from game main `7558f26` |
| CD shared-engine support | `F:/Projects/segagenesisrecomp/_wt-sonic-cd-engine` | `feature/sonic-cd-stage-support`, from engine master `28eb58e` |
| Pinned upstream game reference | `F:/Projects/segagenesisrecomp/_reference-sonic-anthology-cd` | detached `8f5726a754e819cdff6f6a03690737239889c6fa` |
| Pinned upstream engine reference | `F:/Projects/segagenesisrecomp/_reference-anthology-cd-engine` | detached `cd992c815dd8c331859c2178b60a21d22bae6a3d` |

The old `_reference-sonictrilogy` checkout remains at `b55d4be`, with the
new upstream main fetched. Its old working tree was preserved. The upstream
engine's old reference working tree was also preserved.

The CD game's `segagenesisrecomp` junction points to the new CD engine worktree.
Its `recomp-ui` and `game/skdisasm` junctions reuse unchanged pinned references
from `_wt-megamix`. Engine dependency junctions reuse the primary engine's
exact pinned checkouts. Treat shared dependencies as read-only; create an
isolated dependency checkout before changing one.

| Dependency | Verified pin |
| --- | --- |
| recomp-ui | `b9ef2f539be3622eeb0e7d4a2ff33c8edf129667` |
| skdisasm | `1e1b5aff82c21175c593e42c966a6ff8b1586ff3` |
| m68k-recomp-core | `e9ebf608933969903edd80ac0b79d41ddb2f013e` |
| recomp-net | `2103d5eef111ce98846f017fb12d90f5cbe763e6` |
| rbengine | `2a03e73693acee0fb78076ea058642c931bef12e` |
| z80-recomp-core | `0e6606a41245d54c5fd0c3dc322ef9d6890d3923` |

## What upstream actually added

The former SonicTrilogy repository redirects to
[sonsegajp/SonicAnthology](https://github.com/sonsegajp/SonicAnthology).
The substantive CD commit is
[`a5dbfb268c64f39177669c7fc4a33e05b9d6aa42`](https://github.com/sonsegajp/SonicAnthology/commit/a5dbfb268c64f39177669c7fc4a33e05b9d6aa42).
The pinned tip adds builder sources and README screenshots. Relative to our
original upstream reference `b55d4be`, the update changes 108 files, adding
about 43,000 lines. It also includes unrelated Origins sprites, a replacement
title, display options and other enhancements. Import the CD implementation
selectively; retain our game name, recomp-ui, current renderer and package
architecture. Origins and new title art are outside this request.

This is a substantial implementation reference, not merely a CD stage list.
It has per-round C transcriptions for objects, enemies, bosses and gimmicks,
a chunk pager, water/palette/scenery support, time travel, a UFO special-stage
scene, a warp scene, CD-DA streaming, and a transcribed SMPS-PCM/RF5C164 path.
Their correctness still needs validation in our integration.

The source menu enumerates **70 playable stage/time-period variants** in
28 rows: seven rounds, acts 1/2 in Present, Past, Good Future and Bad Future,
and act 3 in Good/Bad Future only. Do not fabricate Present/Past act 3.

| Round index | Source disc round | Zone | Internal guest zone bytes |
| --- | --- | --- | --- |
| 0 | R1 | Palmtree Panic | `$40..$43` |
| 1 | R3 | Collision Chaos | `$44..$47` |
| 2 | R4 | Tidal Tempest | `$48..$4B` |
| 3 | R5 | Quartz Quadrant | `$4C..$4F` |
| 4 | R6 | Wacky Workbench | `$50..$53` |
| 5 | R7 | Stardust Speedway | `$54..$57` |
| 6 | R8 | Metallic Madness | `$58..$5B` |

Within each group: Present=0, Past=1, Good=2, Bad=3. The low stage byte is
zero-based act. Example: `$4901` is Tidal Tempest act 2 Past. This is guest
stage addressing, distinct from stable journal IDs. Source file suffixes
A/B/C/D correspond to Present/Past/Good/Bad. The disc has no R2 round.

| Area | Start reading in the pinned upstream game tree |
| --- | --- |
| Disc extraction and level inspection | `tools/trilogy/scd_iso.py`, `scd_mmd.py` |
| Conversion, art, geometry, object tables | `tools/trilogy/scd_level.py`, `scd_sfx.py` |
| Special stages and warp data | `tools/trilogy/scd_special.py` |
| Builder integration | `tools/trilogy/builder/trilogy_builder.py:358` |
| TSCD mapping and bank allocation | `game/trilogy/trilogy_core.c:87` |
| Geometry pager, palette animation and level music | `game/trilogy/trilogy_scdlevel.c` |
| Objects | `game/trilogy/scd_obj.c`, `scd_ppz*`, `scd_ccz*`, `scd_ttz*`, `scd_qqz*`, `scd_wwz*`, `scd_ssz*`, `scd_mmz*` |
| Time travel and future/projector flags | `game/trilogy/scd_time.c` |
| Camera/level events | `game/trilogy/scd_events.c` |
| UFO and warp scenes | `game/trilogy/trilogy_scdss.c`, `trilogy_scdwarp.c` |
| Audio and disc access | `game/trilogy/scd_audio.c`, `scd_disc.c`, `scd.h` |
| Flow / special-stage return | `game/trilogy/trilogy_flow.c`, `trilogy_ss.c` |
| New cartridge hook candidates | `game/sonic3k/game.toml`, `tools/trilogy/hooksites.py` |

Upstream `scd_mmd.py --dobby` compares some decoded data against a JP
disassembly for R11A-D, DEMO11A and R61A. Its coverage is explicitly that
subset. The CD update adds no CD-specific automated test or per-location
gameplay audit in the tracked `tests/` delta. The README gallery was inspected
(Palmtree Panic and UFO scene); screenshots establish presentation examples,
not completion, collision, sound, time travel or all-stage correctness.

## Findings that must shape the implementation

### CD is not currently an independently saved upstream campaign

Upstream `trilogy_save.c` still covers S1/S2/native codes 0..32. CD is selected
through its own pause-menu zone rows. `trilogy_flow.c:234` explicitly returns
to the title after CD's final act and calls it an unsaved campaign. Do not
copy this flow as a completed integration. Connect actual S2 completion to
CD, CD completion to native S3K, and preserve S3K -> Megamix, skipping every
disabled/missing campaign. Exercise both neighboring campaigns independently.

Future-generator and projector flags are host static arrays in `scd_time.c`;
they reset when changing rounds and have no durable per-save storage there.
UFO time stones are presented through shared native emerald progression in
`trilogy_ss.c`. Define and document the collectible/save contract explicitly;
do not silently clear existing emeralds or infer that host flags persist.
Preserve any CD-specific durable state the final contract promises, including
round/act/period and generator/projector/collectible state as appropriate.

### The default CD bank window overlaps our existing packs

Upstream TSCD v2 uses a window at `$600000`, size `$180000`, then a sound bank.
Its loader requires that window to begin after its monolithic base bank and
remain below `$7E0000`. Our independent private packs currently retain common
payload and are 3,779,830 bytes: mapped at `$400000`, they end at `$79ACF6`.
The default window therefore overlaps them. `scd_bank_init` also runs only
after the upstream base bank loads, which does not satisfy CD-only installs.

Design explicit bank ownership/mapping for CD and common resources, including
Mighty attachment when CD+Megamix are enabled. Reuse our bank-selection and
bounded resource-graph approach where suitable. Do not overwrite live pack
bytes, blindly relocate pointer-looking words, truncate chunks, or require an
unrelated S1/S2 pak. Validate every known pointer/table against its own region
and file bounds. Keep guest synthetic dispatch addresses separate from data.

The CD pager keeps 16-bit logical chunks but pages at most 255 nonzero chunks
into native RAM. It uses the current canvas width and tries progressively
smaller margins. If demand remains above 255 at zero margin, the allocation
loop can still run out of slots. Audit actual demand at native, wide and
adaptive widths, vertical routes, large teleports, loop/tunnel transitions,
moving scenery and off-camera player/object collision. The native and custom
renderers must consume consistent complete geometry; missing terrain is a
failure, not an acceptable page-budget fallback.

### Disc readers need deliberate format and integrity contracts

The reference supports raw BIN/CUE, developed against a Japanese disc.
`scd_iso.py` assumes 2352-byte sectors with 2048 user bytes at offset 16;
the builder checks for R11A and SPSS files rather than qualifying a revision.
Do not claim US/EU, CHD, ISO, WAV/FLAC tracks, MODE1/2048 or other formats from
that filename check. Support verified inputs, and reject unsupported inputs
clearly without replacing a previously valid pak or rewriting source files.

ROM-free synthetic probes reproduced two reference limitations:

1. The Python reader parses `INDEX 01` but does not apply the data track's
   nonzero start in `sector()`. A zero-index synthetic ISO reads correctly;
   the same ISO shifted by two raw sectors fails its volume descriptor read.
2. `Disc.audio(n)` returns the rest of the underlying file, crossing the next
   track when several tracks share one BIN. A two-sector track followed by
   another two-sector track returns 9,408 bytes instead of 4,704. The C runtime
   reader separately calculates track ends; do not misreport this Python
   result as an observed C-runtime defect.

Probe and results, generated locally without ROM content:
`build/cd-investigation/probe_reference.py` and `results.json` in the CD game
worktree. Rerun with the Python executable below. The JSON also enumerates
all 70 menu stage variants and verifies the existing pack/window overlap.

Add focused parser/integrity tests for actual supported track layouts,
nonzero starts, shared/separate BINs, pregaps, truncated tracks, bounds and
malformed directories. The upstream TSCD reader checks some outer ranges
but not the header version/CRC as our packs do. The TSSS reader checks magic
and item count without fully validating every item span. Build a bounded,
versioned, integrity-checked resource contract before enrolling these formats.
Avoid overflow in all range/allocation arithmetic. Conversion must be atomic.

### Adapt a small audio extension onto the current engine

Upstream engine `cd992c8` adds `AudioExtraSource` / `audio_set_extra_source`
in `runner/audio.h` and calls it in `audio_flush` before the generated WAV
tap, master volume and delivery bridge. CD streams and the RF5C164/SMPS-PCM
implementation are game-owned, with an emulated-frame-paced callback.
Our current engine does not have that callback.

Port the needed contract onto current master. The fork's engine diff also
removes runtime evidence, local state support and unrelated fixes; merging
or replacing the engine wholesale would regress our baseline. Preserve
measurement, bridge counts, netplay checks, scalar/batched rendering, host
sprite banks, atomic SRAM and optional save callbacks. Ensure a NULL callback
preserves base audio exactly; handle registration/reset/unregistration and
disc disappearance without stale streams. Verify generated PCM separately
from speaker delivery; a dummy device must not disable audio generation.
Do not infer cross-build determinism or netplay compatibility from its pacing.

### Reconcile gameplay hooks, palettes and character state

Upstream adds negative-floor and wall-ahead hook candidates relevant to CD
quarter pipes and high-speed curves, plus sound/results hooks. Reverify every
needed site's full instructions against our supported S3K ROM and current
hook contract; preserve qualified IceCap/Launch Base/stack-return changes.
Do not indiscriminately copy the Origins, Tails Assist, title or options hooks.

Retain separate character/object palette ownership. Test CD water and fades
with Sonic, Tails, Knuckles, Mighty and both partner modes. The time-warp
stars touch native invincibility state, so probe shields, stored Mighty
shields, Super reversion, water and warp entry/exit together. Preserve selected
character identity across every scene and save reload. If a special scene
deliberately uses the canonical donor Sonic art, document that choice and
preserve identity on return; do not simulate Mighty with Sonic's tinted art.

## Independent mods and save resilience: acceptance requirements

Sonic CD is a fourth mod, default-off and marked Experimental in recomp-ui,
with its own verified content resource. It must work with S3K+CD alone.
Existing S1/S2/Megamix IDs and settings must continue to work unchanged.
Megamix availability always entails Mighty and Mighty & Tails as independent
choices in every installed campaign, including CD. Missing Megamix temporarily
falls back safely while retaining the saved Mighty preference.

The owner confirmed order **S1 -> S2 -> CD -> S3K -> Megamix**. Skip missing
or disabled content while preserving the order of remaining campaigns. CD
absence must not alter the relative order or behavior of the existing three.

Preserve our game-owned journal rather than porting the upstream save adapter:

- Native `<ROM basename>.srm` remains the normal cartridge format, with valid
  primary/backup checksums and only native levels 0..14 and characters 0..3.
  No new engine-wide SRAM filename switch or CD IDs in cartridge records.
- Current `<basename>.srm.campaign` is SCS1 v1, exactly 548 bytes with CRC,
  pending/confirmed native projections and backup recovery. Stable IDs are
  S1 0..6, S2 7..17, native 18..31, Megamix 32..38, completion 39. **Do not
  reinterpret existing 32..38 as CD or reuse 39 as a new CD stage.**
- Current validation rejects resume IDs above 39 and completion bits above
  `$0F`. Adding CD requires a deliberate schema/capability extension and
  tested v1 migration, not merely increasing the dense menu count. Preserve
  old stable IDs; separate catalog order from persistent identity.
- Keep immutable pre-migration copies and original legacy S1/S2/Megamix saves.
  Retain backups, atomic replacement, failed-flush behavior and interrupted
  pending/confirmed recovery. Preserve or park unknown future fields/versions
  without overwriting their companion; keep native play available. Document
  older-binary downgrade behavior rather than promising unsupported readers.
- Distinguish requested settings from resource availability. Missing, damaged,
  moved, disabled or removed CD files must never block native boot or erase
  parked CD progress. Readding the same content restores stage/identity/state.
  Treat missing disc audio separately from stage geometry availability; make
  the final partial-resource policy explicit and testable in the UI.
- Exercise native progression, shared stats/emeralds, saving and explicit slot
  deletion while CD or all addons are absent, then reinstall. Reconcile valid
  native changes without resurrecting deleted slots or losing parked content.
- Keep imported quickstates, rewind and netplay unavailable until all new host
  state is serialized/qualified. Preserve normal base features with no packs.

Primary local files: `game/trilogy/stage_pack_mods.c`, `stage_pack_bank.c`,
`trilogy_core.c`, `trilogy_flow.c`, `campaign_save.c/.h`,
`trilogy_save_journal.c`, `campaign_mighty.c`, `trilogy_save.c` and
`tools/split_campaign_packs.py`. Read `docs/STAGE_PACKS.md` and
`docs/MEGAMIX_STAGE_PACK.md` before changing the contracts.

## Implementation sequence for the next session

1. Refresh central Beads issue `beads-ritf` and confirm prepared branches/pins.
   Locate the owner's CD image. Narrow checks of `E:/Downloads` found no
   Sonic CD-named file; `G:/Nextcloud/Games` listed GBA/PSX/SNES directories.
   This was not an exhaustive ROM search. No actual CD image was opened or
   qualified in this investigation. Record image/files/track hashes and region.
2. Run the source inspector on extracted disc files. Generate a manifest from
   actual level tables, placements, routes, time periods and special-stage
   tables. Expected main coverage is 70 variants; verify it against the disc.
   The special-stage converter contains eight data sets, including the extra
   stage. Determine and record actual accessibility rather than checking seven
   by assumption. Keep demos and unused data separately identified.
3. Design and test bounded independent CD resources and memory ownership,
   selecting only needed converter/runtime dependencies. Establish CD-only
   boot without a prequel pak before claiming independence. Preserve original
   input data and atomic output/integrity behavior.
4. Extend the stable journal and catalog with migration/recovery tests before
   putting CD codes into playable save selection. Implement package settings,
   resources and fail-soft states through recomp-ui, not the upstream builder
   launcher or title replacement.
5. Integrate level geometry, scrolling, objects, enemies, bosses, time travel,
   future/projector changes, UFO/warp scenes, sound/music and real campaign
   transitions. Implement the minimal portable engine audio contract. Verify
   every retail hook and rendering path as it lands.
6. Complete the QA matrix below and fix discovered failures. Keep earlier
   failed fixtures/evidence, and rerun corresponding checks after fixes.
   Qualify the combined game+engine bundle rather than testing incompatible
   fragments in isolation.
7. Finish with visual review of the final build, rerun affected mechanics,
   then package and verify an experimental owner playtest build. Commit/push
   implementation branches, preserve upstream attribution and link source
   pins/validation in Beads. Do not close the issue at a converter milestone.

## QA contract: cheats, real effects and final visual review

Use `AGENTS.md`'s stage/ROM-hack audit strategy. Do not play full routes
normally. Teleport through every stage, time period, sublevel and alternate
route using a manifest derived from the actual content. Cheats may position
or protect the player, set stated prerequisites, or select a stage for an
entry fixture. They must not inject the success state under test: no boss-HP
writes, generator/projector/time-stone awards, race winner flags, checkpoint
flags, exits or campaign completion bytes when claiming those effects.

Each evidence row must identify source hashes, build/pak identities, character,
stage/period, location and fixture commands, actual observed outcome, bypasses,
and passed/failed/unverified status. A loaded stage, screenshot, registered
handler or crossed obstacle does not prove its mechanics. Derive coverage
counts from the manifest, not a target screenshot quota.

| Coverage area | Required evidence |
| --- | --- |
| Four independent mods | All **16 permutations**, correct starting/next campaigns and Mighty entitlement; preserve old eight-case behavior |
| Resources and saves | Individual/all paks removed, moved, damaged, disabled and restored across process launches; missing CUE/BIN/audio/special data; v1 migration, truncation/CRC/backup/failed-write recovery; native-only play and slot deletion; normal and completed saves for all six characters |
| Main geometry | All 70 actual CD variants, both collision paths, vertical/alternate routes, loops and quarter pipes, wrapping, pager boundaries and distant teleports; native and adaptive/custom rendering |
| Objects/enemies | Every placed family/subtype where behavior differs, actual spawns and interactions/defeat; rings, monitors, platforms, tubes, hazards and stage-specific gimmicks; handler registration tracked separately |
| Time travel | Real post activation and charge/cancel rules, actual warp scene and target load, position/velocity/state restoration, checkpoint/death after warp; valid directions across Present/Past/Good/Bad |
| Good futures | Real generator/projector interactions, changed enemy/animal behavior, both act-3 future branches, correct durable state and reload/pack-removal behavior |
| Checkpoints/death | Actual post touch, real hurt/death/retry and game-over paths; active period and appropriate checkpoint restore, no stuck invulnerability or stored-shield leaks |
| Bosses/race/exits | Every boss/variant and Metal Sonic race, actual damage/phase changes/defeat and escape; real sign/capsule/results/next-act transitions, CD final outcome and actual next-campaign load |
| UFO special stages | All actual available data sets, actual entry, movement/collision/UFO interactions, success/failure/timeout, collectible award and exact return path; duplicates and completed files; preserve character identity |
| Audio | Every period's correct music source, past PCM driver and voices, CD-DA track limits/loops, jingles, SFX, pause/fade/death/warp/results transitions; muted captures with non-silent/non-clipped samples and source comparisons |
| Characters | All six saved identities, their native/CD interactions and water palettes; Mighty abilities/storage/Super/water/warp/revert/scene returns with Megamix present; sane fallback and restoration when removed |
| Native regression | Current CTests and 49 known native image/RAM/VRAM artifacts; existing S1/S2/MM entry and representative deep exit/boss/save/ability checks; qualified IceCap/Launch Base routes |
| Portability | Portable kernels under Windows and Ubuntu GCC; scalar/batched host sprite/palette contracts where touched; no unsupported-platform assembly/dependency regression |

Run automated games muted (`SDL_AUDIODRIVER=dummy` or equivalent zero speaker
output), while retaining audio emulation and WAV generation. Owner-facing
interactive handoff must have normal audio enabled. Do not leave a dummy
device or zero volume saved in the owner build.

### Mandatory final screenshot pass

After gameplay/save fixes and on the actual final build, take and **inspect**
reproducible visual captures across all 70 variants at meaningful source-derived
locations: stage starts, middle/alternate routes, water boundaries, animated
scenery, objects/enemies, generators/projectors, bosses/race, ends, tally and
next-stage starts. Add time-warp and every special-stage/result/return scene.
Retain images before and after a fix; capture alone is not review.

Compare representative locations in each round and period against an
independent original-game run or verified original assets/state. Match camera,
time period, animation and water state before attributing differences. Upstream
gallery images are useful examples, not an independent oracle or full coverage.

Inspect character silhouettes, menu/life portraits and native-style names;
Tails must retain his own colors beside Mighty. Check terrain/backgrounds,
parallax, water palettes, monitor icons, springs, enemies, boss/sign faces,
results lettering and art that loads over another object's VRAM. Include native,
16:9, 21:9 and 32:9/adaptive resize cases, actual margin content, entity
visibility, HUD and menu bounds. Preserve existing renderer implementations;
do not satisfy adaptive checks by stretching or showing empty margins.

The prior Mighty water fixture initially stayed above the actual surface,
then landed over a real shield monitor. Its corrected probe reads the live
water height and uses an item-free location. Carry that discipline forward:
examine RAM/object state when a visual or ability probe fails; isolate fixture
interference without writing the game outcome it is meant to verify.

## Existing evidence, tools and owner data

All paths below are relative to `F:/Projects/segagenesisrecomp/_wt-megamix`
unless otherwise stated. Keep these baselines and owner folders intact.

- `build/qa-experimental-final-summary.json`: 39 passed CTests, 58 independent
  S1/S2/MM act-entry probes, 17 production save/permutation/recovery cases,
  29 Mighty real-input checks, 49 byte-identical native artifacts, four native
  IceCap/Launch Base routes and the six-character owner-folder smoke.
- `build/qa-experimental-signs` and `qa-sign-source-3`: actual sign frames,
  loaded tally art, real next acts and independently emulated donor sign data.
- `build/qa-mm-original-reference-frozen`, `qa-mm-water-instructions`,
  `qa-mm-original-mb3-state`: examples of independent source captures/oracles.
- `docs/STAGE_PACK_AUDIT.md` and `docs/MEGAMIX_STAGE_PACK.md`: earlier deep
  gameplay evidence, source comparisons, bypasses and remaining coverage.
- `tests/runtime/run_stage_pack_audit.py`, `run_stage_pack_routes.py`,
  `run_stage_pack_encounters.py`, `run_stage_pack_checkpoints.py`: reusable
  audit helpers. Extend against actual CD tables, not hard-coded success flags.
- `tests/runtime/run_campaign_permutations.py`, `run_campaign_native_parity.py`,
  `run_megamix_selection.py`, `run_megamix_abilities.py`,
  `run_megamix_visual_spots.py`, `run_megamix_reference_spots.py`,
  `run_megamix_signpost_visuals.py`: existing contract and capture examples.
- `build/experimental-packs/{sonic1,sonic2,megamix}-stages.pak`: verified private
  packs. Older base SPK1: `_wt-stage-packs/build/sonic1-2-stages.bin` at workspace
  level. Do not weaken source-ROM identity checks if an input revision is absent.
- S3K ROM: `_wt-megamix/game/sonic3k/sonic3k.bin`, 4 MiB, CRC32 `63522553`.
  Megamix donor: `E:/Downloads/sonic-1-megamix-v3.0 (1).zip`, CRC32 `66ECB4F4`
  for its 1,283,224-byte ROM. No CD source image has yet been qualified.
- Prior owner build: `build/owner-playtest-experimental-v0.5.3-experimental.1`.
  It has prefilled resources, six seeded completed character files, normal
  audio100 and adaptive Fit. It is a reference/handoff fixture, not earned
  completion evidence. Create a new owner folder for CD; do not overwrite it.

## Build and handoff details

Use absolute Windows tool paths to avoid MSYS argument/path reinterpretation:

```powershell
$git = 'C:/Program Files/Git/cmd/git.exe'
$python = 'C:/Users/Matthew/AppData/Local/Programs/Python/Python312/python.exe'
$cmake = 'C:/Program Files/CMake/bin/cmake.exe'
$ctest = 'C:/Program Files/CMake/bin/ctest.exe'
& $git -c core.fsmonitor=false -c core.fscache=false -c core.preloadIndex=false status --short --ignore-submodules=all
& $python build/cd-investigation/probe_reference.py
```

Some earlier MSYS-created dependency worktrees contain `/f/...` gitdir paths
that native Windows Git cannot resolve. Prepared CD dependency links use the
primary engine checkouts instead. Do not rewrite or delete the old worktrees.
Recheck dependency pins before building, especially if other work moves them.
The CD game worktree initially has no ROM or generated build; use the owner's
verified ROM locally and configure its own new build directory.

Point `GENESIS_RECOMP_ROOT` at the CD engine worktree if an explicit override
is needed. Keep build/temp paths under the CD game worktree. Complete manual
CMake configuration before starting the dependent build; do not edit sources
while the build is running. Preserve current default-off behavior. Use an
experimental build version and trace/debug options only in the development
build. Production must have trace/reverse-debug/dev-trace/cosim OFF.

When updating the engine gitlink through a Windows junction, do not accidentally
stage the junction as mode 120000. Use a full 40-character engine SHA with
`git update-index --cacheinfo 160000,<SHA>,segagenesisrecomp`, and verify the
resulting tree entry. Keep files under game/trilogy game-owned and add only
reusable APIs to the engine. Never commit ROMs, converted data, owner saves,
Ghidra databases or extracted disc/audio files. Retain upstream attribution.

Final delivery requires clean/pinned metadata, tested executable and pak hashes,
runtime DLL closure, and comparison of the final staged executable to the one
actually tested. Use `tools/release/stage_payload.py` and
`tools/release/RuntimeDllClosure.ps1` where applicable. A public runtime archive
must contain no copyrighted input/converted content or private owner settings,
and no CD Speed/Fast Loading packages. The private owner folder should have
resources prefilled, all-stage seeded clear fixtures with migration verified,
normal audio and adaptive Fit. State tested limits and outstanding failures.

Tracking is central Beads `beads-ritf`, under game epic `beads-tdq.3`.
Use `F:/Software/beads/issues`, update meaningful findings and source/build
milestones, and sync through `C:/Users/Matthew/.local/bin/Sync-CentralBeads.ps1`.
Allow its own integrity/transfer deadlines; resume the same healthy session
through tool yields. Do not close the implementation issue until the feature
and the required mechanics/save/visual QA are actually validated. Do not
message the SonicAnthology author; the owner handles that communication.
