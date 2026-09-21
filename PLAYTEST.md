# GHZ / EHZ first playtest

Run **Play Trilogy.cmd** from the prepared `build/Trilogy-Playtest` folder.
It opens the game directly. Press Enter at the title, then use Sonic 3's
Data Select menu. Left/right selects a slot; Enter starts it.

Updated build: original GHZ/EHZ art, animated flowers/waterfall, water palette
cycles and background scrolling are restored. HUD, rings and donor enemies
use the correct palettes. Original Sonic 1/2 stage, boss, act-clear, 1-up,
invincibility, game-over and drowning music now comes from the donor ROMs.
Sonic 3 menus, player abilities and sound effects remain in use. The update
preserves your existing playtest save; the table below describes the starter
presets, not necessarily your current progress.

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
| 8 | Emerald Hill 1 | Knuckles |

These are starter presets for testing, not a record of completed playthroughs.
Slots 2 and 3 have the preceding GHZ acts marked cleared. Slots 4 and 8 enroll
only the Sonic 2 chapter. The other slots play GHZ1 -> GHZ2 -> GHZ3 -> EHZ1 ->
native S3&K. A new No Save game starts at GHZ1. Up/down on No Save changes the
character. The GHZ boss has eight hits; release the capsule to finish GHZ3.

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
- Original music, the GHZ boss/results cues, 1-up return, and Sonic 3 music
  resuming when you reach its stages or return to its menus.
- Data Select previews and titles. Report the slot, approximate location,
  character and what you were doing if something breaks.

## Current limits

This is a gameplay prototype. It includes GHZ1-3 and EHZ1; EHZ2 and the other
Sonic 1/2 zones are not included. Hidden bonuses and giant-ring/Blue
Spheres integration remain unfinished. Sonic 3 special stages will serve all
chapters; the original S1/S2 special stages are not part of this build.
Bridges, ledge collapse and some
enemy/object timing are approximations that need playtesting against the
original games. Machine snapshots and netplay are disabled for this experiment.

The launcher uses 4:3. Initial 16:9 rendering checks also pass; wider formats
have not been validated for imported object activation.

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

The corrected visuals were inspected against original S1/S2 captures, including
GHZ2/3, the boss and capsule. Both chapters' seven music cues pass real-driver
playback checks; 1-up returns to the stage music and native audio tables restore
on entering Sonic 3. Captured stage music also closely matches the originals
in a spectral comparison. A human listening/playthrough review remains useful.

## Donor settings

`settings.ini` contains `[trilogy]`, `enabled=1`, `sonic1_rom=...` and
`sonic2_rom=...`. Either donor can be absent. Its chapter progress stays in the
same `.srm`, and loading resumes the next available chapter. Restoring the
donor returns to its unfinished chapter. A native save is enrolled only by
the advertised **B ADD CHAPTERS** action (keyboard X).
