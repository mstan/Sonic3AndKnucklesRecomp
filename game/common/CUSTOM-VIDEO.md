# Sonic 3 family custom video experiment

The combined **Sonic 3 & Knuckles** target was implemented first. Standalone
Sonic 3 and Sonic & Knuckles share the renderer, with a separately verified
S3 address profile. S3's object-index table is ROM-based and its competition
flag has a different address; neither is assumed to match the combined cart.

Enable **Widescreen** in **Mods** and select Adaptive, 16:9, 21:9 or 32:9.
It is disabled by default. The in-game settings overlay exposes the same
controls. Adaptive follows the window without a 32:9 cap. Development CLI
overrides include `--widescreen fit`, `off`, `stage`, and arbitrary `W:H`.

## Implementation

- Read the native 128px chunks, 16px block definitions and interleaved
  foreground/background row pointers. Do not rewrite collision data,
  decompression, native camera limits or tile streaming.
- Capture six-byte object mappings into a host display list before
  `Render_Sprites`; match completed lists against the actual uploaded SAT.
  Keep the displayed list immutable and reproject retained world positions.
- Draw rings from the ROM layout with native per-ring consumption state.
  Expand the native collision search; leave collection, attraction, rewards
  and collision routines intact. HUD stays screen-relative.
- Expand object activation using the same rounded cells as native culling.
  The native pool has 90 dynamic objects; nearest-player placements take
  priority when it fills. Host rendering has no hardware sprite-line limit.
- Optional main-CPU headroom retains VBlank, DMA and audio clocks. Native
  level waits still control gameplay ticks. Disabled, loading, competition
  and special-stage modes retain native CPU timing.
- AIZ1's upper forest bands repeat their actual 512px art section, not its
  complete layout containing the separate coastline and fire compositions.
  The outgoing background snapshot survives replacement-block decompression.
  The native two-row-per-frame intro refresh is counted as unstreamed
  background, not compared as if it already contained the incoming forest.
- AIZ's HUD-hidden landing cutscene is still a level, not a menu. Loading
  flags and valid layout pointers gate stage readiness instead of HUD visibility.
  Its negative virtual background camera repeats the first initialized 512px
  canvas, matching the native streamer's zero clamp.
- Intro and main-forest block/tile/palette banks are decoded into host-only
  storage from the user's ROM. The expanded margins can display both during
  the emerald cutscene and native VRAM replacement. Native-center rendering,
  animations, collision and the guest decompressor are unchanged. Decoder
  bounds tests and optional ROM asset hashes cover both cartridge profiles.
- Title scenery repeats its initialized 40 tile columns while the logo stays
  centered. Fades keep the selected output dimensions.
- Blue Spheres uses a host-only perspective ellipsoid: horizontal radius
  follows the window while local sprite/tile scale stays fixed. An inverse
  ray/surface cache draws the checkerboard and occludes far-side spheres.
  The native 32x32 board is scanned across the enlarged frustum, without
  the prerecorded 15x16 scan or 80-sprite presentation limits. Original
  distance-frame art, animated rings, characters, messages and shadows are
  retained. Both counter panels/icons anchor to the true screen edges.
  Board/position/mapping state is latched at VBlank before native DMA;
  rendering never changes guest state. The exit fade keeps this projection
  until the native special-stage video mode ends. S3 uses mapping metadata
  at RAM $8400; S&K/S3K use $E480.

## Ground truth and validation

The ignored local disassembly checkout had an older ROM-based widescreen
patch. A separate stock `skdisasm` worktree at `1e1b5af` was assembled: all
2,097,152 bytes of the S&K half match the owner's combined cartridge. A stock
standalone Sonic 3 build also matches its 2,097,152-byte ROM. Hook addresses
come from those listings, not the modified listing or generated C edits.

`tests/runtime/run_sonic3_custom_video.py` uses free-running input scripts
and read-only diagnostics. It copies the executable into isolated test
directories because SRAM is executable-relative. Reusing the same save file
otherwise changes stale save-menu RAM even with identical gameplay input.

The combined-cartridge checks cover the title, Angel Island intro,
forest transition and a scrolling/jumping route. Sixteen native RAM, VRAM
and PNG checkpoints match the pre-port executable byte-for-byte with the
mod disabled. Fixed 16:9/21:9/32:9 and Adaptive resizing are exercised, with
terrain/background checks and dispatch-miss checks. The script separately
checks stable post-intro gameplay for missed/double ticks and publications.
The native intro asset-loading sequence still spans a few gameplay ticks;
this is not a claim that all transitions or all stage-length views run at
60 gameplay updates per second.

Standalone checks use `--variant sonic3` or `--variant sandk`, with separate
pre-port native references. S&K's first route is Mushroom Hill. The unit suite
also compiles the S3 address profile separately and exercises the actual Mods
launcher model with the consumer's compile gate. Runtime tests use one game
at a time and port 4386 in isolated diagnostic directories.

Example (from this repository root; the script finds the engine's shared
Sonic 1 diagnostics helper through `--engine`, `GENESIS_RECOMP_ROOT`,
`engine-local` or the `segagenesisrecomp` submodule):

```powershell
python tests/runtime/run_sonic3_custom_video.py `
  --exe build-sonic3-custom/Release/Sonic3KRecomp.exe `
  --rom game/sonic3k/sonic3k.bin `
  --out build-sonic3-custom/validation --modes off 16:9 32:9 2676:374
```

`tests/runtime/run_sonic3_blue_spheres.py` takes a user-owned save plus a
pre-change executable. Isolated runs verify byte-exact native RAM/VRAM
through moving, turning, collection, jumping and the red-sphere exit, with
byte-exact output when disabled. Fixed 16:9/21:9/32:9 and 2676:374 exercise
the same projection; the original save is hashed before/after. Unit tests
cover projection/intersection agreement, native camera reference points,
sprite counts beyond 80, host-only state and both cartridge address profiles.

Remaining certification includes other zones/boss routes, other burning-island
transition routes, bonus stages, other Blue Spheres layouts/emerald-clear sequences,
standalone special-stage runtime routes, and saves during expanded activation.
The experiment is not ready to publish as a full-game-certified mod.

## Backgrounds, title margins and Blue Spheres follow-up (2026-09-28)

The renderer distinguishes scrolling worlds from authored background canvases:

- Flying Battery streams 512px indoor/outdoor banks from layout X=0/$200.
  Doorways replace rows or columns progressively. Sample the uploaded Plane B
  through these wipes instead of treating both banks as one landscape.
- Sandopolis 1's desert, Carnival Night's normal city and Lava Reef 3's distant
  cavern repeat a 512px canvas. The remaining layout contains other scenes or
  boss geometry, and must not become part of the repeating panorama.
- IceCap's outdoor/cave redraws, Sky Sanctuary's cloud bank at X=$1C00 and
  Death Egg 3's edited runway retain their uploaded Plane B compositions.
- Hydrocity 2's moving wall and Hidden Palace use signed world coordinates.
  Native `Get_ChunkRow` can read guard chunks before a layout row; wrapping to
  the row's far end instead selected unrelated art.

Hidden Palace streams two coarse horizontal bands split at Y=$200, while its
deformation uses ten finer bands. The native name table can expose stale cells
outside a coarse band's 21-block streaming interval. Those samples count as
`background_unstreamed`, like the existing foreground streaming exclusion;
the expanded view still draws the actual world layout there.

The title's Tails plane is captured before native SAT clipping and rendered
in the custom margins at its signed position. The native center retains its
original sprite/plane ordering. The mod's Knuckles parade uses the actual
canvas width for both its native-center draw and margin extension, so its
loop endpoints sit beyond the visible edges.

Blue Spheres now projects grid cells and objects using the same camera lift.
Each sphere/ring's visible base, measured from the decoded native distance
and animation frame, anchors to its projected grid intersection. The checkerboard
changes color at integer board coordinates, placing objects at square corners;
the previous half-cell phase put them at cell centers. Transparent texture padding
no longer supplies the anchor. The board, controls and collisions are unchanged.
Tests render all four quadrants around object anchors through movement, turns,
distance frames and widths 320–1600.

`tests/runtime/run_sonic3_video_sweep.py` selects stock attract demos or uses
the original level-select initializer for normal play. It runs one process at
a time in a fresh directory, with strict JSR checks, dispatch-miss checks,
screenshots, RAM/VRAM checkpoints and renderer telemetry. It never patches
stage layouts or scroll state. Examples:

```powershell
python tests/runtime/run_sonic3_video_sweep.py `
  --exe build-count/Release/Sonic3KRecomp.exe --rom game/sonic3k/sonic3k.bin `
  --out build-video-attract --widescreen 32:9 --knuckles
python tests/runtime/run_sonic3_video_sweep.py `
  --exe build-count/Release/Sonic3KRecomp.exe --rom game/sonic3k/sonic3k.bin `
  --out build-video-levels --widescreen 32:9 --stages 3,6,7,14,15,20,21,22
```

Local validation: all 13 native attract selections across the three cartridges
pass at width 654; Flying Battery, Sandopolis and Blue Spheres also pass at
32:9. All 610 recorded S3K/S&K attract RAM and VRAM checkpoints match the
pre-change build. Native/off Blue Spheres and the mod title each retain all 61
reference screenshots. Both playable Blue Spheres selections were exercised
with movement, turning and jumping at 32:9. The normal-stage sweep covered all
28 main level-select entries; affected scenes were replayed after correction.
These are entry/short-route checks, not every boss or transition in a campaign.
The longer IceCap 1 baseline route hit a pre-existing strict JSR-stack abort
at `$653A -> Process_Sprites ($1AADA)` with no dispatch misses; it is tracked
separately as `beads-tdq.3.14`. The shorter corrected IceCap entry run passes.
Owner visual confirmation remains pending.

## Angel Island follow-up (2026-09-27)

Burning AIZ2's `AIZ2BGE_Normal` streams the same 512-pixel background row
from layout X=0, then applies horizontal heat shimmer. Wide rendering now
wraps that row at 512 instead of sampling the unrelated banks later in the
layout. The owner save reproduces clean native output and corruption at
16:9 before the fix; fixed captures are clean at native, 16:9 and 32:9.
A synthetic regression checks all 796 columns with a large scroll offset.
A separate activation-order bug corrupted the first AIZ1 miniboss. Expanded
activation loaded `Obj_AIZMinibossCutscene`'s PLC $5A before `AIZ1_Resize`
queued PLC $0C at camera $2E00. The latter overwrote the boss bank with vine
and tree patterns. That scripted object now uses the stock 128-pixel-aligned
activation interval; ordinary objects retain expanded activation.

`tests/runtime/run_sonic3_aiz_miniboss.py --exe <Sonic3KRecomp> --rom <owner-rom>
--out <fresh-directory>` positions P1 on the approach, then runs the stock
camera lock/cutscene. At 16:9 and 32:9 the boss VRAM matches native width byte
for byte through its descent and flames; captures are visually clean and
all runs have zero dispatch misses. A synthetic regression checks both
cartridge address profiles. Owner confirmation on a normal route is pending.

The owner's later F2 capture exposed a separate green-to-burning transition
bug. `AIZ1BGE_FireTransition` streams a flame composition into Plane B from
layout X=$1000; `AIZ2BGE_WaitFire` continues at X=$200. The curtain uses
`AIZTrans_WavyFlame`'s per-16px-column VScroll, with an eight-column wave
period. World-layout sampling missed the curtain and exposed chunks/blocks
and patterns while the game replaced them. The renderer now follows the
uploaded Plane B through `AIZ2BGE_BGRedraw`, extending the wave into both
margins. It also follows staged Plane A during `AIZ1BGE_FireRefresh`/Finish
and `AIZ2BGE_FireRedraw`. Opaque high-priority flames cover high-priority
canopy in the extended margins; native overlap priority and transparent
flame edges retain their original behavior. This changes no guest memory.

Synthetic tests cover the seven event phases, column scroll, foreground
replacement, margin coverage and native priority in both cartridge profiles.
`tests/runtime/run_sonic3_aiz_transition.py --exe <build> --rom <owner-rom>
--save <compatible-Adaptive-state> --mods <sonic3k-mods.ini> --out <fresh-dir>`
replays a normal-play save through 120 checkpoints. `--width 796` exercises
32:9; `--native` switches to native rendering after loading. `--compare`
checks a previous capture directory's RAM/VRAM (also PNGs with `--native`).
The owner's original F2 and matching executable remain backed up locally.
Adaptive and 32:9 captures show the complete fire curtain and clean art
replacement. Native output and guest RAM/VRAM match the pre-fix F2 replay
at all 120 checkpoints; owner confirmation is pending.

The owner's F1 tree-climb capture exposed another difference between the
uploaded foreground and the world layout. `AIZ1_ScreenEvent` calls
`AIZ_TreeReveal` to replace individual 16px blocks with a widening mask;
`AIZ1SE_ChangeChunk1..4` commit the complete 128px chunks afterward. Reading
only the layout skipped the small steps and exposed the interior in large
rectangles. While `Events_fg_4` is active, the renderer now samples the
uploaded Plane A for the reveal strip at X=$2C80..$2D7F, Y=$280..$47F.
The original camera lock keeps that whole 256px strip resident. Surrounding
widescreen terrain still uses its world coordinates. The flag is frame-local
scratch reconstructed from RAM; private save-state layout is unchanged.

The same replay tool accepts `--scene tree` for an Adaptive save on the
approach: hold right and capture 200 checkpoints through the climb.
`--reference-native <native-capture-dir>` compares the reveal strip's pixels,
excluding the screen-fixed HUD. At widths 796 and 1156, all 42 active-reveal
checkpoints match native output. All 200 RAM/VRAM checkpoints are unchanged,
and native mode's images are byte-identical. Synthetic regressions cover the
uploaded mask, foreground priority, surrounding terrain and event/zone/act
boundaries in both cartridge profiles. Owner replay confirmation is pending.

`SpecialVInt_Array` begins with RTS/NOP before seven unlabeled BRA.W entries.
The generic branch-table scan misses those entries. Game-owned executable
entry discovery comes from the byte-matched listings:

```text
python tools/gen_special_vint_sites.py --sk-listing <sonic3k.lst> --s3-listing <s3.lst>
```

`--check` verifies drift (also wired to CTest through `S3_SK_LISTING` and
`S3_S3_LISTING`). The generated TOML files cover both cartridge halves and
are included by the three game configs. Generated C is never edited.
