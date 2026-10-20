/* Mighty is a separate Data Select character. The stock four native modes
 * remain in their normal ranges; only the saved character nibble uses 4/5.
 * That nibble is resolved before the retail player initializer runs.
 * A private palette line belongs only to Mighty's player/head/menu tiles.
 */
#include "trilogy_obj.h"
#include "stage_pack.h"
#include "megamix_geometry_sites.h"
#include "sonic3_video.h"
#include "video/genesis_machine.h"
#include <string.h>

#define SAVE_POINTER 0xFFE660u
#define PLAYER_OPTION 0xFFFF0Au
/* 0 retail, 1 Mighty, 2 Shadow. Retail code always sees modes 0..3. */
static uint8_t menu_mighty[9];
static uint8_t menu_palette_lines[64],menu_art[2048];
static uint32_t data_for(unsigned character)
{
    uint32_t p=character==2?tri_shadow_descriptor():tri_mighty_descriptor();
    return p && tri_rd32(p)==(character==2?0x4D535731u:0x4D445933u)?p:0;
}
static uint32_t data(void)
{
    return data_for(tri_shadow_selected?2:1);
}
static unsigned menu_index(uint32_t p)
{ return (r32(p)&0xFFFFFFu)==0xD30Cu?8:r8(p+0x2E)&7; }
static unsigned hud_mapping(void)
{
    uint32_t d=data();
    return d && tri_character_active()?tri_rd32(d+84):0;
}
static void choose_current(void)
{
    unsigned mode=r16(PLAYER_OPTION);
    uint32_t save=r32(SAVE_POINTER)&0xFFFFFFu;
    if(mode<4) {
        if(save>=0xFFE6AC && save<0xFFE6FC)mode=r8(save+2)>>4;
        else if(mode<2)mode+=tri_shadow_selected?6:tri_mighty_selected?4:0;
    }
    tri_mighty_selected=mode==4 || mode==5;
    tri_shadow_selected=(mode==6 || mode==7) && s3_stage_pack_shadow && data_for(2);
    if(tri_mighty_selected || mode==6 || mode==7) {
        mode&=1;w16(PLAYER_OPTION,(uint16_t)mode);w16(RAM_Player_mode,(uint16_t)mode);
    }
}
int tri_mighty_presentation_hook(uint32_t pc)
{
    uint32_t p=A(0)&0xFFFFFFu;
    if(pc==TH_LEVEL_ENTRY) {
        choose_current();s3_video_set_hud_mapping(hud_mapping);return 0;
    }
    if(pc==TH_DS_NOSAVE_START || pc==TH_DS_NEWGAME) {
        unsigned character=menu_mighty[menu_index(p)];
        if(!s3_stage_pack_mighty || !data_for(character) || (character==2 && !s3_stage_pack_shadow))character=0;
        unsigned offset=character?character*2+2:0;
        unsigned mode=r16(PLAYER_OPTION);
        if(pc==TH_DS_NEWGAME && character) {
            uint32_t save=r32(SAVE_POINTER)&0xFFFFFFu;
            w8(save+2,(uint8_t)((r8(save+2)&15)|((mode+offset)<<4)));
        }
        tri_mighty_selected=character==1;tri_shadow_selected=character==2;
        /* No-save has no cartridge slot from which to recover this choice. */
        if(character)w16(PLAYER_OPTION,(uint16_t)(mode+offset));
        return 0;
    }
    if(pc==MG_PC_DS_SLOT_CHARACTER) {
        unsigned mode=D(0)&255;
        menu_mighty[menu_index(p)]=(uint8_t)(mode>=4 && mode<=7?mode/2-1:0);
        if(mode>=4 && mode<=7)D(0)=(D(0)&0xFFFFFF00u)|(mode&1);
        return 0;
    }
    if(!s3_stage_pack_mighty || !data_for(1))return 0;
    if(pc==MG_PC_MIGHTY_HUD && tri_character_active()) {
        A(1)=tri_rd32(data()+84);
        return 0; /* native frame selection, positioning and SAT rendering */
    }
    if(pc==MG_PC_DS_CHARACTER) {
        unsigned index=menu_index(p),mode=D(0)&0xFFFFu;
        if(menu_mighty[index] && mode<2)mode+=menu_mighty[index]*2+2;
        unsigned count=s3_stage_pack_shadow && data_for(2)?8:6;
        if(!r16(RAM_Player_2+0x30)) {
            unsigned pressed=r8(RAM_Ctrl_1_pressed);
            if(pressed&1){mode=(mode+1)%count;o_sfx(0x5B);}
            else if(pressed&2){mode=(mode+count-1)%count;o_sfx(0x5B);}
        }
        menu_mighty[index]=(uint8_t)(mode>=4?mode/2-1:0);
        D(0)=(D(0)&0xFFFF0000u)|(mode>=4?mode&1:mode);
        if(g_split_sp_popped>0){g_rte_pending=g_split_sp_popped;g_split_sp_popped=0;}
        return 1;
    }
    if(pc==MG_PC_DS_DRAW && (r8(RAM_Game_mode)&127)==0x4C) {
        unsigned code=r32(p)&0xFFFFFFu;
        if(code!=0xD30C && code!=0xD39E && code!=0xD42C)return 0;
        uint32_t map=r32(p+OBJ_mappings)&0xFFFFFFu;
        unsigned character=menu_mighty[menu_index(p)];
        uint32_t mighty_map=tri_rd32(data_for(1)+64),shadow=data_for(2);
        uint32_t shadow_map=shadow?tri_rd32(shadow+64):0;
        if(map==0xCE0E || map==mighty_map || (shadow_map && map==shadow_map)) {
            unsigned frame=r8(p+OBJ_frame);
            if(frame==8 || frame==9)frame-=4;
            if((frame==4 || frame==5) && character)frame+=4;
            w8(p+OBJ_frame,(uint8_t)frame);
            w32(p+OBJ_mappings,character==2?shadow_map:character?mighty_map:0xCE0E);
        }
    }
    return 0;
}
static void upload(uint32_t src,unsigned dst,unsigned bytes)
{
    for(unsigned i=0;i<bytes;i++)g_machine.vdp.vram[dst+i]=tri_rd8(src+i);
}
int tri_mighty_presentation_frame(uint8_t *tiles,uint16_t *top,uint16_t *bottom)
{
    for(unsigned i=0;i<2048;i++)if(tiles[i]==3)tiles[i]=0;
    uint32_t d=data();
    int menu=(r8(RAM_Game_mode)&127)==0x4C;
    int player=(r8(RAM_Game_mode)&127)==0x0C && tri_character_active();
    if(menu)d=data_for(1);
    if(!d || !s3_stage_pack_mighty || (!menu && !player))return 0;
    if(menu) {
        memset(menu_palette_lines,3,32);memset(menu_palette_lines+32,2,32);
        uint32_t art=tri_rd32(d+68)-TRI_BANK_BASE,bytes=tri_rd32(d+72);
        uint32_t shadow=data_for(2);
        if(shadow && s3_stage_pack_shadow) {
            memset(menu_art,0,sizeof menu_art);memcpy(menu_art,g_rom_ext+art,bytes);
            memcpy(menu_art+1024,g_rom_ext+tri_rd32(shadow+68)-TRI_BANK_BASE,tri_rd32(shadow+72));
            gvdp_set_sprite_patterns(menu_art,sizeof menu_art,menu_palette_lines);
        } else gvdp_set_sprite_patterns(g_rom_ext+art,bytes,menu_palette_lines);
    }
    if(player) {
        memset(tiles+0x680,3,32);
        upload(tri_rd32(d+76),0x7D4*32,128);
        upload(tri_rd32(d+80),0x7D8*32,160);
        upload(tri_rd32(d+80)+160,0x7DE*32,32);
        memset(tiles+0x7D4,3,4);
    }
    /* Native fades own the shared CRAM. Match their per-channel envelope
     * without replacing a single color used by Sonic, Tails or stage art. */
    unsigned envelope[3]={0,0,0},floor[3]={14,14,14};
    for(unsigned i=0;i<64;i++) {
        unsigned color=r16(RAM_Normal_palette+i*2);
        for(unsigned c=0;c<3;c++) {
            unsigned value=(color>>(c*4))&14;
            if(value>envelope[c])envelope[c]=value;
            if(value<floor[c])floor[c]=value;
        }
    }
    for(unsigned wet=0;wet<2;wet++)for(unsigned i=0;i<16;i++) {
        unsigned source=menu?tri_rd16(tri_rd32(d+(wet?36:16))+i*2):tri_mighty_color((int)wet,i),color=0;
        for(unsigned c=0;c<3;c++)color|=(((floor[c]+((source>>(c*4))&14)*(envelope[c]-floor[c])/14)&14)<<(c*4));
        (wet?bottom:top)[i]=(uint16_t)color;
    }
    return 1;
}
void tri_shadow_menu_colors(uint16_t *top,uint16_t *bottom)
{
    uint32_t d=data_for(2);
    if(!d || !s3_stage_pack_shadow || (r8(RAM_Game_mode)&127)!=0x4C)return;
    unsigned hi[3]={0},lo[3]={14,14,14};
    for(unsigned i=0;i<64;i++)for(unsigned c=0;c<3;c++) {
        unsigned v=(r16(RAM_Normal_palette+i*2)>>(c*4))&14;
        if(v>hi[c])hi[c]=v;if(v<lo[c])lo[c]=v;
    }
    for(unsigned i=0;i<16;i++) {
        unsigned source=tri_rd16(tri_rd32(d+16)+i*2),color=0;
        for(unsigned c=0;c<3;c++)color|=(((lo[c]+((source>>(c*4))&14)*(hi[c]-lo[c])/14)&14)<<(c*4));
        top[i]=bottom[i]=(uint16_t)color;
    }
}
