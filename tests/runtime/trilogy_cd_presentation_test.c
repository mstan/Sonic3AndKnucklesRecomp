/* CD UI isolation must survive live scenery/Amy palette replacement, fades,
 * current-build restoration and a transition back to another campaign. */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "video/genesis_machine.h"
#include "video/genesis_vdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
uint8_t g_ram[65536],g_rom[0x400000];
GenesisMachine g_machine;M68KState g_cpu;
static int enabled=1;
uint32_t tri_dir(void) { return enabled?0x500000:0; }
uint8_t tri_rd8(uint32_t a) { return a<sizeof g_rom?g_rom[a]:0; }
uint16_t tri_rd16(uint32_t a) { return (uint16_t)(tri_rd8(a)<<8|tri_rd8(a+1)); }
uint32_t tri_rd32(uint32_t a) { return (uint32_t)tri_rd16(a)<<16|tri_rd16(a+2); }
void tri_jsr(uint32_t a) { (void)a; }
void tri_ext_tiles(uint32_t a,uint32_t n,int on) { (void)a;(void)n;(void)on; }
uint32_t tri_scd_card_art(void) { return 0; }
uint32_t tri_scd_card_map(void) { return 0; }
void tri_obj_titlecard_done(void) { }
void tri_water_load_bubbles(void) { }
#include "../../game/trilogy/trilogy_titlecard.c"
int main(void)
{
    uint8_t tiles[2048]={0},before[65536];uint16_t top[48],bottom[48];
    for(unsigned i=0;i<48;i++)top[i]=bottom[i]=0xACE;
    /* Simulate any player's unchanged line, including white for fade range. */
    w8(RAM_Game_mode,12);w16(RAM_Current_zone_and_act,0x4000);
    w16(RAM_Normal_palette+2,0xEEE);
    g_rom[S3K_Pal_SonicTails+32+10]=0;g_rom[S3K_Pal_SonicTails+32+11]=0xEE;
    tiles[0x410]=1;tiles[0x680]=3;tiles[0x7D4]=3;tiles[0x7DD]=0;
    memcpy(before,g_ram,sizeof before);
    CHECK(tri_scd_ui_frame(tiles,top+16,bottom+16));
    CHECK(top[21]==0xEE && bottom[21]==0xEE);
    CHECK(top[5]==0xACE && top[37]==0xACE && tiles[0x410]==1 && tiles[0x680]==3 && tiles[0x7D4]==3);
    CHECK(tiles[0x6CA]==0x82 && tiles[0x6FF]==0x82 && !tiles[0x700] && !tiles[0x7DD]);
    CHECK(!memcmp(before,g_ram,sizeof before));
    w16(RAM_Current_zone_and_act,0x4400);w16(RAM_Normal_palette+32+10,0x0E0);
    CHECK(tri_scd_ui_frame(tiles,top+16,bottom+16) && top[21]==0xEE);
    w16(RAM_Normal_palette+32+10,0xE00); /* Amy's cutscene palette */
    CHECK(tri_scd_ui_frame(tiles,top+16,bottom+16) && top[21]==0xEE);
    w32(0xFFB094,S3K_Obj_LevelResults);
    CHECK(tri_scd_ui_frame(tiles,top+16,bottom+16));
    CHECK(tiles[0x520]==0x82 && tiles[0x533]==0x82 && !tiles[0x534]);
    CHECK(tiles[0x53A]==0x82 && tiles[0x549]==0x82 && !tiles[0x54A]);
    memset(g_ram+(RAM_Normal_palette&65535),0,128);
    CHECK(tri_scd_ui_frame(tiles,top+16,bottom+16));for(unsigned i=16;i<32;i++)CHECK(!top[i] && !bottom[i]);
    for(unsigned i=0;i<64;i++)w16(RAM_Normal_palette+i*2,0xEEE);
    CHECK(tri_scd_ui_frame(tiles,top+16,bottom+16));for(unsigned i=16;i<32;i++)CHECK(top[i]==0xEEE && bottom[i]==0xEEE);
    w16(RAM_Current_zone_and_act,0);CHECK(!tri_scd_ui_frame(tiles,top+16,bottom+16));
    for(unsigned i=0;i<2048;i++)CHECK(tiles[i]!=0x82);
    CHECK(tiles[0x410]==1 && tiles[0x680]==3 && tiles[0x7D4]==3);
    /* Digit replacement follows restored object state, without a transient
     * host flag, and stops as soon as card/results release the VRAM range. */
    w16(RAM_Current_zone_and_act,0x4002);w8(RAM_Apparent_act,2);
    memset(g_machine.vdp.vram,0xAA,65536);w32(0xFFB094,S3K_Obj_TitleCard);
    tri_titlecard_vblank();CHECK(g_machine.vdp.vram[0x53D*32]==0);
    CHECK(g_machine.vdp.vram[0x53D*32-1]==0xAA && g_machine.vdp.vram[0x54D*32]==0xAA);
    static const uint8_t digit[512]={
#include "../../game/trilogy/trilogy_act3_tiles.inc"
    };
    CHECK(!memcmp(g_machine.vdp.vram+0x53D*32,digit,sizeof digit));
    w32(0xFFB094,S3K_Obj_LevelResults);tri_titlecard_vblank();
    CHECK(!memcmp(g_machine.vdp.vram+0x568*32,digit,sizeof digit));
    w32(0xFFB094,0);memset(g_machine.vdp.vram+0x53D*32,0xBB,512);tri_titlecard_vblank();
    CHECK(g_machine.vdp.vram[0x53D*32]==0xBB);
    w32(0xFFB094,S3K_Obj_TitleCard);w8(RAM_Apparent_act,1);tri_titlecard_vblank();
    CHECK(g_machine.vdp.vram[0x53D*32]==0xBB);
    w8(RAM_Apparent_act,2);enabled=0;tri_titlecard_vblank();CHECK(g_machine.vdp.vram[0x53D*32]==0xBB);
    CHECK(!tri_scd_ui_frame(tiles,top+16,bottom+16));
    puts("CD UI palette isolation, fades, campaign reset and existing act-3 numeral passed");return 0;
}
