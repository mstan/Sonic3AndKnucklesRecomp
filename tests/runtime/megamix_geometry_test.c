#include "megamix_geometry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} }while(0)
static void word(uint8_t *p,unsigned a,unsigned v){p[a]=(uint8_t)(v>>8);p[a+1]=(uint8_t)v;}
static void synthetic(void)
{
    uint8_t layout[28]={ 'M','G','L','1' },chunks[280*128]={0};
    MegamixGeometry g;
    word(layout,4,3);word(layout,6,2);word(layout,8,2);word(layout,10,1);
    word(layout,12,1);word(layout,14,256);word(layout,16,279);
    word(layout,18,258);word(layout,24,257);word(layout,26,259);
    word(chunks,256*128,0xF123);word(chunks,279*128+126,0x4E01);
    word(chunks,258*128+112,0x8122);word(chunks,259*128+126,0xC033);
    CHECK(megamix_geometry_open(&g,layout,sizeof layout,chunks,sizeof chunks,520));
    CHECK(megamix_geometry_cell(&g,0,128,0,0)==0xF123);
    CHECK(megamix_geometry_cell(&g,0,383,127,0)==0x4E01);
    CHECK(megamix_geometry_cell(&g,0,0,240,0)==0x8122);
    CHECK(megamix_geometry_cell(&g,1,-1,-1,1)==0xC033);
    CHECK(!megamix_geometry_cell(&g,0,-1,-1,0));
    CHECK(!megamix_geometry_cell(&g,0,384,0,0));
    CHECK(!megamix_geometry_cell(&g,0,0,256,0));
    CHECK(megamix_geometry_chunk(&g,0,1,0,0)==256);
    word(layout,14,280);CHECK(!megamix_geometry_open(&g,layout,sizeof layout,chunks,sizeof chunks,520));
    CHECK(!g.layout);word(layout,14,256);
    CHECK(!megamix_geometry_open(&g,layout,sizeof layout-1,chunks,sizeof chunks,520));
    CHECK(!megamix_geometry_open(&g,layout,sizeof layout,chunks,sizeof chunks-1,520));
    word(chunks,0,1);CHECK(!megamix_geometry_open(&g,layout,sizeof layout,chunks,sizeof chunks,520));
    word(chunks,0,0);word(chunks,128,520);
    CHECK(!megamix_geometry_open(&g,layout,sizeof layout,chunks,sizeof chunks,520));
    puts("Megamix geometry: 16-bit chunk IDs, seams, plane bounds, negative wrapping and invalid data passed");
}

static uint8_t *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>=0);
    rewind(f);uint8_t *p=malloc((size_t)n+1);CHECK(p);
    CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;
}

int main(int argc,char **argv)
{
    synthetic();
    /* Optional private corpus: validate the exact exported payload using the
     * same portable C reader used by collision and both rendering paths. */
    if(argc==4) {
        size_t ls,cs;uint8_t *l=read_file(argv[1],&ls),*c=read_file(argv[2],&cs);
        MegamixGeometry g;CHECK(megamix_geometry_open(&g,l,ls,c,cs,(unsigned)atoi(argv[3])));
        unsigned checked=0;
        for(int p=0;p<2;p++)for(unsigned y=0;y<g.height[p]*128;y+=16)
            for(unsigned x=0;x<g.width[p]*128;x+=16) {
                unsigned index=megamix_geometry_chunk(&g,p,(int)x/128,(int)y/128,0);
                unsigned offset=index*128+(y&112)+((x&112)>>3);
                CHECK(megamix_geometry_cell(&g,p,(int)x,(int)y,0)==((c[offset]<<8)|c[offset+1]));
                checked++;
            }
        printf("Validated %u chunks and %u cells through the runtime reader\n",g.chunk_count,checked);
        free(l);free(c);
    } else CHECK(argc==1);
    return 0;
}
