# 0.8.1

This update covers Sonic 3, Sonic & Knuckles and Sonic 3 & Knuckles. Options
appear where the corresponding cartridge supports the content.

## Optional restoration

Mods now has independent, off-by-default options for unused Competition course
cards, the Mushroom Valley name, original zone order, prototype music,
prototype palettes, early badnik art and prototype backgrounds. Original zone
order moves Flying Battery before IceCap and preserves the actual saved stage
when the option is changed. Competition cards and early names use assets
already present in the supported retail ROM.

Prototype music and graphics require a player-supplied **Sonic the Hedgehog 3
(Nov 3, 1993 prototype)** ROM. The relevant mod controls link to the
[Hidden Palace documentation](https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype)).
No donor ROM or extracted prototype assets are distributed. Changes require a
restart. These restoration options support local original campaigns; imported
campaigns and netplay retain their supported original content and audio paths.

Restored backgrounds retain retail foreground collision and scripted boss
scenes. Native palette animation remains active. Selected prototype water
colors also apply to retained Hydrocity scenery outside the native viewport.

## Audio

- Corrected Clone Driver SEGA sample pitch and restored the full logo sample
  after skipping a title demonstration.
- Corrected high transposed FM notes used by Knuckles and miniboss music,
  including the original cartridge-specific track variants.
- Corrected FM volume-command parameters and preserved the original PSG
  behavior, restoring save-select percussion balance.
- Reset DAC song panning when changing music, preventing a preceding track
  from leaving subsequent music predominantly in one speaker.

Boss explosions, Hydrocity fan effects and Blue Spheres pickup effects were
checked against the original driver. Their original voices, pitches and
envelopes are retained. Clone Driver remains optional; the original driver is
the default.

## Gameplay and presentation

- Accepted the verified Steam cartridge variant when importing the Megamix
  character add-on, while preserving the supplied file and rejecting
  unsupported variants.
- Restored Knuckles Army execution state during rewind and rollback.
- Applied native currents, fans, whirlpools and spike interactions to extra
  Knuckles actors without stepping the stage object multiple times.
- Reserved usable native child-object slots in crowded scenes so act
  transitions can create their camera-unlock objects.
- Preserved capsule activation timing, including IceCap 2's capsule after
  backtracking, and native Hydrocity fan/current activation timing.
- Reset the complete Super palette index after depowering, preventing a
  subsequent transformation from reading an invalid palette sequence.
- Kept Carnival Night miniboss scenery in its streamed background bank and
  delayed margin enemy graphics until returning title-card art retires.
- Preserved Blue Spheres shadow depth beneath projected spheres and retained
  the original character anchor in the expanded view.
- Corrected Angel Island background publication during partial redraws and
  retained retired grove art outside the boss arena after native VRAM reuse.
- Corrected expanded vertical layout wrapping, finite IceCap snowbanks,
  partial cave transitions and the left snowboard hill.
- Completed expanded gameplay object passes before vertical interrupt,
  preserving high-speed contact with IceCap's floating ice.

Original camera motion, Hydrocity brick color boundaries, giant-ring collection
eligibility, Gumball's centered cabinet HUD and the Angel Island waterfall
artwork are preserved. Native boss damage flashes and capsule camera-extension
timing are also retained.

Save states remain specific to their build. Preserve native battery saves and
settings when upgrading. Widescreen defaults to Fit to screen for new profiles;
existing explicit choices are preserved.
