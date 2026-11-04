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
| A03 | Sound-test HCZ2 music briefly hitches early in track | Pending bounded reproduction |
| A04 | Music may play faster than expected | Tentative; measure original timing |
| A05 | Holding window caption buttons stalls or affects playback | Existing upstream fix incorporated: cooperative event dispatch now covers caption-button tracking, as well as move/resize. Combined Windows modal-event regression passes. Package regression pending. |
| A06 | AIZ1 bombing transition slightly slows | Pending original comparison |
| A07 | Giant-ring entry lacks sound | Pending original comparison |
| A08 | Blue Spheres disappear near screen edges at native aspect | Original native-canvas culling retained. Original-ROM special-stage captures reproduce the finite projection and clipped edge spheres. No change to the already accepted enhanced widescreen projection. |
| A09 | AIZ heat haze disjoins waterfall across waterline | Pending original comparison |
| A10 | Companion Tails behaves strangely at AIZ2 end | Pending replay reproduction |
| A11 | Rolling early through HCZ1 spring/loop clips into wall and may zip | Original behavior reproduced. The native recording and original-ROM collision fixture both settle inside the wall at X1AF5/Y03EC, with matching actor position, velocity, collision and object-control state. Fixture starts before the spring traversal; it does not set the resulting collision state. No correctness patch. |
| A12 | Initial HCZ2 camera fails to follow, causing death; retry works | Fixed candidate: fixed splash slot B172 overwrites live end-sign controller before native act-size installation. Both native allocation loops and the custom widescreen placement loader reserve the slot. Recorded-input transition reaches stored bounds 6000/1000 and a living player in lower HCZ2 with camera following. Gameplay scope/CCR/RAM and real widescreen placement tests pass. Final package regression pending; S3K occurrence unverified. |

## Sonic 3 & Knuckles

The following reports use 32:9 and Display-refresh high FPS. The original
transformation/cutscene timing and guest simulation must remain correct.

| ID | Report | Disposition |
| --- | --- | --- |
| K01 | AIZ1 intro Super transformation looks wrong in extended view | Pending bounded assessment |
| K02 | Horizontal background seams in pre-bombing AIZ1 and AIZ2 drain area | Pending reproduction |
| K03 | Hollow trunk interior closes again near AIZ1 miniboss | Pending reproduction; original first-entry animation excluded |
| K04 | AIZ1 miniboss background disjoins and right-side graphics corrupt | Pending reproduction |
| K05 | Corruption slides back to normal after AIZ1 miniboss | Pending reproduction |
| K06 | Slot bonus foreground remains confined to native canvas | Pending reproduction |
| K07 | Slot bonus red knot background jumps when animation loops | Pending original comparison |
| K08 | Corruption near breakable entrance to AIZ1 second giant ring | Pending reproduction |
| K09 | Companion Tails rapidly turns in corners, AIZ2 and previously LZ1 | Pending bounded assessment |
| K10 | AIZ2 drain waterfall disappears instantly | Review prior approved asset/nonreproduction disposition; no fix claimed |
| K11 | Gumball bonus remains at native aspect | Review prior disposition and renderer coverage |
| K12 | Monkey Dude art corrupts after bonus while title-card art is loaded | Pending reproduction |
| K13 | AIZ2 Flying Battery opening and left/right margins flicker | Pending replay reproduction; retain each phase |
| K14 | Tree scene pops in as Flying Battery disappears | Pending reproduction |
| K15 | Eggman crossing trees corrupts tree art | Pending reproduction |
| K16 | Unloaded tree art remains corrupt at left of AIZ2 boss | Pending reproduction |
| K17 | Waiting Knuckles freezes and can pop in before outro cutscene | Pending reproduction; retain original cutscene timing |
| K18 | AIZ2–HCZ1 outgoing fade corrupts right margin | Pending reproduction |
| K19 | HCZ1 title card corrupts before incoming level fade | Pending reproduction |
| K20 | HCZ1 background scroll breaks at level's left boundary | Pending reproduction |
| K21 | HCZ surface interlacing retains native draw width | Pending reproduction |
| K22 | HCZ1 opening current leaves foreground gap with wrong water palette | Pending reproduction |
| K23 | Current tunnel changes palette before leaving extended view | Pending original/extended-view comparison |

## Sonic & Knuckles Alone

| ID | Report | Disposition |
| --- | --- | --- |
| B01 | Top of title screen corrupts when Sonic changes animation | Fixed candidate: native decoding uploads an overlapping landing-art module while the old falling-pose mapping remains visible. Retire that mapping before publishing the overlapping module through the original FIFO. Native and 32:9 captures remove the corruption; original-ROM comparison confirms the expected pose. Final package regression pending. |
| B02 | Knuckles MHZ1 intro graphics corrupt during fade-in | Fixed candidate: complete the original queued scenery PLC before the opening fade. Native and 32:9 sequences no longer expose partially decompressed blocks. The uniform dark cliff during the first palette steps also occurs in the original ROM and is retained. Final package regression pending. |

## Lower-priority suggestions

- Extend the standalone S&K Sega slide-away animation across widescreen.
- Smooth AIZ2 drain-palette retirement beyond original behavior.
- Obscure the buried portions of AIZ2's raised/falling drawbridge; compare original.
- Alternative aspect targets/experimental labels are suggestions, not adopted policy.

There are **37 report groups**: **29 awaiting decisions**, **5 implemented
candidates awaiting final package regression**, and **3 non-fix dispositions**.
The two explicit prior-disposition reviews remain open. No capture or replay
integrity check alone counts as a fix. Approved asset and nonreproduction
deferrals remain distinct from fixes.

Original-ROM comparisons use Genesis Plus GX v1.7.4 f27655b with the same owned
ROM revisions. The HCZ collision check is an actor/world-state fixture with the
original CPU stack preserved, followed by normal original-ROM execution; it is
not claimed as a complete original playthrough. Rejected setup attempts are
excluded from acceptance.
