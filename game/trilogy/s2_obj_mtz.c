/*
 * s2_obj_mtz.c — Sonic 2 Metropolis objects, transcribed from s2disasm:
 * steam spring (42), button (47), twin stompers (64), moving block + cog
 * (65), spring wall (66), spin tube (67), block with rotating spike (68),
 * nut (69), lava cups (6C), floor spike (6D), moving gear (6E), big wheel
 * (70), lava bubble (71) [conveyor 72: s2_obj_cnz.c], Shellcracker + claw (9F/A0),
 * Slicer + pincers (A1/A2), Asteron (A4).
 *
 * S2 variables at objoff_2A-2F would hit S3K's status/subtype, so they live
 * at $30+ here. ButtonVine_Trigger is S3K's Level_trigger_array.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define OBJ_prev_anim   0x21
#define P_OBJCTRL       0x2E
#define P_MOVE_LOCK     0x32
#define P_SPINDASH      0x3D
#define P_TOP_SOLID     0x46
#define P_LRB_SOLID     0x47
#define P_FLIP_ANGLE    0x27
#define P_FLIPS_LEFT    0x30
#define P_FLIP_SPEED    0x31
#define ST_INAIR        0x02
#define ST_ROLL         0x04
#define ST_ONOBJ        0x08
#define ST_ROLLJUMP     0x10
#define ST_PUSH         0x20
#define ANI_WALK        0
#define ANI_ROLL        2
#define ANI_SPRING      0x10
#define SFX_ROLL        0xBE          /* SndID_Roll */
#define SFX_SWITCH      0xCD          /* SndID_Blip */
#define SFX_SPIKE_MOVE  0xB6          /* SndID_SpikesMove */
#define SFX_SPRING      0xCC          /* SndID_Spring */
#define SFX_EXPLODE     0xC1          /* SndID_Explosion */
#define SFX_DASH        0xBC          /* SndID_SpindashRelease */

uint16_t s2_mtz_cog_x;                /* MTZ_Platform_Cog_X */
uint8_t s2_mtz_cyl_angle[2];          /* MTZCylinder_Angle_Sonic / _Tails (Obj06) */

static int on_screen(uint32_t o) { return (OB8(o, OBJ_render) & 0x80) != 0; }
static uint32_t trigger(int n) { return RAM_Level_trigger_array + (uint32_t)(n & 0xFF); }
static uint32_t word_ptr(uint16_t w) { return 0xFF0000u | w; }

static void gone(uint32_t o)                         /* MarkObjGone */
{
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void gone_x(uint32_t o, uint16_t x)          /* MarkObjGone2 */
{
    if (o_out_of_range(o, x)) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static int far_x(uint16_t x)                         /* the $280 window check on x */
{
    return (uint16_t)((x & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80)) > 0x280;   /* (Camera_X_pos_coarse) */
}

static void obj_move_player(uint32_t p)
{
    int32_t x = (int32_t)r32(p + OBJ_x_pos) + ((int32_t)(int16_t)OB16(p, OBJ_x_vel) << 8);
    int32_t y = (int32_t)r32(p + OBJ_y_pos) + ((int32_t)(int16_t)OB16(p, OBJ_y_vel) << 8);
    w32(p + OBJ_x_pos, (uint32_t)x);
    w32(p + OBJ_y_pos, (uint32_t)y);
}

static void cap_speed(uint32_t o, uint16_t d0, uint16_t d1)   /* Obj_CapSpeed */
{
    uint16_t d2 = OB16(o, OBJ_x_vel), d3 = OB16(o, OBJ_y_vel);
    if ((int16_t)d2 < 0) { uint16_t m = (uint16_t)-d0; if (d2 < m) d2 = m; }
    else if (d2 > d0) d2 = d0;
    if ((int16_t)d3 < 0) { uint16_t m = (uint16_t)-d1; if (d3 < m) d3 = m; }
    else if (d3 > d1) d3 = d1;
    SET16(o, OBJ_x_vel, d2);
    SET16(o, OBJ_y_vel, d3);
}

/* spring-type subtype bits: flips / plane switches (Obj41-style) */
static void spring_flags(uint32_t p, uint8_t d0, int flips, int speed, int more)
{
    if (d0 & 1) {
        SET16(p, OBJ_ground_vel, 1);
        SET8(p, P_FLIP_ANGLE, 1);
        SET8(p, OBJ_anim, ANI_WALK);
        SET8(p, P_FLIPS_LEFT, (uint8_t)flips);
        SET8(p, P_FLIP_SPEED, (uint8_t)speed);
        if (!(d0 & 2)) SET8(p, P_FLIPS_LEFT, (uint8_t)more);
        if (OB8(p, OBJ_status) & 1) {
            SET8(p, P_FLIP_ANGLE, (uint8_t)-(int8_t)OB8(p, P_FLIP_ANGLE));
            SET16(p, OBJ_ground_vel, (uint16_t)-(int16_t)OB16(p, OBJ_ground_vel));
        }
    }
    if ((d0 & 0x0C) == 4) { SET8(p, P_TOP_SOLID, 0x0C); SET8(p, P_LRB_SOLID, 0x0D); }
    if ((d0 & 0x0C) == 8) { SET8(p, P_TOP_SOLID, 0x0E); SET8(p, P_LRB_SOLID, 0x0F); }
}

/* ------------------------------------------------------------------------
 * Object 42 - steam spring: the piston sinks, a puff of steam each side,
 * standing on it while it's down shoots the player up
 * ---------------------------------------------------------------------- */
#define SS_TIME  0x32                 /* objoff_32 (word) */
#define SS_Y0    0x34                 /* objoff_34 */
#define SS_OFF   0x36                 /* objoff_36 (word) */
#define SS_DUR   0x3A                 /* puff: anim_frame_duration */

static void steam_launch(uint32_t o, uint32_t p)     /* loc_2678E */
{
    if (OB8(o, OBJ_2nd) != 2) return;
    SET16(p, OBJ_y_vel, (uint16_t)-0xA00);
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
    SET8(p, OBJ_anim, ANI_SPRING);
    SET8(p, OBJ_routine, 2);
    SET8(p, P_SPINDASH, 0);
    uint8_t d0 = OB8(o, OBJ_subtype);
    if (d0 & 0x80) SET16(p, OBJ_x_vel, 0);
    spring_flags(p, d0, 0, 4, 1);
    o_sfx_s2(SFX_SPRING);
}

static void steam_puff_new(uint32_t o, int dx, int flip)   /* loc_2674C */
{
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x42));
    SET8(a1, OBJ_routine, 4);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
    SET16(a1, OBJ_y_pos, OB16(o, SS_Y0));
    SET8(a1, SS_DUR, 7);
    o_init(a1, S2A_STEAM, 4 | (flip ? 1 : 0), 4, 0x18, 0x10);
}

static void steam_spring(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj42_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_STEAMSPRING, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_frame, 7);
        SET16(o, SS_Y0, OB16(o, OBJ_y_pos));
        SET16(o, SS_OFF, 0x10);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
        /* fall through */
    case 2: {                                           /* loc_26688 */
        for (int n = 0; n < 2; n++) {
            o_solid_1p(o, n, 0x1B, 0x10, 0x10, OB16(o, OBJ_x_pos), 1);
            if (OB8(o, OBJ_status) & (ST_P1_STAND << n)) steam_launch(o, o_player(n));
        }
        switch (OB8(o, OBJ_2nd)) {
        case 0:
            SET16(o, SS_TIME, OB16(o, SS_TIME) - 1);
            if ((int16_t)OB16(o, SS_TIME) < 0) { SET16(o, SS_TIME, 0x3B); SET8(o, OBJ_2nd, 2); }
            break;
        case 2:
            SET16(o, SS_OFF, OB16(o, SS_OFF) - 8);
            if (!OB16(o, SS_OFF)) {
                SET8(o, OBJ_2nd, 4);
                steam_puff_new(o, 0x28, 0);
                steam_puff_new(o, -0x28, 1);
            }
            SET16(o, OBJ_y_pos, OB16(o, SS_OFF) + OB16(o, SS_Y0));
            break;
        case 4:
            SET16(o, SS_TIME, OB16(o, SS_TIME) - 1);
            if ((int16_t)OB16(o, SS_TIME) < 0) { SET16(o, SS_TIME, 0x3B); SET8(o, OBJ_2nd, 6); }
            break;
        default:
            SET16(o, SS_OFF, OB16(o, SS_OFF) + 8);
            if (OB16(o, SS_OFF) == 0x10) SET8(o, OBJ_2nd, 0);
            SET16(o, OBJ_y_pos, OB16(o, SS_OFF) + OB16(o, SS_Y0));
            break;
        }
        gone(o);
        return;
    }
    default:                                            /* loc_2683A: a puff */
        SET8(o, SS_DUR, OB8(o, SS_DUR) - 1);
        if ((int8_t)OB8(o, SS_DUR) < 0) {
            SET8(o, SS_DUR, 7);
            SET8(o, OBJ_coll_flags, 0);
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
            if (OB8(o, OBJ_frame) == 3) SET8(o, OBJ_coll_flags, 0xA6);
            if (OB8(o, OBJ_frame) == 7) { o_delete(o); return; }
        }
        o_touch(o);
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 47 - button (subtype $4x: bit 7 of the trigger instead of bit 0)
 * ---------------------------------------------------------------------- */
static void button(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_BUTTON, 4, 4, 0x10, 8);
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 4);
    }
    if (on_screen(o)) {
        o_solid(o, 0x1B, 4, 5, OB16(o, OBJ_x_pos));
        SET8(o, OBJ_frame, 0);
        uint32_t a3 = trigger(OB8(o, OBJ_subtype) & 0x0F);
        int bit = (OB8(o, OBJ_subtype) & 0x40) ? 7 : 0;
        if (!(OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND))) {
            w8(a3, r8(a3) & ~(1 << bit));
        } else {
            if (!r8(a3)) o_sfx_s2(SFX_SWITCH);
            w8(a3, r8(a3) | (1 << bit));
            SET8(o, OBJ_frame, 1);
        }
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 64 - twin stompers (level art): rise, wait, slam down
 * ---------------------------------------------------------------------- */
#define TS_Y0    0x30
#define TS_X0    0x34
#define TS_TIME  0x36
#define TS_DOWN  0x38
#define TS_OFF   0x3A
#define TS_MAX   0x3C
#define TS_H     0x40                 /* objoff_2E */

static void twin_stompers(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj64_Init */
        static const uint8_t props[8] = { 0x40, 0x0C, 0x40, 1, 0x10, 0x20, 0x40, 1 };
        SET8(o, OBJ_routine, 2);
        int idx = (OB8(o, OBJ_subtype) >> 2) & 0x1C;
        if (idx > 4) idx = 4;
        o_init(o, S2A_TWINSTOMP, OB8(o, OBJ_render) | 4, 4, props[idx], 0x20);
        SET8(o, TS_H, props[idx + 1]);
        SET8(o, OBJ_frame, idx >> 2);
        if (!(idx >> 2)) { SET8(o, OBJ_y_radius, 0x6C); SET8(o, OBJ_height_px, 0x6C); }
        SET16(o, TS_X0, OB16(o, OBJ_x_pos));
        SET16(o, TS_Y0, OB16(o, OBJ_y_pos));
        SET16(o, TS_MAX, props[idx + 2]);
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    if (OB8(o, OBJ_subtype) == 1) {                     /* loc_269FA */
        int a = 0;
        if (!OB8(o, TS_DOWN)) {
            if (OB16(o, TS_OFF)) SET16(o, TS_OFF, OB16(o, TS_OFF) - 8);
            else {
                SET16(o, TS_TIME, OB16(o, TS_TIME) - 1);
                if ((int16_t)OB16(o, TS_TIME) < 0) { SET16(o, TS_TIME, 0x5A); SET8(o, TS_DOWN, 1); a = 1; }
            }
        } else a = 1;
        if (a) {
            if (OB16(o, TS_OFF) != OB16(o, TS_MAX)) SET16(o, TS_OFF, OB16(o, TS_OFF) + 8);
            else {
                SET16(o, TS_TIME, OB16(o, TS_TIME) - 1);
                if ((int16_t)OB16(o, TS_TIME) < 0) { SET16(o, TS_TIME, 0x5A); SET8(o, TS_DOWN, 0); }
            }
        }
        int16_t d0 = (int16_t)OB16(o, TS_OFF);
        if (OB8(o, OBJ_status) & 1) d0 = (int16_t)(-d0 + 0x40);
        SET16(o, OBJ_y_pos, OB16(o, TS_Y0) + d0);
    }
    if (on_screen(o)) {
        int d2 = OB8(o, TS_H);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, d2, d2 + 1, oldx);
    }
    if (far_x(OB16(o, TS_X0))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 65 - moving block (triggered, timed, proximity, carried along)
 * with a cog; frame 2 alone is a cog turning with MTZ_Platform_Cog_X
 * ---------------------------------------------------------------------- */
#define MB_X     0x40                 /* objoff_2E: x before moving (d4) */
#define MB_Y0    0x30
#define MB_X0    0x34
#define MB_TIME  0x36
#define MB_GO    0x38
#define MB_OFF   0x3A
#define MB_MAX   0x3C                 /* (the cog: its block, long) */
#define MB_TRIG  0x3E

static void mb_setx(uint32_t o, int span)            /* loc_26CD0 / loc_26D50 / loc_26E1A */
{
    int16_t d0 = (int16_t)OB16(o, MB_OFF);
    if (OB8(o, OBJ_status) & 1) d0 = (int16_t)(-d0 + span);
    SET16(o, OBJ_x_pos, OB16(o, MB_X0) - d0);
    SET16(o, MB_X, OB16(o, OBJ_x_pos));
}

static void mb_extend(uint32_t o)                    /* loc_26CC2 */
{
    if (OB16(o, MB_MAX) != OB16(o, MB_OFF)) { SET16(o, MB_OFF, OB16(o, MB_OFF) + 2); mb_setx(o, 0x80); return; }
    SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1);      /* loc_26CF2 */
    SET16(o, MB_TIME, 0xB4);
    SET8(o, MB_GO, 0);
    mb_setx(o, 0x80);
}

static void mb_retract(uint32_t o)                   /* loc_26D46 */
{
    if (OB16(o, MB_OFF)) { SET16(o, MB_OFF, OB16(o, MB_OFF) - 2); mb_setx(o, 0x80); return; }
    SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) - 1);      /* loc_26D72 */
    SET16(o, MB_TIME, 0xB4);
    SET8(o, MB_GO, 0);
    mb_setx(o, 0x80);
}

static int mb_near(uint32_t p, uint16_t d4, uint16_t d5, uint16_t d2, uint16_t d3)
{
    uint16_t x = OB16(p, OBJ_x_pos), y = OB16(p, OBJ_y_pos);
    return x >= d4 && x < d5 && y >= d2 && y < d3;
}

static void mb_mode(uint32_t o)
{
    switch (OB8(o, OBJ_subtype)) {
    case 1:                                             /* loc_26CA4: wait for the trigger */
        if (!OB8(o, MB_GO)) {
            if (!(r8(trigger(OB8(o, MB_TRIG))) & 1)) { mb_setx(o, 0x80); return; }
            SET8(o, MB_GO, 1);
        }
        mb_extend(o);
        return;
    case 2:                                             /* loc_26D34: timed return */
        if (!OB8(o, MB_GO)) {
            SET16(o, MB_TIME, OB16(o, MB_TIME) - 1);
            if (OB16(o, MB_TIME)) { mb_setx(o, 0x80); return; }
            SET8(o, MB_GO, 1);
        }
        mb_retract(o);
        return;
    case 3: {                                           /* loc_26D94: out while a player is near */
        uint16_t d4 = OB16(o, MB_X0), d5 = d4;
        if (!(OB8(o, OBJ_status) & 1)) { d4 = (uint16_t)(d4 - 0x20); d5 = (uint16_t)(d5 + 0x60); }
        else { d4 = (uint16_t)(d4 - 0xA0); d5 = (uint16_t)(d5 - 0x20); }
        uint16_t d2 = (uint16_t)(OB16(o, OBJ_y_pos) - 0x10), d3 = (uint16_t)(OB16(o, OBJ_y_pos) + 0x40);
        int near = mb_near(RAM_Player_1, d4, d5, d2, d3) || mb_near(RAM_Player_2, d4, d5, d2, d3);
        if (near) {
            if (OB16(o, MB_MAX) == OB16(o, MB_OFF)) return;
            SET16(o, MB_OFF, OB16(o, MB_OFF) + 0x10);
        } else if (OB16(o, MB_OFF)) {
            SET16(o, MB_OFF, OB16(o, MB_OFF) - 0x10);
        }
        mb_setx(o, 0x40);
        return;
    }
    case 4:                                             /* loc_26E3C: wait for Sonic aboard */
        if (OB8(o, OBJ_status) & ST_P1_STAND) SET8(o, OBJ_subtype, 5);
        return;
    case 5:                                             /* loc_26E4A: carried along */
        if (!OB8(o, MB_GO)) {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 2);
            if (tri_act() == 2) {                       /* MTZ3 (metropolis_zone_2) */
                if (OB16(o, OBJ_x_pos) == 0x1CC0 || OB16(o, OBJ_x_pos) == 0x2940) SET8(o, OBJ_subtype, 0);
            } else if (OB16(o, OBJ_x_pos) == 0x1BC0) {
                SET8(o, MB_GO, 1);
            }
        } else {
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 2);
            if (OB16(o, OBJ_x_pos) == 0x1880) SET8(o, MB_GO, 0);
        }
        SET16(o, MB_X0, OB16(o, OBJ_x_pos));
        s2_mtz_cog_x = OB16(o, OBJ_x_pos);
        return;
    case 6:                                             /* loc_26C90: timed out */
        if (!OB8(o, MB_GO)) {
            SET16(o, MB_TIME, OB16(o, MB_TIME) - 1);
            if (OB16(o, MB_TIME)) { mb_setx(o, 0x80); return; }
            SET8(o, MB_GO, 1);
        }
        mb_extend(o);
        return;
    case 7:                                             /* loc_26D14: triggered back */
        if (!OB8(o, MB_GO)) {
            if (!(r8(trigger(OB8(o, MB_TRIG))) & 1)) { mb_setx(o, 0x80); return; }
            SET8(o, MB_GO, 1);
        }
        mb_retract(o);
        return;
    default:
        return;
    }
}

static void cog_frame(uint32_t o, uint16_t d0)
{
    static const uint8_t frames[8] = { 0, 0, 2, 2, 2, 1, 1, 1 };   /* byte_26EBA */
    SET8(o, OBJ_frame, frames[d0 & 7]);
    gone(o);
}

static void moving_block(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj65_Init */
        static const uint8_t props[16] = { 0x40, 0x0C, 0x80, 1, 0x20, 0x0C, 0x40, 3, 0x10, 0x10, 0x20, 0, 0x40, 0x0C, 0x80, 7 };
        SET8(o, OBJ_routine, 2);
        int idx = (OB8(o, OBJ_subtype) >> 2) & 0x1C;
        int frame = idx >> 2;
        const uint8_t *a3 = props + (idx & 0x0C);       /* (2-byte entries, stride 4) */
        if (frame == 2) {
            SET8(o, OBJ_routine, 6);
            o_init(o, S2A_MTZCOG, OB8(o, OBJ_render) | 4, 4, a3[0], 0x10);
            cog_frame(o, s2_mtz_cog_x);
            return;
        }
        o_init(o, S2A_MTZBLOCK, OB8(o, OBJ_render) | 4, 4, a3[0], 0x10);
        SET8(o, OBJ_y_radius, a3[1]);
        SET8(o, OBJ_frame, frame);
        if (frame == 1) SET8(o, OBJ_status, OB8(o, OBJ_status) | 0x80);
        SET16(o, MB_X0, OB16(o, OBJ_x_pos));
        SET16(o, MB_Y0, OB16(o, OBJ_y_pos));
        SET16(o, MB_MAX, a3[2]);
        uint8_t sub = OB8(o, OBJ_subtype);
        if (sub & 0x80) {
            SET8(o, MB_TRIG, sub & 0x0F);
            SET8(o, OBJ_subtype, a3[3]);
            if (a3[3] == 7) SET16(o, MB_OFF, OB16(o, MB_MAX));
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                SET32(a1, OBJ_code, S2(0x65));
                SET8(a1, OBJ_routine, 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x4C);
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x14);
                int flip = 0;
                if (!(OB8(o, OBJ_status) & 1)) { SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + 0x18); flip = 1; }
                o_init(a1, S2A_MTZCOG, 4 | flip, 4, 0x10, 0x10);
                SET32(a1, MB_MAX, RAMPTR(o));
            }
        }
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
    }   /* fall through */
    case 2: {                                           /* loc_26C1C */
        SET16(o, MB_X, OB16(o, OBJ_x_pos));
        mb_mode(o);
        int d2 = OB8(o, OBJ_y_radius);
        o_solid(o, OB8(o, OBJ_width_px) + 5, d2, d2 + 1, OB16(o, MB_X));
        if (far_x(OB16(o, MB_X0))) { o_delete_respawnable(o); return; }
        o_draw(o);
        return;
    }
    case 4: {                                           /* loc_26EA4: the block's cog */
        uint32_t a1 = r32(o + MB_MAX) & 0xFFFFFF;
        cog_frame(o, OB16(a1, MB_OFF));
        return;
    }
    default:                                            /* loc_26EC2 */
        cog_frame(o, s2_mtz_cog_x);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 66 - spring wall (invisible): bounces an airborne player back
 * ---------------------------------------------------------------------- */
static void wall_launch(uint32_t o, uint32_t p)      /* loc_27042 */
{
    if (OB8(p, OBJ_routine) >= 4) return;
    SET16(p, OBJ_x_vel, (uint16_t)-0x800);
    SET16(p, OBJ_y_vel, (uint16_t)-0x800);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
    if (!(OB8(o, OBJ_status) & 1)) {
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
        SET16(p, OBJ_x_vel, 0x800);
    }
    SET16(p, P_MOVE_LOCK, 0x0F);
    SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
    if (!(OB8(p, OBJ_status) & ST_ROLL)) SET8(p, OBJ_anim, ANI_WALK);
    uint8_t d0 = OB8(o, OBJ_subtype);
    if (d0 & 0x80) SET16(p, OBJ_y_vel, 0);
    spring_flags(p, d0, 1, 8, 3);
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_PUSH | ST_P2_PUSH));
    SET8(p, OBJ_status, OB8(p, OBJ_status) & ~(ST_ROLLJUMP | ST_PUSH));
    o_sfx_s2(SFX_SPRING);
}

static void spring_wall(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj66_Init */
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET8(o, OBJ_width_px, 8);
        SET8(o, OBJ_y_radius, 0x40);
        if ((OB8(o, OBJ_subtype) >> 4) & 7) SET8(o, OBJ_y_radius, 0x80);
    }
    int d2 = OB8(o, OBJ_y_radius);
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        int16_t d4 = o_solid_1p(o, n, 0x13, d2, d2 + 1, OB16(o, OBJ_x_pos), 1);
        if ((d4 & 0xFF) != 1 || !(OB8(p, OBJ_status) & ST_INAIR)) continue;
        uint8_t d1 = OB8(o, OBJ_status);
        if (OB16(o, OBJ_x_pos) >= OB16(p, OBJ_x_pos)) d1 ^= 1;
        if (!(d1 & 1)) wall_launch(o, p);
    }
    if (far_x(OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
}

/* ------------------------------------------------------------------------
 * Object 67 - spin tube: rolls a player down, then flies him along a path
 * (misc/obj67.asm, blob TRI_BLOB_S2_MTZ_TUBES)
 * ---------------------------------------------------------------------- */
#define ST_P1    0x30                 /* objoff_2C: state, sine, timer, -, remaining (w), path (l) */
#define ST_P2    0x3A                 /* objoff_36 */

static uint32_t tube_blob(void)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + TRI_BLOB_S2_MTZ_TUBES * 4u) : 0;
}

static void tube_aim(uint32_t p, uint32_t a4, int16_t d4, int16_t d5)   /* loc_27374 */
{
    int16_t d2 = 0x1000, d3 = 0x1000;
    int16_t d0 = (int16_t)(d4 - OB16(p, OBJ_x_pos));
    if (d0 < 0) { d0 = (int16_t)-d0; d2 = (int16_t)-d2; }
    int16_t d1 = (int16_t)(d5 - OB16(p, OBJ_y_pos));
    if (d1 < 0) { d1 = (int16_t)-d1; d3 = (int16_t)-d3; }
    if ((uint16_t)d1 >= (uint16_t)d0) {
        int32_t dy = (int32_t)(int16_t)(d5 - OB16(p, OBJ_y_pos)) << 16;
        int16_t q = (int16_t)(dy / d3);
        int16_t xv = 0;
        int16_t dx = (int16_t)(d4 - OB16(p, OBJ_x_pos));
        if (dx && q) xv = (int16_t)(((int32_t)dx << 16) / q);
        SET16(p, OBJ_x_vel, (uint16_t)xv);
        SET16(p, OBJ_y_vel, (uint16_t)d3);
        w16(a4 + 2, (uint16_t)(q < 0 ? -q : q));
    } else {
        int32_t dx = (int32_t)(int16_t)(d4 - OB16(p, OBJ_x_pos)) << 16;
        int16_t q = (int16_t)(dx / d2);
        int16_t yv = 0;
        int16_t dy = (int16_t)(d5 - OB16(p, OBJ_y_pos));
        if (dy && q) yv = (int16_t)(((int32_t)dy << 16) / q);
        SET16(p, OBJ_y_vel, (uint16_t)yv);
        SET16(p, OBJ_x_vel, (uint16_t)d2);
        w16(a4 + 2, (uint16_t)(q < 0 ? -q : q));
    }
}

static void tube_start(uint32_t o, uint32_t p, uint32_t a4)   /* loc_27310 */
{
    uint32_t tab = tube_blob();
    if (!tab) return;
    int8_t sub = (int8_t)OB8(o, OBJ_subtype);
    uint32_t a2;
    if (sub < 0) {
        int idx = (-sub) & 0x0F;
        a2 = tab + tri_rd16(tab + (uint32_t)idx * 2u);
        uint16_t d0 = (uint16_t)(tri_rd16(a2) - 4);
        a2 += 2;
        w16(a4 + 4, d0);
        a2 += d0;
        SET16(p, OBJ_x_pos, tri_rd16(a2));
        SET16(p, OBJ_y_pos, tri_rd16(a2 + 2));
        a2 = a2 + 4 - 8;
    } else {
        int idx = sub & 0x0F;
        a2 = tab + tri_rd16(tab + (uint32_t)idx * 2u);
        w16(a4 + 4, (uint16_t)(tri_rd16(a2) - 4));
        SET16(p, OBJ_x_pos, tri_rd16(a2 + 2));
        SET16(p, OBJ_y_pos, tri_rd16(a2 + 4));
        a2 += 6;
    }
    w32(a4 + 6, a2);                                    /* loc_27368 */
    tube_aim(p, a4, (int16_t)tri_rd16(a2), (int16_t)tri_rd16(a2 + 2));
}

static void tube_player(uint32_t o, int n)
{
    uint32_t p = o_player(n), a4 = o + (n ? ST_P2 : ST_P1);
    switch (r8(a4)) {
    case 0: {                                           /* loc_271D0 */
        if (r16(RAM_Debug_placement_mode)) return;
        uint16_t d0 = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 3);
        if (OB8(o, OBJ_status) & 1) d0 = (uint16_t)(d0 + 0x0A);
        if (d0 >= 0x10) return;
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x20) >= 0x40) return;
        if (OB8(p, P_OBJCTRL)) return;
        w8(a4, 2);
        SET8(p, P_OBJCTRL, 0x81);
        SET8(p, OBJ_anim, ANI_ROLL);
        SET16(p, OBJ_ground_vel, 0x800);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~ST_PUSH) | ST_INAIR);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
        w8(a4 + 1, 0);
        o_sfx_s2(SFX_ROLL);
        SET8(o, OBJ_anim, 1);
        SET8(o, OBJ_prev_anim, 0);
        return;
    }
    case 2: {                                           /* loc_27260: drop into the tube */
        uint8_t a = r8(a4 + 1);
        w8(a4 + 1, (uint8_t)(a + 2));
        int16_t s, c;
        o_sincos(a, &s, &c);
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos) - (s >> 5));
        if (r8(a4 + 1) == 0x80) {
            tube_start(o, p, a4);
            w8(a4, 4);
            o_sfx_s2(SFX_DASH);
        }
        return;
    }
    default: {                                          /* loc_27294: follow the path */
        w8(a4 + 2, r8(a4 + 2) - 1);
        if ((int8_t)r8(a4 + 2) >= 0) { obj_move_player(p); return; }
        uint32_t a2 = r32(a4 + 6);
        SET16(p, OBJ_x_pos, tri_rd16(a2));
        SET16(p, OBJ_y_pos, tri_rd16(a2 + 2));
        a2 += 4;
        if (OB8(o, OBJ_subtype) & 0x80) a2 -= 8;
        w32(a4 + 6, a2);
        w16(a4 + 4, r16(a4 + 4) - 4);
        if (!r16(a4 + 4)) {                             /* loc_272EE: out */
            SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) & 0x7FF);
            w8(a4, 0);
            SET8(p, P_OBJCTRL, 0);
            if (!(OB8(o, OBJ_subtype) & 0x10)) { SET16(p, OBJ_x_vel, 0); SET16(p, OBJ_y_vel, 0); }
            return;
        }
        tube_aim(p, a4, (int16_t)tri_rd16(a2), (int16_t)tri_rd16(a2 + 2));
        return;
    }
    }
}

static void spin_tube(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_SPINFLASH, OB8(o, OBJ_render) | 4, 5, 0x10, 0x10);
    }
    tube_player(o, 0);
    tube_player(o, 1);
    if (!(OB8(o, ST_P1) + OB8(o, ST_P2))) {             /* MarkObjGone3: no drawing */
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
        return;
    }
    o_animate_s2(o, o_asset_anim(S2A_SPINFLASH));
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 68 - block with a spike creeping round it; 6D - floor spike
 * ---------------------------------------------------------------------- */
#define SK_X0    0x30
#define SK_Y0    0x32
#define SK_OFF   0x34                 /* (word; the high byte is the offset) */
#define SK_OUT   0x36
#define SK_WAIT  0x38
#define SK_DELAY 0x3A

static const uint8_t sk_flags[4] = { 0x84, 0xA6, 0x84, 0xA6 };   /* Obj68_CollisionFlags */

static void spike_block_action(uint32_t o)           /* Obj68_Spike_Action */
{
    if (OB16(o, SK_WAIT)) {
        if (o_frame_counter() & 0x3F) return;
        SET16(o, SK_WAIT, 0);
        if (on_screen(o)) o_sfx_s2(SFX_SPIKE_MOVE);
    }
    if (OB16(o, SK_OUT)) {
        if (OB16(o, SK_OFF) >= 0x800) { SET16(o, SK_OFF, OB16(o, SK_OFF) - 0x800); return; }
        SET16(o, SK_OFF, 0);
        SET16(o, SK_OUT, 0);
        SET16(o, SK_WAIT, 1);
        SET8(o, OBJ_2nd, (OB8(o, OBJ_2nd) + 1) & 3);
        SET8(o, OBJ_frame, OB8(o, OBJ_2nd));
        SET8(o, OBJ_coll_flags, sk_flags[OB8(o, OBJ_2nd)]);
        return;
    }
    SET16(o, SK_OFF, OB16(o, SK_OFF) + 0x800);
    if (OB16(o, SK_OFF) < 0x2000) return;
    SET16(o, SK_OFF, 0x2000);
    SET16(o, SK_OUT, 1);
    SET16(o, SK_WAIT, 1);
}

static void spike_block(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj68_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_SPIKEBLOCK, 4, 4, 0x10, 0x10);
        uint32_t a1 = o_alloc_after(o);
        if (a1) {
            SET32(a1, OBJ_code, S2(0x68));
            SET8(a1, OBJ_routine, 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, SK_X0, OB16(o, OBJ_x_pos));
            SET16(a1, SK_Y0, OB16(o, OBJ_y_pos));
            o_init(a1, S2A_MTZSPIKE, 4, 4, 0x10, 0x10);
            uint16_t f = (uint16_t)(r16(RAM_Level_frame_counter) >> 6);
            SET16(a1, SK_OUT, f & 1);
            uint8_t d1 = (uint8_t)(((f >> 1) + OB8(o, OBJ_subtype)) & 3);
            SET8(a1, OBJ_2nd, d1);
            SET8(a1, OBJ_frame, d1);
            SET8(a1, OBJ_coll_flags, sk_flags[d1]);
        }
        SET8(o, OBJ_frame, 4);
    }   /* fall through */
    case 2:                                             /* Obj68_Block */
        o_solid(o, 0x1B, 0x10, 0x11, OB16(o, OBJ_x_pos));
        gone(o);
        return;
    default: {                                          /* Obj68_Spike */
        spike_block_action(o);
        int d0 = OB8(o, SK_OFF);
        switch (OB8(o, OBJ_2nd)) {
        case 0: SET16(o, OBJ_y_pos, OB16(o, SK_Y0) - d0); break;
        case 1: SET16(o, OBJ_x_pos, OB16(o, SK_X0) + d0); break;
        case 2: SET16(o, OBJ_y_pos, OB16(o, SK_Y0) + d0); break;
        default: SET16(o, OBJ_x_pos, OB16(o, SK_X0) - d0); break;
        }
        o_touch(o);
        gone_x(o, OB16(o, SK_X0));
        return;
    }
    }
}

static void floor_spike(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj6D_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_MTZSPIKE, OB8(o, OBJ_render) | 4, 4, 4, 0x10);
        SET16(o, SK_X0, OB16(o, OBJ_x_pos));
        SET16(o, SK_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_coll_flags, 0x84);
    }
    if (OB16(o, SK_DELAY)) {                            /* Obj6D_Action */
        SET16(o, SK_DELAY, OB16(o, SK_DELAY) - 1);
    } else {
        int go = 1;
        if (OB16(o, SK_WAIT)) {
            if ((uint8_t)(o_frame_counter() - OB8(o, OBJ_subtype)) & 0x7F) go = 0;
            else SET16(o, SK_WAIT, 0);
        }
        if (go) {
            if (OB16(o, SK_OUT)) {
                if (OB16(o, SK_OFF) >= 0x400) SET16(o, SK_OFF, OB16(o, SK_OFF) - 0x400);
                else { SET16(o, SK_OFF, 0); SET16(o, SK_OUT, 0); SET16(o, SK_WAIT, 1); }
            } else {
                SET16(o, SK_OFF, OB16(o, SK_OFF) + 0x400);
                if (OB16(o, SK_OFF) >= 0x2000) { SET16(o, SK_OFF, 0x2000); SET16(o, SK_OUT, 1); SET16(o, SK_DELAY, 3); }
            }
        }
    }
    SET16(o, OBJ_y_pos, OB16(o, SK_Y0) - OB8(o, SK_OFF));
    o_touch(o);
    gone_x(o, OB16(o, SK_X0));
}

/* ------------------------------------------------------------------------
 * Object 69 - nut: walking along it screws it down (or up); subtype < 0
 * drops off the end of its thread
 * ---------------------------------------------------------------------- */
#define NT_Y0    0x32
#define NT_TURN  0x34                 /* objoff_34 (word) */
#define NT_MAX   0x36
#define NT_P1    0x38                 /* objoff_38/39 (P1), objoff_3C/3D (P2) */
#define NT_P2    0x3C

static void nut_turn(uint32_t o, int16_t d0)
{
    SET16(o, NT_TURN, OB16(o, NT_TURN) + d0);
    int16_t t = (int16_t)((int16_t)OB16(o, NT_TURN) >> 3);
    SET8(o, OBJ_frame, (t >> 1) & 3);
    SET16(o, OBJ_y_pos, OB16(o, NT_Y0) - t);
}

static void nut_action(uint32_t o, int n)
{
    uint32_t p = o_player(n), a4 = o + (n ? NT_P2 : NT_P1);
    uint8_t bit = (uint8_t)(ST_P1_STAND << n);
    if (!(OB8(o, OBJ_status) & bit)) w8(a4, 0);
    switch (r8(a4)) {
    case 0:                                             /* loc_2792C */
        if (!(OB8(o, OBJ_status) & bit)) return;
        w8(a4, 2);
        w8(a4 + 1, OB16(o, OBJ_x_pos) >= OB16(p, OBJ_x_pos) ? 0 : 1);
        /* fall through */
    case 2: {                                           /* loc_2794C: centre on it */
        uint16_t d0 = (uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
        if (r8(a4 + 1)) d0 = (uint16_t)(d0 + 0x0F);
        if (d0 >= 0x10) return;
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        w8(a4, 4);
        return;
    }
    default: {                                          /* loc_2796E: turn it */
        uint16_t px = OB16(p, OBJ_x_pos), ox = OB16(o, OBJ_x_pos);
        int16_t d0 = (int16_t)(px - ox);
        SET16(p, OBJ_x_pos, ox);
        if (px >= ox) {                                 /* loc_279D4: up the thread */
            nut_turn(o, d0);
            return;
        }
        nut_turn(o, d0);                                /* down to the end of the thread */
        int16_t d1 = (int16_t)(OB16(o, OBJ_y_pos) - OB16(o, NT_Y0));
        int16_t mx = (int16_t)OB16(o, NT_MAX);
        if (d1 < mx) return;
        SET16(o, OBJ_y_pos, OB16(o, NT_Y0) + mx);
        SET16(o, NT_TURN, (uint16_t)-(mx << 3));
        SET8(o, OBJ_frame, 0);
        if (OB8(o, OBJ_subtype) & 0x80) SET8(o, OBJ_routine, 4);
        else w8(a4, 0);
        return;
    }
    }
}

static void nut(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj69_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_NUT, 4, 4, 0x20, 0x10);
        SET8(o, OBJ_y_radius, 0x0B);
        SET16(o, NT_Y0, OB16(o, OBJ_y_pos));
        SET16(o, NT_MAX, (uint16_t)((OB8(o, OBJ_subtype) & 0x7F) << 3));
        /* fall through */
    case 2:                                             /* Obj69_Main */
        nut_action(o, 0);
        nut_action(o, 1);
        break;
    case 4: {                                           /* loc_279FC: drops */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, (OB16(o, OBJ_y_pos) + d1) & 0x7FF);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_routine, 6);
        }
        break;
    }
    default:
        break;
    }
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) & 0x7FF);    /* loc_278F4 */
    o_solid(o, 0x2B, 0x0C, 0x0D, OB16(o, OBJ_x_pos));
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 6C - lava cups riding a conveyor loop (subtype < 0 lays out the set)
 * ---------------------------------------------------------------------- */
#define LC_X0    0x30
#define LC_Y0    0x32
#define LC_TX    0x34
#define LC_TY    0x36
#define LC_IDX   0x38
#define LC_LEN   0x39
#define LC_STEP  0x3A
#define LC_PATH  0x3C

static const int16_t lc_paths[3][20] = {              /* byte_28258 / 28282 / 282AC (header $28) */
    { 0, 0, -0x16, 0x0A, -0x20, 0x20, -0x20, 0xE0, -0x16, 0xF6, 0, 0x100, 0x16, 0xF6, 0x20, 0xE0, 0x20, 0x20, 0x16, 0x0A },
    { 0, 0, -0x16, 0x0A, -0x20, 0x20, -0x20, 0x160, -0x16, 0x176, 0, 0x180, 0x16, 0x176, 0x20, 0x160, 0x20, 0x20, 0x16, 0x0A },
    { 0, 0, -0x16, 0x0A, -0x20, 0x20, -0x20, 0x1E0, -0x16, 0x1F6, 0, 0x200, 0x16, 0x1F6, 0x20, 0x1E0, 0x20, 0x20, 0x16, 0x0A },
};
static const int16_t lc_sets[3][24] = {               /* byte_282DC / 2830E / 28340: x, y, subtype */
    { 0, 0, 1, -0x20, 0x3A, 3, -0x20, 0x80, 3, -0x20, 0xC6, 3, 0, 0x100, 6, 0x20, 0xC6, 8, 0x20, 0x80, 8, 0x20, 0x3A, 8 },
    { 0, 0, 0x11, -0x20, 0x5A, 0x13, -0x20, 0xC0, 0x13, -0x20, 0x126, 0x13, 0, 0x180, 0x16, 0x20, 0x126, 0x18, 0x20, 0xC0, 0x18, 0x20, 0x5A, 0x18 },
    { 0, 0, 0x21, -0x20, 0x7A, 0x23, -0x20, 0x100, 0x23, -0x20, 0x186, 0x23, 0, 0x200, 0x26, 0x20, 0x186, 0x28, 0x20, 0x100, 0x28, 0x20, 0x7A, 0x28 },
};

static void lc_aim(uint32_t o)                       /* loc_281DA */
{
    int16_t d2 = -0x100, d3 = -0x100;
    int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, LC_TX));
    if (OB16(o, OBJ_x_pos) < OB16(o, LC_TX)) { d0 = (int16_t)-d0; d2 = 0x100; }
    int16_t d1 = (int16_t)(OB16(o, OBJ_y_pos) - OB16(o, LC_TY));
    if (OB16(o, OBJ_y_pos) < OB16(o, LC_TY)) { d1 = (int16_t)-d1; d3 = 0x100; }
    if ((uint16_t)d1 >= (uint16_t)d0) {
        int16_t q = 0, r = 0;
        int16_t dx = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o, LC_TX));
        if (dx && d1) { int32_t n = (int32_t)dx << 8; q = (int16_t)(n / d1); r = (int16_t)(n % d1); q = (int16_t)-q; }
        SET16(o, OBJ_x_vel, (uint16_t)q);
        SET16(o, OBJ_y_vel, (uint16_t)d3);
        SET16(o, OBJ_x_pos + 2, (uint16_t)r);
        SET16(o, OBJ_y_pos + 2, 0);
    } else {
        int16_t q = 0, r = 0;
        int16_t dy = (int16_t)(OB16(o, OBJ_y_pos) - OB16(o, LC_TY));
        if (dy && d0) { int32_t n = (int32_t)dy << 8; q = (int16_t)(n / d0); r = (int16_t)(n % d0); q = (int16_t)-q; }
        SET16(o, OBJ_y_vel, (uint16_t)q);
        SET16(o, OBJ_x_vel, (uint16_t)d2);
        SET16(o, OBJ_y_pos + 2, (uint16_t)r);
        SET16(o, OBJ_x_pos + 2, 0);
    }
}

static void lc_target(uint32_t o, int d1)
{
    const int16_t *t = lc_paths[OB8(o, LC_PATH)];
    SET16(o, LC_TX, OB16(o, LC_X0) + t[d1 / 2]);
    SET16(o, LC_TY, OB16(o, LC_Y0) + t[d1 / 2 + 1]);
    lc_aim(o);
}

static int lc_next(uint32_t o)
{
    uint8_t d1 = (uint8_t)(OB8(o, LC_IDX) + OB8(o, LC_STEP));
    if (d1 >= OB8(o, LC_LEN)) {
        uint8_t d0 = d1;
        d1 = 0;
        if (d0 & 0x80) d1 = (uint8_t)(OB8(o, LC_LEN) - 4);
    }
    SET8(o, LC_IDX, d1);
    return d1;
}

static void lava_cup(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        uint8_t sub = OB8(o, OBJ_subtype);
        if (sub & 0x80) {                               /* loc_28112: lay out the cups */
            const int16_t *s = lc_sets[(sub & 0x7F) % 3];
            int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
            uint8_t st = OB8(o, OBJ_status);
            for (int k = 0; k < 8; k++) {
                uint32_t a1 = k ? o_alloc() : o;
                if (!a1) continue;
                SET32(a1, OBJ_code, S2(0x6C));
                SET16(a1, OBJ_x_pos, (uint16_t)(x + s[k * 3]));
                SET16(a1, OBJ_y_pos, (uint16_t)(y + s[k * 3 + 1]));
                SET16(a1, LC_X0, (uint16_t)x);
                SET16(a1, LC_Y0, (uint16_t)y);
                SET8(a1, OBJ_subtype, (uint8_t)s[k * 3 + 2]);
                SET8(a1, OBJ_status, st);
            }
            return;
        }
        SET8(o, OBJ_routine, 2);                        /* Obj6C_Init */
        o_init(o, S2A_LAVACUP, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_frame, 0);
        SET8(o, LC_PATH, ((sub >> 4) & 0x0F) % 3);
        SET8(o, LC_LEN, 0x28);
        int d1 = (sub & 0x0F) << 2;
        SET8(o, LC_IDX, (uint8_t)d1);
        SET8(o, LC_STEP, 4);
        if (OB8(o, OBJ_status) & 1) {
            SET8(o, LC_STEP, (uint8_t)-4);
            d1 = lc_next(o);
        }
        lc_target(o, d1);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);                 /* Obj6C_Main / loc_2817E */
    if (OB16(o, OBJ_x_pos) == OB16(o, LC_TX) && OB16(o, OBJ_y_pos) == OB16(o, LC_TY))
        lc_target(o, lc_next(o));
    o_move(o);
    o_platform(o, OB8(o, OBJ_width_px), 8, oldx);
    if (far_x(OB16(o, LC_X0))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 6E - gear riding the oscillators (frame 3: the wheel indent)
 * ---------------------------------------------------------------------- */
static void gear(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj6E_Init */
        static const uint8_t props[8] = { 0x10, 0x0C, 0x28, 8, 0x60, 0x18, 0x0C, 0x0C };
        int idx = (OB8(o, OBJ_subtype) >> 3) & 0x0E;
        int frame = idx >> 1;
        SET8(o, OBJ_routine, frame == 3 ? 4 : 2);
        o_init(o, frame == 3 ? S2A_WHEELINDENT : S2A_MTZGEAR, OB8(o, OBJ_render) | 4, frame == 3 ? 5 : 4, props[idx], 0x20);
        SET8(o, OBJ_y_radius, props[idx + 1]);
        SET8(o, OBJ_frame, frame);
        SET16(o, 0x34, OB16(o, OBJ_x_pos));
        SET16(o, 0x30, OB16(o, OBJ_y_pos));
    }
    int ind = OB8(o, OBJ_routine) == 4;
    uint16_t oldx = OB16(o, OBJ_x_pos);
    int16_t d1, d2;
    if (!ind) { d1 = (int8_t)(o_osc(0x20 + 2) - 0x38); d2 = (int8_t)(o_osc(0x24 + 2) - 0x38); }
    else { d1 = (int8_t)((o_osc(0x20 + 2) >> 1) - 0x1C); d2 = (int8_t)((o_osc(0x24 + 2) >> 1) - 0x1C); }
    if (OB8(o, OBJ_subtype) & 1) { d1 = (int16_t)-d1; d2 = (int16_t)-d2; }
    if (OB8(o, OBJ_subtype) & 2) { int16_t t = (int16_t)-d1; d1 = d2; d2 = t; }
    SET16(o, OBJ_x_pos, OB16(o, 0x34) + d1);
    SET16(o, OBJ_y_pos, OB16(o, 0x30) + d2);
    if (!ind) {
        int h = OB8(o, OBJ_y_radius);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, h, h + 1, oldx);
    }
    if (far_x(OB16(o, 0x34))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 70 - big wheel: eight solid teeth stepping round every 16 frames
 * ---------------------------------------------------------------------- */
#define WH_CY    0x30
#define WH_CX    0x32
#define WH_TOOTH 0x34
#define WH_PHASE 0x36

static const int8_t wh_pos[96] = {                     /* Obj70_Positions: x, y, frame */
    0, -0x48, 0, 0x32, -0x32, 4, 0x48, 0, 8, 0x32, 0x32, 0x0C, 0, 0x48, 0x10, -0x32, 0x32, 0x14, -0x48, 0, 0x18, -0x32, -0x32, 0x1C,
    0x0D, -0x48, 1, 0x3F, -0x26, 5, 0x48, 0x0C, 9, 0x27, 0x3C, 0x0D, -0x0D, 0x48, 0x11, -0x3F, 0x26, 0x15, -0x48, -0x0C, 0x19, -0x27, -0x3C, 0x1D,
    0x19, -0x44, 2, 0x46, -0x17, 6, 0x46, 0x17, 0x0A, 0x19, 0x44, 0x0E, -0x19, 0x44, 0x12, -0x46, 0x17, 0x16, -0x46, -0x17, 0x1A, -0x19, -0x44, 0x1E,
    0x27, -0x3C, 3, 0x48, -0x0C, 7, 0x3F, 0x26, 0x0B, 0x0D, 0x48, 0x0F, -0x27, 0x3C, 0x13, -0x48, 0x0C, 0x17, -0x3F, -0x26, 0x1B, -0x0D, -0x48, 0x1F,
};
static const uint8_t wh_size[32] = {                   /* byte_28706 */
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0C,
    0x10, 8, 0x10, 0x0C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
};

static void wheel(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj70_Init: eight teeth */
        int16_t x = (int16_t)OB16(o, OBJ_x_pos), y = (int16_t)OB16(o, OBJ_y_pos);
        uint8_t st = (uint8_t)(OB8(o, OBJ_status) | 0x80);
        SET8(o, OBJ_status, st);
        for (int k = 0; k < 8; k++) {
            uint32_t a1 = k ? o_alloc_after(o) : o;
            if (!a1) continue;
            SET32(a1, OBJ_code, S2(0x70));
            SET8(a1, OBJ_routine, 2);
            o_init(a1, S2A_MTZWHEEL, 4, 4, 0x10, 0x10);
            SET16(a1, WH_CX, (uint16_t)x);
            SET16(a1, WH_CY, (uint16_t)y);
            SET16(a1, OBJ_x_pos, (uint16_t)(x + wh_pos[k * 3]));
            SET16(a1, OBJ_y_pos, (uint16_t)(y + wh_pos[k * 3 + 1]));
            SET8(a1, OBJ_frame, (uint8_t)wh_pos[k * 3 + 2]);
            SET16(a1, WH_TOOTH, (uint16_t)(k * 3));
            SET8(a1, OBJ_status, st);
        }
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);                 /* Obj70_Main */
    if (!(o_frame_counter() & 0x0F)) {
        int d1 = OB16(o, WH_PHASE);
        if (OB8(o, OBJ_status) & 1) {
            d1 -= 0x18;
            if (d1 < 0) {
                d1 = 0x48;
                if ((int16_t)(OB16(o, WH_TOOTH) - 3) < 0) SET16(o, WH_TOOTH, 0x15);
                else SET16(o, WH_TOOTH, OB16(o, WH_TOOTH) - 3);
            }
        } else {
            d1 += 0x18;
            if (d1 >= 0x60) {
                d1 = 0;
                SET16(o, WH_TOOTH, OB16(o, WH_TOOTH) + 3);
                if (OB16(o, WH_TOOTH) >= 0x18) SET16(o, WH_TOOTH, 0);
            }
        }
        SET16(o, WH_PHASE, (uint16_t)d1);
        const int8_t *a1 = wh_pos + d1 + OB16(o, WH_TOOTH);
        SET16(o, OBJ_x_pos, (uint16_t)(OB16(o, WH_CX) + a1[0]));
        SET16(o, OBJ_y_pos, (uint16_t)(OB16(o, WH_CY) + a1[1]));
        SET8(o, OBJ_frame, (uint8_t)a1[2]);
    }
    int f = (OB8(o, OBJ_frame) * 2) & 0x1E;
    o_solid(o, wh_size[f], wh_size[f + 1], wh_size[f + 1], oldx);
    if (far_x(OB16(o, WH_CX))) { o_delete(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 71 - lava bubble (MTZ: subtype & $F = 2, anim = subtype >> 4)
 * ---------------------------------------------------------------------- */
static void lava_bubble(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_LAVABUBBLE, OB8(o, OBJ_render) | 4, 1, 0x10, 0x10);
        SET8(o, OBJ_frame, 0);
        SET8(o, OBJ_anim, OB8(o, OBJ_subtype) >> 4);
    }
    o_animate_s2(o, o_asset_anim(S2A_LAVABUBBLE));
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 9F - Shellcracker; A0 - its claw (eight links)
 * ---------------------------------------------------------------------- */
#define SC_TIME  0x3A                 /* objoff_2A (word) */
#define SC_DONE  0x3C                 /* objoff_2C: the claw is back */
#define CL_T1    0x3A                 /* claw: objoff_2A */
#define CL_T2    0x3B                 /* objoff_2B */
#define CL_PARENT 0x3C                /* objoff_2C (word) */
#define CL_IDX   0x3E                 /* objoff_2E (word) */

static int sc_sees(uint32_t o)                       /* Sonic within $60 on the side it faces */
{
    int d0;
    int16_t d2;
    s2_orient(o, &d0, NULL, &d2, NULL);
    if (d0 && !(OB8(o, OBJ_render) & 1)) return 0;
    return (uint16_t)(d2 + 0x60) < 0xC0;
}

static void sc_attack(uint32_t o)                    /* loc_380AE */
{
    SET8(o, OBJ_routine, 6);
    SET8(o, OBJ_frame, 0);
    SET16(o, SC_TIME, 8);
}

static void sc_claw(uint32_t o)                      /* loc_38292 */
{
    for (int k = 0; k < 8; k++) {
        uint32_t a1 = o_alloc_after(o);
        if (!a1) return;
        SET32(a1, OBJ_code, S2(0xA0));
        o_init(a1, S2A_SHELLCRACKER, 4, 4, 0x0C, 0x10);
        SET8(a1, OBJ_coll_flags, 0x9A);
        SET8(a1, OBJ_frame, 5);
        SET16(a1, CL_PARENT, (uint16_t)o);
        SET16(a1, CL_IDX, (uint16_t)(k * 2));
        int16_t d2 = -0x14;
        if (OB8(o, OBJ_render) & 1) { d2 = 0x14; if (k) d2 = (int16_t)(d2 - 0x0C); }
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + d2);
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 8);
    }
}

static void shellcracker(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj9F_Init */
        o_init(o, S2A_SHELLCRACKER, OB8(o, OBJ_render) | 4, 5, 0x18, 0x10);
        SET8(o, OBJ_coll_flags, 0x0A);
        SET8(o, OBJ_routine, 2);
        if (OB8(o, OBJ_render) & 1) SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        SET16(o, OBJ_x_vel, (uint16_t)-0x40);
        SET8(o, OBJ_y_radius, 0x0C);
        SET8(o, OBJ_x_radius, 0x18);
        SET16(o, SC_TIME, 0x140);
        o_touch(o);
        return;
    case 2: {                                           /* loc_3804E: walk */
        if (sc_sees(o)) { sc_attack(o); break; }
        o_move(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < -8 || d1 >= 0x0C) {
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        } else {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, SC_TIME, OB16(o, SC_TIME) - 1);
            if ((int16_t)OB16(o, SC_TIME) >= 0) { o_animate_s2(o, o_asset_anim(S2A_SHELLCRACKER)); break; }
        }
        SET8(o, OBJ_routine, 4);                        /* loc_3809A */
        SET8(o, OBJ_frame, 0);
        SET16(o, SC_TIME, 0x3B);
        break;
    }
    case 4:                                             /* loc_380C4: pause */
        if (on_screen(o) && sc_sees(o)) { sc_attack(o); break; }
        SET16(o, SC_TIME, OB16(o, SC_TIME) - 1);
        if ((int16_t)OB16(o, SC_TIME) < 0) { SET8(o, OBJ_routine, 2); SET16(o, SC_TIME, 0x140); }
        break;
    default:                                            /* loc_380FC: the claw */
        switch (OB8(o, OBJ_2nd)) {
        case 0:
            SET16(o, SC_TIME, OB16(o, SC_TIME) - 1);
            if ((int16_t)OB16(o, SC_TIME) < 0) {
                SET8(o, OBJ_2nd, 2);
                SET8(o, OBJ_frame, 3);
                sc_claw(o);
            }
            break;
        case 2:
            if (OB8(o, SC_DONE)) { SET8(o, OBJ_2nd, 4); SET16(o, SC_TIME, 0x20); }
            break;
        default:
            SET16(o, SC_TIME, OB16(o, SC_TIME) - 1);
            if ((int16_t)OB16(o, SC_TIME) < 0) {
                SET8(o, OBJ_2nd, 0);
                SET8(o, SC_DONE, 0);
                SET8(o, OBJ_routine, 2);
                SET16(o, SC_TIME, 0x140);
            }
            break;
        }
        break;
    }
    o_touch(o);
    o_remember(o);
}

static void sc_claw_obj(uint32_t o)
{
    static const uint8_t start[8] = { 0, 3, 5, 7, 9, 0x0B, 0x0D, 0x0F };   /* byte_381A4 */
    static const uint8_t reach[8] = { 0x0D, 0x0C, 0x0A, 8, 6, 4, 2, 0 };    /* byte_38222 */
    uint32_t a1 = word_ptr(OB16(o, CL_PARENT));
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* ObjA0_Init */
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | (OB8(a1, OBJ_render) & 1));
        if (OB16(o, CL_IDX)) {
            SET8(o, OBJ_frame, 4);
            SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 6);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 6);
        }
        SET8(o, CL_T1, start[(OB16(o, CL_IDX) >> 1) & 7]);
        break;
    case 2:                                             /* loc_381AC */
        if (r32(a1) != S2(0x9F)) {                      /* loc_381D0: the shell is gone */
            SET8(o, OBJ_routine, 4);
            SET16(o, CL_T1, 0x40);
            break;
        }
        switch (OB8(o, OBJ_2nd)) {
        case 0:                                         /* loc_381E0: wait */
            SET8(o, CL_T1, OB8(o, CL_T1) - 1);
            if ((int8_t)OB8(o, CL_T1) > 0) break;
            SET8(o, OBJ_2nd, 2);
            if (OB16(o, CL_IDX) >= 0x0E) { SET16(o, CL_T1, 0x0B); break; }
            SET16(o, OBJ_x_vel, (OB8(o, OBJ_render) & 1) ? 0x400 : (uint16_t)-0x400);
            SET8(o, CL_T1, reach[(OB16(o, CL_IDX) >> 1) & 7]);
            SET8(o, CL_T2, reach[(OB16(o, CL_IDX) >> 1) & 7]);
            break;
        case 2:                                         /* loc_3822A: out */
            o_move(o);
            SET8(o, CL_T1, OB8(o, CL_T1) - 1);
            if ((int8_t)OB8(o, CL_T1) > 0) break;
            SET8(o, OBJ_2nd, 4);
            SET8(o, CL_T1, 8);
            break;
        case 4:                                         /* loc_38244: hold */
            SET8(o, CL_T1, OB8(o, CL_T1) - 1);
            if ((int8_t)OB8(o, CL_T1) > 0) break;
            SET8(o, OBJ_2nd, 6);
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
            break;
        default:                                        /* loc_38258: back */
            o_move(o);
            SET8(o, CL_T2, OB8(o, CL_T2) - 1);
            if ((int8_t)OB8(o, CL_T2) > 0) break;
            if (!OB16(o, CL_IDX)) {
                SET8(a1, OBJ_frame, 0);
                SET8(a1, SC_DONE, 0xFF);
            }
            o_delete(o);
            return;
        }
        break;
    default:                                            /* loc_38280: falls off */
        o_fall(o);
        SET16(o, CL_T1, OB16(o, CL_T1) - 1);
        if ((int16_t)OB16(o, CL_T1) < 0) { o_delete(o); return; }
        break;
    }
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object A1 - Slicer; A2 - its pincers, thrown at Sonic
 * ---------------------------------------------------------------------- */
#define SL_TIME  0x3A                 /* objoff_2A */
#define SL_PINS  0x3C                 /* objoff_3C/3E */
#define PN_TIME  0x3A                 /* pincer: objoff_2A (word) */
#define PN_PARENT 0x3C                /* objoff_2C (word) */

static void slicer(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* ObjA1_Init */
        o_init(o, S2A_SLICER, OB8(o, OBJ_render) | 4, 5, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 6);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_render) & 1) ? 0x40 : (uint16_t)-0x40);
        SET8(o, OBJ_y_radius, 0x10);
        SET8(o, OBJ_x_radius, 0x10);
        o_touch(o);
        return;
    case 2: {                                           /* ObjA1_Main */
        if (on_screen(o)) {
            int d0;
            int16_t d2, d3;
            s2_orient(o, &d0, NULL, &d2, &d3);
            if (OB8(o, OBJ_render) & 1) d0 -= 2;
            if (!d0 && (uint16_t)(d2 + 0x80) < 0x100 && (uint16_t)(d3 + 0x40) < 0x80) {
                SET8(o, OBJ_routine, 6);                /* loc_38452 */
                SET8(o, OBJ_frame, 3);
                SET8(o, SL_TIME, 8);
                break;
            }
        }
        o_move(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < -8 || d1 >= 0x0C) {
            SET8(o, OBJ_routine, 4);
            SET8(o, SL_TIME, 0x3B);
            break;
        }
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
        o_animate_s2(o, o_asset_anim(S2A_SLICER));
        break;
    }
    case 4:                                             /* loc_38466: turn */
        SET8(o, SL_TIME, OB8(o, SL_TIME) - 1);
        if ((int8_t)OB8(o, SL_TIME) >= 0) break;
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        break;
    case 6:                                             /* loc_38482: throw */
        SET8(o, SL_TIME, OB8(o, SL_TIME) - 1);
        if ((int8_t)OB8(o, SL_TIME) >= 0) break;
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_frame, 4);
        for (int k = 0; k < 2; k++) {                   /* ObjA1_LoadPincers */
            static const int8_t off[2][2] = { { 6, 0 }, { -0x10, 0 } };
            uint32_t a1 = o_alloc_after(o);
            if (!a1) break;
            SET32(a1, OBJ_code, S2(0xA2));
            o_init(a1, S2A_SLICERP, OB8(o, OBJ_render), 4, 0x10, 0x10);
            SET8(a1, OBJ_coll_flags, 0x9A);
            SET8(a1, OBJ_frame, 5);
            SET16(a1, PN_TIME, 0x78);
            SET16(a1, PN_PARENT, (uint16_t)o);
            SET16(o, SL_PINS + (uint32_t)k * 2u, (uint16_t)a1);
            int16_t xv = -0x200, dx = off[k][0];
            if (OB8(a1, OBJ_render) & 1) {
                xv = 0x200;
                dx = (int16_t)-dx;
                SET8(a1, OBJ_status, OB8(a1, OBJ_status) | 1);
            }
            SET16(a1, OBJ_x_vel, (uint16_t)xv);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + dx);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + off[k][1]);
        }
        break;
    default:
        break;
    }
    o_touch(o);
    o_remember(o);
}

static void slicer_pincer(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* ObjA2_Init */
        SET8(o, OBJ_routine, 2);
        o_touch(o);
        o_remember(o);
        return;
    }
    if (OB8(o, OBJ_routine) == 2) {                     /* ObjA2_Main */
        if (!on_screen(o)) { o_delete(o); return; }
        SET16(o, PN_TIME, OB16(o, PN_TIME) - 1);
        uint32_t a1 = word_ptr(OB16(o, PN_PARENT));
        if ((int16_t)OB16(o, PN_TIME) >= 0 && r32(a1) == S2(0xA1)) {
            int d0, d1;
            s2_orient(o, &d0, &d1, NULL, NULL);
            SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + (d0 ? 0x10 : -0x10));
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + (d1 ? 0x10 : -0x10));
            cap_speed(o, 0x200, 0x200);
            o_move(o);
            o_animate_s2(o, o_asset_anim(S2A_SLICERP));
            o_touch(o);
            o_remember(o);
            return;
        }
        SET8(o, OBJ_routine, 4);                        /* loc_3851A */
        SET16(o, PN_TIME, 0x60);
    }
    SET16(o, PN_TIME, OB16(o, PN_TIME) - 1);            /* ObjA2_Main2: drops */
    if ((int16_t)OB16(o, PN_TIME) < 0) { o_delete(o); return; }
    o_fall(o);
    o_animate_s2(o, o_asset_anim(S2A_SLICERP));
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object A4 - Asteron: drifts at Sonic, then bursts into five spikes
 * ---------------------------------------------------------------------- */
#define AS_TIME  0x3A                 /* objoff_2A */

static void asteron_go(uint32_t o)                   /* loc_38A1E */
{
    SET8(o, OBJ_routine, 6);
    SET8(o, AS_TIME, 0x40);
}

static void asteron(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* LoadSubObject */
        o_init(o, S2A_ASTERON, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 0x0B);
        SET8(o, OBJ_routine, 2);
        break;
    case 2: {                                           /* loc_389B6 */
        int16_t d2, d3;
        s2_orient(o, NULL, NULL, &d2, &d3);
        if ((uint16_t)(d2 + 0x60) < 0xC0 && (uint16_t)(d3 + 0x40) < 0x80) SET8(o, OBJ_routine, 4);
        break;
    }
    case 4: {                                           /* loc_389DA */
        int d0, d1;
        int16_t d2, d3;
        s2_orient(o, &d0, &d1, &d2, &d3);
        uint16_t ax = (uint16_t)(d2 < 0 ? -d2 : d2), ay = (uint16_t)(d3 < 0 ? -d3 : d3);
        if (ax >= 0x10 && ax < 0x60) { SET16(o, OBJ_x_vel, d0 ? 0x40 : (uint16_t)-0x40); asteron_go(o); }
        if (ay >= 0x10 && ay < 0x60) { SET16(o, OBJ_y_vel, d1 ? 0x40 : (uint16_t)-0x40); asteron_go(o); }
        break;
    }
    default:                                            /* loc_38A2C */
        SET8(o, AS_TIME, OB8(o, AS_TIME) - 1);
        if ((int8_t)OB8(o, AS_TIME) >= 0) {
            o_move(o);
            o_animate_s2(o, o_asset_anim(S2A_ASTERON));
            break;
        }
        {                                               /* loc_38A44: burst (Obj_CreateProjectiles) */
            static const int8_t spikes[5][6] = {        /* word_38A68: x, y, x_vel, y_vel (high bytes), frame, flip */
                { 0, -8, 0, -4, 2, 0 }, { 8, -4, 3, -1, 3, 1 }, { 8, 8, 3, 3, 4, 1 },
                { -8, 8, -3, 3, 4, 0 }, { -8, -4, -3, -1, 3, 0 },
            };
            for (int k = 0; k < 5; k++) {
                uint32_t a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, S2(0x98));
                SET8(a1, OBJ_routine, 2);
                o_init(a1, S2A_ASTERON, 0x80 | 4 | (uint8_t)spikes[k][5], 5, 4, 4);
                SET16(a1, OBJ_art_tile, OB16(a1, OBJ_art_tile) | 0x8000);
                SET8(a1, OBJ_coll_flags, 0x98);
                SET8(a1, S2_PROJ_KIND, S2_PROJ_MOVE);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + spikes[k][0]);
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + spikes[k][1]);
                SET16(a1, OBJ_x_vel, (uint16_t)(spikes[k][2] << 8));
                SET16(a1, OBJ_y_vel, (uint16_t)(spikes[k][3] << 8));
                SET8(a1, OBJ_frame, (uint8_t)spikes[k][4]);
            }
            SET32(o, OBJ_code, S3K_Obj_Explosion);      /* ObjID_Explosion, routine 2: no animal */
            SET8(o, OBJ_routine, 6);
            SET8(o, OBJ_coll_flags, 0);
            o_sfx_s2(SFX_EXPLODE);
            o_draw(o);
            return;
        }
    }
    o_touch(o);
    o_remember(o);
}

void s2_obj_mtz_register(TriObjFn *t)
{
    t[0x42] = steam_spring;
    t[0x47] = button;
    t[0x64] = twin_stompers;
    t[0x65] = moving_block;
    t[0x66] = spring_wall;
    t[0x67] = spin_tube;
    t[0x68] = spike_block;
    t[0x69] = nut;
    t[0x6C] = lava_cup;
    t[0x6D] = floor_spike;
    t[0x6E] = gear;
    t[0x70] = wheel;
    t[0x71] = lava_bubble;
    t[0x9F] = shellcracker;
    t[0xA0] = sc_claw_obj;
    t[0xA1] = slicer;
    t[0xA2] = slicer_pincer;
    t[0xA4] = asteron;
}
