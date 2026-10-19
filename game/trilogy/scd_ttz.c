/*
 * scd_ttz.c — Tidal Tempest (Sonic CD round 4; decomp: R4\GAME4.C,
 * PLAYSUB4.C, AWA.C, HARIR4.C, SW4.C, TOBIRA4.C; the disc's level programs
 * R41A-R42D are the truth).
 *
 * The water is S3K's own: its H-int palette split, the underwater physics,
 * the splashes, breathing bubbles, air countdown and drowning (Sonic CD's
 * plwaterchk and plawa / plawamaster are the Sonic 1 code S3K's grew from:
 * $300 / $6 / $40, jump $380, 30 s of air, the count from 12; its $0B splash
 * is the one S3K's player makes). What is Sonic CD's is the water level, in
 * S3K's own variables (Sonic CD keeps its at the same addresses: $F646 the
 * surface, $F648 the mean, $F64A the target, $F64C the speed):
 *   start    the act's and period's height (WaterHeights: past $300 / $5B0,
 *            present $280 / $530, future $1D0 / $510, act 3 $490), from the
 *            level's info block (scd_level.py), with its underwater palette
 *   each frame (watercnt, from V-int on the disc; here S3K's
 *            Handle_Onscreen_Water_Height) the act's water1chk / 2chk / 3chk
 *            set the target from player 1's place (or hold the water just
 *            under the screen), the mean moves to it at the water speed (1;
 *            act 1's future end 3, act 3 2), and the surface bobs: the mean
 *            + (sin(sysdirec) >> 6), sysdirec += 2 a frame (±4 px)
 *   currents (watercoli, acts 1 and 2): the act's rectangles (the level's
 *            table, scd_level.py) set player 1's speed and float pose while
 *            he is in one, the pad nudging him a pixel; some only once their
 *            switch has been pressed, some reversed while it is toggled
 *   the checkpoint ($13: S3K's star post) saves the mean and the full-screen
 *            flag, as Sonic CD's playsave0 does; a time warp does not carry
 *            the water (game_init: the new period's start height)
 * The S3K water routines are the zone's: scd_ttz_water() replaces them
 * (CheckLevelForWater, LoadWaterPalette, DynamicWaterHeight) and adds the
 * bob (Handle_Onscreen_Water_Height); scd_ttz_hook() runs the currents and
 * keeps the surface strip ($04) in the level.
 *
 * Objects here: $04 wave strip, $05 breakable pole, $20 bubble vent (S3K's
 * bubbler), $22 spikes, $27 floor button, $28 switch door. switchFlags (Sonic
 * CD's $F7E0: bit 7 held, 6 ever pressed, 5 toggled) are scd_ttz_switch[],
 * the round's (scd_ttz_blk*.c read them too).
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int scd_time_warping(void);                /* scd_time.c */

#define TTZ_ROUND       2                  /* (ROUND_INDEX: Tidal Tempest) */

#define P_ROUTINE       0x05
#define P_YRAD          0x1E
#define P_ANIM          0x20
#define P_OBJCTRL       0x2E
#define P_JUMPING       0x40
#define ANI_FLOAT       0x0F               /* (CD's $F: S3K's floating pose) */
#define ANI_HANG        0x11               /* (CD's $11: S3K's hanging pose) */
#define BTN_ABC         0x70
#define RAM_Ctrl_1_pressed_raw 0xFFF605u   /* (Sonic CD reads the pad here: $F604 / $F605) */

/* S3K RAM CheckLevelForWater sets (as trilogy_water.c) */
#define RAM_H_int_jump        0xFFFFF6u
#define RAM_H_int_addr        0xFFFFF8u
#define RAM_V_blank_cycles    0xFFFFD6u
#define RAM_Competition_mode  0xFFFFE8u
#define RAM_Saved_mean_water_level 0xFFFE40u
/* Sonic 1's info block (TD_EXTRA; scd_level.py writes it for Sonic CD too) */
#define S1I_WATER_PAL   0x204
#define S1I_WATER_Y     0x206

#define FM_SWITCH       0xBF               /* (sw4's act_move: the button pressed) */
#define FM_SPIKES       0xB7               /* (harir4_sub: the spikes go in or come out) */

uint8_t scd_ttz_switch[32];                /* switchFlags */

static int s_base;                         /* this round's native slots (scd_obj.c) */
#define CD_CODE(id)     (NATIVE_CD_BASE + (uint32_t)(s_base + (id)) * 4u)

static int ttz(void)
{
    int z = tri_zone();
    return tri_cd_zone(z) && ((z - TRI_CD_BASE) >> 2) == TTZ_ROUND;
}

/* timeZone: 0 past, 1 present, 2 future */
static int time_zone(void)
{
    int p = cd_period();
    return p == TRI_CD_PAST ? 0 : p == TRI_CD_PRESENT ? 1 : 2;
}

static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

static uint32_t info(void) { uint32_t e = tri_dir(); return e ? tri_rd32(e + TD_EXTRA) : 0; }

/* the act has its water (the converter gave it an underwater palette) */
static int ttz_water(void)
{
    uint32_t i = ttz() ? info() : 0;
    return i && tri_rd8(i + S1I_WATER_PAL);
}

/* ======================================================================
 * The water
 * ==================================================================== */
static uint8_t s_sysdirec;                 /* sysdirec ($FF1955): the surface's bob */
static uint16_t s_seen_za;                 /* Saved_zone_and_act as the frame began (a warp's mark) */

/* $44's raft (scd_ttz_blk2.c): the water to rise to y */
void scd_ttz_water_target(int y) { w16(RAM_Target_water_level, (uint16_t)y); }

static void load_water_palette(void)       /* LoadWaterPalette: the act's own */
{
    uint8_t pal = tri_rd8(info() + S1I_WATER_PAL);
    D(0) = pal;
    tri_jsr(S3K_LoadPalette2);
    D(0) = pal;
    tri_jsr(S3K_LoadPalette2_Immediate);
    if (r8(RAM_Last_star_post_hit))
        w8(RAM_Water_full_screen_flag, r8(RAM_Saved_water_full_screen_flag));
}

static void check_water(void)              /* CheckLevelForWater; game_init's water */
{
    w8(RAM_Water_flag, 1);
    w16(RAM_Competition_mode, 0);
    w16(RAM_H_int_jump, 0x4EF9);
    w32(RAM_H_int_addr, r16(RAM_V_blank_cycles) >= 0x1000 ? S3K_HInt4 : S3K_HInt3);
    w32(RAM_Water_palette_data_addr, blob(TRI_BLOB_WATER_XFER));
    uint16_t y = tri_rd16(info() + S1I_WATER_Y);
    w16(RAM_Water_level, y);
    w16(RAM_Mean_water_level, y);
    w16(RAM_Target_water_level, y);
    w8(RAM_Water_entered_counter, 0);
    w8(RAM_Water_full_screen_flag, 0);
    w8(RAM_Water_speed, 1);
    s_sysdirec = 0;
    /* (a time warp restarts the level as at a star post, but Sonic CD's warp
     *  keeps no water: the new period's start height, as game_init sets it.
     *  The warp has rewritten the star post's zone since the last frame.) */
    if (r8(RAM_Last_star_post_hit) && r16(RAM_Saved_zone_and_act) != s_seen_za) {
        w16(RAM_Saved_mean_water_level, y);
        w8(RAM_Saved_water_full_screen_flag, 0);
    }
    load_water_palette();
}

static void water1chk(uint16_t x)          /* act 1 */
{
    uint16_t d1;
    int tz = time_zone();
    if (tz == 0) return;                                      /* (the past: it stays) */
    if (tz == 1) d1 = x >= 0x5E0 ? 0x260 : 0x280;
    else {
        d1 = 0x1D0;
        if (x >= 0x920) {
            d1 = 0x110;
            if (x >= 0x1980) { d1 = 0x240; w8(RAM_Water_speed, 3); }
        }
    }
    w16(RAM_Target_water_level, d1);
}

static void water2chk(uint16_t x, uint16_t y)              /* act 2 */
{
    uint16_t d1 = (uint16_t)(r16(RAM_Camera_Y_pos) + 0xE8);   /* (just under the screen) */
    if (x < 0x200 || (y >= 0x600 && y < 0x6C0 && x < 0x2A0)) goto both;
    switch (time_zone()) {
    case 0:
        d1 = 0x5B0;
        goto both;
    case 1:
        d1 = 0x530;
        if (x < 0xC00) break;
        d1 = 0x780;
        if (x < 0xDC0) break;
        d1 = 0x500;
        if (x >= 0x1380) break;
        if (y >= 0x400) return;                               /* (no change: the raft's, say) */
        d1 = 0x3C0;
        if (x >= 0x1100) break;
        return;
    default:
        d1 = 0x510;
        if (x < 0x400) break;
        d1 = 0x390;
        if (x < 0x1600) break;
        d1 = 0x410;
        if (x < 0x1900) break;
        d1 = 0x500;
        break;
    }
    w16(RAM_Target_water_level, d1);
    return;
both:
    w16(RAM_Mean_water_level, d1);
    w16(RAM_Target_water_level, d1);
}

static void water3chk(uint16_t x)          /* act 3 */
{
    uint16_t d1 = (uint16_t)(r16(RAM_Camera_Y_pos) + 0xE8);
    if (x < 0x7C0) w16(RAM_Mean_water_level, d1);
    else {
        d1 = 0x490;
        if (x >= 0xAF0) { w8(RAM_Water_speed, 2); d1 = 0x5C0; }
    }
    w16(RAM_Target_water_level, d1);
}

static void dynamic_water(void)            /* watercntsub */
{
    uint32_t p = RAM_Player_1;
    uint16_t x = OB16(p, OBJ_x_pos);
    switch (tri_act()) {
    case 0: water1chk(x); break;
    case 1: water2chk(x, OB16(p, OBJ_y_pos)); break;
    default: water3chk(x); break;
    }
    int16_t d1 = r8(RAM_Water_speed);
    uint16_t t = r16(RAM_Target_water_level), m = r16(RAM_Mean_water_level);
    if (t == m) return;
    if (t < m) d1 = (int16_t)-d1;
    w16(RAM_Mean_water_level, (uint16_t)(m + d1));
}

/* S3K's water routines, Tidal Tempest's (trilogy_core.c: 1 = replaced) */
int scd_ttz_water(uint32_t pc)
{
    switch (pc) {
    case TH_CHK_WATER:
        if (!ttz_water()) return 0;
        check_water();
        return 1;
    case TH_WATER_PAL:
        if (!ttz_water()) return 0;
        load_water_palette();
        return 1;
    case TH_DYN_WATER:
        if (!ttz_water()) return 0;
        dynamic_water();
        return 1;
    case TH_WATER_LEVEL: {                                    /* (the surface = the mean + its bob) */
        if (!ttz_water()) return 0;
        uint32_t d0 = D(0);                                   /* (o_sincos runs GetSineCosine: d0) */
        int16_t s, c;
        s_sysdirec = (uint8_t)(s_sysdirec + 2);
        o_sincos(s_sysdirec, &s, &c);
        int8_t bob = (int8_t)(uint8_t)((uint16_t)s >> 6);
        D(0) = (d0 & 0xFFFF0000u) | (uint16_t)(d0 + bob);
        return 0;
    }
    default:
        return 0;
    }
}

/* ---- watercoli: the currents (player 1, acts 1 and 2) ------------------- */
static uint8_t s_current;                  /* watercoliflag */

static void currents(void)
{
    uint32_t p = RAM_Player_1;
    /* (CD: also not while he gives up from boredom, an animation S3K's players lack) */
    if ((OB8(p, P_OBJCTRL) & 1) || !o_player_active(p) || r16(RAM_Debug_placement_mode) || tri_act() >= 2)
        return;
    uint32_t t = o_asset_anim(SCDA_TTZ_CURRENTS);             /* (x1, y1, x2, y2, x speed, y speed, type, switch) */
    if (!t) return;
    uint16_t x = OB16(p, OBJ_x_pos), y = OB16(p, OBJ_y_pos);
    for (; !(tri_rd8(t) & 0x80); t += 14) {
        if (x < tri_rd16(t) || x >= tri_rd16(t + 4) || y < tri_rd16(t + 2) || y >= tri_rd16(t + 6)) continue;
        uint8_t type = tri_rd8(t + 12), *sw = &scd_ttz_switch[tri_rd8(t + 13) & 31];
        if (type == 1 && !(*sw & 0x40)) return;               /* (not till its switch has been pressed) */
        if (OB8(p, P_ROUTINE) >= 4) { s_current = 0; return; }
        s_current = 1;
        SET8(p, P_JUMPING, 0);
        SET16(p, OBJ_x_vel, tri_rd16(t + 8));
        SET16(p, OBJ_y_vel, tri_rd16(t + 10));
        SET8(p, P_ANIM, ANI_FLOAT);
        SET8(p, OBJ_status, OB8(p, OBJ_status) | 2);
        if (type == 2 && (*sw & 0x20)) {                      /* (its switch toggled: the other way) */
            SET16(p, OBJ_x_vel, (uint16_t)-OBS16(p, OBJ_x_vel));
            SET16(p, OBJ_y_vel, (uint16_t)-OBS16(p, OBJ_y_vel));
        }
        uint8_t held = r8(RAM_Ctrl_1_held_logical);
        if (held & 1) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 1);
        if (held & 2) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 1);
        if (held & 4) SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) - 1);
        if (held & 8) SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + 1);
        return;
    }
    if (!s_current) return;
    SET8(p, P_ANIM, 0);                                       /* (out of it: walking) */
    s_current = 0;
}

/* ======================================================================
 * Objects. CD's object variables $30-$3F sit at the same offsets ($2A-$2F
 * at $40-$45); spawned children keep CD's layout ($22, $27: the block
 * groups make them, scd_ttz_blk*.c). SolidObject -> o_solid(o, width + 10,
 * yrad, yrad + 1, x), its rider carried as Sonic CD's x speed carries him.
 * ==================================================================== */
static void dims(uint32_t o, int xr, int yr)                  /* (DrawObject: oWidth, oYRadius) */
{
    SET8(o, OBJ_x_radius, xr);
    SET8(o, OBJ_y_radius, yr);
    SET8(o, OBJ_height_px, yr);
}

/* ---- $04 wave: the water's surface strip, both halves of the screen in
 * turn (the level's own, actwk[32]; kept here, s_wave) --------------------- */
#define WV_HALF         0x40               /* oVar2A */
static uint32_t s_wave;

static void wave(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_asset(o, SCDA_TTZ_WAVE);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
    }
    uint8_t h = (uint8_t)(OB8(o, WV_HALF) + 1);
    if (h >= 2) h = 0;
    SET8(o, WV_HALF, h);
    SET16(o, OBJ_x_pos, r16(RAM_Camera_X_pos) + 0xA0 + (h ? 0x60 : -0x60));
    SET16(o, OBJ_y_pos, r16(RAM_Water_level) + 2);
    o_animate(o, o_asset_anim(SCDA_TTZ_WAVE));
    o_draw(o);
}

static void keep_wave(void)
{
    if (s_wave && r32(s_wave) == CD_CODE(0x04)) return;
    s_wave = 0;
    if (!o_asset_maps(SCDA_TTZ_WAVE)) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, CD_CODE(0x04));
    s_wave = a1;
}

/* ---- $05 pole (act 2): player 1 grabs it in passing and hangs, climbing
 * 24 px up or down, till a jump button or 3 s (subtype set: 1 s); then it
 * breaks (bou) ------------------------------------------------------------- */
#define PO_TIME         0x40               /* oVar2A */
static void pole(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_asset(o, SCDA_TTZ_POLE);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        /* fall through */
    case 2: {
        if (r16(RAM_Debug_placement_mode)) break;
        if ((uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos) + 4) >= 8) break;
        if ((uint16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos) + 0x18) >= 0x30) break;
        SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) | 1);           /* (his own move off; his speeds kept) */
        int left = OBS16(p, OBJ_x_vel) < 0;
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) + (left ? -0x18 : 0x18));
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~1) | left);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | left);
        SET8(p, P_ANIM, ANI_HANG);
        SET16(o, PO_TIME, OB8(o, OBJ_subtype) ? 60 : 180);
        SET8(o, OBJ_routine, 4);
        break;
    }
    case 4: {
        SET16(o, PO_TIME, OB16(o, PO_TIME) - 1);
        if (!OB16(o, PO_TIME) || (r8(RAM_Ctrl_1_pressed_raw) & BTN_ABC)) {
            SET8(p, P_OBJCTRL, OB8(p, P_OBJCTRL) & ~1);
            SET8(o, OBJ_frame, 1);                            /* (broken) */
            SET8(o, OBJ_routine, 6);
            break;
        }
        uint8_t held = r8(RAM_Ctrl_1);
        uint16_t d0 = OB16(o, OBJ_y_pos);
        if (held & 1) {
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 1);
            d0 = (uint16_t)(d0 - 0x18);
            if (d0 >= OB16(p, OBJ_y_pos)) SET16(p, OBJ_y_pos, d0);
        } else if (held & 2) {
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 1);
            d0 = (uint16_t)(d0 + 0x18);
            if (d0 < OB16(p, OBJ_y_pos)) SET16(p, OBJ_y_pos, d0);
        }
        break;
    }
    default:
        break;
    }
    o_draw(o);
    cd_despawn(o, OB16(o, OBJ_x_pos));
}

/* ---- $20 bubble vent: S3K's bubbler is Sonic CD's (awa, Sonic 1's
 * bubbles): subtype $80 a vent (its big bubbles give air), else a bubble -- */
static void bubbles(uint32_t o)
{
    SET32(o, OBJ_code, S3K_Obj_Bubbler);
    SET8(o, OBJ_routine, 0);
}

/* ---- $22 spikes: subtype 0 go in and out 32 px (8 px a frame, a second
 * each way), hurting who stands on them; else still, hurting to the touch.
 * A block group's ride on it (oVar34) ---------------------------------------- */
#define SP_Y0           0x32               /* oVar32: their rest */
#define SP_PARENT       0x34               /* oVar34: what they ride (a RAM word, 0 none) */
#define SP_X0           0x36               /* oVar36: their place (despawn) */
#define SP_DX           0x38               /* oVar38 */
#define SP_DY           0x39               /* oVar39 */
#define SP_WAIT         0x3A               /* oVar3A */
#define SP_OFF          0x3B               /* oVar3B: 0 out .. $20 in */
#define SP_IN           0x3C               /* oVar3C */
static void spikes_move(uint32_t o)
{
    if (OB8(o, SP_WAIT)) {
        SET8(o, SP_WAIT, OB8(o, SP_WAIT) - 1);
        if (OB8(o, SP_WAIT)) return;
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SPIKES);
    }
    if (OB8(o, SP_IN)) {
        if (OB8(o, SP_OFF) < 8) {
            SET8(o, SP_OFF, 0);
            SET8(o, SP_IN, 0);
            SET8(o, SP_WAIT, 60);
        } else SET8(o, SP_OFF, OB8(o, SP_OFF) - 8);
    } else {
        uint8_t v = (uint8_t)(OB8(o, SP_OFF) + 8);
        if (v >= 0x20) {
            v = 0x20;
            SET8(o, SP_IN, 1);
            SET8(o, SP_WAIT, 60);
        }
        SET8(o, SP_OFF, v);
    }
}

static void spikes(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_SPIKES, OB8(o, OBJ_render) | 4, 4, 0x12, 0x0C);
        SET8(o, OBJ_y_radius, 0x0C);
        if (!OB16(o, SP_X0)) SET16(o, SP_X0, OB16(o, OBJ_x_pos));
        SET16(o, SP_Y0, OB16(o, OBJ_y_pos));
        if (sub) {
            SET8(o, OBJ_frame, 1);
            SET8(o, OBJ_coll_flags, tri_scd_coll(0x83));
        }
    }
    if (sub != 1) {
        spikes_move(o);
        SET16(o, OBJ_y_pos, OB16(o, SP_Y0) + OB8(o, SP_OFF));
    }
    o_solid(o, 0x12 + 10, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
    if (OB8(o, OBJ_frame)) o_touch(o);
    else if (!scd_time_warping()) {                           /* (CD: not while the warp's sparks are on) */
        uint8_t st = OB8(o, OBJ_status);
        if (st & ST_P1_STAND) o_spike_hurt(o, RAM_Player_1);
        if (st & ST_P2_STAND) o_spike_hurt(o, RAM_Player_2);
    }
    if (OB16(o, SP_PARENT)) {                                 /* (riding: where it is) */
        uint32_t a1 = 0xFF0000u | OB16(o, SP_PARENT);
        if (!r32(a1)) { o_delete(o); return; }
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) + (int8_t)OB8(o, SP_DX));
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + (int8_t)OB8(o, SP_DY));
    }
    if (cd_despawn(o, OB16(o, SP_X0))) return;
    o_draw(o);
}

/* ---- $27 floor button (switchFlags[subtype]); a block group's ride on it
 * (oVar34: its despawn by that one's oVar36). Player 1 presses it --------- */
#define SW_PARENT       0x34               /* oVar34 */
#define SW_DX           0x38               /* oVar38 */
#define SW_DY           0x39               /* oVar39 */
#define SW_FLAG         0x3C               /* oVar3C: its switchFlags entry */
#define SW_PREV         0x3E               /* oVar3E */
#define SW_NOW          0x3F               /* oVar3F */
static void on_top(uint32_t o, uint32_t p)                    /* (hosei: 1 px into it) */
{
    SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - (int8_t)OB8(o, OBJ_y_radius) - (int8_t)OB8(p, P_YRAD) + 1);
}

static void floor_switch(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, SCDA_TTZ_SWITCH, OB8(o, OBJ_render) | 4, 3, 0x10, 0x0A);
        dims(o, 0x10, 0x0A);
        SET8(o, SW_FLAG, OB8(o, OBJ_subtype) & 31);
    }
    uint16_t x0 = OB16(o, OBJ_x_pos);
    uint32_t par = OB16(o, SW_PARENT) ? 0xFF0000u | OB16(o, SW_PARENT) : 0;
    if (par) {                                                /* (its x speed: how far it went) */
        uint16_t x = (uint16_t)(OB16(par, OBJ_x_pos) + (int8_t)OB8(o, SW_DX));
        SET16(o, OBJ_x_pos, x);
        SET16(o, OBJ_x_vel, (uint16_t)((x - x0) << 8));
        SET16(o, OBJ_y_pos, OB16(par, OBJ_y_pos) + (int8_t)OB8(o, SW_DY));
    }
    SET8(o, SW_PREV, OB8(o, SW_NOW));
    o_solid(o, 0x10 + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, x0);
    int on = (OB8(o, OBJ_status) & ST_P1_STAND) != 0;
    uint8_t *f = &scd_ttz_switch[OB8(o, SW_FLAG)];
    SET8(o, SW_NOW, on ? 0xFF : 0);
    if (on) {
        on_top(o, p);
        *f |= 0xC0;
    } else *f &= (uint8_t)~0x80;
    if (!OB8(o, SW_PREV) && on) {                             /* pressed */
        if (OB8(o, OBJ_render) & 0x80) o_sfx_cd(FM_SWITCH);
        *f ^= 0x20;
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 8);
        if (par) SET8(o, SW_DY, OB8(o, SW_DY) + 4);
        else SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
        SET8(o, OBJ_frame, 1);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) - 4);
    } else if (OB8(o, SW_PREV) && !on) {                      /* let go (CD lifts player 1 8 px, wherever he is) */
        SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 8);
        if (par) SET8(o, SW_DY, OB8(o, SW_DY) - 4);
        else SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
        SET8(o, OBJ_frame, 0);
        SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) + 4);
    }
    SET8(o, OBJ_height_px, OB8(o, OBJ_y_radius));
    if (!par) {
        if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    } else if (o_out_of_range(o, OB16(par, 0x36))) {          /* (its rider's place) */
        o_delete_respawnable(o);
        return;
    }
    o_draw(o);
}

/* ---- $28 door: opens (8 px a frame) while switchFlags[subtype & $F] is
 * held; once open it stays till player 1 is through (from whichever side
 * he was), then shuts. Subtype >> 4: 0 a 128 px floor sliding left, 1 right;
 * 2 a 64 px wall sliding up, 3 a 128 px one down; 4 a 128 px one up, open
 * only while held ------------------------------------------------------------ */
#define DR_SWITCH       0x40               /* oVar2A */
#define DR_TYPE         0x41               /* oVar2B */
#define DR_Y0           0x32               /* oVar32 */
#define DR_X0           0x36               /* oVar36 */
#define DR_PX           0x38               /* oVar38: player 1 as it moved */
#define DR_OPEN         0x3A               /* oVar3A */
#define DR_MAX          0x3B               /* oVar3B */
#define DR_SHUT         0x3C               /* oVar3C */
#define DR_PY           0x3E               /* oVar3E */
static void door_move(uint32_t o)                             /* (the opening, then its place) */
{
    if (OB8(o, DR_SHUT)) {
        int v = OB8(o, DR_OPEN) - 8;
        SET8(o, DR_OPEN, v < 0 ? 0 : v);
    } else {
        uint8_t v = (uint8_t)(OB8(o, DR_OPEN) + 8);
        SET8(o, DR_OPEN, v >= OB8(o, DR_MAX) ? OB8(o, DR_MAX) : v);
    }
    int d = OB8(o, DR_OPEN);
    switch (OB8(o, DR_TYPE)) {
    case 0: SET16(o, OBJ_x_pos, OB16(o, DR_X0) - d); break;
    case 1: SET16(o, OBJ_x_pos, OB16(o, DR_X0) + d); break;
    case 3: SET16(o, OBJ_y_pos, OB16(o, DR_Y0) + d); break;
    default: SET16(o, OBJ_y_pos, OB16(o, DR_Y0) - d); break;
    }
}

/* cl_type1 / cl_type2: player 1 clear of it, on the side away from where
 * he was as it opened: 16 px (a floor, he came from below), else 8 px (CD
 * compares those 8 px as a byte: the distance's low byte) */
static int door_passed(uint32_t o, uint32_t p)
{
    int d;
    if (OB8(o, DR_TYPE) < 2) {
        if (OB16(o, OBJ_y_pos) < OB16(o, DR_PY)) {            /* (he came from below: now above it) */
            d = (int16_t)(OB16(p, OBJ_y_pos) + (int8_t)OB8(p, P_YRAD) - OB16(o, OBJ_y_pos));
            return d < 0 && -d >= 0x10;
        }
        d = (int16_t)(OB16(p, OBJ_y_pos) - (int8_t)OB8(p, P_YRAD) - OB16(o, OBJ_y_pos));
        return d >= 0 && (d & 0xFF) >= 8;
    }
    if (OB16(o, OBJ_x_pos) < OB16(o, DR_PX)) {                /* (he came from the right: now left of it) */
        d = (int16_t)(OB16(p, OBJ_x_pos) + (int8_t)OB8(p, OBJ_x_radius) - OB16(o, OBJ_x_pos));
        return d < 0 && (-d & 0xFF) >= 8;
    }
    d = (int16_t)(OB16(p, OBJ_x_pos) - (int8_t)OB8(p, OBJ_x_radius) - OB16(o, OBJ_x_pos));
    return d >= 0 && (d & 0xFF) >= 8;
}

static void door(uint32_t o)
{
    static const uint8_t types[5][4] = {                      /* width, y radius, opening, frame */
        { 0x40, 8, 0x80, 2 }, { 0x40, 8, 0x80, 2 }, { 8, 0x20, 0x40, 1 }, { 8, 0x40, 0x80, 0 }, { 8, 0x40, 0x80, 0 } };
    uint32_t p = RAM_Player_1;
    if (tri_act() == 1 && cd_period() == TRI_CD_PAST) { o_delete(o); return; }   /* (act 2's past: none at $28) */
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        uint8_t sub = OB8(o, OBJ_subtype), ty = (sub >> 4) & 0x0F;
        const uint8_t *t = types[ty < 5 ? ty : 4];
        o_init(o, SCDA_TTZ_DOOR, OB8(o, OBJ_render) | 4, 2, t[0], t[1]);
        SET8(o, OBJ_y_radius, t[1]);
        SET16(o, DR_X0, OB16(o, OBJ_x_pos));
        SET16(o, DR_Y0, OB16(o, OBJ_y_pos));
        SET8(o, DR_SWITCH, sub & 0x0F);
        SET8(o, DR_TYPE, ty);
        SET8(o, DR_MAX, t[2]);
        SET8(o, OBJ_frame, t[3]);
        break;                                                /* (the first frame: unseen) */
    }
    case 2:                                                   /* opening while held */
        SET8(o, DR_SHUT, (scd_ttz_switch[OB8(o, DR_SWITCH)] & 0x80) ? 0 : 0xFF);
        if (OB8(o, DR_TYPE) != 4 && OB8(o, DR_OPEN)) SET8(o, DR_SHUT, 0);   /* (begun: all the way) */
        SET16(o, DR_PX, OB16(p, OBJ_x_pos));
        SET16(o, DR_PY, OB16(p, OBJ_y_pos));
        door_move(o);
        o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
        if (OB8(o, DR_TYPE) != 4 && OB8(o, DR_OPEN) == OB8(o, DR_MAX)) SET8(o, OBJ_routine, 4);
        break;
    case 4:                                                   /* open: player 1 through? */
        if (door_passed(o, p)) SET8(o, OBJ_routine, 6);
        o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
        break;
    default:                                                  /* shutting */
        SET8(o, DR_SHUT, 0xFF);
        door_move(o);
        if (!OB8(o, DR_OPEN)) SET8(o, OBJ_routine, 2);
        o_solid(o, OB8(o, OBJ_width_px) + 10, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
        break;
    }
    if (cd_despawn(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ======================================================================
 * Registration, hook
 * ==================================================================== */
void scd_ttz_gim_register(TriObjFn *t, int base);             /* scd_ttz_gim.c */
void scd_ttz_badnik_register(TriObjFn *t, int base);          /* scd_ttz_badnik.c */

void scd_ttz_register(TriObjFn *t, int base)
{
    s_base = base;
    t[base + 0x04] = wave;
    t[base + 0x05] = pole;
    t[base + 0x20] = bubbles;
    t[base + 0x22] = spikes;
    t[base + 0x27] = floor_switch;
    t[base + 0x28] = door;
    scd_ttz_gim_register(t, base);
    scd_ttz_badnik_register(t, base);
}

void scd_ttz_hook(uint32_t pc)
{
    if (pc == TH_LEVEL_ENTRY) {                               /* (game_init: switchflag cleared) */
        memset(scd_ttz_switch, 0, sizeof scd_ttz_switch);
        s_current = 0;
        s_wave = 0;
        return;
    }
    if (!tri_dir() || !ttz()) return;
    if (pc == TH_LVL_PRE_SPRITES) {
        s_seen_za = r16(RAM_Saved_zone_and_act);
        return;
    }
    if (pc != TH_LVL_POST_SPRITES) return;
    currents();
    keep_wave();
}
