# Knuckles & Knuckles

Opt-in gag for the S&K-code targets (Sonic 3 & Knuckles, Sonic & Knuckles).
Tracking: central Beads `beads-tdq.3.3`.

## Use

Mods → **Knuckles & Knuckles** (group *Characters*, exclusive with any other
character mod), option **Extra Knuckles**: 8 / 16 / 24 / 34. Persisted in
`<mode>-mods.ini` beside `settings.ini` (`[knuckles-army] enabled=1 size=16`),
which tests write directly. Pick Knuckles alone: Data Select (No Save or a new
slot) on the combined cart, the title's Sonic/Knuckles switch on S&K alone.
Story zones only; special/bonus/competition stages, DDZ and demos stay stock.

## How it works (`game/common/sonic3_knuckles_army.c`)

- **Actors.** Each extra is a real `$4A`-byte object in the top of
  `Dynamic_object_RAM` whose code pointer is a plain `rts` (`locret_13FC0`), so
  `Process_Sprites` ignores it. The host steps every extra through stock
  `Obj_Knuckles` from `Obj_ResetCollisionResponseList` — Player 2's timing,
  before the collision list is cleared. A reserve of 16 free dynamic slots is
  kept: extras step aside (and glide back later) when a stage needs slots.
- **Player-1-only state** is saved around each extra: `Ctrl_1*`, `Max_speed`
  block (per-extra copy), `Distance_from_top`, the Dust and Breathing_bubbles
  objects, Super flag and `Update_HUD_timer` (so extras can never transform),
  chain bonus, collision scratch, P2 camera lag. `Knuckles_Load_PLC` and
  `Draw_Sprite` are skipped for extras; `sub_123C2` (death) never costs a life.
- **Control.** Seat `k+1` drives extra `k`: a connected controller (sealed
  `genesis_sim_pad` / `human_mask`) takes over; otherwise a port of
  `Tails_CPU_Control` — follow (delays 8-60 frames, trailing offsets up to
  ±92px, jump phases), spindash when genuinely stuck, 5 s off-screen despawn
  and the catch-up glide-in. Engine capacity today is 4 seats; more seats need
  only `GENESIS_SIM_MAX_PLAYERS` and UI.
- **Solids.** Every single-character solid routine (`btst d6,status(a0)`
  prologue) that objects call for Player 1 is hooked; the host repeats the call
  for each extra with `d6 = p2_standing_bit` and that extra's own copy of the
  P2 standing/pushing bits, then applies the mirrored Player 2 spring reaction.
  Sites are generated: `tools/gen_knuckles_army_sites.py --listing <sonic3k.lst>`
  writes `sonic3_knuckles_army_sites.inc` and the `game.toml` hook blocks
  (`--check` in CTest when `S3_SK_LISTING` is set).
- **Rendering.** Extras are drawn from ROM art (`sonic3_knuckles_art.c`:
  `ArtUnc_Knux` + DPLC + mappings) into the engine's VDP host-sprite layer,
  behind every native sprite, with native plane priority and CRAM (underwater
  palettes included); the custom-width renderer draws the same list.
- **CPU headroom.** Each extra is a full native Knuckles tick charged at 68K
  speed. The crowd adds `2 + ceil(n/4)` to the main-CPU divisor (on top of the
  renderer's). LevelLoop still waits for V-int, so unused headroom is free.

## Title gag (`sonic3_knuckles_title.c`)

S&K: the standing pose name tables are mirrored so Knuckles faces Knuckles and
the banner is recomposed from its VRAM tiles to read KNUCKLES & KNUCKLES.
S3&K: the "& KNUCKLES" subtitle echoes down the screen. Both get a parade of
running/gliding Knuckles (colours remapped to the nearest live CRAM entries).

## Validation (2026-09-27, local)

`tests/runtime/run_knuckles_army.py` (strict JSR stack, input-only):

| Case | Result |
|---|---|
| S3K + S&K × native/16:9/32:9 × 8/34 extras, seat 2 driving extra 1 | 12/12 pass; lag 0 (≤23/2519 on S&K widescreen 34); spread 0.93-1.0 |
| Mod off vs pre-mod build, same Knuckles timeline | FBHASH identical (S3K 44, S&K 51 checkpoints) |
| `--benchmark 3600` attract, all three targets | state/audio hashes identical |
| Host throughput (S&K, `--benchmark 6000`) | 0.85 ms/frame off → 1.05 ms/frame at 34 extras |
| Springs / solids (34 extras) | ~70 solid checks per frame; standing and side spring reactions fire |

Owner playtest pending; no whole-campaign claim.

## Known limits / disproved

- Extras collide with the generic solid routines and springs; objects that
  react only to P1/P2 inline (monitors break only for P1, collapsing ledges,
  grab objects' reactions) treat them as absent or as Player 2. Extras
  share P2's per-object timers where objects keep them.
- Disproved: "the level-start hang is in our step loop". It was the Z80 bus
  race below, caused by crowd lag frames.
- Engine hazard (separate bead): a lag-frame V-int's `stopZ80/startZ80` can
  clear a `Play_SFX` bus request mid-wait, deadlocking the main loop. Stock
  rarely lags; the crowd headroom removes the lag it would add.
