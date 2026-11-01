/* Imported S1's complete palette transfer must leave native IRQ geometry and
 * every other campaign alone, in all four native water handlers. */
#define TRILOGY_INTERNAL 1
#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
uint8_t g_ram[65536];
M68KState g_cpu;
static int enabled=1,game=TRI_GAME_S1,source=TRI_S1_LZ;
uint32_t tri_dir(void) { return enabled?0x500000:0; }
uint8_t tri_rd8(uint32_t a) { return a==0x500000+TD_GAME?(uint8_t)game:0; }
uint16_t tri_rd16(uint32_t a) { (void)a;return 0; }
uint32_t tri_rd32(uint32_t a) { (void)a;return 0; }
int tri_source_zone(void) { return source; }
uint32_t tri_s1_info(void) { return 0; }
void tri_jsr(uint32_t a) { (void)a; }
void tri_refresh_fg(void) { }
int tri_megamix_lz_patch(uint8_t c) { (void)c;return 0; }
int tri_megamix_water_event(unsigned a,uint16_t x,uint16_t y,uint16_t *m,uint16_t *t,uint8_t *r)
{ (void)a;(void)x;(void)y;(void)m;(void)t;(void)r;return 0; }
void tri_lz_surfaces(void) { }
void s2_water_surfaces(void) { }
void o_sfx(int id) { (void)id; }
uint32_t o_player(int n) { (void)n;return 0; }
int o_player_active(uint32_t p) { (void)p;return 0; }
#include "../../game/trilogy/trilogy_water.c"
int main(void)
{
    static const uint32_t sites[]={TH_S1_WATER_COUNT3,TH_S1_WATER_COUNT4,TH_S1_WATER_COUNT5,TH_S1_WATER_COUNT6};
    uint8_t before[65536];
    memset(g_ram,0xA7,sizeof g_ram);memcpy(before,g_ram,sizeof before);
    for(unsigned h=0;h<4;h++)for(enabled=0;enabled<=1;enabled++)
        for(game=0;game<=TRI_GAME_CD;game++)for(source=TRI_S1_GHZ;source<=TRI_S1_SBZ;source++)
            for(unsigned n=0;n<256;n++) {
                g_cpu.D[0]=0xA5B6C700u|n;g_cpu.D[1]=0xDEADBEEFu;g_cpu.SR=0x25A5;
                CHECK(!tri_water_hook(sites[h]));
                unsigned expect=enabled && game==TRI_GAME_S1 && source==TRI_S1_LZ && n>200?200:n;
                CHECK(g_cpu.D[0]==(0xA5B6C700u|expect));
                CHECK(g_cpu.D[1]==0xDEADBEEFu && g_cpu.SR==0x25A5);
            }
    CHECK(!memcmp(before,g_ram,sizeof before));
    puts("S1 complete water palette: four handlers, all counters and campaign guards passed");
    return 0;
}
