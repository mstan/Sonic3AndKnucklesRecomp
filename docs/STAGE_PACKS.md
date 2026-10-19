# Sonic 1 & 2 Stage Pack

This optional campaign adds all 39 retail Sonic 1 and Sonic 2 acts to the
combined **Sonic 3 & Knuckles** target. Enable **Sonic 1 & 2 Stage Pack** on
the launcher's Mods page (experimental channel). It starts at Green Hill,
continues through Sonic 2, then enters the native Sonic 3 & Knuckles campaign.
Sonic & Tails, Sonic, Tails and Knuckles use the S3K player code throughout.
The standalone Sonic 3 and Sonic & Knuckles targets do not enroll this pack.

The game keeps its existing name, launcher, pause menu and native abilities.
Blue Spheres is the special-stage system throughout. Upstream's achievements,
additional abilities, video settings and replacement launcher are not enrolled.

## Build your stage bank

No cartridge data or converted bank is distributed. With Python 3.10 or newer
and Pillow installed (`python -m pip install Pillow`),
run the following from the source checkout, supplying your own ROM paths:

```text
python tools/build_stage_pack.py --s1 sonic1.bin --s2 sonic2.bin --s3k sonic3k.bin --out sonic1-2-stages.bin
```

Select the resulting file under the mod's **Converted Sonic 1 & 2 stage bank**
resource. The pack is disabled by default. Enabling it without a valid complete
bank refuses launch; disabling it does not require the bank.

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
integrity are checked by the picker, again when committing settings, and at boot.
Settings live in `sonic3k-stage-pack.ini` next to the normal settings file.

## Saves and local play

The optional campaign uses **sonic3k-stage-pack.srm** beside the executable.
The normal `<ROM basename>.srm` is never loaded or overwritten while the pack
is active. Turn the pack off to return to the original campaign and saves.
The reference's campaign save-slot numbering is incompatible with native S3K,
so this version deliberately does not silently migrate either format. Existing
prototype extension saves also remain untouched and need an explicit migration.

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
entry and dispatch errors, and verifies native saves remain untouched. Developer
stage selection in that sweep is a test hook, not evidence of completing all
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
old prototype save migration and reference host-state serialization remain
follow-up work.

Sonic CD is excluded: the cited stage-port reference has no CD stage layer.
Megamix v3 stages and Mighty remain a separate stretch task. A Megamix ROM is
an input for that work, not a completed S3K stage or character implementation.
