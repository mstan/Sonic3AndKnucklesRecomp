#include "megamix_geometry.h"
#include <string.h>

static unsigned word(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }

int megamix_geometry_open(MegamixGeometry *g,
                         const uint8_t *layout, size_t layout_size,
                         const uint8_t *chunks, size_t chunks_size,
                         unsigned block_count)
{
    memset(g,0,sizeof *g);
    if (!layout || !chunks || layout_size<12 || memcmp(layout,"MGL1",4) ||
        !chunks_size || chunks_size%128 || chunks_size/128>65536 ||
        !block_count || block_count>768) return 0;
    unsigned fw=word(layout+4), bw=word(layout+6);
    unsigned fh=word(layout+8), bh=word(layout+10);
    if (!fw || !bw || !fh || !bh || fw>128 || bw>128 || fh>16 || bh>16 ||
        layout_size!=12u+2u*(fw*fh+bw*bh)) return 0;
    unsigned count=(unsigned)(chunks_size/128);
    for (size_t i=12;i<layout_size;i+=2)
        if (word(layout+i)>=count) return 0;
    for (size_t i=0;i<chunks_size;i+=2)
        if ((word(chunks+i)&0x3FFu)>=block_count || (i<128 && word(chunks+i))) return 0;
    g->layout=layout; g->chunks=chunks;
    g->layout_size=layout_size; g->chunks_size=chunks_size;
    g->width[0]=fw; g->width[1]=bw; g->height[0]=fh; g->height[1]=bh;
    g->chunk_count=count; g->block_count=block_count;
    return 1;
}

static int modulo(int value,unsigned period)
{ int r=value%(int)period; return r<0?r+(int)period:r; }

unsigned megamix_geometry_chunk(const MegamixGeometry *g,int plane,int cx,int cy,int wrap)
{
    if (!g || !g->layout || plane<0 || plane>1) return 0;
    unsigned w=g->width[plane],h=g->height[plane];
    if (wrap) { cx=modulo(cx,w); cy=modulo(cy,h); }
    if (cx<0 || cy<0 || (unsigned)cx>=w || (unsigned)cy>=h) return 0;
    size_t offset=12u+2u*((plane?g->width[0]*g->height[0]:0u)+(unsigned)cy*w+(unsigned)cx);
    return word(g->layout+offset);
}

uint16_t megamix_geometry_cell(const MegamixGeometry *g,int plane,int x,int y,int wrap)
{
    if (!g || !g->chunks) return 0;
    /* Floor division is explicit: signed right shifts are implementation-defined. */
    int cx=x>=0?x/128:-1-(-(x+1))/128;
    int cy=y>=0?y/128:-1-(-(y+1))/128;
    unsigned chunk=megamix_geometry_chunk(g,plane,cx,cy,wrap);
    size_t offset=chunk*128u+((unsigned)y&112u)+(((unsigned)x&112u)>>3);
    return (uint16_t)word(g->chunks+offset);
}
