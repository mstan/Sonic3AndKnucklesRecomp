/*
 * scd_ttz_badnik.c — Tidal Tempest's badniks, animals, Metal Sonic's
 * projector and the robot generator (Sonic CD; decomp: R4\TONBO.C,
 * AMENBO.C, TAGAMEB4.C, YAGO.C, FRIEND4.C, MOVIE4.C, ET4.C; the disc's
 * code, the same in every period of acts 1 and 2).
 *
 *   $2B Tonbo        a dragonfly: to and fro on a sine 16 px high, turning
 *                    every 256 frames (damaged: half as fast, 512)
 *   $2C Amenbo       a water strider on the surface: speeds up, slows, stops
 *                    a second, turns; every 2 s it shoots at a player in front
 *                    within 96 px (subtype $FF: the shot); damaged: slower,
 *                    no shots
 *   $2D Taga-Taga    a water bug with three spikes on its back: walks to and
 *                    fro; a player 40-159 px in front and up to 56 px above,
 *                    it stops, sheds the spikes (harmless, they fly off) and
 *                    dashes away; damaged: slower, no spikes, never stops
 *   $2E Yago         a nymph: walks in strides along the floor, turning at
 *                    walls; a player 16-79 px in front, it crouches and
 *                    lunges at him (damaged: shorter, it lands)
 *   $2F animals      fish swimming on a sine (subtype bit 0: the other kind);
 *                    in the past and present only once the projector is gone,
 *                    in the futures only good; subtype bit 7: the projector's
 *                    hologram of one, circling
 *   $51 generator    Palmtree Panic's (scd_ppz.c: the same code)
 *   $52 projector    (the past) Palmtree Panic's, its show here: two Metal
 *                    Sonics and two animals
 * Subtype 0 is the robot as built, any other its damaged self (the futures'
 * and where those stand in the present); a good future blows them up
 * (DestroyOnGoodFuture). Children keep a subtype with bit 7.
 */
#include "trilogy_obj.h"


#define FM_SHOT         0xA0               /* (act_tama: Amenbo's shot) */
#define FM_SHED         0xB3               /* (a_stop: Taga-Taga's spikes fly off) */
#define FM_EXPLODE      0x9E               /* (m_baku: the projector's blasts) */

int scd_good_future(void);                 /* scd_time.c */
int scd_proj_destroyed(void);
void scd_proj_destroyed_set(void);
void cd_generator(uint32_t o);             /* scd_ppz.c */
void scd_ttz_animal(uint32_t o);

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static uint32_t ram(uint16_t a) { return 0xFF0000u | a; }
static int32_t lval(uint32_t o, int f) { return (int32_t)r32(o + (uint32_t)f); }

/* ---- shared ------------------------------------------------------------- */
static void radii(uint32_t o, int xr, int width, int yr)      /* (oXRadius, oWidth, oYRadius) */
{
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_width_px, width);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_height_px, yr);
}

static void badnik_init(uint32_t o, int asset, int render, int prio, int xr, int width, int yr, uint8_t coll)
{
    o_asset(o, asset);
    SET8(o, OBJ_render, render);
    SET16(o, OBJ_priority, (uint16_t)(prio << 7));
    radii(o, xr, width, yr);
    SET8(o, OBJ_coll_flags, tri_scd_coll(coll));
}

static void turn(uint32_t o)
{
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
}

/* DrawObject + CheckObjDespawn (from home), touchable */
static void badnik_end(uint32_t o, uint16_t home)
{
    if (cd_despawn(o, home)) return;
    o_touch(o);
    o_draw(o);
}

/* subq.w #1,timer + bpl: 1 while it has not run below 0 */
static int count_down(uint32_t o, int f)
{
    SET16(o, f, OB16(o, f) - 1);
    return OBS16(o, f) >= 0;
}

/* ObjGetLWallDist as Sonic CD has it for objects: S&K's flips the probe's x
 * within its block first (eori #$F), CD's does not; undone (scd_wwz_badnik.c) */
static int16_t cd_wall_left(uint32_t o, int d3)
{
    int x = OB16(o, OBJ_x_pos);
    return o_wall_left(o, (((x + d3) & 0xFFFF) ^ 0xF) - x);
}

static int16_t wall_ahead(uint32_t o)                         /* (facing right: the right; else the left; at its middle) */
{
    return (OB8(o, OBJ_render) & 1) ? o_wall_right(o, 0) : cd_wall_left(o, 0);
}

/* ---- $2B Tonbo ------------------------------------------------------------ */
#define TB_Y0           0x40               /* oVar2A (long) */
#define TB_ANGLE        0x44               /* oVar2E (word: its low byte the angle) */
#define TB_VX           0x30               /* oVar30 (long) */
#define TB_STEP         0x34               /* oVar34 */
#define TB_TIME         0x36               /* oVar36 */
#define TB_TURN         0x38               /* oVar38 */
#define TB_X0           0x3E               /* oVar3E */
static void tonbo(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_TONBO2 : SCDA_TONBO;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 4, 1, 0x1C, 0x1C, 0x10, 0x2C);
        SET16(o, TB_X0, OB16(o, OBJ_x_pos));
        SET32(o, TB_Y0, r32(o + OBJ_y_pos));
        SET32(o, TB_VX, dmg ? (uint32_t)-0x8000 : (uint32_t)-0x10000);
        SET16(o, TB_STEP, dmg ? 1 : 4);
        SET16(o, TB_TURN, dmg ? 0x200 : 0x100);
        SET16(o, TB_TIME, OB16(o, TB_TURN) >> 1);
    }
    SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + TB_VX));
    int16_t s, c;
    o_sincos(OB8(o, TB_ANGLE + 1), &s, &c);
    SET32(o, OBJ_y_pos, (uint32_t)((int32_t)s * 0x1000 + lval(o, TB_Y0)));   /* (16 px) */
    SET16(o, TB_ANGLE, OB16(o, TB_ANGLE) + OB16(o, TB_STEP));
    SET16(o, TB_TIME, OB16(o, TB_TIME) - 1);
    if (!OB16(o, TB_TIME)) {
        SET16(o, TB_TIME, OB16(o, TB_TURN));
        SET32(o, TB_VX, (uint32_t)-lval(o, TB_VX));
        turn(o);
    }
    o_animate(o, o_asset_anim(asset));
    badnik_end(o, OB16(o, TB_X0));
}

/* ---- $2C Amenbo ----------------------------------------------------------- */
#define AM_TIME         0x40               /* oVar2A */
#define AM_VX           0x42               /* oVar2C (long) */
#define AM_ACCEL        0x30               /* oVar30 (long) */
#define AM_DECEL        0x34               /* oVar34 (long) */
#define AM_SHOT         0x3C               /* oVar3C: to the next look */
static int am_sees(uint32_t o, uint32_t p)                    /* (in front, within 96 px each way) */
{
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x60) >= 0xC0) return 0;
    int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if ((uint16_t)(dx + 0x60) >= 0xC0) return 0;
    return (dx < 0) == !(OB8(o, OBJ_render) & 1);
}

static void am_shoot(uint32_t o)
{
    SET16(o, AM_SHOT, OB16(o, AM_SHOT) - 1);
    if (OBS16(o, AM_SHOT) >= 0) return;
    SET16(o, AM_SHOT, 0x78);
    if (!am_sees(o, RAM_Player_1)) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, r32(o + OBJ_code));
    SET8(a1, OBJ_subtype, 0xFF);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    SET16(a1, OBJ_priority, OB16(o, OBJ_priority) + 0x80);
    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
    radii(a1, 4, 4, 4);
    SET8(a1, OBJ_coll_flags, tri_scd_coll(0xB2));
    SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
    SET8(a1, OBJ_anim, 1);
    SET32(a1, AM_VX, (OB8(a1, OBJ_render) & 1) ? 0x20000 : (uint32_t)-0x20000);
    if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SHOT);
}

static void amenbo(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* the shot */
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + AM_VX));
        o_animate(o, o_asset_anim(SCDA_AMENBO));
        badnik_end(o, OB16(o, OBJ_x_pos));
        return;
    }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_AMENBO2 : SCDA_AMENBO;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, OB8(o, OBJ_render) | 4, 3, 0x14, 0x14, 0x0C, 0x31);
        SET16(o, AM_SHOT, 0x78);
        SET32(o, AM_ACCEL, dmg ? (uint32_t)-0x200 : (uint32_t)-0x600);
        SET32(o, AM_DECEL, dmg ? 0xC0 : 0x100);
        /* fall through */
    case 2: {                                                 /* speeding up */
        int32_t d6 = lval(o, AM_VX) + lval(o, AM_ACCEL), d5 = dmg ? -0xC000 : -0x18000;
        if (d6 <= d5 || d6 >= -d5) {
            d6 = d6 <= d5 ? d5 : -d5;
            SET8(o, OBJ_routine, 4);
            SET32(o, AM_ACCEL, (uint32_t)-lval(o, AM_ACCEL));
        }
        SET32(o, AM_VX, (uint32_t)d6);
        if (!dmg) am_shoot(o);
        o_animate(o, o_asset_anim(asset));
        break;
    }
    case 4: {                                                 /* slowing to a stop */
        int32_t v = lval(o, AM_VX), d6 = v + lval(o, AM_DECEL);
        if ((v < 0) != (d6 < 0)) {
            SET8(o, OBJ_routine, 6);
            SET32(o, AM_DECEL, (uint32_t)-lval(o, AM_DECEL));
            d6 = 0;
            SET16(o, AM_TIME, 60);
        }
        SET32(o, AM_VX, (uint32_t)d6);
        if (!dmg) am_shoot(o);
        break;
    }
    case 6:                                                   /* a second still, turns */
        if (!count_down(o, AM_TIME)) {
            SET8(o, OBJ_routine, 8);
            turn(o);
            SET16(o, AM_TIME, 60);
        }
        if (!dmg) am_shoot(o);
        break;
    default:                                                  /* a second more */
        if (!count_down(o, AM_TIME)) SET8(o, OBJ_routine, 2);
        break;
    }
    SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + AM_VX));
    SET16(o, OBJ_y_pos, r16(RAM_Water_level) - 8);            /* (on the surface) */
    badnik_end(o, OB16(o, OBJ_x_pos));
}

/* ---- $2D Taga-Taga; subtype $FF: one of its spikes ------------------------ */
#define TG_TIME         0x40               /* oVar2A */
#define TG_VX           0x42               /* oVar2C (long) */
#define TG_KIDS         0x30               /* oVar30, 32, 34: its spikes */
#define TG_TURN         0x36               /* oVar36 */
#define TS_VX           0x30               /* (a spike's oVar2A, long) */
#define TS_VY           0x34               /* oVar2E (long) */
#define TS_AX           0x38               /* oVar32 (long) */
#define TS_AY           0x3C               /* oVar36 (long) */
#define TS_SLOT         0x40               /* oVar3D: the Taga-Taga's saved-state entry (its respawn word) */
#define TS_PARENT       0x42               /* oVar3E */
static void taga_spike(uint32_t o)
{
    uint32_t par = ram(OB16(o, TS_PARENT));
    if (r32(par) != CD_CODE(0x2D) || OB16(par, OBJ_respawn) != OB16(o, TS_SLOT)) { o_delete(o); return; }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        /* fall through */
    case 2:                                                   /* on its back (its parent moves it) */
        break;
    default: {                                                /* shed */
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + TS_VX));
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + TS_VY));
        SET32(o, TS_VX, r32(o + TS_VX) + r32(o + TS_AX));
        SET32(o, TS_VY, r32(o + TS_VY) + r32(o + TS_AY));
        if ((int16_t)(OB16(o, OBJ_y_pos) - r16(RAM_Player_1 + OBJ_y_pos)) >= 0xE0) { o_delete(o); return; }
        break;
    }
    }
    o_draw(o);
}

static int tg_sees(uint32_t o, uint32_t p)                    /* (in front 40-159 px, up to 56 px above) */
{
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x38) >= 0x38) return 0;
    int16_t dx = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if (!(OB8(o, OBJ_render) & 1)) dx = (int16_t)-dx;
    return (uint16_t)(dx - 0x28) < 0x78;
}

static void taga_taga(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) { taga_spike(o); return; }
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        badnik_init(o, dmg ? SCDA_TTZ_TAGATAGA2 : SCDA_TTZ_TAGATAGA, OB8(o, OBJ_render) | 4, 3, 0x14, 0x14, 8, 0x2D);
        SET32(o, TG_VX, dmg ? (uint32_t)-0x8000 : (uint32_t)-0x10000);
        SET16(o, TG_TURN, dmg ? 0x190 : 0xC8);
        if (dmg) break;
        static const int8_t at[3][2] = { { 2, -10 }, { -3, -10 }, { -7, -9 } };
        for (int k = 0; k < 3; k++) {
            uint32_t a1 = o_alloc();
            if (!a1) { o_delete(o); return; }                 /* (its spikes, or none of it) */
            SET32(a1, OBJ_code, CD_CODE(0x2D));
            SET8(a1, OBJ_subtype, 0xFF);
            SET16(a1, TS_SLOT, OB16(o, OBJ_respawn));
            SET16(a1, TS_PARENT, (uint16_t)o);
            SET16(o, TG_KIDS + k * 2, (uint16_t)a1);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + at[k][0]);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + at[k][1]);
            o_asset(a1, SCDA_TTZ_TAGASPIKE);
            SET8(a1, OBJ_render, OB8(a1, OBJ_render) | 4);
            SET16(a1, OBJ_priority, 3 << 7);
            radii(a1, 2, 2, 2);
        }
        break;
    }
    case 2: {                                                 /* walking */
        if (!dmg && tg_sees(o, RAM_Player_1)) {
            SET8(o, OBJ_routine, 4);
            SET16(o, TG_TIME, 60);
            break;
        }
        int32_t v = lval(o, TG_VX);
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + (uint32_t)v);
        if (!dmg)
            for (int k = 0; k < 3; k++) {
                uint32_t a1 = ram(OB16(o, TG_KIDS + k * 2));
                SET32(a1, OBJ_x_pos, r32(a1 + OBJ_x_pos) + (uint32_t)v);
            }
        if (!count_down(o, TG_TIME)) {
            SET16(o, TG_TIME, OB16(o, TG_TURN));
            SET32(o, TG_VX, (uint32_t)-v);
            turn(o);
            if (!dmg) {                                       /* (its spikes on its back, the other way) */
                int right = OB8(o, OBJ_render) & 1;
                static const int8_t off[2][3] = { { -2, 3, 7 }, { -1, -6, -10 } };
                for (int k = 0; k < 3; k++) {
                    uint32_t a1 = ram(OB16(o, TG_KIDS + k * 2));
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + off[right][k]);
                }
            }
        }
        o_animate(o, o_asset_anim(dmg ? SCDA_TTZ_TAGATAGA2 : SCDA_TTZ_TAGATAGA));
        break;
    }
    case 4:                                                   /* seen: a second, then the spikes fly */
        if (count_down(o, TG_TIME)) break;
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SHED);
        SET16(o, TG_TIME, 60);
        SET8(o, OBJ_routine, 6);
        if (!dmg) {
            static const int32_t vx[3] = { -0x20000, 0x10000, 0x20000 };
            for (int k = 0; k < 3; k++) {
                uint32_t a1 = ram(OB16(o, TG_KIDS + k * 2));
                SET32(a1, TS_VX, (uint32_t)vx[k]);
                SET32(a1, TS_VY, (uint32_t)-0x30000);
                SET32(a1, TS_AX, 0);
                SET32(a1, TS_AY, 0x2000);
                SET8(a1, OBJ_routine, OB8(a1, OBJ_routine) + 2);
            }
        }
        break;
    case 6:                                                   /* a second, then away */
        if (count_down(o, TG_TIME)) break;
        SET8(o, OBJ_routine, 8);
        SET32(o, TG_VX, dmg ? 0x18000 : 0x40000);
        if (!(OB8(o, OBJ_render) & 1)) SET32(o, TG_VX, (uint32_t)-lval(o, TG_VX));
        break;
    default:                                                  /* dashing */
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + TG_VX));
        break;
    }
    badnik_end(o, OB16(o, OBJ_x_pos));
}

/* ---- $2E Yago ------------------------------------------------------------- */
#define YG_STRIDE       0x40               /* oVar2A (long): what is left of a stride */
#define YG_VX           0x44               /* oVar2E (long) */
#define YG_TIME         0x32               /* oVar32 */
#define YG_LVY          0x34               /* oVar34 (long): the lunge */
#define YG_LVX          0x38               /* oVar38 (long) */
#define YG_X0           0x3E               /* oVar3E */
static void yago(uint32_t o)
{
    if (cd_destroy_good_future(o)) return;
    int dmg = OB8(o, OBJ_subtype) != 0, asset = dmg ? SCDA_YAGO2 : SCDA_YAGO;
    int16_t d1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET16(o, YG_X0, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_routine, 2);
        badnik_init(o, asset, 4, 1, 0, 0x18, 0x0C, 0x2F);
        /* fall through */
    case 2:                                                   /* down to the floor */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        d1 = o_floor_dist(o);
        if (d1 >= 5) break;
        SET8(o, OBJ_routine, 4);
        if (dmg) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 - 5);
        SET32(o, YG_STRIDE, 0x500000);
        SET32(o, YG_VX, (uint32_t)-0x4000);
        break;
    case 4: {                                                 /* a stride along the floor */
        int32_t st = lval(o, YG_STRIDE) - 0x4000;
        SET32(o, YG_STRIDE, (uint32_t)st);
        if (st < 0) { SET8(o, OBJ_routine, 6); SET16(o, YG_TIME, 0x1F); break; }
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + YG_VX));
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + o_floor_dist(o) - 5);
        if (wall_ahead(o) < 5) { SET8(o, OBJ_routine, 6); SET16(o, YG_TIME, 0x1F); break; }
        int16_t dx = (int16_t)(OB16(o, OBJ_x_pos) - r16(RAM_Player_1 + OBJ_x_pos));
        if (OB8(o, OBJ_render) & 1) dx = (int16_t)-dx;
        if ((uint16_t)(dx - 0x10) < 0x40) {                   /* (him in front, 16-79 px: crouch) */
            SET8(o, OBJ_routine, 0x0A);
            SET16(o, YG_TIME, 7);
        }
        break;
    }
    case 6:                                                   /* still, then turns */
        SET16(o, YG_TIME, OB16(o, YG_TIME) - 1);
        if (OB16(o, YG_TIME)) break;
        SET8(o, OBJ_routine, 8);
        SET16(o, YG_TIME, 0x1F);
        SET32(o, YG_STRIDE, 0x500000);
        SET32(o, YG_VX, (uint32_t)-lval(o, YG_VX));
        turn(o);
        break;
    case 8:
        SET16(o, YG_TIME, OB16(o, YG_TIME) - 1);
        if (!OB16(o, YG_TIME)) SET8(o, OBJ_routine, 4);
        break;
    case 0x0A:
        SET16(o, YG_TIME, OB16(o, YG_TIME) - 1);
        if (OB16(o, YG_TIME)) break;
        SET8(o, OBJ_routine, 0x0C);
        SET16(o, YG_TIME, 0x1F);
        break;
    case 0x0C:                                                /* crouching */
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + (dmg ? 0x2800 : 0x2000));
        SET16(o, YG_TIME, OB16(o, YG_TIME) - 1);
        if (OB16(o, YG_TIME)) break;
        SET8(o, OBJ_routine, 0x0E);
        SET32(o, YG_LVX, dmg ? (uint32_t)-0x10000 : (uint32_t)-0x48000);
        if (OB8(o, OBJ_render) & 1) SET32(o, YG_LVX, (uint32_t)-lval(o, YG_LVX));
        SET32(o, YG_LVY, (uint32_t)-0x18000);
        break;
    case 0x0E:                                                /* the lunge, to a wall (damaged: to the floor) */
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + r32(o + YG_LVX));
        d1 = wall_ahead(o);
        if (dmg ? d1 < 0 : d1 < 5) {
            SET8(o, OBJ_routine, 0x10);
            if (!dmg) SET16(o, YG_TIME, 0x1F);
            break;
        }
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + r32(o + YG_LVY));
        if (dmg) {
            int32_t v = lval(o, YG_LVY) + 0x1000;
            SET32(o, YG_LVY, (uint32_t)v);
            if (v < 0) break;
            if (o_floor_dist(o) < 5) SET8(o, OBJ_routine, 4);
            break;
        }
        if (lval(o, YG_LVY) < 0) SET32(o, YG_LVY, (uint32_t)(lval(o, YG_LVY) + 0x2000));
        if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
        break;
    default:                                                  /* down to the floor, then turns */
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 1);
        d1 = o_floor_dist(o);
        if (d1 >= 5) break;
        SET8(o, OBJ_routine, 6);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 - 5);
        SET16(o, YG_TIME, 0x1F);
        break;
    }
    o_animate(o, o_asset_anim(asset));
    badnik_end(o, OB16(o, YG_X0));
}

/* ---- $2F animals; subtype bit 7: the projector's hologram of one -------- */
#define AN_VX           0x40               /* oVar2A (long) */
#define AN_Y0           0x44               /* oVar2E */
#define AN_ANG          0x30               /* oVar30 (long: its third byte the angle) */
#define AN_ASPD         0x34               /* oVar34 (long) */
#define AN_X0           0x38               /* oVar38 (the hologram's) */
#define AN_PARENT       0x3E               /* oVar3E: the projector */
#define PJ_GONE         0x3F               /* oVar3F: the projector, destroyed */
#define PJ_COUNT        0x30               /* oVar2A */
#define PJ_LOCS         0x32               /* oVar2C */

/* (sin or cos << 16 | the register's high word, >> n, rounded: the disc's sum) */
static int16_t shrink(int16_t v, uint16_t lo, int n)
{
    int32_t d = (int32_t)v * 0x10000 + lo;
    return (int16_t)(((d >> n) + 0x8000) >> 16);
}

void scd_ttz_animal(uint32_t o)                               /* (act 3's $2F is the same code) */
{
    uint8_t sub = OB8(o, OBJ_subtype);
    int holo = sub & 0x80;
    if (!holo) {                                              /* CheckAnimalPrescence */
        if (cd_period() == TRI_CD_PAST || cd_period() == TRI_CD_PRESENT) {
            if (!scd_proj_destroyed()) { cd_despawn(o, OB16(o, OBJ_x_pos)); return; }
        } else if (!scd_good_future()) { o_delete(o); return; }
    }
    if (!OB8(o, OBJ_routine)) {
        int asset = sub & 1 ? SCDA_TTZ_ANIMAL2 : SCDA_TTZ_ANIMAL;   /* (its tile, by act and period; line 1) */
        if (!o_asset_maps(asset)) { o_delete(o); return; }
        o_asset(o, asset);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET16(o, OBJ_priority, 5 << 7);
        radii(o, 8, 8, 0x0C);
        SET16(o, AN_Y0, OB16(o, OBJ_y_pos));
        if (holo) {
            SET8(o, OBJ_routine, 0xFE);
            turn(o);
            SET16(o, AN_X0, OB16(o, OBJ_x_pos));
            SET32(o, AN_ASPD, 0x200);
            if (sub & 1) {
                turn(o);
                SET32(o, AN_ASPD, (uint32_t)-0x200);
                SET32(o, AN_ANG, 0x3000);
            }
        } else {
            SET8(o, OBJ_routine, 2);
            SET8(o, OBJ_anim, 1);
            SET32(o, AN_VX, 0x10000);
            SET32(o, AN_ASPD, 0x100);
        }
        return;                                               /* (the first frame: unseen) */
    }
    int16_t s, c;
    if (holo) {                                               /* circling by the projector, behind it half the time */
        uint32_t pj = ram(OB16(o, AN_PARENT));
        if (r32(pj) != CD_CODE(0x52) || OB8(pj, PJ_GONE)) { o_delete(o); return; }
        o_sincos(OB8(o, AN_ANG + 2), &s, &c);
        SET16(o, OBJ_x_pos, OB16(o, AN_X0) + shrink(c, 0, 3));
        SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + shrink(s, 0, 4));
        SET32(o, AN_ANG, r32(o + AN_ANG) + r32(o + AN_ASPD));
        if (!(r32(o + AN_ANG) & 0x7FFF)) {
            turn(o);
            SET16(o, OBJ_priority, (OB16(o, OBJ_priority) == 5 << 7 ? 3 : 5) << 7);
        }
        o_animate(o, o_asset_anim(sub & 1 ? SCDA_TTZ_ANIMAL2 : SCDA_TTZ_ANIMAL));
        o_draw(o);
        return;
    }
    int32_t vx = lval(o, AN_VX);
    SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) + (uint32_t)vx);
    o_sincos(OB8(o, AN_ANG + 2), &s, &c);
    SET16(o, OBJ_y_pos, OB16(o, AN_Y0) + shrink(s, (uint16_t)((uint32_t)vx >> 16), 3));   /* (32 px) */
    int32_t a = lval(o, AN_ANG) + lval(o, AN_ASPD);
    SET32(o, AN_ANG, (uint32_t)a);
    if (a < 0 || a >= 0x10000) {                              /* (a half wave: back the other way) */
        SET32(o, AN_ASPD, (uint32_t)-lval(o, AN_ASPD));
        SET32(o, AN_VX, (uint32_t)-vx);
        turn(o);
    }
    o_animate(o, o_asset_anim(sub & 1 ? SCDA_TTZ_ANIMAL2 : SCDA_TTZ_ANIMAL));
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ---- $52 Metal Sonic's projector (the past): Palmtree Panic's (cd_projector,
 * scd_ppz.c), its show Tidal Tempest's; subtype: Metal Sonic's hologram ---- */
static void spawn_holo(uint32_t o, int id, int dx, int dy, uint8_t sub)
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, CD_CODE(id));
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, AN_PARENT, (uint16_t)o);
}

static void ttz_projector(uint32_t o)
{
    if (OB8(o, OBJ_subtype)) {                                /* Metal Sonic's hologram */
        uint32_t pj = ram(OB16(o, AN_PARENT));
        if (r32(pj) != CD_CODE(0x52) || OB8(pj, PJ_GONE)) { o_delete(o); return; }
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            int small = OB8(o, OBJ_subtype) & 0x80;
            o_asset(o, SCDA_PROJECTOR);
            SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
            SET16(o, OBJ_priority, 4 << 7);
            radii(o, small ? 8 : 0x14, small ? 8 : 0x14, small ? 4 : 0x18);
            SET8(o, OBJ_anim, small ? 0 : 1);
        }
        o_animate(o, o_asset_anim(SCDA_PROJECTOR));
        o_draw(o);
        return;
    }
    switch (OB8(o, OBJ_routine)) {
    case 0:
        if (scd_proj_destroyed() || !o_asset_maps(SCDA_PROJECTOR)) { o_delete(o); return; }
        SET8(o, OBJ_routine, 2);
        o_asset(o, SCDA_PROJECTOR);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET16(o, OBJ_priority, 4 << 7);
        radii(o, 0x0C, 0x0C, 0x0C);
        SET32(o, PJ_LOCS, o_asset_anim(SCDA_PROJBOOM));
        spawn_holo(o, 0x52, -0x15, -7, 0xFF);
        spawn_holo(o, 0x52, -0x58, -4, 1);
        spawn_holo(o, 0x2F, -0x4C, -0x1C, 0x80);
        spawn_holo(o, 0x2F, -0x54, -0x11, 0x81);
        /* fall through */
    case 2: {
        int hit = o_s1_touch(o, 0x12, 0x11), broken = 0;      /* (CD's touch $FB: size $3B, rolling) */
        for (int n = 0; n < 2; n++)
            if ((hit >> n & 1) && OB8(o_player(n), OBJ_anim) == 2) broken = 1;
        o_solid(o, 0x0C + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        if (!broken) break;
        SET8(o, OBJ_routine, 6);
        SET8(o, OBJ_frame, 1);
        SET8(o, PJ_GONE, 0xFF);
        cd_get_off(o);
        break;
    }
    case 6: {                                                 /* exploding */
        uint32_t a6 = r32(o + PJ_LOCS);
        uint8_t when = a6 ? tri_rd8(a6) : 0xFF;
        if (when & 0x80) { SET8(o, OBJ_routine, 8); SET16(o, PJ_COUNT, 60); break; }
        SET8(o, PJ_COUNT, OB8(o, PJ_COUNT) + 1);
        if (OB8(o, PJ_COUNT) != when) break;
        int8_t dx = (int8_t)tri_rd8(a6 + 1), dy = (int8_t)tri_rd8(a6 + 2);
        SET32(o, PJ_LOCS, a6 + 3);
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S3K_Obj_Explosion);
        SET8(a1, OBJ_routine, 6);                             /* (no animal) */
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + dy);
        o_sfx_cd(FM_EXPLODE);                                 /* (each blast; no jingle) */
        break;
    }
    default:                                                  /* gone: the act's animals free */
        SET16(o, PJ_COUNT, OB16(o, PJ_COUNT) - 1);
        if (OB16(o, PJ_COUNT)) break;
        scd_proj_destroyed_set();
        o_delete(o);
        return;
    }
    if (OB8(o, OBJ_routine) <= 2 && cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

void scd_ttz_badnik_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x2B] = tonbo;
    t[base + 0x2C] = amenbo;
    t[base + 0x2D] = taga_taga;
    t[base + 0x2E] = yago;
    t[base + 0x2F] = scd_ttz_animal;
    t[base + 0x51] = cd_generator;
    t[base + 0x52] = ttz_projector;
}
