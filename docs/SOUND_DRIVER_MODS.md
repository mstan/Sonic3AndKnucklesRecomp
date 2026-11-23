# Sound and the music library

All three games use the host sound provider by default. The optional
**Music Library and Player** mod enables music customization and the player.
It is disabled by default and does not switch the audio backend. Existing
saved selections retain their identifiers. Restart after changing the library.

Music, sound effects, instruments, envelopes and PCM samples are prepared
locally from the supported original cartridge. No sound pack is required or
downloaded. The standalone S&K cartridge keeps unavailable lock-on songs and
samples unavailable. Driver licenses are in `CLONE_DRIVER_NOTICES.txt`.

The optional library supports local play in original campaigns. Disable its
customizations before imported campaigns or netplay. The original game image
stays read-only; all decoded data is owned by the runtime.

The existing **High refresh rate** mod provides visual interpolation at 120 Hz,
144 Hz or display refresh rate. It works alongside the library while game
and sound timing remain at their native rate.

One adapter serves Sonic 3, S&K and their combined cartridge. Cartridge layouts,
sequence conversion and queue bindings stay in the game repository. The
engine supplies host sequencing, synthesis and native waveform clocks. No
uploaded sound CPU program or ROM overlay is used. Updates follow actual
VBlank interrupts, including pauses and lag, independently of presentation
refresh rate. Current-build saves and rewind retain sound clocks, pending
requests, PCM position, articulation and chip state. Different build or
sound-data identities are rejected; native battery saves are preserved.

Imported Sonic 1, Sonic 2 and Megamix campaigns use the same host provider
with their own songs, percussion and envelopes. Sonic 2 and Sonic CD effects
also bind directly to host resources; CD background tracks retain their
existing CD-audio provider. Experimental campaigns keep their native campaign
saves and existing quickstate restriction.

The separate **November 1993 prototype soundtrack** restoration option is
disabled by default in all three games. Enable the music library, select your
donor ROM in the soundtrack's resource picker, enable the soundtrack and
restart. The supported donor is
[Sonic the Hedgehog 3 (Nov 3, 1993 prototype)](https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype)),
2,097,152 bytes, SHA-1 `fc87a7b411302a576028b7ccd5911b1f266b59b3`.
Extract archives before selecting the ROM. The game verifies the exact donor
at selection and startup; releases include no donor ROM or extracted music.

This option uses the donor's sequences, instruments, envelopes and percussion,
including its unused Super theme. Retail sound effects keep their own data and
envelopes. Donor music and the normal soundtrack have separate save-state
identities. Native battery saves are preserved. Disable the soundtrack before
disabling the library; both selections take effect on application restart.

`S3_AUDIO_BACKEND=HLE` is the default. Generation reads the original image at
its source path and writes separate discovery metadata, with no modified ROM
or uploaded CPU fallback. `S3_AUDIO_BACKEND=LLE` retains the original-driver
accuracy reference when explicitly requested, without the optional library.
The shared provider contract is in the engine's `docs/ENGINE_SMPS_HLE.md`.
Data conversion retains the existing Clone Driver/Mega PCM format provenance
and notices; assembled driver software is not needed by the HLE build.
