Sonic 3 & Knuckles Recomp v0.5.2
============================================================

Native static recompilation for Windows x64 and Linux x86_64.
Bring your own matching raw Genesis ROM; no ROM is included.
Variant: Sonic 3 & Knuckles; ROM size: 4 MB.
Suggested filename: sonic3k.bin. Use the launcher's ROM picker.

WINDOWS
Extract the entire ZIP and run Sonic3KRecomp.exe.
Keep assets/ and the bundled DLLs beside the executable.

LINUX
Place the AppImage in a writable folder, chmod +x it, and launch.
Use --appimage-extract-and-run when FUSE is unavailable.
Requires Ubuntu 24.04 (glibc 2.39) or compatible x86_64 Linux with OpenGL.
Older distributions and Steam Deck have not been validated.

PLAY AND SETTINGS
Configure keyboard/gamepad controls in the launcher. Enable widescreen in
Mods > Widescreen; Adaptive fits the window. Up to four controller seats.
Enable Mods > Knuckles & Knuckles for the optional local crowd mod.
Type 1-74 extras (default 16). Play as Knuckles; controllers 2-4 can drive extras.
Busy stages may temporarily use fewer extras to reserve room for level objects.
The mod is off by default and unavailable in netplay.
With the mod on, empty files and No Save default to Knuckles; existing saves retain their character.

During story gameplay, Shift+F1-F9 saves a state; F1-F9 loads it.
Controller LB/RB saves/loads slot 1. States preserve the stage and crowd.
States require the same build and mod/widescreen configuration. Keep normal
cartridge saves (*.srm) for progress across upgrades.

Preserve settings.ini, rom-*.cfg, *-mods.ini and *.srm when upgrading.
Linux refreshes bundled assets without replacing your settings or saves.
Do not place the application in a read-only installation folder.

OPTIONAL CAMPAIGN MODS (EXPERIMENTAL)
Mods offers six independent switches: Sonic 1 stages, Sonic 2 stages,
Sonic CD stages, Megamix stages, Mighty and Shadow. All default off.
Campaign order: Sonic 1, Sonic 2, Sonic CD, Sonic 3 & Knuckles, Megamix.
Mighty, Shadow and Megamix stages require your matching Megamix v3 ROM;
the three controls share the same ROM and converted-content paths.
Sonic CD requires your supported US disc image, converted with the included
tools. CD-DA music is a separate optional resource. See docs/SONIC_CD.md.
The CD campaign has 70 act/time variants and its own Time Stone progress.
See docs/STAGE_PACKS.md, MEGAMIX_STAGE_PACK.md and SHADOW_CHARACTER.md for
other conversions and character controls. No ROM, BIOS or bank is included.

Keep the .srm save and its .srm.campaign companion, .bak and migration backups
together. Disabled/missing campaigns and characters park their progress for
reinstallation. Quickstates, rewind and netplay are unavailable with these mods.

LICENSE AND SOURCE
Project: PolyForm Noncommercial 1.0.0 (see LICENSE).
Third-party notices: THIRD-PARTY-LICENSES.md and licenses/.
Exact source and dependency commits: build-info.json.
Source: https://github.com/mstan/Sonic3AndKnucklesRecomp
This project is not affiliated with or endorsed by SEGA.
