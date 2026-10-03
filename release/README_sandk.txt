Sonic & Knuckles Recomp v0.5.2
============================================================

Native static recompilation for Windows x64 and Linux x86_64.
Bring your own matching raw Genesis ROM; no ROM is included.
Variant: Sonic & Knuckles; ROM size: 2 MB.
Suggested filename: sandk.bin. Use the launcher's ROM picker.

WINDOWS
Extract the entire ZIP and run SonicAndKnucklesRecomp.exe.
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

During story gameplay, Shift+F1-F9 saves a state; F1-F9 loads it.
Controller LB/RB saves/loads slot 1. States preserve the stage and crowd.
States require the same build and mod/widescreen configuration. Keep normal
cartridge saves (*.srm) for progress across upgrades.

Preserve settings.ini, rom-*.cfg, *-mods.ini and *.srm when upgrading.
Linux refreshes bundled assets without replacing your settings or saves.
Do not place the application in a read-only installation folder.

LICENSE AND SOURCE
Project: PolyForm Noncommercial 1.0.0 (see LICENSE).
Third-party notices: THIRD-PARTY-LICENSES.md and licenses/.
Exact source and dependency commits: build-info.json.
Source: https://github.com/mstan/Sonic3AndKnucklesRecomp
This project is not affiliated with or endorsed by SEGA.
