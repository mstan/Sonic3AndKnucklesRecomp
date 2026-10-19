/*
 * s1_obj_slz.c — Sonic 1 Star Light Zone objects, transcribed from the REV01
 * object code: elevators (59), circling platforms (5A), staircase (5B),
 * foreground pylon (5C), fans (5D), seesaws (5E), Bomb (5F).
 *
 * S1 platforms switch between "solid" and "standing" routines; on S3K one
 * SolidObjectTop call per frame (with the old x) handles both players, so
 * those objects keep a single routine and test the standing bits instead.
 */
#include "trilogy_obj.h"

#define OBJ_2nd      0x25
#define OBJ_jumping  0x40
#define ANI_SPRING   0x10

static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

static int chk_del(uint32_t o, uint16_t x)
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return 1; }
    return 0;
}

/* Throw every player standing on o into the air (spring launch, platform gone) */
static void launch_players(uint32_t o, int16_t yvel, int spring)
{
    for (int i = 0; i < 2; i++) {
        uint8_t bit = i ? ST_P2_STAND : ST_P1_STAND;
        if (!(OB8(o, OBJ_status) & bit)) continue;
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
        uint32_t p = o_player(i);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
        if (!spring) continue;
        SET16(p, OBJ_y_vel, (uint16_t)yvel);
        SET8(p, OBJ_jumping, 0);
        SET8(p, OBJ_anim, ANI_SPRING);
    }
}

/* ------------------------------------------------------------------------
 * Object 59 - platforms that move when stood on (and the platform maker)
 * ---------------------------------------------------------------------- */
#define EL_ORIGY  0x30
#define EL_ORIGX  0x32
#define EL_POS    0x34                /* long: distance moved (16.16) */
#define EL_SPEED  0x38
#define EL_DECEL  0x3A
#define EL_DIST   0x3C
#define EL_RATE   0x3E

static void elev_move(uint32_t o)               /* Elev_Move */
{
    uint16_t d0 = OB16(o, EL_SPEED);
    if (!OB8(o, EL_DECEL)) { if (d0 < 0x800) d0 = (uint16_t)(d0 + 0x10); }
    else if (d0) d0 = (uint16_t)(d0 - 0x10);
    SET16(o, EL_SPEED, d0);
    uint32_t pos = r32(o + EL_POS) + (uint32_t)((int32_t)(int16_t)d0 << 8);
    SET32(o, EL_POS, pos);
    uint16_t p = (uint16_t)(pos >> 16), d2 = OB16(o, EL_DIST);
    if (p > d2) SET8(o, EL_DECEL, 1);
    if (p == (uint16_t)(d2 * 2)) SET8(o, OBJ_subtype, 0);
}

static void elevator(uint32_t o)
{
    static const uint8_t var2[16][2] = {
        { 0x10, 1 }, { 0x20, 1 }, { 0x34, 1 }, { 0x10, 3 }, { 0x20, 3 }, { 0x34, 3 }, { 0x14, 1 }, { 0x24, 1 },
        { 0x2C, 1 }, { 0x14, 3 }, { 0x24, 3 }, { 0x2C, 3 }, { 0x20, 5 }, { 0x20, 7 }, { 0x30, 9 }, { 0x30, 9 } };
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        uint8_t sub = OB8(o, OBJ_subtype);
        if (sub & 0x80) {                      /* Elev_MakeMulti */
            SET8(o, OBJ_routine, 6);
            SET16(o, EL_DIST, (sub & 0x7F) * 6);
            SET16(o, EL_RATE, (sub & 0x7F) * 6);
            return;
        }
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_ELEV, 4, 4, 0x28, 0x10);
        SET8(o, OBJ_frame, 0);
        SET16(o, EL_DIST, var2[sub & 0x0F][0] * 4);
        SET8(o, OBJ_subtype, var2[sub & 0x0F][1]);
        SET16(o, EL_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, EL_ORIGY, OB16(o, OBJ_y_pos));
    }
    /* fall through */
    case 2: {
        uint16_t oldx = OB16(o, OBJ_x_pos);
        int16_t pos;
        switch (OB8(o, OBJ_subtype) & 0x0F) {
        case 1: case 3: case 5: case 7:
            if (o_standing(o)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
            break;
        case 2:
            elev_move(o);
            SET16(o, OBJ_y_pos, OB16(o, EL_ORIGY) - OB16(o, EL_POS));
            break;
        case 4:
            elev_move(o);
            SET16(o, OBJ_y_pos, OB16(o, EL_ORIGY) + OB16(o, EL_POS));
            break;
        case 6:
            elev_move(o);
            pos = (int16_t)OB16(o, EL_POS);
            SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, EL_ORIGY) - (pos >> 1)));
            SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, EL_ORIGX) + pos));
            break;
        case 8:
            elev_move(o);
            pos = (int16_t)OB16(o, EL_POS);
            SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, EL_ORIGY) + (pos >> 1)));
            SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, EL_ORIGX) - pos));
            break;
        case 9:
            elev_move(o);
            SET16(o, OBJ_y_pos, OB16(o, EL_ORIGY) - OB16(o, EL_POS));
            if (!OB8(o, OBJ_subtype)) {            /* .typereset: done, vanish */
                launch_players(o, 0, 0);
                o_delete(o);
                return;
            }
            break;
        default:
            break;
        }
        o_platform(o, OB8(o, OBJ_width_px), 8, oldx);
        if (chk_del(o, OB16(o, EL_ORIGX))) return;
        o_draw(o);
        return;
    }
    default:                                   /* Elev_MakeMulti */
        SET16(o, EL_DIST, OB16(o, EL_DIST) - 1);
        if (!OB16(o, EL_DIST)) {
            SET16(o, EL_DIST, OB16(o, EL_RATE));
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x59 * 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_subtype, 0x0E);
            }
        }
        chk_del(o, OB16(o, OBJ_x_pos));
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 5A - platforms moving in circles
 * ---------------------------------------------------------------------- */
#define CI_ORIGY  0x30
#define CI_ORIGX  0x32

static void circling(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_CIRC, 4, 4, 0x18, 0x10);
        SET16(o, CI_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, CI_ORIGY, OB16(o, OBJ_y_pos));
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    uint8_t sub = OB8(o, OBJ_subtype);
    int16_t d1 = (int8_t)(uint8_t)(o_osc(0x22) - 0x50);
    int16_t d2 = (int8_t)(uint8_t)(o_osc(0x26) - 0x50);
    if (sub & 1) { d1 = (int16_t)-d1; d2 = (int16_t)-d2; }
    if (sub & 2) { d1 = (int16_t)-d1; int16_t t = d1; d1 = d2; d2 = t; }
    if (sub & 4) d1 = (int16_t)-d1;            /* .type04 */
    SET16(o, OBJ_x_pos, (uint16_t)(d1 + OB16(o, CI_ORIGX)));
    SET16(o, OBJ_y_pos, (uint16_t)(d2 + OB16(o, CI_ORIGY)));
    o_platform(o, OB8(o, OBJ_width_px), 8, oldx);
    if (chk_del(o, OB16(o, CI_ORIGX))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 5B - blocks that form a staircase. The parent keeps the four block
 * offsets at $38-$3B; each block reads its own at parent+$37 (child's index).
 * ---------------------------------------------------------------------- */
#define ST_ORIGX   0x30
#define ST_ORIGY   0x32
#define ST_TIME    0x34
#define ST_TOUCH   0x36
#define ST_INDEX   0x37
#define ST_PARENT  0x3C

static void stair_solid(uint32_t o)             /* Stair_Solid */
{
    uint32_t a2 = r32(o + ST_PARENT) & 0xFFFFFF;
    SET16(o, OBJ_y_pos, (uint16_t)(r8(a2 + OB8(o, ST_INDEX)) + OB16(o, ST_ORIGY)));
    o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x10, 0x11, OB16(o, OBJ_x_pos));
    if ((g_o_d6 >> 16) & 0x0C) SET8(a2, ST_TOUCH, 0xFF);   /* hit from below (d4 < 0) */
    if (o_standing(o)) SET8(a2, ST_TOUCH, 1);
}

static void staircase(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        int d3 = 0x38, d4 = 1;
        if (OB8(o, OBJ_status) & 1) { d3 = 0x3B; d4 = -1; }
        uint16_t d2 = OB16(o, OBJ_x_pos);
        uint32_t a1 = o;
        for (int i = 0; i < 4; i++) {
            if (i) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x5B * 4);
                SET8(a1, OBJ_routine, 4);
            }
            o_init(a1, S1A_STAIR, 4, 3, 0x10, 0x10);
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            SET16(a1, OBJ_x_pos, d2);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, ST_ORIGX, OB16(o, OBJ_x_pos));
            SET16(a1, ST_ORIGY, OB16(a1, OBJ_y_pos));
            d2 = (uint16_t)(d2 + 0x20);
            SET8(a1, ST_INDEX, d3);
            SET32(a1, ST_PARENT, RAMPTR(o));
            d3 += d4;
        }
    }
    /* fall through */
    case 2:                                    /* Stair_Move (parent) */
        switch (OB8(o, OBJ_subtype) & 7) {
        case 0:
            if (!OB16(o, ST_TIME)) {
                if (OB8(o, ST_TOUCH) == 1) SET16(o, ST_TIME, 0x1E);
            } else {
                SET16(o, ST_TIME, OB16(o, ST_TIME) - 1);
                if (!OB16(o, ST_TIME)) SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
            }
            break;
        case 2:
            if (!OB16(o, ST_TIME)) {
                if (OB8(o, ST_TOUCH) & 0x80) SET16(o, ST_TIME, 0x3C);
            } else {
                SET16(o, ST_TIME, OB16(o, ST_TIME) - 1);
                if (!OB16(o, ST_TIME)) { SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1); break; }
                uint8_t d0 = (uint8_t)((OB16(o, ST_TIME) >> 2) & 1);
                for (int k = 0; k < 4; k++, d0 ^= 1) SET8(o, 0x38 + k, d0);   /* shake */
            }
            break;
        case 1: case 3: {                      /* Stair_Type01: steps spread out */
            uint8_t a = OB8(o, 0x38);
            if (a == 0x80) break;
            a++;
            SET8(o, 0x38, a);
            uint32_t d1 = ((uint32_t)a << 16) >> 1, d2 = d1;
            d1 >>= 1;
            uint32_t d3 = d1 + d2;
            SET8(o, 0x39, (uint8_t)(d3 >> 16));
            SET8(o, 0x3A, (uint8_t)(d2 >> 16));
            SET8(o, 0x3B, (uint8_t)(d1 >> 16));
            break;
        }
        default:
            break;
        }
        /* fall through */
    default:                                   /* Stair_Solid */
        stair_solid(o);
        break;
    }
    if (o_out_of_range(o, OB16(o, ST_ORIGX))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 5C - metal pylons in the foreground (screen-space, 2x parallax)
 * ---------------------------------------------------------------------- */
static void pylon(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_PYLON, 0, 0, 0x10, 0x80);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);   /* Tile_Prio */
    }
    uint32_t d1 = r32(RAM_Camera_X_pos) << 1;
    SET16(o, OBJ_x_pos, (uint16_t)-(int16_t)(d1 >> 16));
    d1 = r32(RAM_Camera_Y_pos) << 1;
    SET16(o, OBJ_y_pos, (uint16_t)(0x100 - ((d1 >> 16) & 0x3F)));
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 5D - fans
 * ---------------------------------------------------------------------- */
#define FAN_TIME    0x30
#define FAN_SWITCH  0x32

static void fan(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_FAN, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
    }
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!(sub & 2)) {
        SET16(o, FAN_TIME, OB16(o, FAN_TIME) - 1);
        if ((int16_t)OB16(o, FAN_TIME) < 0) {
            SET16(o, FAN_TIME, 120);
            uint8_t sw = OB8(o, FAN_SWITCH);
            SET8(o, FAN_SWITCH, sw ^ 1);
            if (sw & 1) SET16(o, FAN_TIME, 180);     /* switched on: 3 seconds (off: 2) */
        }
    }
    if (OB8(o, FAN_SWITCH)) goto out;
    {
        uint32_t p = RAM_Player_1;
        int flip = OB8(o, OBJ_status) & 1;
        uint16_t d0 = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (!flip) d0 = (uint16_t)-d0;
        d0 = (uint16_t)(d0 + 0x50);
        if (d0 < 0xF0) {
            uint16_t d1 = (uint16_t)(OB16(p, OBJ_y_pos) + 0x60);
            if (d1 >= OB16(o, OBJ_y_pos) && (uint16_t)(d1 - OB16(o, OBJ_y_pos)) < 0x70) {
                if (d0 >= 0x50) d0 = (uint16_t)(d0 - 0x50);       /* .faraway */
                else d0 = (uint16_t)(~(uint16_t)(d0 - 0x50) * 2);
                d0 = (uint16_t)(d0 + 0x60);
                if (!flip) d0 = (uint16_t)-d0;
                d0 = (uint16_t)((d0 & 0xFF00) | (uint8_t)-(int8_t)(uint8_t)d0);
                int16_t push = (int16_t)d0 >> 4;
                if (sub & 1) push = (int16_t)-push;
                SET16(p, OBJ_x_pos, (uint16_t)(OB16(p, OBJ_x_pos) + push));
            }
        }
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
            SET8(o, OBJ_anim_timer, 0);
            uint8_t f = (uint8_t)(OB8(o, OBJ_anim_frame) + 1);
            if (f >= 3) f = 0;
            SET8(o, OBJ_anim_frame, f);
            SET8(o, OBJ_frame, ((sub & 1) ? 2 : 0) + f);
        }
    }
out:
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 5E - seesaws (and their spiked balls)
 * ---------------------------------------------------------------------- */
#define SEE_ORIGX   0x30
#define SEE_ORIGY   0x34
#define SEE_SPEED   0x38
#define SEE_FRAME   0x3A
#define SEE_PARENT  0x3C

static const int16_t see_speeds[5] = { -8, -0x1C, -0x2F, -0x1C, -8 };

static void see_chgframe(uint32_t o, uint8_t d1)  /* See_ChgFrame */
{
    uint8_t d0 = OB8(o, OBJ_frame);
    if (d0 == d1) return;
    if (d0 < d1) d0 = (uint8_t)(d0 + 2);
    d0--;
    SET8(o, OBJ_frame, d0);
    SET8(o, SEE_FRAME, d1);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | ((d0 & 2) ? 1 : 0));
}

static void seesaw_board(uint32_t o)
{
    if (!o_standing(o)) {                      /* See_Slope */
        SET16(o, SEE_SPEED, r16(RAM_Player_1 + OBJ_y_vel));
        see_chgframe(o, OB8(o, SEE_FRAME));
    } else {                                   /* See_ChkSide (player 1) */
        uint8_t d1 = 2;
        int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - r16(RAM_Player_1 + OBJ_x_pos));
        if (d0 < 0) { d0 = (int16_t)-d0; d1 = 0; }
        if (d0 < 8) d1 = 1;
        see_chgframe(o, d1);
    }
    uint32_t slope = blob((OB8(o, OBJ_frame) & 1) ? TRI_BLOB_S1_SEESAW_FLAT : TRI_BLOB_S1_SEESAW_SLOPE);
    o_platform_sloped(o, 0x30, 0, OB16(o, OBJ_x_pos), slope);
}

/* ball lands on the board: set its new tilt, maybe fling whoever stands on the other end */
void s1_seesaw_land(uint32_t board, uint32_t ball, uint8_t d1, int half)
{
    SET8(board, SEE_FRAME, d1);
    SET8(ball, SEE_FRAME, d1);
    if (OB8(board, OBJ_frame) == d1 || !o_standing(board)) return;
    int16_t vy = (int16_t)-OBS16(ball, OBJ_y_vel);
    if (half && OB8(board, OBJ_frame) == 1) vy >>= 1;
    launch_players(board, vy, 1);
    o_sfx(0xB1);                               /* sfx_Spring */
}

static void seesaw(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SEESAW, OB8(o, OBJ_render) | 4, 4, 0x30, 0x20);
        SET16(o, SEE_ORIGX, OB16(o, OBJ_x_pos));
        if (!OB8(o, OBJ_subtype)) {
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x5E * 4);
                SET8(a1, OBJ_routine, 6);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_status, OB8(o, OBJ_status));
                SET32(a1, SEE_PARENT, RAMPTR(o));
            }
        }
        if (OB8(o, OBJ_status) & 1) SET8(o, OBJ_frame, 2);
        SET8(o, SEE_FRAME, OB8(o, OBJ_frame));
        /* fall through */
    case 2: case 4:
        seesaw_board(o);
        break;
    case 6:                                    /* See_Spikeball */
        SET8(o, OBJ_routine, 8);
        o_init(o, S1A_SSAWBALL, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        SET8(o, OBJ_coll_flags, 0x8B);
        SET16(o, SEE_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x28);
        SET16(o, SEE_ORIGY, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_frame, 1);
        if (OB8(o, OBJ_status) & 1) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x50);
            SET8(o, SEE_FRAME, 2);
        }
        /* fall through */
    case 8: {                                  /* See_MoveSpike */
        uint32_t a1 = r32(o + SEE_PARENT) & 0xFFFFFF;
        int8_t d0 = (int8_t)(OB8(o, SEE_FRAME) - OB8(a1, SEE_FRAME));
        if (d0) {
            if (d0 < 0) d0 = (int8_t)-d0;
            int16_t d1 = -0x818, d2 = -0x114;
            if (d0 != 1) {
                d1 = -0xAF0; d2 = -0xCC;
                if ((int16_t)OB16(a1, SEE_SPEED) >= 0xA00) { d1 = -0xE00; d2 = -0xA0; }
            }
            SET16(o, OBJ_y_vel, (uint16_t)d1);
            SET16(o, OBJ_x_vel, (uint16_t)d2);
            if (OB16(o, OBJ_x_pos) < OB16(o, SEE_ORIGX)) SET16(o, OBJ_x_vel, (uint16_t)-d2);
            SET8(o, OBJ_routine, 0x0A);
            goto fall;
        }
        int k = OB8(a1, OBJ_frame);
        int16_t dx = 0x28;
        if (OB16(o, OBJ_x_pos) < OB16(o, SEE_ORIGX)) { dx = -0x28; k += 2; }
        SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, SEE_ORIGY) + see_speeds[k]));
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, SEE_ORIGX) + dx));
        SET16(o, OBJ_y_pos + 2, 0);
        SET16(o, OBJ_x_pos + 2, 0);
        break;
    }
    default:                                   /* See_SpikeFall */
    fall:
        if ((int16_t)OB16(o, OBJ_y_vel) < 0) {
            o_fall(o);
            if ((int16_t)(OB16(o, SEE_ORIGY) - 0x2F) <= (int16_t)OB16(o, OBJ_y_pos)) o_fall(o);
            break;
        }
        o_fall(o);
        {
            uint32_t a1 = r32(o + SEE_PARENT) & 0xFFFFFF;
            int k = OB8(a1, OBJ_frame);
            if (OB16(o, OBJ_x_pos) < OB16(o, SEE_ORIGX)) k += 2;
            if ((int16_t)(OB16(o, SEE_ORIGY) + see_speeds[k]) > (int16_t)OB16(o, OBJ_y_pos)) break;
            s1_seesaw_land(a1, o, (OBS16(o, OBJ_x_vel) < 0) ? 2 : 0, 0);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_routine, 8);
        }
        break;
    }
    /* delete when the board's origin leaves the range (either side) */
    int16_t d0 = (int16_t)((OB16(o, SEE_ORIGX) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80));
    if (d0 < 0 || d0 > 0x280) { o_delete_respawnable(o); return; }
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 5F - Bomb: walks, lights its fuse near Sonic, bursts into shrapnel
 * ---------------------------------------------------------------------- */
#define BOM_TIME    0x30
#define BOM_ORIGY   0x34

static void bomb_chksonic(uint32_t o)            /* .chksonic */
{
    int16_t dx = (int16_t)(r16(RAM_Player_1 + OBJ_x_pos) - OB16(o, OBJ_x_pos));
    int16_t dy = (int16_t)(r16(RAM_Player_1 + OBJ_y_pos) - OB16(o, OBJ_y_pos));
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;
    if ((uint16_t)dx >= 0x60 || (uint16_t)dy >= 0x60 || r16(RAM_Debug_placement_mode)) return;
    SET8(o, OBJ_2nd, 4);
    SET16(o, BOM_TIME, 143);
    SET16(o, OBJ_x_vel, 0);
    SET8(o, OBJ_anim, 2);
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x5F * 4);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET16(a1, BOM_ORIGY, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_status, OB8(o, OBJ_status));
    SET8(a1, OBJ_subtype, 4);
    SET8(a1, OBJ_anim, 3);
    SET16(a1, OBJ_y_vel, (OB8(o, OBJ_status) & 2) ? -0x10 : 0x10);
    SET16(a1, BOM_TIME, 143);
}

static void bomb_end(uint32_t o)                 /* Bom_End */
{
    o_move(o);
    SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
    o_animate(o, o_asset_anim(S1A_BOMB));
    if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
    o_touch(o);
    o_draw(o);
}

static void bomb(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_BOMB, OB8(o, OBJ_render) | 4, 3, 0x0C, 0x0C);
        if (OB8(o, OBJ_subtype)) { SET8(o, OBJ_routine, OB8(o, OBJ_subtype)); return; }
        SET8(o, OBJ_coll_flags, 0x9A);
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        /* fall through */
    case 2:                                    /* Bom_Action */
        switch (OB8(o, OBJ_2nd)) {
        case 0:                                /* .walk */
            bomb_chksonic(o);
            SET16(o, BOM_TIME, OB16(o, BOM_TIME) - 1);
            if ((int16_t)OB16(o, BOM_TIME) < 0) {
                SET8(o, OBJ_2nd, 2);
                SET16(o, BOM_TIME, 1535);
                SET16(o, OBJ_x_vel, 0x10);
                SET8(o, OBJ_anim, 1);
                uint8_t st = OB8(o, OBJ_status);
                SET8(o, OBJ_status, st ^ 1);
                if (st & 1) SET16(o, OBJ_x_vel, -0x10);
            }
            break;
        case 2:                                /* .wait (walking) */
            bomb_chksonic(o);
            SET16(o, BOM_TIME, OB16(o, BOM_TIME) - 1);
            if ((int16_t)OB16(o, BOM_TIME) < 0) {
                SET8(o, OBJ_2nd, 0);
                SET16(o, BOM_TIME, 179);
                SET16(o, OBJ_x_vel, 0);
                SET8(o, OBJ_anim, 0);
            } else {
                o_move(o);
            }
            break;
        default:                               /* .explode */
            SET16(o, BOM_TIME, OB16(o, BOM_TIME) - 1);
            if ((int16_t)OB16(o, BOM_TIME) < 0) {
                SET32(o, OBJ_code, S3K_Obj_Explosion);   /* id_Explosion ($3F: no animal) */
                SET8(o, OBJ_routine, 6);
                o_sfx(0xB4);                             /* sfx_Bomb */
                return;
            }
            break;
        }
        o_animate(o, o_asset_anim(S1A_BOMB));
        if (chk_del(o, OB16(o, OBJ_x_pos))) return;
        o_touch(o);
        o_draw(o);
        return;
    case 4: {                                  /* Bom_Display: the fuse */
        SET16(o, BOM_TIME, OB16(o, BOM_TIME) - 1);
        if ((int16_t)OB16(o, BOM_TIME) >= 0) {
            o_move(o);
            o_animate(o, o_asset_anim(S1A_BOMB));
            if (chk_del(o, OB16(o, OBJ_x_pos))) return;
            o_draw(o);
            return;
        }
        static const int16_t shr[4][2] = { { -0x200, -0x300 }, { -0x100, -0x200 }, { 0x200, -0x300 }, { 0x100, -0x200 } };
        SET16(o, BOM_TIME, 0);
        SET16(o, OBJ_y_pos, OB16(o, BOM_ORIGY));
        uint32_t a1 = o;
        for (int i = 0; i < 4; i++) {
            if (i) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x5F * 4);
                SET8(a1, OBJ_routine, 0);
            }
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_subtype, 6);
            SET8(a1, OBJ_anim, 4);
            SET16(a1, OBJ_x_vel, (uint16_t)shr[i][0]);
            SET16(a1, OBJ_y_vel, (uint16_t)shr[i][1]);
            SET8(a1, OBJ_coll_flags, 0x98);
            SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 0x80);
        }
        SET8(o, OBJ_routine, 6);
        bomb_end(o);
        return;
    }
    default:
        bomb_end(o);
        return;
    }
}

void s1_obj_slz_register(TriObjFn *t)
{
    t[0x59] = elevator;
    t[0x5A] = circling;
    t[0x5B] = staircase;
    t[0x5C] = pylon;
    t[0x5D] = fan;
    t[0x5E] = seesaw;
    t[0x5F] = bomb;
}
