/*
 * s1_obj_syz.c — Sonic 1 Spring Yard Zone objects, transcribed from the REV01
 * object code: spinning light (12), Roller (43), bumper (47), Yadrin (50),
 * floating blocks (56, also SLZ), spiked ball and chain (57), big spiked
 * ball (58).
 *
 * S1 scratch fields that collide with S3K's object layout ($29 collision
 * property, $2C subtype) are moved to $40+; the rest keep their S1 offsets.
 * Bumper and Yadrin use S3K's special collision (sizes $17/$0C count touches
 * in collision_property: +1 player 1, +2 player 2), then apply S1's own
 * React_Special rules for each player that touched.
 */
#include "trilogy_obj.h"

#define OBJ_2nd         0x25          /* ob2ndRout (S1 AnimateSprite convention) */
#define OBJ_double_jump 0x2F
#define OBJ_jumping     0x40

static int chk_del(uint32_t o, uint16_t x)
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return 1; }
    return 0;
}

/* players that touched a special-collision object since its last frame */
static int touched(uint32_t o)
{
    int p = OB8(o, OBJ_coll_prop);
    SET8(o, OBJ_coll_prop, 0);
    return p;
}

/* ------------------------------------------------------------------------
 * Object 12 - spinning light
 * ---------------------------------------------------------------------- */
static void light(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_LIGHT, 4, 6, 0x10, 0x10);
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
        SET8(o, OBJ_anim_timer, 7);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        if (OB8(o, OBJ_frame) >= 6) SET8(o, OBJ_frame, 0);
    }
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 47 - pinball bumper. Hits are counted in the respawn entry (S1
 * v_objstate): no points after the 10th.
 * ---------------------------------------------------------------------- */
static void bumper_hit(uint32_t o, uint32_t p)
{
    D(1) = (uint32_t)(uint16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
    D(2) = (uint32_t)(uint16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos));
    tri_jsr(S3K_GetArcTan);
    int16_t s, c;
    o_sincos((uint8_t)D(0), &s, &c);
    SET16(p, OBJ_x_vel, (int16_t)(((int32_t)c * -0x700) >> 8));
    SET16(p, OBJ_y_vel, (int16_t)(((int32_t)s * -0x700) >> 8));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~0x30);
    SET8(p, OBJ_jumping, 0);
    SET8(o, OBJ_anim, 1);
    o_sfx(0xAA);                               /* sfx_Bumper */
    uint16_t r = OB16(o, OBJ_respawn);
    if (r) {
        uint32_t a2 = 0xFF0000u | r;
        if (r8(a2) >= 0x8A) return;            /* hit 10 times: no points */
        w8(a2, (uint8_t)(r8(a2) + 1));
    }
    D(0) = 1;                                  /* AddPoints: 10 */
    tri_jsr(S3K_HUD_AddToScore);
    o_score(o, 4);
}

static void bumper(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_BUMPER, 4, 1, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0xD7);
    }
    int t = touched(o);
    for (int i = 0; i < 2; i++)
        if (t & (1 << i)) bumper_hit(o, o_player(i));
    o_animate(o, o_asset_anim(S1A_BUMPER));
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 43 - Roller. Hidden until Sonic is $100 past it.
 * ---------------------------------------------------------------------- */
#define RO_TIME   0x30
#define RO_FLAGS  0x32

static void roll_stop(uint32_t o)
{
    if (OB8(o, RO_FLAGS) & 0x80) return;
    uint16_t d0 = (uint16_t)(r16(RAM_Player_1 + OBJ_x_pos) - 0x30);
    if (d0 >= OB16(o, OBJ_x_pos)) return;      /* sub / bcc */
    SET8(o, OBJ_anim, 0);
    SET8(o, OBJ_coll_flags, 0x0E);
    SET16(o, OBJ_x_vel, 0);
    SET16(o, RO_TIME, 120);
    SET8(o, OBJ_2nd, 2);
    SET8(o, RO_FLAGS, OB8(o, RO_FLAGS) | 0x80);
}

static void roller(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {            /* Roll_Main */
        SET8(o, OBJ_y_radius, 0x0E);
        SET8(o, OBJ_x_radius, 8);
        o_fall(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) return;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_ROLLER, 4, 4, 0x10, 0x0E);
        return;
    }
    switch (OB8(o, OBJ_2nd)) {
    case 0: {                                  /* Roll_RollChk */
        uint16_t px = r16(RAM_Player_1 + OBJ_x_pos);
        if (px >= 0x100 && (uint16_t)(px - 0x100) >= OB16(o, OBJ_x_pos)) {
            SET8(o, OBJ_2nd, 4);
            SET8(o, OBJ_anim, 2);
            SET16(o, OBJ_x_vel, 0x700);
            SET8(o, OBJ_coll_flags, 0x8E);
        }
        return;                                /* addq.l #4,sp: not drawn yet */
    }
    case 2:                                    /* Roll_RollNoChk */
        if (OB8(o, OBJ_anim) == 2) { SET8(o, OBJ_2nd, 4); break; }
        SET16(o, RO_TIME, OB16(o, RO_TIME) - 1);
        if ((int16_t)OB16(o, RO_TIME) < 0) {
            SET8(o, OBJ_anim, 1);
            SET16(o, OBJ_x_vel, 0x700);
            SET8(o, OBJ_coll_flags, 0x8E);
        }
        break;
    case 4: {                                  /* Roll_ChkJump */
        roll_stop(o);
        o_move(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < -8 || d1 >= 0x0C) {           /* Roll_Jump */
            SET8(o, OBJ_2nd, 6);
            uint8_t f = OB8(o, RO_FLAGS);
            SET8(o, RO_FLAGS, f | 1);
            if (f & 1) SET16(o, OBJ_y_vel, -0x600);
        } else {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        }
        break;
    }
    default: {                                 /* Roll_MatchFloor */
        o_fall(o);
        if ((int16_t)OB16(o, OBJ_y_vel) < 0) break;
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) break;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET8(o, OBJ_2nd, 4);
        SET16(o, OBJ_y_vel, 0);
        break;
    }
    }
    if (r32(o) != NATIVE_S1_BASE + 0x43 * 4) return;
    o_animate(o, o_asset_anim(S1A_ROLLER));
    int16_t d0 = (int16_t)((OB16(o, OBJ_x_pos) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80));
    if (d0 > 0x280) { o_delete_respawnable(o); return; }
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 50 - Yadrin. Special collision $CC: S1 React_Special .yadrin hurts
 * a player whose feet are within 8px of its top over the spikes; any other
 * touch is a normal enemy hit (S3K Touch_Enemy: spin, invincibility, glide).
 * ---------------------------------------------------------------------- */
#define YAD_TIME  0x30

static int yad_spikes(uint32_t o, uint32_t p)
{
    uint16_t d2 = (uint16_t)(OB16(p, OBJ_x_pos) - 8);
    uint16_t d5 = (uint16_t)(OB8(p, OBJ_y_radius) - 3);
    uint16_t d3 = (uint16_t)(OB16(p, OBJ_y_pos) - d5);
    d5 = (uint16_t)(d5 * 2);
    uint16_t d0 = (uint16_t)(OB16(o, OBJ_y_pos) - 0x10 - d3);
    if ((uint16_t)(d5 - d0) >= 8) return 0;
    d0 = (uint16_t)(OB16(o, OBJ_x_pos) - 4);
    if (OB8(o, OBJ_status) & 1) d0 = (uint16_t)(d0 - 0x10);
    if (d0 >= d2) return (uint16_t)(d0 - d2) <= 0x10;
    d0 = (uint16_t)(d0 - d2);
    return (uint32_t)d0 + 0x18 > 0xFFFF;
}

static int yad_touch(uint32_t o)
{
    int t = touched(o);
    for (int i = 0; i < 2; i++) {
        if (!(t & (1 << i))) continue;
        uint32_t p = o_player(i);
        if (yad_spikes(o, p)) {
            o_hurt_player(p, o);
        } else {
            A(0) = RAMPTR(p);
            A(1) = RAMPTR(o);
            tri_jsr(S3K_Touch_Enemy);
            if (r32(o) != NATIVE_S1_BASE + 0x50 * 4) return 1;   /* destroyed */
        }
    }
    return 0;
}

static void yad_pause(uint32_t o)
{
    SET8(o, OBJ_2nd, 0);
    SET16(o, YAD_TIME, 59);
    SET16(o, OBJ_x_vel, 0);
    SET8(o, OBJ_anim, 0);
}

static void yadrin(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {            /* Yad_Main */
        o_init(o, S1A_YADRIN, 4, 4, 0x14, 0x11);
        SET8(o, OBJ_y_radius, 0x11);
        SET8(o, OBJ_x_radius, 8);
        SET8(o, OBJ_coll_flags, 0xCC);
        o_fall(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) return;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        return;
    }
    if (yad_touch(o)) return;
    if (OB8(o, OBJ_2nd) == 0) {                /* Yad_Move */
        SET16(o, YAD_TIME, OB16(o, YAD_TIME) - 1);
        if ((int16_t)OB16(o, YAD_TIME) < 0) {
            SET8(o, OBJ_2nd, 2);
            SET16(o, OBJ_x_vel, -0x100);
            SET8(o, OBJ_anim, 1);
            uint8_t st = OB8(o, OBJ_status);
            SET8(o, OBJ_status, st ^ 1);
            if (!(st & 1)) SET16(o, OBJ_x_vel, 0x100);
        }
    } else {                                   /* Yad_FixToFloor */
        o_move(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < -8 || d1 >= 0x0C) {
            yad_pause(o);
        } else {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            /* ChkHitLeftRightWall: every 4th frame (frame counter + slot) */
            if (!((o_frame_counter() + o_slot_index(o)) & 3)) {
                int w = OB8(o, OBJ_width_px);
                if ((int16_t)OB16(o, OBJ_x_vel) < 0 ? o_wall_left(o, ~w) < 0 : o_wall_right(o, w) < 0)
                    yad_pause(o);
            }
        }
    }
    o_animate(o, o_asset_anim(S1A_YADRIN));
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 56 - floating blocks (SYZ/SLZ)
 * ---------------------------------------------------------------------- */
#define FB_ORIGY   0x30
#define FB_ORIGX   0x34
#define FB_MOVING  0x38               /* objoff_38 */
#define FB_HEIGHT  0x3A
#define FB_TYPE    0x3C

static int s_obj56;                   /* f_obj56 (REV01: the SYZ3 block has moved) */

void s1_obj_syz_reset(void) { s_obj56 = 0; }

static void fb_respawn_bit(uint32_t o, int set)
{
    uint16_t r = OB16(o, OBJ_respawn);
    if (!r) return;
    uint32_t a2 = 0xFF0000u | r;
    w8(a2, (uint8_t)(set ? (r8(a2) | 1) : (r8(a2) & ~1)));
}

static uint16_t osc_word(int off) { return r16(RAM_Oscillating_table + (uint32_t)off); }

static void fb_move(uint32_t o)
{
    uint8_t sub = OB8(o, OBJ_subtype);
    int flip = OB8(o, OBJ_status) & 1;
    int16_t d0, d1;
    switch (sub & 0x0F) {
    case 0:
        return;
    case 1: d1 = 0x40; d0 = o_osc(0x0A); goto lr;
    case 2: d1 = 0x80; d0 = o_osc(0x1E);
    lr:
        if (flip) d0 = (int16_t)(-d0 + d1);
        SET16(o, OBJ_x_pos, OB16(o, FB_ORIGX) - d0);
        return;
    case 3: d1 = 0x40; d0 = o_osc(0x0A); goto ud;
    case 4: d1 = 0x80; d0 = o_osc(0x1E);
    ud:
        if (flip) d0 = (int16_t)(-d0 + d1);
        SET16(o, OBJ_y_pos, OB16(o, FB_ORIGY) - d0);
        return;
    case 5:                                    /* moves up when a switch is pressed */
        if (!OB8(o, FB_MOVING)) {
            int lz1 = r16(RAM_Current_zone_and_act) == (TRI_S1_LZ << 8) && OB8(o, FB_TYPE) == 3;
            if (lz1) {                         /* LZ1: wind tunnel off until past this door */
                tri_lz_wtunnel_allow = 0;
                if (r16(RAM_Player_1 + OBJ_x_pos) < OB16(o, OBJ_x_pos)) tri_lz_wtunnel_allow = 1;
            }
            if (!(r8(RAM_Level_trigger_array + OB8(o, FB_TYPE)) & 1)) goto y5;
            if (lz1) tri_lz_wtunnel_allow = 0;
            SET8(o, FB_MOVING, 1);
        }
        if (!OB16(o, FB_HEIGHT)) {
            SET8(o, OBJ_subtype, sub + 1);
            SET8(o, FB_MOVING, 0);
            fb_respawn_bit(o, 1);
        } else {
            SET16(o, FB_HEIGHT, OB16(o, FB_HEIGHT) - 2);
        }
    y5:
        d0 = (int16_t)OB16(o, FB_HEIGHT);
        if (flip) d0 = (int16_t)-d0;
        SET16(o, OBJ_y_pos, OB16(o, FB_ORIGY) + d0);
        return;
    case 6:
        if (!OB8(o, FB_MOVING)) {
            if (!(r8(RAM_Level_trigger_array + OB8(o, FB_TYPE)) & 0x80)) goto y6;
            SET8(o, FB_MOVING, 1);
        }
        if ((uint16_t)(OB8(o, OBJ_y_radius) * 2) == OB16(o, FB_HEIGHT)) {
            SET8(o, OBJ_subtype, sub - 1);
            SET8(o, FB_MOVING, 0);
            fb_respawn_bit(o, 0);
        } else {
            SET16(o, FB_HEIGHT, OB16(o, FB_HEIGHT) + 2);
        }
    y6:
        d0 = (int16_t)OB16(o, FB_HEIGHT);
        if (flip) d0 = (int16_t)-d0;
        SET16(o, OBJ_y_pos, OB16(o, FB_ORIGY) + d0);
        return;
    case 7:
        if (!OB8(o, FB_MOVING)) {
            if (!r8(RAM_Level_trigger_array + 0x0F)) return;
            SET8(o, FB_MOVING, 1);
            SET16(o, FB_HEIGHT, 0);
        }
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 1);
        SET16(o, FB_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, FB_HEIGHT, OB16(o, FB_HEIGHT) + 1);
        if (OB16(o, FB_HEIGHT) != 0x380) return;
        s_obj56 = 1;
        SET8(o, FB_MOVING, 0);
        SET8(o, OBJ_subtype, 0);
        return;
    case 0x0C:
        if (!OB8(o, FB_MOVING)) {
            if (!(r8(RAM_Level_trigger_array + OB8(o, FB_TYPE)) & 1)) goto xc;
            SET8(o, FB_MOVING, 1);
        }
        if (!OB16(o, FB_HEIGHT)) {
            SET8(o, OBJ_subtype, sub + 1);
            SET8(o, FB_MOVING, 0);
            fb_respawn_bit(o, 1);
        } else {
            SET16(o, FB_HEIGHT, OB16(o, FB_HEIGHT) - 2);
        }
    xc:
        d0 = (int16_t)OB16(o, FB_HEIGHT);
        if (flip) d0 = (int16_t)(-d0 + 0x80);
        SET16(o, OBJ_x_pos, OB16(o, FB_ORIGX) + d0);
        return;
    case 0x0D:
        if (!OB8(o, FB_MOVING)) {
            if (!(r8(RAM_Level_trigger_array + OB8(o, FB_TYPE)) & 0x80)) goto xd;
            SET8(o, FB_MOVING, 1);
        }
        if (OB16(o, FB_HEIGHT) == 0x80) {
            SET8(o, OBJ_subtype, sub - 1);
            SET8(o, FB_MOVING, 0);
            fb_respawn_bit(o, 0);
        } else {
            SET16(o, FB_HEIGHT, OB16(o, FB_HEIGHT) + 2);
        }
    xd:
        d0 = (int16_t)OB16(o, FB_HEIGHT);
        if (flip) d0 = (int16_t)(-d0 + 0x80);
        SET16(o, OBJ_x_pos, OB16(o, FB_ORIGX) + d0);
        return;
    case 8: d1 = 0x10; d0 = o_osc(0x2A) >> 1; goto square_w8;
    case 9: d1 = 0x30; d0 = o_osc(0x2E); goto square_w9;
    case 0x0A: d1 = 0x50; d0 = o_osc(0x32); goto square_wa;
    case 0x0B: d1 = 0x70; d0 = o_osc(0x36); goto square_wb;
    default:
        return;
    }
    {
        uint16_t d3;
    square_w8: d3 = osc_word(0x2C); goto square;
    square_w9: d3 = osc_word(0x30); goto square;
    square_wa: d3 = osc_word(0x34); goto square;
    square_wb: d3 = osc_word(0x38);
    square:
        if (!d3) SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~3) | ((OB8(o, OBJ_status) + 1) & 3));
        switch (OB8(o, OBJ_status) & 3) {
        case 0:
            SET16(o, OBJ_x_pos, (uint16_t)(d0 - d1 + OB16(o, FB_ORIGX)));
            SET16(o, OBJ_y_pos, (uint16_t)(-d1 + OB16(o, FB_ORIGY)));
            break;
        case 1:
            SET16(o, OBJ_y_pos, (uint16_t)(-(d0 - (d1 - 1)) + OB16(o, FB_ORIGY)));
            SET16(o, OBJ_x_pos, (uint16_t)(d1 + OB16(o, FB_ORIGX)));
            break;
        case 2:
            SET16(o, OBJ_x_pos, (uint16_t)(-(d0 - (d1 - 1)) + OB16(o, FB_ORIGX)));
            SET16(o, OBJ_y_pos, (uint16_t)(d1 + OB16(o, FB_ORIGY)));
            break;
        default:
            SET16(o, OBJ_y_pos, (uint16_t)(d0 - d1 + OB16(o, FB_ORIGY)));
            SET16(o, OBJ_x_pos, (uint16_t)(-d1 + OB16(o, FB_ORIGX)));
            break;
        }
    }
}

static void float_block(uint32_t o)
{
    static const uint8_t var[8][2] = {          /* FBlock_Var: width/2, height/2 */
        { 0x10, 0x10 }, { 0x20, 0x20 }, { 0x10, 0x20 }, { 0x20, 0x1A },
        { 0x10, 0x27 }, { 0x10, 0x10 }, { 8, 0x20 },    { 0x40, 0x10 } };
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        uint8_t sub = OB8(o, OBJ_subtype);
        int v = (sub >> 4) & 7;
        o_init(o, tri_zone() == TRI_S1_LZ ? S1A_FBLOCK_LZ : S1A_FBLOCK, 4, 3, var[v][0], var[v][1]);
        SET8(o, OBJ_y_radius, var[v][1]);
        SET8(o, OBJ_frame, v);
        SET16(o, FB_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, FB_ORIGY, OB16(o, OBJ_y_pos));
        SET16(o, FB_HEIGHT, var[v][1] * 2);
        if (sub == 0x37) {                     /* REV01: the block switch $F moves */
            if (OB16(o, OBJ_x_pos) == 0x1BB8) {
                if (s_obj56) { o_delete(o); return; }
            } else {
                SET8(o, OBJ_subtype, 0);
                sub = 0;
                if (!s_obj56) { o_delete(o); return; }
            }
        }
        int k = (int)(sub & 0x0F) - 8;
        if (tri_zone() != TRI_S1_LZ && k >= 0 && (int16_t)osc_word(0x2C + k * 4) < 0)
            SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        if (sub & 0x80) {
            SET8(o, FB_TYPE, sub & 0x0F);
            SET8(o, OBJ_subtype, 5);
            if (OB8(o, OBJ_frame) == 7) {
                SET8(o, OBJ_subtype, 0x0C);
                SET16(o, FB_HEIGHT, 0x80);
            }
            uint16_t r = OB16(o, OBJ_respawn);
            if (r) {
                uint32_t a2 = 0xFF0000u | r;
                if (r8(a2) & 1) {
                    SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
                    SET16(o, FB_HEIGHT, 0);
                }
            }
        }
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    fb_move(o);
    if (OB8(o, OBJ_render) & 0x80) {
        int d2 = OB8(o, OBJ_y_radius);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, d2, d2 + 1, oldx);
    }
    if (o_out_of_range(o, OB16(o, FB_ORIGX))) {
        if (OB8(o, OBJ_subtype) == 0x37 && OB8(o, FB_MOVING)) { o_draw(o); return; }
        o_delete_respawnable(o);
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 57 - spiked ball and chain (SYZ: every link hurts)
 * ---------------------------------------------------------------------- */
#define SB_CHILD   0x30               /* $30-$37: slot numbers (last = the ball) */
#define SB_ORIGY   0x38
#define SB_ORIGX   0x3A
#define SB_RADIUS  0x3C
#define SB_SPEED   0x3E
#define SB_ANGLE   0x26               /* obAngle word: high byte = angle */
#define SB_COUNT   0x40               /* sball_childs */

static void spike_ball(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        int lz = tri_zone() == TRI_S1_LZ;
        int asset = lz ? S1A_SBALL_LZ : S1A_SBALL;
        o_init(o, asset, 4, 4, 8, 8);
        SET16(o, SB_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, SB_ORIGY, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_coll_flags, lz ? 0 : 0x98);   /* LZ: the chain doesn't hurt */
        uint8_t sub = OB8(o, OBJ_subtype);
        SET16(o, SB_SPEED, (uint16_t)((int16_t)(int8_t)(sub & 0xF0) << 3));
        SET16(o, SB_ANGLE, (uint16_t)((OB8(o, OBJ_status) & 3) << 14));
        int d1 = sub & 7, d3 = d1 << 4, n = 0;
        SET8(o, SB_RADIUS, d3);
        d1--;
        if (d1 >= 0 && (sub & 8)) d1--;
        for (; d1 >= 0 && n < 7; d1--) {
            uint32_t a1 = o_alloc();
            if (!a1) break;
            SET8(o, SB_CHILD + n++, o_slot_index(a1));
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x57 * 4);
            SET8(a1, OBJ_routine, 4);
            o_init(a1, asset, 4, 4, 8, 8);
            SET8(a1, OBJ_coll_flags, OB8(o, OBJ_coll_flags));
            d3 -= 0x10;
            SET8(a1, SB_RADIUS, d3);
            if (lz && !d3) SET8(a1, OBJ_frame, 2);   /* LZ chain anchor */
        }
        SET8(o, SB_CHILD + n, o_slot_index(o));
        SET8(o, SB_COUNT, n);
        if (lz) {                              /* LZ: bigger ball on the end */
            SET8(o, OBJ_coll_flags, 0x8B);
            SET8(o, OBJ_frame, 1);
        }
    }
    /* fall through */
    case 2: {
        SET16(o, SB_ANGLE, OB16(o, SB_ANGLE) + OB16(o, SB_SPEED));
        int16_t s, c;
        o_sincos(OB8(o, SB_ANGLE), &s, &c);
        int16_t y0 = (int16_t)OB16(o, SB_ORIGY), x0 = (int16_t)OB16(o, SB_ORIGX);
        int n = OB8(o, SB_COUNT);
        for (int i = 0; i <= n; i++) {
            uint32_t a1 = o_slot(OB8(o, SB_CHILD + i));
            int r = OB8(a1, SB_RADIUS);
            SET16(a1, OBJ_y_pos, (uint16_t)(y0 + ((r * s) >> 8)));
            SET16(a1, OBJ_x_pos, (uint16_t)(x0 + ((r * c) >> 8)));
        }
        if (o_out_of_range(o, OB16(o, SB_ORIGX))) {
            for (int i = 0; i < n; i++) o_delete(o_slot(OB8(o, SB_CHILD + i)));
            o_delete_respawnable(o);
            return;
        }
        o_touch(o);
        o_draw(o);
        return;
    }
    default:                                   /* SBall_Display: chain link */
        o_touch(o);
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 58 - big spiked ball
 * ---------------------------------------------------------------------- */
static void big_spike_ball(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_BBALL, 4, 4, 0x18, 0x18);
        SET16(o, SB_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, SB_ORIGY, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_coll_flags, 0x86);
        SET16(o, SB_SPEED, (uint16_t)((int16_t)(int8_t)(OB8(o, OBJ_subtype) & 0xF0) << 3));
        SET16(o, SB_ANGLE, (uint16_t)((OB8(o, OBJ_status) & 3) << 14));
        SET8(o, SB_RADIUS, 0x50);
    }
    int flip = OB8(o, OBJ_status) & 1;
    switch (OB8(o, OBJ_subtype) & 7) {
    case 1: {
        int16_t d0 = o_osc(0x0E);
        if (flip) d0 = (int16_t)(-d0 + 0x60);
        SET16(o, OBJ_x_pos, OB16(o, SB_ORIGX) - d0);
        break;
    }
    case 2: {
        int16_t d0 = o_osc(0x0E);
        if (flip) d0 = (int16_t)(-d0 + 0x80);
        SET16(o, OBJ_y_pos, OB16(o, SB_ORIGY) - d0);
        break;
    }
    case 3: {
        SET16(o, SB_ANGLE, OB16(o, SB_ANGLE) + OB16(o, SB_SPEED));
        int16_t s, c;
        o_sincos(OB8(o, SB_ANGLE), &s, &c);
        int r = OB8(o, SB_RADIUS);
        SET16(o, OBJ_y_pos, (uint16_t)((int16_t)OB16(o, SB_ORIGY) + ((r * s) >> 8)));
        SET16(o, OBJ_x_pos, (uint16_t)((int16_t)OB16(o, SB_ORIGX) + ((r * c) >> 8)));
        break;
    }
    default:
        break;
    }
    if (chk_del(o, OB16(o, SB_ORIGX))) return;
    o_touch(o);
    o_draw(o);
}

void s1_obj_syz_register(TriObjFn *t)
{
    t[0x12] = light;
    t[0x43] = roller;
    t[0x47] = bumper;
    t[0x50] = yadrin;
    t[0x56] = float_block;
    t[0x57] = spike_ball;
    t[0x58] = big_spike_ball;
}
