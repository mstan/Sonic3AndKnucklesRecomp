#include "sonic3_sound_legacy.h"
#include "sonic3_sound_themes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(int argc,char **argv)
{
    CHECK(argc==3);unsigned donor=(unsigned)atoi(argv[2]);
    FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>0);rewind(f);
    uint8_t *rom=malloc((size_t)size),*bank=malloc(0x8000);CHECK(rom&&bank);CHECK(fread(rom,1,(size_t)size,f)==(size_t)size);fclose(f);
    for(unsigned t=0;t<S3_THEME_COUNT;t++){
        S3LegacySource source;CHECK(s3_legacy_source(rom,(size_t)size,donor,t,&source));
        uint8_t drums[128]={0},map[128];char error[256]={0};for(unsigned i=0;i<128;i++)map[i]=(uint8_t)(i+128);
        int n=s3_legacy_song(&source,bank,0x8000,104,map,drums,error,sizeof error);
        if(!n)fprintf(stderr,"%s\n",error);CHECK(n>0 && n<0x8000);
        unsigned voices=(bank[0]<<8)|bank[1];CHECK(voices>8 && voices<(unsigned)n);
        printf("PASS donor %u theme %u source %06X bytes %d drums",donor,t,source.source,n);
        for(unsigned i=0;i<128;i++)if(drums[i])printf(" %02X",i+128);puts("");
    }
    free(rom);free(bank);return 0;
}
