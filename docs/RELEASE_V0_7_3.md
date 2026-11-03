# Sonic family v0.7.3 release record

Published: [v0.7.3](https://github.com/mstan/Sonic3AndKnucklesRecomp/releases/tag/v0.7.3).

The release delivers40 further qualified fixes. The S3-family burndown now has75 released fixes,35 explicit original/nonreproduction/asset dispositions and0open reports. Deferred work is not counted as fixed. Standalone Sonic1/2 remain separate projects.

The source tag points to `294fd5be11a5eae508599f667aa1e7a7bcb8a688`, with engine `13d065d0f68c0d497cbd22c35e166d56b1192481` and UI `aee41e1dc834c105fd290d7c833bbbd7fbc71c1e`. Documentation recorded after publication does not change those binary identities.

| Archive | Bytes | SHA256 |
| --- | ---: | --- |
| Sonic3KRecomp-linux-x86_64-v0.7.3.AppImage | 41925112 | `1653613b13d584f8a67cca0cf684c67b65e61c7cd57280dabea9839ef3d03e02` |
| Sonic3KRecomp-windows-x64-v0.7.3.zip | 37897124 | `3e668006665507a12107ce553a21752a80c0403858d139bb1b46e1e98ed54187` |
| Sonic3Recomp-linux-x86_64-v0.7.3.AppImage | 15862264 | `74f6475619f1476401316e8f637f7d2b2291c523fad03c0185bff16b2fddd64e` |
| Sonic3Recomp-windows-x64-v0.7.3.zip | 9479996 | `8a85e8fd4ff70f27112b0eae821d79adc33f8903c69a2358d59ddedfb01b51fb` |
| SonicAndKnucklesRecomp-linux-x86_64-v0.7.3.AppImage | 19933688 | `bbaaa070a573fbb5593d6d0782c90756fd3ac653db2ed569cade37c9fd5d418a` |
| SonicAndKnucklesRecomp-windows-x64-v0.7.3.zip | 13706826 | `adeb77c678035ffc082188eb621a41eea0e9fc3e16dd4bb54440c198ddaa82a9` |

Each archive has a published SHA256 sidecar. All12 public downloads were streamed after publication and matched their exact sizes and hashes; the release is marked latest. No proprietary ROM or converted stage bank is bundled.

Three Windows and three Linux production builds pass15 focused checks per platform. The packages pass supported layout/no-ROM/relocation/settings gates, actual fresh launcher/gameplay Adaptive Fit plus diagnostics, explicit saved Off/21:9 choices, complete diagnostic replay and native-save preservation. The three native Windows/Linux references match RAM,VRAM andRGBA. A real packaged Mods/provider Sonic1 import passes conversion, PLAY commit, persisted resource selection and imported-stage gameplay; its native OS file chooser was not automated.

Current-build disk save/load matches settled images/VRAM/mode/lives. The first image comparison was adjacent native ticks, so it does not prove immediate same-phase parity. Actual local rewind and focused first-restored-publication tests are separate evidence. Exact joined-fire native-only rewind was not observed; the joined scene passes focused state roundtrips and actual native-intro rewind passes78 image/RAM/VRAM pairs. Other per-report limitations remain in [the burndown](PLAYER_FEEDBACK_BURNDOWN.md).

The owner confirmed all three reviewed builds look good. The guarded reviewed heads were merged in:

- [mstan/Sonic3AndKnucklesRecomp PR17](https://github.com/mstan/Sonic3AndKnucklesRecomp/pull/17): merge `4e0b5f5949076c124b01ab46baf392e6397b2f9d`, qualified head `294fd5be11a5eae508599f667aa1e7a7bcb8a688`.
- [mstan/segagenesisrecomp PR65](https://github.com/mstan/segagenesisrecomp/pull/65): merge `5d6ebf9acf854f2c31141ba33616eaf158c636b4`, qualified head `13d065d0f68c0d497cbd22c35e166d56b1192481`.
- [RetroPortingToolKit/recomp-ui PR129](https://github.com/RetroPortingToolKit/recomp-ui/pull/129): merge `dd47b1887b1858473bb42c38f4307f0eb1b07e60`, qualified head `aee41e1dc834c105fd290d7c833bbbd7fbc71c1e`.

Fresh packages default to Adaptive Fit widescreen and diagnostics. Updates preserve explicit saved choices and native saves; old save states remain build-specific. Missing character artwork remains deferred when suitable existing frames are unavailable. No movement/asset stretch feature was added.
