/* Local converted data is integrity-checked before any hooks or save loading.
 * SPK1 footer: payload length and CRC32, both big endian. */
#include "stage_pack.h"
#include "scd_pack.h"
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
static int character_graph(const uint8_t *data,uint32_t size,uint32_t descriptor)
{
    if(!range(descriptor,88,size))return 0;
    const uint8_t *m=data+descriptor-0x400000u;
    if(memcmp(m,"MSW1",4) || m[32] || m[33]!=176 || m[34] || m[35]!=38 ||
       be32(m+24)>0x20000 || be32(m+28)!=480 || be32(m+72)>0x400)return 0;
    const unsigned fields[]={4,8,12,16,20,36,40,44,48,64,68,76,80,84};
    for(unsigned k=0;k<14;k++) {
        unsigned f=fields[k],count=f==4||f==8?176:f==64?36:f==84?6:0;
        uint32_t p=be32(m+f),bytes=count?count*2:f==12?be32(m+24):f==20?480:
            f==44||f==48?128:f==68?be32(m+72):f==76?128:f==80?192:32;
        if(!range(p,bytes,size))return 0;
        for(unsigned i=0;i<count;i++) {
            const uint8_t *offset=data+p-0x400000u+i*2;
            unsigned off=((unsigned)offset[0]<<8)|offset[1];
            if(off<count*2 || !range(p+off,2,size))return 0;
            const uint8_t *entry=data+p+off-0x400000u;
            unsigned n=((unsigned)entry[0]<<8)|entry[1];
            if(n>80 || !range(p+off,2+n*(f==8?2:6),size))return 0;
            if(f==8)for(unsigned j=0;j<n;j++) {
                unsigned v=((unsigned)entry[2+j*2]<<8)|entry[3+j*2];
                if(((v&4095)+((v>>12)+1))*32>be32(m+24))return 0;
            }
        }
    }
    for(unsigned i=0;i<3;i++) {
        uint32_t p=be32(m+52+i*4);if(!range(p,8,size))return 0;
        const uint8_t *plc=data+p-0x400000u;
        if(!range(be32(plc),32,size) || plc[6] || plc[7]!=7)return 0;
    }
    return 1;
}
int s3_pack_validate(const char *path, int content, char *error, unsigned capacity)
{
    if(content==4)return scd_pack_validate(path,error,capacity);
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
    if(content<0 || content>3)goto done;
    if(!megamix && content==3) {
        message="Megamix stages and Mighty require an extended bank built with tools/build_megamix_pack.py";
        goto done;
    }
    if (memcmp(data,"TRLG\0\1",6) || (!megamix && memcmp(footer,"SPK1",4)) ||
        be32(footer+4)!=(uint32_t)size-12 || crc32(data,(size_t)size-12)!=be32(footer+8)) goto done;
    /* CPK1 tags an independently installable campaign. Legacy combined
     * banks remain accepted for migration, but never imply another toggle. */
    if(!memcmp(data+0x28,"CPK1",4) && (data[0x2C]!=content || !content || data[0x2D]!=1 || data[0x2E] || data[0x2F])) {
        message="This content pack belongs to a different campaign";goto done;
    }
    /* All 39 acts must be present. This pack is a complete combined campaign. */
    static const uint8_t zones[][2]={{0x20,3},{0x21,4},{0x22,3},{0x23,3},{0x24,3},{0x25,3},
        {0x28,2},{0x29,2},{0x2a,2},{0x2b,2},{0x2c,2},{0x2d,2},{0x2e,2},{0x2f,3},
        {0x30,1},{0x31,1},{0x32,1}};
    for (unsigned z=0;z<sizeof zones/sizeof zones[0];z++)
        for(unsigned a=0;a<zones[z][1];a++) {
            if(content==3 || (content==1 && zones[z][0]>=0x28) ||
               (content==2 && zones[z][0]<0x28))continue;
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
           data[p+10]!=0 || data[p+11]<2 || data[p+11]>5)goto done;
        if(data[p+11]>=5 && (p>payload-44 || !character_graph(data,payload,be32(data+p+40))))goto done;
        if(data[p+11]>=3) {
            if(p>payload-36 || !range(be32(data+p+28),256,payload) ||
               !range(be32(data+p+32),256,payload))goto done;
        } else if(content==3) {
            message="Upgrade the converted bank with --upgrade-presentation for Megamix visual fixes";
            goto done;
        }
        if(data[p+11]>=4) {
            unsigned characters=data[p+11]>=5?3:2;
            if(p>payload-40 || !range(be32(data+p+36),12+characters*20,payload))goto done;
            const uint8_t *sign=data+be32(data+p+36)-0x400000u;
            if(memcmp(sign,data[p+11]>=5?"MGS2":"MGS1",4) || !range(be32(sign+4),10,payload) || !range(be32(sign+8),28,payload))goto done;
            for(unsigned c=0;c<characters;c++) {
                const uint8_t *r=sign+12+c*20;
                if(be32(r+4)!=58*32 || !range(be32(r),58*32,payload))goto done;
                for(unsigned k=8;k<20;k+=4)if(!range(be32(r+k),32,payload))goto done;
            }
        } else if(content==3) {
            message="Upgrade the converted bank with --upgrade-presentation for corrected Megamix signposts";
            goto done;
        }
        uint32_t mighty=be32(data+p+12),music=be32(data+p+16),order=be32(data+p+20),cycles=be32(data+p+24);
        if(!range(mighty,64,payload) || !range(music,112,payload) ||
           !range(order,38,payload) || !range(cycles,54*4,payload))goto done;
        const uint8_t *m=data+mighty-0x400000u;
        int native_hud=!memcmp(m,"MDY3",4);
        int ui=native_hud || !memcmp(m,"MDY2",4);
        if(content==3 && !native_hud) {
            message="Upgrade the converted bank with --upgrade-presentation for Mighty's native HUD lettering";
            goto done;
        }
        if((memcmp(m,"MDY1",4) && !ui) || m[32]!=0 || m[33]!=176 || m[34]!=0 || m[35]!=38 ||
           be32(m+28)!=480 || !range(be32(m+4),352,payload) || !range(be32(m+8),352,payload) ||
           !range(be32(m+12),be32(m+24),payload) || !range(be32(m+16),32,payload) ||
           !range(be32(m+20),480,payload))goto done;
        if(ui && (!range(mighty,84,payload) || !range(be32(m+64),72,payload) ||
           !range(be32(m+68),be32(m+72),payload) || be32(m+72)>0x400 ||
           !range(be32(m+76),128,payload) || !range(be32(m+80),native_hud?192:128,payload)))goto done;
        if(native_hud) {
            if(!range(mighty,88,payload) || !range(be32(m+84),12,payload))goto done;
            uint32_t maps=be32(m+84);
            for(unsigned i=0;i<6;i++) {
                const uint8_t *entry=data+maps-0x400000u+i*2;
                uint32_t record=maps+((uint32_t)entry[0]<<8)+entry[1];
                if(!range(record,2,payload))goto done;
                const uint8_t *r=data+record-0x400000u;
                unsigned count=((unsigned)r[0]<<8)|r[1];
                if(count>14 || !range(record,2+count*6,payload))goto done;
            }
        }
        for(unsigned i=0;i<3;i++) {
            uint32_t pal=be32(m+52+i*4);
            if(!range(pal,8,payload))goto done;
            const uint8_t *point=data+pal-0x400000u;
            if(!range(be32(point),32,payload) || point[6] || point[7]!=7)goto done;
        }
        static const uint16_t acts[]={0x3400,0x3401,0x3402,0x3600,0x3601,0x3602,
            0x3800,0x3801,0x3802,0x3500,0x3501,0x3502,0x3700,0x3701,0x3702,
            0x3900,0x3901,0x3503,0x3902};
        for(unsigned i=0;content==3 && i<19;i++) {
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
int s3_stage_pack_validate(const char *path,char *error,unsigned capacity)
{ return s3_pack_validate(path,0,error,capacity); }

/* Require the owner's actual donor at selection and each boot, even when a
 * previously converted pack is installed. Never infer ownership from a pack. */
int s3_megamix_rom_validate(const char *path,char *error,unsigned capacity)
{
    const char *message="Select your Sonic Megamix v3 ROM (extract ZIP archives first)";
    FILE *f=path && *path?fopen(path,"rb"):NULL;
    int ok=0;
    if(f) {
        if(!fseek(f,0,SEEK_END) && ftell(f)==1283224L) {
            rewind(f);uint8_t *data=malloc(1283224);
            if(data) {
                ok=fread(data,1,1283224,f)==1283224 && fgetc(f)==EOF && crc32(data,1283224)==0x66ECB4F4u;
                free(data);
            }
        }
        fclose(f);
        message=ok?"Verified Sonic Megamix v3 ROM (shared by all three mods)":"Expected raw Megamix v3: 1,283,224 bytes, CRC32 66ECB4F4";
    }
    if(error && capacity)snprintf(error,capacity,"%s",message);
    return ok;
}
