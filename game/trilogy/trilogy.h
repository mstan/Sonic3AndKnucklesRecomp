/*
 * trilogy.h — Sonic Trilogy layer: Sonic 1 / Sonic 2 zones on the retail
 * Sonic 3 & Knuckles engine.
 *
 * The cartridge is recompiled UNMODIFIED. Everything here is game-owned
 * extension code that runs through two engine contracts:
 *   - game_hook sites   (trilogy_hooks.h, generated): pre-instruction calls
 *     that may repoint registers at our data or replace a routine (return 1);
 *   - dispatch_override: code addresses in TRILOGY_NATIVE_BASE.. are native
 *     C routines (zone screen events, new objects, ...).
 * Converted S1/S2 data lives in the expansion bank mapped at $400000
 * (trilogy.bin, built from the player's own ROMs by tools/trilogy/).
 */
#pragma once
#include <stdint.h>
#include "trilogy_syms.h"
#include "trilogy_hooks.h"


/* ---- zone ids ---------------------------------------------------------- */
#define TRI_ZONE_BASE   0x20
#define TRI_S1_GHZ      0x20
#define TRI_S1_LZ       0x21
#define TRI_S1_MZ       0x22
#define TRI_S1_SLZ      0x23
#define TRI_S1_SYZ      0x24
#define TRI_S1_SBZ      0x25
#define TRI_S1_FZ       0x26
#define TRI_S2_EHZ      0x28
#define TRI_S2_CPZ      0x29
#define TRI_S2_ARZ      0x2A
#define TRI_S2_CNZ      0x2B
#define TRI_S2_HTZ      0x2C
#define TRI_S2_MCZ      0x2D
#define TRI_S2_OOZ      0x2E
#define TRI_S2_MTZ      0x2F
#define TRI_S2_SCZ      0x30
#define TRI_S2_WFZ      0x31
#define TRI_S2_DEZ      0x32
#define TRI_ZONE_END    0x40

#define TRI_GAME_S3K    0
#define TRI_GAME_S1     1
#define TRI_GAME_S2     2

/* ---- expansion bank ---------------------------------------------------- */
#define TRI_BANK_BASE   0x400000u
#define TRI_DIR_OFF     0x40u          /* directory of zone-act entries */
#define TRI_DIR_ENTRY   0x40u
#define TRI_DIR_COUNT   0x80u          /* (zone-$20)*4 + act */

/* directory entry field offsets (big-endian, in the bank) */
enum {
    TD_LLB      = 0x00,  /* u32 -> S3K LevelLoadBlock entry (24 bytes)   */
    TD_LAYOUT   = 0x04,  /* u32 -> S3K layout ($1000 bytes)              */
    TD_SOLIDS   = 0x08,  /* u32 collision index (S3 format) — read in place */
    TD_SIZES    = 0x0C,  /* u16 xstart,xend,ystart,yend — read in place    */
    TD_START_S  = 0x14,  /* u16 x,y                                         */
    TD_START_K  = 0x18,  /* u16 x,y                                         */
    TD_OBJECTS  = 0x1C,  /* u32 -> object layout                           */
    TD_RINGS    = 0x20,  /* u32 -> ring layout                             */
    TD_MUSIC    = 0x24,  /* u8                                              */
    TD_GAME     = 0x25,  /* u8 TRI_GAME_*                                  */
    TD_FLAGS    = 0x26,  /* u8                                              */
    TD_LOOKSHIFT= 0x27,  /* u8                                              */
    TD_ANGLES   = 0x28,  /* u32 -> angle array (256)                       */
    TD_HEIGHTS  = 0x2C,  /* u32 -> height maps (256*16)                    */
    TD_HEIGHTSR = 0x30,  /* u32 -> rotated height maps                     */
    TD_OBJPTRS  = 0x34,  /* u32 -> object pointer table (256 longs)        */
    TD_ANIM     = 0x38,  /* u32 -> animated-art descriptor                 */
    TD_EXTRA    = 0x3C,  /* u32 -> S1: raw FG layout 64x8 + LoopTileNums[4] */
};

/* bank header (at TRI_BANK_BASE) */
enum {
    TB_MAGIC    = 0x00,  /* "TRLG" */
    TB_VERSION  = 0x04,  /* u16 */
    TB_PALPOINT = 0x08,  /* u32 -> PalPoint-format table for ids $80.. */
    TB_PALCOUNT = 0x0C,  /* u16 */
    TB_PLCTABLE = 0x10,  /* u32 -> table of u32 PLC list pointers, ids $80.. */
    TB_PLCCOUNT = 0x14,  /* u16 */
    TB_TITLEART = 0x18,  /* u32 -> 32 x u32 KosM letter art, index zone-$20 */
    TB_TITLEMAP = 0x1C,  /* u32 -> mapping table, frame = zone-$20+1 */
    TB_BLOBS    = 0x20,  /* u32 -> table of u32 blob pointers (TRI_BLOB_*) */
};
#define TRI_BLOB_S1_LEDGE_SLOPE 0   /* GHZ ledge heightmap, one byte per pixel */
#define TRI_BLOB_ANIMAL_TYPES   1   /* byte[zone*2+n] animal type, zones 0..$3F */
#define TRI_BLOB_ANIMAL_PLCS    2   /* u32[zone-$20] -> Load_PLC_Raw list */
#define TRI_BLOB_EMPTY_LIST     3   /* word $FFFF */
#define TRI_BLOB_S1_LZ_WOBBLE   4   /* S1 Drown_WobbleData, 256 bytes */
#define TRI_BLOB_S1_LZ_SCROLL   5   /* S1 Lz_Scroll_Data, 256 bytes */
#define TRI_BLOB_S1_GHZ_WATER   6   /* Art_GhzWater, 2 frames x 8 tiles */
#define TRI_BLOB_S1_GHZ_FLOWER1 7   /* Art_GhzFlower1, 2 frames x 16 tiles */
#define TRI_BLOB_S1_GHZ_FLOWER2 8   /* Art_GhzFlower2, 3 frames x 12 tiles */
#define TRI_BLOB_S1_GHZ_PALCYC  9   /* Pal_GHZCyc, 4 steps x 4 colours */
#define TRI_BLOB_ACT3_NUMERAL   10  /* KosM title-card digit "3" (act 3) */
#define TRI_BLOB_S1_OSC_INIT    11  /* S1 OscillateNumInit baselines ($42 bytes) */
#define TRI_BLOB_S1_OSC_DATA    12  /* S1 OscillateNumDo settings (16 x freq,amp) */
#define TRI_BLOB_S1_MZ_LAVA     13  /* Art_MzLava1, 3 frames x 8 tiles */
#define TRI_BLOB_S1_MZ_MAGMA    14  /* Art_MzLava2, 3 frames x 32 rows x 16 bytes */
#define TRI_BLOB_S1_MZ_TORCH    15  /* Art_MzTorch, 4 frames x 6 tiles */
#define TRI_BLOB_S1_MZ_PFM1     16  /* LGrass_Data1 heightmap */
#define TRI_BLOB_S1_MZ_PFM2     17  /* LGrass_Data2 heightmap */
#define TRI_BLOB_S1_MZ_PFM3     18  /* LGrass_Data3 heightmap */
#define TRI_BLOB_S1_MUSIC       19  /* u8 envs, 0, envs x u16 LE, then songs $81..$93 (bank, 0, ptr LE) */
#define TRI_BLOB_MUSIC_SLOTS    20  /* bytes $20, $21 */
#define TRI_BLOB_S1_SYZ_PALCYC  21  /* Pal_SYZCyc1 (4 x 8 bytes) + Pal_SYZCyc2 (4 x 4 bytes) */
#define TRI_BLOB_WATER_XFER     22  /* HInt3 water colour list (count-1, CRAM offsets) */
#define TRI_BLOB_BUBBLES_PLC    23  /* PLC: S3K ArtNem_Bubbles -> $45C */
#define TRI_BLOB_S1_PTFM_LISTS  26  /* 16 x u16 offset; lists: count-1, (x, y, subtype) words */
#define TRI_BLOB_S1_SEESAW_SLOPE 24 /* See_DataSlope, a byte per pixel */
#define TRI_BLOB_S1_SEESAW_FLAT  25 /* See_DataFlat */
#define TRI_BLOB_S1_LZ_PALCYC   27  /* Pal_LZCyc1/2/3 + Pal_SBZ3Cyc ($64 bytes) */
#define TRI_BLOB_S1_SLZ_PALCYC  28  /* Pal_SLZCyc */
#define TRI_BLOB_S1_SBZ_SMOKE1  29  /* Art_SbzSmoke for ArtTile_SBZ_Smoke_Puff_1's line */
#define TRI_BLOB_S1_SBZ_SMOKE2  30  /* ... Puff_2's line */
#define TRI_BLOB_S1_SBZ_PALCYC  31  /* ROM $1C9E-$1DB0: Pal_SBZCycList1/2 + colour tables */
#define TRI_BLOB_MENU_FONT      32  /* S3K level-select font, 40 tiles: 0-9 * (c) : . A-Z */
#define TRI_BLOB_S2_EHZ_WATER   33  /* S2 CyclingPal_EHZ_ARZ_Water, 4 steps x 4 colours */
#define TRI_BLOB_S2_MUSIC       34  /* u8 envs, 0, envs x u16 LE, then songs $81..$9F (bank, 0, ptr LE) */
#define TRI_BLOB_S2_TUBES       35  /* S2 Obj1E paths: off_22980 at 0, off_22E88 at $508 */
#define TRI_BLOB_S2_SLOPES      36  /* S2 sloped-solid heightmaps: Obj40 diagonal (0), straight ($28), Obj86 flipper frames ($50 + $24 n) */
#define TRI_BLOB_S2_CPZ_PALCYC  37  /* S2 CyclingPal_CPZ1 (9 x 3 colours), CPZ2 at $36 (21), CPZ3 at $60 (16) */
#define TRI_BLOB_S2_CNZ_PALCYC  38  /* S2 CyclingPal_CNZ1 (0), CNZ3 ($24), CNZ4 ($36), CNZ1_B ($5E), CNZ2_B ($70), CNZ3_B ($84) */
#define TRI_BLOB_EXTPAL         39  /* per dir entry ((zone-$20)*4+act): 16 colours (extended sprite line 1), 16 underwater */
#define TRI_BLOB_S2_HTZ_LAVA    41  /* S2 CyclingPal_Lava (16 x 4 colours) */
#define TRI_BLOB_S2_MCZ_LANTERN 42  /* S2 CyclingPal_Lantern (4 colours) */
#define TRI_BLOB_S2_OOZ_OIL     43  /* S2 CyclingPal_Oil (7 colours, read 4 at a time) */
#define TRI_BLOB_S2_MTZ_PALCYC  45  /* S2 CyclingPal_MTZ1 (0), MTZ2 ($C), MTZ3 ($18) */
#define TRI_BLOB_S2_PILOT       46  /* Tornado pilot: Tails $10,1,2,3,4 then Sonic $2D-$30, 6 tiles (192 bytes) each */
#define TRI_BLOB_S2_SS          49  /* Sonic 2's half-pipe special stage (s2_special.py) */
#define TRI_BLOB_S2_SFX         50  /* the Sonic 2 sound set: u8 bank, $AD x u16 SFX pointers ($33-$DF), $51 x u8 S3K id per S2 SndID $A0-$F0 */
#define TRI_BLOB_S2_DAC         51  /* Sonic 2's DAC samples: u8 bank, u8 first S3K id, u8 count (s2_dac.py) */
#define TRI_BLOB_S2_CNZ_BUMPERS 52  /* u16 offset of act 2's list; act 1's (after a 0,0,0 marker), act 2's: SpecialCNZBumpers (id, x, y words; x $FFFF ends) */
#define TRI_BLOB_KNUX_WALLDASH 53  /* Knuckles' spin dash frames + spin dash dust turned a quarter (knux_walldash.py) */
#define TRI_BLOB_S2_ENDING      48  /* Sonic 2's ending: art, maps, palettes, credits (s2_ending.py) */
#define TRI_BLOB_S2_WFZ_PALCYC  47  /* S2 CyclingPal_WFZFire (0), WFZBelt ($20), WFZ1 ($40), WFZ2 ($84) */
#define TRI_BLOB_S2_MTZ_TUBES   44  /* S2 Obj67 spin-tube paths (off_273F2: offsets, then count + x,y points) */
#define TRI_BLOB_S2_HTZ         40  /* Obj14 seesaw heights x2 (0, $62; a byte per pixel), Obj30 lava slope ($C4) */
/* S2 info block (TD_EXTRA when TD_GAME = S2) */
#define S2I_PILOT      0x18  /* u16 VRAM tile of the Tornado pilot (Tails); +2: (Sonic) */
#define S2I_ANIM       0x00  /* u32 -> animated art scripts (0 = none) */
#define S2I_WATER_PAL  0x04  /* u8 underwater palette id (0 = no water) */
#define S2I_BOSS_PAL   0x05  /* u8 boss palette id (PalLoad_Now at the arena, 0 = none) */
#define S2I_WATER_Y    0x06  /* u16 starting water height (WaterHeight) */
#define S2I_SLOT_TILE  0x08  /* u16 CNZ slot-machine pictures' first tile (3 x 16, 0 = none) */
#define S2I_SLOT_ART   0x0C  /* u32 -> slot pictures (6 x 16 tiles, recoloured for their line) */
#define S2I_HTZ_TILE   0x0A  /* u16 HTZ mountains (24 tiles) + clouds (8) first tile, 0 = none */
#define S2I_HTZ_CLIFFS 0x10  /* u32 -> ArtNem_HTZCliffs decoded (48 x 4 tiles), recoloured */
#define S2I_HTZ_CLOUDS 0x14  /* u32 -> ArtUnc_HTZClouds ($400 bytes), recoloured */
#define TRI_SLOT_ZONE           0x20  /* driver song slots borrowed for S1/S2 music */
#define TRI_SLOT_BOSS           0x21
#define TRI_PAL_BASE  0x80
#define TRI_PLC_BASE  0x80

/* ---- native pseudo code addresses --------------------------------------- */
#define TRILOGY_NATIVE_BASE  0x7F0000u
#define TRILOGY_NATIVE_MAX   0x400u

typedef void (*TriNativeFn)(void);
uint32_t tri_native(TriNativeFn fn);     /* register (idempotent) -> 68K address */

/* ---- 68K state helpers (layer-internal) ---------------------------------- */
#include "genesis_runtime.h"
#ifdef TRILOGY_INTERNAL
extern uint8_t g_ram[0x10000];

static inline uint8_t  r8 (uint32_t a) { return g_ram[a & 0xFFFF]; }
static inline uint16_t r16(uint32_t a) { a &= 0xFFFF; return (uint16_t)((g_ram[a] << 8) | g_ram[(a + 1) & 0xFFFF]); }
static inline uint32_t r32(uint32_t a) { return ((uint32_t)r16(a) << 16) | r16(a + 2); }
static inline void w8 (uint32_t a, uint8_t v)  { g_ram[a & 0xFFFF] = v; }
static inline void w16(uint32_t a, uint16_t v) { a &= 0xFFFF; g_ram[a] = (uint8_t)(v >> 8); g_ram[(a + 1) & 0xFFFF] = (uint8_t)v; }
static inline void w32(uint32_t a, uint32_t v) { w16(a, (uint16_t)(v >> 16)); w16(a + 2, (uint16_t)v); }

/* bank / any-address reads (ROM, bank, RAM) */
uint8_t  tri_rd8 (uint32_t a);
uint16_t tri_rd16(uint32_t a);
uint32_t tri_rd32(uint32_t a);

/* Call a recompiled routine exactly as a JSR does (push/pop return slot). */
void tri_jsr(uint32_t addr);
/* 68K RAM pointers as S3K builds them ($FFFFxxxx via .w addressing):
 * code comparing with cmpa.w relies on this sign extension. */
#define RAMPTR(a) (0xFFFF0000u | ((uint32_t)(a) & 0xFFFFu))
#define D(n) (g_cpu.D[n])
#define A(n) (g_cpu.A[n])
#endif /* TRILOGY_INTERNAL */

/* S3K object status table offsets */
#define OBJ_code        0x00
#define OBJ_render      0x04
#define OBJ_height_px   0x06
#define OBJ_width_px    0x07
#define OBJ_priority    0x08
#define OBJ_art_tile    0x0A
#define OBJ_mappings    0x0C
#define OBJ_x_pos       0x10
#define OBJ_y_pos       0x14
#define OBJ_x_vel       0x18
#define OBJ_y_vel       0x1A
#define OBJ_ground_vel  0x1C
#define OBJ_y_radius    0x1E
#define OBJ_x_radius    0x1F
#define OBJ_anim        0x20
#define OBJ_frame       0x22
#define OBJ_anim_frame  0x23
#define OBJ_anim_timer  0x24
#define OBJ_angle       0x26
#define OBJ_coll_flags  0x28
#define OBJ_coll_prop   0x29
#define OBJ_status      0x2A
#define OBJ_subtype     0x2C
#define OBJ_routine     0x05
#define OBJ_respawn     0x48
#define OBJ_top_solid   0x46
#define OBJ_lrb_solid   0x47
#define OBJ_SIZE        0x4A

/* ---- current-zone queries ----------------------------------------------- */
#ifdef TRILOGY_INTERNAL
static inline int tri_zone(void) { return r8(RAM_Current_zone); }
static inline int tri_act(void)  { return r8(RAM_Current_act); }
static inline int tri_active(void) { int z = tri_zone(); return z >= TRI_ZONE_BASE && z < TRI_ZONE_END; }
#endif
void tri_music_patch(int slot, int song);  /* trilogy_music.c */
void tri_music_play(int slot, int song);
void tri_music_play_s2(int slot, int song);     /* a Sonic 2 song wherever the game is */
void tri_music_zone(void);
void tri_music_restore(void);
uint32_t tri_dir(void);                   /* bank address of the current entry, 0 if none */
uint32_t tri_dir_for(int zone, int act);
int      tri_bank_loaded(void);

/* ---- module entry points ------------------------------------------------ */
void trilogy_init(void);
int  trilogy_instruction(uint32_t pc);    /* chained in front of other hooks */
int  trilogy_owns(uint32_t pc);           /* one of the trilogy's hook sites */
/* extended sprite line (trilogy_extpal.c) */
void tri_ext_tiles(uint32_t tile, uint32_t n, int on);
void tri_ext_level(void);
int  tri_ext_hook(uint32_t pc);
void tri_ext_frame(void);
void tri_refresh_fg(void);                /* redraw the FG plane after a layout change */
int  trilogy_dispatch(uint32_t addr);

int  tri_level_hook(uint32_t pc);         /* trilogy_level.c */
int  tri_screen_hook(uint32_t pc);        /* trilogy_screen.c */
int  tri_titlecard_hook(uint32_t pc);     /* trilogy_titlecard.c */
int  tri_flow_hook(uint32_t pc);          /* trilogy_flow.c */
int  tri_water_hook(uint32_t pc);         /* trilogy_water.c */
int  tri_menu_hook(uint32_t pc);          /* trilogy_menu.c */
void tri_ach_act_clear(uint16_t za);
void tri_vram_queue(uint32_t src, uint32_t vram, uint32_t len);   /* copy at the next V-int (trilogy_obj.c) */      /* trilogy_menu.c */
uint16_t tri_next_act(uint16_t za);
uint16_t tri_apparent(uint16_t za);
void tri_screen_register(void);
void tri_player_frame(void);             /* trilogy_player.c */
int  tri_obj_dispatch(uint32_t addr);     /* trilogy_obj.c */
void tri_obj_upload_art(void);
