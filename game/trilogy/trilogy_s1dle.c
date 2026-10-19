/*
 * trilogy_s1dle.c — Sonic 1 DynamicLevelEvents (camera limits, boss
 * triggers), transcribed. S1 -> S3K variables:
 *   v_limitbtm1 = Camera_target_max_Y_pos   v_limitbtm2 = Camera_max_Y_pos
 *   v_limittop2 = Camera_min_Y_pos          v_limitleft2 = Camera_min_X_pos
 *   v_screenposx/y = Camera_X/Y_pos         v_dle_routine = Dynamic_resize_routine
 *   f_lockscreen = Scroll_lock
 */
#include "trilogy_obj.h"

#define MUS_BOSS  0x19          /* S3K boss theme (player's choice over S1's) */

static uint16_t camx(void) { return r16(RAM_Camera_X_pos); }
static uint16_t camy(void) { return r16(RAM_Camera_Y_pos); }
static void btm1(uint16_t v) { w16(RAM_Camera_target_max_Y_pos, v); }
static uint8_t rt(void) { return r8(RAM_Dynamic_resize_routine); }
static void rt_add(int d) { w8(RAM_Dynamic_resize_routine, (uint8_t)(rt() + d)); }

static void spawn_boss(int id, int x, int y)
{
    uint32_t a1 = o_alloc();
    if (a1) {
        SET32(a1, OBJ_code, NATIVE_S1_BASE + (uint32_t)id * 4u);
        if (x >= 0) { SET16(a1, OBJ_x_pos, x); SET16(a1, OBJ_y_pos, y); }
    }
}

/* S1 f_lockscreen only changes music handling and Sonic's right-edge leeway;
 * the arena itself is held by the level limits. S3K's Scroll_lock would
 * freeze the camera (fatal for the LZ chase up the shaft), so it isn't set. */
static void boss_start(void)
{
    D(0) = MUS_BOSS;
    tri_jsr(S3K_Play_Music);
}

static void lock_left(void) { w16(RAM_Camera_min_X_pos, camx()); }

static void dle_ghz(int act)
{
    switch (act) {
    case 0:
        btm1(0x300);
        if (camx() >= 0x1780) btm1(0x400);
        return;
    case 1:
        btm1(0x300);
        if (camx() < 0xED0) return;
        btm1(0x200);
        if (camx() < 0x1600) return;
        btm1(0x400);
        if (camx() < 0x1D60) return;
        btm1(0x300);
        return;
    default:
        switch (rt()) {
        case 0:
            btm1(0x300);
            if (camx() < 0x380) return;
            btm1(0x310);
            if (camx() < 0x960) return;
            if (camy() >= 0x280) {
                btm1(0x400);
                if (camx() < 0x1380) {
                    btm1(0x4C0);
                    w16(RAM_Camera_max_Y_pos, 0x4C0);
                }
                if (camx() < 0x1700) return;
            }
            btm1(0x300);                      /* boss_ghz_y */
            rt_add(2);
            return;
        case 2:
            if (camx() < 0x960) rt_add(-2);
            if (camx() < 0x2960) return;
            spawn_boss(0x3D, 0x2960 + 0x100, 0x300 - 0x80);
            boss_start();
            rt_add(2);
            return;
        default:
            lock_left();
            return;
        }
    }
}

static void dle_mz(int act)
{
    if (act == 1) {
        btm1(0x520);
        if (camx() >= 0x1700) btm1(0x200);
        return;
    }
    if (act == 2) {
        if (rt() == 0) {
            btm1(0x720);
            if (camx() < 0x1800 - 0x2A0) return;
            btm1(0x210);
            if (camx() < 0x1800 - 0x10) return;
            spawn_boss(0x73, 0x1800 + 0x1F0, 0x210 + 0x1C);
            boss_start();
            rt_add(2);
        } else lock_left();
        return;
    }
    switch (rt()) {                           /* MZ1 */
    case 0:
        btm1(0x1D0);
        if (camx() < 0x700) return;
        btm1(0x220);
        if (camx() < 0xD00) return;
        btm1(0x340);
        if (camy() < 0x340) return;
        rt_add(2);
        return;
    case 2:
        if (camy() < 0x340) { rt_add(-2); return; }
        w16(RAM_Camera_min_Y_pos, 0);
        if (camx() >= 0xE00) return;
        w16(RAM_Camera_min_Y_pos, 0x340);
        btm1(0x340);
        if (camx() >= 0xA90) return;
        btm1(0x500);
        if (camy() < 0x370) return;
        rt_add(2);
        return;
    case 4:
        if (camy() < 0x370) { rt_add(-2); return; }
        if (camy() < 0x500) return;
        if (camx() < 0xB80) return;           /* REV01 */
        w16(RAM_Camera_min_Y_pos, 0x500);
        rt_add(2);
        return;
    default:
        if (camx() < 0xB80) {
            if (r16(RAM_Camera_min_Y_pos) != 0x340)
                w16(RAM_Camera_min_Y_pos, (uint16_t)(r16(RAM_Camera_min_Y_pos) - 2));
            return;
        }
        if (r16(RAM_Camera_min_Y_pos) != 0x500) {
            if (camy() < 0x500) return;
            w16(RAM_Camera_min_Y_pos, 0x500);
        }
        if (camx() < 0xE70) return;
        w16(RAM_Camera_min_Y_pos, 0);
        btm1(0x500);
        if (camx() < 0x1430) return;
        btm1(0x210);
        return;
    }
}

static void dle_lz(int act)
{
    if (act == 2) {                           /* LZ3 */
        extern uint8_t tri_lz_s1_chunk(int col, int row);
        extern void tri_lz3_patch(uint8_t chunk);
        if (r8(RAM_Level_trigger_array + 0x0F) && tri_lz_s1_chunk(6, 2) != 7) {
            tri_lz3_patch(7);                 /* switch $F: modify level layout */
            o_sfx(0x6F);                      /* sfx_Rumbling */
        }
        if (rt()) return;
        if (camx() < 0x1DE0 - 0x140) return;
        if (camy() >= 0xC0 + 0x540) return;
        spawn_boss(0x77, -1, 0);
        boss_start();
        rt_add(2);
        return;
    }
    if (act == 3) {                           /* SBZ3 */
        if (camx() < 0xD00) return;
        if (r16(RAM_Player_1 + OBJ_y_pos) >= 0x18) return;
        tri_ach_act_clear(0x2103);           /* SBZ3 ends here, without a tally */
        w8(RAM_Last_star_post_hit, 0);
        w16(RAM_Restart_level_flag, 1);
        w16(RAM_Current_zone_and_act, 0x2502);
        w16(RAM_Apparent_zone_and_act, tri_apparent(0x2502));
        w8(RAM_Ctrl_1_locked, 1);
    }
}

static void dle_slz(int act)
{
    if (act != 2) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2000 - 0x190) return;
        btm1(0x210);
        rt_add(2);
        return;
    case 2:
        if (camx() < 0x2000) return;
        spawn_boss(0x7A, -1, 0);
        boss_start();
        rt_add(2);
        return;
    default:
        lock_left();
    }
}

static void dle_syz(int act)
{
    if (act == 1) {
        btm1(0x520);
        if (camx() < 0x25A0) return;
        btm1(0x420);
        if (r16(RAM_Player_1 + OBJ_y_pos) >= 0x4D0) btm1(0x520);
        return;
    }
    if (act != 2) return;
    switch (rt()) {
    case 0:
        if (camx() < 0x2C00 - 0x140) return;
        spawn_boss(0x76, -1, 0);              /* blocks the boss picks up */
        rt_add(2);
        return;
    case 2:
        if (camx() < 0x2C00) return;
        btm1(0x4CC);
        spawn_boss(0x75, -1, 0);
        rt_add(2);
        boss_start();
        return;
    default:
        lock_left();
    }
}

static void dle_sbz(int act)
{
    if (act == 0) {
        btm1(0x720);
        if (camx() < 0x1880) return;
        btm1(0x620);
        if (camx() < 0x2000) return;
        btm1(0x2A0);
        return;
    }
    if (act == 1) {
        extern int tri_sbz2_open;
        if (tri_sbz2_open && r16(RAM_Camera_max_X_pos) < 0x2100)   /* Got_Boundary */
            w16(RAM_Camera_max_X_pos, (uint16_t)(r16(RAM_Camera_max_X_pos) + 2));
        /* Sonic_LevelBound: falling off the bottom past $2000 is the way to SBZ3 */
        uint16_t px = r16(RAM_Player_1 + OBJ_x_pos);
        w8(RAM_Disable_death_plane, px >= 0x2000);
        if (px >= 0x2000 && (int16_t)r16(RAM_Player_1 + OBJ_y_pos) > (int16_t)(r16(RAM_Camera_max_Y_pos) + 0xE0)
            && !r16(RAM_Restart_level_flag)) {
            extern int tri_sbz2_fall(void);
            if (tri_sbz2_fall()) return;              /* (its giant ring's special stage first) */
            w8(RAM_Last_star_post_hit, 0);
            w16(RAM_Restart_level_flag, 1);
            w16(RAM_Current_zone_and_act, 0x2103);
            w16(RAM_Apparent_zone_and_act, tri_apparent(0x2103));
            return;
        }
        switch (rt()) {
        case 0:
            btm1(0x800);
            if (camx() < 0x1800) return;
            btm1(0x510);                      /* boss_sbz2_y */
            if (camx() < 0x1E00) return;
            rt_add(2);
            return;
        case 2:
            if (camx() < 0x2050 - 0x1A0) return;
            spawn_boss(0x83, -1, 0);          /* false floor */
            rt_add(2);
            return;
        case 4:
            if (camx() >= 0x2050 - 0xF0) {
                spawn_boss(0x82, -1, 0);      /* SBZ2 Eggman */
                rt_add(2);

            }
            lock_left();
            return;
        default:
            if (camx() < 0x2050) lock_left();
            return;
        }
    }
    /* Final Zone */
    switch (rt()) {
    case 0:
        if (camx() >= 0x2450 - 0x308) rt_add(2);
        break;
    case 2:
        if (camx() >= 0x2450 - 0x150) {
            spawn_boss(0x85, -1, 0);
            rt_add(2);
        }
        break;
    case 4:
        if (camx() >= 0x2450) rt_add(2);
        break;
    case 6:
        return;
    default:
        break;
    }
    lock_left();
}

void s1_dynamic_level_events(void)
{
    int z = tri_zone(), act = tri_act();
    switch (z) {
    case TRI_S1_GHZ: dle_ghz(act); break;
    case TRI_S1_LZ:  dle_lz(act); break;
    case TRI_S1_MZ:  dle_mz(act); break;
    case TRI_S1_SLZ: dle_slz(act); break;
    case TRI_S1_SYZ: dle_syz(act); break;
    case TRI_S1_SBZ: dle_sbz(act); break;
    default: break;
    }
}
