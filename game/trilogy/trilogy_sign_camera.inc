/* S1 SignpostArtLoad also locks the left boundary $100 before the end.
 * Converted art is already resident; retain the original camera side effect
 * independently of the later Sign_Touch lock at the right boundary. */
static void s1_signpost_camera_lock(void)
{
    uint32_t entry=tri_dir();
    if (!entry || tri_rd8(entry+TD_GAME)!=TRI_GAME_S1 ||
        r16(RAM_Debug_placement_mode) || tri_act()==2 ||
        !r8(RAM_Update_HUD_timer)) return;
    uint16_t limit=(uint16_t)(r16(RAM_Camera_max_X_pos)-0x100);
    if ((int16_t)r16(RAM_Camera_X_pos)<(int16_t)limit) return;
    if (r16(RAM_Camera_min_X_pos)!=limit) w16(RAM_Camera_min_X_pos,limit);
}
