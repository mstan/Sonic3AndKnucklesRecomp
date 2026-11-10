#include "sonic3_sound_assets.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>100);rewind(f);
    uint8_t *p=malloc((size_t)n);CHECK(p);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;
}
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
int main(int argc,char **argv)
{
    CHECK(argc==4);size_t size,input_size;uint8_t *rom=read_file(argv[1],&size),*input=read_file(argv[2],&input_size);
    S3SoundPCMLayout l={be32(input+56),be32(input+84),be32(input+88),be32(input+92),be32(input+96)};
    uint8_t *bank=calloc(1,0x400000);CHECK(bank);memcpy(bank,input,input_size);
    uint32_t used=(uint32_t)input_size;uint8_t before[32],after[32];char error[256];recompui_sha256_compute(rom,size,before);
    CHECK(!s3_sound_build_pcm(rom,size,&l,bank,used,&used,error,sizeof error));CHECK(used==input_size);CHECK(!memcmp(bank,input,input_size));
    CHECK(!s3_sound_build_pcm(rom,size,&l,rom,size,&used,error,sizeof error));CHECK(used==input_size);
    int ok=s3_sound_build_pcm(rom,size,&l,bank,0x400000,&used,error,sizeof error);if(!ok)fprintf(stderr,"%s\n",error);CHECK(ok);
    recompui_sha256_compute(rom,size,after);CHECK(!memcmp(before,after,32));
    for(size_t i=0;i<input_size;i++)if(i<l.sample_table-0x400000 || i>=l.sample_table-0x400000+l.sample_count*10)CHECK(bank[i]==input[i]);
    CHECK(used>input_size && used<=0x400000);
    FILE *f=fopen(argv[3],"wb");CHECK(f);CHECK(fwrite(bank,1,used,f)==used);fclose(f);
    printf("PASS ROM-derived available DAC samples + SEGA; immutable ROM/software, bounds/alias rejection; %u bank bytes\n",used);
    free(bank);free(input);free(rom);return 0;
}
