#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "megamix_geometry_sites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
uint8_t g_ram[65536],g_rom[0x400000];
static uint8_t bank[1024];
uint8_t *g_rom_ext=bank;uint32_t g_rom_ext_size=sizeof bank;
M68KState g_cpu;int g_split_sp_popped;
static int pending,enabled=1;int *g_rte_pending_ptr=&pending;
uint32_t tri_dir(void){return enabled?0x400040:0;}
uint8_t tri_rd8(uint32_t a){CHECK(a>=0x400000 && a<0x400400);return bank[a-0x400000];}
uint16_t tri_rd16(uint32_t a){return (uint16_t)((tri_rd8(a)<<8)|tri_rd8(a+1));}
uint32_t tri_rd32(uint32_t a){return ((uint32_t)tri_rd16(a)<<16)|tri_rd16(a+2);}
extern int tri_megamix_sprite_hook(uint32_t pc);
int main(void)
{
    bank[0x40+TD_GAME]=3;
    /* Asymmetric local coordinates expose each independent flip. */
    const uint8_t piece[]={0xFB,0x86,0x80,0x10,0xFF,0xFD};memcpy(bank+0x100,piece,6);
    for(unsigned flip=0;flip<4;flip++) {
        memset(g_ram,0,sizeof g_ram);memset(&g_cpu,0,sizeof g_cpu);
        g_cpu.A[1]=0x400100;g_cpu.A[6]=0xFFFFD800;
        g_cpu.D[0]=160;g_cpu.D[1]=170;g_cpu.D[4]=0xABCD0000;
        g_cpu.D[5]=0;g_cpu.D[6]=flip;g_cpu.D[7]=7;g_cpu.D[3]=0xC0FFEE;
        CHECK(tri_megamix_sprite_hook(MG_PC_SK_SPRITE_RAW));
        CHECK(r16(0xFFD800)==((flip&2)?151:165) && r16(0xFFD806)==((flip&1)?147:157));
        CHECK(r8(0xFFD802)==0x86 && r8(0xFFD803)==0);
        CHECK(r16(0xFFD804)==(0x8010^((flip&1)?0x800:0)^((flip&2)?0x1000:0)));
        CHECK(g_cpu.D[3]==0xC0FFEE && g_cpu.D[4]==0xABCDFFFF && g_cpu.D[7]==6);
        CHECK(g_cpu.A[1]==0x400106 && g_cpu.A[6]==0xFFFFD808);
    }
    /* Clipping consumes the mapping but leaves the hardware sprite budget
     * and link byte intact when the piece lies outside the native viewport. */
    g_cpu.A[1]=0x400100;g_cpu.A[6]=0xFFFFD800;g_cpu.D[4]=0;g_cpu.D[6]=0;g_cpu.D[7]=7;
    g_cpu.D[0]=50;g_cpu.D[1]=50;w8(0xFFD803,0x37);
    CHECK(tri_megamix_sprite_hook(MG_PC_SK_SPRITE_CLIP));
    CHECK(g_cpu.A[6]==0xFFFFD800 && g_cpu.D[7]==7 && r8(0xFFD803)==0x37);
    enabled=0;g_cpu.A[1]=0x400100;CHECK(!tri_megamix_sprite_hook(MG_PC_SK_SPRITE_RAW));
    enabled=1;g_cpu.A[1]=0x120000;CHECK(!tri_megamix_sprite_hook(MG_PC_SK_SPRITE_RAW));
    /* Menu avatars use host patterns without adding the native menu's tile
     * base. The original Tails pieces in the same mapping keep that base. */
    enabled=0;g_ram[RAM_Game_mode&65535]=0x4C;
    g_cpu.A[1]=0x400100;g_cpu.A[6]=0xFFFFD800;
    g_cpu.D[0]=160;g_cpu.D[1]=170;g_cpu.D[4]=0;g_cpu.D[5]=0x829F;
    g_cpu.D[6]=0;g_cpu.D[7]=7;
    CHECK(tri_megamix_sprite_hook(MG_PC_SK_SPRITE_RAW));
    CHECK(r16(0xFFD804)==0x0010); /* source priority 8000 + base priority 8000 wraps */
    CHECK(r8(0xFFD802)==0x86);
    puts("Megamix SAT marker, all flips, clipping, budget, links and native opt-out passed");return 0;
}
