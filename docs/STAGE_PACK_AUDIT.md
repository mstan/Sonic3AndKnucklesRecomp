# Sonic 1 & 2 Stage Pack audit — 2026-10-18

This is a systematic cheat-assisted content audit of the optional S3K campaign.
All 39 acts were surveyed and their ordinary stage-end paths were exercised.
This remains an experimental build: the coverage below does not establish
complete original-game fidelity or every character's behavior.

## Findings fixed

- **Aquatic Ruin 2 deadlock:** two runs of the same 149-point survey stopped
  after point 104, with ten million reads of the Z80 bus-request register.
  The runner yielded before returning its synchronous acknowledgement; a
  V-int could release the request during that yield. Framework commit
  `aeaff277c9838d07ec65512d12ea3adff8c119c9` latches the byte/word ACK first.
  The identical survey then completed 149/149 points. Native mod-off output
  remained identical to v0.5.2 in 49 PNG/RAM/VRAM comparisons.
- **Silver Sonic could not take damage:** its body set collision flags but
  never registered in S3K's collision-response list. Original S2 scans object
  RAM instead. Register the live body, retaining its separate back spikes and
  defeated routines. Native contact now reduces its eight HP to zero, followed
  by the robot's twelve HP, its defeat and the ending into Angel Island.
- **Spring Yard 2 skipped Act 3:** the two alternate-route signs share X and
  each spawned a result tally. Both advanced the campaign before Act 3 loaded.
  Per-level results guards permit one tally and one completion. The old build
  fails the expected-next-act check; the fixed build loads Act 3 at its start.
  All 21 signpost paths pass that stricter check.

## Reference implementation review

The source is pinned to SonicTrilogy
`b55d4be26ebe68de228906263bbf979391646d99`. The author's
[implementation notes](https://github.com/sonsegajp/SonicTrilogy/blob/b55d4be26ebe68de228906263bbf979391646d99/docs/HOW_THE_ZONES_GOT_IN.md)
were reviewed alongside the converter and runtime source. These are the
integration seams that need separate checks:

| Area | Strategy retained from the reference | What this audit establishes |
| --- | --- | --- |
| S1 geometry | Split 256px chunks into 128px chunks; mirror collision shapes; synthesize secondary loop paths | All static source tile placements and primary solidity flags match; complete loop/wall physics remains unverified |
| Art and palettes | Pack reachable tiles, reserve animated slots/object art, separate enemy palettes | Static tile data matches after source palette normalization; animated art and every palette effect remain unverified |
| Objects | Relocate donor scratch fields, adapt horizontal activation and remember bits, reuse native shared objects | Survey activation plus sampled real contact/defeat; Silver Sonic demonstrated why flags alone are insufficient |
| Camera/events | Transcribe donor dynamic limits, boss setup and parallax | Sampled regions and real exits; checkpoint prerequisites must be crossed, not silently skipped |
| Sound | Translate SMPS tempo/commands, FM operator ordering, PSG notes/envelopes and DAC banks | Actual PCM present without clipping in every act and six standalone SFX; musical/timing fidelity remains unverified |
| Campaign | Adapt native results, bosses, special-stage hooks and fresh act starts | All normal exits and boss defeats below; Blue Spheres entrance/return routes remain unverified |

The author's original-emulator object comparison tooling requires unpublished
local Python modules and a reference DLL. It was not run here. The static
checks use the author's source readers/decoders, so they are source-data
comparisons, not independent emulator comparisons.

## Coverage and limits

| Check | Result | Scope |
| --- | --- | --- |
| Location survey | Passed: 3,837/3,837 samples in 39 acts | Points derive from all 6,757 placement coordinates, vertical branches and 256px horizontal samples; frozen survey player, not a mechanics test |
| Registered handlers | Passed: 6,757/6,757 | Boot audit; registration is not behavior proof |
| S1 static conversion | Passed: 19 acts; 5,891,440 nonempty static tile placements | Zero tile or primary solidity-flag mismatches; excludes animation and empty source chunks |
| S2 static conversion | Passed: 20 acts, full static foreground/background layouts | Zero mismatches; camera-dependent HTZ mountain/cloud tiles `$500–$51F` are explicitly excluded alongside animation |
| Stage music output | Passed: 39/39 | Actual generated PCM, nonzero and unclipped; no speaker output during automation |
| Standalone SFX | Passed: S2 `$A0–$A5` | Developer sound dispatch; natural interaction triggers and all other sounds remain unverified |
| Enemy families | Spawn passed: 38/38; defeat interaction passed: 26/38 | One representative placement per code family; twelve conditional/spiked/self-detonating families remain interaction-unverified |
| Boss encounters | Passed: 17 | Native contact damage and defeat, including both Death Egg bosses; never write HP or defeat flags |
| Stage-end paths | Passed: 39/39 | Expected next act loaded in gameplay with checkpoint cleared and player near its proper start |
| Checkpoint/death/retry | Passed: 33/33 acts with starposts | First post per act; native time-out death, life loss and 120-frame settled retry; all other post instances and death causes remain unverified |
| Native regression | Passed: 49/49 byte-identical PNG/RAM/VRAM captures | Pack off, compared with released v0.5.2 after bus fix |
| Automated C tests | Passed: 26/26 | Integration/resource/save/netplay guards and existing framework tests |
| Character entry smoke | Passed: 156/156 | All 39 acts × four player modes; deeper interaction audit primarily Sonic alone |

The seven placement instances absent from the frozen survey are retained as
unobserved in its JSON: three suppressed residual S2 act-two signs, two
capsules, a Spring Yard floating block and a Death Egg barrier. The two
capsules are exercised in the separate live boss-exit probes. The block and
barrier's individual behavior remains unverified. Do not turn these into a
claim that every individual placement's behavior passed.

## Cheats, fixtures and reproducible evidence

Survey protection holds the player, bypasses crushing/void deaths and can
restore stage art after an unsafe checkpoint fixture. That mode is disabled
for interactions. Boss fixtures change player position, posture, velocity and
protection; some revive the cheat player and cancel its pending death retry.
Those revivals are recorded and are never enabled in checkpoint/death tests.
Hill Top supplies saved DLE `$0A`, explicitly bypassing its earlier event
prerequisites. Its capsule and Wing Fortress's plane landing require camera
travel; neither completion state is supplied. Final Zone side hits and the
Tornado's native landing/ending routines run normally after positioning.

Checkpoint probes start without a saved checkpoint, travel in bounded camera
steps, disable travel debug protection before touching the real starpost,
advance only the timer to 9:59:59 and observe native death/life loss/retry.
Marble 1 crosses its real underground camera-event thresholds before touching
the post. Large instant camera jumps can exhaust the object loader; they are
invalid fixtures, not evidence that an object is absent.

Private local evidence is under `build/`. No cartridges, converted bank,
RAM/VRAM or audio captures are published in the repository or ZIP.

| Evidence directory | Contents |
| --- | --- |
| `qa-stage-survey-v4` + `qa-arz2-bus-fixed` | Full bank-derived manifest, sampled coordinates, frame/object/respawn observations, screenshots, per-act RAM/VRAM; original ARZ2 failure retained and replaced by fixed replay |
| `qa-arz2-bus-repro` | Second identical pre-fix watchdog failure |
| `qa-s1-data-portable`, `qa-s2-data-portable` | Source-data comparison results and explicit exclusions |
| `qa-stage-audio-busfix`, `qa-stage-sfx` | Muted captures and PCM statistics |
| `qa-signposts-fixed`, `qa-syz2-before` | 21 strict next-act checks and old-build negative regression |
| `qa-bosses-strict-s1`, `qa-bosses-strict-s2a`, `qa-bosses-strict-s2b` | Coherent frame/RAM/object/HP observations and native damage/defeat; HTZ/WFZ incomplete fixture attempts retained |
| `qa-htz-capsule-strict`, `qa-wfz-camera-strict` | Corrected camera fixtures, real capsule/plane transitions and settled next-act checks |
| `qa-special-routes-strict`, `qa-final-zone-strict` | SBZ3 and Sky Chase real exits; Final Zone eight side hits, escape and EHZ start |
| `qa-checkpoints-stable` | All 33 real checkpoint/time-out/life-loss/settled-retry probes |
| `qa-enemy-types-v2`, `qa-enemy-rechecks` | Family spawn/activity/contact evidence; audited explosion code `1E66E` distinguished from an unresolved hit |
| `qa-mod-off-busfix` | All 49 comparisons against v0.5.2 |

The survey spans two executable versions: the initial 38 completed acts and
the fixed ARZ2 replay. Both use the identical bank; survey manifests include
executable/bank SHA256. Behavior checks were rerun for the fixes. This is not
a claim that the complete survey was repeated on every later binary.

From a checkout with private cartridge inputs and a generated bank, substitute
paths in the following commands. The interaction scripts need a developer
build with `GEN_ENABLE_TRACE=ON`; the location, sound and signpost scripts also
work in the production trace-off build. All tools use isolated settings/saves
and dummy output devices. Production QA keeps normal audio enabled.

```text
python tests/runtime/run_stage_pack_audit.py --exe <exe> --rom <s3k> --bank <bank> --out build/survey
python tests/runtime/check_stage_pack_s1_data.py --reference-tools <reference>/tools/trilogy --rom <s1> --survey build/survey --out build/s1-data
python tests/runtime/check_stage_pack_s2_data.py --reference-tools <reference>/tools/trilogy --rom <s2> --survey build/survey --out build/s2-data
python tests/runtime/run_stage_pack_checks.py --exe <exe> --rom <s3k> --bank <bank> --out build/signs --kind signposts
python tests/runtime/run_stage_pack_checks.py --exe <exe> --rom <s3k> --bank <bank> --out build/audio --kind audio
python tests/runtime/run_stage_pack_checks.py --exe <exe> --rom <s3k> --bank <bank> --out build/sfx --kind sfx
python tests/runtime/run_stage_pack_encounters.py --exe <exe> --rom <s3k> --bank <bank> --out build/bosses --kind bosses --stages "2002,2102,2202,2302,2402,2801,2901,2a01,2b01,2c01,2d01,2e01,2f02,3100,3200"
python tests/runtime/run_stage_pack_routes.py --exe <exe> --rom <s3k> --bank <bank> --out build/routes
python tests/runtime/run_stage_pack_encounters.py --exe <exe> --rom <s3k> --bank <bank> --out build/enemies --kind enemies
python tests/runtime/run_stage_pack_checkpoints.py --exe <exe> --rom <s3k> --bank <bank> --out build/posts --death-kind timeout
```

Final Zone is covered by `run_stage_pack_routes.py`, not the generic boss
fixture. Quote comma-separated hexadecimal stage lists in PowerShell.
Enemy probes deliberately return nonzero for unverified interactions; the JSON
distinguishes them from failures. Preserve failed attempts as well as passes.

Unverified: exhaustive terrain collision/loops/moving platforms, every alternate
mechanic and post instance, animated art/parallax correctness, original-emulator
timing and sound fidelity, all-character boss/interaction routes, Blue Spheres
entrances/returns, prototype save migration, serialization and cross-platform
or netplay qualification. Sonic CD, Megamix stages and Mighty are not included.

## Per-act matrix

`Post` means the real checkpoint/death/settled-retry probe above. `N/A` means
the placement table has no starpost. Every row passes static data, music-output
presence and its listed ordinary exit. Boss rows include native damage/defeat.

| Act | ID | Survey samples | Checkpoint | Exit | Next act |
| --- | --- | ---: | --- | --- | --- |
| Green Hill 1 | `2000` | 69 | Passed | Signpost | `2001` |
| Green Hill 2 | `2001` | 76 | Passed | Signpost | `2002` |
| Green Hill 3 | `2002` | 102 | Passed | Boss / capsule | `2200` |
| Labyrinth 1 | `2100` | 67 | Passed | Signpost | `2101` |
| Labyrinth 2 | `2101` | 60 | Passed | Signpost | `2102` |
| Labyrinth 3 | `2102` | 115 | Passed | Boss / capsule | `2300` |
| Scrap Brain 3 | `2103` | 97 | Passed | Position / event | `2502` |
| Marble 1 | `2200` | 50 | Passed | Signpost | `2201` |
| Marble 2 | `2201` | 74 | Passed | Signpost | `2202` |
| Marble 3 | `2202` | 97 | Passed | Boss / capsule | `2400` |
| Star Light 1 | `2300` | 91 | N/A | Signpost | `2301` |
| Star Light 2 | `2301` | 69 | N/A | Signpost | `2302` |
| Star Light 3 | `2302` | 88 | Passed | Boss / capsule | `2500` |
| Spring Yard 1 | `2400` | 63 | Passed | Signpost | `2401` |
| Spring Yard 2 | `2401` | 78 | Passed | Signpost | `2402` |
| Spring Yard 3 | `2402` | 99 | Passed | Boss / capsule | `2100` |
| Scrap Brain 1 | `2500` | 111 | Passed | Signpost | `2501` |
| Scrap Brain 2 | `2501` | 126 | N/A | Signpost | `2103` |
| Final Zone | `2502` | 37 | N/A | Boss / event | `2800` |
| Emerald Hill 1 | `2800` | 82 | Passed | Signpost | `2801` |
| Emerald Hill 2 | `2801` | 92 | Passed | Boss / capsule | `2900` |
| Chemical Plant 1 | `2900` | 98 | Passed | Signpost | `2901` |
| Chemical Plant 2 | `2901` | 117 | Passed | Boss / capsule | `2a00` |
| Aquatic Ruin 1 | `2a00` | 113 | Passed | Signpost | `2a01` |
| Aquatic Ruin 2 | `2a01` | 149 | Passed | Boss / capsule | `2b00` |
| Casino Night 1 | `2b00` | 162 | Passed | Signpost | `2b01` |
| Casino Night 2 | `2b01` | 145 | Passed | Boss / capsule | `2c00` |
| Hill Top 1 | `2c00` | 84 | Passed | Signpost | `2c01` |
| Hill Top 2 | `2c01` | 135 | Passed | Boss / capsule | `2d00` |
| Mystic Cave 1 | `2d00` | 87 | Passed | Signpost | `2d01` |
| Mystic Cave 2 | `2d01` | 127 | Passed | Boss / capsule | `2e00` |
| Oil Ocean 1 | `2e00` | 130 | Passed | Signpost | `2e01` |
| Oil Ocean 2 | `2e01` | 140 | Passed | Boss / capsule | `2f00` |
| Metropolis 1 | `2f00` | 114 | Passed | Signpost | `2f01` |
| Metropolis 2 | `2f01` | 124 | Passed | Signpost | `2f02` |
| Metropolis 3 | `2f02` | 161 | Passed | Boss / capsule | `3000` |
| Sky Chase 1 | `3000` | 72 | N/A | Position / event | `3100` |
| Wing Fortress 1 | `3100` | 120 | Passed | Boss / event | `3200` |
| Death Egg 1 | `3200` | 16 | N/A | Boss / event | `0000` |
