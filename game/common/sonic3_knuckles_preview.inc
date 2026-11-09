/* Original AIZ2 waiting pose, visible in the expanded margin before the native
 * controller allocates its cutscene actor. This is derived presentation only:
 * it never creates an object, advances a timer or changes native art/palettes.
 */
#include <stdint.h>
#include <string.h>
#include "video/genesis_dac.h"

enum { S3_KNUX_PREVIEW_SIZE=64,
       S3_KNUX_PREVIEW_PATTERN_BYTES=18*32, S3_KNUX_PREVIEW_FRAME=22 };
#ifdef SONIC3_STANDALONE
enum { S3_KNUX_PREVIEW_ROM_SIZE=0x200000,
       S3_KNUX_PREVIEW_MAP=0x164016, S3_KNUX_PREVIEW_DPLC=0x16430E,
       S3_KNUX_PREVIEW_ART=0x182DC6, S3_KNUX_PREVIEW_PALETTE=0x457AE,
       S3_KNUX_PREVIEW_HANDOVER=0x4744A };
#else
enum { S3_KNUX_PREVIEW_ROM_SIZE=0x400000,
       S3_KNUX_PREVIEW_MAP=0x364016, S3_KNUX_PREVIEW_DPLC=0x36430E,
       S3_KNUX_PREVIEW_ART=0x382DC6, S3_KNUX_PREVIEW_PALETTE=0x66912,
       S3_KNUX_PREVIEW_HANDOVER=0x69588 };
#endif
static struct {
    const uint8_t *rom;
    int ready;
    unsigned pattern_bytes;
    uint8_t patterns[S3_KNUX_PREVIEW_PATTERN_BYTES];
    uint8_t pixels[S3_KNUX_PREVIEW_SIZE*S3_KNUX_PREVIEW_SIZE];
    uint32_t colors[16];
} s3_knux_preview_cache;

static inline unsigned s3_knux_preview_word(const uint8_t *p,unsigned a)
{ return (unsigned)p[a]<<8|p[a+1]; }

static int s3_knux_preview_needed(const uint8_t *ram)
{
    if(!ram || ram[0xF600]!=0x0C || s3_knux_preview_word(ram,0xFE10)!=1 ||
       ram[0xB038]==2)return 0;
    /* Defeat can release the camera while the boss is still retiring.
     * Keep the waiting actor on its ledge throughout that controller,
     * before loc_694D4's capsule wait. Its native pose and 120-tick wait
     * stay unchanged; its successor still allocates the real actor. */
    for(unsigned o=0xB000;o<0xCFCC;o+=0x4A) {
#ifdef SONIC3_STANDALONE
        unsigned code=(unsigned)s3_knux_preview_word(ram,o)<<16 |
                      s3_knux_preview_word(ram,o+2);
        unsigned callback=(unsigned)s3_knux_preview_word(ram,o+0x34)<<16 |
                          s3_knux_preview_word(ram,o+0x36);
        if(code==0x47360 || code==0x473C6 || code==0x47402 ||
           (code==0x53CE2 && callback==0x47360) ||
           (code==0x53008 && callback==0x47390))return 1;
#else
        if(s3_knux_preview_word(ram,o)==6 &&
           (s3_knux_preview_word(ram,o+2)==0x923E ||
            s3_knux_preview_word(ram,o+2)==0x94D4 ||
            s3_knux_preview_word(ram,o+2)==0x9526))return 1;
        /* Defeat first waits for the music fade, then uses Obj_Wait.
         * Both callbacks still own the ledge until capsule publication. */
        if(s3_knux_preview_word(ram,o)==8 &&
           s3_knux_preview_word(ram,o+0x34)==6 &&
           ((s3_knux_preview_word(ram,o+2)==0x5668 &&
             s3_knux_preview_word(ram,o+0x36)==0x9482) ||
            (s3_knux_preview_word(ram,o+2)==0x488A &&
             s3_knux_preview_word(ram,o+0x36)==0x94A4)))return 1;
#endif
    }
    return 0;
}

static inline int s3_knux_preview_load(const uint8_t *rom)
{
    if(!rom)return 0;
    if(s3_knux_preview_cache.rom==rom)return s3_knux_preview_cache.ready>0;
    memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
    s3_knux_preview_cache.rom=rom;s3_knux_preview_cache.ready=-1;
    unsigned d=S3_KNUX_PREVIEW_DPLC+(int16_t)s3_knux_preview_word(rom,
        S3_KNUX_PREVIEW_DPLC+S3_KNUX_PREVIEW_FRAME*2);
    if(d>S3_KNUX_PREVIEW_ROM_SIZE-2)return 0;
    unsigned count=s3_knux_preview_word(rom,d)+1;
    if(count>16 || d>S3_KNUX_PREVIEW_ROM_SIZE-2-count*2)return 0;
    unsigned bytes=0;
    for(unsigned i=0;i<count;++i) {
        unsigned cue=s3_knux_preview_word(rom,d+2+i*2);
        unsigned n=((cue&15)+1)*32;
        unsigned from=S3_KNUX_PREVIEW_ART+(cue&0xFFF0)*2;
        if(n>S3_KNUX_PREVIEW_PATTERN_BYTES-bytes ||
           from>S3_KNUX_PREVIEW_ROM_SIZE-n)return 0;
        memcpy(s3_knux_preview_cache.patterns+bytes,rom+from,n);bytes+=n;
    }
    unsigned m=S3_KNUX_PREVIEW_MAP+(int16_t)s3_knux_preview_word(rom,
        S3_KNUX_PREVIEW_MAP+S3_KNUX_PREVIEW_FRAME*2);
    if(m>S3_KNUX_PREVIEW_ROM_SIZE-2)return 0;
    count=s3_knux_preview_word(rom,m);
    if(count>16 || m>S3_KNUX_PREVIEW_ROM_SIZE-2-count*6)return 0;
    for(unsigned i=0;i<count;++i) {
        unsigned p=m+2+i*6,size=rom[p+1];
        if(size&0xF0)return 0;
        int cw=((size>>2)&3)+1,ch=(size&3)+1;
        int x=-(int16_t)s3_knux_preview_word(rom,p+4)-cw*8+32;
        int y=(int8_t)rom[p]+32;
        unsigned attr=s3_knux_preview_word(rom,p+2)^0x800u;
        if(x<0 || y<0 || x+cw*8>64 || y+ch*8>64 ||
           ((attr&0x7FFu)+(unsigned)(cw*ch))*32>bytes)return 0;
        for(int dy=0;dy<ch*8;++dy)for(int dx=0;dx<cw*8;++dx) {
            int ix=(attr&0x800)?cw*8-1-dx:dx;
            int iy=(attr&0x1000)?ch*8-1-dy:dy;
            unsigned tile=(attr&0x7FFu)+(unsigned)(ix/8*ch+iy/8);
            unsigned packed=s3_knux_preview_cache.patterns[tile*32+(iy&7)*4+(ix&7)/2];
            unsigned nib=(packed>>((ix&1)?0:4))&15;
            uint8_t *pixel=&s3_knux_preview_cache.pixels[(y+dy)*64+x+dx];
            if(nib && !*pixel)*pixel=(uint8_t)nib;
        }
    }
    for(unsigned i=0;i<16;++i)s3_knux_preview_cache.colors[i]=
        genesis_dac_cram_to_argb((uint16_t)s3_knux_preview_word(rom,
            S3_KNUX_PREVIEW_PALETTE+i*2),GENESIS_DAC_NORMAL);
    s3_knux_preview_cache.pattern_bytes=bytes;s3_knux_preview_cache.ready=1;
    return 1;
}

static void s3_knux_preview_scanline(const uint8_t *rom,int line,uint32_t *out,
                                   int width,int origin,int camera_x,int camera_y)
{
    int64_t y=(int64_t)line-0x17D+camera_y+32;
    if(!out || width<=320 || y<0 || y>=64 || !s3_knux_preview_load(rom))return;
    int64_t left=(int64_t)origin+0x4B8E - camera_x-32;
    for(int dx=0;dx<64;++dx) {
        int64_t x=left+dx;
        if(x<0 || x>=width || (x>=origin && x<(int64_t)origin+320))continue;
        unsigned nib=s3_knux_preview_cache.pixels[(unsigned)y*64+(unsigned)dx];
        if(nib)out[(int)x]=s3_knux_preview_cache.colors[nib];
    }
}
