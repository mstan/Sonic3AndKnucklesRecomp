/*
 * trilogy_player.c — per-frame player rules that belong to the zone's game.
 *
 * Sonic 1 loops and S-tunnels: S1 decides "behind the loop" from the 256px
 * chunk under the player plus x/angle (Sonic_Loops) and forces a roll in
 * tunnel chunks (Sonic_ChkRoll). Our converted chunks carry the front path as
 * primary collision and the behind path as secondary, so "behind" selects the
 * S3K player's secondary solidity bits.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"

#define SFX_ROLL 0x3C   /* S3K sfx_Roll */

static void s1_set_behind(uint32_t p, int behind)
{
    w8(p + OBJ_top_solid, behind ? 0x0E : 0x0C);
    w8(p + OBJ_lrb_solid, behind ? 0x0F : 0x0D);
}

static int s1_behind(uint32_t p) { return r8(p + OBJ_top_solid) == 0x0E; }

static void s1_chk_roll(uint32_t p)
{
    uint8_t st = r8(p + OBJ_status);
    if (st & 4)
        return;
    w8(p + OBJ_status, st | 4);
    w8(p + OBJ_y_radius, 0x0E);
    w8(p + OBJ_x_radius, 7);
    w8(p + OBJ_anim, 2);
    w16(p + OBJ_y_pos, (uint16_t)(r16(p + OBJ_y_pos) + 5));
    D(0) = SFX_ROLL;
    tri_jsr(S3K_Play_SFX);
    if (r16(p + OBJ_ground_vel) == 0)
        w16(p + OBJ_ground_vel, 0x200);
}

static void s1_loops(uint32_t p, uint32_t info)
{
    uint8_t loop1 = tri_rd8(info + 512), loop2 = tri_rd8(info + 513);
    uint8_t roll1 = tri_rd8(info + 514), roll2 = tri_rd8(info + 515);
    uint16_t x = r16(p + OBJ_x_pos), y = r16(p + OBJ_y_pos);
    uint8_t c = tri_rd8(info + ((y >> 8) & 7) * 64u + ((x >> 8) & 0x3F));
    if (c == roll1 || c == roll2) {
        uint32_t d0 = D(0), d1 = D(1), a0 = A(0), a1 = A(1);
        A(0) = RAMPTR(p);
        s1_chk_roll(p);
        D(0) = d0; D(1) = d1; A(0) = a0; A(1) = a1;
        return;
    }
    if (c == loop2) {
        if (r8(p + OBJ_status) & 2) {         /* in air: back to high plane */
            s1_set_behind(p, 0);
            return;
        }
    } else if (c != loop1) {
        s1_set_behind(p, 0);
        return;
    }
    uint8_t xl = (uint8_t)x;
    if (xl < 44) { s1_set_behind(p, 0); return; }
    if (xl >= 224) { s1_set_behind(p, 1); return; }
    uint8_t ang = r8(p + OBJ_angle);
    if (!s1_behind(p)) {
        if (ang == 0 || ang > 0x80) return;
        s1_set_behind(p, 1);
    } else {
        if (ang <= 0x80) return;
        s1_set_behind(p, 0);
    }
}

void tri_player_frame(void)
{
    uint32_t e = tri_dir();
    if (!e)
        return;
    extern void tri_obj_frame(void);
    tri_obj_frame();
    if (tri_rd8(e + TD_GAME) == TRI_GAME_S1) {
        extern void s1_synchro_animate(void);
        s1_synchro_animate();
        uint32_t info = tri_rd32(e + TD_EXTRA);
        if (!info)
            return;
        for (uint32_t p = RAM_Player_1; p <= RAM_Player_2; p += OBJ_SIZE)
            if (r32(p + OBJ_code) && !r8(RAM_Debug_placement_mode))
                s1_loops(p, info);
    }
}
