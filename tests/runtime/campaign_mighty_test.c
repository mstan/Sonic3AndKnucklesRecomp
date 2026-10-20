#include "campaign_mighty.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint32_t rd(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void wr(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
int main(int argc,char **argv)
{
    if(argc!=2 && argc!=3){puts("Private character corpus optional; run with a converted Megamix pack [shadow]");return 0;}
    int shadow=argc==3 && !strcmp(argv[2],"shadow");unsigned field=shadow?40:12;
    uint32_t (*attach)(uint8_t **,uint32_t *,const uint8_t *,uint32_t)=shadow?campaign_shadow_copy:campaign_mighty_copy;
    FILE *f=fopen(argv[1],"rb");CHECK(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    CHECK(n>0 && n<=0x400000);uint8_t *source=malloc((size_t)n),*dst=malloc(0x2040);CHECK(source&&dst);
    CHECK(fread(source,1,(size_t)n,f)==(size_t)n);fclose(f);memset(dst,0xA5,0x2040);
    uint32_t size=0x2040,d=attach(&dst,&size,source,(uint32_t)n);CHECK(d>=0x402040 && d+88<=0x400000+size);
    for(unsigned i=0;i<0x2040;i++)CHECK(dst[i]==0xA5);
    const uint8_t *a=source+rd(source+rd(source+0x24)-0x400000+field)-0x400000,*b=dst+d-0x400000;
    CHECK(!memcmp(a,b,4));
    const unsigned fields[]={4,8,12,16,20,36,40,44,48,64,68,76,80,84};
    for(unsigned i=0;i<14;i++) {
        unsigned k=fields[i];const uint8_t *x=source+rd(a+k)-0x400000,*y=dst+rd(b+k)-0x400000;
        unsigned bytes=k==12?rd(a+24):k==20?480:k==44||k==48?128:k==68?rd(a+72):k==76?128:k==80?192:32;
        unsigned count=k==4||k==8?176:k==64?36:k==84?6:0;
        if(count) {
            CHECK(!memcmp(x,y,count*2));
            for(unsigned j=0;j<count;j++) {
                unsigned off=(x[j*2]<<8)|x[j*2+1],pieces=(x[off]<<8)|x[off+1];
                CHECK(!memcmp(x+off,y+off,2+pieces*(k==8?2:6)));
            }
        } else CHECK(!memcmp(x,y,bytes));
    }
    for(unsigned i=0;i<3;i++) {
        const uint8_t *x=source+rd(a+52+i*4)-0x400000,*y=dst+rd(b+52+i*4)-0x400000;
        CHECK(!memcmp(x+4,y+4,4));CHECK(!memcmp(source+rd(x)-0x400000,dst+rd(y)-0x400000,32));
    }
    uint32_t oldsize=size;wr(source+rd(source+0x24)-0x400000+field,0);
    CHECK(!attach(&dst,&size,source,(uint32_t)n));CHECK(size==oldsize);
    free(source);free(dst);printf("%s attachment: all 176 maps/DPLCs, 38 animations, art, palettes and UI preserved; malformed graph rejected\n",shadow?"Shadow":"Mighty");return 0;
}
