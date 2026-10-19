/*
 * s2_obj_arz.c — Sonic 2 Aquatic Ruin objects, transcribed from s2disasm:
 * swinging platform (15), collapsing platform (1F), arrow shooter (22),
 * pillar that drops its lower part (23), rising pillar (2B), leaves (2C),
 * falling pillar (82), three rotating platforms (83), Whisp (8C), Grounder
 * (8D/8E with its wall 8F and rocks 90), Chop Chop (91). Bubble makers (24)
 * are S3K's Obj_Bubbler (s2_level.pointer_table).
 *
 * S2 variables at objoff_2A-2F would hit S3K's status/subtype, so they live
 * at $30+/$40+ here. S2's multi-sprite chains use S3K's own multi-draw
 * (render bit 6: $16 child count, $18 + 6n x/y/-/frame).
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"

#define S2(id)          (NATIVE_S2_BASE + (id) * 4u)
#define OBJ_2nd         0x25
#define OBJ_prev_anim   0x21
#define P_INTERACT      0x42
#define MS_COUNT        0x16          /* mainspr_childsprites (word) */
#define MS_SUB          0x18          /* sub2_x_pos: x, y, -, frame per child */

static void ms_child(uint32_t o, int k, int16_t x, int16_t y, int frame)
{
    uint32_t a = o + MS_SUB + (uint32_t)k * 6u;
    w16(a, (uint16_t)x);
    w16(a + 2, (uint16_t)y);
    w8(a + 5, (uint8_t)frame);
}

/* a multi-sprite chain object: drawn by Render_Sprites, its code only displays it */
static uint32_t ms_new(uint32_t parent, int asset, int w, int h, int count)
{
    uint32_t a1 = o_alloc_after(parent);
    if (!a1) return 0;
    SET32(a1, OBJ_code, r32(parent));
    o_asset(a1, asset);
    SET8(a1, OBJ_render, 4 | 0x40);
    SET16(a1, OBJ_priority, OB16(parent, OBJ_priority));
    SET8(a1, OBJ_width_px, w);
    SET8(a1, OBJ_height_px, h);
    SET16(a1, MS_COUNT, (uint16_t)count);
    SET8(a1, OBJ_frame, 1);
    for (int k = 0; k < count; k++) ms_child(a1, k, 0, 0, 1);
    return a1;
}

/* ------------------------------------------------------------------------
 * Object 15 - swinging platform on a chain (ARZ: the link art)
 * ---------------------------------------------------------------------- */
#define SW_PREV   0x40                /* objoff_2E: last swing angle */
#define SW_CHAIN  0x30                /* objoff_30 (long) */
#define SW_HOLD   0x34                /* objoff_34 */
#define SW_WAIT   0x36                /* objoff_36 (word) */
#define SW_Y0     0x38                /* objoff_38 */
#define SW_X0     0x3A                /* objoff_3A */
#define SW_DIR    0x3D                /* objoff_3D */
#define SW_SPEED  0x3E                /* objoff_3E (word) */
#define SW_LINKS  0x44                /* the chain's link count (S2 kept it in the chain) */

static uint8_t swing_ball(uint32_t o)                /* loc_FF6E: MCZ spike ball, swings once Sonic is near */
{
    if (OB16(o, SW_WAIT)) {
        SET16(o, SW_WAIT, OB16(o, SW_WAIT) - 1);
        return OB8(o, OBJ_angle);
    }
    if (!OB8(o, SW_HOLD)) {
        if ((uint16_t)(OB16(RAM_Player_1, OBJ_x_pos) - OB16(o, SW_X0) + 0x20) >= 0x40) return OB8(o, OBJ_angle);
        if (r16(RAM_Debug_placement_mode)) return OB8(o, OBJ_angle);
        SET8(o, SW_HOLD, 1);
    }
    if (OB8(o, SW_DIR)) {
        uint16_t d0 = (uint16_t)(OB16(o, SW_SPEED) + 8);
        SET16(o, SW_SPEED, d0);
        SET16(o, OBJ_angle, OB16(o, OBJ_angle) + d0);
        if (d0 == 0x200) {
            SET16(o, SW_SPEED, 0);
            SET16(o, OBJ_angle, 0x8000);
            SET8(o, SW_DIR, 0);
            SET16(o, SW_WAIT, 60);
        }
    } else {
        uint16_t d0 = (uint16_t)(OB16(o, SW_SPEED) - 8);
        SET16(o, SW_SPEED, d0);
        SET16(o, OBJ_angle, OB16(o, OBJ_angle) + d0);
        if (d0 == 0xFE00) {
            SET16(o, SW_SPEED, 0);
            SET16(o, OBJ_angle, 0x4000);
            SET8(o, SW_DIR, 1);
            SET16(o, SW_WAIT, 60);
        }
    }
    return OB8(o, OBJ_angle);
}

static void swing_place(uint32_t o)                  /* sub_FE70 (types 0/$10/$30/$40) */
{
    uint8_t d0 = o_osc(0x18 + 2);
    uint8_t t = OB8(o, OBJ_subtype);
    if (t == 0x40) {
        d0 = swing_ball(o);
    } else if (t == 0x10) {
        if (d0 == 0x3F) { if (OB8(o, OBJ_render) & 0x80) o_sfx_s2(0xD7); /* SndID_PlatformKnock */ d0 = 0x40; }
        else if (d0 < 0x3F) d0 = 0x40;
    } else if (t == 0x30) {
        if (d0 == 0x41) { if (OB8(o, OBJ_render) & 0x80) o_sfx_s2(0xD7); /* SndID_PlatformKnock */ d0 = 0x40; }
        else if (d0 > 0x41) d0 = 0x40;
    } else if (t == 0x20) {
        return;
    }
    if (OB8(o, SW_PREV) == d0) return;
    SET8(o, SW_PREV, d0);
    uint8_t ang = d0;
    if (OB8(o, OBJ_status) & 1) ang = (uint8_t)(0x80 - d0);
    int16_t s, c;
    o_sincos(ang, &s, &c);
    int32_t ds = (int32_t)s << 12, dc = (int32_t)c << 12;   /* swap; asr.l #4 */
    int16_t y0 = (int16_t)OB16(o, SW_Y0), x0 = (int16_t)OB16(o, SW_X0);
    uint32_t ch = r32(o + SW_CHAIN) & 0xFFFFFF;
    int n = OB8(o, SW_LINKS);
    int32_t y = 0, x = 0;
    if (ch) {
        for (int k = 0; k < n; k++) {                   /* anchor + links */
            ms_child(ch, k, (int16_t)(x0 + (x >> 16)), (int16_t)(y0 + (y >> 16)), k ? 1 : 2);
            y += ds; x += dc;
        }
        SET16(ch, OBJ_x_pos, (uint16_t)(x0 + (x >> 16)));     /* the last link: the main sprite */
        SET16(ch, OBJ_y_pos, (uint16_t)(y0 + (y >> 16)));
    } else {
        y = ds * n; x = dc * n;
    }
    y += ds >> 1; x += dc >> 1;
    SET16(o, OBJ_y_pos, (uint16_t)(y0 + (y >> 16)));
    SET16(o, OBJ_x_pos, (uint16_t)(x0 + (x >> 16)));
}

static int swing_gone(uint32_t o)                    /* loc_1000C */
{
    if (!o_out_of_range(o, OB16(o, SW_X0))) { o_draw(o); return 0; }
    uint32_t ch = r32(o + SW_CHAIN) & 0xFFFFFF;
    if (ch) o_delete(ch);
    o_delete_respawnable(o);
    return 1;
}

static void swing(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the chain */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj15_Init (ARZ / MCZ) */
        int mcz = tri_zone() == TRI_S2_MCZ, ooz = tri_zone() == TRI_S2_OOZ;
        int asset = mcz ? S2A_MCZPLAT : ooz ? S2A_OOZSWING : S2A_ARZSWING;
        o_init(o, asset, OB8(o, OBJ_render) | 4, 3, mcz ? 0x18 : 0x20, 0x10);
        SET8(o, OBJ_y_radius, ooz ? 0x10 : 8);
        SET16(o, SW_Y0, OB16(o, OBJ_y_pos));
        SET16(o, SW_X0, OB16(o, OBJ_x_pos));
        uint8_t sub = OB8(o, OBJ_subtype);
        SET8(o, OBJ_routine, (sub & 0x80) ? 6 : 2);
        int n = sub & 0x0F;                             /* n children (anchor, links) + the main sprite */
        SET8(o, SW_LINKS, n);
        SET32(o, SW_CHAIN, ms_new(o, asset, 0x48, 0x50, n));
        SET8(o, SW_PREV, 0xFF);
        SET16(o, OBJ_angle, 0x8000);
        SET16(o, SW_SPEED, 0);
        SET8(o, OBJ_subtype, sub & 0x70);
        if ((sub & 0x70) == 0x40) {                     /* the spike ball */
            SET32(o, OBJ_mappings, o_asset_maps(S2A_MCZBALL));
            SET8(o, OBJ_coll_flags, 0xA7);
        }
        swing_place(o);
        return;
    }
    case 2: {                                           /* Obj15_State2 */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        swing_place(o);
        o_platform(o, OB8(o, OBJ_width_px), OB8(o, OBJ_y_radius) + 1, oldx);
        o_touch(o);                                     /* (S2 touches every object: the spike ball) */
        swing_gone(o);
        return;
    }
    case 6: {                                           /* Obj15_State4: breaks off when stood on at the bottom */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        swing_place(o);
        o_platform(o, OB8(o, OBJ_width_px), OB8(o, OBJ_y_radius) + 1, oldx);
        if ((OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) && !o_osc(0x18 + 2)) {
            uint32_t a1 = o_alloc_after(o);
            if (a1) {
                for (int k = 0; k < OBJ_SIZE; k += 2) w16(a1 + (uint32_t)k, r16(o + (uint32_t)k));
                SET16(a1, OBJ_respawn, 0);
                SET8(a1, OBJ_routine, tri_zone() == TRI_S2_ARZ ? 0x0C : 0x0A);   /* State7 (ARZ) / State6 */
                SET16(a1, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? (uint16_t)-0x200 : 0x200);
                SET8(a1, OBJ_status, OB8(a1, OBJ_status) | ST_YFLIP);
                for (int n = 0; n < 2; n++) {           /* whoever stood on it now stands on the copy */
                    uint32_t p = o_player(n);
                    if (OB16(p, P_INTERACT) == (uint16_t)o) SET16(p, P_INTERACT, (uint16_t)a1);
                }
            }
            SET8(o, OBJ_frame, 3);
            SET8(o, OBJ_routine, 8);
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~(ST_P1_STAND | ST_P2_STAND));
        }
        swing_gone(o);
        return;
    }
    case 8:                                             /* Obj15_State5: just the chain */
        swing_place(o);
        swing_gone(o);
        return;
    case 0x0A: {                                        /* Obj15_State6 (other zones): fall, then bob */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        if (OB8(o, OBJ_status) & ST_YFLIP) {
            o_move(o);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            if (OB16(o, OBJ_y_pos) >= 0x720) {
                SET16(o, OBJ_y_pos, 0x720);
                SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_YFLIP);
                SET16(o, OBJ_x_vel, 0);
                SET16(o, OBJ_y_vel, 0);
                SET16(o, SW_Y0, 0x720);
            }
        } else {
            SET16(o, OBJ_y_pos, OB16(o, SW_Y0) + (o_osc(0x14 + 2) >> 1));
        }
        o_platform(o, OB8(o, OBJ_width_px), OB8(o, OBJ_y_radius) + 1, oldx);
        o_remember(o);
        return;
    }
    default: {                                          /* Obj15_State7 (ARZ): falls in and floats off */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        o_move(o);
        if (OB8(o, OBJ_status) & ST_YFLIP) {
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
            uint16_t wl = r16(RAM_Mean_water_level);
            if (wl <= OB16(o, OBJ_y_pos)) {
                SET16(o, OBJ_y_pos, wl);
                SET16(o, SW_Y0, wl);
                SET8(o, OBJ_status, OB8(o, OBJ_status) & ~ST_YFLIP);
                SET16(o, OBJ_x_vel, 0x100);
                SET16(o, OBJ_y_vel, 0);
            }
        } else {
            SET16(o, OBJ_y_pos, OB16(o, SW_Y0) + (o_osc(0x14 + 2) >> 1));
            if (OB16(o, OBJ_x_vel)) {
                int16_t d1 = o_wall_right(o, OB8(o, OBJ_width_px));
                if (d1 < 0) {
                    SET16(o, OBJ_x_pos, OB16(o, OBJ_x_pos) + d1);
                    SET16(o, OBJ_x_vel, 0);
                }
            }
        }
        o_platform(o, OB8(o, OBJ_width_px), OB8(o, OBJ_y_radius) + 1, oldx);
        o_remember(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 1F - collapsing platform (ARZ art)
 * ---------------------------------------------------------------------- */
#define CL_COUNT 0x34                 /* collapsing_platform_delay_counter */
#define CL_STOOD 0x35                 /* collapsing_platform_stood_on_flag */

static void collapse_top(uint32_t o)                 /* sub_10AF8 */
{
    o_platform(o, OB8(o, OBJ_width_px), 0x10, OB16(o, OBJ_x_pos));
    o_remember(o);
}

static void collapse(uint32_t o)
{
    /* Obj1F_MCZ_DelayData runs on into Obj1F_ARZ_DelayData (S2 reads (a4)+ per piece) */
    static const uint8_t delay_tab[14] = { 0x1A, 0x16, 0x12, 0x0E, 0x0A, 2,
                                           0x16, 0x1A, 0x18, 0x12, 6, 0x0E, 0x0A, 2 };
    static const uint8_t ooz_delays[7] = { 0x1A, 0x12, 0x0A, 2, 0x16, 0x0E, 6 };   /* Obj1F_OOZ_DelayData */
    const uint8_t *delays = tri_zone() == TRI_S2_MCZ ? delay_tab : tri_zone() == TRI_S2_OOZ ? ooz_delays : delay_tab + 6;
    int ndelay = tri_zone() == TRI_S2_MCZ ? 14 : tri_zone() == TRI_S2_OOZ ? 7 : 8;
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, tri_zone() == TRI_S2_MCZ ? S2A_MCZCOLLAPSE : tri_zone() == TRI_S2_OOZ ? S2A_OOZCOLLAPSE : S2A_ARZCOLLAPSE,
               OB8(o, OBJ_render) | 4, 4, tri_zone() == TRI_S2_OOZ ? 0x40 : 0x20, 0x10);
        SET8(o, CL_COUNT, 7);
        /* fall through */
    case 2:                                             /* Obj1F_Main */
        if (OB8(o, CL_STOOD)) {
            if (!OB8(o, CL_COUNT)) {                    /* Obj1F_CreateFragments */
                SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
                uint32_t maps = r32(o + OBJ_mappings);
                uint32_t fa = maps + tri_rd16(maps + OB8(o, OBJ_frame) * 2u);
                int n = tri_rd16(fa);
                uint8_t render = (uint8_t)(OB8(o, OBJ_render) | 0x20);
                uint32_t a1 = o;
                for (int k = 0; k < n; k++) {
                    if (k) {
                        a1 = o_alloc();
                        if (!a1) break;
                        SET32(a1, OBJ_code, S2(0x1F));
                        SET8(a1, CL_STOOD, 0);
                    }
                    SET8(a1, OBJ_routine, 4);
                    SET32(a1, OBJ_mappings, fa + 2 + (uint32_t)k * 6u);
                    SET8(a1, OBJ_render, render);
                    SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
                    SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
                    SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
                    SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
                    SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
                    SET8(a1, OBJ_height_px, 0x10);
                    SET8(a1, OBJ_y_radius, OB8(o, OBJ_y_radius));
                    SET8(a1, CL_COUNT, delays[k < ndelay ? k : ndelay - 1]);
                    if ((a1 & 0xFFFF) < (o & 0xFFFF)) o_draw(a1);
                }
                o_draw(o);
                o_sfx_s2(0xB9);                            /* SndID_Smash */
                return;
            }
            SET8(o, CL_COUNT, OB8(o, CL_COUNT) - 1);
        }
        if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) SET8(o, CL_STOOD, 1);
        collapse_top(o);
        return;
    default:                                            /* Obj1F_Fragment */
        if (!OB8(o, CL_COUNT)) {                        /* Obj1F_FragmentFall */
            o_fall(o);
            if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
            o_draw(o);
            return;
        }
        if (!OB8(o, CL_STOOD)) {
            SET8(o, CL_COUNT, OB8(o, CL_COUNT) - 1);
            o_draw(o);
            return;
        }
        collapse_top(o);
        SET8(o, CL_COUNT, OB8(o, CL_COUNT) - 1);
        if (!OB8(o, CL_COUNT)) {
            for (int n = 0; n < 2; n++) {               /* sub_10B36 */
                uint32_t p = o_player(n);
                if (!(OB8(p, OBJ_status) & 8)) continue;
                SET8(p, OBJ_status, OB8(p, OBJ_status) & ~(8 | 0x20));
                SET8(p, OBJ_prev_anim, 1);              /* AniIDSonAni_Run: restart the animation */
            }
        }
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 22 - arrow shooter and its arrow
 * ---------------------------------------------------------------------- */
static void arrow_shooter(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj22_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_ARROW, OB8(o, OBJ_render) | 4, 3, 0x10, 0x10);
        SET8(o, OBJ_frame, 1);
        SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) & 0x0F);
        /* fall through */
    case 2:                                             /* Obj22_Main */
        if (OB8(o, OBJ_anim) != 2) {
            int d2 = 0;
            for (int n = 0; n < 2; n++) {
                int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o_player(n), OBJ_x_pos));
                if (d0 < 0) d0 = (int16_t)-d0;
                if ((uint16_t)d0 < 0x40) d2 = 1;
            }
            if (!d2 && OB8(o, OBJ_anim)) d2 = 2;
            SET8(o, OBJ_anim, d2);
        }
        o_animate_s2(o, o_asset_anim(S2A_ARROW));
        o_remember(o);
        return;
    case 4: {                                           /* Obj22_ShootArrow */
        uint32_t a1 = o_alloc();
        if (a1) {
            SET32(a1, OBJ_code, S2(0x22));
            SET8(a1, OBJ_routine, 6);
            SET32(a1, OBJ_mappings, r32(o + OBJ_mappings));
            SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET8(a1, OBJ_render, OB8(o, OBJ_render));
            SET8(a1, OBJ_status, OB8(o, OBJ_status));
            o_sfx_s2(0xDB);                                /* SndID_PreArrowFiring */
        }
        SET8(o, OBJ_routine, 2);
        o_animate_s2(o, o_asset_anim(S2A_ARROW));
        o_remember(o);
        return;
    }
    case 6:                                             /* Obj22_Arrow_Init */
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_y_radius, 8);
        SET8(o, OBJ_x_radius, 0x10);
        SET16(o, OBJ_priority, 4 << 7);
        SET8(o, OBJ_coll_flags, 0x9B);
        SET8(o, OBJ_width_px, 8);
        SET8(o, OBJ_height_px, 8);
        SET8(o, OBJ_frame, 0);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? (uint16_t)-0x400 : 0x400);
        o_sfx_s2(0xAE);                                    /* SndID_ArrowFiring/LavaBall */
        /* fall through */
    default:                                            /* Obj22_Arrow */
        o_move(o);
        if (!(OB8(o, OBJ_status) & 1) ? o_wall_left(o, -8) < 0 : o_wall_right(o, 8) < 0) { o_delete(o); return; }
        o_touch(o);
        o_remember(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 23 - pillar that drops its lower part
 * ---------------------------------------------------------------------- */
#define PD_X0    0x30
#define PD_YSET  0x32
#define PD_TIMER 0x34

static void pillar_drop(uint32_t o)
{
    if (!OB8(o, OBJ_routine)) {                         /* Obj23_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_ARZPILLAR, OB8(o, OBJ_render) | 4, 4, 0x10, 0x20);
        SET8(o, OBJ_y_radius, 0x20);
        uint32_t a1 = o_alloc_after(o);
        if (a1) {                                       /* the lower part */
            SET32(a1, OBJ_code, S2(0x23));
            SET8(a1, OBJ_routine, 2);
            SET8(a1, OBJ_2nd, 2);
            o_init(a1, S2A_ARZPILLAR, OB8(o, OBJ_render), 4, 0x10, 0x10);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, PD_X0, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 0x30);
            SET8(a1, OBJ_y_radius, 0x10);
            SET8(a1, OBJ_frame, 1);
        }
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    switch (OB8(o, OBJ_2nd)) {
    case 2:                                             /* wait for a player within $80 */
        if (r16(RAM_Debug_placement_mode)) break;
        for (int n = 0; n < 2; n++) {
            int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o_player(n), OBJ_x_pos));
            if (d0 < 0) d0 = (int16_t)-d0;
            if ((uint16_t)d0 < 0x80) { SET8(o, OBJ_2nd, 4); SET16(o, PD_TIMER, 8); }
        }
        break;
    case 4: {                                           /* shake */
        static const int8_t shake[8] = { 0, 1, -1, 1, 0, -1, 0, 1 };
        uint16_t d0 = OB16(o, PD_TIMER);
        if (!d0) { SET8(o, OBJ_2nd, 6); break; }
        d0--;
        SET16(o, PD_TIMER, d0);
        SET16(o, OBJ_x_pos, OB16(o, PD_X0) + shake[d0]);
        break;
    }
    case 6: {                                           /* drop */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x38);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            SET16(o, OBJ_y_vel, 0);
            SET16(o, PD_YSET, OB16(o, OBJ_y_pos));
            SET8(o, OBJ_frame, 2);
            SET8(o, OBJ_2nd, 0);
        }
        break;
    }
    default: break;
    }
    o_solid(o, OB8(o, OBJ_width_px) + 0x0B, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, oldx);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 2B - rising pillar (bursts when stood on)
 * ---------------------------------------------------------------------- */
#define RP_TIMER 0x34
#define RP_DELAY 0x3F

static void rising_pillar(uint32_t o)
{
    static const uint8_t delays[14] = { 0, 0, 0, 0, 4, 4, 8, 8, 0x0C, 0x0C, 0x10, 0x10, 0x14, 0x14 };
    static const int16_t speeds[28] = {
        -0x200, -0x200, 0x200, -0x200, -0x1C0, -0x1C0, 0x1C0, -0x1C0, -0x180, -0x180, 0x180, -0x180,
        -0x140, -0x140, 0x140, -0x140, -0x100, -0x100, 0x100, -0x100, -0xC0, -0xC0, 0xC0, -0xC0,
        -0x80, -0x80, 0x80, -0x80,
    };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_RISEPILLAR, OB8(o, OBJ_render) | 4, 4, 0x10, 0x18);
        SET8(o, OBJ_y_radius, 0x18);
        /* fall through */
    case 2: {                                           /* Obj2B_Main */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        switch (OB8(o, OBJ_2nd)) {
        case 0:
            if (r16(RAM_Debug_placement_mode)) break;
            for (int n = 0; n < 2; n++) {
                int16_t d0 = (int16_t)(OB16(o, OBJ_x_pos) - OB16(o_player(n), OBJ_x_pos));
                if (d0 < 0) d0 = (int16_t)-d0;
                if ((uint16_t)d0 < 0x40) SET8(o, OBJ_2nd, 2);
            }
            break;
        case 2:                                         /* rise 4px every 4 frames */
            if (OB16(o, RP_TIMER)) { SET16(o, RP_TIMER, OB16(o, RP_TIMER) - 1); break; }
            SET16(o, RP_TIMER, 3);
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - 4);
            SET8(o, OBJ_y_radius, OB8(o, OBJ_y_radius) + 4);
            SET8(o, OBJ_height_px, OB8(o, OBJ_y_radius));
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 1);
            if (OB8(o, OBJ_frame) == 6) SET8(o, OBJ_2nd, 4);
            break;
        default: break;
        }
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, oldx);
        if (!(OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND))) { o_remember(o); return; }
        /* loc_25ACE: burst into pieces, throwing whoever stood on it */
        SET8(o, OBJ_frame, OB8(o, OBJ_frame) + 7);
        uint32_t maps = r32(o + OBJ_mappings);
        uint32_t fa = maps + tri_rd16(maps + OB8(o, OBJ_frame) * 2u);
        int n = tri_rd16(fa);
        uint8_t render = (uint8_t)(OB8(o, OBJ_render) | 0x20);
        uint32_t a1 = o;
        for (int k = 0; k < n && k < 14; k++) {
            if (k) {
                a1 = o_alloc_after(o);
                if (!a1) break;
                SET32(a1, OBJ_code, S2(0x2B));
            }
            SET8(a1, OBJ_routine, 4);
            SET32(a1, OBJ_mappings, fa + 2 + (uint32_t)k * 6u);
            SET8(a1, OBJ_render, render);
            SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
            SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
            SET16(a1, OBJ_art_tile, OB16(o, OBJ_art_tile));
            SET16(a1, OBJ_priority, OB16(o, OBJ_priority));
            SET8(a1, OBJ_width_px, OB8(o, OBJ_width_px));
            SET8(a1, OBJ_height_px, 0x10);
            SET16(a1, OBJ_x_vel, (uint16_t)speeds[k * 2]);
            SET16(a1, OBJ_y_vel, (uint16_t)speeds[k * 2 + 1]);
            SET8(a1, RP_DELAY, delays[k]);
        }
        o_sfx_s2(0xCB);                                    /* SndID_SlowSmash */
        for (int p = 0; p < 2; p++) {
            uint8_t bit = (uint8_t)(ST_P1_STAND << p);
            if (!(OB8(o, OBJ_status) & bit)) continue;
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~bit);
            uint32_t pl = o_player(p);
            SET8(pl, OBJ_status, (OB8(pl, OBJ_status) | 4 | 2) & ~8);
            SET8(pl, OBJ_y_radius, 0x0E);
            SET8(pl, OBJ_x_radius, 7);
            SET8(pl, OBJ_anim, 2);
            SET8(pl, OBJ_routine, 2);
        }
    }   /* fall through: o is now piece 0 */
    default:                                            /* loc_25B8E: a piece */
        if (OB8(o, RP_DELAY)) SET8(o, RP_DELAY, OB8(o, RP_DELAY) - 1);
        else {
            o_move(o);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 0x18);
        }
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 2C - invisible leaves maker: running through it shakes off leaves.
 * Its touch sizes ($14-$16 special) are tested here (S3K has no special $14).
 * ---------------------------------------------------------------------- */
#define LV_TIME  0x40                 /* objoff_2E (word) */
#define LV_X     0x30                 /* leaf: objoff_30 (long) */
#define LV_Y     0x34                 /* leaf: objoff_34 (long) */
#define LV_SPIN  0x38                 /* leaf: objoff_38 */

static void leaves_create(uint32_t o, uint32_t p)    /* Obj2C_CreateLeaves */
{
    static const int16_t spd[8] = { -0x80, -0x80, 0xC0, -0x40, -0xC0, 0x40, 0x80, 0x80 };
    int16_t xv = (int16_t)OB16(p, OBJ_x_vel), yv = (int16_t)OB16(p, OBJ_y_vel);
    if ((xv < 0 ? -xv : xv) < 0x200 && (yv < 0 ? -yv : yv) < 0x200) { SET16(o, LV_TIME, 0); return; }
    for (int k = 0; k < 4; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) break;
        SET32(a1, OBJ_code, S2(0x2C));
        SET8(a1, OBJ_routine, 4);
        uint32_t r = (uint32_t)o_random();
        SET16(a1, OBJ_x_pos, OB16(p, OBJ_x_pos) + (int16_t)((r & 0x0F) - 8));
        int16_t ry = (int16_t)(((r >> 16) & 0x0F) - 8);
        SET16(a1, OBJ_y_pos, OB16(p, OBJ_y_pos) + ry);
        SET16(a1, OBJ_x_vel, (uint16_t)((OB8(p, OBJ_status) & 1) ? -spd[k * 2] : spd[k * 2]));
        SET16(a1, OBJ_y_vel, (uint16_t)spd[k * 2 + 1]);
        SET32(a1, LV_X, (uint32_t)OB16(a1, OBJ_x_pos) << 16);
        SET32(a1, LV_Y, (uint32_t)OB16(a1, OBJ_y_pos) << 16);
        o_init(a1, S2A_LEAVES, 0x84, 1, 8, 8);
        SET16(a1, OBJ_art_tile, OB16(a1, OBJ_art_tile) | 0x8000);
        SET8(a1, OBJ_frame, (uint8_t)ry & 1);
        SET8(a1, LV_SPIN, 4);                           /* (S2 writes the random angle to the maker) */
    }
    o_sfx_s2(0xE5);                                        /* SndID_Leaves */
}

static void leaves(uint32_t o)
{
    static const uint8_t box[3][2] = { { 0x20, 0x20 }, { 0x40, 0x20 }, { 0x80, 0x20 } };   /* $16, $14, $15 */
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj2C_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_LEAVES, 0x84, 4, 0x80, 0x20);     /* (on-screen bit set: S2 never displays it) */
        SET8(o, OBJ_frame, OB8(o, OBJ_subtype));
        /* fall through */
    case 2: {                                           /* Obj2C_Main */
        if (o_out_of_range(o, OB16(o, OBJ_x_pos))) { o_delete(o); return; }
        int sz = OB8(o, OBJ_subtype) < 3 ? OB8(o, OBJ_subtype) : 0;
        SET8(o, OBJ_render, OB8(o, OBJ_render) | 0x80);
        uint8_t prop = (uint8_t)o_s1_touch(o, box[sz][0], box[sz][1]);
        if (!prop) { SET16(o, LV_TIME, 0); return; }
        uint16_t d0 = OB16(o, LV_TIME);
        int who = 0;
        if (d0) {
            d0 = (uint16_t)((uint8_t)(d0 + o_frame_counter()) & 0x0F);
            if (d0) {
                if ((d0 + 8) & 0x0F) return;
                who = 1;
            }
        }
        if (prop & (1 << who)) {
            leaves_create(o, o_player(who));
            if (!OB16(o, LV_TIME)) SET16(o, LV_TIME, r16(RAM_Level_frame_counter));
        }
        return;
    }
    default: {                                          /* Obj2C_Leaf */
        uint8_t d0 = OB8(o, LV_SPIN);
        SET8(o, OBJ_angle, OB8(o, OBJ_angle) + d0);
        d0 = (uint8_t)(d0 + o_frame_counter());
        if (!(d0 & 0x1F) && ((d0 + o_slot_index(o)) & 1))   /* (S2 mixes in RunObjects' d7) */
            SET8(o, LV_SPIN, (uint8_t)-(int8_t)OB8(o, LV_SPIN));
        uint32_t x = r32(o + LV_X) + (uint32_t)((int32_t)(int16_t)OB16(o, OBJ_x_vel) << 8);
        uint32_t y = r32(o + LV_Y) + (uint32_t)((int32_t)(int16_t)OB16(o, OBJ_y_vel) << 8);
        SET32(o, LV_X, x);
        SET32(o, LV_Y, y);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + (y & 3) + 4);
        int16_t s, c;
        o_sincos(OB8(o, OBJ_angle), &s, &c);
        SET16(o, OBJ_x_pos, (uint16_t)((s >> 6) + (x >> 16)));
        SET16(o, OBJ_y_pos, (uint16_t)((c >> 6) + (y >> 16)));
        SET8(o, OBJ_anim_timer, OB8(o, OBJ_anim_timer) - 1);
        if ((int8_t)OB8(o, OBJ_anim_timer) < 0) {
            SET8(o, OBJ_anim_timer, 0x0B);
            SET8(o, OBJ_frame, OB8(o, OBJ_frame) ^ 2);
        }
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_draw(o);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * Object 82 - pillar that falls when stood on
 * ---------------------------------------------------------------------- */
#define FP_Y0    0x30
#define FP_X0    0x34
#define FP_TIMER 0x36
#define FP_SAG   0x38
#define FP_ANG   0x3E
#define FP_TOUCH 0x3F

static void falling_pillar(uint32_t o)
{
    static const uint8_t props[4][2] = { { 0x20, 8 }, { 0x1C, 0x30 }, { 0x10, 0x10 }, { 0x10, 0x10 } };
    if (!OB8(o, OBJ_routine)) {                         /* Obj82_Init */
        SET8(o, OBJ_routine, 2);
        int k = (OB8(o, OBJ_subtype) >> 4) & 7;
        o_init(o, S2A_ARZBLOCK, OB8(o, OBJ_render) | 4, 3, props[k & 3][0], props[k & 3][1]);
        SET8(o, OBJ_y_radius, props[k & 3][1]);
        SET8(o, OBJ_frame, k);
        SET16(o, FP_X0, OB16(o, OBJ_x_pos));
        SET16(o, FP_Y0, OB16(o, OBJ_y_pos));
        uint8_t t = OB8(o, OBJ_subtype) & 0x0F;
        if (t && t != 7) SET8(o, FP_SAG, 1);
        SET8(o, OBJ_subtype, t);
    }
    uint16_t oldx = OB16(o, OBJ_x_pos);
    switch (OB8(o, OBJ_subtype)) {
    case 1: case 3:                                     /* wait to be stood on, then $1E frames */
        if (!OB16(o, FP_TIMER)) {
            if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) SET16(o, FP_TIMER, 0x1E);
        } else {
            SET16(o, FP_TIMER, OB16(o, FP_TIMER) - 1);
            if (!OB16(o, FP_TIMER)) { SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1); SET8(o, FP_SAG, 0); }
        }
        break;
    case 2: case 6: {                                   /* fall to the floor */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + 8);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 + 1);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_subtype, 0);
        }
        break;
    }
    case 4: {                                           /* rise to the ceiling */
        o_move(o);
        SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) - 8);
        int16_t d1 = o_ceiling_dist(o);
        if (d1 < 0) {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - d1);
            SET16(o, OBJ_y_vel, 0);
            SET8(o, OBJ_subtype, 0);
        }
        break;
    }
    case 5:                                             /* pushed from the side */
        if (OB8(o, FP_TOUCH) & 3) { SET8(o, OBJ_subtype, OB8(o, OBJ_subtype) + 1); SET8(o, FP_SAG, 0); }
        break;
    case 7: {                                           /* float on the water */
        int16_t d0 = (int16_t)(r16(RAM_Water_level) - OB16(o, OBJ_y_pos));
        if (!d0) break;
        if (d0 < 0) {
            if (d0 < -2) d0 = -2;
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
            int16_t d1 = o_ceiling_dist(o);
            if (d1 < 0) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) - d1);
        } else {
            if (d0 > 2) d0 = 2;
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d0);
            int16_t d1 = o_floor_dist(o);
            if (d1 < 0) SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1 + 1);
        }
        break;
    }
    default: break;
    }
    if (OB8(o, OBJ_render) & 0x80) {
        o_solid(o, OB8(o, OBJ_width_px) + 0x0B, OB8(o, OBJ_y_radius), OB8(o, OBJ_y_radius) + 1, oldx);
        SET8(o, FP_TOUCH, (uint8_t)(g_o_d6 >> 16));
        int moved = 0;                                  /* loc_2A432: sag under a player */
        if (OB8(o, FP_SAG)) {
            if (OB8(o, OBJ_status) & (ST_P1_STAND | ST_P2_STAND)) {
                if (OB8(o, FP_ANG) != 0x40) { SET8(o, FP_ANG, OB8(o, FP_ANG) + 4); moved = 1; }
            } else if (OB8(o, FP_ANG)) {
                SET8(o, FP_ANG, OB8(o, FP_ANG) - 4);
                moved = 1;
            }
        }
        if (moved) {
            int16_t s, c;
            o_sincos(OB8(o, FP_ANG), &s, &c);
            SET16(o, OBJ_y_pos, (uint16_t)(((s * 0x400) >> 16) + OB16(o, FP_Y0)));
        }
    }
    if (o_out_of_range(o, OB16(o, FP_X0))) { o_delete_respawnable(o); return; }
    o_draw(o);
}

/* ------------------------------------------------------------------------
 * Object 83 - three platforms on chains rotating round a point
 * ---------------------------------------------------------------------- */
#define RT_LASTX  0x40                /* Obj83_last_x_pos (objoff_2C) */
#define RT_SPEED  0x42                /* Obj83_speed (objoff_2E) */
#define RT_X0     0x30
#define RT_Y0     0x32
#define RT_CHAIN  0x34
#define RT_PLAT2  0x38
#define RT_PLAT3  0x3C

static uint32_t rot_platform_new(uint32_t o)            /* Obj83_LoadSubObject */
{
    uint32_t a1 = o_alloc_after(o);
    if (!a1) return 0;
    SET32(a1, OBJ_code, S2(0x83));
    SET8(a1, OBJ_routine, 4);
    o_init(a1, S2A_ARZSWING, 4, 4, 0x20, 0x10);
    SET16(a1, RT_X0, OB16(o, OBJ_x_pos));
    SET16(a1, RT_Y0, OB16(o, OBJ_y_pos));
    SET16(a1, RT_LASTX, OB16(o, OBJ_x_pos));
    return a1;
}

static void rot_spokes(uint32_t ch, int *k, uint8_t ang, int links, int16_t x0, int16_t y0,
                       int16_t *px, int16_t *py)
{
    int16_t s, c;
    o_sincos(ang, &s, &c);
    int32_t ds = (int32_t)s << 12, dc = (int32_t)c << 12;
    for (int i = 1; i <= 3; i++)
        if (i > 3 - links && ch) ms_child(ch, (*k)++, (int16_t)(x0 + ((dc * i) >> 16)), (int16_t)(y0 + ((ds * i) >> 16)), 1);
    *px = (int16_t)(x0 + ((dc * 4) >> 16));
    *py = (int16_t)(y0 + ((ds * 4) >> 16));
}

static void rot_platforms(uint32_t o)
{
    if (OB8(o, OBJ_render) & 0x40) { o_draw(o); return; }  /* the chains */
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj83_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_ARZSWING, 4, 4, 0x20, 0x10);
        SET16(o, RT_X0, OB16(o, OBJ_x_pos));
        SET16(o, RT_Y0, OB16(o, OBJ_y_pos));
        SET16(o, RT_SPEED, (uint16_t)((int16_t)(int8_t)(OB8(o, OBJ_subtype) & 0xF0) << 3));
        SET8(o, OBJ_angle, (uint8_t)(((OB8(o, OBJ_status) >> 2) | (OB8(o, OBJ_status) << 6)) & 0xC0));
        uint32_t ch = ms_new(o, S2A_ARZSWING, 0x40, 0x40, 8);
        SET32(o, RT_CHAIN, ch);
        if (ch) {
            SET32(o, RT_PLAT2, rot_platform_new(o));
            SET32(o, RT_PLAT3, rot_platform_new(o));
        }
    }   /* fall through */
    case 2: {                                           /* Obj83_Main */
        uint16_t oldx = OB16(o, OBJ_x_pos);
        SET16(o, OBJ_angle, OB16(o, OBJ_angle) + OB16(o, RT_SPEED));
        int16_t y0 = (int16_t)OB16(o, RT_Y0), x0 = (int16_t)OB16(o, RT_X0);
        uint32_t ch = r32(o + RT_CHAIN) & 0xFFFFFF;
        uint8_t ang = OB8(o, OBJ_angle);
        int k = 0;
        int16_t px, py;
        if (ch) {                                       /* first spoke: main sprite + 2 children */
            int16_t s, c;
            o_sincos(ang, &s, &c);
            SET16(ch, OBJ_x_pos, (uint16_t)(x0 + (((int32_t)c << 12) >> 16)));
            SET16(ch, OBJ_y_pos, (uint16_t)(y0 + (((int32_t)s << 12) >> 16)));
        }
        rot_spokes(ch, &k, ang, 2, x0, y0, &px, &py);
        SET16(o, OBJ_x_pos, (uint16_t)px);
        SET16(o, OBJ_y_pos, (uint16_t)py);
        static const uint8_t third = 256 / 3;
        uint32_t p2 = r32(o + RT_PLAT2) & 0xFFFFFF, p3 = r32(o + RT_PLAT3) & 0xFFFFFF;
        rot_spokes(ch, &k, (uint8_t)(ang + third), 3, x0, y0, &px, &py);
        if (p2) { SET16(p2, OBJ_x_pos, (uint16_t)px); SET16(p2, OBJ_y_pos, (uint16_t)py); }
        rot_spokes(ch, &k, (uint8_t)(ang - third), 3, x0, y0, &px, &py);
        if (p3) { SET16(p3, OBJ_x_pos, (uint16_t)px); SET16(p3, OBJ_y_pos, (uint16_t)py); }
        o_platform(o, OB8(o, OBJ_width_px) + 0x0B, 9, oldx);
        if (o_out_of_range(o, OB16(o, RT_X0))) {
            if (ch) o_delete(ch);
            o_delete_respawnable(o);
            return;
        }
        o_draw(o);
        return;
    }
    default:                                            /* Obj83_PlatformSubObject */
        o_platform(o, OB8(o, OBJ_width_px) + 0x0B, 9, OB16(o, RT_LASTX));
        SET16(o, RT_LASTX, OB16(o, OBJ_x_pos));
        if (o_out_of_range(o, OB16(o, RT_X0))) { o_delete(o); return; }
        o_draw(o);
        return;
    }
}

/* ------------------------------------------------------------------------
 * Object 8C - Whisp (blowfly)
 * ---------------------------------------------------------------------- */
#define WH_TIMER   0x30               /* obj8C_timer (objoff_2A) */
#define WH_ATTACKS 0x31               /* obj8C_attacks_remaining (objoff_2B) */

static void cap_speed(uint32_t o, int16_t mx, int16_t my)   /* Obj_CapSpeed */
{
    int16_t xv = (int16_t)OB16(o, OBJ_x_vel), yv = (int16_t)OB16(o, OBJ_y_vel);
    if (xv < 0) { if ((uint16_t)xv < (uint16_t)-mx) xv = (int16_t)-mx; }
    else if ((uint16_t)xv > (uint16_t)mx) xv = mx;
    if (yv < 0) { if ((uint16_t)yv < (uint16_t)-my) yv = (int16_t)-my; }
    else if ((uint16_t)yv > (uint16_t)my) yv = my;
    SET16(o, OBJ_x_vel, (uint16_t)xv);
    SET16(o, OBJ_y_vel, (uint16_t)yv);
}

static void whisp_attack(uint32_t o)                 /* loc_36970 */
{
    SET8(o, WH_ATTACKS, OB8(o, WH_ATTACKS) - 1);
    if ((int8_t)OB8(o, WH_ATTACKS) < 0) {               /* done: fly away */
        SET8(o, OBJ_routine, 8);
        SET8(o, OBJ_status, OB8(o, OBJ_status) & ~1);
        SET16(o, OBJ_x_vel, (uint16_t)-0x200);
        SET16(o, OBJ_y_vel, (uint16_t)-0x200);
        return;
    }
    SET8(o, OBJ_routine, 4);
    SET16(o, OBJ_y_vel, (uint16_t)-0x100);
    SET8(o, WH_TIMER, 96);
}

static void whisp(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj8C_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_WHISP, OB8(o, OBJ_render) | 4, 4, 0x0C, 0x0C);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 0x0B);
        SET8(o, WH_TIMER, 0x10);
        SET8(o, WH_ATTACKS, 4);
        return;
    case 2:                                             /* wait until on screen */
        if (OB8(o, OBJ_render) & 0x80) whisp_attack(o);
        break;
    case 6:                                             /* hover a while */
        SET8(o, WH_TIMER, OB8(o, WH_TIMER) - 1);
        if ((int8_t)OB8(o, WH_TIMER) < 0) whisp_attack(o);
        break;
    default: break;
    }
    if (OB8(o, OBJ_routine) == 4) {                     /* Obj8C_ChasePlayer */
        SET8(o, WH_TIMER, OB8(o, WH_TIMER) - 1);
        if ((int8_t)OB8(o, WH_TIMER) < 0) {
            SET8(o, OBJ_routine, 6);
            o_random();
            SET8(o, WH_TIMER, r8(RAM_RNG_seed + 3) & 0x1F);
            SET16(o, OBJ_x_vel, 0);
            SET16(o, OBJ_y_vel, 0);
        } else {
            static const int16_t delta[2] = { -0x10, 0x10 };
            int d0, d1;
            s2_orient(o, &d0, &d1, NULL, NULL);
            SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | (d0 ? 1 : 0));
            SET16(o, OBJ_x_vel, OB16(o, OBJ_x_vel) + delta[d0 >> 1]);
            SET16(o, OBJ_y_vel, OB16(o, OBJ_y_vel) + delta[d1 >> 1]);
            cap_speed(o, 0x200, 0x200);
            o_move(o);
        }
    } else if (OB8(o, OBJ_routine) == 8) {              /* Obj8C_FlyAway */
        o_move(o);
    }
    o_animate_s2(o, o_asset_anim(S2A_WHISP));
    o_touch(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Objects 8D/8E - Grounder (8D bursts out of a wall), 8F its wall pieces,
 * 90 the rocks it throws
 * ---------------------------------------------------------------------- */
#define GD_TIMER  0x30                /* objoff_2A */
#define GD_OUT    0x31                /* objoff_2B: burst out (the wall pieces watch it) */
#define GD_PARENT 0x40                /* pieces: objoff_2C */
#define GD_DIR    0x42                /* pieces: objoff_2E */

static void grounder_bits(uint32_t o, uint8_t id, int count, int asset)
{
    static const int8_t wall_off[4][2] = { { 0, -0x14 }, { 0x10, -4 }, { 0, 0x0C }, { -0x10, -4 } };
    for (int k = 0; k < count; k++) {
        uint32_t a1 = o_alloc();
        if (!a1) return;
        SET32(a1, OBJ_code, S2(id));
        SET16(a1, GD_PARENT, (uint16_t)o);
        SET16(a1, GD_DIR, (uint16_t)(k * 2));
        SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos));
        SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos));
        if (id == 0x8F) {
            SET16(a1, OBJ_x_pos, OB16(a1, OBJ_x_pos) + wall_off[k][0]);
            SET16(a1, OBJ_y_pos, OB16(a1, OBJ_y_pos) + wall_off[k][1]);
        }
        (void)asset;
    }
}

static void grounder(uint32_t o)
{
    int16_t dx;
    switch (OB8(o, OBJ_routine)) {
    case 0: {                                           /* Obj8D_Init */
        o_init(o, S2A_GROUNDER, OB8(o, OBJ_render) | 4, 5, 0x10, 0x14);
        SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) | 0x8000);
        SET8(o, OBJ_coll_flags, 2);
        SET8(o, OBJ_routine, 2);
        if (OB8(o, OBJ_render) & 2) {                   /* placed y-flipped: behind the level */
            SET8(o, OBJ_render, OB8(o, OBJ_render) & ~2);
            SET8(o, OBJ_status, OB8(o, OBJ_status) & ~2);
            SET16(o, OBJ_art_tile, OB16(o, OBJ_art_tile) & 0x7FFF);
        }
        SET8(o, OBJ_y_radius, 0x14);
        SET8(o, OBJ_x_radius, 0x10);
        int16_t d1 = o_floor_dist(o);
        if (d1 < 0) { SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1); SET16(o, OBJ_y_vel, 0); }
        if (r32(o) == S2(0x8D)) grounder_bits(o, 0x8F, 4, S2A_GRWALL);   /* loc_36C64 */
        else SET8(o, OBJ_routine, 6);
        return;
    }
    case 2:                                             /* waiting in the wall */
        s2_orient(o, NULL, NULL, &dx, NULL);
        if ((dx < 0 ? -dx : dx) <= 0x60) {
            SET8(o, OBJ_routine, 4);
            SET8(o, GD_OUT, 0xFF);
            grounder_bits(o, 0x90, 5, S2A_GRROCKS);     /* loc_36C2C */
        }
        o_touch(o);
        o_remember(o);
        return;
    case 4:                                             /* Obj8D_Animate: bursting out */
        o_animate_s2(o, o_asset_anim(S2A_GROUNDERB));
        break;
    case 6: {                                           /* turn toward the player */
        int d0;
        SET8(o, OBJ_routine, 8);
        s2_orient(o, &d0, NULL, NULL, NULL);
        SET16(o, OBJ_x_vel, d0 ? 0x100 : (uint16_t)-0x100);
        SET8(o, OBJ_status, (OB8(o, OBJ_status) & ~1) | (d0 ? 1 : 0));
        break;
    }
    case 8: {                                           /* walk along the floor */
        o_move(o);
        int16_t d1 = o_floor_dist(o);
        if (d1 < -1 || d1 >= 0x0C) {
            SET8(o, OBJ_routine, 0x0A);
            SET8(o, GD_TIMER, 0x3B);
        } else {
            SET16(o, OBJ_y_pos, OB16(o, OBJ_y_pos) + d1);
            o_animate_s2(o, o_asset_anim(S2A_GROUNDER));
        }
        break;
    }
    default:                                            /* pause at an edge, then turn */
        SET8(o, GD_TIMER, OB8(o, GD_TIMER) - 1);
        if ((int8_t)OB8(o, GD_TIMER) < 0) {
            SET8(o, OBJ_routine, 8);
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
            SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
        }
        break;
    }
    o_touch(o);
    o_remember(o);
}

static void grounder_wall(uint32_t o)                /* Obj8F */
{
    static const int8_t dirs[4][2] = { { 1, -2 }, { 1, -1 }, { -1, -2 }, { -1, -1 } };
    switch (OB8(o, OBJ_routine)) {
    case 0:
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_GRWALL, 0x84, 4, 0x10, 0x10);
        return;
    case 2: {
        uint32_t p = 0xFF0000u | OB16(o, GD_PARENT);
        if (OB8(p, GD_OUT) && r32(p) == S2(0x8D)) {
            SET8(o, OBJ_routine, 4);
            int k = (OB16(o, GD_DIR) >> 1) & 3;
            SET16(o, OBJ_x_vel, (uint16_t)(dirs[k][0] << 8));
            SET16(o, OBJ_y_vel, (uint16_t)(dirs[k][1] << 8));
        }
        o_remember(o);
        return;
    }
    default:                                            /* Obj8F_Move */
        if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }
        o_fall(o);
        o_remember(o);
        return;
    }
}

static void grounder_rock(uint32_t o)                /* Obj90 */
{
    static const int8_t dirs[5][2] = { { -1, -4 }, { 4, -3 }, { 2, 0 }, { -3, -1 }, { -3, -3 } };
    static const uint8_t frames[6] = { 0, 2, 0, 1, 0, 0 };
    if (!OB8(o, OBJ_routine)) {                         /* Obj90_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_GRROCKS, 0x84, 4, 8, 8);
        int k = (OB16(o, GD_DIR) >> 1);
        if (k > 4) k = 4;
        SET16(o, OBJ_x_vel, (uint16_t)(dirs[k][0] << 8));
        SET16(o, OBJ_y_vel, (uint16_t)(dirs[k][1] << 8));
        SET8(o, OBJ_frame, frames[k]);
        o_remember(o);
        return;
    }
    if (!(OB8(o, OBJ_render) & 0x80)) { o_delete(o); return; }   /* Obj90_Move */
    o_fall(o);
    o_remember(o);
}

/* ------------------------------------------------------------------------
 * Object 91 - Chop Chop (piranha)
 * ---------------------------------------------------------------------- */
#define CC_MOVE   0x30                /* Obj91_move_timer (objoff_2A, word; byte-used later) */
#define CC_BUBBLE 0x32                /* Obj91_bubble_timer (objoff_2C, word; counts its high byte) */

static void chopchop(uint32_t o)
{
    switch (OB8(o, OBJ_routine)) {
    case 0:                                             /* Obj91_Init */
        SET8(o, OBJ_routine, 2);
        o_init(o, S2A_CHOPCHOP, OB8(o, OBJ_render) | 4, 4, 0x10, 0x10);
        SET8(o, OBJ_coll_flags, 2);
        SET16(o, CC_MOVE, 0x200);
        SET16(o, CC_BUBBLE, 0x50);
        SET16(o, OBJ_x_vel, (OB8(o, OBJ_status) & 1) ? 0x40 : (uint16_t)-0x40);
        return;
    case 2: {                                           /* Obj91_Main: patrol */
        SET8(o, CC_BUBBLE, OB8(o, CC_BUBBLE) - 1);
        if (!OB8(o, CC_BUBBLE)) {                       /* Obj91_MakeBubble */
            SET16(o, CC_BUBBLE, 0x50);
            uint32_t a1 = o_alloc();
            if (a1) {
                SET32(a1, OBJ_code, S3K_Obj_Bubbler);   /* a small rising bubble */
                SET8(a1, OBJ_subtype, 0);
                SET16(a1, OBJ_x_pos, OB16(o, OBJ_x_pos) + ((OB8(o, OBJ_render) & 1) ? -0x14 : 0x14));
                SET16(a1, OBJ_y_pos, OB16(o, OBJ_y_pos) + 6);
            }
        }
        SET16(o, CC_MOVE, OB16(o, CC_MOVE) - 1);
        if ((int16_t)OB16(o, CC_MOVE) < 0) {
            SET16(o, CC_MOVE, 0x200);
            SET8(o, OBJ_status, OB8(o, OBJ_status) ^ 1);
            SET8(o, OBJ_render, OB8(o, OBJ_render) ^ 1);
            SET16(o, OBJ_x_vel, (uint16_t)-(int16_t)OB16(o, OBJ_x_vel));
        }
        o_move(o);
        int16_t d2, d3;
        s2_orient(o, NULL, NULL, &d2, &d3);
        int charge = 0;                                 /* Obj91_TestCharacterPos */
        if ((uint16_t)(d3 + 0x20) < 0x40) {
            int16_t xv = (int16_t)OB16(o, OBJ_x_vel);
            if (d2 >= 0 ? xv < 0 : xv >= 0) {
                uint16_t ad = (uint16_t)(d2 < 0 ? -d2 : d2);
                charge = ad >= 0x20 && ad < 0xA0;
            }
        }
        if (charge) {                                   /* Obj91_PrepareCharge */
            SET8(o, OBJ_routine, 4);
            SET8(o, CC_MOVE, 0x10);
            SET16(o, OBJ_x_vel, 0);
        }
        o_touch(o);
        o_remember(o);
        return;
    }
    case 4:                                             /* Obj91_Waiting */
        SET8(o, CC_MOVE, OB8(o, CC_MOVE) - 1);
        if ((int8_t)OB8(o, CC_MOVE) < 0) {              /* Obj91_MoveTowardsPlayer */
            int d0, d1;
            int16_t d3;
            SET8(o, OBJ_routine, 6);
            s2_orient(o, &d0, &d1, NULL, &d3);
            SET8(o, OBJ_x_vel, d0 ? 2 : (uint8_t)-2);
            if ((uint16_t)(d3 + 0x10) >= 0x20) SET8(o, OBJ_y_vel + 1, 0x80);
        }
        break;
    default:                                            /* Obj91_Charge */
        o_move(o);
        break;
    }
    o_animate_s2(o, o_asset_anim(S2A_CHOPCHOP));
    o_touch(o);
    o_remember(o);
}

void s2_obj_arz_register(TriObjFn *t)
{
    t[0x15] = swing;
    t[0x1F] = collapse;
    t[0x22] = arrow_shooter;
    t[0x23] = pillar_drop;
    t[0x2B] = rising_pillar;
    t[0x2C] = leaves;
    t[0x82] = falling_pillar;
    t[0x83] = rot_platforms;
    t[0x8C] = whisp;
    t[0x8D] = grounder;
    t[0x8E] = grounder;
    t[0x8F] = grounder_wall;
    t[0x90] = grounder_rock;
    t[0x91] = chopchop;
}
