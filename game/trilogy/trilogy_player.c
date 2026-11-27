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

static uint8_t s1_chunk_at(uint32_t p,uint32_t info)
{
    uint16_t x=r16(p+OBJ_x_pos),y=r16(p+OBJ_y_pos);
    return tri_rd8(info+((y>>8)&7u)*64u+((x>>8)&0x3Fu));
}

static int s1_roll_chunk(uint32_t info,uint8_t chunk)
{
    return chunk==tri_rd8(info+514) || chunk==tri_rd8(info+515);
}

int tri_s1_keep_tunnel_roll(uint32_t pc)
{
    if(pc!=0x01157Cu && pc!=0x014DA6u && pc!=0x0175AEu)return 0;
    uint32_t player=A(0)&0xFFFFFFu,entry=tri_dir();
    if(!entry || tri_rd8(entry+TD_GAME)!=TRI_GAME_S1 ||
       (player!=RAM_Player_1 && player!=RAM_Player_2) ||
       r8(RAM_Debug_placement_mode) || r8(player+0x3D) ||
       !(r8(player+OBJ_status)&4) || !(g_cpu.SR&1u) ||
       tri_rd32(pc-4)!=0x0C400080u || tri_rd16(pc)!=0x6448)
        return 1;

    uint32_t info=tri_s1_info();
    if(!info || !s1_roll_chunk(info,s1_chunk_at(player,info)))return 1;

    /* Apply the tunnel's existing zero-speed kick before native physics
     * and animation. The ground-speed check must keep the roll pose while
     * the player is still inside the authored tunnel. */
    if(!r16(player+OBJ_ground_vel))w16(player+OBJ_ground_vel,0x200);
    g_cpu.SR&=(uint16_t)~1u;
    return 1;
}

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
    uint8_t c = s1_chunk_at(p,info);
    if (s1_roll_chunk(info,c)) {
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
    uint8_t xl = (uint8_t)r16(p+OBJ_x_pos);
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
    if (tri_rd8(e + TD_GAME) == TRI_GAME_S1 || tri_rd8(e+TD_GAME)==TRI_GAME_CD || tri_rd8(e+TD_GAME)==TRI_GAME_MEGAMIX) {
        extern void s1_synchro_animate(void);
        s1_synchro_animate();
        if (tri_rd8(e+TD_GAME)==TRI_GAME_MEGAMIX &&
            tri_source_zone()!=TRI_S1_GHZ && tri_source_zone()!=TRI_S1_SLZ) return;
        uint32_t info = tri_s1_info();
        if (!info)
            return;
        for (uint32_t p = RAM_Player_1; p <= RAM_Player_2; p += OBJ_SIZE)
            if (r32(p + OBJ_code) && !r8(RAM_Debug_placement_mode))
                s1_loops(p, info);
    }
}
