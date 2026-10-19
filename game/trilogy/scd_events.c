/*
 * scd_events.c — Sonic CD's level events (the disc's DynamicLevelEvents,
 * the same code in every round's program: R72B [$203404], zone1chk..zone8chk).
 *
 * Every frame they set the camera's bottom target (destBottomBound $F726:
 * S3K's Camera_target_max_Y_pos); the bottom (bottomBound $F72E) then moves
 * to it, 4 px a frame, 16 falling near it (trilogy_screen.c's resize tail,
 * at the disc's speed). The level's own bottom (scd_level.py's bounds) is
 * only where it starts: Stardust Speedway act 2's past and futures start at
 * $510 and its events take them to $710 (the present's), the room their
 * lower half needs.
 *
 * Act 3's are with its boss's code (the arenas: scd_*_boss.c), except
 * Stardust Speedway's (the race: none of its own), here. Tidal Tempest act 2's
 * ($800 by its start, $710 past it) are left to the level's $800: S3K's
 * wrapping level (n_fg_init) has no bottom to move. Not here: anim $2B's
 * bottom at the camera (Sonic giving up, which this game has not).
 */
#include "trilogy_obj.h"

#define RAM_Distance_from_top  0xFFEE2Cu    /* (CD's $F73E: where the player sits on screen) */

enum { PPZ, CCZ, TTZ, QQZ, WWZ, SSZ, MMZ };

static void dest(uint16_t y) { w16(RAM_Camera_target_max_Y_pos, y); }

static void ppz1_present(void)                                 /* zone1chk act 1, timeZone 1 [$2034A4] */
{
    uint16_t px = OB16(RAM_Player_1, OBJ_x_pos);
    uint16_t cx = r16(RAM_Camera_X_pos);
    if (px >= 0x1C16 && px < 0x21C6) w16(RAM_Distance_from_top, 0x88);
    dest(0x710);
    if (cx < 0x840) return;
    if (r8(RAM_Update_HUD_timer) && r16(RAM_Camera_min_X_pos) < 0x820) {   /* (no way back) */
        w16(RAM_Camera_min_X_pos, 0x820);
        w16(RAM_Camera_target_min_X_pos, 0x820);
    }
    dest(0x410);
    if (cx >= 0xE00) dest(0x310);
}

static void ssz3(void)                                         /* zone7chk act 3 [$2036E8]: the race */
{
    uint16_t px = OB16(RAM_Player_1, OBJ_x_pos);
    if ((int16_t)px < 0x930 || (int16_t)px >= 0xDC0) { dest(0x210); return; }
    dest(0x120);
    int16_t d = (int16_t)(0x120 - r16(RAM_Camera_max_Y_pos));
    if ((d < 0 ? -d : d) < 4) w16(RAM_Camera_max_Y_pos, 0x120);
}

void tri_scd_level_events(void)
{
    int z = tri_zone();
    if (!tri_cd_zone(z)) return;
    int round = (z - TRI_CD_BASE) >> 2, period = (z - TRI_CD_BASE) & 3, act = tri_act();
    if (act == 2) {
        if (round == SSZ) ssz3();
        return;
    }
    switch (round) {
    case PPZ: if (act == 0 && period == 0) ppz1_present(); else dest(0x310); break;
    case CCZ: dest(0x510); break;
    case TTZ: if (act == 0) dest(0x510); break;
    case QQZ: dest(0x310); break;
    case WWZ: dest(0x710); break;                              /* (zone6chk: all acts; act 3's boss, its own) */
    case SSZ: dest(act ? 0x710 : 0x510); break;
    case MMZ: dest(0x710); break;
    }
}
