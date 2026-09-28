# Releasing the Sonic 3 family

Release all three native targets: Sonic3Recomp, Sonic3KRecomp and
SonicAndKnucklesRecomp. Build from a clean committed checkout with initialized
submodules and the owner's ROMs in `game/<mode>/<mode>.bin`. Never distribute
those ROMs, generated art, settings, saves or developer builds.

## Windows x64

```powershell
./tools/build-windows.ps1 -Version 0.5.0 -Python C:/path/to/python.exe
```

Requires Visual Studio 2022 C++ tools, CMake and Python. `-CMake`, `-BuildDir`
and `-NoPackage` are available. Production builds disable tracing, reverse
debugging and co-simulation, and use the static MSVC runtime. Packaging creates
one ZIP per game in `release-stage/`, with the executable, its verified DLL
closure, launcher assets, licenses and exact build metadata. The packager
rejects stale versions/commits, dirty inputs and unpinned submodules.

## Linux x86_64

On Ubuntu 24.04 install `build-essential cmake pkg-config libsdl2-dev
libgl1-mesa-dev curl python3 patchelf imagemagick` and run:

```sh
bash tools/build-linux.sh --version 0.5.0 --jobs 8
```

`--build DIR`, `--out DIR` and `--no-package` are available. An ext4 build
directory is recommended under WSL. The script downloads versioned,
SHA-verified linuxdeploy/appimagetool images, reuses the verified embedded
runtime, bundles each game's library closure and distribution notices, and
runs the AppImage layout gate. Three AppImages land in `release-linux/`.

The AppRun entry point refreshes release-owned launcher assets next to the
AppImage and preserves settings, ROM selection, mod settings and saves.
Adjacent ROM discovery checks the variant's size and product header. Mods
remain off by default. The directory must be writable. FUSE-free launch uses
`--appimage-extract-and-run`. Ubuntu 24.04 determines the minimum glibc 2.39
baseline; older distributions and Steam Deck require separate validation.

## Validate and publish

1. Run CTest for both production builds. Use `tools/verify_release.py` for each
   actual package and its owner ROM. It checks attract state/audio hashes,
   native/wide title captures, every widescreen attract demo (including Blue
   Spheres), mod titles, empty/existing saves and Knuckles
   gameplay. Linux `--reference` can point at the Windows evidence to compare
   every captured pixel and RAM/VRAM byte. Add `--launcher` to capture launcher
   and controller pages (using Xvfb on Linux). Pillow is required.
2. Smoke-test the packaged launcher on both platforms with its assets. Confirm
   Windows starts with a system-only PATH and Linux passes state/ROM discovery,
   relocation and read-only payload checks. Keep evidence under `build*/`.
3. Merge the release tooling/notes to `main`, then reconfigure and package from
   that exact clean commit. `build-info.json` must identify the tag target and
   the committed engine/UI/disassembly pins. Keep repository visibility as-is.
4. Tag `v0.5.0`, publish the three ZIPs, three AppImages and `SHA256SUMS.txt`.
   Verify the remote tag, `origin/main`, asset names/sizes and uploaded SHA256s.

Do not zip a build folder. All staging uses explicit native targets and
allowlisted assets; package contents are audited before publication. The
project license and full third-party notices accompany every package.

## Provenance

Adapted from RocketKnightAdventuresRecomp `70ebd85` (v0.1.1), including its
Windows dependency closure, payload/notices staging, pinned AppImage tools,
state/layout gate and packaged cross-platform validation approach. Those
scripts originated in SuperMarioWorldRecomp `55628231b1fe683fe96062ce5a7f322ab05ed8b3`.
The shared engine and launcher pins are unchanged by release packaging.
