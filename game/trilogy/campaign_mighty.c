#include "campaign_mighty.h"
#include <stdlib.h>
#include <string.h>
#define BASE 0x400000u
static uint32_t rd(const uint8_t *p) {return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static unsigned word(const uint8_t *p) {return (p[0]<<8)|p[1];}
static void wr(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int bounds(uint32_t p,uint32_t n,uint32_t size) {return p>=BASE && p-BASE<=size && n<=size-(p-BASE);}
typedef struct Copy {uint8_t *out;uint32_t used,capacity;const uint8_t *src;uint32_t size;} Copy;
static uint32_t copy(Copy *c,uint32_t p,uint32_t n)
{
    if(!bounds(p,n,c->size)||n>c->capacity-c->used)return 0;
    uint32_t result=BASE+c->used;memcpy(c->out+c->used,c->src+p-BASE,n);c->used+=(n+1)&~1u;return result;
}
static uint32_t table(Copy *c,uint32_t p,unsigned count,unsigned unit)
{
    if(!bounds(p,count*2,c->size))return 0;
    unsigned n=count*2;
    for(unsigned i=0;i<count;i++) {
        unsigned off=word(c->src+p-BASE+i*2);
        if(off<count*2 || !bounds(p+off,2,c->size))return 0;
        unsigned entries=word(c->src+p-BASE+off),end=off+2+entries*unit;
        if(entries>80 || end>65536 || !bounds(p,end,c->size))return 0;
        if(end>n)n=end;
    }
    return copy(c,p,n);
}
static uint32_t character_copy(uint8_t **dst,uint32_t *dst_size,const uint8_t *src,uint32_t size,unsigned field,const char *magic)
{
    if(!dst||!*dst||!dst_size||!src||size<0x28||*dst_size>0x400000-88)return 0;
    uint32_t metadata=rd(src+0x24);
    if(!bounds(metadata,field+4,size)||memcmp(src+metadata-BASE,"MDI1",4))return 0;
    if(field==40 && word(src+metadata-BASE+10)!=5)return 0;
    uint32_t d=rd(src+metadata-BASE+field);
    if(!bounds(d,88,size)||memcmp(src+d-BASE,magic,4))return 0;
    const uint8_t *m=src+d-BASE;uint8_t descriptor[88];memcpy(descriptor,m,88);
    Copy c={malloc(0x30000),0,0x30000,src,size};if(!c.out)return 0;
    /* Temporarily build at a zero-based guest address, then adjust only the
     * explicit descriptor and PLC pointer fields by the final append offset. */
    const unsigned offsets[]={4,8,12,16,20,36,40,44,48,64,68,76,80,84};
    uint32_t values[14];int ok=rd(m+24)<=0x20000 && rd(m+28)==480 && rd(m+72)<=0x400;
    for(unsigned i=0;i<14 && ok;i++) {
        unsigned k=offsets[i];uint32_t p=rd(m+k),n=0;
        if(k==4||k==8)values[i]=table(&c,p,176,k==4?6:2);
        else if(k==64)values[i]=table(&c,p,36,6);
        else if(k==84)values[i]=table(&c,p,6,6);
        else {
            n=k==12?rd(m+24):k==20?480:k==44||k==48?128:k==68?rd(m+72):k==76?128:k==80?192:32;
            values[i]=copy(&c,p,n);
        }
        if(!values[i])ok=0;
    }
    uint32_t plc[3]={0};
    for(unsigned i=0;i<3 && ok;i++) {
        uint32_t p=rd(m+52+i*4);if(!bounds(p,8,size)){ok=0;break;}
        uint32_t pal=copy(&c,rd(src+p-BASE),32);plc[i]=copy(&c,p,8);
        if(!pal||!plc[i]){ok=0;break;}
        wr(c.out+plc[i]-BASE,pal+((*dst_size+1)&~1u));
    }
    uint32_t append=(*dst_size+1)&~1u,result=BASE+append+c.used;
    if(ok && append+c.used+88<=0x400000) {
        for(unsigned i=0;i<14;i++)wr(descriptor+offsets[i],values[i]+append);
        for(unsigned i=0;i<3;i++)wr(descriptor+52+i*4,plc[i]+append);
        uint8_t *expanded=realloc(*dst,append+c.used+88);
        if(expanded) {
            memcpy(expanded+append,c.out,c.used);memcpy(expanded+append+c.used,descriptor,88);
            *dst=expanded;*dst_size=append+c.used+88;
        } else result=0;
    } else result=0;
    free(c.out);return result;
}
uint32_t campaign_mighty_copy(uint8_t **dst,uint32_t *n,const uint8_t *src,uint32_t size)
{return character_copy(dst,n,src,size,12,"MDY3");}
uint32_t campaign_shadow_copy(uint8_t **dst,uint32_t *n,const uint8_t *src,uint32_t size)
{return character_copy(dst,n,src,size,40,"MSW1");}
