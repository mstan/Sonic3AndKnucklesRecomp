# Sonic 3 family feedback — November 4, 2026

This follow-up begins after the completed v0.7.3 burndown. Reports are candidates,
not confirmed regressions. Validate against the original game where applicable;
fix both hosts only when the same defect occurs in both. Preserve native saves,
explicit player settings, and approved deferrals. Features remain lower priority.
Tracking: `beads-tdq.3.27`. Release the next Windows/Linux family update after
the applicable reproduction, regression, packaging and owner-review gates.

## Sonic 3 Alone

| ID | Report | Disposition |
| --- | --- | --- |
| A01 | Launcher cover-art rating differs from other games | No correctness change: the US Genesis Sonic 3 cover uses the period GA mark. Sonic 3 predates ESRB's September 1994 start. Uniform cover ratings would be a cosmetic asset request. |
| A02 | Competition background is dark | Fixed candidate: transparent high-priority plane tiles must retain normal SHI intensity. Scalar, packed and raster-worker paths pass; actual S3 and S3K Competition submenus are bright at 32:9. Final package regression pending. |
| A03 | Sound-test HCZ2 music briefly hitches early in track | Deferred after bounded nonreproduction. Native sound-test ID04/C input produces35 seconds without a silent10ms block. This is generated PCM with muted output, not an actual-device dropout test; no hitch fix claimed. |
| A04 | Music may play faster than expected | Not reproduced.35-second RMS-envelope comparison against original-ROM sound-test ID04 gives correlation0.91027, best time scale0.9995 (about one tick over the interval), with no sustained speed increase. Different synthesizer samples are not claimed bit-identical. |
| A05 | Holding window caption buttons stalls or affects playback | Existing upstream fix incorporated: cooperative event dispatch now covers caption-button tracking, as well as move/resize. Combined Windows modal-event regression passes. Package regression pending. |
| A06 | AIZ1 bombing transition slightly slows | Pending original comparison |
| A07 | Giant-ring entry lacks sound | Original conditional swish retained: S3's4429C entry path branches past the BigRing SFX when approaching from the left. The later443A8 warp SFX is unconditional unless all emeralds are collected. No new audio patch or blanket silence claim. |
| A08 | Blue Spheres disappear near screen edges at native aspect | Original native-canvas culling retained. Original-ROM special-stage captures reproduce the finite projection and clipped edge spheres. No change to the already accepted enhanced widescreen projection. |
| A09 | AIZ heat haze disjoins waterfall across waterline | Original behavior reproduced: original-ROM AIZ2 at X2780/Y057B shows the same waterfall offset at the waterline as the native recording. Retain the original per-band underwater deformation; no correction. |
| A10 | Companion Tails behaves strangely at AIZ2 end | Pending replay reproduction |
| A11 | Rolling early through HCZ1 spring/loop clips into wall and may zip | Original behavior reproduced. The native recording and original-ROM collision fixture both settle inside the wall at X1AF5/Y03EC, with matching actor position, velocity, collision and object-control state. Fixture starts before the spring traversal; it does not set the resulting collision state. No correctness patch. Explicit S3K occurrence/original-ROM comparison remains to be checked separately. |
| A12 | Initial HCZ2 camera fails to follow, causing death; retry works | Fixed candidate: fixed splash slot B172 overwrites live end-sign controller before native act-size installation. Both native allocation loops and the custom widescreen placement loader reserve the slot. Recorded-input transition reaches stored bounds 6000/1000 and a living player in lower HCZ2 with camera following. Gameplay scope/CCR/RAM and real widescreen placement tests pass. Final package regression pending; S3K occurrence unverified; explicitly assess its real first-entry transition too. |

## Sonic 3 & Knuckles

The following reports use 32:9 and Display-refresh high FPS. The original
transformation/cutscene timing and guest simulation must remain correct.

| ID | Report | Disposition |
| --- | --- | --- |
| K01 | AIZ1 intro Super transformation looks wrong in extended view | Original offscreen pose handover retained. Dense32:9 captures show clean original blue/yellow flight poses; the instantaneous transformation was authored outside the320px view. A newly staged transformation/effect is a lower-priority cutscene enhancement, not an art-corruption fix. |
| K02 | Horizontal background seams in pre-bombing AIZ1 and AIZ2 drain area | Pending reproduction |
| K03 | Hollow trunk interior closes again near AIZ1 miniboss | Fixed candidate: use live revealed chunks before the fire instead of the cached unopened map. Full172118-execution input route passes every checkpoint;359 paired RAM/VRAM/native-center samples are unchanged and the exposed trunk stays open. Package regression pending; original first-entry animation retained. |
| K04 | AIZ1 miniboss background disjoins and right-side graphics corrupt | Fixed candidate: the extended join exposed placeholder animation tiles and the next act before its burned art was installed. Use the original animation frames and extend the current coast through the inaccessible pre-fire margin. All867 actual cutscene RAM/VRAM/native-center samples match the baseline; package regression pending. |
| K05 | Corruption slides back to normal after AIZ1 miniboss | Fixed candidate with K04: retain each installed art phase through the real fire transition and coordinate rebase. The actual controller completes the cutscene and reaches act2 with unchanged guest state; package regression pending. |
| K06 | Slot bonus foreground remains confined to native canvas | Finite-arena presentation retained. The machine board and actors remain authored size; decorative B-plane scenery fills the margins. Extending the interactive foreground is a lower-priority enhancement, separate from the already fixed corrupt margins. |
| K07 | Slot bonus red knot background jumps when animation loops | Original eight-frame loop retained. Original-ROM frames1204/1208 and native bonus samples41/42 show the same background animation index7-to0 wrap. The stock Slots background routine resets that frame index and redraws the band; no new animation patch. |
| K08 | Corruption near breakable entrance to AIZ1 second giant ring | Pending reproduction |
| K09 | Companion Tails rapidly turns in corners, AIZ2 and previously LZ1 | Pending bounded assessment |
| K10 | AIZ2 drain waterfall disappears instantly | Prior S3-74 decision retained: original button0 drainage clears the exact four chunks and changes water target0528 to0618. Smooth retirement is an enhancement; no additional fix. |
| K11 | Gumball bonus remains at native aspect | Prior S3-28 margin fix retained; the machine arena intentionally remains finite. A full-width interactive arena is a lower-priority renderer enhancement, not an unqualified claim from the previous margin repair. |
| K12 | Monkey Dude art corrupts after bonus while title-card art is loaded | Pending reproduction |
| K13 | AIZ2 Flying Battery opening and left/right margins flicker | Fixed candidate for fractional camera selection: identify the HInt band using its completed scroll, then interpolate its position. The old selection fails the rendered bobbing-ship fixture; correction passes in both hosts. Actual32:9 route presents9743 interpolated images at144Hz. Native chase/bank handovers remain unchanged; no separate guest timing change. Final package/visual regression pending. |
| K14 | Tree scene pops in as Flying Battery disappears | Fixed candidate: introduce the existing incoming canopy in the right margin during the last second of the final ship loop, before the native scenery handover.600 paired actual-route RAM/VRAM/native-center samples remain unchanged; only8 retained lead-in images change. Package/owner visual regression pending. |
| K15 | Eggman crossing trees corrupts tree art | Pending reproduction |
| K16 | Unloaded tree art remains corrupt at left of AIZ2 boss | Fixed candidate: retain the retired grove's original ship-phase patterns outside the native view after boss art replaces them. Actual chase/boss route:600 paired RAM/VRAM/native-center samples unchanged;71 extended-view images repair the old tree bank. Both renderer fixtures pass; final package regression pending. |
| K17 | Waiting Knuckles freezes and can pop in before outro cutscene | Fixed candidate for pop-in: retain the existing waiting pose while the defeated boss is still retiring, before the capsule wait begins. Actual defeat/outro route has507 paired images, no native-center changes, and6 margin-only changes; four renderer/predicate tests pass. The original120-tick wait and pose remain; a new walk-in staging is a lower-priority enhancement. Package regression pending. |
| K18 | AIZ2–HCZ1 outgoing fade corrupts right margin | Pending reproduction |
| K19 | HCZ1 title card corrupts before incoming level fade | Pending reproduction |
| K20 | HCZ1 background scroll breaks at level's left boundary | Fixed candidate: keep B's original parallax on the centered screen while the foreground viewport clamps at the left edge. The added HCZ regression fails in both old renderer profiles and passes the correction at four widths. Actual HCZ1 camera sweep0..608 retains90 paired RAM/VRAM samples;37 pictures change only in the exposed background. Final package regression pending. |
| K21 | HCZ surface interlacing retains native draw width | Pending reproduction |
| K22 | HCZ1 opening current leaves foreground gap with wrong water palette | Pending reproduction |
| K23 | Current tunnel changes palette before leaving extended view | Pending original/extended-view comparison |

## Sonic & Knuckles Alone

| ID | Report | Disposition |
| --- | --- | --- |
| B01 | Top of title screen corrupts when Sonic changes animation | Fixed candidate: native decoding uploads an overlapping landing-art module while the old falling-pose mapping remains visible. Retire that mapping before publishing the overlapping module through the original FIFO. Native and 32:9 captures remove the corruption; original-ROM comparison confirms the expected pose. Final package regression pending. |
| B02 | Knuckles MHZ1 intro graphics corrupt during fade-in | Fixed candidate: complete the original queued scenery PLC before the opening fade. Native and 32:9 sequences no longer expose partially decompressed blocks. The uniform dark cliff during the first palette steps also occurs in the original ROM and is retained. Final package regression pending. |

## Lower-priority suggestions

- Optional Reversed Frequency soundtrack arrangements; outside this bugfix release.
- Extend the standalone S&K Sega slide-away animation across widescreen.
- Smooth AIZ2 drain-palette retirement beyond original behavior.
- Obscure the buried portions of AIZ2's raised/falling drawbridge; compare original.
- Alternative aspect targets/experimental labels are suggestions, not adopted policy.
- Extend the finite Slots/Gumball interactive foregrounds beyond their authored arenas.

There are **37 report groups**: **12 awaiting decisions**, **13 implemented
candidates awaiting final package regression**, and **12 non-fix dispositions**.
The two explicit prior-disposition reviews are complete. No capture or replay
integrity check alone counts as a fix. Approved asset and nonreproduction
deferrals remain distinct from fixes.

Original-ROM comparisons use Genesis Plus GX v1.7.4 f27655b with the same owned
ROM revisions. The HCZ collision check is an actor/world-state fixture with the
original CPU stack preserved, followed by normal original-ROM execution; it is
not claimed as a complete original playthrough. Rejected setup attempts are
excluded from acceptance.
