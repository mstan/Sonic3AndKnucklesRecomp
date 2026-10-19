/*
 * s1_obj_ghz.c — Sonic 1 Green Hill objects, transcribed from the original
 * object code (s1disasm routine names in comments). Engine services are the
 * retail S3K ones, so both S3 players interact with them.
 */
#include "trilogy_obj.h"

/* S1 secondary routine lives in a free byte of the S3K table */
#define R2          0x2D

/* ------------------------------------------------------------------------
 * Object 40 - Moto Bug
 * ---------------------------------------------------------------------- */
#define MOTO_TIME   0x30    /* word */
#define MOTO_SMOKE  0x33    /* byte */

static void moto(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:   /* Moto_Main */
        o_init(o, S1A_MOTO, 4, 4, 0x14, 0x10);
        if (OB8(o, OBJ_anim)) {             /* smoke trail */
            SET8(o, OBJ_routine, 4);
            goto animate;
        }
        SET8(o, OBJ_y_radius, 0x0E);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_coll_flags, 0x0C);
        o_fall(o);
        {
            int16_t d1 = o_floor_dist(o);
            if (d1 < 0) {
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                SET16(o, OBJ_y_vel, 0);
                SET8(o, OBJ_routine, 2);
                SET8(o, OBJ_status, OB8(o, OBJ_status) ^ ST_XFLIP);
            }
        }
        return;
    case 2:   /* Moto_Action */
        if (OB8(o, R2) == 0) {              /* .move */
            int16_t t = (int16_t)(OBS16(o, MOTO_TIME) - 1);
            SET16(o, MOTO_TIME, t);
            if (t < 0) {
                SET8(o, R2, 2);
                SET16(o, OBJ_x_vel, -0x100);
                SET8(o, OBJ_anim, 1);
                uint8_t st = OB8(o, OBJ_status);
                SET8(o, OBJ_status, st ^ ST_XFLIP);
                if (!(st & ST_XFLIP))
                    SET16(o, OBJ_x_vel, 0x100);
            }
        } else {                            /* .findfloor */
            o_move(o);
            int16_t d1 = o_floor_dist(o);
            if (d1 < -8 || d1 >= 0x0C) {    /* .pause */
                SET8(o, R2, 0);
                SET16(o, MOTO_TIME, 59);
                SET16(o, OBJ_x_vel, 0);
                SET8(o, OBJ_anim, 0);
            } else {
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                int8_t sd = (int8_t)(OB8(o, MOTO_SMOKE) - 1);
                SET8(o, MOTO_SMOKE, sd);
                if (sd < 0) {
                    SET8(o, MOTO_SMOKE, 0x0F);
                    uint32_t a1 = o_alloc();
                    if (a1) {
                        SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x40 * 4);
                        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                        SET8(a1, OBJ_status, OB8(o, OBJ_status));
                        SET8(a1, OBJ_anim, 2);
                    }
                }
            }
        }
        o_animate(o, o_asset_anim(S1A_MOTO));
        o_touch(o);
        o_remember(o);
        return;
    case 4:   /* Moto_Animate */
    animate:
        o_animate(o, o_asset_anim(S1A_MOTO));
        o_draw(o);
        return;
    default:  /* Moto_Delete */
        o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 1F - Crabmeat
 * ---------------------------------------------------------------------- */
#define CRAB_TIME   0x30
#define CRAB_MODE   0x32

static int crab_set_ani(uint32_t o)
{
    int8_t d3 = (int8_t)OB8(o, OBJ_angle);
    int x = OB8(o, OBJ_status) & ST_XFLIP;
    if (d3 >= 0) {
        if ((uint8_t)d3 < 6) return 0;
        return x ? 1 : 2;
    }
    if ((uint8_t)d3 > (uint8_t)-6) return 0;
    return x ? 2 : 1;
}

static void crab_ball(uint32_t o, int dx, int vx, uint32_t parent)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x1F * 4);
    SET8(a1, OBJ_routine, 6);
    SET16(a1, OBJ_x_pos, OB16(parent, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(parent, OBJ_y_pos));
    SET16(a1, OBJ_x_vel, vx);
}

static void crab(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: { /* Crab_Main */
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_x_radius, 8);
        o_init(o, S1A_CRAB, 4, 3, 0x15, 0x10);
        SET8(o, OBJ_coll_flags, 6);
        o_fall(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET8(o, OBJ_angle, (uint8_t)D(3));
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_routine, 2);
        }
        return;
    }
    case 2: { /* Crab_Action */
        if (OB8(o, R2) == 0) {              /* .waittofire */
            int16_t t = (int16_t)(OBS16(o, CRAB_TIME) - 1);
            SET16(o, CRAB_TIME, t);
            if (t < 0) {
                int fire = 0;
                if (OB8(o, OBJ_render) & 0x80) {
                    uint8_t m = OB8(o, CRAB_MODE);
                    SET8(o, CRAB_MODE, m ^ 2);
                    fire = (m & 2) != 0;
                }
                if (fire) {                 /* .fire */
                    SET16(o, CRAB_TIME, 59);
                    SET8(o, OBJ_anim, 6);
                    crab_ball(o, -0x10, -0x100, o);
                    crab_ball(o, 0x10, 0x100, o);
                } else {                    /* .movecrab */
                    SET8(o, R2, 2);
                    SET16(o, CRAB_TIME, 127);
                    SET16(o, OBJ_x_vel, 0x80);
                    SET8(o, OBJ_anim, crab_set_ani(o) + 3);
                    uint8_t st = OB8(o, OBJ_status);
                    SET8(o, OBJ_status, st ^ ST_XFLIP);
                    if (!(st & ST_XFLIP))
                        SET16(o, OBJ_x_vel, -0x80);
                }
            }
        } else {                            /* .walkonfloor */
            int16_t t = (int16_t)(OBS16(o, CRAB_TIME) - 1);
            SET16(o, CRAB_TIME, t);
            int stop = t < 0;
            if (!stop) {
                o_move(o);
                uint8_t m = OB8(o, CRAB_MODE);
                SET8(o, CRAB_MODE, m ^ 1);
                if (!(m & 1)) {
                    int d3 = OB16(o, OBJ_x_pos) + 0x10;
                    if (OB8(o, OBJ_status) & ST_XFLIP) d3 -= 0x20;
                    A(0) = RAMPTR(o);
                    D(3) = (uint32_t)(uint16_t)d3;
                    tri_jsr(S3K_ObjCheckFloorDist + 4);     /* ObjCheckFloorDist2 */
                    int16_t d1 = (int16_t)D(1);
                    if (d1 < -8 || d1 >= 0x0C) stop = 1;
                } else {
                    int16_t d1 = o_floor_dist(o);
                    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                    SET8(o, OBJ_angle, (uint8_t)D(3));
                    SET8(o, OBJ_anim, crab_set_ani(o) + 3);
                }
            }
            if (stop) {                     /* loc_966E */
                SET8(o, R2, 0);
                SET16(o, CRAB_TIME, 59);
                SET16(o, OBJ_x_vel, 0);
                SET8(o, OBJ_anim, crab_set_ani(o));
            }
        }
        o_animate(o, o_asset_anim(S1A_CRAB));
        o_touch(o);
        o_remember(o);
        return;
    }
    case 6:   /* Crab_BallMain */
        o_init(o, S1A_CRAB, 4, 3, 8, 8);
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_coll_flags, 0x87);
        SET16(o, OBJ_y_vel, -0x400);
        SET8(o, OBJ_anim, 7);
        /* fall through */
    case 8: { /* Crab_BallMove */
        o_animate(o, o_asset_anim(S1A_CRAB));
        o_fall(o);
        uint16_t lim = (uint16_t)(r16(RAM_Camera_max_Y_pos) + 0xE0);
        if (lim < OB16(o, OBJ_y_pos)) { o_delete(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
    default:
        o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 22 - Buzz Bomber, Object 23 - missile
 * ---------------------------------------------------------------------- */
#define BUZZ_TIME   0x32
#define BUZZ_STAT   0x34
#define BUZZ_PARENT 0x3C    /* long */

static void buzz(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_BUZZ, 4, 3, 0x18, 0x10);
        SET8(o, OBJ_coll_flags, 8);
        /* fall through */
    case 2:
        if (OB8(o, R2) == 0) {              /* .move */
            int16_t t = (int16_t)(OBS16(o, BUZZ_TIME) - 1);
            SET16(o, BUZZ_TIME, t);
            if (t < 0) {
                if (OB8(o, BUZZ_STAT) & 2) {    /* .fire */
                    uint32_t a1 = o_alloc();
                    if (a1) {
                        SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x23 * 4);
                        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x1C);
                        SET16(a1, OBJ_y_vel, 0x200);
                        SET16(a1, OBJ_x_vel, 0x200);
                        int d0 = 0x18 - 4;          /* FixBugs offset */
                        if (!(OB8(o, OBJ_status) & ST_XFLIP)) {
                            d0 = -d0;
                            SET16(a1, OBJ_x_vel, -0x200);
                        }
                        SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + d0);
                        SET8(a1, OBJ_status, OB8(o, OBJ_status));
                        SET16(a1, BUZZ_TIME, 0x0E);
                        SET32(a1, BUZZ_PARENT, o);
                        SET8(o, BUZZ_STAT, 1);
                        SET16(o, BUZZ_TIME, 59);
                        SET8(o, OBJ_anim, 2);
                    }
                } else {
                    SET8(o, R2, 2);
                    SET16(o, BUZZ_TIME, 127);
                    SET16(o, OBJ_x_vel, 0x400);
                    SET8(o, OBJ_anim, 1);
                    if (!(OB8(o, OBJ_status) & ST_XFLIP))
                        SET16(o, OBJ_x_vel, -0x400);
                }
            }
        } else {                            /* .chknearsonic */
            int16_t t = (int16_t)(OBS16(o, BUZZ_TIME) - 1);
            SET16(o, BUZZ_TIME, t);
            int stop = 0;
            if (t < 0) {                    /* .chgdirection */
                SET8(o, BUZZ_STAT, 0);
                SET8(o, OBJ_status, OB8(o, OBJ_status) ^ ST_XFLIP);
                SET16(o, BUZZ_TIME, 59);
                stop = 1;
            } else {
                o_move(o);
                if (!OB8(o, BUZZ_STAT)) {
                    int d0 = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
                    if (d0 < 0) d0 = -d0;
                    if (d0 < 0x60 && (OB8(o, OBJ_render) & 0x80)) {
                        SET8(o, BUZZ_STAT, 2);
                        SET16(o, BUZZ_TIME, 29);
                        stop = 1;
                    }
                }
            }
            if (stop) {
                SET8(o, R2, 0);
                SET16(o, OBJ_x_vel, 0);
                SET8(o, OBJ_anim, 0);
            }
        }
        o_animate(o, o_asset_anim(S1A_BUZZ));
        o_touch(o);
        o_remember(o);
        return;
    default:
        o_delete(o);
        return;
    }
}

static int missile_cancel(uint32_t o)
{
    uint32_t p = OB16(o, BUZZ_PARENT + 2) | 0xFF0000u;
    if (r32(p) == S3K_Obj_Explosion) {
        o_delete(o);
        return 1;
    }
    return 0;
}

static void missile(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: { /* Msl_Main */
        int16_t t = (int16_t)(OBS16(o, BUZZ_TIME) - 1);
        SET16(o, BUZZ_TIME, t);
        if (t >= 0) { missile_cancel(o); return; }
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_MISSILE, 4, 3, 8, 8);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & 3);
        if (OB8(o, OBJ_subtype)) {          /* from a Newtron */
            SET8(o, OBJ_routine, 8);
            SET8(o, OBJ_coll_flags, 0x87);
            SET8(o, OBJ_anim, 1);
            goto animate2;
        }
    }
    /* fall through */
    case 2:   /* Msl_Animate */
        if (missile_cancel(o)) return;
        o_animate(o, o_asset_anim(S1A_MISSILE));
        o_draw(o);
        return;
    case 4: { /* Msl_FromBuzz */
        SET8(o, OBJ_coll_flags, 0x87);
        SET8(o, OBJ_anim, 1);
        o_move(o);
        uint16_t lim = (uint16_t)(r16(RAM_Camera_max_Y_pos) + 0xE0);
        if (lim < OB16(o, OBJ_y_pos)) { o_delete(o); return; }
        o_animate(o, o_asset_anim(S1A_MISSILE));
        o_touch(o);
        o_draw(o);
        return;
    }
    case 8:   /* Msl_FromNewt */
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_move(o);
    animate2:
        o_animate(o, o_asset_anim(S1A_MISSILE));
        o_touch(o);
        o_draw(o);
        return;
    default:
        o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 2B - Chopper
 * ---------------------------------------------------------------------- */
#define CHOP_ORIGY  0x30

static void chopper(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_CHOP, 4, 4, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 9);
        SET16(o, OBJ_y_vel, -0x700);
        SET16(o, CHOP_ORIGY, OB16(o, OBJ_y_pos));
    }
    o_animate(o, o_asset_anim(S1A_CHOP));
    o_move(o);
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
    uint16_t d0 = OB16(o, CHOP_ORIGY);
    if (d0 < OB16(o, OBJ_y_pos)) {
        SET16(o, OBJ_y_pos, d0);
        SET16(o, OBJ_y_vel, -0x700);
    }
    SET8(o, OBJ_anim, 1);
    d0 = (uint16_t)(d0 - 0xC0);
    if (d0 < OB16(o, OBJ_y_pos)) {
        SET8(o, OBJ_anim, 0);
        if (OBS16(o, OBJ_y_vel) >= 0)
            SET8(o, OBJ_anim, 2);
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 42 - Newtron
 * ---------------------------------------------------------------------- */
static void newtron(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_NEWT, 4, 4, 0x14, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_x_radius, 8);
        /* fall through */
    case 2: {
        uint16_t px = r16(RAM_Player_1 + OBJ_x_pos);
        switch (OB8(o, R2)) {
        case 0: { /* .chkdistance */
            SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_XFLIP);
            int d0 = (int16_t)(px - OB16(o, OBJ_x_pos));
            if (d0 < 0) {
                d0 = -d0;
                SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_XFLIP);
            }
            if (d0 < 0x80) {
                SET8(o, R2, 2);
                SET8(o, OBJ_anim, 1);
                if (OB8(o, OBJ_subtype)) {
                    o_asset(o, S1A_NEWT2);
                    SET8(o, R2, 8);
                    SET8(o, OBJ_anim, 4);
                }
            }
            break;
        }
        case 2:   /* .type00 */
            if (OB8(o, OBJ_frame) < 4) {
                SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_XFLIP);
                if ((int16_t)(px - OB16(o, OBJ_x_pos)) < 0)
                    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_XFLIP);
                break;
            }
            if (OB8(o, OBJ_frame) == 1)
                SET8(o, OBJ_coll_flags, 0x0C);
            o_fall(o);
            {
                int16_t d1 = o_floor_dist(o);
                if (d1 < 0) {
                    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                    SET16(o, OBJ_y_vel, 0);
                    SET8(o, R2, 4);
                    SET8(o, OBJ_anim, 2);
                    if (OB16(o, OBJ_art_tile) & 0x2000)     /* green (line 1) */
                        SET8(o, OBJ_anim, 3);
                    SET8(o, OBJ_coll_flags, 0x0D);
                    SET16(o, OBJ_x_vel, 0x200);
                    if (!(OB8(o, OBJ_status) & ST_XFLIP))
                        SET16(o, OBJ_x_vel, -0x200);
                }
            }
            break;
        case 4: { /* .matchfloor */
            o_move(o);
            int16_t d1 = o_floor_dist(o);
            if (d1 < -8 || d1 >= 0x0C) SET8(o, R2, 6);
            else SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            break;
        }
        case 6:   /* .speed */
            o_move(o);
            break;
        case 8:   /* .type01 */
            if (OB8(o, OBJ_frame) == 1)
                SET8(o, OBJ_coll_flags, 0x0C);
            if (OB8(o, OBJ_frame) == 2 && !OB8(o, 0x32)) {
                SET8(o, 0x32, 1);
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x23 * 4);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 8);
                    SET16(a1, OBJ_x_vel, 0x200);
                    int d0 = 0x14;
                    if (!(OB8(o, OBJ_status) & ST_XFLIP)) {
                        d0 = -d0;
                        SET16(a1, OBJ_x_vel, -0x200);
                    }
                    SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + d0);
                    SET8(a1, OBJ_status, OB8(o, OBJ_status));
                    SET8(a1, OBJ_subtype, 1);
                }
            }
            break;
        }
        o_animate(o, o_asset_anim(S1A_NEWT));
        o_touch(o);
        o_remember(o);
        return;
    }
    default:
        o_delete(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 3B - purple rock, 44 - edge walls, 1C - scenery
 * ---------------------------------------------------------------------- */
static void purple_rock(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_PROCK, 4, 4, 0x13, 0x10);
    }
    o_solid(o, 0x1B, 0x10, 0x10, OB16(o, OBJ_x_pos));
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void edge_wall(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_EDGE, OB8(o, OBJ_render) | 4, 6, 8, 0x40);
        uint8_t f = OB8(o, OBJ_subtype);
        SET8(o, OBJ_frame, f & ~0x10);
        if (f & 0x10)
            SET8(o, OBJ_routine, 4);
    }
    if (OB8(o, OBJ_routine) == 2)
        o_solid(o, 0x13, 0x28, 0x29, OB16(o, OBJ_x_pos));
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void scenery(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        if (OB8(o, OBJ_subtype) < 3) {       /* SLZ fireball launcher */
            o_init(o, S1A_SCEN_SLZ, OB8(o, OBJ_render) | 4, 2, 8, 0x10);
        } else {                             /* GHZ (subtype 3): the bridge stump */
            o_init(o, S1A_BRIDGE, OB8(o, OBJ_render) | 4, 1, 0x10, 0x10);
            SET8(o, OBJ_frame, 1);
        }
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 11 - bridge: the S3K tension bridge (two-player physics) wearing
 * the Sonic 1 log art.
 * ---------------------------------------------------------------------- */
static void bridge(uint32_t o)
{
    A(0) = RAMPTR(o);
    tri_jsr(S3K_Obj_TensionBridge);          /* builds the log children */
    uint32_t maps = o_asset_maps(S1A_BRIDGE);
    uint16_t tile = (uint16_t)(o_asset_tile(S1A_BRIDGE));
    SET32(o, OBJ_mappings, maps);
    SET16(o, OBJ_art_tile, tile);
    for (int k = 0; k < 2; k++) {
        uint32_t c = r32(o + 0x30 + k * 4) & 0xFFFFFF;
        if (c >= 0xFF0000u) {
            SET32(c, OBJ_mappings, maps);
            SET16(c, OBJ_art_tile, tile);
        }
    }
}

void s1_obj_ghz_register(TriObjFn *t)
{
    t[0x11] = bridge;
    t[0x1C] = scenery;
    t[0x1F] = crab;
    t[0x22] = buzz;
    t[0x23] = missile;
    t[0x2B] = chopper;
    t[0x3B] = purple_rock;
    t[0x40] = moto;
    t[0x42] = newtron;
    t[0x44] = edge_wall;
}

void s1_objects_register(TriObjFn *t)
{
    s1_obj_ghz_register(t);
    s1_obj_ghz2_register(t);
    s1_obj_flow_register(t);
    s1_obj_boss_register(t);
    s1_obj_mz_register(t);
    s1_obj_syz_register(t);
    s1_obj_lz_register(t);
    s1_obj_slz_register(t);
    s1_obj_sbz_register(t);
    s1_obj_fz_register(t);
}
