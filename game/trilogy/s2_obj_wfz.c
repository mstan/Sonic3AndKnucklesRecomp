/*
 * s2_obj_wfz.c — Sonic 2 Wing Fortress objects, transcribed from s2disasm:
 * palette switcher (8B), Clucker and its base (AE/AD), tilting platform
 * (B6), wall turret (B8), the laser that downs the Tornado (B9), belt wheel
 * (BA), the getaway ship's thrust (BC), belt platforms (BD), gun platform
 * (BE), catapult (C0), breakable plating (C1), rivet (C2), the Tornado's
 * smoke (C3), hang bar (D9) and the horizontal propeller's lift (B5).
 *
 * S2's objoff_2A..2F collide with S3K's status ($2A) and subtype ($2C):
 * here 2A -> $40, 2C -> $42, 2E -> $44, 2F -> $45.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define OBJ_prev_anim   0x21
#define F2A             0x40
#define F2C             0x42
#define P_OBJCTRL       0x2E
#define P_FLIP_ANGLE    0x27
#define P_FLIPS_LEFT    0x30
#define P_FLIP_SPEED    0x31
#define ST_INAIR        0x02
#define ST_ONOBJ        0x08
#define ANI_FLOAT2      0x0F
#define ANI_HANG        0x11
#define ANI_HANG2       0x14
#define BTN_ABC         0x70
#define BTN_DIRS        0x0F
#define RAM_WindTunnel_holding  0xFFF7C8u   /* WindTunnel_flag */
#define RAM_Ctrl_1_press_logical 0xFFF603u
#define RAM_Camera_X_coarse     0xFFF76Eu
#define SFX_SLOWSMASH   0xCB          /* SndID_SlowSmash */
#define SFX_LARGELASER  0xEF          /* SndID_LargeLaser */
#define S3K_Map_Explosion   0x01E758u
#define ArtTile_Explosion   0x05A0

extern uint8_t s2_wfz_fire_toggle;            /* WFZ_SCZ_Fire_Toggle (trilogy_screen.c) */
extern int16_t s2_wfz_bg_xoff;                /* Camera_BG_X_offset (trilogy_s2dle.c) */

static int on_screen(uint32_t o) { return (OB8(o, OBJ_render) & 0x80) != 0; }
static uint32_t ptr(uint16_t w) { return 0xFF0000u | w; }
static uint16_t pad(int n) { return r16(n ? 0xFFF606u : RAM_Ctrl_1); }

static void gone(uint32_t o)                         /* MarkObjGone */
{
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void gone3(uint32_t o)                        /* MarkObjGone3: no display */
{
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
}

static void anim_reset(uint32_t o, int frame)        /* clr.l mapping_frame..; clr anim_frame_duration */
{
    SET8(o, OBJ_frame, frame);
    SET8(o, OBJ_anim_frame, 0);
    SET8(o, OBJ_anim, 0);
    SET8(o, OBJ_prev_anim, 0);
    SET8(o, OBJ_anim_timer, 0);
}

static void drop_riders(uint32_t o)                  /* loc_3B7BC */
{
    for (int n = 0; n < 2; n++) {
        uint8_t bit = n ? ST_P2_STAND : ST_P1_STAND;
        if (!(OB8(o, OBJ_status) & bit)) continue;
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
        uint32_t p = o_player(n);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~ST_ONOBJ) | ST_INAIR);
    }
}

static void sub_time(uint32_t o)                     /* loc_3B77E */
{
    uint8_t d0 = OB8(o, OBJ_subtype) & 0xF0;
    SET8(o, OBJ_subtype, d0);
    SET16(o, F2A, d0);
}

static int beat(uint32_t o)                          /* (Vint_runcount+3) & $F0 == subtype */
{
    return (o_frame_counter() & 0xF0) == OB8(o, OBJ_subtype);
}

/* Obj98 shots: S2_PROJ_ANIM moves and animates with the asset at +1 */
static void shot(uint32_t o, int asset, int frame, int prio, int16_t x, int16_t y, int16_t xv, int16_t yv)
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x98));
    SET8(a1, OBJ_routine, 2);
    o_init(a1, asset, 0x80 | 4, prio, 4, 4);
    SET8(a1, OBJ_coll_flags, 0x98);
    SET8(a1, OBJ_frame, frame);
    SET8(a1, S2_PROJ_KIND, S2_PROJ_ANIM);
    SET8(a1, S2_PROJ_KIND + 1, asset);
    SET16(a1, OBJ_x_pos, (uint16_t)x);
    SET16(a1, OBJ_y_pos, (uint16_t)y);
    SET16(a1, OBJ_x_vel, (uint16_t)xv);
    SET16(a1, OBJ_y_vel, (uint16_t)yv);
}

/* ------------------------------------------------------------------------
 * Object 8B - palette switcher: crossing it turns the fire cycle to the
 * belt cycle (or back)
 * ---------------------------------------------------------------------- */
#define PS_RANGE 0x32                 /* objoff_32 (word) */
#define PS_SIDE  0x34                 /* objoff_34/35 */

static void pal_switch_act(uint32_t o, int n)
{
    uint32_t p = o_player(n), a2 = o + PS_SIDE + (uint32_t)n;
    uint16_t d1 = OB16(o, OBJ_x_pos), px = OB16(p, OBJ_x_pos);
    int flip = OB8(o, OBJ_render) & 1;
    if (!r8(a2)) {
        if (d1 > px) return;
        w8(a2, 1);
    } else {
        if (d1 <= px) return;
        w8(a2, 0);
        flip = !flip;
    }
    uint16_t y = OB16(o, OBJ_y_pos), r = OB16(o, PS_RANGE), py = OB16(p, OBJ_y_pos);
    if (py < (uint16_t)(y - r) || py >= (uint16_t)(y + r)) return;
    s2_wfz_fire_toggle = flip ? 0 : 1;
}

static void pal_switch(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj8B_Init */
        static const uint16_t range[4] = { 0x20, 0x40, 0x80, 0x100 };
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET8(o, OBJ_width_px, 0x10);
        SET16(o, OBJ_priority, 5 << 7);
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype) & 3);
        SET16(o, PS_RANGE, range[OB8(o, OBJ_subtype) & 3]);
        for (int n = 0; n < 2; n++)
            if (OB16(o, OBJ_x_pos) < OB16(o_player(n), OBJ_x_pos)) SET8(o, PS_SIDE + n, 1);
    }
    if (!r16(RAM_Debug_placement_mode)) {               /* Obj8B_Main */
        pal_switch_act(o, 0);
        pal_switch_act(o, 1);
    }
    gone3(o);
}

/* ------------------------------------------------------------------------
 * Object AD - Clucker's base (solid); AE - Clucker: rises when Sonic
 * comes near, then spits shots
 * ---------------------------------------------------------------------- */
static void clucker_base(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_CLUCKBASE, OB8(o, OBJ_render) | 4, 4, 0x18, 0x10);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_frame, 0x0C);
        return;
    }
    o_solid(o, 0x1B, 8, 8, OB16(o, OBJ_x_pos));
    gone(o);
}

static void clucker(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* ObjAE_Init */
        o_init(o, S2A_CLUCKER, OB8(o, OBJ_render) | 4, 5, 0x10, 0x10);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_frame, 0x15);
        if (OB8(o, OBJ_render) & 1) SET8(o, OBJ_status, OB8(o, OBJ_status) | 1);
        return;
    case 2: {
        int16_t d2;
        s2_orient(o, NULL, NULL, &d2, NULL);
        if ((uint16_t)(d2 + 0x80) < 0x100) SET8(o, OBJ_routine, 4);
        break;
    }
    case 4:
        o_animate_s2(o, o_asset_anim(S2A_CLUCKER));     /* rises: $FC -> routine 6 */
        if (OB8(o, OBJ_routine) != 4) {
            anim_reset(o, 8);
            SET8(o, OBJ_coll_flags, 6);
        }
        break;
    case 6:
        o_animate_s2(o, o_asset_anim(S2A_CLUCKERB));
        break;
    case 8:
        if (OB8(o, F2A)) { SET8(o, F2A, OB8(o, F2A) - 1); break; }
        SET8(o, OBJ_routine, 0x0A);
        anim_reset(o, 0x0B);
        {                                               /* loc_39526 */
            int flip = OB8(o, OBJ_render) & 1;
            shot(o, S2A_CLUCKSHOT, 0x0D, 5, (int16_t)(OB16(o, OBJ_x_pos) + (flip ? 8 : -8)),
                 (int16_t)(OB16(o, OBJ_y_pos) + 0x0B), flip ? 0x200 : -0x200, 0);
        }
        break;
    case 0x0A:
        o_animate_s2(o, o_asset_anim(S2A_CLUCKERC));
        break;
    default:                                            /* $C */
        SET8(o, OBJ_routine, 8);
        SET8(o, F2A, 0x40);
        break;
    }
    if (OB8(o, OBJ_coll_flags)) o_touch(o);
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object B6 - tilting platform: tips on its beat (subtype $x0) and tips
 * riders off; subtype 4: tips toward Sonic when stood on
 * ---------------------------------------------------------------------- */
static void tilt_solid(uint32_t o) { o_solid(o, 0x23, 4, 4, OB16(o, OBJ_x_pos)); }       /* loc_3B790 */

static void tilt_platform(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* ObjB6_Init */
        o_init(o, S2A_TILTPLAT, OB8(o, OBJ_render) | 4, 4, 0x10, 0x20);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_routine, (OB8(o, OBJ_subtype) & 6) + 2);
        return;
    }
    uint32_t ani = o_asset_anim(S2A_TILTPLAT);
    if (OB8(o, OBJ_routine) == 6) {                     /* loc_3B6C8 */
        switch (OB8(o, OBJ_2nd)) {
        case 0:
            tilt_solid(o);
            if (o_standing(o)) { SET8(o, OBJ_2nd, 2); SET16(o, F2A, 0x10); }
            break;
        case 2:
            tilt_solid(o);
            SET16(o, F2A, OB16(o, F2A) - 1);
            if ((int16_t)OB16(o, F2A) >= 0) break;
            SET8(o, OBJ_2nd, 4);
            SET8(o, OBJ_anim, 0);
            {
                int d0;
                s2_orient(o, &d0, NULL, NULL, NULL);
                SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | (d0 ? 0 : 1));
            }
            drop_riders(o);
            break;
        case 4:
            o_animate_s2(o, ani);
            break;
        default:
            SET8(o, OBJ_2nd, 0);
            break;
        }
        gone(o);
        return;
    }
    switch (OB8(o, OBJ_2nd)) {                          /* loc_3B602 (routine 2) */
    case 0:
        SET8(o, OBJ_2nd, 2);
        sub_time(o);
        break;
    case 2:
        tilt_solid(o);
        if (beat(o)) {
            SET8(o, OBJ_2nd, 4);
            SET8(o, OBJ_anim, 0);
            drop_riders(o);
        }
        break;
    case 4:
        o_animate_s2(o, ani);
        break;
    default:
        SET8(o, OBJ_2nd, 2);
        SET16(o, F2A, 0xC0);
        break;
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object B8 - wall turret: wakes when Sonic is below, aims and fires
 * ---------------------------------------------------------------------- */
static void wall_turret(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, S2A_TURRET, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_routine, 2);
        return;
    case 2: {
        int d1;
        int16_t d2;
        if (on_screen(o)) {
            s2_orient(o, NULL, &d1, &d2, NULL);
            if (d1 && (uint16_t)(d2 + 0x60) < 0xC0) {
                SET8(o, OBJ_routine, 4);
                SET16(o, F2A, 2);
            }
        }
        break;
    }
    default: {                                          /* loc_3B9AA */
        static const int8_t aim[3][4] = { { 0, 0x18, 0, 1 }, { -0x11, 0x10, -1, 1 }, { 0x11, 0x10, 1, 1 } };
        int d0;
        int16_t d2;
        s2_orient(o, &d0, NULL, &d2, NULL);
        int d6 = (uint16_t)(d2 + 0x20) < 0x40 ? 0 : (d0 >> 1) + 1;
        SET8(o, OBJ_frame, d6);
        SET16(o, F2A, OB16(o, F2A) - 1);
        if (!OB16(o, F2A)) {
            SET16(o, F2A, 0x60);
            const int8_t *a = aim[d6];
            shot(o, S2A_TURRET, 3, 3, (int16_t)(OB16(o, OBJ_x_pos) + a[0]), (int16_t)(OB16(o, OBJ_y_pos) + a[1]),
                 a[2] * 0x100, a[3] * 0x100);
        }
        break;
    }
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object B9 - the laser that shoots down the Tornado
 * ---------------------------------------------------------------------- */
static void hlaser(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, S2A_HLASER, OB8(o, OBJ_render) | 4, 1, 0x60, 0x08);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_routine, 2);
        return;
    case 2:
        if (on_screen(o)) {
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_x_vel, (uint16_t)-0x1000);
            o_sfx_s2(SFX_LARGELASER);
        }
        break;
    default:
        o_move(o);
        break;
    }
    if ((int16_t)OB16(o, OBJ_x_pos) < (int16_t)(r16(RAM_Camera_X_pos) - 0x40)) { o_delete(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object BA - conveyor belt wheel (scenery)
 * ---------------------------------------------------------------------- */
static void belt_wheel(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_BELTWHEEL, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_routine, 2);
        return;
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object BC - fire out of the getaway ship (moves with the BG ship)
 * ---------------------------------------------------------------------- */
static void ship_fire(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_THRUST, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_routine, 2);
        SET16(o, F2C, OB16(o, OBJ_x_pos));
        return;
    }
    uint16_t d1 = (uint16_t)s2_wfz_bg_xoff;
    if (d1 >= 0x380) { o_delete(o); return; }
    SET16(o, OBJ_x_pos, OB16(o, F2C) + d1);
    SET8(o, F2A, OB8(o, F2A) ^ 1);
    if (!(OB8(o, F2A) & 1)) o_draw(o);                  /* bchg: shown when the bit was set */
}

/* ------------------------------------------------------------------------
 * Object BD - belt platforms: a spawner sends one up (subtype $7E) or
 * down ($80) every $40 frames; they fold away at the end
 * ---------------------------------------------------------------------- */
static void belt_platform(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* ObjBD_Init: the spawner */
        SET8(o, OBJ_routine, 2);
        SET16(o, F2A, 1);
        return;
    case 2:
        SET16(o, F2A, OB16(o, F2A) - 1);
        if (!OB16(o, F2A)) {
            SET16(o, F2A, 0x40);
            uint32_t a1 = o_alloc_after(o);             /* loc_3BCF8 */
            if (a1) {
                SET32(a1, OBJ_code, S2(0xBD));
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, OBJ_routine, 4);
                SET8(a1, OBJ_subtype, OB8(o, OBJ_subtype));
                SET8(a1, OBJ_render, OB8(o, OBJ_render));
            }
        }
        gone3(o);
        return;
    }
    uint32_t ani = o_asset_anim(S2A_BELTPLAT);
    switch (OB8(o, OBJ_2nd)) {                          /* loc_3BC50 */
    case 0: {
        o_init(o, S2A_BELTPLAT, OB8(o, OBJ_render) | 4, 4, 0x18, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_frame, 2);
        SET8(o, OBJ_2nd, 2);
        SET16(o, F2A, (OB8(o, OBJ_render) & 1) ? 0x1C7 : 0xC7);
        uint8_t k = (uint8_t)(OB8(o, OBJ_subtype) - 0x7E);
        SET8(o, OBJ_subtype, k);
        SET16(o, OBJ_y_vel, k ? 0x100 : (uint16_t)-0x100);
        break;
    }
    case 2:
        o_animate_s2(o, ani);
        break;
    case 4: {
        SET16(o, F2A, OB16(o, F2A) - 1);
        if ((int16_t)OB16(o, F2A) < 0) {
            SET8(o, OBJ_2nd, 6);
            SET8(o, OBJ_anim, 1);
            break;
        }
        uint16_t oldx = OB16(o, OBJ_x_pos);             /* loc_3BCDE */
        o_move(o);
        o_platform(o, 0x23, 5, oldx);
        break;
    }
    case 6:
        o_animate_s2(o, ani);
        break;
    default:                                            /* loc_3BCD6 */
        drop_riders(o);
        o_delete(o);
        return;
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object BE - lateral cannon: a platform that slides out on its beat
 * ---------------------------------------------------------------------- */
static void gun_platform(uint32_t o)
{
    uint32_t ani = o_asset_anim(S2A_GUNPLAT);
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, S2A_GUNPLAT, OB8(o, OBJ_render) | 4, 4, 0x18, 0x20);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_routine, 2);
        sub_time(o);
        return;
    case 2:
        if (beat(o)) {
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_anim, 0);
            SET16(o, F2A, 0xA0);
        }
        break;
    case 4: case 8:
        o_animate_s2(o, ani);                           /* $FC: on to 6 / $A */
        break;
    case 6:
        SET16(o, F2A, OB16(o, F2A) - 1);
        if (!OB16(o, F2A)) {
            SET8(o, OBJ_routine, 8);
            SET8(o, OBJ_anim, 1);
            drop_riders(o);
            break;
        }
        if (OB8(o, OBJ_frame) == 3 || OB8(o, OBJ_frame) == 4)       /* loc_3BE04 */
            o_platform(o, 0x23, 0x19, OB16(o, OBJ_x_pos));
        else
            drop_riders(o);
        break;
    default:                                            /* $A */
        SET8(o, OBJ_routine, 2);
        SET16(o, F2A, 0x40);
        break;
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object C0 - speed launcher: shoots along subtype*16 px and flings riders
 * ---------------------------------------------------------------------- */
#define CP_ACC   0x30                 /* objoff_30 (word) */
#define CP_END   0x32                 /* objoff_32 */
#define CP_HOME  0x34                 /* objoff_34 */

static void catapult_hold(uint32_t o, uint32_t p)    /* loc_3BFB4 */
{
    SET16(p, OBJ_ground_vel, 0);
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~1) | ((OB8(o, OBJ_status) & 1) ? 0 : 1));
}

static void catapult_riders(uint32_t o, void (*fn)(uint32_t, uint32_t))
{
    if (OB8(o, OBJ_status) & ST_P1_STAND) fn(o, o_player(0));
    if (OB8(o, OBJ_status) & ST_P2_STAND) fn(o, o_player(1));
}

static void catapult_fling(uint32_t o, uint32_t p)   /* loc_3C020 */
{
    SET16(p, OBJ_x_vel, OB16(o, OBJ_x_vel));
    SET16(p, OBJ_y_vel, (uint16_t)-0x400);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_INAIR);
}

static void catapult(uint32_t o)
{
    int flip = OB8(o, OBJ_status) & 1;
    if (!OB8(o, OBJ_routine)) {                         /* ObjC0_Init */
        o_init(o, S2A_CATAPULT, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_routine, 2);
        int16_t d0 = (int16_t)(OB8(o, OBJ_subtype) << 4);
        if (!flip) d0 = (int16_t)-d0;
        SET16(o, CP_HOME, OB16(o, OBJ_x_pos));
        SET16(o, CP_END, (uint16_t)(OB16(o, OBJ_x_pos) + d0));
    }
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* loc_3BF66 */
        if (!o_standing(o)) break;
        SET8(o, OBJ_2nd, 2);
        SET16(o, OBJ_x_vel, flip ? 0xC00 : (uint16_t)-0xC00);
        SET16(o, CP_ACC, flip ? 0x80 : (uint16_t)-0x80);
        o_move(o);
        catapult_riders(o, catapult_hold);
        break;
    case 2: {                                           /* loc_3BFD8 */
        SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + OB16(o, CP_ACC));
        o_move(o);
        int16_t d0 = (int16_t)(OB16(o, CP_END) - OB16(o, OBJ_x_pos));
        if (flip) d0 = (int16_t)-d0;
        if (d0 < 0) { catapult_riders(o, catapult_hold); break; }
        SET8(o, OBJ_2nd, 4);                            /* loc_3C034 */
        SET16(o, OBJ_x_pos, OB16(o, CP_END));
        catapult_riders(o, catapult_fling);
    }   /* fall through */
    default: {                                          /* loc_3C062: back home */
        uint16_t d0 = OB16(o, OBJ_x_pos);
        int up = (int16_t)OB16(o, CP_ACC) >= 0;
        int d2 = up;
        d0 = (uint16_t)(d0 + (up ? -4 : 4));
        if (d0 < OB16(o, CP_HOME)) d2 = !d2;
        if (!d2) {
            SET8(o, OBJ_2nd, 0);
            d0 = OB16(o, CP_HOME);
        }
        SET16(o, OBJ_x_pos, d0);
        break;
    }
    }
    o_platform(o, 0x10, 0x11, OB16(o, OBJ_x_pos));
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object C1 - breakable plating: hang on; it breaks after a while
 * ---------------------------------------------------------------------- */
#define PL_TIME  0x30                 /* plating_time (word) */
#define PL_GRAB  0x32                 /* plating_grabbed */
#define PL_WAIT  0x3F                 /* plating_unk */

static void plating_breakup(uint32_t o)              /* loc_3C1F4 */
{
    static const int16_t pos[4][2] = { { -0x10, -0x10 }, { -0x10, 0x10 }, { -0x30, -0x10 }, { -0x30, 0x10 } };
    static const uint8_t wait[4] = { 0, 4, 0x18, 0x20 };
    uint16_t x = OB16(o, OBJ_x_pos), y = OB16(o, OBJ_y_pos);
    uint16_t prio = (uint16_t)(OB16(o, OBJ_priority) - 0x80);
    uint32_t maps = r32(o + OBJ_mappings), code = r32(o);
    uint16_t art = OB16(o, OBJ_art_tile);
    uint32_t a1 = o;
    for (int k = 0; k < 4; k++) {
        if (k && !(a1 = o_alloc_after(o))) break;
        SET8(a1, OBJ_routine, 4);
        SET32(a1, OBJ_code, code);
        w32(a1 + OBJ_mappings, maps);
        SET16(a1, OBJ_art_tile, art);
        SET8(a1, OBJ_render, 0x84);
        SET16(a1, OBJ_x_pos, (uint16_t)(x + pos[k][0]));
        SET16(a1, OBJ_y_pos, (uint16_t)(y + pos[k][1]));
        SET16(a1, OBJ_priority, prio);
        SET8(a1, OBJ_width_px, 0x10);
        SET8(a1, OBJ_height_px, 0x10);
        SET8(a1, OBJ_frame, 1);
        SET16(a1, OBJ_x_vel, (uint16_t)-0x400);
        SET16(a1, OBJ_y_vel, 0);
        SET8(a1, OBJ_coll_flags, 0);
        SET8(a1, PL_WAIT, wait[k]);
    }
    o_sfx_s2(SFX_SLOWSMASH);
}

static void plating(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* ObjC1_Init */
        o_init(o, S2A_PANELS, OB8(o, OBJ_render) | 4, 4, 0x40, 0x20);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 0xE1);
        SET8(o, OBJ_routine, 2);
        SET16(o, PL_TIME, (uint16_t)(OB8(o, OBJ_subtype) * 60));
        /* fall through */
    case 2: {
        uint32_t p = o_player(0);
        if (OB8(o, PL_GRAB)) {
            int release = 0;
            if (OB16(o, PL_TIME)) {
                SET16(o, PL_TIME, OB16(o, PL_TIME) - 1);
                if (!OB16(o, PL_TIME)) release = 1;
            }
            if (!release) {
                uint16_t d0 = (uint16_t)(OB16(o, OBJ_y_pos) - 24);
                uint8_t held = r8(RAM_Ctrl_1);
                if (held & 1) {                         /* up */
                    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 1);
                    if (d0 >= OB16(p, OBJ_y_pos)) SET16(p, OBJ_y_pos, d0);
                }
                d0 = (uint16_t)(d0 + 48);
                if (held & 2) {                         /* down */
                    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + 1);
                    if (d0 < OB16(p, OBJ_y_pos)) SET16(p, OBJ_y_pos, d0);
                }
                if (!(r8(RAM_Ctrl_1_press_logical) & BTN_ABC)) break;
            }
            SET8(o, OBJ_coll_flags, 0);                 /* ObjC1_Release */
            SET8(p, P_OBJCTRL, 0);
            w8(RAM_WindTunnel_holding, 0);
            SET8(o, PL_GRAB, 0);
            plating_breakup(o);
            goto fall;
        }
        if (OB8(o, OBJ_coll_flags)) {                   /* TouchResponse's special touch ($E1: 4x$10) */
            int hit = o_s1_touch(o, 4, 0x10);
            if (hit & 1) SET8(o, OBJ_coll_prop, OB8(o, OBJ_coll_prop) + 1);
            if (hit & 2) SET8(o, OBJ_coll_prop, OB8(o, OBJ_coll_prop) + 2);
        }
        if (!OB8(o, OBJ_coll_prop)) break;              /* ObjC1_Grab */
        if ((uint16_t)(OB16(o, OBJ_x_pos) - 20) >= OB16(p, OBJ_x_pos)) break;
        SET8(o, OBJ_coll_prop, 0);
        if (OB8(p, OBJ_routine) >= 4) break;
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) - 20);
        SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
        SET8(p, OBJ_anim, ANI_HANG);
        SET8(p, P_OBJCTRL, 1);
        w8(RAM_WindTunnel_holding, 1);
        SET8(o, PL_GRAB, 1);
        break;
    }
    default:
    fall:                                               /* ObjC1_FallOff */
        if (OB8(o, PL_WAIT)) {
            SET8(o, PL_WAIT, OB8(o, PL_WAIT) - 1);
        } else {
            o_move(o);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 8);
            o_animate_s2(o, o_asset_anim(S2A_PANELS));
        }
        if (!on_screen(o)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object C2 - the rivet: spin onto it to bust into the ship
 * ---------------------------------------------------------------------- */
void s2_layout_put(int cx, int cy, int bg, uint8_t chunk);     /* trilogy_s2dle.c */

static void rivet(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_RIVET, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_routine, 2);
        return;
    }
    uint32_t p = o_player(0);
    uint8_t anim = OB8(p, OBJ_anim);                    /* objoff_30 */
    o_solid(o, 0x1B, 8, 9, OB16(o, OBJ_x_pos));
    if ((OB8(o, OBJ_status) & ST_P1_STAND) && anim == 2) {   /* ObjC2_Bust */
        static const uint8_t row8[6] = { 0x8A, 0x70, 0x71, 0x72, 0x73, 0x74 };
        static const uint8_t row9[6] = { 0x6E, 0x78, 0x79, 0x78, 0x78, 0x7A };
        w16(RAM_Camera_min_X_pos, 0x2880);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_STAND);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
        for (int k = 0; k < 6; k++) {
            s2_layout_put(0x50 + k, 8, 0, row8[k]);
            s2_layout_put(0x50 + k, 9, 0, row9[k]);
        }
        SET32(o, OBJ_code, S3K_Obj_Explosion);          /* turns into an explosion (Obj27) */
        SET8(o, OBJ_routine, 6);
        o_sfx_s2(0xC1); /* SndID_Explosion */
        return;
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object C3 - the falling Tornado's smoke (S3K's explosion frames)
 * ---------------------------------------------------------------------- */
static void smoke(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* ObjC3_Init */
        SET8(o, OBJ_routine, 2);
        w32(o + OBJ_mappings, S3K_Map_Explosion);
        SET16(o, OBJ_art_tile, ArtTile_Explosion);
        SET8(o, OBJ_render, 4);
        SET16(o, OBJ_priority, 5 << 7);
        SET8(o, OBJ_width_px, 0x0C);
        SET8(o, OBJ_height_px, 0x0C);
        SET8(o, OBJ_anim_timer, 7);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - (o_random() & 0x1C));
        SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x10);
        SET16(o, OBJ_y_vel, (uint16_t)-0x100);
        SET16(o, OBJ_x_vel, (uint16_t)-0x100);
        return;
    }
    o_move(o);
    SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
    if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
        SET8(o, OBJ_anim_timer, 7);
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        if (OB8(o, OBJ_frame) == 5) { o_delete(o); return; }
    }
    o_draw(o);
}

void s2_wfz_smoke(uint32_t o)                       /* ObjB2_Main_WFZ_Start_load_smoke */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0xC3));
    SET16(a1, F2C, (uint16_t)o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
}

/* ------------------------------------------------------------------------
 * Object D9 - invisible hang bar
 * ---------------------------------------------------------------------- */
static void hang_bar_act(uint32_t o, int n)
{
    uint32_t p = o_player(n), a2 = o + 0x30 + (uint32_t)n;
    uint16_t d0 = pad(n);
    if (r8(a2)) {
        if (!(d0 & BTN_ABC)) return;
        SET8(p, P_OBJCTRL, 0);
        w8(a2, 0);
        w8(a2 + 2, 18);
        if (d0 & (BTN_DIRS << 8)) w8(a2 + 2, 60);
        SET16(p, OBJ_y_vel, (uint16_t)-0x300);
        return;
    }
    if (r8(a2 + 2)) {
        w8(a2 + 2, r8(a2 + 2) - 1);
        if (r8(a2 + 2)) return;
    }
    if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x18) >= 0x30) return;
    if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos)) >= 0x10) return;
    if ((int8_t)OB8(p, P_OBJCTRL) < 0) return;
    if (OB8(p, OBJ_routine) >= 6) return;
    if (r16(RAM_Debug_placement_mode)) return;
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, 0);
    SET16(p, OBJ_ground_vel, 0);
    SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(p, OBJ_anim, ANI_HANG2);
    SET8(p, P_OBJCTRL, 1);
    w8(a2, 1);
}

static void hang_bar(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 4);
        SET8(o, OBJ_width_px, 0x18);
        SET16(o, OBJ_priority, 4 << 7);
    }
    hang_bar_act(o, 0);
    hang_bar_act(o, 1);
    gone3(o);
}

/* ------------------------------------------------------------------------
 * Object B5 (WFZ) - horizontal propeller: lifts players over it, tumbling
 * ---------------------------------------------------------------------- */
static void hprop_lift(uint32_t o, uint32_t p)       /* ObjB5_CheckPlayer */
{
    if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x40) >= 0x80) return;
    uint16_t d1 = (uint16_t)(o_osc(0x14 + 2) + OB16(p, OBJ_y_pos) + 0x60);
    if (d1 < OB16(o, OBJ_y_pos)) return;
    d1 = (uint16_t)(d1 - OB16(o, OBJ_y_pos));
    if (d1 >= 0x90) return;
    if (d1 >= 0x60) {
        d1 = (uint16_t)(d1 - 0x60);
        d1 = (uint16_t)(~d1 * 2);
    } else {
        d1 = (uint16_t)(d1 - 0x60);
    }
    d1 = (uint16_t)(d1 + 0x60);
    int16_t dy = (int16_t)(-(int16_t)d1) >> 4;
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + dy);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_INAIR);
    SET16(p, OBJ_y_vel, 0);
    SET16(p, OBJ_ground_vel, 1);
    if (OB8(p, P_FLIP_ANGLE)) return;
    SET8(p, P_FLIP_ANGLE, 1);
    SET8(p, OBJ_anim, ANI_FLOAT2);
    SET8(p, P_FLIPS_LEFT, 0x7F);
    SET8(p, P_FLIP_SPEED, 8);
}

static void hprop_main(uint32_t o)                   /* ObjB5_Main */
{
    if (OB8(o, OBJ_anim) == 4) {
        hprop_lift(o, o_player(0));
        hprop_lift(o, o_player(1));
    }
    o_animate_s2(o, o_asset_anim(S2A_HPROP));
    gone(o);
}

void s2_obj_wfz_register(TriObjFn *t)
{
    extern void (*s2_hprop_main)(uint32_t o);
    s2_hprop_main = hprop_main;
    t[0x8B] = pal_switch;
    t[0xAD] = clucker_base;
    t[0xAE] = clucker;
    t[0xB6] = tilt_platform;
    t[0xB8] = wall_turret;
    t[0xB9] = hlaser;
    t[0xBA] = belt_wheel;
    t[0xBC] = ship_fire;
    t[0xBD] = belt_platform;
    t[0xBE] = gun_platform;
    t[0xC0] = catapult;
    t[0xC1] = plating;
    t[0xC2] = rivet;
    t[0xC3] = smoke;
    t[0xD9] = hang_bar;
}
