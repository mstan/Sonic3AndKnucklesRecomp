#include "sonic3_restoration.h"
#include "sonic3_gameplay.h"
#include "sonic3_state_io.h"
#include "sonic3_art.h"
#include "sonic3_video.h"
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
static S3VideoBackground background;
void s3_video_set_background_override(S3VideoBackground fn){background=fn;}
static S3VideoPaletteSource palette;
void s3_video_set_palette_source(S3VideoPaletteSource fn){palette=fn;}
static const char *donor_path;
uint8_t *s3_sound_prototype_read(void)
{
    if(!donor_path)return NULL;
    FILE *f=fopen(donor_path,"rb");CHECK(f);uint8_t *p=malloc(0x200000);CHECK(p);
    CHECK(fread(p,1,0x200000,f)==0x200000 && fgetc(f)==EOF);CHECK(!fclose(f));return p;
}
const char *s3_sound_error(void){return "test donor not selected";}
void s3_video_set_title_card_name_map(unsigned map){title_map=map;}
static unsigned word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
int main(int argc,char **argv)
{
    CHECK(argc==2 || argc==4);FILE *f=fopen(argv[1],"rb");CHECK(f);
    size_t bytes=fread(g_rom,1,sizeof g_rom,f);CHECK(bytes==0x200000 || bytes==0x400000);CHECK(!fclose(f));
    uint8_t *original=malloc(sizeof g_rom);CHECK(original);memcpy(original,g_rom,sizeof g_rom);
    if(argc==4){
        int s3=!strcmp(argv[3],"sonic3"),sk=!strcmp(argv[3],"sandk");
        s3_restoration_settings("unused-restoration-test/settings.ini",argv[3]);
        CHECK(s3_restoration_netplay_allowed() && !s3_restoration_select(1,1));
        donor_path=argv[2];CHECK(s3_restoration_select(1,1));
        CHECK(s3_restoration_select(2,1)==!sk);CHECK(!s3_restoration_select(3,1));CHECK(s3_restoration_select(4,1));
        s3_restoration_post_reset();CHECK(g_rom_ext_size==0x200000 && !s3_restoration_netplay_allowed());
        CHECK(palette);
        CHECK(palette(s3?0x8C594u:0x28C594u)==0x5E9B54u);
        CHECK(palette(s3?0x8C654u:0x28C654u)==0x5E9C14u);
        CHECK(background);g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xFE10]=(uint8_t)(sk?4:1);
        CHECK(background(g_ram,0,0,0,0,NULL,NULL,NULL));uint16_t attr=0,color=0;uint8_t pixel=0;
        CHECK(background(g_ram,-1,0,-1,0,&attr,&pixel,&color));CHECK(pixel<64);
        g_ram[0xF664]=1;CHECK(!background(g_ram,0,0,0,0,&attr,&pixel,&color));g_ram[0xF664]=0;
        const unsigned hooks[]={s3?0x3500u:0x3DCAu,s3?0x351Cu:0x3DE6u,s3?0x3534u:0x3DFEu,s3?0x3550u:0x3E1Au};
        for(unsigned i=0;i<4;i++){
            CHECK(word(g_rom+hooks[i])==0x3659); /* MOVEA.W (A1)+,A3 */
            g_cpu.A[2]=s3?0x8C654:0xA8E5C;g_cpu.SR=0x2715;
            CHECK(s3_restoration_instruction(hooks[i]));
            CHECK(g_cpu.A[2]==0x5E9C14 && g_cpu.SR==0x2715);
        }
        g_cpu.A[0]=s3?0x16A3E2:0x36A3E2;
        CHECK(word(g_rom+g_cpu.A[0]-2)==16 || sk);CHECK(word(g_rom_ext+0x11667C)==9);
        g_cpu.D[2]=0x12340010;g_cpu.A[5]=0xABCDEF;
        CHECK(s3_restoration_instruction(s3?0x1932:0x16A8));
        CHECK(g_cpu.A[0]==(sk?0x36A3E2u:0x51667Eu));
        CHECK(g_cpu.D[2]==(sk?0x12340010u:0x12340009u) && g_cpu.A[5]==0xABCDEF);
        g_cpu.A[0]=s3?0x16A3E2:0x36A3E2;g_cpu.D[2]=0;g_cpu.A[5]=128;
        CHECK(s3_restoration_instruction(s3?0x1932:0x16A8));CHECK(g_cpu.A[5]==(sk?128u:72u));
        g_cpu.A[0]=0xFFB0DE;unsigned map=s3?0x160EB4u:0x360EB4u;
        for(unsigned i=0;i<4;i++)g_ram[0xB0EA+i]=(uint8_t)(map>>(24-8*i));
        CHECK(s3_restoration_instruction(s3?0x529AC:0x8403C));
        CHECK(((unsigned)word(g_ram+0xB0EA)<<16|word(g_ram+0xB0EC))==(sk?map:0x50DE4Eu));
        uint8_t state[8];S3StateIO io={state,sizeof state,0,0,1};s3_restoration_state(&io);CHECK(io.ok && io.pos==8);
        state[7]^=1;io=(S3StateIO){state,sizeof state,0,1,1};s3_restoration_state(&io);CHECK(!io.ok);
        CHECK(!memcmp(original,g_rom,sizeof g_rom));
        puts("Prototype palette sources, Nemesis header and mappings preserve the retail cartridge PASS");
        free(original);free(g_rom_ext);return 0;
    }
    s3_restoration_load();s3_restoration_post_reset();CHECK(!g_rom_ext_size && !title_map && !palette && s3_restoration_netplay_allowed());
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
    uint8_t state[8];S3StateIO io={state,sizeof state,0,0,1};s3_restoration_state(&io);CHECK(io.ok && io.pos==8);
    memset(state,0,sizeof state);io=(S3StateIO){state,sizeof state,0,1,1};s3_restoration_state(&io);CHECK(!io.ok);
    free(original);free(g_rom_ext);puts("Retail-derived font, native mappings/menu text and state identity PASS");return 0;
}
