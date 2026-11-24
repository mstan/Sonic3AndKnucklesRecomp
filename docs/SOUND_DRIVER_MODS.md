# Clone Driver + Mega PCM

The optional Audio mod uses the S3K Clone Driver port by TheBlad768 and
Clownacy, with Mega PCM 2.1 by Vladikcomper. It is disabled by default.
Enable **Clone Driver + Mega PCM** in Mods and restart the application.

Music, sound effects, instruments, envelopes and PCM samples are prepared
locally from the supported original cartridge. No sound pack is required or
downloaded. The standalone S&K cartridge keeps unavailable lock-on songs and
samples unavailable. Driver licenses are in `CLONE_DRIVER_NOTICES.txt`.

This implementation supports local play in original campaigns with the
standard Z80 backend. Imported campaigns and netplay require the original
sound driver. A changed selection takes effect on the next application start;
the running session keeps its driver and immutable sound bank.

The existing **High refresh rate** mod provides visual interpolation at 120 Hz,
144 Hz or display refresh rate. It works alongside Clone Driver while game
and sound timing remain at their native rate.

One adapter serves Sonic 3, S&K and their combined cartridge. Cartridge layouts,
Z80 sequence conversion and queue translation stay in the game repository.
The engine supplies its existing CPU/chip scheduler, native expansion-ROM
execution and a bounded cartridge RAM mapping. Driver updates follow actual
VBlank interrupts, including pauses and lag, independently of presentation
refresh rate. File saves and local rewind include the private sound RAM and
pending queues; states from a different driver identity are rejected.

The separate **November 1993 prototype soundtrack** restoration option is
disabled by default in all three games. Enable Clone Driver, select your raw
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
disabling Clone Driver; both selections take effect on application restart.

Normal builds automatically prepare pinned driver software when supported.
`S3_CLONE_DRIVER_SUPPORT=ON` requires preparation to succeed; `OFF` omits it;
the default `AUTO` allows builds without the enhancement when preparation is
unavailable. `S3_CLONE_PROGRAM_DIR` can supply the output of
`tools/clone_driver/build.py` for an offline build. The builder accepts a paired
native Macroassembler AS and p2bin through `--assembler` and `--p2bin`.
Generated images, ROM-derived sound data and generated C stay in the build
tree. The original cartridge bytes and native battery saves are preserved.
