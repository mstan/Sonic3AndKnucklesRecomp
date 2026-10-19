/*
 * s2_obj_htz.c — Sonic 2 Hill Top objects, transcribed from s2disasm:
 * seesaw and its Sol ball (14), zipline lift (16), smashable ground (2F),
 * rising lava (30), lava marker (31), Spiker and its drill (92/93),
 * Rexon (94/96, head and neck 97), Sol (95).
 *
 * S2 variables at objoff_2A-2F would hit S3K's status/subtype, so they live
 * at $30+ here. Object links S2 keeps as RAM words stay RAM words.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define P_JUMPING       0x40
#define P_SPINDASH      0x3D
#define P_TOP_SOLID     0x46
#define P_LRB_SOLID     0x47
#define ST_INAIR        0x02
#define ST_ROLL         0x04
#define ST_ONOBJ        0x08
#define ANI_ROLL        2
#define ANI_SPRING      0x10

/* the earthquake (LevEvents_HTZ / SwScrl_HTZ / Obj30): plane B becomes a
 * second terrain layer whose camera follows the FG camera at an offset */
uint16_t s2_htz_bg_yoff;              /* Camera_BG_Y_offset */
int16_t  s2_htz_bg_xoff;              /* Camera_BG_X_offset */
uint8_t  s2_htz_quake;                /* Screen_Shaking_Flag_HTZ */
int16_t  s2_htz_bgx, s2_htz_bgy;      /* Camera_BG_X/Y_pos while it shakes */
int16_t  s2_htz_dx, s2_htz_dy;        /* Camera_BG_X/Y_pos_diff (ScrollBG) */
uint8_t  s2_htz_dir;                  /* HTZ_Terrain_Direction */
int16_t  s2_htz_delay;                /* HTZ_Terrain_Delay */

static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

static uint32_t ram_obj(uint16_t w) { return 0xFF0000u | w; }

/* MarkObjGone with another x (MarkObjGone2) */
static void gone_x(uint32_t o, uint16_t x)
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void drop_rider(uint32_t o, int n)            /* bclr standing; player in the air */
{
    uint8_t bit = (uint8_t)(ST_P1_STAND << n);
    if (!(OB8(o, OBJ_status) & bit)) return;
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
    uint32_t p = o_player(n);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
}

/* ------------------------------------------------------------------------
 * Object 14 - seesaw (routines 0-4) and the Sol ball on it (6-A)
 * ---------------------------------------------------------------------- */
#define SS_X0     0x30                /* objoff_30: the seesaw's x */
#define SS_Y0     0x34                /* ball: objoff_34, the seesaw's bottom */
#define SS_YV     0x38                /* objoff_38: the faster player's y speed */
#define SS_STATE  0x3A                /* objoff_3A: tilt (0 left down, 1 level, 2 right down) */
#define SS_PARENT 0x3C

static const int16_t ss_yoff[5] = { -8, -28, -47, -28, -8 };   /* Obj14_YOffsets */

static int ss_side(uint32_t o, uint32_t p)          /* 2 left of centre, 0 right, 1 centre */
{
    int d = 2;
    int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
    if (OB16(o, OBJ_x_pos) < OB16(p, OBJ_x_pos)) { d0 = (int16_t)-d0; d = 0; }
    if ((uint16_t)d0 < 8) d = 1;
    return d;
}

static void seesaw(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj14_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_SEESAW, OB8(o, OBJ_render) | 4, 4, 0x30, 0x20);
        SET16(o, SS_X0, OB16(o, OBJ_x_pos));
        if (!OB8(o, OBJ_subtype)) {                     /* its ball */
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                SET32(a1, OBJ_code, S2(0x14));
                SET8(a1, OBJ_routine, 6);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_status, OB8(o, OBJ_status));
                SET32(a1, SS_PARENT, RAMPTR(o));
            }
        }
        if (OB8(o, OBJ_status) & 1) SET8(o, OBJ_frame, 2);
        SET8(o, SS_STATE, OB8(o, OBJ_frame));
    }
    int d1 = OB8(o, SS_STATE);                          /* Obj14_Main */
    int s1 = OB8(o, OBJ_status) & ST_P1_STAND, s2 = OB8(o, OBJ_status) & ST_P2_STAND;
    if (s1) {
        d1 = ss_side(o, RAM_Player_1);
        if (s2) {
            d1 += ss_side(o, RAM_Player_2);
            if (d1 == 3) d1 = 4;
            d1 >>= 1;
        }
    } else if (s2) {
        d1 = ss_side(o, RAM_Player_2);
    } else {
        int16_t a = (int16_t)OB16(RAM_Player_1, OBJ_y_vel), b = (int16_t)OB16(RAM_Player_2, OBJ_y_vel);
        SET16(o, SS_YV, (uint16_t)(b >= a ? b : a));
    }
    int f = OB8(o, OBJ_frame);                          /* Obj14_SetMapping */
    if (f != d1) {
        if (f < d1) f += 2;
        f -= 1;
        SET8(o, OBJ_frame, f);
        SET8(o, SS_STATE, d1);
        SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | ((f & 2) ? 1 : 0));
    }
    uint32_t sl = blob(TRI_BLOB_S2_HTZ);
    o_platform_sloped(o, 0x30, 8, OB16(o, OBJ_x_pos), sl + ((OB8(o, OBJ_frame) & 1) ? 0x62u : 0u));
}

static void ss_ball_animate(uint32_t o)              /* Obj14_Animate */
{
    if (!(o_frame_counter() & 3)) SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) ^ 0x2000);
    SET8(o, OBJ_render, OB8(o, OBJ_render) & ~1);
    if (OB16(RAM_Player_1, OBJ_x_pos) >= OB16(o, OBJ_x_pos)) SET8(o, OBJ_render, OB8(o, OBJ_render) | 1);
}

static void ss_launch(uint32_t ball, uint32_t p)     /* Obj14_LaunchCharacter */
{
    SET16(p, OBJ_y_vel, (uint16_t)-(int16_t)OB16(ball, OBJ_y_vel));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
    SET8(p, P_JUMPING, 0);
    SET8(p, OBJ_anim, ANI_SPRING);
    SET8(p, OBJ_routine, 2);
    SET8(p, P_SPINDASH, 0);
    o_sfx_s2(0xCC);                                        /* SndID_Spring */
}

static void ss_ball(uint32_t o)
{
    uint32_t ss = r32(o + SS_PARENT) & 0xFFFFFF;
    switch (OB8(o, OBJ_routine)) {
    case 6:                                             /* Obj14_Ball_Init */
        SET8(o, OBJ_routine, 8);
        o_init(o, S2A_SOLBALL, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        SET8(o, OBJ_coll_flags, 0x8B);
        SET16(o, SS_X0, OB16(o, OBJ_x_pos));
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x28);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
        SET16(o, SS_Y0, OB16(o, OBJ_y_pos));
        if (OB8(o, OBJ_status) & 1) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x50);
            SET8(o, SS_STATE, 2);
        }
        /* fall through */
    case 8: {                                           /* Obj14_Ball_Main */
        ss_ball_animate(o);
        int d0 = (int8_t)(OB8(o, SS_STATE) - OB8(ss, SS_STATE));
        if (!d0) {                                      /* Obj14_SetBallToRestOnSeeSaw */
            int k = OB8(ss, OBJ_frame);
            int16_t d2 = 0x28;
            if (OB16(o, OBJ_x_pos) < OB16(o, SS_X0)) { d2 = -0x28; k += 2; }
            SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, SS_Y0) + ss_yoff[k]));
            SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, SS_X0) + d2));
            SET16(o, OBJ_y_pos + 2, 0);
            SET16(o, OBJ_x_pos + 2, 0);
            break;
        }
        if (d0 < 0) d0 = -d0;
        int16_t yv = -0x818, xv = -0x114;
        if (d0 != 1) {
            yv = -0xAF0; xv = -0xCC;
            if ((int16_t)OB16(ss, SS_YV) >= 0xA00) { yv = -0xE00; xv = -0xA0; }
        }
        SET16(o, OBJ_y_vel, (uint16_t)yv);
        SET16(o, OBJ_x_vel, (uint16_t)xv);
        if (OB16(o, OBJ_x_pos) < OB16(o, SS_X0)) SET16(o, OBJ_x_vel, (uint16_t)-xv);
        SET8(o, OBJ_routine, 0x0A);
    }
        /* fall through */
    default: {                                          /* Obj14_Ball_Fly */
        ss_ball_animate(o);
        if ((int16_t)OB16(o, OBJ_y_vel) < 0) {
            o_fall(o);
            if ((int16_t)(OB16(o, SS_Y0) - 0x2F) <= (int16_t)OB16(o, OBJ_y_pos)) o_fall(o);
            break;
        }
        o_fall(o);
        int k = OB8(ss, OBJ_frame);
        if (OB16(o, OBJ_x_pos) < OB16(o, SS_X0)) k += 2;
        int16_t d1 = (int16_t)(OB16(o, SS_Y0) + ss_yoff[k]);
        if (d1 > (int16_t)OB16(o, OBJ_y_pos)) break;
        uint8_t t = (int16_t)OB16(o, OBJ_x_vel) < 0 ? 2 : 0;
        SET8(ss, SS_STATE, t);
        SET8(o, SS_STATE, t);
        if (OB8(ss, OBJ_frame) != t) {
            for (int n = 0; n < 2; n++) {
                uint8_t bit = (uint8_t)(ST_P1_STAND << n);
                if (!(OB8(ss, OBJ_status) & bit)) continue;
                SET8(ss, OBJ_status, OB8(ss, OBJ_status) & ~bit);
                ss_launch(o, o_player(n));
            }
        }
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_routine, 8);
        break;
    }
    }
    o_touch(o);
}

static void obj14(uint32_t o)
{
    if (OB8(o, OBJ_routine) >= 6) ss_ball(o);
    else seesaw(o);
    if (!r32(o)) return;
    gone_x(o, OB16(o, SS_X0));
}

/* ------------------------------------------------------------------------
 * Object 16 - zipline lift: slides when stood on, then falls
 * ---------------------------------------------------------------------- */
#define LF_TIMER 0x34                 /* objoff_34 (word) */

static void zipline(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj16_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_ZIPLINE, OB8(o, OBJ_render) | 4, 1, 0x20, 0x40);
        SET8(o, OBJ_frame, 0);
        SET16(o, LF_TIMER, (uint16_t)(OB8(o, OBJ_subtype) << 3));
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* Obj16_Wait */
        if (!o_standing(o)) break;
        SET8(o, OBJ_2nd, 2);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? (uint16_t)-0x200 : 0x200);
        SET16(o, OBJ_y_vel, 0x100);
        break;
    case 2:                                             /* Obj16_Slide */
        if (!(r16(RAM_Level_frame_counter) & 0x0F)) o_sfx_s2(0xE4);   /* SndID_HTZLiftClick */
        o_move(o);
        SET16(o, LF_TIMER, OB16(o, LF_TIMER) - 1);
        if (OB16(o, LF_TIMER)) break;
        SET8(o, OBJ_2nd, 4);
        SET8(o, OBJ_frame, 2);
        SET16(o, OBJ_x_vel, 0);
        SET16(o, OBJ_y_vel, 0);
        {
            uint32_t a1 = o_alloc_after(o);             /* the hook left on the wire */
            if (a1) {
                SET32(a1, OBJ_code, S2(0x1C));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_render, OB8(o, OBJ_render));
                SET8(a1, OBJ_subtype, 6);
            }
        }
        break;
    default:                                            /* Obj16_Fall */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        if ((uint16_t)(r16(RAM_Camera_max_Y_pos) + 224) >= OB16(o, OBJ_y_pos)) break;
        drop_rider(o, 0);
        drop_rider(o, 1);
        SET16(o, OBJ_x_pos, 0x4000);
        break;
    }
    o_platform(o, OB8(o, OBJ_width_px), -0x28, oldx);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 2F - smashable ground: roll onto it to break through
 * ---------------------------------------------------------------------- */
#define SG_A1    0x32                 /* objoff_32/33: the players' animations */
#define SG_A2    0x33
#define SG_CHAIN 0x38                 /* objoff_38: Chain_Bonus_counter */

static void sg_roll(uint32_t p)                      /* loc_23444 */
{
    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
    SET8(p, OBJ_y_radius, 0x0E);
    SET8(p, OBJ_x_radius, 7);
    SET8(p, OBJ_anim, ANI_ROLL);
}

static void sg_drop(uint32_t p)                      /* loc_2345C */
{
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
    SET8(p, OBJ_routine, 2);
}

/* does this player break it? (rolling, and the object's or the player's path allows) */
static int sg_breaks(uint32_t o, uint32_t p, uint8_t anim)
{
    if (anim == ANI_ROLL && ((int8_t)OB8(o, OBJ_subtype) < 0 || OB8(p, P_TOP_SOLID) == 0x0E)) return 1;
    SET8(p, P_TOP_SOLID, 0x0C);
    SET8(p, P_LRB_SOLID, 0x0D);
    return 0;
}

static void smash_ground(uint32_t o)
{
    static const int16_t vel[20] = {
        -0x100, -0x800, 0x100, -0x800, -0xE0, -0x700, 0xE0, -0x700, -0xC0, -0x600,
        0xC0, -0x600, -0xA0, -0x500, 0xA0, -0x500, -0x80, -0x400, 0x80, -0x400,
    };
    static const uint8_t props[5][2] = { { 0x24, 0 }, { 0x20, 2 }, { 0x18, 4 }, { 0x10, 6 }, { 8, 8 } };
    static const uint16_t bonus[4] = { 10, 20, 50, 100 };
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj2F_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_SMASHGND, 4, 4, 0x10, 0x20);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        int k = ((OB8(o, OBJ_subtype) & 0x1E) >> 1) % 5;
        SET8(o, OBJ_frame, props[k][1]);
        SET8(o, OBJ_y_radius, 0x20);
    }
        /* fall through */
    case 2: {                                           /* Obj2F_Main */
        SET16(o, SG_CHAIN, r16(RAM_Chain_bonus_counter));
        SET8(o, SG_A1, OB8(RAM_Player_1, OBJ_anim));
        SET8(o, SG_A2, OB8(RAM_Player_2, OBJ_anim));
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, OB16(o, OBJ_x_pos));
        uint8_t st = OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND);
        if (!st) { o_remember(o); return; }
        if (st == (ST_P1_STAND | ST_P2_STAND)) {
            if (!sg_breaks(o, RAM_Player_1, OB8(o, SG_A1)) &&      /* (either player's roll breaks it: loc_233C0) */
                !sg_breaks(o, RAM_Player_2, OB8(o, SG_A2))) {
                o_remember(o);
                return;
            }
            if (OB8(o, SG_A1) == ANI_ROLL) sg_roll(RAM_Player_1);    /* loc_2343E */
            sg_drop(RAM_Player_1);
            if (OB8(o, SG_A2) == ANI_ROLL) sg_roll(RAM_Player_2);
            sg_drop(RAM_Player_2);
        } else {
            int n = (st & ST_P1_STAND) ? 0 : 1;
            uint32_t p = o_player(n);
            if (!sg_breaks(o, p, OB8(o, n ? SG_A2 : SG_A1))) { o_remember(o); return; }
            sg_roll(p);
            sg_drop(p);
        }
        w16(RAM_Chain_bonus_counter, OB16(o, SG_CHAIN));          /* loc_234A4 */
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));
        int f = OB8(o, OBJ_frame);
        SET8(o, OBJ_frame, f + 1);
        uint32_t maps = r32(o + OBJ_mappings);
        int pieces = (int)tri_rd16(maps + tri_rd16(maps + (uint32_t)(f + 1) * 2u));
        o_smash(o, pieces, vel + f * 2, 0x18, 4);
        uint16_t d2 = r16(RAM_Chain_bonus_counter);     /* SmashableObject_LoadPoints */
        w16(RAM_Chain_bonus_counter, d2 + 2);
        if (d2 >= 6) d2 = 6;
        uint16_t pts = bonus[d2 >> 1];
        if (r16(RAM_Chain_bonus_counter) >= 0x20) { pts = 1000; d2 = 10; }
        D(0) = pts;
        tri_jsr(S3K_HUD_AddToScore);
        o_score(o, d2 >> 1);
        return;
    }
    default:                                            /* Obj2F_Fragment */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 30 - the earthquake's rising lava (invisible, solid, hurts riders)
 * Object 31 - lava marker: an invisible hurt zone
 * ---------------------------------------------------------------------- */
#define LV_X0 0x30
#define LV_Y0 0x32

static void lava_solid(uint32_t o, int d1, int d2, int d3)   /* SolidObject_Always */
{
    o_solid_1p(o, 0, d1, d2, d3, OB16(o, OBJ_x_pos), 1);
    o_solid_1p(o, 1, d1, d2, d3, OB16(o, OBJ_x_pos), 1);
}

static void lava_drop(uint32_t o)                    /* DropOnFloor: riders on real ground step off */
{
    for (int n = 0; n < 2; n++) {
        uint8_t bit = (uint8_t)(ST_P1_STAND << n);
        if (!(OB8(o, OBJ_status) & bit)) continue;
        if (o_floor_dist(o_player(n)) > 0) continue;
        drop_rider(o, n);
    }
}

static void lava_hurt(uint32_t o)                    /* Obj30_HurtSupportedPlayers */
{
    for (int n = 0; n < 2; n++)
        if (OB8(o, OBJ_status) & (ST_P1_STAND << n)) o_hurt_player(o_player(n), o);
}

static void rising_lava(uint32_t o)
{
    static const uint8_t widths[5] = { 0xC0, 0xC0, 0xC0, 0xE0, 0xC0 };
    if (!OB8(o, OBJ_routine)) {                         /* Obj30_Init */
        SET8(o, OBJ_routine, 2);
        SET16(o, LV_Y0, OB16(o, OBJ_y_pos));
        SET16(o, LV_X0, OB16(o, OBJ_x_pos));
        int st = OB8(o, OBJ_subtype);
        SET8(o, OBJ_width_px, widths[(st >> 1) % 5]);
        if (st >= 6) {
            int below = r16(RAM_Camera_Y_pos) >= 0x380;
            if (st == 6 ? !below : below) { o_delete_respawnable(o); return; }
        }
    }
    SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, LV_Y0) + s2_htz_bg_yoff));
    switch (OB8(o, OBJ_subtype)) {
    case 0: case 2:
        lava_solid(o, 0xCB, 0x80, 0x81); lava_drop(o); break;
    case 4:
        lava_solid(o, 0xCB, 0x78, 0x79); lava_drop(o); lava_hurt(o); break;
    case 6:
        lava_solid(o, 0xEB, 0x78, 0x79); lava_drop(o); lava_hurt(o); break;
    default:                                            /* 8: a slope */
        o_solid_sloped(o, 0xCB, 0x2E, OB16(o, OBJ_x_pos), blob(TRI_BLOB_S2_HTZ) + 0xC4);
        lava_drop(o);
        break;
    }
    if (!s2_htz_quake && o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);   /* MarkObjGone3 */
}

static void lava_marker(uint32_t o)
{
    static const uint8_t flags[3] = { 0x96, 0x94, 0x95 };
    if (!OB8(o, OBJ_routine)) {                         /* Obj31_Init */
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_coll_flags, flags[OB8(o, OBJ_subtype) % 3]);
        SET8(o, OBJ_width_px, 0x80);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 0x84);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }   /* (Camera_X_pos_coarse: camera - $80) */
    o_touch(o);
}

/* ------------------------------------------------------------------------
 * Object 92 - Spiker (drill badnik), 93 - its drill
 * ---------------------------------------------------------------------- */
#define SP_TIMER  0x30                /* objoff_2A */
#define SP_THROWN 0x31                /* objoff_2B */
#define SP_COUNT  0x32                /* objoff_2E */
#define SP_PREV   0x33                /* objoff_2F */
#define SP_PARENT 0x34                /* drill: objoff_2C */

static int spiker_sees(uint32_t o)                   /* loc_3703E */
{
    if (OB8(o, SP_THROWN) || !(OB8(o, OBJ_render) & 0x80)) return 0;
    int16_t d2, d3;
    s2_orient(o, NULL, NULL, &d2, &d3);
    if ((uint16_t)(d2 + 0x20) >= 0x40 || (uint16_t)(d3 + 0x80) >= 0x100) return 0;
    SET8(o, SP_PREV, OB8(o, OBJ_routine));
    SET8(o, OBJ_routine, 6);
    SET8(o, SP_COUNT, 0x10);
    return 1;
}

static void spiker(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj92_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_SPIKER, OB8(o, OBJ_render) | 4, 4, 0x10, 0x12);
        SET8(o, OBJ_coll_flags, 0x12);
        SET8(o, SP_TIMER, 0x40);
        SET16(o, OBJ_x_vel, 0x80);
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        return;
    case 2:                                             /* walk */
        if (!spiker_sees(o)) {
            SET8(o, SP_TIMER, OB8(o, SP_TIMER) - 1);
            if ((int8_t)OB8(o, SP_TIMER) < 0) {
                SET8(o, OBJ_routine, 4);
                SET8(o, SP_TIMER, 0x10);
                break;
            }
        }
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_SPIKER));
        break;
    case 4:                                             /* pause, then turn */
        if (spiker_sees(o)) break;
        SET8(o, SP_TIMER, OB8(o, SP_TIMER) - 1);
        if ((int8_t)OB8(o, SP_TIMER) >= 0) break;
        SET8(o, OBJ_routine, 2);
        SET8(o, SP_TIMER, 0x40);
        SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        break;
    default: {                                          /* aim, then throw the drill */
        uint8_t d0 = OB8(o, SP_COUNT);
        if (d0 != 8) { SET8(o, SP_COUNT, d0 - 1); break; }
        uint32_t a1 = o_alloc_after(o);
        if (a1) {
            SET8(o, SP_THROWN, 0xFF);
            SET32(a1, OBJ_code, S2(0x93));
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
            SET16(a1, SP_PARENT, (uint16_t)o);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_frame, 4);
            SET8(o, OBJ_frame, 2);
            SET8(o, OBJ_anim, 1);
        }
        SET8(o, OBJ_routine, OB8(o, SP_PREV));
        break;
    }
    }
    o_touch(o);
    o_remember(o);
}

static void spiker_drill(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj93_Init */
        SET8(o, OBJ_routine, 2);
        uint32_t sp = ram_obj(OB16(o, SP_PARENT));
        uint8_t fr = OB8(o, OBJ_frame);
        o_init(o, S2A_SPIKER, 4 | 0x80 | (OB8(sp, OBJ_render) & 3), 4, 0x10, 0x12);
        SET8(o, OBJ_frame, fr);
        SET8(o, OBJ_coll_flags, 0x92);
        SET16(o, OBJ_y_vel, (OB8(sp, OBJ_render) & 2) ? 0x200 : (uint16_t)-0x200);
        return;
    }
    if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
    SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
    o_move(o);
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 95 - Sol: four fireballs orbit it, then fly off one by one
 * ---------------------------------------------------------------------- */
#define SOL_DIR    0x31               /* objoff_36: orbit direction */
#define SOL_COUNT  0x32               /* objoff_37: fireballs left */
#define FB_PARENT  0x3C               /* fireball: objoff_3C */

static void sol(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj95_Init */
        o_init(o, S2A_SOL, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        SET8(o, OBJ_coll_flags, 0x0B);
        SET16(o, OBJ_x_vel, (uint16_t)-0x40);
        uint8_t ang = 0;
        for (int k = 0; k < 4; k++) {
            uint32_t a1 = o_alloc_after(o);
            if (!a1) break;
            SET8(o, SOL_COUNT, OB8(o, SOL_COUNT) + 1);
            SET32(a1, OBJ_code, S2(0x95));
            SET8(a1, OBJ_routine, 6);
            o_init(a1, S2A_SOLFIRE, 4, 4, 8, 8);
            SET8(a1, OBJ_frame, 3);
            SET8(a1, OBJ_coll_flags, 0x98);
            SET8(a1, OBJ_angle, ang);
            ang = (uint8_t)(ang + 0x40);
            SET32(a1, FB_PARENT, RAMPTR(o));
        }
        int flip = OB8(o, OBJ_status) & 1;
        SET8(o, SOL_DIR, flip ? 0xFF : 1);
        SET8(o, OBJ_routine, OB8(o, OBJ_subtype) + 2);
        SET16(o, OBJ_x_vel, flip ? 0x40 : (uint16_t)-0x40);
        return;
    }
    case 2: {                                           /* Obj95_WaitForPlayer */
        int16_t dx = (int16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        int16_t dy = (int16_t)(OB16(RAM_Player_1, OBJ_y_pos) - OB16(o, OBJ_y_pos));
        if (dx < 0) dx = (int16_t)-dx;
        if (dy < 0) dy = (int16_t)-dy;
        if ((uint16_t)dx < 0xA0 && (uint16_t)dy < 0x50 && !r16(RAM_Debug_placement_mode)) SET8(o, OBJ_anim, 1);
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_SOL));
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) & 3);
        break;
    }
    default:                                            /* loc_37224 */
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_SOLFIRE));
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) & 3);
        break;
    }
    o_touch(o);
    o_remember(o);
}

static void sol_fireball(uint32_t o)
{
    o_animate_s2(o, o_asset_anim(S2A_SOLFIRE));
    if (OB8(o, OBJ_routine) == 6) {                     /* Obj95_FireballUpdate */
        uint32_t s = r32(o + FB_PARENT) & 0xFFFFFF;
        if (r32(s) != S2(0x95) || OB8(s, OBJ_routine) >= 6) { o_delete(o); return; }
        if (OB8(s, OBJ_frame) == 2 && OB8(o, OBJ_angle) == 0x40) {
            SET8(o, OBJ_routine, 8);
            SET8(o, OBJ_anim, 0);
            SET8(s, SOL_COUNT, OB8(s, SOL_COUNT) - 1);
            if (!OB8(s, SOL_COUNT)) SET8(s, OBJ_routine, OB8(s, OBJ_routine) + 2);
            SET16(o, OBJ_x_vel, (OB8(s, OBJ_status) & 1) ? 0x200 : (uint16_t)-0x200);
        } else {                                        /* Obj95_FireballOrbit */
            int16_t sn, cs;
            o_sincos(OB8(o, OBJ_angle), &sn, &cs);
            SET16(o, OBJ_x_pos, (uint16_t)((cs >> 4) + OB16(s, OBJ_x_pos)));
            SET16(o, OBJ_y_pos, (uint16_t)((sn >> 4) + OB16(s, OBJ_y_pos)));
            SET8(o, OBJ_angle, OB8(o, OBJ_angle) + OB8(s, SOL_DIR));
        }
    } else {                                            /* loc_372B8: flying off */
        o_move(o);
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
    }
    o_touch(o);
    o_draw(o);
}

static void obj95(uint32_t o)
{
    if (OB8(o, OBJ_routine) >= 6) sol_fireball(o);
    else sol(o);
}

/* ------------------------------------------------------------------------
 * Object 94/96 - Rexon's body; 97 - its neck and head
 * ---------------------------------------------------------------------- */
#define RX_TIMER  0x3A                /* objoff_2A */
#define RX_LIST   0x30                /* objoff_2C: the 5 segments (words) */
#define HD_IDX    0x3A                /* segment: objoff_2E (0,2..8; 8 = the head) */
#define HD_NEXT   0x3C                /* objoff_30: the next segment */
#define HD_HEAD   0x3E                /* objoff_32: the head */
#define HD_TIMER  0x40                /* objoff_2A */
#define HD_OSC    0x41                /* objoff_2B */
#define HD_DIR    0x42                /* objoff_38 */
#define HD_CNT    0x43                /* objoff_39 */
#define HD_PARENT 0x44                /* objoff_2C */

static const uint8_t rx_wait[6] = { 0x1E, 0x18, 0x12, 0x0C, 6, 0 };     /* byte_3744E */
static const uint8_t rx_osc[5] = { 0x24, 0x20, 0x1C, 0x1A, 0x61 };     /* byte_374BE (+ the next byte) */
static const int8_t rx_wave[64] = {
    15, 0, 15, -1, 15, -1, 15, -2, 15, -3, 15, -4, 14, -4, 14, -5, 14, -6, 14, -6, 13, -7, 13, -8,
    12, -8, 12, -9, 12, -10, 11, -10, 11, -11, 10, -11, 10, -12, 9, -12, 8, -12, 8, -13, 7, -13,
    6, -14, 6, -14, 5, -14, 4, -14, 4, -15, 3, -15, 2, -15, 1, -15, 1, -15,
};
static const int16_t rx_fall[5] = { 0x80, -0x100, 0x100, -0x80, 0x80 }; /* word_37528 */

static void rexon_make_head(uint32_t o, int left)   /* Obj94_CreateHead */
{
    SET8(o, OBJ_routine, 6);
    SET8(o, OBJ_render, (OB8(o, OBJ_render) & ~1) | (left ? 1 : 0));
    SET16(o, OBJ_x_vel, 0);
    SET16(o, OBJ_y_vel, 0);
    for (int k = 0; k < 5; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) return;
        SET32(a1, OBJ_code, S2(0x97));
        SET8(a1, OBJ_render, OB8(o, OBJ_render));
        SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
        SET16(a1, HD_PARENT, (uint16_t)o);
        SET16(o, RX_LIST + k * 2, (uint16_t)a1);
        SET16(a1, HD_IDX, (uint16_t)(k * 2));
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    }
}

static void rexon(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj94_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_REXON, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_frame, 2);
        SET16(o, OBJ_x_vel, (uint16_t)-0x20);
        SET8(o, RX_TIMER, 0x80);
        return;
    case 2: {                                           /* Obj94_WaitForPlayer */
        int d0;
        int16_t d2;
        s2_orient(o, &d0, NULL, &d2, NULL);
        if ((uint16_t)(d2 + 0x60) < 0x100) rexon_make_head(o, d0 != 0);
        uint16_t oldx = OB16(o, OBJ_x_pos);
        SET8(o, RX_TIMER, OB8(o, RX_TIMER) - 1);        /* Obj94_CheckTurnAround */
        if ((int8_t)OB8(o, RX_TIMER) < 0) {
            SET8(o, RX_TIMER, 0x80);
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
        }
        o_move(o);
        o_solid(o, 0x1B, 8, 0x11, oldx);
        break;
    }
    default:                                            /* Obj94_PostCreateHead */
        o_solid(o, 0x1B, 8, 8, OB16(o, OBJ_x_pos));
        break;
    }
    o_remember(o);
}

static int rexon_head_alive(uint32_t o)              /* Obj97_CheckHeadIsAlive */
{
    uint32_t h = ram_obj(OB16(o, HD_HEAD));
    if (r32(h) == S2(0x97) && OB8(h, OBJ_routine) != 0) return 1;
    SET8(o, OBJ_routine, 8);
    SET16(o, OBJ_x_vel, (uint16_t)rx_fall[(OB16(o, HD_IDX) >> 1) % 5]);
    return 0;
}

static void rexon_fire(uint32_t o)                   /* Obj97_FireProjectile */
{
    SET8(o, HD_TIMER, 0x7F);
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x98));
    o_init(a1, S2A_REXONFIRE, 0x84, 4, 4, 4);
    SET8(a1, OBJ_frame, 3);
    SET8(a1, OBJ_coll_flags, 0x98);
    SET8(a1, S2_PROJ_KIND, S2_PROJ_MOVE);
    int right = OB8(o, OBJ_render) & 1;
    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + (right ? 0x10 : -0x10)));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
    SET16(a1, OBJ_x_vel, right ? 0x100 : (uint16_t)-0x100);
    SET16(a1, OBJ_y_vel, 0x80);
}

static void rexon_wave(uint32_t o)                   /* loc_3758A + Obj97_Oscillate */
{
    uint8_t d0 = (uint8_t)(OB8(o, HD_OSC) + OB8(o, HD_DIR));
    SET8(o, HD_OSC, d0);
    uint8_t t = (uint8_t)(d0 - 0x18);
    if (d0 <= 0x18 || t >= 0x10) SET8(o, HD_DIR, (uint8_t)-OB8(o, HD_DIR));
    if (!OB16(o, HD_NEXT)) return;
    uint32_t a1 = ram_obj(OB16(o, HD_NEXT));
    int v = OB8(o, HD_OSC) & 0x7F, k = v & 0x1F;
    int16_t d2 = rx_wave[k * 2], d3 = rx_wave[k * 2 + 1];
    switch ((v >> 4) & 6) {
    case 2: { int16_t t2 = d2; d2 = d3; d3 = (int16_t)-t2; break; }
    case 4: d2 = (int16_t)-d2; d3 = (int16_t)-d3; break;
    case 6: { int16_t t2 = d2; d2 = (int16_t)-d3; d3 = t2; break; }
    default: break;
    }
    SET16(a1, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + d2));
    SET8(a1, OBJ_y_pos + 1, (uint8_t)(OB8(o, OBJ_y_pos + 1) + d3));   /* (S2 moves the low byte only) */
}

static void rexon_head(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj97_Init */
        SET8(o, OBJ_routine, 2);
        uint8_t render = OB8(o, OBJ_render);
        o_init(o, S2A_REXON, render | 4, 4, 8, 8);
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + ((render & 1) ? 0x28 : -0x18)));
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
        SET8(o, HD_DIR, 1);
        uint32_t body = ram_obj(OB16(o, HD_PARENT));
        SET8(o, OBJ_coll_flags, 0x0B);
        int idx = OB16(o, HD_IDX);
        if (idx != 8) {
            SET8(o, OBJ_frame, 1);
            SET8(o, OBJ_coll_flags, 0x8B);
            SET16(o, HD_NEXT, OB16(body, RX_LIST + idx + 2));
        }
        SET16(o, HD_HEAD, OB16(body, RX_LIST + 8));
        SET8(o, HD_TIMER, rx_wait[idx >> 1]);
        SET8(o, HD_CNT, (uint8_t)(idx >> 1));
        return;
    }
    case 2:                                             /* Obj97_InitialWait */
        rexon_head_alive(o);
        if (OB8(o, OBJ_routine) != 2) break;
        SET8(o, HD_TIMER, OB8(o, HD_TIMER) - 1);
        if ((int8_t)OB8(o, HD_TIMER) >= 0) break;
        SET8(o, OBJ_routine, 4);                        /* Obj97_StartRaise */
        SET16(o, OBJ_x_vel, (uint16_t)-0x120);
        SET16(o, OBJ_y_vel, (uint16_t)-0x200);
        SET8(o, HD_TIMER, rx_wait[(8 - OB16(o, HD_IDX)) >> 1]);
        break;
    case 4:                                             /* Obj97_RaiseHead */
        rexon_head_alive(o);
        if (OB8(o, OBJ_routine) != 4) break;
        SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + 0x10);
        SET8(o, HD_TIMER, OB8(o, HD_TIMER) - 1);
        if ((int8_t)OB8(o, HD_TIMER) < 0) {             /* Obj97_StartNormalState */
            SET8(o, OBJ_routine, 6);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, HD_TIMER, 0x20);
            SET8(o, HD_OSC, rx_osc[(OB16(o, HD_IDX) >> 1) % 5]);
            break;
        }
        o_move(o);
        break;
    case 6:                                             /* Obj97_Normal */
        if (!rexon_head_alive(o)) break;
        if (OB16(o, HD_IDX) == 8) {
            SET8(o, HD_TIMER, OB8(o, HD_TIMER) - 1);
            if ((int8_t)OB8(o, HD_TIMER) < 0) rexon_fire(o);
        }
        SET8(o, HD_CNT, OB8(o, HD_CNT) + 1);
        if (!(OB8(o, HD_CNT) & 3)) rexon_wave(o);
        break;
    default:                                            /* Obj97_DeathDrop */
        if ((uint16_t)(r16(RAM_Camera_max_Y_pos) + 224) < OB16(o, OBJ_y_pos)) { o_delete(o); return; }
        o_fall(o);
        break;
    }
    o_touch(o);
    o_remember(o);
}

void s2_obj_htz_reset(void)
{
    s2_htz_bg_yoff = 0; s2_htz_bg_xoff = 0; s2_htz_quake = 0;
    s2_htz_bgx = s2_htz_bgy = s2_htz_dx = s2_htz_dy = 0;
    s2_htz_dir = 0; s2_htz_delay = 0;
}

void s2_obj_htz_register(TriObjFn *t)
{
    t[0x14] = obj14;
    t[0x16] = zipline;
    t[0x2F] = smash_ground;
    t[0x30] = rising_lava;
    t[0x31] = lava_marker;
    t[0x92] = spiker;
    t[0x93] = spiker_drill;
    t[0x94] = rexon;
    t[0x96] = rexon;
    t[0x97] = rexon_head;
    t[0x95] = obj95;
}
