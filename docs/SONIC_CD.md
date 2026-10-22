# Sonic CD campaign preview

Enable **Sonic CD stages** in Mods. It is independent of the other five
campaign/character switches and defaults off. Enabled campaigns run in this
order: Sonic 1, Sonic 2, Sonic CD, Sonic 3 & Knuckles, Megamix.

This preview adapts CD's stages, objects, bosses, time travel, UFO stages and
audio to the S3K campaign. Characters and ordinary HUD/abilities use the S3K
presentation; the CD warp/UFO scenes retain their original Sonic artwork.
The [standalone Sonic CD recomp](https://github.com/mstan/SonicCDRecomp) runs
the original CD programs and remains the original-behavior reference. This
campaign deliberately uses S3K player mechanics and presentation. Its CD
objects, bosses, time travel and special scenes are adapted to that campaign;
the shared disc importer does not turn it into an original-program CD session.

## Convert your owned disc

The supported input is **Sonic CD (US)** with one data track and 34 audio
tracks. Use its CUE/BIN files or the same verified `.sgcd` image accepted by
SonicCDRecomp. Extract archives first and keep CUE/BIN relative names together.
No disc, BIOS, ROM, converted pack or save is distributed. This S3K campaign
does not require a BIOS.

Install Python 3.11 or later and Pillow (`python -m pip install Pillow`). Then:

1. Select your supported S3K cartridge in the launcher.
2. Enable **Sonic CD stages** in Mods.
3. Under its Files, select **Sonic CD (US) disc**.
4. Wait for the import progress window. Both **Campaign content pack** and
   **CD music** fill automatically after the complete installation is verified.
5. Select PLAY to save your choices and launch.

The importer runs in the background. A failed import keeps the previous stage
and music selections. Imported resources stay private in `sonic-cd-imports`
beside your campaign settings. Clearing the disc selection does not delete
those resources or any progress. Windows uses the installed Python launcher
(`py -3`), or Python on PATH; `SONIC3K_PYTHON` can name a specific executable.

For command-line conversion, one invocation prepares both resources:

```text
python tools/import_sonic_cd.py --disc "D:/My Disc/Sonic CD (US).sgcd" --s3k "D:/My ROMs/sonic3k.bin" --validator scd_pack_validate.exe --out "sonic-cd-imports"
```

The cartridge input must be the supported 4 MiB lock-on S3K ROM. The converter
validates the disc data, each audio track and the complete resource graph before
replacing an output. On Linux, use the built `scd_pack_validate` executable in
place of the Windows helper. From a source checkout it is a normal CMake target.

For that command-line route, select the two files in the returned directory
under **Campaign content pack** and **CD music** in Mods. Existing separately
converted resources still work; `build_sonic_cd_pack.py --cue` remains supported
and also accepts `--disc` for either input format.

The content pack contains all 70 stage/time variants, UFO/warp
data, Past PCM music and voices. The separate music pack supplies CD-DA tracks;
without it, Present/Future CD-DA music is unavailable. All converted files remain
private. Older experimental TSCD v2 content needs rebuilding for this preview;
the launcher reports that incompatibility rather than loading a partial graph.

## Adaptation boundaries

S3K movement and character abilities change routes, traversal difficulty and
the effort required to charge time travel. HUD, ordinary player art, saves and
campaign flow follow S3K. The CD final sequence continues into the next enabled
campaign. UFO/warp scenes currently retain original Sonic artwork for every
selected character. Some CD monitor types map to S3K rewards. These are explicit
adaptation limits, not claims of original CD gameplay equivalence.

Stage/time-travel content, bosses, generators/projectors, Time Stones, UFO
stages, Past PCM/voices and all 34 CD-DA tracks remain part of the CD campaign.
The shared upstream CUE/SGCD and ISO readers supply owned media; conversion to
S3K layouts, collision tables, palettes and object formats remains game-owned.

## Campaign and saves

Acts 1 and 2 each have Present, Past, Good Future and Bad Future variants.
Act 3 has only Good/Bad Future. Touch a time post and maintain sufficient speed
to travel. Reach a giant ring with 50 rings to enter a UFO stage; destroy its
UFOs through actual jumps to earn one of seven Time Stones. Generator and
projector progress is saved separately from native Chaos/Super Emeralds.
The optional No Save UFO practice selection does not award campaign progress.

Completed files select from the combined campaign catalog. With CD enabled
alone, it contains 70 CD variants followed by 14 S3K stages; each game's zone
numbers restart at 1. From CLEAR, Up selects CD variant 1, while Down wraps to
S3K Zone 14. Fifteen Down presses from CLEAR reach CD variant 70.

Keep `sonic3k.srm`, `sonic3k.srm.campaign`, its `.bak` and immutable migration
backups together. The journal retains disabled or temporarily missing campaign
progress and character identities. Restoring the same supported packs makes
them available again. Unknown future save schemas are kept read-only.
Quickstates, rewind and netplay remain unavailable with campaign mods enabled.

Mighty and Shadow each work independently of Megamix stages. All three require
the same owned Megamix v3 ROM and converted pack; selecting either shared path
from one control fills the corresponding resource for the other two.

Qualified checks and explicit audit limits are in
[SONIC_CD_VALIDATION.md](SONIC_CD_VALIDATION.md). Seeded completed files used in
owner QA are navigation fixtures, not evidence of an earned full playthrough.
