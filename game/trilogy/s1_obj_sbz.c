/*
 * s1_obj_sbz.c — Sonic 1 Scrap Brain Zone objects, transcribed from the REV01
 * object code: Ball Hog (1E) and its cannonball (20), small door (2A),
 * rotating junction (66), running disc (67), conveyor belt (68), trapdoors
 * and spinning platforms (69), saws (6A), stompers and sliding doors (6B,
 * also SBZ3), vanishing platforms (6C), flamethrower (6D), electric orbs (6E),
 * girder (70), teleporter (72). The conveyor-loop spinning platforms (6F)
 * share the LZ conveyor code in s1_obj_lz.c.
 *
 * S1 sounds S3K lacks use stand-ins: saw = MechaSpark, electric orb =
 * EnergyZap, flamethrower = FlamethrowerQuiet, teleporter = TunnelBooster.
 */
#include "trilogy_obj.h"

#define OBJ_2nd          0x25
#define OBJ_prev_anim    0x21
#define OBJ_object_ctrl  0x2E
#define OBJ_stick_convex 0x3C
#define SFX_DOOR    0x91
#define SFX_SAW     0x5C
#define SFX_ZAP     0xA8
#define SFX_FLAME   0x4F
#define SFX_ROLL    0x3C
#define SFX_TELE    0x74
#define ANI_ROLL    2

static int chk_del(uint32_t o, uint16_t x)
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return 1; }
    return 0;
}

static void remember(uint32_t o)
{
    if (chk_del(o, OB16(o, OBJ_x_pos))) return;
    o_touch(o);
    o_draw(o);
}

static uint8_t sw_bit0(int n) { return r8(RAM_Level_trigger_array + (uint32_t)(n & 0x0F)) & 1; }

/* the platform stopped being solid: drop anyone standing on it */
static void drop_players(uint32_t o)
{
    for (int i = 0; i < 2; i++) {
        uint8_t bit = i ? ST_P2_STAND : ST_P1_STAND;
        if (!(OB8(o, OBJ_status) & bit)) continue;
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
        uint32_t p = o_player(i);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | 2) & ~8);
    }
}

/* ------------------------------------------------------------------------
 * Object 1E - Ball Hog; object 20 - its cannonball
 * ---------------------------------------------------------------------- */
#define HOG_LAUNCH  0x32
#define CB_TIME     0x30

static void ball_hog(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {            /* Hog_Main */
        SET8(o, OBJ_y_radius, 0x13);
        SET8(o, OBJ_x_radius, 8);
        o_init(o, S1A_HOG, 4, 4, 0x0C, 0x14);
        SET8(o, OBJ_coll_flags, 5);
        o_fall(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 >= 0) return;
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        SET16(o, OBJ_y_vel, 0);
        SET8(o, OBJ_routine, 2);
        return;
    }
    o_animate(o, o_asset_anim(S1A_HOG));
    if (OB8(o, OBJ_frame) != 1) {
        SET8(o, HOG_LAUNCH, 0);
    } else if (!OB8(o, HOG_LAUNCH)) {
        SET8(o, HOG_LAUNCH, 1);
        uint32_t a1 = o_alloc();
        if (a1) {
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x20 * 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, OBJ_x_vel, -0x100);
            SET16(a1, OBJ_y_vel, 0);
            int d0 = -4;
            if (OB8(o, OBJ_status) & 1) { d0 = 4; SET16(a1, OBJ_x_vel, 0x100); }
            SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + d0);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x0C);
            SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
        }
    }
    remember(o);
}

static void cannonball(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_y_radius, 7);
        o_init(o, S1A_HOG, 4, 3, 8, 8);
        SET8(o, OBJ_coll_flags, 0x87);
        SET16(o, CB_TIME, OB8(o, OBJ_subtype) * 60);
        SET8(o, OBJ_frame, 4);
    }
    o_fall(o);
    if ((int16_t)OB16(o, OBJ_y_vel) >= 0) {
        int16_t d1 = o_floor_dist(o);
        int8_t ang = (int8_t)(uint8_t)D(3);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, OBJ_y_vel, -0x300);
            if (ang < 0) { if (OBS16(o, OBJ_x_vel) >= 0) SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel)); }
            else if (ang > 0) { if (OBS16(o, OBJ_x_vel) < 0) SET16(o, OBJ_x_vel, (uint16_t)-OBS16(o, OBJ_x_vel)); }
        }
    }
    SET16(o, CB_TIME, OB16(o, CB_TIME) - 1);
    if ((int16_t)OB16(o, CB_TIME) < 0) {       /* Cbal_Explode: id_Explosion ($3F) */
        SET32(o, OBJ_code, S3K_Obj_Explosion);
        SET8(o, OBJ_routine, 6);
        o_sfx(0xB4);
        return;
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
        SET8(o, OBJ_anim_timer, 5);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
    }
    if ((uint16_t)(r16(RAM_Camera_max_Y_pos) + 0xE0) < OB16(o, OBJ_y_pos)) { o_delete(o); return; }
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 2A - small vertical door (opens for Sonic from one side)
 * ---------------------------------------------------------------------- */
static void auto_door(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_ADOOR, OB8(o, OBJ_render) | 4, 4, 8, 0x20);
    }
    SET8(o, OBJ_anim, 0);                      /* closing */
    uint16_t px = r16(RAM_Player_1 + OBJ_x_pos), ox = OB16(o, OBJ_x_pos);
    int flip = OB8(o, OBJ_status) & 1;
    if ((uint16_t)(px + 0x40) >= ox && (uint16_t)(px - 0x40) < ox) {
        if (px >= ox ? flip : !flip) SET8(o, OBJ_anim, 1);   /* opening */
    }
    o_animate(o, o_asset_anim(S1A_ADOOR));
    if (!OB8(o, OBJ_frame)) o_solid(o, 0x11, 0x20, 0x21, ox);
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 66 - rotating junction that grabs Sonic
 * ---------------------------------------------------------------------- */
#define JUN_TIMER   0x30
#define JUN_GRAB    0x32
#define JUN_DIR     0x34
#define JUN_REV     0x36
#define JUN_SWITCH  0x38

static void jun_switch(uint32_t o)               /* Jun_ChkSwitch */
{
    if (sw_bit0(OB8(o, JUN_SWITCH))) {
        if (!OB8(o, JUN_REV)) {
            SET8(o, JUN_DIR, (uint8_t)-(int8_t)OB8(o, JUN_DIR));
            SET8(o, JUN_REV, 1);
        }
    } else {
        SET8(o, JUN_REV, 0);
    }
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) return;
    SET8(o, OBJ_anim_timer, 7);
    SET8(o, OBJ_frame, (OB8(o, OBJ_frame) + OB8(o, JUN_DIR)) & 0x0F);
}

static void jun_chgpos(uint32_t o)               /* Jun_ChgPos: Sonic rides the gap */
{
    static const int8_t data[16][2] = {
        { -0x20, 0 }, { -0x1E, 0x0E }, { -0x18, 0x18 }, { -0x0E, 0x1E }, { 0, 0x20 }, { 0x0E, 0x1E },
        { 0x18, 0x18 }, { 0x1E, 0x0E }, { 0x20, 0 }, { 0x1E, -0x0E }, { 0x18, -0x18 }, { 0x0E, -0x1E },
        { 0, -0x20 }, { -0x0E, -0x1E }, { -0x18, -0x18 }, { -0x1E, -0x0E } };
    uint32_t p = RAM_Player_1;
    const int8_t *d = data[OB8(o, OBJ_frame) & 0x0F];
    SET16(p, OBJ_x_pos, (uint16_t)(OB16(o, OBJ_x_pos) + d[0]));
    SET16(p, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) + d[1]));
}

static void junction(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        uint32_t a1 = o_alloc();
        if (a1) {                              /* the large disc behind */
            SET32(a1, OBJ_code, NATIVE_S1_BASE + 0x66 * 4);
            SET8(a1, OBJ_routine, 4);
            o_init(a1, S1A_JUN, 4, 3, 0x38, 0x38);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_frame, 0x10);
        }
        o_init(o, S1A_JUN, OB8(o, OBJ_render) | 4, 4, 0x30, 0x38);
        SET16(o, JUN_TIMER, 0x3C);
        SET8(o, JUN_DIR, 1);
        SET8(o, JUN_SWITCH, OB8(o, OBJ_subtype));
    }
    /* fall through */
    case 2:                                    /* Jun_Action */
        jun_switch(o);
        if (OB8(o, OBJ_render) & 0x80) {
            o_solid(o, 0x30, 0x30, 0x31, OB16(o, OBJ_x_pos));
            if (OB8(o, OBJ_status) & ST_P1_PUSH) {
                uint8_t d1 = OB16(p, OBJ_x_pos) < OB16(o, OBJ_x_pos) ? 0x0E : 7;
                if (OB8(o, OBJ_frame) == d1) {    /* the gap faces Sonic: grab him */
                    SET8(o, JUN_GRAB, d1);
                    SET8(o, OBJ_routine, 6);
                    SET8(p, OBJ_object_ctrl, 1);
                    SET8(p, OBJ_anim, ANI_ROLL);
                    SET16(p, OBJ_ground_vel, 0x800);
                    SET16(p, OBJ_x_vel, 0);
                    SET16(p, OBJ_y_vel, 0);
                    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH);
                    SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x20) | 2);
                    uint16_t x0 = OB16(p, OBJ_x_pos), y0 = OB16(p, OBJ_y_pos);
                    jun_chgpos(o);
                    SET16(p, OBJ_x_pos, (uint16_t)((int16_t)(OB16(p, OBJ_x_pos) + x0) >> 1));
                    SET16(p, OBJ_y_pos, (uint16_t)((int16_t)(OB16(p, OBJ_y_pos) + y0) >> 1));
                }
            }
        }
        break;
    case 6: {                                  /* Jun_Release */
        uint8_t d0 = OB8(o, OBJ_frame);
        if ((d0 == 4 || d0 == 7) && d0 != OB8(o, JUN_GRAB)) {
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, 0x800);
            if (d0 != 4) SET16(p, OBJ_x_vel, 0x800);
            SET8(p, OBJ_object_ctrl, 0);
            SET8(o, OBJ_routine, 2);
        }
        jun_switch(o);
        jun_chgpos(o);
        break;
    }
    default:                                   /* Jun_Display */
        break;
    }
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 67 - disc that you run around
 * ---------------------------------------------------------------------- */
#define DISC_ORIGY   0x30
#define DISC_ORIGX   0x32
#define DISC_SPOT    0x34
#define DISC_SPEED   0x36
#define DISC_RADIUS  0x38
#define DISC_ON      0x3A
#define DISC_ANGLE   0x26             /* word */

static void running_disc(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_DISC, 4, 4, 8, 8);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET16(o, DISC_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, DISC_ORIGY, OB16(o, OBJ_y_pos));
        SET8(o, DISC_SPOT, 0x18);
        SET8(o, DISC_RADIUS, 0x48);
        if (OB8(o, OBJ_subtype) & 0x0F) { SET8(o, DISC_SPOT, 0x10); SET8(o, DISC_RADIUS, 0x38); }
        SET16(o, DISC_SPEED, (uint16_t)((int16_t)(int8_t)(OB8(o, OBJ_subtype) & 0xF0) << 3));
        SET16(o, DISC_ANGLE, (uint16_t)((OB8(o, OBJ_status) & 3) << 14));
    }
    /* Disc_MoveSonic */
    uint32_t p = RAM_Player_1;
    uint16_t r = OB8(o, DISC_RADIUS);
    uint16_t dx = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, DISC_ORIGX) + r);
    uint16_t dy = (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, DISC_ORIGY) + r);
    if (dx >= 2 * r || dy >= 2 * r) {
        if (OB8(o, DISC_ON)) { SET8(p, OBJ_stick_convex, 0); SET8(o, DISC_ON, 0); }
    } else if (OB8(p, OBJ_status) & 2) {
        SET8(o, DISC_ON, 0);
    } else {
        if (!OB8(o, DISC_ON)) {
            SET8(o, DISC_ON, 1);
            if (!(OB8(p, OBJ_status) & 4)) SET8(p, OBJ_anim, 0);
            SET8(p, OBJ_status, OB8(p, OBJ_status) & ~0x20);
            SET8(p, OBJ_prev_anim, 1);         /* id_Run: restart the animation */
            SET8(p, OBJ_stick_convex, 1);
        }
        int16_t v = (int16_t)OB16(p, OBJ_ground_vel);
        if ((int16_t)OB16(o, DISC_SPEED) < 0) {
            if (v > -0x400) v = -0x400; else if (v < -0xF00) v = -0xF00;
        } else {
            if (v < 0x400) v = 0x400; else if (v > 0xF00) v = 0xF00;
        }
        SET16(p, OBJ_ground_vel, (uint16_t)v);
    }
    /* Disc_MoveSpot */
    SET16(o, DISC_ANGLE, OB16(o, DISC_ANGLE) + OB16(o, DISC_SPEED));
    int16_t s, c;
    o_sincos(OB8(o, DISC_ANGLE), &s, &c);
    int32_t d4 = (int32_t)OB8(o, DISC_SPOT) << 8;
    SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, DISC_ORIGY) + (int16_t)((d4 * s) >> 16)));
    SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, DISC_ORIGX) + (int16_t)((d4 * c) >> 16)));
    if (chk_del(o, OB16(o, DISC_ORIGX))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 68 - conveyor belts (invisible: they move whoever runs on them)
 * ---------------------------------------------------------------------- */
#define CONV_SPEED  0x36
#define CONV_WIDTH  0x38

static void conveyor_belt(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        SET8(o, CONV_WIDTH, (OB8(o, OBJ_subtype) & 0x0F) ? 56 : 128);
        SET16(o, CONV_SPEED, (uint16_t)((int16_t)(int8_t)(OB8(o, OBJ_subtype) & 0xF0) >> 4));
    }
    uint16_t w = OB8(o, CONV_WIDTH);
    for (int i = 0; i < 2; i++) {
        uint32_t p = o_player(i);
        if (!o_player_active(p)) continue;
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + w) >= 2 * w) continue;
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x30) >= 0x30) continue;
        if (OB8(p, OBJ_status) & 2) continue;
        SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + OB16(o, CONV_SPEED));
    }
    chk_del(o, OB16(o, OBJ_x_pos));
}

/* ------------------------------------------------------------------------
 * Object 69 - trapdoors and spinning platforms
 * ---------------------------------------------------------------------- */
#define SPIN_TIMER    0x30
#define SPIN_TIMELEN  0x32
#define SPIN_GO       0x34
#define SPIN_MASK     0x36

static void spin_platform(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        uint8_t sub = OB8(o, OBJ_subtype);
        o_init(o, S1A_TRAP, OB8(o, OBJ_render) | 4, 0, 0x80, 0x10);
        SET16(o, SPIN_TIMELEN, (sub & 0x0F) * 0x3C);
        if (sub & 0x80) {
            SET8(o, OBJ_routine, 4);
            o_init(o, S1A_SPIN, OB8(o, OBJ_render) | 4, 0, 0x10, 0x10);
            SET8(o, OBJ_anim, 2);
            SET16(o, SPIN_TIMER, (sub & 0x0F) * 6);
            SET16(o, SPIN_TIMELEN, (sub & 0x0F) * 6);
            SET16(o, SPIN_MASK, (uint16_t)((((sub & 0x70) + 0x10) << 2) - 1));
            goto spinner;
        }
    }
    /* fall through */
    case 2:                                    /* Spin_Trapdoor */
        SET16(o, SPIN_TIMER, OB16(o, SPIN_TIMER) - 1);
        if ((int16_t)OB16(o, SPIN_TIMER) < 0) {
            SET16(o, SPIN_TIMER, OB16(o, SPIN_TIMELEN));
            SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
            if (OB8(o, OBJ_render) & 0x80) o_sfx(SFX_DOOR);
        }
        o_animate(o, o_asset_anim(S1A_TRAP));
        if (!OB8(o, OBJ_frame)) o_solid(o, 0x4B, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
        else drop_players(o);
        break;
    default:                                   /* Spin_Spinner */
    spinner:
        if (!(r16(RAM_Level_frame_counter) & OB16(o, SPIN_MASK))) SET8(o, SPIN_GO, 1);
        if (OB8(o, SPIN_GO)) {
            SET16(o, SPIN_TIMER, OB16(o, SPIN_TIMER) - 1);
            if ((int16_t)OB16(o, SPIN_TIMER) < 0) {
                SET16(o, SPIN_TIMER, OB16(o, SPIN_TIMELEN));
                SET8(o, SPIN_GO, 0);
                SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
            }
        }
        o_animate(o, o_asset_anim(S1A_SPIN));
        if (!OB8(o, OBJ_frame)) o_solid(o, 0x1B, 7, 8, OB16(o, OBJ_x_pos));
        else drop_players(o);
        break;
    }
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 6A - pizza cutters (0-2) and ground saws (3-4)
 * ---------------------------------------------------------------------- */
#define SAW_ORIGY  0x38
#define SAW_ORIGX  0x3A
#define SAW_HERE   0x3D

static void saw_frame(uint32_t o)
{
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) return;
    SET8(o, OBJ_anim_timer, 2);
    SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 1);
}

static void saws(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_SAW, 4, 4, 0x20, 0x20);
        SET16(o, SAW_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, SAW_ORIGY, OB16(o, OBJ_y_pos));
        if (OB8(o, OBJ_subtype) < 3) SET8(o, OBJ_coll_flags, 0xA2);
    }
    uint32_t p = RAM_Player_1;
    int flip = OB8(o, OBJ_status) & 1;
    switch (OB8(o, OBJ_subtype) & 7) {
    case 1: {
        int16_t d0 = o_osc(0x0E);
        if (flip) d0 = (int16_t)(0x60 - d0);
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, SAW_ORIGX) - d0));
        saw_frame(o);
        if ((OB8(o, OBJ_render) & 0x80) && !(r16(RAM_Level_frame_counter) & 0x0F)) o_sfx(SFX_SAW);
        break;
    }
    case 2: {
        int16_t d0 = o_osc(0x06);
        if (flip) d0 = (int16_t)(0x80 - d0);
        SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, SAW_ORIGY) - d0));
        saw_frame(o);
        if ((OB8(o, OBJ_render) & 0x80) && o_osc(0x06) == 0x18) o_sfx(SFX_SAW);
        break;
    }
    case 3: case 4: {
        if (OB8(o, SAW_HERE)) {
            o_move(o);
            SET16(o, SAW_ORIGX, OB16(o, OBJ_x_pos));
            saw_frame(o);
            break;
        }
        uint16_t px = OB16(p, OBJ_x_pos), py = OB16(p, OBJ_y_pos), ox = OB16(o, OBJ_x_pos);
        int xok = (OB8(o, OBJ_subtype) & 7) == 3
            ? (px >= 0xC0 && (uint16_t)(px - 0xC0) >= ox)          /* Sonic has passed it */
            : ((uint16_t)(px + 0xE0) < ox);                         /* Sonic is well left */
        if (!xok) return;                      /* addq.l #4,sp: not drawn */
        uint16_t d0 = (uint16_t)(py - 0x80);
        if (d0 >= OB16(o, OBJ_y_pos) || (uint16_t)(d0 + 0x100) < OB16(o, OBJ_y_pos)) break;
        SET8(o, SAW_HERE, 1);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_subtype) & 7) == 3 ? 0x600 : -0x600);
        SET8(o, OBJ_coll_flags, 0xA2);
        SET8(o, OBJ_frame, 2);
        o_sfx(SFX_SAW);
        return;                                /* also skips the draw (addq.l #4,sp) */
    }
    default:
        break;
    }
    if (chk_del(o, OB16(o, SAW_ORIGX))) return;
    o_touch(o);
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 6B - stompers and sliding doors (SBZ1/2; SBZ3's big door)
 * ---------------------------------------------------------------------- */
#define STO_ORIGY   0x30
#define STO_ORIGX   0x34
#define STO_TIME    0x36
#define STO_ACTIVE  0x38
#define STO_POS     0x3A
#define STO_DIST    0x3C
#define STO_SWITCH  0x3E

static uint8_t s_obj6B;                        /* v_obj6B (SBZ3: one door at a time) */
void s1_obj_sbz_reset(void) { s_obj6B = 0; }

static void sto_respawn_bit0(uint32_t o, int set)
{
    uint16_t r = OB16(o, OBJ_respawn);
    if (!r) return;
    uint32_t a2 = 0xFF0000u | r;
    w8(a2, (uint8_t)(set ? (r8(a2) | 1) : (r8(a2) & ~1)));
}

static void sto_posx(uint32_t o)
{
    int16_t d0 = (int16_t)OB16(o, STO_POS);
    if (OB8(o, OBJ_status) & 1) d0 = (int16_t)(0x80 - d0);
    SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, STO_ORIGX) - d0));
}

static void sto_posy(uint32_t o)
{
    int16_t d0 = (int16_t)OB16(o, STO_POS);
    if (OB8(o, OBJ_status) & 1) d0 = (int16_t)(0x38 - d0);
    SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, STO_ORIGY) + d0));
}

static void stomper(uint32_t o)
{
    static const uint8_t var[5][4] = {         /* width, height, distance, type */
        { 0x40, 0x0C, 0x80, 1 }, { 0x1C, 0x20, 0x38, 3 }, { 0x1C, 0x20, 0x40, 4 },
        { 0x1C, 0x20, 0x60, 4 }, { 0x80, 0x40, 0x00, 5 } };
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        uint8_t sub = OB8(o, OBJ_subtype);
        int k = ((sub >> 2) & 0x1C) >> 2;
        if (k > 4) k = 4;
        o_init(o, S1A_STOMP, OB8(o, OBJ_render) | 4, 4, var[k][0], var[k][1]);
        SET8(o, OBJ_y_radius, var[k][1]);
        SET8(o, OBJ_frame, k);
        if (tri_zone() == TRI_S1_LZ) {         /* SBZ3 */
            uint8_t was = s_obj6B;
            s_obj6B |= 1;
            int gone = (was & 1);
            if (!gone && OB16(o, OBJ_x_pos) == 0xA80) {
                uint16_t r = OB16(o, OBJ_respawn);
                if (r && (r8(0xFF0000u | r) & 1)) { s_obj6B = 0; gone = 1; }
            }
            if (gone) { o_delete_respawnable(o); return; }
        }
        SET16(o, STO_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, STO_ORIGY, OB16(o, OBJ_y_pos));
        SET16(o, STO_DIST, var[k][2]);
        if (sub & 0x80) {
            SET8(o, STO_SWITCH, sub & 0x0F);
            SET8(o, OBJ_subtype, var[k][3]);
            if (var[k][3] == 5) SET8(o, OBJ_render, OB8(o, OBJ_render) | 0x10);
            uint16_t r = OB16(o, OBJ_respawn);
            if (r) w8(0xFF0000u | r, (uint8_t)(r8(0xFF0000u | r) & 0x7F));
        }
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    switch (OB8(o, OBJ_subtype) & 0x0F) {
    case 1:                                    /* slide open on a switch */
        if (!OB8(o, STO_ACTIVE)) {
            if (!sw_bit0(OB8(o, STO_SWITCH))) { sto_posx(o); break; }
            SET8(o, STO_ACTIVE, 1);
        }
        if (OB16(o, STO_DIST) == OB16(o, STO_POS)) {
            SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);
            SET16(o, STO_TIME, 0xB4);
            SET8(o, STO_ACTIVE, 0);
            sto_respawn_bit0(o, 1);
        } else {
            SET16(o, STO_POS, OB16(o, STO_POS) + 2);
        }
        sto_posx(o);
        break;
    case 2:                                    /* ...and shut again */
        if (!OB8(o, STO_ACTIVE)) {
            SET16(o, STO_TIME, OB16(o, STO_TIME) - 1);
            if (OB16(o, STO_TIME)) { sto_posx(o); break; }
            SET8(o, STO_ACTIVE, 1);
        }
        if (!OB16(o, STO_POS)) {
            SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) - 1);
            SET8(o, STO_ACTIVE, 0);
            sto_respawn_bit0(o, 0);
        } else {
            SET16(o, STO_POS, OB16(o, STO_POS) - 2);
        }
        sto_posx(o);
        break;
    case 3:                                    /* stomper: slams down, creeps back */
        if (!OB8(o, STO_ACTIVE)) {
            if (OB16(o, STO_POS)) { SET16(o, STO_POS, OB16(o, STO_POS) - 1); sto_posy(o); break; }
            SET16(o, STO_TIME, OB16(o, STO_TIME) - 1);
            if ((int16_t)OB16(o, STO_TIME) >= 0) { sto_posy(o); break; }
            SET16(o, STO_TIME, 0x3C);
            SET8(o, STO_ACTIVE, 1);
        }
        SET16(o, STO_POS, OB16(o, STO_POS) + 8);
        if (OB16(o, STO_POS) == OB16(o, STO_DIST)) SET8(o, STO_ACTIVE, 0);
        sto_posy(o);
        break;
    case 4:                                    /* stomper: down, wait, up */
        if (!OB8(o, STO_ACTIVE)) {
            if (OB16(o, STO_POS)) { SET16(o, STO_POS, OB16(o, STO_POS) - 8); sto_posy(o); break; }
            SET16(o, STO_TIME, OB16(o, STO_TIME) - 1);
            if ((int16_t)OB16(o, STO_TIME) >= 0) { sto_posy(o); break; }
            SET16(o, STO_TIME, 0x3C);
            SET8(o, STO_ACTIVE, 1);
        }
        if (OB16(o, STO_POS) == OB16(o, STO_DIST)) {
            SET16(o, STO_TIME, OB16(o, STO_TIME) - 1);
            if ((int16_t)OB16(o, STO_TIME) < 0) { SET16(o, STO_TIME, 0x3C); SET8(o, STO_ACTIVE, 0); }
        } else {
            SET16(o, STO_POS, OB16(o, STO_POS) + 8);
        }
        sto_posy(o);
        break;
    case 5:                                    /* SBZ3 big door slides down-left */
        if (!OB8(o, STO_ACTIVE)) {
            if (!sw_bit0(OB8(o, STO_SWITCH))) break;
            SET8(o, STO_ACTIVE, 1);
            sto_respawn_bit0(o, 1);
        }
        SET32(o, OBJ_x_pos, r32(o + OBJ_x_pos) - 0x10000u);
        SET32(o, OBJ_y_pos, r32(o + OBJ_y_pos) + 0x8000u);
        SET16(o, STO_ORIGX, OB16(o, OBJ_x_pos));
        if (OB16(o, OBJ_x_pos) == 0x980) { SET8(o, OBJ_subtype, 0); SET8(o, STO_ACTIVE, 0); }
        break;
    default:
        break;
    }
    if (OB8(o, OBJ_render) & 0x80) {
        int d2 = OB8(o, OBJ_y_radius);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, d2, d2 + 1, oldx);
    }
    if (o_out_of_range(o, OB16(o, STO_ORIGX))) {
        if (tri_zone() == TRI_S1_LZ) {
            s_obj6B = 0;
            o_delete_respawnable(o);
        } else {
            o_delete(o);
        }
        return;
    }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 6C - vanishing platforms
 * ---------------------------------------------------------------------- */
#define VAN_TIMER    0x30
#define VAN_TIMELEN  0x32
#define VAN_PHASE    0x36
#define VAN_MASK     0x38

static void vanish_platform(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 6);
        o_init(o, S1A_VANP, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        uint8_t sub = OB8(o, OBJ_subtype);
        uint16_t d1 = (uint16_t)(((sub & 0x0F) + 1) << 7);
        SET16(o, VAN_TIMER, d1 - 1);
        SET16(o, VAN_TIMELEN, d1 - 1);
        SET16(o, VAN_PHASE, (uint16_t)(((uint32_t)(sub & 0xF0) * (uint32_t)(d1 + 0x80)) >> 8));
        SET16(o, VAN_MASK, d1 + 0x80 - 1);
    }
    if (OB8(o, OBJ_routine) == 6) {            /* VanP_Sync */
        if (((r16(RAM_Level_frame_counter) - OB16(o, VAN_PHASE)) & OB16(o, VAN_MASK)) != 0) {
            o_animate(o, o_asset_anim(S1A_VANP));
            remember(o);
            return;
        }
        SET8(o, OBJ_routine, 2);
    }
    SET16(o, VAN_TIMER, OB16(o, VAN_TIMER) - 1);
    if ((int16_t)OB16(o, VAN_TIMER) < 0) {
        SET16(o, VAN_TIMER, 127);
        if (OB8(o, OBJ_anim)) SET16(o, VAN_TIMER, OB16(o, VAN_TIMELEN));
        SET8(o, OBJ_anim, OB8(o, OBJ_anim) ^ 1);
    }
    o_animate(o, o_asset_anim(S1A_VANP));
    if (!(OB8(o, OBJ_frame) & 2)) o_platform(o, OB8(o, OBJ_width_px), 8, OB16(o, OBJ_x_pos));
    else drop_players(o);
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 6D - flamethrower; object 6E - electric orbs
 * ---------------------------------------------------------------------- */
static void flamethrower(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_FLAME, OB8(o, OBJ_render) | 4, 1, 0x0C, 0x20);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        uint16_t t = (uint16_t)((OB8(o, OBJ_subtype) & 0xF0) * 2);
        SET16(o, 0x30, t);
        SET16(o, 0x32, t);
        SET16(o, 0x34, (uint16_t)((OB8(o, OBJ_subtype) & 0x0F) << 5));
        SET8(o, 0x36, 0x0A);
        if (OB8(o, OBJ_status) & 2) { SET8(o, OBJ_anim, 2); SET8(o, 0x36, 0x15); }
    }
    SET16(o, 0x30, OB16(o, 0x30) - 1);
    if ((int16_t)OB16(o, 0x30) < 0) {
        SET16(o, 0x30, OB16(o, 0x34));         /* pause */
        uint8_t a = OB8(o, OBJ_anim);
        SET8(o, OBJ_anim, a ^ 1);
        if (a & 1) {                           /* flaming again */
            SET16(o, 0x30, OB16(o, 0x32));
            o_sfx(SFX_FLAME);
        }
    }
    o_animate(o, o_asset_anim(S1A_FLAME));
    SET8(o, OBJ_coll_flags, OB8(o, OBJ_frame) == OB8(o, 0x36) ? 0xA3 : 0);
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_touch(o);
    o_draw(o);
}

static void electrocuter(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_ELEC, OB8(o, OBJ_render) | 4, 0, 0x28, 0x10);
        SET16(o, 0x34, (uint16_t)((OB8(o, OBJ_subtype) << 4) - 1));
    }
    if (!(r16(RAM_Level_frame_counter) & OB16(o, 0x34))) {
        SET8(o, OBJ_anim, 1);
        if (OB8(o, OBJ_render) & 0x80) o_sfx(SFX_ZAP);
    }
    o_animate(o, o_asset_anim(S1A_ELEC));
    SET8(o, OBJ_coll_flags, OB8(o, OBJ_frame) == 4 ? 0xA4 : 0);
    remember(o);
}

/* ------------------------------------------------------------------------
 * Object 70 - large girder block
 * ---------------------------------------------------------------------- */
#define GIRD_ORIGY  0x30
#define GIRD_ORIGX  0x32
#define GIRD_TIME   0x34
#define GIRD_SET    0x38
#define GIRD_DELAY  0x3A

static void gird_chgmove(uint32_t o)
{
    static const int16_t settings[4][3] = { { 0x100, 0, 0x60 }, { 0, 0x100, 0x30 }, { -0x100, -0x40, 0x60 }, { 0, -0x100, 0x18 } };
    int k = (OB8(o, GIRD_SET) & 0x18) >> 3;
    SET16(o, OBJ_x_vel, (uint16_t)settings[k][0]);
    SET16(o, OBJ_y_vel, (uint16_t)settings[k][1]);
    SET16(o, GIRD_TIME, (uint16_t)settings[k][2]);
    SET8(o, GIRD_SET, OB8(o, GIRD_SET) + 8);
    SET16(o, GIRD_DELAY, 7);
}

static void girder(uint32_t o)
{
    if (OB8(o, OBJ_routine) == 0) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S1A_GIRD, OB8(o, OBJ_render) | 4, 4, 0x60, 0x18);
        SET8(o, OBJ_y_radius, 0x18);
        SET16(o, GIRD_ORIGX, OB16(o, OBJ_x_pos));
        SET16(o, GIRD_ORIGY, OB16(o, OBJ_y_pos));
        gird_chgmove(o);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    int go = 1;
    if (OB16(o, GIRD_DELAY)) {
        SET16(o, GIRD_DELAY, OB16(o, GIRD_DELAY) - 1);
        if (OB16(o, GIRD_DELAY)) go = 0;
    }
    if (go) {
        o_move(o);
        SET16(o, GIRD_TIME, OB16(o, GIRD_TIME) - 1);
        if (!OB16(o, GIRD_TIME)) gird_chgmove(o);
    }
    if (OB8(o, OBJ_render) & 0x80) o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 0x18, 0x19, oldx);
    if (chk_del(o, OB16(o, GIRD_ORIGX))) return;
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 72 - teleporter (invisible): rolls Sonic along a fixed route
 * ---------------------------------------------------------------------- */
#define TELE_FRAMES  0x2E             /* word, the high byte counts frames */
#define TELE_BUMP    0x32
#define TELE_TX      0x36
#define TELE_TY      0x38
#define TELE_IDX     0x3A
#define TELE_LEN     0x3B
#define TELE_TYPE    0x3C

static const uint16_t tele_route[8][11] = {     /* bytes (4 per point), then x, y pairs */
    { 4, 0x794, 0x98C },
    { 4, 0x94, 0x38C },
    { 0x1C, 0x794, 0x2E8, 0x7A4, 0x2C0, 0x7D0, 0x2AC, 0x858, 0x2AC, 0x884, 0x298 },
    { 4, 0x894, 0x690 },
    { 0x1C, 0x1194, 0x470, 0x1184, 0x498, 0x1158, 0x4AC, 0xFD0, 0x4AC, 0xFA4, 0x4C0 },
    { 4, 0x1294, 0x490 },
    { 0x1C, 0x1594, 0xFFE8, 0x1584, 0xFFC0, 0x1560, 0xFFAC, 0x14D0, 0xFFAC, 0x14A4, 0xFF98 },
    { 4, 0x894, 0x90 } };
static const uint16_t tele_tail[8][4] = {       /* the last two points of the long routes */
    { 0 }, { 0 }, { 0x894, 0x270, 0x894, 0x190 }, { 0 },
    { 0xF94, 0x4E8, 0xF94, 0x590 }, { 0 }, { 0x1494, 0xFF70, 0x1494, 0xFD90 }, { 0 } };

static void tele_point(int type, int idx, uint16_t *x, uint16_t *y)
{
    int n = idx / 4;                           /* point number */
    if (n < 5) { *x = tele_route[type][1 + n * 2]; *y = tele_route[type][2 + n * 2]; }
    else { *x = tele_tail[type][(n - 5) * 2]; *y = tele_tail[type][(n - 5) * 2 + 1]; }
}

static void tele_aim(uint32_t o)                 /* sub_1681C */
{
    uint32_t p = RAM_Player_1;
    int16_t dx = (int16_t)(OB16(o, TELE_TX) - OB16(p, OBJ_x_pos));
    int16_t dy = (int16_t)(OB16(o, TELE_TY) - OB16(p, OBJ_y_pos));
    int16_t d2 = 0x1000, d3 = 0x1000;
    uint16_t ax = (uint16_t)dx, ay = (uint16_t)dy;
    if (dx < 0) { ax = (uint16_t)-dx; d2 = -0x1000; }
    if (dy < 0) { ay = (uint16_t)-dy; d3 = -0x1000; }
    if (ay >= ax) {                            /* mostly vertical */
        int16_t d1 = (int16_t)(((int32_t)dy << 16) / d3);
        int16_t d0 = 0;
        if (dx) d0 = d1 ? (int16_t)(((int32_t)dx << 16) / d1) : 0;
        SET16(p, OBJ_x_vel, (uint16_t)d0);
        SET16(p, OBJ_y_vel, (uint16_t)d3);
        SET16(o, TELE_FRAMES, (uint16_t)(d1 < 0 ? -d1 : d1));
    } else {
        int16_t d0 = (int16_t)(((int32_t)dx << 16) / d2);
        int16_t d1 = 0;
        if (dy) d1 = d0 ? (int16_t)(((int32_t)dy << 16) / d0) : 0;
        SET16(p, OBJ_y_vel, (uint16_t)d1);
        SET16(p, OBJ_x_vel, (uint16_t)d2);
        SET16(o, TELE_FRAMES, (uint16_t)(d0 < 0 ? -d0 : d0));
    }
}

static void teleporter(uint32_t o)
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_routine)) {
    case 0: {
        SET8(o, OBJ_routine, 2);
        int type = OB8(o, OBJ_subtype) & 7;
        SET8(o, TELE_TYPE, type);
        SET8(o, TELE_IDX, 0);
        SET8(o, TELE_LEN, (uint8_t)tele_route[type][0]);
        uint16_t x, y;
        tele_point(type, 0, &x, &y);
        SET16(o, TELE_TX, x);
        SET16(o, TELE_TY, y);
    }
    /* fall through */
    case 2: {                                  /* Tele_Action: Sonic steps in */
        uint16_t d0 = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (OB8(o, OBJ_status) & 1) d0 = (uint16_t)(d0 + 0x0F);
        if (d0 >= 0x10) break;
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x20) >= 0x40) break;
        if (OB8(p, OBJ_object_ctrl)) break;
        if (OB8(o, OBJ_subtype) == 7 && r16(RAM_Ring_count) < 50) break;
        SET8(o, OBJ_routine, 4);
        SET8(p, OBJ_object_ctrl, 0x81);
        SET8(p, OBJ_anim, ANI_ROLL);
        SET16(p, OBJ_ground_vel, 0x800);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~0x20);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~0x20) | 2);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET8(o, TELE_BUMP, 0);
        o_sfx(SFX_ROLL);
        break;
    }
    case 4: {                                  /* Tele_Bump: hop in place */
        uint8_t a = OB8(o, TELE_BUMP);
        SET8(o, TELE_BUMP, a + 2);
        int16_t s, c;
        o_sincos(a, &s, &c);
        SET16(p, OBJ_y_pos, (uint16_t)(OB16(o, OBJ_y_pos) - (s >> 5)));
        if (OB8(o, TELE_BUMP) == 0x80) {
            tele_aim(o);
            SET8(o, OBJ_routine, 6);
            o_sfx(SFX_TELE);
        }
        break;
    }
    default: {                                 /* Tele_Bend: fly the route */
        int8_t t = (int8_t)(OB8(o, TELE_FRAMES) - 1);
        SET8(o, TELE_FRAMES, (uint8_t)t);
        if (t >= 0) {
            SET32(p, OBJ_x_pos, r32(p + OBJ_x_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_x_vel) << 8));
            SET32(p, OBJ_y_pos, r32(p + OBJ_y_pos) + (uint32_t)((int32_t)OBS16(p, OBJ_y_vel) << 8));
            return;                            /* addq.l #4,sp: no range check */
        }
        SET16(p, OBJ_x_pos, OB16(o, TELE_TX));
        SET16(p, OBJ_y_pos, OB16(o, TELE_TY));
        uint8_t d1 = (uint8_t)(OB8(o, TELE_IDX) + 4);
        if (d1 >= OB8(o, TELE_LEN)) {          /* arrived */
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);
            SET8(o, OBJ_routine, 0);
            SET8(p, OBJ_object_ctrl, 0);
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, 0x200);
            return;
        }
        SET8(o, TELE_IDX, d1);
        uint16_t x, y;
        tele_point(OB8(o, TELE_TYPE), d1, &x, &y);
        SET16(o, TELE_TX, x);
        SET16(o, TELE_TY, y);
        tele_aim(o);
        return;
    }
    }
    chk_del(o, OB16(o, OBJ_x_pos));
}

void s1_obj_sbz_register(TriObjFn *t)
{
    t[0x1E] = ball_hog;
    t[0x20] = cannonball;
    t[0x2A] = auto_door;
    t[0x66] = junction;
    t[0x67] = running_disc;
    t[0x68] = conveyor_belt;
    t[0x69] = spin_platform;
    t[0x6A] = saws;
    t[0x6B] = stomper;
    t[0x6C] = vanish_platform;
    t[0x6D] = flamethrower;
    t[0x6E] = electrocuter;
    t[0x70] = girder;
    t[0x72] = teleporter;
}
