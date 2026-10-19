/*
 * scd_ttz_blk.c — Tidal Tempest's moving block groups (Sonic CD; decomp:
 * R4\UDBLK4.C, LRBLK4.C, RBLK4.C, RENKETU4.C; the disc's code, R41B / R41C).
 *
 *   $23 up/down blocks    groups of solid blocks, each riding a sine about its
 *                         own middle, their phases set apart (subtype the
 *                         group: 0/1 a row of seven, 3 one tall, 4 three with
 *                         harir4 spikes under them, 5/7 a stair of four, 8
 *                         pairs, 6/9 columns, A a row of seven, B a lift that
 *                         carries blocks down in the middle, up at the sides
 *                         and wraps them round)
 *   $24 left/right blocks the same across: 0/1/7 a column of six swaying pairs,
 *                         2 a pair, 3 two rows of three that slide and wrap,
 *                         4/8 blocks that run one way and wrap (8: a pair below
 *                         a pair of 4s), 5 a column of five stepping to and
 *                         fro, 6 stairs of four
 *   $3D turnstile         an L of blocks round a pivot (two unseen arms are
 *                         the solid parts): stood on beyond the pivot with no
 *                         floor under it, it tips a quarter turn; pushed by
 *                         one stood on its other arm, it turns carrying him
 *                         over
 *   $3E linked platforms  three platforms and six links on an arc 96 px from
 *                         a centre, turning (one turn in 256 frames; subtype
 *                         bit 4 the other way); a switch (subtype & $F, unless
 *                         bit 7) held turns them back
 * Their code is the same in every period of acts 1 and 2. Each group's first
 * block (index 0) makes the rest (Sonic CD's FindObjSlot: o_alloc), all of
 * the same id; each goes by its own index. CD's object variables sit at
 * S3K's $30.. (a #define each names CD's), routines keep CD's even numbers.
 * SolidObject -> o_solid, TopSolidObject -> o_platform, GetOffObject ->
 * cd_get_off; objPlayerSlot -> Player 1 (the solids take Player 2 as S3K's
 * do). CD carries a rider by the object's speed: S3K's by where it was (d4),
 * so a block moves, then is solid from there.
 */
#include "trilogy_obj.h"

/* (Sonic CD assets, scd_level.py CD_ASSETS -> trilogy_obj.h) */

/* switchflag[] (bit 7 held, 6 ever, 5 toggled): the round's switches own it */
extern uint8_t scd_ttz_switch[32];

#define ST_ONOBJ        0x08
#define FM_TURN         0xBF               /* (rblk4_move: a click each step as it tips) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static uint32_t ram(uint16_t w) { return 0xFF0000u | w; }

/* SolidObject from where it was (x0) */
static void solid(uint32_t o, uint16_t x0)
{
    o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, x0);
}

/* the frame's move in px as CD's speed word (long difference >> 8) */
static uint16_t speed_of(uint32_t now, uint32_t was) { return (uint16_t)((int32_t)(now - was) >> 8); }

/* ======================================================================
 * $23 up/down blocks
 * ==================================================================== */
#define UD_YBASE        0x32               /* oVar32: the middle of its swing */
#define UD_PARENT       0x34               /* oVar34: (6's blocks) the group's controller */
#define UD_X0           0x36               /* oVar36: the group's place (its despawn) */
#define UD_ANGLE        0x3A               /* oVar3A (type 8: a word, 8.8) */
#define UD_INDEX        0x3C               /* oVar3C: its place in the group (0 the first) */
#define UD_FLAG         0x3D               /* oVar3D */
#define SP_PARENT       0x34               /* (harir4 $22, subtype 1: oVar34, the block it hangs from) */
#define SP_X0           0x36               /* (oVar36) */
#define SP_DY           0x39               /* (oVar39: below it by) */

static void ud_init(uint32_t o, int asset, int w, int h)
{
    SET8(o, OBJ_routine, 2);
    o_init(o, asset, OB8(o, OBJ_render) | 4, 3, w, h);
    SET8(o, OBJ_y_radius, h);
}

static uint32_t ud_new(uint32_t o, int type)    /* FindObjSlot: a block of the group */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_subtype, type);
    return a1;
}

/* the shared sway: about its middle by sin * (2^d2 + 2^d3) / 256, a step a frame */
static void ud_sway(uint32_t o, int d2, int d3)
{
    uint32_t was = r32(o + OBJ_y_pos);
    int16_t s, c;
    o_sincos(OB8(o, UD_ANGLE), &s, &c);
    int16_t d = (int16_t)((int16_t)((int16_t)(s << d2) + (int16_t)(s << d3)) >> 8);
    SET8(o, UD_ANGLE, OB8(o, UD_ANGLE) + 1);
    SET16(o, OBJ_y_pos, OB16(o, UD_YBASE) + d);
    SET16(o, OBJ_y_vel, speed_of((uint32_t)OB16(o, OBJ_y_pos) << 16, was));   /* (swap: no fraction) */
    solid(o, OB16(o, OBJ_x_pos));
}

static void ud_type1(uint32_t o)                /* 0, 1: a row of seven, out of step */
{
    static const int8_t tbl[7][2] = {
        { (int8_t)0xC0, 0 }, { (int8_t)0xA8, -32 }, { (int8_t)0x90, -64 }, { 0x78, -96 },
        { (int8_t)0xD8, 32 }, { (int8_t)0xF0, 64 }, { 8, 96 },
    };
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD1, 0x10, 0x40);
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos) + 0x40);
        if (!OB8(o, UD_INDEX)) {
            SET16(o, UD_X0, OB16(o, OBJ_x_pos));
            for (int d1 = 1; d1 <= 6; d1++) {
                uint32_t a1 = ud_new(o, 1);
                if (!a1) continue;
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(a1, UD_X0, OB16(o, UD_X0));
                SET8(a1, UD_INDEX, d1);
            }
        }
        const int8_t *t = tbl[OB8(o, UD_INDEX) % 7];
        SET8(o, UD_ANGLE, (uint8_t)t[0]);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + t[1]);
    }
    ud_sway(o, 6, 0);                                         /* (65 px) */
}

static void ud_type3(uint32_t o)                /* one tall block, 80 px */
{
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD3, 0x20, 0x60);
        SET16(o, UD_X0, OB16(o, OBJ_x_pos));
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos) - 0x50);
        SET8(o, UD_ANGLE, 0x40);
    }
    ud_sway(o, 6, 4);
}

static void ud_type4_new(uint32_t o, int index, int dx)
{
    uint32_t a1 = ud_new(o, 4);
    if (!a1) return;
    SET8(a1, UD_INDEX, index);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, UD_X0, OB16(o, UD_X0));
}

static void ud_type4(uint32_t o)                /* three, 48 px, spikes under each */
{
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD4, 0x12, 0x30);
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos) + 0x30);
        SET8(o, UD_ANGLE, 0xC0);
        if (!OB8(o, UD_INDEX)) SET16(o, UD_X0, OB16(o, OBJ_x_pos));
        uint32_t a1 = o_alloc();                              /* (harir4, sideways: it follows us) */
        if (a1) {
            SET32(a1, OBJ_code, CD_CODE(0x22));
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, SP_X0, OB16(o, UD_X0));
            SET16(a1, SP_PARENT, (uint16_t)o);
            SET8(a1, SP_DY, 0x40);
            SET8(a1, OBJ_subtype, 1);
        }
        if (!OB8(o, UD_INDEX)) {
            ud_type4_new(o, 1, -0x50);
            ud_type4_new(o, 2, 0x50);
        }
    }
    uint16_t y0 = OB16(o, OBJ_y_pos);
    int16_t s, c;
    o_sincos(OB8(o, UD_ANGLE), &s, &c);
    int16_t d = (int16_t)((int16_t)(s * 3) >> 4);
    SET16(o, OBJ_y_pos, OB16(o, UD_YBASE) + d);
    SET8(o, UD_ANGLE, OB8(o, UD_ANGLE) + 1);
    SET16(o, OBJ_y_vel, (uint16_t)((OB16(o, OBJ_y_pos) - y0) << 8));
    /* (CD then hands its y speed to oVar34's object: never set on a block) */
    solid(o, OB16(o, OBJ_x_pos));
}

static void ud_type5_new(uint32_t o, int dx, uint8_t angle, int dy)
{
    uint32_t a1 = ud_new(o, 5);
    if (!a1) return;
    SET8(a1, UD_INDEX, 1);
    SET8(a1, UD_ANGLE, angle);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET16(a1, UD_X0, OB16(o, UD_X0));
}

static void ud_type5(uint32_t o)                /* a stair of four, up to the right */
{
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD5, 0x14, 0x41);
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos) + 0x40);
        if (!OB8(o, UD_INDEX)) {
            SET16(o, UD_X0, OB16(o, OBJ_x_pos));
            SET8(o, UD_ANGLE, 0);
            ud_type5_new(o, 0x20, 0xF0, -0x20);
            ud_type5_new(o, 0x40, 0xE0, -0x40);
            ud_type5_new(o, 0x60, 0xD0, -0x60);
        }
    }
    ud_sway(o, 6, 0);
}

static void ud_type6(uint32_t o)                /* (unseen) seven columns of type 9 */
{
    static const int8_t tbl[7][2] = {
        { 0, 0 }, { 0x20, -32 }, { 0x40, -64 }, { 0x60, -96 }, { 0x20, 32 }, { 0x40, 64 }, { 0x60, 96 },
    };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET16(o, UD_X0, OB16(o, OBJ_x_pos));
        for (int k = 0; k < 7; k++) {
            uint32_t a1 = ud_new(o, 9);
            if (!a1) continue;
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, UD_PARENT, (uint16_t)o);
            SET8(a1, UD_FLAG, 0xFF);
            SET8(a1, UD_ANGLE, (uint8_t)tbl[k][0]);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + tbl[k][1]);
        }
    }
    cd_despawn(o, OB16(o, UD_X0));
}

static void ud_type7_new(uint32_t o, int dx, uint8_t angle)
{
    uint32_t a1 = ud_new(o, 7);
    if (!a1) return;
    SET8(a1, UD_INDEX, 1);
    SET8(a1, UD_ANGLE, angle);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, UD_X0, OB16(o, UD_X0));
}

static void ud_type7(uint32_t o)                /* four in a row, a wave along them */
{
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD5, 0x10, 0x40);
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos) + 0x40);
        if (!OB8(o, UD_INDEX)) {
            SET16(o, UD_X0, OB16(o, OBJ_x_pos) + 0x30);
            SET8(o, UD_ANGLE, 0xC0);
            ud_type7_new(o, 0x20, 0xE0);
            ud_type7_new(o, 0x40, 0x00);
            ud_type7_new(o, 0x60, 0x20);
        }
    }
    ud_sway(o, 6, 0);
}

static void ud_type8(uint32_t o)                /* pairs, one $A0 px under the other, 32 px */
{
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD8, 0x20, 0x30);
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos) - 0x20);
        if (!OB8(o, UD_FLAG)) SET16(o, UD_X0, OB16(o, OBJ_x_pos));
        uint32_t a1;
        if (!OB8(o, UD_INDEX) && (a1 = ud_new(o, 8))) {
            SET8(a1, UD_INDEX, 1);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, UD_X0, OB16(o, UD_X0));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0xA0);
            SET8(a1, UD_ANGLE, OB8(o, UD_ANGLE));
            SET8(a1, UD_FLAG, OB8(o, UD_FLAG));
            if (!OB8(o, UD_FLAG))
                for (int d1 = 1; d1 <= 2; d1++) {                 /* (two more pairs, either side) */
                    if (!(a1 = ud_new(o, 8))) continue;
                    SET8(a1, UD_FLAG, d1);
                    SET8(a1, UD_ANGLE, 0x80);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + (d1 == 1 ? -0x40 : 0x40));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                    SET16(a1, UD_X0, OB16(o, UD_X0));
                }
        }
    }
    uint16_t y0 = OB16(o, OBJ_y_pos);
    int16_t s, c;
    o_sincos(OB8(o, UD_ANGLE), &s, &c);
    int16_t d = (int16_t)((int16_t)(s * 2) >> 4);
    SET16(o, UD_ANGLE, OB16(o, UD_ANGLE) + 0x80);             /* (half a step a frame) */
    SET16(o, OBJ_y_pos, OB16(o, UD_YBASE) + d);
    SET16(o, OBJ_y_vel, (uint16_t)((OB16(o, OBJ_y_pos) - y0) << 8));
    solid(o, OB16(o, OBJ_x_pos));
}

static void ud_type9(uint32_t o)                /* a pair in a column, $80 apart, 33 px */
{
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD9, 0x10, 0x20);
        SET16(o, UD_X0, OB16(o, OBJ_x_pos));
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos));
        uint32_t a1;
        if (!OB8(o, UD_INDEX) && (a1 = ud_new(o, 9))) {
            SET8(a1, UD_INDEX, 1);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            int d1 = 0x80;
            uint8_t a = OB8(o, UD_ANGLE);
            if (OB8(o, UD_FLAG) & 0x80) { d1 = 0xA0; a = (uint8_t)(a - 0x80); }
            SET8(a1, UD_ANGLE, a);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        }
        return;                                               /* (still this frame) */
    }
    ud_sway(o, 5, 0);
}

static void ud_typeA(uint32_t o)                /* a row of seven, 48 px, a V of phases */
{
    static const int8_t tbl[7][2] = {
        { 0, 0 }, { (int8_t)0xE0, -32 }, { (int8_t)0xC0, -64 }, { (int8_t)0xA0, -96 },
        { (int8_t)0xE0, 32 }, { (int8_t)0xC0, 64 }, { (int8_t)0xA0, 96 },
    };
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UDA, 0x10, 0x10);
        SET16(o, UD_YBASE, OB16(o, OBJ_y_pos));
        if (!OB8(o, UD_INDEX)) {
            SET16(o, UD_X0, OB16(o, OBJ_x_pos));
            for (int d1 = 1; d1 <= 6; d1++) {
                uint32_t a1 = ud_new(o, 0x0A);
                if (!a1) continue;
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(a1, UD_X0, OB16(o, UD_X0));
                SET8(a1, UD_INDEX, d1);
                SET8(a1, UD_ANGLE, (uint8_t)tbl[d1][0]);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + tbl[d1][1]);
            }
        }
    }
    ud_sway(o, 5, 4);
}

static void ud_typeB(uint32_t o)                /* the lift: six blocks round a loop */
{
    static const int16_t tbl[6][3] = {                       /* x, y, y speed */
        { 0, 0, 0x80 }, { 0, -0x80, 0x80 }, { -0x50, 0x20, -0x100 },
        { -0x50, -0x60, -0x100 }, { 0x50, 0x20, -0x100 }, { 0x50, -0x60, -0x100 },
    };
    if (!OB8(o, OBJ_routine)) {
        ud_init(o, SCDA_TTZ_UD9, 0x10, 0x20);
        SET8(o, OBJ_frame, 0);
        if (!OB8(o, UD_INDEX)) {
            SET16(o, UD_X0, OB16(o, OBJ_x_pos));
            SET16(o, UD_YBASE, OB16(o, OBJ_y_pos));
            for (int d1 = 1; d1 <= 5; d1++) {
                uint32_t a1 = ud_new(o, 0x0B);
                if (!a1) continue;
                SET8(a1, UD_INDEX, d1);
                SET16(a1, UD_X0, OB16(o, UD_X0));
                SET16(a1, UD_YBASE, OB16(o, UD_YBASE));
            }
        }
        const int16_t *t = tbl[OB8(o, UD_INDEX) % 6];
        SET16(o, OBJ_x_pos, OB16(o, UD_X0) + (int8_t)t[0]);
        SET16(o, OBJ_y_pos, OB16(o, UD_YBASE) + (int8_t)t[1]);
        SET16(o, OBJ_y_vel, (uint16_t)t[2]);
        return;
    }
    cd_move(o, 0, (int32_t)OBS16(o, OBJ_y_vel) * 256);
    uint16_t base = OB16(o, UD_YBASE), y = OB16(o, OBJ_y_pos);
    if (OBS16(o, OBJ_y_vel) < 0) {                            /* (up $80 past its middle: to $80 under) */
        if (base >= y && (uint16_t)(base - y) >= 0x80) SET16(o, OBJ_y_pos, base + 0x80);
    } else if (y >= base && (uint16_t)(y - base) >= 0x80)
        SET16(o, OBJ_y_pos, base - 0x80);
    solid(o, OB16(o, OBJ_x_pos));
}

static void udblk(uint32_t o)
{
    uint8_t type = OB8(o, OBJ_subtype);
    switch (type) {
    case 0: case 1: ud_type1(o); break;
    case 3: ud_type3(o); break;
    case 4: ud_type4(o); break;
    case 5: ud_type5(o); break;
    case 6: ud_type6(o); return;                              /* (unseen) */
    case 7: ud_type7(o); break;
    case 8: ud_type8(o); break;
    case 9: ud_type9(o); break;
    case 0x0A: ud_typeA(o); break;
    case 0x0B: ud_typeB(o); break;
    default: return;                                          /* (2: nothing; none past B) */
    }
    if (type >= 8 && OB16(o, UD_PARENT)) {                    /* (6's blocks: they go with it) */
        uint32_t par = ram(OB16(o, UD_PARENT));
        if (type == 8 ? r32(par) != CD_CODE(0x23) : !r32(par)) { o_delete_respawnable(o); return; }
        o_draw(o);
        return;
    }
    if (cd_despawn(o, OB16(o, UD_X0))) return;
    o_draw(o);
}

/* ======================================================================
 * $24 left/right blocks
 * ==================================================================== */
#define LR_SPEED        0x30               /* oVar30: (type 5) its step's speed */
#define LR_XBASE        0x32               /* oVar32: the middle of its swing */
#define LR_X0           0x36               /* oVar36: the group's place (its despawn) */
#define LR_ANGLE        0x3A               /* oVar3A; (type 5) the step's frames */
#define LR_STEP         0x3B               /* oVar3B: (type 5) its step; (type 6) its swing's size */
#define LR_INDEX        0x3C               /* oVar3C */
#define LR_FLAG         0x3D               /* oVar3D */

static void lr_init(uint32_t o, int asset, int w, int h)
{
    SET8(o, OBJ_routine, 2);
    o_init(o, asset, OB8(o, OBJ_render) | 4, 3, w, h);
    SET8(o, OBJ_y_radius, h);
}

static uint32_t lr_new(uint32_t o, int type)
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_subtype, type);
    return a1;
}

static void lr_sway(uint32_t o)                 /* (types 1, 2) 32 px about its middle, a step a frame */
{
    uint16_t x0 = OB16(o, OBJ_x_pos);
    int16_t s, c;
    o_sincos(OB8(o, LR_ANGLE), &s, &c);
    SET8(o, LR_ANGLE, OB8(o, LR_ANGLE) + 1);
    SET16(o, OBJ_x_pos, OB16(o, LR_XBASE) + (s >> 3));
    SET16(o, OBJ_x_vel, (uint16_t)((OB16(o, OBJ_x_pos) - x0) << 8));
    solid(o, x0);
}

static void lr_type1(uint32_t o)                /* 0, 1, 7: a column of six, each one of a pair */
{
    if (!OB8(o, OBJ_routine)) {
        lr_init(o, SCDA_TTZ_LR1, 0x30, 0x10);
        SET16(o, LR_XBASE, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_frame, 1);
        if (!OB8(o, LR_INDEX)) {
            SET16(o, LR_X0, OB16(o, OBJ_x_pos));
            if (!OB8(o, LR_FLAG))
                for (int d1 = 1; d1 <= 5; d1++) {                 /* (the column, $60 apart) */
                    uint32_t a1 = lr_new(o, 1);
                    if (!a1) continue;
                    SET8(a1, LR_FLAG, d1);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET8(a1, LR_ANGLE, (uint8_t)((d1 << 5) + 0x10));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 * 0x60);
                }
            uint32_t a1 = lr_new(o, 1);                       /* (its pair, $E0 right) */
            if (a1) {
                SET8(a1, LR_INDEX, 1);
                SET16(a1, LR_X0, OB16(o, LR_X0));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0xE0);
                SET8(a1, LR_ANGLE, OB8(o, LR_ANGLE));
            }
        }
    }
    lr_sway(o);
}

static void lr_type2(uint32_t o)                /* a pair, 64 px apart, swaying apart and back */
{
    if (!OB8(o, OBJ_routine)) {
        lr_init(o, SCDA_TTZ_LR2, 0x20, 0x10);
        if (!OB8(o, LR_INDEX)) {
            SET16(o, LR_X0, OB16(o, OBJ_x_pos));
            SET16(o, LR_XBASE, OB16(o, OBJ_x_pos) - 0x20);
            uint32_t a1 = lr_new(o, 2);
            if (a1) {
                SET8(a1, LR_INDEX, 1);
                SET16(a1, LR_X0, OB16(o, LR_X0));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x40);
                SET16(a1, LR_XBASE, OB16(o, OBJ_x_pos) + 0x60);
                SET8(a1, LR_ANGLE, OB8(o, LR_ANGLE) - 0x80);
            }
        }
    }
    lr_sway(o);
}

static void lr_type3(uint32_t o)                /* two rows of three, sliding, wrapping 224 px */
{
    static const int16_t tbl[6][3] = {                       /* x, y, x speed */
        { 0, 0, -0x80 }, { -0x90, 0, -0x80 }, { 0x90, 0, -0x80 },
        { 0, 0x40, 0x80 }, { -0x90, 0x40, 0x80 }, { 0x90, 0x40, 0x80 },
    };
    if (!OB8(o, OBJ_routine)) {
        lr_init(o, SCDA_TTZ_LR3, 0x20, 0x20);
        if (!OB8(o, LR_INDEX)) {
            SET16(o, LR_X0, OB16(o, OBJ_x_pos));
            for (int d1 = 1; d1 <= 5; d1++) {
                uint32_t a1 = lr_new(o, 3);
                if (!a1) continue;
                SET8(a1, LR_INDEX, d1);
                SET16(a1, LR_X0, OB16(o, LR_X0));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            }
        }
        const int16_t *t = tbl[OB8(o, LR_INDEX) % 6];
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + t[0]);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + t[1]);
        SET16(o, OBJ_x_vel, (uint16_t)t[2]);
        return;
    }
    uint16_t x0 = OB16(o, OBJ_x_pos);
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, 0);
    uint16_t home = OB16(o, LR_X0), x = OB16(o, OBJ_x_pos);
    if (OBS16(o, OBJ_x_vel) < 0) {
        if (home >= x && (uint16_t)(home - x) >= 0xE0) SET16(o, OBJ_x_pos, home + 0xE0);
    } else if (x >= home && (uint16_t)(x - home) >= 0xE0)
        SET16(o, OBJ_x_pos, home - 0xE0);
    if (!(OB8(o, OBJ_render) & 0x80)) return;                 /* (seen, and player 1 within 64 px up or down) */
    uint16_t d = (uint16_t)(OB16(RAM_Player_1, OBJ_y_pos) - OB16(o, OBJ_y_pos));
    if (OB16(RAM_Player_1, OBJ_y_pos) < OB16(o, OBJ_y_pos)) d = (uint16_t)-d;
    if (d < 0x40) solid(o, x0);                               /* (a wrap jumps: no carry, CD goes by speed) */
}

static void lr_type4(uint32_t o)                /* a pair, $100 apart, running left, wrapping */
{
    if (!OB8(o, OBJ_routine)) {
        int top = OB8(o, LR_FLAG) != 0;                       /* (8's: seen, flat; else unseen, 128 px) */
        lr_init(o, SCDA_TTZ_LR1, 0x40, top ? 0x10 : 0x40);
        if (!OB8(o, LR_INDEX)) {
            SET16(o, LR_X0, OB16(o, OBJ_x_pos));
            uint32_t a1 = lr_new(o, 4);
            if (a1) {
                SET8(a1, LR_INDEX, 1);
                SET8(a1, LR_FLAG, OB8(o, LR_FLAG));
                SET16(a1, LR_X0, OB16(o, LR_X0));
                SET16(a1, OBJ_x_pos, OB16(o, LR_X0) + 0x80);
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(o, OBJ_x_pos, OB16(o, LR_X0) - 0x80);
            }
        }
        SET16(o, OBJ_x_vel, (uint16_t)-0x200);
        return;
    }
    uint16_t x0 = OB16(o, OBJ_x_pos);
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, 0);
    solid(o, x0);
    uint16_t home = OB16(o, LR_X0), x = OB16(o, OBJ_x_pos);
    if (home >= x && (uint16_t)(home - x) == 0x100) SET16(o, OBJ_x_pos, home + 0x100);
}

static void lr_type5(uint32_t o)                /* a column of five, stepping right and back */
{
    static const uint8_t tbl[7][2] = { { 0, 0 }, { 0x40, 1 }, { 0x80, 2 }, { 0xC0, 3 }, { 0x80, 4 }, { 0x40, 5 }, { 0, 0 } };
    static const int16_t steps[6] = { 0x80, 0x80, 0x80, -0x80, -0x80, -0x80 };   /* ($80 frames each) */
    if (!OB8(o, OBJ_routine)) {
        lr_init(o, SCDA_TTZ_LR5, 0x20, 0x20);
        SET16(o, LR_X0, OB16(o, OBJ_x_pos));
        if (!OB8(o, LR_INDEX))
            for (int d1 = 1; d1 <= 4; d1++) {
                uint32_t a1 = lr_new(o, 5);
                if (!a1) continue;
                SET8(a1, LR_INDEX, d1);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 * 0x40);
            }
        const uint8_t *t = tbl[OB8(o, LR_INDEX) % 7];
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + t[0]);
        SET8(o, LR_STEP, t[1]);
        return;
    }
    uint16_t x0 = OB16(o, OBJ_x_pos);
    for (;;) {
        if (!OB8(o, LR_ANGLE)) {                              /* (the next step: no move this frame) */
            SET8(o, LR_ANGLE, 0x80);
            SET16(o, LR_SPEED, (uint16_t)steps[OB8(o, LR_STEP) % 6]);
            break;
        }
        SET16(o, OBJ_x_vel, OB16(o, LR_SPEED));
        cd_move(o, (int32_t)OBS16(o, LR_SPEED) * 256, 0);
        SET8(o, LR_ANGLE, OB8(o, LR_ANGLE) - 1);
        if (OB8(o, LR_ANGLE)) break;
        SET8(o, LR_STEP, OB8(o, LR_STEP) + 1 == 6 ? 0 : OB8(o, LR_STEP) + 1);
    }
    solid(o, x0);
}

static void lr_type6(uint32_t o)                /* stairs: four, 16-64 px wide, each swinging its width */
{
    if (!OB8(o, OBJ_routine)) {
        lr_init(o, SCDA_TTZ_LR6, 0, 0x10);
        if (!OB8(o, LR_INDEX)) {
            SET16(o, LR_X0, OB16(o, OBJ_x_pos));
            for (int d1 = 1; d1 <= 3; d1++) {
                uint32_t a1 = lr_new(o, 6);
                if (!a1) continue;
                SET8(a1, LR_INDEX, d1);
                SET16(a1, LR_X0, OB16(o, LR_X0));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) - (int8_t)(d1 << 4));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 * 0x20);
            }
        }
        uint8_t k = OB8(o, LR_INDEX);
        SET8(o, OBJ_frame, k);
        SET8(o, LR_STEP, k + 1);
        uint8_t w = (uint8_t)((k + 1) * 16);
        SET8(o, OBJ_width_px, w);
        SET16(o, LR_XBASE, OB16(o, OBJ_x_pos) + w);
        SET8(o, LR_ANGLE, 0x80);
    }
    uint16_t x0 = OB16(o, OBJ_x_pos);
    int16_t s, c;
    o_sincos(OB8(o, LR_ANGLE), &s, &c);
    int16_t d3 = 0;
    for (unsigned n = OB8(o, LR_STEP); n; n--) d3 = (int16_t)(d3 + s);
    SET16(o, OBJ_x_pos, OB16(o, LR_XBASE) + (d3 >> 4));
    SET16(o, OBJ_x_vel, (uint16_t)((OB16(o, OBJ_x_pos) - x0) << 8));
    SET8(o, LR_ANGLE, OB8(o, LR_ANGLE) + 1);
    solid(o, x0);
}

static void lr_type8(uint32_t o)                /* a pair running right, a pair of 4s $60 above */
{
    if (!OB8(o, OBJ_routine)) {
        lr_init(o, SCDA_TTZ_LR1, 0x40, 0x10);
        uint32_t a1;
        if (!OB8(o, LR_INDEX)) {
            SET16(o, LR_X0, OB16(o, OBJ_x_pos));
            if ((a1 = lr_new(o, 4))) {
                SET8(a1, LR_FLAG, 8);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x60);
            }
            if ((a1 = lr_new(o, 8))) {
                SET8(a1, LR_INDEX, 1);
                SET16(a1, LR_X0, OB16(o, LR_X0));
                SET16(a1, OBJ_x_pos, OB16(o, LR_X0) - 0x80);
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(o, OBJ_x_pos, OB16(o, LR_X0) + 0x80);
            }
        }
        SET16(o, OBJ_x_vel, 0x200);
        return;
    }
    uint16_t x0 = OB16(o, OBJ_x_pos);
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, 0);
    solid(o, x0);
    uint16_t home = OB16(o, LR_X0), x = OB16(o, OBJ_x_pos);
    if (x >= home && (uint16_t)(x - home) == 0x100) SET16(o, OBJ_x_pos, home - 0x100);
}

static void lrblk(uint32_t o)
{
    switch (OB8(o, OBJ_subtype)) {
    case 0: case 1: case 7: lr_type1(o); break;
    case 2: lr_type2(o); break;
    case 3: lr_type3(o); break;
    case 4: lr_type4(o); break;
    case 5: lr_type5(o); break;
    case 6: lr_type6(o); break;
    case 8: lr_type8(o); break;
    default: return;
    }
    if (o_out_of_range(o, OB16(o, LR_X0))) {                  /* (its group's place, seen or not) */
        o_delete_respawnable(o);
        return;
    }
    if (OB8(o, OBJ_subtype) == 4 && !OB8(o, LR_FLAG)) return;  /* (its frame is empty) */
    o_draw(o);
}

/* ======================================================================
 * $3D turnstile
 * ==================================================================== */
#define RB_ARM1         0x30               /* oVar30: (the L) its top arm */
#define RB_ARM2         0x34               /* oVar34: (the L) its other arm; (an arm) the L */
#define RB_X0           0x36               /* oVar36: (an arm) the L's place */
#define RB_TIME         0x3A               /* oVar3A */
#define RB_STEP         0x3B               /* oVar3B */
#define RB_ARM          0x3C               /* oVar3C: $FF an arm */
#define RB_TURN         0x3D               /* oVar3D: (the L) its way (0-3); (an arm) 0 the top one, 1 the other */
#define RB_ON           0x3E               /* oVar3E: (top arm) stood on; (other) pushed */
#define RB_LATCH        0x3F               /* oVar3F */

/* ObjGetFloorDist from (x, y + dy) */
static int16_t floor_below(uint32_t o, int dy)
{
    uint8_t yr = OB8(o, OBJ_y_radius);
    SET8(o, OBJ_y_radius, dy);
    int16_t d1 = o_floor_dist(o);
    SET8(o, OBJ_y_radius, yr);
    return d1;
}

static uint32_t rb_new_arm(uint32_t o, int which)
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET16(a1, RB_X0, OB16(o, OBJ_x_pos));
    SET16(a1, RB_ARM2, (uint16_t)o);
    SET8(a1, RB_ARM, 0xFF);
    SET8(a1, RB_TURN, which);
    return a1;
}

static void rb_arm(uint32_t o)                  /* an arm: solid where the L's frame puts it */
{
    static const int8_t top[4][2] = { { -16, 0 }, { -16, 0 }, { 16, 0 }, { 16, 0 } };
    static const int8_t side[4][2] = { { 0, -32 }, { 0, 32 }, { 0, 32 }, { 0, -32 } };
    uint32_t p = RAM_Player_1;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_RBLK, OB8(o, OBJ_render) | 4, 3, OB8(o, RB_TURN) ? 0x10 : 0x20, 0x10);
        SET8(o, OBJ_frame, 8);
        SET8(o, OBJ_y_radius, 0x10);
    }
    uint32_t par = ram(OB16(o, RB_ARM2));
    uint8_t f = OB8(par, OBJ_frame);
    if (f >= 4)                                               /* (turning: nothing to stand on) */
        cd_get_off(o);
    else {
        const int8_t *t = (OB8(o, RB_TURN) ? side : top)[f];
        SET16(o, OBJ_x_pos, OB16(par, OBJ_x_pos) + t[0]);
        SET16(o, OBJ_y_pos, OB16(par, OBJ_y_pos) + t[1]);
        int16_t xv = OBS16(p, OBJ_x_vel);
        int left = OBS16(p, OBJ_x_pos) < OBS16(o, OBJ_x_pos);
        solid(o, OB16(o, OBJ_x_pos));
        if (!OB8(o, RB_TURN))
            SET8(o, RB_ON, OB8(o, OBJ_status) & ST_P1_STAND ? 0xFF : 0);
        else {
            /* CD's push bit: on the ground, moving into it (S3K's is set on
             * any touch; CD's code takes it once a push, and clears it) */
            int pushed = (OB8(o, OBJ_status) & ST_P1_PUSH) && xv && (left ? xv > 0 : xv < 0);
            if (!pushed) SET16(o, RB_ON, 0);                  /* (and the latch) */
            else if (!OB8(o, RB_LATCH)) SET16(o, RB_ON, 0xFFFF);
        }
    }
    if (cd_despawn(o, OB16(o, RB_X0))) return;
    o_draw(o);
}

static void rblk(uint32_t o)
{
    static const uint8_t turns[16] = { 0, 4, 1, 0xFF, 1, 5, 2, 0xFF, 2, 5, 1, 0xFF, 3, 6, 2, 0xFF };
    static const uint8_t pushes[16] = { 0, 7, 3, 0xFF, 1, 4, 2, 0xFF, 2, 4, 1, 0xFF, 3, 7, 0, 0xFF };
    if (OB8(o, RB_ARM) == 0xFF) { rb_arm(o); return; }
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_RBLK, OB8(o, OBJ_render) | 4, 3, 0x40, 0x40);
        SET8(o, OBJ_y_radius, 0x40);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype) & 3);
        SET8(o, RB_TURN, OB8(o, OBJ_frame));
        uint32_t a1 = rb_new_arm(o, 0);
        if (!a1) { o_delete_respawnable(o); return; }
        SET16(o, RB_ARM1, (uint16_t)a1);
        if (!(a1 = rb_new_arm(o, 1))) { o_delete_respawnable(o); return; }
        SET16(o, RB_ARM2, (uint16_t)a1);
        break;
    }
    case 2: {                                                 /* waiting: stood on its top arm, past the pivot */
        if (!(OB8(ram(OB16(o, RB_ARM1)), RB_ON) & 0x80)) break;
        int w = (int8_t)(OB8(p, OBJ_width_px) + 2);
        uint16_t d0, d1;
        if (OB8(o, RB_TURN) < 2) { d0 = OB16(o, OBJ_x_pos); d1 = (uint16_t)(OB16(p, OBJ_x_pos) + w); }
        else { d1 = OB16(o, OBJ_x_pos); d0 = (uint16_t)(OB16(p, OBJ_x_pos) + (int8_t)-w); }
        if (d0 < d1) break;
        if ((int8_t)floor_below(o, 0x10) >= 0x10) {           /* (nothing under it: it tips) */
            SET8(o, OBJ_routine, 4);
            SET8(o, RB_TIME, 0x10);
            SET8(o, RB_STEP, 0);
            break;
        }
        if (OB8(ram(OB16(o, RB_ARM2)), RB_ON)) {              /* (pushed: it turns, carrying him) */
            SET8(o, OBJ_routine, 6);
            SET8(o, RB_TIME, 0x10);
            SET8(o, RB_STEP, 0);
        }
        break;
    }
    case 4: {                                                 /* tipping */
        uint8_t f = turns[(OB8(o, RB_TURN) * 4 + OB8(o, RB_STEP)) & 15];
        if (f & 0x80) {
            SET8(o, RB_TURN, OB8(o, OBJ_frame));
            SET8(o, OBJ_routine, 2);
            break;
        }
        SET8(o, OBJ_frame, f);
        SET8(o, RB_TIME, OB8(o, RB_TIME) - 1);
        if (OB8(o, RB_TIME)) break;
        o_sfx_cd(FM_TURN);
        SET8(o, RB_TIME, 4);
        SET8(o, RB_STEP, OB8(o, RB_STEP) + 1);
        break;
    }
    default: {                                                /* 6: turning, player 1 on its top */
        uint8_t f = pushes[(OB8(o, RB_TURN) * 4 + OB8(o, RB_STEP)) & 15];
        if (f & 0x80) {
            SET8(o, RB_TURN, OB8(o, OBJ_frame));
            SET8(o, OBJ_routine, 2);
            break;
        }
        SET8(o, OBJ_frame, f);
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - (int8_t)OB8(o, OBJ_y_radius) - (int8_t)OB8(p, OBJ_y_radius));
        SET8(o, RB_TIME, OB8(o, RB_TIME) - 1);
        if (OB8(o, RB_TIME)) break;
        SET8(o, RB_TIME, 8);
        SET8(o, RB_STEP, OB8(o, RB_STEP) + 1);
        if (OB8(o, RB_STEP) == 1)
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        else if (OB8(o, RB_STEP) == 2)
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) + (OB8(o, RB_TURN) & 2 ? -16 : 16));
        break;
    }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ======================================================================
 * $3E linked platforms
 * ==================================================================== */
#define RK_SPEED        0x30               /* oVar30: its turn a frame (8.8) */
#define RK_Y0           0x32               /* oVar32: the centre */
#define RK_X0           0x36               /* oVar36 */
#define RK_INDEX        0x3C               /* oVar3C: 0-2 platforms, 3-8 links */
#define RK_HELD         0x3D               /* oVar3D: the switch seen held */
#define RK_ANGLE        0x3E               /* oVar3E (8.8) */

static void rk_switch(uint32_t o)               /* (swchk) its switch pressed: the other way */
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub & 0x80) return;
    if (!(scd_ttz_switch[sub & 0x0F] & 0x80)) { SET8(o, RK_HELD, 0); return; }
    if (OB8(o, RK_HELD)) return;
    SET16(o, RK_SPEED, (uint16_t)-OBS16(o, RK_SPEED));
    SET8(o, RK_HELD, 1);
}

/* (muls #6, asr.w #4, swap: the low word keeps the product's sign) */
static uint32_t rk_arc(int16_t v, uint16_t base)
{
    int32_t prod = (int32_t)v * 6;
    uint16_t hi = (uint16_t)(base + (int16_t)((int16_t)prod >> 4));
    return ((uint32_t)hi << 16) | ((uint32_t)prod >> 16 & 0xFFFF);
}

static void renketu(uint32_t o)
{
    static const int8_t tbl[9][2] = {                        /* its place on the arc, its frame */
        { 0x20, 0 }, { 0, 0 }, { (int8_t)0xE0, 0 }, { 0x18, 1 }, { 0x10, 1 },
        { 8, 1 }, { (int8_t)0xF8, 1 }, { (int8_t)0xF0, 1 }, { (int8_t)0xE8, 1 },
    };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        if (!OB8(o, RK_INDEX)) {
            SET16(o, RK_X0, OB16(o, OBJ_x_pos));
            SET16(o, RK_Y0, OB16(o, OBJ_y_pos));
            for (int d1 = 1; d1 <= 8; d1++) {
                uint32_t a1 = o_alloc();
                if (!a1) continue;
                SET32(a1, OBJ_code, r32(o + OBJ_code));
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
                SET8(a1, RK_INDEX, d1);
                SET16(a1, RK_X0, OB16(o, RK_X0));
                SET16(a1, RK_Y0, OB16(o, RK_Y0));
            }
        }
        const int8_t *t = tbl[OB8(o, RK_INDEX) % 9];
        /* (act 3's tile is $4400: none placed there) */
        o_init(o, t[1] ? SCDA_TTZ_RENKETU2 : SCDA_TTZ_RENKETU, OB8(o, OBJ_render) | 4, t[1] ? 3 : 1, 0x20, 8);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, RK_ANGLE, (uint8_t)t[0]);
        SET8(o, OBJ_frame, t[1]);
        SET16(o, RK_SPEED, OB8(o, OBJ_subtype) & 0x10 ? (uint16_t)-0x100 : 0x100);
    }
    rk_switch(o);
    SET16(o, RK_ANGLE, OB16(o, RK_ANGLE) + OB16(o, RK_SPEED));
    int16_t s, c;
    o_sincos(OB8(o, RK_ANGLE), &s, &c);
    uint32_t y = rk_arc(s, OB16(o, RK_Y0)), x = rk_arc(c, OB16(o, RK_X0));
    uint32_t y0 = r32(o + OBJ_y_pos), x0 = r32(o + OBJ_x_pos);
    SET32(o, OBJ_y_pos, y);
    SET32(o, OBJ_x_pos, x);
    SET16(o, OBJ_y_vel, (uint16_t)((y - y0) << 8 >> 16));
    SET16(o, OBJ_x_vel, (uint16_t)((x - x0) << 8 >> 16));
    if (!OB8(o, OBJ_frame))                                   /* (a platform: CD sets the rider 2 px in) */
        o_platform(o, 0x20 + 10, 8 - 2, (uint16_t)(x0 >> 16));
    if (o_out_of_range(o, OB16(o, RK_X0))) { o_delete(o); return; }   /* (CD: not seen or not, no respawn) */
    o_draw(o);
}

void scd_ttz_blk2_register(TriObjFn *t, int base);            /* scd_ttz_blk2.c */

void scd_ttz_blk_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x23] = udblk;
    t[base + 0x24] = lrblk;
    t[base + 0x3D] = rblk;
    t[base + 0x3E] = renketu;
    scd_ttz_blk2_register(t, base);
}
