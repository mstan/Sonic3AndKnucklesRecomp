/* S3K mapping -> SAT adapter for explicitly marked Megamix sprites.
 * The size marker survives to the host VDP; size arithmetic uses only the
 * hardware nibble. Native object ordering, piece limits, flips and priority
 * remain intact. Unmarked/native stages use the original routines.
 *
 * Inputs: A1 pieces, A6 SAT cursor, D0/D1 origin, D4 count-1, D5 art offset,
 * D6 object flip flags, D7 remaining pieces-1. Clobbers D2,D4,D6,D7,A1,A6
 * and CCR; the render caller preserves its flags in D3 or on its own stack.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "megamix_geometry_sites.h"

int tri_megamix_sprite_hook(uint32_t pc)
{
    if(pc!=MG_PC_SK_SPRITE_RAW && pc!=MG_PC_SK_SPRITE_CLIP)return 0;
    uint32_t e=tri_dir(),map=A(1)&0xFFFFFFu;
    int menu=(r8(RAM_Game_mode)&127)==0x4C;
    if(((!e || tri_rd8(e+TD_GAME)!=TRI_GAME_MEGAMIX) && !menu) || map<TRI_BANK_BASE ||
       map-TRI_BANK_BASE>=g_rom_ext_size)return 0;
    uint32_t p=A(1),out=A(6);
    uint16_t count=(uint16_t)D(4),remaining=(uint16_t)D(7),last=0;
    unsigned flags=D(6)&3u,clip=pc==MG_PC_SK_SPRITE_CLIP;
    do {
        if((int16_t)remaining<0)break;
        int dy=(int8_t)tri_rd8(p),size=tri_rd8(p+1);
        unsigned width=(((unsigned)size>>2)&3u)+1u,height=((unsigned)size&3u)+1u;
        int dx=(int16_t)tri_rd16(p+4);
        uint16_t base=(uint16_t)D(5);
        if(menu && (size&0x80))base&=0xE000u;
        uint16_t tile=(uint16_t)(tri_rd16(p+2)+base);
        if(flags&1u){dx=-dx-(int)width*8;tile^=0x800;}
        if(flags&2u){dy=-dy-(int)height*8;tile^=0x1000;}
        uint16_t x=(uint16_t)(dx+(uint16_t)D(0)),y=(uint16_t)(dy+(uint16_t)D(1));
        p+=6;
        if(clip && (y<=96 || y>=352 || x<=96 || x>=448)) {
            last=y; if(--count==0xFFFFu)break;continue;
        }
        if(!clip){x&=0x1FFu;if(!x)x=1;}
        w16(out,y);w8(out+2,(uint8_t)size); /* link byte is written by Render_Sprites */
        w16(out+4,tile);w16(out+6,x);out+=8;last=x;
        --remaining;
        if((int16_t)remaining<0)break;
    } while(--count!=0xFFFFu);
    D(2)=(D(2)&0xFFFF0000u)|last;
    D(4)=(D(4)&0xFFFF0000u)|count;
    D(7)=(D(7)&0xFFFF0000u)|remaining;
    A(1)=p;A(6)=out;
    g_cpu.SR&=~0xFu;
    if(!remaining)g_cpu.SR|=SR_Z;
    if(remaining&0x8000u)g_cpu.SR|=SR_N;
    if(g_split_sp_popped>0){g_rte_pending=g_split_sp_popped;g_split_sp_popped=0;}
    return 1;
}
