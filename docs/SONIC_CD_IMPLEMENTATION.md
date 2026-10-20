# Sonic CD implementation record

Current integration findings, qualified evidence and remaining gates are tracked
in [SONIC_CD_VALIDATION.md](SONIC_CD_VALIDATION.md). Historical checkpoints below
retain their original scope and are superseded by that working ledger.

## Current owner direction (supersedes older sequencing below)

The owner resumed this as an active goal: finish Sonic CD end to end on the
CD branch, validate and commit it, then implement a general Sega CD recompiler
backend. An independent Sonic CD repository is authorized if useful. Shadow
has already merged to game main `4e387ee` and engine master `3f17adf`; it is no
longer queued after CD. CD's original AS IS snapshot branches remain preserved.
Work stays solo. CD is not to be merged into main/master before the required
backend work and final acceptance. A campaign checkpoint is not that backend.

The CD engine now includes current master on `332ff1f`. Game integration
checkpoint `7de1bf6` reconciles six independent default-off controls: S1, S2,
CD, Megamix stages, Mighty and Shadow. The last three still require the owner's
shared raw Megamix v3 ROM. Four stage switches have 16 combinations; with the
two independent character switches, validate 64 configurations.

SCS1 keeps all stable IDs and 548-byte records. It reads v1, CD v2/capability1,
and Shadow v3/capability2. Combining both writes v3/capability3, preserving an
immutable `.pre-v3-cap3.bak` from either predecessor. Capabilities remain parked
when mods are removed. Character availability uses Shadow bit16 and Mighty
bit32 independently of Megamix stage bit4. Unknown capabilities remain read-only.
Strict Windows GCC passes both predecessor migrations, normal/completed saves
across all64 masks, and the six-control resource/provider matrix. Integrated
runtime gameplay and broader CD fidelity checks still need qualification.

The owner's BIOS archive `E:/Downloads/Sega Mega CD BIOS.zip` has been qualified.
Use only its `Sega CD (U) - Model 1 v1.10 (1992).bin` entry: 128KiB, CRC32
`C6D10268`, SHA256
`807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`.
The private extracted input is under `build/private-inputs`; no BIOS is committed.
This records input identity, not original-program/backend boot validation.


Work remains on the prepared CD feature branches. The acceptance contract is
[SONIC_CD_HANDOFF.md](SONIC_CD_HANDOFF.md); central tracking is `beads-ritf`.

## Verified source and foundations (2026-10-19)

The owner's `Sonic CD (US).7z` contains one MODE1/2352 data BIN and 34 raw
audio BINs, with stored pregaps. The normalized data-volume SHA256 is
`db17110d5c9f80e7a0ec92ead5b23c9e524b53df9ff6b5069c7352e1d357925f`.
Normalized track identities are in `tools/trilogy/data/sonic_cd_us.json`.
Only this source revision is enrolled. JP, EU, CHD, ISO, MODE1/2048, WAV,
FLAC and other source formats have not been qualified.

`tools/inspect_sonic_cd.py` derives its manifest from the actual filesystem,
decoded layouts, camera bounds, object placements and source tables. Private
evidence is `build/cd-investigation/us-source-manifest.json`: 70 playable
variants and three demos inspect successfully. The current source conversion
produces 70 level blobs, 80 FM effects and 51 special/warp items, including
eight special-stage datasets. All eight now have an explicit No Save practice
entry and real win/timeout/return checks. Broader mechanics and final visuals
remain open; qualified audio checks and their limits are recorded below.
Failed conversion attempts are retained
beside the corrected third run; they are not gameplay evidence.

Converter/runtime reference: `sonsegajp/SonicAnthology` at
`8f5726a754e819cdff6f6a03690737239889c6fa`. Imported `scd_mmd.py`,
`scd_level.py`, `scd_sfx.py`, `scd_special.py` and the CD title helpers are
adapted under [the retained upstream license](../game/trilogy/LICENSE.md).
The original renderer, launcher and existing pack converters are retained.

The current development runtime passes 46 combined CTests with MSVC. The disc
reader passes 13 synthetic tests. The resource directory contract passes seven
fixture groups against the Python and C loaders under Windows and Ubuntu GCC
with `-Wall -Wextra -Werror`. The journal's 16 catalogs, v1 migration,
immutable backup, CD/Mighty parking, native deletion, future versions and
failed-write recovery also pass Windows and Ubuntu GCC. These kernel checks
do not establish 16 real-input campaign configurations or gameplay.

The engine adds only an optional emulation-thread stereo callback before the
generated WAV tap, master volume and delivery bridge. NULL preserves base
mixing. Its dummy-device test verifies base PCM, speaker-volume-independent
capture, replacement, unregistration and close/reopen cleanup. This does not
qualify CD host state for quickstates, rewind, or netplay.

The qualified private music resource is 450,265,556 bytes, SHA256
`1f2318b96cc08a78a6c20852a74398b2a9c298472ccafc6244a9b5843ea68fab`.
Its 34 tracks total 450,264,528 PCM bytes. This establishes conversion and
integrity, not in-game music selection or delivery.

## Resource contract

Content (`SCDP`) and CD-DA music (`SCDA`) are separate private resources.
They carry version 1, a source identity, explicit item names/offsets/sizes,
per-item CRC32, a whole-file length/CRC32 footer and bounded allocations.
Directories must be complete, ordered, nonoverlapping and exactly consume
the payload, with zero alignment padding. Unknown capabilities are rejected.

The content resource includes levels, special/warp scenes and all nine PCM
banks; it requires no S1/S2/Megamix resource. Music contains only normalized
tracks 02..35, excluding pregaps. Conversion rejects unsupported source or
audio identities and stages output beside its destination for atomic replace.
Source files remain untouched. After conversion the source CUE/BIN files are
not runtime dependencies. Missing CD-DA music can therefore be reported
separately from stage geometry and past PCM music. The independent CD mod is
default off and marked experimental in recomp-ui. CD-only gameplay works;
its full acceptance matrix is still in progress.

The runtime guest mapping belongs to CD's selected bank: common header and
Mighty attachment below `$480000`, the level window at `$480000..$600000`,
then the FM bank. Existing independent packs retain their original bytes and
guest pointers. Synthetic dispatch remains at `$7E0000` and above.

## SCS1 v2 journal

Stable IDs remain S1 0..6, S2 7..17, native 18..31, Megamix 32..38 and
completion 39. CD receives exact act/period IDs 40..109. Each round uses ten
entries: Present acts 1/2, Past acts 1/2, Good Future acts 1/2/3 and Bad Future
acts 1/2/3. Display order is separate:
S1 -> S2 -> CD -> native -> Megamix. Mask bits 0/1/2/3 mean S1/S2/MM/CD;
completion bits 0/1/2/3/4 mean S1/S2/native/MM/CD.

The file remains 548 bytes: 32-byte header, eight 64-byte slots and four-byte
big-endian CRC32. Version is header byte 4, pending projection byte 5;
version 2 requires CD capability byte 6 equal to 1. Other header fields stay
reserved. Each slot retains the v1 layout. Bytes 48/49 hold CD act and unique
seven-stone mask; bytes 50..51/52..53 hold big-endian generator/projector
masks (bit `round*2+act`, for acts 1/2). The stable cursor holds the act/period.
Native emeralds are separate from Time Stones.

Existing journals remain v1 until CD is enrolled or CD state is written.
Migration preserves an immutable `.pre-v2.bak`, including migration recovered
from the normal `.bak`. Once migrated, removal of CD retains version 2 and
its parked state. Explicit native deletion clears the whole slot. Native
SRAM retains only native levels/characters and its original checksums.

Unknown versions/capabilities in either companion are retained byte-for-byte;
campaign hooks are suspended so native saving remains available. Older
binaries do not understand v2: downgrade requires preserving the v2 file and
restoring a v1 pre-migration copy. An older binary can otherwise overwrite an
unrecognized companion; downgrading readers are not promised compatibility.

## Development validation and remaining acceptance

Development build 7, SHA256
`272d01b5db46760494e7f62528dc7b0eeffb6882794a5843ae17f946999737d9`,
passes 46/46 combined CTests and 49/49 native image/RAM/VRAM byte comparisons
(`build/qa-cd-native-parity-dev7`). It remains a trace-enabled development
build, not the owner handoff or the mandatory final visual comparison build.

The original-ROM Data Select code resolves the zone at `$D57A`, before setting
`Save_pointer` at `$D5B2`. Importing CD state in the former hook made resumed
files repeat UFO 1 despite a retained journal stone. The failed real-save
chain is preserved in `build/qa-cd-ufo-awards-chain-dev6`. CD state now imports
at level entry, after selection settles; pending menu saves cannot capture a
previous slot's in-memory CD state. With the fix, the actual saved UFO chain
continues through all seven unique stones, across process restarts, without
changing native emeralds (`build/qa-cd-ufo-real-entry-probe-dev6` for stone 1,
`build/qa-cd-ufo-awards-chain-dev7` for stones 2..7). It uses real giant-ring
contact and tally, 50 prerequisite rings, player X/Y UFO positioning, real
jump collisions, results, identity-preserving act-2 return and exact saves.
No UFO, timer, damage, entry, award or completion flags are injected.

An earned seven-stone save also passes a real act-2 sign/tally transition
into Good Future act 3, with the exact saved period
(`build/qa-cd-seven-stones-good-exit-dev7`). Both PPZ boss variants pass real
damage phases 3/2/1, defeat, escape, capsule break, tally, next-round load and
stable save (`build/qa-cd-ppz-bosses-dev7`). Player attack posture, position,
camera travel and protection bypass route/attack execution, not outcomes.
An earlier PPZ probe missed phase 2 between host samples; its eight-frame
game log retained that phase. The corrected verifier uses both observations,
and both variants were rerun rather than rewriting the failed evidence.

All 56 source sign/tally/next-stage/save cases pass on immutable development
build 5: 53 in `build/qa-cd-sign-exits-dev5` and three isolated startup retries
(`4a01`, `5100`, `5b00`). The original startup failures remain recorded.
All 14 generators and 12 projectors pass contact/break/durable-bit checks
(`build/qa-cd-collectibles-dev6`); Metallic Madness has no projectors. A new
build-7 chain passes all 26 accumulated contacts through resumed processes,
ending with generator/projector bytes `3fff0fff`
(`build/qa-cd-collectibles-chain-dev7`). All eight No Save
practices pass natural timeout and real six-UFO jump-collision wins, return
without changing durable collectibles or saved native files
(`build/qa-cd-practice-timeouts-dev5`, `build/qa-cd-practice-success-probe-dev6`,
`build/qa-cd-practice-success-dev6`). UFO navigation is bypassed with a
trace-only player X/Y follow helper; collision, jump height, defeat and awards
remain game-owned.

All 70 source-derived time-post direction cases now have passing outcomes:
66 in `build/qa-cd-warps-dev8`, with its four failed fixtures retained; all
four corrected reruns pass in `build/qa-cd-warp-fixture-reruns-dev9`.
A used destination post can immediately
start a second charge, so the corrected leak check stops player motion and
observes cancellation instead of assuming an arrival invincibility bit is a
leak. Missing live samples can be recovered from completed-frame history.
Earlier ambiguous/failed fixtures are retained in `build/qa-cd-warps-dev6`.
The stop fixture now writes only player velocities: rewriting the whole
player from an older frame could restore a charge flag already cleared by
the game. Post activation is checked both immediately and during charging.
The trace-only Right-held position/velocity helper leaves object control
untouched, because MMZ deliberately holds warps while an object owns Sonic.

Development executable SHA256
`07850a2af6ba5b862d531a88f576c307fde97f140edccd36ed29e7fbe7f4b449`
passes all 49 existing native screenshot/RAM/VRAM artifacts byte for byte
(`build/qa-cd-native-parity-dev5`). All 16 actual private resource/settings
combinations pass the C provider test (`build/qa-cd-pack-matrix-dev5`). All
16 actual New Game combinations and seven older process recovery cases also
pass (`build/qa-cd-permutations-dev5`); these are distinct from CD-specific
collectible/removal/completed-file resilience, which still needs coverage.
Portable geometry and malformed graph tests pass strict Windows/Ubuntu GCC;
the private corpus validates 3,163,648 cells across all 41 larger layouts.

The CD-only real-menu bringup succeeds at `$4000`, with movement and rings
observed; its initial two images were inspected. A multi-stage debug survey
reached 21 variants before a watchdog on `$4801`. That stage passes a fresh
isolated launch. The original survey used separate zone/restart writes across
live frames, exposing a new stage ID while its old window remained mapped;
the later atomic fixture also retains four interior-dispatch addresses. The failed logs remain
under `build/qa-cd-entries-dev5`. Do not treat stage entry or handler coverage
as a mechanics pass.

The atomic fixture produced entry/movement captures for all 70 variants
(`build/qa-cd-entries-atomic-dev5/results.json`), but it is superseded for clean
entry qualification. Even a single live zone/restart write can expose the new
zone before its window loads. The replacement uses a fresh process per stage
and the existing level-entry override. All 70 fresh-process entries and
movement checks pass with no interior/unsafe-floor fallbacks
(`build/qa-cd-fresh-entries-dev7`). Real exits, warps, deeper
mechanics and final visual fidelity remain separate checks.

Runtime resource enrollment, independent CD bank ownership, gameplay hooks,
campaign transitions, 70-entry replay navigation and durable CD state now
compile in the development runtime. These remain subject to gameplay QA.
The first real-menu CD-only probe safely fell back to native S3K after its
object audit found three PPZ act-3 shared IDs mapped to unregistered slots.
The converter now shares their existing acts 1/2 implementations; the failed
probe is retained under `build/qa-cd-bringup-1` and is not CD entry evidence.
The combined run passed 42/43 tests; the failing new save assertion incorrectly
expected replay to revoke an earned native clear. Its corrected check and a
separate pre-clear CD parking/native-progress fixture pass strict GCC.

An independent Sonic Retro original disassembly at
`f8cf3f0af7bf419d18182ba98554f09307ad3cc6` matches all 104 comparison checks
across R11A-D and R61A. Its demo chunk comparison remains discrepant and is
excluded from playable coverage. Original code confirms seven Time Stones
force good futures, independently of native emeralds. The eighth UFO dataset
is an original sound-test secret (07/07/07). The CD mod exposes a default
Normal campaign / UFO practice 1..8 choice: starting No Save with a practice
stage chosen enters CD directly and then that UFO stage. Practice returns to
Palmtree Panic without awarding stones or changing saved campaign progress.
Practice entry, both outcomes and return durability now pass the development
checks above. Full original-game runtime/visual equivalence remains unqualified.

Content conversion now requires the `scd_pack_validate` executable (build that
CMake target or supply `tools/build_sonic_cd_pack.py --validator <path>`). It
validates the nested runtime graph before replacing a previous output. Music
conversion continues to validate every normalized track against the enrolled
source hashes. The raw BIN little-endian stereo interpretation also agrees
with the independent [Genesis Plus GX drive implementation](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/core/cd_hw/cdd.c#L1554).

PPZ and CCZ bosses pass in both futures on build 8; TTZ and QQZ pass in both
futures on build 9. Evidence is `build/qa-cd-{ppz,ccz}-bosses-dev8` and
`build/qa-cd-{ttz,qqz}-bosses-dev9`. These check actual damage/event phases,
escape, capsule, tally, next round and exact save. TTZ also observes all 16
bubbles, a real bubble restoring air from 2 to 30, and shots. Its chase
positioning respects the player's `$800` wrap before the pod's `$880` wrap.
QQZ uses real Right input through the entire treadmill wear sequence with
zero position resets in both runs. Completed-frame history retains its zero
HP crossing before the belt's natural signed underflow while slowing down.
Navigation/attack-posture/protection bypasses are explicit in every result.
All imported `TRILOGY_BOSSAUTOHIT` shortcuts have been removed; acceptance
tests never enabled them. WWZ also passes both futures, with real hits on
each floor, three timed beam/block/launch events, rubble/spikes, defeat,
escape, capsule, next round and save (`build/qa-cd-wwz-boss-probe-dev9` and
`build/qa-cd-wwz-good-boss-dev9`). Platform riding and hazard avoidance are
positioning bypasses, not qualified mechanics.

SSZ wins pass both futures (`build/qa-cd-ssz-race-probe-dev9`,
`build/qa-cd-ssz-good-race-paced-dev9`): actual start/gate sequence, laser,
Metal's dash/electric field, spike-block smashing, player finish, barrier crash,
Eggman escape, Amy capsule/hug, tally, next round and save. Both loss variants
also pass with God disabled: Metal wins, his finish gate clamps the loser,
Eggman's actual laser kills Sonic, lives change 3 to 2 and the stage naturally
retries without CD completion/collectible awards (`build/qa-cd-ssz-race-loss-dev9`).
Player/camera teleports bypass racing skill and terrain traversal.

All 14 boss/race variants have passing outcome evidence. MMZ both futures
pass on development build 12, SHA256
`efc5dc05268d9515175e2d534f79d8c313c71925fe28b4425d18e7148d0bd7e4`
(`build/qa-cd-mmz-walk-bosses-dev12`). Three fireflies are defeated through
real vulnerable-window contacts; afterimages and sparks are observed. The
final boss completes one whole source-derived attack script at each of four
blade counts before each real hit. Script lengths are 28/28/22/12 entries;
completed-frame history retains brief boundary actions. Amy's actual pad
script, fade, tally and transition into native S3K pass, with stable cursor 18,
CD completion, identity and collectibles preserved and native emeralds unchanged.

The stricter MMZ probe found a real integration bug: S3K clamps the player to
camera max + `$128` (`$F88`), before Amy's `$F90` turn. The original CD player
uses `320-16`, plus 56 outside the fight. A margin of 8/64, scoped to MMZ's
Amy sequence and player 1, now supplies those original bounds without moving
the camera. All pad states 0/2/4/6 and the actual right/left walk are observed;
per-frame history catches the single exact turn frame. The failed pre-fix
probe and stricter sampling failures remain retained. Native build-12 parity
still matches all 49 image/RAM/VRAM artifacts exactly. Final combined-head
regressions remain required after Shadow.

## Independent audio qualification

Development build 9 SHA256 is
`73446967214af7f9a833b5ca14e157f613acf3be0fda38cdbc231fa135596b8e`.
The 28 period captures on build 8 pass selection/delivery/no-clipping checks
(`build/qa-cd-period-audio-dev8`). All 21 CD-DA period tracks correlate with
the owned original BIN stereo samples at greater than 0.99988 per channel.
Raw track reads use the original CUE start/pregap information independently
of the runtime music pack.

`tests/runtime/sonic_cd_pcm_reference.py` executes the owned banks' original
68000 bytes in Unicorn, against unmodified Genesis Plus GX RF5C164 code at
`49c584764893b0505ac7f768a754f97330fa4392`. The caller supplies the reference
checkout; neither that core nor disc bytes are vendored or delivered. The
bank has a 16-byte filename header: driver entry is `$40010`, variables
`$40018`. Calls are atomic at 512-chip-sample service boundaries. All nine
banks pass 180-second original/transcribed driver variable comparisons with
zero differences (`build/qa-cd-original-pcm-long`). Commands for voices,
pause, resume and fade also pass; the complete valid PPZ voice index passes
`build/qa-cd-original-pcm-all-voices-v2`.

The independent check exposed an extra sample-address advance at a loop
marker. The fixed chip also reloads a stopped channel's address on ST writes.
The [Sega PCM manual, section 4-8](https://www.megadrive.org/elbarto/megacd/Official%20Sega%20CD%20Manual/MegaCD/PCM-Sound/pcm-13.gif)
specifies 16-bit accumulated output; the transcription's extra six-bit
quantization has been removed. The multiply/truncate/add-subtract order
differs from GX's signed-shift rounding by at most one LSB per channel (eight
summed). Reference results record that bound rather than claiming bit equality.
Raw correlations exceed 0.9999998. Bus timing and analogue filtering remain
outside this driver/sample-stream qualification.

All seven in-game Past themes on build 9 correlate with that original-driver
reference above 0.99995 per stereo channel, with no clipping
(`build/qa-cd-past-audio-analysis-dev9` for six themes and
`build/qa-cd-past-audio-mmz-analysis-dev9` for MMZ). Initial analysis failures
used the wrong raw-rate resampling ratio and a command-interrupted reference;
their captures remain unchanged. The corrected analysis verifies build/resource
identities and reuses those same WAV/RAM/log captures with pure-music references.

The unused PPZ `$B0` sound contains undefined `$FE` bytecode: the original
driver runs into data and faults. Its first transcription probe hung in an
empty stream refill (debugger stack retained). Undefined bytecode and invalid
empty/out-of-bank streams now stop safely; the production-only invalid-command
probe passes `build/qa-cd-pcm-invalid-safe`. This is safe rejection, not claimed
original-driver equivalence. The guarded valid-bank long sweep passes all nine
180-second streams (`build/qa-cd-original-pcm-guarded-long`), and the guarded
valid voice sequence passes `build/qa-cd-pcm-guarded-voices`.
Music/voice delivery during bosses, warps, results, UFO, pausing and pack removal
still needs broader scene/transition checks.

Mighty's initial 12-process removal/reinstallation/recovery chain passes on
build 12 (`build/qa-cd-save-resilience-mighty-probe-dev12`), retaining seven
earned stones and identity. All 72 normal-file cases across six identities
also pass (`build/qa-cd-save-resilience-all-state-2-dev12`): CD disabled/enabled,
removed/restored, damaged/repaired; Mighty entitlement removed/restored; all
content removed/restored; and journal backup recovery. Cursor 41, all seven
stones, 14 generators and 12 projectors survive. The final CRC-valid journals
retain completion 0, so these probes have not awarded campaign clears. With
all content absent the journals remain byte-identical. All 72 completed-file
cases pass too (`build/qa-cd-complete-resilience-all-state-dev12`): cursor 39,
completion bits 31, collectibles and identity survive all the same operations,
with real menu replay selection and safe native records. Saved-file Continue
and completed-file replay each pass every one of the 16 mod combinations
(`build/qa-cd-saved-permutations-dev12` and
`build/qa-cd-complete-permutations-dev12`). These deliberately combine separately earned stone and
generator/projector saves into declared resilience fixtures; they claim no
joint playthrough. Completion fixtures are also explicit prerequisites. All
tests operate through actual menu Continue/replay input without runtime zone,
character or award overrides.

The source-derived route survey now understands independent CD resources.
Initial probes in PPZ1 Present and MMZ2 Past capture all 103/108 planned
locations and observe all 188/200 placements, with no runtime errors or wrong
stage captures (`build/qa-cd-route-survey-probes-dev12`). The completed 70-variant
survey is recorded below. This establishes positional/object coverage only; terrain,
enemy and gimmick interactions require separate outcome evidence.

The actual S2->CD boundary passes on build 12 (`build/qa-cd-s2-to-cd-dev12`):
real Silver Sonic and Death Egg Robot damage/defeat, escape/ending and fresh
Palmtree Panic entry. The resulting save has cursor 40, S2 completion bit 2,
identity 1 and empty CD collectibles. Position, attack posture and protection
bypass navigation/attack execution; no boss HP or transition flags are set.

The native S3K->Megamix boundary also passes with CD installed, using Mighty
on build 13 (`build/qa-cd-native-to-mm-dev13`). The probe starts at the final
boss segment, bypassing earlier Death Egg encounters. Real contact damage,
defeat, chase, ending and credits lead into Sunny Shores; no boss HP, ending
or campaign-transition flags are written.

The native-only deletion/recreation probe exposed a real save bug: an old CD
cursor and collectibles could return if the file was erased and recreated
before packs were reinstalled. The journal now observes actual native save
writes whenever an existing companion is present, even with every mod absent.
It does not project addon data into native RAM or force cartridge IO in that
mode. Actual deletion clears the parked state before a recreated file can
inherit it; unchanged native records leave the companion untouched. A binary
that does not implement this observer cannot record an intermediate deletion
that is no longer visible in the final native record.

Build 13 SHA256 is
`c1c55c3fd6a47d4e8325d2a5a2a604e490240341b4eb5d2596ad794dae49da26`.
It passes 46 CTests, all 49 native baseline artifacts, all 19 save-edge cases
(`build/qa-cd-save-edges-2-dev13`) and the affected 12 normal/12 completed
Mighty resilience cases. Future schema/capability in either companion remains
read-only with native saving available. Moved content paths, absent/damaged/
restored music, actual native-menu deletion, native recreation and two subsequent
reinstalled Continue launches pass. The pre-fix resurrection and initial
post-fix verifier's obsolete byte-preservation expectations remain retained.

All 14 Present acts that actually place checkpoints pass real starpost touch,
timeout death, life decrement, natural retry and stable checkpoint return:
12 cases in `build/qa-cd-checkpoints-dev12`, plus PPZ2/SSZ2 reruns in
`build/qa-cd-checkpoint-reruns-dev13`. The first retry window ended during their
normal fade/title sequence; the corrected verifier waits for the actual return.
Timeout is an explicit timer prerequisite, not injected death/retry state.
Post-warp checkpoint state and hurt/game-over behavior remain separate checks.

The full positional survey now captures all 7,893 planned locations across
70 variants (`build/qa-cd-route-survey-dev12`, with both WWZ1 futures rerun in
`build/qa-cd-route-survey-wwz-reruns-dev14`). It lists 14,184 source placements;
780 have no active-object observation, including future self-destruction and
script-dependent objects. These remain explicitly listed, not declared working.
Both WWZ1 future surveys initially crashed because the hold cheat zeroed a
tunnel's already-assigned ground speed, violating the original DIVS prerequisite.
The corrected survey preserves that speed. Separate unfrozen runs pass actual
catch, route movement and release in both futures (`build/qa-cd-wwz-tunnels-dev13`).

Fourteen real Future-post warps with seven separately earned stones all select
Good Future; another fourteen with the separately earned generator flags do
the same (`build/qa-cd-seven-stone-future-warps-dev14` and
`build/qa-cd-generator-future-warps-dev14`). Neither sweep injects award or warp
outcomes. The source-derived enemy subtype sweep currently has 59 defeat
passes and ten actual enemy/spawner subtypes requiring further interaction
probes. The initial inventory also included Koma, which is a spring platform;
that non-enemy is now excluded from the kill inventory.

Adaptive probes found two real background integration defects. CD's native
writer drew only source rows 0..255 despite a nonzero vertical scroll, and its
cell reader bypassed the complete 16-bit CD grid/QQZ background selection.
It now draws the visible circular-table rows and reads the same complete live
geometry used by collision/rendering. Build 16 SHA256 is
`2d1d5b4beee8fbf998e04d1050e9f4e15ab61fe0517f79ea441bab7fe3d22dfc`;
46 CTests and all 49 native baseline artifacts pass. All nine tested QQZ/MMZ
background locations now report zero background mismatches through six live
Fit window sizes, from 4:3 through a 4000x500 custom window and back.

The initial 70-middle-location resize sweep on build 15 passed 50 cases and
failed 20; its failures are retained. Some middle fixtures have stale native
foreground rows or enter death/fade during observation. Nine isolated normal
start reruns pass the affected foreground comparisons. Those reruns do not
qualify traversal or replace the required meaningful-location visual review.
Six further normal-start cases pass all six live resizes on build 16
(`build/qa-cd-adaptive-start-reruns-dev16`), including both WWZ bosses and the
MMZ1 variants whose arbitrary middle fixtures died. Across the retained sweeps,
each of the 70 variants now has a passing resize case; unresolved middle-location
differences remain open and this is not a final visual fidelity qualification.
The screenshot dimensions are correctly rounded (21:9 at 224 lines is 523
pixels). Stage-selection helpers now reject unknown IDs: PowerShell converts
unquoted comma-list entries such as `4e00` to `4`; quote such lists.

Original background/parallax fidelity remains open: CD still uses the imported
generic half-speed background rather than every original deformation routine.
Independent reference investigation now also uses Clownacy's Gems Collection
C decompilation at `fd4e17382beb6c705032a533b4db307fe6f1ea9b` (CC0), privately
checked out under `build/reference-scd-c`. A private isolated prototype extracts
the 70 background-only function sets (42B shares 42A, 42D shares 42C as its
original build does), with camera/events/drawing omitted for analysis. It
compiles and produces scroll samples for all 70, but has not been integrated
or qualified against the original Sega CD routines. It is not a fidelity pass.

The independent original-code reference now executes the owned MMD's scroll
initialization, original camera/scroll routines, both original terrain-plane
drawers and original tile animation in Unicorn. A checked VDP write/DMA sink
retains the actual name-table/pattern bytes. All 140 captures (70 variants,
fresh states after 1/64 updates) complete in
`build/qa-cd-original-animated-terrain-starts`, using
`tests/runtime/sonic_cd_background_reference.py`. Its two 24-bit RAM aliases
share the same backing storage; owned program bytes remain unchanged. Only the
level-event call is omitted. Palette cycles, objects, water palette splits,
event-driven scene changes and bus timing remain outside this reference scope.
Captured is not a gameplay or production-fidelity pass. Initial TTZ reference
images exposed placeholder patterns because the first reference omitted tile
animation; executing the original animation/DMA replaces those placeholders.
The earlier reference captures and signature failures remain retained.

The private C-profile extraction now links without allowing duplicate definitions.
Original-code investigation has identified initializer, gradient, water-band and
array-index differences in the C port. A single stationary 64-update sample now
matches background Y/224 line values exactly for 67 profiles; the three QQZ Bad
Future profiles differ by one pixel in some lines, with original register/fraction
carry observations retained. This narrow sample does not qualify motion, the full
profile domains, portability or integration. The production background is still
generic; replacing it and resolving the deeper render comparisons remain open.

Remaining acceptance includes broader geometry/gimmick/enemy/character interactions,
checkpoint/death/warp state and additional native progression cases,
scene audio behavior, adaptive rendering, full-runtime portability and final owner
delivery. The mandatory final independent visual comparisons remain open.

## Follow-up after CD

The owner requested Shadow from Sonic Megamix using the Mighty integration
as the model. This is queued in `beads-8y9v`, dependent on CD validation.
The final combined build must receive regression checks and mandatory visual
comparisons against independent original-game or verified asset references
before the owner's playtest handoff. Earlier development captures do not
satisfy that final comparison gate.

## AS IS checkpoint and required engine refactor (2026-10-19)

The owner clarified that the final target includes a general Sega CD
recompilation backend in segagenesisrecomp. Current campaign code and the
engine's optional game-owned audio callback are not that backend. The owner
authorized preserving the current work AS IS now, with the architectural
refactor deferred until before merge. `beads-ct0o` tracks that engine requirement;
it blocks CD completion/merge. Both repositories have a matching
`checkpoint/sonic-cd-as-is-2026-10-19` snapshot branch. Keep main/master unchanged.

This checkpoint includes the unfinished, isolated `scd_background` component,
its pinned CC0 profile generator/manifest and the extended original-code
comparison tool. The component is not connected to CD's renderer: production
scrolling still uses the generic implementation. Standalone MinGW builds used
`-fwrapv -fno-strict-aliasing`; those assumptions and portability still require
review before integration. Negative signed shifts have explicit register-style
helpers, and MMZ3 Good reproduces the original DIVU overflow/EXT behavior.

Candidate 3 passes all 70 original-start math cases over 64 updates each
(`build/qa-cd-background-candidate-3-starts`). At the declared camera prerequisite
(1000,100), 67 profiles pass and the three QQZ Bad profiles retain bounded
one-pixel differences (`build/qa-cd-background-candidate-3-upper-math`). The first
combined upper-position capture also had eight CCZ animation/reference failures;
its outcome is retained and is not a game-mechanics failure or pass. The
math-only rerun explicitly excludes drawing/animation. Earlier candidate failures
and corrected TTZ water-boundary/MMZ division cases remain retained. These are
component qualifications, not complete profile-domain or gameplay acceptance.

No general backend refactor, final production build, final independent visual
comparison or owner playtest delivery is claimed by this snapshot. Shadow is
still queued after Sonic CD. The prior integrated development runtime remains
build 16, with 46 CTests and 49 native baseline artifacts passing.
