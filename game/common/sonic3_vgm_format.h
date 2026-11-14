#pragma once
/* Import-cache format, little endian, validated independently of synthesis. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"
static uint32_t s3_vgm_u32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t s3_vgm_u64(const uint8_t *p){return s3_vgm_u32(p)|((uint64_t)s3_vgm_u32(p+4)<<32);}
static int s3_vgm_valid(const uint8_t *p,size_t n)
{
    if(n<48||memcmp(p,"S3VG",4)||s3_vgm_u32(p+4)!=1)return 0;
    unsigned count=s3_vgm_u32(p+8),loop=s3_vgm_u32(p+12);uint64_t total=s3_vgm_u64(p+16),start=s3_vgm_u64(p+24),previous=0;
    if(!count||count>2000000||n!=32u+(size_t)count*16||total<1217||total>UINT64_C(53693175)*600)return 0;
    if(loop!=UINT32_MAX&&(loop>=count||start>=total||s3_vgm_u64(p+32+loop*16)<start))return 0;
    if(loop==UINT32_MAX&&start)return 0;
    if(loop!=UINT32_MAX&&(total-start<1217||(uint64_t)(count-loop)*53693175>(total-start)*500000))return 0;
    unsigned burst=0;uint64_t window=0;
    for(unsigned i=0;i<count;i++){const uint8_t *e=p+32+i*16;uint64_t at=s3_vgm_u64(e);
        if(at<previous||at>total||e[8]>2||e[11]||s3_vgm_u32(e+12)||(e[8]==0&&e[10]))return 0;previous=at;}
    for(unsigned i=0;i<count;i++){const uint8_t *e=p+32+i*16;uint64_t at=s3_vgm_u64(e)/895680;
        if(at!=window){window=at;burst=0;}if(++burst>32768||(e[8]==1&&e[9]==0x27&&(e[10]&0xc0)))return 0;}
    return 1;
}
static uint8_t *s3_vgm_read(const char *path,const char *hash,size_t *bytes)
{
    FILE *f=fopen(path,"rb");if(!f)return NULL;if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);rewind(f);
    if(n<48||n>32000032){fclose(f);return NULL;}uint8_t *raw=malloc((size_t)n);int ok=raw&&fread(raw,1,(size_t)n,f)==(size_t)n;fclose(f);
    if(ok){uint8_t digest[32];char actual[65];recompui_sha256_compute(raw,(size_t)n,digest);for(unsigned i=0;i<32;i++)snprintf(actual+i*2,3,"%02x",digest[i]);ok=!strcmp(actual,hash)&&s3_vgm_valid(raw,(size_t)n);}
    if(!ok){free(raw);return NULL;}if(bytes)*bytes=(size_t)n;return raw;
}
