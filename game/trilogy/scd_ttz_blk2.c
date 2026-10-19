/*
 * scd_ttz_blk2.c — Tidal Tempest's platforms, lifts and walls (Sonic CD;
 * decomp: R4\ESCAL4.C, BRUNKO4.C, FUNSUI4.C, UKIDAI.C, BANEIWA.C, DAID4.C,
 * WALL1.C, WALL42.C, WALL4.C, KUZURE4.C, TOBIRAS4.C; the disc's code).
 *
 *   $26 escalator      a block that, once player 1 lands on it (or hits it
 *                      from below) while it is seen, runs up and right 160 px
 *                      (1 px a frame; futures 2 px, 80 frames)
 *   $29 rotating swing a pivot, five links and a seat 96 px out, all turning
 *                      (subtype bit 4: $100 a frame one way, else $80 the
 *                      other); the seat carries a button (sw4, $27: subtype
 *                      & $F its switch)
 *   $2A fountain       a spout platform 48 px over the water's surface (the
 *                      water level as it moves), spraying a drop every 16
 *                      frames; it sets three colours of line 3
 *   $44 raft           floats on the water while the water rests at $780 or
 *                      $5E0 (else it goes); stood on at $780, it sends the
 *                      water up to $5E0 (scd_ttz_water_target); then it
 *                      follows the player along the surface to a wall, and
 *                      keeps him at its middle
 *   $48 bobbing rocks  a pair, 32 px apart, bobbing ~128 px out of step
 *   $49 shaft lift     unseen until player 1 is 224 px below where it is;
 *                      then it drops in above him (to 1 px a frame); landed
 *                      on, it carries him up at 2 px a frame to where it was
 *   $34 block          solid, seven sizes (subtype)
 *   $45 sliding wall   once (the respawn flag's bit 0 keeps it), a second
 *                      after it comes, slides 192 px right at 1/2 px a frame
 *   $47 chase gate     (act 3) player 1 160-191 px right of it and 0-127 px
 *                      below its middle: it drops 64 px (subtype 1: 128 px,
 *                      when something sets its oVar3F); once, by bit 0
 *   $39 crumbling ledge  8 px a column wide (subtype); stood on, eight frames
 *                      later it breaks a column of two pieces off the end
 *                      the player was running from every eight frames
 *   $25 shutter        a 128 px door, solid; once its switch (subtype) has
 *                      ever been pressed it splits into four strips that go
 *                      up and down alternately 128 px, and is gone
 * Their code is the same in every period of acts 1 and 2 (and act 3's $34).
 * CD's object variables sit at S3K's $30.. (a #define each names CD's),
 * routines keep CD's even numbers. SolidObject -> o_solid, TopSolidObject ->
 * o_platform, GetOffObject -> cd_get_off; objPlayerSlot -> Player 1 (the
 * solids take Player 2 as S3K's do). CD carries a rider by the object's
 * speed, S3K's by where it was (d4): a mover moves, then is solid from there.
 */
#include "trilogy_obj.h"

/* (Sonic CD assets, scd_level.py CD_ASSETS -> trilogy_obj.h) */

/* switchflag[] (bit 7 held, 6 ever, 5 toggled): the round's switches own it */
extern uint8_t scd_ttz_switch[32];
/* the round's water: Sonic CD's raft writes waterMoveTarget ($F64A, the
 * height the water rises or sinks to at its speed) */
extern void scd_ttz_water_target(int y);

#define ST_AIR          0x02
#define FM_CLICK        0xBF               /* (daid4's a_off1: the lift landed on) */
#define FM_COLLAPSE     0xA3               /* (kuzure4's main_check: the ledge stood on) */
#define PAL(off)        (RAM_Normal_palette + (off))   /* (CD's palette: lines 2 and 3 are S3K's own) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static uint32_t ram(uint16_t w) { return 0xFF0000u | w; }

static void solid(uint32_t o, uint16_t x0)      /* SolidObject from where it was */
{
    o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, x0);
}

/* SolidObject's d0 = 1 (player n on top, or hit from below) */
static int touched(uint32_t o, int n)
{
    return (OB8(o, OBJ_status) & (n ? ST_P2_STAND : ST_P1_STAND)) || ((g_o_d6 >> 16) & (n ? 8 : 4));
}

static int time_zone(void)                      /* timeZone: 0 past, 1 present, 2 either future */
{
    int p = cd_period();
    return p == TRI_CD_PAST ? 0 : p == TRI_CD_PRESENT ? 1 : 2;
}

/* the object's own entry of the respawn table: Sonic CD keeps a "done" in
 * bit 0 of it (as S3K's objects do theirs); 0: none */
static uint32_t flag_byte(uint32_t o) { return OB16(o, OBJ_respawn) ? ram(OB16(o, OBJ_respawn)) : 0; }

/* ---- $26 escalator ------------------------------------------------------- */
#define ES_TIME         0x3E               /* oVar3E */
static void escal(uint32_t o)
{
    static const int16_t tbl[3][3] = { { 0x100, -0x100, 160 }, { 0x100, -0x100, 160 }, { 0x200, -0x200, 80 } };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_ESCAL, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        break;
    case 2: {
        if (!(OB8(o, OBJ_render) & 0x80)) break;
        solid(o, OB16(o, OBJ_x_pos));
        if (!touched(o, 0)) break;
        SET8(o, OBJ_routine, 4);
        const int16_t *t = tbl[time_zone()];
        SET16(o, OBJ_x_vel, (uint16_t)t[0]);
        SET16(o, OBJ_y_vel, (uint16_t)t[1]);
        SET16(o, ES_TIME, (uint16_t)t[2]);
        break;
    }
    default: {                                                /* 4: up */
        uint16_t x0 = OB16(o, OBJ_x_pos);
        if (OB16(o, ES_TIME)) {
            cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, (int32_t)OBS16(o, OBJ_y_vel) * 256);
            SET16(o, ES_TIME, OB16(o, ES_TIME) - 1);
            if (!OB16(o, ES_TIME)) { SET16(o, OBJ_x_vel, 0); SET16(o, OBJ_y_vel, 0); }
        }
        solid(o, x0);
        break;
    }
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }   /* (seen or not) */
    o_draw(o);
}

/* ---- $29 rotating swing --------------------------------------------------- */
#define BR_Y0           0x32               /* oVar32: the pivot */
#define BR_X0           0x36               /* oVar36 */
#define BR_ANGLE        0x3A               /* oVar3A (8.8) */
#define BR_LINK         0x3C               /* oVar3C: 0 the pivot, 1-5 links, 6 the seat */
#define SW_PARENT       0x34               /* (sw4 $27's oVar34: what it rides; it reads that one's $36) */
#define SW_DX           0x38               /* (oVar38) */
#define SW_DY           0x39               /* (oVar39) */
static void brunko(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        int seat = OB8(o, OBJ_frame) == 1;                    /* (its tile: line 2; the rest line 0) */
        o_init(o, seat ? SCDA_TTZ_BRUNKO2 : SCDA_TTZ_BRUNKO, OB8(o, OBJ_render) | 4, 4, 0x20, 0x0A);
        SET8(o, OBJ_y_radius, 0x0A);
        SET8(o, BR_ANGLE, 0xC0);                              /* (up) */
        if (OB8(o, BR_LINK) == 6) {
            uint32_t a1 = o_alloc_after(o);                   /* (FindNextObjSlot: it moves after the seat) */
            if (a1) {
                SET32(a1, OBJ_code, CD_CODE(0x27));
                SET16(a1, SW_PARENT, (uint16_t)o);
                SET8(a1, SW_DY, 0xF0);
                SET8(a1, SW_DX, 0x10);
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
            }
        }
        if (!OB8(o, BR_LINK)) {
            SET16(o, BR_X0, OB16(o, OBJ_x_pos));
            SET16(o, BR_Y0, OB16(o, OBJ_y_pos));
            for (int d1 = 6; d1 >= 1; d1--) {
                uint32_t a1 = o_alloc();
                if (!a1) continue;
                SET32(a1, OBJ_code, r32(o + OBJ_code));
                SET16(a1, BR_X0, OB16(o, BR_X0));
                SET16(a1, BR_Y0, OB16(o, BR_Y0));
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
                SET8(a1, BR_LINK, d1);
                SET8(a1, OBJ_frame, d1 == 6 ? 1 : 2);
            }
        }
    }
    uint32_t x0 = r32(o + OBJ_x_pos), y0 = r32(o + OBJ_y_pos);
    SET16(o, BR_ANGLE, OB16(o, BR_ANGLE) + (OB8(o, OBJ_subtype) & 0x10 ? 0x100 : -0x80));
    int16_t s, c;
    o_sincos(OB8(o, BR_ANGLE), &s, &c);
    int16_t d3 = 0, d4 = 0;
    for (unsigned n = OB8(o, BR_LINK); n; n--) { d3 = (int16_t)(d3 + s); d4 = (int16_t)(d4 + c); }
    SET16(o, OBJ_y_pos, OB16(o, BR_Y0) + (d3 >> 4));
    SET16(o, OBJ_x_pos, OB16(o, BR_X0) + (d4 >> 4));
    if (OB8(o, BR_LINK) == 6) {                               /* (the seat: CD sets the rider 2 px in) */
        SET16(o, OBJ_x_vel, (uint16_t)((int32_t)(r32(o + OBJ_x_pos) - x0) >> 8));
        SET16(o, OBJ_y_vel, (uint16_t)((int32_t)(r32(o + OBJ_y_pos) - y0) >> 8));
        o_platform(o, 0x20 + 10, 0x0A - 2, (uint16_t)(x0 >> 16));
    }
    if (o_out_of_range(o, OB16(o, BR_X0))) { o_delete(o); return; }  /* (seen or not) */
    o_draw(o);
}

/* ---- $2A fountain ---------------------------------------------------------- */
#define FS_STEP         0x3A               /* oVar3A: (the spout) its next drop's way */
#define FS_TIME         0x3B               /* oVar3B */
static void funsui_drop(uint32_t o)             /* (every 16 frames) */
{
    static const int16_t tbl[4][4] = {                       /* x, y, x speed, y speed */
        { -40, -72, -32, -128 }, { 8, -72, 32, -128 }, { 40, -72, 64, -128 }, { -8, -72, -64, -128 },
    };
    int carry = OB8(o, FS_TIME) + 0x10 > 0xFF;
    SET8(o, FS_TIME, OB8(o, FS_TIME) + 0x10);
    if (!carry) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_subtype, 0xFF);
    SET8(a1, OBJ_frame, 5);
    unsigned k = OB8(o, FS_STEP) & 3;
    SET8(a1, FS_STEP, k);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + tbl[k][0]);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + tbl[k][1]);
    SET16(a1, OBJ_x_vel, (uint16_t)tbl[k][2]);
    SET16(a1, OBJ_y_vel, (uint16_t)tbl[k][3]);
    SET8(o, FS_STEP, OB8(o, FS_STEP) + 1);
}

static void funsui(uint32_t o)
{
    uint32_t anim = o_asset_anim(SCDA_TTZ_FUNSUI);
    if (OB8(o, OBJ_subtype) == 0xFF) {                        /* a drop: 128 frames */
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            o_init(o, SCDA_TTZ_FUNSUI, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
            SET8(o, OBJ_y_radius, 0x10);
            SET8(o, OBJ_anim, 1);
        }
        int carry = OB8(o, FS_TIME) + 2 > 0xFF;
        SET8(o, FS_TIME, OB8(o, FS_TIME) + 2);
        if (carry) { o_delete(o); return; }
        cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) * 256, (int32_t)OBS16(o, OBJ_y_vel) * 256);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 8);
        if (anim) o_animate(o, anim);
        if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
        o_draw(o);
        return;
    }
    if (!OB8(o, OBJ_routine)) {
        w16(PAL(0x6E), 0x646);                                /* (line 3, colours 7-9: its water) */
        w16(PAL(0x70), 0xA8A);
        w16(PAL(0x72), 0xEEE);
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_FUNSUI, OB8(o, OBJ_render) | 4, 1, 0x20, 0x3C);
        SET8(o, OBJ_y_radius, 0x3C);
    }
    funsui_drop(o);
    if (anim) o_animate(o, anim);
    SET16(o, OBJ_y_pos, r16(RAM_Water_level) - 0x30);         /* (CD's waterposi, the surface) */
    o_platform(o, 0x20 + 10, 0x3C, OB16(o, OBJ_x_pos));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $44 raft ---------------------------------------------------------------- */
#define UK_X0           0x36               /* oVar36 */
static void ukidai(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_UKIDAI, OB8(o, OBJ_render) | 4, 3, 0x10, 8);
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, UK_X0, OB16(o, OBJ_x_pos));
        if (r16(RAM_Target_water_level) != 0x780 && r16(RAM_Target_water_level) != 0x5E0) { o_delete(o); return; }
        /* fall through */
    case 2: {                                                 /* the water moving: on it, wait */
        SET16(o, OBJ_y_pos, r16(RAM_Water_level));
        uint16_t d0 = r16(RAM_Mean_water_level);
        if (d0 != r16(RAM_Target_water_level)) break;
        if (d0 == 0x780) { SET8(o, OBJ_routine, 4); goto wait; }
        if (d0 == 0x5E0) { SET8(o, OBJ_routine, 6); goto move; }
        o_delete(o);
        return;
    }
    case 4:
    wait:                                                     /* low: stood on, the water rises */
        SET16(o, OBJ_y_pos, r16(RAM_Water_level));
        o_platform(o, 0x10 + 10, 8, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_P1_STAND) {
            SET8(o, OBJ_routine, 6);
            SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
            scd_ttz_water_target(0x5E0);
        }
        break;
    case 6:
    move:
        SET8(o, OBJ_routine, 8);
        SET16(o, OBJ_y_vel, (uint16_t)-(int16_t)r16(RAM_Water_speed));   /* (CD's carry: the water's speed word, $F64C) */
        /* fall through */
    default: {                                                /* 8: high: after player 1 (where he will be), to a wall */
        SET16(o, OBJ_y_pos, r16(RAM_Water_level));
        int16_t d4 = (int16_t)((int8_t)OB8(p, OBJ_x_vel) + OBS16(p, OBJ_x_pos) - OBS16(o, OBJ_x_pos));
        if (d4) {
            int dir = d4 < 0 ? -1 : 1, edge = d4 < 0 ? -(int8_t)OB8(o, OBJ_x_radius) : (int8_t)OB8(o, OBJ_x_radius);
            int16_t n = (int16_t)(d4 < 0 ? -d4 : d4);
            uint16_t y = OB16(o, OBJ_y_pos);
            SET16(o, OBJ_y_pos, y + 7);                       /* (its edge, 7 px down) */
            for (;;) {
                int16_t d1 = dir > 0 ? o_wall_right(o, edge) : o_wall_left(o, edge);
                if (d1 < 0) break;
                if (--n <= 0) break;
                SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + dir);
            }
            SET16(o, OBJ_y_pos, y);
        }
        o_platform(o, 0x10 + 10, 8, OB16(o, OBJ_x_pos));      /* (no carry: CD's x speed is 0) */
        if (OB8(o, OBJ_status) & ST_P1_STAND) SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        break;
    }
    }
    if (cd_despawn(o, OB16(o, UK_X0))) return;
    o_draw(o);
}

/* ---- $48 bobbing rocks ------------------------------------------------------- */
#define BI_PARENT       0x34               /* oVar34: (the second) the first */
#define BI_COUNT        0x3A               /* oVar3A */
#define BI_STEP         0x3B               /* oVar3B */
#define BI_PAIR         0x3C               /* oVar3C: 1 the second */
#define BI_ACCEL        0x3E               /* oVar3E */
static void baneiwa(uint32_t o)
{
    static const int16_t steps[4][3] = { { 0x80, -4, 0x200 }, { 0x80, -4, 0 }, { 0x80, 4, -0x200 }, { 0x80, 4, 0 } };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_BANEIWA, OB8(o, OBJ_render) | 4, 3, 0x10, 0x20);
        SET8(o, OBJ_y_radius, 0x20);
        uint32_t a1;
        if (!OB8(o, BI_PAIR) && (a1 = o_alloc())) {
            SET32(a1, OBJ_code, r32(o + OBJ_code));
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x20);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, BI_PAIR, 1);
            SET16(a1, BI_PARENT, (uint16_t)o);
        }
    } else {
        if (!OB8(o, BI_COUNT)) {                              /* (the next step: still this frame) */
            const int16_t *t = steps[OB8(o, BI_STEP) & 3];
            int16_t a = t[1], v = t[2];
            if (OB8(o, BI_PAIR)) { a = (int16_t)-a; v = (int16_t)-v; }
            SET8(o, BI_COUNT, t[0]);
            SET16(o, BI_ACCEL, (uint16_t)a);
            SET16(o, OBJ_y_vel, (uint16_t)v);
        } else {
            cd_move(o, 0, (int32_t)OBS16(o, OBJ_y_vel) * 256);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + OB16(o, BI_ACCEL));
            SET8(o, BI_COUNT, OB8(o, BI_COUNT) - 1);
            if (!OB8(o, BI_COUNT)) SET8(o, BI_STEP, OB8(o, BI_STEP) + 1 == 4 ? 0 : OB8(o, BI_STEP) + 1);
        }
        solid(o, OB16(o, OBJ_x_pos));
    }
    if (OB8(o, BI_PAIR)) {                                    /* (the second goes with the first) */
        if (r32(ram(OB16(o, BI_PARENT))) != r32(o + OBJ_code)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $49 shaft lift ------------------------------------------------------------ */
#define DA_Y0           0x30               /* oVar2A: where it is placed: the top */
#define DA_VY           0x32               /* oVar2C (long) */
static void daid(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_DAID, OB8(o, OBJ_render) | 4, 3, 0x20, 0x21);
        SET8(o, OBJ_x_radius, 0x20);
        SET16(o, DA_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_y_radius, 0x21);
        return;
    case 2: {                                                 /* unseen: till player 1 is far below */
        int16_t d0 = (int16_t)(OB16(p, OBJ_y_pos) - 0xE0);
        if (d0 > OBS16(o, DA_Y0)) {
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_y_pos, (uint16_t)d0);
        }
        cd_despawn(o, OB16(o, OBJ_x_pos));
        return;
    }
    case 4:
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_y_radius, 0x21);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) & ~1);
        SET32(o, DA_VY, 0);
        SET16(o, OBJ_y_vel, 0);
        /* fall through */
    case 6: {                                                 /* dropping in */
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + DA_VY));
        int32_t v = (int32_t)r32(o + DA_VY) + 0x400;
        SET32(o, DA_VY, (uint32_t)(v > 0x10000 ? 0x10000 : v));
        if (!(OB8(o, OBJ_render) & 0x80)) {
            int16_t d = (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos));
            if (d < 0) d = (int16_t)-d;
            if (d > 0xE0) { SET8(o, OBJ_routine, 2); break; } /* (he got away: back) */
        }
        o_platform(o, 0x20 + 10, 0x21, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_P1_STAND) {               /* (landed: pressed down, it goes up) */
            SET8(o, OBJ_routine, 8);
            cd_get_off(o);
            SET8(o, OBJ_y_radius, 0x1F);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 2);
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 4);
            o_sfx_cd(FM_CLICK);
        }
        break;
    }
    case 8:
        SET8(o, OBJ_routine, 10);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) | 1);
        SET8(o, OBJ_y_radius, 0x1F);
        SET16(o, OBJ_y_vel, (uint16_t)-0x200);
        /* fall through */
    default: {                                                /* 10: up 2 px a frame, with him */
        /* (CD: solid, then the move; its carry, the y speed, puts him on
         * the new top: here the move comes first) */
        int home = OBS16(o, OBJ_y_pos) <= OBS16(o, DA_Y0);
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) - 0x20000);
        if (home)
            SET8(o, OBJ_routine, 2);                          /* (at the top: gone, as CD; he is not let go) */
        else {
            o_platform(o, 0x20 + 10, 0x1F, OB16(o, OBJ_x_pos));
            if (!(OB8(o, OBJ_status) & ST_P1_STAND)) {
                SET8(o, OBJ_routine, 4);
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 2);
                SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 4);
            }
        }
        break;
    }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $34 block ---------------------------------------------------------------- */
static void wall1(uint32_t o)
{
    static const uint8_t sizes[7][2] = { { 16, 16 }, { 16, 16 }, { 32, 16 }, { 16, 32 }, { 32, 32 }, { 64, 16 }, { 16, 64 } };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        const uint8_t *t = sizes[OB8(o, OBJ_subtype) % 7];   /* (0-6 placed) */
        o_init(o, SCDA_TTZ_WALL1, OB8(o, OBJ_render) | 4, 1, t[0], t[1]);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
        SET8(o, OBJ_x_radius, t[0]);
        SET8(o, OBJ_y_radius, t[1]);
    }
    solid(o, OB16(o, OBJ_x_pos));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $45 sliding wall ------------------------------------------------------------ */
#define WL_TIME         0x30               /* oVar2A */
static void wall42(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_init(o, SCDA_TTZ_WALL42, OB8(o, OBJ_render) | 4, 3, 0x20, 0x40);
        SET8(o, OBJ_x_radius, 0x20);
        SET8(o, OBJ_y_radius, 0x40);
        uint32_t f = flag_byte(o);
        if (!f || !(r8(f) & 1)) {
            if (f) w8(f, r8(f) | 1);
            SET16(o, WL_TIME, 0x3C);
            SET8(o, OBJ_routine, 2);
        } else {                                              /* (it has slid: where it stopped) */
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0xC0);
            SET8(o, OBJ_routine, 6);
        }
        break;
    }
    case 2:                                                   /* a second */
        SET16(o, WL_TIME, OB16(o, WL_TIME) - 1);
        if (!OB16(o, WL_TIME)) { SET16(o, WL_TIME, 0x180); SET8(o, OBJ_routine, 4); }
        solid(o, OB16(o, OBJ_x_pos));
        break;
    case 4:                                                   /* sliding: a rider walks with it */
        solid(o, OB16(o, OBJ_x_pos));                         /* (no carry: CD's x speed is 0) */
        for (int n = 0; n < 2; n++)
            if (touched(o, n)) SET16(o_player(n), OBJ_ground_vel, 0x80);
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + 0x8000);
        SET16(o, WL_TIME, OB16(o, WL_TIME) - 1);
        if (!OB16(o, WL_TIME)) SET8(o, OBJ_routine, 6);
        break;
    default:
        solid(o, OB16(o, OBJ_x_pos));
        break;
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }   /* (seen or not) */
    o_draw(o);
}

/* ---- $47 chase gate (act 3) -------------------------------------------------------- */
#define W4_TIME         0x30               /* oVar2A */
#define W4_GO           0x3F               /* oVar3F: drop (subtype 1: set by another) */
static void wall4(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    uint8_t sub = OB8(o, OBJ_subtype);
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        o_init(o, SCDA_TTZ_WALL4, OB8(o, OBJ_render) | 4, 3, 0x40, 0x40);
        SET8(o, OBJ_x_radius, 0x40);
        SET8(o, OBJ_y_radius, 0x40);
        uint32_t f = flag_byte(o);
        if (!f || !(r8(f) & 1)) {
            SET16(o, W4_TIME, sub ? 0x20 : 0x10);
            SET8(o, OBJ_routine, (sub + 1) * 2);
        } else {                                              /* (down already) */
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + (sub ? 0x80 : 0x40));
            SET8(o, OBJ_routine, 4);
        }
        break;
    }
    case 2:                                                   /* player 1 past it: it drops behind him */
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos)) < 0x80 &&
            (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) - 0xA0) < 0x20) {
            SET8(o, W4_GO, 0xFF);
            SET8(o, OBJ_routine, 4);
        }
        break;
    case 4:
        if (OB8(o, W4_GO)) {
            uint32_t f = flag_byte(o);
            if (f) w8(f, r8(f) | 1);
            SET8(o, OBJ_routine, 6);
        }
        break;
    case 6:                                                   /* 4 px a frame */
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + 0x40000);
        SET16(o, W4_TIME, OB16(o, W4_TIME) - 1);
        if (!OB16(o, W4_TIME)) { SET8(o, W4_GO, 0); SET8(o, OBJ_routine, 4); }
        break;
    default:
        break;
    }
    solid(o, OB16(o, OBJ_x_pos));
    if (sub != 1 && o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ---- $39 crumbling ledge ------------------------------------------------------------- */
#define KZ_TIME         0x30               /* oVar2A */
#define KZ_DIR          0x32               /* oVar2C: 8 px the way it breaks (a piece: its fall, long) */
#define KZ_PIECE        0x80               /* (subtype: a piece; CD's subtype 2) */
static int kz_piece(uint32_t o, int dy, int dx, int t)         /* (CD copies its first $10 bytes) */
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
    SET32(a1, OBJ_x_pos, r32(o + OBJ_x_pos));
    SET32(a1, OBJ_y_pos, r32(o + OBJ_y_pos));
    SET8(a1, OBJ_subtype, KZ_PIECE);
    SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + dy);
    SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + dx);
    SET16(a1, KZ_TIME, (uint16_t)t);
    return 1;
}

static void kuzure(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (OB8(o, OBJ_subtype) & KZ_PIECE) {                     /* a piece */
        switch (OB8(o, OBJ_routine)) {
        case 0:
            SET8(o, OBJ_routine, 2);
            SET8(o, OBJ_frame, 8);
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
            int32_t v = (int32_t)r32(o + KZ_DIR) + 0x4000;
            if (v > 0x160000) v = 0x160000;
            SET32(o, KZ_DIR, (uint32_t)v);
            SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (uint32_t)v);
            break;
        }
        }
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        uint8_t n = OB8(o, OBJ_subtype);
        o_init(o, SCDA_TTZ_KUZURE, 4, 3, (uint8_t)(n << 3), 0x11);
        SET8(o, OBJ_y_radius, 0x11);
        SET8(o, OBJ_frame, n - 1);
        SET8(o, OBJ_x_radius, (uint8_t)(n << 3));
    }   /* fall through */
    case 2:
        o_platform(o, OB8(o, OBJ_width_px) + 10, 0x11, OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & ST_P1_STAND) {
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_respawn, 0);                         /* (CD clears oRespawn: it stays gone) */
            SET16(o, KZ_TIME, 8);
            SET16(o, KZ_DIR, OBS16(p, OBJ_x_vel) < 0 ? (uint16_t)-8 : 8);
            o_sfx_cd(FM_COLLAPSE);
        }
        if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
        o_draw(o);
        return;
    case 4:
        o_platform(o, OB8(o, OBJ_width_px) + 10, 0x11, OB16(o, OBJ_x_pos));
        SET16(o, KZ_TIME, OB16(o, KZ_TIME) - 1);
        if (OBS16(o, KZ_TIME) < 0) SET8(o, OBJ_routine, 6);
        o_draw(o);
        return;
    default: {                                                /* 6: a column breaks off the far end */
        int d3 = (OB8(o, OBJ_subtype) - 1) * 8;
        if (OBS16(o, KZ_DIR) >= 0) d3 = -d3;
        if (kz_piece(o, -8, d3, 4))                           /* (the top one, then the one under it) */
            kz_piece(o, 8, d3, 0);
        o_platform(o, OB8(o, OBJ_width_px) + 10, 0x11, OB16(o, OBJ_x_pos));
        if (cd_stood_on(o)) cd_get_off(o);
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) - 1);
        if ((int8_t)OB8(o, OBJ_subtype) <= 0) { o_delete(o); return; }
        SET8(o, OBJ_routine, 4);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 1);
        SET8(o, OBJ_x_radius, OB8(o, OBJ_x_radius) - 8);
        SET8(o, OBJ_width_px, OB8(o, OBJ_width_px) - 8);
        SET16(o, KZ_TIME, 7);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + OB16(o, KZ_DIR));
        o_platform(o, OB8(o, OBJ_width_px) + 10, 0x11, OB16(o, OBJ_x_pos));
        o_draw(o);
        return;
    }
    }
}

/* ---- $25 shutter ------------------------------------------------------------------------ */
#define TB_SW           0x30               /* oVar30: its switch (CD keeps a pointer) */
#define TB_TIME         0x32               /* oVar2A */
#define TB_VY           0x34               /* oVar2C (long) */
static void tobiras(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        uint8_t k = OB8(o, OBJ_subtype) & 31;
        if (scd_ttz_switch[k]) { o_delete(o); return; }       /* (its switch was pressed: open) */
        SET8(o, TB_SW, k);
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_TOBIRAS, OB8(o, OBJ_render) | 4, 3, 0x40, 0x40);
        SET8(o, OBJ_y_radius, 0x40);
        SET8(o, OBJ_x_radius, 0x40);
    }   /* fall through */
    case 2: {
        if (!scd_ttz_switch[OB8(o, TB_SW) & 31]) {
            solid(o, OB16(o, OBJ_x_pos));
            if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
            o_draw(o);
            return;
        }
        SET8(o, OBJ_routine, 4);                              /* (into four strips: up, down, up, down) */
        SET8(o, OBJ_x_radius, 0x10);
        SET8(o, OBJ_width_px, 0x10);
        SET16(o, TB_TIME, 0x80);
        SET32(o, TB_VY, 0xFFFF0000u);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x30);
        uint16_t d5 = OB16(o, OBJ_x_pos);
        for (int d6 = 2; d6 >= 0; d6--) {
            uint32_t a1 = o_alloc_after(o);
            if (!a1) { o_delete(o); return; }
            for (unsigned k = 0; k < OBJ_SIZE; k += 2) w16(a1 + k, r16(o + k));
            SET16(a1, OBJ_respawn, 0);                        /* (S3K: the copies are not the layout's) */
            SET8(a1, OBJ_status, OB8(a1, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND | ST_P1_PUSH | ST_P2_PUSH));
            d5 = (uint16_t)(d5 + 0x20);
            SET16(a1, OBJ_x_pos, d5);
            if (!(d6 & 1)) {
                SET32(a1, TB_VY, (uint32_t)-(int32_t)r32(a1 + TB_VY));
                SET8(a1, OBJ_frame, OB8(a1, OBJ_frame) + 1);
            }
        }
        break;
    }
    default:                                                  /* 4: 1 px a frame, 129 frames */
        SET16(o, TB_TIME, OB16(o, TB_TIME) - 1);
        if (OBS16(o, TB_TIME) < 0) { o_delete(o); return; }
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + TB_VY));
        break;
    }
    solid(o, OB16(o, OBJ_x_pos));
    o_draw(o);
}

void scd_ttz_blk2_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x25] = tobiras;
    t[base + 0x26] = escal;
    t[base + 0x29] = brunko;
    t[base + 0x2A] = funsui;
    t[base + 0x34] = wall1;
    t[base + 0x39] = kuzure;
    t[base + 0x44] = ukidai;
    t[base + 0x45] = wall42;
    t[base + 0x48] = baneiwa;
    t[base + 0x49] = daid;
}

/* act 3's (its own ids): the block, the chase gate */
void scd_ttz_blk3_register(TriObjFn *t, int base3)
{
    t[base3 + 0x34] = wall1;
    t[base3 + 0x47] = wall4;
}
