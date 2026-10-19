/*
 * trilogy_s2ss.c — Sonic 2's half-pipe special stage, transcribed from
 * s2disasm (SpecialStage, SSTrack_Draw, Vint_S2SS, the stage objects
 * Obj09/10/5A/5B/5E/5F/59/60/61/63/87/88 and the results, Obj6F).
 *
 * It runs as a native scene off S3K's GameLoop (TH_GAMELOOP), like the S2
 * ending: S2's object table ($40-byte SSTs, so the objects transcribe offset
 * for offset), S2's BuildSprites over the converted mappings, and the stage's
 * own VBlank work (track PNT transfer, animation timers, plane flip) done
 * right after S3K's VInt_6 has sent the palette, sprite table and V-scroll.
 * The VDP is set up exactly as S2 sets it (H32, shadow/highlight, 128x32
 * planes at $C000/$8000/$A000, H-scroll at $FC00); VInt_6 also sends its
 * H-scroll RAM to $F000, where this stage keeps star art, so that RAM holds
 * those very tiles.
 *
 * All data is Sonic 2's (TRI_BLOB_S2_SS, tools/trilogy/s2_special.py); the
 * track is decoded every frame from the cartridge's own bitstreams.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "genesis_machine.h"
#include "video/genesis_vdp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* blob items (s2_special.ITEMS) */
enum {
    SS_TRACK, SS_TRACKOFS, SS_UNCLUT, SS_RLELUT, SS_TRACKART, SS_PERSP, SS_LAYOUT, SS_OBJLOC,
    SS_ENI_BACK, SS_ENI_BACKBOTTOM,
    SS_ART_EMERALD, SS_ART_MESSAGES, SS_ART_HUD, SS_ART_FLATSHADOW, SS_ART_DIAGSHADOW, SS_ART_SIDESHADOW,
    SS_ART_EXPLOSION, SS_ART_RINGS, SS_ART_START, SS_ART_BACK, SS_ART_STARS, SS_ART_TAILSTEXT,
    SS_ART_BOMB, SS_ART_PLAYERS,
    SS_PAL, SS_PAL_EMERALD,
    SS_MAP_09, SS_MAP_10, SS_MAP_63, SS_MAP_88, SS_MAP_61, SS_MAP_RINGS, SS_MAP_5A, SS_MAP_59, SS_MAP_5E,
    SS_MAP_5F_A, SS_MAP_5F_B,
    SS_DPLC, SS_ANI_09, SS_ANI_10, SS_ANI_88, SS_ANI_61, SS_ANI_60, SS_ANI_59, SS_5F_FRAME2,
};

#define GM_SCENE        0x10
#define UNC_PART2       0x40          /* (SSPNT_UncLUT_Part2-SSPNT_UncLUT)/2 */
#define RLE_PART2       0x40          /* (SSPNT_RLELUT_Part2-SSPNT_RLELUT)/4 */
#define PNT_A1          0xC000u       /* VRAM_SS_Plane_A_Name_Table1 */
#define PNT_A2          0x8000u       /* VRAM_SS_Plane_A_Name_Table2 */
#define PNT_B           0xA000u       /* VRAM_SS_Plane_B_Name_Table */
#define HSCROLL         0xFC00u       /* VRAM_Horiz_Scroll_Table */
#define PNT_BUF_LEN     0x700         /* PNT_Buffer_End-PNT_Buffer */

/* ArtTile_ArtNem_* (s2.constants.asm) */
#define AT_EMERALD      0x174
#define AT_MESSAGES     0x1A2
#define AT_HUD          0x1FA
#define AT_FLATSHADOW   0x23C
#define AT_DIAGSHADOW   0x262
#define AT_SIDESHADOW   0x29C
#define AT_EXPLOSION    0x2B5
#define AT_SONIC        0x2E5
#define AT_TAILS        0x300
#define AT_TAILS_TAILS  0x316
#define AT_RINGS        0x322
#define AT_START        0x38A
#define AT_BOMB         0x38A
#define AT_BACK         0x700
#define AT_STARS        0x77F
#define AT_TAILSTEXT    0x7A4
#define PAL1            0x2000
#define PAL2            0x4000
#define PAL3            0x6000

/* S2 sound ids -> the S3K driver's */
#define SFX_JUMP        0xA0          /* SndID_Jump */
#define SFX_RING        0xB5          /* SndID_Ring */
#define SFX_RINGSPILL   0xC6          /* SndID_RingSpill */
#define SFX_CHECKPOINT  0xA1          /* SndID_Checkpoint */
#define SFX_ERROR       0xED          /* SndID_Error */
#define SFX_SLOWSMASH   0xCB          /* SndID_SlowSmash */
#define SFX_SSENTRY     0xCA          /* SndID_SpecStageEntry */
#define MUS_SPECSTAGE   0x92
#define MUS_ENDLEVEL    0x9A
#define MUS_EMERALD     0x9D

static uint32_t item(int i, uint32_t *len)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t b = t ? tri_rd32(t + TRI_BLOB_S2_SS * 4u) : 0;
    if (!b) { if (len) *len = 0; return 0; }
    if (len) *len = tri_rd32(b + (uint32_t)i * 8u + 4u);
    return b + tri_rd32(b + (uint32_t)i * 8u);
}

/* ---- VDP ---------------------------------------------------------------- */
static uint8_t *vram(void) { return g_machine.vdp.vram; }
static void vram_w16(uint32_t a, uint16_t v)
{
    vram()[a & 0xFFFF] = (uint8_t)(v >> 8);
    vram()[(a + 1) & 0xFFFF] = (uint8_t)v;
}
static void vram_fill(uint32_t a, uint32_t n) { for (uint32_t k = 0; k < n; k++) vram()[(a + k) & 0xFFFF] = 0; }
static void load_art(int it, uint16_t tile)
{
    uint32_t n, src = item(it, &n);
    for (uint32_t k = 0; k < n; k++) vram()[((uint32_t)tile * 32u + k) & 0xFFFF] = tri_rd8(src + k);
}
static void set_reg(int r, uint8_t v) { g_machine.vdp.reg[r] = v; }
static void tri_music_fade(void) { D(0) = 0xE1; tri_jsr(S3K_Play_Music); }   /* MusID_FadeOut -> cmd_FadeOut */

/* ========================================================================
 * Stage RAM (s2.constants.asm SS_Shared_RAM and friends)
 * ====================================================================== */
static int s_active, s_phase, s_timer;
static uint8_t s_saved_regs[24];
static uint8_t Current_Special_Stage, Current_Special_Act;
static uint16_t Ring_count, Ring_count_2P;
static uint8_t Perfect_rings_flag, Got_Emerald, Emerald_count;
static uint16_t SS_Perfect_rings_left, SS_Ring_Requirement, SS_RingsToGoBCD, SS_NoRingsTogoLifetime;
static uint8_t SS_HideRingsToGo, SS_TriggerRingsToGo, SS_NoCheckpoint_flag, SS_NoCheckpointMsg_flag;
static uint8_t SS_Checkpoint_Rainbow_flag, SS_Rainbow_palette, SS_Star_color_1, SS_Star_color_2;
static uint8_t SS_Pause_Only_flag, SS_Check_Rings_flag, SpecialStage_Started, SS_Swap_Positions_Flag;
static uint16_t Tails_control_counter;
static uint16_t SS_Ctrl_Record_Buf[16];
static uint8_t Sonic_LastLoadedDPLC, Tails_LastLoadedDPLC, TailsTails_LastLoadedDPLC;
static uint16_t SS_Offset_X, SS_Offset_Y;
static uint32_t SS_New_Speed_Factor, SS_Cur_Speed_Factor;
static uint8_t SSTrack_anim, SSTrack_last_anim_frame, SpecialStage_CurrentSegment, SSTrack_anim_frame;
static uint8_t SS_Alternate_PNT, SSTrack_drawing_index, SSTrack_Orientation, SS_Alternate_HorizScroll_Buf;
static uint8_t SSTrack_mapping_frame, SS_Last_Alternate_HorizScroll_Buf, SSTrack_duration_timer;
static uint8_t SS_player_anim_frame_timer, SpecialStage_LastSegment, SpecialStage_LastSegment2;
static uint8_t SSTrack_last_mapping_frame;
static int SSTrack_last_mappings, SSTrack_last_mappings_copy;     /* (frames whose data the pointers held) */
static uint32_t SSTrack_mappings_bitflags, SSTrack_mappings_uncompressed, SSTrack_mappings_RLE;
static uint16_t SSDrawRegBuffer[6];                               /* d2-d7 */
static uint16_t Vscroll_Factor_FG, Vscroll_Factor_BG, SSTrack_LastVScroll;
static uint32_t SS_CurrentPerspective, SS_CurrentLevelLayout, SS_CurrentLevelObjectLocations;
static uint32_t SS_Horiz_Scroll_Buf_1[224], SS_Horiz_Scroll_Buf_2[224];
static uint16_t PNT_Buffer[PNT_BUF_LEN / 2];
static uint32_t Vint_runcount;
static uint16_t Ctrl_1, Ctrl_2, Ctrl_1_Logical, Ctrl_2_Logical;   /* held<<8 | pressed */
static uint16_t s_player_mode;
static int s_super;                                               /* awarding a Super Emerald */

#define Ctrl_1_Held_Logical   ((uint8_t)(Ctrl_1_Logical >> 8))
#define Ctrl_1_Press_Logical  ((uint8_t)Ctrl_1_Logical)
#define Ctrl_2_Held_Logical   ((uint8_t)(Ctrl_2_Logical >> 8))
#define Ctrl_2_Press_Logical  ((uint8_t)Ctrl_2_Logical)
enum { BTN_UP = 0x01, BTN_DOWN = 0x02, BTN_LEFT = 0x04, BTN_RIGHT = 0x08, BTN_B = 0x10, BTN_C = 0x20,
       BTN_A = 0x40, BTN_START = 0x80 };

/* ---- palettes (S2's fades on S3K's Normal_palette / Target_palette) ------ */
static uint16_t pal(int i) { return r16(RAM_Normal_palette + (uint32_t)i * 2u); }
static void set_pal(int i, uint16_t c) { w16(RAM_Normal_palette + (uint32_t)i * 2u, c); }

static void fade_to_white_step(void)                 /* Pal_FadeToWhite .UpdateAllColours */
{
    for (int i = 0; i < 64; i++) {
        uint16_t d2 = pal(i);
        if (d2 == 0xEEE) continue;
        if ((d2 & 0x00E) != 0x00E) set_pal(i, (uint16_t)(d2 + 2));
        else if ((d2 & 0x0E0) != 0x0E0) set_pal(i, (uint16_t)(d2 + 0x20));
        else if ((d2 & 0xE00) != 0xE00) set_pal(i, (uint16_t)(d2 + 0x200));
    }
}

static void fade_from_white_step(void)               /* Pal_FadeFromWhite .UpdateAllColours */
{
    for (int i = 0; i < 64; i++) {
        uint16_t d2 = r16(RAM_Target_palette + (uint32_t)i * 2u), d3 = pal(i);
        if (d2 == d3) continue;
        if (d3 >= 0x200 && (uint16_t)(d3 - 0x200) >= d2) { set_pal(i, (uint16_t)(d3 - 0x200)); continue; }   /* blue */
        if (d3 >= 0x20 && (uint16_t)(d3 - 0x20) >= d2) { set_pal(i, (uint16_t)(d3 - 0x20)); continue; }      /* green */
        set_pal(i, (uint16_t)(d3 - 2));                                                                       /* red */
    }
}

/* ========================================================================
 * Objects: S2's SST layout ($40 bytes), slots as in the stage's Object_RAM
 * ====================================================================== */
#define NOBJ 127
static uint8_t s_o[NOBJ][0x40];
enum { O_id = 0, O_render = 1, O_art = 2, O_map = 4, O_x = 8, O_xsub = 0xA, O_y = 0x0C, O_ysub = 0xE,
       O_xv = 0x10, O_yv = 0x12, O_inertia = 0x14, O_yrad = 0x16, O_xrad = 0x17, O_prio = 0x18, O_width = 0x19,
       O_frame = 0x1A, O_aframe = 0x1B, O_anim = 0x1C, O_prev = 0x1D, O_atime = 0x1E, O_coll = 0x20,
       O_cprop = 0x21, O_status = 0x22, O_23 = 0x23, O_rout = 0x24, O_rout2 = 0x25, O_angle = 0x26 };
/* special stage player fields */
enum { ss_dplc_timer = 0x23, ss_x_pos = 0x2A, ss_y_pos = 0x2E, ss_y_sub = 0x30, ss_init_flip_timer = 0x32,
       ss_flip_timer = 0x33, ss_z_pos = 0x34, ss_hurt_timer = 0x36, ss_slide_timer = 0x37, ss_parent = 0x38,
       ss_rings_base = 0x3C, ss_rings_hundreds = 0x3C, ss_rings_tens = 0x3D, ss_rings_units = 0x3E,
       ss_last_angle_index = 0x3F };
/* multi-sprite fields */
enum { mainspr_mapframe = 0x0B, mainspr_width = 0x0E, mainspr_childsprites = 0x0F, mainspr_height = 0x14,
       subspr_data = 0x10, next_subspr = 6 };
#define SUB_X(k)    (subspr_data + next_subspr * (k))
#define SUB_Y(k)    (subspr_data + next_subspr * (k) + 2)
#define SUB_F(k)    (subspr_data + next_subspr * (k) + 5)
enum { ST_SS_JUMPING = 1 << 2, ST_SS_SLOWING = 1 << 6, ST_NO_BALANCING = 1 << 7 };
enum { RF_XFLIP = 1, RF_YFLIP = 2, RF_LEVEL_FG = 4, RF_MULTI = 0x40, RF_ONSCREEN = 0x80 };
/* object ids */
enum { ID_SONICSS = 0x09, ID_TAILSSS = 0x10, ID_SSEMERALD = 0x59, ID_SSMESSAGE = 0x5A, ID_SSRINGSPILL = 0x5B,
       ID_SSHUD = 0x5E, ID_STARTBANNER = 0x5F, ID_SSRING = 0x60, ID_SSBOMB = 0x61, ID_SSSHADOW = 0x63,
       ID_SSRESULTS = 0x6F, ID_SSNUMBEROFRINGS = 0x87, ID_SSTAILSTAILS = 0x88 };
/* slots */
enum { MainCharacter = 0, Sidekick = 1, SpecialStageHUD = 2, SpecialStageStartBanner = 3,
       SpecialStageNumberOfRings = 4, SpecialStageShadow_Sonic = 5, SpecialStageShadow_Tails = 6,
       SpecialStageTails_Tails = 7, SS_Dynamic_Object_RAM = 8, SpecialStageResults = 8 + 0x18 };

#define B(o, f)        s_o[o][f]
static uint16_t W(int o, int f) { return (uint16_t)(s_o[o][f] << 8 | s_o[o][f + 1]); }
static void SW(int o, int f, int v) { s_o[o][f] = (uint8_t)(v >> 8); s_o[o][f + 1] = (uint8_t)v; }
static int16_t SWs(int o, int f) { return (int16_t)W(o, f); }
static uint32_t L(int o, int f) { return (uint32_t)W(o, f) << 16 | W(o, f + 2); }
static void SL(int o, int f, uint32_t v) { SW(o, f, (int)(v >> 16)); SW(o, f + 2, (int)v); }
/* object pointers (ss_parent, objoff_34) hold slot+1; 0 is none */
static void SP(int o, int f, int p) { SL(o, f, p < 0 ? 0u : (uint32_t)p + 1u); }
static int GP(int o, int f) { return (int)L(o, f) - 1; }

static void delete_obj(int o) { memset(s_o[o], 0, 0x40); }   /* DeleteObject */

static int ss_alloc(void)                            /* SSAllocateObject */
{
    for (int k = SS_Dynamic_Object_RAM; k < NOBJ; k++) if (!B(k, O_id)) return k;
    return -1;
}

static int ss_alloc_after(int a0)                    /* SSAllocateObjectAfterCurrent */
{
    for (int k = a0; k < NOBJ; k++) if (!B(k, O_id)) return k;
    return -1;
}

static void object_move(int o)                       /* ObjectMove */
{
    SL(o, O_x, L(o, O_x) + (uint32_t)((int32_t)SWs(o, O_xv) << 8));
    SL(o, O_y, L(o, O_y) + (uint32_t)((int32_t)SWs(o, O_yv) << 8));
}

static void object_move_and_fall(int o)              /* ObjectMoveAndFall */
{
    int32_t yv = SWs(o, O_yv);
    SW(o, O_yv, yv + 0x38);
    SL(o, O_x, L(o, O_x) + (uint32_t)((int32_t)SWs(o, O_xv) << 8));
    SL(o, O_y, L(o, O_y) + (uint32_t)(yv << 8));
}

static void calc_sine(uint8_t a, int16_t *d0, int16_t *d1) { o_sincos(a, d0, d1); }   /* CalcSine: d0 sin, d1 cos */

/* AnimateSprite (S2) */
static void animate(int o, int it)
{
    uint32_t ani = item(it, NULL);
    uint8_t d0 = B(o, O_anim);
    if (d0 != B(o, O_prev)) { B(o, O_prev) = d0; B(o, O_aframe) = 0; B(o, O_atime) = 0; }
    if ((int8_t)--B(o, O_atime) >= 0) return;
    uint32_t a1 = ani + tri_rd16(ani + d0 * 2u);
    B(o, O_atime) = tri_rd8(a1);
    uint8_t f = tri_rd8(a1 + 1 + B(o, O_aframe));
    if (f & 0x80) {
        switch (f) {
        case 0xFF: B(o, O_aframe) = 0; f = tri_rd8(a1 + 1); break;
        case 0xFE: B(o, O_aframe) = (uint8_t)(B(o, O_aframe) - tri_rd8(a1 + 2 + B(o, O_aframe))); f = tri_rd8(a1 + 1 + B(o, O_aframe)); break;
        case 0xFD: B(o, O_anim) = tri_rd8(a1 + 2 + B(o, O_aframe)); return;
        case 0xFC: B(o, O_rout) += 2; B(o, O_atime) = 0; B(o, O_aframe)++; return;
        case 0xFB: B(o, O_aframe) = 0; B(o, O_rout2) = 0; return;
        case 0xFA: B(o, O_rout2) += 2; return;
        default: return;
        }
    }
    B(o, O_frame) = f & 0x7F;
    B(o, O_render) = (uint8_t)((B(o, O_render) & 0xFC) | (B(o, O_status) & 3));
    B(o, O_aframe)++;
}

/* ---- display lists + BuildSprites --------------------------------------- */
static uint8_t s_disp[8][NOBJ];
static int s_ndisp[8];
static void display(int o)                           /* DisplaySprite: list = priority & 7 */
{
    int p = B(o, O_prio) & 7;
    if (s_ndisp[p] < 0x3F) s_disp[p][s_ndisp[p]++] = (uint8_t)o;
}

static int s_nspr;
static void draw_frame(uint32_t maps, int frame, int x, int y, uint16_t art, uint8_t render)   /* DrawSprite */
{
    uint32_t f = maps + tri_rd16(maps + (uint32_t)frame * 2u);
    int n = tri_rd16(f);
    for (int i = 0; i < n && s_nspr < 80; i++) {
        uint32_t p = f + 2 + (uint32_t)i * 6u;
        int py = (int8_t)tri_rd8(p), size = tri_rd8(p + 1);
        uint16_t tile = tri_rd16(p + 2);
        int px = (int16_t)tri_rd16(p + 4);
        int w = ((size >> 2) & 3) + 1, h = (size & 3) + 1;
        if (render & RF_XFLIP) { px = -px - w * 8; tile ^= 0x800; }
        if (render & RF_YFLIP) { py = -py - h * 8; tile ^= 0x1000; }
        tile = (uint16_t)(tile + art);
        uint16_t sx = (uint16_t)((x + px) & 0x1FF);
        if (!sx) sx = 1;                                 /* (x 0 would mask the line) */
        uint32_t e = RAM_Sprite_table + (uint32_t)s_nspr * 8u;
        w16(e, (uint16_t)(y + py));
        w8(e + 2, (uint8_t)size);
        w8(e + 3, (uint8_t)(s_nspr + 1));
        w16(e + 4, tile);
        w16(e + 6, sx);
        s_nspr++;
    }
}

static void build_sprites(void)                      /* BuildSprites (camera at 0,0) */
{
    s_nspr = 0;
    for (int p = 0; p < 8; p++) {
        for (int k = 0; k < s_ndisp[p]; k++) {
            int o = s_disp[p][k];
            if (!B(o, O_id)) continue;
            B(o, O_render) &= (uint8_t)~RF_ONSCREEN;
            uint8_t d4 = B(o, O_render);
            uint32_t maps = item((int)L(o, O_map), NULL);
            if (d4 & RF_MULTI) {                         /* BuildSprites_MultiDraw */
                int d3 = SWs(o, O_x), w = B(o, mainspr_width);
                if (d3 + w < 0 || d3 - w >= 320) continue;
                int d2 = (SWs(o, O_y) + 128) & 0x7FF;
                if (d2 < 128 - 32 || d2 >= 128 + 224 + 32) continue;
                if (B(o, mainspr_mapframe))
                    draw_frame(maps, B(o, mainspr_mapframe), d3 + 128, d2, W(o, O_art), d4);
                B(o, O_render) |= RF_ONSCREEN;
                int n = B(o, mainspr_childsprites);
                for (int c = 0; c < n; c++)
                    draw_frame(maps, B(o, SUB_F(c)), SWs(o, SUB_X(c)) + 128, (SWs(o, SUB_Y(c)) + 128) & 0x7FF, W(o, O_art), d4);
                continue;
            }
            int d3 = SWs(o, O_x), w = B(o, O_width);
            if (d4 & (RF_LEVEL_FG | 8)) {
                if (d3 + w < 0 || d3 - w >= 320) continue;
                d3 += 128;
                int d2 = (SWs(o, O_y) + 128) & 0x7FF;
                if (d2 < 128 - 32 || d2 >= 128 + 224 + 32) continue;
                draw_frame(maps, B(o, O_frame), d3, d2, W(o, O_art), d4);
            } else {
                draw_frame(maps, B(o, O_frame), SWs(o, O_x), SWs(o, O_xsub), W(o, O_art), d4);   /* x_pixel / y_pixel */
            }
            B(o, O_render) |= RF_ONSCREEN;
        }
        s_ndisp[p] = 0;
    }
    if (s_nspr) w8(RAM_Sprite_table + (uint32_t)(s_nspr - 1) * 8u + 3u, 0);
    else { w32(RAM_Sprite_table, 0); w32(RAM_Sprite_table + 4, 0); }
}

/* ========================================================================
 * The track (SSTrack_Draw), its VBlank half (Vint_S2SS) and scrolling
 * ====================================================================== */
#define SS_BGOFS (SS_5F_FRAME2 + 1)

/* Ani_SpecialStageTrack / Ani_SSTrack_Len */
static const uint8_t k_ani_turn_rise[] = { 0x26,0x27,0x28,0x29,0x2A,0x2B,0x26, 0,1,2,3,4,5,6,7,8,9,0xA,0xB,0xC,0xD,0xE,0xF,0x10 };
static const uint8_t k_ani_turn_drop[] = { 0x26,0x27,0x28,0x29,0x2A,0x2B,0x26, 0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25 };
static const uint8_t k_ani_turn_straight[] = { 0x26,0x27,0x28,0x29,0x2A,0x2B,0x26, 0x2C,0x2D,0x2E,0x2F,0x30 };
static const uint8_t k_ani_straight[] = { 0x11,0x12,0x13,0x14,0x11,0x12,0x13,0x14, 0x11,0x12,0x13,0x14,0x11,0x12,0x13,0x14 };
static const uint8_t k_ani_straight_turn[] = { 0x11,0x12,0x13,0x14, 0x31,0x32,0x33,0x34,0x35,0x36,0x37 };
static const uint8_t *const k_track_ani[5] = { k_ani_turn_rise, k_ani_turn_drop, k_ani_turn_straight, k_ani_straight, k_ani_straight_turn };
static const uint8_t k_track_len[6] = { 24, 24, 12, 16, 11, 0 };
/* SSAnim_Base_Duration */
static const uint8_t k_base_duration[8] = { 60, 30, 15, 10, 8, 6, 5, 0 };
enum { MS_RISE14 = 0x0D, MS_STRAIGHT2 = 0x12, MS_STRAIGHT4 = 0x14, MS_DROP1 = 0x15, MS_DROP6 = 0x1A };

static uint8_t layout(int seg) { return tri_rd8(SS_CurrentLevelLayout + (uint32_t)seg); }
static uint32_t track_frame(int f) { return item(SS_TRACK, NULL) + tri_rd32(item(SS_TRACKOFS, NULL) + (uint32_t)f * 4u); }

/* the decoder's bit buffers: byte (d2/d4/d6) + bits left (d3/d5/d7), MSB first */
typedef struct { uint32_t a; uint8_t byte; int cnt; } Bits;
static int bit1(Bits *s)
{
    if (--s->cnt < 0) { s->byte = tri_rd8(s->a++); s->cnt = 7; }
    int b = s->byte >> 7;
    s->byte = (uint8_t)(s->byte << 1);
    return b;
}
static int bitsn(Bits *s, int n)                     /* the Read<n>_Got<k> handlers */
{
    int v = 0;
    while (n--) {
        if (!s->cnt) { s->byte = tri_rd8(s->a++); s->cnt = 8; }
        v = v << 1 | s->byte >> 7;
        s->byte = (uint8_t)(s->byte << 1);
        s->cnt--;
    }
    return v;
}

static void SSTrackSetOrientation(void)
{
    SS_Last_Alternate_HorizScroll_Buf = SS_Alternate_HorizScroll_Buf;
    int f = SSTrack_mapping_frame;                   /* (the frame the bit-flags pointer is in) */
    int flipped;
    if (f == MS_STRAIGHT2)      flipped = (layout(SpecialStage_CurrentSegment) & 0x80) != 0;
    else if (f == MS_RISE14)    flipped = (layout(SpecialStage_CurrentSegment) & 0x80) != 0;
    else if (f == MS_DROP6)     flipped = (layout(SpecialStage_CurrentSegment) & 0x80) != 0;
    else return;
    SSTrack_Orientation = flipped ? 0xFF : 0;
    if ((int8_t)SSTrack_drawing_index < (int8_t)SS_player_anim_frame_timer) return;
    SS_Alternate_HorizScroll_Buf = flipped ? 0xFF : 0;
}

static int s_cost_unc, s_cost_rle, s_cost_words, s_cost_flip, s_cost_objs;   /* the pass's work (lag model) */
static void SSTrack_Draw(void)
{
    int d0 = SSTrack_drawing_index;
    if (d0 >= 4) { SSTrackSetOrientation(); return; }
    if (d0 == 0) {                                   /* a new track frame */
        SSTrack_last_mappings_copy = SSTrack_last_mappings;
        SSTrack_last_mapping_frame = SSTrack_mapping_frame;
        int d3 = layout(SpecialStage_CurrentSegment) & 0x7F;
        SSTrack_anim = (uint8_t)d3;
        int d4 = k_track_ani[d3 < 5 ? d3 : 3][SSTrack_anim_frame];
        SSTrack_mapping_frame = (uint8_t)d4;
        uint32_t a0 = track_frame(d4);
        uint32_t a1 = a0 + tri_rd16(a0 + 2) + 4u;
        uint32_t a2 = a1 + tri_rd16(a1 + 2) + 4u;
        SSTrack_mappings_bitflags = a0 + 4;
        SSTrack_last_mappings = d4;
        SSTrack_mappings_uncompressed = a1 + 4;
        SSTrack_mappings_RLE = a2 + 4;
        SSTrack_last_anim_frame = SSTrack_anim_frame;
        if (++SSTrack_anim_frame == k_track_len[d3 < 6 ? d3 : 5]) {
            SSTrack_anim_frame = 0;
            SpecialStage_LastSegment = SpecialStage_CurrentSegment;
            SpecialStage_CurrentSegment++;
        }
    }
    /* SSTrack_BeginDraw */
    int q = d0 + (SS_Alternate_PNT ? 4 : 0);
    uint32_t dst = (q < 4 ? PNT_A2 : PNT_A1) + (uint32_t)(q & 3) * PNT_BUF_LEN;
    SSTrackSetOrientation();
    Bits F = { SSTrack_mappings_bitflags, (uint8_t)SSDrawRegBuffer[4], (int16_t)SSDrawRegBuffer[5] };
    Bits U = { SSTrack_mappings_uncompressed, (uint8_t)SSDrawRegBuffer[2], (int8_t)SSDrawRegBuffer[3] };
    Bits R = { SSTrack_mappings_RLE, (uint8_t)SSDrawRegBuffer[0], (int8_t)SSDrawRegBuffer[1] };
    uint32_t unc = item(SS_UNCLUT, NULL), rle = item(SS_RLELUT, NULL);
    int flip = SSTrack_Orientation != 0;
    s_cost_flip = flip;
    for (int line = 0; line < 7; line++) {
        int pos = flip ? (line + 1) * 128 : 0;           /* (flipped: from the line's end, backwards) */
        for (;;) {
            if (bit1(&F)) {                              /* a single tile (uncompressed) */
                uint16_t w;
                if (bit1(&U)) w = tri_rd16(unc + (uint32_t)(UNC_PART2 + bitsn(&U, 10)) * 2u);
                else          w = tri_rd16(unc + (uint32_t)bitsn(&U, 6) * 2u);
                s_cost_unc++;
                if (flip) PNT_Buffer[--pos] = (uint16_t)(w ^ 0x6800);       /* flip_x|palette_line_3 */
                else { vram_w16(dst, (uint16_t)(w | PAL3)); dst += 2; }
                continue;
            }
            if (--R.cnt < 0) {                           /* SSTrackDrawRLE: a new byte; $FF ends the line */
                uint8_t b = tri_rd8(R.a++);
                if (b == 0xFF) { R.cnt = 0; break; }
                R.byte = b;
                R.cnt = 7;
            }
            int big = R.byte >> 7;
            R.byte = (uint8_t)(R.byte << 1);
            uint32_t e;
            if (big) {
                int i = bitsn(&R, 7);
                if (i == 0x7F) break;
                e = rle + (uint32_t)(RLE_PART2 + i) * 4u;
            } else {
                e = rle + (uint32_t)bitsn(&R, 6) * 4u;
            }
            uint16_t w = (uint16_t)(tri_rd16(e) | 0xE000), n = tri_rd16(e + 2);
            s_cost_rle++;
            s_cost_words += n + 1;
            for (int k = 0; k <= n; k++) {
                if (flip) { if (pos > line * 128) PNT_Buffer[--pos] = w; }
                else { vram_w16(dst, w); dst += 2; }
            }
        }
    }
    /* SSTrackDraw_return */
    if (SSTrack_drawing_index == 3) {
        memset(SSDrawRegBuffer, 0, sizeof SSDrawRegBuffer);
        return;
    }
    SSTrack_mappings_bitflags = F.a;
    SSTrack_mappings_uncompressed = U.a;
    SSTrack_mappings_RLE = R.a;
    SSDrawRegBuffer[0] = R.byte; SSDrawRegBuffer[1] = (uint16_t)R.cnt;
    SSDrawRegBuffer[2] = U.byte; SSDrawRegBuffer[3] = (uint16_t)U.cnt;
    SSDrawRegBuffer[4] = F.byte; SSDrawRegBuffer[5] = (uint16_t)F.cnt;
}

static int SSRun_Animation_Timers(void)              /* -> d1 */
{
    if ((SS_Cur_Speed_Factor >> 16) != (SS_New_Speed_Factor >> 16)) {
        SS_Cur_Speed_Factor = SS_New_Speed_Factor;
        SSTrack_duration_timer = 0;
    }
    if ((int8_t)--SSTrack_duration_timer > 0)
        return (uint8_t)(SS_player_anim_frame_timer + 1);
    uint8_t d1 = k_base_duration[((SS_Cur_Speed_Factor >> 16) >> 1) & 7];
    SS_player_anim_frame_timer = d1;
    SSTrack_duration_timer = d1;
    SS_player_anim_frame_timer--;
    return d1;
}

/* Vint_S2SS, after S3K's VInt_6 (palette, sprites, V-scroll): H-scroll, the
 * flipped track's PNT transfer, the timers and the plane flip */
static void vint_s2ss(void)
{
    const uint32_t *hs = SS_Alternate_HorizScroll_Buf ? SS_Horiz_Scroll_Buf_2 : SS_Horiz_Scroll_Buf_1;
    for (int l = 0; l < 224; l++) {
        vram_w16(HSCROLL + (uint32_t)l * 4u, (uint16_t)(hs[l] >> 16));
        vram_w16(HSCROLL + (uint32_t)l * 4u + 2u, (uint16_t)hs[l]);
    }
    if (SSTrack_Orientation && SSTrack_drawing_index < 4) {
        uint32_t d = (SS_Alternate_PNT ? PNT_A1 : PNT_A2) + (uint32_t)SSTrack_drawing_index * PNT_BUF_LEN;
        for (int k = 0; k < PNT_BUF_LEN / 2; k++) vram_w16(d + (uint32_t)k * 2u, PNT_Buffer[k]);
    }
    int d1 = SSRun_Animation_Timers();
    SSTrack_drawing_index++;
    if ((int8_t)SSTrack_drawing_index < (int8_t)d1) return;
    SSTrack_drawing_index = 0;
    set_reg(2, (uint8_t)((SS_Alternate_PNT ? PNT_A1 : PNT_A2) >> 10));
    SS_Alternate_PNT ^= 1;
}

static void vint_plain(void)                         /* the other VInts: H-scroll buffer 1 */
{
    for (int l = 0; l < 224; l++) {
        vram_w16(HSCROLL + (uint32_t)l * 4u, (uint16_t)(SS_Horiz_Scroll_Buf_1[l] >> 16));
        vram_w16(HSCROLL + (uint32_t)l * 4u + 2u, (uint16_t)SS_Horiz_Scroll_Buf_1[l]);
    }
}

static uint8_t bgofs(int d1, int d0)                 /* off_6DEE(d1)[d0] */
{
    uint32_t a0 = item(SS_BGOFS, NULL);
    return tri_rd8(a0 + tri_rd16(a0 + (uint32_t)d1) + (uint32_t)d0);
}

static void SSPlaneB_SetHorizOffset(void)
{
    int d2 = (int8_t)SSTrack_last_anim_frame, d1;
    if (SSTrack_anim > 2) return;
    if (d2 < 1) d1 = 0;
    else if (d2 < 2) d1 = 2;
    else if (d2 < 0xA) d1 = 4;
    else if (d2 < 0xB) d1 = 2;
    else if (d2 < 0xC) d1 = 0;
    else return;
    int16_t off = bgofs(d1, SSTrack_drawing_index);
    uint32_t *a1;
    if (SS_Last_Alternate_HorizScroll_Buf) {
        if (SS_Alternate_HorizScroll_Buf) { a1 = SS_Horiz_Scroll_Buf_2; off = (int16_t)-off; }
        else a1 = SS_Horiz_Scroll_Buf_1;
    } else if (SS_Alternate_HorizScroll_Buf) {
        a1 = SS_Horiz_Scroll_Buf_2; off = (int16_t)-off;
    } else {
        a1 = SS_Horiz_Scroll_Buf_1;
    }
    for (int l = 0; l < 224; l++)
        a1[l] = (a1[l] & 0xFFFF0000u) | (uint16_t)((uint16_t)a1[l] - (uint16_t)off);
}

static const int8_t k_vs_rise[24] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 8,8,2,4,4,4,4,4,4,0xA,0xC,0xE,0x12,0x10 };
static const int8_t k_vs_drop[24] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0x10,0x12,0xE,0xC,0xA,4,4,4,4,4,4,2,0 };
static const int8_t k_vs_straight[16] = { 6,6,0x14,0x14, 6,6,0x14,0x14, 6,6,0x14,0x14, 6,6,0x14,0x14 };

static void SSTrack_SetVscroll(void)
{
    SSTrack_LastVScroll = Vscroll_Factor_BG;
    int up = 0, d1;
    int d2 = SSTrack_last_anim_frame;
    switch (SSTrack_anim) {
    case 0: d1 = k_vs_rise[d2 % 24]; break;
    case 1: up = 1; d1 = k_vs_drop[d2 % 24]; break;
    case 3: if (SS_Pause_Only_flag) return; d1 = k_vs_straight[d2 & 15]; break;
    default: return;
    }
    if (d1 < 0) return;
    uint8_t d = bgofs(d1, SSTrack_drawing_index);    /* SSTrack_ApplyVscroll */
    if (up) Vscroll_Factor_BG = (uint16_t)(Vscroll_Factor_BG + d);
    else Vscroll_Factor_BG = (uint16_t)(Vscroll_Factor_BG - d);
}

static void SS_ScrollBG(void) { SSPlaneB_SetHorizOffset(); SSTrack_SetVscroll(); }

static void SSLoadCurrentPerspective(void)
{
    if (SSTrack_drawing_index != 4) return;
    uint32_t a0 = item(SS_PERSP, NULL);
    SS_CurrentPerspective = a0 + tri_rd16(a0 + (uint32_t)SSTrack_mapping_frame * 2u);
}

/* SSCurveOffsets (x, y) per track frame */
static const int8_t k_curve[0x38][2] = {
    {0x13,0},{0x13,0},{0x13,0},{0x13,0}, {9,-0xA},{0,-0x1C},{0,-0x1C},{0,-0x20}, {0,-0x24},{0,-0x2A},{0,-0x10},{0,6},
    {0,0xE},{0,0x10},{0,0x12},{0,0x12}, {9,0x12}, {0,0},{0,0},{0,0},{0,0},
    {0x13,0},{0x13,0},{0x13,0},{0x13,0}, {0xB,0xC},{0,0xC},{0,0x12},{0,0xA}, {0,8},{0,2},{0,0x10},{0,-0x20},
    {0,-0x1F},{0,-0x1E},{0,-0x1B},{0,-0x18}, {0,-0xE},
    {0x13,0},{0x13,0},{0x13,0},{0x13,0},{0x13,0},{0x13,0},
    {0x13,0},{0x13,0},{0x13,0},{0x13,0},{0xB,0},
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{3,0},
};

static void SSSetGeometryOffsets(void)
{
    if (SSTrack_drawing_index != SS_player_anim_frame_timer) return;
    int8_t x = k_curve[SSTrack_mapping_frame][0];
    if (SSTrack_Orientation) x = (int8_t)-x;
    SS_Offset_X = (uint16_t)(x + 0x80);
    SS_Offset_Y = (uint16_t)(k_curve[SSTrack_mapping_frame][1] + 0x36);
}

static void SSObjectsManager(void)
{
    if (SSTrack_drawing_index != 4) return;
    uint8_t d0 = SpecialStage_CurrentSegment;
    if (d0 == SpecialStage_LastSegment2) return;
    SpecialStage_LastSegment2 = d0;
    int d3 = k_track_len[(layout(d0) & 0x7F) < 6 ? (layout(d0) & 0x7F) : 5] * 4;
    uint32_t a0 = SS_CurrentLevelObjectLocations;
    for (;;) {
        int a1 = ss_alloc();
        if (a1 < 0) return;
        uint8_t b = tri_rd8(a0++);
        if (!(b & 0x80)) {
            if (!(b & 0x40)) {                           /* a ring */
                SS_Perfect_rings_left++;
                B(a1, O_id) = ID_SSRING;
            } else {                                     /* a bomb */
                b &= 0x3F;
                B(a1, O_id) = ID_SSBOMB;
            }
            SW(a1, 0x30, b * 4 + d3);
            B(a1, O_angle) = tri_rd8(a0++);
            continue;
        }
        SS_CurrentLevelObjectLocations = a0;
        if (b == 0xFF) return;                           /* end of this segment's objects */
        if (b == 0xFD) { B(a1, O_id) = ID_SSEMERALD; return; }
        if (b != 0xFE) { SS_NoCheckpoint_flag = 0xFF; SS_NoCheckpointMsg_flag = 0; }
        B(a1, O_id) = ID_SSMESSAGE;
        return;
    }
}

static const uint16_t k_star_colours[8] = { 0xEEE, 0xCCC, 0xAAA, 0x888, 0x888, 0xAAA, 0xCCC, 0xEEE };   /* Pal_SpecialStageStars */
static const uint16_t k_rainbow[] = { 0x0EE, 0x0C0, 0x0EE, 0x0C0, 0x0EE, 0x0CC, 0x088, 0x0E0, 0x0C0, 0x080, 0xEE0, 0xCC0, 0x880, 0xE0E, 0xC0C, 0x808 };

static void PalCycle_SS(void)
{
    if (!(Vint_runcount & 3)) {
        set_pal(0x0E, k_star_colours[SS_Star_color_1++ & 7]);
        set_pal(0x0F, k_star_colours[SS_Star_color_2++ & 7]);
    }
    if (Current_Special_Stage == 6 && Current_Special_Act == 3 && SS_Pause_Only_flag && !(Vint_runcount & 1)
        && Ring_count + Ring_count_2P >= SS_Ring_Requirement) {
        uint16_t d0 = pal(1);                            /* SSCheckpoint_rainbow: rotate colours 1..13 */
        for (int i = 1; i < 13; i++) set_pal(i, pal(i + 1));
        set_pal(13, d0);
        return;
    }
    if (!SS_Checkpoint_Rainbow_flag || (Vint_runcount & 7)) return;
    int d0 = (SS_Rainbow_palette++ & 3) * 6;             /* word_54C4 + d0 */
    set_pal(0x30 + 0x0B, k_rainbow[4 + d0 / 2]);
    set_pal(0x30 + 0x0C, k_rainbow[5 + d0 / 2]);
    set_pal(0x30 + 0x0D, k_rainbow[6 + d0 / 2]);
}

static void ssInitTableBuffers(void)
{
    uint32_t d0 = 0, d1 = (uint32_t)(uint16_t)-0x100 << 16, d2 = (uint32_t)(uint16_t)-0x200 << 16, d3 = (uint32_t)(uint16_t)-0x300 << 16;
    const uint32_t a[8] = { d0, d0, d1, d1, d2, d2, d3, d3 }, b[8] = { d3, d3, d2, d2, d1, d1, d0, d0 };
    for (int l = 0; l < 224; l++) { SS_Horiz_Scroll_Buf_1[l] = a[l & 7]; SS_Horiz_Scroll_Buf_2[l] = b[l & 7]; }
}

static void SSPlaneB_Background(void)
{
    uint32_t bot = item(SS_ENI_BACKBOTTOM, NULL), top = item(SS_ENI_BACK, NULL);
    for (int c = 0; c < 4; c++)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                uint32_t src = y < 16 ? bot + (uint32_t)(y * 32 + x) * 2u : top + (uint32_t)((y - 16) * 32 + x) * 2u;
                vram_w16(PNT_B + (uint32_t)y * 0x100u + (uint32_t)(c * 32 + x) * 2u, (uint16_t)(tri_rd16(src) + AT_BACK));
            }
}

static void SSInitPalAndData(void)
{
    Current_Special_Act = 0;
    SpecialStage_LastSegment2 = 0xFF;
    Ring_count = Ring_count_2P = 0;
    Perfect_rings_flag = 0;
    Got_Emerald = 0;
    SS_Star_color_2 = 4;
    uint32_t p = item(SS_PAL, NULL);
    for (int i = 0; i < 48; i++) w16(RAM_Target_palette + (uint32_t)i * 2u, tri_rd16(p + (uint32_t)i * 2u));   /* PalID_SS */
    for (int i = 0; i < 16; i++)                                                                              /* PalID_SS1+n: line 3 */
        w16(RAM_Target_palette + 0x60u + (uint32_t)i * 2u, tri_rd16(p + 0x60u + (uint32_t)Current_Special_Stage * 0x20u + (uint32_t)i * 2u));
    uint32_t a0 = item(SS_OBJLOC, NULL);
    SS_CurrentLevelObjectLocations = a0 + tri_rd16(a0 + (uint32_t)Current_Special_Stage * 2u);
    a0 = item(SS_LAYOUT, NULL);
    SS_CurrentLevelLayout = a0 + tri_rd16(a0 + (uint32_t)Current_Special_Stage * 2u);
}

/* SpecialStage_RingReq_Team / _Alone */
static const uint8_t k_req_team[7][4] = { {40,80,140,120},{50,100,140,150},{60,110,160,170},{40,100,150,160},{55,110,200,200},{80,140,220,220},{100,190,210,210} };
static const uint8_t k_req_alone[7][4] = { {30,70,130,110},{50,100,140,140},{50,110,160,160},{40,110,150,150},{50,90,160,160},{80,140,210,210},{100,150,190,190} };

static void SSStartNewAct(uint16_t *bcd, int *digits)    /* -> d1 (BCD), d2 (digits - 1) */
{
    int d2 = 1;
    int act = Current_Special_Act & 3;
    uint16_t d1 = s_player_mode ? k_req_alone[Current_Special_Stage][act] : k_req_team[Current_Special_Stage][act];
    SS_Ring_Requirement = d1;
    uint16_t d0 = 0;
    if (d1 >= 100) {
        d2++;
        d1 -= 100; d0 = 0x100;
        if (d1 >= 100) { d1 -= 100; d0 += 0x100; }
    }
    d0 |= (uint16_t)((d1 / 10) << 4 | d1 % 10);
    *bcd = d0;
    *digits = d2;
    Current_Special_Act++;
}

#define X_(o)  SWs(o, O_x)
#define Y_(o)  SWs(o, O_y)

/* ========================================================================
 * Object 09 - Sonic, Object 10 - Tails (and their shared SSPlayer_* code)
 * ====================================================================== */
static void ss_record_ctrl(void)                     /* loc_33908 */
{
    for (int k = 15; k >= 1; k--) SS_Ctrl_Record_Buf[k] = SS_Ctrl_Record_Buf[k - 1];
    SS_Ctrl_Record_Buf[0] = Ctrl_1_Logical;
}

static void SSPlayer_Move(int o, uint8_t held)
{
    int16_t d2 = SWs(o, O_inertia);
    if (held & (BTN_LEFT | BTN_RIGHT)) {
        if (held & BTN_LEFT) { d2 = (int16_t)(d2 + 0x60); if (d2 > 0x600) d2 = 0x600; }    /* SSPlayer_MoveLeft */
        else { d2 = (int16_t)(d2 - 0x60); if (d2 < -0x600) d2 = -0x600; }                   /* SSPlayer_MoveRight */
        SW(o, O_inertia, d2);
        B(o, O_status) &= (uint8_t)~ST_SS_SLOWING;
        B(o, ss_slide_timer) = 0;
        return;
    }
    if (!(B(o, O_status) & ST_SS_SLOWING)) B(o, ss_slide_timer) = 0x1E;
    B(o, O_status) |= ST_SS_SLOWING;
    d2 = (int16_t)(d2 - (d2 >> 3));                  /* (both of S2's paths do the same) */
    SW(o, O_inertia, d2);
    if (B(o, ss_slide_timer)) B(o, ss_slide_timer)--;
}

static void SSPlayer_Traction(int o)
{
    int16_t s, c;
    if (!B(o, ss_slide_timer)) {
        calc_sine(B(o, O_angle), &s, &c);
        SW(o, O_inertia, SWs(o, O_inertia) + (int16_t)((c * 0x50) >> 8));
    }
    uint8_t d0 = B(o, O_angle);
    if (!(d0 & 0x80)) return;
    d0 = (uint8_t)(d0 + 4);
    if (d0 < 0x88) return;
    int16_t in = SWs(o, O_inertia);
    if ((in < 0 ? -in : in) >= 0x100) return;
    B(o, O_rout) = 8;
}

static void SSPlayerSwapPositions(int o)
{
    if (s_player_mode) return;
    uint16_t d0 = W(o, ss_z_pos);
    int inc = o == MainCharacter ? SS_Swap_Positions_Flag != 0 : SS_Swap_Positions_Flag == 0;
    if (inc) { if (d0 == 0x80) return; d0++; }
    else { if (d0 == 0x6E) return; d0--; }
    SW(o, ss_z_pos, d0);
    B(o, O_prio) = d0 >= 0x77 ? 2 : 3;
}

static void SSObjectMove(int o)
{
    int16_t d2 = SWs(o, O_inertia), s, c;
    if (d2 < 0) B(o, O_angle) = (uint8_t)(B(o, O_angle) - (uint8_t)((uint16_t)-d2 >> 8));
    else B(o, O_angle) = (uint8_t)(B(o, O_angle) + (uint8_t)((uint16_t)d2 >> 8));
    calc_sine(B(o, O_angle), &s, &c);
    int16_t z = SWs(o, ss_z_pos);
    SW(o, ss_x_pos, (int16_t)((c * z) >> 8));
    SW(o, ss_y_pos, (int16_t)((s * z) >> 8));
}

static void SSAnglePos(int o)
{
    SW(o, O_x, (int16_t)((SWs(o, ss_x_pos) * 0xCC) >> 8) + SS_Offset_X);
    SW(o, O_y, SWs(o, ss_y_pos) + SS_Offset_Y);
}

static void SSPlayer_Jump(int o, uint8_t press)
{
    if (!(press & (BTN_B | BTN_C | BTN_A))) return;
    int16_t s, c;
    calc_sine((uint8_t)(B(o, O_angle) + 0x80), &s, &c);
    SW(o, O_xv, SWs(o, O_xv) + (int16_t)((c * 0x780) >> 8));
    SW(o, O_yv, SWs(o, O_yv) + (int16_t)((s * 0x780) >> 7));
    B(o, O_status) |= ST_SS_JUMPING;
    B(o, O_rout) = 4;
    B(o, O_anim) = 3;
    B(o, O_atime) = B(o, O_aframe) = B(o, O_cprop) = 0;
    if (!s_player_mode) SS_Swap_Positions_Flag = (uint8_t)~SS_Swap_Positions_Flag;
    o_sfx_s2(SFX_JUMP);
}

static void SSObjectMoveAndFall(int o)
{
    uint32_t d2 = L(o, ss_x_pos), d3 = L(o, ss_y_pos);
    d2 += (uint32_t)((int32_t)SWs(o, O_xv) << 8);
    int32_t yv = SWs(o, O_yv);
    SW(o, O_yv, yv + 0xA8);                          /* gravity */
    d3 += (uint32_t)(yv << 8);
    SL(o, ss_x_pos, d2);
    SL(o, ss_y_pos, d3);
}

static void SSPlayer_ChgJumpDir(int o, uint8_t held)
{
    if (held & BTN_LEFT) SW(o, O_xv, SWs(o, O_xv) - 0x40);
    else if (held & BTN_RIGHT) SW(o, O_xv, SWs(o, O_xv) + 0x40);
}

static uint16_t divu(uint32_t n, uint16_t d) { return d ? (uint16_t)(n / d) : 0; }

static void SSPlayer_JumpAngle(int o)
{
    uint32_t d2 = W(o, ss_y_pos), d3;
    if (!(d2 & 0x8000)) {
        d3 = W(o, ss_x_pos);
        if (!(d3 & 0x8000)) {
            if (d3 < d2) { B(o, O_angle) = (uint8_t)-(int)(divu(d3 << 5, (uint16_t)d2) - 0x40); return; }
            if (d3 == d2 && !d3) { B(o, O_angle) = 0x40; return; }
            B(o, O_angle) = (uint8_t)divu(d2 << 5, (uint16_t)d3);
            return;
        }
        d3 = (uint16_t)-(int16_t)d3;
        if (d3 >= d2) { B(o, O_angle) = (uint8_t)-(int)(divu(d2 << 5, (uint16_t)d3) - 0x80); return; }
        B(o, O_angle) = (uint8_t)(divu(d3 << 5, (uint16_t)d2) + 0x40);
        return;
    }
    d2 = (uint16_t)-(int16_t)d2;                     /* SSPlayer_JumpAngle_above_screen */
    d3 = W(o, ss_x_pos);
    if (d3 & 0x8000) {
        d3 = (uint16_t)-(int16_t)d3;
        if (d3 < d2) { B(o, O_angle) = (uint8_t)-(int)(divu(d3 << 5, (uint16_t)d2) - 0xC0); return; }
        B(o, O_angle) = (uint8_t)(divu(d2 << 5, (uint16_t)d3) + 0x80);
        return;
    }
    if (d3 >= d2) { B(o, O_angle) = (uint8_t)-(int)(divu(d2 << 5, (uint16_t)d3) - 0x100); return; }
    B(o, O_angle) = (uint8_t)(divu(d3 << 5, (uint16_t)d2) + 0xC0);
}

static const uint8_t k_shadow_angle[130] = {           /* byte_33D32 */
    0x40,0x40,0x40,0x40,0x41,0x41,0x41,0x42,0x42,0x42,0x43,0x43,0x43,0x44,0x44,0x44,
    0x45,0x45,0x45,0x46,0x46,0x46,0x47,0x47,0x47,0x48,0x48,0x48,0x48,0x49,0x49,0x49,
    0x4A,0x4A,0x4A,0x4B,0x4B,0x4B,0x4C,0x4C,0x4C,0x4D,0x4D,0x4D,0x4E,0x4E,0x4E,0x4F,
    0x4F,0x50,0x50,0x50,0x51,0x51,0x51,0x52,0x52,0x52,0x53,0x53,0x53,0x54,0x54,0x54,
    0x55,0x55,0x56,0x56,0x56,0x57,0x57,0x57,0x58,0x58,0x59,0x59,0x59,0x5A,0x5A,0x5B,
    0x5B,0x5B,0x5C,0x5C,0x5D,0x5D,0x5E,0x5E,0x5E,0x5F,0x5F,0x60,0x60,0x61,0x61,0x62,
    0x62,0x63,0x63,0x64,0x64,0x65,0x65,0x66,0x66,0x67,0x67,0x68,0x68,0x69,0x6A,0x6A,
    0x6B,0x6C,0x6C,0x6D,0x6E,0x6E,0x6F,0x70,0x71,0x72,0x73,0x74,0x75,0x77,0x78,0x7A,
    0x80,0x00 };

static void shadow_angle(int a0, int a1)             /* loc_33D02 */
{
    int neg = 0;
    uint32_t d0 = W(a1, ss_x_pos);
    if (d0 & 0x8000) { neg = 1; d0 = (uint16_t)-(int16_t)d0; }
    uint16_t q = divu(d0 << 7, W(a1, ss_z_pos));
    uint8_t a = k_shadow_angle[q < 130 ? q : 129];
    if (!neg) a = (uint8_t)-(int8_t)(uint8_t)(a - 0x80);
    if (SWs(a1, ss_y_pos) < 0) a = (uint8_t)-(int8_t)a;
    B(a0, O_angle) = a;
}

static void SSPlayer_DoLevelCollision(int o)
{
    int16_t y = SWs(o, ss_y_pos), x = SWs(o, ss_x_pos);
    if (y <= 0) return;
    uint32_t d0 = (uint32_t)((int32_t)y * y);
    uint32_t d1 = (uint32_t)((int32_t)x * x);
    d0 = (d0 & 0xFFFF0000u) | (uint16_t)((uint16_t)d0 + (uint16_t)d1);   /* add.w */
    uint32_t z = W(o, ss_z_pos);
    if (d0 < z * z) return;
    B(o, O_rout) = 2;
    B(o, O_status) &= (uint8_t)~ST_SS_JUMPING;
    SW(o, O_xv, 0); SW(o, O_yv, 0); SW(o, O_inertia, 0);
    B(o, ss_slide_timer) = 0;
    B(o, O_status) |= ST_SS_SLOWING;
    SSObjectMove(o);
    SSAnglePos(o);
}

static void SSPlayer_Collision(int o)
{
    if (!B(o, O_cprop)) return;
    B(o, O_cprop) = 0;
    if (B(o, ss_dplc_timer)) return;
    B(o, O_inertia) = 0;                             /* (high byte only: a bit of speed is left) */
    uint16_t rings;
    if (o == MainCharacter) { SS_Swap_Positions_Flag = 0xFF; rings = Ring_count; }
    else { SS_Swap_Positions_Flag = 0; rings = Ring_count_2P; }
    if (rings) o_sfx_s2(SFX_RINGSPILL);
    B(o, O_rout2) = 2;                               /* hurt */
    B(o, ss_hurt_timer) = 0;
}

static const uint8_t k_direction[8][2] = { {1,1},{0,0},{1,0},{2,0},{1,2},{0,2},{1,3},{2,1} };   /* byte_33E90 */

static void SSPlayer_SetAnimation(int o)
{
    if (B(o, O_status) & ST_SS_JUMPING) {
        B(o, O_anim) = 3;
        B(o, O_status) &= 0xFC;
        return;
    }
    uint8_t d0 = (uint8_t)((uint8_t)(B(o, O_angle) - 0x10) >> 5), d1 = d0;
    uint8_t d2 = k_direction[d0][0];
    if (d2 == B(o, O_anim) && d1 == B(o, ss_last_angle_index)) return;
    B(o, ss_last_angle_index) = d1;
    B(o, O_anim) = d2;
    B(o, O_status) = (uint8_t)((B(o, O_status) & 0xFC) | k_direction[d0][1]);
    if (d1 == 1 || d1 == 5) SW(o, ss_init_flip_timer, 0x400);
}

static void SSPlayer_Animate(int o, int ani_item)
{
    uint8_t d0 = B(o, O_anim);
    if (d0 != B(o, O_prev)) { B(o, O_aframe) = 0; B(o, O_prev) = d0; B(o, O_atime) = 0; }
    if ((int8_t)--B(o, O_atime) >= 0) return;        /* SSAnim_Do */
    uint32_t a1 = item(ani_item, NULL);
    a1 += tri_rd16(a1 + d0 * 2u);
    B(o, O_atime) = (uint8_t)(SS_player_anim_frame_timer >> 1);
    if (B(o, O_anim) == 0 && (int8_t)--B(o, ss_flip_timer) <= 0) {
        B(o, O_status) ^= RF_XFLIP;
        B(o, O_render) ^= RF_XFLIP;
        B(o, ss_flip_timer) = B(o, ss_init_flip_timer);
    }
    uint8_t f = tri_rd8(a1 + 1 + B(o, O_aframe));
    if (f & 0x80) { B(o, O_aframe) = 0; f = tri_rd8(a1 + 1); }
    B(o, O_frame) = f & 0x7F;
    B(o, O_render) = (uint8_t)((B(o, O_render) & 0xFC) | (B(o, O_status) & 3));
    B(o, O_aframe)++;
}

static const uint8_t k_hurt_frames[8][2] = { {4,1},{0,0},{4,0},{0xC,0},{4,2},{0,2},{4,3},{0xC,1} };   /* byte_33A92 */

static void SSHurt_Animation(int o)
{
    uint8_t d0 = (uint8_t)(B(o, ss_hurt_timer) + 8);
    B(o, ss_hurt_timer) = d0;
    if (!d0) { B(o, O_rout2) = 0; B(o, ss_dplc_timer) = 0x1E; }
    d0 = (uint8_t)(d0 + B(o, O_angle));
    B(o, O_render) &= 0xFC;
    d0 = (uint8_t)((uint8_t)(d0 - 0x10) >> 5);
    B(o, O_frame) = k_hurt_frames[d0][0];
    B(o, O_render) |= k_hurt_frames[d0][1];
    if ((uint8_t)(B(o, ss_hurt_timer) - 8)) return;
    B(o, O_cprop) = 0;
    if (!(o == MainCharacter ? Ring_count : Ring_count_2P)) return;
    int a1 = ss_alloc();
    if (a1 < 0) return;
    SP(a1, ss_parent, o);
    B(a1, O_id) = ID_SSRINGSPILL;
}

/* the art the players' DPLCs read: dword_33AA2 (Sonic), dword_349B8 (Tails), dword_34AA0 (Tails' tails) */
static const uint16_t k_sonic_art[4] = { 0x000, 0x058, 0x124, 0x171 };
static const uint16_t k_tails_art[4] = { 0x183, 0x1C0, 0x264, 0x29E };
static const uint16_t k_tails_tails_art[3] = { 0x2AE, 0x2E3, 0x31E };

static void dplc_entry(uint32_t src_base, uint16_t entry, uint32_t *dst)
{
    uint32_t len = ((uint32_t)(entry >> 12) + 1u) * 32u;
    tri_vram_queue(src_base + (uint32_t)(entry & 0xFFF) * 2u, *dst, len);
    *dst += len;
}

static void LoadSSPlayerDynPLC(int o, const uint16_t *art, uint8_t *last, uint16_t vtile, int d1)
{
    int d0 = B(o, O_frame);
    if (d0 == *last) return;
    *last = (uint8_t)d0;
    int k = d0 < 4 ? 0 : d0 < 0xC ? 1 : d0 < 0x10 ? 2 : 3;
    uint32_t src = item(SS_ART_PLAYERS, NULL) + (uint32_t)art[k] * 32u;
    uint32_t pl = item(SS_DPLC, NULL);
    pl += tri_rd16(pl + (uint32_t)(d0 + d1) * 2u);
    int n = tri_rd16(pl);
    uint32_t dst = (uint32_t)vtile * 32u;
    for (int i = 0; i < n; i++) dplc_entry(src, tri_rd16(pl + 2u + (uint32_t)i * 2u), &dst);
}

static int dplc_flicker(int o)                       /* the hurt flicker: 1 = not drawn this frame */
{
    uint8_t d0 = B(o, ss_dplc_timer);
    if (!d0) return 0;
    B(o, ss_dplc_timer) = --d0;
    return d0 & 1;
}

static void LoadSSSonicDynPLC(int o)
{
    if (dplc_flicker(o)) return;
    display(o);
    LoadSSPlayerDynPLC(o, k_sonic_art, &Sonic_LastLoadedDPLC, AT_SONIC, 0);
}

static void LoadSSTailsDynPLC(int o)
{
    if (dplc_flicker(o)) return;
    display(o);
    LoadSSPlayerDynPLC(o, k_tails_art, &Tails_LastLoadedDPLC, AT_TAILS, 0x12);
}

static void make_shadow(int o, int slot)
{
    int a1 = slot;
    B(a1, O_id) = ID_SSSHADOW;
    SW(a1, O_x, X_(o));
    SW(a1, O_y, Y_(o) + 0x18);
    SL(a1, O_map, SS_MAP_63);
    SW(a1, O_art, PAL3 | AT_FLATSHADOW);
    B(a1, O_render) = RF_LEVEL_FG;
    B(a1, O_prio) = 4;
    SP(a1, ss_parent, o);
}

static void Obj09(int o)
{
    ss_record_ctrl();
    switch (B(o, O_rout)) {
    case 0:                                          /* Obj09_Init */
        B(o, O_rout) = 2;
        SL(o, ss_x_pos, 0);
        SW(o, ss_y_pos, 0x80);
        SW(o, ss_y_sub, 0);
        SW(o, O_x, SS_Offset_X);
        SW(o, O_y, 0x80 + SS_Offset_Y);
        B(o, O_yrad) = 0xE;
        B(o, O_xrad) = 7;
        SL(o, O_map, SS_MAP_09);
        SW(o, O_art, PAL1 | AT_SONIC);
        B(o, O_render) = RF_LEVEL_FG;
        B(o, O_prio) = 3;
        SW(o, ss_z_pos, 0x6E);
        SS_Swap_Positions_Flag = 0;
        SW(o, ss_init_flip_timer, 0x400);
        B(o, O_angle) = 0x40;
        Sonic_LastLoadedDPLC = 1;
        B(o, ss_slide_timer) = 0;
        B(o, O_status) &= (uint8_t)~ST_SS_SLOWING;
        B(o, O_cprop) = 0;
        B(o, ss_dplc_timer) = 0;
        make_shadow(o, SpecialStageShadow_Sonic);
        LoadSSSonicDynPLC(o);
        return;
    case 2:                                          /* Obj09_MdNormal */
        if (B(o, O_rout2)) {                         /* Obj09_Hurt */
            SSHurt_Animation(o);
            SSPlayerSwapPositions(o);
            SSObjectMove(o);
            SSAnglePos(o);
            LoadSSSonicDynPLC(o);
            return;
        }
        SSPlayer_Move(o, Ctrl_1_Held_Logical);
        SSPlayer_Traction(o);
        SSPlayerSwapPositions(o);
        SSObjectMove(o);
        SSAnglePos(o);
        SSPlayer_Jump(o, Ctrl_1_Press_Logical);
        SSPlayer_SetAnimation(o);
        SSPlayer_Animate(o, SS_ANI_09);
        SSPlayer_Collision(o);
        LoadSSSonicDynPLC(o);
        return;
    case 4:                                          /* Obj09_MdJump */
        SSPlayer_ChgJumpDir(o, Ctrl_1_Held_Logical);
        SSObjectMoveAndFall(o);
        SSPlayer_JumpAngle(o);
        SSPlayer_DoLevelCollision(o);
        SSPlayerSwapPositions(o);
        SSAnglePos(o);
        SSPlayer_Animate(o, SS_ANI_09);
        LoadSSSonicDynPLC(o);
        return;
    case 8:                                          /* Obj09_MdAir */
        SSPlayer_ChgJumpDir(o, Ctrl_1_Held_Logical);
        SSObjectMoveAndFall(o);
        SSPlayer_JumpAngle(o);
        SSPlayer_DoLevelCollision(o);
        SSPlayerSwapPositions(o);
        SSAnglePos(o);
        SSPlayer_SetAnimation(o);
        SSPlayer_Animate(o, SS_ANI_09);
        LoadSSSonicDynPLC(o);
        return;
    default:
        return;
    }
}

static void SSTailsCPU_Control(void)
{
    if (s_player_mode) return;
    if (Ctrl_2_Held_Logical & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_B | BTN_C | BTN_A)) {
        SS_Ctrl_Record_Buf[0] = SS_Ctrl_Record_Buf[1] = 0;   /* (the pointer never moves: only the first long clears) */
        Tails_control_counter = 0xB4;
        return;
    }
    if (Tails_control_counter) { Tails_control_counter--; return; }
    Ctrl_2_Logical = SS_Ctrl_Record_Buf[15];
}

static void Obj10(int o)
{
    uint8_t held = s_player_mode ? Ctrl_1_Held_Logical : Ctrl_2_Held_Logical;
    switch (B(o, O_rout)) {
    case 0: {                                        /* Obj10_Init */
        B(o, O_rout) += 2;
        SW(o, ss_x_pos, 0);
        SW(o, ss_y_pos, 0x80);
        SW(o, O_x, SS_Offset_X);
        SW(o, O_y, 0x80 + SS_Offset_Y);
        B(o, O_yrad) = 0xE;
        B(o, O_xrad) = 7;
        SL(o, O_map, SS_MAP_10);
        SW(o, O_art, PAL2 | AT_TAILS);
        B(o, O_render) = RF_LEVEL_FG;
        B(o, O_prio) = 2;
        SW(o, ss_z_pos, 0x80);
        if (s_player_mode) { B(o, O_prio) = 3; SW(o, ss_z_pos, 0x6E); }
        SW(o, ss_init_flip_timer, 0x400);
        B(o, O_angle) = 0x40;
        Tails_LastLoadedDPLC = 1;
        B(o, O_cprop) = 0;
        B(o, ss_dplc_timer) = 0;
        LoadSSTailsDynPLC(o);
        make_shadow(o, SpecialStageShadow_Tails);
        int a1 = SpecialStageTails_Tails;
        B(a1, O_id) = ID_SSTAILSTAILS;
        SW(a1, O_x, X_(o));
        SW(a1, O_y, Y_(o));
        SL(a1, O_map, SS_MAP_88);
        SW(a1, O_art, PAL2 | AT_TAILS_TAILS);
        B(a1, O_render) = RF_LEVEL_FG;
        B(a1, O_prio) = (uint8_t)(B(o, O_prio) - 1);
        SP(a1, ss_parent, o);
        TailsTails_LastLoadedDPLC = 1;
        B(a1, ss_dplc_timer) = 0;
        return;
    }
    case 2:                                          /* Obj10_MdNormal */
        if (B(o, O_rout2)) {                         /* Obj10_Hurt */
            SSHurt_Animation(o);
            SSPlayerSwapPositions(o);
            SSObjectMove(o);
            SSAnglePos(o);
            LoadSSTailsDynPLC(o);
            return;
        }
        SSTailsCPU_Control();
        held = s_player_mode ? Ctrl_1_Held_Logical : Ctrl_2_Held_Logical;
        SSPlayer_Move(o, held);
        SSPlayer_Traction(o);
        SSPlayerSwapPositions(o);
        SSObjectMove(o);
        SSAnglePos(o);
        SSPlayer_Jump(o, s_player_mode ? Ctrl_1_Press_Logical : Ctrl_2_Press_Logical);
        SSPlayer_SetAnimation(o);
        SSPlayer_Animate(o, SS_ANI_10);
        SSPlayer_Collision(o);
        LoadSSTailsDynPLC(o);
        return;
    case 4:                                          /* Obj10_MdJump */
        SSPlayer_ChgJumpDir(o, held);
        SSObjectMoveAndFall(o);
        SSPlayer_DoLevelCollision(o);
        SSPlayerSwapPositions(o);
        SSAnglePos(o);
        SSPlayer_JumpAngle(o);
        SSPlayer_Animate(o, SS_ANI_10);
        LoadSSTailsDynPLC(o);
        return;
    case 8:                                          /* Obj10_MdAir */
        SSPlayer_ChgJumpDir(o, held);
        SSObjectMoveAndFall(o);
        SSPlayer_JumpAngle(o);
        SSPlayer_DoLevelCollision(o);
        SSPlayerSwapPositions(o);
        SSAnglePos(o);
        SSPlayer_SetAnimation(o);
        SSPlayer_Animate(o, SS_ANI_10);
        LoadSSTailsDynPLC(o);
        return;
    default:
        return;
    }
}

/* ---- Object 88 - Tails' tails ---------------------------------------------- */
static void Obj88(int o)
{
    int a1 = GP(o, ss_parent);
    SW(o, O_x, X_(a1));
    SW(o, O_y, Y_(a1));
    B(o, O_render) = B(a1, O_render);
    B(o, O_status) = B(a1, O_status);
    B(o, O_anim) = B(a1, O_anim);
    B(o, O_prio) = (uint8_t)(B(a1, O_prio) - 1);
    if (B(o, O_anim) >= 3) return;
    animate(o, SS_ANI_88);
    uint8_t d0 = B(a1, ss_dplc_timer);               /* LoadSSTailsTailsDynPLC */
    if (d0 && (d0 & 1)) return;
    display(o);
    int f = B(o, O_frame);
    if (f == TailsTails_LastLoadedDPLC) return;
    TailsTails_LastLoadedDPLC = (uint8_t)f;
    int k = f < 7 ? 0 : f < 0xE ? 1 : 2;
    uint32_t src = item(SS_ART_PLAYERS, NULL) + (uint32_t)k_tails_tails_art[k] * 32u;
    uint32_t pl = item(SS_DPLC, NULL);
    pl += tri_rd16(pl + (uint32_t)(f + 0x24) * 2u);
    uint32_t dst = (uint32_t)AT_TAILS_TAILS * 32u;
    dplc_entry(src, tri_rd16(pl), &dst);
}

/* ---- Object 63 - the players' shadows --------------------------------------- */
static const struct { uint16_t art; int16_t x, y; uint8_t frame, flip; } k_shadow[8] = {   /* word_3417A */
    { AT_DIAGSHADOW, 0x14, 0x14, 1, 1 }, { AT_FLATSHADOW, 0, 0x18, 0, 0 }, { AT_DIAGSHADOW, -0x14, 0x14, 1, 0 },
    { AT_SIDESHADOW, -0x14, 0, 2, 0 }, { AT_DIAGSHADOW, -0x14, -0x14, 7, 0 }, { AT_FLATSHADOW, 0, -0x18, 9, 0 },
    { AT_DIAGSHADOW, 0x14, -0x14, 7, 1 }, { AT_SIDESHADOW, 0x14, 0, 2, 1 } };

static void shadow_place(int o, int a1)              /* loc_340CC */
{
    if (B(a1, O_rout) == 2) {                        /* loc_34108 */
        SW(o, O_x, X_(a1));
        SW(o, O_y, Y_(a1));
        B(o, O_angle) = B(a1, O_angle);
    } else {
        int16_t s, c;
        shadow_angle(o, a1);
        calc_sine(B(o, O_angle), &s, &c);
        int16_t lo = (int16_t)(c * SWs(a1, ss_z_pos));
        SW(o, O_x, (int16_t)(((int32_t)lo * 0xCC) >> 16) + SS_Offset_X);
        SW(o, O_y, (int16_t)((s * SWs(a1, ss_z_pos)) >> 8) + SS_Offset_Y);
    }
    uint8_t d0 = (uint8_t)((uint8_t)(B(o, O_angle) - 0x10) >> 5), d1 = d0;   /* loc_3411A */
    SW(o, O_art, PAL3 | k_shadow[d0].art);
    SW(o, O_x, X_(o) + k_shadow[d0].x);
    SW(o, O_y, Y_(o) + k_shadow[d0].y);
    B(o, O_frame) = k_shadow[d0].frame;
    B(o, O_render) = (uint8_t)((B(o, O_render) & 0xFC) | k_shadow[d0].flip);
    if (!(B(o, O_angle) & 0x80)) return;
    if (d1 == 3 || d1 == 7) B(o, O_frame) += 3;
    int16_t dy = (int16_t)(SS_Offset_Y - Y_(o));
    SW(o, O_y, Y_(o) + dy * 2);
}

static void Obj63(int o)
{
    int a1 = GP(o, ss_parent);
    if (a1 == MainCharacter) {
        shadow_place(o, MainCharacter);
        display(o);
        return;
    }
    shadow_place(o, Sidekick);
    if (B(Sidekick, O_anim) == 1) {                  /* loc_341BA */
        int d1 = B(Sidekick, O_status) & 3;
        if (d1 < 2) {
            SW(o, O_x, X_(o) + (d1 ? -4 : 4));
            SW(o, O_y, Y_(o) - 4);
        }
    }
    display(o);
}

/* ========================================================================
 * Objects 60 (rings), 61 (bombs), 59 (emerald) and their shadows
 * ====================================================================== */
static int s_abort;                                  /* an object popped RunObjects' frame (move.l (sp)+,d0) */

static int Obj61_TestCollision_one(int a0, int a1, int d6)   /* loc_350E2 */
{
    if (!B(a1, O_id) || B(a1, O_rout) != 2 || B(a1, O_rout2)) return 0;
    uint8_t d0 = B(a1, O_angle), d1 = B(a0, O_angle), d2 = d1;
    int hit;
    if ((int)d1 + d6 > 0xFF) {                        /* loc_35110 */
        d1 = (uint8_t)(d1 + d6);
        d2 = (uint8_t)(d2 - d6);
        hit = d0 < d1 || d0 >= d2;
    } else {
        d1 = (uint8_t)(d1 + d6);
        if (d2 < d6) { d2 = (uint8_t)(d2 - d6); hit = d0 < d1 || d0 >= d2; }   /* loc_35112 */
        else { d2 = (uint8_t)(d2 - d6); hit = d0 < d1 && d0 >= d2; }
    }
    if (!hit) return 0;
    B(a0, O_coll) = 0;
    return 1;
}

static int Obj61_TestCollision(int a0, int d6)       /* -> the player hit, or -1 */
{
    if (B(a0, O_anim) != 8 || !B(a0, O_coll)) return -1;
    int a2 = MainCharacter, a3 = Sidekick;
    if (W(a2, ss_z_pos) < W(a3, ss_z_pos)) {
        if (Obj61_TestCollision_one(a0, a2, d6)) return a2;
        return Obj61_TestCollision_one(a0, a3, d6) ? a3 : -1;
    }
    if (Obj61_TestCollision_one(a0, a3, d6)) return a3;
    return Obj61_TestCollision_one(a0, a2, d6) ? a2 : -1;
}

static void unlink_shadow(int o)                     /* objoff_34: the shadow goes with it */
{
    int s = GP(o, 0x34);
    if (s < 0) return;
    SL(o, 0x34, 0);
    B(s, 0x2A) = 0xFF;
}

static const uint8_t k_depth_anim[32] = { 9,9,9,8,8,7,7,6,6,5,5,4,4,3,3,3, 2,2,2,1,1,1,1,1,1,1,1,1,1,1,0,0 };   /* byte_35180 */

static int ss_depth(int o)                           /* loc_3512A: 1 = deleted */
{
    int dead = (B(o, O_status) & ST_NO_BALANCING) != 0;
    if (!dead) {
        int32_t z = (int32_t)L(o, 0x30) - (SSTrack_drawing_index == 4 ? 0xCCCC : 0xCCCD);
        SL(o, 0x30, (uint32_t)z);
        dead = z <= 0;
    }
    if (dead) {                                      /* loc_3516C */
        int s = GP(o, 0x34);
        if (s >= 0) B(s, 0x2A) = 0xFF;
        delete_obj(o);
        return 1;
    }
    if (B(o, O_anim) == 0xA) return 0;
    int16_t d0 = SWs(o, 0x30);
    if (d0 > 0x1D) d0 = 0x1E;
    B(o, O_anim) = k_depth_anim[d0];
    return 0;
}

static void ss_project(int o)                        /* loc_351A0 */
{
    uint32_t a1 = SS_CurrentPerspective;
    uint16_t d0 = W(o, 0x30);
    if (!d0 || (int16_t)d0 > (int16_t)tri_rd16(a1)) { B(o, O_render) &= (uint8_t)~RF_ONSCREEN; return; }
    a1 += 2;
    uint32_t e = a1 + (uint32_t)(d0 - 1) * 6u;
    uint8_t ang = B(o, O_angle);
    int d2, d3, d4, d5;
    if (!SSTrack_Orientation) {
        uint8_t d6 = tri_rd8(e + 4), d7 = tri_rd8(e + 5);
        if (d7 && ang >= d6 && ang < d7) { B(o, O_render) &= (uint8_t)~RF_ONSCREEN; return; }
        d2 = tri_rd8(e);
    } else {                                         /* loc_35260 */
        uint8_t d6 = tri_rd8(e + 4), d7 = tri_rd8(e + 5);
        if (d7) {
            d6 = (uint8_t)(0x80 - d6);
            d7 = (uint8_t)(0x80 - d7);
            if (ang >= d7 && ang < d6) { B(o, O_render) &= (uint8_t)~RF_ONSCREEN; return; }
        }
        d2 = 0x100 - tri_rd8(e);
    }
    d4 = tri_rd8(e + 2);
    d5 = tri_rd8(e + 3);
    d3 = tri_rd8(e + 1);
    if (d3 & 0x80) d3 = (int8_t)d3;                  /* loc_351F8 */
    int16_t s, c;
    calc_sine(ang, &s, &c);
    SW(o, O_x, (int16_t)((c * d4) >> 8) + d2);
    SW(o, O_y, (int16_t)((s * d5) >> 8) + d3);
    int sh = GP(o, 0x34);
    if (sh >= 0) {
        int d4b = d4 + (d4 >> 2), d5b = d5 + (d5 >> 2);
        SW(sh, O_x, (int16_t)((c * d4b) >> 8) + d2);
        SW(sh, O_y, (int16_t)((s * d5b) >> 8) + d3);
    }
    B(o, O_render) |= RF_ONSCREEN;
}

static void ss_make_object_shadow(int o)             /* loc_3529C */
{
    int a1 = ss_alloc_after(o);
    if (a1 < 0) return;
    SP(a1, 0x34, o);
    B(a1, O_id) = B(o, O_id);
    B(a1, O_rout) = 4;
    SL(a1, O_map, SS_MAP_63);
    SW(a1, O_art, PAL3 | AT_FLATSHADOW);
    B(a1, O_render) = RF_LEVEL_FG;
    B(a1, O_prio) = 5;
    int8_t d0 = (int8_t)B(o, O_angle);
    if (d0 <= 0x10) { B(a1, O_render) |= RF_XFLIP; B(a1, 0x2B) = 2; }
    else if (d0 <= 0x30) { B(a1, O_render) |= RF_XFLIP; B(a1, 0x2B) = 1; }
    else if (d0 <= 0x50) B(a1, 0x2B) = 0;
    else if (d0 <= 0x70) B(a1, 0x2B) = 1;
    else B(a1, 0x2B) = 2;
    SP(o, 0x34, a1);
}

static void ss_object_shadow(int o)                  /* loc_3533A */
{
    if (B(o, 0x2A)) { delete_obj(o); return; }
    int a1 = GP(o, 0x34);
    if (a1 < 0 || !(B(a1, O_render) & RF_ONSCREEN)) return;
    int d0 = 10 - B(a1, O_anim);
    if (d0 == 0xA) d0 = 9;
    d0 *= 3;
    int d1 = B(o, 0x2B);
    d0 += d1;
    SW(o, O_art, PAL3 | (d1 == 0 ? AT_FLATSHADOW : d1 == 1 ? AT_DIAGSHADOW : AT_SIDESHADOW));
    B(o, O_frame) = (uint8_t)d0;
    display(o);
}

static void ss_animate(int o, int ani_item)          /* loc_3539E */
{
    if ((int8_t)--B(o, O_atime) >= 0) return;
    uint32_t a1 = item(ani_item, NULL);
    a1 += tri_rd16(a1 + B(o, O_anim) * 2u);
    B(o, O_atime) = tri_rd8(a1);
    uint8_t f = tri_rd8(a1 + 1 + B(o, O_aframe));
    if (f & 0x80) { B(o, O_aframe) = 0; f = tri_rd8(a1 + 1); }
    B(o, O_frame) = f & 0x7F;
    B(o, O_render) = (uint8_t)((B(o, O_render) & 0xFC) | (B(o, O_status) & 3));
    B(o, O_aframe)++;
}

static void near_prio(int o)                         /* loc_34F90 */
{
    if (W(o, 0x30) < 4) B(o, O_prio) = 1;
}

static void Obj61(int o)
{
    switch (B(o, O_rout)) {
    case 0:                                          /* Obj61_Init */
        B(o, O_rout) += 2;
        SW(o, O_x, 0x7F);
        SW(o, O_y, 0x58);
        SL(o, O_map, SS_MAP_61);
        SW(o, O_art, PAL1 | AT_BOMB);
        B(o, O_render) = RF_LEVEL_FG;
        B(o, O_prio) = 3;
        B(o, O_coll) = 2;
        if (!(B(o, O_angle) & 0x80)) ss_make_object_shadow(o);
        /* fall through */
    case 2: {
        if (ss_depth(o)) return;
        ss_project(o);
        ss_animate(o, SS_ANI_61);
        if (!(B(o, O_render) & RF_ONSCREEN)) return;
        int a1 = Obj61_TestCollision(o, 8);          /* loc_34F28 */
        if (a1 >= 0) {
            B(a1, O_cprop) = 1;
            o_sfx_s2(SFX_SLOWSMASH);
            B(o, O_rout) = 6;
            B(o, O_aframe) = 0;
            B(o, O_atime) = 0;
            unlink_shadow(o);
        }
        display(o);
        return;
    }
    case 4:
        ss_object_shadow(o);
        return;
    case 6:                                          /* loc_34F6A */
        B(o, O_anim) = 0xA;
        SW(o, O_art, PAL2 | AT_EXPLOSION);
        near_prio(o);
        if (ss_depth(o)) return;
        ss_project(o);
        animate(o, SS_ANI_61);
        display(o);
        return;
    default:
        return;
    }
}

static void Obj60(int o)
{
    switch (B(o, O_rout)) {
    case 0:                                          /* Obj60_Init */
        B(o, O_rout) += 2;
        SW(o, O_x, 0x7F);
        SW(o, O_y, 0x58);
        SL(o, O_map, SS_MAP_RINGS);
        SW(o, O_art, PAL3 | AT_RINGS);
        B(o, O_render) = RF_LEVEL_FG;
        B(o, O_prio) = 3;
        B(o, O_coll) = 1;
        if (!(B(o, O_angle) & 0x80)) ss_make_object_shadow(o);
        /* fall through */
    case 2: {
        if (ss_depth(o)) return;
        ss_project(o);
        int a1 = Obj61_TestCollision(o, 0xA);        /* loc_35036 */
        if (a1 >= 0) {
            if (a1 == MainCharacter) Ring_count++;
            else Ring_count_2P++;
            if (++B(a1, ss_rings_units) >= 10) {
                B(a1, ss_rings_tens)++;
                B(a1, ss_rings_units) = 0;
                if (B(a1, ss_rings_tens) >= 10) { B(a1, ss_rings_hundreds)++; B(a1, ss_rings_tens) = 0; }
            }
            B(o, O_rout) = 6;
            unlink_shadow(o);
            o_sfx_s2(SFX_RING);
        }
        ss_animate(o, SS_ANI_60);
        if (B(o, O_render) & RF_ONSCREEN) display(o);
        return;
    }
    case 4:
        ss_object_shadow(o);
        return;
    case 6:                                          /* loc_35010 */
        B(o, O_anim) = 0xA;
        SW(o, O_art, PAL2 | AT_STARS);
        near_prio(o);
        if (ss_depth(o)) return;
        ss_project(o);
        animate(o, SS_ANI_60);
        display(o);
        return;
    default:
        return;
    }
}

/* ---- Object 5B - the rings a hit player drops --------------------------------- */
static const uint8_t k_spill_even[10] = { 0x38, 0x48, 0x2A, 0x56, 0x1C, 0x64, 0x0E, 0x72, 0x00, 0x80 };   /* byte_353EA */
static const uint8_t k_spill_odd[10] = { 0x40, 0x30, 0x50, 0x20, 0x60, 0x10, 0x70, 0x00, 0x80, 0x00 };     /* byte_353F4 */

static void Obj5B(int o)
{
    if (B(o, O_rout) == 0) {                         /* Obj5B_Init */
        int a3 = GP(o, ss_parent);
        int d1 = 0;
        const uint8_t *a2 = k_spill_even;
        if (B(a3, ss_rings_tens)) { B(a3, ss_rings_tens)--; d1 = 10; }
        else if (B(a3, ss_rings_hundreds)) { B(a3, ss_rings_hundreds)--; B(a3, ss_rings_tens) = 9; d1 = 10; }
        else if ((d1 = B(a3, ss_rings_units)) != 0) { B(a3, ss_rings_units) = 0; if (d1 & 1) a2 = k_spill_odd; }
        if (B(a3, O_id) == ID_SONICSS) Ring_count = (uint16_t)(Ring_count - d1);
        else Ring_count_2P = (uint16_t)(Ring_count_2P - d1);
        if (d1 - 1 < 0) { delete_obj(o); return; }
        int px = X_(a3), py = Y_(a3);
        uint8_t pa = B(a3, O_angle);
        int a1 = o, ai = 0;
        for (int k = 0; k < d1; k++) {
            if (k) { a1 = ss_alloc(); if (a1 < 0) continue; }   /* (a2 only steps for a ring made) */
            B(a1, O_id) = ID_SSRINGSPILL;
            B(a1, O_rout) = 2;
            SL(a1, O_map, SS_MAP_RINGS);
            SW(a1, O_art, PAL3 | AT_RINGS);
            B(a1, O_render) = RF_LEVEL_FG;
            B(a1, O_prio) = 5;
            B(a1, O_coll) = 0;
            B(a1, O_anim) = 8;
            SW(a1, O_x, px);
            SW(a1, O_y, py);
            int16_t s, c;
            calc_sine((uint8_t)(pa + 0x40 + a2[ai++]), &s, &c);
            SW(a1, O_xv, (int16_t)((c * 0x400) >> 8));
            SW(a1, O_yv, (int16_t)((s * 0x1000) >> 8));
        }
        return;
    }
    object_move_and_fall(o);                         /* Obj5B_Main */
    SW(o, O_yv, SWs(o, O_yv) + 0x80);
    if (SWs(o, O_yv) >= 0) { B(o, O_prio) = 0; B(o, O_anim) = 9; }   /* loc_3551C */
    if (X_(o) < 0 || (uint16_t)X_(o) >= 256 || Y_(o) > 224) { delete_obj(o); return; }
    animate(o, SS_ANI_60);
    display(o);
}

/* ---- Object 59 - the emerald ---------------------------------------------- */
static void SSClearObjs(void)
{
    memset(s_o, 0, sizeof s_o);
    for (uint32_t a = 0; a < 0x284; a += 2) w16(RAM_Sprite_table + a, 0);
}

static void emerald_colours(int d0)                  /* loc_35F76 */
{
    uint32_t p = item(SS_PAL_EMERALD, NULL) + (uint32_t)d0 * 6u;
    set_pal(0x30 + 0x0B, tri_rd16(p));
    set_pal(0x30 + 0x0C, tri_rd16(p + 2));
    set_pal(0x30 + 0x0D, tri_rd16(p + 4));
}

static void Obj5A_PrintCheckpointText(int d0);

static void emerald_project(int o)                   /* loc_3603C */
{
    uint32_t e = SS_CurrentPerspective + 2u + (uint32_t)(W(o, 0x30) - 1) * 6u;
    int d2 = tri_rd8(e), d3 = tri_rd8(e + 1), d4 = tri_rd8(e + 2), d5 = tri_rd8(e + 3);   /* (d3 is never sign-extended here) */
    int d4a = (d4 * 3) >> 2, d5a = (d5 * 3) >> 2;
    int16_t s, c;
    calc_sine(B(o, O_angle), &s, &c);
    int x = (int16_t)((c * d4a) >> 8) + d2, y = (int16_t)((s * d5a) >> 8) + d3;
    SW(o, O_x, x);
    SW(o, O_y, y);
    B(o, 0x3A) = (uint8_t)x;
    B(o, 0x3B) = (uint8_t)y;
    int sh = GP(o, 0x34);
    if (sh < 0) return;
    int d4b = d4 + (d4 >> 2), d5b = d5 + (d5 >> 2);
    SW(sh, O_x, (int16_t)((c * d4b) >> 8) + d2);
    SW(sh, O_y, (int16_t)((s * d5b) >> 8) + d3);
}

static void Obj59(int o)
{
    switch (B(o, O_rout)) {
    case 0:                                          /* Obj59_Init */
        SS_NoCheckpointMsg_flag = 0xFF;
        SS_Pause_Only_flag = 0xFF;
        SW(o, 0x2A, W(o, 0x2A) - 1);
        if (SWs(o, 0x2A) != -0x3C) return;
        emerald_colours(Current_Special_Stage);
        B(o, O_rout) += 2;
        SL(o, O_map, SS_MAP_59);
        SW(o, O_art, PAL3 | AT_EMERALD);
        B(o, O_render) = RF_LEVEL_FG;
        B(o, O_prio) = 4;
        SW(o, 0x30, 0x36);
        B(o, O_angle) = 0x40;
        ss_make_object_shadow(o);
        /* fall through */
    case 2:
        if (B(o, O_anim) >= 3) {                     /* loc_360F0 */
            if (!B(o, 0x3E)) { tri_music_fade(); B(o, 0x3E) = 0xFF; }
            if (B(o, O_anim) >= 6) {
                if (Ring_count + Ring_count_2P < SS_Ring_Requirement) {   /* loc_36142 */
                    SS_New_Speed_Factor = 0;
                    B(o, O_rout) = 6;
                    SW(o, 0x2A, 0x4F);
                    Obj5A_PrintCheckpointText(6);
                } else if (B(o, O_anim) >= 9) {
                    SW(o, 0x2A, 0x63);
                    B(o, O_rout) = 8;
                    tri_music_play_s2(TRI_SLOT_ZONE, MUS_EMERALD);
                }
            }
        }
        if (ss_depth(o)) return;
        emerald_project(o);
        ss_animate(o, SS_ANI_59);
        display(o);
        return;
    case 4:
        ss_object_shadow(o);
        return;
    case 6:                                          /* loc_36160 */
        SW(o, 0x2A, W(o, 0x2A) - 1);
        if (SWs(o, 0x2A) >= 0) { display(o); return; }
        SS_Check_Rings_flag = 0xFF;
        SSClearObjs();
        return;
    case 8:                                          /* loc_36172 */
        SW(o, 0x2A, W(o, 0x2A) - 1);
        if (SWs(o, 0x2A) >= 0) {                     /* loc_361A4: it bobs */
            static const int8_t bob[4] = { -1, 0, 1, 0 };
            B(o, 0x3C)++;
            SW(o, O_y, B(o, 0x3B) + bob[(B(o, 0x3C) >> 2) & 3]);
            display(o);
            return;
        }
        Got_Emerald = 0xFF;                          /* (Got_Emeralds_array: tri_s2ss_award) */
        Current_Special_Stage++;
        Emerald_count++;
        SS_Check_Rings_flag = 0xFF;
        SSClearObjs();
        s_abort = 1;
        return;
    default:
        return;
    }
}

/* ========================================================================
 * Object 5A - messages / checkpoint
 * ====================================================================== */
/* SSMessage_TextFrames (title-card letter frames), index = word id / 2 */
static const uint8_t k_w_get[] = { 0, 1, 2, 0xFF }, k_w_rings[] = { 3, 4, 5, 0, 6, 0xFF }, k_w_cool[] = { 7, 8, 8, 9, 0x11, 0xFF },
    k_w_not[] = { 5, 8, 2, 0xFF }, k_w_enough[] = { 1, 5, 8, 0xA, 0, 0xB, 0xFF }, k_w_player[] = { 0xC, 9, 0xD, 0xE, 1, 3, 0xFF },
    k_w_most[] = { 0xF, 8, 6, 2, 0xFF }, k_w_wins[] = { 0x10, 4, 5, 6, 0x11, 0xFF }, k_w_sonic[] = { 6, 8, 5, 4, 7, 0xFF },
    k_w_miles[] = { 0xF, 4, 9, 1, 6, 0xFF }, k_w_tie[] = { 2, 4, 1, 0x11, 0xFF }, k_w_win[] = { 0x10, 4, 5, 0xFF },
    k_w_twice[] = { 2, 0x10, 4, 7, 1, 0xFF }, k_w_all[] = { 0xD, 9, 9, 0x11, 0xFF }, k_w_excl[] = { 0x11, 0xFF },
    k_w_dots[] = { 0x12, 0x12, 0x12, 0xFF }, k_w_vs[] = { 0x13, 0xFF }, k_w_tails[] = { 2, 0xD, 4, 9, 6, 0xFF };
static const uint8_t *const k_words[] = { k_w_get, k_w_rings, k_w_cool, k_w_not, k_w_enough, k_w_player, k_w_most,
    k_w_wins, k_w_sonic, k_w_miles, k_w_tie, k_w_win, k_w_twice, k_w_all, k_w_excl, k_w_dots, k_w_vs, k_w_tails };
/* SSMessage_TextPhrases (word ids) */
static const uint8_t k_p_empty[] = { 0xFF }, k_p_rings[] = { 2, 0x1C, 0xFF }, k_p_cool[] = { 4, 0xFF },
    k_p_notenough[] = { 6, 8, 2, 0x1E, 0xFF };
static const uint8_t *const k_phrases[] = { k_p_empty, k_p_rings, k_p_cool, k_p_notenough };

static void SSRainbowPaletteColors(int d0)
{
    static const uint16_t c[6] = { 0xEE, 0x88, 0x44, 0xEE, 0xCC, 0x88 };   /* word_35548 */
    set_pal(0x30 + 0x0B, c[d0 / 2]);
    set_pal(0x30 + 0x0C, c[d0 / 2 + 1]);
    set_pal(0x30 + 0x0D, c[d0 / 2 + 2]);
}

static void Init_Obj5A(int a1)
{
    B(a1, O_id) = ID_SSMESSAGE;
    SL(a1, O_map, SS_MAP_5A);
    SW(a1, O_art, PAL2 | AT_MESSAGES);
    B(a1, O_render) = RF_LEVEL_FG;
    B(a1, O_prio) = 1;
}

static void Obj5A_CreateRingsToGoText(void)
{
    static const uint8_t ring[] = { 3, 4, 5, 0 }, togo[] = { 0x11, 8, 0, 8, 2 };    /* "RING", "!OGOT" */
    static const int16_t offs[6] = { 0xC0, 0xB8, 0xB0, 0xA0, 0x98, 0x88 };      /* Obj5A_ToGoOffsets */
    SS_TriggerRingsToGo = 0xFF;
    int a1 = ss_alloc();
    if (a1 < 0) return;
    SL(a1, O_map, SS_MAP_5F_B);
    SW(a1, O_art, PAL2 | AT_HUD);
    B(a1, O_id) = ID_SSMESSAGE;
    B(a1, O_render) = RF_LEVEL_FG;
    B(a1, O_prio) = 1;
    B(a1, O_render) |= RF_MULTI;
    B(a1, mainspr_childsprites) = 0;
    B(a1, O_rout) = 0xE;
    for (int k = 0, d1 = 0x5A; k < 3; k++, d1 -= 8) {
        SW(a1, SUB_X(k), d1);
        SW(a1, SUB_Y(k), 0x38);
        SW(a1, SUB_X(k) + 4, 0);
    }
    int d1 = 0x68;
    for (int k = 0; k < 4; k++, d1 += 8) {
        if ((a1 = ss_alloc()) < 0) return;
        Init_Obj5A(a1);
        B(a1, O_rout) = 0x10;
        SW(a1, O_x, d1);
        SW(a1, O_y, 0x38);
        B(a1, O_frame) = ring[k];
    }
    for (int k = 0; k < 5; k++) {
        if ((a1 = ss_alloc()) < 0) return;
        Init_Obj5A(a1);
        B(a1, O_rout) = 0x12;
        SW(a1, 0x2A, offs[k]);
        SW(a1, O_y, 0x38);
        B(a1, O_frame) = togo[k];
    }
    if ((a1 = ss_alloc()) < 0) return;
    Init_Obj5A(a1);
    B(a1, O_rout) = 0x14;
    SW(a1, O_x, offs[5]);
    SW(a1, O_y, 0x38);
    B(a1, O_frame) = 6;                              /* "S" */
}

static int Obj5A_PrintWord(int a0, int d0, int d1, int d2)   /* -> d1 after the word */
{
    for (const uint8_t *a3 = k_words[d0 / 2]; *a3 != 0xFF; a3++) {
        int a1 = ss_alloc_after(a0);
        if (a1 < 0) break;
        B(a1, O_frame) = *a3;
        SL(a1, O_map, SS_MAP_5A);
        SW(a1, O_art, PAL2 | AT_MESSAGES);
        B(a1, O_id) = ID_SSMESSAGE;
        B(a1, O_rout) = 4;
        B(a1, O_render) = RF_LEVEL_FG;
        B(a1, O_prio) = 1;
        SW(a1, O_x, d1);
        SW(a1, O_y, d2);
        SW(a1, 0x2A, 0x46);
        d1 += 8;
    }
    return d1;
}

static void Obj5A_PrintNumber(int a0, int d0, int d1, int d2)
{
    int a1 = ss_alloc_after(a0);
    if (a1 < 0) return;
    B(a1, O_frame) = (uint8_t)d0;
    SL(a1, O_map, SS_MAP_5F_B);
    SW(a1, O_art, PAL2 | AT_HUD);
    B(a1, O_id) = ID_SSMESSAGE;
    B(a1, O_rout) = 4;
    B(a1, O_render) = RF_LEVEL_FG;
    B(a1, O_prio) = 1;
    SW(a1, O_x, d1);
    SW(a1, O_y, d2);
    SW(a1, 0x2A, 0x46);
}

static int s_cur;                                    /* the running object (the allocators' a0) */

static void Obj5A_CreateRingReqMessage(void)
{
    int a0 = s_cur;
    Obj5A_PrintWord(a0, 0, 0x54, 0x6C);             /* GET */
    uint16_t d4;
    int d5;
    SSStartNewAct(&d4, &d5);
    int a3 = d5;
    int d1 = 0x80;
    if (d5 != 2) d1 -= 8;
    for (int k = 0; k <= d5; k++) {
        Obj5A_PrintNumber(a0, d4 & 0xF, d1, 0x6C);
        d4 >>= 4;
        d1 -= 8;
    }
    d1 = 0x84;
    if (a3 == 2) d1 += 8;
    for (const uint8_t *a2 = k_phrases[1]; *a2 != 0xFF; a2++)   /* RINGS! */
        d1 = Obj5A_PrintWord(a0, *a2, d1, 0x6C);
}

/* -> the hand (its wings in *wings) */
static int Obj5A_CreateCheckpointWingedHand(int d0, int d3, int *wings)
{
    int d4 = 0x48;
    int a1 = ss_alloc();
    *wings = -1;
    if (a1 < 0) return -1;
    B(a1, O_id) = ID_SSMESSAGE;
    B(a1, O_rout) = 6;
    SL(a1, O_map, SS_MAP_5A);
    SW(a1, O_art, PAL1 | AT_MESSAGES);
    B(a1, O_render) = RF_LEVEL_FG;
    B(a1, O_prio) = 1;
    SW(a1, O_x, d3);
    SW(a1, O_y, d4);
    SW(a1, 0x2A, 0x46);
    B(a1, O_frame) = 0x14;                           /* the wings */
    *wings = a1;
    if ((a1 = ss_alloc()) < 0) return -1;
    B(a1, O_id) = ID_SSMESSAGE;
    B(a1, O_rout) = 6;
    SL(a1, O_map, SS_MAP_5A);
    SW(a1, O_art, PAL1 | AT_MESSAGES);
    B(a1, O_render) = RF_LEVEL_FG;
    B(a1, O_prio) = 0;
    SW(a1, O_x, d3);
    SW(a1, O_y, d4);
    SW(a1, 0x30, d4);                                /* the handshake's target */
    SW(a1, 0x2A, 0x46);
    B(a1, O_frame) = 0x15;                           /* the hand */
    if (d0 == 6) {                                   /* not enough rings: thumb down */
        B(a1, 0x2F) = 0xFF;
        B(a1, O_render) |= RF_YFLIP;
    }
    return a1;
}

static void Obj5A_PrintCheckpointText(int d0)        /* loc_35D6E */
{
    int a0 = s_cur;
    int d1 = 0x74, d2 = 0x68;
    if (d0 != 4) d1 = 0x5E;
    for (const uint8_t *a2 = k_phrases[d0 / 2]; *a2 != 0xFF; a2++) {
        if (*a2 == 2) { d1 = 0x5E; d2 = 0x7E; }
        d1 = Obj5A_PrintWord(a0, *a2, d1, d2) + 8;
    }
}

static void Obj5A_PrintCheckpointMessage(int d0)
{
    int wings, hand = Obj5A_CreateCheckpointWingedHand(d0, 0x80, &wings);
    if (s_player_mode > 1) {                         /* (Tails alone: palette line 1 + 1) */
        if (hand >= 0) SW(hand, O_art, W(hand, O_art) + PAL1);
        if (wings >= 0) SW(wings, O_art, W(wings, O_art) + PAL1);
    }
    Obj5A_PrintCheckpointText(d0);
}

static void SSClearObjs(void);

static void text_flyout(int o)                       /* Obj5A_TextFlyout / Obj5F loc_710A */
{
    int16_t s, c;
    calc_sine(B(o, O_angle), &s, &c);
    int16_t sp = SWs(o, 0x14);
    SW(o, O_x, X_(o) + (int16_t)((int16_t)(c * sp) >> 8));
    SW(o, O_y, Y_(o) + (int16_t)((int16_t)(s * sp) >> 8));
    if (X_(o) < 0 || X_(o) > 0x100 || Y_(o) < 0) { delete_obj(o); return; }
    display(o);
}

static void Obj5A_FlashMessage(int o)
{
    if (SS_NoCheckpointMsg_flag || SS_HideRingsToGo) return;
    if ((Vint_runcount & 7) < 6) display(o);
}

static void Obj5A(int o)
{
    switch (B(o, O_rout)) {
    case 0:                                          /* Obj5A_Init */
        if (!SS_NoCheckpoint_flag) {
            if (SSTrack_last_mappings_copy != MS_STRAIGHT4) return;
            SSRainbowPaletteColors(6);
            SS_Checkpoint_Rainbow_flag = 0xFF;
            for (int d0 = 6; d0 >= 0; d0--) {
                int a1 = ss_alloc();
                if (a1 < 0) continue;
                B(a1, O_id) = ID_SSMESSAGE;
                B(a1, O_rout) = 2;
                SL(a1, O_map, SS_MAP_RINGS);
                SW(a1, O_art, PAL3 | AT_RINGS);
                B(a1, O_render) = RF_LEVEL_FG;
                B(a1, O_prio) = 5;
                B(a1, 0x2A) = (uint8_t)d0;
                SW(a1, 0x30, 0);
                B(a1, O_frame) = 0xFF;
            }
            delete_obj(o);
            return;
        }
        SS_NoCheckpoint_flag = 0;                    /* Obj5A_RingsMessageInit */
        SS_HideRingsToGo = 0;
        SS_TriggerRingsToGo = 0;
        SS_NoRingsTogoLifetime = 0;
        B(o, 0x3A) = 0;
        delete_obj(o);
        return;
    case 2: {                                        /* Obj5A_CheckpointRainbow */
        static const uint8_t frames[10] = { 0, 1, 1, 1, 2, 4, 6, 8, 9, 0xFF };
        static const uint8_t pos[63][2] = {
            {0xF6,0xF6},{0x70,0x5E},{0x76,0x58},{0x7E,0x56},{0x88,0x58},{0x8E,0x5E},{0xF6,0xF6},
            {0xF6,0xF6},{0x6D,0x5A},{0x74,0x54},{0x7E,0x50},{0x8A,0x54},{0x92,0x5A},{0xF6,0xF6},
            {0xF6,0xF6},{0x6A,0x58},{0x72,0x50},{0x7E,0x4C},{0x8C,0x50},{0x94,0x58},{0xF6,0xF6},
            {0xF6,0xF6},{0x68,0x56},{0x70,0x4C},{0x7E,0x48},{0x8E,0x4C},{0x96,0x56},{0xF6,0xF6},
            {0x62,0x5E},{0x66,0x50},{0x70,0x46},{0x7E,0x42},{0x8E,0x46},{0x98,0x50},{0x9C,0x5E},
            {0x5C,0x5A},{0x62,0x4A},{0x70,0x3E},{0x7E,0x38},{0x8E,0x3E},{0x9C,0x4A},{0xA2,0x5A},
            {0x54,0x54},{0x5A,0x3E},{0x6A,0x30},{0x7E,0x2A},{0x94,0x30},{0xA4,0x3E},{0xAA,0x54},
            {0x42,0x4A},{0x4C,0x28},{0x62,0x12},{0x7E,0x0A},{0x9C,0x12},{0xB2,0x28},{0xBC,0x4A},
            {0x16,0x26},{0x28,0xFC},{0xEC,0xEC},{0xEC,0xEC},{0xEC,0xEC},{0xD6,0xFC},{0xE8,0x26} };
        if (SSTrack_drawing_index != 4) {
            if (!(B(o, O_frame) & 0x80)) display(o);
            return;
        }
        uint16_t d0 = W(o, 0x2C);
        B(o, O_frame) = frames[d0 < 10 ? d0 : 9];
        if (!(B(o, O_frame) & 0x80)) {
            SW(o, 0x2C, d0 + 1);
            int k = (B(o, 0x2A) * 2 + W(o, 0x30)) / 2;
            B(o, O_x + 1) = pos[k][0];
            B(o, O_y + 1) = pos[k][1];
            SW(o, 0x30, W(o, 0x30) + 0xE);
            display(o);
            return;
        }
        if (W(o, O_x) != 0xE8) { delete_obj(o); return; }
        SSRainbowPaletteColors(0);
        SS_Checkpoint_Rainbow_flag = 0;
        SS_NoCheckpointMsg_flag = 0xFF;
        int d1 = 6, sfx = SFX_ERROR;                 /* loc_35978 */
        if (Ring_count + Ring_count_2P >= SS_Ring_Requirement) { d1 = 4; sfx = SFX_CHECKPOINT; }
        o_sfx_s2(sfx);
        s_cur = o;
        Obj5A_PrintCheckpointMessage(d1);
        delete_obj(o);
        return;
    }
    case 4:                                          /* Obj5A_TextFlyoutInit */
        SW(o, 0x2A, W(o, 0x2A) - 1);
        if (W(o, 0x2A)) { display(o); return; }
        if ((int8_t)B(o, O_frame) > 0x13) { delete_obj(o); return; }
        B(o, O_rout) = 8;
        SW(o, 0x14, 8);
        B(o, O_angle) = o_atan((int16_t)(X_(o) - 0x80), (int16_t)(Y_(o) - 0x70));
        display(o);
        return;
    case 6: {                                        /* Obj5A_Handshake */
        if (B(o, O_frame) == 0x15) {
            int16_t d0 = SWs(o, 0x30);
            if (!B(o, 0x2E)) {
                SW(o, O_y, Y_(o) - 1);
                d0 -= 4;
                if (!(d0 < Y_(o))) { SW(o, O_y, d0 + 1); B(o, 0x2E) = 0xFF; }
            } else {
                SW(o, O_y, Y_(o) + 1);
                d0 += 4;
                if (!(d0 > Y_(o))) { SW(o, O_y, d0 - 1); B(o, 0x2E) = 0; }
            }
        }
        SW(o, 0x2A, W(o, 0x2A) - 1);
        if (W(o, 0x2A)) { display(o); return; }
        if (B(o, 0x2F)) {                            /* not enough rings: the stage ends */
            tri_music_fade();
            SW(o, 0x2A, 0x30);
            B(o, O_rout) = 0xC;
            return;
        }
        if (B(o, O_frame) != 0x15 || !W(o, 0x30)) { delete_obj(o); return; }
        s_cur = o;
        Obj5A_CreateRingReqMessage();
        delete_obj(o);
        return;
    }
    case 8:
        text_flyout(o);
        return;
    case 0xC:                                        /* Obj5A_RingCheckTrigger */
        SW(o, 0x2A, W(o, 0x2A) - 1);
        if (W(o, 0x2A)) return;
        SS_Check_Rings_flag = 0xFF;
        SSClearObjs();
        return;
    case 0xE: {                                      /* Obj5A_RingsNeeded */
        SS_HideRingsToGo = SS_TriggerRingsToGo;
        if (SS_HideRingsToGo) return;
        int d0 = s_player_mode == 0 ? Ring_count + Ring_count_2P : s_player_mode == 1 ? Ring_count : Ring_count_2P;
        d0 = (int16_t)(SS_Ring_Requirement - d0);
        uint32_t bcd;
        if (d0 <= 0) {
            bcd = 0;
            if (++SS_NoRingsTogoLifetime >= 0xC) SS_HideRingsToGo = 0xFF;
        } else {
            uint32_t d1 = (uint16_t)d0;
            bcd = 0;
            if (d1 >= 100) {                         /* (S2's conversion: it breaks above 100) */
                do { bcd += 0x100; d1 = (d1 & 0xFFFF0000u) | (uint16_t)(d1 - 100); } while ((int16_t)d1 > 0);
            }
            uint32_t q = (d1 / 10) & 0xFFFF, r = (d1 % 10) & 0xFFFF;
            bcd = (bcd & 0xFF00) | (uint8_t)((uint8_t)bcd | (uint8_t)(q << 4) | (uint8_t)r);
            SS_NoRingsTogoLifetime = 0;
            SS_HideRingsToGo = 0;
        }
        int d2 = 1;                                  /* loc_3577A */
        SS_RingsToGoBCD = (uint16_t)bcd;
        B(o, SUB_F(0)) = bcd & 0xF;
        bcd = (bcd & 0xFFFF) >> 4;
        if (bcd) {
            d2++;
            B(o, SUB_F(1)) = bcd & 0xF;
            bcd >>= 4;
            if (bcd) { d2++; B(o, SUB_F(2)) = bcd & 0xF; }
        }
        B(o, mainspr_childsprites) = (uint8_t)d2;
        Obj5A_FlashMessage(o);
        return;
    }
    case 0x10:
        Obj5A_FlashMessage(o);
        return;
    case 0x12:                                       /* Obj5A_MoveAndFlash */
        SW(o, O_x, SWs(o, 0x2A) + (SS_RingsToGoBCD < 2 ? -8 : 0));
        Obj5A_FlashMessage(o);
        return;
    case 0x14:                                       /* Obj5A_FlashOnly */
        if (SS_RingsToGoBCD >= 2) Obj5A_FlashMessage(o);
        return;
    default:
        return;
    }
}

/* ========================================================================
 * Object 5E - HUD, Object 5F - start banner, Object 87 - ring counts
 * ====================================================================== */
static void Obj5E(int o)
{
    if (B(o, O_rout)) { display(o); return; }
    static const uint8_t layout_[3][5] = { { 3, 0x80, 0, 2, 3 }, { 1, 0xD4, 0, 0, 0 }, { 1, 0x38, 2, 0, 0 } };   /* SSHUD_SonicTailsTotal, _Sonic_2, _Tails */
    SL(o, O_map, SS_MAP_5E);
    SW(o, O_art, AT_HUD);
    B(o, O_render) = RF_LEVEL_FG;
    B(o, O_prio) = 0;
    B(o, O_rout) = 1;
    B(o, O_render) |= RF_MULTI;
    const uint8_t *a1 = layout_[s_player_mode % 3];   /* (+3: "TAILS", the overseas name) */
    B(o, mainspr_childsprites) = a1[0];
    for (int k = 0; k < a1[0]; k++) {
        SW(o, SUB_X(k), a1[1]);
        B(o, SUB_F(k)) = a1[2 + k];
    }
}

static void Obj5F(int o)
{
    switch (B(o, O_rout)) {
    case 0:                                          /* Obj5F_Init */
        SW(o, O_x, 0x80);
        SW(o, O_y, -0x40);
        SW(o, O_yv, 0x100);
        SL(o, O_map, SS_MAP_5F_A);
        SW(o, O_art, AT_START);
        B(o, O_render) = RF_LEVEL_FG;
        B(o, O_prio) = 1;
        B(o, O_rout) = 2;
        /* fall through */
    case 2:                                          /* Obj5F_Main */
        object_move(o);
        if (Y_(o) >= 0x48) {
            SW(o, O_yv, 0);
            SW(o, O_y, 0x48);
            B(o, O_rout) = 4;
            B(o, 0x2A) = 0xF;
        }
        display(o);
        return;
    case 4: {                                        /* loc_71B4 */
        if (--B(o, 0x2A)) { display(o); return; }
        uint32_t a2 = item(SS_5F_FRAME2, NULL);
        int d3 = 2;
        SW(o, 0x14, 8);
        B(o, O_rout) = 6;
        for (int k = 0; k < 7; k++) {
            int a1 = ss_alloc();
            if (a1 < 0) continue;
            memcpy(s_o[a1], s_o[o], 0x40);
            B(a1, O_frame) = (uint8_t)d3++;
            B(a1, O_angle) = o_atan((int16_t)tri_rd16(a2 + 8), -0x28);
            a2 += 0xA;
        }
        B(o, O_rout) = 0xA;
        SW(o, 0x2A, 0x1E);
        return;
    }
    case 6:
        text_flyout(o);                              /* loc_710A */
        return;
    case 0xA:                                        /* loc_7218 */
        SW(o, 0x2A, W(o, 0x2A) - 1);
        if (SWs(o, 0x2A) >= 0) return;
        s_cur = o;
        Obj5A_CreateRingReqMessage();
        SpecialStage_Started = 0xFF;
        delete_obj(o);
        return;
    default:
        return;
    }
}

static void Obj87_digits(int a, int base_x, int *d3, int *d5, int o, int step)
{
    int n = 0;
    if (B(a, ss_rings_hundreds)) { B(o, SUB_F(*d3 + n)) = B(a, ss_rings_hundreds); n++; }
    if (n || B(a, ss_rings_tens)) { B(o, SUB_F(*d3 + n)) = B(a, ss_rings_tens); n++; }
    B(o, SUB_F(*d3 + n)) = B(a, ss_rings_units); n++;
    for (int k = 0; k < n; k++, base_x += 8) { SW(o, SUB_X(*d5 / 6), base_x); *d5 += step; }
    *d3 += n;
}

static void Obj87(int o)
{
    switch (B(o, 0x0A)) {
    case 0:                                          /* Obj87_Init */
        B(o, 0x0A) = 2;
        SL(o, O_map, SS_MAP_5F_B);
        SW(o, O_art, PAL2 | AT_HUD);
        B(o, O_render) = RF_LEVEL_FG | RF_MULTI;
        B(o, mainspr_childsprites) = 2;
        SW(o, SUB_X(0), 0x48); SW(o, SUB_Y(0), 0x20); SW(o, mainspr_height, 0);
        SW(o, SUB_X(1), 0xE0); SW(o, SUB_Y(1), 0x20); SW(o, O_frame, 0);
        for (int k = 2; k < 6; k++) SW(o, SUB_Y(k), 0x20);
        if (s_player_mode) {
            B(o, mainspr_childsprites)--;
            SW(o, SUB_X(0), 0x94);
            return;
        }
        {
            int a1 = ss_alloc();
            if (a1 < 0) return;
            B(a1, O_id) = ID_SSNUMBEROFRINGS;
            B(a1, 0x0A) = 4;
            SL(a1, O_map, SS_MAP_5F_B);
            SW(a1, O_art, PAL2 | AT_HUD);
            B(a1, O_render) = RF_LEVEL_FG | RF_MULTI;
            B(a1, mainspr_childsprites) = 1;
            SW(a1, SUB_X(0), 0x80); SW(a1, SUB_Y(0), 0x20); SW(a1, mainspr_height, 0);
            SW(a1, SUB_Y(1), 0x20); SW(a1, SUB_Y(2), 0x20);
        }
        return;
    case 2: {                                        /* loc_7480 */
        int d3 = 0, d5 = 0;
        if (s_player_mode != 2)
            Obj87_digits(MainCharacter, s_player_mode ? 0x48 + 0x54 : 0x48, &d3, &d5, o, 6);
        if (s_player_mode != 1)
            Obj87_digits(Sidekick, s_player_mode ? 0xE0 - 0x44 : 0xE0, &d3, &d5, o, 6);
        B(o, mainspr_childsprites) = (uint8_t)d3;
        display(o);
        return;
    }
    case 4: {                                        /* loc_753E */
        int d0 = B(MainCharacter, ss_rings_units) + B(Sidekick, ss_rings_units);
        int d1 = B(MainCharacter, ss_rings_tens) + B(Sidekick, ss_rings_tens);
        int d2 = B(MainCharacter, ss_rings_hundreds) + B(Sidekick, ss_rings_hundreds);
        int d3 = 1;
        if (d0 >= 10) { d1++; d0 -= 10; }
        if (d1) {
            if (d1 >= 10) { d2++; d1 -= 10; }
            d3++;
            if (d2) d3++;
        } else if (d2) {
            d3 += 2;
        }
        B(o, mainspr_childsprites) = (uint8_t)d3;
        if (d3 > 2) {
            SW(o, SUB_X(0), 0x78); B(o, SUB_F(0)) = (uint8_t)d2;
            SW(o, SUB_X(1), 0x80); B(o, SUB_F(1)) = (uint8_t)d1;
            SW(o, SUB_X(2), 0x88); B(o, SUB_F(2)) = (uint8_t)d0;
        } else if (d3 < 2) {
            SW(o, SUB_X(0), 0x80); B(o, SUB_F(0)) = (uint8_t)d0;
        } else {
            SW(o, SUB_X(0), 0x7C); B(o, SUB_F(0)) = (uint8_t)d1;
            SW(o, SUB_X(1), 0x84); B(o, SUB_F(1)) = (uint8_t)d0;
        }
        display(o);
        return;
    }
    default:
        return;
    }
}

/* ========================================================================
 * The results (SpecialStage's tally: Obj6F, HudUpdate's score and bonus
 * counters), after the stage's white fade
 * ====================================================================== */
enum { SS_ART_TITLECARD = SS_BGOFS + 1, SS_ART_LETTERS, SS_ART_RESULTS, SS_ART_HUDTEXT, SS_ART_HUDDIGITS,
       SS_PAL_RESULT, SS_MAP_6F };
#define AT_TITLECARD    0x580
#define AT_RESULTS      0x590
#define AT_HUDTEXT      0x6CA                       /* ArtTile_ArtNem_HUD */
#define AT_HUD_SCORE_E  0x6E2
#define AT_HUD_SCORE    0x6E4
#define AT_HUD_BONUS    0x520                       /* ArtTile_HUD_Bonus_Score */
#define RAM_Score_      0xFFFE26u
#define SFX_TALLYEND    0xC5          /* SndID_TallyEnd */
#define SFX_BLIP        0xCD          /* SndID_Blip */
#define SFX_SIGNPOST    0xCF          /* SndID_Signpost */
enum { titlecard_x_target = 0x30, titlecard_x_source = 0x32 };

static uint16_t Bonus_Countdown_1, Bonus_Countdown_2, Bonus_Countdown_3, Total_Bonus_Countdown;
static uint8_t Update_HUD_score, Update_Bonus_score;
static uint16_t Level_Inactive_flag;

static void hud_digit(uint32_t tile, int d)          /* one Art_Hud character (2 tiles); d < 0: blank */
{
    uint32_t src = item(SS_ART_HUDDIGITS, NULL) + (uint32_t)d * 64u;
    for (uint32_t k = 0; k < 64; k++) vram()[(tile * 32u + k) & 0xFFFF] = d < 0 ? 0 : tri_rd8(src + k);
}

static void Hud_Base(void)                            /* (the score's part: Hud_TilesBase "E      0" "0:00" "  0") */
{
    static const int8_t base[15] = { 0x16 / 2, -1, -1, -1, -1, -1, -1, 0, 0, 0x14 / 2, 0, 0, -1, -1, 0 };
    for (int i = 0; i < 15; i++) hud_digit(AT_HUD_SCORE_E + (uint32_t)i * 2u, base[i]);
}

static void hud_number(uint32_t tile, uint32_t v, int digits, int clear)   /* Hud_Score / Hud_TimeRingBonus */
{
    static const uint32_t pow10[6] = { 100000, 10000, 1000, 100, 10, 1 };
    int lead = 0;
    for (int i = 6 - digits; i < 6; i++, tile += 2) {
        int d = 0;
        while (v >= pow10[i]) { v -= pow10[i]; d++; }
        if (d) lead = 1;
        if (lead) hud_digit(tile, d);
        else if (clear) hud_digit(tile, -1);
    }
}

static void results_hud(void)                         /* HudUpdate: score, bonus counters */
{
    if (Update_HUD_score) {
        Update_HUD_score = 0;
        hud_number(AT_HUD_SCORE, r32(RAM_Score_), 6, 0);
    }
    if (Update_Bonus_score) {
        Update_Bonus_score = 0;
        hud_number(AT_HUD_BONUS + 0x00, Total_Bonus_Countdown, 4, 1);
        hud_number(AT_HUD_BONUS + 0x08, Bonus_Countdown_1, 4, 1);
        hud_number(AT_HUD_BONUS + 0x10, Bonus_Countdown_2, 4, 1);
        hud_number(AT_HUD_BONUS + 0x18, Bonus_Countdown_3, 4, 1);
    }
}

static void add_points(int d0)                        /* AddPoints (S3K's score, in tens) */
{
    D(0) = (uint32_t)d0;
    tri_jsr(S3K_HUD_AddToScore);
    Update_HUD_score = 1;
}

static uint8_t s_res_got[7];                         /* Got_Emeralds_array (the trilogy's held emeralds) */
static int emerald_got(int i) { return s_res_got[i] != 0; }

static void Obj34_Move(int o)                         /* Obj34_MoveTowardsTargetPosition */
{
    int d0 = 0x10;
    uint16_t d1 = W(o, O_x);
    if (d1 != W(o, titlecard_x_target)) {
        if (!(d1 > W(o, titlecard_x_target))) d0 = -d0;
        SW(o, O_x, d1 - d0);
        if (W(o, O_x) > 0x200) return;
    }
    display(o);
}

static void Obj6F(int o)
{
    static const struct { uint16_t sx, tx; int16_t y; uint8_t rout, frame; } meta[13] = {   /* Obj6F_SubObjectMetaData */
        { 0x80 + 320 + 128, 288, -70, 2, 0 }, { 0, 288, -88, 4, 1 },
        { 280, 0, -44, 6, 5 }, { 304, 0, -32, 8, 6 }, { 304, 0, -8, 0xA, 7 }, { 280, 0, 4, 0xC, 8 },
        { 256, 0, -8, 0xE, 9 }, { 256, 0, -32, 0x10, 0xA }, { 280, 0, -20, 0x12, 0xB },
        { 0x80 + 320 + 368, 288, 24, 0x14, 0xC }, { 0x80 + 320 + 384, 288, 40, 0x16, 0xD },
        { 0x80 + 320 + 400, 288, 56, 0x18, 0xE }, { 0x80 + 320 + 416, 288, 72, 0x1A, 0x10 } };
    int r = B(o, O_rout);
    if (r == 0) {                                     /* Obj6F_Init */
        for (int i = 0; i < 13; i++) {
            int a1 = o + i;
            B(a1, O_id) = ID_SSRESULTS;
            SW(a1, O_x, meta[i].sx);
            SW(a1, titlecard_x_source, meta[i].sx);
            SW(a1, titlecard_x_target, meta[i].tx);
            SW(a1, O_xsub, 128 + 112 + meta[i].y);
            B(a1, O_rout) = meta[i].rout;
            B(a1, O_frame) = meta[i].frame;
            SL(a1, O_map, SS_MAP_6F);
            B(a1, O_width) = 0x78;
            B(a1, O_render) = 0;
        }
        r = 2;
    }
    int d6 = 0;
    switch (r) {
    case 2:                                           /* Obj6F_InitEmeraldText */
        if (Got_Emerald) B(o, O_frame) = 4;          /* "Chaos Emerald" */
        if (Emerald_count == 7) B(o, O_frame) = 0x19;   /* "Chaos Emeralds" */
        if (W(o, titlecard_x_target) == W(o, O_x)) { B(o, O_rout) = 0x1C; SW(o, O_atime, 0xB4); }
        Obj34_Move(o);
        return;
    case 4: {                                         /* Obj6F_InitResultTitle */
        int d0;
        if (Emerald_count == 7) d0 = 0x16;          /* "Sonic has all the" */
        else { if (!Got_Emerald) { delete_obj(o); return; } d0 = 1; }   /* "Sonic got a" */
        if (s_player_mode == 2) d0 += 2;              /* ("Tails", the overseas name) */
        B(o, O_frame) = (uint8_t)d0;
        Obj34_Move(o);
        return;
    }
    case 0x12: d6++; /* fall through */
    case 0x10: d6++; /* fall through */
    case 0xE: d6++; /* fall through */
    case 0xC: d6++; /* fall through */
    case 0xA: d6++; /* fall through */
    case 8: d6++; /* fall through */
    case 6:                                           /* Obj6F_Emerald0..6 */
        if (!emerald_got(d6)) { delete_obj(o); return; }
        if (Vint_runcount & 1) display(o);
        return;
    case 0x14:
        Obj34_Move(o);
        return;
    case 0x18: {                                      /* Obj6F_P2Rings */
        if (s_player_mode || B(SpecialStageResults, O_rout) == 0x26) { delete_obj(o); return; }
        B(o, O_frame) = (uint8_t)(Bonus_Countdown_2 ? 0xF : 0xF + 5);   /* "Tails rings" */
        Obj34_Move(o);
        return;
    }
    case 0x16: {                                      /* Obj6F_P1Rings */
        if (B(SpecialStageResults, O_rout) == 0x26) {
            Bonus_Countdown_1 = 5000;                 /* perfect bonus */
            B(o, O_rout) = 0x2A;
            SW(o, O_xsub, 128 + 112 + 48);
            Update_Bonus_score = 0xFF;
            o_sfx_s2(SFX_SIGNPOST);
            SW(SpecialStageResults, O_atime, 0x5A);
            goto perfect;
        }
        int d0;
        uint16_t cnt;
        if (s_player_mode) {
            SW(o, O_xsub, 128 + 112 + 48);
            if (s_player_mode == 1) { d0 = 0xD; cnt = Bonus_Countdown_1; }
            else { d0 = 0xF; cnt = Bonus_Countdown_2; }
        } else { d0 = 0xD; cnt = Bonus_Countdown_1; }
        if (!cnt) d0 += 5;
        B(o, O_frame) = (uint8_t)d0;
        Obj34_Move(o);
        return;
    }
    case 0x1A:                                        /* Obj6F_DeleteIfNotEmerald */
        if (!Got_Emerald) { delete_obj(o); return; }
        Obj34_Move(o);
        return;
    case 0x1C: case 0x20: case 0x24: case 0x26: case 0x2C:   /* Obj6F_TimedDisplay */
        SW(o, O_atime, W(o, O_atime) - 1);
        if (!W(o, O_atime)) B(o, O_rout) += 2;
        display(o);
        return;
    case 0x1E: {                                      /* Obj6F_TallyScore */
        display(o);
        Update_Bonus_score = 1;
        int d0 = 0;
        if (Bonus_Countdown_1) { d0 += 10; Bonus_Countdown_1--; }
        if (Bonus_Countdown_2) { d0 += 10; Bonus_Countdown_2--; }
        if (Total_Bonus_Countdown) { d0 += 10; Total_Bonus_Countdown -= 10; }
        if (d0) {
            add_points(d0);
            if (!(Vint_runcount & 3)) o_sfx_s2(SFX_BLIP);
            return;
        }
        o_sfx_s2(SFX_TALLYEND);
        B(o, O_rout) += 2;
        SW(o, O_atime, 0x78);
        if (Perfect_rings_flag) { B(o, O_rout) = 0x24; SW(o, O_atime, 0x5A); return; }
        if (s_player_mode == 2 || !Got_Emerald || Emerald_count != 7 || s_super) return;
        B(o, O_rout) = 0x30;
        return;
    }
    case 0x22: case 0x2E:                             /* Obj6F_DisplayOnly */
        Level_Inactive_flag = 1;
        display(o);
        return;
    case 0x28: {                                      /* Obj6F_TallyPerfect */
        display(o);
        Update_Bonus_score = 1;
        if (Bonus_Countdown_1) {
            Bonus_Countdown_1 = (uint16_t)(Bonus_Countdown_1 - 20);
            add_points(20);
            if (!(Vint_runcount & 3)) o_sfx_s2(SFX_BLIP);
            return;
        }
        o_sfx_s2(SFX_TALLYEND);
        B(o, O_rout) += 4;
        SW(o, O_atime, 0x78);
        if (s_player_mode == 2 || !Got_Emerald || Emerald_count != 7 || s_super) return;
        B(o, O_rout) = 0x30;
        return;
    }
    case 0x2A:
    perfect:                                          /* Obj6F_PerfectBonus */
        B(o, O_frame) = (uint8_t)((Vint_runcount & 8) ? 0x15 : 0x11);
        display(o);
        return;
    case 0x30: {                                      /* Obj6F_InitAndMoveSuperMsg */
        int n = o + 1;
        B(n, O_rout) = 0x32;
        if (W(o, O_x) != W(o, titlecard_x_source)) goto to_source;
        B(n, O_rout) = 0x14;
        SW(n, O_xsub, W(n, O_xsub) - 8);
        B(n, O_frame) = 0x1A;                         /* "Now Sonic can" */
        B(o, O_rout) = 0x34;
        SW(o, O_xsub, W(o, O_xsub) - 8);
        B(o, O_frame) = 0x1B;                         /* "Change into" */
        {
            int a1 = SpecialStageResults + 1 + 0xC;   /* SpecialStageResults2 */
            B(a1, O_id) = ID_SSRESULTS;
            SW(a1, O_x, 0);
            SW(a1, titlecard_x_target, 288);
            SW(a1, O_xsub, 128 + 112 - 60);
            B(a1, O_rout) = 0x14;
            B(a1, O_frame) = 0x1C;                    /* "Super Sonic" */
            SL(a1, O_map, SS_MAP_6F);
            B(a1, O_width) = 0x78;
            B(a1, O_render) = 0;
        }
        display(o);
        return;
    }
    case 0x32:
    to_source: {                                      /* Obj6F_MoveTowardsSourcePosition */
        int d0 = 0x20;
        uint16_t d1 = W(o, O_x);
        if (d1 != W(o, titlecard_x_source)) {
            if (!(d1 > W(o, titlecard_x_source))) d0 = -d0;
            SW(o, O_x, d1 - d0);
            if (W(o, O_x) > 0x200) return;
        }
        display(o);
        return;
    }
    case 0x34:                                        /* Obj6F_MoveAndDisplay */
        if (W(o, O_x) != W(o, titlecard_x_target)) { Obj34_Move(o); return; }
        SW(o, O_atime, 0xB4);
        B(o, O_rout) = 0x20;
        display(o);
        return;
    default:
        return;
    }
}

static void results_setup(void)                      /* SpecialStage, after the white fade */
{
    set_reg(2, 0xC000 >> 10);
    set_reg(4, 0xE000 >> 13);
    set_reg(16, 0x01);                               /* 64x32 */
    set_reg(12, 0x81);                               /* H40, no shadow/highlight */
    set_reg(11, 0x00);
    vram_fill(0x0000, 0x40);                         /* ClearScreen: the first two tiles, both planes */
    vram_fill(0xC000, 0x2000);
    vram_fill(0xE000, 0x1000);
    vram_fill(HSCROLL, 0x400);
    memset(SS_Horiz_Scroll_Buf_1, 0, sizeof SS_Horiz_Scroll_Buf_1);
    Vscroll_Factor_FG = Vscroll_Factor_BG = 0;
    for (uint32_t a = 0; a < 0x280; a += 2) w16(RAM_Sprite_table + a, 0);
    uint32_t p = item(SS_PAL_RESULT, NULL);           /* PalLoad_Now */
    for (int i = 0; i < 64; i++) set_pal(i, tri_rd16(p + (uint32_t)i * 2u));
    load_art(SS_ART_HUDTEXT, AT_HUDTEXT);            /* PLCID_Std1 (what the tally shows of it) */
    Hud_Base();
    load_art(SS_ART_LETTERS, 2);                     /* LoadTitleCardSS */
    load_art(SS_ART_TITLECARD, AT_TITLECARD);
    load_art(SS_ART_RESULTS, AT_RESULTS);
    if (s_player_mode == 2) Ring_count = 0;
    else if (s_player_mode == 1) Ring_count_2P = 0;
    Bonus_Countdown_1 = Ring_count;
    Bonus_Countdown_2 = Ring_count_2P;
    Bonus_Countdown_3 = 0;
    Total_Bonus_Countdown = Got_Emerald ? 1000 : 0;
    /* the emeralds held, with the one just won (trilogy_ss.c awards it after): Chaos
     * Emeralds, or once all seven are held, the Super Emeralds */
    s_super = r8(RAM_Chaos_emerald_count) >= 7;
    Emerald_count = 0;
    for (int i = 0; i < 7; i++) {
        uint8_t v = r8(RAM_Collected_emeralds_array + (uint32_t)i);
        s_res_got[i] = (uint8_t)(s_super ? v == 3 : v != 0);
    }
    if (Got_Emerald && Current_Special_Stage >= 1 && Current_Special_Stage <= 7) s_res_got[Current_Special_Stage - 1] = 1;
    for (int i = 0; i < 7; i++) Emerald_count = (uint8_t)(Emerald_count + s_res_got[i]);
    Update_HUD_score = 1;
    Update_Bonus_score = 1;
    Level_Inactive_flag = 0;
    tri_music_play_s2(TRI_SLOT_ZONE, MUS_ENDLEVEL);
    memset(s_o, 0, sizeof s_o);
    memset(s_ndisp, 0, sizeof s_ndisp);
    B(SpecialStageResults, O_id) = ID_SSRESULTS;
}

static void run_objects(void)                        /* RunObjects */
{
    s_abort = 0;
    for (int o = 0; o < NOBJ && !s_abort; o++) {
        s_cur = o;
        if (B(o, O_id)) s_cost_objs++;
        switch (B(o, O_id)) {
        case ID_SONICSS: Obj09(o); break;
        case ID_TAILSSS: Obj10(o); break;
        case ID_SSEMERALD: Obj59(o); break;
        case ID_SSMESSAGE: Obj5A(o); break;
        case ID_SSRINGSPILL: Obj5B(o); break;
        case ID_SSHUD: Obj5E(o); break;
        case ID_STARTBANNER: Obj5F(o); break;
        case ID_SSRING: Obj60(o); break;
        case ID_SSBOMB: Obj61(o); break;
        case ID_SSSHADOW: Obj63(o); break;
        case ID_SSNUMBEROFRINGS: Obj87(o); break;
        case ID_SSRESULTS: Obj6F(o); break;
        case ID_SSTAILSTAILS: Obj88(o); break;
        default: break;
        }
    }
}

/* ========================================================================
 * SpecialStage: the scene, one call per frame from GameLoop
 * ====================================================================== */
#define Z80_PAUSE_FLAG  0x1C10u                     /* zPauseFlag */

enum { P_FADE_OUT, P_SETUP, P_WAIT0, P_PREDRAW, P_MUSIC, P_FADE_IN, P_LOOP_A, P_LOOP_B, P_PAUSED,
       P_END_FADE, P_RES_SETUP, P_RESULTS, P_RES_FADE, P_DONE };
static int s_vint_ss;                                /* the VInt this frame followed was VintID_S2SS */
static int s_pause_return, s_return;                 /* the phase to resume; where the game goes after */
static void (*s_done_cb)(int got_emerald);

static uint8_t s_held_1, s_held_2;
static void read_pads(void)                          /* VInt's ReadJoypads (S3K polled the pads) */
{
    uint8_t h1 = r8(RAM_Ctrl_1_held), h2 = r8(0xFFF606);
    Ctrl_1 = (uint16_t)(h1 << 8 | (uint8_t)(h1 & ~s_held_1));   /* (pressed = new since the last read: */
    Ctrl_2 = (uint16_t)(h2 << 8 | (uint8_t)(h2 & ~s_held_2));   /*  a lag frame doesn't read them) */
    s_held_1 = h1;
    s_held_2 = h2;
}

/* The hardware's lag: S2's main loop often overruns a frame here, and the
 * VInt that finds it unfinished does nothing but the sound driver (Vint_Lag)
 * - everything holds for a frame. The pass's 68K time is estimated from its
 * work (track tiles decoded, RLE runs and words, objects run, sprite pieces
 * built); weights and budget fitted to the real cartridge's frame-by-frame
 * timing over a whole stage (tools/trilogy/s2ss_lagfit.py: 94% of passes, total lag
 * within 2%). */
static int s_lag_next;
static int pass_overruns(void)
{
    double c = 150.0 * s_cost_unc + 34.9 * s_cost_rle + 3.6 * s_cost_words + 230.9 * s_cost_objs + 313.9 * s_nspr;
    return c > 44106.0;
}

static void music_pause(int on) { g_machine.bus.z80_ram[Z80_PAUSE_FLAG] = on ? 1 : 0x80; }

static int pause_game(int resume)                    /* PauseGame: 1 = now paused */
{
    if (!((uint8_t)Ctrl_1 & BTN_START) && !((uint8_t)Ctrl_2 & BTN_START)) return 0;
    music_pause(1);
    s_pause_return = resume;
    s_phase = P_PAUSED;
    return 1;
}

static void load_stage_art(void)                     /* PlrList_SpecialStage (+ the track art, ssLdComprsdData) */
{
    load_art(SS_TRACKART, 0);
    load_art(SS_ART_EMERALD, AT_EMERALD);
    load_art(SS_ART_MESSAGES, AT_MESSAGES);
    load_art(SS_ART_HUD, AT_HUD);
    load_art(SS_ART_FLATSHADOW, AT_FLATSHADOW);
    load_art(SS_ART_DIAGSHADOW, AT_DIAGSHADOW);
    load_art(SS_ART_SIDESHADOW, AT_SIDESHADOW);
    load_art(SS_ART_EXPLOSION, AT_EXPLOSION);
    load_art(SS_ART_RINGS, AT_RINGS);
    load_art(SS_ART_START, AT_START);
    load_art(SS_ART_BACK, AT_BACK);
    load_art(SS_ART_STARS, AT_STARS);
    load_art(SS_ART_TAILSTEXT, AT_TAILSTEXT);
    /* VInt_6 sends H_scroll_buffer to $F000: let it carry the star art that lives there */
    for (uint32_t k = 0; k < 0x380; k++) w8(RAM_H_scroll_buffer + k, vram()[0xF000 + k]);
}

static void ss_setup(void)                           /* SpecialStage, from the VDP set-up to the objects */
{
    memcpy(s_saved_regs, g_machine.vdp.reg, sizeof s_saved_regs);
    set_reg(11, 0x03);                               /* V scroll by screen, H scroll by line */
    set_reg(0, 0x04);                                /* H-INT disabled */
    set_reg(2, PNT_A1 >> 10);
    set_reg(4, PNT_B >> 13);
    set_reg(12, 0x08);                               /* H32, shadow/highlight */
    set_reg(16, 0x03);                               /* 128x32 planes */
    set_reg(7, 0x00);
    set_reg(13, HSCROLL >> 10);
    set_reg(5, 0xF800 >> 9);
    set_reg(1, (uint8_t)(g_machine.vdp.reg[1] & 0xBF));
    vram_fill(PNT_A2, 0x2000);
    vram_fill(PNT_A1, 0x2000);
    vram_fill(PNT_B, 0x2000);
    vram_fill(HSCROLL, 0x400);
    Vscroll_Factor_FG = Vscroll_Factor_BG = 0;
    SpecialStage_Started = 0;
    for (uint32_t a = 0; a < 0x284; a += 2) w16(RAM_Sprite_table + a, 0);
    memset(SS_Horiz_Scroll_Buf_1, 0, sizeof SS_Horiz_Scroll_Buf_1);
    /* SS_Shared_RAM */
    SSTrack_anim = SSTrack_last_anim_frame = SpecialStage_CurrentSegment = SSTrack_anim_frame = 0;
    SS_Alternate_PNT = SSTrack_drawing_index = SSTrack_Orientation = SS_Alternate_HorizScroll_Buf = 0;
    SSTrack_mapping_frame = SS_Last_Alternate_HorizScroll_Buf = SSTrack_duration_timer = 0;
    SS_player_anim_frame_timer = SpecialStage_LastSegment = SSTrack_last_mapping_frame = 0;
    SSTrack_last_mappings = SSTrack_last_mappings_copy = 0;
    SS_New_Speed_Factor = SS_Cur_Speed_Factor = 0;
    memset(SSDrawRegBuffer, 0, sizeof SSDrawRegBuffer);
    memset(SS_Ctrl_Record_Buf, 0, sizeof SS_Ctrl_Record_Buf);
    SS_Check_Rings_flag = SS_Pause_Only_flag = 0;
    SS_HideRingsToGo = SS_TriggerRingsToGo = SS_NoCheckpoint_flag = SS_NoCheckpointMsg_flag = 0;
    SS_Checkpoint_Rainbow_flag = SS_Rainbow_palette = SS_Star_color_1 = SS_Swap_Positions_Flag = 0;
    SS_Perfect_rings_left = SS_RingsToGoBCD = SS_NoRingsTogoLifetime = 0;
    Tails_control_counter = 0;
    memset(s_o, 0, sizeof s_o);
    memset(s_ndisp, 0, sizeof s_ndisp);
    ssInitTableBuffers();
    load_stage_art();
    s_player_mode = r16(RAM_Player_mode);
    if (s_player_mode > 2) s_player_mode = 1;
    if (s_player_mode <= 1) B(MainCharacter, O_id) = ID_SONICSS;
    if (s_player_mode != 1) B(Sidekick, O_id) = ID_TAILSSS;
    B(SpecialStageHUD, O_id) = ID_SSHUD;
    B(SpecialStageStartBanner, O_id) = ID_STARTBANNER;
    B(SpecialStageNumberOfRings, O_id) = ID_SSNUMBEROFRINGS;
    SS_Offset_X = 0x80;
    SS_Offset_Y = 0x36;
    SSPlaneB_Background();
    SSInitPalAndData();
    SS_New_Speed_Factor = 0xC0000;
    Ctrl_1_Logical = Ctrl_2_Logical = 0;
}

static void ss_frame_vdp(void)                       /* the RAM VInt_6 sends next */
{
    w8(RAM_Water_full_screen_flag, 0);
    w16(RAM_V_scroll_value, Vscroll_Factor_FG);
    w16(RAM_V_scroll_value_BG, Vscroll_Factor_BG);
}

static void ss_finish(void);

static void s2ss_frame(void)
{
    if (s_lag_next) {                                /* Vint_Lag: nothing moves this frame */
        s_lag_next = 0;
        Vint_runcount++;
        ss_frame_vdp();
        w8(RAM_V_int_routine, 6);
        tri_jsr(S3K_Wait_VSync);
        return;
    }
    read_pads();
    if (s_phase >= P_RESULTS) results_hud();         /* (Vint_Level's HudUpdate) */
    else if (s_phase > P_SETUP) {                    /* (the level's VRAM until the stage is set up) */
        if (s_vint_ss) vint_s2ss(); else vint_plain();
    }
    Vint_runcount++;
    int ss_vint = 0;
    switch (s_phase) {
    case P_FADE_OUT:                                 /* Pal_FadeToWhite over the level */
        fade_to_white_step();
        if (--s_timer <= 0) s_phase = P_SETUP;
        break;
    case P_SETUP:
        ss_setup();
        ss_vint = 1;
        s_phase = P_WAIT0;
        break;
    case P_WAIT0:                                    /* until the track timer starts a frame */
        ss_vint = 1;
        if (SSTrack_drawing_index) break;
        SSTrack_Draw();
        s_phase = P_PREDRAW;
        break;
    case P_PREDRAW:
        SSTrack_Draw();
        SSLoadCurrentPerspective();
        SSObjectsManager();
        ss_vint = 1;
        if (SSTrack_duration_timer != 1) break;
        s_cur = SpecialStageHUD;
        Obj5A_CreateRingsToGoText();
        SS_ScrollBG();
        run_objects();
        build_sprites();
        ss_vint = 0;                                 /* VintID_CtrlDMA */
        s_phase = P_MUSIC;
        break;
    case P_MUSIC:
        tri_music_play_s2(TRI_SLOT_ZONE, MUS_SPECSTAGE);
        set_reg(1, (uint8_t)(g_machine.vdp.reg[1] | 0x40));
        for (int i = 0; i < 64; i++) set_pal(i, 0xEEE);   /* Pal_FadeFromWhite */
        s_phase = P_FADE_IN;
        s_timer = 0x16;
        break;
    case P_FADE_IN:
        fade_from_white_step();
        if (--s_timer <= 0) { s_phase = P_LOOP_A; Ctrl_1_Logical = Ctrl_1; Ctrl_2_Logical = Ctrl_2; ss_vint = 1; }
        break;
    case P_LOOP_A:
        SSTrack_Draw();
        SSSetGeometryOffsets();
        SSLoadCurrentPerspective();
        SSObjectsManager();
        SS_ScrollBG();
        run_objects();
        build_sprites();
        ss_vint = 1;
        if (SpecialStage_Started) {
            load_art(SS_ART_BOMB, AT_BOMB);          /* PLCID_SpecStageBombs */
            s_phase = P_LOOP_B;
            if (pause_game(P_LOOP_B)) ss_vint = 0;
            break;
        }
        if (pause_game(P_LOOP_A)) { ss_vint = 0; break; }
        Ctrl_1_Logical = Ctrl_1;                     /* (copied before the next VInt reads the pads) */
        Ctrl_2_Logical = Ctrl_2;
        break;
    case P_LOOP_B:
        SSTrack_Draw();
        SSSetGeometryOffsets();
        SSLoadCurrentPerspective();
        SSObjectsManager();
        SS_ScrollBG();
        PalCycle_SS();
        if (SS_Pause_Only_flag) {
            Ctrl_1_Logical = Ctrl_1 & ((BTN_START << 8) | BTN_START);
            Ctrl_2_Logical = Ctrl_2 & ((BTN_START << 8) | BTN_START);
        } else {
            Ctrl_1_Logical = Ctrl_1;
            Ctrl_2_Logical = Ctrl_2;
        }
        {
            static int rings = -1;                   /* development: TRILOGY_SSRINGS=n holds Sonic's rings at n */
            if (rings < 0) { const char *g = getenv("TRILOGY_SSRINGS"); rings = g && *g ? atoi(g) : 0; }
            if (rings) Ring_count = (uint16_t)rings;
        }
        run_objects();
        if (SS_Check_Rings_flag) {                   /* the stage is over */
            Emerald_count &= 7;
            if (Ring_count + Ring_count_2P == SS_Perfect_rings_left) Perfect_rings_flag = 0xFF;
            s_phase = P_END_FADE;
            s_timer = 0x16;
            break;
        }
        build_sprites();
        ss_vint = 1;
        if (pause_game(P_LOOP_B)) ss_vint = 0;
        break;
    case P_PAUSED:                                   /* Pause_Loop */
        if (((uint8_t)Ctrl_1 & BTN_START) || ((uint8_t)Ctrl_2 & BTN_START)) {
            music_pause(0);
            s_phase = s_pause_return;
            ss_vint = 1;                             /* (the loop's own WaitForVint follows) */
        }
        break;
    case P_END_FADE:                                 /* Pal_FadeToWhite */
        fade_to_white_step();
        if (--s_timer <= 0) s_phase = P_RES_SETUP;
        break;
    case P_RES_SETUP:                                /* the results */
        results_setup();
        s_phase = P_RESULTS;
        break;
    case P_RESULTS:
        run_objects();
        build_sprites();
        if (Level_Inactive_flag) {
            o_sfx_s2(SFX_SSENTRY);
            s_phase = P_RES_FADE;
            s_timer = 0x16;
        }
        break;
    case P_RES_FADE:                                 /* Pal_FadeToWhite, then the level */
        fade_to_white_step();
        if (--s_timer <= 0) s_phase = P_DONE;
        break;
    default:
        ss_finish();
        return;
    }
    s_vint_ss = ss_vint;
    {
        static int lag = -1;                         /* development: TRILOGY_SSNOLAG=1 runs every frame */
        if (lag < 0) { const char *g = getenv("TRILOGY_SSNOLAG"); lag = !(g && *g); }
        if (lag && (s_phase == P_LOOP_A || s_phase == P_LOOP_B) && ss_vint && pass_overruns())
            s_lag_next = 1;
    }
    {
        static int log = -1;
        if (log < 0) log = getenv("TRILOGY_SSLOG") != NULL;    /* development: per-frame state */
        if (log)
            fprintf(stderr, "[ss] ph=%d idx=%d mf=%02X seg=%d anim=%d af=%d vs=%04X x=%04X y=%04X ang=%02X rings=%d"
                    " cost=%d,%d,%d,%d,%d,%d\n",
                    s_phase, SSTrack_drawing_index, SSTrack_mapping_frame, SpecialStage_CurrentSegment, SSTrack_anim,
                    SSTrack_anim_frame, Vscroll_Factor_BG, W(0, O_x), W(0, O_y), B(0, O_angle), Ring_count,
                    s_cost_unc, s_cost_rle, s_cost_words, s_cost_flip, s_cost_objs, s_nspr);
    }
    s_cost_unc = s_cost_rle = s_cost_words = s_cost_flip = s_cost_objs = 0;
    ss_frame_vdp();
    w8(RAM_V_int_routine, 6);
    tri_jsr(S3K_Wait_VSync);
}

static void ss_finish(void)
{
    s_active = 0;
    memcpy(g_machine.vdp.reg, s_saved_regs, sizeof s_saved_regs);
    for (uint32_t a = 0; a < 0x380; a += 2) w16(RAM_H_scroll_buffer + a, 0);
    for (uint32_t a = 0; a < 0x280; a += 2) w16(RAM_Sprite_table + a, 0);
    for (int i = 0; i < 64; i++) set_pal(i, 0);      /* (the level comes in from black, as from any start) */
    vram_fill(0x0000, 0x10000);                      /* (no stage or results art where the level's arrives) */
    fprintf(stderr, "[trilogy] S2 special stage %d over: %s (rings %d/%d)\n", Current_Special_Stage + (Got_Emerald ? 0 : 1),
            Got_Emerald ? "emerald" : "no emerald", Ring_count + Ring_count_2P, SS_Perfect_rings_left);
    if (s_done_cb) s_done_cb(Got_Emerald != 0);
}

/* ---- the trilogy's side ------------------------------------------------------ */
void tri_s2ss_start(int stage, void (*done)(int got_emerald))
{
    Current_Special_Stage = (uint8_t)(stage >= 7 ? 0 : stage);
    s_done_cb = done;
    {   extern void tri_s2_sound_set(int on); tri_s2_sound_set(1); }   /* (Sonic 2's sounds, from any zone) */
    s_active = 1;
    s_phase = P_FADE_OUT;
    s_timer = 0x16;
    s_vint_ss = 0;
    Vint_runcount = 0;
    o_sfx_s2(SFX_SSENTRY);
    tri_music_fade();
    w8(RAM_Game_mode, GM_SCENE);
    fprintf(stderr, "[trilogy] S2 special stage %d\n", Current_Special_Stage + 1);
}

int tri_s2ss_active(void) { return s_active; }

int tri_s2ss_hook(uint32_t pc)
{
    if (pc != TH_GAMELOOP || !s_active) return 0;
    if (r8(RAM_Game_mode) != GM_SCENE) { s_active = 0; return 0; }
    A(0) = tri_native(s2ss_frame);
    return 0;
}
