/* Game-owned independent content contract; see tools/trilogy/scd_resource.py.
 * No host structures, guessed pointer relocation or input-disc parsing. */
#include "scd_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t be32(const uint8_t *p)
{return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static unsigned be16(const uint8_t *p){return ((unsigned)p[0]<<8)|p[1];}
static void crc_table(uint32_t t[256])
{for(unsigned i=0;i<256;i++){uint32_t c=i;for(unsigned k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&(0u-(c&1)));t[i]=c;}}
static uint32_t crc_update(uint32_t c,const uint8_t *p,unsigned n,const uint32_t t[256])
{while(n--)c=t[(c^*p++)&255]^(c>>8);return c;}
static void item_name(unsigned i,int audio,char out[16])
{
    static const char *names[]={"LEVELS","SCENES","SNCBNK1B.BIN","SNCBNK3B.BIN","SNCBNK4B.BIN",
        "SNCBNK5B.BIN","SNCBNK6B.BIN","SNCBNK7B.BIN","SNCBNK8B.BIN","SNCBNKB1.BIN","SNCBNKB2.BIN"};
    memset(out,0,16);if(audio)snprintf(out,16,"TRACK%02u",i+2);else snprintf(out,16,"%s",names[i]);
}
void scd_resource_close(ScdResource *r){if(r){free(r->data);memset(r,0,sizeof *r);}}
const ScdResourceItem *scd_resource_find(const ScdResource *r,const char *name)
{if(r && name)for(unsigned i=0;i<r->count;i++)if(!strcmp(r->items[i].name,name))return &r->items[i];return NULL;}
const uint8_t *scd_resource_item(const ScdResource *r,const char *name,uint32_t *size)
{
    const ScdResourceItem *i=scd_resource_find(r,name);
    if(!i || !r->data)return NULL;
    if(size)*size=i->size;
    return r->data+i->offset;
}
int scd_resource_open(ScdResource *out,const char *path,int audio,char *error,unsigned capacity)
{
    static const uint8_t source[32]={0xdb,0x17,0x11,0x0d,0x5c,0x9f,0x80,0xe7,0xa0,0xec,0x92,0xea,0xd5,0xb2,0x3c,0x9e,
        0x52,0x4b,0x53,0xdf,0x9f,0xf6,0xb5,0x06,0x9c,0x73,0x52,0xe1,0xd3,0x57,0x92,0x5f};
    const char *message="Choose a Sonic CD resource built with tools/build_sonic_cd_pack.py";
    ScdResource r={0};FILE *f=NULL;uint8_t *scratch=NULL;
    if(!out || !path || !*path || strlen(path)>=sizeof r.path)goto fail;
    f=fopen(path,"rb");if(!f)goto fail;
    if(fseek(f,0,SEEK_END))goto fail;
    long n=ftell(f);unsigned count=audio?34:11,head=64+count*28;
    message="Invalid or damaged Sonic CD resource; rebuild it from your owned disc";
    if(n<(long)(head+12) || n>(audio?700L:32L)*1024*1024)goto fail;
    r.size=(uint32_t)n;r.count=count;snprintf(r.path,sizeof r.path,"%s",path);
    uint8_t h[64],directory[34*28],footer[12];
    rewind(f);if(fread(h,1,64,f)!=64 || fread(directory,28,count,f)!=count)goto fail;
    if(memcmp(h,audio?"SCDA":"SCDP",4) || be16(h+4)!=1 || be16(h+6)!=count || memcmp(h+8,source,32) ||
       be32(h+40)!=0x480000 || be32(h+44)!=0x180000)goto fail;
    for(unsigned i=48;i<64;i++)if(h[i])goto fail;
    if(fseek(f,n-12,SEEK_SET) || fread(footer,1,12,f)!=12 || memcmp(footer,"CDP1",4) || be32(footer+4)!=(uint32_t)n-12)goto fail;
    uint32_t end=head,payload=(uint32_t)n-12;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *e=directory+i*28;char name[16];item_name(i,audio,name);
        ScdResourceItem *it=r.items+i;
        if(memcmp(e,name,16))goto fail;
        memcpy(it->name,e,16);it->offset=be32(e+16);it->size=be32(e+20);it->crc=be32(e+24);
        if(it->offset!=((end+3)&~3u) || it->offset>payload || !it->size || it->size>payload-it->offset ||
           (audio && it->size%2352))goto fail;
        uint8_t padding[3];unsigned gap=it->offset-end;
        if(fseek(f,(long)end,SEEK_SET) || fread(padding,1,gap,f)!=gap)goto fail;
        for(unsigned k=0;k<gap;k++)if(padding[k])goto fail;
        end=it->offset+it->size;
    }
    if(end!=payload)goto fail;
    scratch=malloc(65536);if(!scratch)goto fail;
    uint32_t table[256],c=~0u;crc_table(table);rewind(f);
    for(uint32_t at=0;at<payload;) {
        unsigned size=payload-at>65536?65536:payload-at;
        if(fread(scratch,1,size,f)!=size)goto fail;
        c=crc_update(c,scratch,size,table);at+=size;
    }
    if(~c!=be32(footer+8))goto fail;
    for(unsigned i=0;i<count;i++) {
        const ScdResourceItem *it=r.items+i;c=~0u;
        if(fseek(f,(long)it->offset,SEEK_SET))goto fail;
        for(uint32_t at=0;at<it->size;) {
            unsigned size=it->size-at>65536?65536:it->size-at;
            if(fread(scratch,1,size,f)!=size)goto fail;
            c=crc_update(c,scratch,size,table);at+=size;
        }
        if(~c!=it->crc)goto fail;
    }
    if(!audio) {
        r.data=malloc(r.size);if(!r.data)goto fail;
        rewind(f);if(fread(r.data,1,r.size,f)!=r.size)goto fail;
        if(~crc_update(~0u,r.data,payload,table)!=be32(footer+8) || memcmp(r.data+payload,footer,12))goto fail;
    }
    fclose(f);free(scratch);*out=r;if(error && capacity)*error=0;return 1;
fail:
    if(f)fclose(f);
    free(scratch);free(r.data);
    if(error && capacity)snprintf(error,capacity,"%s",message);
    return 0;
}
