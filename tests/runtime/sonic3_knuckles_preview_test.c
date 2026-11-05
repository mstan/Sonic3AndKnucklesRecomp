/* Original cutscene art stays private, and never paints the native viewport. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../game/common/sonic3_knuckles_preview.inc"
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} }while(0)
static void word(uint8_t *p,unsigned a,unsigned n){p[a]=(uint8_t)(n>>8);p[a+1]=(uint8_t)n;}
static void lng(uint8_t *p,unsigned a,unsigned n){word(p,a,n>>16);word(p,a+2,n);}
static void predicate_test(void)
{
    uint8_t ram[65536]={0};
    CHECK(!s3_knux_preview_needed(NULL));CHECK(!s3_knux_preview_needed(ram));
    ram[0xF600]=0x0C;word(ram,0xFE10,1);ram[0xB038]=1;
    lng(ram,0xCF82,0x69526);
#ifdef SONIC3_STANDALONE
    CHECK(!s3_knux_preview_needed(ram));
    lng(ram,0xCF82,0x694D4);CHECK(!s3_knux_preview_needed(ram));
#else
    CHECK(s3_knux_preview_needed(ram));
    /* Real capsule-wait controller and the native far-right camera cap:
     * the ledge enters the margin before automatic approach starts. */
    lng(ram,0xCF82,0x6923E);CHECK(s3_knux_preview_needed(ram));
    lng(ram,0xCF82,0x694D4);word(ram,0xEE80,0x4980);
    uint8_t before[65536];memcpy(before,ram,sizeof before);
    CHECK(s3_knux_preview_needed(ram));CHECK(!memcmp(before,ram,sizeof before));
    ram[0xB038]=2;CHECK(!s3_knux_preview_needed(ram));ram[0xB038]=0;
    word(ram,0xFE10,0);CHECK(!s3_knux_preview_needed(ram));
    word(ram,0xFE10,0x2001);CHECK(!s3_knux_preview_needed(ram));word(ram,0xFE10,1);
    ram[0xF600]=0x8C;CHECK(!s3_knux_preview_needed(ram));ram[0xF600]=0x0C;
    lng(ram,0xCF82,0x69588);CHECK(!s3_knux_preview_needed(ram));
    lng(ram,0xCF82,0);lng(ram,0xB206,0x69526);CHECK(s3_knux_preview_needed(ram));
#endif
}
static uint8_t *synthetic_rom(void)
{
    uint8_t *rom=(uint8_t *)calloc(1,0x400000);CHECK(rom);
    word(rom,S3_KNUX_PREVIEW_MAP+22*2,0x80);
    unsigned m=S3_KNUX_PREVIEW_MAP+0x80;
    word(rom,m,1);rom[m+2]=(uint8_t)-8;rom[m+3]=4;
    word(rom,m+4,0);word(rom,m+6,(unsigned)-4);
    word(rom,S3_KNUX_PREVIEW_DPLC+22*2,0x80);
    unsigned d=S3_KNUX_PREVIEW_DPLC+0x80;
    word(rom,d,0);word(rom,d+2,0x11);
    memset(rom+S3_KNUX_PREVIEW_ART+32,0x01,32);
    memset(rom+S3_KNUX_PREVIEW_ART+64,0x22,32);
    word(rom,S3_KNUX_PREVIEW_PALETTE+2,0xE);
    word(rom,S3_KNUX_PREVIEW_PALETTE+4,0xE00);
    return rom;
}
static void fill(uint32_t *out){for(int i=0;i<800;++i)out[i]=0x12345678;}
static void render_test(void)
{
    uint8_t *rom=synthetic_rom();uint32_t out[800];
    fill(out);s3_knux_preview_scanline(rom,92,out,800,240,0x4B8E - 370,0x17D-100);
#ifdef SONIC3_STANDALONE
    for(int x=0;x<800;++x)CHECK(out[x]==0x12345678);
#else
    for(int x=0;x<800;++x) {
        uint32_t wanted=0x12345678;
        if(x>=598 && x<606)wanted=0xFF0000FF;
        if(x>=606 && x<614 && !(x&1))wanted=0xFFFF0000;
        CHECK(out[x]==wanted);
    }
    CHECK(s3_knux_preview_cache.pattern_bytes==64);
    /* A repeated row consumes the decoded private cache, without rereading ROM. */
    memset(rom+S3_KNUX_PREVIEW_ART+32,0xFF,32);
    s3_knux_preview_scanline(rom,92,out,800,240,0x4B8E - 370,0x17D-100);
    CHECK(out[606]==0xFFFF0000 && out[607]==0x12345678);
    for(int line=91;line<=100;line+=9) {
        fill(out);s3_knux_preview_scanline(rom,line,out,800,240,0x4B8E - 370,0x17D-100);
        for(int x=0;x<800;++x)CHECK(out[x]==0x12345678);
    }
    /* The actor straddles the right native edge: only the margin is painted. */
    fill(out);s3_knux_preview_scanline(rom,92,out,800,240,0x4B8E - 325,0x17D-100);
    unsigned changed=0;
    for(int x=0;x<800;++x){if(x>=240 && x<560)CHECK(out[x]==0x12345678);changed+=out[x]!=0x12345678;}
    CHECK(changed>0);
    /* Left margin and opt-out retain the same clipping/opacity contract. */
    fill(out);s3_knux_preview_scanline(rom,92,out,800,240,0x4B8E + 10,0x17D-100);
    CHECK(out[218]==0xFF0000FF);for(int x=240;x<560;++x)CHECK(out[x]==0x12345678);
    fill(out);s3_knux_preview_scanline(rom,92,out,320,0,0x4B8E - 325,0x17D-100);
    for(int x=0;x<800;++x)CHECK(out[x]==0x12345678);
    /* Malformed cue counts and mappings fail closed inside the fixed cache. */
    memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
    word(rom,S3_KNUX_PREVIEW_DPLC+0x80,16);CHECK(!s3_knux_preview_load(rom));
    memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
    word(rom,S3_KNUX_PREVIEW_DPLC+0x80,0);
    word(rom,S3_KNUX_PREVIEW_MAP+0x80+4,2);CHECK(!s3_knux_preview_load(rom));
    memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
    word(rom,S3_KNUX_PREVIEW_MAP+0x80+4,0);
    word(rom,S3_KNUX_PREVIEW_MAP+0x80+6,60);CHECK(!s3_knux_preview_load(rom));
#endif
    free(rom);memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
}
static uint8_t *read_file(const char *name,size_t bytes)
{
    FILE *f=fopen(name,"rb");CHECK(f);uint8_t *p=(uint8_t *)malloc(bytes);CHECK(p);
    CHECK(fread(p,1,bytes,f)==bytes);CHECK(fgetc(f)==EOF);fclose(f);return p;
}
static void original_oracle(const char *rom_path,const char *ram_path,const char *vram_path)
{
    uint8_t *rom=read_file(rom_path,0x400000),*ram=read_file(ram_path,65536),*vram=read_file(vram_path,65536);
    CHECK(s3_knux_preview_load(rom));CHECK(s3_knux_preview_cache.pattern_bytes==576);
    CHECK(!memcmp(s3_knux_preview_cache.patterns,vram+0x4DA*32,576));
    for(unsigned n=0;n<16;++n)CHECK(s3_knux_preview_cache.colors[n]==
        genesis_dac_cram_to_argb((uint16_t)s3_knux_preview_word(ram,0xFC20+n*2),GENESIS_DAC_NORMAL));
    /* Independent native map/VRAM sampling verifies the entire mirrored pose. */
    uint8_t expected[4096]={0};unsigned o=0xB128;
    CHECK(s3_knux_preview_word(ram,o+0x10)==0x4B8E);
    CHECK(s3_knux_preview_word(ram,o+0x14)==0x17D);
    CHECK(ram[o+0x22]==22 && (ram[o+4]&1));
    unsigned map=(unsigned)s3_knux_preview_word(ram,o+0xC)<<16|s3_knux_preview_word(ram,o+0xE);
    CHECK(map==S3_KNUX_PREVIEW_MAP);
    unsigned p=map+(int16_t)s3_knux_preview_word(rom,map+ram[o+0x22]*2);
    unsigned count=s3_knux_preview_word(rom,p);p+=2;
    CHECK(count==2);
    for(unsigned n=0;n<count;++n,p+=6) {
        int h=(rom[p+1]&3)+1,w=((rom[p+1]>>2)&3)+1;
        int px=32-(int16_t)s3_knux_preview_word(rom,p+4)-w*8,py=32+(int8_t)rom[p];
        unsigned attr=(s3_knux_preview_word(rom,p+2)+s3_knux_preview_word(ram,o+10))^0x800u;
        for(int y=0;y<h*8;++y)for(int x=0;x<w*8;++x) {
            int sx=attr&0x800?w*8-1-x:x,sy=attr&0x1000?h*8-1-y:y;
            unsigned tile=(attr&2047u)+(unsigned)(sx/8*h+sy/8);
            unsigned packed=vram[tile*32+(sy&7)*4+(sx&7)/2];
            unsigned nib=(packed>>((sx&1)?0:4))&15;
            if(nib && !expected[(py+y)*64+px+x])expected[(py+y)*64+px+x]=(uint8_t)nib;
        }
    }
    CHECK(!memcmp(expected,s3_knux_preview_cache.pixels,sizeof expected));
    unsigned opaque=0;for(unsigned i=0;i<4096;++i)opaque+=expected[i]!=0;
    CHECK(opaque>400);printf("original oracle PASS: 576 DPLC bytes, 16 colors, %u opaque pixels\n",opaque);
    free(rom);free(ram);free(vram);memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
}
int main(int argc,char **argv)
{
    predicate_test();render_test();
    if(argc==4)original_oracle(argv[1],argv[2],argv[3]);else CHECK(argc==1);
    puts("Knuckles margin preview PASS");return 0;
}
