#include "sonic3_sound_assets.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sonic3_sound_sequences.c"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static uint8_t *read_file(const char *name,size_t *size)
{
    FILE *f=fopen(name,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    uint8_t *p=(uint8_t*)malloc((size_t)n);CHECK(p);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;
}
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int inside(size_t p,uint32_t address,size_t size)
{return p>=address-0x400000 && p-(address-0x400000)<size;}
static void volume_command_test(void)
{
    uint8_t rom[0x8100]={0},bank[0x2000]={0};
    Convert *c=calloc(1,sizeof *c);CHECK(c);
    c->rom=rom;c->bank=bank;c->capacity=sizeof bank;c->source.rom_size=sizeof rom;
    rom[0x8000]=0x20; /* one silent software instrument in the Z80 buffer */
    rom[0x8002]=2;rom[0x8003]=1;rom[0x8004]=1;
    rom[0x8006]=0x50;rom[0x8007]=0x80;
    rom[0x800A]=0x60;rom[0x800B]=0x80;
    rom[0x800E]=0x70;rom[0x800F]=0x80;
    rom[0x8050]=0xF2;
    const uint8_t notes[]={0xE5,3,0xFE,0x81,1,0xF2};
    memcpy(rom+0x8060,notes,sizeof notes);memcpy(rom+0x8070,notes,sizeof notes);
    CHECK(convert_song(c,0x8000,0x1000,1)>0);
    CHECK(bank[0x1005]==1); /* S3 PSG compatibility, distinct from legacy donors. */
    unsigned fm=0x1000+((bank[0x100C]<<8)|bank[0x100D]);
    unsigned psg=0x1000+((bank[0x1010]<<8)|bank[0x1011]);
    CHECK(bank[fm]==0xFF && bank[fm+1]==6 && bank[fm+2]==0xFE);
    CHECK(bank[fm+3]==0x81 && bank[fm+4]==1);
    CHECK(bank[psg]==0x81 && bank[psg+1]==1);
    free(c);
}
int main(int argc,char **argv)
{
    volume_command_test();
    CHECK(argc==4);size_t size,program_size;uint8_t *rom=read_file(argv[1],&size),*program=read_file(argv[2],&program_size);
    CHECK(program_size>=84 && !memcmp(program,"S3CR",4) && be32(program+4)==program_size);
    S3SoundLayout l={(uint32_t)program_size,be32(program+60),be32(program+64),be32(program+68),be32(program+72)};
    uint8_t *bank=(uint8_t*)calloc(1,0x400000);CHECK(bank);memcpy(bank,program,program_size);
    uint8_t before[32],after[32];recompui_sha256_compute(rom,size,before);S3SoundAssetStats stats;char error[256];
    int ok=s3_sound_build_sequences(rom,size,&l,bank,0x400000,&stats,error,sizeof error);if(!ok)fprintf(stderr,"%s\n",error);CHECK(ok);
    recompui_sha256_compute(rom,size,after);CHECK(!memcmp(before,after,sizeof before));
    for(size_t i=0;i<program_size;i++){
        if(inside(i,l.music_index,51*4)||inside(i,l.sound_index,169*4)||inside(i,l.psg_index,51*4)||inside(i,l.modulation_index,8*4))continue;
        CHECK(bank[i]==program[i]);
    }
    CHECK(stats.music>=30 && stats.music<=51 && stats.sfx==169 && stats.nodes>10000 && stats.bytes<0x400000);
    S3SoundAssetStats saved=stats;
    CHECK(!s3_sound_build_sequences(rom,size,&l,bank,84,&stats,error,sizeof error));CHECK(!memcmp(&stats,&saved,sizeof stats));
    CHECK(!s3_sound_build_sequences(rom,size,&l,rom,size,&stats,error,sizeof error));
    FILE *f=fopen(argv[3],"wb");CHECK(f);CHECK(fwrite(bank,1,saved.bytes,f)==saved.bytes);fclose(f);
    printf("PASS ROM-derived sequence graph, instruments and envelopes: %u music, %u SFX, %u nodes, %u bytes; original ROM and all software instructions unchanged\n",saved.music,saved.sfx,saved.nodes,saved.bytes);
    free(bank);free(program);free(rom);return 0;
}
