# Sonic 3 (standalone) — Known Issues

Sonic-3-standalone bring-up. Boots to AIZ Act 1, playable, **0 dispatch
misses**. All known remaining problems are **value divergences** in the
recompiled C (the recompiler is the product — never patched in the runner;
oracle/interpreter renders these correctly). See
`segagenesisrecomp/PRINCIPLES.md` (local junction to the top-level checkout).

Ports: native = 4384, oracle = 4385. `debug.ini` must sit next to each exe.
Observe via the always-on rings (`frame_timeseries` / `get_frame` /
`object_table` / `rdb_*` / `read_vram` / `read_cram`); **never** pause/step
(pause is disabled by policy) — PRINCIPLES #17/#22.

---

## Current focus: attract-demo parity (no user input required)

**Strategy (2026-05-28):** Instead of chasing individual per-object render
bugs by hand-driving, use the **attract demo** as an autonomous parity
harness. The demo is canned-input playback — it exercises Sonic physics,
camera, object spawns/interactions and rendering with zero controller
input, and is fully deterministic, so native and oracle should be
bit-identical through it. Any divergence is a recompiler bug, surfaced
without manual driving.

### Demo mechanism (from s3.lst, ground truth)

| Symbol | Addr | Notes |
|---|---|---|
| `Demo_timer`       | `$FFF614` (w) | Counts down each title frame. |
| `Demo_mode_flag`   | `$FFFFF0` (w) | 1 ⇒ input comes from demo data, not the pad. |
| `Next_demo_number` | `$FFFFF2` (w) | Demo index; increments then wraps 0→1→2→0. |
| `Demo_data_addr`   | `$FFEF52` (l) | Advances as the demo's input stream is consumed. |
| `Game_mode`        | `$FFF600` (b) | title `0x04`; **demo level `0x08`**; normal play `0x0C`. |

Flow: boot → SEGA logo → title (`Wait_Title` @ `$375E`, `Game_mode 0x04`)
sets `Demo_timer = 15*60 = 900` (`$3740`). On expiry with no Start pressed →
`$3978`: fade out, pick `DemoLevels[Next_demo_number]`, set the zone/act
vars, increment+wrap `Next_demo_number`, set `Demo_mode_flag = 1`,
**`Game_mode = 0x08`**, reset lives/rings/score, run the demo level. So
**~15 s idle on the title → demo plays automatically**.

### Status / next steps
- [x] Native **enters and plays** the demo after title idle (game_mode
      0x04→0x08, demo input consumed). Demos cycle AIZ→HCZ→… The AIZ demo
      runs Sonic right past the MonkeyDude (world x≈6200) — so the demo is a
      no-input repro of visual bug #1.
- [x] Drift-tolerant native↔oracle comparator over the rings, anchored on
      demo position / `sonic_x` (NOT wall-frame): `tools/demo_catch.py`,
      `tools/monkey_trace.py`, `tools/analyze_trace.py`,
      `tools/analyze_frames.py`, `tools/demo_watch.py`, `tools/mk_probe.py`.
      Finding: at identical `sonic_x` everything is bit-identical between
      native and oracle **except** RNG-/WaitOffscreen-affected objects.
- [x] First divergence localized + root-caused → the WaitOffscreen
      return-capture `jsr` recompiler bug (see visual bug #1 below). Fix
      staged in `recompiler/src`; behavioral re-verify pending a native
      rebuild.
- [x] Rebuilt native, monkey body renders correctly — **user-confirmed
      2026-05-31**. The WaitOffscreen return-capture fix is validated end-to-end.
- [x] AIZ water/reflection bug (#3) **FIXED 2026-06-03** — HInt was dispatched
      at the static stub instead of the RAM-installed handler; see #3 below.
- [ ] Next: pivot to the Giant Ring (#2) and the Special Stage rings (#4) with
      the same attract-demo parity harness.

---

## Confound: free-running RNG / VInt-timing drift

**Finding (2026-05-28):** Two long-lived free-running instances left idle
in-level were **bit-identical** (`RNG_seed $FFF636 = 0x0000` on both, same
object table, same Sonic pos) despite a constant **+22 internal-frame
(V_int_run_count) offset** between them. That offset is the known-benign
cycle-timing approximation (native vs oracle traverse interrupt-masked
DMA/decompress sections at slightly different rates) — it does NOT by itself
cause state divergence while inputs match.

**But:** several objects consume `Random_Number`/`RNG_seed` (e.g. the
MonkeyDude swing — `sub_55434` @ `$55434`). If the two builds ever consume
RNG a different number of times (e.g. during a DMA-heavy load), their RNG
trajectories diverge and any RNG-driven object will *look* different without
that being a logic bug. **Implication:** for clean A/B, anchor on identical
start state + identical input (the demo gives both for free) and verify
`RNG_seed` matches at the comparison point. Do not chase the +22 VInt
offset.

---

## Visual bug #1 — MonkeyDude (Coconuts) body invisible  — **FIXED — user-confirmed (2026-05-31): the swinging monkey renders properly**

**Root cause (recompiler):** `jsr`/`bsr` to a routine that *unconditionally
pops its own return address off the stack* — the `Obj_WaitOffscreen` idiom
(`$53FD8`: `move.l (sp)+,$34(a0)`, which saves the return PC ($54F56) as the
object's resume code pointer and redirects control via `jmp`) — was emitted
as a normal call that **falls through to the instruction after the `jsr`**
(and did a second A7 pop). On real hardware that pop makes WaitOffscreen's
`rts` return to the *dispatcher*, so `$54F56` runs **next** frame as the
object's code pointer — never by fall-through. The recompiled caller instead
ran `$54F56` (the on-screen dispatch → routine-0 init `loc_54F6E`) **a frame
early**, so the monkey created its children and advanced its routine before
it should, ending stuck at code=`$54FBE` (resume routine) with `timer_2E`
frozen — drawing the wrong frame, body art (tile `$548`) never rendered.

**Diagnosis method (attract-demo parity, no input):** caught the AIZ demo at
identical `sonic_x=6199` on native vs oracle (`demo_catch.py`); every object
was bit-identical *except* the monkey. Per-frame trace (`monkey_trace.py` +
`analyze_frames.py`) showed both spawn the monkey identically at sx=5793
(`code=$4F52`), but one frame later native had already run init (children
created, `$34=$4FBE`, routine 2) while oracle had not (`$34=$4F56`, routine
0, code=`$3FFC` wait). Camera (`Camera_X_pos_coarse_back`) was frame-identical
throughout — ruling out activation-timing — leaving the fall-through as the
only explanation, confirmed in the generated `func_054F52`.

**Fix (in recompiler, regen'd; NOT yet built/verified):**
`function_finder.c` gained `function_finder_pops_return_unconditionally()`
(strict: a `move.l (a7)+` long pop reached at entry with no intervening
push/call/branch). `code_generator.c` emits a `jsr`/`bsr` to such a routine
as a **non-returning tail transfer** — `recomp_push_return; call; return;` —
with no second pop and no boundary fall-through. In S3 this fires on exactly
**36 call sites, all to `func_053FD8` (Obj_WaitOffscreen)** — no over-firing.
Regen: 11051 funcs, 0 unsupported, dispatch audit clean. Generated
`func_054F52` now matches the oracle. **Behavioral verify: DONE** — native
rebuilt and the user confirmed (2026-05-31) the swinging monkey renders
properly in AIZ Act 1.

### (original notes)

AIZ Act 1 swinging monkey (world x≈6200; object base `$FFB000`, size `$4A`).
Body sprite pieces (art tile `$548`, VRAM `$A900`) land at **Y=0** in the
VDP sprite table (`$F800`) in native — hidden above the screen — while the
oracle draws the full body on-screen.

**Ruled out:** monkey object logic in isolation (oracle sandbox-compare on
`func_054F52/4F56/50C8/5218/5248` = 0 divergence), art at VRAM `$A900`,
palette line 1, camera_Y. So it's an *upstream value/state* divergence that
puts the monkey's state machine in a different place, not a bug in the
monkey's own recompiled code.

**State-machine map (s3.lst):**
- `Obj_MonkeyDude` `$54F52`: `jsr Obj_WaitOffscreen` then dispatch by
  `routine($05)` via `MonkeyDude_Index` `$54F68` → `loc_54F6E` (r0 init),
  `loc_54FB8` (r2), `loc_54FD6` (r4 = the drop: `addq.w #8,y_pos`).
- `Obj_WaitOffscreen` `$53FD8`: pops its return ($54F56) into `$34(a0)`,
  sets the object code ptr (`$00`) to wait-loop `loc_53FFC`; restores
  code=`$34(a0)`=`$54F56` only once on-screen. So **code=`$53FFC`** = waiting
  offscreen, **code=`$54F56`** = active/on-screen dispatch, **code=`$54FBE`**
  = the `$34` resume routine set during init (`move.b #4,routine`).
- Body child `loc_550C8`: `sub_552FC` sets **body.y_pos = parent.y_pos − 2
  (−4 if parent mapping_frame≠0)** every frame; swing via
  `MoveSprite_CircularSimple` using angle `$3C`. Arms = `loc_55218` /
  `loc_55248`.
- Swing direction `$40` and timer `$2E` are **RNG-seeded** (`sub_55434`).

**Observed divergence (prior session, free-running — phase-suspect):**
native slot-7 code=`$54FBE` y=1040; oracle code=`$54F56` y=1024 (16px lower
in native = 2 extra `addq #8,y_pos` drops in `loc_54FD6`). Because the
monkey is RNG/timer-driven and the two instances were free-running, this
snapshot may conflate a real bug with RNG/phase drift — re-confirm under the
clean (demo / aligned-input) method before fixing.

**Probe:** `SonicTheHedgehogRecomp/tools/mk_probe.py` (closed-loop drive +
full-slot dump + monkey-tile sprite-table walk; runs the drive inside one
process to avoid round-trip overshoot).

## Visual bug #2 — Giant Ring never spawns  *(deferred, not yet investigated)*

The big bonus-stage warp ring does not appear. Likely a value divergence;
possibly a clean single-instruction bug. Not yet localized.

## Visual bug #3 — AIZ water not rendering correctly (reflections)  — **FIXED 2026-06-03 (user-reported working)**

**Redefined 2026-05-31 (user).** Previously filed as "ground missing" /
below-ground transparency (see-through where terrain should be opaque). On
closer look this is **not** missing ground — it's the **AIZ water surface /
reflection effect rendering incorrectly**. What read as "background plane shows
through" is the water reflection not being drawn right.

As suspected, the AIZ water line is a **horizontal-interrupt (HInt) raster /
palette-swap effect** — the prediction was correct.

**Root cause (recompiler):** the ROM installs its H-blank handler into **RAM**
at boot and reaches it via a `JmpTo_HInt` stub (vector `$70`). The recompiler
dispatched HInt at the *static* stub address, so the RAM-installed handler was
never reached and the water raster/palette swap never ran — the lower region
rendered with the unmodified (non-reflected) palette/scroll.

**Fix (recompiler, in `segagenesisrecomp`):**
- `runner/glue.c`: `recomp_resolve_ram_trampoline()` follows `JMP`/`JSR`
  (abs.l/abs.w) trampolines installed in RAM (≤4 hops) to the real handler.
- `recompiler/src/code_generator.c`: `recomp_dispatch_once()` resolves RAM
  trampolines before the dispatch-table lookup, so RAM-redirected handlers
  (HInt) dispatch to the correct recompiled function.
- `sonic3/sonic3_spec.c` / `sonic3k/sonic3k_spec.c`: point HBlank at
  `JmpTo_HInt` (`$000F9E` / `$000D0C`) so it routes through the RAM handler.

Committed as `segagenesisrecomp@4528b2d` (dev + master). **Verify:** user
reported the AIZ water rendering correctly 2026-06-03.

## Visual bug #4 — Special Stage rings do not work  *(new 2026-05-31, user-reported; not yet investigated)*

In the Special Stage (blue-sphere bonus stage), the **rings do not work**
(do not behave/collect/render as expected). Not yet localized. Possibly related
to the Giant Ring warp (#2) — confirm whether the Special Stage is reached via a
working Giant Ring or by another path before assuming they're independent.

---

## Recompiler fixes landed this bring-up (for reference)

- Memory-operand shift/rotate `ASd/LSd/ROd/ROXd <ea>` (size field `11`) was
  decoded as a register shift → out-of-bounds `g_cpu.D[-1]/D[40]`. Added
  `mem_shift` decode + `emit_mem_shift` RMW path. (Fixed the title banner
  bounce.) `m68k_decoder.{c,h}`, `code_generator.c`.
- `function_finder.c`: register the return address when a call targets a
  routine that pops its return off the stack (the `Obj_WaitOffscreen` idiom
  `$53FD8`: `move.l (sp)+,$34(a0)` then reached via `jmp (An)`).
- `function_finder.c`: enumerate `jsr (d8,PC,Dn.W)` bra-ladder tables
  (Sonic `Animate_Raw*` command tables) — probe base, base+2, base+4.
- Result: Sonic 3 dispatch_misses 6440 → 0.
- `runner/cmd_server.c`: `pause` disabled by policy (ring-only observation).
