/* Check the actual hook adapter's bank pointers, guest-register contract,
 * published renderer source, and opt-out behavior without a private ROM. */
#include "trilogy.h"
#include "megamix_geometry_sites.h"
#include "sonic3_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} }while(0)
uint8_t g_ram[65536],g_rom[0x400000];
static uint8_t bank[0x10000];
uint8_t *g_rom_ext=bank;
uint32_t g_rom_ext_size=sizeof bank;
M68KState g_cpu;
int g_split_sp_popped;
static int rte;
int *g_rte_pending_ptr=&rte;
static int active=1;
static S3VideoWorldCell video;
void s3_video_set_world_cell(S3VideoWorldCell fn){video=fn;}
uint32_t tri_dir(void){return active?0x400040:0;}
uint8_t tri_rd8(uint32_t a){CHECK(a>=0x400000 && a<0x410000);return bank[a-0x400000];}
uint16_t tri_rd16(uint32_t a){return (uint16_t)((tri_rd8(a)<<8)|tri_rd8(a+1));}
uint32_t tri_rd32(uint32_t a){return ((uint32_t)tri_rd16(a)<<16)|tri_rd16(a+2);}
static void word(uint8_t *p,unsigned a,unsigned v){p[a]=(uint8_t)(v>>8);p[a+1]=(uint8_t)v;}
static void lng(uint8_t *p,unsigned a,uint32_t v){word(p,a,v>>16);word(p,a+2,v);}
static void fixture(void)
{
    bank[0x40+TD_GAME]=TRI_GAME_MEGAMIX;lng(bank,0x40+TD_EXTRA,0x400100);
    memcpy(bank+0x100,"MGI1",4);lng(bank,0x104,0x400200);lng(bank,0x108,24);
    lng(bank,0x10C,0x401000);lng(bank,0x110,280*128);word(bank,0x114,520);
    memcpy(bank+0x200,"MGL1",4);word(bank,0x204,2);word(bank,0x206,1);
    word(bank,0x208,2);word(bank,0x20A,2);
    word(bank,0x20C,256);word(bank,0x20E,279);word(bank,0x210,258);
    word(bank,0x214,257);word(bank,0x216,259);
    word(bank,0x1000+256*128+126,0xF123);
    word(bank,0x1000+257*128+126,0x8121);
    word(g_ram,0xFE10,0x3400);word(g_ram,0xEEAE,0x7C);
    tri_megamix_geometry_begin();CHECK(video);
}
static void private_corpus(const char *layout,const char *chunks,unsigned blocks)
{
    const char *paths[]={layout,chunks};const unsigned offsets[]={0x200,0x2000};
    unsigned sizes[2];
    memset(bank,0,sizeof bank);
    for(unsigned k=0;k<2;k++) {
        FILE *f=fopen(paths[k],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);
        CHECK(size>0 && size<=(long)(sizeof bank-offsets[k]));rewind(f);
        sizes[k]=(unsigned)size;
        CHECK(fread(bank+offsets[k],1,sizes[k],f)==sizes[k]);CHECK(!fclose(f));
    }
    CHECK(sizes[0]<=0x1E00);
    bank[0x40+TD_GAME]=TRI_GAME_MEGAMIX;lng(bank,0x40+TD_EXTRA,0x400100);
    memcpy(bank+0x100,"MGI1",4);lng(bank,0x104,0x400200);lng(bank,0x108,sizes[0]);
    lng(bank,0x10C,0x402000);lng(bank,0x110,sizes[1]);word(bank,0x114,blocks);
    word(g_ram,0xFE10,0x3400);active=1;tri_megamix_geometry_begin();
    unsigned checked=0;
    for(unsigned p=0;p<2;p++) {
        unsigned width=tri_rd16(0x400204+p*2),height=tri_rd16(0x400208+p*2);
        for(unsigned y=0;y<height*128;y+=16)for(unsigned x=0;x<width*128;x+=16) {
            uint16_t cell,rendered;
            CHECK(tri_megamix_geometry_cell((int)x,(int)y,(int)p,0,&cell));
            CHECK(video(g_ram,(int)x,(int)y,(int)p,&rendered) && cell==rendered);
            g_cpu.D[2]=y;g_cpu.D[3]=x;
            CHECK(tri_megamix_geometry_hook(p?MG_PC_SK_BG:MG_PC_SK_FG));
            CHECK(tri_rd16(g_cpu.A[1])==cell);
            g_cpu.D[0]=y;g_cpu.D[1]=x/128;g_cpu.A[3]=p?0xFFFF800A:0xFFFF8008;
            CHECK(tri_megamix_geometry_hook(MG_PC_SK_ROW));
            CHECK(tri_rd16(g_cpu.A[5]+((x&112)>>3))==cell);
            checked++;
        }
    }
    printf("Collision, streamer and published renderer agree on %u private donor cells\n",checked);
}
int main(int argc,char **argv)
{
    fixture();uint16_t cell;
    CHECK(tri_megamix_geometry_cell(127,127,0,0,&cell) && cell==0xF123);
    CHECK(video(g_ram,127,127,1,&cell) && cell==0x8121);
    uint8_t snapshot[65536];memcpy(snapshot,g_ram,sizeof snapshot);snapshot[0xFE10]=0x20;
    CHECK(!video(snapshot,127,127,0,&cell));
    const unsigned fg[]={MG_PC_SK_FG,MG_PC_S3_FG},bg[]={MG_PC_SK_BG,MG_PC_S3_BG};
    for(unsigned i=0;i<2;i++) {
        for(unsigned p=0;p<2;p++) {
            for(unsigned k=0;k<8;k++){g_cpu.D[k]=0xABCD0123;g_cpu.A[k]=0xFFABCD;}
            g_cpu.D[2]=127;g_cpu.D[3]=127;g_cpu.SR=0xA71F;
            uint32_t before_d[8],before_a[8];memcpy(before_d,g_cpu.D,sizeof before_d);memcpy(before_a,g_cpu.A,sizeof before_a);
            CHECK(tri_megamix_geometry_hook(p?bg[i]:fg[i]));
            unsigned addr=0x401000+(p?257:256)*128+126;
            CHECK(g_cpu.A[1]==addr && g_cpu.D[1]==addr);
            CHECK(tri_rd16(addr)==(p?0x8121:0xF123));
            CHECK(g_cpu.D[0]==0xABCD0070 && g_cpu.D[4]==0xABCD000E);
            CHECK(g_cpu.SR==0xA708);
            for(unsigned k=0;k<8;k++) {
                if(k!=0 && k!=1 && k!=4)CHECK(g_cpu.D[k]==before_d[k]);
                if(k!=1)CHECK(g_cpu.A[k]==before_a[k]);
            }
        }
        /* The native streamer increments D1 across a 128px chunk seam. */
        g_cpu.D[0]=0xABCD007F;g_cpu.D[1]=0;g_cpu.D[4]=0x98760000;g_cpu.A[3]=0xFFFF8008;
        unsigned row=i?MG_PC_S3_ROW:MG_PC_SK_ROW;
        CHECK(tri_megamix_geometry_hook(row));CHECK(g_cpu.A[5]==0x401000+256*128+112);
        CHECK(g_cpu.D[4]==0x98760070 && g_cpu.D[0]==0xABCD007F);
        g_cpu.D[1]=1;CHECK(tri_megamix_geometry_hook(row));
        CHECK(g_cpu.A[5]==0x401000+279*128+112);
        g_cpu.D[1]=0xFFFF;CHECK(tri_megamix_geometry_hook(row));CHECK(g_cpu.A[5]==0x401000+112);
    }
    CHECK(tri_megamix_geometry_owns(MG_PC_SK_FG) && !tri_megamix_geometry_owns(42));
    g_split_sp_popped=2;g_cpu.D[2]=g_cpu.D[3]=0;
    CHECK(tri_megamix_geometry_hook(MG_PC_SK_FG));CHECK(rte==2 && !g_split_sp_popped);
    word(g_ram,0xFE10,0x2000);CHECK(!tri_megamix_geometry_hook(MG_PC_SK_FG));
    active=0;tri_megamix_geometry_begin();word(g_ram,0xFE10,0x3400);
    CHECK(!tri_megamix_geometry_cell(0,0,0,0,&cell) && !video(g_ram,0,0,0,&cell));
    puts("Megamix hooks: collision/streaming bank pointers, chunk seams, guest registers, frame isolation and opt-out passed");
    if(argc==4)private_corpus(argv[1],argv[2],(unsigned)atoi(argv[3]));else CHECK(argc==1);
    return 0;
}
