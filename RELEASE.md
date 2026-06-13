# Sonic 3 and Knuckles — macOS (Apple Silicon) build

Native arm64 macOS build of Sonic 3 and Knuckles, attached to release **v0.2.0** as
`Sonic3AndKnucklesRecomp-macos-arm64.zip`.

## What this is
- The original game statically recompiled to native arm64 (no emulator core shipped).
- Self-contained `.app`: SDL2 bundled via `@executable_path`, ad-hoc codesigned.
- Verified by manual play on Apple Silicon (looks/sounds correct on the golden path).


## Install
1. Download `Sonic3AndKnucklesRecomp-macos-arm64.zip` from the **v0.2.0** release and unzip.
2. First launch: right-click `Sonic 3 and Knuckles.app` -> Open (ad-hoc signed), or
   `xattr -dr com.apple.quarantine "Sonic 3 and Knuckles.app"`.
3. ROM not included — supply your own dump: Sonic 3 & Knuckles (Genesis) .bin/.md dump
4. Run: `"Sonic 3 and Knuckles.app/Contents/MacOS/Sonic 3 and Knuckles" /path/to/rom`

## Build it yourself
`scripts/release-mac.sh` reproduces this artifact (build -> .app -> zip);
`scripts/release-mac.sh --publish` re-attaches it to the latest release.
Requires: `brew install cmake ninja sdl2 dylibbundler` on Apple Silicon.
