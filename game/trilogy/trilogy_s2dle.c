/*
 * trilogy_s2dle.c — Sonic 2 level events (LevelEventArray / LevEvents_*),
 * transcribed from s2disasm: boss arenas, camera limits, boss spawns.
 * Tails' separate limits (2P) don't exist in the S3K engine.
 */
#include "trilogy_obj.h"
#include "trilogy_assets_s2.h"
#include <string.h>
#include <stdlib.h>

#define MUS_BOSS  0x19          /* S3K boss theme */

extern int s2_boss_defeated_flag;
static uint8_t s_spawn_delay;   /* Boss_spawn_delay */

static uint16_t camx(void) { return r16(RAM_Camera_X_pos); }
static uint16_t camy(void) { return r16(RAM_Camera_Y_pos); }
static uint8_t rt(void) { return r8(RAM_Dynamic_resize_routine); }
static void rt_add(int d) { w8(RAM_Dynamic_resize_routine, (uint8_t)(rt() + d)); }

static uint32_t spawn(int id, int x, int y, int sub)
{
    uint32_t a1 = o_alloc();
    if (a1) {
        SET32(a1, OBJ_code, NATIVE_S2_BASE + (uint32_t)id * 4u);
        SET16(a1, OBJ_x_pos, (uint16_t)x);
        SET16(a1, OBJ_y_pos, (uint16_t)y);
        SET8(a1, OBJ_subtype, sub);
    }
    return a1;
}

static void ehz(int act)                                /* LevEvents_EHZ */
{
    if (!act) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2780) return;
        w16(RAM_Camera_min_X_pos, camx());
        w16(RAM_Camera_target_max_Y_pos, 0x390);
        rt_add(2);
        return;
    case 2:
        if (camx() < 0x28F0) return;
        w16(RAM_Camera_min_X_pos, 0x28F0);
        w16(RAM_Camera_max_X_pos, 0x2940);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        return;
    case 4:
        if (camy() >= 0x388) w16(RAM_Camera_min_Y_pos, 0x388);
        if (++s_spawn_delay < 0x5A) return;
        spawn(0x56, 0x29D0, 0x426, 0x81);
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:
        if (s2_boss_defeated_flag) w16(RAM_Camera_min_X_pos, camx());
        return;
    }
}

static void cpz(int act)                                /* LevEvents_CPZ (act 2 only) */
{
    if (!act) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2680) return;
        w16(RAM_Camera_min_X_pos, camx());
        w16(RAM_Camera_target_max_Y_pos, 0x450);
        rt_add(2);
        return;
    case 2:
        if (camx() < 0x2A20) return;
        w16(RAM_Camera_min_X_pos, 0x2A20);
        w16(RAM_Camera_max_X_pos, 0x2A20);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        return;
    case 4:
        if (camy() >= 0x448) w16(RAM_Camera_min_Y_pos, 0x448);
        if (++s_spawn_delay < 0x5A) return;
        spawn(0x5D, 0, 0, 0);
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:
        w16(RAM_Camera_min_X_pos, camx());
        return;
    }
}

/* CheckLoadSignpostArt: $100 short of the act's end the camera stops going
 * back and the signpost art comes in (over badnik art left behind) */
static void check_signpost_art(void)
{
    if (o_asset_ready(S2A_SIGNPOST)) return;            /* not deferred here, or already in */
    if (r16(RAM_Debug_placement_mode)) return;
    uint16_t d1 = (uint16_t)(r16(RAM_Camera_max_X_pos) - 0x100);
    if ((int16_t)r16(RAM_Camera_X_pos) < (int16_t)d1) return;
    if (!r8(RAM_Update_HUD_timer)) return;
    if (r16(RAM_Camera_min_X_pos) == d1) return;
    w16(RAM_Camera_min_X_pos, d1);
    o_upload_asset(S2A_SIGNPOST);
}

/* PalLoad_Now: into the palette now, and into the fade target so a fade
 * still running (arriving straight into an arena) doesn't undo it */
static void boss_palette(uint8_t pal)
{
    if (!pal) return;
    D(0) = pal;
    tri_jsr(S3K_LoadPalette);
    D(0) = pal;
    tri_jsr(0x003DDAu);                                 /* LoadPalette_Immediate */
}

/* Level_Layout writes: the FG (or BG) chunk at (chunk column, row); the FG
 * plane is redrawn (the BG is drawn whole every frame) */
void s2_layout_put(int cx, int cy, int bg, uint8_t chunk)
{
    uint16_t row = r16(RAM_Level_layout_header + 8 + (uint32_t)cy * 4u + (bg ? 2u : 0u));
    w8(0xFF0000u | (uint16_t)(row + cx), chunk);
    if (!bg) tri_refresh_fg();
}
static void layout_put(int cx, int cy, uint8_t chunk) { s2_layout_put(cx, cy, 0, chunk); }

/* Obj51 opens the arena's right wall when it's beaten (Level_Layout+$C54) */
void s2_cnz_boss_wall(uint8_t chunk) { layout_put(0x54, 0x0C, chunk); }

static void cnz(int act)                                /* LevEvents_CNZ (act 2 only) */
{
    extern uint8_t s2_current_boss;
    if (!act) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x27C0) return;
        w16(RAM_Camera_min_X_pos, camx());
        w16(RAM_Camera_target_max_Y_pos, 0x62E);
        layout_put(0x54, 0x0C, 0xF9);                   /* the arena's right wall */
        rt_add(2);
        return;
    case 2: {
        if (camx() < 0x2890) return;
        layout_put(0x50, 0x0C, 0xF9);                   /* and its left wall */
        w16(RAM_Camera_min_X_pos, 0x2860);
        w16(RAM_Camera_max_X_pos, 0x28E0);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        s2_current_boss = 6;
        uint32_t inf = tri_rd32(tri_dir() + TD_EXTRA);
        uint8_t pal = inf ? tri_rd8(inf + S2I_BOSS_PAL) : 0;
        boss_palette(pal);                              /* PalLoad_Now: PalID_CNZ_B */
        return;
    }
    case 4:
        if (camy() >= 0x4E0) w16(RAM_Camera_min_Y_pos, 0x4E0);
        if (++s_spawn_delay < 0x5A) return;
        spawn(0x51, 0, 0, 0);
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:
        if (camx() < 0x2A00) return;
        w16(RAM_Camera_target_max_Y_pos, 0x5D0);
        w16(RAM_Camera_min_X_pos, camx());
        return;
    }
}

/* ---- Hill Top (LevEvents_HTZ / LevEvents_HTZ2): the earthquake sections ---- */
extern uint16_t s2_htz_bg_yoff;
extern int16_t s2_htz_bg_xoff, s2_htz_bgx, s2_htz_bgy, s2_htz_dx, s2_htz_dy, s2_htz_delay;
extern uint8_t s2_htz_quake, s2_htz_dir, s2_screen_shaking;

static int htz_scroll_bg(int16_t d0, int16_t d1)    /* ScrollBG: steer plane B, 16px a frame at most */
{
    d0 = (int16_t)(d0 - s2_htz_bgx - s2_htz_bg_xoff);
    if (d0 < 0) { if (d0 <= -16) d0 = -16; }
    else if ((uint16_t)d0 >= 16) d0 = 16;
    s2_htz_dx = d0;
    d1 = (int16_t)(d1 - s2_htz_bgy - (int16_t)s2_htz_bg_yoff);
    if (d1 < 0) { if (d1 <= -16) d1 = -16; }
    else if ((uint16_t)d1 >= 16) d1 = 16;
    s2_htz_dy = d1;
    return d0 | d1;
}

static void htz_enter(int16_t yoff)                  /* the quake starts: plane B tracks the camera */
{
    s2_htz_quake = 1;
    s2_htz_bgx = (int16_t)camx();
    s2_htz_bgy = (int16_t)(camy() - 0x100);
    s2_htz_dx = s2_htz_dy = 0;
    s2_htz_bg_xoff = 0;
    s2_htz_bg_yoff = (uint16_t)yoff;
    s2_htz_delay = 0;
}

static void htz_exit(void)
{
    s2_htz_bgx = 0x400;
    s2_htz_bgy = 0;
    s2_htz_bg_xoff = 0;
    s2_htz_bg_yoff = 0;
    s2_htz_dir = 0;
}

static void htz_ease_back(void)                     /* _Part2: plane B settles, then back to scenery */
{
    if (!s2_htz_quake) return;
    s2_htz_dx = s2_htz_dy = 0;
    if (!htz_scroll_bg(0x200, 0)) s2_htz_quake = 0;
}

static void htz_shake(uint16_t lo, uint16_t hi)     /* .keep_shaking: rise, rest, sink, rest */
{
    uint16_t f = r16(RAM_Level_frame_counter);
    if (s2_htz_bg_yoff != (s2_htz_dir ? lo : hi)) {
        if (f & 3) return;
        s2_htz_bg_yoff = (uint16_t)(s2_htz_bg_yoff + (s2_htz_dir ? -1 : 1));
        if (!(f & 0x3F)) o_sfx_s2(0xE1);                /* SndID_Rumbling2 */
        return;
    }
    s2_screen_shaking = 0;                              /* .flip_delay */
    if (--s2_htz_delay >= 0) return;
    s2_htz_delay = 0x78;
    s2_htz_dir ^= 1;
    s2_screen_shaking = 1;
}

/* the quake section's own camera, or leaving it on either side */
static void htz_follow(uint16_t left, uint16_t right, int back, int on)
{
    if (camx() < left) { htz_exit(); rt_add(-back); return; }
    if (camx() >= right) { htz_exit(); rt_add(on); return; }
    htz_scroll_bg((int16_t)camx(), (int16_t)camy());
}

static void htz(int act)
{
    if (!act) {
        switch (rt()) {
        case 0:                                         /* left of the earthquake */
            if (camy() >= 0x400 && camx() >= 0x1800) { htz_enter(320); rt_add(2); return; }
            htz_ease_back();
            return;
        case 2:                                         /* the earthquake */
            if (camx() >= 0x1978) {
                if (camx() >= 0x1E00) s2_screen_shaking = 0;
                else htz_shake(224, 320);
            }
            htz_follow(0x1800, 0x1F00, 2, 2);
            return;
        default:                                        /* right of it */
            if (camx() < 0x1F00) { htz_enter(320); rt_add(-2); return; }
            htz_ease_back();
            return;
        }
    }
    if (camx() >= 0x2B00 && rt() < 0x0A) {              /* LevEvents_HTZ2_Prepare */
        w8(RAM_Dynamic_resize_routine, 0x0A);
        s2_htz_quake = 0;
    }
    switch (rt()) {
    case 0:                                             /* earthquake left */
        if (camx() < 0x14C0) { htz_ease_back(); return; }
        htz_enter(0x2C0);
        rt_add(2);
        if (camy() >= 0x380) {                          /* the lower route */
            s2_htz_bg_xoff = -0x680;
            s2_htz_bgx += 0x480;
            s2_htz_bg_yoff = 0x300;
            rt_add(6);
        }
        return;
    case 2:                                             /* earthquake (top) */
        if (camx() >= 0x1678) {
            if (camx() >= 0x1A00) s2_screen_shaking = 0;
            else htz_shake(0, 0x2C0);
        }
        htz_follow(0x14C0, 0x1B00, 2, 2);
        return;
    case 4:                                             /* right of it (top) */
        if (camx() < 0x1B00) { htz_enter(0x2C0); rt_add(-2); return; }
        htz_ease_back();
        return;
    case 6:                                             /* earthquake (bottom) */
        if (camx() >= 0x15F0 && camx() < 0x1AC0) htz_shake(0, 0x300);
        htz_follow(0x14C0, 0x1B00, 6, 2);
        return;
    case 8:                                             /* right of it (bottom) */
        if (camx() < 0x1B00) {
            htz_enter(0x300);
            s2_htz_bg_xoff = -0x680;
            s2_htz_bgx += 0x480;
            rt_add(-2);
            return;
        }
        htz_ease_back();
        return;
    case 0x0A:                                          /* boss area cut-off */
        if (camx() < 0x2C50) return;
        w16(RAM_Camera_min_X_pos, camx());
        w16(RAM_Camera_target_max_Y_pos, 0x480);
        rt_add(2);
        return;
    case 0x0C: {                                        /* boss area camera */
        extern uint8_t s2_current_boss;
        if (camx() < 0x2EDF) return;
        w16(RAM_Camera_min_X_pos, 0x2EE0);
        w16(RAM_Camera_max_X_pos, 0x2F5E);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        s2_current_boss = 3;
        return;
    }
    case 0x0E:
        if (camy() >= 0x478) w16(RAM_Camera_min_Y_pos, 0x478);
        if (++s_spawn_delay < 0x5A) return;
        spawn(0x52, 0, 0, 0);
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:                                            /* boss end: the camera opens up */
        if (!s2_boss_defeated_flag) return;
        w16(RAM_Camera_min_X_pos, camx());
        if (camx() < 0x30E0) return;
        if (r16(RAM_Camera_min_Y_pos) >= 0x428) w16(RAM_Camera_min_Y_pos, r16(RAM_Camera_min_Y_pos) - 2);
        if (r16(RAM_Camera_target_max_Y_pos) >= 0x430)
            w16(RAM_Camera_target_max_Y_pos, r16(RAM_Camera_target_max_Y_pos) - 2);
        return;
    }
}

static void arz(int act)                                /* LevEvents_ARZ (act 2 only) */
{
    extern uint8_t s2_current_boss;
    if (!act) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2810) return;
        w16(RAM_Camera_min_X_pos, camx());
        w16(RAM_Camera_target_max_Y_pos, 0x400);
        rt_add(2);
        s2_current_boss = 4;                            /* (the waterfalls stop animating) */
        return;
    case 2:
        if (camx() < 0x2A40) return;
        w16(RAM_Camera_max_X_pos, 0x2A40);
        w16(RAM_Camera_min_X_pos, 0x2A40);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        spawn(0x89, 0, 0, 0);
        return;
    case 4:
        if (camy() >= 0x3F8) w16(RAM_Camera_min_Y_pos, 0x3F8);
        if (++s_spawn_delay < 0x5A) return;
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:
        w16(RAM_Camera_min_X_pos, camx());
        return;
    }
}

static void mcz(int act)                                /* LevEvents_MCZ (act 2 only) */
{
    extern uint8_t s2_current_boss, s2_screen_shaking;
    if (!act) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2080) return;
        w16(RAM_Camera_min_X_pos, camx());
        w16(RAM_Camera_target_max_Y_pos, 0x5D0);
        rt_add(2);
        return;
    case 2: {
        if (camx() < 0x20F0) return;
        w16(RAM_Camera_max_X_pos, 0x20F0);
        w16(RAM_Camera_min_X_pos, 0x20F0);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        s2_current_boss = 5;                            /* (ArtUnc_FallingRocks + PLC_MczBoss: Obj57's art) */
        uint32_t inf = tri_rd32(tri_dir() + TD_EXTRA);
        uint8_t pal = inf ? tri_rd8(inf + S2I_BOSS_PAL) : 0;
        boss_palette(pal);                              /* PalLoad_Now: PalID_MCZ_B */
        return;
    }
    case 4:
        if (camy() >= 0x5C8) w16(RAM_Camera_min_Y_pos, 0x5C8);
        if (++s_spawn_delay < 0x5A) return;
        spawn(0x57, 0, 0, 0);
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:
        if (s2_screen_shaking && !(r16(RAM_Level_frame_counter) & 0x1F)) {
            o_sfx_s2(0xE1);                             /* SndID_Rumbling2 */
        }
        w16(RAM_Camera_min_X_pos, camx());
        return;
    }
}

static void ooz(int act)                                /* LevEvents_OOZ (act 2 only) + the oil */
{
    extern uint8_t s2_current_boss;
    extern void s2_ooz_frame(void), s2_ooz_oil_y(uint16_t y);
    s2_ooz_frame();                                     /* Level: Obj07 */
    if (!act) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2668) return;
        w16(RAM_Camera_min_X_pos, camx());
        s2_ooz_oil_y(0x2D8);
        w16(RAM_Camera_target_max_Y_pos, 0x1E0);
        rt_add(2);
        return;
    case 2: {
        if (camx() < 0x2880) return;
        w16(RAM_Camera_min_X_pos, 0x2880);
        w16(RAM_Camera_max_X_pos, 0x28C0);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        s2_current_boss = 8;
        uint32_t inf = tri_rd32(tri_dir() + TD_EXTRA);
        boss_palette(inf ? tri_rd8(inf + S2I_BOSS_PAL) : 0);   /* PalLoad_Now: PalID_OOZ_B */
        return;
    }
    case 4:
        if (camy() >= 0x1D8) w16(RAM_Camera_min_Y_pos, 0x1D8);
        if (++s_spawn_delay < 0x5A) return;
        spawn(0x55, 0, 0, 0);
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:
        if (s2_boss_defeated_flag) w16(RAM_Camera_min_X_pos, camx());
        return;
    }
}

static void mtz(int act)                                /* LevEvents_MTZ3 (act 3 only) */
{
    extern uint8_t s2_current_boss;
    if (act != 2) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2530) return;
        w16(RAM_Camera_max_Y_pos, 0x500);
        w16(RAM_Camera_target_max_Y_pos, 0x450);
        rt_add(2);
        return;
    case 2:
        if (camx() < 0x2980) return;
        w16(RAM_Camera_min_X_pos, camx());
        w16(RAM_Camera_target_max_Y_pos, 0x400);
        rt_add(2);
        return;
    case 4:
        if (camx() < 0x2A80) return;
        w16(RAM_Camera_min_X_pos, 0x2AB0);
        w16(RAM_Camera_max_X_pos, 0x2AB0);
        rt_add(2);
        D(0) = 0xE1;                                    /* cmd_FadeOut */
        tri_jsr(S3K_Play_Music);
        s_spawn_delay = 0;
        s2_current_boss = 7;                            /* (PLC_MtzBoss: Obj54's art) */
        return;
    case 6:
        if (camy() >= 0x400) w16(RAM_Camera_min_Y_pos, 0x400);
        if (++s_spawn_delay < 0x5A) return;
        spawn(0x54, 0, 0, 0);
        rt_add(2);
        D(0) = MUS_BOSS;
        tri_jsr(S3K_Play_Music);
        return;
    default:
        w16(RAM_Camera_min_X_pos, camx());
        return;
    }
}

static void scz(int act)                                /* LevEvents_SCZ: the Tornado's course */
{
    extern int16_t s2_tornado_vx, s2_tornado_vy;
    (void)act;
    switch (rt()) {
    case 0:
        s2_tornado_vx = 1; s2_tornado_vy = 0;
        rt_add(2);
        return;
    case 2:
        if (camx() < 0x1180) return;
        s2_tornado_vx = -1; s2_tornado_vy = 1;
        w16(RAM_Camera_target_max_Y_pos, 0x500);
        rt_add(2);
        return;
    case 4:
        if (camy() < 0x500) return;
        s2_tornado_vx = 1; s2_tornado_vy = 0;
        rt_add(2);
        return;
    case 6:
        if (camx() < 0x1400) return;
        s2_tornado_vx = 0; s2_tornado_vy = 0;
        rt_add(2);
        return;
    default:
        return;
    }
}

/* LevEvents_WFZ: the BG camera trails the camera by Camera_BG_X/Y_offset
 * (the getaway ship's moves); WFZ_LevEvent_Subrout brings in the boss art
 * and locks the controls for the finale */
int16_t s2_wfz_bg_xoff, s2_wfz_bg_yoff;     /* Camera_BG_X_offset / _Y_offset (trilogy_s2bg.inc) */
uint8_t s2_wfz_bg_snap;                     /* Routine1: BG camera = camera */
static uint16_t s_wfz_bgy_speed;            /* WFZ_BG_Y_Speed */
static uint8_t s_wfz_sub;                   /* WFZ_LevEvent_Subrout */

static void wfz(void)
{
    switch (rt()) {
    case 0:
        s2_wfz_bg_xoff = s2_wfz_bg_yoff = 0;
        s2_wfz_bg_snap = 1;
        s_wfz_sub = 0;
        rt_add(2);
        break;
    case 2:
        if (camx() >= 0x2BC0 && camy() >= 0x580) {
            rt_add(2);
            s_wfz_bgy_speed = 0;
        }
        break;
    case 4:
        if (s2_wfz_bg_xoff != 0x800) s2_wfz_bg_xoff += 2;
        if (s2_wfz_bg_xoff >= 0x600) {
            uint16_t d0 = s_wfz_bgy_speed;
            if (d0 < 0x840) s_wfz_bgy_speed = d0 = (uint16_t)(d0 + 4);
            s2_wfz_bg_yoff = (int16_t)(s2_wfz_bg_yoff + (d0 >> 8));
        }
        break;
    case 6:                                             /* (set by the Tornado) */
        if (s2_wfz_bg_xoff == -0x2C0) break;
        s2_wfz_bg_xoff -= 2;
        if (s2_wfz_bg_yoff == 0x1B81) break;
        {
            uint16_t d0 = s_wfz_bgy_speed;
            if (d0) { s_wfz_bgy_speed = d0 = (uint16_t)(d0 - 4); d0 >>= 8; }
            s2_wfz_bg_yoff = (int16_t)(s2_wfz_bg_yoff + d0 + 1);
        }
        break;
    }
    if (s_wfz_sub == 0) {
        if (camx() >= 0x2880 && camy() >= 0x400) {
            s_wfz_sub = 2;
            o_upload_asset(S2A_WFZBOSS);                /* PLCID_WfzBoss */
            o_upload_asset(S2A_ROBOTNIK);
            o_upload_asset(S2A_ROBOPLAT);
            w16(RAM_Camera_min_X_pos, 0x2880);
        }
    } else if (s_wfz_sub == 2) {
        if (camy() >= 0x500) {
            s_wfz_sub = 4;
            w8(RAM_Ctrl_1_locked, 0xFF);
        }
    }
}

/* LevEvents_DEZ: Silver Sonic drops in; the robot's room locks */
static void dez(void)
{
    switch (rt()) {
    case 0:
        if (camx() < 320) return;
        rt_add(2);
        spawn(0xAF, 0x348, 0xA0, 0x48);                 /* Silver Sonic (+ PLCID_FieryExplosion) */
        return;
    case 4:                                             /* (Silver Sonic's defeat: rt 2 -> 4) */
        w16(RAM_Camera_min_X_pos, camx());
        if (camx() < 0x300) return;
        rt_add(2);                                      /* PLCID_DezBoss */
        return;
    case 6:
        w16(RAM_Camera_min_X_pos, camx());
        if (camx() < 0x680) return;
        rt_add(2);
        w16(RAM_Camera_min_X_pos, 0x680);
        w16(RAM_Camera_max_X_pos, 0x680 + 0xC0);
        return;
    default:
        return;
    }
}

/* InitPlayers: SCZ, WFZ and DEZ load no CPU Tails (he flies the Tornado) */
static void no_sidekick(void)
{
    static const uint32_t slots[] = { RAM_Player_2, 0xFFB000u + 95u * 0x4Au, 0xFFB000u + 97u * 0x4Au,
                                      0xFFB000u + 99u * 0x4Au, 0xFFB000u + 101u * 0x4Au };
    if (!r32(RAM_Player_2) || r16(RAM_Player_mode)) return;
    for (unsigned k = 0; k < sizeof slots / sizeof slots[0]; k++)
        for (uint32_t i = 0; i < OBJ_SIZE; i += 2) w16(slots[k] + i, 0);
}

void s2_dynamic_level_events(void)
{
    check_signpost_art();
    if (!r8(RAM_Dynamic_resize_routine) && !s_spawn_delay) {
        extern uint8_t s2_current_boss, s2_screen_shaking;
        s2_boss_defeated_flag = 0;
        s2_current_boss = 0;
        s2_screen_shaking = 0;
    }
    int act = tri_act();
    switch (tri_zone()) {
    case TRI_S2_EHZ: ehz(act); break;
    case TRI_S2_CPZ: { extern void s2_cpz_pylon(void); s2_cpz_pylon(); cpz(act); break; }
    case TRI_S2_ARZ: arz(act); break;
    case TRI_S2_CNZ: { extern void s2_cnz_frame(void); s2_cnz_frame(); cnz(act); break; }
    case TRI_S2_HTZ: htz(act); break;
    case TRI_S2_MCZ: mcz(act); break;
    case TRI_S2_OOZ: ooz(act); break;
    case TRI_S2_MTZ: mtz(act); break;
    case TRI_S2_SCZ: no_sidekick(); scz(act); break;
    case TRI_S2_WFZ: no_sidekick(); wfz(); break;
    case TRI_S2_DEZ: no_sidekick(); dez(); break;
    default: break;
    }
}
