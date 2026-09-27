Sonic 3 & Knuckles Recomp v0.4.0
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
Choose 8/16/24/34 extras. Play as Knuckles; controllers 2-4 can drive extras.
The mod is off by default and unavailable in netplay.
With the mod on, empty files and No Save default to Knuckles; existing saves retain their character.

Preserve settings.ini, rom-*.cfg, *-mods.ini and *.srm when upgrading.
Linux refreshes bundled assets without replacing your settings or saves.
Do not place the application in a read-only installation folder.

LICENSE AND SOURCE
Project: PolyForm Noncommercial 1.0.0 (see LICENSE).
Third-party notices: THIRD-PARTY-LICENSES.md and licenses/.
Exact source and dependency commits: build-info.json.
Source: https://github.com/mstan/Sonic3AndKnucklesRecomp
This project is not affiliated with or endorsed by SEGA.
