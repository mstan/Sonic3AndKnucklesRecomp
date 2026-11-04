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
| A01 | Launcher cover-art rating differs from other games | Pending identity check; cosmetic |
| A02 | Competition background is dark | Pending original comparison |
| A03 | Sound-test HCZ2 music briefly hitches early in track | Pending bounded reproduction |
| A04 | Music may play faster than expected | Tentative; measure original timing |
| A05 | Holding window caption buttons stalls or affects playback | Pending reproduction |
| A06 | AIZ1 bombing transition slightly slows | Pending original comparison |
| A07 | Giant-ring entry lacks sound | Pending original comparison |
| A08 | Blue Spheres disappear near screen edges at native aspect | Pending original comparison |
| A09 | AIZ heat haze disjoins waterfall across waterline | Pending original comparison |
| A10 | Companion Tails behaves strangely at AIZ2 end | Pending replay reproduction |
| A11 | Rolling early through HCZ1 spring/loop clips into wall and may zip | Pending replay reproduction; high priority |
| A12 | Initial HCZ2 camera fails to follow, causing death; retry works | Reproduced: fixed splash slot B172 overwrites live end-sign controller before native act-size installation. Candidate reserves the slot in both native allocation loops; actual recorded-input transition now reaches stored bounds 6000/1000 and a living player in lower HCZ2 with camera following. Gameplay scope/CCR/RAM tests pass. Final regression and release pending; S3K occurrence unverified. |

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
| B01 | Top of title screen corrupts when Sonic changes animation | Pending reproduction; also check native aspect |
| B02 | Knuckles MHZ1 intro graphics corrupt during fade-in | Pending reproduction; also check native aspect |

## Lower-priority suggestions

- Extend the standalone S&K Sega slide-away animation across widescreen.
- Smooth AIZ2 drain-palette retirement beyond original behavior.
- Obscure the buried portions of AIZ2's raised/falling drawbridge; compare original.
- Alternative aspect targets/experimental labels are suggestions, not adopted policy.

There are **37 report groups awaiting decisions**, including the two explicit
prior-disposition reviews. None is counted as a new fix merely from capture or
replay integrity checks. Previously approved asset and nonreproduction deferrals
remain distinct from fixes.
