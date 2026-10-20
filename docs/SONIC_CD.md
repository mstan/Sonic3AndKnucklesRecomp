# Sonic CD campaign preview

Enable **Sonic CD stages** in Mods. It is independent of the other five
campaign/character switches and defaults off. Enabled campaigns run in this
order: Sonic 1, Sonic 2, Sonic CD, Sonic 3 & Knuckles, Megamix.

This preview adapts CD's stages, objects, bosses, time travel, UFO stages and
audio to the S3K campaign. Characters and ordinary HUD/abilities use the S3K
presentation; the CD warp/UFO scenes retain their original Sonic artwork.
The separate general Sega CD recompilation backend is still in development.

## Convert your owned disc

The supported input is **Sonic CD (US)** with one data track and 34 audio
tracks. Extract your own archive to its CUE/BIN files; keep their relative
names together. No disc, BIOS, ROM, converted pack or save is distributed.
This campaign conversion does not require a BIOS.

Install Python 3.11 or later and Pillow (`python -m pip install Pillow`). From
the extracted preview folder, run these commands with your own paths:

```text
python tools/build_sonic_cd_pack.py --cue "D:/My Disc/Sonic CD (US).cue" --s3k "D:/My ROMs/sonic3k.bin" --kind content --validator scd_pack_validate.exe --out "mods/sonic-cd-stages.pak"
python tools/build_sonic_cd_pack.py --cue "D:/My Disc/Sonic CD (US).cue" --kind audio --out "mods/sonic-cd-audio.pak"
```

The cartridge input must be the supported 4 MiB lock-on S3K ROM. The converter
validates the disc data, each audio track and the complete resource graph before
replacing an output. On Linux, use the built `scd_pack_validate` executable in
place of the Windows helper. From a source checkout it is a normal CMake target.

Select **Campaign content pack** and, optionally, **CD music** under Sonic CD's
Mods resources. The content pack contains all 70 stage/time variants, UFO/warp
data, Past PCM music and voices. The separate music pack supplies CD-DA tracks;
without it, Present/Future CD-DA music is unavailable. All converted files remain
private. Older experimental TSCD v2 content needs rebuilding for this preview;
the launcher reports that incompatibility rather than loading a partial graph.

## Campaign and saves

Acts 1 and 2 each have Present, Past, Good Future and Bad Future variants.
Act 3 has only Good/Bad Future. Touch a time post and maintain sufficient speed
to travel. Reach a giant ring with 50 rings to enter a UFO stage; destroy its
UFOs through actual jumps to earn one of seven Time Stones. Generator and
projector progress is saved separately from native Chaos/Super Emeralds.
The optional No Save UFO practice selection does not award campaign progress.

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
