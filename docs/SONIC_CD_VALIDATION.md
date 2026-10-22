# Sonic CD validation ledger

## Experimental main integration, 2026-10-21

The owner accepted the corrected `5c00831` playtest and authorized main/master
integration. Sonic CD retains the launcher name **Sonic CD stages (Experimental)**,
the experimental feature channel and its disabled-by-default setting. README
and setup instructions now explicitly identify it as experimental. The
qualified engine `64c1ffb` and UI `4261bad` pins are retained.

All 15 focused integration checks pass: ten game/provider/save/render/import
checks and five shared audio/device/media/build-identity checks. Evidence is
`ctest-main-integration.log` and `ctest-engine-main-integration.log` under the
existing private evidence root. Earlier full content/visual qualification and
the actual 70-entry wrap checks below are reused; the final integration changes
only documentation. No owned ROM, disc, converted pack or save is added.

## Corrected owner request: original ZONE text and 70-entry wrap, 2026-10-21

The owner rejected the CD/S3K caption change below. Original ZONE text, font
and thumbnail rendering are restored exactly from `c98be0a`. With CD as the
only enabled stage pack, the completed-save replay selector now wraps only
through its 70 CD variants. The original CLEAR cursor is normalized only when
an arrow is pressed, preserving the untouched CLEAR card. This also fixes the
first Down press from CLEAR; changing the maximum alone would still subtract
from the old global CLEAR cursor. Native campaign progression, journal IDs,
save records and the cold-load buzzing fix are retained.

`cd-zone-wrap/report.json` under the existing private evidence root records:

- PASS: actual Down input visits precisely all 70 CD codes, then wraps
  Zone 1 -> Zone 70. Actual Up input wraps Zone 70 -> Zone 1. Both native and
  398-pixel menu runs pass. No replay cursor or success flags are written by QA.
- PASS: a single Down/Start from CLEAR enters original CD variant 70 (`5B02`);
  a single Up/Start enters variant 1 (`4000`). Existing completed owner saves
  are the declared navigation prerequisite, copied into private test profiles.
- PASS: final native/wide ZONE 70, ZONE 19 and ZONE 1 images were inspected
  against the original pre-caption menu. The original font/artwork is restored.
- PASS: Windows/Linux journal and audio-control tests still pass, covering
  all 16 catalogs and the retained loading-audio behavior. `zone-wrap-linux.log`.

Issue `beads-tdq.3.22` was reopened for this correction. The earlier caption
evidence below is historical and does not represent the requested UI.

## Superseded replay captions; retained cold-load audio fix, 2026-10-21

Issue `beads-tdq.3.22`: completed-save replay still includes the installed
combined campaign. Its selected card now says **CD 70**, **S3K 14**, etc.,
instead of showing an ambiguous bare ZONE number. Catalog order, input
navigation and save records are unchanged. The original CLEAR card is retained.

Cold music-pack integrity verification and first-song loudness measurement
blocked the producer while device stall concealment repeated its last sound.
Both explicit loading boundaries now call the existing host playback-discard
API. Guest audio, integrity validation, loudness normalization and generated
samples are retained. Warm requests for the already-playing song do not reset
playback. This fixes the stuck tone; it does not promise to remove cold I/O or
CPU preparation time.

Evidence under `G:/Temp/Codex/segagenesisrecomp/sonic3k-cd-integration`:

| Result | Check | Scope / evidence |
| --- | --- | --- |
| PASS | Actual device output | Normal-speed, muted SDL disk-device capture of real Down/Start input into CD 70. The baseline repeats a 1242 Hz tone for 24 consecutive seconds. Both corrected captures reach the stage without that stationary tone, and stage music resumes. `audio-before`, `audio-after`, `audio-delivery`, `device-analysis.json`. |
| PASS | Generated audio preserved | All 7,680,456 stereo PCM frames match exactly before/after, SHA256 `652aea226ffa54de77a659ccfd558792529eb5ce293bc8a4fd7c549e537792a2`. Script EXIT leaves the WAV header length zero; comparison explicitly reads the actual nonempty data payload. `audio-comparison.json`. |
| PASS | Replay UI and visuals | Actual input selects S3K 14, CD 1 and CD 70 at native and 398-pixel widths. CLEAR remains original. Native pixels outside the selected 48x7 caption field are identical. Final caption alignment, ink and complete seven-row glyphs were inspected. `replay-captions-final2`, `menu-audio-visual-comparison.json`. |
| PASS | Gameplay visual regression | Final CD 70 stage image is RGB-identical to the baseline at 398x224. Independently inspected against original CD `qa-cd-original-settled-starts-1/R83D/start.png`: terrain, background art and palette agree; S3K HUD/characters and camera/animation phase differences remain intentional. |
| PASS | Focused Windows/Linux checks | Audio loop, scene/native pause, resume, fade and one-shot tests pass, including cold preparation and same-track playback. Journal tests pass all 16 catalogs, game-specific captions, removal/restoration and durable-save recovery. `menu-audio-linux-final.log`. |
| UNVERIFIED | Identical cold-load duration | Baseline took 122 seconds wall time for the entire scripted session; corrected sessions took about 37. File cache and machine load differ, so this is not a qualified loading-speed improvement. No caches were flushed or drives scanned. |

Earlier caption experiments in `replay-captions*` had alignment or final-row
ink defects and are not delivery evidence. The script's `clear-return` image
actually selects CD 1 after wrapping the replay list; only `clear.png` is the
initial CLEAR-card check. Device captures use no speakers; owner playtests
retain normal audio. Original profile saves are not reseeded by this fix.

## Owner-reported starting-area background movement, 2026-10-21

Issue `beads-tdq.3.21`: adaptive widescreen used the foreground's changing
bounded origin for CD's background, and drove parallax from the native camera
while the wider view was still clamped. CD now samples B from the centered
canvas and drives its original scroll profiles from the displayed camera.
Foreground/player simulation retains its native camera. Initial offsets and
resizes use a full-width horizontal delta, preserving fractional accumulators
and autonomous animation without wrapping the native signed 8.8 delta.

Private evidence is under
`G:/Temp/Codex/segagenesisrecomp/sonic3k-cd-integration`:

- `bg-shift-before-v2-*` and `bg-shift-after-v2-*`: actual Right/Left movement
  in CD variant 70, at native width and 398 pixels. All 42 native captures are
  pixel-identical. Player/camera observations are identical at both widths.
  Widescreen background X stays fixed while the starting view is clamped,
  including the return trip; every capture beyond that edge is unchanged.
  Initial origin-only experiments remain separate as `after-v1` and were
  insufficient because the native camera still drove parallax.
- `bg-shift-*-v2-ppp-*` repeats the same actual movement in Palmtree Panic
  Present. All 42 native captures and the widescreen interior captures remain
  identical; player/camera observations match. Its autonomous background
  animation remains active. Comparisons are `bg-shift-comparison*.json`.
- Renderer tests cover the first/last CD stage families, both bounds and
  320/398/523/796-pixel canvases, and retain genuine line-scroll motion.
  MMZ3 original half-speed vectors cover forward/reverse offsets through
  736 pixels without 8.8 overflow. All 70 original 64-frame scroll vectors
  still pass, including reset/context reuse.
- Windows and Linux combined/standalone renderer tests pass. Linux's kernel
  also passes UBSan with the production fixed-width wrap policy. That check
  found an existing QQZ Present variable shift with an out-of-range count;
  runtime and generator now explicitly preserve the qualified Windows count
  mask. Original-vector results are unchanged.
- Final moving/return images were inspected against the prior native captures
  and the independent original CD `qa-cd-original-settled-starts-1/R83D` image.
  Widescreen edge framing intentionally differs; terrain, palette, background
  artwork and ordinary parallax remain intact.

The completed-file selector separately passed actual Up/Down/Start probes:
CLEAR Down -> S3K Zone 14 (`0C00`), CLEAR Up -> CD variant 1 (`4000`), and
CLEAR Down 15 times -> CD variant 70 (`5B02`). This is a combined 84-entry
catalog with per-game numbering, not a 14-entry limit. `menu-boundaries`.

## Integration follow-up, 2026-10-21

The campaign remains an S3K adaptation. Standalone SonicCDRecomp now supplies
the original-program reference; earlier notes that its general backend was
still pending are historical. The follow-up shares its upstream media/ISO
readers, adds CUE/SGCD input parity, imports content and music as one atomic
installation, and exposes a background owned-disc import in recomp-ui.
Gameplay code, resource formats and persistent campaign progress are unchanged.
The historical gameplay/visual results below retain their original scope.
New evidence is private under
`G:/Temp/Codex/segagenesisrecomp/sonic3k-cd-integration`:

| Result | Check | Scope / evidence |
| --- | --- | --- |
| PASS | Shared owned-media input | Synthetic CUE/SGCD program-area parity includes stored/virtual pregaps and postgaps; 15 disc tests cover corrupt SGCD payloads, invalid MODE1 sectors and bounded reads. Native SGCD/ISO extraction has seven independent tests. |
| PASS | Complete owned import | Real native wrapper/provider imports the qualified US SGCD into both resources. Content SHA256 is `fe8055c36f6c2836d7dc6427da8aee1ad483ef363ae51c50011f03052cf73c02`; music is `1f2318b96cc08a78a6c20852a74398b2a9c298472ccafc6244a9b5843ea68fab`. Both exactly match the previously qualified packs. `owned-import.log`. |
| PASS | Failure and persistence | Failed retry retains both paths; settings commit/reload preserves the complete installation and other mod switches. Transaction tests cover audio failure, atomic publication, reuse and corrupt-cache rejection. |
| PASS | Launcher completion | Actual hidden, muted launcher imports the owned disc, shows progress, restores resource controls and re-enables Play. `candidate-ui/{disc-resource,import-progress,import-complete}.png`, `qa-driver-ui-final.log`. |
| PASS | Process completion | Windows and Linux tests spawn an importer descendant retaining stdout. Both success and failure return on the actual importer exit, retaining final progress/error output. `linux-import-pipe.log`; CTest `scd_import_process`. |
| PASS | Installed Python selection | Real Windows `py -3` dispatch rejects a missing input with literal shell metacharacters without invoking a shell or the Windows Store Python alias. |
| PASS | Existing feature combinations | All 64 actual stage/character provider configurations pass using the rebuilt resources and shared owned Megamix donor. `provider-matrix`. |
| PASS | Final visual comparison | Palmtree Panic with Shadow matches the qualified prior campaign build exactly in RGB at 320x224 and 398x224. Independently inspected against standalone native CD's `docs/assets/games/sonic-cd-wide.png`: terrain patterns, foliage, background and palette agree; character art/HUD, camera and animation phase intentionally differ. Launcher dashboard layout also matches the prior build; the new resource/progress/completion views were inspected. `visual-comparison.png`, `qa-report.json`. |
| PASS | Focused regressions | Nine focused CTests pass, plus the real UI worker/poll unit on Windows and Linux. Linux component compilation includes the POSIX importer. `linux-checks.log`, `linux-import-pipe.log`. |
| PASS | Public converter payload | Production staging includes the importer, shared media readers and graph validator (47 allowlisted files); both CLIs resolve their packaged tools outside the source checkout. No owned inputs are staged. `distribution-check.log`. |
| REUSED | Audio, controls, campaign/save mechanics | Gameplay/audio implementations and both generated resources are unchanged. The earlier qualified checks below retain their scope; no new full 70-variant playthrough or durable-save matrix is claimed. |
| UNVERIFIED | Linux graphical import | Linux worker/process/component checks pass; a full graphical Linux game/import run was not repeated for this follow-up. |

The initial visible automated launcher was not an owner handoff. It exposed a
completion polling gate restricted to first-run-wizard titles; cartridge mods
now poll and display import progress too. Process handling also no longer waits
for pipe EOF from inherited descendant handles. Earlier GUI attempts failed;
the final hidden run passed. An earlier capture used the executable's existing
save directory instead of its working directory; isolated executable copies
fixed QA provenance before the reported visual comparisons.

No campaign mechanics were deferred by this importer change. Existing
adaptation limits remain: S3K movement/HUD/ordinary character art, shared Sonic
art in UFO/warp scenes, some monitor reward substitutions and the S3K campaign
ending flow. Original-program behavior remains in standalone SonicCDRecomp.

This ledger records campaign qualification and its explicit limits; it is not a
main/master merge declaration. The owner goal
is campaign completion and a commit on the CD branch, followed by a general
upstream Sega CD recompilation backend. Work remains solo. Main/master and the
preserved AS IS snapshots are unchanged by this campaign work.

## Current implementation

Game branch: `feature/sonic-cd-stage-pack`, integration checkpoint `7de1bf6`.
Engine branch: `feature/sonic-cd-stage-support`, `332ff1fbdb4073cddffd63305a34da4935c262c4`.
The current campaign candidate is build 30, SHA256
`ec4085de7e269bc8da2364e4ab537f10ac9cac61df54920407b3b2ece5c0d39a`.
It fixes ownership of both new music hooks before renderer fallthrough. The
new static contract verifies all 97 declared hooks are owned and emitted.
Build 29 was `e13c10952382eb394f2828e4099f03eeadeed3fdcb3abecce91edceb0530142e`.
Build 28 (level pause/lifecycle predecessor) is
`a8bb1bf0f221ebca25a02e8a76cc660ee2cdb396aefcb5a64a5c0f7620e31b14`.
Build 27 (miniature drawing) is
`97f052fe7a0a3b82f59b3f396de16df213cfc11fd063e0a7edc1bcb0955ee663`.
Build 26 (full Quartz Quadrant section palettes) is
`6ed7fd121a938428674c0e5ffba895d997f9c72fe8aff3959b044673028be337`.
No copyrighted inputs or converted
resources belong in Git; evidence paths below are private under `build/`.

The campaign now includes current main's independent Mighty, Shadow and
Megamix-stage controls and shared owned-ROM requirement. S1, S2, CD and
Megamix stages have 16 combinations, or 64 with the two character switches.
The journal reads the CD and Shadow predecessors and retains their capability
union, identity, completed state and parked collectibles when mods are removed.

CD now uses the original per-line background profiles instead of the generic
half-speed background. The work-buffer accesses have explicit logical word
order and defined signed wrap; local union halves are initialized. The backdrop
uses the original level setup's color index `$20`. CD combat classification
also covers the extended original hitbox shapes in Shadow homing and Mighty
Super attacks while excluding controllers, hazards and screen-relative poses.

The deeper independent comparison found Quartz Quadrant open sections loading
only 24 of the original 48 level colours. Converter, runtime and graph validator
now use all three level palette lines. The private content uses TSCD v3 and has
SHA256 `fe8055c36f6c2836d7dc6427da8aee1ad483ef363ae51c50011f03052cf73c02`.
Older experimental packs are rejected with an instruction to rebuild; saves are
unaffected. The eight changed QQZ act-1/2 blobs are the only level-data changes.

Metallic Madness had working miniature collision but an omitted drawing hook.
The renderer now halves each character's own mappings/tiles and the P1 Tails
tail attachment. The correction also works at native width with widescreen off;
it does not enable the widescreen CPU policy. Original S3K sprites/HUD and the
CD scenes' shared original Sonic art remain the presentation contract.

Real Start-pause captures exposed the external music source ignoring the
cartridge sound driver's pause flag. Both CD-DA and PCM clocks now suspend at
the level's V-int pause state and resume without resetting playback. The synthetic
audio test covers stereo ordering, the exact loop seam, both pause paths,
resume, fade and one-shot end. Audio qualification measures AC energy and reports
the native mixer's constant 380-unit DC offset separately; that offset is not
continued music. Build 29 additionally routes actual native-player invincibility,
speed-shoes and Game Over requests to CD's tracks, suspends the stage score for
the retained S3K drowning countdown, and clears external audio on menu return.
All five actual pickup/natural-expiry/last-life Game Over and menu-return
checks pass in `qa-cd-music-events-dev29`. These wide-off cases do not depend on
the subsequently corrected renderer hook ownership.

The water audit found a real integration failure: the generic dry-act handler
returned before the existing TTZ handler. Enabling TTZ then exposed title-card
completion reading a bubble PLC through an absent S1 blob table and corrupting
WRAM. TTZ now owns its hooks first, uses the native full-palette H-int handler,
and loads the cartridge's one-entry bubble PLC independently of other packs.
White-fade warp arrivals also load the bubble art. Development checkpoint
starts seed the converted water height and the correct Tails PLC tile base.

## Qualified results and their limits

| Area | Evidence | Scope |
| --- | --- | --- |
| Integration kernels | `cd-investigation/ctest-resume-integration.log` | 50 CTests passed after the Shadow/CD integration. The combined journal test performed 256 durable stores and took 1074.99 seconds; it was not hung. |
| Current water regression | `cd-investigation/ctest-water-dev24.log` | 50 tests passed in 83.27 seconds, excluding the unchanged combined-journal durable-store test. This run includes the added background vectors and updated combat tests. |
| Current shared regressions | `cd-investigation/ctest-dev28.log` | 51 applicable tests pass in 48.52 seconds, including the new audio stream/control test; the unchanged already-qualified combined durable-store test is excluded. |
| Current native parity | `qa-cd-native-parity-dev30/result.json` | All 49 artifacts remain byte-identical after the renderer size callback and both shared music hooks. |
| Current owned controls | `qa-cd-owned-controls-dev28/run.log` | All 64 actual resource/provider configurations pass with TSCD v3 and the shared required raw Megamix donor. |
| Water warp retries | `qa-cd-water-warp-checkpoints-dev25`, `qa-cd-water-warp-no-checkpoints-dev25` | Four Present-origin cases touch an actual checkpoint, warp, timeout, lose a life and retry at it in the destination period. Two source-no-checkpoint Past cases warp and retry at the destination start with checkpoint zero. |
| Distinct terrain gimmicks | `qa-cd-gimmicks-dev27`, `qa-cd-gimmicks-2-dev27`, `qa-cd-gimmicks-retry-dev27` | Four bar catches/movement/jump releases, eight conveyor direction samples, four exact bounce launches, four rotating-pole catches/movement/releases and all 63 selected current direction/type samples pass. Current switch-gated activation and inverted switch states are separate open interactions. |
| Actual size rays | `qa-cd-mini-dev27` | All eight identities pass source-placed shrink/grow beams without losing rings. All eight small/grown sprite pairs were inspected. The retained raw save-pointer nibbles confirm identities 0 through 7; native Player_mode alone is 0/1 for Mighty/Shadow and cannot identify them. |
| Actual scene returns | `qa-cd-shadow-ufo-2-dev27`, `qa-cd-ufo-character-4-dev28`, `qa-cd-ufo-character-5-dev28`, `qa-cd-ufo-character-7-dev28`, `qa-cd-shadow-warp-dev28` | Mighty, Mighty+Tails and both Shadow identities earn an actual UFO Time Stone and retain live/durable identity on return. Shadow+Tails also performs a real time-post/charge/visible-warp/arrival sequence with music and muted WAV. The first Shadow UFO assertion incorrectly compared the native base mode with extended identity; raw live/journal identity is six, and the corrected rerun passes. |
| Level pause | `qa-cd-pause-analysis-dev28` | Real Start pauses/resumes Present CD-DA and Past PCM. Both paused captures have zero AC energy, retaining only native DC, then music resumes. No pause flag was injected. |
| Release-profile portability | `cd-investigation/linux-release-3.log`, `qa-cd-linux-runtime-2-dev28` | Full Linux GCC release executable builds with all four development features off. Background, graph, renderer and audio-control tests pass. Actual menu Continue boots native, active-water TTZ1 Present and QQZ1 Past with no dispatch errors. CD cursors are explicit private save fixtures, not earned routes. Initial attempts looked for Windows gitdirs with Linux Git, lacked a standard NULL declaration, then used the build directory's executable/config rather than a per-case copy; all failures are retained. |
| Clean real captions | `qa-cd-window-titles-dev28/results.json` | Actual hidden Win32 gameplay and launcher captions are Sonic 3 & Knuckles when a debug port is supplied, without technical labels. |
| Actual six-control resources | `cd-investigation/stage-pack-owned-dev17.log` | All 64 configurations validated against owned resources; donor-ROM requirements remain enforced. |
| Combined runtime saves | `qa-cd-combined-permutations-2-dev17`, `qa-cd-combined-completed-dev20`, `qa-cd-combined-removal-dev20` | 130 normal, 65 completed and 96 identity/removal/restoration/backup cases passed: 291 total. Some starting identity/completion/collectible combinations are declared save fixtures, not joint earned playthroughs. |
| Native regression | `qa-cd-native-parity-dev24/result.json` | All 49 native image/RAM/VRAM artifacts remain byte-identical to the retained build-16 baseline. |
| Background math | `scd_background_test`, `cd-investigation/background-safety-portable-3-*`, `cd-investigation/background-motion-msvc.log` | All 70 original-start numeric vectors pass. An 840,000-update motion sweep has hash `19d3c27e` on MSVC, MinGW GCC and Linux GCC with ASan/UBSan. Motion determinism is not independent full-domain fidelity. |
| Enemy/spawner interactions | `qa-cd-spawner-probes-dev18`, `qa-cd-spawner-contact-reruns-dev18`, `qa-cd-spawner-scarab-rerun-dev18` | All ten formerly open representative subtypes now have actual interactions, completing the 69-group representative enemy inventory. Bigbom is an indestructible proximity/fire hazard; no stomp kill is claimed. This does not certify every placement or every secondary behavior. |
| Shadow in CD | `qa-cd-shadow-abilities-3-dev20` | 12 actual input/state checks pass, including homing defeat of a CD shape `$1A` enemy, Super, drain/release/revert and real timeout death/retry. |
| Mighty in CD | `qa-cd-mighty-attack-4-dev23` | Actual Megamix monitor pickup, real CD Super transformation/jump and actual shape `$1A` enemy defeat for 16 rings pass. The observed shield is an explicit prerequisite in a fresh CD process; cross-campaign inventory transfer is not claimed. |
| Water branch rules | `qa-cd-water-mechanics-2-dev24` | All 50 branch samples across ten TTZ variants match the actual original MMD program's targets. The act-3 entrance comparison accounts explicitly for the original's prior-frame camera input. Surface bob stays within four pixels. |
| Water physics and death | `qa-cd-water-mechanics-2-dev24`, `qa-cd-water-tails-dev24` | All eight identities pass actual underwater entry/exit, air reset and native movement profiles. Tails uses the retail P2 speed block even in Tails Alone. Sonic's advanced-air prerequisite causes actual drowning, life decrement and water-stage retry. |
| Water bosses | `qa-cd-water-bosses-dev24/4b02`, `qa-cd-water-boss-rerun-dev24/4a02` | Both variants pass actual chase damage/escape, bubble formation/contact/breathing, shots, defeat, escape, capsule and next-stage/save. Music is present and muted WAVs are retained. The first Good Future cold-load attempt hit the former 20-second debug-command timeout; the bounded 60-second rerun passes. |
| Water warp arrivals | `qa-cd-water-warps-dev24` | Ten real post/charge/scene/arrival cases pass with water active and music present. Saved water after arrival belongs to the restored previous checkpoint, not the arriving period's initial surface. |
| Water checkpoints | `qa-cd-water-checkpoints-dev24` | Actual Present checkpoint touch, timeout death and retry pass in both acts. The source Past layouts contain no checkpoints; those are separate start-retry cases. |
| Adaptive resize | `qa-cd-adaptive-reruns-dev24` | All 17 reruns pass six live window sizes, including all ten TTZ variants and the seven prior cold-start/time-out cases. Wider bounds have real terrain/object content, with no native comparison errors in these samples. |
| Current audio spots | `qa-cd-audio-regression-dev22` | Present CD-DA and Past PCM spots both pass against independent owned-track/original-driver references. Earlier full 28-period music and nine-bank 180-second PCM qualification remains retained. Scene/loop/pause/fade coverage is a separate gate. |

Initial water-exit tests used a generic posture helper that cleared the live
underwater bit before the native exit check. The corrected fixture writes only
position, velocity and control. Initial Tails profile checks read the wrong
speed block; the real P2 block contains the expected wet profile. Initial Tails
checkpoint visual captures used Sonic's tile base; the development fixture is
now corrected. These failures and their images remain retained.

## Independent original-disc reference

The supplied US 1.10 BIOS and owned disc now boot in a separate external
Genesis Plus GX QA core (`v1.7.4 aa74d79`, DLL SHA256
`cbd42d44b063fd45e0448b0976f9e6ae61128c90c0fe8bd3b1edbbab4fda82da`).
This is an external reference tool, not a linked/shipped dependency or an
implementation of the requested upstream backend. The BIOS identity remains
`807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`.

`qa-cd-original-stage-starts-1` qualifies all 70 selections by exact 256 KiB
Word RAM equality to the corresponding owned MMD. The real title-screen cheat
and stage-select menu perform selection. `qa-cd-original-settled-starts-1`
retains all 70 title-card-free original states/images. All seven start galleries
(`qa-cd-final-galleries-dev20/round-0.png` through `round-6.png`) were inspected
against native/adaptive candidate captures; terrain, backdrop and background
composition agree at these starts. The campaign deliberately uses S3K character
sprites/HUD. Original animation phases differ and were not claimed identical.
The MMZ1 Present missing-frame capture was rerun on build 22.

The built-in original stage select executes one MMD and returns. IPX's
`StageSelect` does not enter `RunLevel`'s multi-program warp/campaign loop.
These states therefore cannot establish original cross-MMD warp/exit behavior;
a normal campaign reference fixture is required for that comparison. Retained
failed original warp attempts returned to the title flow, not the desired MMD.
This does not negate the reference's in-level terrain/water/objects observations.

Matched-camera water-boundary comparisons for all four TTZ1 periods were
inspected in `qa-cd-water-visuals-2-dev24/gallery.png`: terrain, underwater
palette split, surface strips and authored adaptive margins agree. The first
paired attempt had different camera dead-zone framing and is retained.
Separate source-verified empty-foreground captures at (984,672), inspected in
`qa-cd-water-visible-characters-dev25.png`, show all eight identities' bodies,
life icons and intentional S3K underwater colours. Earlier terrain-occluded or
offscreen attempts remain unqualified. The Tails checkpoint art address is fixed.

## Final acceptance work

All seven deeper galleries in `qa-cd-deep-visual-review-dev25` were inspected.
Their independently loaded original MMD states and actual camera/player/water
observations accompany the images. The QQZ difference was fixed and all ten
QQZ rows were re-captured and inspected in `qa-cd-deep-framing-dev26/qqz-gallery.png`.
A normal left-edge approach resolves the candidate checkpoint's 16-pixel camera
dead zone without writing camera outcomes. Only two WWZ boss-shake phase offsets
remain separately scoped; exact cross-emulator animation/shake timing is not
claimed. The portable background motion hash alone also does not establish all
fractional-register fidelity.

Original warp and UFO1 main programs exactly match the owned MMD loaded bytes
before `qa-cd-original-scenes-1` captures. The build-28 visible warp is inspected;
initial white-fade/title-card images do not qualify the visible scene. The final UFO1
practice capture, actual timeout/return and no-award/no-save checks pass.
`qa-cd-scenes-final-review-dev30.png` was inspected: visible warp art and UFO1
terrain projection, background, palette, HUD and sprite artwork agree, allowing
independent animation/timer phase. UFO uses the original 256-pixel H32 scene. Original
scene select bypasses progression and cannot certify campaign transitions.

Actual enemy contact/hurt/ring loss passes in all seven rounds
(`qa-cd-hurt-dev28`). All eight identities pass real miniature emitter, actual
checkpoint contact, post/charge/warp, death/life loss and miniature checkpoint
retry (`qa-cd-mini-warp-0-dev29` through `-7-dev29`). Native-width/off miniature
shrink/grow also passes (`qa-cd-mini-off-dev30`), in addition to the renderer
kernel's flip, bounds, tile and CPU-policy checks.

All twelve source-selected switch/current contracts pass in
`qa-cd-current-switches-2-dev30`: actual floor-button contacts enable gated
currents or reverse existing ones. No switch/current flags are written. The
check accepts the source velocity or its native underwater +16 gravity update;
first attempts wrongly required only one completed-frame phase and are retained.

Build 30's final Bad Future MMZ boss replay (`qa-cd-final-native-transition-dev30`)
passes all four complete blade-count scripts, actual hits/defeat, escape,
Amy's scripted walk/ending, actual native AIZ arrival and durable stable ID 18,
CD completion and retained earned stones. Music is present with muted WAV.
This fixture uses declared navigation/position/protection prerequisites.

The build-30 kernel run passed 51 of 52 applicable tests. Its generated-profile
ordering failure was corrected without changing hook addresses, and both that
check and the new 97-hook ownership contract pass in
`ctest-dev30-hook-closure-2.log`. This qualifies 52 current checks; the unchanged
already-qualified durable combined-journal test remains excluded from this run.
All 70 final native/adaptive start pairs were re-captured on build 30 and all
seven `qa-cd-final-galleries-dev30/round-0.png` through `round-6.png` galleries
were inspected. Common terrain/background/backdrop/palette agree. S3K HUD and
characters are intentional; actual framing and independent animation phase are
recorded rather than claimed identical. Deeper and water/mini/scene comparisons
above retain their separate scope.

The distributed converter was run from an isolated copy of the allowlisted
tools and public helper, without checkout-only inputs. Both owned-disc conversions
pass: content SHA256 `fe8055c36f6c2836d7dc6427da8aee1ad483ef363ae51c50011f03052cf73c02`,
audio SHA256 `1f2318b96cc08a78a6c20852a74398b2a9c298472ccafc6244a9b5843ea68fab`.
Evidence is `qa-cd-distributed-converter` and `cd-investigation/distributed-converter-*.log`.

The Windows production build (all four development options OFF, SHA256
`f38e1ed22e439420c98afaec51c5bc233b6cb34051ba927a81a2256eb6c4692c`)
passes all 70 actual completed-file replay selections in
`qa-cd-replay-menu-release-all`. Inputs are real Up/Start presses; completion
and identity are declared private save prerequisites. No development runtime
overrides select stages. The ZONE 70 card and actual native transition were
visually inspected.

The current Linux production profile also passes actual native, wet TTZ and
QQZ Past boots in `qa-cd-linux-runtime-dev30`, with no runtime/dispatch errors.
The capture-path correction and exact byte transfer are disclosed in that
report; old capture evidence was restored. Windows/Linux full builds and the
portable kernels are qualified.

The campaign code is ready for its feature-branch delivery commit. Release
metadata is refreshed after that commit; the resulting archive must pass the
existing payload, DLL-closure and executable-package checks before handoff.

The retained 7,893-location survey still lists 780 source placements without an
active observation (including deliberate future removal and script conditions).
They remain explicit unverified observations rather than implied mechanics passes.

An owner handoff must use a fresh private folder, normal window/audio, adaptive
Fit, prefilled owned inputs and declared seeded completion fixtures, with actual
completed-file navigation across the intended acts/periods. A public archive must
exclude owned/converted/private data and the prohibited loading packages.

After campaign completion/validation/commit, implement the general upstream
Sega CD backend (`beads-ct0o`). The current engine still supplies only the optional
game-owned stereo mix callback and existing Genesis runtime. No general Sega CD
hardware/backend implementation is claimed. `beads-ritf` tracks campaign delivery and packaging; `beads-ct0o` tracks the
subsequent upstream implementation.
