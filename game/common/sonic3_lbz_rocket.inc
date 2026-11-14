/* The native resize queues this bank only when the 320px view approaches
 * the Death Egg. Wider margins can see its foreground and pillars earlier. */
static struct {
    int attempted,ready;
    uint8_t world[65536];
    GVDP video;
} s_lbz_rocket;
static int lbz_rocket_load(void)
{
    if(s_lbz_rocket.attempted)return s_lbz_rocket.ready;
    s_lbz_rocket.attempted=1;size_t used=0;
    if(art_kos(g_rom+S3_ART_BANK+0x1ED3D4,0xCF0,s_lbz_rocket.world,0x8000,&used)!=0x7C00 ||
       art_kos(g_rom+S3_ART_BANK+0x1E69B0,0xE20,s_lbz_rocket.world+0x9000,0x1610,&used)!=0x1610 ||
       art_kosm(S3_ART_BANK+0x1E8F72,0x1F92,s_lbz_rocket.video.vram,0x64A0)!=0x64A0 ||
       art_kosm(S3_ART_BANK+0x17F6EE,0x112,s_lbz_rocket.video.vram+0xB400,0x200)!=0x200)return 0;
    memcpy(s_lbz_rocket.world+0x8000,g_rom+S3_ART_BANK+0x8A6FE,0xF28);
    s_lbz_rocket.world[0xEEAF]=0x7C;
    return s_lbz_rocket.ready=1;
}
