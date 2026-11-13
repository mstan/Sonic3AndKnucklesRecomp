# Android build

This module builds the combined Sonic 3 & Knuckles cartridge runner with the
shared launcher. It does not contain a ROM. Supply the supported combined
cartridge at `game/sonic3k/sonic3k.bin` when generating the native code; the
installed app separately imports the player's matching 4 MiB cartridge
(CRC32 `63522553`). Imports, settings and native saves stay in private app
storage. An application update keeps that storage; uninstalling removes it.

Requirements: Java 17, Gradle 8.11.1, Android SDK 35, CMake 3.22.1, NDK
27.1.12297006, Python 3, and an SDL 2 source checkout with its Android Java
sources. The minimum Android API is 24. NDK 27's ARMv7 target requires NEON;
this does not target older non-NEON ARM processors.

Build the host tools from the same engine and pinned submodules first:

```sh
cmake -S segagenesisrecomp/recompiler -B host-tools -DCMAKE_BUILD_TYPE=Release
cmake --build host-tools --config Release --target GenesisRecomp M68kStepRecomp M68kSpanRecomp
gradle -p android assembleDebug \
  -PgenesisHostTools=/absolute/path/to/host-tools \
  -PSDL2_SOURCE_DIR=/absolute/path/to/SDL
```

For multi-configuration generators, use the host tools' `Release` directory.
`genesisEngineRoot`, `genesisUiRoot` and `genesisPython` optionally select
absolute engine, UI and host Python paths. Cross builds run these host tools;
they never try to execute an Android recompiler on the build machine.
On Windows, `genesisNativeBuildRoot` can select a short absolute staging path
when the optional sound-driver assembler's include paths exceed its path limit.

The default ABI set is `arm64-v8a,armeabi-v7a`. `-PgenesisAbis=x86_64` builds
an emulator package. `-PgenesisQa=true` gives a debug build a separate `.qa`
application ID and enables native diagnostics. Compilation alone does not
qualify an ABI's gameplay, controls or audio on a device. Release signing is
configured by the distributor; `assembleRelease` produces an unsigned APK.

The Android importer accepts the raw combined cartridge above. The desktop
runner's optional donor-ROM and external-tool conversion workflows are not
provided by this importer. Widescreen defaults to Fit; saved mod choices are
retained. In gameplay, the Touch controls button toggles the three-button pad.
The pad is hidden while the launcher or a runtime menu owns input.
