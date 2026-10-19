/* The donor's late sign loader owns its own graphics and character colors.
 * Dedicated immutable bank 1 cannot overwrite monitors or result-card art. */
#include "trilogy_obj.h"
#include "stage_pack.h"
#include "video/genesis_vdp.h"
#include <string.h>

static uint32_t descriptor(void)
{
    uint32_t e=tri_dir();
    if(!e || tri_rd8(e+TD_GAME)!=TRI_GAME_MEGAMIX)return 0;
    uint32_t d=tri_rd32(TRI_BANK_BASE+0x24);
    if(!d || tri_rd16(d+10)<4)return 0;
    uint32_t p=tri_rd32(d+36);
    return p && tri_rd32(p)==0x4D475331u?p:0;
}
uint32_t tri_signpost_mapping(void) { uint32_t p=descriptor();return p?tri_rd32(p+4):0; }
uint32_t tri_signpost_animation(void) { uint32_t p=descriptor();return p?tri_rd32(p+8):0; }
int tri_signpost_frame(uint16_t *top,uint16_t *bottom)
{
    static uint8_t lines[58];
    uint32_t p=descriptor();
    if(!p || (r8(RAM_Game_mode)&127)!=12) {
        gvdp_set_sprite_patterns_bank(1,NULL,0,NULL);return 0;
    }
    unsigned character=tri_mighty_active()?1:0;
    p+=12+character*20;
    memset(lines,2,sizeof lines);
    gvdp_set_sprite_patterns_bank(1,g_rom_ext+tri_rd32(p)-TRI_BANK_BASE,tri_rd32(p+4),lines);
    uint32_t normal=tri_rd32(p+8),wet=tri_rd32(p+(r16(RAM_Current_zone_and_act)==0x3503?16:12));
    unsigned hi[3]={0},lo[3]={14,14,14};
    for(unsigned i=0;i<64;i++)for(unsigned c=0;c<3;c++) {
        unsigned v=(r16(RAM_Normal_palette+i*2)>>(c*4))&14;
        if(v>hi[c])hi[c]=v;if(v<lo[c])lo[c]=v;
    }
    for(unsigned w=0;w<2;w++)for(unsigned i=0;i<16;i++) {
        unsigned value=tri_rd16((w?wet:normal)+i*2),color=0;
        for(unsigned c=0;c<3;c++)color|=(((lo[c]+((value>>(c*4))&14)*(hi[c]-lo[c])/14)&14)<<(c*4));
        (w?bottom:top)[i]=(uint16_t)color;
    }
    return 1;
}
