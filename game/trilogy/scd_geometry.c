/* Complete 16-bit layouts shared by collision and published-frame rendering. */
#include "scd_geometry.h"
#include "scd_pack.h"
static const uint8_t *at(const ScdGeometry *g,uint32_t a,unsigned n)
{
    if(!g->blob || a<SCD_WINDOW || a-SCD_WINDOW>g->size || n>g->size-(a-SCD_WINDOW))return 0;
    return g->blob+a-SCD_WINDOW;
}
static unsigned w(const uint8_t *p){return ((unsigned)p[0]<<8)|p[1];}
static uint32_t l(const uint8_t *p){return ((uint32_t)w(p)<<16)|w(p+2);}
static const uint8_t *pager(const ScdGeometry *g)
{
    const uint8_t *p=at(g,SCD_WINDOW+0x50,4);
    return p && l(p)?at(g,l(p),22):0;
}
static void plane_info(const ScdGeometry *g,const uint8_t *p,int bg,unsigned *width,unsigned *height,uint32_t *grid)
{
    *width=w(p+(bg?6:2));*height=w(p+(bg?8:4));*grid=l(p+(bg?18:14));
    if(bg && g->background){*width=g->background_width;*height=g->background_height;*grid=g->background;}
}
int scd_geometry_chunk(const ScdGeometry *g,int plane,int column,int row,uint32_t *definitions,unsigned *id)
{
    const uint8_t *p=pager(g);if(!p)return 0;
    unsigned width,height;uint32_t grid;plane_info(g,p,plane,&width,&height,&grid);
    *definitions=l(p+10);*id=0;
    if(column>=0 && row>=0 && (unsigned)column<width && (unsigned)row<height) {
        uint64_t offset=((uint64_t)(unsigned)row*width+(unsigned)column)*2;
        const uint8_t *v=offset<=UINT32_MAX-grid?at(g,grid+(uint32_t)offset,2):0;
        if(v && w(v)<w(p))*id=w(v);
    }
    return 1;
}
int scd_geometry_cell(const ScdGeometry *g,int x,int y,int background,unsigned row_mask,uint16_t *cell)
{
    const uint8_t *p=pager(g);if(!p)return 0;
    unsigned width,height;uint32_t grid;plane_info(g,p,background,&width,&height,&grid);
    *cell=0;
    if(!width || !height || width>128 || height>32)return 1;
    int column=x>>7,row=background?y>>7:(int)((((unsigned)(uint16_t)y>>5)&row_mask)>>2);
    if(background){column=(column%(int)width+(int)width)%(int)width;row=(row%(int)height+(int)height)%(int)height;}
    uint32_t definitions;unsigned id;
    scd_geometry_chunk(g,background,column,row,&definitions,&id);
    if(column<0 || row<0 || (unsigned)column>=width || (unsigned)row>=height)return 1;
    unsigned offset=id*128+((unsigned)y&0x70)+(((unsigned)x&0x70)>>3);
    const uint8_t *v=offset<=UINT32_MAX-definitions?at(g,definitions+offset,2):0;
    if(v)*cell=(uint16_t)w(v);
    return 1;
}
