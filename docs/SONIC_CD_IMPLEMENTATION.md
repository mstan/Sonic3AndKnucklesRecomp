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

The combined game/engine foundations pass 42 CTests with MSVC. The disc
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
completion 39. CD receives period-row IDs 40..67. Display order is separate:
S1 -> S2 -> CD -> native -> Megamix. Mask bits 0/1/2/3 mean S1/S2/MM/CD;
completion bits 0/1/2/3/4 mean S1/S2/native/MM/CD.

The file remains 548 bytes: 32-byte header, eight 64-byte slots and four-byte
big-endian CRC32. Version is header byte 4, pending projection byte 5;
version 2 requires CD capability byte 6 equal to 1. Other header fields stay
reserved. Each slot retains the v1 layout. Bytes 48/49 hold CD act and unique
seven-stone mask; bytes 50..51/52..53 hold big-endian generator/projector
masks (bit `round*2+act`, for acts 1/2). The stable cursor holds the period.
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

Runtime resource enrollment, CD bank ownership, gameplay hooks, campaign
transitions, UI/replay navigation and durable runtime CD state remain to be
integrated. All mechanics, process-level removal/reinstallation, screenshot
review, original-reference comparisons, 49 native artifacts, portability of
the full runtime and the final owner playtest delivery remain outstanding.
