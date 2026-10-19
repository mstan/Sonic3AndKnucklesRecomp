/* Megamix's larger chunk set is read from the expansion bank, never squeezed
 * into S3K's 256-chunk RAM area. The original tile/collision callers continue
 * to read ordinary big-endian cell words through their existing interface.
 *
 * TD_EXTRA (only when TD_GAME=3) points at an MGI1 descriptor:
 * magic, layout pointer/byte size, chunks pointer/byte size, u16 block count,
 * u16 reserved. MGL1 stores both planes' complete 16-bit chunk IDs.
 * No package is enrolled by this module; assets/events/actors are separate.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "megamix_geometry.h"
#include "megamix_geometry_sites.h"
#include "sonic3_video.h"
#include "video/genesis_vdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MegamixGeometry s_geometry;
static uint8_t s_layout[12+128*16*4];
static uint16_t s_stage;
static uint32_t s_chunks_address;

static const uint8_t *bank_range(uint32_t address,uint32_t bytes)
{
    uint32_t offset=address-TRI_BANK_BASE;
    return g_rom_ext && offset<g_rom_ext_size && bytes<=g_rom_ext_size-offset
        ? g_rom_ext+offset:NULL;
}
static int current(void)
{
    return s_geometry.layout && (s_stage==r16(RAM_Current_zone_and_act) || r16(RAM_Restart_level_flag));
}
static int video_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    unsigned a=RAM_Current_zone_and_act&0xFFFF;
    unsigned restart=RAM_Restart_level_flag&0xFFFF;
    if(!s_geometry.layout || (s_stage!=((ram[a]<<8)|ram[a+1]) && !ram[restart] && !ram[restart+1]))return 0;
    *cell=megamix_geometry_cell(&s_geometry,background,x,y,background);
    return 1;
}

void tri_megamix_geometry_begin(void)
{
    memset(&s_geometry,0,sizeof s_geometry);
    s_chunks_address=0;
    gvdp_set_sprite_patterns(NULL,0,NULL);
    s3_video_set_world_cell(video_cell);
    uint32_t e=tri_dir();
    if(!e || tri_rd8(e+TD_GAME)!=TRI_GAME_MEGAMIX)return;
    uint32_t info=tri_rd32(e+TD_EXTRA);
    if(!bank_range(info,24) || tri_rd32(info)!=0x4D474931u)goto invalid;
    uint32_t layout=tri_rd32(info+4),layout_size=tri_rd32(info+8);
    uint32_t chunks=tri_rd32(info+12),chunks_size=tri_rd32(info+16);
    const uint8_t *layout_data=bank_range(layout,layout_size);
    if(!layout_data || layout_size>sizeof s_layout)goto invalid;
    memcpy(s_layout,layout_data,layout_size);
    if(tri_rd16(info+22) || !megamix_geometry_open(&s_geometry,
            s_layout,layout_size,
            bank_range(chunks,chunks_size),chunks_size,tri_rd16(info+20)))goto invalid;
    s_stage=r16(RAM_Current_zone_and_act);s_chunks_address=chunks;
    if(bank_range(info,52)) {
        uint32_t pixels=tri_rd32(info+40),bytes=tri_rd32(info+44),lines=tri_rd32(info+48);
        if(bytes && (!bank_range(pixels,bytes) || !bank_range(lines,bytes/32) ||
           !gvdp_set_sprite_patterns(bank_range(pixels,bytes),bytes,bank_range(lines,bytes/32))))goto invalid;
    }
    return;
invalid:
    fprintf(stderr,"[megamix] invalid converted geometry for stage %04X\n",r16(RAM_Current_zone_and_act));
    exit(EXIT_FAILURE);
}

int tri_megamix_lz_patch(uint8_t chunk)
{
    if(!current())return 0;
    uint32_t e=tri_dir(),info=tri_rd32(e+TD_EXTRA);
    if(tri_rd8(info+24)!=1 || tri_rd8(info+25)!=2)return 0;
    uint32_t swaps=tri_rd32(info+52)+(chunk==7?8u:0u);
    if(!bank_range(swaps,8))return 0;
    for(unsigned q=0;q<4;q++) {
        unsigned offset=12u+2u*((4u+(q>>1))*s_geometry.width[0]+12u+(q&1));
        uint16_t id=tri_rd16(swaps+q*2);
        if(id>=s_geometry.chunk_count || offset+1>=s_geometry.layout_size)return 0;
        s_layout[offset]=(uint8_t)(id>>8);s_layout[offset+1]=(uint8_t)id;
    }
    return 1;
}

int tri_megamix_geometry_cell(int x,int y,int background,int wrap,uint16_t *cell)
{
    if(!current())return 0;
    *cell=megamix_geometry_cell(&s_geometry,background,x,y,wrap);
    return 1;
}
int tri_megamix_geometry_owns(uint32_t pc)
{
    switch(pc) { MG_GEOMETRY_CASES return 1; default:return 0; }
}
static void low_word(unsigned reg,unsigned value)
{ D(reg)=(D(reg)&0xFFFF0000u)|(value&0xFFFFu); }

/* Final ADD.W flags for the original routine's logical chunk offset. The
 * pointer itself is a full bank address; MOVEA does not change these flags.
 * D0/D4's untouched upper words and every unrelated register are preserved. */
static void add_flags(unsigned left,unsigned right)
{
    unsigned sum=(left&0xFFFFu)+(right&0xFFFFu),result=sum&0xFFFFu;
    g_cpu.SR&=~0x1Fu;
    if(!result)g_cpu.SR|=SR_Z;
    if(result&0x8000u)g_cpu.SR|=SR_N;
    if(sum>0xFFFFu)g_cpu.SR|=SR_C|SR_X;
    if(!((left^right)&0x8000u) && ((left^result)&0x8000u))g_cpu.SR|=SR_V;
}
static unsigned chunk(int plane,int x,int y)
{
    /* The row mask belongs to the live guest layout. It can select empty
     * guard rows beyond the donor's height; those must stay empty. */
    unsigned row=(((unsigned)(uint16_t)y>>5)&r16(RAM_Layout_row_index_mask))>>2;
    return megamix_geometry_chunk(&s_geometry,plane,x,(int)row,0);
}
int tri_megamix_geometry_hook(uint32_t pc)
{
    if(!current())return 0;
    switch(pc) {
    case MG_PC_SK_FG:case MG_PC_S3_FG:
    case MG_PC_SK_BG:case MG_PC_S3_BG: {
        int bg=pc==MG_PC_SK_BG || pc==MG_PC_S3_BG;
        unsigned x=(uint16_t)D(3),y=(uint16_t)D(2);
        unsigned id=chunk(bg,(int)(x>>7),(int)y),row=y&0x70u,col=(x&0x70u)>>3;
        low_word(0,row);low_word(4,col);
        D(1)=A(1)=s_chunks_address+id*128u+row+col;
        add_flags(id*128u+row,col);
        break;
    }
    case MG_PC_SK_ROW:case MG_PC_S3_ROW: {
        int bg=(A(3)&0xFFFFu)==((RAM_Level_layout_header+10)&0xFFFFu);
        unsigned id=chunk(bg,(int16_t)D(1),(uint16_t)D(0));
        unsigned row=(uint16_t)D(0)&0x70u;
        low_word(4,row);D(3)=A(5)=s_chunks_address+id*128u+row;
        add_flags(id*128u,row);
        break;
    }
    default:return 0;
    }
    /* Same RTS bookkeeping as generated retail routines. The caller owns
     * the guest return slot and performs its normal JSR stack cleanup. */
    if(g_split_sp_popped>0) {g_rte_pending=g_split_sp_popped;g_split_sp_popped=0;}
    return 1;
}
