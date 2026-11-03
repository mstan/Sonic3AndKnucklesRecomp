/* The native catch-up flight owns object_control=$81 and moves the actor
 * from Tails_CPU_Control. A negative end-act input lock skips that CPU and
 * strands a companion respawning after the giant-ring flash. Positive one
 * keeps physical input locked while allowing the native flight and SFX. */
static void tail_ring_respawn_tick(void)
{
    uint32_t entry=tri_dir(),p=RAM_Player_2;
    if (!s_ss_pending || !entry || tri_rd8(entry+TD_GAME)!=TRI_GAME_S1 ||
        r8(RAM_Game_mode)!=0x0C || r16(RAM_Player_mode)!=0 ||
        r8(RAM_Ctrl_2_locked)!=0xFF || !r32(p) || OB8(p,0x38)!=1 ||
        OB8(p,OBJ_routine)!=2 || OB8(p,0x2E)!=0x81 ||
        r16(RAM_Tails_CPU_routine)!=4) return;
    s_runoff&=~10;                 /* this newly respawned actor is not gone */
    w8(RAM_Ctrl_2_locked,1);
}
