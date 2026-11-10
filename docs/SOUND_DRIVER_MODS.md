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

One adapter serves Sonic 3, S&K and their combined cartridge. Cartridge layouts,
Z80 sequence conversion and queue translation stay in the game repository.
The engine supplies its existing CPU/chip scheduler, native expansion-ROM
execution and a bounded cartridge RAM mapping. Driver updates follow actual
VBlank interrupts, including pauses and lag, independently of presentation
refresh rate. File saves and local rewind include the private sound RAM and
pending queues; states from a different driver identity are rejected.

Normal builds automatically prepare pinned driver software when supported.
`S3_CLONE_DRIVER_SUPPORT=ON` requires preparation to succeed; `OFF` omits it;
the default `AUTO` allows builds without the enhancement when preparation is
unavailable. `S3_CLONE_PROGRAM_DIR` can supply the output of
`tools/clone_driver/build.py` for an offline build. The builder accepts a paired
native Macroassembler AS and p2bin through `--assembler` and `--p2bin`.
Generated images, ROM-derived sound data and generated C stay in the build
tree. The original cartridge bytes and native battery saves are preserved.
