/*
 * scd_ccz_plat.c — Collision Chaos' traps and platforms (Sonic CD; the disc's
 * R31A-R32D programs; decomp: R3\TRAP_R3.C, TOGEBL3A/B/D.C).
 *
 *   $20 spikes          up (stood on, they hurt), down or to a side (touched);
 *                       subtype bit 2: they hide (32 px) and come out again,
 *                       a second each way; a trap block's ride on it
 *   $21 swinging block  to and fro 64 px either way (subtype 1: still);
 *                       subtypes 2, 3 carry a spring (3 yellow); still in the
 *                       past
 *   $22 trap block      shut where it is placed; player 1 coming from its
 *                       left opens it (32 px down, subtype bit 2 up), and it
 *                       shuts once he is 80 px into it; subtype 1 spikes on
 *                       it, 2-3 a spring (to the right), bits 2-3 its frame
 *   $23 platforms       0 still, 1-2 back and forth, 3-4 up and down, 5-7
 *                       rise 128-170 px half a second after they are stood
 *                       on; most sink 4 px under a player
 *   $26 gate ledge      out while player 1 is above it, drawn in 64 px (to
 *                       the left; subtype bit 7 right) while he is not
 *   $27 turret          (present and bad future) a plate player 1 sets off
 *                       from 120 px: it blinks a second, rises 32 px, fires
 *                       two shots along the floor and throws whoever stands
 *                       on it up at $700; subtype 1 its plate, 2 a shot
 *   $2C spike ball      spiked balls on an arm (2 in the past, 3 in the
 *                       present, 4 in the futures; 16 px apart) turning round
 *                       its pivot, one turn in 256 frames (subtype: the other
 *                       way); the balls are subtype $FF
 * Their code is the same in every period of acts 1 and 2. CD's object
 * variables oVar2A.. sit at S3K's $30.. (oVar + 6); routines keep CD's even
 * numbers. SolidObject -> o_solid, TopSolidObject -> o_platform (S3K carries a
 * standing player by how far it moved this frame: where CD carries him by its
 * x speed, the solid is called after the move, else with its own x),
 * GetOffObject -> cd_get_off; objPlayerSlot -> Player 1.
 */
#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>

/* this zone's asset ids (scd_level.py CD_ASSETS; -> trilogy_obj.h) */

#define P_ANIM          0x20
#define ST_AIR          0x02
#define ST_ONOBJ        0x08
#define FM_SPIKES       0xB7               /* (harir3_sub: the spikes go in or come out) */

static int s_base;
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static int time_zone(void)                      /* timeZone: 0 past, 1 present, 2 either future */
{
    int p = cd_period();
    return p == TRI_CD_PAST ? 0 : p == TRI_CD_PRESENT ? 1 : 2;
}

static int range_out(uint32_t o, uint16_t x)    /* (x out of range: gone, its respawn cleared: tr3_fout) */
{
    if (!o_out_of_range(o, x)) return 0;
    o_delete_respawnable(o);
    return 1;
}

/* A spring riding a block: S3K's own (Obj_Spring), moved with its block each
 * frame (Sonic CD's spring follows its parent: its $34/$38/$39). S3K's
 * spring replaces its code after its first frame; its mappings mark it. */
#define RS_SPR          0x42               /* (the spring) */
#define RS_MAP          0x44               /* (its mappings' low word) */
#define RS_WANT         0x47               /* (its subtype + 1: S3K's spring leaves on its own range check
                                            *  while its block stays; the block makes it again in range) */
static void spring_new(uint32_t o, int dx, int dy, uint8_t sub)   /* (FindNextObjSlot) */
{
    SET8(o, RS_WANT, (uint8_t)(sub + 1));
    SET16(o, RS_SPR, 0);
    SET16(o, RS_MAP, 0);
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET32(a1, OBJ_code, S3K_Obj_Spring);
    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + dx));
    SET16(a1, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + dy));
    SET8(a1, OBJ_subtype, sub);
    SET16(o, RS_SPR, (uint16_t)a1);
}

static uint32_t spring_of(uint32_t o)           /* (its spring still, or 0) */
{
    uint32_t spr = 0xFF0000u | OB16(o, RS_SPR);
    if (!OB16(o, RS_SPR) || !r32(spr + OBJ_code)) { SET16(o, RS_SPR, 0); return 0; }
    uint16_t map = (uint16_t)r32(spr + OBJ_mappings);
    if (!OB16(o, RS_MAP)) SET16(o, RS_MAP, map);
    else if (map != OB16(o, RS_MAP)) { SET16(o, RS_SPR, 0); return 0; }
    return spr;
}

static void spring_follow(uint32_t o, int dx, int dy)
{
    uint32_t spr = spring_of(o);
    if (!spr && OB8(o, RS_WANT) && !o_out_of_range(o, (uint16_t)(OB16(o, OBJ_x_pos) + dx))) {
        spring_new(o, dx, dy, (uint8_t)(OB8(o, RS_WANT) - 1));
        spr = spring_of(o);
    }
    if (!spr) return;
    SET16(spr, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + dx));
    SET16(spr, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + dy));
}

static void spring_drop(uint32_t o)
{
    uint32_t spr = spring_of(o);
    if (spr) o_delete(spr);
}

/* ---- $20 spikes ---------------------------------------------------------- */
#define SP_Y0    0x38                  /* oVar32 */
#define SP_PAR   0x3A                  /* oVar34: the block it rides (0: none) */
#define SP_X0    0x3C                  /* oVar36 */
#define SP_DX    0x3E                  /* oVar38: from the block */
#define SP_DY    0x3F                  /* oVar39 */
#define SP_WAIT  0x40                  /* oVar3A */
#define SP_OFF   0x41                  /* oVar3B: how far in */
#define SP_IN    0x42                  /* oVar3C: going in */
static void spikes_hide(uint32_t o)             /* harir3_sub: out, 60 frames, in, 60 frames */
{
    if (OB8(o, SP_WAIT)) {
        SET8(o, SP_WAIT, OB8(o, SP_WAIT) - 1);
        if (OB8(o, SP_WAIT)) return;
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SPIKES);
    }
    if (OB8(o, SP_IN)) {
        int v = OB8(o, SP_OFF) - 8;
        if (v >= 0) { SET8(o, SP_OFF, v); return; }
        SET8(o, SP_OFF, 0);
        SET8(o, SP_IN, 0);
        SET8(o, SP_WAIT, 60);
        return;
    }
    int v = OB8(o, SP_OFF) + 8;
    if (v < 0x20) { SET8(o, SP_OFF, v); return; }
    SET8(o, SP_OFF, 0x20);
    SET8(o, SP_IN, 1);
    SET8(o, SP_WAIT, 60);
}

void spikes_ccz(uint32_t o)                     /* (act 3's too: scd_ccz.c) */
{
    static const uint8_t sizes[4][3] = { { 16, 8, 0 }, { 18, 12, 0x83 }, { 12, 16, 0x82 }, { 12, 16, 0x82 } };
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        const uint8_t *z = sizes[sub & 3];
        o_init(o, SCDA_CCZSPIKES, OB8(o, OBJ_render) | 4, 3, z[0], z[1]);
        SET8(o, OBJ_y_radius, z[1]);
        if (z[2]) SET8(o, OBJ_coll_flags, tri_scd_coll(z[2]));
        SET16(o, SP_X0, OB16(o, OBJ_x_pos));
        SET16(o, SP_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_frame, sub & 3);
    }
    if (sub & 4) {                                            /* harir3_kind */
        spikes_hide(o);
        int d = OB8(o, SP_OFF);
        switch (sub & 3) {
        case 0: SET16(o, OBJ_y_pos, OB16(o, SP_Y0) + d); break;
        case 1: SET16(o, OBJ_y_pos, OB16(o, SP_Y0) - d); break;
        case 2: SET16(o, OBJ_x_pos, OB16(o, SP_X0) - d); break;
        default: SET16(o, OBJ_x_pos, OB16(o, SP_X0) + d); break;
        }
    }
    int w = OB8(o, OBJ_width_px), h = OB8(o, OBJ_y_radius);
    o_solid(o, w + 10, h, h + 1, OB16(o, OBJ_x_pos));
    if (sub & 3)
        o_touch(o);                                           /* (down or sideways: touched, they hurt) */
    else {                                                    /* (up: stood on) */
        uint8_t st = OB8(o, OBJ_status);
        if (st & ST_P1_STAND) o_spike_hurt(o, RAM_Player_1);
        if (st & ST_P2_STAND) o_spike_hurt(o, RAM_Player_2);
    }
    o_draw(o);
    if (OB16(o, SP_PAR)) {                                    /* (riding a block) */
        uint32_t par = 0xFF0000u | OB16(o, SP_PAR);
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(par, OBJ_x_pos) + (int8_t)OB8(o, SP_DX)));
        SET16(o, OBJ_y_pos, (uint16_t)(OB16(par, OBJ_y_pos) + (int8_t)OB8(o, SP_DY)));
    }
    range_out(o, OB16(o, SP_X0));
}

/* ---- $21 swinging block --------------------------------------------------- */
#define SW_Y0    0x36                  /* oVar30 */
#define SW_X0    0x38                  /* oVar32 */
#define SW_SPD   0x3C                  /* oVar36 */
#define SW_ACC   0x3E                  /* oVar38 */
#define SW_COUNT 0x40                  /* oVar3A */
#define SW_STEP  0x41                  /* oVar3B */
static void swing_block(uint32_t o)
{
    static const int16_t steps[4][3] = {                      /* frames, ease, speed */
        { 0x40, -8, 0x200 }, { 0x40, -8, 0 }, { 0x40, 8, -0x200 }, { 0x40, 8, 0 },
    };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SWINGBLOCK, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET16(o, SW_X0, OB16(o, OBJ_x_pos));
        SET16(o, SW_Y0, OB16(o, OBJ_y_pos));
        SET16(o, RS_SPR, 0);
        if (OB8(o, OBJ_subtype) >= 2) {
            spring_new(o, 0, -22, (uint8_t)((OB8(o, OBJ_subtype) & 1) << 1));
            if (!OB16(o, RS_SPR)) { o_delete(o); return; }    /* (FindNextObjSlot failed: gone) */
        }
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    if (time_zone() && OB8(o, OBJ_subtype) != 1) {            /* (still in the past) */
        for (;;) {
            if (!OB8(o, SW_COUNT)) {                          /* (a step: no move this frame) */
                const int16_t *t = steps[OB8(o, SW_STEP) & 3];
                SET8(o, SW_COUNT, t[0]);
                SET16(o, SW_ACC, (uint16_t)t[1]);
                SET16(o, SW_SPD, (uint16_t)t[2]);
                break;
            }
            SET16(o, SW_SPD, OB16(o, SW_SPD) + OB16(o, SW_ACC));
            SET16(o, OBJ_x_vel, OB16(o, SW_SPD));
            cd_move(o, (int32_t)OBS16(o, SW_SPD) << 8, 0);
            SET8(o, SW_COUNT, OB8(o, SW_COUNT) - 1);
            if (OB8(o, SW_COUNT)) break;
            SET8(o, SW_STEP, (OB8(o, SW_STEP) + 1) & 3);
        }
    }
    o_solid(o, 0x10 + 10, 0x10, 0x11, oldx);
    spring_follow(o, 0, -22);
    o_draw(o);
    if (o_out_of_range(o, OB16(o, SW_X0))) { spring_drop(o); o_delete_respawnable(o); }
}

/* ---- $22 trap block ------------------------------------------------------- */
#define TB_Y0    0x38                  /* oVar32 */
#define TB_X0    0x3C                  /* oVar36 */
#define TB_OFF   0x41                  /* oVar3B */
static void trap_block(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!OB8(o, OBJ_routine)) {
        o_init(o, SCDA_TRAPBLOCK, OB8(o, OBJ_render) | 4, 3, 0x10, 0x0E);
        SET8(o, OBJ_frame, sub >> 2);
        SET8(o, OBJ_y_radius, 0x0E);
        SET16(o, TB_X0, OB16(o, OBJ_x_pos));
        SET16(o, TB_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_routine, 2);
        SET16(o, RS_SPR, 0);
        if ((sub & 3) == 1) {                                 /* (spikes on it, to the right) */
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                SET32(a1, OBJ_code, CD_CODE(0x20));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET16(a1, SP_PAR, (uint16_t)o);
                SET8(a1, SP_DX, 0x20);
                SET8(a1, OBJ_subtype, 2);
            }
        } else if (sub & 3)                                   /* (a spring, sideways, to the right) */
            spring_new(o, 0x18, 0, (uint8_t)(0x10 | ((sub & 1) << 1)));
    }
    uint32_t p = RAM_Player_1;                                /* trapdr3_updown */
    uint16_t d1 = (uint16_t)(OB16(p, OBJ_x_pos) - (uint16_t)(OB16(o, OBJ_x_pos) - OB8(o, OBJ_width_px)));
    if (OB16(p, OBJ_x_pos) < (uint16_t)(OB16(o, OBJ_x_pos) - OB8(o, OBJ_width_px))) {   /* (left of it: open) */
        int v = OB8(o, TB_OFF) + 8;
        SET8(o, TB_OFF, v >= 0x20 ? 0x20 : v);
    } else if (d1 >= 0x50) {                                  /* (well into it: shut) */
        int v = OB8(o, TB_OFF) - 8;
        SET8(o, TB_OFF, v < 0 ? 0 : v);
    }
    SET16(o, OBJ_y_pos, OB16(o, TB_Y0) + (sub & 4 ? -OB8(o, TB_OFF) : OB8(o, TB_OFF)));
    o_solid(o, 0x10 + 10, 0x0E, 0x0F, OB16(o, OBJ_x_pos));
    spring_follow(o, 0x18, 0);
    o_draw(o);
    if (o_out_of_range(o, OB16(o, TB_X0))) { spring_drop(o); o_delete_respawnable(o); }
}

/* ---- $23 platforms --------------------------------------------------------- */
#define PF_Y0    0x36                  /* oVar30 */
#define PF_X0    0x38                  /* oVar32 */
#define PF_SPD   0x3A                  /* oVar34 */
#define PF_ACC   0x3C                  /* oVar36 */
#define PF_COUNT 0x3E                  /* oVar38 */
#define PF_STEP  0x3F                  /* oVar39 */
#define PF_PHASE 0x40                  /* oVar3A (5-7) */
#define PF_SINK  0x44                  /* oVar3E: the sink's angle */
#define PF_SINKS 0x45                  /* oVar3F: it sinks */
typedef struct { int8_t count, ease; int16_t speed; } PfStep;
static const PfStep pf_right[8] = { { 28, 0, 0x300 }, { 8, -48, 0x300 }, { 8, -48, 0 }, { 56, 0, -0x300 },
                                    { 8, 48, -0x300 }, { 8, 48, 0 }, { 56, 0, 0x300 }, { 8, -48, 0x300 } };
static const PfStep pf_left[8] = { { 28, 0, -0x300 }, { 8, 48, -0x300 }, { 8, 48, 0 }, { 56, 0, 0x300 },
                                   { 8, -48, 0x300 }, { 8, -48, 0 }, { 56, 0, -0x300 }, { 8, 48, -0x300 } };
static const PfStep pf_up[8] = { { 35, 0, -0x200 }, { 8, 64, -0x200 }, { 8, 64, 0 }, { 70, 0, 0x200 },
                                 { 8, -64, 0x200 }, { 8, -64, 0 }, { 70, 0, -0x200 }, { 8, 64, -0x200 } };
static const PfStep pf_down[8] = { { 35, 0, 0x200 }, { 8, -64, 0x200 }, { 8, -64, 0 }, { 70, 0, -0x200 },
                                   { 8, 64, -0x200 }, { 8, 64, 0 }, { 70, 0, 0x200 }, { 8, -64, 0x200 } };
static const PfStep pf_rise[3][3] = {
    { { 16, -16, 0 }, { 48, 0, -0x200 }, { 16, 16, -0x200 } },
    { { 16, -48, 0 }, { 69, 0, -0x300 }, { 16, 48, -0x300 } },
    { { 16, -64, 0 }, { 88, 0, -0x400 }, { 16, 64, -0x400 } },
};

static void pf_load(uint32_t o, const PfStep *t)
{
    t += OB8(o, PF_STEP);
    SET8(o, PF_COUNT, t->count);
    SET16(o, PF_ACC, (uint16_t)t->ease);
    SET16(o, PF_SPD, (uint16_t)t->speed);
}

static void pf_next(uint32_t o)                 /* (the step after: 2-7 again and again) */
{
    SET8(o, PF_STEP, OB8(o, PF_STEP) + 1);
    if (OB8(o, PF_STEP) == 8) SET8(o, PF_STEP, 2);
}

static void platform(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_CCZPLATFORM, OB8(o, OBJ_render) | 4, 3, 0x20, 0x10);
        SET8(o, OBJ_y_radius, 0x10);
        SET16(o, PF_X0, OB16(o, OBJ_x_pos));
        SET16(o, PF_Y0, OB16(o, OBJ_y_pos));
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    uint8_t sub = OB8(o, OBJ_subtype);
#define PF_SOLID() o_platform(o, 0x20 + 10, 0x10, oldx)
    switch (sub) {
    case 0:                                                   /* still */
        SET8(o, PF_SINKS, 1);
        PF_SOLID();
        break;
    case 1: case 2:                                           /* across */
        for (;;) {
            if (!OB8(o, PF_COUNT)) {
                SET8(o, PF_SINKS, 1);
                pf_load(o, sub == 1 ? pf_right : pf_left);
                PF_SOLID();
                break;
            }
            SET16(o, OBJ_x_vel, OB16(o, PF_SPD));
            cd_move(o, (int32_t)OBS16(o, PF_SPD) << 8, 0);
            SET16(o, PF_SPD, OB16(o, PF_SPD) + OB16(o, PF_ACC));
            SET8(o, PF_COUNT, OB8(o, PF_COUNT) - 1);
            if (OB8(o, PF_COUNT)) { PF_SOLID(); break; }
            pf_next(o);
        }
        break;
    case 3: case 4:                                           /* up and down */
        for (;;) {
            PF_SOLID();
            if (!OB8(o, PF_COUNT)) {
                SET8(o, PF_SINKS, 0);
                pf_load(o, sub == 3 ? pf_up : pf_down);
                break;
            }
            cd_move(o, 0, (int32_t)OBS16(o, PF_SPD) << 8);
            SET16(o, PF_SPD, OB16(o, PF_SPD) + OB16(o, PF_ACC));
            SET16(o, OBJ_y_vel, OB16(o, PF_SPD) & 0xFF00);
            SET8(o, PF_COUNT, OB8(o, PF_COUNT) - 1);
            if (OB8(o, PF_COUNT)) break;
            pf_next(o);
        }
        break;
    default:                                                  /* 5-7: rises once stood on */
        PF_SOLID();
        if (OB8(o, PF_PHASE) == 0) {
            if (!OB8(o, PF_COUNT)) {
                SET8(o, PF_SINKS, 1);
                PF_SOLID();
                if (cd_stood_on(o)) SET8(o, PF_COUNT, 30);
            } else {
                SET8(o, PF_COUNT, OB8(o, PF_COUNT) - 1);
                if (!OB8(o, PF_COUNT)) { SET8(o, PF_SINKS, 0); SET8(o, PF_PHASE, 2); }
            }
        } else if (OB8(o, PF_PHASE) == 2) {
            for (;;) {
                if (!OB8(o, PF_COUNT)) { pf_load(o, pf_rise[(sub - 5) % 3]); break; }
                SET16(o, PF_SPD, OB16(o, PF_SPD) + OB16(o, PF_ACC));
                SET16(o, OBJ_y_vel, OB16(o, PF_SPD) & 0xFF00);
                cd_move(o, 0, (int32_t)OBS16(o, PF_SPD) << 8);
                SET8(o, PF_COUNT, OB8(o, PF_COUNT) - 1);
                if (OB8(o, PF_COUNT)) break;
                SET8(o, PF_STEP, OB8(o, PF_STEP) + 1);
                if (OB8(o, PF_STEP) != 3) continue;
                SET16(o, OBJ_y_vel, 0);                       /* (up: it stays there) */
                SET8(o, PF_SINKS, 1);
                SET16(o, PF_Y0, OB16(o, OBJ_y_pos));
                SET8(o, PF_PHASE, 4);
                break;
            }
        }
        break;
    }
#undef PF_SOLID
    if (OB8(o, PF_SINKS)) {                                   /* dai3sub: 4 px down under a player */
        int a = OB8(o, PF_SINK), moved = 1;
        if (!cd_stood_on(o)) { if (a) a -= 8; else moved = 0; }
        else if (a != 0x40) a += 8; else moved = 0;
        if (moved) {
            SET8(o, PF_SINK, a);
            int16_t s, c;
            o_sincos((uint8_t)a, &s, &c);
            SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, PF_Y0) + (int16_t)(((int32_t)s * 0x400) >> 16)));
        }
    }
    if (range_out(o, OB16(o, PF_X0))) return;
    o_draw(o);
}

/* ---- $26 gate ledge -------------------------------------------------------- */
#define GL_Y0    0x38                  /* oVar32 */
#define GL_X0    0x3C                  /* oVar36 */
#define GL_TIME  0x40                  /* oVar3A (set, never read) */
#define GL_OFF   0x41                  /* oVar3B: drawn in by */
#define GL_ONCE  0x42                  /* oVar3C */
static void gate_ledge(uint32_t o)
{
    static const uint8_t times[3] = { 0, 30, 60 };
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_GATELEDGE, OB8(o, OBJ_render) | 4, 3, 0x1C, 8);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, GL_X0, OB16(o, OBJ_x_pos));
        SET16(o, GL_Y0, OB16(o, OBJ_y_pos));
        SET8(o, GL_TIME, times[(OB8(o, OBJ_subtype) & 0x0F) % 3]);
    }
    uint32_t p = RAM_Player_1;                                /* getdair3_move_sub */
    if (OB16(p, OBJ_y_pos) < OB16(o, OBJ_y_pos)) {            /* (above it: out) */
        int v = OB8(o, GL_OFF) - 0x10;
        if (v < 0) {
            SET8(o, GL_OFF, 0);
            if (!(OB8(o, OBJ_subtype) & 0x0F) && !OB8(o, GL_ONCE)) { SET8(o, GL_TIME, 60); SET8(o, GL_ONCE, 0xFF); }
        } else
            SET8(o, GL_OFF, v);
    } else {                                                  /* (else: in) */
        int v = OB8(o, GL_OFF) + 0x10;
        if (v >= 0x40) { v = 0x40; SET8(o, GL_TIME, 8); }
        SET8(o, GL_OFF, v);
    }
    int d = OB8(o, GL_OFF);
    SET16(o, OBJ_x_pos, OB16(o, GL_X0) + (OB8(o, OBJ_subtype) & 0x80 ? d : -d));
    o_platform(o, 0x1C + 10, 8, OB16(o, OBJ_x_pos));          /* (its x speed is 0: it carries no one) */
    o_draw(o);
    range_out(o, OB16(o, GL_X0));
}

/* ---- $27 turret ------------------------------------------------------------ */
#define TU_Y0    0x38                  /* oVar32 */
#define TU_X0    0x3C                  /* oVar36 */
#define TU_TIME  0x40                  /* oVar3A; (a shot) its way: 1 right */
#define TU_OFF   0x41                  /* oVar3B: up by; (a shot) fading */
#define TU_ACC   0x44                  /* oVar3E: (a shot) */
static int fires(void)                          /* (present or bad future) */
{
    int p = cd_period();
    return p == TRI_CD_PRESENT || p == TRI_CD_BAD;
}

static void turret_shot(uint32_t o)
{
    uint32_t anim = o_asset_anim(SCDA_TURRET);
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TURRET, OB8(o, OBJ_render) | 4, 4, 8, 8);
        SET8(o, OBJ_coll_flags, tri_scd_coll(0x98));
        SET16(o, TU_X0, OB16(o, OBJ_x_pos));
        int right = OB16(o, TU_TIME) != 0;
        SET16(o, OBJ_x_vel, right ? 0x100 : -0x100);
        SET16(o, TU_ACC, right ? 0x10 : (uint16_t)-0x10);
        SET8(o, OBJ_anim, right ? 2 : 1);
        o_animate(o, anim);                                   /* (and again below: CD's init does too) */
    }
    SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(o, TU_ACC));
    cd_move(o, (int32_t)OBS16(o, OBJ_x_vel) << 8, 0);
    o_animate(o, anim);
    int16_t d = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, TU_X0));
    if ((d < 0 ? -d : d) >= 0x40) {                           /* (64 px out: harmless, fades, gone) */
        SET8(o, OBJ_coll_flags, 0);
        if (!OB8(o, TU_OFF)) {
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) + 2);
            SET8(o, TU_OFF, 30);
        } else {
            SET8(o, TU_OFF, OB8(o, TU_OFF) - 1);
            if (!OB8(o, TU_OFF)) { o_delete(o); return; }
        }
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    o_draw(o);
}

static void turret_fire(uint32_t o)             /* gandair3_tamaset */
{
    if (!fires()) return;
    for (int k = 0; k < 2; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) continue;
        SET32(a1, OBJ_code, CD_CODE(0x27));
        SET8(a1, OBJ_subtype, 2);
        uint16_t y = OB16(o, OBJ_y_pos);
        SET16(a1, OBJ_y_pos, (uint16_t)((y & 0xFF00) | ((y + 8) & 0xFF)));   /* (addq.b: no carry) */
        SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + (k ? 0x18 : -0x18)));
        if (k) SET8(a1, TU_TIME, 1);
    }
}

static void turret_plate(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    int solid = 1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TURRET, OB8(o, OBJ_render) | 4, 3, 0x10, 0x14);
        SET8(o, OBJ_y_radius, 0x14);
        SET16(o, TU_X0, OB16(o, OBJ_x_pos));
        SET16(o, TU_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_frame, 1);
        solid = 0;
        break;
    case 2:                                                   /* waiting; then blinking */
        if (!OB8(o, TU_TIME)) {
            int16_t d = (int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
            if ((uint16_t)(d < 0 ? -d : d) >= 0x78) break;
            SET8(o, TU_TIME, 60);
        }
        SET8(o, TU_TIME, OB8(o, TU_TIME) - 1);
        if (!OB8(o, TU_TIME)) {
            SET16(o, TU_TIME, 0);
            SET8(o, OBJ_frame, 1);
            SET8(o, OBJ_routine, 4);
            solid = 0;
            break;
        }
        if (fires()) o_animate(o, o_asset_anim(SCDA_TURRET));
        break;
    case 4: {                                                 /* rising: at the top, fire */
        int v = OB8(o, TU_OFF) + 8;
        SET8(o, TU_OFF, v >= 0x20 ? 0x20 : v);
        SET16(o, OBJ_y_pos, OB16(o, TU_Y0) - OB8(o, TU_OFF));
        if (OB8(o, TU_OFF) != 0x20) break;
        turret_fire(o);
        o_solid(o, 0x10 + 10, 0x14, 0x15, OB16(o, OBJ_x_pos));
        solid = 0;
        int stood = 0;
        for (int n = 0; n < 2; n++) {                         /* (Sonic CD: player 1) */
            uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
            if (!(OB8(o, OBJ_status) & bit)) continue;
            uint32_t q = o_player(n);
            SET16(q, OBJ_y_vel, (uint16_t)-0x700);
            SET8(q, OBJ_status, (OB8(q, OBJ_status) | ST_AIR) & ~ST_ONOBJ);
            SET8(q, P_ANIM, 0x10);                            /* (the spring's) */
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
            stood = 1;
        }
        if (!stood) { SET8(o, TU_TIME, 8); SET8(o, OBJ_routine, 6); }
        break;
    }
    case 6: {                                                 /* down again */
        if (OB8(o, TU_TIME)) { SET8(o, TU_TIME, OB8(o, TU_TIME) - 1); break; }
        int v = OB8(o, TU_OFF) - 4;
        SET8(o, TU_OFF, v < 0 ? 0 : v);
        SET16(o, OBJ_y_pos, OB16(o, TU_Y0) - OB8(o, TU_OFF));
        if (!OB8(o, TU_OFF)) { SET8(o, TU_TIME, 60); SET8(o, OBJ_routine, 8); }
        break;
    }
    default:                                                  /* resting */
        if (OB8(o, TU_TIME)) { SET8(o, TU_TIME, OB8(o, TU_TIME) - 1); break; }
        SET8(o, OBJ_routine, 2);
        solid = 0;
        break;
    }
    if (solid) o_solid(o, 0x10 + 10, 0x14, 0x15, OB16(o, OBJ_x_pos));
    o_draw(o);
    range_out(o, OB16(o, TU_X0));
}

static void turret(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (sub == 1) { turret_plate(o); return; }
    if (sub == 2) { turret_shot(o); return; }
    if (!OB8(o, OBJ_routine)) {                               /* the base: its plate */
        o_init(o, SCDA_TURRET, OB8(o, OBJ_render) | 4, 2, 0x10, 0x10);
        SET16(o, TU_X0, OB16(o, OBJ_x_pos));
        SET16(o, TU_Y0, OB16(o, OBJ_y_pos));
        uint32_t a1 = o_alloc();
        if (a1) {                                             /* (else again next frame) */
            SET32(a1, OBJ_code, CD_CODE(0x27));
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 8);
            SET8(a1, OBJ_subtype, 1);
            SET8(o, OBJ_routine, 2);
        }
    }
    o_draw(o);
    range_out(o, OB16(o, TU_X0));
}

/* ---- $2C spike ball ---------------------------------------------------------- */
#define SB_ANGLE 0x30                  /* oVar2A (8.8) */
#define SB_SPEED 0x32                  /* oVar2C */
#define SB_BALLS 0x34                  /* oVar2E..: the balls, 2-4 */
#define SB_X     0x30                  /* (a ball's: oVar2A, its place, 16.16) */
#define SB_Y     0x34                  /* oVar2E */
#define SB_PARENT 0x44                 /* oVar3E */
static void spike_ball(uint32_t o)
{
    if (OB8(o, OBJ_subtype) & 0x80) {                         /* a ball */
        uint32_t par = 0xFF0000u | OB16(o, SB_PARENT);
        if (!OB8(o, OBJ_routine)) {
            SET8(o, OBJ_routine, 2);
            o_init(o, SCDA_SPIKEBALLS, OB8(o, OBJ_render) | 4, 1, 8, 8);
            SET8(o, OBJ_coll_flags, tri_scd_coll(0x8B));
        }
        SET16(o, OBJ_x_pos, OB16(o, SB_X));
        SET16(o, OBJ_y_pos, OB16(o, SB_Y));
        if (r32(par) != CD_CODE(0x2C)) { o_delete(o); return; }
        o_touch(o);
        o_draw(o);
        return;
    }
    int per = cd_period(), n = per == TRI_CD_PAST ? 2 : per == TRI_CD_PRESENT ? 3 : 4;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_SPIKEPIVOT, OB8(o, OBJ_render) | 4, 1, 8, 8);
        SET8(o, OBJ_y_radius, 8);
        SET16(o, SB_SPEED, OB8(o, OBJ_subtype) ? (uint16_t)-0x100 : 0x100);
        for (int k = 0; k < n; k++) {
            uint32_t a1 = o_alloc_after(o);                   /* (FindNextObjSlot) */
            if (!a1) { o_delete(o); return; }
            SET16(a1, SB_PARENT, (uint16_t)o);
            SET16(o, SB_BALLS + k * 2, (uint16_t)a1);
            SET32(a1, OBJ_code, CD_CODE(0x2C));
            SET8(a1, OBJ_subtype, 0xFF);
            SET8(a1, OBJ_width_px, 8);
            SET8(a1, OBJ_y_radius, 8);
        }
    }
    SET16(o, SB_ANGLE, OB16(o, SB_ANGLE) + OB16(o, SB_SPEED));
    int16_t sn, cs;
    o_sincos((uint8_t)(OB16(o, SB_ANGLE) >> 8), &sn, &cs);
    int32_t y16 = ((int32_t)sn << 16) >> 4, x16 = ((int32_t)cs << 16) >> 4;   /* (16 px a step) */
    static const uint8_t mul[4] = { 1, 2, 3, 4 };
    for (int k = 0; k < n; k++) {
        uint32_t a1 = 0xFF0000u | OB16(o, SB_BALLS + k * 2);
        SET32(a1, SB_X, r32(o + OBJ_x_pos) + (uint32_t)(x16 * mul[k]));
        SET32(a1, SB_Y, r32(o + OBJ_y_pos) + (uint32_t)(y16 * mul[k]));
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

void scd_ccz_plat_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x20] = spikes_ccz;
    t[base + 0x21] = swing_block;
    t[base + 0x22] = trap_block;
    t[base + 0x23] = platform;
    t[base + 0x26] = gate_ledge;
    t[base + 0x27] = turret;
    t[base + 0x2C] = spike_ball;
}
