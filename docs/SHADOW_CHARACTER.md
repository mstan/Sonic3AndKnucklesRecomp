# Megamix v3 Shadow

Owner priority: implement Shadow first on an independently mergeable branch,
then resume Sonic CD. `feature/megamix-shadow` starts at game main `7558f26`;
it has no CD feature ancestry. The preserved CD checkpoints remain separate.
Central tracking: `beads-8y9v`. Work is solo.

## Resource and character contracts

The existing default-off Megamix mod also offers Shadow & Tails and Shadow.
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
| Shadow sign | late loader `$625E`, PLC `$25` |
| Ground A boost | `$197EC–$19844`, `$19B4E` |
| Super boost / faster ring drain / release | `$19844–$198EE`, `$11CEEE–$11CF0A`, `$11D10C–$11D188` |
| Homing acquisition, steering and paid chain | `$19DD0–$19F1E` |

Ground A costs five rings in normal form. B/C retain native jumping and add
airborne homing toward a nearby eligible enemy or monitor; continuing with A
can acquire another target for five rings. B+C uses the existing S3K requirement
of seven Chaos Emeralds and at least 50 rings to transform; airborne B+C reverts.
Super ground A boosts while held and accelerates native ring draining. Native
S3K movement, Super presentation and corresponding native sound effects remain
the integration basis, as with Mighty. Further Super interactions are under QA.

## Save format

SCS1 remains 548 bytes. Version 3, capability byte 2, extends character IDs
without moving any slot or campaign field. Version 1 migrates with an immutable
`.pre-shadow.bak`; the recoverable primary/backup transaction is retained.
Unknown versions/capabilities, including the separate CD version-2 companion,
remain read-only and select native saving. CD integration must explicitly merge
both capability contracts before it can use these saves.

MM availability is mask bit 4; loaded Shadow resources use bit 16. No campaign
catalog ID is consumed. An unavailable identity projects to Sonic & Tails or
Sonic while remaining parked in the companion. The native-only IO observer
retains explicit deletion/recreation and does not resurrect an old character.

## Validation checkpoint — implementation in progress

Private evidence lives under `build/`; captures are not committed.

| Check | Evidence / result |
|---|---|
| Shared/game tests | 42/42 on dev2; latest rebuilds require corresponding reruns |
| Native baseline | dev3: 49/49 PNG/RAM/VRAM artifacts byte-identical |
| Mighty abilities | dev3: all 29 actual input/monitor/water/death checks pass |
| Shadow selection / Continue | dev1: both identities pass real menu selection and process restart |
| Save kernel | both identities × normal/completed × eight pack masks × old/new resources, migration and future-primary/backup preservation pass |
| Original animation oracle | 432/432 cases, 27,648 frames; original unchanged `$1A7F4` executes in Unicorn |
| Attached Shadow graph | all 176 mappings/DPLCs, 38 animations, art/palettes/UI match source graph; malformed descriptor rejected |
| Initial movement | MM/S1/S2 pass; initial native fixture sampled AIZ intro, settled retry reaches normal player and moves |
| Normal boost / homing | dev3: real input boost fee, homing movement, actual enemy defeat and score pass |
| Super transform / revert | transform passes; first revert fixture had already landed and is retained as failed; corrected airborne fixture pending |
| Signpost | dev2: all five real frames, tally and actual next-act transition pass |

The original instruction oracle caught signed offsets from the Super table
back into shared normal scripts. Fixed before qualification. A real runtime
boost probe caught clobbered native A4/A5/A6; the collision call now preserves
them and the corrected boost probe passes. Earlier failed fixtures/logs remain.

Remaining before standalone acceptance: full process removal/reinstallation
and deletion checks, broader native/S1/S2/MM ability/water/shield/Super/death
and transition coverage, adaptive rendering and actual caption checks,
portability, mandatory final independent visual comparisons, and a clean
normal-window/normal-audio owner build. No merge or final playtest delivery yet.
After this branch qualifies, integrate Shadow into CD and resume the general
Sega CD backend and outstanding CD acceptance work.
