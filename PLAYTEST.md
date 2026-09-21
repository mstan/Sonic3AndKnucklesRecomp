# GHZ / EHZ first playtest

Run **Play Trilogy.cmd** from the prepared `build/Trilogy-Playtest` folder.
It opens the game directly. Press Enter at the title, then use Sonic 3's
Data Select menu. Left/right selects a slot; Enter starts it.

Updated build: original GHZ/EHZ art, animated flowers/waterfall, water palette
cycles and background scrolling are restored. HUD, rings and donor enemies
use the correct palettes. Original Sonic 1/2 stage, boss, act-clear, 1-up,
invincibility, game-over and drowning music now comes from the donor ROMs.
Sonic 3 menus, player abilities and sound effects remain in use. The update
preserves your current save, including the cleared slot 8. The save immediately
before this update is backed up in `before-donor-entry-state-update/sonic3k.srm`.
The table below describes the presets, not necessarily your current progress.

The prepared folder has its own save and local copies of the owner's verified
ROMs. The normal installed game's save is not used.

| Slot | Starting stage | Character |
| --- | --- | --- |
| 1 | Green Hill 1 | Sonic & Tails |
| 2 | Green Hill 2 | Sonic & Tails |
| 3 | Green Hill 3 / boss route | Sonic & Tails |
| 4 | Emerald Hill 1 | Sonic & Tails |
| 5 | Green Hill 1 | Sonic |
| 6 | Green Hill 1 | Tails |
| 7 | Green Hill 1 | Knuckles |
| 8 | Cleared — choose any available stage | Sonic & Tails |

These are starter presets for testing, not a record of completed playthroughs.
Slots 2 and 3 have the preceding GHZ acts marked cleared. Slot 4 enrolls
only the Sonic 2 chapter. The other active slots play GHZ1 -> GHZ2 -> GHZ3 -> EHZ1 ->
native S3&K. A new No Save game starts at GHZ1. Up/down on No Save changes the
character. The GHZ boss has eight hits; release the capsule to finish GHZ3.

On slot 8, **Up/Down chooses a stage or act; Enter starts it**. This includes
GHZ1–3, EHZ1 and the native S3&K stages, including Doomsday. The save has seven
Chaos Emeralds. It is a test preset, not an earned campaign clear.

To test Blue Spheres, use a slot without all seven Chaos Emeralds:

- **GHZ1/2:** finish with at least 50 rings and jump into the original goal ring.
  Act results run first, then Blue Spheres, then the next act. GHZ3 has no ring.
- **EHZ:** activate a new checkpoint with at least 50 rings, then jump into its
  orbiting stars before they disappear. Blue Spheres returns to that checkpoint.

The starting giant rings have been removed. Emeralds and used entrances autosave.
Slot 8 has all seven emeralds, so use another slot for entrance testing.

**Save states work:** Shift+F1 saves; F1 loads. F2-F9 provide more slots with the
same Shift-to-save convention. Controller L1/LB saves slot 1; R1/RB loads it.
The files are `native_save_1.bin` etc. in this folder. Save during stage gameplay
or Blue Spheres, not loading/menu screens. Loading restores progress too. States
require this build and the same enabled donor ROMs; they survive closing the game.
Include the state file when reporting a reproducible bug.

Keyboard defaults: arrows move, Z/X/C are A/B/C, Enter is Start. Controllers
use the existing runner bindings. Down + repeated jump charges a spin dash.
Close the window to quit; progress autosaves. Relaunching resumes the stage
and last checkpoint. To restore the presets, close the game and copy
`starter-save/sonic3k.srm` over the playtest folder's `sonic3k.srm`.

## What to test

- Movement through loops, roll tunnels, bridges, moving/falling platforms and
  the EHZ corkscrew, with your usual character.
- Rings, monitors, springs, spikes, enemy shots, the GHZ boss and capsule.
- Checkpoints, death/retry, quitting/reopening, and act transitions.
- Giant rings, Blue Spheres, emerald awards and the return to the imported act.
- Original music, the GHZ boss/results cues, 1-up return, and Sonic 3 music
  resuming when you reach its stages or return to its menus.
- Data Select previews and titles. Report the slot, approximate location,
  character and what you were doing if something breaks.

## Current limits

This is a gameplay prototype. It includes GHZ1-3 and EHZ1; EHZ2 and the other
Sonic 1/2 zones are not included. Hidden bonuses remain unfinished. Sonic 3
Blue Spheres serves all chapters; the original S1/S2 special stages are not
part of this build.
Bridges, ledge collapse and some
enemy/object timing are approximations that need playtesting against the
original games. Netplay is disabled for this experiment.

The launcher uses 4:3. Initial 16:9 rendering checks also pass; wider formats
have not been validated for imported object activation.

Remaining campaign work is tracked in central Beads `beads-3ixl`: EHZ2 and its
boss, the other donor zones and events, object fidelity, compatible save-format
expansion, and complete routes/campaign clears for every supported character.

## Validation

Verified native-menu launches for all eight presets and all four character
choices in both chapters, No Save, missing/restored donor permutations, and
checkpoint -> death -> retry -> quit -> reload. A component test completed
the full act-transition chain, including eight native-collision boss hits and
the capsule. GHZ1 also cleared from its normal start with controller input
only. Automated controller attempts did not complete the other routes; the
component tests are not a substitute for full human playthroughs.

With the experiment disabled, 16 reference screenshots and RAM/VRAM captures
match the native build byte for byte. Native save writes and checksum repair
preserve the extension records.

Donor entrance tests cover GHZ1/2 goal jumps, EHZ checkpoint entry, the 49/50-ring
threshold, all-emerald suppression, portal expiry, native outcomes and return.
Position/last-sphere fixtures make these component tests, not full playthroughs.
Quickstates are checked across process restarts and different imported stages.

The corrected visuals were inspected against original S1/S2 captures, including
GHZ2/3, the boss and capsule. Both chapters' seven music cues pass real-driver
playback checks; 1-up returns to the stage music and native audio tables restore
on entering Sonic 3. Captured stage music also closely matches the originals
in a spectral comparison. A human listening/playthrough review remains useful.

An independent original-Sonic-1 comparison confirms all three GHZ starts and
camera positions. Across the full foreground maps, 185,856 terrain blocks match
the donor's block IDs, flips and primary solidity; 16x16 art mappings also match.
The owner reviewed the original/port GHZ2/3 start screenshots. This verifies the
terrain and start locations, not full object behavior or completed playthroughs.

## Donor settings

`settings.ini` contains `[trilogy]`, `enabled=1`, `sonic1_rom=...` and
`sonic2_rom=...`. Either donor can be absent. Its chapter progress stays in the
same `.srm`, and loading resumes the next available chapter. Restoring the
donor returns to its unfinished chapter. A native save is enrolled only by
the advertised **B ADD CHAPTERS** action (keyboard X).
