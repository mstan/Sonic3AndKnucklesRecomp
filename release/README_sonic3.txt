Sonic 3 Recomp v0.5.0
============================================================

Native static recompilation for Windows x64 and Linux x86_64.
Bring your own matching raw Genesis ROM; no ROM is included.
Variant: Sonic 3; ROM size: 2 MB.
Suggested filename: sonic3.bin. Use the launcher's ROM picker.

WINDOWS
Extract the entire ZIP and run Sonic3Recomp.exe.
Keep assets/ and the bundled DLLs beside the executable.

LINUX
Place the AppImage in a writable folder, chmod +x it, and launch.
Use --appimage-extract-and-run when FUSE is unavailable.
Requires Ubuntu 24.04 (glibc 2.39) or compatible x86_64 Linux with OpenGL.
Older distributions and Steam Deck have not been validated.

PLAY AND SETTINGS
Configure keyboard/gamepad controls in the launcher. Enable widescreen in
Mods > Widescreen; Adaptive fits the window. Up to four controller seats.

Preserve settings.ini, rom-*.cfg, *-mods.ini and *.srm when upgrading.
Linux refreshes bundled assets without replacing your settings or saves.
Do not place the application in a read-only installation folder.

LICENSE AND SOURCE
Project: PolyForm Noncommercial 1.0.0 (see LICENSE).
Third-party notices: THIRD-PARTY-LICENSES.md and licenses/.
Exact source and dependency commits: build-info.json.
Source: https://github.com/mstan/Sonic3AndKnucklesRecomp
This project is not affiliated with or endorsed by SEGA.
