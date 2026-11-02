# Player reports — 2026-11-01

Owner request: reproduce and fix all eight reports against the current Sonic
3 & Knuckles game and its experimental campaign/character mods. The separate
replay-diagnostics implementation did not fix these gameplay reports.

| Report | Reproduction | Fix / qualification |
| --- | --- | --- |
| Widescreen title and save-select margins wrap graphics | Confirmed: title scenery wraps across opposite edges; save-select reflects the complete portrait/text into both margins | Fixed: title scenery joins by reflection; save-select extends its edge colors and retains the finite carousel. One central logo/portrait. Native-width center preserved. 4:3 / 32:9 captures inspected. |
| Failed Blue Spheres repeats after entering from a later act (MZ1 → SYZ1) | Actual giant-ring entries and natural failures: stage 0, then stage 0; no emerald awarded | Fixed: preserve the native special-stage cursor rather than replacing it with the first missing emerald. Same route now plays 0, then 1, returning to MZ2 / SYZ2. |
| Background scrolling glitches at level entry | Confirmed extra pan/reversal in imported GHZ1 and MZ1 while the expanded foreground clamps at the left edge | Fixed: give imported backgrounds a centered canvas origin, as already done for CD. Foreground/HUD retain their bounded view. Live captures plus fixed-background and real-scroll checks across six campaign zones and four widths pass. Stage clarification remains requested; other reported interpretations need a precise location. |
| Submerged lightning shield near LZ1 start | Confirmed at x1520, y240, below water y190; monitor subtype 6, display frame 7 | Fixed in the runtime for existing S1/S2 packs: submerged elemental boxes use bubble subtype 7 and native animation/frame 8. Native/authored Megamix contents unaffected. |
| GHZ1 signpost visually spins indefinitely with Sonic & Tails; gameplay still progresses | **Visual behavior unverified.** Earlier normal, spindash-speed, jump, high-air, 16:9, 32:9 and Tails-carry approaches established act completion. Owner subsequently clarified that progression was never blocked; completion alone does not settle the visual report | **Deferred this pass, not claimed fixed.** Observe the actual spin animation through act clear in a future affected replay. No speculative timeout or forced-completion patch. |
| Shadow bubbles then loops walking during act clear | Confirmed during imported GHZ1 tally: native ending animation 0x13 was translated to the donor's bubble script | Fixed: imported ordinary acts use normal runoff; native ending holds use the donor's standing frame. GHZ1 Shadow leaves the viewport before tally completion, then loads GHZ2. |
| Companion Tails has no act-clear pose or runoff | Confirmed: Tails stays at the sign; first candidate exposed a second defect, his independent right-boundary clamp | Fixed: include the active companion in imported runoff and extend his independently verified native boundary path. Sonic/Shadow and Tails both leave, with ordinary physics. SBZ2/Metallic Base 2 preserve their control-restoring tally instead of runoff. |
| Shadow's 1-UP and life icon are cropped | Confirmed fixed center crop cuts both sides and the lower eight rows of the donor's 32x24 head | Fixed: reconstruct and fit the complete head into the shared 16x16 HUD/1-UP tiles, including already converted MSW1 resources. Synthetic edge/bottom coverage passes. Actual monitor break awards life 3 → 4. Before/after pixels inspected. |

Source base: game main `ddafabc479`, engine `252535d124` plus the existing
uncommitted replay-diagnostics changes. Work is solo. Reuse the supported CMake
consumer and existing TCP/state/stage-audit helpers. Preserve original native
saves, use isolated muted runtimes, and do not write the outcomes being tested.
The original investigation was local. The owner subsequently authorized a new
release with these fixes and diagnostics before work on the next reports.
Release v0.6.3 integrates onto the shipped v0.6.2 source and engine base.

Seven reports have fixes with built-runtime evidence; the intermittent signpost
report remains open. No claim of reproducing all eight is made.

The focused check set passes: `sonic3_video`, `sonic3_standalone_video`,
`megamix_mighty`, `megamix_presentation`, `trilogy_hook_ownership`,
`sonic_cd_sites`, `diagnostic_replay`, `video_mod`. Independent Capstone
decoding matches all 11 hook instruction contracts. The private character
corpus also passes all 36 animation states in four orientations with 29,434
bounded DPLC uploads. Tests do not establish every campaign act/boss mechanic.

Private evidence is in `build/player-reports-20261101/`: `baseline/` (menus),
`baseline-gameplay/` (original sign/animation/water behavior),
`baseline-special-3/` (repeated failed stage), `baseline-background-icons/`,
`baseline-sign-variants/`, `baseline-sign-carried/`, `candidate-3/` (final
renderer captures), `candidate-icons/`, and `final-gameplay/` (combined final
gameplay qualification). Each run retains inputs, logs, WRAM/images and its
declared setup bypasses; only player/prerequisite setup is injected. Sign,
results, emerald/failure and life-award outcomes are observed, never written.
The Marble-to-Spring-Yard probe explicitly skips intervening ordinary acts.

Reproduction helper: `tests/runtime/run_player_reports.py`; use the retained
job JSON for exact owned ROM/resource paths. No private assets are checked in.
The baseline executable SHA256 is
`bf17e88758afe4b30da74ae5c3ffaf58514f0d24420b11b77ade7734903b45ab`.
The final combined executable SHA256 is
`ee17b65c42f04c392ec5dd62d81d4eeb2398f2a1eea2ffc626a00bd90da66f73`.
It is built at
`../segagenesisrecomp/build/feedback-20261101/consumer/Release/Sonic3KRecomp.exe`.
The renderer-only captures use the immediately preceding build recorded in
their `results.json`; subsequent changes only preserve SBZ2 tally control.
Default-on replay diagnostics from the earlier task are included in this build.
Failed fixture attempts and redundant executable copies were removed. Retained
task evidence is about 70 MiB; the frozen baseline and current build remain for
the unresolved signpost investigation. Existing donor files were reused.

Central Beads search was attempted before implementation and remains unavailable:
the client cannot find database `beads` on port 3307. Preserve the unrelated
service; backfill the owning game issue and its eight dispositions once the
central tracker is available. Do not create a replacement database.
