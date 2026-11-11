#include "sonic3_restoration.h"
#include "sonic3_gameplay.h"
#include "sonic3_state_io.h"
#include "sonic3_art.h"
#include "genesis_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
uint8_t g_rom[0x400000],g_ram[65536],*g_rom_ext;
uint32_t g_rom_ext_size;
M68KState g_cpu;
S3GameplayConfig s3_gameplay;
static unsigned title_map;
void s3_video_set_title_card_name_map(unsigned map){title_map=map;}
static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
int main(int argc,char **argv)
{
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);
    size_t bytes=fread(g_rom,1,sizeof g_rom,f);CHECK(bytes==0x200000 || bytes==0x400000);CHECK(!fclose(f));
    uint8_t *original=malloc(sizeof g_rom);CHECK(original);memcpy(original,g_rom,sizeof g_rom);
    s3_restoration_load();s3_restoration_post_reset();CHECK(!g_rom_ext_size && !title_map && s3_restoration_netplay_allowed());
    s3_restoration_load();s3_gameplay.early_zone_names=1;s3_restoration_post_reset();
    CHECK(title_map && g_rom_ext_size && !s3_restoration_netplay_allowed());
    g_cpu.A[1]=0x15C454;CHECK(s3_restoration_instruction(0x1AD2));
    const uint8_t *art=g_rom_ext+g_cpu.A[1]-0x400000;uint8_t unpacked[4096];size_t used=0;
    CHECK(word(art)==0x720 && art_kos(art+2,g_rom_ext_size-2,unpacked,sizeof unpacked,&used)==0x720);
    const uint8_t *map=g_rom_ext+title_map-0x400000;
    CHECK(word(map+22)==26 && word(map+26)==14);
    CHECK((int16_t)word(map+28+4)==-120 && word(map+28+13*6+2)==0x8580);
    CHECK(s3_restoration_instruction(0x7B40));
    const uint8_t *text=g_rom_ext+g_cpu.A[1]-0x400000;
    for(unsigned i=0;i<6;i++)text+=text[0]+2;
    static const char expected[]="MUSHROOM VALLEY";CHECK(text[0]==14);
    for(unsigned i=0;i<14+1;i++)CHECK(text[i+1]==(expected[i]==' '?0:expected[i]-'A'+30));
    g_cpu.A[0]=0xFFB0DE;g_ram[0xEE4E]=7;CHECK(s3_restoration_instruction(0x2D92C));
    CHECK(((uint32_t)word(g_ram+0xB0EA)<<16|word(g_ram+0xB0EC))==title_map);
    CHECK(!memcmp(original,g_rom,sizeof g_rom));
    uint8_t state[4];S3StateIO io={state,sizeof state,0,0,1};s3_restoration_state(&io);CHECK(io.ok && io.pos==4);
    memset(state,0,sizeof state);io=(S3StateIO){state,sizeof state,0,1,1};s3_restoration_state(&io);CHECK(!io.ok);
    free(original);free(g_rom_ext);puts("Retail-derived font, native mappings/menu text and state identity PASS");return 0;
}
