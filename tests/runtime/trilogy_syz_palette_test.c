/* SYZ scenery reuses the original, faded/wet object palette without tagging
 * player patterns or reading a bitmap from legacy S1 info blocks. */
#define TRILOGY_INTERNAL 1
#include "trilogy_obj.h"
#include "stage_pack.h"
#include "video/genesis_vdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} }while(0)
uint8_t g_ram[65536];M68KState g_cpu;
static uint8_t bank[0x4000];
static uint32_t dir=0x400440;
static unsigned mask_reads;
static const uint8_t *published;
static uint16_t top[48],bottom[48];
static int split;
static void word(uint8_t *p,unsigned a,unsigned n){p[a]=(uint8_t)(n>>8);p[a+1]=(uint8_t)n;}
static void lng(uint8_t *p,unsigned a,uint32_t n){word(p,a,n>>16);word(p,a+2,n);}
uint32_t tri_dir(void){return dir;}
uint8_t tri_rd8(uint32_t a)
{
    CHECK(a>=0x400000 && a<0x404000);
    if(a>=0x402210 && a<0x402310)++mask_reads;
    return bank[a-0x400000];
}
uint16_t tri_rd16(uint32_t a){return (uint16_t)(tri_rd8(a)<<8|tri_rd8(a+1));}
uint32_t tri_rd32(uint32_t a){return (uint32_t)tri_rd16(a)<<16|tri_rd16(a+2);}
uint32_t tri_scd_extpal(void){return 0;}
int tri_s2end_active(void){return 0;}
int tri_mighty_presentation_frame(uint8_t *t,uint16_t *a,uint16_t *b)
{(void)a;(void)b;t[0x680]=3;return 1;}
int tri_signpost_frame(uint16_t *a,uint16_t *b){(void)a;(void)b;return 0;}
void tri_shadow_menu_colors(uint16_t *a,uint16_t *b){(void)a;(void)b;}
int tri_scd_ui_frame(uint8_t *t,uint16_t *a,uint16_t *b){(void)t;(void)a;(void)b;return 0;}
void gvdp_set_ext_tiles(const uint8_t *p){published=p;}
void gvdp_set_ext_colors(const uint16_t *a,const uint16_t *b,int row)
{memcpy(top,a,sizeof top);memcpy(bottom,b,sizeof bottom);split=row;}
int scd_pack_validate(const char *p,char *e,unsigned n){(void)p;(void)e;(void)n;return 0;}
#include "../../game/trilogy/trilogy_extpal.c"
#include "../../game/trilogy/stage_pack_bank.c"
static void runtime_test(void)
{
    lng(bank,TB_BLOBS,0x400100);lng(bank,0x100+TRI_BLOB_EXTPAL*4,0x401000);
    bank[0x440+TD_GAME]=TRI_GAME_S1;bank[0x440+TD_FLAGS]=S3_PACK_S1_PLANE_EXTPAL;
    lng(bank,0x440+TD_EXTRA,0x402000);
    bank[0x2210+(0x123>>3)]|=1u<<(0x123&7);
    /* Defense in depth: even a mask bypassing validation cannot mark a
     * character pattern. The real loader rejects this out-of-range bit. */
    bank[0x2210+(0x680>>3)]|=1u<<(0x680&7);
    uint16_t normal[16],wet[16];
    for(unsigned i=0;i<16;i++) {
        normal[i]=(uint16_t)((i&7)*0x222);wet[i]=(uint16_t)((15-i)&7)*0x202;
        word(bank,0x1400+i*2,normal[i]);word(bank,0x1420+i*2,wet[i]);
    }
    w16(RAM_Current_zone_and_act,0x2400);w8(RAM_Game_mode,12);
    w8(RAM_Water_flag,1);w16(RAM_Water_level,200);w16(RAM_Camera_Y_pos_copy,100);
    tri_ext_level();CHECK(mask_reads==256);
    tri_ext_tiles(0x500,1,1);tri_ext_frame();
    CHECK(published && published[0x123]==1 && published[0x500]==1 && published[0x680]==3);
    CHECK(!published[0] && !published[S3_PACK_LEVEL_TILE_LIMIT]);
    CHECK(!memcmp(top,normal,sizeof normal) && !memcmp(bottom,wet,sizeof wet) && split==100);
    unsigned reads=mask_reads;tri_ext_frame();CHECK(mask_reads==reads); /* cached */
    uint8_t before[65536];memcpy(before,g_ram,sizeof before);
    w8(RAM_Palette_fade_info,0);
    for(unsigned n=0;n<21;n++)tri_ext_hook(TH_PAL_TOBLACK);
    tri_ext_frame();for(unsigned i=0;i<16;i++)CHECK(!top[i] && !bottom[i]);
    for(unsigned n=0;n<21;n++)tri_ext_hook(TH_PAL_FROMBLACK);
    tri_ext_frame();CHECK(!memcmp(top,normal,sizeof normal) && !memcmp(bottom,wet,sizeof wet));
    before[RAM_Palette_fade_info&65535]=0;CHECK(!memcmp(before,g_ram,sizeof before));
    tri_ext_clear_tiles();tri_ext_frame();CHECK(!published[0x123]);
    bank[0x440+TD_FLAGS]=0;mask_reads=0;tri_ext_level();tri_ext_frame();
    CHECK(!mask_reads && !published[0x123] && published[0x680]==3);
    bank[0x440+TD_FLAGS]=S3_PACK_S1_PLANE_EXTPAL;w16(RAM_Current_zone_and_act,0x2200);
    mask_reads=0;tri_ext_level();tri_ext_frame();CHECK(!mask_reads && !published[0x123]);
}
static void validate(uint8_t *p,unsigned bytes,int expected)
{
    memcpy(p+bytes,"SPK1",4);lng(p,bytes+4,bytes);lng(p,bytes+8,crc32(p,bytes));
    FILE *f=fopen("syz-mask-test.pak","wb");CHECK(f);
    CHECK(fwrite(p,1,bytes+12,f)==bytes+12 && !fclose(f));
    CHECK(s3_pack_validate("syz-mask-test.pak",1,NULL,0)==expected);
    CHECK(!remove("syz-mask-test.pak"));
}
static void validation_test(void)
{
    uint8_t p[0x2800+12]={0};memcpy(p,"TRLG\0\1",6);memcpy(p+0x28,"CPK1\1\1",6);
    static const unsigned counts[]={3,4,3,3,3,3},ptrs[]={0,4,8,0x1c,0x20,0x28,0x2c,0x30,0x34};
    for(unsigned z=0;z<6;z++)for(unsigned a=0;a<counts[z];a++) {
        unsigned e=0x40+(z*4+a)*0x40;p[e+0x25]=1;
        for(unsigned k=0;k<sizeof ptrs/sizeof ptrs[0];k++)lng(p,e+ptrs[k],0x402040);
    }
    validate(p,0x2800,1); /* legacy flag zero: no extra bitmap required */
    unsigned e=0x440;lng(p,e+0x3c,0x4024F0);p[e+0x26]=S3_PACK_S1_PLANE_EXTPAL;
    p[0x2700]=2;validate(p,0x2800,1); /* full $310 bytes, exact payload end */
    lng(p,e+0x3c,0x4024F1);validate(p,0x2800,0); /* one byte short */
    lng(p,e+0x3c,0x4025F0);validate(p,0x2800,0); /* only old $210-byte base remains */
    lng(p,e+0x3c,0xFFFFFFFF);validate(p,0x2800,0); /* wrapped/missing pointer */
    lng(p,e+0x3c,0x4024F0);p[e+0x25]=2;validate(p,0x2800,0);p[e+0x25]=1;
    p[0x40+0x26]=1;validate(p,0x2800,0);p[0x40+0x26]=0; /* wrong zone */
    p[e+3*0x40+0x26]=1;validate(p,0x2800,0);p[e+3*0x40+0x26]=0; /* unused SYZ act */
    p[0x2700]|=1;validate(p,0x2800,0);p[0x2700]&=~1u;
    p[0x2700+(S3_PACK_LEVEL_TILE_LIMIT>>3)]|=1u<<(S3_PACK_LEVEL_TILE_LIMIT&7);
    validate(p,0x2800,0);
}
int main(void)
{
    runtime_test();validation_test();
    puts("SYZ lossless palette: legacy guards, cached source mask, fades/water and malformed pack bounds passed");
    return 0;
}
