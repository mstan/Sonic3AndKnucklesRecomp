#pragma once
#include <stddef.h>
#include <stdint.h>
/* Bounded cartridge Kosinski decoder shared by presentation/restoration. */
typedef struct { const uint8_t *p,*end; unsigned desc,bits; int error; } KosReader;
static unsigned kos_byte(KosReader *r)
{
    if(r->p==r->end){r->error=1;return 0;}
    return *r->p++;
}
static unsigned kos_bit(KosReader *r)
{
    unsigned bit=r->desc&1u;r->desc>>=1;
    if(!--r->bits) {
        unsigned low=kos_byte(r);r->desc=low|(kos_byte(r)<<8);r->bits=16;
    }
    return bit;
}
static size_t art_kos(const uint8_t *src,size_t size,uint8_t *out,size_t capacity,size_t *used)
{
    KosReader r={src,src+size,0,16,0};size_t n=0;
    unsigned low=kos_byte(&r);r.desc=low|(kos_byte(&r)<<8);
    while(!r.error) {
        if(kos_bit(&r)) {
            unsigned value=kos_byte(&r);
            if(r.error || n==capacity)return 0;
            out[n++]=(uint8_t)value;continue;
        }
        unsigned count;int offset;
        if(kos_bit(&r)) {
            unsigned lo=kos_byte(&r),hi=kos_byte(&r);
            offset=(int)(((hi&248u)<<5)|lo)-8192;count=hi&7u;
            if(!count) {
                count=kos_byte(&r);
                if(r.error)return 0;
                if(!count){*used=(size_t)(r.p-src);return n;}
                if(count==1)continue;
                ++count;
            } else count+=2;
        } else {
            count=kos_bit(&r)<<1;count=(count|kos_bit(&r))+2;
            offset=(int)kos_byte(&r)-256;
        }
        if(r.error || (size_t)-offset>n || count>capacity-n)return 0;
        while(count--){out[n]=out[n-(size_t)-offset];++n;}
    }
    return 0;
}
