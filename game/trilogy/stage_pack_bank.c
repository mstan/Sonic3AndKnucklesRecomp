/* Local converted data is integrity-checked before any hooks or save loading.
 * SPK1 footer: payload length and CRC32, both big endian. */
#include "stage_pack.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static int range(uint32_t p,uint32_t n,uint32_t size)
{ return p>=0x402040u && p-0x400000u<=size && n<=size-(p-0x400000u); }
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = ~0u;
    while (n--) { c ^= *p++; for (int i=0;i<8;i++) c=(c>>1)^(0xEDB88320u & (0u-(c&1))); }
    return ~c;
}
int s3_stage_pack_validate(const char *path, char *error, unsigned capacity)
{
    const char *message = "Choose a Sonic 1 & 2 stage bank built with tools/build_stage_pack.py";
    uint8_t *data = NULL;
    FILE *f = path && *path ? fopen(path,"rb") : NULL;
    if (!f) goto done;
    if (fseek(f,0,SEEK_END)) goto done;
    long size = ftell(f);
    if (size < 0x2040+12 || size > 0x400000 || (size&1)) goto done;
    rewind(f);
    data = malloc((size_t)size);
    if (!data || fread(data,1,(size_t)size,f)!=(size_t)size) goto done;
    const uint8_t *footer=data+size-12;
    message = "Invalid or damaged stage bank; rebuild it from your source ROMs";
    int megamix=!memcmp(footer,"SPK2",4);
    if(!megamix && (s3_stage_pack_megamix||s3_stage_pack_mighty)) {
        message="Megamix stages and Mighty require an extended bank built with tools/build_megamix_pack.py";
        goto done;
    }
    if (memcmp(data,"TRLG\0\1",6) || (!megamix && memcmp(footer,"SPK1",4)) ||
        be32(footer+4)!=(uint32_t)size-12 || crc32(data,(size_t)size-12)!=be32(footer+8)) goto done;
    /* All 39 acts must be present. This pack is a complete combined campaign. */
    static const uint8_t zones[][2]={{0x20,3},{0x21,4},{0x22,3},{0x23,3},{0x24,3},{0x25,3},
        {0x28,2},{0x29,2},{0x2a,2},{0x2b,2},{0x2c,2},{0x2d,2},{0x2e,2},{0x2f,3},
        {0x30,1},{0x31,1},{0x32,1}};
    for (unsigned z=0;z<sizeof zones/sizeof zones[0];z++)
        for(unsigned a=0;a<zones[z][1];a++) {
            const uint8_t *e=data+0x40+((zones[z][0]-0x20)*4+a)*0x40;
            static const uint8_t ptrs[]={0,4,8,0x1c,0x20,0x28,0x2c,0x30,0x34};
            for(unsigned k=0;k<sizeof ptrs;k++) {
                uint32_t p=be32(e+ptrs[k]);
                if(p<0x402040 || p-0x400000>=(uint32_t)size-12) goto done;
            }
        }
    if(megamix) {
        uint32_t p=be32(data+0x24)-0x400000u;
        uint32_t payload=(uint32_t)size-12;
        if(p>payload-28 || memcmp(data+p,"MDI1",4) ||
           be32(data+p+4)!=0x66ECB4F4 || data[p+8]!=0 || data[p+9]!=19 ||
           data[p+10]!=0 || data[p+11]!=2)goto done;
        uint32_t mighty=be32(data+p+12),music=be32(data+p+16),order=be32(data+p+20),cycles=be32(data+p+24);
        if(!range(mighty,64,payload) || !range(music,112,payload) ||
           !range(order,38,payload) || !range(cycles,54*4,payload))goto done;
        const uint8_t *m=data+mighty-0x400000u;
        if(memcmp(m,"MDY1",4) || m[32]!=0 || m[33]!=176 || m[34]!=0 || m[35]!=38 ||
           be32(m+28)!=480 || !range(be32(m+4),352,payload) || !range(be32(m+8),352,payload) ||
           !range(be32(m+12),be32(m+24),payload) || !range(be32(m+16),32,payload) ||
           !range(be32(m+20),480,payload))goto done;
        for(unsigned i=0;i<3;i++) {
            uint32_t pal=be32(m+52+i*4);
            if(!range(pal,8,payload))goto done;
            const uint8_t *point=data+pal-0x400000u;
            if(!range(be32(point),32,payload) || point[6] || point[7]!=7)goto done;
        }
        static const uint16_t acts[]={0x3400,0x3401,0x3402,0x3600,0x3601,0x3602,
            0x3800,0x3801,0x3802,0x3500,0x3501,0x3502,0x3700,0x3701,0x3702,
            0x3900,0x3901,0x3503,0x3902};
        for(unsigned i=0;i<19;i++) {
            const uint8_t *e=data+0x40+(((acts[i]>>8)-0x20)*4+(acts[i]&3))*0x40;
            if(e[0x25]!=3 || !be32(e))goto done;
            uint32_t info=be32(e+0x3C)-0x400000u;
            if(info>(uint32_t)size-12-64 || memcmp(data+info,"MGI1",4))goto done;
        }
    }
    message = NULL;
done:
    if(f) fclose(f);
    free(data);
    if(error && capacity) snprintf(error,capacity,"%s",message?message:"");
    return !message;
}
