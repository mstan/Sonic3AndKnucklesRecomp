#!/usr/bin/env python3
"""Sonic CD levels -> scd.bin, from the player's own disc.

Sonic CD's levels are in Sonic 1's format (256 px chunks of 16 px blocks,
Sonic 1's collision tables and loop rule), so each is converted the way
build_bank.py converts the Sonic 1 zones: 128 px S3K chunks with loop blocks
split into synthetic blocks, collision in S3K's index, level art packed
under S3K's common-object tiles with the palette lines moved to S3K's
conventions (CD's line 0 is Sonic 1's Sonic palette; its line 1 has Sonic
1's common colours), common objects as S3K's own.

Sonic CD loads one level at a time into Word RAM, and so does this port:
every level is a blob assembled for one fixed window of the expansion bank,
which the game fills with the level about to start (trilogy_core.c,
tri_scd_map).

    scd_level.py --disc DIR [--out build/Release/scd.bin] [--window 0x600000]
                 [--only R11A,...]

scd.bin   "TSCD", u16 version (2), u16 levels, u32 window base, u32 window
          size, levels x { u16 zone_and_act, u16 flags, u32 offset, u32 size }
          One more entry, zone_and_act $FFFF (flags 1), is no level but Sonic
          CD's FM sound set (scd_sfx.py): its 32 KB SFX bank, then its table,
          for the bank address just past the window (window base + size,
          32 KB aligned), where the game puts it once (trilogy_core.c).
blob      +$00 the level's directory entry (TD_*, trilogy.h)
          +$40 u32 palette table, u16 count, u16 0, u32 PLC table, u16 count,
               u16 0: palette / PLC ids $F0.. are this level's own
          +$50 u32 pager (0: none)
          +$54 u32 title card letters (KosM, tiles from $54D), u32 its
               mapping table (frame 1: the zone's name)
          +$5C u8 $E2: S3K's stop command, the level's music byte while the
               disc plays its track
          +$60 u16 x 32: the extended sprite line (trilogy_extpal.c): Sonic
               CD's line 0 for its objects' line-0 art, above / below water
          +$A0 u8 x 64: Sonic CD's touch sizes ($00-$3F) as S3K's: the S3K
               size with the same box (scd_obj.c, cd_coll)
          +$F0 u32 animated level art (anim_tiles): u16 n; n x { u8 duration
               (negative: each frame its own), u8 frames, u8 tiles, u8 flags
               (bit 0: Wacky Workbench's good-future sparks, on and off; bit 1:
               act 3's, off (blank) once its boss's flag bit 7 is set; bit 2:
               the same frames on the boss's beam, only then; bit 3: Metallic
               Madness act 3's curtain, held at frame 0 while it is shut; bit 4:
               Quartz Quadrant's background, the frame its section's set),
               u16 VRAM tile, u16 0, u32 art (recoloured for the line its
               blocks use), frames (tile offsets, + durations) }
          +$F4 u32 Quartz Quadrant's sections (scenery_sections): u16 n, n x u16
               player x limits, (n + 1) x u8 set (even padded), u16 sets, sets x
               24 colours (palette 4 + set: line 1, S3K's order, line 2's 0-7),
               u32 x 4 colour cycles (Sonic CD's {u8 index, frames, colours, time}
               + colours: the belts, the lever's, even sections', odd's; 0 none),
               u16 width, height, u32 grid: set 1's background (128 px cells, the
               pager's chunk ids; 0: none)
          +$E0 u32 object art segments: { u32 tiles, u16 VRAM tile, u16 count |
               $8000 extended line, u16 sections (bit n: resident in section n),
               u16 0 } until a count of 0; u32 sections: u16 count, the camera
               X each ends at (Sonic CD swaps object art by section; so do we)
          then the level's data
assets    TD_ANIM: Sonic CD's own objects' art, as the Sonic 1 / 2 object
          asset records (trilogy_obj.c): 512 x { u32 mappings (S3K format,
          tiles placed), u16 art_tile (palette line), u16 0, u32 animations
          (or a data table), u32 0 }; ids SCDA_* (trilogy_obj.h). Their tiles
          are the window's segments (+$E0).
pager     A level with more 128 px chunks than S3K's 256 keeps them all here
          and the game pages the ones around the camera into the chunk table
          (trilogy_scdlevel.c): u16 chunks, u16 FG width, height, BG width,
          height (128 px cells), u32 chunk definitions (128 bytes each, id 0
          empty), u32 FG, u32 BG (u16 chunk ids, row by row). The layout and
          chunk table LevelLoadBlock loads are the page around the start.
Zones are $40 + round * 4 + period (rounds 1, 3-8 as 0-6; periods 0 present,
1 past, 2 good future, 3 bad future); acts 0-2. TD_MUSIC is the disc's CD-DA
track (0: the past's PCM music).
"""
import argparse
import collections
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_bank as bb  # noqa: E402
from mdcodecs import kosinski_encode, kosm_encode, nemesis_decode  # noqa: E402
import scd_mmd  # noqa: E402
import scd_sfx  # noqa: E402
from scd_mmd import BASE, Level, level_files, scan  # noqa: E402
from s1_objects import (RING_POS, WINDOWS_DRY, WINDOWS_WET, _find_run, _piece_ranges_lines, _retile_lines,  # noqa: E402
                        anim_blob)
from tilepack import Packer  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WINDOW, WINDOW_SIZE = 0x600000, 0x180000
SOUND_ENTRY = 0xFFFF                # (the level table's entry for the FM sound set)
ROUND_INDEX = {1: 0, 3: 1, 4: 2, 5: 3, 6: 4, 7: 5, 8: 6}
ROUND_NAME = {1: 'PALMTREE PANIC', 3: 'COLLISION CHAOS', 4: 'TIDAL TEMPEST', 5: 'QUARTZ QUADRANT',
              6: 'WACKY WORKBENCH', 7: 'STARDUST SPEEDWAY', 8: 'METALLIC MADNESS'}
PERIOD = {'A': 0, 'B': 1, 'C': 2, 'D': 3}
TRI_GAME_CD = 4  # 3 is the existing independent Megamix campaign.
MAX_BLOCKS = 0x400                  # (chunk words hold 10 bits; the index is S&K's interleaved one)
PAGE_MARGIN = 384                   # (px each side of the screen the pager keeps; trilogy_scdlevel.c)

# S3K monitor subtypes for CD's (0 1-up, 1 rings, 2 shield, 3 invincibility,
# 4 shoes, 5 time stop, 6 combine, 7 all): no plain shield or time stop in S3K
CD_MONITOR = {0: 1, 1: 3, 2: 7, 3: 8, 4: 4, 5: 3, 6: 8, 7: 8}
OBJ_SPRING, OBJ_CHECKPOINT, OBJ_RING, OBJ_MONITOR = 0x0A, 0x13, 0x10, 0x19
# Sonic CD's own objects, native in the game (scd_obj.c): common ids at their
# id, a round's at $20 + round index * $80 + id. A round's ids (ids from $20,
# and some below in some rounds) name different objects act by act (each act's
# own object index).
NATIVE_CD_BASE = 0x7E2000
NATIVE_COMMON = {0x12, 0x14, 0x16, 0x17, 0x1B}  # floating block, big ring, goal post, signpost, boulder
# objects the converter makes of others: layout id -> native slot (past the rounds')
OBJ_TIMEPOST, SLOT_TIMEPOST = 0x80, 0x3A0       # monitor subtypes 8 / 9: past / future time post
_WWZ = {0x04, 0x05, 0x07, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2A,   # (Wacky Workbench:
        0x2B, 0x2C, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33}                                    #  scd_wwz*.c)
_QQZ = {0x07, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31}
_CCZ = {0x1D, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x29, 0x2A, 0x2C, 0x2D, 0x2E, 0x2F, 0x35, 0x37,
        0x38, 0x39}
_TTZ = {0x20, 0x22, 0x23, 0x24, 0x27, 0x28, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x33, 0x34, 0x35, 0x36, 0x38, 0x39, 0x3D,
        0x3E, 0x51, 0x52}
_SSZ = {0x08, 0x1D, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x35, 0x38}
_PPZ = {0x07, 0x09, 0x0B, 0x0D, 0x20, 0x21, 0x22, 0x24, 0x26, 0x2D, 0x2E, 0x34, 0x35, 0x36, 0x3D, 0x3E, 0x3F}
NATIVE_ROUND = {(1, 0): _PPZ | {0x0F, 0x28, 0x2A, 0x2C, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x40},   # (round, act) -> {ids}
                (1, 1): _PPZ | {0x0F, 0x28, 0x29, 0x32, 0x33, 0x40},
                (1, 2): {0x15, 0x22, 0x24, 0x34} | set(range(0x2A, 0x33)),
                (6, 0): _WWZ | {0x08}, (6, 1): _WWZ | {0x26, 0x2D},
                (6, 2): {0x07, 0x15, 0x22, 0x27, 0x28, 0x29, 0x34, 0x3E},   # (act 3: scd_wwz_boss.c)
                (3, 0): _CCZ | {0x30, 0x31, 0x32, 0x33, 0x34},   # (Collision Chaos: scd_ccz*.c)
                (3, 1): _CCZ | {0x28},
                (3, 2): {0x15, 0x1D, 0x1E, 0x20, 0x29, 0x30, 0x31, 0x35, 0x36},   # (act 3: scd_ccz_boss.c)
                (4, 0): _TTZ | {0x26, 0x29, 0x2A, 0x30, 0x32, 0x3F},   # (Tidal Tempest: scd_ttz*.c)
                (4, 1): _TTZ | {0x05, 0x25, 0x26, 0x37, 0x42, 0x44, 0x45, 0x48, 0x49},
                (4, 2): {0x15, 0x2F, 0x34, 0x47, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F},   # (act 3: scd_ttz_boss.c)
                (5, 0): _QQZ, (5, 1): _QQZ | {0x2A},         # (Quartz Quadrant: scd_qqz*.c)
                (5, 2): {0x15, 0x20, 0x21, 0x22, 0x23, 0x28, 0x30} | set(range(0x32, 0x3A)),   # (act 3: scd_qqz_boss.c)
                (7, 0): _SSZ, (7, 1): _SSZ,                  # (Stardust Speedway: scd_ssz*.c)
                (7, 2): set(range(0x2B, 0x38)),               # (act 3: the race, scd_ssz_race.c)
                (8, 0): {0x0F, 0x24, 0x25, 0x27, 0x28, 0x2A, 0x2C, 0x2E, 0x2F, 0x32, 0x34, 0x35, 0x36, 0x38, 0x43},
                (8, 1): {0x07, 0x09, 0x0F, 0x1D, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x29, 0x2D, 0x32,
                         0x33, 0x34, 0x36, 0x3E, 0x43},   # (Metallic Madness: scd_mmz*.c)
                (8, 2): {0x09, 0x0F, 0x1D, 0x24, 0x25, 0x27, 0x29, 0x2A, 0x2B, 0x32, 0x33, 0x34, 0x36, 0x39, 0x3D,
                         0x3E}}                       # (act 3: scd_mmz_boss.c; 3F-43 spawned)
# act 3's ids whose code is acts 1-2's (those files check their own slots): acts 1-2's slots
ACT3_SHARED = {1: {0x22, 0x24, 0x34},  # PPZ's badnik, animals and spin tags retain acts 1-2's code
              8: {0x09, 0x0F, 0x24, 0x25, 0x27, 0x29, 0x32, 0x33, 0x34, 0x3E}}   # (act 2's $08 is another, unplaced)
# native slots: acts 1 and 2 at $20 + round * $80 + id; act 3 (the boss act,
# its own ids) at $400 + round * $80 + id
SLOT_ACT3 = 0x400
BLOCK_ROUNDS = {3, 5, 6, 7, 8}                  # rounds whose player code reads Sonic CD's own block numbers (header +$E8)

# Sonic CD's object art (asset ids SCDA_*, trilogy_obj.h), found in each
# level's own program: the object's code (its object index entry, to the next
# entry) refers to its mapping and animation tables (obj_refs: each address
# once, in code order) and loads its art tile (move.w #tile,oTile(a0) or
# SetObjectTileID's per-period table); the tiles are cut from the VRAM the
# level's PLCs fill in the section where the object is.
# id, name, levels ((round, act)s; None: every level placing it), object id,
# its mapping table's ref (None: data only), its animation table's ref (None:
# none; ('raw', k[, size]): instead a data table of the object's, size bytes
# or up to the next table it refers to). An art tile of 0 (the period has no
# such art): none.
PPZ1, PPZ12, PPZ3 = ((1, 0),), ((1, 0), (1, 1)), ((1, 2),)
WWZ12, WWZ2, WWZ3, WWZ = ((6, 0), (6, 1)), ((6, 1),), ((6, 2),), ((6, 0), (6, 1), (6, 2))
MMZ1, MMZ2, MMZ12, MMZ3 = ((8, 0),), ((8, 1),), ((8, 0), (8, 1)), ((8, 2),)
SSZ12, SSZ3 = ((7, 0), (7, 1)), ((7, 2),)
CCZ1, CCZ2, CCZ3 = ((3, 0),), ((3, 1),), ((3, 2),)
CCZ12 = CCZ1 + CCZ2
TTZ1, TTZ2, TTZ3 = ((4, 0),), ((4, 1),), ((4, 2),)
TTZ12, TTZ = TTZ1 + TTZ2, TTZ1 + TTZ2 + TTZ3
QQZ12, QQZ2, QQZ3 = ((5, 0), (5, 1)), ((5, 1),), ((5, 2),)
CD_ASSETS = [
    (0, 'BOOSTER', PPZ1, 0x2A, 0, None),          # PPZ1 3D ramp booster
    (1, 'PLANT', PPZ1, 0x2C, 0, None),            # PPZ1 3D ramp plants
    (2, 'MOSQUI', PPZ12, 0x3D, 0, 1),
    (3, 'MOSQUI2', PPZ12, 0x3D, 2, 3),            # (bad future: damaged)
    (4, 'PATABATA', PPZ12, 0x3E, 0, 2),
    (5, 'PATABATA2', PPZ12, 0x3E, 1, 2),
    (6, 'ANTON', PPZ12, 0x3F, 0, 1),
    (7, 'TAMABBOH', PPZ12, 0x22, 0, 1),
    (8, 'TAMABBOH2', PPZ12 + PPZ3, 0x22, 2, 3),
    (9, 'TAMAMISSILE', PPZ12, 0x22, 4, 5),
    (10, 'FLOATBLOCK', None, 0x12, 0, 1),         # (common)
    (11, 'SPIKES', PPZ12, 0x26, 0, None),
    (12, 'PLATFORM', PPZ12, 0x21, 0, None),
    (13, 'LEDGE', PPZ12, 0x20, 0, ('raw', 1)),    # collapsing ledge (+ its piece layouts)
    (14, 'LEDGE2', PPZ12, 0x20, 2, ('raw', 3)),   # (single row)
    (15, 'LEDGEBITS', PPZ12, 0x20, 5, None),      # its pieces
    (16, 'LEDGEBITS2', PPZ12, 0x20, 6, None),
    (17, 'TUNNELPATHS', PPZ12, 0x07, None, ('raw', 1, 0x200)),
    (18, 'TUNNELDOOR', PPZ12, 0x0D, 0, 1),
    (19, 'DOORSPLASH', PPZ12, 0x0B, 0, 1),
    (20, 'DOORV', PPZ12, 0x35, 0, None),
    (21, 'SONICHOLE', PPZ1, 0x31, 0, None),
    (22, 'SPINDISC', PPZ12, 0x09, 1, 0),
    (23, 'BOULDER', None, 0x1B, 0, None),         # (common)
    (24, 'LOGSHADOW', PPZ12, 0x33, 0, None),
    (25, 'WALL', PPZ12, 0x36, 0, ('raw', 1, 0x88)),   # breakable wall (+ its pieces' frames, places, speeds)
    (26, 'TIMEPOST', None, 0x19, 0, 1),           # (the monitor's: its time post frames, ASSET_FRAMES)
    (27, 'GENERATOR', PPZ12, 0x2D, 1, 2),         # robot generator (its tile: by act and period)
    (28, 'GENBOOM', PPZ12, 0x2D, None, None),     # (its explosions: SPECIAL_DATA)
    (29, 'GOALPOST', None, 0x16, 0, None),        # (common; its tile: by round, act, period)
    (30, 'ANIMALFLY', PPZ12 + PPZ3, 0x24, 0, 1),  # (its tile: by act and period)
    (31, 'ANIMALGROUND', PPZ12 + PPZ3, 0x24, 2, 3),
    (32, 'HOLOFLY', PPZ12, 0x24, 0, 1),           # (the projector's holograms of them)
    (33, 'HOLOGROUND', PPZ12, 0x24, 2, 3),
    (34, 'PROJECTOR', PPZ12, 0x2E, 0, 1),         # Metal Sonic's projector (+ hologram)
    (35, 'PROJBOOM', PPZ12, 0x2E, None, None),
    (36, 'AMY', PPZ1, 0x2F, 1, ('raw', 2, 0x3A)),  # Amy Rose (+ her animations: frame, time pairs)
    (37, 'AMYPAL', PPZ1, 0x2F, None, ('raw', 3, 0x20)),   # (her palette line, in S3K line 1's order)
    (38, 'HEART', PPZ1, 0x30, 0, None),           # (Amy's: spawned, ASSET_PLACED)
    # Egg-HVC-001 (act 3): its controller (Eggman) builds the rest
    (39, 'EGGMAN', PPZ3, 0x2A, 1, 0),
    (40, 'EGGBODY', PPZ3, 0x2B, 0, None),
    (41, 'EGGLEG1', PPZ3, 0x2C, 0, None),
    (42, 'EGGLEG2', PPZ3, 0x2D, 0, None),
    (43, 'EGGLEG3', PPZ3, 0x2E, 0, None),
    (44, 'EGGARM1', PPZ3, 0x2F, 0, None),
    (45, 'EGGARM2', PPZ3, 0x30, 0, None),
    (46, 'EGGARM3', PPZ3, 0x2A, 2, None),         # (the forearms, near and far: mappings the
    (47, 'EGGARM3B', PPZ3, 0x2A, 3, None),        #  controller gives them, their own tile: ASSET_TILE_OF)
    (48, 'EGGARM4', PPZ3, 0x32, 0, None),
    (49, 'BOSSPAL', PPZ3, 0x2A, None, None),      # (palette 5, line 1: ASSET_PALETTE)
    (50, 'GENEPAL', PPZ3, 0x2A, None, None),      # (genecolor's line 1, after: SPECIAL_DATA)
    (51, 'CAPSULE', PPZ3, 0x15, 0, 1),            # the capsule (+ its pieces)
    (52, 'FLOWER', PPZ3 + WWZ3 + CCZ3 + TTZ3 + QQZ3, 0x1F, 0, 1),    # (what its pieces become: a
    (53, 'FLOWER2', PPZ3 + WWZ3 + CCZ3 + TTZ3 + QQZ3, 0x1F, 0, 1),   #  seed, line 0, then the flower)
    (54, 'SCENERY', PPZ12, 0x32, 0, None),        # (its tile: by period and act)
    (55, 'TAGATAGA', PPZ12, 0x40, 0, 1),
    (56, 'TAGATAGA2', PPZ12, 0x40, 2, 3),         # (damaged)
    (57, 'SPRINGBOARD', PPZ12, 0x28, 0, 1),
    (58, 'SWING', PPZ12, 0x29, 0, None),
    (59, 'KOMA', PPZ12, 0x0F, 0, 1),              # spinning top (a spring rides it)
    # Wacky Workbench
    (60, 'SWITCH', WWZ, 0x24, 0, None),
    (61, 'DOOR', WWZ12, 0x25, 0, None),
    (62, 'PISTON', WWZ, 0x20, 0, None),
    (63, 'TUBEDOOR', WWZ, 0x27, 0, None),
    (17, 'TUNNELPATHS', WWZ, 0x07, None, ('raw', 1, 0x3A0)),     # (its routes: 19; act 3's the same)
    (27, 'GENERATOR', WWZ12, 0x2E, 1, 2),         # (Palmtree Panic's, at Wacky Workbench's ids)
    (28, 'GENBOOM', WWZ12, 0x2E, None, None),
    (30, 'ANIMALFLY', WWZ, 0x29, 0, 1),
    (31, 'ANIMALGROUND', WWZ, 0x29, 2, 3),
    (32, 'HOLOFLY', WWZ12, 0x29, 0, 1),
    (33, 'HOLOGROUND', WWZ12, 0x29, 2, 3),
    (34, 'PROJECTOR', WWZ12, 0x2F, 0, 1),
    (35, 'PROJBOOM', WWZ12, 0x2F, None, None),
    (64, 'WWZ_PLATFORM', WWZ, 0x22, 0, None),     # platform (one sine for all)
    (65, 'WWZ_BOUNCE', WWZ12, 0x23, 0, None),     # bouncing platform
    (66, 'WWZ_SPIN', WWZ, 0x28, 0, 1),            # spinning platform (+ its anim)
    (67, 'WWZ_SEESAW', WWZ12, 0x2C, 0, 3),        # seesaw (+ its anims; its sides: frame 9)
    (84, 'BATAPYON', WWZ12, 0x30, 0, None),       # (movea.l #MapSpr_BataPyon,a1: obj_refs)
    (85, 'BATAPYON2', WWZ12, 0x30, 1, None),      # (damaged)
    (86, 'POHBEE', WWZ12, 0x31, 1, 0),
    (87, 'POHBEE2', WWZ12, 0x31, 2, 0),           # (damaged)
    (88, 'POHBEEMISSILE', WWZ12, 0x31, 3, 4),
    (89, 'SEMI', WWZ12, 0x32, 0, 2),
    (90, 'SEMI2', WWZ12, 0x32, 1, 2),             # (damaged)
    (91, 'SEMIBOMB', WWZ12, 0x32, 3, 4),          # (its own tile: ASSET_TILE)
    (92, 'MINOMUSI', WWZ12, 0x33, 0, 2),          # (frames 0-8: its thread)
    (93, 'MINOMUSI2', WWZ12, 0x33, 1, 2),
    (72, 'LAUNCHER', WWZ12, 0x04, 0, None),
    (73, 'FREEZER', WWZ12, 0x05, 0, 1),
    (74, 'ICEBLOCK', WWZ12, 0x05, 0, None),       # (the freezer's ice: its own tile, ASSET_TILE)
    (75, 'BEAMS', WWZ12, 0x21, 0, None),
    (76, 'SNAKEBLOCK', WWZ12, 0x2A, 0, None),
    (77, 'EGGSTATUE', WWZ12, 0x2B, 0, None),
    (78, 'SPIKEBOMB', WWZ12, 0x2B, 2, 3),         # (the statue's bombs: its own tile)
    (79, 'CRUSHER', WWZ2, 0x26, 0, None),
    (80, 'SPIKEBALL', WWZ2, 0x2D, 0, None),        # (its pivot)
    (81, 'SPIKEBALL2', WWZ2, 0x2D, 1, None),       # (its balls)
    # Wacky Workbench act 3: the boss (scd_wwz_boss.c)
    (304, 'W3EGGMAN', WWZ3, 0x34, 1, 0),          # Eggman in the capsule (frames 0-10)
    (305, 'W3EGGMAN2', WWZ3, 0x34, 1, 0),         # (out of it: frames 11-16; over the fight's art)
    (306, 'W3BOSSPAL', WWZ3, 0x34, None, None),   # (palette 6, line 1)
    (307, 'W3MECA0', WWZ3, 0x35, 0, None),        # the machine (frame 0)
    (308, 'W3MECA0B', WWZ3, 0x35, 0, None),       # (wrecked: frame 1; over the fight's art)
    (309, 'W3ROTOR', WWZ3, 0x36, 0, None),
    (310, 'W3FLAME', WWZ3, 0x37, 1, 0),
    (311, 'W3SPIKE', WWZ3, 0x38, 0, None),
    (312, 'W3RUBBLE', WWZ3, 0x39, 0, 1),
    (313, 'W3RUBBLE2', WWZ3, 0x3E, 4, None),      # (its pieces)
    (314, 'W3FLOOR', WWZ3, 0x3D, 0, None),
    (315, 'W3DOOR', WWZ3, 0x3E, 0, None),
    (316, 'W3BEAMER', WWZ3, 0x3E, 1, None),
    (317, 'W3BOOM', WWZ3, 0x3F, 0, 1),
    (318, 'W3BEAM', WWZ3, 0x40, 0, None),
    (319, 'W3ENKEI', WWZ3, 0x34, None, None),     # (the background's colours toward white)
    (50, 'GENEPAL', WWZ3, 0x34, None, None),
    (51, 'W3CAPSULE', WWZ3 + TTZ3, 0x15, 0, 1),   # (the goal post's PLC)
    # Collision Chaos (scd_ccz.c, _plat.c, _badnik.c, _amy.c)
    (96, 'BUMPER', CCZ12 + CCZ3, 0x1D, 0, 1),
    (97, 'FLIPPER', CCZ12 + CCZ3, 0x1E, 0, 1),
    (98, 'BUMPERBREAK', CCZ2, 0x29, 0, 1),        # (the tile bumpers' burst: placed with the bumpers)
    (98, 'BUMPERBREAK3', CCZ3, 0x29, 0, 1),
    (99, 'BARBUMPER', CCZ12, 0x2A, 0, None),
    (100, 'POCKET', CCZ12, 0x2F, 0, None),
    (101, 'POCKETLID', CCZ12, 0x2F, 1, 2),        # (its own tile)
    (102, 'DRUM', CCZ2, 0x28, 0, None),
    (103, 'CCZSPIKES', CCZ12, 0x20, 0, None),
    (103, 'CCZSPIKES3', CCZ3, 0x20, 0, None),
    (104, 'SWINGBLOCK', CCZ12, 0x21, 0, None),
    (105, 'TRAPBLOCK', CCZ12, 0x22, 0, None),
    (106, 'CCZPLATFORM', CCZ12, 0x23, 0, None),
    (107, 'GATELEDGE', CCZ12, 0x26, 0, None),
    (108, 'TURRET', CCZ12, 0x27, 0, 1),
    (109, 'SPIKEPIVOT', CCZ12, 0x2C, 0, None),
    (110, 'SPIKEBALLS', CCZ12, 0x2C, 1, None),
    (111, 'KAMAKAMA', CCZ12, 0x24, 0, 2),         # (move.l #map,d1)
    (112, 'KAMAKAMA2', CCZ12, 0x24, 1, 2),        # (damaged)
    (113, 'SCYTHE', CCZ12, 0x25, 0, 1),           # (the Kama-Kama's: placed with it)
    (114, 'GA', CCZ12, 0x2D, 1, 2),
    (115, 'GA2', CCZ12, 0x2D, 0, 2),              # (damaged)
    (116, 'GAPOLLEN', CCZ12, 0x2D, 3, 4),         # (the ring's sparkle)
    (117, 'TENTOU', CCZ12, 0x2E, 2, 0),
    (118, 'TENTOU2', CCZ12, 0x2E, 1, 0),          # (damaged)
    (119, 'TENTOUBOMB', CCZ12, 0x2E, 5, 6),
    (120, 'AMY3', CCZ1, 0x32, 0, None),
    (121, 'AMYPAL3', CCZ1, 0x32, None, ('raw', 2, 0x20)),   # (her palette line: L1_ORDER)
    (122, 'HEART3', CCZ1, 0x33, 0, None),         # (Amy's hearts)
    (123, 'METALSONIC', CCZ1, 0x31, 0, None),     # (and his jet, $34)
    (124, 'BARRIER', CCZ1, 0x30, 0, None),
    (126, 'PROJECTOR3', CCZ12, 0x38, 0, 1),       # (Metal Sonic's projector: its own tile, $33E)
    (27, 'GENERATOR', CCZ12, 0x37, 1, 2),
    (28, 'GENBOOM', CCZ12, 0x37, None, None),
    (30, 'ANIMALFLY', CCZ12, 0x39, 0, 1),
    (31, 'ANIMALGROUND', CCZ12, 0x39, 2, 3),
    (32, 'HOLOFLY', CCZ12, 0x39, 0, 1),
    (33, 'HOLOGROUND', CCZ12, 0x39, 2, 3),
    (35, 'PROJBOOM', CCZ12, 0x38, None, None),
    # Tidal Tempest act 3: the pod chase and the boss (scd_ttz_boss.c)
    (368, 'T3POD', TTZ3, 0x4A, 1, 0),             # Eggman's pod (+ anims; 3-5: hit)
    (369, 'T3PODHEAD', TTZ3, 0x4B, 0, 1),         # (Eggman in it)
    (370, 'T3EGGMAN', TTZ3, 0x4C, 1, 0),          # the boss (+ anims)
    (371, 'T3MECA', TTZ3, 0x4D, 0, None),
    (372, 'T3SHOT', TTZ3, 0x4E, 1, 0),
    (373, 'T3BUBBLE', TTZ3, 0x4F, 0, 1),
    (49, 'BOSSPAL', TTZ3, 0x4A, None, None),      # (palette 5, line 1)
    (50, 'GENEPAL', TTZ3, 0x4C, None, None),
    # Metallic Madness act 3: the final boss (scd_mmz_boss.c)
    (464, 'M3EGG', MMZ3, 0x3F, 1, 0),             # Eggman's pod (+ its anims)
    (465, 'M3MECA', MMZ3, 0x41, 0, None),         # the shell
    (466, 'M3BLADE0', MMZ3, 0x40, 0, None),       # the blades' mapping sets 0-5 (the table of tables: ref 1)
    (467, 'M3BLADE1', MMZ3, 0x40, 2, None),
    (468, 'M3BLADE2', MMZ3, 0x40, 4, None),
    (469, 'M3BLADE3', MMZ3, 0x40, 6, None),       # (no ref names it: ASSET_SUBMAP)
    (470, 'M3BLADE4', MMZ3, 0x40, 5, None),
    (471, 'M3BLADE5', MMZ3, 0x40, 3, None),
    (472, 'M3SPARK', MMZ3, 0x42, 0, None),
    (473, 'M3AMY', MMZ3, 0x43, 0, 1),             # Amy (+ her anims)
    (474, 'M3BOSSPAL', MMZ3, 0x3F, None, None),   # (palette 7: line 1)
    (475, 'M3ENDPAL', MMZ3, 0x3F, None, None),    # (palette 11: line 1)
    (476, 'M3BOSSCYC', MMZ3, 0x3F, None, None),   # (PalCycleBoss8's colours)
    (477, 'M3FIREFLY', MMZ3, 0x39, 0, 1),         # the dark room's fireflies (+ anims)
    (478, 'M3ROOMPAL', MMZ3, 0x39, None, None),   # (the room's colours: SPECIAL_DATA)
    (479, 'M3CURTAIN', MMZ3, 0x3D, 0, None),      # the curtain's edges
    (480, 'M3PANEL', MMZ3, 0x2A, 2, 1),           # flip panel (+ its anim)
    (481, 'M3JET', MMZ3, 0x2B, 0, 1),             # jet spike column (+ its anim)
    (482, 'M3BLOCK', MMZ3, 0x36, 0, None),        # act 3's block
    (483, 'M3SPIKES', MMZ3, 0x36, 1, None),       # its spikes (frame 1: hanging)
    # (acts 1-2's and Collision Chaos' assets, act 3's own tiles)
    (96, 'BUMPER', MMZ3, 0x1D, 0, 1),
    (260, 'MMZ_KAITEN', MMZ3, 0x3E, 0, 1),
    (261, 'MMZ_ANIMALFLY', MMZ3, 0x32, 0, 1),
    (262, 'MMZ_ANIMALGROUND', MMZ3, 0x32, 2, None),
    (266, 'KABUTO', MMZ3, 0x24, 0, None),
    (267, 'KABUTOHORN', MMZ3, 0x24, 1, 2),
    (280, 'MMZ_KOMA3', MMZ3, 0x0F, 0, 3),
    (281, 'MMZ_KUZURE3', MMZ3, 0x25, 0, None),
    (283, 'MMZ_PISTON', MMZ3, 0x34, 0, None),
    (290, 'TOGEITA', MMZ3, 0x33, 0, None),
    (293, 'TOBIRA2', MMZ3, 0x29, 3, 1),
    (297, 'NOKOGIRI', MMZ3, 0x27, 1, 0),
    (298, 'NOKOBAR', MMZ3, 0x27, 2, None),
    # Stardust Speedway act 3: the Metal Sonic race (scd_ssz_race.c)
    (432, 'S7_EGGMAN', SSZ3, 0x2C, 1, 0),
    (433, 'S7_JET', SSZ3, 0x2D, 0, 1),
    (434, 'S7_BEAM', SSZ3, 0x2E, 0, 2),
    (435, 'S7_BOOM', SSZ3, 0x2E, 1, 3),           # (CD's explosion art, tile $680)
    (436, 'S7_MSNC', SSZ3, 0x2F, 1, 0),
    (437, 'S7_ELE', SSZ3, 0x30, 0, None),
    (438, 'S7_BARA', SSZ3, 0x31, 0, None),
    (439, 'S7_WALL', SSZ3, 0x2B, 0, None),
    (440, 'S7_SPIKE', SSZ3, 0x32, 0, None),
    (441, 'S7_AMY', SSZ3, 0x33, 1, ('raw', 2, 0x46)),
    (442, 'S7_HEART', SSZ3, 0x34, 0, None),
    (443, 'S7_LAMP', SSZ3, 0x36, 0, None),
    (444, 'S7_HAHEN', SSZ3, 0x37, 0, None),
    (445, 'S7_AMYPAL', SSZ3, 0x2C, None, ('raw', 3, 0x20)),
    (446, 'S7_BOSSPAL', SSZ3, 0x2C, None, None),
    # Quartz Quadrant act 3: the press (scd_qqz_boss.c)
    (400, 'Q3EGGMAN', QQZ3, 0x32, 1, 0),
    (401, 'Q3MECA1', QQZ3, 0x33, 0, None),        # the housing
    (402, 'Q3MECA2', QQZ3, 0x34, 0, None),        # the press
    (403, 'Q3MECA3', QQZ3, 0x35, 0, None),        # its drill
    (404, 'Q3PIPE', QQZ3, 0x36, 0, None),
    (405, 'Q3CATCH', QQZ3, 0x37, 1, 0),           # the claw
    (406, 'Q3BOMB', QQZ3, 0x38, 0, None),         # ($39, its shrapnel: frame 1)
    (407, 'Q3HIBANA', QQZ3, 0x20, 1, 0),          # (ref 0 looks like mappings but is the anim)
    (408, 'Q3HARI', QQZ3, 0x21, 0, None),
    (409, 'Q3BAKUHA', QQZ3, 0x23, 1, 0),
    (410, 'Q3BOSSPAL', QQZ3, 0x33, None, None),
    (50, 'GENEPAL', QQZ3, 0x33, None, None),
    (207, 'Q3LEDGE', QQZ3, 0x28, 0, None),
    (213, 'Q3PLATFORM', QQZ3, 0x30, 0, None),
    # Collision Chaos act 3: the boss's pinball table (scd_ccz_boss.c)
    (336, 'CCZ3_LID', CCZ3, 0x30, 0, 1),
    (337, 'CCZ3_BODY', CCZ3, 0x31, 0, None),
    (338, 'CCZ3_GEAR', CCZ3, 0x32, 0, 1),
    (339, 'CCZ3_EGGMAN', CCZ3, 0x33, 1, 0),
    (340, 'CCZ3_EGGMAN2', CCZ3, 0x33, 1, 0),      # (fleeing: PLC 4's art)
    (341, 'CCZ3_CANNON', CCZ3, 0x3D, 0, None),
    (342, 'CCZ3_BOMB', CCZ3, 0x3E, 0, None),
    (343, 'CCZ3_GATE', CCZ3, 0x3F, 0, None),
    (344, 'CCZ3_SPINNER', CCZ3, 0x2F, 3, 4),      # (its code lies in $2F's range)
    (345, 'CCZ3_STOPPER', CCZ3, 0x2F, 5, 6),
    (346, 'CCZ3_LIGHT', CCZ3, 0x2D, 5, None),     # (in $2D's range)
    (347, 'CCZ3_BOSSPAL', CCZ3, 0x31, None, None),
    (348, 'CCZ3_FLASHPAL', CCZ3, 0x31, None, None),
    (50, 'GENEPAL', CCZ3, 0x31, None, None),
    (51, 'CAPSULE', CCZ3 + QQZ3, 0x15, 0, 1),
    # Tidal Tempest: water, gimmicks, badniks (scd_ttz.c, _gim.c, _badnik.c)
    (128, 'TTZ_WAVE', TTZ, 0x04, 0, 1),           # (the water's surface strip: placed with the level's $16)
    (129, 'TTZ_POLE', TTZ2, 0x05, 0, None),
    (130, 'TTZ_SPIKES', TTZ12, 0x22, 0, None),
    (131, 'TTZ_SWITCH', TTZ12, 0x27, 0, None),
    (132, 'TTZ_DOOR', TTZ12, 0x28, 0, None),
    (133, 'TONBO', TTZ12, 0x2B, 0, 2),
    (134, 'TONBO2', TTZ12, 0x2B, 1, 2),           # (damaged)
    (135, 'AMENBO', TTZ12, 0x2C, 0, 1),           # (+ its shot: frames 2, 3)
    (136, 'AMENBO2', TTZ12, 0x2C, 2, 3),
    (137, 'TTZ_TAGATAGA', TTZ12, 0x2D, 0, 2),
    (138, 'TTZ_TAGATAGA2', TTZ12, 0x2D, 1, 2),
    (139, 'TTZ_TAGASPIKE', TTZ12, 0x2D, 3, None), # (its shed spikes)
    (140, 'YAGO', TTZ12, 0x2E, 0, 1),
    (141, 'YAGO2', TTZ12, 0x2E, 4, 5),
    (142, 'TTZ_ANIMAL', TTZ, 0x2F, 1, 3),         # (its tile: by act and period)
    (143, 'TTZ_ANIMAL2', TTZ12, 0x2F, 2, 3),      # (the other kind: line 1)
    (144, 'TTZ_ELEVATOR', TTZ1, 0x30, 0, None),
    (145, 'TTZ_ELEVBUTTON', TTZ1, 0x31, 0, None), # (the lift's: placed with it)
    (146, 'TTZ_CRUSHER', TTZ1, 0x32, 0, None),
    (147, 'TTZ_WALLS', TTZ12, 0x33, 0, None),
    (148, 'TTZ_SCREW', TTZ12, 0x35, 0, None),
    (149, 'TTZ_SCREW2', TTZ12, 0x35, 1, None),    # (upright)
    (150, 'TTZ_PIVOT', TTZ12, 0x36, 0, None),
    (151, 'TTZ_LINK', TTZ12, 0x36, 1, None),
    (152, 'TTZ_BALL', TTZ12, 0x36, 2, None),      # (its own tile: ASSET_TILE)
    (153, 'TTZ_BALL1', TTZ2, 0x37, 0, None),
    (154, 'TTZ_BALL4', TTZ12, 0x38, 0, None),     # (its maps by a6: ASSET_MAPSCAN)
    (155, 'TTZ_PRIZE', TTZ1, 0x3F, 0, None),
    (156, 'TTZ_NOZZLE', TTZ2, 0x42, 0, None),
    (157, 'TTZ_JET', TTZ2, 0x42, 1, 2),           # (its own tile: ASSET_TILE)
    (158, 'TTZ_CURRENTS', TTZ12, 0x20, None, None),   # (watercoli's rectangles: SPECIAL_DATA)
    (27, 'GENERATOR', TTZ12, 0x51, 1, 2),
    (28, 'GENBOOM', TTZ12, 0x51, None, None),
    (34, 'PROJECTOR', TTZ12, 0x52, 0, 1),
    (35, 'PROJBOOM', TTZ12, 0x52, None, None),
    # Tidal Tempest: blocks and platforms (scd_ttz_blk*.c)
    (160, 'TTZ_UD1', TTZ12, 0x23, 0, None),
    (161, 'TTZ_UD3', TTZ12, 0x23, 1, None),
    (162, 'TTZ_UD4', TTZ12, 0x23, 2, None),
    (163, 'TTZ_UD5', TTZ12, 0x23, 3, None),
    (164, 'TTZ_UD8', TTZ12, 0x23, 4, None),
    (165, 'TTZ_UD9', TTZ12, 0x23, 5, None),
    (166, 'TTZ_UDA', TTZ12, 0x23, 6, None),
    (167, 'TTZ_LR1', TTZ12, 0x24, 0, None),
    (168, 'TTZ_LR2', TTZ12, 0x24, 1, None),
    (169, 'TTZ_LR3', TTZ12, 0x24, 2, None),
    (170, 'TTZ_LR5', TTZ12, 0x24, 4, None),
    (171, 'TTZ_LR6', TTZ12, 0x24, 6, None),
    (172, 'TTZ_RBLK', TTZ12, 0x3D, 0, None),
    (173, 'TTZ_RENKETU', TTZ12, 0x3E, 0, None),   # (its platforms: frame 0)
    (174, 'TTZ_RENKETU2', TTZ12, 0x3E, 0, None),  # (its links: frame 1, line 0)
    (175, 'TTZ_ESCAL', TTZ12, 0x26, 0, None),
    (176, 'TTZ_BRUNKO', TTZ1, 0x29, 0, None),     # (pivot and links: frames 0, 2)
    (177, 'TTZ_BRUNKO2', TTZ1, 0x29, 0, None),    # (its seat: frame 1, line 2)
    (178, 'TTZ_FUNSUI', TTZ1, 0x2A, 0, 1),
    (179, 'TTZ_UKIDAI', TTZ2, 0x44, 0, None),
    (180, 'TTZ_BANEIWA', TTZ2, 0x48, 0, None),
    (181, 'TTZ_DAID', TTZ2, 0x49, 0, None),
    (182, 'TTZ_WALL1', TTZ, 0x34, 0, None),
    (183, 'TTZ_WALL42', TTZ2, 0x45, 0, None),
    (184, 'TTZ_WALL4', TTZ3, 0x47, 0, None),
    (185, 'TTZ_KUZURE', TTZ12, 0x39, 0, None),
    (186, 'TTZ_TOBIRAS', TTZ2, 0x25, 0, None),
    # Quartz Quadrant
    (192, 'QQZ_DENDEN', QQZ12, 0x20, 0, 3),
    (193, 'QQZ_DENDEN2', QQZ12, 0x20, 1, 3),      # (damaged)
    (195, 'QQZ_SASORI', QQZ12, 0x21, 1, 0),
    (196, 'QQZ_SASORI2', QQZ12, 0x21, 2, 0),      # (damaged)
    (197, 'QQZ_SASORITAIL', QQZ12, 0x21, 3, None),
    (198, 'QQZ_SASORISHOT', QQZ12, 0x21, 4, 5),
    (199, 'QQZ_KEMUSI', QQZ12, 0x22, 0, None),
    (200, 'QQZ_KUMO', QQZ12, 0x23, 0, 1),
    (201, 'QQZ_KUMO2', QQZ12, 0x23, 2, 3),        # (damaged)
    (202, 'QQZ_KUMOWEB', QQZ12, 0x23, 4, 5),
    (203, 'QQZ_ROCK', QQZ12, 0x24, 0, None),
    (204, 'QQZ_STALACTITE', QQZ12, 0x25, 0, None),   # (its tile: by period)
    (205, 'QQZ_SPIKES', QQZ12, 0x26, 0, None),
    (206, 'QQZ_ROCKRING', QQZ12, 0x27, 1, None),
    (207, 'QQZ_LEDGE', QQZ12, 0x28, 0, None),
    (208, 'QQZ_WAVEROCK', QQZ12, 0x29, 0, None),
    (209, 'QQZ_SWING', QQZ2, 0x2A, 0, None),      # (its links: frames 0, 1)
    (210, 'QQZ_SWINGTOP', QQZ2, 0x2A, 0, None),   # (its seat: frame 2)
    (211, 'QQZ_PILLAR', QQZ12, 0x2B, 0, None),
    (212, 'QQZ_BRIDGE', QQZ12, 0x2F, 0, None),
    (213, 'QQZ_PLATFORM', QQZ12, 0x30, 0, None),
    (214, 'QQZ_LEVER', QQZ12, 0x31, 0, None),
    (17, 'TUNNELPATHS', QQZ12, 0x07, None, ('raw', 0, 0x33A)),   # (its 22 routes)
    (27, 'GENERATOR', QQZ12, 0x2E, 1, 2),
    (28, 'GENBOOM', QQZ12, 0x2E, None, None),
    (30, 'ANIMALFLY', QQZ12, 0x2D, 0, 1),
    (31, 'ANIMALGROUND', QQZ12, 0x2D, 2, 3),
    (32, 'HOLOFLY', QQZ12, 0x2D, 0, 1),
    (33, 'HOLOGROUND', QQZ12, 0x2D, 2, 3),
    (34, 'PROJECTOR', QQZ12, 0x2C, 0, 1),
    (35, 'PROJBOOM', QQZ12, 0x2C, None, None),
    # Stardust Speedway
    (224, 'SSZ_KICKER', SSZ12, 0x08, 0, None),    # piston kicker bar
    (225, 'SSZ_CAGE', SSZ12, 0x1E, 0, None),      # rotating spring / cage
    (226, 'SSZ_SPRING_UP', SSZ12, 0x0A, 0, None), # (the cage's springs: CD's own spring art)
    (227, 'SSZ_SPRING_SIDE', SSZ12, 0x0A, 1, None),
    (228, 'SSZ_SPRING_DIAG', SSZ12, 0x0A, 2, None),
    (229, 'SSZ_SPARKS', SSZ12, 0x09, 0, 1),
    (230, 'SSZ_PANEL', SSZ12, 0x1D, 0, 1),        # layer-swap flip panel
    (231, 'SSZ_SWING', SSZ12, 0x20, 0, None),
    (232, 'KANABUN', SSZ12, 0x21, 0, 1),
    (233, 'KANABUN2', SSZ12, 0x21, 2, 3),         # (damaged)
    (234, 'KABASIRA', SSZ12, 0x22, 1, 0),
    (235, 'HOTARU', SSZ12, 0x23, 1, 2),
    (236, 'HOTARU2', SSZ12, 0x23, 3, 4),          # (damaged)
    (237, 'HOTARU_SPARK', SSZ12, 0x23, 5, 6),
    (238, 'SSZ_WALL', SSZ12, 0x24, 0, None),
    (239, 'SSZ_IRONBALL', SSZ12, 0x25, 0, None),
    (240, 'SSZ_LAUNCHER', SSZ12, 0x26, 0, None),
    (241, 'SSZ_LAUNCHBALL', SSZ12, 0x26, 1, None),
    (242, 'SSZ_DANGO', SSZ12, 0x2A, 0, 1),        # (its tile: by period)
    (243, 'SSZ_DANGO2', SSZ12, 0x2A, 2, 3),       # (damaged)
    (244, 'SEARCHLIGHT', SSZ12, 0x38, 0, None),
    (245, 'SEARCHLIGHT2', SSZ12, 0x38, 0, None),  # (past and futures: their own tile)
    (27, 'GENERATOR', SSZ12, 0x29, 1, 2),
    (28, 'GENBOOM', SSZ12, 0x29, None, None),
    (30, 'ANIMALFLY', SSZ12, 0x28, 0, 1),
    (31, 'ANIMALGROUND', SSZ12, 0x28, 2, 3),
    (32, 'HOLOFLY', SSZ12, 0x28, 0, 1),
    (33, 'HOLOGROUND', SSZ12, 0x28, 2, 3),
    (34, 'PROJECTOR', SSZ12, 0x27, 0, 1),
    (35, 'PROJBOOM', SSZ12, 0x27, None, None),
    # Metallic Madness
    (17, 'TUNNELPATHS', MMZ2, 0x07, None, ('raw', 1, 0x44E)),   # tube transport: its 61 routes (absolute)
    (27, 'GENERATOR', MMZ12, 0x43, 1, 2),
    (28, 'GENBOOM', MMZ12, 0x43, None, None),
    (256, 'MMZ_SWITCH', MMZ2, 0x21, 0, None),     # floor switch
    (257, 'MMZ_PIPE', MMZ2, 0x20, 0, 1),          # pipe-top hatch and rims (+ its anims)
    (258, 'MMZ_BEAM', MMZ2, 0x1D, 0, 2),          # shrink ray (subtype 0) (+ its anims)
    (259, 'MMZ_BEAM2', MMZ2, 0x1D, 1, 2),         # grow ray (subtype 1)
    (260, 'MMZ_KAITEN', MMZ2, 0x3E, 0, 1),        # rotating cylinder (+ its anim)
    (261, 'MMZ_ANIMALFLY', MMZ12, 0x32, 0, 1),    # (its own: no tile table, no holograms)
    (262, 'MMZ_ANIMALGROUND', MMZ12, 0x32, 2, None),
    (266, 'KABUTO', MMZ12, 0x24, 0, None),        # Kabuto (its body)
    (267, 'KABUTOHORN', MMZ12, 0x24, 1, 2),       # (its horn: a child, + its anims)
    (268, 'HACHI', MMZ1, 0x2E, 0, 2),             # Hachi (+ its mace, chain: frames 2, 3)
    (269, 'HACHI2', MMZ1, 0x2E, 1, 2),            # (damaged)
    (270, 'SCARAB', MMZ1, 0x2F, 0, 1),            # Scarab (+ its shell: frames 3-5)
    (271, 'SCARAB2', MMZ1, 0x2F, 0, 2),           # (damaged: slower legs)
    (272, 'DANGO', MMZ2, 0x2D, 0, 1),
    (273, 'DANGO2', MMZ2, 0x2D, 2, 3),            # (damaged)
    (274, 'BIGBOM', MMZ2, 0x23, 0, None),         # the giant bomb
    (275, 'BIGBOMFUSE', MMZ2, 0x23, 2, 3),        # (its fuse)
    (276, 'BIGBOMBOOM', MMZ2, 0x23, 4, 5),        # (its blast: its own tile)
    (277, 'BIGBOMFIRE', MMZ2, 0x23, 6, 7),        # (its fire)
    (280, 'MMZ_KOMA', MMZ12, 0x0F, 0, 3),         # rolling spring cart (+ its anim; a spring rides it)
    (281, 'MMZ_KUZURE', MMZ12, 0x25, 0, None),    # collapsing floor (frames 0-7: 1-8 blocks; 8, 9: pieces)
    (282, 'MMZ_DAI', MMZ1, 0x2C, 2, 1),           # materialising blocks (+ appear / vanish anims)
    (283, 'MMZ_PISTON', MMZ12, 0x34, 0, None),    # piston platforms / launcher column
    (288, 'MMZ_SPIKES', MMZ12, 0x36, 1, None),    # harir8 (frame 1: hanging)
    (289, 'MMZ_LAYERBLOCK', MMZ2, 0x36, 2, None), # tokusyu block (subtype $F8)
    (290, 'TOGEITA', MMZ2, 0x33, 0, None),        # sinking spiked boards
    (291, 'FUTAGOR', MMZ1, 0x38, 0, None),        # rotating flap bar
    (292, 'TOBIRA1', MMZ2, 0x29, 2, 1),           # sliding door, light right (subtype 1)
    (293, 'TOBIRA2', MMZ2, 0x29, 3, 1),           # (light left: subtype 2)
    (296, 'LEGMECA', MMZ2, 0x26, 0, None),        # cartwheel platform: legs (frame 0), links (1)
    (297, 'NOKOGIRI', MMZ1, 0x27, 1, 0),          # buzz saw on its arm (+ its anims)
    (298, 'NOKOBAR', MMZ1, 0x27, 2, None),        # (its mount)
    (299, 'PROPERA', MMZ1, 0x28, 1, 0),           # twin-rotor walker: body (+ its anim)
    (300, 'PROPROTOR', MMZ1, 0x28, 3, 2),         # (its rotors)
    (301, 'KONBEA', MMZ1, 0x2A, 3, ('raw', 2, 0x37C)),   # conveyor panels (+ the loop's path)
    (302, 'OKUBALL', MMZ1, 0x35, 0, None),        # background seesaw's ball
    (303, 'OKUSIESO', MMZ1, 0x35, 0, None),       # background seesaw (+ its platform)
]
_EGG = ('EGGMAN', 'EGGBODY', 'EGGLEG1', 'EGGLEG2', 'EGGLEG3', 'EGGARM1', 'EGGARM2', 'EGGARM3', 'EGGARM3B', 'EGGARM4')
ASSET_TILE_OF = {'EGGARM3': 0x31, 'EGGARM3B': 0x31, 'W3RUBBLE2': 0x39}
ASSET_TILE = {'FLOWER': 0x06D7, 'FLOWER2': 0x26D7,   # (its code sets these as it grows)
              'SEMIBOMB': 0x84C8, 'ICEBLOCK': 0x02E1, 'SPIKEBOMB': 0x04C8,
              'MMZ_SWITCH': 0x04E4, 'MMZ_ANIMALFLY': 0x0300, 'MMZ_ANIMALGROUND': 0x0300, 'BIGBOMBOOM': 0x8680,
              'MMZ_KOMA': 0x04EA, 'MMZ_KUZURE': 0x44D8, 'MMZ_DAI': 0x0398, 'MMZ_SPIKES': 0x0488,
              'MMZ_LAYERBLOCK': 0x44D8, 'TOBIRA1': 0x047E, 'TOBIRA2': 0x047E, 'LEGMECA': 0x0318,
              'KONBEA': 0x034C, 'OKUBALL': 0x43A0, 'W3EGGMAN2': 0x0300, 'W3MECA0B': 0x2393,
              'SSZ_SPRING_DIAG': 0x04F2, 'KABASIRA': 0x23C6, 'HOTARU': 0x845D, 'HOTARU2': 0x845D,
              'HOTARU_SPARK': 0x845D, 'SSZ_LAUNCHBALL': 0x0386, 'SEARCHLIGHT2': 0x44AF,
              'QQZ_SASORI': 0x23B6, 'QQZ_SASORI2': 0x23B6, 'QQZ_SASORITAIL': 0x23B6, 'QQZ_SASORISHOT': 0x23B6,
              'QQZ_KEMUSI': 0x23FE, 'QQZ_SPIKES': 0x044B, 'QQZ_ROCKRING': 0x4300, 'QQZ_LEDGE': 0x4350,
              'QQZ_WAVEROCK': 0x4487, 'QQZ_SWINGTOP': 0x431C, 'QQZ_PLATFORM': 0x4304,
              'TTZ_BALL': 0x0370, 'TTZ_JET': 0x0318, 'TTZ_RENKETU2': 0x0340, 'TTZ_BRUNKO2': 0x4340,
              'BUMPERBREAK': 0x638B, 'BUMPERBREAK3': 0x642A, 'POCKETLID': 0x43AF, 'CCZSPIKES': 0x031E,
              'CCZSPIKES3': 0x03FC, 'GAPOLLEN': 0xA7AE, 'TENTOUBOMB': 0x0495,
              'CCZ3_EGGMAN2': 0x03FC, 'CCZ3_SPINNER': 0x443E, 'CCZ3_STOPPER': 0x043E, 'CCZ3_LIGHT': 0x643E,
              'Q3LEDGE': 0x43A0, 'Q3PLATFORM': 0x4378, 'S7_BOOM': 0x0680,
              'M3CURTAIN': 0x8797, 'M3SPIKES': 0x0488, 'MMZ_KOMA3': 0x0372, 'MMZ_KUZURE3': 0x437A}
# a table of mapping tables: one of them, as another ref (ref k + its entry n)
ASSET_SUBMAP = {'M3BLADE3': (1, 3)}
# maps the code puts in a6 (move.l #maps,4(a6)), out of obj_refs' sight: a signature and the tile
ASSET_MAPSCAN = {'TTZ_BALL4': ('2D7C (L) 0004', 0x0370)}
ASSET_PALETTE = {'BOSSPAL': 5, 'W3BOSSPAL': 6, 'CCZ3_BOSSPAL': 6, 'CCZ3_FLASHPAL': 5, 'Q3BOSSPAL': 6, 'S7_BOSSPAL': 6,
                 'M3BOSSPAL': 7, 'M3ENDPAL': 11}   # (a palette id of the level's, line 1, S3K's order)
# art only in its own section, never spread (the cutscene's: Amy's PLC $B)
ASSET_OWN_SECTION = {'AMY3', 'HEART3', 'METALSONIC'}
_M3BOSS = ('M3EGG', 'M3MECA', 'M3BLADE0', 'M3BLADE1', 'M3BLADE2', 'M3BLADE3', 'M3BLADE4', 'M3BLADE5', 'M3SPARK')
ASSET_PLACED = {'HEART': 0x2F, 'FLOWER': 0x15, 'FLOWER2': 0x15, **{n: 0x2A for n in _EGG[1:]},   # (spawned by another:
                'SSZ_SPRING_UP': 0x1E, 'SSZ_SPRING_SIDE': 0x1E, 'SSZ_SPRING_DIAG': 0x1E, 'SSZ_SPARKS': 0x08,
                **{n: 0x34 for n in ('W3MECA0', 'W3MECA0B', 'W3ROTOR', 'W3FLAME', 'W3SPIKE', 'W3RUBBLE',   #  placed where that is)
                                     'W3FLOOR', 'W3BOOM', 'W3BEAM')},
                'TTZ_WAVE': 0x16, 'TTZ_ELEVBUTTON': 0x30,
                'BUMPERBREAK': 0x1D, 'BUMPERBREAK3': 0x1D, 'SCYTHE': 0x24, 'HEART3': 0x32, 'METALSONIC': 0x32,
                **{n: 0x31 for n in ('CCZ3_GEAR', 'CCZ3_EGGMAN', 'CCZ3_EGGMAN2', 'CCZ3_CANNON', 'CCZ3_BOMB',
                                     'CCZ3_GATE')},
                **{n: 0x36 for n in ('CCZ3_SPINNER', 'CCZ3_STOPPER', 'CCZ3_LIGHT')},
                'T3PODHEAD': 0x4A, 'T3MECA': 0x4C, 'T3SHOT': 0x4C, 'T3BUBBLE': 0x4C,
                **{n: 0x33 for n in ('Q3EGGMAN', 'Q3MECA2', 'Q3MECA3', 'Q3PIPE', 'Q3CATCH', 'Q3BOMB', 'Q3HIBANA',
                                     'Q3BAKUHA')},
                **{n: 0x2C for n in ('S7_JET', 'S7_BEAM', 'S7_BOOM', 'S7_MSNC', 'S7_ELE', 'S7_BARA', 'S7_HAHEN')},
                'S7_HEART': 0x33,
                **{n: 0x39 for n in _M3BOSS + ('M3AMY', 'M3BOSSPAL', 'M3ENDPAL', 'M3BOSSCYC')}}
# art a PLC of the level's own brings in (by period): the past's animals (list
# 4) and the projector's holograms of them (list 5) share their tiles
ASSET_PLC = {'ANIMALFLY': {'B': 4}, 'ANIMALGROUND': {'B': 4}, 'HOLOFLY': {'B': 5}, 'HOLOGROUND': {'B': 5},
             **{n: {'C': 4, 'D': 4} for n in _EGG + ('CAPSULE',)},   # (act 3: the boss's PLC)
             **{n: {'C': 19, 'D': 19} for n in ('W3EGGMAN', 'W3MECA0', 'W3ROTOR', 'W3FLAME', 'W3SPIKE', 'W3RUBBLE',
                                                'W3RUBBLE2', 'W3FLOOR', 'W3BOOM')},
             'W3EGGMAN2': {'C': 20, 'D': 20}, 'W3MECA0B': {'C': 20, 'D': 20}, 'W3CAPSULE': {'C': 18, 'D': 18},
             **{n: {'A': 11} for n in ('AMY3', 'HEART3', 'METALSONIC')},   # (Collision Chaos 1: Amy's PLC)
             'CCZ3_EGGMAN2': {'C': 4, 'D': 4},
             **{n: {'C': 4, 'D': 4} for n in ('T3EGGMAN', 'T3MECA', 'T3SHOT', 'T3BUBBLE', 'S7_AMY', 'S7_HEART')},
             **{n: {'C': 4, 'D': 4} for n in _M3BOSS}, 'M3AMY': {'C': 5, 'D': 5}}
# art shown only after the art it replaces is done with: put on that art's tiles (the record's own
# segments, which the object uploads itself: o_upload_asset), not given VRAM of its own
ASSET_OVERLAY = {n: ('W3EGGMAN', 'W3MECA0', 'W3ROTOR', 'W3FLAME') for n in ('W3EGGMAN2', 'W3MECA0B')}
_M3LEFT = ('M3PANEL', 'M3FIREFLY', 'M3JET', 'M3BLOCK', 'M3SPIKES', 'BUMPER', 'MMZ_KAITEN', 'MMZ_ANIMALFLY',
           'MMZ_ANIMALGROUND', 'KABUTO', 'KABUTOHORN', 'MMZ_KOMA3', 'MMZ_KUZURE3', 'MMZ_PISTON', 'TOGEITA', 'TOBIRA2',
           'NOKOGIRI', 'NOKOBAR')
ASSET_OVERLAY.update({n: _M3LEFT + ('SPARE',) for n in _M3BOSS})   # (Metallic Madness act 3: the arena's, over the
ASSET_OVERLAY['M3AMY'] = ('M3BLADE3', 'M3BLADE4', 'M3BLADE5')     #  level left of it and what that leaves; Amy's over
                                                                   #  the blade sets the last blade doesn't use)
ASSET_NEEDS = {'HOLOFLY': {1: 0x2E, 3: 0x38, 5: 0x2C, 6: 0x2F, 7: 0x27},
               'HOLOGROUND': {1: 0x2E, 3: 0x38, 5: 0x2C, 6: 0x2F, 7: 0x27}}
# a round's own PLC lists for an asset (Quartz Quadrant's past: animals 7, holograms 8)
ASSET_PLC_ROUND = {('ANIMALFLY', 5): {'B': 7}, ('ANIMALGROUND', 5): {'B': 7},
                   ('HOLOFLY', 5): {'B': 8}, ('HOLOGROUND', 5): {'B': 8}}   # (only where the projector is, by round)
# variants for a subtype: only where an object of it is placed (damaged: any but 0)
ASSET_SUBTYPE = {'MOSQUI': lambda s: s == 0, 'MOSQUI2': lambda s: s != 0, 'PATABATA': lambda s: s == 0,
                 'PATABATA2': lambda s: s != 0, 'TAGATAGA': lambda s: s == 0, 'TAGATAGA2': lambda s: s != 0, 'TAMABBOH': lambda s: s != 1 and s == 0,
                 'TAMABBOH2': lambda s: s not in (0, 1), 'TAMAMISSILE': lambda s: s == 0,
                 'LEDGE': lambda s: not s & 0x80, 'LEDGEBITS': lambda s: not s & 0x80,
                 'LEDGE2': lambda s: bool(s & 0x80), 'LEDGEBITS2': lambda s: bool(s & 0x80),
                 **{n: lambda s: s == 0 for n in ('BATAPYON', 'POHBEE', 'POHBEEMISSILE', 'SEMI', 'SEMIBOMB', 'MINOMUSI')},
                 **{n: lambda s: s != 0 for n in ('BATAPYON2', 'POHBEE2', 'SEMI2', 'MINOMUSI2')},
                 'MMZ_BEAM': lambda s: s == 0, 'MMZ_BEAM2': lambda s: s != 0,
                 'MMZ_ANIMALFLY': lambda s: not s & 0x7F, 'MMZ_ANIMALGROUND': lambda s: bool(s & 0x7F),
                 'HACHI': lambda s: s == 0, 'HACHI2': lambda s: s != 0, 'DANGO': lambda s: s == 0,
                 'DANGO2': lambda s: s != 0, 'SCARAB': lambda s: not s & 1, 'SCARAB2': lambda s: bool(s & 1),
                 'MMZ_SPIKES': lambda s: s >= 0x80 and s != 0xF8, 'MMZ_LAYERBLOCK': lambda s: s == 0xF8,
                 'TOBIRA1': lambda s: s == 1, 'TOBIRA2': lambda s: s == 2,
                 'KANABUN': lambda s: s == 0, 'KANABUN2': lambda s: s != 0, 'HOTARU': lambda s: not s & 1,
                 'HOTARU2': lambda s: bool(s & 1), 'HOTARU_SPARK': lambda s: not s & 1,
                 'SSZ_DANGO': lambda s: s == 0, 'SSZ_DANGO2': lambda s: s != 0,
                 **{n: lambda s: s == 0 for n in ('QQZ_DENDEN', 'QQZ_SASORI', 'QQZ_SASORISHOT', 'QQZ_KUMO', 'QQZ_KUMOWEB')},
                 **{n: lambda s: s != 0 for n in ('QQZ_DENDEN2', 'QQZ_SASORI2', 'QQZ_KUMO2')},
                 **{n: lambda s: s == 0 for n in ('TONBO', 'AMENBO', 'TTZ_TAGATAGA', 'TTZ_TAGASPIKE', 'YAGO')},
                 **{n: lambda s: s != 0 for n in ('TONBO2', 'AMENBO2', 'TTZ_TAGATAGA2', 'YAGO2')},
                 'TTZ_SCREW': lambda s: not s & 2, 'TTZ_SCREW2': lambda s: bool(s & 2),
                 'TTZ_UD1': lambda s: s in (0, 1), 'TTZ_UD3': lambda s: s == 3, 'TTZ_UD4': lambda s: s == 4,
                 'TTZ_UD5': lambda s: s in (5, 7), 'TTZ_UD8': lambda s: s == 8, 'TTZ_UD9': lambda s: s in (6, 9, 11),
                 'TTZ_UDA': lambda s: s == 10, 'TTZ_LR1': lambda s: s in (0, 1, 4, 7, 8), 'TTZ_LR2': lambda s: s == 2,
                 'TTZ_LR3': lambda s: s == 3, 'TTZ_LR5': lambda s: s == 5, 'TTZ_LR6': lambda s: s == 6,
                 'KAMAKAMA': lambda s: s < 0x80, 'KAMAKAMA2': lambda s: s >= 0x80, 'SCYTHE': lambda s: s < 0x80,
                 'GA': lambda s: s == 0, 'GA2': lambda s: s >= 0x80, 'GAPOLLEN': lambda s: s == 0,
                 'TENTOU': lambda s: s == 0, 'TENTOU2': lambda s: s >= 0x80, 'TENTOUBOMB': lambda s: s == 0,
                 'M3BLOCK': lambda s: s < 0x80, 'M3SPIKES': lambda s: s >= 0x80 or s == 1}
# an asset only in these periods (the others have none of its art)
ASSET_PERIODS = {'SEARCHLIGHT2': 'BCD'}
# mapping tables that read wrong as tables: just the first n frames
MAP_FRAMES = {'SSZ_SPRING_UP': 3, 'SSZ_SPRING_SIDE': 3, 'M3BLADE3': 16}
# data no ref names: the generator's explosions (move.l #ExplosionLocs,oVar2C(a0))
SPECIAL_DATA = {'GENBOOM': ('217C (L) 002C', 0x40), 'PROJBOOM': ('217C (L) 002C', 0x28),
                'GENEPAL': ('3C3C 0007 43F9 (L) 45F8 FB20', 0x20),   # (genecolor: anywhere in the program)
                'TTZ_CURRENTS': ('7000 1039 00FF 1507 0C00 0002 6400 ???? 45F9 (L)', 0x600),   # (watercoli's)
                'M3ROOMPAL': ('43F9 (L) 43F1 0000 45F8 FB60', 0xD8)}   # (MMZ act 3's dark room colours)
L1_ORDER = ('AMYPAL', 'BOSSPAL', 'GENEPAL', 'AMYPAL3', 'S7_AMYPAL')     # (line 1 palettes: into S3K line 1's order)
ASSET_FRAMES = {'TIMEPOST': set(range(0x0A, 0x0E)) | {0x12, 0x13},   # (only these frames' art: posts, HUD P / F)
                'GOALPOST': {5},                                     # (the post; the sign is S3K's)
                'AMY': set(range(17)) - {8, 9, 10}, 'HEART': {8, 9, 10},   # (her frames; the hearts')
                'FLOWER': {0, 1, 2, 3}, 'FLOWER2': {4, 5, 6, 7},
                'FREEZER': set(range(8)), 'ICEBLOCK': {8, 9, 10, 11},
                'OKUSIESO': {0, 1, 2, 3, 4, 8}, 'OKUBALL': {5, 6, 7},
                'W3EGGMAN': set(range(11)), 'W3EGGMAN2': set(range(11, 17)), 'W3MECA0': {0}, 'W3MECA0B': {1},
                'QQZ_SWING': {0, 1}, 'QQZ_SWINGTOP': {2},
                'TTZ_RENKETU': {0}, 'TTZ_RENKETU2': {1}, 'TTZ_BRUNKO': {0, 2}, 'TTZ_BRUNKO2': {1},
                'AMY3': set(range(8)), 'HEART3': {10, 11, 12}, 'GAPOLLEN': {4, 5, 6, 7},
                'METALSONIC': {0, 1, 12, 13, 14, 15}, 'CCZ3_EGGMAN': set(range(7)), 'CCZ3_EGGMAN2': {7, 8, 9},
                'S7_MSNC': set(range(24)) - {3, 15, 16, 17}, 'S7_BOOM': set(range(5)),
                'S7_AMY': set(range(8)) | {14, 17, 18, 19}, 'S7_HEART': {8, 9, 10}}
ASSET_COUNT, ASSET_REC = 512, 16          # (Sonic CD ids: per round ranges; the game indexes the table, no bound)


def l1_order(data):
    """16 colours of a Sonic CD line 1 in S3K line 1's order (bb.L1_PERM)"""
    return bytes(b for i in range(16) for b in data[bb.L1_PERM.index(i) * 2:bb.L1_PERM.index(i) * 2 + 2])


def native_slot(lv, oid):
    if oid in NATIVE_COMMON:
        return oid
    if oid in NATIVE_ROUND.get((lv.round, lv.act), ()):
        shared = lv.act == 2 and oid in ACT3_SHARED.get(lv.round, ())
        return (SLOT_ACT3 if lv.act == 2 and not shared else 0x20) + ROUND_INDEX[lv.round] * 0x80 + oid
    return None


def be16(b, a): return (b[a] << 8) | b[a + 1]
def be32(b, a): return (be16(b, a) << 16) | be16(b, a + 2)


class Window:
    """One level's blob, assembled for the window at `base`."""
    def __init__(self, base):
        self.base = base
        self.data = bytearray(0x100)
        self.data[0x5C] = 0xE2
        self.pals, self.plcs, self.dedupe = [], [], {}

    def add(self, blob, align=2, dedupe=True):
        blob = bytes(blob)
        if dedupe and blob in self.dedupe:
            return self.dedupe[blob]
        while len(self.data) % align:
            self.data.append(0)
        a = self.base + len(self.data)
        self.data += blob
        if len(self.data) & 1:
            self.data.append(0)
        if dedupe:
            self.dedupe[blob] = a
        return a

    def add_plc(self, entries):
        """entries: [(Nemesis art address, VRAM byte address)] -> PLC id $F0.."""
        blob = struct.pack('>H', len(entries) - 1) + b''.join(struct.pack('>IH', a, v) for a, v in entries)
        self.plcs.append(self.add(blob))
        return 0xF0 + len(self.plcs) - 1

    def add_palette(self, colours, dest):
        self.pals.append((self.add(colours), dest, len(colours) // 4 - 1))
        return 0xF0 + len(self.pals) - 1

    def finish(self):
        pt = b''.join(struct.pack('>IHH', s, d, c) for s, d, c in self.pals)
        lt = b''.join(struct.pack('>I', a) for a in self.plcs)
        pa = self.add(pt, dedupe=False) if pt else 0
        la = self.add(lt, dedupe=False) if lt else 0
        struct.pack_into('>IHHIHH', self.data, 0x40, pa, len(self.pals), 0, la, len(self.plcs), 0)
        return bytes(self.data)


S3K_TOUCH_SIZES = 0x00FF62                     # (Touch_Sizes, S&K)
S3K_DIAG_SPRING = 0x190B4C                     # (ArtNem_DiagonalSpring)


def touch_map(lv, s3k):
    """Sonic CD's touch sizes as S3K's: per CD size, the S3K size with the same box"""
    hit = scan(lv.data, '0240 003F D040 45F9 (L) 45F2 00FE')       # Player_ObjCollide: lea ObjColSizes,a2
    if not hit:
        raise SystemExit(f'{lv.name}: no ObjColSizes')
    cd = hit[0][1][0] - 2                                              # (size n at -2 + 2n)
    ours = [(s3k[S3K_TOUCH_SIZES + i * 2], s3k[S3K_TOUCH_SIZES + i * 2 + 1]) for i in range(0x40)]
    out = bytearray(0x40)
    for n in range(1, 0x40):
        w, h = lv.b(cd + n * 2), lv.b(cd + n * 2 + 1)
        out[n] = min(range(1, 0x40), key=lambda i: (abs(ours[i][0] - w) + abs(ours[i][1] - h), i != n))
    return bytes(out)


def code_called(lv, target):
    """target can run: it does not follow an rts, or some branch / call (Bcc, bra, bsr,
    jsr / jmp, pc-relative or absolute) in the program goes to it"""
    d = lv.data
    o0 = target - BASE
    if o0 < 2 or d[o0 - 2:o0] != b'\x4E\x75':
        return True                                         # (reached by falling in)
    for o in range(0, len(d) - 6, 2):
        op = be16(d, o)
        if op & 0xF000 == 0x6000:
            disp = scd_mmd.s8(op & 0xFF) if op & 0xFF else scd_mmd.s16(be16(d, o + 2))
            if BASE + o + 2 + disp == target:
                return True
        if op in (0x4EBA, 0x4EFA) and BASE + o + 2 + scd_mmd.s16(be16(d, o + 2)) == target:
            return True
        if op in (0x4EB9, 0x4EF9) and be32(d, o + 2) == target:
            return True
    return False


def palette_cycles(lv):
    """the level's PaletteCycle scripts (Level/Palette Cycle Script.asm's
    CycleColor): [(set, script, colours)], palette indices as S3K keeps them
    (line 1 in its common-colour order; line 0 is S3K's own: dropped)"""
    out = []
    for k, (o, (timers, steps), _) in enumerate(scan(lv.data, '4BF8 (W) 49F8 (W)')):
        if not code_called(lv, BASE + o):
            continue                                        # (dead: Stardust Speedway's past and good future call
        pos = o + 8                                         #  the rts before it: their cycles never run)
        while lv.data[pos:pos + 2] == b'\x43\xF9' and lv.data[pos + 6:pos + 8] == b'\x45\xF9':
            sa, ca = be32(lv.data, pos + 2), be32(lv.data, pos + 8)
            pos += 12
            so, co = lv.off(sa), lv.off(ca)
            idx, n = lv.data[so], lv.data[so + 1]
            script = bytes(lv.data[so:so + 2 + n * 2])
            ncol = max(script[3::2]) + 1 if n else 0
            cols = bytes(lv.data[co:co + ncol * 2])
            if 16 <= idx < 32:
                script = bytes([16 + bb.L1_PERM[idx - 16]]) + script[1:]
            if idx >= 16 and n and len(cols) == ncol * 2:
                out.append((k, script, cols))
            op = lv.data[pos]
            if op == 0x61:                                  # bsr CycleColor (.w: $6100 disp)
                pos += 4 if lv.data[pos + 1] == 0 else 2
            elif lv.data[pos:pos + 8] == bytes.fromhex('0C39000100FF1507') and lv.data[pos + 8] == 0x67:
                d = lv.data[pos + 9]                        # (cmpi.b #1,(act).l; beq.s: act 2's own set)
                pos = pos + 10 + (d - 256 if d & 0x80 else d) if lv.act == 1 else pos + 10
            else:
                break                                       # (the routine itself follows: the last one)
    return out


# UpdateAnimTiles (Level/<zone>/Animated Tiles Update): per art, lea AniArt,a1;
# move.w #longs-1,d6; bsr AnimateTilesSimple / Script; bne.s; LVLDMA to VRAM
ANIM_SIG = '43F9 (L) 3C3C (W) 6100 (W) 66?? 4BF9 00C0 0004 2ABC (L) 2ABC (L) 3ABC (W) 3ABC (W) 31FC (W) F640'
ANIM_GATE = '5338 (W) 6B02 6012 0A38 0001 (W)'    # (WWZ good future: the sparks on 30 frames, off 90)
ANIM_SHUT = '4A39 00FF 1959 6704 4A14 67'        # (MMZ act 3's curtain: held at its first frame while shut)
# Collision Chaos act 2's drums: their own routine (not AnimateTiles): every 11 frames ($F66C from 10) the
# next of 2 pictures (a pc-relative table of (a1, a2) pairs), each pointer's art DMAed to its own place
ANIM_DRUM = '5338 (W) 6A00 ???? 11FC 00 (B) ???? 7000 1038 ???? 5200 0C00 00 (B) 6502 7000 11C0 ???? E748 227B 00 (B)'
ANIM_DRUM_LEN = '2ABC 9400 93 (B)'                 # (its DMAs: words)
ANIM_DRUM_DEST = '3ABC (W) 31FC (W) F640'          # (and where)
# Wacky Workbench act 3's sparks: bsr; bne.w; then by bossFlags ($F7A7) bit 7 to the beam's tiles, else the
# level's (bit 6 asks: the level's blanked once)
ANIM_BOSS = '43F9 (L) 3C3C (W) 6100 (W) 6600 (W) 08B8 0006 F7A7'
ANIM_DEST = '3ABC (W) 31FC (W) F640'


def scenery_sections(lv):
    """Quartz Quadrant acts 1-2: the act's sections by player 1's x ($FF156C), each
    with its background set (PLC $E + set over the level's background art, palette
    4 + set) and the colour cycles the zone's cycler runs (the belts': the lever's
    flag picks the other table; a second by the section's parity) -> (thresholds,
    sets per section, {set: (art tile, tiles, [art by set])}, belt, belt reversed,
    second (even sections), second (odd)) or None"""
    if lv.act > 1:
        return None
    th = scan(lv.data, '1239 00FF 1507 3601 D241 D243 D241 D041 323B 00 (B)')   # (a row by act)
    if th:
        o, (d,), n = th[0]
        a = BASE + o + n - 2 + d + lv.act * 6
    else:
        th = scan(lv.data, '41F9 (L) 3218 6B?? B278 D008')
        if not th:
            return None
        a = th[0][1][0]
    lims = []
    while lv.w(a) < 0x8000:
        lims.append(lv.w(a))
        a += 2
    w = scan(lv.data, '13C2 00FF 156C')
    st = scan(lv.data, '123B 20 (B)', w[0][0], w[0][0] + 0x20) if w else []
    if not lims or not st:
        return None
    o, (d,), n = st[0]
    sets = [lv.b(BASE + o + 2 + d + k) >> 2 for k in range(len(lims) + 1)]
    spans = [(dst, dst + len(lv.nemesis(src)[0])) for k in set(sets) for src, dst in lv.plcs[0xE + k]]
    lo, hi = min(a for a, _ in spans), max(b for _, b in spans)
    arts = [bytes(lv.vram([lv.plc_level, 0xE + k])[lo:hi]) for k in range(max(sets) + 1)]
    r = scan(lv.data, '49F8 F7B7 4BF8 F7B0')                 # (the cycler: its timers, frames)
    if not r:
        return None
    r0 = r[0][0]
    bp = scan(lv.data, '43FA (W) 4A39 00FF 158F 67?? 43FA (W)', r0, r0 + 0x60)
    if not bp:
        return None
    o, (d1, d2), n = bp[0]
    belt = BASE + o + 2 + scd_mmd.s16(d1)
    belt_rev = BASE + o + 14 + scd_mmd.s16(d2)
    even = odd = None
    bt = scan(lv.data, '0839 0000 00FF 156C (B) (B)', r0, r0 + 0x40)
    if bt:
        o, (op, disp), n = bt[0]
        fall, to = o + n, o + n + scd_mmd.s8(disp)
        lea = lambda at: lv.l(BASE + at + 2) if be16(lv.data, at) == 0x43F9 else None
        if op == 0x67:                                         # (beq: the even sections' at the target)
            even, odd = lea(to), lea(fall)
        else:
            odd, even = lea(to), lea(fall)
    return lims, sets, (lo // 32, (hi - lo) // 32, arts), belt, belt_rev, even, odd


def anim_tiles(lv):
    """the level's animated art: [(gate, duration (-1: per frame), [(art index, duration)],
    first CD tile, tiles, [frame art], the other place's first CD tile or None)]; gate 1: the good
    future's sparks, 2: Wacky Workbench act 3's (their other place: the boss's beam, an object's art),
    8: Metallic Madness act 3's curtain (held at its first frame while it is shut)"""
    hits = scan(lv.data, '45F8 (W) 49F8 (W)')                 # (lea aniArtTimers.w,a2; lea aniArtFrames.w,a4)
    out = []
    for o, (_, dur, nf, disp), n in scan(lv.data, ANIM_DRUM):   # (Collision Chaos act 2's drums: their own)
        tab = BASE + o + 0x26 + disp
        lens = [w for _, (w,), _ in scan(lv.data, ANIM_DRUM_LEN, o, o + 0x90)]
        dests = [((hi & 0x3FFF) | ((lo & 3) << 14)) // 32 for _, (hi, lo), _ in scan(lv.data, ANIM_DRUM_DEST, o, o + 0x90)]
        for i, (words, dest) in enumerate(zip(lens[:2], dests[:2])):
            nt = words * 2 // 32
            arts = [lv.bytes_at(lv.l(tab + k * 8 + i * 4), nt * 32) for k in range(nf)]
            out.append((0, dur, [(k, dur) for k in range(nf)], dest, nt, arts, None))
    sc = scenery_sections(lv)
    if sc and len(sc[2][2]) > 1:                              # (Quartz Quadrant's background by section)
        t0, nt, arts = sc[2]
        out.append((16, 0, [(k, 0) for k in range(len(arts))], t0, nt, arts, None))
    if not hits:
        return out
    o0 = hits[0][0]
    prev = o0
    for o, (tab, d6, _, _), n in scan(lv.data, ANIM_BOSS, o0, o0 + 0x200):   # (WWZ3: two places, by the boss)
        dests = [((hi & 0x3FFF) | ((lo & 3) << 14)) // 32 for _, (hi, lo), _ in scan(lv.data, ANIM_DEST, o, o + 0xC0)][:2]
        if len(dests) < 2:
            continue
        nt = (d6 + 1) * 4 // 32
        dur, nf = lv.b(tab), lv.b(tab + 1)
        arts = [lv.bytes_at(lv.l(tab + 2 + k * 4), nt * 32) for k in range(nf)]
        out.append((2, dur, [(k, dur) for k in range(nf)], dests[1], nt, arts, dests[0]))
        prev = o + n
    for o, (tab, d6, br, _, _, _, hi, lo), n in scan(lv.data, ANIM_SIG, o0, o0 + 0x200):
        gated = 1 if scan(lv.data, ANIM_GATE, prev, o) else 8 if scan(lv.data, ANIM_SHUT, prev, o) else 0
        prev = o + n
        target = o + 12 + scd_mmd.s16(br)
        simple = lv.data[target + 2:target + 4] == b'\x6A\x00'   # (Simple: bpl.w; Script: bpl.s)
        dest = ((hi & 0x3FFF) | ((lo & 3) << 14)) // 32
        nt = (d6 + 1) * 4 // 32
        if simple:
            dur, nf = lv.b(tab), lv.b(tab + 1)
            ptrs = [lv.l(tab + 2 + k * 4) for k in range(nf)]
            frames = [(k, dur) for k in range(nf)]
        else:
            ns = lv.b(tab)
            frames = [(lv.b(tab + 3 + k * 2), lv.b(tab + 2 + k * 2)) for k in range(ns)]
            ptrs = [lv.l(tab + 2 + ns * 2 + k * 4) for k in range(max(i for i, _ in frames) + 1)]
            dur = -1
        out.append((gated, dur, frames, dest, nt, [lv.bytes_at(a, nt * 32) for a in ptrs], None))
    return out


def music_track(lv):
    """the disc's CD-DA track for the level (0: the past, PCM music)"""
    if lv.time == 'B':
        return 0
    return 3 + ROUND_INDEX[lv.round] * 3 + {'A': 0, 'C': 1, 'D': 2}[lv.time]


def convert_objects(lv):
    """(object layout, ring layout, pointer table): common objects as S3K's own"""
    objs, rings = [], []
    for o in lv.visible_objects():
        x, y, oid, sub = o['x'], o['y'], o['id'], o['subtype']
        flips = (1 if o['xflip'] else 0) | (2 if o['yflip'] else 0)
        if oid == OBJ_RING:                                   # formation -> ring layout
            n = sub & 7
            n = 6 if n == 7 else n
            dx, dy = RING_POS[sub >> 4]
            rings += [(x + dx * i, y + dy * i) for i in range(n + 1)]
            continue
        if oid == OBJ_MONITOR:
            if sub in (8, 9):                                 # (time posts: Sonic CD's own)
                oid = OBJ_TIMEPOST
            elif sub > 7:
                continue
            else:
                sub = CD_MONITOR[sub]
        elif oid == OBJ_SPRING:                               # CD: bit 2 sideways, bit 3 diagonal
            d = 0x10 if sub & 4 else (0x40 if o['yflip'] else 0x30) if sub & 8 else (0x20 if o['yflip'] else 0)
            if lv.round == 4 and not d and lv.water and y > lv.water['height']:
                sub &= ~2                                     # (Tidal Tempest: under the water a red spring; S3K's
                #  water keeps less of a yellow one's push than Sonic CD's, which takes him out of it)
            sub = d | (sub & 2)
        elif oid not in (OBJ_CHECKPOINT, OBJ_TIMEPOST) and native_slot(lv, oid) is None:
            continue
        elif (lv.round, lv.act, oid) == (1, 0, 0x2A) and o['subtype2']:  # (the booster's other part: bit 7)
            sub |= 0x80
        elif lv.round in (1, 6) and oid == 0x07 and o['subtype2']:     # (tunnel path: the player hidden, bit 6)
            sub |= 0x40
        elif (lv.round, lv.act, oid) == (8, 0, 0x2F) and sub & 0x80:   # (scarab: its monitor's kind)
            s2 = o['subtype2']
            sub = (sub & 0x91) | ((0x40 | s2 << 1) if s2 <= 7 else 0)
        elif (lv.round, lv.act, oid) == (8, 1, 0x23):                  # (bigbom: its layer)
            sub = o['subtype2'] & 3
        elif lv.round == 7 and oid == 0x38:                            # (SSZ's searchlights: subtype 2, high nibble)
            sub |= (o['subtype2'] & 0xF) << 4
        elif (lv.round, lv.act, oid) == (6, 2, 0x3E):                  # (WWZ3's emitters: their floor, bits 4-6)
            sub |= (o['subtype2'] & 7) << 4
        elif (lv.round, lv.act, oid) == (7, 2, 0x2B) and o['subtype2']:  # (SSZ3's start gate, barrier: bit 3, its
            assert o['subtype2'] & 0xF == 2 and sub < 8                 #  flags in 4-7; the 2 moves implied)
            sub = sub | 8 | (o['subtype2'] & 0xF0)
        objs.append((x, y | (flips << 13) | 0x8000 | (0x1000 if o["saved"] else 0), oid, sub))   # (bit 12: remembered)
    objs.sort(key=lambda o: o[0])
    rings.sort()
    ob = b''.join(struct.pack('>HHBB', x, y, i, s) for x, y, i, s in objs) + b'\xFF\xFF\0\0\0\0'
    rb = b'\0\0\0\0' + b''.join(struct.pack('>HH', x & 0xFFFF, y & 0xFFFF) for x, y in rings) + b'\xFF\xFF\0\0'
    ptrs = bytearray(256 * 4)
    for oid, name in ((OBJ_MONITOR, 'Obj_Monitor'), (OBJ_SPRING, 'Obj_Spring'), (OBJ_CHECKPOINT, 'Obj_StarPost')):
        struct.pack_into('>I', ptrs, oid * 4, bb.S3K_ADDR[name])
    for oid in range(0x80):
        slot = native_slot(lv, oid)
        if slot is not None:
            struct.pack_into('>I', ptrs, oid * 4, NATIVE_CD_BASE + slot * 4)
    struct.pack_into('>I', ptrs, OBJ_TIMEPOST * 4, NATIVE_CD_BASE + SLOT_TIMEPOST * 4)
    return b'\xFF\xFF\0\0\0\0' + ob, rb, bytes(ptrs)


def obj_refs(lv, oid):
    """(addresses, art tile) an object's code refers to (each once, in code order)"""
    code = sorted(set(lv.object_index))
    start = lv.object_index[oid - 1]
    end = min([c for c in code if c > start] + [BASE + len(lv.data)])
    sot = scan(lv.data, '43F9 (L) D040 383B 00?? 45FB 40??')     # SetObjectTileID: lea ObjectTileIDs,a1
    sot_at, ids = (sot[0][0] + BASE, sot[0][1][0]) if sot else (None, None)
    refs, tile, d = [], None, lv.data
    inside = lambda v: BASE + 0x100 <= v < BASE + len(d) and not v & 1
    for i in range(start - BASE, end - BASE - 1, 2):
        op = be16(d, i)
        v = None
        if op in (0x217C, 0x237C) and be16(d, i + 6) == 4:            # move.l #x,oMap(a0/a1)
            v = lv.l(BASE + i + 2)
        elif op in (0x203C, 0x223C) and inside(lv.l(BASE + i + 2)):    # move.l #x,d0 / d1
            v = lv.l(BASE + i + 2)
        elif op & 0xF1FF == 0x41FA:                                    # lea x(pc),an
            v = BASE + i + 2 + scd_mmd.s16(be16(d, i + 2))
        elif op & 0xF1FF == 0x41F9:                                    # lea x,an
            v = lv.l(BASE + i + 2)
        elif op & 0xF1FF == 0x207C:                                    # movea.l #x,an
            v = lv.l(BASE + i + 2)
        elif tile is None and op == 0x317C and be16(d, i + 4) == 2:    # move.w #tile,oTile(a0)
            tile = be16(d, i + 2)
        elif tile is None and sot_at and op & 0xFF00 == 0x7000 and (   # moveq #n,d0; jsr SetObjectTileID
                (be16(d, i + 2) == 0x4EB9 and lv.l(BASE + i + 4) == sot_at) or
                (be16(d, i + 2) == 0x4EBA and BASE + i + 4 + scd_mmd.s16(be16(d, i + 4)) == sot_at)):
            tile = lv.w(ids + lv.w(ids + (op & 0xFF) * 2))             # (subtype 2 = 0)
        if v is not None and inside(v) and v not in refs:
            refs.append(v)
    for i in range(start - BASE, end - BASE - 7, 2):               # move.l #maps,oMap(a6) / move.w #tile,oTile(a6)
        op = be16(d, i)
        if op == 0x2D7C and be16(d, i + 6) == 4:
            v = lv.l(BASE + i + 2)
            if inside(v) and v not in refs:
                refs.append(v)
        elif tile is None and op == 0x3D7C and be16(d, i + 4) == 2:
            tile = be16(d, i + 2)
    if (lv.round, lv.act, oid) in ((8, 1, 0x29), (8, 2, 0x29)):                    # (MMZ2's door: its mappings by subtype)
        refs = refs + [lv.l(refs[0] + 4), lv.l(refs[0] + 8)]
    return refs, tile


def map_frames(lv, a, count=None):
    """a mapping table's frame offsets: as many as come before the nearest frame
    (frames need not follow the table in order); count: just that many"""
    offs, first = [], 0x10000
    while len(offs) * 2 < first and len(offs) < (count or 0x100):
        o = lv.w(a + len(offs) * 2)
        if o < (len(offs) + 1) * 2 and not (count and o == 0):   # (with a count: 0 = an empty frame)                         # (an offset into the table: the next table's)
            break
        offs.append(o)
        first = min(first, o) if o else first
    return offs


def is_mappings(lv, a, count=None):
    """does a look like Sonic 1-format mappings (frame offsets, then 5-byte pieces)?"""
    try:
        offs = map_frames(lv, a, count)
        nz = [o for o in offs if o]
        if not nz or min(nz) & 1 or min(nz) < len(offs) * 2 or max(offs) > 0x4000:
            return False
        for o in offs:
            n = lv.b(a + o)
            n = 0 if n & 0x80 else n                       # (DrawObjects: subq / bmi, nothing drawn)
            if n > 0x40 or any(lv.b(a + o + 2 + k * 5) & 0xF0 for k in range(n)):
                return False
        return True
    except ValueError:
        return False


def cd_mappings(lv, a, count=None):
    """Sonic 1-format mappings (byte count, 5-byte pieces) -> S3K's (word count, 6-byte pieces)"""
    frames = []
    for o in map_frames(lv, a, count):
        n = lv.b(a + o)
        n = 0 if n & 0x80 else n                           # (an empty frame)
        out = struct.pack('>H', n)
        for k in range(n):
            q = a + o + 1 + k * 5
            y, size, tile, x = lv.b(q), lv.b(q + 1), lv.w(q + 2), lv.b(q + 4)
            out += struct.pack('>bBHh', y - 256 if y & 0x80 else y, size, tile, x - 256 if x & 0x80 else x)
        frames.append(out)
    head, body = bytearray(), bytearray()
    for fr in frames:
        head += struct.pack('>H', len(frames) * 2 + len(body))
        body += fr
    return bytes(head + body)


def object_art(win, lv, free, tables):
    """the asset table (TD_ANIM) of the Sonic CD objects the level places, or 0"""
    ids = {o['id'] for o in lv.visible_objects() if o['id'] != OBJ_MONITOR or o['subtype'] in (8, 9)}
    placed = lambda a: ASSET_PLACED.get(a[1], a[3])
    todo = [a for a in CD_ASSETS if (a[2] is None or (lv.round, lv.act) in a[2]) and placed(a) in ids
            and (lambda n: n.get(lv.round) if isinstance(n, dict) else n)(ASSET_NEEDS.get(a[1], placed(a))) in ids
            and lv.time in ASSET_PERIODS.get(a[1], 'ABCD')
            and any(ASSET_SUBTYPE.get(a[1], lambda s: True)(o['subtype'])
                    for o in lv.visible_objects() if o['id'] == placed(a))]
    if not todo:
        return 0
    # the art: what the level's PLCs leave in VRAM in the section of the level
    # (by camera X: Sonic CD swaps object art by section) where the object first is
    vrams = {}

    def section(oid, extra=None, name=None):
        x = min(o['x'] for o in lv.visible_objects() if o['id'] in (oid, ASSET_PLACED.get(name))) - 160
        plcs = [lv.plc_level, 1, lv.plc_zone]
        for k, (lim, ini, _) in enumerate(lv.sections):
            if x < lim or lim == 0xFFFF:
                plcs = [lv.plc_level, 1, ini]
                break
        else:
            k = None
        key = (k, extra)
        if key not in vrams:
            vrams[key] = lv.vram([i for i in plcs + [extra] if i])
        return key
    found, nm = [], {}
    for aid, name, _, oid, mk, ak in todo:
        nm[aid] = name
        if name == 'M3BOSSCYC':                                        # (PalCycleBoss8's colours: right after its code)
            h = scan(lv.data, '47FB 000C 49F8 FB30 28DB 2893 4E75')
            if h:
                a = BASE + h[0][0] + h[0][2]
                found.append((aid, None, 0, (a, lv.bytes_at(a, 0x50)), [], None))
            continue
        if name == 'W3ENKEI':                                          # (its colour steps: a pc-relative table)
            h = scan(lv.data, '1680 47FB 00 (B) 31DB FB40')
            n = scan(lv.data, '1013 0600 00?? 0C00 00 (B)')
            if h and n:
                a = BASE + h[0][0] + 4 + h[0][1][0]
                found.append((aid, None, 0, (a, lv.bytes_at(a, n[0][1][0])), [], None))
            continue
        if lv.object_index[oid - 1] == collections.Counter(lv.object_index).most_common(1)[0][0]:
            continue                                                   # (the period's index: the empty object)
        refs, tile = obj_refs(lv, oid)
        if name in ASSET_SUBMAP:                                       # (a table of mapping tables: one of them)
            k, n = ASSET_SUBMAP[name]
            refs = refs + [refs[k] + lv.w(refs[k] + n * 2)]
        if name in ASSET_MAPSCAN:                                      # (refs obj_refs cannot see: a signature)
            at = lv.object_index[oid - 1]
            hit = scan(lv.data, ASSET_MAPSCAN[name][0], at - BASE, at - BASE + 0x200)
            refs, tile = [hit[0][1][0]], ASSET_MAPSCAN[name][1]
        if name == 'SONICHOLE' and lv.time == 'B':
            tile = 0x3BB                                               # (its code: in the past, $3BB)
        tz = {'B': 0, 'A': 1}.get(lv.time, 2)                          # (timeZone: past, present, future)
        if name == 'GENERATOR':                                        # (its code: a table by act and period)
            tile = lv.w(refs[0] + (lv.act * 4 + tz) * 2)
        if name in ('TTZ_ANIMAL', 'TTZ_ANIMAL2'):                      # (its code: a table by act and period)
            tile = lv.w(refs[0] + (lv.act * 4 + tz) * 2) | (0x2000 if name == 'TTZ_ANIMAL2' else 0)
        if name in ('ANIMALFLY', 'ANIMALGROUND', 'HOLOFLY', 'HOLOGROUND'):
            tile = lv.w(refs[4] + (lv.act * 4 + tz) * 2)               # (ObjAnimal_BaseTileList)
        if name == 'PROJECTOR' and lv.round in (1, 6):                 # (its code: by round, act; TTZ's, QQZ's, SSZ's own move)
            tile = (0x300 if lv.act else 0x4E8) if lv.round == 6 else (0x3AF if lv.act else 0x403)
        if name in ('SSZ_DANGO', 'SSZ_DANGO2'):                        # (its code: by period)
            tile = 0x23E9 if lv.time == 'B' else 0x245D
        if name == 'QQZ_STALACTITE':                                   # (its code: line 2 in the past, bad future)
            tile = 0x4342 if lv.time in 'BD' else 0x0342
        if name == 'GOALPOST':                                         # (its code: by round, act, period)
            at = lv.object_index[oid - 1]
            hit = scan(lv.data, '317B 00 (B) 0002', at - BASE, at - BASE + 0x200)
            k = ROUND_INDEX[lv.round] * 8 + lv.act * 4 + {'B': 0, 'A': 1, 'D': 2, 'C': 3}[lv.time]
            tile = lv.w(hit[0][0] + BASE + 2 + hit[0][1][0] + k * 2) | (0 if lv.round == 5 else 0x8000)
        if name in SPECIAL_DATA:
            at = lv.object_index[oid - 1]
            hit = (scan(lv.data, SPECIAL_DATA[name][0]) if name in ('GENEPAL', 'TTZ_CURRENTS', 'M3ROOMPAL') else
                   scan(lv.data, SPECIAL_DATA[name][0], at - BASE, at - BASE + 0x200))
            data = lv.bytes_at(hit[0][1][0], SPECIAL_DATA[name][1])
            found.append((aid, None, 0, (hit[0][1][0], l1_order(data) if name in L1_ORDER else data), [], None))
            continue
        if name in ASSET_PALETTE:
            cols = list(lv.palette_colours(ASSET_PALETTE[name]))
            if len(cols) < 16:                                         # (a short one: over the level's line 1)
                cols += list(lv.palette[16 + len(cols):32])
            data = b''.join(struct.pack('>H', c) for c in cols)
            found.append((aid, None, 0, (0, l1_order(data[:32])), [], None))
            continue
        if name in ASSET_TILE_OF:
            tile = obj_refs(lv, ASSET_TILE_OF[name])[1]
        tile = ASSET_TILE.get(name, tile)
        if name == 'SCENERY':                                          # (its code: by period and act; the bad
            at = lv.object_index[oid - 1]                              #  future's is the present's)
            hit = scan(lv.data, '317B 00 (B) 0002', at - BASE, at - BASE + 0x100)
            k = {'B': 0, 'A': 1, 'D': 1, 'C': 2}[lv.time] * 2 + lv.act
            tile = lv.w(hit[0][0] + BASE + 2 + hit[0][1][0] + k * 2) | 0x4000
        if name == 'LOGSHADOW':                                        # (its code: a table by period and act)
            at = lv.object_index[oid - 1]
            hit = scan(lv.data, '317B 00 (B) 0002', at - BASE, at - BASE + 0x80)
            k = {'B': 0, 'A': 1, 'D': 2, 'C': 3}[lv.time] * 2 + lv.act
            tile = lv.w(hit[0][0] + BASE + 2 + hit[0][1][0] + k * 2) | 0x4000
        raw = ak if isinstance(ak, tuple) else None
        ak = raw[1] if raw else ak
        if (ak is not None and ak >= len(refs)) or (mk is not None and (
                tile is None or mk >= len(refs) or not is_mappings(lv, refs[mk], MAP_FRAMES.get(name)))):
            raise SystemExit(f'{lv.name}: {name}: object ${oid:02X} refers to {[hex(r) for r in refs]}, tile {tile}')
        data = None
        if raw:                                                        # (to the next table it refers to)
            end = refs[ak] + raw[2] if len(raw) > 2 else min([r for r in refs if r > refs[ak]] + [refs[ak] + 0x100])
            data = lv.bytes_at(refs[ak], end - refs[ak])
            if name in L1_ORDER:                                       # (CD's line 1 -> S3K's order)
                data = l1_order(data)
        if mk is None or not tile & 0x7FF:                             # (data only)
            found.append((aid, None, 0, (refs[ak], data) if ak is not None else None, [], None))
            continue
        maps = cd_mappings(lv, refs[mk], MAP_FRAMES.get(name))
        sec = section(oid, ASSET_PLC_ROUND.get((name, lv.round), ASSET_PLC.get(name, {})).get(lv.time), name)
        pieces = [((sec, (tile >> 13) + pl & 3), (tile & 0x7FF) + t0, n)
                  for t0, n, pl in _piece_ranges_lines(maps, ASSET_FRAMES.get(name))]
        found.append((aid, maps, tile, (refs[ak], data) if ak is not None else None, pieces, sec))
    # The sections each asset's art is in, as Sonic CD has it: where the level's
    # PLCs leave the same pixels at its tiles as in its own section. Spans (the
    # tiles pieces need whole, overlapping pieces merged) take VRAM free in all
    # their sections, biggest first; sections share what the others leave.
    secs = list(range(len(lv.sections))) or [None]

    def section_vram(k, extra):
        key = (k, extra)
        if key not in vrams:
            ini = lv.sections[k][1] if k is not None else lv.plc_zone
            vrams[key] = lv.vram([i for i in (lv.plc_level, 1, ini, extra) if i])
        return vrams[key]
    spans = {}
    for aid, maps, tile, anim, pieces, sec in found:
        if not pieces or nm.get(aid) in ASSET_OVERLAY:
            continue
        home = vrams[sec]
        avail = set()
        for k in secs:
            v = section_vram(k, sec[1])
            if nm.get(aid) not in ASSET_OWN_SECTION and (
                    sec[1] is not None or all(v[t0 * 32:(t0 + n) * 32] == home[t0 * 32:(t0 + n) * 32]
                                              for _, t0, n in pieces)):
                avail.add(k)                                           # (overlaid art: everywhere)
        avail.add(sec[0])
        for key, t0, n in pieces:
            spans.setdefault(key, []).append((t0, t0 + n, frozenset(avail)))
    runs = []
    for key, ivs in spans.items():
        cur = None
        for lo, hi, av in sorted(ivs, key=lambda v: (v[0], v[1])) + [(None, None, None)]:
            if cur and lo is not None and lo < cur[1]:
                cur[1], cur[2] = max(cur[1], hi), cur[2] | av
                continue
            if cur:
                runs.append((key, cur[0], cur[1], cur[2]))
            cur = [lo, hi, av] if lo is not None else None
    runs.sort(key=lambda r: (r[1] - r[2], -len(r[3]), str(r[0]), r[1]))
    pools = {k: set(free) for k in secs}
    place, segs = {}, []
    for key, lo, hi, av in runs:
        sec, eline = key
        common = set.intersection(*(pools[k] for k in av))
        dst = _find_run(common, hi - lo)
        if dst is None:
            raise SystemExit(f'{lv.name}: no VRAM for object art ({hi - lo} tiles, sections {sorted(map(str, av))})')
        for k in av:
            pools[k] -= set(range(dst, dst + hi - lo))
        raw = bytes(vrams[sec][lo * 32:hi * 32])
        tab = list(range(16)) if eline == 0 else tables[eline]       # (line 0: its own colours)
        art = bytes((tab[b >> 4] << 4) | tab[b & 15] for b in raw)
        mask = sum(1 << k for k in av) if secs != [None] else 0xFFFF
        segs.append((win.add(art), dst, hi - lo, eline == 0, mask))
        place.update({(key, lo + k): dst + k for k in range(hi - lo)})
    lv.art_place = place
    # overlays: on the tiles of the art they replace, as the record's own segments
    spare = set.intersection(*pools.values()) if pools else set()   # (what the residents leave: 'SPARE')
    pools, ovl = {}, {}
    for aid, maps, tile, anim, pieces, sec in found:
        base = ASSET_OVERLAY.get(nm.get(aid))
        if not base or not pieces:
            continue
        if base not in pools:
            pools[base] = {place[(key, t0 + k)] for b, _, _, _, ps, _ in found if nm.get(b) in base
                           for key, t0, n in ps for k in range(n) if (key, t0 + k) in place}
            if 'SPARE' in base:
                pools[base] |= spare
        pool, segs_o = pools[base], b''
        merged = {}
        for key, t0, n in pieces:
            merged.setdefault(key, []).append((t0, t0 + n))
        for key, ivs in merged.items():
            cur = None
            for lo, hi in sorted(ivs) + [(None, None)]:
                if cur and lo is not None and lo < cur[1]:
                    cur[1] = max(cur[1], hi)
                    continue
                subs, s0 = [], None                                # (tiles another overlay put already: shared)
                for t in (range(cur[0], cur[1] + 1) if cur else ()):
                    if t < cur[1] and (key, t) not in place:
                        s0 = t if s0 is None else s0
                    elif s0 is not None:
                        subs.append((s0, t))
                        s0 = None
                for lo_, hi_ in subs:
                    (sec_k, eline) = key
                    dst = _find_run(pool, hi_ - lo_)
                    if dst is None:
                        raise SystemExit(f'{lv.name}: {nm[aid]}: no room on the art it replaces ({hi_ - lo_} tiles)')
                    pool -= set(range(dst, dst + hi_ - lo_))
                    tab = list(range(16)) if eline == 0 else tables[eline]
                    art = bytes((tab[b >> 4] << 4) | tab[b & 15] for b in vrams[sec_k][lo_ * 32:hi_ * 32])
                    segs_o += struct.pack('>IHH', win.add(art), dst, (hi_ - lo_) | (0x8000 if eline == 0 else 0))
                    place.update({(key, lo_ + k): dst + k for k in range(hi_ - lo_)})
                cur = [lo, hi] if lo is not None else None
        ovl[aid] = (len(segs_o) // 8, win.add(segs_o))
    table = bytearray(ASSET_COUNT * ASSET_REC)
    for aid, maps, tile, anim, pieces, sec in found:
        line, first = (tile >> 13) & 3, tile & 0x7FF
        maps = _retile_lines(maps, lambda t, pl: place.get(((sec, (line + pl) & 3), first + t), 0)) if maps else None
        anim_a = 0
        if anim:
            anim_a = win.add(anim[1] if anim[1] is not None else anim_blob(lv.data, anim[0] - BASE))
        nseg, seg_a = ovl.get(aid, (0, 0))
        struct.pack_into('>IHHII', table, aid * ASSET_REC, win.add(maps) if maps else 0, tile & 0xE000, nseg, anim_a, seg_a)
    segtab = b''.join(struct.pack('>IHHHH', a, d, n | (0x8000 if x else 0), m, 0) for a, d, n, x, m in segs)
    secs_t = struct.pack('>H', len(lv.sections)) + b''.join(struct.pack('>H', lim) for lim, _, _ in lv.sections)
    struct.pack_into('>II', win.data, 0xE0, win.add(segtab + bytes(12), dedupe=False), win.add(secs_t, dedupe=False))
    return win.add(table, dedupe=False)


def convert(lv, base, glyphs, s3k):
    """one level -> (zone_and_act, blob)"""
    win = Window(base)
    chunks256 = bytearray(lv.chunk_data())
    nblk = lv.nblocks
    for i in range(0, len(chunks256), 2):                     # (cells past the blocks draw garbage)
        if (be16(chunks256, i) & 0x3FF) >= nblk:
            chunks256[i:i + 2] = b'\0\0'
    planes = [lv.fg, lv.bg]
    # Quartz Quadrant's other background (its open middle: scenery_sections)
    bg2_lay = None
    if scenery_sections(lv):
        alts = [lv.layouts[s_['layout']] for s_ in getattr(lv, 'bg_sections', [])
                if s_['layout'] != lv.layout_table[1] and s_['layout'] in lv.layouts]
        if alts and alts[0].width <= lv.bg.width and alts[0].height <= lv.bg.height:
            bg2_lay = alts[0]
            planes.append(bg2_lay)

    # loop chunks: where the alternate chunk's cell differs, a synthetic block
    # (front visuals, front shape primary, the alternate's secondary)
    synth = {}
    for plane in planes:
        for row in plane.rows:
            for b in row:
                alts = lv.loop_alternates(b & 0x7F) if b & 0x80 else []
                if not alts:
                    continue
                v, v2 = b & 0x7F, alts[0]
                for i in range(256):
                    w = be16(chunks256, (v - 1) * 512 + i * 2)
                    wb = be16(chunks256, (v2 - 1) * 512 + i * 2)
                    key = (bb._cell(w), bb._cell(wb))
                    if key[0] != key[1] and key not in synth:
                        synth[key] = None

    # the blocks this level shows, renumbered from 1 (S3K's index holds $300)
    def used_chunks():
        seen = set()
        for plane in planes:
            for row in plane.rows:
                for b in row:
                    if b:
                        seen.add(b & 0x7F)
                        if b & 0x80:
                            seen.update(lv.loop_alternates(b & 0x7F))
        return seen
    used = {0}
    for c in used_chunks():
        for i in range(256):
            used.add(be16(chunks256, (c - 1) * 512 + i * 2) & 0x3FF)
    # identical blocks (same tiles, same shape) share one number
    order, renum, seen = [], {}, {}
    for b in sorted(used):
        key = (bytes(lv.blocks[b * 8:b * 8 + 8]), lv.collision[b] if b < len(lv.collision) else 0)
        if b and key in seen:
            renum[b] = renum[seen[key]]
            continue
        seen[key] = b
        renum[b] = len(order)
        order.append(b)
    for key in synth:
        synth[key] = len(order) + len([k for k in synth if synth[k] is not None])
    if len(order) + len(synth) > MAX_BLOCKS:
        raise SystemExit(f'{lv.name}: {len(order) + len(synth)} blocks (S3K holds {MAX_BLOCKS})')
    blocks = bytearray()
    for b in order:
        blocks += lv.blocks[b * 8:b * 8 + 8]
    for (fb, bb_), nid in sorted(synth.items(), key=lambda kv: kv[1]):
        blocks += lv.blocks[fb[0] * 8:fb[0] * 8 + 8]

    # collision: S&K's interleaved index (primary, secondary per block; the
    # game points the secondary path at +1: TH_SOLIDS_SPLIT); loop blocks mirror shapes
    col = lv.collision
    solids = bytearray(0x800)
    for b, n in renum.items():
        solids[n * 2] = solids[n * 2 + 1] = col[b] if b < len(col) else 0
    angs, hgts, hgtr = bytearray(lv.angles), bytearray(lv.heights), bytearray(lv.widths)
    free_shapes = [i for i in range(1, 256) if i not in (set(col) | {0})]
    made = {}
    for (fb, bk), nid in synth.items():
        solids[nid * 2] = col[fb[0]]
        rel = (fb[1] ^ bk[1], fb[2] ^ bk[2])
        shape = col[bk[0]]
        if rel != (0, 0) and shape:
            k = (shape, rel)
            if k not in made:
                ns = free_shapes.pop(0)
                n, r, a = bb.flip_shape(hgts, angs, shape, rel)
                hgts[ns * 16:ns * 16 + 16], hgtr[ns * 16:ns * 16 + 16], angs[ns] = n, r, a
                made[k] = ns
            shape = made[k]
        solids[nid * 2 + 1] = shape

    def word(w, behind=None):
        idx = renum.get(w & 0x3FF, 0)
        if behind is not None and bb._cell(behind) != bb._cell(w):
            idx = synth[(bb._cell(w), bb._cell(behind))]
        top, lrb = (w >> 13) & 1, (w >> 14) & 1
        top2, lrb2 = (top, lrb) if behind is None else ((behind >> 13) & 1, (behind >> 14) & 1)
        return idx | ((w >> 11) & 1) << 10 | ((w >> 12) & 1) << 11 | top << 12 | lrb << 13 | top2 << 14 | lrb2 << 15

    chunks = [bytes(128)]
    index = {bytes(128): 0}

    def quad(ci, qx, qy, alt=None):
        out = bytearray()
        for r in range(8):
            for c in range(8):
                o = ((qy * 8 + r) * 16 + qx * 8 + c) * 2
                w = be16(chunks256, ci * 512 + o)
                out += struct.pack('>H', word(w, be16(chunks256, alt * 512 + o) if alt is not None else None))
        blob = bytes(out)
        # (Collision Chaos: a bumper that bursts clears its blocks from its own Sonic CD chunk,
        #  so its quads are that chunk's own, not shared with a look-alike elsewhere)
        key = (blob, ci) if lv.round == 3 and blob != bytes(128) else blob
        if key not in index:
            index[key] = len(chunks)
            chunks.append(blob)
        return index[key]

    def rows128(plane):
        out = []
        for row in plane.rows:
            top, bot = [], []
            for b in row:
                if not b:
                    top += [0, 0]
                    bot += [0, 0]
                    continue
                v = b & 0x7F
                alts = lv.loop_alternates(v) if b & 0x80 else []
                alt = alts[0] - 1 if alts else None
                ids = [quad(v - 1, qx, qy, alt) for qy in range(2) for qx in range(2)]
                top += ids[0:2]
                bot += ids[2:4]
            out += [top, bot]
        return out

    fg, bg = rows128(lv.fg), rows128(lv.bg)
    bg2 = rows128(bg2_lay) if bg2_lay else None
    grid = lambda rows: b''.join(struct.pack('>%dH' % len(r), *r) for r in rows)
    bg2_a = win.add(grid(bg2)) if bg2 else 0
    pager_a = 0
    if len(chunks) > 256 or bg2:                              # (paged around the camera; a swapped background too)
        pager_a = win.add(struct.pack('>HHHHHIII', len(chunks), len(fg[0]), len(fg), len(bg[0]), len(bg),
                                      win.add(b''.join(chunks)), win.add(grid(fg)), win.add(grid(bg))))
        sx = lv.start[0]
        lo, hi = (sx - 160 - PAGE_MARGIN) // 128, (sx + 240 + PAGE_MARGIN) // 128
        need = sorted({v for r in fg for c, v in enumerate(r) if lo <= c <= hi and v} | {v for r in bg for v in r if v})
        if len(need) > 255:
            raise SystemExit(f'{lv.name}: {len(need)} chunks around the start')
        slot = {v: n + 1 for n, v in enumerate(need)}
        fg = [[slot.get(v, 0) if lo <= c <= hi else 0 for c, v in enumerate(r)] for r in fg]
        bg = [[slot[v] if v else 0 for v in r] for r in bg]
        page = [bytes(128)] + [chunks[v] for v in need]
        chunks_out = page + [bytes(128)] * (256 - len(page))
    else:
        chunks_out = chunks
    layout_a = win.add(bb.s3k_layout(fg, bg))
    chunks_a = win.add(kosinski_encode(b''.join(chunks_out)))

    # level art: the tiles the blocks draw, packed under S3K's common objects
    vram = lv.vram([lv.plc_level])
    tables = {0: bb.L0_MAP, 1: bb.L1_PERM, 2: list(range(16)), 3: list(range(16))}
    pk = Packer(vram, tables, bb.S3K_LEVEL_TILE_LIMIT, [])
    # animated art: slots of its own (never shared) for each line its blocks use
    anims, pin_src, anim_blob_ = anim_tiles(lv), {}, b''
    used_lines = {}
    for b in range(len(blocks) // 8):
        for k in range(4):
            w = be16(blocks, b * 8 + k * 2)
            used_lines.setdefault(w & 0x7FF, set()).add((w >> 13) & 3)
    scripts = []
    for gated, dur, frames, t0, nt, arts, _ in anims:
        for line in sorted(set().union(*(used_lines.get(t0 + k, set()) for k in range(nt)))):
            base = pk._find(nt)
            for k in range(nt):
                pk.taken.add(base + k)
                pk.slot[(t0 + k, line)] = base + k
            rec = b''.join(bb.remap_tile(a[k * 32:k * 32 + 32], tables[line]) for a in arts for k in range(nt))
            for k in range(nt):
                pin_src[base + k] = rec[frames[0][0] * nt * 32 + k * 32:frames[0][0] * nt * 32 + k * 32 + 32]
            body = bytes(v for i, d in frames for v in ((i * nt, d) if dur < 0 else (i * nt,)))
            scripts.append((dur & 0xFF, len(frames), nt, int(gated), base, rec, body))   # (flags: 1, 2, 8 gates)
    # diagonal springs are S3K's Obj_Spring: its art at ArtTile_DiagonalSpring, by the act's PLC
    diag = any(o['id'] == OBJ_SPRING and o['subtype'] & 8 for o in lv.visible_objects())
    if diag:
        pk.reserve(0x43A, len(nemesis_decode(s3k, S3K_DIAG_SPRING)[0]) // 32)
    for b in range(len(blocks) // 8):
        for k in range(4):
            w = be16(blocks, b * 8 + k * 2)
            pk.place_single(w & 0x7FF, (w >> 13) & 3)
    for b in range(len(blocks) // 8):
        for k in range(4):
            o = b * 8 + k * 2
            w = be16(blocks, o)
            w = (w & 0xF800) | pk.slot.get((w & 0x7FF, (w >> 13) & 3), 0)
            blocks[o], blocks[o + 1] = w >> 8, w & 0xFF
    art = pk.art(pin_src)
    if len(art) & 1:
        art += bytes(1)
    art_a, art2_a = bb.level_art_modules(win, art)
    blocks_a = win.add(kosinski_encode(bytes(blocks)))

    # palette: lines 1-3 (line 1 in S3K's common-colour order); line 0 stays S3K's
    lines = lv.palette[16:64]
    l1 = lines[0:16]
    perm = [0] * 16
    for i in range(16):
        perm[bb.L1_PERM[i]] = l1[i]
    lines = perm + lines[16:]
    pal_id = win.add_palette(b''.join(struct.pack('>H', c) for c in lines), 0xFC20)
    plc_id = win.add_plc([(S3K_DIAG_SPRING, 0x43A * 32)]) if diag else 0
    llb_a = win.add(struct.pack('>IIIIII', (plc_id << 24) | art_a, art2_a, (pal_id << 24) | blocks_a,
                                (pal_id << 24) | blocks_a, chunks_a, chunks_a))
    solids_a = win.add(solids)
    ang_a, hgt_a, hgtr_a = win.add(bytes(angs)), win.add(bytes(hgts)), win.add(bytes(hgtr))

    # Sonic 1's info block: raw FG (64x8) + special chunks (loop, special loop,
    # roll tunnels) + water (none yet) + no LZ3 swap
    raw = bytearray(64 * 8)
    for r, row in enumerate(lv.fg.rows[:8]):
        raw[r * 64:r * 64 + min(64, len(row))] = bytes(row[:64])
    water_pal, water_y = 0, 0
    if lv.water and 'palette' in lv.water and 'height' in lv.water:   # (Tidal Tempest: its underwater palette, start)
        pp = be32(s3k, 0x0A872C + 0x31 * 8)                           # (S3K's underwater player line: HCZ1's)
        wl = list(lv.water['palette'])
        wperm = [0] * 16
        for i in range(16):
            wperm[bb.L1_PERM[i]] = wl[16 + i]
        water_pal = win.add_palette(bytes(s3k[pp:pp + 0x20]) + b''.join(struct.pack('>H', c) for c in wperm + wl[32:64]),
                                    0xFC00)
        water_y = lv.water['height']
    info_a = win.add(bytes(raw) + bytes(lv.special_chunks) + struct.pack('>BBH', water_pal, 0, water_y) + bytes(8))
    ob, rb, ptrs = convert_objects(lv)
    objs_a, rings_a, ptrs_a = win.add(ob, dedupe=False), win.add(rb, dedupe=False), win.add(ptrs)

    b_ = lv.bounds
    sx, sy = lv.start
    d = win.data
    struct.pack_into('>IIIHHHHHHHHII', d, 0, llb_a, layout_a, solids_a,
                     b_['left'], b_['right'], (-0x100 if b_['bottom'] == 0x800 else b_['top']) & 0xFFFF,
                     b_['bottom'], sx, sy, sx, sy, objs_a + 6, rings_a)    # (bottom $800: Sonic CD wraps, S3K's -$100)
    d[0x24] = music_track(lv)
    d[0x25] = TRI_GAME_CD
    struct.pack_into('>III', d, 0x28, ang_a, hgt_a, hgtr_a)
    free = (set(range(pk.end(), bb.S3K_LEVEL_TILE_LIMIT)) | {t for lo, hi in (WINDOWS_WET if water_pal else WINDOWS_DRY) for t in range(lo, hi)}) - pk.reserved
    struct.pack_into('>II', d, 0x34, ptrs_a, object_art(win, lv, free, tables))
    struct.pack_into('>16H', d, 0x60, *lv.palette[0:16])     # (above the water; below: Tidal Tempest's own)
    struct.pack_into('>16H', d, 0x80, *(lv.water['palette'][0:16] if water_pal else lv.palette[0:16]))
    d[0xA0:0xE0] = touch_map(lv, s3k)
    struct.pack_into('>I', d, 0x3C, info_a)
    cyc = palette_cycles(lv)                                 # (header +$EC: u16 n; n x (u8 set, script, colours))
    if cyc:
        blob = struct.pack('>H', len(cyc))
        for k, script, cols in cyc:
            blob += bytes([k]) + script + cols
            if len(blob) & 1:
                blob += b'\0'
        struct.pack_into('>I', d, 0xEC, win.add(blob, dedupe=False))
    if lv.round in BLOCK_ROUNDS:                          # (the CD block under a point: player-level gimmicks)
        rows = lv.fg.rows                                 # u16 chunks, u16 w, u16 h, FG w*h, chunks x $200
        fw, fh = max(len(r) for r in rows), len(rows)
        grid = b''.join(bytes(r) + bytes(fw - len(r)) for r in rows)
        nch = len(lv.chunk_data()) // 0x200
        blob = struct.pack('>HHH', nch, fw, fh) + grid + (b'\0' if len(grid) & 1 else b'') + lv.chunk_data()
        struct.pack_into('>I', d, 0xE8, win.add(blob, dedupe=False))
    struct.pack_into('>I', d, 0x50, pager_a)
    for gated, dur, frames, t0, nt, arts, alt in anims:              # (the other place: an object's tiles)
        if alt is None:
            continue
        for (key, t), dst in sorted(getattr(lv, 'art_place', {}).items(), key=lambda kv: kv[1]):
            if t != alt:
                continue
            line = key[1]
            rec = b''.join(bb.remap_tile(a[k * 32:k * 32 + 32], tables[line]) for a in arts for k in range(nt))
            body = bytes(v for i, d_ in frames for v in ((i * nt, d_) if dur < 0 else (i * nt,)))
            scripts.append((dur & 0xFF, len(frames), nt, 4, dst, rec, body))
            break
    if scripts:
        blob = struct.pack('>H', len(scripts))
        for dur, nf, nt, flags, base, rec, body in scripts:
            blob += struct.pack('>BBBBHHI', dur, nf, nt, flags, base, 0, win.add(rec)) + body + bytes(len(body) & 1)
        struct.pack_into('>I', d, 0xF0, win.add(blob, dedupe=False))
    sc = scenery_sections(lv)
    if sc:                                                   # (+$F4: Quartz Quadrant's sections)
        lims, sets, (_, _, arts), *cyc = sc
        out = struct.pack('>H%dH' % len(lims), len(lims), *lims) + bytes(sets) + bytes(len(sets) & 1)
        out += struct.pack('>H', len(arts))
        for k in range(len(arts)):
            cols = list(lv.palette_colours(4 + k))[:24] + [0] * 24
            l1 = [0] * 16
            for i in range(16):
                l1[bb.L1_PERM[i]] = cols[i]
            out += struct.pack('>24H', *(l1 + cols[16:24]))
        for t in cyc:                                          # (belt, reversed, even, odd: Sonic CD's own form)
            out += struct.pack('>I', win.add(lv.bytes_at(t, 4 + lv.b(t + 1) * lv.b(t + 2) * 2)) if t else 0)
        out += struct.pack('>HHI', len(bg2[0]), len(bg2), bg2_a) if bg2 else bytes(8)   # (set 1's background)
        struct.pack_into('>I', d, 0xF4, win.add(out))
    tiles, frame = bb.title_card_name(ROUND_NAME[lv.round], glyphs)
    card_a = win.add(kosm_encode(tiles if tiles else bytes(32)))
    card_map = struct.pack('>HH', 4, 6) + struct.pack('>H', 0) + frame      # (frame 0 empty, 1 the name)
    struct.pack_into('>II', d, 0x54, card_a, win.add(card_map))
    blob = win.finish()
    zone = 0x40 + ROUND_INDEX[lv.round] * 4 + PERIOD[lv.time]
    print(f'  {lv.name}: zone ${zone:02X} act {lv.act}, {len(order)} blocks + {len(synth)} loop, '
          f'{len(chunks)} chunks{" (paged)" if pager_a else ""}, {pk.end()} tiles, fg {len(fg[0])}x{len(fg)}, {len(blob)} bytes')
    return zone << 8 | lv.act, blob


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--disc', required=True, help="the disc's files (scd_iso.py)")
    ap.add_argument('--out', default=str(ROOT / 'build' / 'Release' / 'scd.bin'))
    ap.add_argument('--window', type=lambda s: int(s, 0), default=WINDOW)
    ap.add_argument('--only', help='level names, e.g. R11A,R11B')
    ap.add_argument('--s3k', default=str(ROOT / 'game' / 'sonic3k' / 'sonic3k.bin'), help='for the title card font')
    a = ap.parse_args(argv)
    only = {n.strip().upper() for n in a.only.split(',')} if a.only else None
    s3k = Path(a.s3k).read_bytes()
    glyphs = bb.title_card_glyphs(s3k)
    levels = []
    for path in level_files(a.disc):
        lv = Level(Path(path).read_bytes(), Path(path).name)
        if lv.demo or (only and lv.name not in only):
            continue
        levels.append(convert(lv, a.window, glyphs, s3k))
    if not levels:
        raise SystemExit('no levels converted')
    size = max(len(b) for _, b in levels)
    if size > WINDOW_SIZE:
        raise SystemExit(f'a level needs ${size:X} bytes; the window holds ${WINDOW_SIZE:X}')
    entries = [(za, 0, blob) for za, blob in levels]
    snd_addr = a.window + WINDOW_SIZE
    try:                                                     # (Sonic CD's FM effects: scd_sfx.py)
        bank, table, ids = scd_sfx.build(a.disc, s3k, snd_addr)
        entries.append((SOUND_ENTRY, 1, bank + table))
        print(f'  FM sound set: {len(ids)} effects, bank at ${snd_addr:06X}')
    except (SystemExit, ValueError) as e:
        print(f'  no FM sound set: {e}')
    head = bytearray(b'TSCD' + struct.pack('>HHII', 2, len(entries), a.window, WINDOW_SIZE))
    body = bytearray()
    off = len(head) + len(entries) * 12
    for za, flags, blob in entries:
        head += struct.pack('>HHII', za, flags, off + len(body), len(blob))
        body += blob
    Path(a.out).write_bytes(bytes(head) + bytes(body))
    print(f'{len(levels)} levels, largest ${size:X} bytes -> {a.out}')


if __name__ == '__main__':
    main()
