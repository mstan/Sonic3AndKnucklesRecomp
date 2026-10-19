/*
 * scd_mmz_trap.c — Metallic Madness's traps (Sonic CD; decomp: R8\TRAP_R81.C,
 * TRAP_R82.C, TOBIRA.C; the disc's code is the truth where they differ).
 *
 *   $36 spikes (harir8)   both acts (subtypes $FB-$FF; the block it is named
 *                         for, togedair8, positive subtypes, is not placed):
 *                         odd subtypes in act 2 / $FF in act 1 point up and
 *                         hurt who stands on them; the others hang and hurt
 *                         by touch from below. Act 2's $FD are the front
 *                         layer's (drawn over it, solid only while player 1
 *                         is on it), $FB the back layer's (solid only while
 *                         he is on the back: scd_mmz_back, CD's prio_flag);
 *                         act 1 is solid only with the player within 32 px
 *                         up or down. Subtype $F8 (act 2's past only) is a
 *                         block, solid only from the back layer
 *   $33 spiked boards     (act 2) a set of three boards with spikes under
 *       (togeita)         them; with player 1 below (to 256 px) they sink at
 *                         1/2 px a frame until each meets the floor; the
 *                         first set's first board, while he is near, sets
 *                         another set 16 px higher each time at 300, 600, 840
 *                         and 1080 frames (four more); solid boards, the
 *                         spikes' touch (subtype $FF: a board's spikes)
 *   $38 flap bar          (act 1) turns from upright to flat and back: 121
 *       (futagor8)        frames upright, 121 flat, 5 a step between, a 33
 *                         frame stop half way (only its pivot); solid as it
 *                         stands
 *   $29 door (tobira)     (act 2) two halves; player 1 coming up to it from
 *                         the side it opens to (subtype 1 the right, 2 the
 *                         left) opens it, 2 px a frame each way; it shuts
 *                         once he is out of its range. Nothing else opens it
 *                         (its "open now" flag, oVar3F, is never set: no
 *                         switch drives it)
 * Their code is the same in every period of an act; $36 differs between the
 * acts (tri_act). CD's object variables keep their own offsets where they are
 * $30 or more (free in S3K's object), the others sit at $30.. as named;
 * routines keep CD's even numbers. SolidObject -> o_solid, GetOffObject ->
 * cd_get_off; objPlayerSlot -> Player 1.
 */
#include "trilogy_obj.h"

int scd_mmz_back(void);                    /* scd_mmz.c: CD's prio_flag (1: player 1 on the back layer) */
int scd_time_warping(void);                /* scd_time.c */


static int s_base;                         /* this round's native slots (scd_obj.c) */
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static void draw_despawn(uint32_t o, uint16_t x)     /* DrawObject + CheckObjDespawn2 */
{
    if (cd_despawn(o, x)) return;
    o_draw(o);
}

static void solid(uint32_t o)                        /* SolidObject: CD's width and y radius */
{
    int h = OB8(o, OBJ_y_radius);
    o_solid(o, OB8(o, OBJ_width_px) + 10, h, h + 1, OB16(o, OBJ_x_pos));
}

/* SolidObject, called only with player 1 within 32 px up or down of it (S3K:
 * each player by his own place; player 2 only on screen, as SolidObjectFull) */
static void solid_near(uint32_t o)
{
    int h = OB8(o, OBJ_y_radius);
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (n && !(OB8(p, OBJ_render) & 0x80)) continue;
        int16_t dy = (int16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos));
        if ((dy < 0 ? -dy : dy) >= 0x20) continue;
        o_solid_1p(o, n, OB8(o, OBJ_width_px) + 10, h, h + 1, OB16(o, OBJ_x_pos), 0);
    }
}

/* ---- $36 spikes (harir8), the back layer's block (tokusyu_block) --------- */
#define SP_Y0           0x32               /* oVar32 (kept, never read) */
#define SP_X0           0x36               /* oVar36 */

/* stood on (pointing up): hurt, unless invincible, flashing, hurt already
 * (S3K's spikes, sub_24280, as CD's: the fall undone, then HurtPlayer) or
 * time warping (CD's plpower_a: player 1) */
static void spikes_hurt(uint32_t o)
{
    for (int n = 0; n < 2; n++) {
        if (!(OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND))) continue;
        if (!n && scd_time_warping()) continue;
        o_spike_hurt(o, o_player(n));
    }
}

static void spikes_act1(uint32_t o)                          /* TRAP_R81 harir8_move */
{
    if (!OB8(o, OBJ_frame)) {
        solid_near(o);
        spikes_hurt(o);
    } else if (OB8(o, OBJ_render) & 0x80)
        solid_near(o);
}

static void spikes_act2(uint32_t o)                          /* TRAP_R82 harir8_move */
{
    uint8_t d5 = (uint8_t)-OB8(o, OBJ_subtype);
    int on = 1;
    if (d5 >= 5) {                                            /* ($FB) the back layer's */
        SET16(o, OBJ_priority, 3 << 7);
        on = scd_mmz_back() != 0;
    } else if (d5 >= 3) {                                     /* ($FD) the front layer's: drawn over it */
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        on = !scd_mmz_back();
    }
    if (d5 >= 3) SET8(o, OBJ_coll_flags, on ? tri_scd_coll(0x83) : 0);
    if (!on) { cd_get_off(o); return; }                       /* (the other layer's: not there) */
    if (!OB8(o, OBJ_frame)) {                                 /* (pointing up: no touch, only stood on) */
        SET8(o, OBJ_coll_flags, 0);
        solid(o);
        spikes_hurt(o);
    } else if (OB8(o, OBJ_render) & 0x80)
        solid(o);
}

static void layer_block(uint32_t o)                          /* tokusyu_block: subtype $F8 */
{
    if (!OB8(o, OBJ_routine)) {                               /* (CD sets these every frame) */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_MMZ_LAYERBLOCK, OB8(o, OBJ_render) | 4, 3, 24, 16);
        SET8(o, OBJ_y_radius, 16);
    }
    if (scd_mmz_back()) solid(o);                             /* (solid from the back layer only) */
    else cd_get_off(o);
    draw_despawn(o, OB16(o, OBJ_x_pos));
}

static void spikes(uint32_t o)                               /* togedair8 */
{
    int act2 = tri_act() == 1;
    uint8_t sub = OB8(o, OBJ_subtype);
    if (act2 && sub == 0xF8) { layer_block(o); return; }
    if (!(sub & 0x80)) { o_delete(o); return; }               /* (the block with a spring or spikes: none placed) */
    if (!OB8(o, OBJ_routine)) {                               /* harir8_init */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_MMZ_SPIKES, OB8(o, OBJ_render) | 4, act2 ? 1 : 4, 16, 8);
        SET16(o, SP_X0, OB16(o, OBJ_x_pos));
        SET16(o, SP_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_y_radius, 8);
        if (act2 ? !(sub & 1) : sub != 0xFF) {                /* (hanging: frame 1, touched) */
            SET8(o, OBJ_frame, 1);
            SET8(o, OBJ_width_px, 18);
            SET8(o, OBJ_coll_flags, tri_scd_coll(0x83));
        }
    }
    if (act2) spikes_act2(o); else spikes_act1(o);
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    draw_despawn(o, OB16(o, SP_X0));
}

/* ---- $33 spiked boards (togeita) ------------------------------------------ */
#define TG_COUNT        0x30               /* oVar30 (word): (a set's first) frames player 1 is near */
#define TG_Y0           0x32               /* oVar32: (a set's first) where it was placed */
#define TG_LINK         0x34               /* oVar34 (word): (a board) its spikes; (the spikes) their board */
#define TG_X0           0x36               /* oVar36: the set's place */
#define TG_SETS         0x3B               /* oVar3B: (the first set's first) sets so far */
#define TG_KIND         0x3C               /* oVar3C: (a board) its kind of set; (the spikes) the board's frame */
#define TG_NTH          0x3D               /* oVar3D: 0 a set's first board, 1-2 the others */
#define TG_DOWN         0x3F               /* oVar3F: on the floor */
/* setitano: the kind of set (0-2) by subtype */
static const uint8_t tg_kinds[56] = {
    0, 1, 2, 1, 0, 2, 0, 0, 1, 2, 1, 2, 1, 2, 1, 2, 0, 2, 1, 2, 0, 1, 2, 1, 1, 2, 1, 2,
    2, 1, 2, 2, 1, 2, 1, 1, 0, 1, 2, 0, 1, 2, 2, 1, 0, 1, 2, 1, 1, 0, 2, 1, 0, 1, 2, 2,
};
/* togeita_data0-2: a set's boards (first, second, third) by its kind: frame,
 * x from the set's place, width, y radius (frame 3 has no spikes, no solidity) */
static const int8_t tg_boards[3][3][4] = {
    { { 1, -52, 28, 8 }, { 4, -68, 12, 8 }, { 5, -36, 44, 8 } },
    { { 3, 0, 24, 8 }, { 3, -32, 24, 8 }, { 3, 32, 24, 8 } },
    { { 2, 52, 28, 8 }, { 5, 36, 44, 8 }, { 4, 68, 12, 8 } },
};

static const int8_t *tg_board(uint32_t o, int nth)
{
    return tg_boards[nth][OB8(o, TG_KIND) % 3];
}

static void tg_size(uint32_t a, const int8_t *d)
{
    SET8(a, OBJ_frame, d[0]);
    SET8(a, OBJ_width_px, d[2]);
    SET8(a, OBJ_y_radius, d[3]);
    SET8(a, OBJ_height_px, d[3]);
}

/* togeita_plchk_1: player 1 within d1 px either side of the set's place */
static int tg_near(uint32_t o, int d1)
{
    uint16_t d0 = (uint16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, TG_X0) + d1);
    return !(d0 & 0x8000) && d0 < d1 * 2;
}

/* itaset_chk: (the first set's first board) player 1 within 128 px above it
 * (or anywhere below) and 80 px either side: another set, higher */
static void tg_more(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    uint16_t py = OB16(p, OBJ_y_pos), y = OB16(o, OBJ_y_pos);
    if (py < y && (uint16_t)(y - py) >= 0x80) return;
    if (!tg_near(o, 0x50)) return;                            /* (the disc's: 80, not the decomp's 160) */
    uint16_t n = (uint16_t)(OB16(o, TG_COUNT) + 1);
    SET16(o, TG_COUNT, n);
    if (n != 300 && n != 600 && n != 840 && n != 1080) return;
    unsigned k = OB8(o, TG_SETS);
    if (k >= 5) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, CD_CODE(0x33));
    SET16(a1, OBJ_x_pos, OB16(o, TG_X0));
    SET8(a1, OBJ_subtype, k);
    SET16(a1, OBJ_y_pos, OB16(o, TG_Y0) - k * 16);
    SET8(o, TG_SETS, k + 1);
}

static void tg_sink(uint32_t o)                              /* togeita_move2 */
{
    static const uint8_t feet[6] = { 8, 32, 56, 80, 104, 128 };   /* (by subtype: the set's height) */
    if (!OB8(o, OBJ_subtype) && !OB8(o, TG_NTH)) tg_more(o);
    if (OB8(o, TG_DOWN)) return;
    uint32_t y = r32(o + OBJ_y_pos);
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + feet[OB8(o, OBJ_subtype) % 6]);
    int16_t d1 = o_floor_dist(o);
    SET32(o, OBJ_y_pos, y);
    if (d1 >= 0) {
        SET32(o, OBJ_y_pos, y + 0x8000);
        SET16(o, OBJ_y_vel, 0x80);
        return;
    }
    SET16(o, OBJ_y_vel, 0);
    SET16(o, TG_LINK, 0);
    SET8(o, TG_DOWN, 0xFF);
}

static void tg_spikes(uint32_t o)                            /* togeitax: a board's spikes (unseen: frame 6) */
{
    static const uint8_t touch[6] = { 0, 0x9C, 0x9C, 0, 0x9B, 0x9D };   /* (by the board's frame) */
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TOGEITA, OB8(o, OBJ_render) | 4, 0, 16, 8);
        SET8(o, OBJ_frame, 6);
    }
    uint32_t par = 0xFF0000u | OB16(o, TG_LINK);
    if (!OB16(o, TG_LINK) || r32(par) != CD_CODE(0x33)) { o_delete(o); return; }
    SET16(o, OBJ_y_vel, OB16(par, OBJ_y_vel));
    SET16(o, OBJ_y_pos, OB16(par, OBJ_y_pos) + 12);
    uint8_t c = touch[OB8(o, TG_KIND) % 6];
    SET8(o, OBJ_coll_flags, c ? tri_scd_coll(c) : 0);
    if (c) o_touch(o);
    o_draw(o);
}

static void togeita(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { tg_spikes(o); return; }
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                                 /* togeita_init */
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TOGEITA, OB8(o, OBJ_render) | 4, 3, OB8(o, OBJ_width_px), OB8(o, OBJ_height_px));
        unsigned sub = OB8(o, OBJ_subtype);
        static const uint8_t act3[8] = { 0, 1, 0, 2, 0, 0, 0, 0 };   /* (act 3's table: its first 8 differ) */
        SET8(o, TG_KIND, sub < 8 && tri_act() == 2 ? act3[sub] : sub < sizeof tg_kinds ? tg_kinds[sub] : 0);
        if (!OB8(o, TG_NTH)) {                                /* (a set's first: it places the others) */
            const int8_t *d = tg_board(o, 0);
            tg_size(o, d);
            SET16(o, TG_X0, OB16(o, OBJ_x_pos));
            SET16(o, TG_Y0, OB16(o, OBJ_y_pos));
            SET16(o, OBJ_x_pos, OB16(o, TG_X0) + d[1]);
            SET8(o, TG_SETS, 1);
            for (int k = 1; k < 3; k++) {
                uint32_t a1 = o_alloc();
                if (!a1) { o_delete_respawnable(o); return; } /* (tr8_fout) */
                SET32(a1, OBJ_code, CD_CODE(0x33));
                SET8(a1, TG_NTH, k);
                d = tg_board(o, k);                           /* togeita_dataset */
                tg_size(a1, d);
                SET16(a1, TG_X0, OB16(o, TG_X0));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(a1, OBJ_x_pos, OB16(o, TG_X0) + d[1]);
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            }
        }
        if (OB8(o, OBJ_frame) == 3) break;                    /* (no spikes) */
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, CD_CODE(0x33));
        SET8(a1, OBJ_subtype, 0xFF);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET8(a1, TG_KIND, OB8(o, OBJ_frame));
        SET16(a1, TG_LINK, (uint16_t)o);
        SET16(o, TG_LINK, (uint16_t)a1);
        break;
    }
    case 2: {                                                 /* togeita_move1: player 1 below, to 256 px */
        uint16_t dy = (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos));
        if (OB16(p, OBJ_y_pos) < OB16(o, OBJ_y_pos) || dy >= 0x100) break;
        if (tg_near(o, OB8(o, OBJ_subtype) ? 0x50 : 0x30)) SET8(o, OBJ_routine, 4);
        break;
    }
    case 4:
        tg_sink(o);
        break;
    default:                                                  /* (togeita_move3: never reached) */
        break;
    }
    if (OB8(o, OBJ_frame) != 3 && (OB8(o, OBJ_render) & 0x80)) solid_near(o);   /* togeita_ridechk */
    draw_despawn(o, OB16(o, TG_X0));
}

/* ---- $38 flap bar (futagor8): turns, upright to flat and back ------------- */
#define FG_Y0           0x32               /* oVar32 (kept, never read) */
#define FG_X0           0x36               /* oVar36 */
#define FG_TIME         0x3A               /* oVar3A */
#define FG_STEP         0x3C               /* oVar3C */
/* futagor8_pchg: frame, frames (+1), width, y radius (steps 10 and 11 keep
 * flat boxes for upright frames: so on the disc too) */
static const uint8_t fg_steps[12][4] = {
    { 0, 120, 8, 56 }, { 1, 4, 8, 40 }, { 2, 4, 8, 24 }, { 3, 32, 8, 8 }, { 4, 4, 24, 8 }, { 5, 4, 40, 8 },
    { 6, 120, 56, 8 }, { 5, 4, 40, 8 }, { 4, 4, 24, 8 }, { 3, 32, 8, 8 }, { 2, 4, 24, 8 }, { 1, 4, 40, 8 },
};

static void futagor(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_FUTAGOR, OB8(o, OBJ_render) | 4, 3, 8, 56);
        SET16(o, FG_X0, OB16(o, OBJ_x_pos));
        SET16(o, FG_Y0, OB16(o, OBJ_y_pos));
    }
    if (!OB8(o, FG_TIME)) {                                   /* (a step: its frame, time and box) */
        const uint8_t *s = fg_steps[OB8(o, FG_STEP) % 12];
        SET8(o, OBJ_frame, s[0]);
        SET8(o, FG_TIME, s[1]);
        SET8(o, OBJ_width_px, s[2]);
        SET8(o, OBJ_y_radius, s[3]);
        SET8(o, OBJ_height_px, s[3]);
    } else {
        SET8(o, FG_TIME, OB8(o, FG_TIME) - 1);
        if (!OB8(o, FG_TIME)) SET8(o, FG_STEP, OB8(o, FG_STEP) + 1 >= 12 ? 0 : OB8(o, FG_STEP) + 1);
    }
    /* (upright: CD's SolidObject takes its y speed $100 into a player on top,
     *  then puts him on the floor if he is in it; S3K's solidity takes none) */
    SET16(o, OBJ_y_vel, OB8(o, OBJ_frame) ? 0 : 0x100);
    solid(o);
    draw_despawn(o, OB16(o, FG_X0));
}

/* ---- $29 door (tobira): two halves part as player 1 comes ----------------- */
#define TB_SHUT         0x30               /* oVar30: its y, shut */
#define TB_OPEN         0x32               /* oVar32: its y, open */
#define TB_DOOR         0x34               /* oVar2A (word): (the top half) its door; 0: the door */
#define TB_HALF         0x36               /* oVar2C (word): (the door) its top half */
#define TB_MID          0x38               /* oVar2E: the doorway's middle (its y - 16) */
#define TB_NOW          0x3F               /* oVar3F: open now (nothing sets it) */

static void tb_half(uint32_t o, uint32_t a1, int asset)      /* ini_com */
{
    o_init(a1, asset, OB8(a1, OBJ_render) | 4, 3, 8, 16);
    SET8(a1, OBJ_y_radius, 16);
    SET16(a1, OBJ_y_pos, OB16(o, TB_MID));
    SET16(a1, TB_SHUT, OB16(o, TB_MID));
    SET16(a1, TB_OPEN, OB16(o, TB_MID));
}

/* m_area: player 1 from d4 to d4 + d5 px right of it, and within 48 px of
 * the doorway's middle up or down */
static int tb_near(uint32_t o, int d4, int d5)
{
    uint32_t p = RAM_Player_1;
    if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) - d4) >= (uint16_t)d5) return 0;
    return (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, TB_MID) + 0x30) < 0x60;
}

static void tb_solid(uint32_t o, uint32_t a1)                /* ms_hitchk: the top half, then the door */
{
    o_solid(a1, 8 + 10, 16, 17, OB16(a1, OBJ_x_pos));
    o_solid(o, 8 + 10, 16, 17, OB16(o, OBJ_x_pos));
}

static void tobira(uint32_t o)
{
    if (OB16(o, TB_DOOR)) {                                   /* slave: the door moves and draws it */
        uint32_t par = 0xFF0000u | OB16(o, TB_DOOR);
        if (r32(par) != CD_CODE(0x29) || OB16(par, TB_HALF) != (uint16_t)o) o_delete(o);
        return;
    }
    int sub = OB8(o, OBJ_subtype), asset = sub == 1 ? SCDA_TOBIRA1 : SCDA_TOBIRA2;   /* (0, no light: unplaced) */
    uint32_t a1 = 0xFF0000u | OB16(o, TB_HALF);
    switch (OB8(o, OBJ_routine)) {
    case 0:                                                   /* m_init (then nothing more this frame) */
        SET8(o, OBJ_routine, 2);
        SET16(o, TB_MID, OB16(o, OBJ_y_pos) - 16);
        a1 = o_alloc_after(o);                                /* (FindNextObjSlot) */
        if (!a1) { o_delete(o); return; }
        SET32(a1, OBJ_code, CD_CODE(0x29));
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, TB_DOOR, (uint16_t)o);
        SET16(o, TB_HALF, (uint16_t)a1);
        tb_half(o, a1, asset);                                /* (the top half: 16 px up, opens 49 up) */
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 16);
        SET16(a1, TB_SHUT, OB16(a1, TB_SHUT) - 16);
        SET16(a1, TB_OPEN, OB16(a1, TB_OPEN) - 49);
        tb_half(o, o, asset);                                 /* (the door: 16 px down, opens 49 down) */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 16);
        SET16(o, TB_SHUT, OB16(o, TB_SHUT) + 16);
        SET16(o, TB_OPEN, OB16(o, TB_OPEN) + 49);
        return;
    case 2:                                                   /* m_closed: player 1 coming from its side */
        tb_solid(o, a1);
        if (OB8(o, TB_NOW)) {
            SET8(o, TB_NOW, 0);
            SET8(o, OBJ_routine, 4);
        } else if (sub && tb_near(o, sub == 1 ? 8 : -0x48, 0x40))
            SET8(o, OBJ_routine, 4);
        break;
    case 4:                                                   /* m_open: parting */
        tb_solid(o, a1);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 2);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 2);
        if (OBS16(o, OBJ_y_pos) >= OBS16(o, TB_OPEN)) {
            SET8(o, OBJ_routine, 6);
            SET16(o, OBJ_y_pos, OB16(o, TB_OPEN));
            SET16(a1, OBJ_y_pos, OB16(a1, TB_OPEN));
        }
        break;
    case 6:                                                   /* m_opend: till he is out of range */
        tb_solid(o, a1);
        if (sub && !tb_near(o, sub == 1 ? -0x28 : -0x48, 0x70)) SET8(o, OBJ_routine, 8);
        break;
    default:                                                  /* m_close: shutting */
        tb_solid(o, a1);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 2);
        SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 2);
        if (OBS16(o, OBJ_y_pos) <= OBS16(o, TB_SHUT)) {
            SET8(o, OBJ_routine, 2);
            SET16(o, OBJ_y_pos, OB16(o, TB_SHUT));
            SET16(a1, OBJ_y_pos, OB16(a1, TB_SHUT));
        }
        break;
    }
    uint32_t anim = o_asset_anim(asset);                      /* ms_end: its light blinks */
    if (anim) o_animate(o, anim);
    SET8(a1, OBJ_frame, OB8(o, OBJ_frame));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;            /* (CD: after drawing both; the half goes with it) */
    o_draw(a1);
    o_draw(o);
}

void scd_mmz_trap_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x29] = tobira;
    t[base + 0x33] = togeita;
    t[base + 0x36] = spikes;
    t[base + 0x38] = futagor;
}
