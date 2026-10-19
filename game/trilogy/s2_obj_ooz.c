/*
 * s2_obj_ooz.c — Sonic 2 Oil Ocean objects, transcribed from s2disasm:
 * the oil surface (07, loaded by the level) and the oil slides (OilSlides,
 * run from VBlank), burner platform (33), striped block + its invisible
 * launcher (3D), fan (3F), sliding spike pair (43), pressure spring (45),
 * launch ball (48), Octus (4A), Aquis (50).
 *
 * S2 variables at objoff_2A-2F would hit S3K's status/subtype, so they live
 * at $30+ here. S3K kept S2's sliding flag (status_secondary bit 7) and its
 * slide animation ($1B), so the oil slides drive the S3 characters as is.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define OBJ_prev_anim   0x21
#define P_STATUS2       0x2B          /* status_secondary */
#define P_OBJCTRL       0x2E
#define P_MOVE_LOCK     0x32
#define P_JUMPING       0x40
#define P_INTERACT      0x42
#define P_TOP_SOLID     0x46
#define P_LRB_SOLID     0x47
#define P_FLIP_ANGLE    0x27
#define P_FLIPS_LEFT    0x30
#define P_FLIP_SPEED    0x31
#define ST_INAIR        0x02
#define ST_ROLL         0x04
#define ST_ONOBJ        0x08
#define ST_PUSH         0x20
#define SLIDING         0x80          /* status_secondary.sliding */
#define ANI_WALK        0
#define ANI_ROLL        2
#define ANI_WAIT        5
#define ANI_SPRING      0x10
#define ANI_SLIDE       0x1B
#define SFX_ROLL        0xBE          /* SndID_Roll */
#define SFX_LID_POP     0xD4          /* SndID_OOZLidPop */
#define SFX_SPIKE_MOVE  0xD5          /* SndID_SlidingSpike */
#define SFX_SPRING      0xCC          /* SndID_Spring */
#define SFX_OIL_SLIDE   0xF0          /* SndID_OilSlide */

static int on_screen(uint32_t o) { return (OB8(o, OBJ_render) & 0x80) != 0; }

static void gone(uint32_t o)                         /* MarkObjGone */
{
    if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

static void obj_move_player(uint32_t p)              /* ObjectMove on a carried player */
{
    int32_t x = (int32_t)r32(p + OBJ_x_pos) + ((int32_t)(int16_t)OB16(p, OBJ_x_vel) << 8);
    int32_t y = (int32_t)r32(p + OBJ_y_pos) + ((int32_t)(int16_t)OB16(p, OBJ_y_vel) << 8);
    w32(p + OBJ_x_pos, (uint32_t)x);
    w32(p + OBJ_y_pos, (uint32_t)y);
}

static void clr_anim(uint32_t o)                     /* clr.l mapping_frame; clr.w anim_frame_duration */
{
    SET8(o, OBJ_frame, 0);
    SET8(o, OBJ_anim_frame, 0);
    SET8(o, OBJ_anim, 0);
    SET8(o, OBJ_prev_anim, 0);
    SET8(o, OBJ_anim_timer, 0);
}

/* ------------------------------------------------------------------------
 * OilSlides (VBlank): the slide chunks push a grounded player along; the
 * flat ones leave him a little grip. Not on a slide: lock the pad 5 frames.
 * ---------------------------------------------------------------------- */
static const int8_t oil_speeds[32] = {
    -8, -8, -8, 8, 8, 0, 0, 0, -8, -8, 0, 8, 8, 8, 0, 8,
     8, 8, 0, -8, 0, 0, -8, 8, -8, -8, -8, 8, 8, 8, -8, -8,
};
static const uint8_t oil_chunks[32] = {
    0x2F, 0x30, 0x31, 0x33, 0x35, 0x38, 0x3A, 0x3C, 0x63, 0x64, 0x83, 0x90, 0x91, 0x93, 0xA1, 0xA3,
    0xBD, 0xC7, 0xC8, 0xCE, 0xD7, 0xD8, 0xE6, 0xEB, 0xEC, 0xED, 0xF1, 0xF2, 0xF3, 0xF4, 0xFA, 0xFD,
};

static uint8_t chunk_at(uint16_t x, uint16_t y)
{
    uint16_t row = r16(RAM_Level_layout_header + 8 + (uint32_t)((y >> 7) & 0x0F) * 4u);
    return r8(0xFF0000u | (uint16_t)(row + ((x >> 7) & 0x7F)));
}

static void oil_slide(uint32_t a1, uint8_t d2)
{
    int k = -1;
    if (!(OB8(a1, OBJ_status) & ST_INAIR)) {
        uint8_t c = chunk_at(OB16(a1, OBJ_x_pos), OB16(a1, OBJ_y_pos));
        for (k = 31; k >= 0 && oil_chunks[k] != c; k--) {}
    }
    if (k < 0) {
        if (OB8(a1, P_STATUS2) & SLIDING) {
            SET16(a1, P_MOVE_LOCK, 5);
            SET8(a1, P_STATUS2, OB8(a1, P_STATUS2) & ~SLIDING);
        }
        return;
    }
    int8_t d0 = oil_speeds[k];
    if (d0) {                                           /* loc_4712: slide */
        int8_t d1 = (int8_t)OB8(a1, OBJ_ground_vel);
        if (d0 < 0) { if (d1 > d0) SET16(a1, OBJ_ground_vel, OB16(a1, OBJ_ground_vel) - 0x40); }
        else if (d1 < d0) SET16(a1, OBJ_ground_vel, OB16(a1, OBJ_ground_vel) + 0x40);
        SET8(a1, OBJ_status, (OB8(a1, OBJ_status) & ~1) | (d1 < 0 ? 1 : 0));
        SET8(a1, OBJ_anim, ANI_SLIDE);
        SET8(a1, P_STATUS2, OB8(a1, P_STATUS2) | SLIDING);
        if (!(o_frame_counter() & 0x1F)) o_sfx_s2(SFX_OIL_SLIDE);
        return;
    }
    int16_t v = (int16_t)OB16(a1, OBJ_ground_vel);      /* loc_476E: flat oil, slow control */
    if (d2 & 4) {
        SET8(a1, OBJ_anim, ANI_WALK);
        SET8(a1, OBJ_status, OB8(a1, OBJ_status) | 1);
        v = (int16_t)(v - 4);
        if (v < 0) v = (int16_t)(v - 4);
    }
    if (d2 & 8) {
        SET8(a1, OBJ_anim, ANI_WALK);
        SET8(a1, OBJ_status, OB8(a1, OBJ_status) & ~1);
        v = (int16_t)(v + 4);
        if (v >= 0) v = (int16_t)(v + 4);
    }
    if (v > 0) {
        v = (int16_t)(v - 4);
        if (v <= 0) { v = 0; SET8(a1, OBJ_anim, ANI_WAIT); }
    } else if (v < 0) {
        v = (int16_t)(v + 4);
        if (v >= 0) { v = 0; SET8(a1, OBJ_anim, ANI_WAIT); }
    }
    SET16(a1, OBJ_ground_vel, (uint16_t)v);
    SET8(a1, P_STATUS2, OB8(a1, P_STATUS2) | SLIDING);
}

void s2_ooz_vblank(void)
{
    oil_slide(RAM_Player_1, r8(RAM_Ctrl_1_held_logical));
    oil_slide(RAM_Player_2, r8(RAM_Ctrl_2_held_logical));
}

/* ------------------------------------------------------------------------
 * Object 07 - the oil: holds a player up, sinking him slowly; drowned at 0
 * ---------------------------------------------------------------------- */
#define OL_Y0    0x30
#define OL_SUB1  0x38                 /* oil_char1submersion */
#define OL_SUB2  0x3A                 /* oil_char2submersion */

static uint16_t s_oil;

static void oil(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj07_Init */
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_y_pos, 0x758);
        SET8(o, OBJ_width_px, 0x20);
        SET16(o, OL_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OL_SUB1, 0x30);
        SET8(o, OL_SUB2, 0x30);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 0x80);
    }
    if (r16(RAM_Debug_placement_mode)) return;
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n), sub = o + (n ? OL_SUB2 : OL_SUB1);
        uint8_t bit = (uint8_t)(ST_P1_STAND << n);
        if (OB8(o, OBJ_status) & bit) {
            if (!r8(sub)) {                             /* Obj07_SuffocateCharacter */
                SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
                o_kill_player(p);
                return;
            }
            w8(sub, r8(sub) - 1);
        } else if (r8(sub) != 0x30) {
            w8(sub, r8(sub) + 1);
        }
        SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos));
        o_platform_1p(o, n, 0x20, r8(sub), OB16(p, OBJ_x_pos));
    }
}

/* the level loads the oil (Level: Obj07 at Oil) */
void s2_ooz_frame(void)
{
    uint32_t o = s_oil ? (0xFF0000u | s_oil) : 0;
    if (o && r32(o) == S2(0x07)) return;
    uint32_t a1 = o_alloc();
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x07));
    s_oil = (uint16_t)(a1 & 0xFFFF);
}

void s2_ooz_oil_y(uint16_t y)                        /* LevEvents_OOZ2: the boss arena's oil */
{
    uint32_t o = s_oil ? (0xFF0000u | s_oil) : 0;
    if (o && r32(o) == S2(0x07)) SET16(o, OBJ_y_pos, y);
}

/* ------------------------------------------------------------------------
 * Object 33 - burner platform: pops up on its own (subtype 0) or flings
 * a player standing in the middle (subtype != 0); the flame under it hurts
 * ---------------------------------------------------------------------- */
#define BN_Y0    0x30                 /* objoff_30 */
#define BN_VEL   0x32                 /* objoff_32 (long, 16.16 y speed) */
#define BN_TIME  0x36                 /* objoff_36 (word) */
#define BN_LID   0x3C                 /* flame: objoff_3C, its lid */

static void burner_pop(uint32_t o, uint32_t p)       /* loc_23C26: catch a player */
{
    SET8(p, P_OBJCTRL, 1);
    SET16(p, OBJ_ground_vel, 0);
    SET16(p, OBJ_x_vel, 0);
    SET16(p, OBJ_y_vel, 0);
    SET8(p, OBJ_status, OB8(p, OBJ_status) & ~ST_PUSH);
    SET16(p, OBJ_art_tile, OB16(p, OBJ_art_tile) & 0x7FFF);
    (void)o;
}

static void burner_mode(uint32_t o)
{
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* loc_23BC6: wait, then pop */
        SET16(o, BN_TIME, OB16(o, BN_TIME) - 1);
        if ((int16_t)OB16(o, BN_TIME) >= 0) return;
        SET16(o, BN_TIME, 0x78);
        SET32(o, BN_VEL, (uint32_t)-0x96800);
        SET8(o, OBJ_2nd, 2);
        if (on_screen(o)) o_sfx_s2(SFX_LID_POP);
        return;
    case 2: {                                           /* loc_23BEA: up and bounce down */
        int32_t y = (int32_t)r32(o + OBJ_y_pos) + (int32_t)r32(o + BN_VEL);
        w32(o + OBJ_y_pos, (uint32_t)y);
        SET32(o, BN_VEL, r32(o + BN_VEL) + 0x3800);
        if ((uint16_t)(y >> 16) < OB16(o, BN_Y0)) return;
        uint32_t d0 = r32(o + BN_VEL);
        if (d0 < 0x10000u) SET8(o, OBJ_2nd, 0);
        SET32(o, BN_VEL, (uint32_t)-(int32_t)(d0 >> 2));
        SET16(o, OBJ_y_pos, OB16(o, BN_Y0));
        return;
    }
    case 4: {                                           /* loc_23C26: wait for a rider in the middle */
        uint16_t d2 = (uint16_t)(OB16(o, OBJ_x_pos) - 0x10), d3 = (uint16_t)(OB16(o, OBJ_x_pos) + 0x10);
        uint8_t st = OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND);
        if (!st) return;
        if (st == (ST_P1_STAND | ST_P2_STAND)) {        /* loc_23CA0: both must be in the middle */
            uint16_t x1 = OB16(RAM_Player_1, OBJ_x_pos), x2 = OB16(RAM_Player_2, OBJ_x_pos);
            if (x1 < d2 || x1 >= d3 || x2 < d2 || x2 >= d3) return;
            burner_pop(o, RAM_Player_1);
            burner_pop(o, RAM_Player_2);
        } else {
            int n = (st & ST_P1_STAND) ? 0 : 1;
            uint32_t p = o_player(n);
            uint16_t x = OB16(p, OBJ_x_pos);
            if (x < d2 || x >= d3) return;
            burner_pop(o, p);
        }
        SET32(o, BN_VEL, (uint32_t)-0x96800);
        SET8(o, OBJ_2nd, 6);
        if (on_screen(o)) o_sfx_s2(SFX_LID_POP);
        return;
    }
    case 6: {                                           /* loc_23D20: up, then fling */
        int32_t y = (int32_t)r32(o + OBJ_y_pos) + (int32_t)r32(o + BN_VEL);
        w32(o + OBJ_y_pos, (uint32_t)y);
        SET32(o, BN_VEL, r32(o + BN_VEL) + 0x3800);
        if ((uint16_t)(y >> 16) != (uint16_t)(OB16(o, BN_Y0) - 0x7D)) return;
        SET8(o, OBJ_2nd, 8);
        for (int n = 0; n < 2; n++) {
            if (!(OB8(o, OBJ_status) & (ST_P1_STAND << n))) continue;
            uint32_t p = o_player(n);
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET8(p, OBJ_anim, ANI_ROLL);
            SET16(p, OBJ_ground_vel, 0x800);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
            SET16(p, OBJ_y_vel, (uint16_t)-0x1000);
            SET8(p, P_OBJCTRL, 0);
            if (on_screen(o)) o_sfx_s2(SFX_SPRING);
        }
        return;
    }
    default:
        return;
    }
}

static void burner(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj33_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_BURNERLID, 4, 3, 0x18, 0x10);
        SET16(o, BN_Y0, OB16(o, OBJ_y_pos));
        SET8(o, OBJ_2nd, 2);
        SET16(o, BN_TIME, 0x78);
        if (OB8(o, OBJ_subtype)) SET8(o, OBJ_2nd, 4);
        uint32_t a1 = o_alloc_after(o);
        if (a1) {
            SET32(a1, OBJ_code, S2(0x33));
            SET8(a1, OBJ_routine, 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x10);
            o_init(a1, S2A_BURNER, 4, 4, 0x10, 0x10);
            SET32(a1, BN_LID, RAMPTR(o));
        }
    }   /* fall through */
    case 2: {                                           /* Obj33_Main */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        burner_mode(o);
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, 8, 9, oldx);
        gone(o);
        return;
    }
    default: {                                          /* Obj33_Flame */
        uint32_t lid = r32(o + BN_LID) & 0xFFFFFF;
        if ((int16_t)(OB16(o, OBJ_y_pos) - OB16(lid, OBJ_y_pos)) < 0x14) {
            SET8(o, OBJ_coll_flags, 0);                 /* Obj33_FlameOff */
            SET8(o, OBJ_anim_frame, 0);
            return;
        }
        SET8(o, OBJ_coll_flags, 0x9B);
        o_animate_s2(o, o_asset_anim(S2A_BURNER));
        o_touch(o);
        gone(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 3D - striped block: a rolling player smashes it and is shot off
 * by the invisible launcher it leaves behind
 * ---------------------------------------------------------------------- */
#define SB_ANI1  0x32                 /* objoff_32/33: the players' anims */
#define SB_YV1   0x34                 /* objoff_34/36: their y speeds */
#define SB_YV2   0x36
#define SB_ST1   0x40                 /* launcher: objoff_2C (P1 state) */
#define SB_ST2   0x36                 /* launcher: objoff_36 (P2 state) */

static const int16_t sb_vel[32] = {   /* word_2507A */
    -0x400, -0x400, -0x200, -0x400, 0x200, -0x400, 0x400, -0x400,
    -0x3C0, -0x200, -0x1C0, -0x200, 0x1C0, -0x200, 0x3C0, -0x200,
    -0x380, 0x200, -0x180, 0x200, 0x180, 0x200, 0x380, 0x200,
    -0x340, 0x400, -0x140, 0x400, 0x140, 0x400, 0x340, 0x400,
};

static void sb_fall(uint32_t p, int roll, uint16_t yv)   /* loc_24EB2 / loc_24EB8 */
{
    if (roll) {
        SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_ROLL);
        SET8(p, OBJ_y_radius, 0x0E);
        SET8(p, OBJ_x_radius, 7);
        SET8(p, OBJ_anim, ANI_ROLL);
        SET16(p, OBJ_y_vel, yv);
    }
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
    SET8(p, OBJ_routine, 2);
}

static void sb_launch(uint32_t o, int n)             /* loc_24F74 */
{
    uint32_t p = o_player(n), a4 = o + (n ? SB_ST2 : SB_ST1);
    if (!r8(a4)) {                                      /* loc_24F84: a rolling player arrives */
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x10) >= 0x20) return;
        uint16_t d1 = (uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos));
        if (OB8(o, OBJ_subtype)) d1 = (uint16_t)(d1 + 0x10);
        if (d1 >= 0x10) return;
        if (n && r16(RAM_Tails_CPU_routine) == 4) return;   /* (TailsCPU_Flying) */
        w8(a4, 2);
        SET8(p, P_OBJCTRL, 0x81);
        SET8(p, OBJ_anim, ANI_ROLL);
        SET16(p, OBJ_ground_vel, 0x800);
        if (OB8(o, OBJ_subtype)) {
            SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(p, OBJ_x_vel, 0);
            SET16(p, OBJ_y_vel, (uint16_t)-0x800);
        } else {
            SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(p, OBJ_x_vel, 0x800);
            SET16(p, OBJ_y_vel, 0);
        }
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~ST_PUSH) | ST_INAIR | ST_ONOBJ);
        SET16(p, P_INTERACT, (uint16_t)o);
        o_sfx_s2(SFX_ROLL);
        return;
    }
    if (!on_screen(p)) {                                /* loc_25036 */
        SET8(p, P_OBJCTRL, 0);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
        w8(a4, 0);
        return;
    }
    obj_move_player(p);                                 /* Obj3D_MoveCharacter */
}

static void striped_block(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj3D_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, OB8(o, OBJ_subtype) ? S2A_STRIPEDH : S2A_STRIPEDV, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        if (OB8(o, OBJ_subtype)) SET8(o, OBJ_frame, 2);
        SET8(o, OBJ_status, OB8(o, OBJ_status) | 0x80);
        /* fall through */
    case 2: {                                           /* Obj3D_Main */
        SET8(o, SB_ANI1, OB8(RAM_Player_1, OBJ_anim));
        SET8(o, SB_ANI1 + 1, OB8(RAM_Player_2, OBJ_anim));
        SET16(o, SB_YV1, OB16(RAM_Player_1, OBJ_y_vel));
        SET16(o, SB_YV2, OB16(RAM_Player_2, OBJ_y_vel));
        o_solid(o, 0x1B, 0x10, 0x11, OB16(o, OBJ_x_pos));
        uint8_t st = OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND);
        if (!st) { gone(o); return; }
        if (st == (ST_P1_STAND | ST_P2_STAND)) {
            if (OB8(o, SB_ANI1) != ANI_ROLL && OB8(o, SB_ANI1 + 1) != ANI_ROLL) { gone(o); return; }
            sb_fall(RAM_Player_1, OB8(o, SB_ANI1) == ANI_ROLL, OB16(o, SB_YV1));
            sb_fall(RAM_Player_2, OB8(o, SB_ANI1 + 1) == ANI_ROLL, OB16(o, SB_YV2));
        } else if (st & ST_P1_STAND) {
            if (OB8(o, SB_ANI1) != ANI_ROLL) { gone(o); return; }
            sb_fall(RAM_Player_1, 1, OB16(o, SB_YV1));
        } else {
            if (OB8(o, SB_ANI1 + 1) != ANI_ROLL) { gone(o); return; }
            sb_fall(RAM_Player_2, 1, OB16(o, SB_YV2));
        }
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));   /* loc_24F04 */
        uint32_t a1 = o_alloc_after(o);
        if (a1) {
            for (uint32_t k = 0; k < 0x30; k += 2) w16(a1 + k, r16(o + k));
            for (uint32_t k = 0x30; k < 0x48; k += 2) w16(a1 + k, 0);
            SET16(a1, OBJ_respawn, OB16(o, OBJ_respawn));
            SET8(a1, OBJ_routine, 6);
        }
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
        SET16(o, OBJ_respawn, 0);
        o_smash(o, 16, sb_vel, 0x18, 4);                /* o is the first piece: falls through */
    }   /* fall through */
    case 4:                                             /* Obj3D_Fragment */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        if (!on_screen(o)) { o_delete(o); return; }
        o_draw(o);
        return;
    default:                                            /* Obj3D_InvisibleLauncher */
        sb_launch(o, 0);
        sb_launch(o, 1);
        if (!(OB8(o, SB_ST1) + OB8(o, SB_ST2))) {       /* MarkObjGone3: no drawing */
            if (o_out_of_range(o, OB16(o, OBJ_x_pos))) o_delete_respawnable(o);
        }
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 3F - fan: blows players sideways / upward, switching on and off
 * (subtype bit 1: always on)
 * ---------------------------------------------------------------------- */
#define FN_TIME  0x30                 /* objoff_30 (word) */
#define FN_OFF   0x32                 /* objoff_32: 1 = winding down */
#define FN_SPIN  0x34                 /* objoff_34 (word) */

static void fan_push_h(uint32_t o, uint32_t p)       /* loc_2A894 */
{
    if (OB8(p, OBJ_routine) >= 4 || OB8(p, P_OBJCTRL)) return;
    int16_t d0 = (int16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos));
    if (!(OB8(o, OBJ_status) & 1)) d0 = (int16_t)-d0;
    d0 = (int16_t)(d0 + 0x50);
    if ((uint16_t)d0 >= 0xF0) return;
    uint16_t d1 = (uint16_t)(OB16(p, OBJ_y_pos) + 0x60);
    if (d1 < OB16(o, OBJ_y_pos)) return;
    d1 = (uint16_t)(d1 - OB16(o, OBJ_y_pos));
    if (d1 >= 0x70) return;
    if ((uint16_t)d0 < 0x50) { d0 = (int16_t)(d0 - 0x50); d0 = (int16_t)~d0; d0 = (int16_t)(d0 + d0); }
    else d0 = (int16_t)(d0 - 0x50);
    d0 = (int16_t)(d0 + 0x60);
    if (!(OB8(o, OBJ_status) & 1)) d0 = (int16_t)-d0;
    d0 = (int16_t)((d0 & 0xFF00) | (uint8_t)-(int8_t)(d0 & 0xFF));   /* neg.b */
    d0 = (int16_t)(d0 >> 4);
    if (OB8(o, OBJ_subtype) & 1) d0 = (int16_t)-d0;
    SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + d0);
}

static void fan_push_v(uint32_t o, uint32_t p)       /* loc_2A990 */
{
    if (OB8(p, OBJ_routine) >= 4 || OB8(p, P_OBJCTRL)) return;
    if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x40) >= 0x80) return;
    uint16_t d1 = (uint16_t)(o_osc(0x14 + 2) + OB16(p, OBJ_y_pos) + 0x60);   /* Oscillating_Data+$14 */
    if (d1 < OB16(o, OBJ_y_pos)) return;
    d1 = (uint16_t)(d1 - OB16(o, OBJ_y_pos));
    if (d1 >= 0x90) return;
    int16_t v = (int16_t)d1;
    if (d1 >= 0x60) { v = (int16_t)(d1 - 0x60); v = (int16_t)~v; v = (int16_t)(v + v); }
    else v = (int16_t)(d1 - 0x60);
    v = (int16_t)(v + 0x60);
    v = (int16_t)-v;
    v = (int16_t)(v >> 4);
    SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) + v);
    SET8(p, OBJ_status, OB8(p, OBJ_status) | ST_INAIR);
    SET16(p, OBJ_y_vel, 0);
    SET16(p, OBJ_ground_vel, 1);
    if (OB8(p, P_FLIP_ANGLE)) return;
    SET8(p, P_FLIP_ANGLE, 1);
    SET8(p, OBJ_anim, ANI_WALK);
    SET8(p, P_FLIPS_LEFT, 0x7F);
    SET8(p, P_FLIP_SPEED, 8);
}

static void fan(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj3F_Init */
        int up = (OB8(o, OBJ_subtype) & 0x80) != 0;
        SET8(o, OBJ_routine, up ? 4 : 2);
        o_init(o, up ? S2A_FANV : S2A_FANH, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
    }
    int up = OB8(o, OBJ_routine) == 4;
    if (!(OB8(o, OBJ_subtype) & 2)) {                   /* on/off timer */
        SET16(o, FN_TIME, OB16(o, FN_TIME) - 1);
        if ((int16_t)OB16(o, FN_TIME) < 0) {
            SET16(o, FN_SPIN, 0);
            SET16(o, FN_TIME, 0x78);
            uint8_t was = OB8(o, FN_OFF) & 1;
            SET8(o, FN_OFF, OB8(o, FN_OFF) ^ 1);
            if (was) SET16(o, FN_TIME, 0xB4);
        }
    }
    int step = 0;
    if (OB8(o, FN_OFF)) {                               /* winding down */
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) { gone(o); return; }
        if (OB16(o, FN_SPIN) >= 0x400) { gone(o); return; }
        SET16(o, FN_SPIN, OB16(o, FN_SPIN) + 0x2A);
        SET8(o, OBJ_anim_timer, OB8(o, FN_SPIN));
        step = 1;
    } else {
        for (int n = 0; n < 2; n++) (up ? fan_push_v : fan_push_h)(o, o_player(n));
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) >= 0) { gone(o); return; }
        SET8(o, OBJ_anim_timer, 0);
        step = 1;
    }
    if (step) {
        SET8(o, OBJ_anim_frame, OB8(o, OBJ_anim_frame) + 1);
        if (OB8(o, OBJ_anim_frame) >= 6) SET8(o, OBJ_anim_frame, 0);
        SET8(o, OBJ_frame, ((OB8(o, OBJ_subtype) & 1) ? 5 : 0) + OB8(o, OBJ_anim_frame));
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 43 - spiked thing sliding back and forth (in pairs per subtype)
 * ---------------------------------------------------------------------- */
#define SP_X0    0x30
#define SP_MIN   0x32
#define SP_MAX   0x34
#define SP_DIR   0x36
#define SP_MATE  0x3C

static void spiky_step(uint32_t o)                   /* loc_23F66 */
{
    uint16_t x = OB16(o, OBJ_x_pos);
    if (!OB8(o, SP_DIR)) {
        x = (uint16_t)(x - 1);
        if (x == OB16(o, SP_MIN)) { SET8(o, SP_DIR, 1); if (on_screen(o)) o_sfx_s2(SFX_SPIKE_MOVE); }
    } else {
        x = (uint16_t)(x + 1);
        if (x == OB16(o, SP_MAX)) { SET8(o, SP_DIR, 0); if (on_screen(o)) o_sfx_s2(SFX_SPIKE_MOVE); }
    }
    SET16(o, OBJ_x_pos, x);
}

static void spiky(uint32_t o)
{
    static const struct { uint8_t n; uint8_t origin; int16_t parent_dx, child_dx; } props[3] = {
        { 0, 0x68, 0, 0 }, { 1, (uint8_t)-0x18, -0x18, 0x18 }, { 1, (uint8_t)-0x58, -0x58, -0x28 },
    };
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj43_Init */
        SET8(o, OBJ_routine, 2);
        int idx = (OB8(o, OBJ_subtype) / 6) % 3;
        uint32_t a1 = o;
        for (int k = 0; k <= props[idx].n; k++) {
            if (k) {
                uint32_t c = o_alloc_after(o);
                if (!c) break;
                a1 = c;
                SET32(a1, OBJ_code, S2(0x43));
                SET8(a1, OBJ_routine, 4);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                SET8(a1, SP_DIR, 1);
            }
            o_init(a1, S2A_SPIKYTHING, 4, 4, 0x18, 0x18);
            SET16(a1, OBJ_art_tile, OB16(a1, OBJ_art_tile) | 0x8000);
            SET8(a1, OBJ_coll_flags, 0xA5);
            SET16(a1, SP_X0, OB16(a1, OBJ_x_pos));
        }
        SET32(a1, SP_MATE, RAMPTR(o));
        SET32(o, SP_MATE, RAMPTR(a1));
        uint16_t d0 = (uint16_t)(OB16(o, SP_X0) - (uint8_t)props[idx].origin);
        SET16(o, SP_MIN, d0);
        SET16(a1, SP_MIN, d0);
        d0 = (uint16_t)(d0 + 2 * (uint8_t)props[idx].origin);
        SET16(o, SP_MAX, d0);
        SET16(a1, SP_MAX, d0);
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + props[idx].parent_dx);
        if (a1 != o) SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + props[idx].child_dx);
    }   /* fall through */
    case 2: {                                           /* loc_23F0A */
        spiky_step(o);
        o_touch(o);
        uint16_t c = (uint16_t)((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80);   /* Camera_X_pos_coarse */
        if ((uint16_t)((OB16(o, SP_MIN) & 0xFF80) - c) <= 0x280 ||
            (uint16_t)((OB16(o, SP_MAX) & 0xFF80) - c) <= 0x280) { o_draw(o); return; }
        uint32_t m = r32(o + SP_MATE) & 0xFFFFFF;
        if (m != (o & 0xFFFFFF)) o_delete(m);
        o_delete_respawnable(o);
        return;
    }
    default: {                                          /* loc_23F5C: the second one */
        spiky_step(o);
        uint32_t m = r32(o + SP_MATE) & 0xFFFFFF;       /* loc_23FB0: they bump into each other */
        if ((uint16_t)(OB16(o, OBJ_x_pos) - 0x18) == (uint16_t)(OB16(m, OBJ_x_pos) + 0x18)) {
            SET8(o, SP_DIR, OB8(o, SP_DIR) ^ 1);
            SET8(m, SP_DIR, OB8(m, SP_DIR) ^ 1);
            if (on_screen(o)) o_sfx_s2(SFX_SPIKE_MOVE);
        }
        o_touch(o);
        o_draw(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 45 - pressure spring: vertical (squashed by standing on it) or
 * horizontal (pushed in); subtype bits as Obj41's (flips, plane switch)
 * ---------------------------------------------------------------------- */
#define PS_STR   0x30                 /* obj45_strength */
#define PS_FRAME 0x32                 /* obj45_frame */
#define PS_X0    0x34                 /* obj45_original_x_pos */
#define PS_COMP  0x36                 /* obj45_compress */

static void ps_subtype_flags(uint32_t p, uint8_t d0, int flips_h)
{
    if (d0 & 1) {                                       /* flips */
        SET16(p, OBJ_ground_vel, 1);
        SET8(p, P_FLIP_ANGLE, 1);
        SET8(p, OBJ_anim, ANI_WALK);
        if (flips_h) {
            SET8(p, P_FLIPS_LEFT, 1);
            SET8(p, P_FLIP_SPEED, 8);
            if (!(d0 & 2)) SET8(p, P_FLIPS_LEFT, 3);
        } else {
            SET8(p, P_FLIPS_LEFT, 0);
            SET8(p, P_FLIP_SPEED, 4);
            if (!(d0 & 2)) SET8(p, P_FLIPS_LEFT, 1);
        }
        if (OB8(p, OBJ_status) & 1) {
            SET8(p, P_FLIP_ANGLE, (uint8_t)-(int8_t)OB8(p, P_FLIP_ANGLE));
            SET16(p, OBJ_ground_vel, (uint16_t)-(int16_t)OB16(p, OBJ_ground_vel));
        }
    }
    if ((d0 & 0x0C) == 4) { SET8(p, P_TOP_SOLID, 0x0C); SET8(p, P_LRB_SOLID, 0x0D); }
    if ((d0 & 0x0C) == 8) { SET8(p, P_TOP_SOLID, 0x0E); SET8(p, P_LRB_SOLID, 0x0F); }
}

static void ps_launch_v(uint32_t o, int n)           /* Obj45_LaunchCharacterVertical */
{
    uint8_t bit = (uint8_t)(ST_P1_STAND << n);
    if (!(OB8(o, OBJ_status) & bit)) return;
    SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
    uint32_t p = o_player(n);
    SET16(p, OBJ_y_vel, OB16(o, PS_STR));
    SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
    SET8(p, OBJ_anim, ANI_SPRING);
    SET8(p, OBJ_routine, 2);
    uint8_t d0 = OB8(o, OBJ_subtype);
    if (d0 & 0x80) SET16(p, OBJ_x_vel, 0);
    ps_subtype_flags(p, d0, 0);
    o_sfx_s2(SFX_SPRING);
}

static void ps_push(uint32_t o, uint32_t p, int16_t d0)   /* loc_2433C: pushing it in */
{
    int dx = 0, v = 0;
    if (OB8(o, OBJ_status) & 1) {
        if (OB8(p, OBJ_status) & 1) return;
        if (!d0) {
            if (!OB16(p, OBJ_ground_vel) || (int16_t)OB16(p, OBJ_ground_vel) < 0) return;
            SET8(o, PS_COMP, 1);
            return;
        }
        if ((uint16_t)(OB16(o, PS_X0) + 0x12) == OB16(o, OBJ_x_pos)) { SET8(o, PS_COMP, 1); return; }
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 1);
        dx = 1; v = 0x40;
    } else {
        if (!(OB8(p, OBJ_status) & 1)) return;
        if (!d0) {
            if ((int16_t)OB16(p, OBJ_ground_vel) < 0) SET8(o, PS_COMP, 1);
            return;
        }
        if ((uint16_t)(OB16(o, PS_X0) - 0x12) == OB16(o, OBJ_x_pos)) { SET8(o, PS_COMP, 1); return; }
        SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) - 1);
        dx = -1; v = -0x40;
    }
    SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + dx);        /* loc_243A6 */
    SET16(p, OBJ_ground_vel, (uint16_t)v);
    SET16(p, OBJ_x_vel, 0);
    int16_t d = (int16_t)(OB16(o, PS_X0) - OB16(o, OBJ_x_pos));
    if (d < 0) d = (int16_t)-d;
    SET8(o, OBJ_frame, d + 0x0A);
    SET8(o, PS_COMP, 1);
}

static void ps_launch_h(uint32_t o)                  /* Obj45_LaunchCharacterHorizontal */
{
    if (!(OB8(o, OBJ_status) & (ST_P1_PUSH | ST_P2_PUSH))) return;
    for (int n = 0; n < 2; n++) {
        uint8_t bit = (uint8_t)(ST_P1_PUSH << n);
        if (!(OB8(o, OBJ_status) & bit)) continue;
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
        uint32_t p = o_player(n);
        int16_t d = (int16_t)(OB16(o, PS_X0) - OB16(o, OBJ_x_pos));
        if (d < 0) d = (int16_t)-d;
        int16_t xv = (int16_t)-(int16_t)((d + 0x0A) << 7);
        SET16(p, OBJ_x_vel, (uint16_t)xv);
        SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) - 4);
        SET8(p, OBJ_status, OB8(p, OBJ_status) | 1);
        if (!(OB8(o, OBJ_status) & 1)) {
            SET8(p, OBJ_status, OB8(p, OBJ_status) & ~1);
            SET16(p, OBJ_x_pos, OB16(p, OBJ_x_pos) + 8);
            SET16(p, OBJ_x_vel, (uint16_t)-(int16_t)OB16(p, OBJ_x_vel));
        }
        SET16(p, P_MOVE_LOCK, 0x0F);
        SET16(p, OBJ_ground_vel, OB16(p, OBJ_x_vel));
        if (!(OB8(p, OBJ_status) & ST_ROLL)) SET8(p, OBJ_anim, ANI_WALK);
        uint8_t d0 = OB8(o, OBJ_subtype);
        if (d0 & 0x80) SET16(p, OBJ_y_vel, 0);
        ps_subtype_flags(p, d0, 1);
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~ST_PUSH);
        SET8(p, OBJ_prev_anim, 1);                      /* AniIDSonAni_Run: restart the animation */
        o_sfx_s2(SFX_SPRING);
    }
}

static void push_spring(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj45_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_PUSHSPRING, OB8(o, OBJ_render) | 4, 4, 16, 0x20);
        if ((OB8(o, OBJ_subtype) >> 3) & 2) {           /* Obj45_InitHorizontal (fixBugs mask) */
            SET8(o, OBJ_routine, 4);
            SET8(o, OBJ_anim, 1);
            SET8(o, OBJ_frame, 0x0A);
            SET8(o, OBJ_width_px, 20);
            SET16(o, PS_X0, OB16(o, OBJ_x_pos));
        }
        SET16(o, PS_STR, (OB8(o, OBJ_subtype) & 2) ? (uint16_t)-0xA00 : (uint16_t)-0x1000);
    }
    if (OB8(o, OBJ_routine) == 2) {                     /* Obj45_Vertical */
        if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) {
            if (OB8(o, PS_FRAME) == 9) {
                ps_launch_v(o, 0);
                ps_launch_v(o, 1);
                gone(o);
                return;
            }
            SET8(o, PS_FRAME, OB8(o, PS_FRAME) + 1);
        } else if (OB8(o, PS_FRAME)) {
            SET8(o, PS_FRAME, OB8(o, PS_FRAME) - 1);
        }
        int d3 = OB8(o, PS_FRAME);                      /* SolidObject45: the top sinks 2px a frame */
        SET8(o, OBJ_frame, d3);
        uint16_t y = OB16(o, OBJ_y_pos);
        SET16(o, OBJ_y_pos, y + d3 * 2);
        o_solid(o, 27, 20, 20, OB16(o, OBJ_x_pos));
        SET16(o, OBJ_y_pos, y);
        gone(o);
        return;
    }
    SET8(o, PS_COMP, 0);                                /* Obj45_Horizontal */
    for (int n = 0; n < 2; n++) {
        uint32_t p = o_player(n);
        int16_t d4 = o_solid_1p(o, n, 31, 12, 13, OB16(o, OBJ_x_pos), 1);
        int16_t d0 = (int16_t)D(0);
        if (d4 != 1) continue;
        uint8_t d1 = OB8(o, OBJ_status);
        if ((int16_t)(OB16(o, OBJ_x_pos) - OB16(p, OBJ_x_pos)) >= 0) d1 ^= 1;
        if (d1 & 1) continue;
        ps_push(o, p, d0);
    }
    if (!OB8(o, PS_COMP)) {
        uint16_t x0 = OB16(o, PS_X0), x = OB16(o, OBJ_x_pos);
        if (x0 != x) {
            if (x0 < x) {
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 4);
                SET16(o, OBJ_x_pos, x - 4);
                if (x0 >= OB16(o, OBJ_x_pos)) { SET8(o, OBJ_frame, 0x0A); SET16(o, OBJ_x_pos, x0); }
            } else {
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 4);
                SET16(o, OBJ_x_pos, x + 4);
                if (x0 < OB16(o, OBJ_x_pos)) { SET8(o, OBJ_frame, 0x0A); SET16(o, OBJ_x_pos, x0); }
            }
            ps_launch_h(o);
        }
    }
    gone(o);
}

/* ------------------------------------------------------------------------
 * Object 48 - launch ball: catches a player, turns, fires him on
 * ---------------------------------------------------------------------- */
#define LB_ST1   0x40                 /* objoff_2C: P1 state */
#define LB_ST2   0x36                 /* objoff_36: P2 state */
#define LB_DUR   0x44                 /* anim_frame_duration (word) */
#define LB_COOL  0x3C                 /* objoff_3C (word) */
#define LB_OPEN  0x3E                 /* objoff_3E: turns towards frame 7 */
#define LB_FRAME 0x3F                 /* objoff_3F: start frame */

static void lb_mode(uint32_t o, int n)
{
    uint32_t p = o_player(n), a4 = o + (n ? LB_ST2 : LB_ST1);
    switch (r8(a4)) {
    case 0: {                                           /* loc_252F0: catch */
        if (r16(RAM_Debug_placement_mode)) return;
        if ((uint16_t)(OB16(p, OBJ_x_pos) - OB16(o, OBJ_x_pos) + 0x10) >= 0x20) return;
        if ((uint16_t)(OB16(p, OBJ_y_pos) - OB16(o, OBJ_y_pos) + 0x10) >= 0x20) return;
        if (n && r16(RAM_Tails_CPU_routine) == 4) return;   /* (TailsCPU_Flying) */
        if (OB8(p, OBJ_routine) >= 6) return;
        if (OB8(p, OBJ_status) & ST_ONOBJ) {            /* leave the ball he was in */
            uint32_t a3 = 0xFF0000u | OB16(p, P_INTERACT);
            if (r32(a3) == S2(0x48)) w8(a3 + (n ? LB_ST2 : LB_ST1), 0);
        }
        SET16(p, P_INTERACT, (uint16_t)o);
        w8(a4, 2);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_y_pos, OB16(o, OBJ_y_pos));
        SET8(p, OBJ_routine, 2);                        /* (fixBugs) */
        SET8(p, P_OBJCTRL, 0x81);
        SET8(p, OBJ_anim, ANI_ROLL);
        SET16(p, OBJ_ground_vel, 0x1000);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_P1_PUSH);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~ST_PUSH) | ST_INAIR | ST_ONOBJ);
        SET8(o, OBJ_frame, OB8(o, LB_FRAME));
        o_sfx_s2(SFX_ROLL);
        return;
    }
    case 2: {                                           /* loc_253C6: turn */
        int fire = 0;
        if (!OB8(o, LB_OPEN)) {
            if (OB8(o, OBJ_frame) == 7) fire = 1;
            else {
                SET16(o, LB_DUR, OB16(o, LB_DUR) - 1);
                if ((int16_t)OB16(o, LB_DUR) >= 0) return;
                SET16(o, LB_DUR, 7);
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
                if (OB8(o, OBJ_frame) == 7) fire = 1;
            }
        } else {
            if (!OB8(o, OBJ_frame)) fire = 1;
            else {
                SET16(o, LB_DUR, OB16(o, LB_DUR) - 1);
                if ((int16_t)OB16(o, LB_DUR) >= 0) return;
                SET16(o, LB_DUR, 7);
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 1);
                if (!OB8(o, OBJ_frame)) fire = 1;
            }
        }
        if (!fire) return;
        static const int16_t dirs[8] = { 0, -0x1000, 0x1000, 0, 0, 0x1000, -0x1000, 0 };   /* word_25464 */
        w8(a4, 4);                                      /* loc_25408 */
        uint8_t d0 = (uint8_t)(OB8(o, OBJ_subtype) + 1);
        if (OB8(o, OBJ_status) & 1) d0 = (uint8_t)(d0 - 2);
        d0 &= 3;
        SET16(p, OBJ_x_vel, dirs[d0 * 2]);
        SET16(p, OBJ_y_vel, dirs[d0 * 2 + 1]);
        SET16(o, LB_DUR, 3);
        if (!(OB8(o, OBJ_subtype) & 0x80)) return;
        SET8(p, P_OBJCTRL, 0);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
        SET8(p, P_JUMPING, 0);
        SET8(p, OBJ_routine, 2);
        w8(a4, 6);
        SET16(o, LB_COOL, 7);
        return;
    }
    case 4:                                             /* loc_25474: carry him off */
        if (!on_screen(p)) {
            SET8(p, P_OBJCTRL, 0);
            SET8(p, OBJ_status, (OB8(p, OBJ_status) | ST_INAIR) & ~ST_ONOBJ);
            w8(a4, 0);
            return;
        }
        if (OB8(o, LB_ST1) != 2 && OB8(o, LB_ST2) != 2) {
            SET16(o, LB_DUR, OB16(o, LB_DUR) - 1);
            if ((int16_t)OB16(o, LB_DUR) < 0) {
                SET16(o, LB_DUR, 1);
                if (OB8(o, LB_OPEN)) { if (OB8(o, OBJ_frame) != 7) SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1); }
                else if (OB8(o, OBJ_frame)) SET8(o, OBJ_frame, OB8(o, OBJ_frame) - 1);
            }
        }
        obj_move_player(p);
        return;
    default:                                            /* loc_254F2 */
        SET16(o, LB_COOL, OB16(o, LB_COOL) - 1);
        if ((int16_t)OB16(o, LB_COOL) < 0) w8(a4, 0);
        return;
    }
}

static void launch_ball(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj48_Init */
        static const uint8_t props[16] = { 0, 0, 2, 7, 3, 0, 1, 7, 1, 0, 0, 7, 2, 0, 3, 7 };   /* flips, frame */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_LAUNCHBALL, 4, 1, 0x28, 0x28);
        int d0 = OB8(o, OBJ_subtype) & 0x0F;
        if (OB8(o, OBJ_status) & 1) d0 += 4;
        d0 &= 7;
        SET8(o, OBJ_render, 4 | (props[d0 * 2] & 1) | (props[d0 * 2] & 2));
        SET8(o, LB_FRAME, props[d0 * 2 + 1]);
        if (props[d0 * 2 + 1]) SET8(o, LB_OPEN, 1);
        SET8(o, OBJ_frame, OB8(o, LB_FRAME));
    }
    lb_mode(o, 0);
    lb_mode(o, 1);
    if (!(OB8(o, LB_ST1) + OB8(o, LB_ST2))) { gone(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 4A - Octus: hops up and spits at Sonic
 * ---------------------------------------------------------------------- */
#define OC_Y0    0x3A                 /* octus_start_position (objoff_2A) */
#define OC_TIME  0x3C                 /* objoff_2C (word) */

static void octus(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj4A_Init: drop onto the floor */
        o_init(o, S2A_OCTUS, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0x0A);
        SET8(o, OBJ_y_radius, 0x0B);
        SET8(o, OBJ_x_radius, 8);
        o_fall(o);
        {
            int16_t d1 = o_floor_dist(o);
            if (d1 < 0) {
                SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
                SET16(o, OBJ_y_vel, 0);
                SET8(o, OBJ_routine, 2);
                if ((int16_t)(OB16(o, OBJ_x_pos) - OB16(RAM_Player_1, OBJ_x_pos)) < 0)
                    SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
            }
        }
        SET16(o, OC_Y0, OB16(o, OBJ_y_pos));
        o_touch(o);
        return;
    case 2:                                             /* Obj4A_Main */
        switch (OB8(o, OBJ_2nd)) {
        case 0: {                                       /* Obj4A_WaitForCharacter */
            int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(RAM_Player_1, OBJ_x_pos));
            if (d0 > 0x80 || d0 < -0x80) break;
            SET8(o, OBJ_2nd, 2);
            SET8(o, OBJ_anim, 3);
            SET16(o, OC_TIME, 0x20);
            break;
        }
        case 2:                                         /* Obj4A_DelayBeforeMoveUp */
            SET16(o, OC_TIME, OB16(o, OC_TIME) - 1);
            if ((int16_t)OB16(o, OC_TIME) >= 0) break;
            SET8(o, OBJ_2nd, 4);
            SET8(o, OBJ_anim, 4);
            SET16(o, OBJ_y_vel, (uint16_t)-0x200);
            o_move(o);
            break;
        case 4:                                         /* Obj4A_MoveUp */
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x10);
            if ((int16_t)OB16(o, OBJ_y_vel) < 0) { o_move(o); break; }
            SET8(o, OBJ_2nd, 6);
            SET16(o, OC_TIME, 60);
            {                                           /* Obj4A_FireBullet */
                uint32_t a1 = o_alloc();
                if (a1) {
                    SET32(a1, OBJ_code, S2(0x4A));
                    SET8(a1, OBJ_routine, 6);
                    o_init(a1, S2A_OCTUS, OB8(o, OBJ_render), 4, 0x10, 0x10);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                    SET16(a1, OC_TIME, 0x0F);
                    SET8(a1, OBJ_status, OB8(o, OBJ_status));
                    SET8(a1, OBJ_anim, 2);
                    SET8(a1, OBJ_coll_flags, 0x98);
                    SET16(a1, OBJ_x_vel, (OB8(a1, OBJ_render) & 1) ? 0x200 : (uint16_t)-0x200);
                }
            }
            break;
        case 6:                                         /* Obj4A_Hover */
            SET16(o, OC_TIME, OB16(o, OC_TIME) - 1);
            if ((int16_t)OB16(o, OC_TIME) < 0) SET8(o, OBJ_2nd, 8);
            break;
        default:                                        /* Obj4A_MoveDown */
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x10);
            if (OB16(o, OBJ_y_pos) < OB16(o, OC_Y0)) { o_move(o); break; }
            SET8(o, OBJ_2nd, 0);
            SET8(o, OBJ_anim, 0);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_frame, 1);
            break;
        }
        o_animate_s2(o, o_asset_anim(S2A_OCTUS));
        o_touch(o);
        o_remember(o);
        return;
    default:                                            /* Obj4A_Bullet */
        SET16(o, OC_TIME, OB16(o, OC_TIME) - 1);
        if ((int16_t)OB16(o, OC_TIME) >= 0) return;
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_OCTUS));
        o_touch(o);
        gone(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 50 - Aquis: seahorse badnik, chases, shoots three times, leaves
 * ---------------------------------------------------------------------- */
#define AQ_SHOOT 0x3E                 /* Obj50_shooting_flag (objoff_2D) */
#define AQ_SHOTS 0x40                 /* Obj50_shots_remaining (objoff_2E, high byte) */
#define AQ_LINK  0x36                 /* Obj50_child / Obj50_parent */
#define AQ_WATER 0x3A                 /* Obj50_waterlevel */
#define AQ_TIME  0x3C                 /* Obj50_timer */

static void aq_cap(uint32_t o, uint16_t d0, uint16_t d1)   /* Obj_CapSpeed */
{
    uint16_t d2 = OB16(o, OBJ_x_vel), d3 = OB16(o, OBJ_y_vel);
    if ((int16_t)d2 < 0) { uint16_t m = (uint16_t)-d0; if (d2 < m) d2 = m; }
    else if (d2 > d0) d2 = d0;
    if ((int16_t)d3 < 0) { uint16_t m = (uint16_t)-d1; if (d3 < m) d3 = m; }
    else if (d3 > d1) d3 = d1;
    SET16(o, OBJ_x_vel, d2);
    SET16(o, OBJ_y_vel, d3);
}

static void aq_wing(uint32_t o)                      /* Obj50_ControlWing */
{
    uint32_t a1 = r32(o + AQ_LINK) & 0xFFFFFF;
    if (!a1) return;
    int16_t d0 = 0x0A;
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET8(a1, OBJ_status, OB8(o, OBJ_status));
    SET8(a1, OBJ_render, OB8(o, OBJ_render));
    if (OB8(a1, OBJ_status) & 1) d0 = (int16_t)-d0;
    SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + d0);
    SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) - 6);
}

static void aquis(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj50_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_AQUIS, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 0x0A);
        SET16(o, OBJ_x_vel, (uint16_t)-0x100);
        SET16(o, AQ_WATER, r16(RAM_Water_level));
        SET8(o, AQ_SHOTS, 3);
        uint32_t a1 = o_alloc();
        if (a1) {
            SET32(a1, OBJ_code, S2(0x50));
            SET8(a1, OBJ_routine, 4);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + 0x0A);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 6);
            o_init(a1, S2A_AQUIS, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
            SET8(a1, OBJ_status, OB8(o, OBJ_status));
            SET8(a1, OBJ_anim, 3);
            SET32(o, AQ_LINK, RAMPTR(a1));
            SET32(a1, AQ_LINK, RAMPTR(o));
            SET8(o, OBJ_status, OB8(o, OBJ_status) | ST_P2_PUSH);
        }
    }   /* fall through */
    case 2:                                             /* Obj50_Main */
        o_animate_s2(o, o_asset_anim(S2A_AQUIS));
        switch (OB8(o, OBJ_2nd)) {
        case 0:                                         /* Obj50_CheckIfOnScreen */
            if (on_screen(o)) SET8(o, OBJ_2nd, 2);
            break;
        case 2: {                                       /* Obj50_FollowPlayer */
            SET8(o, AQ_TIME, OB8(o, AQ_TIME) - 1);
            if ((int8_t)OB8(o, AQ_TIME) < 0) {          /* Obj50_DoneFollowing */
                SET8(o, OBJ_2nd, 4);
                SET8(o, AQ_TIME, 0x20);
                SET16(o, OBJ_x_vel, 0);
                SET16(o, OBJ_y_vel, 0);
                break;
            }
            int d0, d1;
            s2_orient(o, &d0, &d1, NULL, NULL);
            SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | (d0 ? 1 : 0));
            SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + (d0 ? 0x10 : -0x10));
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + (d1 ? 0x10 : -0x10));
            aq_cap(o, 0x100, 0x100);
            o_move(o);
            break;
        }
        case 4:                                         /* Obj50_Shooting */
            SET8(o, AQ_TIME, OB8(o, AQ_TIME) - 1);      /* Obj50_WaitForNextShot */
            if ((int8_t)OB8(o, AQ_TIME) < 0) {
                SET8(o, AQ_SHOTS, OB8(o, AQ_SHOTS) - 1);
                if ((int8_t)OB8(o, AQ_SHOTS) < 0) {     /* Obj50_GoAway */
                    SET8(o, OBJ_2nd, 6);
                    SET16(o, OBJ_x_vel, (uint16_t)-0x200);
                    SET16(o, OBJ_y_vel, 0);
                } else {
                    SET8(o, OBJ_2nd, 2);
                    SET16(o, OBJ_y_vel, (uint16_t)-0x100);
                    SET8(o, AQ_TIME, 0x80);
                    SET8(o, AQ_SHOOT, 0);
                }
            }
            if (!OB8(o, AQ_SHOOT)) {                    /* Obj50_ChkIfShoot */
                SET8(o, AQ_SHOOT, 0xFF);
                int d1;
                s2_orient(o, NULL, &d1, NULL, NULL);
                if (!d1) break;
                uint32_t a1 = o_alloc();
                if (!a1) break;
                SET32(a1, OBJ_code, S2(0x50));
                SET8(a1, OBJ_routine, 6);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x0A);
                o_init(a1, S2A_AQUIS, OB8(a1, OBJ_render) | 4, 3, 0x10, 0x10);
                SET8(a1, OBJ_coll_flags, 0x98);
                SET8(a1, OBJ_anim, 2);
                int flip = OB8(o, OBJ_status) & 1;
                SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) - (flip ? -0x10 : 0x10));
                SET16(a1, OBJ_x_vel, flip ? 0x300 : (uint16_t)-0x300);
                SET16(a1, OBJ_y_vel, 0x200);
            }
            break;
        default:                                        /* fleeing */
            o_move(o);
            break;
        }
        aq_wing(o);
        o_touch(o);
        o_remember(o);
        return;
    case 4: {                                           /* Obj50_Wing */
        uint32_t a1 = r32(o + AQ_LINK) & 0xFFFFFF;
        if (r32(a1) != S2(0x50) || (OB8(a1, OBJ_status) & 0x80)) { o_delete(o); return; }
        o_animate_s2(o, o_asset_anim(S2A_AQUIS));
        o_draw(o);
        return;
    }
    default:                                            /* Obj50_Bullet */
        o_move(o);
        o_animate_s2(o, o_asset_anim(S2A_AQUIS));
        o_touch(o);
        gone(o);
        return;
    }
}

void s2_obj_ooz_register(TriObjFn *t)
{
    t[0x07] = oil;
    t[0x33] = burner;
    t[0x3D] = striped_block;
    t[0x3F] = fan;
    t[0x43] = spiky;
    t[0x45] = push_spring;
    t[0x48] = launch_ball;
    t[0x4A] = octus;
    t[0x50] = aquis;
}
