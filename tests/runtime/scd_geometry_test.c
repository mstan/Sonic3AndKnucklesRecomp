#include "scd_geometry.h"
#include "scd_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static unsigned w(const uint8_t *p){return ((unsigned)p[0]<<8)|p[1];}
static uint32_t l(const uint8_t *p){return ((uint32_t)w(p)<<16)|w(p+2);}
static void word(uint8_t *b,unsigned a,unsigned v){b[a]=(uint8_t)(v>>8);b[a+1]=(uint8_t)v;}
static void ptr(uint8_t *b,unsigned a,unsigned v){word(b,a,v>>16);word(b,a+2,v);}
static void synthetic(void)
{
    uint8_t b[52000]={0},snapshot[52000];
    ptr(b,0x50,SCD_WINDOW+0x100);
    word(b,0x100,400);word(b,0x102,3);word(b,0x104,2);word(b,0x106,2);word(b,0x108,1);
    ptr(b,0x10A,SCD_WINDOW+0x300);ptr(b,0x10E,SCD_WINDOW+0x200);ptr(b,0x112,SCD_WINDOW+0x210);
    word(b,0x200,1);word(b,0x202,256);word(b,0x204,399);word(b,0x206,258);
    word(b,0x210,257);word(b,0x212,259);
    word(b,0x300+256*128,0xF123);word(b,0x300+399*128+126,0x4E01);
    word(b,0x300+258*128+112,0x8122);word(b,0x300+259*128+126,0xC033);
    ScdGeometry g={b,sizeof b,0,0,0};uint16_t cell;unsigned id;uint32_t defs;
    CHECK(scd_geometry_chunk(&g,0,1,0,&defs,&id) && id==256 && defs==SCD_WINDOW+0x300);
    CHECK(scd_geometry_cell(&g,128,0,0,0x7C,&cell) && cell==0xF123);
    CHECK(scd_geometry_cell(&g,383,127,0,0x7C,&cell) && cell==0x4E01);
    CHECK(scd_geometry_cell(&g,0,240,0,0x7C,&cell) && cell==0x8122);
    CHECK(scd_geometry_cell(&g,-1,-1,1,0x7C,&cell) && cell==0xC033);
    CHECK(scd_geometry_cell(&g,-1,-1,0,0x7C,&cell) && !cell);
    CHECK(scd_geometry_cell(&g,384,0,0,0x7C,&cell) && !cell);
    CHECK(scd_geometry_cell(&g,0,256,0,0x7C,&cell) && !cell);
    CHECK(scd_geometry_cell(&g,128,4096,0,0x7C,&cell) && cell==0xF123);
    memcpy(snapshot,b,sizeof b);ScdGeometry published={snapshot,sizeof snapshot,0,0,0};
    word(b,0x300+256*128,0); /* destructible chunk edit is live, old frame is stable */
    CHECK(scd_geometry_cell(&g,128,0,0,0x7C,&cell) && !cell);
    CHECK(scd_geometry_cell(&published,128,0,0,0x7C,&cell) && cell==0xF123);
    g.background=SCD_WINDOW+0x202;g.background_width=1;g.background_height=1;
    CHECK(scd_geometry_chunk(&g,1,0,0,&defs,&id) && id==256);
    ptr(b,0x50,SCD_WINDOW+sizeof b-4);CHECK(!scd_geometry_cell(&g,0,0,0,0x7C,&cell));
    puts("CD geometry: high IDs, distant cells, seams, bounds, wrapping, background switch and frame isolation passed");
}
static void corpus(const char *path)
{
    ScdPack p={0};char error[256];CHECK(scd_pack_open(&p,path,error,sizeof error));
    unsigned stages=0,paged=0,cells=0;
    for(unsigned z=0x40;z<0x5C;z++)for(unsigned a=0;a<3;a++) {
        uint32_t size;const uint8_t *b=scd_pack_level(&p,(uint16_t)(z*256+a),&size);if(!b)continue;
        stages++;uint32_t pg=l(b+0x50);if(!pg)continue;
        paged++;const uint8_t *v=b+pg-SCD_WINDOW;ScdGeometry g={b,size,0,0,0};
        for(unsigned plane=0;plane<2;plane++) {
            unsigned width=w(v+(plane?6:2)),height=w(v+(plane?8:4));
            const uint8_t *grid=b+l(v+(plane?18:14))-SCD_WINDOW;
            const uint8_t *definitions=b+l(v+10)-SCD_WINDOW;
            for(unsigned y=0;y<height*128;y+=16)for(unsigned x=0;x<width*128;x+=16) {
                unsigned id=w(grid+((y/128)*width+x/128)*2);
                unsigned expected=w(definitions+id*128+(y&0x70)+((x&0x70)>>3));uint16_t cell;
                CHECK(scd_geometry_cell(&g,(int)x,(int)y,(int)plane,0x7C,&cell) && cell==expected);cells++;
            }
        }
    }
    CHECK(stages==70 && paged==41);
    printf("CD private corpus: %u variants, %u complete paged grids, %u exact source cells passed\n",stages,paged,cells);
    scd_resource_close(&p.resource);
}
int main(int argc,char **argv){CHECK(argc==1 || argc==2);synthetic();if(argc==2)corpus(argv[1]);return 0;}
