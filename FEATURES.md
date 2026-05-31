# Sonic 3 (standalone) — Features & Roadmap

Companion to `ISSUES.md` (which tracks rendering/value-divergence bugs). This
file tracks **features** of the native PC port and **discovery** for larger
pieces of work. Both features below are engine-level: they live in the shared
`segagenesisrecomp/runner/` (and would-be recompiler), so they apply to the
Sonic 1 / Sonic 2 / Sonic 3 builds alike.

---

## Architecture refresher: what is "native" and what is emulated

The recompiler translates the cartridge's **68000 CPU code** into native C.
That recompiled C is the sole executor of 68K instructions in the native build
— the clown68000 *CPU interpreter* is **excluded from the native link**
(`runner/stub_clown68000.c`: its `Clown68000_DoCycles` runs
`glue_run_game_chunk()`, i.e. the recompiled C, never an interpreted
instruction). The interpreter exists only in the **oracle** build
(`HYBRID_RECOMPILED_CODE`, port 4385) as a differential reference.

Everything *around* the main CPU is provided by `clownmdemu-core`, exactly as a
runtime/runner — this is hardware emulation, **not** CPU interpretation, and is
the same role a renderer/runtime plays in the other recomp projects:

| Subsystem | Native build | Notes |
|---|---|---|
| 68000 main CPU | **recompiled C** | no interpreter linked |
| Memory bus / address map | clownmdemu | `M68kReadCallback`/`WriteCallback`; WRAM lives in `state.m68k.ram`, SRAM in `state.external_ram.buffer` |
| VDP (graphics) | clownmdemu | renders the frame |
| YM2612 + SN76489 (sound chips) | clownmdemu (+ runner audio layer) | |
| **Z80 sub-CPU (sound driver)** | **clownz80 interpreter** | the one remaining interpreter — see Feature B |
| Scheduler / timing / interrupts | `ClownMDEmu_Iterate` | calls into the recompiled CPU once per scanline-sync |

Native and oracle share *identical* clownmdemu hardware emulation; the **only**
difference between them is who runs the 68K. That is what makes the oracle a
valid reference — any divergence isolates to the recompiler, never to
peripheral emulation.

---

## Feature A — battery-backed cartridge SRAM persisted to disk  ✅ implemented (pending end-to-end verify)

**What it is.** Sonic 3 has a battery-backed save (the Data Select slots; the
game auto-saves on act completion). The save data is cartridge SRAM.

**The gap (before this change).** ClownMDEmu emulates cartridge SRAM correctly
*in memory* (`state.external_ram.buffer`, 64 KiB; geometry parsed from the ROM
header by `SetUpExternalRAM` — for Sonic 3 it sees the `'RA'` marker at `$1B0`
and the battery bit `metadata & 0x4000`, so `size>0` and `non_volatile==true`).
But it **never wrote that buffer to disk**: its `save_file_*` callbacks are for
**Mega-CD backup RAM only** (`bus-sub-m68k.c`), and the runner stubbed them
out. There was no `.srm` anywhere. Result: in-game saves worked *within a
session* but were lost on quit/relaunch. The only on-disk saves were full
emulator savestates (`native_save_N.bin`, Shift+F1–F9 / controller LB·RB),
which *do* include SRAM because the whole `g_clownmdemu` struct is serialized.

**The implementation** (`runner/main.c`, shared runner — fully game-agnostic):
- `runner_sram_init_and_load(rom_path)` — called right after
  `ClownMDEmu_HardReset` (which has run `SetUpExternalRAM`). If the cartridge
  reports `non_volatile && size>0`, it loads `<rom-basename>.srm` (next to the
  exe) into `external_ram.buffer`. Cartridges without battery SRAM (Sonic 1/2)
  report `size==0` → no-op.
- `runner_sram_autosave_tick(frame_num)` — once per frame, FNV-1a hashes the
  SRAM buffer; on change it marks dirty, then flushes ~30 frames (~0.5 s) later.
  The delay coalesces the game's multi-frame save burst into one write and
  ensures the save reaches disk well before a manual `taskkill` (the standard
  relaunch path, PRINCIPLES #24). Cheap: hashing only, no per-frame I/O.
- `runner_sram_flush()` — also called on every clean exit path (normal quit and
  the input-script early-return).

**File:** `<exe-dir>/sonic3.srm` (raw SRAM image; round-trips within the
runner). Look for `[SRAM] loaded …` / `[SRAM] flushed …` on stderr.

**How to verify end-to-end (user):**
1. Launch the native build; play Angel Island Act 1 to completion so the game
   auto-saves; watch for `[SRAM] flushed N bytes to …sonic3.srm`.
2. Quit (or `taskkill`), confirm `sonic3.srm` exists next to the exe.
3. Relaunch; the Data Select screen should show the saved progress
   (`[SRAM] loaded …` on boot).

**Notes / possible follow-ups:**
- EEPROM-type external RAM is *not* supported by clownmdemu (`data_size==1` /
  `device_type==2` log a warning) — Sonic 3 uses SRAM, so unaffected.
- The `.srm` is keyed by ROM basename, next to the exe. If we later want
  per-profile saves or a saves/ subfolder, that's a small extension.

---

## Feature B — Z80 sound CPU: recompile vs. keep interpreting  🔬 discovery only

**Question driving this:** for an authentic native PC port, is it acceptable
that the Z80 (the sound co-processor) is still **interpreted** by clownz80,
while the main 68000 is recompiled?

### Current state (discovered)
- The recompiler is **68000-only**. `recompiler/src` contains **zero** Z80
  references — there is no Z80 decoder or code generator. A Z80 backend would
  be net-new.
- The Z80 runs on the **clownz80 interpreter**
  (`clownmdemu-core/libraries/clownz80/source/interpreter.c`), advanced by
  `ClownZ80_DoCycles` / `ClownZ80_Interrupt` from `ClownMDEmu_Iterate`, synced
  to the 68K per scanline (`SyncZ80`).
- The Z80 has **8 KiB RAM** (`state.z80.ram[0x2000]`) plus a **bank register**
  windowing `$8000–$FFFF` into 68K/cartridge space.
- In Sonic 3 the sound driver (SMPS) is a **data blob embedded in the 68K ROM**
  (labels `WaitForZ80` `$254`, `sndDriverInput` `$1340`,
  `Z80_DefaultVariables_end` `$15E2`) that the 68K **copies into Z80 RAM at
  boot**. The Z80 then executes from RAM addresses `$0000+`, not from a fixed
  ROM location.

### Why this is the genuinely hard part of "zero interpreters"
Static recompilation assumes code sits at known, fixed addresses in the image.
The Z80 violates that on several axes:
1. **RAM-resident code.** The driver is copied into Z80 RAM at runtime. A static
   recompiler must (a) locate the driver blob in the 68K ROM, and (b) recompile
   it as code that *executes from Z80 RAM*, not where it sits in ROM.
2. **Bank window.** `$8000–$FFFF` maps into 68K/cartridge space via a runtime
   bank register (used to stream song/sample data). Reads/jumps through the
   window are dynamic.
3. **Self-modifying code.** SMPS patches jump pointers, DAC/sample addresses and
   tempo values in place — a classic static-recomp breaker.
4. **Cycle-accurate interleave.** Music tempo depends on Z80 cycle counts and on
   the per-scanline 68K↔Z80 interleave. A recompiled Z80 needs its own cycle
   accounting to keep audio in sync.

(A Z80 **disassembler** already ships with clownz80 —
`clownz80/source/disassembler.c` — which could seed a recompiler frontend, but
it does not solve 1–4.)

### Options
- **C — keep the clownz80 interpreter (status quo).** It runs the *original*
  Z80 code, bit-accurately, for negligible cost, with zero risk. The game's
  brain (68K) is fully native; the Z80 is a tiny fixed-function sound
  coprocessor that happens to be programmable. **Authenticity-wise this is
  running the real driver** — just interpreted. Recommended default.
- **A — recompile the Z80 (purist "zero interpreters").** Build a Z80→C backend
  and solve 1–4 above. Highest authenticity (original code, natively), highest
  cost and risk; SMC + dynamic banking may force per-driver special-casing.
- **B — HLE the sound driver.** Replace SMPS with a native reimplementation that
  consumes the same song/sample data. Removes the interpreter, but is *less*
  authentic (no longer the original code) and carries audio-fidelity risk.

### Recommendation
For an authentic native PC port, **Option C is the standard, defensible
choice** and is what comparable CPU-recompilation projects do for audio
co-processors: recompile the main CPU, emulate the peripherals (including the
small audio CPU). "Authentic" here means *the original game program runs
natively* — which it does. Pursue **Option A** only as a deliberate
"zero-interpreter purity" goal or if profiling ever shows the Z80 interpreter is
a bottleneck (it isn't). **Avoid Option B** unless authenticity is explicitly
deprioritized.

**If we ever do Option A**, suggested staging:
1. Z80 decoder + C emitter (seed from `clownz80/disassembler.c`), validated
   against the clownz80 interpreter as an oracle (mirror the 68K native/oracle
   differential harness).
2. Model the driver as RAM-resident: recompile the ROM-embedded blob to a
   function table indexed by Z80 PC; trap bank-window and SMC writes.
3. Cycle-accurate `DoCycles` shim so `SyncZ80`/tempo behavior is unchanged.
4. Differential-verify audio against the interpreter before switching the link.
