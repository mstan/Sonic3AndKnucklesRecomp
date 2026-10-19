/*
 * s2_obj_cnz.c — Sonic 2 Casino Night objects, transcribed from s2disasm:
 * round bumper (44), conveyor (72), pinball mode switch (84), launcher
 * spring (85), flipper (86), Crawl (C8), flashing blocks (D2), spike prize
 * (D3), big moving block (D4), elevator (D5), cage / slot machine (D6, with
 * SlotMachine), hexagon bumper (D7), saucer bumper (D8), ring prize (DC).
 *
 * S2 variables at objoff_2A-2F would hit S3K's status/subtype, so they live
 * at $40+ here. Player fields: obj_control $2E, pinball_mode = S3K's
 * spin_dash_flag $3D (S3K's own auto-spin uses it the same way), jumping $40,
 * move_lock $32.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"
#include "genesis_machine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define P_OBJCTRL       0x2E
#define P_PINBALL       0x3D
#define P_JUMPING       0x40
#define P_MOVELOCK      0x32
#define ST_INAIR        0x02
#define ST_ROLL         0x04
#define ST_ONOBJ        0x08
#define ST_ROLLJUMP     0x10
#define ST_PUSH         0x20
#define MS_COUNT        0x16            /* multi-draw: child count, children at $18 + 6n */
#define MS_SUB          0x18
#define BTN_ABC         0x70            /* button_A|B|C */
#define ANI_ROLL        2

static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

static uint16_t ctrl(int n)                          /* Ctrl_1_Logical / Ctrl_2: held<<8 | pressed */
{
    return r16(n ? RAM_Ctrl_2_held_logical : RAM_Ctrl_1_held_logical);
}

static void add_points(int pts) { D(0) = (uint32_t)pts; tri_jsr(S3K_HUD_AddToScore); }

static void roll_up(uint32_t p)                      /* curl the player into a ball */
{
    SET8(p, OBJ_y_radius, 0x0E);
    SET8(p, OBJ_x_radius, 7);
    SET8(p, OBJ_anim, ANI_ROLL);
}

static void bounce_end(uint32_t p)                   /* the bumpers' common tail */
{
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~(ST_ROLLJUMP | ST_PUSH));
    SET8(p, P_JUMPING, 0);
}

/* Obj44_BumpCharacter / Crawl: off the bumper's centre at speed $700 */
static void bump_away(uint32_t o, uint32_t p)
{
    uint8_t a = o_atan((int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)),
                       (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos)));
    a = (uint8_t)(a + (r8(RAM_Level_frame_counter) & 3));   /* (the counter's high byte) */
    int16_t s, c;
    o_sincos(a, &s, &c);
    SET16(p, OBJ_x_vel, (uint16_t)(((int32_t)c * -0x700) >> 8));
    SET16(p, OBJ_y_vel, (uint16_t)(((int32_t)s * -0x700) >> 8));
    bounce_end(p);
}

/* ------------------------------------------------------------------------
 * Object 44 - round bumper
 * ---------------------------------------------------------------------- */
static void round_bumper(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_ROUNDBUMP, 4, 1, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0xD7);
    }
    uint8_t prop = OB8(o, OBJ_coll_prop);
    if (prop) {
        for (int n = 0; n < 2; n++) {
            if (!(prop & (1 << n))) continue;
            uint32_t p = o_player(n);
            bump_away(o, p);
            SET8(o, OBJ_anim, 1);
            o_sfx_s2(0xB4);                                /* SndID_Bumper */
            uint16_t r = OB16(o, OBJ_respawn);          /* 10 points for the first ten hits */
            if (r) {
                uint8_t v = r8(0xFF0000u | r);
                if (v >= 0x8A) continue;
                w8(0xFF0000u | r, v + 1);
            }
            add_points(1);
            o_score(o, 4);
        }
        SET8(o, OBJ_coll_prop, 0);
    }
    o_animate_s2(o, o_asset_anim(S2A_ROUNDBUMP));
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 72 - conveyor belt (invisible): carries grounded players along
 * ---------------------------------------------------------------------- */
#define CV_SPEED 0x36
#define CV_HALFW 0x38
#define CV_RANGE 0x3C

static void conveyor(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET16(o, CV_RANGE, (OB8(o, OBJ_subtype) & 0x80) ? 0x70 : 0x30);
        SET8(o, CV_HALFW, (uint8_t)((OB8(o, OBJ_subtype) & 0x7F) << 4));
        SET16(o, CV_SPEED, (OB8(o, OBJ_status) & 1) ? (uint16_t)-2 : 2);
    }
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        uint16_t d2 = OB8(o, CV_HALFW);
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + d2) >= (uint16_t)(d2 * 2)) continue;
        uint16_t r = OB16(o, CV_RANGE);
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + r) >= r) continue;
        if (OB8(p, OBJ_status) & ST_INAIR) continue;
        SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + OB16(o, CV_SPEED));
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);   /* MarkObjGone3 */
}

/* ------------------------------------------------------------------------
 * Object 84 - pinball mode switch (invisible): crossing it sets or clears
 * "stay in a ball" (S3K's spin_dash_flag)
 * ---------------------------------------------------------------------- */
#define PB_RANGE 0x32
#define PB_SIDE  0x34                 /* +0 P1, +1 P2: which side the player is on */

static void pinball_roll(uint32_t p)                 /* loc_212C4 */
{
    if (OB8(p, OBJ_status) & ST_ROLL) return;
    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
    roll_up(p);
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 5);
    o_sfx_s2(0xBE);                                        /* SndID_Roll */
}

static void pinball_switch(uint32_t o)
{
    static const uint16_t ranges[4] = { 0x20, 0x40, 0x80, 0x100 };
    int vertical;
    if (!OB8(o, OBJ_routine)) {                         /* Obj84_Init */
        uint8_t d0 = OB8(o, OBJ_subtype);
        vertical = (d0 & 4) != 0;
        SET8(o, OBJ_routine, vertical ? 4 : 2);
        SET16(o, PB_RANGE, ranges[d0 & 3]);
        for (int n = 0; n < 2; n++) {
            uint32_t p = o_player(n);
            uint16_t me = vertical ? OB16(o, OBJ_y_pos) : OB16(o, OBJ_x_pos);
            uint16_t pv = vertical ? OB16(p, OBJ_y_pos) : OB16(p, OBJ_x_pos);
            if (me < pv) SET8(o, PB_SIDE + n, 1);
        }
    }
    vertical = OB8(o, OBJ_routine) == 4;
    if (!r16(RAM_Debug_placement_mode)) {
        for (int n = 0; n < 2; n++) {
            uint32_t p = o_player(n);
            if (n && r16(RAM_Tails_CPU_routine) == 4) break;   /* Tails flying in */
            uint16_t me = vertical ? OB16(o, OBJ_y_pos) : OB16(o, OBJ_x_pos);
            uint16_t pv = vertical ? OB16(p, OBJ_y_pos) : OB16(p, OBJ_x_pos);
            uint16_t ac = vertical ? OB16(o, OBJ_x_pos) : OB16(o, OBJ_y_pos);
            uint16_t pa = vertical ? OB16(p, OBJ_x_pos) : OB16(p, OBJ_y_pos);
            uint16_t r = OB16(o, PB_RANGE);
            int was = OB8(o, PB_SIDE + n);
            if (!was ? me > pv : me <= pv) continue;    /* hasn't crossed */
            SET8(o, PB_SIDE + n, was ? 0 : 1);
            if (pa < (uint16_t)(ac - r) || pa >= (uint16_t)(ac + r)) continue;
            int flip = (OB8(o, OBJ_render) & 1) != 0;          /* both types test x_flip */
            int enable = was ? flip : !flip;
            if (enable) { SET8(p, P_PINBALL, 1); pinball_roll(p); }
            else SET8(p, P_PINBALL, 0);
        }
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

/* ------------------------------------------------------------------------
 * Object 85 - launcher spring: hold a button to wind it, let go to fly.
 * The plunger head is the object's own frame, the spring base a child sprite.
 * ---------------------------------------------------------------------- */
#define LS_X0    0x2E                 /* objoff_2E (word, diagonal) - S3K subtype is $2C */
#define LS_STEP  0x32                 /* objoff_32 */
#define LS_BLINK 0x33                 /* objoff_33 */
#define LS_Y0    0x34                 /* objoff_34 */
#define LS_STATE 0x36                 /* +0 P1, +1 P2 */
#define LS_PULL  0x38                 /* objoff_38 (word) */
#define LS_PRESS 0x3A                 /* objoff_3A */
#define LS_BASEX 0x40
#define LS_BASEY 0x42
#define LS_BASEF 0x44

static void launcher_player(uint32_t o, int n, int diag)
{
    uint32_t p = o_player(n);
    uint8_t st = OB8(o, LS_STATE + n);
    uint16_t pad = ctrl(n);
    uint8_t sbit = (uint8_t)(ST_P1_STAND << n);
    if (!st) {
    grab:
        if (r16(RAM_Debug_placement_mode) || (int16_t)OB16(p, OBJ_y_vel) < 0) return;
        if (diag) o_solid_1p(o, n, 0x23, 8, 5, OB16(o, OBJ_x_pos), 1);
        else o_solid_1p(o, n, 0x23, 0x20, 0x1D, OB16(o, OBJ_x_pos), 1);
        if (!(OB8(o, OBJ_status) & sbit)) {
            if (!diag || !(OB8(o, OBJ_status) & (ST_P1_PUSH << n))) return;
            SET8(o, OBJ_status, OB8(o, OBJ_status) | sbit);
        }
        SET8(p, P_OBJCTRL, 0x81);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) + (diag ? 0x13 : 0));
        if (diag) SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x13);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_ground_vel, 0);
        SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
        roll_up(p);
        SET8(o, LS_STATE + n, st + 1);
        return;
    }
    if (OB8(p, OBJ_routine) >= 4) return;               /* (REV01) hurt or dead */
    if (st == 1) {                                      /* held in the launcher */
        if (!(OB8(p, OBJ_render) & 0x80)) {             /* off screen: let go */
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~sbit);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
            SET8(p, OBJ_routine, 2);
            SET8(p, P_OBJCTRL, 0);
            SET8(o, LS_STATE + n, 0);
            return;
        }
        if ((pad >> 8) & BTN_ABC && !OB8(o, LS_PRESS)) {   /* winding */
            SET8(o, LS_PRESS, 1);
            SET8(o, LS_STEP, OB8(o, LS_STEP) - 1);
            if ((int8_t)OB8(o, LS_STEP) < 0) {
                SET8(o, LS_STEP, 3);
                if (OB16(o, LS_PULL) != (diag ? 0x1C : 0x20)) SET16(o, LS_PULL, OB16(o, LS_PULL) + 1);
            }
            SET8(o, LS_BLINK, OB8(o, LS_BLINK) - 1);
            if ((int8_t)OB8(o, LS_BLINK) < 0) {
                SET8(o, LS_BLINK, (uint8_t)((uint16_t)-(int16_t)(OB16(o, LS_PULL) - (diag ? 0x1C : 0x20)) >> 1));
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 4);
            } else {
                SET8(o, OBJ_frame, 1);
            }
        }
        if (diag) {
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x13);
            SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x13);
        } else {
            SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x2E);
        }
        return;
    }
    /* state 2: launch */
    SET8(o, LS_STATE + n, 0);
    if (!(OB8(o, OBJ_status) & sbit)) { if (diag) return; goto grab; }
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~sbit);
    if (!diag) {
        SET16(p, OBJ_y_vel, (uint16_t)-(int16_t)((OB16(o, LS_PULL) + 0x10) << 7));
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_ground_vel, 0x800);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
    } else {
        int16_t d0 = (int16_t)((OB16(o, LS_PULL) + 4) << 7);
        SET16(p, OBJ_x_vel, (uint16_t)d0);
        SET16(p, OBJ_y_vel, (uint16_t)-d0);
        SET16(p, OBJ_ground_vel, 0x800);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
        if (OB8(o, OBJ_subtype) & 0x80) {               /* launched along the floor */
            SET16(p, OBJ_ground_vel, (uint16_t)d0);
            SET8(p, OBJ_status, OB8(p, OBJ_status) & ~(1 | ST_INAIR));
            SET8(p, OBJ_angle, (uint8_t)-0x20);
        }
    }
    SET8(p, OBJ_routine, 2);
    SET8(p, P_OBJCTRL, 0);
    o_sfx_s2(0xE2);                                        /* SndID_CNZLaunch */
}

static void launcher(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }   /* the spring base (child sprite) */
    int diag = OB8(o, OBJ_subtype) != 0;
    if (!OB8(o, OBJ_routine)) {                         /* Obj85_Init */
        SET8(o, OBJ_routine, diag ? 4 : 2);
        o_init(o, diag ? S2A_LAUNCHD : S2A_LAUNCHV, OB8(o, OBJ_render) | 4, 4, 0x18, 0x30);
        SET8(o, OBJ_frame, 1);
        SET16(o, LS_X0, OB16(o, OBJ_x_pos));
        SET16(o, LS_Y0, OB16(o, OBJ_y_pos));
        uint32_t base = o_alloc_after(o);               /* sub2: the base, never moves */
        if (base) {
            SET32(base, OBJ_code, S2(0x85));
            o_asset(base, diag ? S2A_LAUNCHD : S2A_LAUNCHV);
            SET8(base, OBJ_render, (OB8(o, OBJ_render) & 3) | 4 | 0x40);
            SET16(base, OBJ_priority, OB16(o, OBJ_priority));
            SET8(base, OBJ_width_px, diag ? 0x20 : 0x18);
            SET8(base, OBJ_height_px, 0x30);
            SET16(base, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(base, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(base, OBJ_frame, 0);                   /* the launcher draws the main sprite */
            SET16(base, MS_COUNT, 1);
            uint32_t c = base + MS_SUB;
            w16(c, OB16(o, OBJ_x_pos));
            w16(c + 2, (uint16_t)(OB16(o, OBJ_y_pos) + (diag ? 0 : 0x20)));
            w8(c + 5, 2);
        }
        SET32(o, LS_BASEX, base ? RAMPTR(base) : 0);
    }
    uint32_t base = r32(o + LS_BASEX) & 0xFFFFFF;
    SET8(o, LS_PRESS, 0);
    if (diag) {
        uint16_t d1 = OB16(o, LS_PULL) >> 1;
        SET16(o, OBJ_x_pos, OB16(o, LS_X0) - d1);
        SET16(o, OBJ_y_pos, OB16(o, LS_Y0) + d1);
    } else {
        SET16(o, OBJ_y_pos, OB16(o, LS_Y0) + OB16(o, LS_PULL));
    }
    if (base) w8(base + MS_SUB + 5, OB16(o, LS_PULL) >= 0x10 ? 3 : 2);
    launcher_player(o, 0, diag);
    launcher_player(o, 1, diag);
    if (OB16(o, LS_STATE)) {
        if (OB16(o, LS_PULL)) {
            uint16_t held = 0;
            if (OB8(o, LS_STATE) == 1) held |= ctrl(0);
            if (OB8(o, LS_STATE + 1) == 1) held |= ctrl(1);
            if (!((held >> 8) & BTN_ABC)) SET16(o, LS_STATE, 0x202);
        }
    } else {
        SET8(o, OBJ_frame, 1);
        uint16_t pull = OB16(o, LS_PULL);
        SET16(o, LS_PULL, pull >= 4 ? pull - 4 : 0);
    }
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) {
        if (base) o_delete(base);
        o_delete_respawnable(o);
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 86 - flipper (upward: hold a ball, flip with a button; sideways:
 * a kicker)
 * ---------------------------------------------------------------------- */
#define FL_STATE 0x36                 /* +0 P1, +1 P2 */
#define FL_FLIP  0x38

static void flipper_launch(uint32_t o, int n)        /* loc_2B290 */
{
    uint8_t sbit = (uint8_t)(ST_P1_STAND << n);
    if (!(OB8(o, OBJ_status) & sbit)) return;
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~sbit);
    uint32_t p = o_player(n);
    int16_t d0 = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if (OB8(o, OBJ_status) & 1) d0 = (int16_t)-d0;
    d0 = (int16_t)(d0 + 0x23);
    int16_t d2 = d0;
    if ((uint16_t)d2 >= 0x40) d2 = 0x40;
    d2 = (int16_t)-((d2 << 5) + 0x800);
    int16_t s, c;
    o_sincos((uint8_t)((d0 >> 2) + 0x40), &s, &c);
    int16_t yv = (int16_t)(((int32_t)s * d2) >> 8), xv = (int16_t)(((int32_t)c * d2) >> 8);
    SET16(p, OBJ_y_vel, (uint16_t)yv);
    if (OB8(o, OBJ_status) & 1) xv = (int16_t)-xv;
    SET16(p, OBJ_x_vel, (uint16_t)xv);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
    SET8(p, OBJ_routine, 2);
    SET8(p, P_OBJCTRL, 0);
    SET8(o, OBJ_anim, 1);
    SET8(o, FL_STATE + n, 0);
    o_sfx_s2(0xE3);                                        /* SndID_Flipper */
}

static void flipper(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj86_Init */
        o_init(o, S2A_FLIPPER, OB8(o, OBJ_render) | 4, 4, 0x18, 0x18);
        SET8(o, OBJ_routine, OB8(o, OBJ_subtype) ? 4 : 2);
        if (OB8(o, OBJ_subtype)) SET8(o, OBJ_anim, 2);
    }
    if (OB8(o, OBJ_routine) == 2) {                     /* Obj86_UpwardsType */
        if (!r16(RAM_Debug_placement_mode)) {
            uint32_t slopes = blob(TRI_BLOB_S2_SLOPES);
            int fr = OB8(o, OBJ_frame);
            o_solid_sloped(o, 0x23, 6, OB16(o, OBJ_x_pos), slopes + 0x50 + (uint32_t)(fr > 2 ? 2 : fr) * 0x24u);
            for (int n = 0; n < 2; n++) {
                uint32_t p = o_player(n);
                uint8_t sbit = (uint8_t)(ST_P1_STAND << n);
                if (!OB8(o, FL_STATE + n)) {            /* loc_2B20A: catch a player */
                    if (!(OB8(o, OBJ_status) & sbit)) continue;
                    SET8(p, P_OBJCTRL, 1);
                    roll_up(p);
                    if (!(OB8(p, OBJ_status) & ST_ROLL)) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 5);
                    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
                    SET8(o, FL_STATE + n, 1);
                    continue;
                }
                if (ctrl(n) & BTN_ABC) { SET8(o, FL_FLIP, 1); continue; }   /* pressed: flip */
                if (!(OB8(o, OBJ_status) & sbit)) {
                    SET8(p, P_OBJCTRL, 0);
                    SET8(o, FL_STATE + n, 0);
                    continue;
                }
                int16_t d1 = (int16_t)(OB8(o, OBJ_frame) - 1);   /* roll down the flipper */
                SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
                if (!(OB8(o, OBJ_status) & 1)) {
                    d1 = (int16_t)-d1;
                    SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
                }
                SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + d1);
                SET16(p, OBJ_x_vel, (uint16_t)(d1 << 8));
                SET16(p, OBJ_ground_vel, (uint16_t)(d1 << 8));
                SET16(p, OBJ_y_vel, 0);
            }
            if (OB8(o, FL_FLIP)) {
                SET8(o, FL_FLIP, 0);
                flipper_launch(o, 1);
                flipper_launch(o, 0);
            }
            o_animate_s2(o, o_asset_anim(S2A_FLIPPER));
        }
    } else {                                            /* Obj86_HorizontalType */
        for (int n = 0; n < 2; n++) {
            o_solid_1p(o, n, 0x13, 0x18, 0x19, OB16(o, OBJ_x_pos), 1);
            if (!(OB8(o, OBJ_status) & (ST_P1_PUSH << n))) continue;
            uint32_t p = o_player(n);
            SET16(o, OBJ_anim, 0x0300);
            SET16(p, OBJ_x_vel, (uint16_t)-0x1000);
            SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + 8);
            SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
            if (OB16(o, OBJ_x_pos) < OB16(p, OBJ_x_pos)) {
                SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
                SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) - 0x10);
                SET16(p, OBJ_x_vel, 0x1000);
                SET16(o, OBJ_anim, 0x0400);
            }
            SET16(p, P_MOVELOCK, 0x0F);
            SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
            roll_up(p);
            if (!(OB8(p, OBJ_status) & ST_ROLL)) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 5);
            SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
            o_sfx_s2(0xE3); /* SndID_Flipper */
        }
        o_animate_s2(o, o_asset_anim(S2A_FLIPPER));
    }
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object C8 - Crawl (the bouncer badnik): its shield bounces you off
 * ---------------------------------------------------------------------- */
#define CR_TIMER 0x30                 /* objoff_2A (word) */
#define CR_PREV  0x32                 /* objoff_2C */

static void crawl_near(uint32_t o)                   /* loc_3D416 */
{
    int16_t d2, d3;
    s2_orient(o, NULL, NULL, &d2, &d3);
    if ((uint16_t)(d2 + 0x40) >= 0x80 || (uint16_t)(d3 + 0x40) >= 0x80) return;
    SET8(o, CR_PREV, OB8(o, OBJ_routine));
    SET8(o, OBJ_routine, 6);
    SET8(o, OBJ_frame, 0);
}

static void crawl_bounce(uint32_t o, uint32_t p)     /* loc_3D3A4 */
{
    SET8(o, OBJ_frame, (OB8(p, OBJ_status) & ST_INAIR) ? 3 : 2);
    bump_away(o, p);
    o_sfx_s2(0xB4); /* SndID_Bumper */
}

static void crawl(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* ObjC8_Init */
        o_init(o, S2A_CRAWL, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 0xD7);
        SET8(o, OBJ_routine, 2);
        SET16(o, CR_TIMER, 0x200);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_render) & 1) ? 0x20 : (uint16_t)-0x20);
        SET8(o, OBJ_y_radius, 0x0F);
        SET8(o, OBJ_x_radius, 0x10);
        return;
    case 2:                                             /* walk */
        SET16(o, CR_TIMER, OB16(o, CR_TIMER) - 1);
        if (!OB16(o, CR_TIMER)) {
            SET8(o, OBJ_routine, 4);
            SET16(o, CR_TIMER, 0x3B);
            break;
        }
        o_move(o);
        crawl_near(o);
        o_animate_s2(o, o_asset_anim(S2A_CRAWL));
        break;
    case 4:                                             /* pause, then turn */
        SET16(o, CR_TIMER, OB16(o, CR_TIMER) - 1);
        if ((int16_t)OB16(o, CR_TIMER) < 0) {
            SET8(o, OBJ_routine, 2);
            SET16(o, CR_TIMER, 0x200);
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
            SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        } else {
            crawl_near(o);
        }
        break;
    default: {                                          /* guarding */
        SET8(o, OBJ_coll_flags, 0xD7);
        int16_t d2, d3;
        s2_orient(o, NULL, NULL, &d2, &d3);
        uint32_t a1 = s2_orient_player;
        if ((uint16_t)(d2 + 0x40) >= 0x80 || (uint16_t)(d3 + 0x40) >= 0x80) {   /* loc_3D39A */
            SET8(o, OBJ_routine, OB8(o, CR_PREV));
            break;
        }
        if (OB8(o, OBJ_status) & ST_P1_STAND) {         /* (S2 reuses the bit as "hurt mode") */
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_STAND);
            SET8(o, OBJ_frame, 1);
            break;
        }
        uint8_t prop = OB8(o, OBJ_coll_prop);
        if (!prop) break;
        for (int n = 0; n < 2; n++) {
            if (!(prop & (1 << n))) continue;
            prop &= (uint8_t)~(1 << n);                 /* bclr: only this player's bit, then on */
            SET8(o, OBJ_coll_prop, prop);
            if (n) a1 = RAM_Player_2;
            if (OB8(a1, OBJ_anim) != ANI_ROLL) {        /* loc_3D36C: not a ball: it hurts */
                SET8(o, OBJ_coll_flags, (r8(a1 + 0x2B) & 2) ? 0x17 : 0x97);
                SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_P1_STAND);
                SET8(o, OBJ_frame, 1);
                o_touch(o);
                o_remember(o);
                return;
            }
            if (!(OB8(a1, OBJ_status) & ST_INAIR)) {    /* rolling into its face: it can be hit */
                int d0;
                s2_orient(o, &d0, NULL, NULL, NULL);
                if (OB8(o, OBJ_render) & 1) d0 -= 2;
                if (d0) {                               /* loc_3D390 */
                    SET8(o, OBJ_coll_flags, 0x17);
                    o_touch(o);
                    o_remember(o);
                    return;
                }
            }
            crawl_bounce(o, a1);
        }
        SET8(o, OBJ_coll_prop, 0);
        break;
    }
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object D2 - flashing blocks walking round a rectangle
 * ---------------------------------------------------------------------- */
#define SN_X0    0x30
#define SN_Y0    0x32
#define SN_W     0x34
#define SN_H     0x36
#define SN_WAIT  0x38
#define SN_TIMER 0x3A

static void snake_place(uint32_t o)                  /* loc_2B60C */
{
    static const int8_t tab[16][4] = {
        { -0x28, 0x18, 8, 8 }, { -0x28, 0x10, 8, 0x10 }, { -0x28, 8, 8, 0x18 }, { -0x28, 0, 8, 0x20 },
        { -0x20, 0, 0x10, 0x20 }, { -0x18, -8, 0x18, 0x18 }, { -0x10, -0x10, 0x20, 0x10 }, { -8, -0x18, 0x28, 8 },
        { 8, -0x18, 0x28, 8 }, { 0x10, -0x10, 0x20, 0x10 }, { 0x18, -8, 0x18, 0x18 }, { 0x20, 0, 0x10, 0x20 },
        { 0x28, 0, 8, 0x20 }, { 0x28, 8, 8, 0x18 }, { 0x28, 0x10, 8, 0x10 }, { 0x28, 0x18, 8, 8 },
    };
    const int8_t *t = tab[OB8(o, OBJ_frame) & 0x0F];
    int16_t dx = t[0];
    if (OB8(o, OBJ_status) & 1) dx = (int16_t)-dx;
    SET16(o, OBJ_x_pos, OB16(o, SN_X0) + dx);
    SET16(o, OBJ_y_pos, OB16(o, SN_Y0) + t[1]);
    SET16(o, SN_W, (uint8_t)t[2]);
    SET8(o, OBJ_width_px, (uint8_t)t[2]);
    SET16(o, SN_H, (uint8_t)t[3]);
}

static void snake_blocks(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_SNAKE, OB8(o, OBJ_render) | 4, 4, 8, 0x20);
        SET16(o, SN_X0, OB16(o, OBJ_x_pos));
        SET16(o, SN_Y0, OB16(o, OBJ_y_pos));
        SET16(o, SN_WAIT, (uint16_t)(OB8(o, OBJ_subtype) << 4));
        snake_place(o);
    }
    if (OB16(o, SN_WAIT)) {
        SET16(o, SN_WAIT, OB16(o, SN_WAIT) - 1);
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);   /* MarkObjGone3 */
        return;
    }
    SET16(o, SN_TIMER, OB16(o, SN_TIMER) - 1);
    if ((int16_t)OB16(o, SN_TIMER) < 0) {
        SET16(o, SN_TIMER, 0x0F);
        SET8(o, OBJ_frame, (OB8(o, OBJ_frame) + 1) & 0x0F);
        if (!OB8(o, OBJ_frame)) {                       /* round complete: rest, drop riders */
            SET16(o, SN_WAIT, (uint16_t)(OB8(o, OBJ_subtype) << 4));
            for (int n = 0; n < 2; n++) {
                uint8_t sbit = (uint8_t)(ST_P1_STAND << n);
                if (!(OB8(o, OBJ_status) & sbit)) continue;
                SET8(o, OBJ_status, OB8(o, OBJ_status) & ~sbit);
                uint32_t p = o_player(n);
                SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
            }
            SET16(o, OBJ_x_pos, OB16(o, SN_X0));
        }
        snake_place(o);
    }
    o_solid(o, OB16(o, SN_W) + 0x0B, OB16(o, SN_H), OB16(o, SN_H) + 1, OB16(o, OBJ_x_pos));
    if (o_out_of_range(o, OB16(o, SN_X0))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object D4 - big block swinging back and forth (spring-like pull)
 * ---------------------------------------------------------------------- */
static void big_block(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_BIGBLOCK, 4, 4, 0x20, 0x20);
        SET16(o, 0x30, OB16(o, OBJ_x_pos));
        SET16(o, 0x32, OB16(o, OBJ_y_pos));
        SET16(o, OBJ_x_pos + 2, 0x8000);
        SET16(o, OBJ_y_pos + 2, 0x8000);
        if (!OB8(o, OBJ_subtype)) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x60 + ((OB8(o, OBJ_status) & 1) ? 0xC0 : 0));
        } else {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x60 + ((OB8(o, OBJ_status) & 2) ? 0xC0 : 0));
        }
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    if (!OB8(o, OBJ_subtype))
        SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + (OB16(o, 0x30) > OB16(o, OBJ_x_pos) ? 4 : -4));
    else
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + (OB16(o, 0x32) > OB16(o, OBJ_y_pos) ? 4 : -4));
    o_move(o);
    o_solid(o, 0x2B, 0x20, 0x21, oldx);
    if (o_out_of_range(o, OB16(o, 0x30))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object D5 - elevator: rides between two heights when stood on / left
 * ---------------------------------------------------------------------- */
#define EV_Y0   0x32
#define EV_MODE 0x34

static void cnz_elev_home(uint32_t o, int other)     /* snap to the end it stopped at */
{
    SET16(o, OBJ_y_pos, OB16(o, EV_Y0));
    int16_t d0 = (int16_t)(OB8(o, OBJ_subtype) << 2);
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - d0);
    if (((OB8(o, OBJ_status) & 1) != 0) != other) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0 * 2);
}

static void cnz_elevator(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_CNZELEV, 4, 4, 0x10, 0x10);
        SET16(o, EV_Y0, OB16(o, OBJ_y_pos));
        SET16(o, OBJ_y_pos + 2, 0x8000);
        cnz_elev_home(o, 0);
    }
    o_move(o);
    uint16_t mode = OB16(o, EV_MODE);
    if (mode == 0 || mode == 4) {                       /* wait to be stood on / left */
        int on = (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) != 0;
        if (mode == 0 ? on : !on) { o_sfx_s2(0xD6); /* SndID_CNZElevator */ SET16(o, EV_MODE, mode + 2); }   /* (S2: CNZElevator) */
    } else {                                            /* travel: accelerate through the midpoint */
        int16_t d1 = OB16(o, EV_Y0) >= OB16(o, OBJ_y_pos) ? 8 : -8;
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + d1);
        if (!OB16(o, OBJ_y_vel)) {
            SET16(o, EV_MODE, mode == 2 ? 4 : 0);
            cnz_elev_home(o, mode == 2);
        }
    }
    if (OB16(o, EV_MODE) < 6) o_platform(o, 0x10, 9, OB16(o, OBJ_x_pos));
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Casino prizes: rings (DC) and spikes (D3) flying into the cage
 * ---------------------------------------------------------------------- */
#define PZ_X     0x30                 /* casino_prize_x_pos (long) */
#define PZ_Y     0x34                 /* casino_prize_y_pos (long) */
#define PZ_MX    0x38                 /* casino_prize_machine_x_pos */
#define PZ_MY    0x3A
#define PZ_DELAY 0x3C                 /* casino_prize_display_delay */
#define PZ_ANGLE 0x44                 /* objoff_2E */
#define PZ_COUNT 0x40                 /* -> the cage's outstanding-prize counter (long) */
static uint16_t s_bonus_countdown3;   /* Bonus_Countdown_3 */

static void prize_home(uint32_t o)                   /* close in on the cage */
{
    int32_t d0 = (int32_t)r32(o + PZ_X) - (int32_t)((uint32_t)OB16(o, PZ_MX) << 16);
    SET32(o, PZ_X, r32(o + PZ_X) - (uint32_t)(d0 >> 4));
    SET16(o, OBJ_x_pos, OB16(o, PZ_X));
    d0 = (int32_t)r32(o + PZ_Y) - (int32_t)((uint32_t)OB16(o, PZ_MY) << 16);
    SET32(o, PZ_Y, r32(o + PZ_Y) - (uint32_t)(d0 >> 4));
    SET16(o, OBJ_y_pos, OB16(o, PZ_Y));
}

static void ring_prize(uint32_t o)                   /* ObjDC */
{
    if (OB8(o, OBJ_routine) == 0) {
        prize_home(o);
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);   /* Ani_objDC: 1,0,1,2,3 */
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
            SET8(o, OBJ_anim_timer, 1);
            SET8(o, OBJ_frame, (OB8(o, OBJ_frame) + 1) & 3);
        }
        SET16(o, PZ_DELAY, OB16(o, PZ_DELAY) - 1);
        if (OB16(o, PZ_DELAY)) { o_draw(o); return; }
        uint32_t cnt = r32(o + PZ_COUNT) & 0xFFFFFF;
        w16(cnt, r16(cnt) - 1);
        tri_jsr(S3K_GiveRing);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_frame, 4);
        SET8(o, OBJ_anim_timer, 5);
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);   /* Ani_Ring: sparkle 4-7 */
    if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
        SET8(o, OBJ_anim_timer, 5);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        if (OB8(o, OBJ_frame) > 7) { o_delete(o); return; }
    }
    o_draw(o);
}

static void spike_prize(uint32_t o)                  /* ObjD3 */
{
    prize_home(o);
    SET16(o, PZ_DELAY, OB16(o, PZ_DELAY) - 1);
    if (OB16(o, PZ_DELAY)) { o_draw(o); return; }
    uint32_t cnt = r32(o + PZ_COUNT) & 0xFFFFFF;
    w16(cnt, r16(cnt) - 1);
    if (s_bonus_countdown3 >= 5) {
        s_bonus_countdown3 = 0;
        o_sfx_s2(0xA6);                                    /* SndID_HurtBySpikes */
    }
    if (r16(RAM_Ring_count)) {
        w16(RAM_Ring_count, r16(RAM_Ring_count) - 1);
        w8(RAM_Update_HUD_ring_count, r8(RAM_Update_HUD_ring_count) | 0x81);
    }
    o_delete(o);
}

/* ------------------------------------------------------------------------
 * The slot machine (SlotMachine): three reels drawn into the level's slot
 * tiles; the cage that started it pays out its reward.
 * ---------------------------------------------------------------------- */
static uint8_t sm[18];                /* SlotMachineVariables */
static int16_t sm_in_use;             /* SlotMachineInUse */
#define SM_ROUT   0
#define SM_TIMER  1
#define SM_INDEX  3
#define SM_TARG   4                   /* slots_targ / SlotMachine_Reward (word) */
#define SM_DATA   6                   /* 3 x { index/offset word, speed, routine } */
static const uint8_t sm_seq[3][8] = {
    { 3, 0, 1, 4, 2, 5, 4, 1 }, { 3, 0, 1, 4, 2, 5, 0, 2 }, { 3, 0, 1, 4, 2, 5, 4, 1 },
};
static const int16_t sm_rewards[6] = { 30, 25, -1, 150, 10, 20 };
static const uint8_t sm_targets[] = { 8, 3, 0x33, 0x12, 0, 0x00, 0x12, 1, 0x11, 0x24, 2, 0x22,
                                      0x1E, 4, 0x44, 0x1E, 5, 0x55, 0xFF, 0x0F, 0xFF };

static uint16_t sm_w(int off) { return (uint16_t)((sm[off] << 8) | sm[off + 1]); }
static void sm_ww(int off, uint16_t v) { sm[off] = (uint8_t)(v >> 8); sm[off + 1] = (uint8_t)v; }

void s2_slot_machine_reset(void)                     /* Level_FromCheckpoint */
{
    sm[SM_ROUT] = 0;
    sm_in_use = 0;
}

static uint8_t sm_vint(int b) { return r8(RAM_Level_frame_counter + 1 - b); }

static void sm_pixel_row(uint32_t art, const uint8_t *seq, uint16_t d3, uint32_t *src)
{
    uint8_t id = seq[(d3 >> 8) & 7] & 7;
    *src = art + (uint32_t)id * 0x200u + (uint32_t)((d3 & 0xF8) >> 1);
}

static void sm_draw(void)                            /* SlotMachine_DrawSlot */
{
    uint32_t inf = tri_rd32(tri_dir() + TD_EXTRA);
    uint16_t base = inf ? tri_rd16(inf + S2I_SLOT_TILE) : 0;
    uint32_t art = inf ? tri_rd32(inf + S2I_SLOT_ART) : 0;
    if (base & 0x8000) {                             /* S2 colours on Sonic's line: the extended line */
        extern void tri_ext_tiles(uint32_t tile, uint32_t n, int on);
        base &= 0x7FF;
        tri_ext_tiles(base, 0x30, 1);
    }
    int k = sm[SM_INDEX] >> 2;
    uint8_t *a1 = sm + SM_DATA + k * 4;
    const uint8_t *seq = sm_seq[k];
    uint16_t dest;
    if (k == 0) { sm[SM_INDEX] = 4; dest = 0; }
    else if (k == 1) { sm[SM_INDEX] = 8; dest = 16; }
    else { sm[SM_INDEX] = 0; sm[SM_TIMER]--; dest = 32; }
    uint16_t d0 = (uint16_t)((a1[0] << 8) | a1[1]);
    uint16_t v = (uint16_t)(d0 - (int16_t)(int8_t)a1[2]);
    a1[0] = (uint8_t)(v >> 8); a1[1] = (uint8_t)v;
    uint16_t d3 = v & 0x7F8;
    if ((d0 & 0x7F8) == d3 || !base || !art) return;
    uint32_t src;
    sm_pixel_row(art, seq, d3, &src);
    uint8_t buf[16 * 32];
    for (int r = 0; r < 32; r++) {                      /* 32 pixel rows of 4 tile columns */
        for (int c = 0; c < 4; c++)
            for (int b = 0; b < 4; b++)
                buf[c * 0x80 + r * 4 + b] = tri_rd8(src + (uint32_t)c * 0x80u + (uint32_t)b);
        src += 4;
        d3 = (uint16_t)((d3 & 0xFF00) | (uint8_t)(d3 + 8));
        if (!(d3 & 0xFF)) {
            d3 = (uint16_t)((d3 + 0x100) & 0x700);
            sm_pixel_row(art, seq, d3, &src);
        }
    }
    for (int i = 0; i < 16 * 32; i++)
        g_machine.vdp.vram[((uint32_t)(base + dest) * 32u + (uint32_t)i) & 0xFFFFu] = buf[i];
}

static uint8_t sm_target_for_slot(void)              /* SlotMachine_GetTargetForSlot */
{
    uint16_t d1 = sm_w(SM_TARG);
    if (sm[SM_INDEX]) d1 >>= sm[SM_INDEX];
    d1 &= 7;
    if (d1 > 5) d1 -= 2;
    return (uint8_t)d1;
}

static void sm_change_target(uint8_t face)           /* SlotMachine_ChangeTarget */
{
    uint16_t d1 = face & 0x0F, d2 = 0xFFF0;
    int s = sm[SM_INDEX];
    if (s) { d1 = (uint16_t)(d1 << s); d2 = (uint16_t)((d2 << s) | (d2 >> (16 - s))); }
    uint16_t t = (uint16_t)(((sm_w(SM_TARG) & d2) | d1) & 0x777);
    sm_ww(SM_TARG, t);
}

static int16_t sm_reward(int i) { return sm_rewards[i]; }

static void sm_choose_reward(void)                   /* SlotMachine_ChooseReward */
{
    uint8_t s1 = sm[SM_TARG], s23 = sm[SM_TARG + 1];
    uint8_t d2 = (s23 & 0xF0) >> 4, d3 = s23 & 0x0F;
    int m12 = s1 == d2, m13 = s1 == d3;
    int16_t r;
    if (m12 && m13) r = sm_reward(d2);                  /* triple */
    else if (m13) {
        if (d3 == 3) r = (int16_t)(sm_reward(d2) * 4);
        else if (d2 == 3) r = (int16_t)(sm_reward(d3) * 2);
        else goto unmatched;
    } else if (m12) {
        if (d2 == 3) r = (int16_t)(sm_reward(d3) * 4);
        else if (d3 == 3) r = (int16_t)(sm_reward(d2) * 2);
        else goto unmatched;
    } else {
    unmatched:
        if (d2 == d3 && s1 == 3) r = (int16_t)(sm_reward(d2) * 2);
        else if (d2 == d3 && d2 == 3) r = (int16_t)(sm_reward(s1 & 0x0F) * 4);
        else {                                          /* bars: 2 rings each (S2 then reads past) */
            int16_t n = 0;
            if (s1 == 5) n += 2;
            if (d2 == 5) n += 2;
            if (d3 == 5) n += 2;
            r = sm_reward(n);                           /* (no rts: GetReward runs with d0) */
        }
    }
    sm_ww(SM_TARG, (uint16_t)r);
}

static void sm_routine5_slot(void)
{
    int k = sm[SM_INDEX] >> 2;
    uint8_t *a1 = sm + SM_DATA + k * 4;
    const uint8_t *a3 = sm_seq[k];
    uint16_t pos = (uint16_t)((a1[0] << 8) | a1[1]);
    switch (a1[3]) {
    case 0:                                             /* wait our turn, then for our face */
        if (k == 0) { if ((int8_t)sm[SM_TIMER] >= 0) return; }
        else if ((int8_t)a1[3 - 4] < 8) return;
        if (a3[((uint16_t)(pos - 0xA0) >> 8) & 7] != sm_target_for_slot()) return;
        a1[3] = 4;
        a1[2] = 0x60;
        return;
    case 4: {                                           /* slow down onto it */
        uint8_t t = sm_target_for_slot();
        if (a3[((uint16_t)(pos + 0xF0) & 0x700) >> 8] == t) {
            uint16_t d0 = (uint16_t)(pos + 0x80), d1 = (uint16_t)((d0 & 0x700) - 0x10);
            a1[0] = (uint8_t)(d1 >> 8); a1[1] = (uint8_t)d1;
            sm_change_target(a3[(d0 >> 8) & 7]);
            a1[2] = (uint8_t)-8;
            a1[3] = 8;
            return;
        }
        if (a1[2] > 0x20) a1[2] -= 0x0C;
        if ((int8_t)a1[2] <= 0x18) return;
        if (a1[1] > 0x80) return;
        a1[2] -= 2;
        return;
    }
    case 8:                                             /* settle */
        if (a1[1]) return;
        a1[2] = 0;
        a1[3] = 0x0C;
        return;
    default:
        return;
    }
}

static void slot_machine(void)                       /* SlotMachine */
{
    switch (sm[SM_ROUT]) {
    case 0x00:                                          /* reset, show random faces once */
        memset(sm, 0, sizeof sm);
        {
            uint8_t d0 = sm_vint(0);
            sm[SM_DATA + 0] = d0;
            d0 = (uint8_t)((d0 >> 3) | (d0 << 5));
            sm[SM_DATA + 4] = d0;
            d0 = (uint8_t)((d0 >> 3) | (d0 << 5));
            sm[SM_DATA + 8] = d0;
        }
        for (int k = 0; k < 3; k++) { sm[SM_DATA + k * 4 + 1] = 8; sm[SM_DATA + k * 4 + 2] = 8; }
        sm[SM_TIMER] = 1;
        sm[SM_ROUT] = 4;
        return;
    case 0x04:
        sm_draw();
        if (sm[SM_TIMER]) return;
        sm[SM_ROUT] = 0x18;
        for (int k = 0; k < 3; k++) { sm[SM_DATA + k * 4 + 2] = 0; sm[SM_DATA + k * 4 + 3] = 0; }
        return;
    case 0x08: {                                        /* a cage started it: spin */
        uint8_t v = sm_vint(0);
        sm[SM_DATA + 2] = (uint8_t)((v & 7) - 4 + 0x30);
        sm[SM_DATA + 6] = (uint8_t)((((v << 4) | (v >> 4)) & 7) - 4 + 0x30);
        sm[SM_DATA + 10] = (uint8_t)((sm_vint(1) & 7) - 4 + 0x30);
        sm[SM_TIMER] = 2;
        sm[SM_INDEX] = 0;
        sm[SM_DATA + 3] = sm[SM_DATA + 7] = sm[SM_DATA + 11] = 0;
        sm[SM_ROUT] = 0x0C;
        uint8_t d0 = (uint8_t)((v >> 3) | (v << 5));
        const uint8_t *a2 = sm_targets;
        for (;;) {                                      /* pick a weighted outcome */
            uint8_t sub = *a2;
            if (d0 < sub) break;
            d0 = (uint8_t)(d0 - sub);
            a2 += 3;
        }
        if (*a2 != 0xFF) { sm[SM_TARG] = a2[1]; sm[SM_TARG + 1] = a2[2]; return; }
        d0 = (uint8_t)(d0 - *a2);                       /* (the failed subtraction stays) */
        sm[SM_TARG] = sm_seq[0][d0 & 0xFF & 7 ? d0 & 7 : d0 & 7];
        sm[SM_TARG] = sm_seq[0][d0 < 8 ? d0 : (d0 & 7)]; /* (S2 indexes with the whole byte) */
        d0 = (uint8_t)((d0 >> 3) | (d0 << 5));
        uint8_t d2 = (uint8_t)(sm_seq[1][d0 & 7] << 4);
        d0 = (uint8_t)((d0 >> 3) | (d0 << 5));
        sm[SM_TARG + 1] = (uint8_t)(d2 | sm_seq[2][d0 & 7]);
        return;
    }
    case 0x0C:
        sm_draw();
        if (sm[SM_TIMER]) return;
        sm[SM_DATA + 2] += 0x30;
        sm[SM_DATA + 6] += 0x30;
        sm[SM_DATA + 10] += 0x30;
        sm[SM_TIMER] = (uint8_t)((sm_vint(0) & 0x0F) + 0x0C);
        sm[2] = 0;
        sm[SM_ROUT] = 0x10;
        return;
    case 0x10:
        sm_draw();
        if (sm[SM_DATA + 3] == 0x0C && sm[SM_DATA + 7] == 0x0C && sm[SM_DATA + 11] == 0x0C) {
            for (int k = 0; k < 3; k++) { sm[SM_DATA + k * 4 + 2] = 0; sm[SM_DATA + k * 4 + 3] = 0x0C; }
            sm[SM_TIMER] = 0;
            sm_choose_reward();
            sm[SM_ROUT] = 0x18;
            return;
        }
        sm_routine5_slot();
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object D6 - cage: holds the player a while for points, or (subtype set)
 * runs the slot machine and pays out ring or spike prizes
 * ---------------------------------------------------------------------- */
#define CG_STATE 0x30                 /* +0 P1 (state word, timer word), +4 P2 */
#define CG_BOMBS 0x40                 /* objoff_2A */
#define CG_OUT   0x42                 /* objoff_2C: prizes in flight */
#define CG_ANGLE 0x44                 /* objoff_2E */
#define CG_WHO   0x46                 /* parent */

static uint32_t cage_prize(uint32_t o, int ring)
{
    uint32_t a1 = o_alloc();
    if (!a1) return 0;
    SET32(a1, OBJ_code, S2(ring ? 0xDC : 0xD3));
    if (ring) {
        SET32(a1, OBJ_mappings, S3K_Map_Ring);
        SET16(a1, OBJ_art_tile, 0x26BC);                /* make_art_tile(ArtTile_Ring,1,0) */
        SET16(a1, OBJ_priority, 3 << 7);
        SET8(a1, OBJ_width_px, 8);
        SET8(a1, OBJ_height_px, 8);
        SET16(a1, PZ_DELAY, 0x1A);
    } else {
        o_asset(a1, S2A_BONUSSPIKE);
        SET16(a1, OBJ_priority, 4 << 7);
        SET8(a1, OBJ_width_px, 0x10);
        SET8(a1, OBJ_height_px, 0x10);
        SET16(a1, PZ_DELAY, 0x1E);
    }
    SET8(a1, OBJ_render, 4);
    SET16(a1, PZ_ANGLE, OB16(o, CG_ANGLE));
    SET16(o, CG_ANGLE, OB16(o, CG_ANGLE) + (ring ? 0x89 : 0x90));
    SET16(a1, PZ_MX, OB16(o, OBJ_x_pos));
    SET16(a1, PZ_MY, OB16(o, OBJ_y_pos));
    int16_t s, c;
    o_sincos((uint8_t)OB16(a1, PZ_ANGLE), &s, &c);
    int16_t x = (int16_t)((c >> 1) + OB16(o, OBJ_x_pos)), y = (int16_t)((s >> 1) + OB16(o, OBJ_y_pos));
    SET16(a1, PZ_X, (uint16_t)x); SET16(a1, PZ_X + 2, 0);
    SET16(a1, OBJ_x_pos, (uint16_t)x);
    SET16(a1, PZ_Y, (uint16_t)y); SET16(a1, PZ_Y + 2, 0);
    SET16(a1, OBJ_y_pos, (uint16_t)y);
    SET32(a1, PZ_COUNT, RAMPTR(o + CG_OUT));
    SET16(o, CG_OUT, OB16(o, CG_OUT) + 1);
    return a1;
}

static void cage_release(uint32_t o, int n)          /* loc_2BE2E */
{
    uint32_t p = o_player(n);
    uint32_t st = o + CG_STATE + (uint32_t)n * 4u;
    SET16(o, CG_OUT, 0);
    SET8(o, OBJ_anim, 0);
    SET8(p, P_OBJCTRL, 0);
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND << n));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~ST_ONOBJ) | ST_INAIR);
    SET16(p, OBJ_y_vel, 0x400);
    w16(st, r16(st) + 2);
    w16(st + 2, 0x1E);
}

static void cage_payout(uint32_t o, int n)           /* loc_2BC86 */
{
    int16_t reward = (int16_t)sm_w(SM_TARG);
    if (reward < 0) {                                   /* spikes: lose rings */
        if ((int16_t)OB16(o, CG_BOMBS) < 0) SET16(o, CG_BOMBS, 0x64);
        if (OB16(o, CG_BOMBS)) {
            if ((r8(RAM_Level_frame_counter + 1) & 1) && OB16(o, CG_OUT) < 0x10 && cage_prize(o, 0))
                SET16(o, CG_BOMBS, OB16(o, CG_BOMBS) - 1);
            s_bonus_countdown3++;
            return;
        }
        if (!OB16(o, CG_OUT)) cage_release(o, n);
        else s_bonus_countdown3++;
        return;
    }
    if (reward) {                                       /* loc_2BD4E: no ring this frame -> just wait */
        if (!(r8(RAM_Level_frame_counter + 1) & 1) || OB16(o, CG_OUT) >= 0x10 || !cage_prize(o, 1)) return;
        sm_ww(SM_TARG, (uint16_t)(reward - 1));
    }
    if (!OB16(o, CG_OUT)) cage_release(o, n);
}

static void cage(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_CAGE, 4, 1, 0x18, 0x18);
    }
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        uint32_t st = o + CG_STATE + (uint32_t)n * 4u;
        switch (r16(st)) {
        case 0:                                         /* catch a player */
            if (OB8(p, P_OBJCTRL)) break;
            if (OB8(o, OBJ_subtype) && sm_in_use) break;
            if ((int16_t)o_solid_1p(o, n, 0x23, 0x10, 0x11, OB16(o, OBJ_x_pos), 1) >= 0) break;
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, 0);
            SET16(p, OBJ_ground_vel, 0);
            SET8(p, P_OBJCTRL, 0x81);
            SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
            roll_up(p);
            SET8(o, OBJ_anim, 1);
            w16(st, 2);
            w16(st + 2, 0x78);
            SET16(o, CG_WHO, (uint16_t)p);
            if (OB8(o, OBJ_subtype) && sm[SM_ROUT] == 0x18) {
                sm[SM_ROUT] = 8;
                SET16(o, CG_ANGLE, 0);
                sm_in_use = -1;
                SET16(o, CG_BOMBS, 0xFFFF);
            }
            break;
        case 2:                                         /* held */
            if (!(OB8(o, OBJ_render) & 0x80)) { cage_release(o, n); break; }
            if (OB8(o, OBJ_subtype)) {
                if (sm[SM_ROUT] == 0x18) { cage_payout(o, n); break; }
                if (!(r8(RAM_Level_frame_counter + 1) & 0x0F)) o_sfx_s2(0xC0);   /* SndID_CasinoBonus */
                break;
            }
            w16(st + 2, r16(st + 2) - 1);
            if ((int16_t)r16(st + 2) < 0) { cage_release(o, n); break; }
            if (!(r16(st + 2) & 0x0F)) {                /* 100 points every 16 frames */
                o_sfx_s2(0xC0); /* SndID_CasinoBonus */
                add_points(10);
                o_score(o, 0);
            }
            break;
        default:                                        /* cool-down */
            w16(st + 2, r16(st + 2) - 1);
            if ((int16_t)r16(st + 2) < 0) {
                w16(st, 0);
                if (OB8(o, OBJ_subtype)) sm_in_use = 0;
            }
            break;
        }
    }
    o_animate_s2(o, o_asset_anim(S2A_CAGE));
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object D7 - hexagon bumper (some ride left and right)
 * ---------------------------------------------------------------------- */
#define HB_MIN 0x30
#define HB_MAX 0x32
#define HB_DIR 0x34

static void hex_bumper(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_HEXBUMP, 4, 1, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0xCA);
        if (OB8(o, OBJ_status) & 1) SET8(o, HB_DIR, 1);
        SET16(o, HB_MIN, OB16(o, OBJ_x_pos) - 0x60);
        SET16(o, HB_MAX, OB16(o, OBJ_x_pos) + 0x60);
    }
    uint8_t prop = OB8(o, OBJ_coll_prop);
    if (prop) {
        for (int n = 0; n < 2; n++) {
            if (!(prop & (1 << n))) continue;
            uint32_t p = o_player(n);
            int16_t d1 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos));
            uint8_t a = (uint8_t)((o_atan(d1, (int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos))) + 0x20) & 0xC0);
            if (a == 0x40) {                            /* hit from above: down */
                SET16(p, OBJ_x_vel, OB16(p, OBJ_x_vel) - 0x200 + (d1 < 0 ? 0x400 : 0));
                SET16(p, OBJ_y_vel, (uint16_t)-0x800);
                SET8(o, OBJ_anim, 1);
            } else if (a == 0x80) {
                SET16(p, OBJ_x_vel, 0x800);
                SET8(o, OBJ_anim, 2);
            } else if (a == 0xC0) {
                SET16(p, OBJ_x_vel, OB16(p, OBJ_x_vel) - 0x200 + (d1 < 0 ? 0x400 : 0));
                SET16(p, OBJ_y_vel, 0x800);
                SET8(o, OBJ_anim, 1);
            } else {
                SET16(p, OBJ_x_vel, (uint16_t)-0x800);
                SET8(o, OBJ_anim, 2);
            }
            bounce_end(p);
            o_sfx_s2(0xB4); /* SndID_Bumper */
        }
        SET8(o, OBJ_coll_prop, 0);
    }
    o_animate_s2(o, o_asset_anim(S2A_HEXBUMP));
    o_touch(o);
    if (!OB8(o, OBJ_subtype)) { o_remember(o); return; }
    int16_t x = (int16_t)OB16(o, OBJ_x_pos);
    if (OB8(o, HB_DIR)) { x--; if (x == (int16_t)OB16(o, HB_MIN)) SET8(o, HB_DIR, 0); }
    else { x++; if (x == (int16_t)OB16(o, HB_MAX)) SET8(o, HB_DIR, 1); }
    SET16(o, OBJ_x_pos, (uint16_t)x);
    if (o_out_of_range(o, OB16(o, HB_MIN)) && o_out_of_range(o, OB16(o, HB_MAX))) {
        o_delete_respawnable(o);
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object D8 - saucer bumper: three hits and it's gone (and 3 of a group
 * give a bonus)
 * ---------------------------------------------------------------------- */
#define SB_DELAY 0x30                 /* +0 P1, +1 P2 re-hit delay */
#define SB_LINE  0x32                 /* palette line left (2 -> 0) */
static uint8_t s_saucer_data[64];     /* CNZ_saucer_data */

void s2_cnz_reset(void) { memset(s_saucer_data, 0, sizeof s_saucer_data); s_bonus_countdown3 = 0; }

static void saucer_hit(uint32_t o, uint32_t p)       /* loc_2C74E */
{
    uint8_t f = OB8(o, OBJ_frame);
    if (f == 0 || f == 3) {                             /* vertical: straight up / down */
        SET8(o, OBJ_anim, 3);
        SET16(p, OBJ_y_vel, (uint16_t)-0x700);
        if ((int16_t)(OB16(o, OBJ_y_pos) - OB16(p, OBJ_y_pos)) < 0) SET16(p, OBJ_y_vel, 0x700);
    } else if (f == 1 || f == 4) {                      /* diagonal: reflect */
        SET8(o, OBJ_anim, 4);
        int16_t d3 = (OB8(o, OBJ_status) & 1) ? 0x20 : 0x60;
        int16_t d0 = (int16_t)(o_atan((int16_t)OB16(p, OBJ_x_vel), (int16_t)OB16(p, OBJ_y_vel)) - d3);
        int16_t d1 = d0 < 0 ? (int16_t)-d0 : d0;
        d0 = (int16_t)(-d0 + d3);
        if ((uint8_t)d1 >= 0x40) {
            d1 = (int16_t)-(d1 - 0x80);
            if ((uint8_t)d1 >= 0x38) d0 = (int16_t)(d3 + 0x80);
        } else if ((uint8_t)d1 >= 0x38) {
            d0 = d3;
        }
        int16_t s, c;
        o_sincos((uint8_t)d0, &s, &c);
        SET16(p, OBJ_x_vel, (uint16_t)(((int32_t)c * -0x700) >> 8));
        SET16(p, OBJ_y_vel, (uint16_t)(((int32_t)s * -0x700) >> 8));
    } else {                                            /* horizontal */
        SET8(o, OBJ_anim, 5);
        SET16(p, OBJ_x_vel, (uint16_t)-0x700);
        if ((int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)) < 0) SET16(p, OBJ_x_vel, 0x700);
    }
    bounce_end(p);
    o_sfx_s2(0xD8);                                        /* SndID_BonusBumper */
    int frame = 4, pts = 1;
    if (OB8(o, SB_LINE)) {                              /* one palette line down */
        SET8(o, SB_LINE, OB8(o, SB_LINE) - 1);
        o_asset(o, OB8(o, SB_LINE) == 1 ? S2A_MINIBUMP1 : S2A_MINIBUMP0);
    } else {                                            /* gone */
        SET8(o, OBJ_routine, 4);
        uint8_t *c = &s_saucer_data[OB8(o, OBJ_subtype) & 0x3F];
        if (++*c >= 3) { frame = 2; pts = 50; }
    }
    add_points(pts);
    o_score(o, frame);
}

static void saucer_bumper(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_MINIBUMP2, 4, 1, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0xD7);
        SET8(o, SB_LINE, 2);
        SET8(o, OBJ_anim, (uint8_t)(((OB8(o, OBJ_subtype) << 2) | (OB8(o, OBJ_subtype) >> 6)) & 3));
        /* fall through */
    case 2: {
        uint8_t prop = OB8(o, OBJ_coll_prop);
        if (prop || OB16(o, SB_DELAY)) {
            for (int n = 0; n < 2; n++) {
                if (OB8(o, SB_DELAY + n)) { SET8(o, SB_DELAY + n, OB8(o, SB_DELAY + n) - 1); continue; }
                if (!(prop & (1 << n))) continue;
                saucer_hit(o, o_player(n));
                SET8(o, SB_DELAY + n, 4);
            }
            SET8(o, OBJ_coll_prop, 0);
        }
        o_animate_s2(o, o_asset_anim(S2A_MINIBUMP2));
        if (OB8(o, OBJ_routine) == 2) o_touch(o);
        o_remember(o);
        return;
    }
    default:                                            /* last flash, then gone */
        o_animate_s2(o, o_asset_anim(S2A_MINIBUMP2));
        if (OB8(o, OBJ_anim) < 3) { o_delete(o); return; }
        o_remember(o);
        return;
    }
}

/* CNZ level events run the slot machine every frame (LevEvents_CNZ) */
void s2_cnz_frame(void) { slot_machine(); }

/* ------------------------------------------------------------------------
 * The wall bumpers: not objects in S2 but SpecialCNZBumpers lists, checked
 * from TouchResponse (Check_CNZ_bumpers); here once a frame per player.
 * ---------------------------------------------------------------------- */
static void bumper_bounce(uint32_t p, int16_t d3)    /* loc_175EA */
{
    D(1) = OB16(p, OBJ_x_vel);
    D(2) = OB16(p, OBJ_y_vel);
    tri_jsr(S3K_GetArcTan);                          /* = S2 CalcAngle */
    int16_t d0 = (int16_t)((D(0) & 0xFF) - d3);
    uint8_t d1 = (uint8_t)(d0 < 0 ? -d0 : d0);
    uint8_t ang = (uint8_t)(d3 - d0);
    if (d1 >= 0x38) ang = (uint8_t)d3;
    int16_t s, c;
    o_sincos(ang, &s, &c);
    SET16(p, OBJ_x_vel, (uint16_t)(int16_t)(((int32_t)c * -0xA00) >> 8));
    SET16(p, OBJ_y_vel, (uint16_t)(int16_t)(((int32_t)s * -0xA00) >> 8));
}

static int bumper_hit(uint32_t p, uint16_t id, int16_t bx, int16_t by)   /* off_1757A: 1 = bounced */
{
    int16_t px = (int16_t)OB16(p, OBJ_x_pos), py = (int16_t)OB16(p, OBJ_y_pos), d0;
    switch (id) {
    case 0: case 2:                                  /* loc_17586 / loc_17638: triangles */
        if (py - by >= 0x20) { SET16(p, OBJ_y_vel, 0xA00); return 1; }
        if (id == 0 ? px - bx >= 0x20 : bx - px >= 0x20) {
            SET16(p, OBJ_x_vel, (uint16_t)(id == 0 ? 0xA00 : -0xA00));
            return 1;
        }
        d0 = (int16_t)(id == 0 ? bx - px : px - bx);
        if (d0 >= 0x20) d0 = 0x20;
        if ((uint16_t)(d0 + by - 8) >= (uint16_t)(py + 0x0E)) return 0;
        bumper_bounce(p, id == 0 ? 0x20 : 0x60);
        return 1;
    case 4: case 6:                                  /* loc_1769E / loc_176F6: flat, up / down */
        if (id == 4 ? py - by >= 8 : by - py >= 8) {
            SET16(p, OBJ_y_vel, (uint16_t)(id == 4 ? 0xA00 : -0xA00));
            return 1;
        }
        d0 = (int16_t)(bx - px);
        if (d0 >= 0x40) { SET16(p, OBJ_x_vel, (uint16_t)-0xA00); return 1; }
        d0 = (int16_t)-d0;
        if (d0 >= 0x40) { SET16(p, OBJ_x_vel, 0xA00); return 1; }
        bumper_bounce(p, id == 4 ? (d0 < 0 ? 0x38 : 0x48) : (d0 < 0 ? 0xC8 : 0xB8));
        return 1;
    default:                                         /* 8 / $A: loc_1774C / loc_177A4, upright */
        if (id == 8 ? px - bx >= 8 : bx - px >= 8) { SET16(p, OBJ_x_vel, 0xA00); return 1; }   /* (S2: +$A00 both) */
        d0 = (int16_t)(by - py);
        if (d0 >= 0x40) { SET16(p, OBJ_y_vel, (uint16_t)-0xA00); return 1; }
        d0 = (int16_t)-d0;
        if (d0 >= 0x40) { SET16(p, OBJ_x_vel, 0xA00); return 1; }                              /* (S2: x_vel) */
        bumper_bounce(p, id == 8 ? (d0 < 0 ? 8 : 0xF8) : (d0 < 0 ? 0x78 : 0x88));
        return 1;
    }
}

void s2_cnz_bumpers(void)
{
    static const uint8_t size[6][2] = { { 0x20, 0x20 }, { 0x20, 0x20 }, { 0x40, 8 }, { 0x40, 8 }, { 8, 0x40 }, { 8, 0x40 } };
    if (tri_zone() != TRI_S2_CNZ || r16(RAM_Debug_placement_mode)) return;
    uint32_t b = blob(TRI_BLOB_S2_CNZ_BUMPERS);
    if (!b) return;
    uint32_t list = r8(RAM_Current_zone_and_act + 1) ? b + tri_rd16(b) : b + 2u;
    uint16_t d4 = (uint16_t)(r16(RAM_Camera_X_pos) - 8);   /* SpecialCNZBumpers: the visible window */
    if ((int16_t)d4 <= 0) d4 = 1;
    uint32_t a1 = list;
    while (tri_rd16(a1 + 2) < d4) a1 += 6;
    uint32_t a2 = a1;
    while (tri_rd16(a2 + 2) < (uint16_t)(d4 + 0x150)) a2 += 6;
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        if (!o_player_active(p) || OB8(p, OBJ_routine) != 2 || (OB8(p, P_OBJCTRL) & 0x80)) continue;
        uint16_t d2 = (uint16_t)(OB16(p, OBJ_x_pos) - 9);
        uint16_t d5 = (uint16_t)(OB8(p, OBJ_y_radius) - 3);
        uint16_t d3 = (uint16_t)(OB16(p, OBJ_y_pos) - d5);
        if (OB8(p, OBJ_anim) == 8) { d3 = (uint16_t)(d3 + 0x0C); d5 = 0x0A; }   /* ducking */
        d5 = (uint16_t)(d5 * 2);
        for (uint32_t e = a1; e < a2; e += 6) {
            uint16_t id = tri_rd16(e) & 0x0E;
            int w = size[id >> 1][0], h = size[id >> 1][1];
            int16_t bx = (int16_t)tri_rd16(e + 2), by = (int16_t)tri_rd16(e + 4);
            uint16_t d0 = (uint16_t)(bx - w - d2);
            if ((uint16_t)(bx - w) >= d2 ? d0 > 0x12 : (uint32_t)d0 + (uint32_t)(2 * w) <= 0xFFFF) continue;
            d0 = (uint16_t)(by - h - d3);
            if ((uint16_t)(by - h) >= d3 ? d0 > d5 : (uint32_t)d0 + (uint32_t)(2 * h) <= 0xFFFF) continue;
            if (bumper_hit(p, tri_rd16(e), bx, by)) {   /* loc_177FA */
                SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~(ST_ROLLJUMP | ST_PUSH));
                SET8(p, P_JUMPING, 0);
                o_sfx_s2(0xD9);                          /* SndID_LargeBumper */
                if (getenv("TRILOGY_BUMPLOG"))            /* development */
                    fprintf(stderr, "[BUMP] p%d id %X at %X,%X -> vel %X,%X\n", n, tri_rd16(e), bx, by,
                            OB16(p, OBJ_x_vel), OB16(p, OBJ_y_vel));
            }
            break;
        }
    }
}

void s2_obj_cnz_register(TriObjFn *t)
{
    t[0x44] = round_bumper;
    t[0x72] = conveyor;
    t[0x84] = pinball_switch;
    t[0x85] = launcher;
    t[0x86] = flipper;
    t[0xC8] = crawl;
    t[0xD2] = snake_blocks;
    t[0xD3] = spike_prize;
    t[0xD4] = big_block;
    t[0xD5] = cnz_elevator;
    t[0xD6] = cage;
    t[0xD7] = hex_bumper;
    t[0xD8] = saucer_bumper;
    t[0xDC] = ring_prize;
}
