/*
 * s1_obj_fz.c — Sonic 1's last two set pieces, transcribed from REV01:
 * the SBZ2 ending (Eggman on foot 82, the floor that crumbles away 83) and
 * the Final Zone (Eggman 85, the crushing cylinders 84, the plasma ball
 * launcher and its balls 86).
 *
 * S1's objoff_29 flags (cylinder / launcher "go") collide with S3K's
 * collision_property, so they live at $40.
 */
#include "trilogy_obj.h"
#include <stdlib.h>

#define OBJ_2nd      0x25
#define PARENT       0x34
#define FZ_GO        0x40

#define SBZ2_X  0x2050
#define SBZ2_Y  0x510
#define FZ_X    0x2450
#define FZ_Y    0x510
#define FZ_END  (FZ_X + 0x2B0)

extern void s1_boss_defeated(uint32_t o);

static uint32_t parent_of(uint32_t o) { return r32(o + PARENT) & 0xFFFFFF; }

static void follow_flip(uint32_t o, uint32_t p)  /* status copy + render flips */
{
    SET8(o, OBJ_status, OB8(p, OBJ_status));
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & 0xFC) | (OB8(o, OBJ_status) & 3));
}

/* ------------------------------------------------------------------------
 * Object 82 - Eggman (SBZ2): taunts, leaps onto his switch, floor collapses
 * ---------------------------------------------------------------------- */
#define SEGG_TIME   0x3C
#define SEGG_FLAG   0x2C              /* S1 obSubtype word: "SW" once he lands */
#define FLAG_SW     0x5357
#define FLAG_GO     0x474F

static void scrap_eggman(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_upload_asset(S1A_SEGG);
        o_upload_asset(S1A_SEGGBUT);
        o_upload_asset(S1A_FFLOOR);
        SET16(o, OBJ_x_pos, SBZ2_X + 0x110);
        SET16(o, OBJ_y_pos, SBZ2_Y + 0x94);
        SET8(o, OBJ_coll_flags, 0x0F);
        SET8(o, OBJ_coll_prop, 16);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
        SET8(o, OBJ_2nd, 0);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_anim, 0);
        o_init(o, S1A_SEGG, 4 | 0x80, 3, 0x20, 0x20);
        uint32_t a1 = o_alloc_after(o);
        if (a1) {                              /* his switch */
            SET32(a1, PARENT, RAMPTR(o));
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x82 * 4);
            SET16(a1, OBJ_x_pos, SBZ2_X + 0xE0);
            SET16(a1, OBJ_y_pos, SBZ2_Y + 0xAC);
            SET8(a1, OBJ_routine, 4);
            SET8(a1, OBJ_anim, 0);
            o_init(a1, S1A_SEGGBUT, 4 | 0x80, 3, 0x10, 8);
            SET8(a1, OBJ_frame, 0);
        }
    }
    /* fall through */
    case 2:                                    /* SEgg_Eggman */
        switch (OB8(o, OBJ_2nd)) {
        case 0:                                /* SEgg_ChkSonic */
            if ((uint16_t)(OB16(o, OBJ_x_pos) - r16(RAM_Player_1 + OBJ_x_pos)) < 128) {
                SET8(o, OBJ_2nd, 2);
                SET16(o, SEGG_TIME, 180);
                SET8(o, OBJ_anim, 1);
            }
            break;
        case 2:                                /* SEgg_PreLeap */
            SET16(o, SEGG_TIME, OB16(o, SEGG_TIME) - 1);
            if (!OB16(o, SEGG_TIME)) {
                SET8(o, OBJ_2nd, 4);
                SET8(o, OBJ_anim, 2);
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
                SET16(o, SEGG_TIME, 15);
            }
            break;
        case 4: {                              /* SEgg_Leap */
            SET16(o, SEGG_TIME, OB16(o, SEGG_TIME) - 1);
            int16_t t = (int16_t)OB16(o, SEGG_TIME);
            if (t > 0) break;
            if (t == 0) {
                SET16(o, OBJ_x_vel, -0xFC);
                SET16(o, OBJ_y_vel, -0x3C0);
            }
            if ((int16_t)OB16(o, OBJ_x_pos) <= SBZ2_X + 0xE2) SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x24);
            if ((int16_t)OB16(o, OBJ_y_vel) >= 0 && OB16(o, OBJ_y_pos) >= SBZ2_Y + 0x85) {
                SET16(o, SEGG_FLAG, FLAG_SW);
                if (OB16(o, OBJ_y_pos) >= SBZ2_Y + 0x8B) {
                    SET16(o, OBJ_y_pos, SBZ2_Y + 0x8B);
                    SET16(o, OBJ_y_vel, 0);
                }
            }
            if (OB16(o, OBJ_x_vel) | OB16(o, OBJ_y_vel)) break;
            for (int i = 0; i < 110; i++) {    /* SEgg_FindBlocks: the floor */
                uint32_t a1 = o_slot(i);
                if (r32(a1) != NATIVE_S1_BASE + 0x83 * 4) continue;
                SET16(a1, SEGG_FLAG, FLAG_GO);
                SET8(o, OBJ_2nd, 6);
                SET8(o, OBJ_anim, 1);
                break;
            }
            break;
        }
        default:
            break;
        }
        o_move(o);                             /* SEgg_Move */
        o_animate(o, o_asset_anim(S1A_SEGG));
        o_draw(o);
        return;
    default:                                   /* SEgg_Switch */
        if (!OB8(o, OBJ_2nd) && OB16(parent_of(o), SEGG_FLAG) == FLAG_SW) {
            SET8(o, OBJ_frame, 1);
            SET8(o, OBJ_2nd, 2);
        }
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 83 - the floor that crumbles away under Sonic (SBZ2). The main
 * object is the invisible solid strip; eight blocks are the visible floor.
 * ---------------------------------------------------------------------- */
#define FF_BLOCKS   0x30              /* 8 words: block addresses (main object) */

static void ffloor_solid(uint32_t o)            /* FFloor_Solid */
{
    int16_t d0 = (int16_t)((8 - (int8_t)OB8(o, OBJ_frame)) << 4);
    SET8(o, OBJ_width_px, (uint8_t)d0);
    SET16(o, OBJ_x_pos, (uint16_t)(SBZ2_X + 0xB0 - d0));
    o_solid(o, 0x0B + d0, 0x10, 0x11, OB16(o, OBJ_x_pos));
}

static void false_floor(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET16(o, OBJ_x_pos, SBZ2_X + 0x30);
        SET16(o, OBJ_y_pos, SBZ2_Y + 0xC0);
        SET8(o, OBJ_width_px, 0x80);
        SET8(o, OBJ_height_px, 0x10);
        SET8(o, OBJ_render, 4 | 0x80);
        uint16_t x = SBZ2_X - 0x40;
        for (int i = 0; i < 8; i++, x += 0x20) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            SET16(o, FF_BLOCKS + i * 2, a1 & 0xFFFF);
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x83 * 4);
            o_init(a1, S1A_FFLOOR, 4, 3, 0x10, 0x10);
            SET16(a1, OBJ_x_pos, x);
            SET16(a1, OBJ_y_pos, SBZ2_Y + 0xC0);
            SET8(a1, OBJ_routine, 8);
        }
        SET8(o, OBJ_routine, 2);
        return;
    }
    case 2:                                    /* FFloor_ChkBreak */
        if (OB16(o, SEGG_FLAG) == FLAG_GO) {
            SET8(o, OBJ_frame, 0);
            SET8(o, OBJ_routine, 4);
        }
        ffloor_solid(o);
        return;
    case 4: {                                  /* FFloor_Break: one block at a time */
        uint8_t t = OB8(o, OBJ_anim_timer);
        SET8(o, OBJ_anim_timer, (uint8_t)(t - 0x0E));
        if (t >= 0x0E) { ffloor_solid(o); return; }
        uint16_t w = OB16(o, FF_BLOCKS + OB8(o, OBJ_frame) * 2);
        if (w) SET16(0xFF0000u | w, SEGG_FLAG, FLAG_GO);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        if (OB8(o, OBJ_frame) != 8) { ffloor_solid(o); return; }
        /* fall through: FFloor_AllGone */
    }
    case 6:
        for (int i = 0; i < 2; i++) {
            uint8_t bit = i ? ST_P2_STAND : ST_P1_STAND;
            if (!(OB8(o, OBJ_status) & bit)) continue;
            uint32_t p = o_player(i);
            SET8(p, OBJ_status, OB8(p, OBJ_status) & ~8);
        }
        o_delete(o);
        return;
    case 8:                                    /* FFloor_Block */
        if (OB16(o, SEGG_FLAG) != FLAG_GO) { o_draw(o); return; }
        {                                      /* FFloor_BlockBreak */
            static const int16_t yv[4] = { 0x80, 0, 0x120, 0xC0 };
            static const int16_t pos[4][2] = { { -8, -8 }, { 0x10, 0 }, { 0, 0x10 }, { 0x10, 0x10 } };
            SET8(o, OBJ_routine, 0x0A);
            SET8(o, OBJ_width_px, 8);
            SET8(o, OBJ_height_px, 8);
            uint32_t a1 = o;
            for (int i = 0; i < 4; i++) {
                if (i) {
                    a1 = o_alloc_after(o);
                    if (!a1) break;
                    for (uint32_t k = 0; k < OBJ_SIZE; k += 2) w16(a1 + k, r16(o + k));
                }
                SET16(a1, OBJ_y_vel, (uint16_t)yv[i]);
                SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + pos[i][0]);
                SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + pos[i][1]);
                SET8(a1, OBJ_frame, i + 1);
            }
            o_sfx(0x59);                       /* sfx_WallSmash (S3K: collapse) */
            o_draw(o);
        }
        return;
    default:                                   /* FFloor_Frag */
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_fall(o);
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 84 - the four cylinders that crush (Eggman rides one each time)
 * ---------------------------------------------------------------------- */
#define CYL_BASEY   0x38              /* long: obBossY */
#define CYL_OFS     0x3C              /* long: extension (16.16) */
#define CYL_CARRY   0x30              /* word: -1 = Eggman inside */

static void cylinder_y(uint32_t o)               /* loc_1A4EA */
{
    uint32_t d1 = r32(o + CYL_BASEY) + r32(o + CYL_OFS);
    SET16(o, OBJ_y_pos, (uint16_t)(d1 >> 16));
    if (OB8(o, OBJ_routine) == 4 && (int16_t)OB16(o, CYL_CARRY) < 0) {
        int16_t d0 = OB8(o, OBJ_subtype) > 2 ? 0x0E : -0x0A;
        uint32_t a1 = parent_of(o);
        SET16(a1, OBJ_y_pos, (uint16_t)((d1 >> 16) + d0));
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    }
    o_solid(o, 0x2B, 0x60, 0x61, OB16(o, OBJ_x_pos));
    int16_t off = (int16_t)OB16(o, CYL_OFS);
    uint8_t f = 0;
    if (off >= 0) { if (off >= 0x27) f = (uint8_t)(1 + ((off - 0x27) >> 4)); }
    else { int16_t n = (int16_t)(-off - 8); if (n >= 0) f = (uint8_t)(1 + (n >> 4)); }
    SET8(o, OBJ_frame, f);
    int16_t dx = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if (dx >= 0 && dx >= 0x140 && !(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
    o_draw(o);
}

static void cylinder(uint32_t o)
{
    static const uint16_t pos[4][2] = { { FZ_X + 0x80, FZ_Y + 0x110 }, { FZ_X + 0x100, FZ_Y + 0x110 },
                                        { FZ_X + 0x40, FZ_Y - 0x50 }, { FZ_X + 0xC0, FZ_Y - 0x50 } };
    uint8_t sub = OB8(o, OBJ_subtype);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, S1A_EGGCYL, 4 | 0x80 | 0x10, 3, 0x20, 0x60);
        SET16(o, OBJ_x_pos, pos[(sub >> 1) & 3][0]);
        SET16(o, OBJ_y_pos, pos[(sub >> 1) & 3][1]);
        SET32(o, CYL_BASEY, (uint32_t)pos[(sub >> 1) & 3][1] << 16);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2:                                    /* EggmanCylinder_Action */
        if (sub > 2) SET8(o, OBJ_render, OB8(o, OBJ_render) | 2);
        SET32(o, CYL_OFS, 0);
        if (OB8(o, FZ_GO)) SET8(o, OBJ_routine, 4);
        cylinder_y(o);
        return;
    default: {                                 /* EggmanCylinder_Move */
        uint32_t a1 = parent_of(o);
        uint32_t ofs = r32(o + CYL_OFS);
        if (sub <= 2) {                        /* bottom: rises up */
            if (!OB8(o, FZ_GO)) {              /* retracting */
                if (!OB8(a1, OBJ_coll_prop)) { s1_boss_defeated(o); ofs -= 0x10000u; }
                uint32_t n = ofs + 0x20000u;
                if (n < ofs) {                 /* carry: back home */
                    SET32(o, CYL_OFS, 0);
                    SET16(a1, 0x32, OB16(a1, 0x32) - 1);
                    SET16(a1, 0x30, 0);
                    SET8(o, OBJ_routine, 2);
                    cylinder_y(o);
                    return;
                }
                ofs = n;
            } else {
                if ((int16_t)(ofs >> 16) < -0x10) ofs -= 0x28000u;
                ofs -= 0x8000u;
                if ((int16_t)(ofs >> 16) <= -0xA0) { ofs = 0xFF600000u; SET8(o, FZ_GO, 0); }
            }
        } else {                               /* top: comes down */
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 2);
            if (!OB8(o, FZ_GO)) {
                if (!OB8(a1, OBJ_coll_prop)) { s1_boss_defeated(o); ofs += 0x10000u; }
                uint32_t n = ofs - 0x20000u;
                if (n > ofs) {                 /* borrow: back home */
                    SET32(o, CYL_OFS, 0);
                    SET16(a1, 0x32, OB16(a1, 0x32) - 1);
                    SET16(a1, 0x30, 0);
                    SET8(o, OBJ_routine, 2);
                    cylinder_y(o);
                    return;
                }
                ofs = n;
            } else {
                if ((int16_t)(ofs >> 16) >= 0x10) ofs += 0x28000u;
                ofs += 0x8000u;
                if ((int16_t)(ofs >> 16) >= 0xA0) { ofs = 0x00A00000u; SET8(o, FZ_GO, 0); }
            }
        }
        SET32(o, CYL_OFS, ofs);
        cylinder_y(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 86 - plasma ball launcher and its balls
 * ---------------------------------------------------------------------- */
#define PL_TARGET  0x30               /* ball: target x / launcher: spread counter */
#define PL_COUNT   0x32
#define PL_LIVE    0x38
#define PL_TIME    0x3A               /* ball: S1 obSubtype word */

static void plaunch_solid(uint32_t o)            /* loc_1A86C */
{
    o_solid(o, 0x13, 8, 0x11, OB16(o, OBJ_x_pos));
    int16_t dx = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if (dx >= 0x140 && !(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
    o_animate(o, o_asset_anim(S1A_PLAUNCH));
    o_draw(o);
}

static void boss_plasma(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET16(o, OBJ_x_pos, FZ_X + 0x138);
        SET16(o, OBJ_y_pos, FZ_Y + 0x2C);
        o_init(o, S1A_PLAUNCH, 4 | 0x80, 3, 8, 8);
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2: {                                  /* BossPlasma_Generator */
        uint32_t a1 = parent_of(o);
        if (OB8(a1, 0x34) == 6) {              /* boss beaten: blow up */
            SET32(o, OBJ_code, S3K_Obj_Explosion);
            SET8(o, OBJ_routine, 6);
            o_sfx(0xB4);
            return;
        }
        SET8(o, OBJ_anim, 0);
        if (OB8(o, FZ_GO)) {
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_anim, 1);
        }
        plaunch_solid(o);
        return;
    }
    case 4:                                    /* BossPlasma_MakeBalls */
        if (OB8(o, FZ_GO)) {
            SET8(o, FZ_GO, 0);
            SET16(o, PL_TARGET, OB16(o, PL_TARGET) + 4);
            SET16(o, PL_COUNT, 0);
            for (int i = 0; i < 4; i++) {
                uint32_t a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x86 * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, FZ_Y + 0x2C);
                SET8(a1, OBJ_routine, 8);
                o_init(a1, S1A_PLASMA, 4 | 0x80, 3, 0x0C, 0x0C);
                SET8(a1, OBJ_coll_flags, 0);
                SET16(a1, PL_TIME, 0x3E);
                SET32(a1, PARENT, RAMPTR(o));
                int r = o_random();
                int16_t d1 = (int16_t)((int16_t)OB16(o, PL_COUNT) * -0x4F + FZ_X + 0x128);
                SET16(a1, PL_TARGET, (uint16_t)((r & 0x1F) - 0x10 + d1));
                SET16(o, PL_COUNT, OB16(o, PL_COUNT) + 1);
                SET16(o, PL_LIVE, OB16(o, PL_COUNT));
            }
        }
        if (!OB16(o, PL_COUNT)) SET8(o, OBJ_routine, 6);
        plaunch_solid(o);
        return;
    case 6:                                    /* BossPlasma_Finish */
        SET8(o, OBJ_anim, 2);
        if (!OB16(o, PL_LIVE)) {
            SET8(o, OBJ_routine, 2);
            SET16(parent_of(o), 0x32, 0xFFFF);
        }
        plaunch_solid(o);
        return;
    default: {                                 /* BossPlasma_Balls */
        uint32_t lau = parent_of(o);
        switch (OB8(o, OBJ_2nd)) {
        case 0:                                /* Spread */
            SET16(o, OBJ_x_vel, (uint16_t)((int16_t)(OB16(o, PL_TARGET) - OB16(o, OBJ_x_pos)) << 4));
            SET16(o, PL_TIME, 0xB4);
            SET8(o, OBJ_2nd, 2);
            break;
        case 2:                                /* Drop */
            if (OB16(o, OBJ_x_vel)) {
                o_move(o);
                int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, PL_TARGET));
                if (OB16(o, OBJ_x_pos) < OB16(o, PL_TARGET)) {
                    SET16(o, OBJ_x_vel, 0);
                    SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + d0));   /* (S1 nudges it further left) */
                    SET16(lau, PL_COUNT, OB16(lau, PL_COUNT) - 1);
                }
            }
            SET8(o, OBJ_anim, 0);
            SET16(o, PL_TIME, OB16(o, PL_TIME) - 1);
            if (!OB16(o, PL_TIME)) {
                SET8(o, OBJ_2nd, 4);
                SET8(o, OBJ_anim, 1);
                SET8(o, OBJ_coll_flags, 0x9A);
                SET16(o, PL_TIME, 0xB4);
                SET16(o, OBJ_x_vel, (uint16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos)));
                SET16(o, OBJ_y_vel, 0x140);
            }
            break;
        default:                               /* Move */
            o_move(o);
            if (OB16(o, OBJ_y_pos) >= FZ_Y + 0xD0 || (SET16(o, PL_TIME, OB16(o, PL_TIME) - 1), !OB16(o, PL_TIME))) {
                SET16(lau, PL_LIVE, OB16(lau, PL_LIVE) - 1);
                o_delete(o);
                return;
            }
            break;
        }
        o_animate(o, o_asset_anim(S1A_PLASMA));
        o_touch(o);
        o_draw(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 85 - Eggman (FZ) and the five parts that go with him
 * ---------------------------------------------------------------------- */
#define FZ_STATE   0x34               /* byte: Eggman's routine */
#define FZ_HURT    0x35
#define FZ_LAUNCH  0x36               /* word: launcher */
#define FZ_CYL     0x38               /* 4 words: cylinders */

static int in_ship(uint32_t p) { return r32(p + OBJ_mappings) == o_asset_maps(S1A_EGGMAN); }

static void fz_side_solid(uint32_t o)            /* loc_1A15C / loc_1A166 */
{
    o_animate(o, o_asset_anim(S1A_FZEGG));
    if ((int16_t)r16(RAM_Camera_max_X_pos) < FZ_END)
        w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
    if (OB8(o, FZ_STATE) < 0x0C) o_solid(o, 0x1B, 0x70, 0x71, OB16(o, OBJ_x_pos));
}

static void fz_eggman(uint32_t o)
{
    uint32_t pl = RAM_Player_1;
    switch (OB8(o, FZ_STATE)) {
    case 0:                                    /* Wait */
        if (r16(RAM_Camera_X_pos) >= FZ_X) SET8(o, FZ_STATE, 2);
        o_random();
        return;
    case 2: {                                  /* Crush */
        if ((int16_t)OB16(o, 0x30) < 0) {
            static const uint16_t pairs[8] = { 0, 2, 2, 4, 4, 6, 6, 0 };
            SET16(o, 0x30, 0);
            int32_t r = o_random();
            int d0 = r & 0x0C, d1 = d0 + 2;
            if (r < 0) { int t = d0; d0 = d1; d1 = t; }
            uint16_t c0 = pairs[d0 / 2], c1 = pairs[d1 / 2];
            SET16(o, 0x30, c0);
            uint32_t a1 = 0xFF0000u | OB16(o, FZ_CYL + c0);
            SET8(a1, FZ_GO, 0xFF);
            SET16(a1, CYL_CARRY, 0xFFFF);
            a1 = 0xFF0000u | OB16(o, FZ_CYL + c1);
            SET8(a1, FZ_GO, 1);
            SET16(a1, CYL_CARRY, 0);
            SET16(o, 0x32, 1);
            SET8(o, FZ_HURT, 0);
            o_sfx(0x6F);                       /* sfx_Rumbling */
        }
        if ((int16_t)OB16(o, 0x32) < 0) {       /* loc_19FA6: cylinders done */
            if (OB8(o, OBJ_coll_prop)) {
                SET8(o, FZ_STATE, 4);
                SET16(o, 0x30, 0xFFFF);
                SET16(o, 0x32, 0);
            } else {
                D(0) = 100;
                tri_jsr(S3K_HUD_AddToScore);
                SET8(o, FZ_STATE, 6);
                SET16(o, OBJ_x_pos, FZ_X + 0x170);
                SET16(o, OBJ_y_pos, FZ_Y + 0x2C);
                SET8(o, OBJ_y_radius, 0x14);
            }
            return;
        }
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
        if (OB16(pl, OBJ_x_pos) >= OB16(o, OBJ_x_pos)) SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        o_solid(o, 0x2B, 0x14, 0x14, OB16(o, OBJ_x_pos));
        int hit = 0;
        if ((g_o_d6 >> 16) & 1) {              /* Sonic hit the side */
            if (OB8(pl, OBJ_anim) == 2) {      /* rolling: bounce off, damage */
                SET16(pl, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? 0x300 : -0x300);
                if (!OB8(o, FZ_HURT) && OB8(o, OBJ_coll_prop)) {   /* (fixed: no underflow) */
                    SET8(o, OBJ_coll_prop, OB8(o, OBJ_coll_prop) - 1);
                    SET8(o, FZ_HURT, 0x64);
                    o_sfx(0x6E);
                }
            }
            hit = 1;
        }
        (void)hit;
        if (OB8(o, FZ_HURT)) {
            SET8(o, FZ_HURT, OB8(o, FZ_HURT) - 1);
            if (OB8(o, FZ_HURT)) SET8(o, OBJ_anim, 3);
            else if (OB8(o, OBJ_coll_prop)) SET8(o, OBJ_anim, 1);
        } else if (OB8(o, OBJ_coll_prop)) {
            SET8(o, OBJ_anim, 1);
        }
        o_animate(o, o_asset_anim(S1A_FZEGG));
        return;
    }
    case 4: {                                  /* Plasma */
        uint32_t a1 = 0xFF0000u | OB16(o, FZ_LAUNCH);
        if ((int16_t)OB16(o, 0x30) < 0) {
            SET16(o, 0x30, 0);
            SET8(a1, FZ_GO, 0xFF);
            o_sfx(0xA8);
        }
        if (!(r16(RAM_Level_frame_counter) & 0x0F)) o_sfx(0xA8);
        if (OB16(o, 0x32)) {
            SET8(o, FZ_STATE, 2);
            SET16(o, 0x30, 0xFFFF);
            SET16(o, 0x32, 0);
        }
        return;
    }
    case 6:                                    /* Fall */
        SET8(o, OBJ_width_px, 0x30);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        o_move(o);
        SET8(o, OBJ_frame, 6);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x10);
        if (OB16(o, OBJ_y_pos) >= FZ_Y + 0x8C) {
            SET16(o, OBJ_y_pos, FZ_Y + 0x8C);
            SET8(o, FZ_STATE, 8);
            SET8(o, OBJ_width_px, 0x20);
            SET16(o, OBJ_x_vel, 0x100);
            SET16(o, OBJ_y_vel, -0x100);
            w8(RAM_Dynamic_resize_routine, r8(RAM_Dynamic_resize_routine) + 2);
        }
        if ((int16_t)r16(RAM_Camera_max_X_pos) < FZ_END)
            w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
        o_solid(o, 0x1B, 0x70, 0x71, OB16(o, OBJ_x_pos));
        return;
    case 8: {                                  /* Run: keeps just ahead of Sonic */
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        SET8(o, OBJ_anim, 4);
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x10);
        if (OB16(o, OBJ_y_pos) >= FZ_Y + 0x93) SET16(o, OBJ_y_vel, -0x40);
        int16_t v = 0x400;
        int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(pl, OBJ_x_pos));
        if (d0 < 0) v = 0x500;
        else {
            static const int16_t steps[6][2] = { { 0x70, 0x100 }, { 8, 0x100 }, { 8, 0x80 }, { 8, 0x80 }, { 8, 0x80 }, { 0x38, 0 } };
            uint16_t u = (uint16_t)d0;
            for (int k = 0; k < 6; k++) {
                if (u < (uint16_t)steps[k][0]) break;
                u = (uint16_t)(u - steps[k][0]);
                if (k == 5) v = 0; else v = (int16_t)(v - steps[k][1]);
            }
        }
        SET16(o, OBJ_x_vel, (uint16_t)v);
        if (OB16(o, OBJ_x_pos) >= FZ_X + 0x250) {
            SET16(o, OBJ_x_pos, FZ_X + 0x250);
            SET16(o, OBJ_x_vel, 0x240);
            SET16(o, OBJ_y_vel, -0x4C0);
            SET8(o, FZ_STATE, 0x0A);
        }
        fz_side_solid(o);
        return;
    }
    case 0x0A:                                 /* Jump onto the ship */
        o_move(o);
        if (OB16(o, OBJ_x_pos) >= FZ_X + 0x290) SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x34);
        if ((int16_t)OB16(o, OBJ_y_vel) >= 0 && OB16(o, OBJ_y_pos) >= FZ_Y + 0x82) {
            SET16(o, OBJ_y_pos, FZ_Y + 0x82);
            SET16(o, OBJ_y_vel, 0);
        }
        if (!(OB16(o, OBJ_x_vel) | OB16(o, OBJ_y_vel))) {
            SET8(o, FZ_STATE, 0x0C);
            SET16(o, OBJ_y_vel, -0x180);
            SET8(o, OBJ_coll_prop, 1);
        }
        fz_side_solid(o);
        return;
    case 0x0C:                                 /* Ship: rises */
        o_asset(o, S1A_EGGMAN);
        SET8(o, OBJ_anim, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        o_move(o);
        if (OB16(o, OBJ_y_pos) < FZ_Y + 0x34) {
            SET16(o, OBJ_x_vel, 0x180);
            SET16(o, OBJ_y_vel, -0x18);
            SET8(o, OBJ_coll_flags, 0x0F);
            SET8(o, FZ_STATE, 0x0E);
        }
        fz_side_solid(o);
        return;
    default: {                                 /* Escape: one hit knocks him down */
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        o_move(o);
        int run = 1;
        if (!OB16(o, 0x30)) {
            if (OB8(o, OBJ_coll_flags)) run = 0;
            else { SET16(o, 0x30, 0x1E); o_sfx(0x6E); }
        }
        if (run) {
            SET16(o, 0x30, OB16(o, 0x30) - 1);
            if (!OB16(o, 0x30)) {
                if (OB8(o, OBJ_status) & 0x80) SET16(o, OBJ_y_vel, 0x60);
                else SET8(o, OBJ_coll_flags, 0x0F);
            }
        }
        if ((int16_t)OB16(pl, OBJ_x_pos) >= FZ_END + 0x90) {   /* lock Sonic at the end */
            w8(RAM_Ctrl_1_locked, 1);
            w16(RAM_Ctrl_1_held_logical, 0);
            SET16(pl, OBJ_ground_vel, 0);
            if ((int16_t)OB16(o, OBJ_y_vel) < 0) {
                w16(RAM_Ctrl_1_held_logical, 0x0100);   /* got away: look up */
                if ((int16_t)OB16(pl, OBJ_x_pos) >= FZ_END + 0xE0) SET16(pl, OBJ_x_pos, FZ_END + 0xE0);
            }
        }
        if (OB16(o, OBJ_x_pos) >= FZ_END + 0x200 && !(OB8(o, OBJ_render) & 0x80)) {
            extern void tri_s1_complete(void);
            tri_s1_complete();                 /* id_Ending */
            o_delete(o);
            return;
        }
        fz_side_solid(o);
        return;
    }
    }
}

static void fz_child_follow(uint32_t o)          /* loc_1A296 */
{
    uint32_t p = parent_of(o);
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
    SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
    follow_flip(o, p);
    o_draw(o);
}

static void boss_final(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        static const struct { uint16_t x, y; uint8_t asset, routine, anim, prio, w, h; } part[6] = {
            { 0x100, 0x100, S1A_FZEGG, 2, 0, 4, 0x20, 0x19 },
            { FZ_X + 0x160, FZ_Y + 0x80, S1A_EGGCYL, 4, 0, 1, 0x12, 8 },
            { FZ_X + 0x290, FZ_Y + 0x86, S1A_FZLEGS, 6, 0, 3, 0, 0 },
            { FZ_X + 0x290, FZ_Y + 0x86, S1A_FZEGG, 8, 0, 3, 0, 0 },
            { FZ_X + 0x290, FZ_Y + 0x86, S1A_EGGMAN, 0x0A, 0, 3, 0x20, 0x20 },
            { FZ_X + 0x290, FZ_Y + 0x86, S1A_EGGMAN, 0x0C, 0, 3, 0, 0 } };
        o_upload_asset(S1A_EGGMAN);
        o_upload_asset(S1A_FZEGG);
        o_upload_asset(S1A_EGGCYL);
        o_upload_asset(S1A_FZLEGS);
        o_upload_asset(S1A_FZDMG);
        o_upload_asset(S1A_PLAUNCH);
        o_upload_asset(S1A_PLASMA);
        uint32_t a1 = o;
        for (int i = 0; i < 6; i++) {
            if (i) {
                a1 = o_alloc_after(o);
                if (!a1) break;
            }
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x85 * 4);
            SET16(a1, OBJ_x_pos, part[i].x);
            SET16(a1, OBJ_y_pos, part[i].y);
            o_init(a1, part[i].asset, 4, part[i].prio, part[i].w ? part[i].w : 0x20, part[i].h ? part[i].h : 0x20);
            SET8(a1, OBJ_routine, part[i].routine);
            SET8(a1, OBJ_anim, part[i].anim);
            SET8(a1, OBJ_y_radius, part[i].h);
            SET32(a1, PARENT, RAMPTR(o));
        }
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 0x80);
        a1 = o_alloc();
        if (a1) {
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x86 * 4);
            SET16(o, FZ_LAUNCH, a1 & 0xFFFF);
            SET32(a1, PARENT, RAMPTR(o));
        }
        for (int i = 0; i < 4; i++) {
            uint32_t c = o_alloc_after(a1 ? a1 : o);
            if (!c) break;
            SET16(o, FZ_CYL + i * 2, c & 0xFFFF);
            SET32(c, OBJ_code, NATIVE_S1_BASE + 0x84 * 4);
            SET32(c, PARENT, RAMPTR(o));
            SET8(c, OBJ_subtype, i * 2);
            a1 = c;
        }
        SET16(o, FZ_STATE, 0);
        SET8(o, OBJ_coll_prop, 8);
        {   /* development: TRILOGY_BOSSHITS=n */
            const char *h = getenv("TRILOGY_BOSSHITS");
            if (h && *h) SET8(o, OBJ_coll_prop, (uint8_t)atoi(h));
        }
        SET16(o, 0x30, 0xFFFF);
    }
    /* fall through */
    case 2:                                    /* BossFinal_Eggman */
        fz_eggman(o);
        if (r32(o) != NATIVE_S1_BASE + 0x85 * 4) return;
        o_touch(o);
        o_draw(o);
        return;
    case 4: {                                  /* Panel */
        SET8(o, OBJ_frame, 0x0B);
        if (r16(RAM_Player_1 + OBJ_x_pos) >= OB16(o, OBJ_x_pos) && !(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
    case 6: {                                  /* Legs (stay behind when he jumps in) */
        uint32_t p = parent_of(o);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        if (!in_ship(p)) { follow_flip(o, p); o_draw(o); return; }
        SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
        SET16(o, OBJ_y_pos, OB16(p, OBJ_y_pos));
        if (!OB8(o, OBJ_anim_timer)) SET8(o, OBJ_anim_timer, 0x14);
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) <= 0) {
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
            if ((int8_t)OB8(o, OBJ_frame) > 2) { o_delete(o); return; }
        }
        fz_child_follow(o);
        return;
    }
    case 8: {                                  /* Cockpit */
        uint32_t p = parent_of(o);
        if (r32(p) != r32(o)) { o_delete(o); return; }
        if (!in_ship(p)) { SET8(o, OBJ_frame, 0x0A); follow_flip(o, p); o_draw(o); return; }
        SET8(o, OBJ_anim, 1);
        if ((int8_t)OB8(p, OBJ_coll_prop) > 0) {
            SET8(o, OBJ_anim, 6);
            o_asset(o, S1A_EGGMAN);
            o_animate(o, o_asset_anim(S1A_EGGMAN));
            fz_child_follow(o);
            return;
        }
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        s1_boss_defeated(o);
        SET16(o, OBJ_priority, 2 << 7);
        SET8(o, OBJ_anim, 0);
        o_asset(o, S1A_FZDMG);
        o_animate(o, o_asset_anim(S1A_FZDMG));
        fz_child_follow(o);
        return;
    }
    case 0x0A: {                               /* EmptyShip */
        uint32_t p = parent_of(o);
        SET8(o, OBJ_frame, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        if (OB8(p, FZ_STATE) == 0x0C && in_ship(p)) { o_delete(o); return; }
        follow_flip(o, p);
        o_draw(o);
        return;
    }
    default: {                                 /* Flame */
        uint32_t p = parent_of(o);
        if (r32(p) != r32(o)) { o_delete(o); return; }
        SET8(o, OBJ_anim, 7);
        if (OB8(p, FZ_STATE) < 0x0C) { follow_flip(o, p); o_draw(o); return; }
        if (OB16(p, OBJ_x_vel)) SET8(o, OBJ_anim, 0x0B);
        o_animate(o, o_asset_anim(S1A_EGGMAN));
        fz_child_follow(o);
        return;
    }
    }
}

void s1_obj_fz_register(TriObjFn *t)
{
    t[0x82] = scrap_eggman;
    t[0x83] = false_floor;
    t[0x84] = cylinder;
    t[0x85] = boss_final;
    t[0x86] = boss_plasma;
}
