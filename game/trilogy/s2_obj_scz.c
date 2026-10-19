/*
 * s2_obj_scz.c — Sonic 2 Sky Chase objects, transcribed from s2disasm: the
 * Tornado (B2: the SCZ ride, WFZ's opening and finale), clouds (B3),
 * propellers (B4/B5), Balkiry (AC), Nebula (99), Turtloid with its rider
 * and jets (9A/9B/9C).
 *
 * In SCZ the camera follows the Tornado's velocity (SwScrl_SCZ), not the
 * player: trilogy_screen.c moves it in the zone's resize hook with S3K's
 * Scroll_lock set. The Tornado's pilot is drawn from six tiles of Tails'
 * (or, Tails alone, Sonic's) frames, which Animate_Pilot streams in like
 * the DPLC S2 used (S2I_PILOT, TRI_BLOB_S2_PILOT).
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"
#include "genesis_machine.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define ST_ONOBJ        0x08

int16_t s2_tornado_vx, s2_tornado_vy;   /* Tornado_Velocity_X / _Y */

static int on_screen(uint32_t o) { return (OB8(o, OBJ_render) & 0x80) != 0; }
static uint32_t word_ptr(uint16_t w) { return 0xFF0000u | w; }

static void tornado_drift(uint32_t o)                /* loc_36776 */
{
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + s2_tornado_vx);
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + s2_tornado_vy);
}

static void delete_behind(uint32_t o)                /* Obj_DeleteBehindScreen */
{
    if ((int16_t)((OB16(o, OBJ_x_pos) & 0xFF80) - ((r16(RAM_Camera_X_pos) - 0x80) & 0xFF80)) < 0) { o_delete(o); return; }   /* (Camera_X_pos_coarse) */
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object B2 - the Tornado. SCZ: Sonic rides it; it rises and dips when he
 * lands / jumps off, Up/Down steer it, it chases him when he drifts off.
 * ---------------------------------------------------------------------- */
#define TN_FLAG  0x3E                 /* objoff_2E: standing changed this frame */
#define TN_DIP   0x3F                 /* objoff_2F */
#define TN_DIP2  0x30                 /* objoff_30 */
#define TN_TIME  0x31                 /* objoff_31 */
#define TN_PIL   0x36                 /* objoff_36: pilot frame index */
#define TN_PILT  0x37                 /* objoff_37: pilot timer */
#define TN_CHASE 0x38                 /* objoff_38 (word) */

static const uint8_t pilot_tails[24] = {             /* Tails_pilot_frames (as blob frame numbers) */
    0, 0, 0, 0, 1, 2, 3, 2, 1, 1, 0, 0, 0, 0, 1, 2, 3, 2, 1, 1, 4, 4, 1, 1,
};

static void tornado_pilot(uint32_t o)                /* ObjB2_Animate_Pilot */
{
    SET8(o, TN_PILT, OB8(o, TN_PILT) - 1);
    if ((int8_t)OB8(o, TN_PILT) >= 0) return;
    SET8(o, TN_PILT, 8);
    int sonic = r16(RAM_Player_mode) == 2;
    int n = sonic ? 4 : 24;
    int k = OB8(o, TN_PIL) + 1;
    if (k >= n) k = 0;
    SET8(o, TN_PIL, k);
    uint32_t inf = tri_rd32(tri_dir() + TD_EXTRA);
    uint16_t dst = inf ? tri_rd16(inf + S2I_PILOT + (sonic ? 2u : 0u)) : 0;
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t src = t ? tri_rd32(t + TRI_BLOB_S2_PILOT * 4u) : 0;
    if (!dst || !src) return;
    int f = sonic ? 5 + k : pilot_tails[k];
    src += (uint32_t)f * 6u * 32u;
    tri_vram_queue(src, (uint32_t)dst * 32u, 6u * 32u);     /* (with the sprite table: no torn frame) */
}

static void tornado_vlimit(uint32_t o)               /* ObjB2_Vertical_limit */
{
    int16_t v = (int16_t)OB16(o, OBJ_y_vel);
    if (!v) return;
    uint16_t cy = r16(RAM_Camera_Y_pos), y = OB16(o, OBJ_y_pos);
    if (v < 0) { if (y < (uint16_t)(cy + 0x34)) SET16(o, OBJ_y_vel, 0); }
    else if (y >= (uint16_t)(cy + 0xA8)) SET16(o, OBJ_y_vel, 0);
}

static void tornado_dip(uint32_t o, uint32_t flag, int time, int with_move)   /* ObjB2_Move_vert / _vert2 */
{
    if (!OB8(o, flag)) {
        if (!OB8(o, TN_FLAG)) return;
        SET8(o, flag, 0xFF);
        SET8(o, flag == TN_DIP ? TN_DIP2 : TN_DIP, 0);
        SET16(o, OBJ_y_vel, 0x200);
        SET8(o, TN_TIME, (uint8_t)time);
    }
    SET8(o, TN_TIME, OB8(o, TN_TIME) - 1);
    if ((int8_t)OB8(o, TN_TIME) < 0) {
        SET8(o, flag, 0);
        SET16(o, OBJ_y_vel, 0);
        return;
    }
    int16_t d0 = (int16_t)OB16(o, OBJ_y_vel);
    if (d0 > -0x100) d0 = (int16_t)(d0 - 0x20);
    SET16(o, OBJ_y_vel, (uint16_t)d0);
    if (with_move) { tornado_vlimit(o); o_move(o); }
}

static void tornado_follow(uint32_t o)               /* ObjB2_Move_with_player */
{
    uint32_t p = RAM_Player_1;
    if (OB8(p, OBJ_status) & ST_ONOBJ) {
        tornado_dip(o, TN_DIP, 0x14, 0);
        tornado_vlimit(o);
        o_move(o);
        tornado_drift(o);
        return;
    }
    if (OB8(o, TN_FLAG)) {                              /* ObjB2_Move_below_player */
        int16_t d2;
        s2_orient(o, NULL, NULL, &d2, NULL);
        SET16(o, TN_CHASE, (uint16_t)d2);
    }
    int16_t d3 = (int16_t)OB16(o, TN_CHASE);
    if (d3) {
        d3 = (int16_t)(d3 + (d3 > 0 ? -1 : 1));
        SET16(o, TN_CHASE, (uint16_t)d3);
    }
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos) + d3);
    tornado_drift(o);
}

static void tornado_steer(uint32_t o)                /* ObjB2_Move_obbey_player */
{
    uint32_t p = RAM_Player_1;
    if (!(OB8(p, OBJ_status) & ST_ONOBJ)) {
        tornado_dip(o, TN_DIP2, 0x2B, 1);
        return;
    }
    if (!OB8(o, TN_DIP)) {
        SET16(o, OBJ_y_vel, 0);
        uint16_t d2 = r16(RAM_Ctrl_1);
        if (d2 & 0x0300) {
            SET16(o, OBJ_y_vel, (d2 & 0x0200) ? 0x80 : (uint16_t)-0x80);
            tornado_vlimit(o);
            o_move(o);
        }
    }
    int d0;
    int16_t d2;
    s2_orient(o, &d0, NULL, &d2, NULL);
    if ((uint16_t)(d2 + 0x10) < 0x20) return;
    int16_t gv = (int16_t)OB16(p, OBJ_ground_vel);
    if ((gv < 0 ? -gv : gv) >= 0x900) return;
    SET16(o, OBJ_x_pos, OB16(p, OBJ_x_pos) + (d0 ? -0x10 : 0x10));
}

static void tornado_scz(uint32_t o)                  /* ObjB2_Main_SCZ */
{
    tornado_pilot(o);
    if (!r16(RAM_Debug_placement_mode)) {
        uint32_t p = RAM_Player_1;
        SET16(o, OBJ_art_tile, (OB16(o, OBJ_art_tile) & 0x7FFF) | (OB16(p, OBJ_art_tile) & 0x8000));
        uint16_t oldx = OB16(o, OBJ_x_pos);
        tornado_follow(o);
        uint8_t before = OB8(o, OBJ_status);
        SET8(o, TN_FLAG, before);                       /* (objoff_2E = status: what Move_vert2 tests) */
        o_solid(o, 0x1B, 8, 9, oldx);
        tornado_steer(o);
        SET8(o, TN_FLAG, (uint8_t)((before ^ OB8(o, OBJ_status)) & ST_P1_STAND));
        uint16_t px = OB16(p, OBJ_x_pos), cx = r16(RAM_Camera_X_pos);
        w16(RAM_Camera_min_X_pos, cx);
        if (px <= (uint16_t)(cx + 0x11)) { px = (uint16_t)(px + 1); SET16(p, OBJ_x_pos, px); }
        if (cx >= 0x1400) {
            if (px >= 0x1568) {                         /* ObjB2_SCZ_Finished: on to Wing Fortress */
                w16(RAM_Current_zone_and_act, (uint16_t)(TRI_S2_WFZ << 8));
                w16(RAM_Apparent_zone_and_act, tri_apparent((uint16_t)(TRI_S2_WFZ << 8)));
                w8(RAM_Last_star_post_hit, 0);
                w16(RAM_Restart_level_flag, 1);
            } else {
                w8(RAM_Ctrl_1_locked, 0xFF);
                w16(RAM_Ctrl_1_held_logical, 0x0808);   /* right held + pressed */
                /* S2's level bound is max + $168 outside boss fights, S3K's max + $128;
                 * Scroll_lock keeps the camera itself put */
                w16(RAM_Camera_max_X_pos, (uint16_t)(cx + 0x40));
            }
        } else {
            w16(RAM_Camera_max_X_pos, (uint16_t)(cx - 0x40));
        }
    }
    o_animate_s2(o, o_asset_anim(S2A_TORNADO));
    o_draw(o);
}

/* ---- WFZ: the opening (shot down by the fortress) and the finale (after
 * the getaway ship, then on to the Death Egg) ---------------------------- */
#define TN_T2A    0x40                /* objoff_2A (word) */
#define TN_T2E    0x3E                /* objoff_2E (word; TN_FLAG in SCZ) */
#define TN_T32    0x32                /* objoff_32 (word) */
#define TN_T34    0x34                /* objoff_34 (word) */
#define TN_THR    0x44                /* objoff_3A: the thruster */
#define TN_PARENT 0x42                /* children: objoff_2C */
#define ANI_WAIT  0x05
#define ANI_HANG  0x11
#define SFX_SCATTER 0xEB          /* SndID_LaserFloor/Scatter */
#define RAM_Camera_Y_bias      0xFFEE2Cu   /* Distance_from_top */
#define RAM_WindTunnel_holding 0xFFF7C8u
#define RAM_Camera_X_coarse    0xFFF76Eu

extern int16_t s2_wfz_bg_xoff;
void s2_wfz_smoke(uint32_t o);
void s2_layout_put(int cx, int cy, int bg, uint8_t chunk);

static void tornado_align(uint32_t o)                /* ObjB2_Align_plane */
{
    uint16_t oldx = OB16(o, OBJ_x_pos);
    o_move(o);
    tornado_drift(o);
    o_solid(o, 0x1B, 8, 9, oldx);
}

static void tornado_hlimit(uint32_t o)               /* ObjB2_Horizontal_limit */
{
    int d0;
    int16_t d2;
    s2_orient(o, &d0, NULL, &d2, NULL);
    if ((uint16_t)(d2 + 0x10) < 0x20) return;
    SET16(s2_orient_player, OBJ_x_pos, OB16(o, OBJ_x_pos) - (d0 ? -0x10 : 0x10));
}

static void tornado_drop_riders(uint32_t o)          /* loc_3B7BC */
{
    for (int n = 0; n < 2; n++) {
        uint8_t bit = n ? 0x10 : 0x08;
        if (!(OB8(o, OBJ_status) & bit)) continue;
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
        uint32_t p = o_player(n);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) & ~ST_ONOBJ) | 0x02);
    }
}

static void tornado_wfz_start(uint32_t o)            /* ObjB2_Main_WFZ_Start */
{
    tornado_pilot(o);
    switch (OB8(o, OBJ_2nd)) {
    case 0:
        SET8(o, OBJ_2nd, 2);
        SET16(o, TN_T32, 0xC0);
        SET16(o, OBJ_x_vel, 0x100);
        break;
    case 2:
        SET16(o, TN_T32, OB16(o, TN_T32) - 1);
        if ((int16_t)OB16(o, TN_T32) >= 0) {
            tornado_align(o);
            tornado_hlimit(o);
            break;
        }
        SET8(o, OBJ_2nd, 4);
        SET16(o, TN_T2A, 0x60);
        SET16(o, TN_T32, 1);
        SET16(o, OBJ_x_vel, 0x100);
        SET16(o, OBJ_y_vel, 0x100);
        break;
    default:
        if (OB8(o, OBJ_2nd) == 4) {                     /* _shot_down */
            if (!(r8(RAM_Level_frame_counter + 1) & 0x1F)) o_sfx_s2(SFX_SCATTER);
            SET16(o, TN_T2A, OB16(o, TN_T2A) - 1);
            if ((int16_t)OB16(o, TN_T2A) < 0) {
                SET8(o, OBJ_2nd, 6);
                tornado_drop_riders(o);
                break;
            }
        } else {
            o_move(o);                                  /* _fall_down */
        }
        tornado_align(o);
        SET16(o, TN_T32, OB16(o, TN_T32) - 1);
        if (!OB16(o, TN_T32)) {
            SET16(o, TN_T32, 0x0E);
            s2_wfz_smoke(o);
        }
        break;
    }
    o_animate_s2(o, o_asset_anim(S2A_TORNADO));
    if ((uint16_t)((OB16(o, OBJ_x_pos) & 0xFF80) - r16(RAM_Camera_X_coarse)) > 0x280) { o_delete(o); return; }
    o_draw(o);                                          /* Obj_DeleteOffScreen */
}

static uint32_t tornado_child(uint32_t o, uint8_t sub, int16_t x, int16_t y)   /* LoadChildObject */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    SET32(a1, OBJ_code, S2(0xB2));
    SET8(a1, OBJ_subtype, sub);
    SET16(a1, TN_PARENT, (uint16_t)o);
    SET16(a1, OBJ_x_pos, (uint16_t)x);
    SET16(a1, OBJ_y_pos, (uint16_t)y);
    return a1;
}

static void tornado_wait_anim(void)                  /* ObjB2_Waiting_animation */
{
    uint32_t p = RAM_Player_1;
    SET8(p, OBJ_frame, 1);
    SET8(p, OBJ_anim_frame, 0);
    SET8(p, OBJ_anim, ANI_WAIT);
    SET8(p, 0x21, ANI_WAIT);
    SET8(p, OBJ_anim_timer, 1);
}

static void tornado_wfz_end(uint32_t o)              /* ObjB2_Main_WFZ_End */
{
    static const uint16_t when[10] = { 0x1E0, 0x260, 0x2A0, 0x2C0, 0x300, 0x3A0, 0x3F0, 0x460, 0x4A0, 0x580 };
    static const int8_t vel[22] = { -1, -1, 1, 0, 0, 1, 1, -1, 1, 1, 1, -1, -1, 1, -1, -1, -1, 1, -2, 0, 0, 0 };
    uint32_t p = RAM_Player_1;
    int show = 0;
    tornado_pilot(o);
    switch (OB8(o, OBJ_2nd)) {
    case 0:                                             /* _Wait_Leader_position */
        if (OB16(p, OBJ_y_pos) < 0x5EC) break;
        w16(RAM_Ctrl_1_held_logical, 0);
        SET16(o, TN_T2E, OB16(o, TN_T2E) + 1);
        if (OB16(o, TN_T2E) < 0x40) break;
        SET8(o, OBJ_2nd, 2);
        SET16(o, OBJ_x_pos, 0x2E58);
        SET16(o, OBJ_y_pos, 0x66C);
        tornado_wait_anim();
        tornado_child(o, 0x56, 0x3118, 0x3F0);          /* the grabber on the ship */
        tornado_child(o, 0x58, 0x3070, 0x3B0);          /* the ship's flames */
        tornado_child(o, 0x58, 0x3070, 0x430);
        SET16(o, TN_THR, (uint16_t)tornado_child(o, 0x5C, 0, 0));
        break;
    case 2:                                             /* _Move_Leader_edge */
        if (OB16(p, OBJ_x_pos) < 0x2E30) { w16(RAM_Ctrl_1_held_logical, 0x0808); break; }
        SET8(o, OBJ_2nd, 4);
        w16(RAM_Ctrl_1_held_logical, 0);
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_ground_vel, 0);
        tornado_wait_anim();
        break;
    case 4:                                             /* _Wait_for_plane */
        if ((uint16_t)s2_wfz_bg_xoff < 0x380) { w16(RAM_Ctrl_1_held_logical, 0); tornado_wait_anim(); break; }
        SET8(o, OBJ_2nd, 6);
        SET16(o, OBJ_x_vel, 0x100);
        SET16(o, OBJ_y_vel, (uint16_t)-0x100);
        SET16(o, TN_T2A, 0);
        tornado_wait_anim();
        break;
    case 6:                                             /* _Prepare_to_jump */
        tornado_wait_anim();
        SET16(o, TN_T2A, OB16(o, TN_T2A) + 1);
        if (OB16(o, TN_T2A) == 0x30) {
            SET8(o, OBJ_2nd, 8);
            w16(RAM_Ctrl_1_held_logical, 0x7070);       /* jump */
            SET16(o, TN_T2E, r8(RAM_Super_Sonic_Knux_flag) ? 0x28 : 0x38);
        }
        tornado_align(o);
        show = 1;
        break;
    case 8:                                             /* _Jump_to_plane */
        w16(RAM_Ctrl_1_held_logical, 0);
        SET16(o, TN_T2A, OB16(o, TN_T2A) + 1);
        SET16(o, TN_T2E, OB16(o, TN_T2E) - 1);
        if ((int16_t)OB16(o, TN_T2E) >= 0)             /* right + jump held (S2 re-presses: S3K's  */
            w16(RAM_Ctrl_1_held_logical, 0x7808);       /* players would insta-shield / fly / glide) */
        tornado_align(o);
        if (OB8(o, OBJ_status) & 0x08) {
            static const uint8_t a[4] = { 0x50, 0x1F, 0x00, 0x25 }, b[4] = { 0x25, 0x00, 0x1F, 0x50 };
            SET8(o, OBJ_2nd, 0x0A);
            SET16(o, TN_T2E, 0x20);
            for (int k = 0; k < 4; k++) {               /* Level_Layout+$0D2/$1D2/$BD6/$CD6 (BG) */
                s2_layout_put(0x52 + k, 0x00, 1, a[k]);
                s2_layout_put(0x52 + k, 0x01, 1, b[k]);
                s2_layout_put(0x56 + k, 0x0B, 1, a[k]);
                s2_layout_put(0x56 + k, 0x0C, 1, b[k]);
            }
        }
        show = 1;
        break;
    case 0x0A:                                          /* _Landed_on_plane */
        SET16(o, TN_T2A, OB16(o, TN_T2A) + 1);
        if (OB16(o, TN_T2A) >= 0x100) {
            SET8(o, OBJ_2nd, 0x0C);
            uint16_t t = OB16(o, TN_THR);
            if (t) SET8(0xFF0000u | t, OBJ_2nd, 2);     /* the thruster lights */
        }
        w16(RAM_Ctrl_1_held_logical, 0);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_ground_vel, 0);
        SET8(p, OBJ_status, OB8(p, OBJ_status) & ~(0x02 | 0x04));
        tornado_wait_anim();
        SET8(p, OBJ_y_radius, r16(RAM_Player_mode) == 2 ? 0x0F : 0x13);
        tornado_align(o);
        show = 1;
        break;
    default:
        if (OB8(o, OBJ_2nd) == 0x0C) {                  /* _Approaching_ship */
            w16(RAM_Ctrl_1_held_logical, 0);
            tornado_wait_anim();
            if (OB16(o, TN_T2A) >= 0x437) SET8(o, OBJ_2nd, 0x0E);
        }
        if (OB8(o, OBJ_2nd) == 0x0E) {                  /* _Jump_to_ship */
            if (OB16(o, TN_T2A) < 0x447)               /* press once, then hold (as above) */
                w16(RAM_Ctrl_1_held_logical, OB16(o, TN_T2A) == 0x437 ? 0x7070 : 0x7000);
        }
        if (OB8(o, OBJ_2nd) < 0x10 && OB16(o, TN_T2A) >= 0x460) {
            w8(RAM_Dynamic_resize_routine, 6);          /* => LevEvents_WFZ_Routine4 */
            SET8(o, OBJ_2nd, 0x10);
            tornado_child(o, 0x58, 0x3090, 0x3D0);
            tornado_child(o, 0x58, 0x30C0, 0x3F0);
            tornado_child(o, 0x58, 0x3090, 0x410);
        }
        if (OB16(o, TN_T2A) >= 0x9C0) {                 /* ObjB2_Start_DEZ */
            w16(RAM_Current_zone_and_act, (uint16_t)(TRI_S2_DEZ << 8));
            w16(RAM_Apparent_zone_and_act, tri_apparent((uint16_t)(TRI_S2_DEZ << 8)));
            w8(RAM_Last_star_post_hit, 0);
            w16(RAM_Restart_level_flag, 1);
            break;
        }
        {                                               /* ObjB2_Dock_on_DEZ */
            uint16_t d0 = (uint16_t)(OB16(o, TN_T2A) + 1);
            SET16(o, TN_T2A, d0);
            uint16_t d1 = OB16(o, TN_T34);
            if (d1 / 2 < 10 && d0 >= when[d1 / 2]) {
                d1 = (uint16_t)(d1 + 2);
                SET16(o, TN_T34, d1);
                SET8(o, OBJ_x_vel, (uint8_t)vel[d1]);
                SET8(o, OBJ_y_vel, (uint8_t)vel[d1 + 1]);
            }
        }
        tornado_align(o);
        show = 1;
        break;
    }
    o_animate_s2(o, o_asset_anim(S2A_TORNADO));
    if (show) o_draw(o);
}

static void tornado_grabber(uint32_t o)              /* ObjB2_Invisible_grabber */
{
    uint32_t p = RAM_Player_1;
    switch (OB8(o, OBJ_2nd)) {
    case 0:
        SET8(o, OBJ_coll_flags, 0xC7);
        SET8(o, OBJ_2nd, 2);
        /* fall through */
    case 2:
        if (!OB8(o, OBJ_coll_prop)) { o_touch(o); return; }
        SET8(o, OBJ_2nd, 4);
        SET8(o, OBJ_coll_flags, 0);
        w16(RAM_Camera_Y_bias, 224 / 2 + 8);
        {
            uint32_t a1 = 0xFF0000u | OB16(o, TN_PARENT);
            SET8(a1, OBJ_status, OB8(a1, OBJ_status) | 0x40);
        }
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x10);
        if (r16(RAM_Player_mode) == 2) SET16(p, OBJ_y_pos, OB16(p, OBJ_y_pos) - 0x10);
        SET8(p, OBJ_status, (OB8(p, OBJ_status) | 1) & ~(0x02 | 0x04));
        SET8(p, OBJ_anim, ANI_HANG);
        SET8(p, 0x2E, 1);                               /* obj_control */
        w8(RAM_WindTunnel_holding, 1);
        w16(RAM_Ctrl_1_held_logical, 0);
        return;
    default:
        SET16(p, OBJ_x_vel, 0);
        SET16(p, OBJ_y_vel, 0);
        SET16(p, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x10);
        return;
    }
}

static void tornado(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* ObjB2_Init (LoadSubObject) */
        uint8_t sub = OB8(o, OBJ_subtype);
        if (sub == 0x56 || sub == 0x58) {               /* ObjBC_SubObjData2: the ship's flames */
            o_init(o, S2A_THRUST, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        } else if (sub == 0x5C) {                       /* ObjB2_SubObjData2: thruster */
            o_init(o, S2A_THRUSTER, OB8(o, OBJ_render) | 4, 3, 0x40, 0x10);
        } else {
            o_init(o, S2A_TORNADO, OB8(o, OBJ_render) | 4, 4, 0x60, 0x20);
            SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        }
        uint8_t r = (uint8_t)(sub - 0x4E);
        SET8(o, OBJ_routine, r);
        if (r16(RAM_Player_mode) == 2 && r < 8) {      /* Tails alone: Sonic flies it */
            SET8(o, OBJ_frame, 4);
            SET8(o, OBJ_anim, 1);
        }
        o_draw(o);
        return;
    }
    case 2: tornado_scz(o); return;
    case 4: tornado_wfz_start(o); return;
    case 6: tornado_wfz_end(o); return;
    case 8: tornado_grabber(o); return;
    case 0x0A:                                          /* loc_3AD0C: flames, every other frame */
        SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 0x80);
        if (OB8(o, OBJ_status) & 0x80) o_draw(o);       /* (bchg: shown when the bit was clear) */
        return;
    case 0x0C:                                          /* loc_3AD2A */
        o_move(o);
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }
        o_draw(o);
        return;
    default: {                                          /* loc_3AD42: the thruster */
        uint32_t a1 = 0xFF0000u | OB16(o, TN_PARENT);
        SET16(o, OBJ_x_pos, OB16(a1, OBJ_x_pos) - 0x0C);
        SET16(o, OBJ_y_pos, OB16(a1, OBJ_y_pos) + 0x28);
        if (OB8(o, OBJ_2nd)) o_animate_s2(o, o_asset_anim(S2A_THRUSTER));
        o_draw(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object B3 - cloud drifting past (subtypes $5E/$60/$62: near to far)
 * ---------------------------------------------------------------------- */
static void cloud(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        static const int16_t speed[3] = { -0x80, -0x40, -0x20 };
        int k = ((OB8(o, OBJ_subtype) - 0x5E) >> 1) % 3;
        o_init(o, S2A_CLOUDS, OB8(o, OBJ_render) | 4, 6, 0x30, 0x10);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_vel, (uint16_t)speed[k]);
        SET8(o, OBJ_frame, k);
        return;
    }
    o_move(o);
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + s2_tornado_vx);
    delete_behind(o);
}

/* ------------------------------------------------------------------------
 * Object B4 - vertical propeller (harmful); B5 - horizontal propeller
 * (SCZ: just turns; WFZ's lifts players: s2_obj_wfz.c)
 * ---------------------------------------------------------------------- */
static void vprop(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_VPROP, OB8(o, OBJ_render) | 4, 4, 4, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 0xA8);
        SET8(o, OBJ_routine, 2);
        if (OB8(o, OBJ_render) & 2) {                   /* y-flipped: harmless */
            SET8(o, OBJ_render, OB8(o, OBJ_render) & ~2);
            SET8(o, OBJ_coll_flags, 0);
        }
        return;
    }
    o_animate_s2(o, o_asset_anim(S2A_VPROP));
    if (!(o_frame_counter() & 0x1F) && (OB8(o, OBJ_render) & 0x80))
        o_sfx_s2(0xDE);                                    /* SndID_Helicopter (PlaySoundLocal) */
    o_touch(o);
    o_remember(o);
}

void (*s2_hprop_main)(uint32_t o);                     /* ObjB5_Main (WFZ) */

static void hprop(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_HPROP, OB8(o, OBJ_render) | 4, 4, 0x40, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_anim, 4);
        SET8(o, OBJ_routine, (uint8_t)(OB8(o, OBJ_subtype) - 0x64));
        return;
    }
    if (OB8(o, OBJ_routine) == 2 && s2_hprop_main) { s2_hprop_main(o); return; }
    o_animate_s2(o, o_asset_anim(S2A_HPROP));           /* ObjB5_Animate */
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Jets (Object 9C) behind Balkiry / Turtloid
 * ---------------------------------------------------------------------- */
#define JT_PARENT 0x3C                /* objoff_2C (word) */
#define JT_ID     0x3E                /* objoff_32: the parent's id (code, low word) */
#define JT_ASSET  0x40                /* objoff_2E: its animation */

static void jet_new(uint32_t o, int asset)            /* loc_37ABE */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return;
    SET32(a1, OBJ_code, S2(0x9C));
    o_init(a1, asset, 4, 5, 8, 8);
    SET8(a1, OBJ_routine, 2);
    SET8(a1, OBJ_frame, 6);
    SET16(a1, JT_PARENT, (uint16_t)o);
    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
    SET16(a1, JT_ID, (uint16_t)r32(o));
    SET8(a1, JT_ASSET, (uint8_t)asset);
}

static void jet(uint32_t o)                          /* Obj9C_Main */
{
    uint32_t a1 = word_ptr(OB16(o, JT_PARENT));
    if ((uint16_t)r32(a1) != OB16(o, JT_ID)) { o_delete(o); return; }
    w32(o + OBJ_x_pos, r32(a1 + OBJ_x_pos));
    w32(o + OBJ_y_pos, r32(a1 + OBJ_y_pos));
    o_animate_s2(o, o_asset_anim(OB8(o, JT_ASSET)));
    delete_behind(o);
}

/* ------------------------------------------------------------------------
 * Object AC - Balkiry: dives past at speed
 * ---------------------------------------------------------------------- */
static void balkiry(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {
        o_init(o, S2A_BALKIRY, OB8(o, OBJ_render) | 4, 4, 0x20, 0x10);
        SET8(o, OBJ_coll_flags, 8);
        SET8(o, OBJ_routine, 2);
        SET8(o, OBJ_frame, 1);
        SET16(o, OBJ_x_vel, (uint16_t)-0x300);
        if (OB8(o, OBJ_render) & 2) {
            SET8(o, OBJ_render, OB8(o, OBJ_render) & ~2);
            SET16(o, OBJ_x_vel, (uint16_t)-0x500);
        }
        jet_new(o, S2A_TURTLEJET);
        o_touch(o);
        return;
    }
    o_move(o);
    tornado_drift(o);
    o_touch(o);
    delete_behind(o);
}

/* ------------------------------------------------------------------------
 * Object 99 - Nebula: drifts in, rises over Sonic, drops a bomb
 * ---------------------------------------------------------------------- */
#define NB_DROPPED 0x3A               /* objoff_2A */

static void nebula(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:
        o_init(o, S2A_NEBULA, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 6);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_vel, (uint16_t)-0xC0);
        o_touch(o);
        return;
    case 2: {                                           /* loc_377E8 */
        int d0;
        int16_t d2;
        s2_orient(o, &d0, NULL, &d2, NULL);
        if (!d0 && (uint16_t)d2 < 0x80) {
            SET8(o, OBJ_routine, 4);
            SET16(o, OBJ_y_vel, (uint16_t)-0xA0);
        }
        break;
    }
    default: {                                          /* loc_3781C */
        if (!OB8(o, NB_DROPPED)) {
            int16_t d2;
            s2_orient(o, NULL, NULL, &d2, NULL);
            if ((uint16_t)(d2 + 8) < 0x10) {
                SET8(o, NB_DROPPED, 0xFF);              /* loc_37850: the bomb */
                uint32_t a1 = o_alloc_after(o);
                if (a1) {
                    SET32(a1, OBJ_code, S2(0x98));
                    SET8(a1, OBJ_routine, 2);
                    o_init(a1, S2A_NEBULA, 0x80 | 4, 4, 8, 8);
                    SET16(a1, OBJ_art_tile, OB16(a1, OBJ_art_tile) | 0x8000);
                    SET8(a1, OBJ_coll_flags, 0x8B);
                    SET8(a1, OBJ_frame, 4);
                    SET8(a1, S2_PROJ_KIND, S2_PROJ_NEBULA);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x18);
                }
            }
        }
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 1);
        break;
    }
    }
    o_move(o);
    tornado_drift(o);
    o_animate_s2(o, o_asset_anim(S2A_NEBULA));
    o_touch(o);
    delete_behind(o);
}

/* ------------------------------------------------------------------------
 * Object 9A - Turtloid (a platform; its rider 9B is the badnik) shoots
 * ---------------------------------------------------------------------- */
#define TT_TIME   0x3A                /* objoff_2A */
#define TT_LINK   0x3C                /* objoff_2C (word): rider <-> turtle */

static void turtloid(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj9A_Init */
        o_init(o, S2A_TURTLOID, OB8(o, OBJ_render) | 4, 5, 0x18, 0x10);
        SET8(o, OBJ_routine, 2);
        SET16(o, OBJ_x_vel, (uint16_t)-0x80);
        uint32_t a1 = o_alloc_after(o);                 /* loc_37A4A: the rider */
        if (a1) {
            SET32(a1, OBJ_code, S2(0x9B));
            o_init(a1, S2A_TURTLOID, 4, 4, 0x0C, 0x10);
            SET8(a1, OBJ_coll_flags, 0x1A);
            SET8(a1, OBJ_routine, 2);
            SET8(a1, OBJ_frame, 2);
            SET16(a1, TT_LINK, (uint16_t)o);
            SET16(o, TT_LINK, (uint16_t)a1);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + 4);
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x18);
        }
        jet_new(o, S2A_TURTLOID);
        return;
    }
    switch (OB8(o, OBJ_2nd)) {
    case 0: {                                           /* loc_379A0 */
        int16_t d2;
        s2_orient(o, NULL, NULL, &d2, NULL);
        if ((uint16_t)d2 >= 0x80) break;
        SET8(o, OBJ_2nd, 2);
        SET16(o, OBJ_x_vel, 0);
        SET8(o, TT_TIME, 4);
        SET8(o, OBJ_frame, 1);
        break;
    }
    case 2:                                             /* loc_379CA */
        SET8(o, TT_TIME, OB8(o, TT_TIME) - 1);
        if ((int8_t)OB8(o, TT_TIME) >= 0) break;
        SET8(o, OBJ_2nd, 4);
        SET8(o, TT_TIME, 8);
        SET8(word_ptr(OB16(o, TT_LINK)), OBJ_frame, 3);
        {                                               /* loc_37AF2: the shot */
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, S2(0x98));
                SET8(a1, OBJ_routine, 2);
                o_init(a1, S2A_TURTLESHOT, 0x80 | 4, 4, 4, 4);
                SET8(a1, OBJ_coll_flags, 0x98);
                SET8(a1, OBJ_frame, 6);
                SET8(a1, S2_PROJ_KIND, S2_PROJ_TURTLOID);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) - 0x14);
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x0A);
                SET16(a1, OBJ_x_vel, (uint16_t)-0x100);
            }
        }
        break;
    case 4:                                             /* loc_379EA */
        SET8(o, TT_TIME, OB8(o, TT_TIME) - 1);
        if ((int8_t)OB8(o, TT_TIME) >= 0) break;
        SET8(o, OBJ_2nd, 6);
        SET16(o, OBJ_x_vel, (uint16_t)-0x80);
        SET8(o, OBJ_frame, 0);
        break;
    default:
        break;
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);                 /* loc_37982 */
    o_move(o);
    tornado_drift(o);
    o_platform(o, 0x18, 0x0E, oldx);
    delete_behind(o);
}

static void turtloid_rider(uint32_t o)               /* Obj9B_Main */
{
    uint32_t a1 = word_ptr(OB16(o, TT_LINK));
    w32(o + OBJ_x_pos, r32(a1 + OBJ_x_pos));
    w32(o + OBJ_y_pos, r32(a1 + OBJ_y_pos));
    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + 4);
    SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 0x18);
    o_touch(o);
    delete_behind(o);
}

void s2_obj_scz_register(TriObjFn *t)
{
    t[0xB2] = tornado;
    t[0xB3] = cloud;
    t[0xB4] = vprop;
    t[0xB5] = hprop;
    t[0xAC] = balkiry;
    t[0x99] = nebula;
    t[0x9A] = turtloid;
    t[0x9B] = turtloid_rider;
    t[0x9C] = jet;
}
