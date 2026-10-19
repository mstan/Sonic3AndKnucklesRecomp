/*
 * scd_mmz_plat.c — Metallic Madness's platforms (Sonic CD round 8, acts 1-2;
 * decomp: R8\KOMA8.C, KUZURE8.C, DAI8.C, PISTON.C; the disc's code is the
 * truth).
 *
 *   $0F rolling spring cart  comes down onto the floor, then rolls to and fro
 *                            at $100 (turning at a wall, a step of more than
 *                            7 px, or 80 px from its place; subtype bit 7: 64
 *                            px); a spring rides it 16 px up (subtype bit 1:
 *                            yellow)
 *   $25 collapsing floor     a row of 1-8 blocks (subtype); stood on, 9 frames
 *                            later it breaks a block at a time from the end
 *                            the player came from (8 frames a block), each into
 *                            two falling pieces; gone for good (CD clears its
 *                            respawn entry). Subtype negative: a solid block of
 *                            -subtype blocks that never breaks
 *   $2C materialising blocks (act 1) one to three 32 px blocks (subtype 0-2),
 *                            each after its own delay: appear, stay solid on top
 *                            for 2 s, vanish and come back, and again
 *   $34 piston               up and down on a sine (subtype bits 4-5 the
 *                            start, 6 the reach 32 / 48 px, 2-3 the speed);
 *                            type 1 (bits 0-1) is the tall launcher column: it
 *                            rests 61 frames at either end and, rising past
 *                            mid-way, throws whoever stands on it up at -$1000
 *                            (bit 7: -$A00)
 * The code of all four is the same in both acts and every period (cd_dis.py
 * cmp: only the data after the piston's code differs, the act's section art
 * tables). No subtype 2 is placed for any of them, none of them is on the back
 * layer or tests it (CD's SolidObject does not), nor miniature Sonic.
 * CD's object variables oVar2A.. sit at S3K's $30.. (free in S3K's object);
 * routines keep CD's even numbers. TopSolidObject -> o_platform, SolidObject
 * -> o_solid, GetOffObject -> cd_get_off; CD's objPlayerSlot -> both players
 * where it is only standing on it.
 */
#include "trilogy_obj.h"


#define OBJ_prev_anim   0x21               /* (CD's oPrevAnim; o_animate's) */
#define ST_AIR          0x02
#define ST_ON_OBJ       0x08               /* (a player's: on an object) */

static int s_base;

static void draw_despawn(uint32_t o)            /* DrawObject + CheckObjDespawn */
{
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $0F rolling spring cart -------------------------------------------- */
#define KM_Y0           0x32               /* CD $32: where it came down (never read) */
#define KM_X0           0x36               /* CD $36: its place */
#define KM_RANGE        0x3A               /* CD $3A: how far from it it rolls */
#define KM_SPR          0x38               /* (its spring) */
#define KM_MAP          0x3C               /* (the spring's mappings, low word: its mark) */
static void koma(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_MMZ_KOMA, OB8(o, OBJ_render) | 4, 4, 8, 8);
        SET8(o, OBJ_y_radius, 6);
        SET16(o, KM_X0, OB16(o, OBJ_x_pos));
        SET16(o, OBJ_x_vel, 0x100);
        uint32_t a1 = o_alloc_after(o);                       /* (FindNextObjSlot) */
        if (!a1) { o_delete(o); return; }
        /* (CD's spring $0A, its oVar34 the cart, oVar39 -16: one that follows
         * its parent; here S3K's, moved by the cart) */
        SET32(a1, OBJ_code, S3K_Obj_Spring);
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x10);
        SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype) & 2);      /* (CD: subtype & $F; bit 1 yellow) */
        SET16(o, KM_SPR, (uint16_t)a1);
        SET16(o, KM_RANGE, OB8(o, OBJ_subtype) & 0x80 ? 0x40 : 0x50);
    }   /* fall through */
    case 2: {                                                 /* down onto the floor */
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) { SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1); break; }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET16(o, KM_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_routine, 4);
        break;
    }
    default: {                                                /* rolling */
        int16_t d1 = OBS16(o, OBJ_x_vel) >= 0 ? o_wall_right(o, 0x20) : o_wall_left(o, -0x18);
        int back = d1 < 7;
        if (!back) {
            int16_t dx = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, KM_X0));
            back = (uint16_t)(dx < 0 ? -dx : dx) >= OB16(o, KM_RANGE);
        }
        if (!back) {
            d1 = o_floor_dist(o);
            back = d1 < -7 || d1 > 7;
            if (!back) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        }
        if (back) SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel));
        o_move(o);
        o_animate(o, o_asset_anim(SCDA_MMZ_KOMA));
        break;
    }
    }
    /* (its spring: S3K's replaces its code after its init; its mappings mark it) */
    uint32_t spr = 0xFF0000u | OB16(o, KM_SPR);
    int has = OB16(o, KM_SPR) && r32(spr + OBJ_code);
    if (has && !OB16(o, KM_MAP)) SET16(o, KM_MAP, (uint16_t)r32(spr + OBJ_mappings));
    if (has && OB16(o, KM_MAP) && (uint16_t)r32(spr + OBJ_mappings) != OB16(o, KM_MAP)) {
        SET16(o, KM_SPR, 0);
        has = 0;
    }
    if (has) {
        SET16(spr, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(spr, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x10);
    }
    if (!(OB8(o, OBJ_render) & 0x80) && o_out_of_range(o, OB16(o, KM_X0))) {   /* (CheckObjDespawn2: its place) */
        if (has) o_delete(spr);
        o_delete_respawnable(o);
        return;
    }
    if (OB8(o, OBJ_routine) == 4) o_draw(o);                  /* (unseen till it is down) */
}

/* ---- $25 collapsing floor ----------------------------------------------- */
#define KZ_TIME         0x30               /* CD $2A (word) */
#define KZ_DIR          0x32               /* CD $2C: +-8, the way player 1 ran on */
#define KZ_VY           0x32               /* CD $2C (a piece's, long): its fall */
#define KZ_PIECE        0x3F               /* CD $29 (subtype 2): $FF a piece */
#define KZ_YRAD         0x11

static int kz_piece(uint32_t o, int dy, int dx, int timer, int frame)
{
    uint32_t a1 = o_alloc();                                  /* (FindObjSlot) */
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));                   /* (movem: its id .. y) */
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET8(a1, KZ_PIECE, 0xFF);
    SET16(a1, KZ_TIME, (uint16_t)timer);
    SET8(a1, OBJ_frame, frame);
    return 1;
}

static void kuzure(uint32_t o)
{
    if (OB8(o, KZ_PIECE)) {                                   /* a piece: waits, then falls */
        switch (OB8(o, OBJ_routine)) {
        case 0:
            SET8(o, OBJ_routine, 2);
            SET8(o, OBJ_y_radius, 8);
            SET8(o, OBJ_x_radius, 8);
            SET16(o, OBJ_priority, 3 << 7);
            SET8(o, OBJ_width_px, 8);
            SET8(o, OBJ_height_px, 8);
            break;
        case 2:
            SET16(o, KZ_TIME, OB16(o, KZ_TIME) - 1);
            if (OBS16(o, KZ_TIME) < 0) SET8(o, OBJ_routine, 4);
            break;
        default: {
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            int32_t v = (int32_t)r32(o + KZ_VY) + 0x4000;
            if (v > 0x160000) v = 0x160000;
            SET32(o, KZ_VY, (uint32_t)v);
            SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)v);
            break;
        }
        }
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        int8_t sub = (int8_t)OB8(o, OBJ_subtype);
        uint8_t n = (uint8_t)(sub < 0 ? -sub : sub), w = (uint8_t)(n << 3);
        o_init(o, SCDA_MMZ_KUZURE, 4, sub < 0 ? 1 : 3, w, KZ_YRAD);   /* (move.b #4: no flips) */
        SET8(o, OBJ_y_radius, KZ_YRAD);
        SET8(o, OBJ_x_radius, w);
        SET8(o, OBJ_frame, n - 1);                            /* (frames 0-7: 1-8 blocks) */
        SET8(o, OBJ_routine, sub < 0 ? 8 : 2);
        return;                                               /* (nothing drawn this frame) */
    }
    case 2:                                                   /* whole */
        o_platform(o, OB8(o, OBJ_width_px) + 10, KZ_YRAD, OB16(o, OBJ_x_pos));
        if (!cd_stood_on(o)) break;
        SET8(o, OBJ_routine, 4);
        /* (CD: clr.b oRespawn, it never comes back: o_delete at the end) */
        SET16(o, KZ_TIME, 8);
        {
            uint32_t p = OB8(o, OBJ_status) & ST_P1_STAND ? RAM_Player_1 : RAM_Player_2;
            SET16(o, KZ_DIR, OBS16(p, OBJ_x_vel) < 0 ? -8 : 8);
        }
        o_sfx_cd(0xA3);                                       /* (main_check: stood on, it gives) */
        o_draw(o);                                            /* (stood on: in view) */
        return;
    case 4:                                                   /* about to give */
        o_platform(o, OB8(o, OBJ_width_px) + 10, KZ_YRAD, OB16(o, OBJ_x_pos));
        SET16(o, KZ_TIME, OB16(o, KZ_TIME) - 1);
        if (OBS16(o, KZ_TIME) < 0) SET8(o, OBJ_routine, 6);
        o_draw(o);
        return;
    case 6: {                                                 /* a block breaks off its end */
        int d3 = ((int)OB8(o, OBJ_subtype) - 1) << 3;
        if (OBS16(o, KZ_DIR) >= 0) d3 = -d3;                  /* (the end the player came from) */
        if (kz_piece(o, -8, d3, 4, 8))                        /* (its top half, then its bottom) */
            kz_piece(o, 8, d3, 0, 9);
        o_platform(o, OB8(o, OBJ_width_px) + 10, KZ_YRAD, OB16(o, OBJ_x_pos));
        if (cd_stood_on(o)) cd_get_off(o);                    /* (off it, and on again if still over it) */
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) - 1);
        if ((int8_t)OB8(o, OBJ_subtype) <= 0) { o_delete(o); return; }
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 1);
        SET8(o, OBJ_x_radius, OB8(o, OBJ_x_radius) - 8);
        SET8(o, OBJ_width_px, OB8(o, OBJ_width_px) - 8);
        SET16(o, KZ_TIME, 7);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, KZ_DIR));
        o_platform(o, OB8(o, OBJ_width_px) + 10, KZ_YRAD, OB16(o, OBJ_x_pos));
        o_draw(o);
        return;
    }
    default:                                                  /* (subtype negative) a solid block */
        o_solid(o, OB8(o, OBJ_width_px) + 10, KZ_YRAD, KZ_YRAD + 1, OB16(o, OBJ_x_pos));
        break;
    }
    draw_despawn(o);
}

/* ---- $2C materialising blocks (act 1) ----------------------------------- */
#define DA_TIME         0x30               /* CD $2A (word) */
#define DA_PARENT       0x32               /* CD $2C: the first block (RAM low word) */
#define DA_X0           0x34               /* CD $2E: the first block's place */
#define DA_Y0           0x36               /* CD $30 */
#define DA_STOOD        0x38               /* CD $3F */
static void dai_init_sub(uint32_t o, uint32_t a6)             /* (each block's) */
{
    o_init(a6, SCDA_MMZ_DAI, OB8(a6, OBJ_render) | 4, 3, 0x10, 0x10);
    SET8(a6, OBJ_x_radius, 0x10);
    SET8(a6, OBJ_y_radius, 0x10);
    SET8(a6, OBJ_frame, 6);                                   /* (nothing) */
    SET8(a6, OBJ_routine, 2);
    SET16(a6, DA_X0, OB16(o, OBJ_x_pos));
    SET16(a6, DA_Y0, OB16(o, OBJ_y_pos));
}

static void dai(uint32_t o)
{
    /* subtype: the first block's delay; then per block: delay, x, y from it */
    static const int16_t g0[] = { 60, 0 }, g1[] = { 60, 1, 60, 0x20, 0 },
                         g2[] = { 90, 2, 180, 0x20, 0x10, 270, 0x40, 0x20 };
    static const int16_t *const groups[] = { g0, g1, g2 };
    int child = OB8(o, OBJ_subtype) & 0x80;
    if (child) {                                              /* (its first block still there?) */
        uint32_t par = 0xFF0000u | OB16(o, DA_PARENT);
        if (r32(par + OBJ_code) != r32(o + OBJ_code) || OB16(par, DA_X0) != OB16(o, DA_X0) ||
            OB16(par, DA_Y0) != OB16(o, DA_Y0)) {
            cd_get_off(o);
            o_delete(o);
            return;
        }
    }
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        const int16_t *t = groups[OB8(o, OBJ_subtype) > 2 ? 2 : OB8(o, OBJ_subtype)];   /* (0 and 2 placed) */
        SET16(o, DA_TIME, (uint16_t)*t++);
        dai_init_sub(o, o);
        for (int k = *t++; k > 0; k--, t += 3) {
            uint32_t a1 = o_alloc();                          /* (FindObjSlot) */
            if (!a1) { o_delete(o); return; }
            SET32(a1, OBJ_code, r32(o + OBJ_code));
            SET16(a1, DA_PARENT, (uint16_t)o);
            SET8(a1, OBJ_subtype, 0xFF);
            SET16(a1, DA_TIME, (uint16_t)t[0]);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + t[1]);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + t[2]);
            dai_init_sub(o, a1);
        }
        break;
    }
    case 2:                                                   /* waiting */
        SET16(o, DA_TIME, OB16(o, DA_TIME) - 1);
        if (!OB16(o, DA_TIME)) SET8(o, OBJ_routine, 4);
        break;
    case 4:                                                   /* appearing (anim 0, ends on frame 0) */
        o_animate(o, o_asset_anim(SCDA_MMZ_DAI));
        if (OB8(o, OBJ_frame)) break;
        SET16(o, DA_TIME, 120);
        SET8(o, OBJ_routine, 6);
        break;
    case 6:                                                   /* there: solid on top, 2 s */
        o_platform(o, 0x10 + 10, 0x10, OB16(o, OBJ_x_pos));
        SET8(o, DA_STOOD, cd_stood_on(o) ? 0xFF : 0);
        SET16(o, DA_TIME, OB16(o, DA_TIME) - 1);
        if (OB16(o, DA_TIME)) break;
        SET8(o, OBJ_anim, 1);                                 /* (move.w #$1FF,oAnim: anim 1 afresh) */
        SET8(o, OBJ_prev_anim, 0xFF);
        SET8(o, OBJ_routine, 8);
        if (OB8(o, DA_STOOD)) cd_get_off(o);
        break;
    default:                                                  /* vanishing, back (anim 1, ends on frame 0) */
        o_animate(o, o_asset_anim(SCDA_MMZ_DAI));
        if (OB8(o, OBJ_frame)) break;
        SET16(o, DA_TIME, 120);
        SET8(o, OBJ_routine, 6);
        break;
    }
    if (!child && cd_despawn(o, OB16(o, DA_X0))) return;      /* (CheckObjDespawn2, the first block only) */
    o_draw(o);
}

/* ---- $34 piston --------------------------------------------------------- */
#define PS_Y0           0x30               /* CD $30: its place */
#define PS_ANG          0x32               /* CD $32: angle, $400 a turn */
#define PS_SPD          0x34               /* CD $34 */
#define PS_RAD          0x36               /* CD $36: reach */
#define PS_DELAY        0x38               /* CD $38: the launcher's rest */
#define PS_TIME         0x3A               /* CD $3A */
#define PS_HOLD         0x3E               /* CD $3E: bit 7 resting */
static int16_t ps_y(uint32_t o)
{
    int16_t s, c;
    o_sincos((uint8_t)(OB16(o, PS_ANG) >> 2), &s, &c);
    return (int16_t)(OB16(o, PS_Y0) + (int16_t)(((int32_t)s * OBS16(o, PS_RAD)) >> 8));
}

static void piston(uint32_t o)
{
    static const uint16_t spd[4] = { 4, 8, 0x10, 0x20 };
    uint8_t sub = OB8(o, OBJ_subtype);
    int launcher = (sub & 3) == 1;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_MMZ_PISTON, OB8(o, OBJ_render) | 4, 3, 0x20, launcher ? 0x60 : 0x20);
        SET16(o, PS_Y0, OB16(o, OBJ_y_pos));
        SET16(o, OBJ_y_vel, 0);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, PS_ANG, (sub & 0x30) << 4);                  /* (0, $100, $200, $300) */
        SET16(o, PS_RAD, sub & 0x40 ? 0x30 : 0x20);
        SET16(o, OBJ_y_pos, ps_y(o));
        if (launcher) {                                       /* init_piston1: the column */
            SET8(o, OBJ_frame, 1);
            SET8(o, OBJ_y_radius, 0x60);
            SET16(o, PS_SPD, spd[(sub & 4) >> 2]);
            /* (CD's table is 60, 90, read at (bit 3) * 2: with bit 3 the
             * byte after it, $4A; none placed so) */
            SET8(o, PS_DELAY, sub & 8 ? 0x4A : 0x3C);
            SET8(o, PS_HOLD, 0);
        } else {                                              /* init_piston0 (types 0, 2) */
            SET8(o, OBJ_frame, 0);
            SET8(o, OBJ_y_radius, 0x20);
            SET16(o, PS_SPD, spd[(sub & 0x0C) >> 2]);
        }
        draw_despawn(o);                                     /* (the first frame: no move) */
        return;
    }
    uint16_t y0 = OB16(o, OBJ_y_pos);
    int16_t y;
    int rest = 0;
    if (launcher) {                                           /* rests at either end */
        if (OB8(o, PS_HOLD) & 0x80) {
            uint8_t t = OB8(o, PS_TIME);
            SET8(o, PS_TIME, t - 1);
            if (t) rest = 1;                                  /* (subq.b / bcc: till it wraps) */
            else SET8(o, PS_HOLD, OB8(o, PS_HOLD) & 0x7F);
        } else if (OB16(o, PS_ANG) == 0x100 || OB16(o, PS_ANG) == 0x300) {
            SET8(o, PS_HOLD, OB8(o, PS_HOLD) | 0x80);
            SET8(o, PS_TIME, OB8(o, PS_DELAY));
            rest = 1;
        }
    }
    if (rest)
        y = (int16_t)y0;
    else {
        SET16(o, PS_ANG, (OB16(o, PS_ANG) + OB16(o, PS_SPD)) & 0x3FF);
        y = ps_y(o);
    }
    SET16(o, OBJ_y_pos, (uint16_t)y);
    SET16(o, OBJ_y_vel, (uint16_t)((y - (int16_t)y0) * 256));   /* (CD: (new y - old) asr 8) */
    /* (CD runs SolidObject before the move while it rises and after it
     * otherwise, its SolidObject moving the rider by the y speed; S3K's puts
     * him on top where it is: once, after the move) */
    o_solid(o, 0x20 + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
    if (launcher && OB16(o, PS_ANG) == 0x210) {               /* (rising past mid-way: the throw) */
        for (int n = 0; n < 2; n++) {
            uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
            if (!(OB8(o, OBJ_status) & bit)) continue;
            uint32_t p = o_player(n);
            SET16(p, OBJ_y_vel, sub & 0x80 ? -0xA00 : -0x1000);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_AIR) & ~ST_ON_OBJ);
            SET8(p, OBJ_anim, 0x10);                          /* (the spring's) */
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
        }
    }
    draw_despawn(o);
}

void scd_mmz_plat_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x0F] = koma;
    t[base + 0x25] = kuzure;
    t[base + 0x2C] = dai;
    t[base + 0x34] = piston;
}
