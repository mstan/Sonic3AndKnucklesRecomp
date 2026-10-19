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
static uint8_t menu_mighty[9];
static uint8_t menu_palette_lines[32];
static uint32_t data(void)
{
    uint32_t p=tri_mighty_descriptor();
    return p && tri_rd32(p)==0x4D445933u?p:0;
}
static unsigned menu_index(uint32_t p)
{ return (r32(p)&0xFFFFFFu)==0xD30Cu?8:r8(p+0x2E)&7; }
static unsigned hud_mapping(void)
{
    uint32_t d=data();
    return d && s3_stage_pack_mighty && tri_mighty_active()?tri_rd32(d+84):0;
}
static void choose_current(void)
{
    unsigned mode=r16(PLAYER_OPTION);
    uint32_t save=r32(SAVE_POINTER)&0xFFFFFFu;
    if(mode<4) {
        if(save>=0xFFE6AC && save<0xFFE6FC)mode=r8(save+2)>>4;
        else if(tri_mighty_selected && mode<2)mode+=4;
    }
    tri_mighty_selected=mode==4 || mode==5;
    if(tri_mighty_selected) {
        mode-=4;w16(PLAYER_OPTION,(uint16_t)mode);w16(RAM_Player_mode,(uint16_t)mode);
    }
}
int tri_mighty_presentation_hook(uint32_t pc)
{
    uint32_t p=A(0)&0xFFFFFFu;
    if(pc==TH_LEVEL_ENTRY) {
        choose_current();s3_video_set_hud_mapping(hud_mapping);return 0;
    }
    if(pc==TH_DS_NOSAVE_START || pc==TH_DS_NEWGAME) {
        int mighty=menu_mighty[menu_index(p)] && s3_stage_pack_mighty && data();
        unsigned mode=r16(PLAYER_OPTION);
        if(pc==TH_DS_NEWGAME && mighty) {
            uint32_t save=r32(SAVE_POINTER)&0xFFFFFFu;
            w8(save+2,(uint8_t)((r8(save+2)&15)|((mode+4)<<4)));
        }
        tri_mighty_selected=mighty;
        /* No-save has no cartridge slot from which to recover this choice. */
        if(mighty)w16(PLAYER_OPTION,(uint16_t)(mode+4));
        return 0;
    }
    if(pc==MG_PC_DS_SLOT_CHARACTER) {
        unsigned mode=D(0)&255;
        menu_mighty[menu_index(p)]=(uint8_t)(mode==4 || mode==5);
        if(mode>=4 && mode<=5)D(0)=(D(0)&0xFFFFFF00u)|(mode-4);
        return 0;
    }
    if(!s3_stage_pack_mighty || !data())return 0;
    if(pc==MG_PC_MIGHTY_HUD && tri_mighty_active()) {
        A(1)=tri_rd32(data()+84);
        return 0; /* native frame selection, positioning and SAT rendering */
    }
    if(pc==MG_PC_DS_CHARACTER) {
        unsigned index=menu_index(p),mode=D(0)&0xFFFFu;
        if(menu_mighty[index] && mode<2)mode+=4;
        if(!r16(RAM_Player_2+0x30)) {
            unsigned pressed=r8(RAM_Ctrl_1_pressed);
            if(pressed&1){mode=(mode+1)%6;o_sfx(0x5B);}
            else if(pressed&2){mode=(mode+5)%6;o_sfx(0x5B);}
        }
        menu_mighty[index]=(uint8_t)(mode>=4);
        D(0)=(D(0)&0xFFFF0000u)|(mode>=4?mode-4:mode);
        if(g_split_sp_popped>0){g_rte_pending=g_split_sp_popped;g_split_sp_popped=0;}
        return 1;
    }
    if(pc==MG_PC_DS_DRAW && (r8(RAM_Game_mode)&127)==0x4C) {
        unsigned code=r32(p)&0xFFFFFFu;
        if(code!=0xD30C && code!=0xD39E && code!=0xD42C)return 0;
        uint32_t map=r32(p+OBJ_mappings)&0xFFFFFFu;
        uint32_t custom=tri_rd32(data()+64);
        if(map==0xCE0E || map==custom) {
            unsigned frame=r8(p+OBJ_frame);
            if(frame==8 || frame==9)frame-=4;
            int mighty=menu_mighty[menu_index(p)];
            if((frame==4 || frame==5) && mighty)frame+=4;
            w8(p+OBJ_frame,(uint8_t)frame);
            w32(p+OBJ_mappings,mighty?custom:0xCE0E);
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
    int player=(r8(RAM_Game_mode)&127)==0x0C && tri_mighty_active();
    if(!d || !s3_stage_pack_mighty || (!menu && !player))return 0;
    if(menu) {
        memset(menu_palette_lines,3,sizeof menu_palette_lines);
        uint32_t art=tri_rd32(d+68)-TRI_BANK_BASE,bytes=tri_rd32(d+72);
        gvdp_set_sprite_patterns(g_rom_ext+art,bytes,menu_palette_lines);
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
        unsigned source=tri_mighty_color((int)wet,i),color=0;
        for(unsigned c=0;c<3;c++)color|=(((floor[c]+((source>>(c*4))&14)*(envelope[c]-floor[c])/14)&14)<<(c*4));
        (wet?bottom:top)[i]=(uint16_t)color;
    }
    return 1;
}
