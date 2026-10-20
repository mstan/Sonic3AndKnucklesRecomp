# Megamix v3 Shadow

Owner priority: implement Shadow first on an independently mergeable branch,
then resume Sonic CD. `feature/megamix-shadow` starts at game main `7558f26`;
it has no CD feature ancestry. The preserved CD checkpoints remain separate.
Central tracking: `beads-8y9v`. Work is solo.

## Resource and character contracts

The default-off Shadow checkbox offers Shadow & Tails and Shadow. Mighty and
Megamix stages have separate default-off checkboxes. All three require the
owner?s raw Megamix v3 ROM and locally converted content pack, with one shared
path per resource: selecting either file in any entry fills all three. The ROM
is verified at selection and boot; a pack alone cannot enable any of them.
Character identities 0–5 retain their meanings; the new identities are 6 and 7.
Retail player modes remain 0–3. Shadow uses native Sonic movement/collision
with the exact Mega Drive v3 donor graphics, animation and translated abilities.
The donor is CRC32 `66ECB4F4`, SHA256
`85d9e9ba79f0035d5854618666ff198871abe1931558496e6563b1cd90e5cdbe`.
No cartridge, converted graphics, saves or reference captures are distributed.

Run `tools/build_megamix_pack.py --upgrade-presentation` on an existing SPK2
bank with the same owned Megamix donor and supported S3K cartridge. Then export
the independent packs with `tools/split_campaign_packs.py`. A new full conversion
also includes Shadow. MDI version 5 adds an explicit Shadow descriptor pointer
at offset 40; MSW1 uses the bounded 88-byte character graph. MGS2 adds the
third sign strip. Older MDI4/MDY3 banks retain Mighty and their existing stages;
unavailable Shadow choices fall back while the journal retains their identity.

The menu publishes distinct Mighty and Shadow atlas/palette ranges so saved
files can show both characters together without changing Sonic or Tails.
HUD heads, native-style names, normal/water/MB3 colors, DPLC uploads and stage
sign graphics have explicit resource ownership. Quickstates, rewind and
netplay retain the existing unavailable-addon policy.

## Source audit

All bindings are checked against actual donor instructions before conversion.
The older 2005 disassembly and later CD Megamix versions are not v3 oracles.

| Data or behavior | Original v3 source |
|---|---|
| Character selector | `$FFBB = 2` |
| Mappings / DPLC / art | `$2B6A4` / `$2CA1C` / `$BE2E4` |
| Normal / water / MB3 palette | palette indices `$16` / `$1A` / `$1C` |
| Shared normal / Super animation | `$1AA1C` / `$1AB5E`; routine `$1A7F4` |
| Shadow Super normal / water color cycles | `$11CB7C` / `$11CCFC`, selected by `$11C8EC` / `$11C942` |
| Shadow sign | late loader `$625E`, PLC `$25` |
| Ground A boost | `$197EC–$19844`, `$19B4E` |
| Super boost / faster ring drain / release | `$19844–$198EE`, `$11CEEE–$11CF0A`, `$11D10C–$11D188` |
| Homing acquisition, steering and paid chain | `$19DD0–$19F1E` |

Ground A costs five rings in normal form. B/C retain native jumping and add
airborne homing toward a nearby eligible enemy or monitor; continuing with A
can acquire another target for five rings. B+C uses the existing S3K requirement
of seven Chaos Emeralds and at least 50 rings to transform; airborne B+C reverts.
Super ground A boosts while held and accelerates native ring draining. The v3
counter resets to ten and uses BPL, giving eleven-frame drain intervals; the
native equivalent is `Super_frame_count` at `$F670`. Native
S3K movement, Super presentation and corresponding native sound effects remain
the integration basis, as with Mighty.

The source contains a dormant airborne A burst at `$198F0`, requiring both
Super and a nonzero invincibility timer. The original transformation clears
that timer (`$11CE96`), and the original invincibility monitor rejects Super
(`$EFD6`). Native S3K follows the same rules. This is not an exposed v3 ability;
the port preserves normal pickup behavior rather than adding a way around it.

## Save format

SCS1 remains 548 bytes. Version 3, capability byte 2, extends character IDs
without moving any slot or campaign field. Version 1 migrates with an immutable
`.pre-shadow.bak`; the recoverable primary/backup transaction is retained.
Unknown versions/capabilities, including the separate CD version-2 companion,
remain read-only and select native saving. CD integration must explicitly merge
both capability contracts before it can use these saves.

Megamix stage availability is mask bit 4, Shadow bit 16 and Mighty bit 32.
The character bits are independent of the stage bit. No campaign
catalog ID is consumed. An unavailable identity projects to Sonic & Tails or
Sonic while remaining parked in the companion. The native-only IO observer
retains explicit deletion/recreation and does not resurrect an old character.

## Validation checkpoint — implementation in progress

Private evidence lives under `build/`; captures are not committed.

| Check | Evidence / result |
|---|---|
| Shared/game tests | 42/42 on dev6 |
| Native baseline | dev6: 49/49 PNG/RAM/VRAM artifacts byte-identical |
| Mighty abilities | dev3: all 29 actual input/monitor/water/death checks pass |
| Shadow selection / Continue | dev1: both identities pass real menu selection and process restart |
| Save kernel | both identities × normal/completed × eight pack masks × old/new resources, migration and future-primary/backup preservation pass |
| Original animation oracle | 432/432 cases, 27,648 frames; unchanged `$1A7F4` executes in Unicorn |
| Original palette and sprite oracles | 1,152 color samples match; 176 sprite uploads match unchanged `$1ABFC` through 574 DMA calls |
| Independent paired artwork | 528 normal/water/MB3-water renders match; all six galleries visually inspected; partial unused shared poses are retained as in donor |
| Attached Shadow graph | all 176 mappings/DPLCs, 38 animations, art/palettes/UI match source graph; malformed descriptor rejected |
| Initial movement | MM/S1/S2 pass; initial native fixture sampled AIZ intro, settled retry reaches normal player and moves |
| Normal boost / homing | dev6: real input boost fee, homing movement, actual enemy defeat and score pass in MM; S1/S2/native broader reruns in progress |
| Super and death | dev6 MM: actual transform, boosted speed and three observed ring deductions, release, airborne revert and natural timeout/death/life decrement/retry pass |
| Signpost | dev2: all five real frames, tally and actual next-act transition pass |
| Process saves | dev3: 16 saved identity/pack combinations, six old/missing/damaged/restored resource launches, native-menu deletion and reinstall all pass (24 cases) |
| Adaptive rendering | dev4: seven representative native/S1/S2/MM locations, including underwater, pass six live Fit resize shapes each; images retained and inspected |
| Actual captions | dev4: both hidden real Win32 gameplay/launcher captions are exactly Sonic 3 & Knuckles with a debug port supplied |
| Other host | Ubuntu GCC strict save/migration/future-preservation matrix passes |

The original instruction oracle caught signed offsets from the Super table
back into shared normal scripts. Fixed before qualification. A real runtime
boost probe caught clobbered native A4/A5/A6; the collision call now preserves
them and the corrected boost probe passes. The extended Super check caught
the distinction between native palette and ring counters; it now updates the
original native ring-deduction path. The separate selector-2 Super palettes
and eight-frame/nine-step cycle are original-instruction qualified. Earlier
failed fixtures/logs remain; one S1 homing setup landed before the button press
and is being rerun with an explicitly checked airborne prerequisite.

The owner playtested Shadow with all emeralds and approved merging the
experimental character on 2026-10-19. The independent mod/resource split was
then qualified against game `64a6ef0` and engine `3f17adf` (including current
master). Candidate SHA256:
`4c5944857bdc2564f85c6c2d2aedfacc536eaf649ff7106603c695b7db325a2c`.

- 43/43 combined CTests pass, including the rebuilt current engine tests.
- Native mod-off parity remains 49/49 PNG/RAM/VRAM artifacts byte-identical.
- Windows and Ubuntu GCC strict builds pass the shared donor/resource tests,
  all 32 checkbox combinations, and normal/completed parked-save matrices for
  identities 4?7 across all 32 configurations. No ROM data is in these tests.
- Real New Game and cross-process Continue pass all eight combinations of
  Megamix stages/Mighty/Shadow. A Shadow-only file parks under missing or wrong
  ROM and restores its identity after the owned donor returns (19 launches
  plus the retained-journal assertion).
- With Mighty disabled, actual Shadow boost, homing enemy defeat/score,
  transform, held Super boost with three native ring deductions, release,
  airborne revert and natural timeout/death/retry all pass. An earlier probe
  faced left near the stage boundary and correctly stopped; its retained log
  led to an explicit rightward input through the open route.
- Actual launcher screenshots show three separate checkboxes and the same
  verified ROM/content path in all three detail pages. Real menu portraits,
  native-stage Shadow and MM Super/HUD captures were visually reviewed,
  retaining the earlier independent original-ROM artwork qualification.
- Actual hidden Win32 gameplay and launcher captions are clean with configured
  debug ports. The launcher probe now opens without a positional ROM/CLI port,
  which deliberately bypass the launcher in the runner; it uses rom.cfg and
  debug.ini instead. The old probe exercised gameplay twice.

Private evidence: `build/qa-shadow-split-*`,
`build/qa-shadow-only-abilities-final-2`, and `build/shadow-investigation`.
Broader stage/boss/special-stage and water/shield interaction auditing remains
explicitly incomplete under this experimental status. No final CD acceptance
is implied. CD and its general Sega CD backend remain paused pending the
owner?s BIOS; their AS IS checkpoint branches are separate from this merge.
