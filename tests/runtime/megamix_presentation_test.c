/* Exercise independent native-menu choices, persisted character identity and
 * the actual palette/art publisher without any copyrighted assets. */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "video/genesis_machine.h"
#include "sonic3_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} } while(0)
uint8_t g_ram[65536],g_rom[0x400000];
static uint8_t bank[4096];
uint8_t *g_rom_ext=bank;uint32_t g_rom_ext_size=sizeof bank;
GenesisMachine g_machine;M68KState g_cpu;
int g_split_sp_popped;static int rte;int *g_rte_pending_ptr=&rte;
int s3_stage_pack_mighty=1,s3_stage_pack_megamix=1,s3_stage_pack_enabled=1;
int tri_mighty_selected;static int player_active=1,atlas_calls;
uint8_t tri_rd8(uint32_t a) { CHECK(a>=0x400000 && a<0x401000);return bank[a-0x400000]; }
uint16_t tri_rd16(uint32_t a) { return (uint16_t)((tri_rd8(a)<<8)|tri_rd8(a+1)); }
uint32_t tri_rd32(uint32_t a) { return ((uint32_t)tri_rd16(a)<<16)|tri_rd16(a+2); }
uint32_t tri_mighty_descriptor(void){uint32_t d=tri_rd32(0x400024);return d?tri_rd32(d+12):0;}
int tri_mighty_active(void) { return player_active; }
static S3VideoHudMapping custom_hud;
void s3_video_set_hud_mapping(S3VideoHudMapping fn) { custom_hud=fn; }
uint16_t tri_mighty_color(int wet,unsigned color) { return color==2?(wet?0xE00:0x00E):0; }
void o_sfx(int sound) { CHECK(sound==0x5B); }
int gvdp_set_sprite_patterns(const uint8_t *pixels,unsigned bytes,const uint8_t *lines)
{ CHECK(pixels==bank+0x600 && bytes==128 && lines[0]==3 && lines[3]==3);++atlas_calls;return 1; }
#include "../../game/trilogy/trilogy_mighty_presentation.c"
static void word(unsigned p,unsigned v) { bank[p]=(uint8_t)(v>>8);bank[p+1]=(uint8_t)v; }
static void longword(unsigned p,unsigned v) { word(p,v>>16);word(p+2,v); }
int main(void)
{
    longword(0x24,0x400100);longword(0x100,0x4D444931);longword(0x10C,0x400200);
    longword(0x200,0x4D445933);longword(0x240,0x400800);
    longword(0x244,0x400600);longword(0x248,128);
    longword(0x24C,0x400900);longword(0x250,0x400A00);
    longword(0x254,0x400B00);
    memset(bank+0x900,0x99,128);memset(bank+0xA00,0xAA,192);
    A(0)=0xFFB128;w32(A(0),0xD42C);w8(A(0)+0x2E,0);
    D(0)=0;w8(RAM_Ctrl_1_pressed,1);
    for(unsigned choice=1;choice<=6;choice++) {
        CHECK(tri_mighty_presentation_hook(MG_PC_DS_CHARACTER));
        unsigned virtual_mode=choice%6;
        CHECK((D(0)&65535)==(virtual_mode>=4?virtual_mode-4:virtual_mode));
        CHECK(menu_mighty[0]==(virtual_mode>=4));
    }
    /* Saved Mighty remains distinct while every retail mode/table index is
     * kept in its normal range. The save's nibble survives the conversion. */
    w8(0xFFE6AE,0x40);D(0)=4;
    CHECK(!tri_mighty_presentation_hook(MG_PC_DS_SLOT_CHARACTER));
    CHECK((D(0)&255)==0 && menu_mighty[0] && r8(0xFFE6AE)==0x40);
    w8(RAM_Game_mode,0x4C);w32(A(0)+OBJ_mappings,0xCE0E);w8(A(0)+OBJ_frame,4);
    CHECK(!tri_mighty_presentation_hook(MG_PC_DS_DRAW));
    CHECK(r8(A(0)+OBJ_frame)==8 && r32(A(0)+OBJ_mappings)==0x400800);
    w32(A(0),0xD7A4);w8(A(0)+OBJ_frame,8);w32(A(0)+OBJ_mappings,0xCE0E);
    CHECK(!tri_mighty_presentation_hook(MG_PC_DS_DRAW));
    CHECK(r8(A(0)+OBJ_frame)==8 && r32(A(0)+OBJ_mappings)==0xCE0E); /* Delete untouched */
    w32(A(0),0xD42C);w32(SAVE_POINTER,0xFFE6AC);w16(PLAYER_OPTION,0);
    CHECK(!tri_mighty_presentation_hook(TH_LEVEL_ENTRY));
    CHECK(tri_mighty_selected && r16(RAM_Player_mode)==0 && r16(PLAYER_OPTION)==0);
    w8(0xFFE6AE,0);CHECK(!tri_mighty_presentation_hook(TH_LEVEL_ENTRY));CHECK(!tri_mighty_selected);
    /* New saved selection writes its own identity, never Sonic's nibble. */
    menu_mighty[0]=1;w16(PLAYER_OPTION,1);
    CHECK(!tri_mighty_presentation_hook(TH_DS_NEWGAME));CHECK(r8(0xFFE6AE)==0x50);
    CHECK(!tri_mighty_presentation_hook(TH_LEVEL_ENTRY));
    CHECK(tri_mighty_selected && r16(RAM_Player_mode)==1 && r16(PLAYER_OPTION)==1);
    CHECK(custom_hud && custom_hud()==0x400B00);
    A(1)=0xDBB6;CHECK(!tri_mighty_presentation_hook(MG_PC_MIGHTY_HUD));
    CHECK(A(1)==0x400B00);
    player_active=0;A(1)=0xDBB6;CHECK(!tri_mighty_presentation_hook(MG_PC_MIGHTY_HUD));
    CHECK(A(1)==0xDBB6 && !custom_hud());player_active=1;
    /* Menu art is wholly separate from native VRAM. Gameplay writes only
     * Mighty's head/name tiles, preserving the stock lives digits. */
    uint8_t tiles[2048]={0};uint16_t top[16],bottom[16];
    static uint8_t before[65536];
    w16(RAM_Normal_palette+2,0xEEE);memcpy(before,g_ram,sizeof before);
    memset(g_machine.vdp.vram,0x5A,65536);
    CHECK(tri_mighty_presentation_frame(tiles,top,bottom));CHECK(atlas_calls==1);
    for(unsigned i=0;i<65536;i++)CHECK(g_machine.vdp.vram[i]==0x5A);
    CHECK(!memcmp(before,g_ram,sizeof before));CHECK(top[2]==0xE && bottom[2]==0xE00);
    w8(RAM_Game_mode,0x0C);memcpy(before,g_ram,sizeof before);
    CHECK(tri_mighty_presentation_frame(tiles,top,bottom));
    CHECK(tiles[0x680]==3 && tiles[0x69F]==3 && !tiles[0x6A0]);
    CHECK(tiles[0x7D4]==3 && tiles[0x7D7]==3 && !tiles[0x7D8]);
    CHECK(g_machine.vdp.vram[0x7D4*32]==0x99 && g_machine.vdp.vram[0x7D8*32]==0xAA);
    CHECK(g_machine.vdp.vram[0x7DC*32]==0xAA && g_machine.vdp.vram[0x7DE*32]==0xAA);
    CHECK(g_machine.vdp.vram[0x7DD*32]==0x5A && g_machine.vdp.vram[0x7DF*32]==0x5A);
    CHECK(!memcmp(before,g_ram,sizeof before));
    for(unsigned i=0;i<64;i++)w16(RAM_Normal_palette+i*2,0xEEE);
    CHECK(tri_mighty_presentation_frame(tiles,top,bottom));
    CHECK(top[0]==0xEEE && top[2]==0xEEE && bottom[0]==0xEEE && bottom[2]==0xEEE);
    memset(g_ram+(RAM_Normal_palette&65535),0,128);
    CHECK(tri_mighty_presentation_frame(tiles,top,bottom));
    CHECK(!top[0] && !top[2] && !bottom[2]);
    player_active=0;CHECK(!tri_mighty_presentation_frame(tiles,top,bottom));CHECK(!tiles[0x680]);
    puts("Mighty selection, save identity, menu atlas, palette isolation and HUD digits passed");return 0;
}
