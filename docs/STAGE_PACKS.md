# Experimental campaign stage mods

Six independent mods extend **Sonic 3 & Knuckles**: **Sonic 1 stages**,
**Sonic 2 stages**, **Sonic CD stages**, **Sonic Megamix stages**, **Mighty** and **Shadow**.
All are experimental and disabled by default. Enable any combination on the
launcher's Mods page. Campaign order is Sonic 1, Sonic 2, Sonic CD, native Sonic 3 &
Knuckles, then Megamix; absent or disabled campaigns are skipped. Sonic 1
supplies 19 acts and Sonic 2 supplies 20. Megamix supplies 19 acts. Mighty
and Shadow each add their own solo and Tails-partner Data Select choices;
neither character requires the Megamix stage checkbox or the other character.
The standalone Sonic 3 and Sonic & Knuckles targets do not enroll these mods.

The game keeps its existing name, launcher, pause menu and native abilities.
Sonic CD uses its UFO special stages; the other campaigns retain Blue Spheres. Upstream's achievements,
additional abilities, video settings and replacement launcher are not enrolled.

For the separate owned-disc conversion and its 70 act/time variants, see
[SONIC_CD.md](SONIC_CD.md).

## Build your stage bank

No cartridge data or converted bank is distributed. With Python 3.10 or newer
and Pillow installed (`python -m pip install Pillow`),
run the following from the source checkout, supplying your own ROM paths:

```text
python tools/build_stage_pack.py --s1 sonic1.bin --s2 sonic2.bin --s3k sonic3k.bin --out sonic1-2-stages.bin
```

Export independent packs from that converted bank:

```text
python tools/split_campaign_packs.py --bank sonic1-2-stages.bin --content sonic1 --out mods
python tools/split_campaign_packs.py --bank sonic1-2-stages.bin --content sonic2 --out mods
```

Sonic 1 and Sonic 2 each have their own **Campaign content pack** resource.
Megamix stages, Mighty and Shadow share one **Campaign content pack** and one
required **Sonic Megamix v3 ROM** resource. Selecting either path from any of
these three entries fills it in all three immediately, including after restart.
The raw ROM (.bin/.md/.gen) must be 1,283,224 bytes with CRC32 `66ECB4F4`;
extract ZIP archives first. The picker verifies it and boot verifies it again.
A converted pack alone cannot activate any Megamix addition. If the ROM is
removed or invalid, all three fall back safely with settings/progress retained.
Replacing it restores only the independently selected additions.

Default content pack paths are
`mods/sonic1-stages.pak`, `mods/sonic2-stages.pak`, and
`mods/megamix-stages.pak`, relative to settings. Legacy combined `.bin` banks
remain accepted for migration; assigning one never enables another mod.
Missing or damaged resources are skipped without refusing the base game.
Their requested settings and progress remain, so putting the file back at
the same path restores the campaign on the next launch.

The converter requires these exact raw cartridge revisions:

| Source | Bytes | CRC32 |
| --- | ---: | --- |
| Sonic 1 REV01 | 524,288 | `AFE05EEE` |
| Sonic 2 REV00 (header checksum `DEB8`) | 1,048,576 | `24AB4C3A` |
| Sonic 3 & Knuckles lock-on | 4,194,304 | `63522553` |

Sonic 2 REV A (`7B905383`) has different table addresses and is rejected.
Some upstream comments call the `DEB8` cartridge REV01; the CRC above is the
identity the converter actually uses. Conversion is atomic and never rewrites
the supplied cartridges, an existing output on failure, or checked-in headers.
The bank has a versioned `SPK1` length/CRC footer. Its complete act directory and
integrity are checked by the picker and again at boot. CPK1 adds a campaign
identity and enrolls only that campaign's directory. Each pack retains common
conversion tables and unused payload bytes so it is self-contained and keeps
verified guest pointers intact; it has no dependency on another installed pack.
Settings live in `sonic3k-stage-pack.ini` next to the normal settings file.

## Saves and local play

All configurations share the normal **<ROM basename>.srm**. Its on-disk
records always contain native S3K levels and characters, with the cartridge's
original checksums and primary/backup copies. Addon progress and preferred
Mighty and Shadow identities live in **<ROM basename>.srm.campaign**, using stable
campaign/zone IDs independent of the installed catalog. Keep this companion
and its `.bak` with the SRAM when backing up or moving the complete campaign.
Native SRAM exports alone carry the native campaign and shared stats.

Removing packs parks their progress. The available menu falls back safely,
and a temporary Sonic fallback retains the extra character?s saved identity. Re-adding a
pack restores its parked resume stage. Native progression, lives and emeralds
can still advance while addons are absent. Explicit native slot deletion
clears that slot's parked progress when the journal next reconciles it.

The first addon launch retains an immutable `.pre-campaign.bak` copy of an
existing native SRAM. Valid old `sonic3k-stage-pack.srm` and
`sonic3k-megamix.srm` files are imported once into a new journal; their original
files remain untouched. Atomic writes, CRC validation, a last-valid journal
backup and pending/confirmed native projections handle interrupted saves.
With every addon absent or disabled, the native cartridge retains its normal
projection. The companion observes explicit native deletion/recreation so a
removed character cannot resurrect a deleted file.

Campaign saving works through the native Data Select and save routines.
Quickstates and rewind are unavailable while the pack is active: this larger
reference has additional host state that still needs a complete serialization
contract. Netplay refuses the pack until its simulation, capabilities and
compatibility identity have been qualified. Native S3K retains its existing
state and network behavior with the pack disabled.

## Source and validation

Stage conversion, object behavior, bosses, events, music and rendering support
are adapted from [sonsegajp/SonicTrilogy](https://github.com/sonsegajp/SonicTrilogy)
at `b55d4be26ebe68de228906263bbf979391646d99`. The optional expansion-ROM and
palette support originates in
[sonsegajp/segagenesisrecomp](https://github.com/sonsegajp/segagenesisrecomp)
at `014e63ff4c890abe031f13f33d9fa69cb360cd3c`, adapted onto the current framework.
Upstream code is under PolyForm Noncommercial 1.0.0; its attribution is retained.
The addresses originate in the Sonic Retro disassemblies, as documented upstream.

Internal `game/trilogy` filenames and `tri_*` symbols are retained to simplify
updates and rebasing. They do not rename the game. The converters build data
from the local cartridges; they do not bundle the games.

`tests/runtime/run_stage_pack_sweep.py` exercises every imported act and player
mode in separate muted processes, records screenshots and RAM, checks stage
entry and dispatch errors, and verifies the saved native projection remains
valid. Developer stage selection in that sweep is a test hook, not evidence of completing all
acts through ordinary play. Full campaign, boss and special-stage route QA must
be recorded separately. ROM-independent tests cover default-off settings,
invalid-resource rollback, persistence, save selection, bank integrity boundaries,
palette behavior and the netplay guard.

The 2026-10-18 cheat-assisted audit covers all 39 acts at 3,837 sampled
locations, all 39 normal stage exits, all 17 boss encounters, and checkpoint,
native death and settled retry in all 33 acts that contain starposts. It found
and fixed a Z80 bus deadlock, Silver Sonic's missing collision registration,
and duplicate Spring Yard 2 results that skipped Act 3. Separate source-data
checks cover all static layouts; all acts produce non-silent, unclipped PCM.
All 38 sampled enemy families spawn; 26 have confirmed native defeat reactions.
The other interactions remain unverified.

See [the audit record](STAGE_PACK_AUDIT.md) for the per-act matrix, evidence,
repeatable commands, fixture bypasses and limits. The earlier 156 act/player
entry probes, 26 CTests and 49 mod-off image/RAM/VRAM comparisons are separate
checks. The bank's 6,757 placements have registered handlers, but registration
alone does not prove behavior. Complete physics/animation/sound fidelity,
special-stage entrance/return routes, all-character interaction coverage,
reference host-state serialization remain
follow-up work.

That original S1/S2 audit excludes Sonic CD. The separately implemented CD
campaign now has its own [conversion, adaptation and validation guide](SONIC_CD.md).
Megamix v3 stages and Mighty are implemented in the third independent mod;
see [the Megamix guide](MEGAMIX_STAGE_PACK.md) for conversion and audit bounds.

## Independent-pack integration checks (2026-10-19)

The combined changes include the qualified 0.5.3 game fixes and engine
renderer, stack-return and session-setup updates. They pass 39 CTests,
all eight real-input new-game configurations, and 49 byte-identical native
image/RAM/VRAM artifacts against the earlier mod-off baseline.
`run_campaign_permutations.py` checks actual cartridge checksums and native
stage/character bounds after each save. Its 17 production cases cover legacy
migration, individual missing/re-added packs, removing and restoring every
pack, truncated companion/native SRAM recovery, and Mighty in independently
converted legacy S1/S2 banks. With all packs missing the companion remains
byte-for-byte untouched. The first missing-pack
fixture omitted the normal replay-zone choice for a completed file; its
failure is retained and superseded by the corrected second run.

Production entry/movement/render/save sweeps pass all 58 imported acts with
their separate paks (S1 19, S2 20, Megamix 19). These use Sonic & Tails in
16:9; they do not replace the separately recorded encounter/exit audits.
Four native IceCap/Launch Base routes also pass with live renderer telemetry
and six image/RAM/VRAM checkpoints per act. The initial telemetry run used
an unavailable TCP port and is retained; the corrected run uses port 46051.

`qa-experimental-s1-native` observes all eight real Final Zone boss hits,
the escape and the fresh Angel Island start with Sonic 2 absent.
`qa-experimental-native-megamix` observes actual final-boss damage, chase,
ending and Sunny Shores with only Megamix enabled; earlier DEZ bosses are
explicitly bypassed. Ten Megamix signs pass all five real animation frames,
loaded tally art and real next-act starts (`qa-experimental-signs`).

The bounded MDY3 attachment preserves every mapping/DPLC record, animation,
art and palette/UI resource, so Mighty can follow the player into S1/S2
packs converted without Megamix. The save and attachment kernels also pass
Ubuntu GCC builds with `-Wall -Wextra -Werror`. This establishes the listed
contracts, not complete donor fidelity or every route's mechanics.
