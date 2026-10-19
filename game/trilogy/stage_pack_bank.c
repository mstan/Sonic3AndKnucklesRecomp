/* Local converted data is integrity-checked before any hooks or save loading.
 * SPK1 footer: payload length and CRC32, both big endian. */
#include "stage_pack.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
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
    if (memcmp(data,"TRLG\0\1",6) || memcmp(footer,"SPK1",4) ||
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
    message = NULL;
done:
    if(f) fclose(f);
    free(data);
    if(error && capacity) snprintf(error,capacity,"%s",message?message:"");
    return !message;
}
