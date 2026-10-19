# Sonic CD implementation record

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
eight special-stage datasets. Dataset accessibility, mechanics, visuals and
audio fidelity remain unverified. Failed conversion attempts are retained
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
separately from stage geometry and past PCM music. Runtime enrollment and its
UI policy are still being implemented; the container alone is not playable.

The planned guest mapping belongs to CD's selected bank: common header and
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

## Outstanding acceptance

Development executable SHA256
`07850a2af6ba5b862d531a88f576c307fde97f140edccd36ed29e7fbe7f4b449`
passes all 49 existing native screenshot/RAM/VRAM artifacts byte for byte
(`build/qa-cd-native-parity-dev5`). All 16 actual private resource/settings
combinations pass the C provider test (`build/qa-cd-pack-matrix-dev5`). Actual
new-game permutations are running separately; these are distinct checks.
Portable geometry and malformed graph tests pass strict Windows/Ubuntu GCC;
the private corpus validates 3,163,648 cells across all 41 larger layouts.

The CD-only real-menu bringup succeeds at `$4000`, with movement and rings
observed; its initial two images were inspected. A multi-stage debug survey
reached 21 variants before a watchdog on `$4801`. That stage passes a fresh
isolated launch. The original survey used separate zone/restart writes across
live frames, exposing a new stage ID while its old window remained mapped;
an atomic selection fixture is now under validation. The failed logs remain
under `build/qa-cd-entries-dev5`. Do not treat stage entry or handler coverage
as a mechanics pass.

The corrected atomic fixture passes entry and real movement input for all 70
variants (`build/qa-cd-entries-atomic-dev5/results.json`), with no crash errors.
Dispatch telemetry retains eight fallback events at four addresses; their
classification remains to be reviewed. The multi-stage fixture resets its
checkpoint and supplies 63 lives, explicitly bypassing those prerequisites.
All real exits, warp transitions and deeper mechanics remain separate checks.

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
Runtime entry, outcomes and return behavior still require validation.

Content conversion now requires the `scd_pack_validate` executable (build that
CMake target or supply `tools/build_sonic_cd_pack.py --validator <path>`). It
validates the nested runtime graph before replacing a previous output. Music
conversion continues to validate every normalized track against the enrolled
source hashes. The raw BIN little-endian stereo interpretation also agrees
with the independent [Genesis Plus GX drive implementation](https://github.com/ekeeke/Genesis-Plus-GX/blob/master/core/cd_hw/cdd.c#L1554).

All mechanics, process-level removal/reinstallation, screenshot
review, original-reference comparisons, 49 native artifacts, portability of
the full runtime and the final owner playtest delivery remain outstanding.

## Follow-up after CD

The owner requested Shadow from Sonic Megamix using the Mighty integration
as the model. This is queued in `beads-8y9v`, dependent on CD validation.
The final combined build must receive regression checks and mandatory visual
comparisons against independent original-game or verified asset references
before the owner's playtest handoff. Earlier development captures do not
satisfy that final comparison gate.
