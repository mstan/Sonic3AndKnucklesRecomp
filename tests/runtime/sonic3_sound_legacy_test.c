#include "sonic3_sound_legacy.h"
#include "sonic3_sound_themes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void operator_registers(void)
{
    /* Four distinct operators make a middle-operator swap audible. These
     * are synthetic YM2612 register values, independent of cartridge assets. */
    static const uint8_t native_s1[]={0x3D,0x71,0x73,0x72,0x74,0x1F,0x1D,0x1E,0x1C,
        0x81,0x83,0x82,0x84,1,3,2,4,0x15,0x37,0x26,0x48,0x11,0x33,0x22,0x44};
    static const uint8_t native_s2[]={0x3D,0x71,0x72,0x73,0x74,0x1F,0x1E,0x1D,0x1C,
        0x81,0x82,0x83,0x84,1,2,3,4,0x15,0x26,0x37,0x48,0x11,0x22,0x33,0x44};
    static const uint8_t expected[]={0x3D,0x71,0x72,0x73,0x74,0x11,0xA2,0xB3,0xC4,
        0x1F,0x1E,0x1D,0x1C,0x81,0x82,0x83,0x84,1,2,3,4,0x15,0x26,0x37,0x48};
    for(unsigned donor=S3_DONOR_S1;donor<=S3_DONOR_WAI;donor++){
        uint8_t rom[0x300]={0},bank[0x8000],map[128]={0},drums[128]={0};char error[256];
        S3LegacySource source={0};source.rom=rom;source.size=sizeof rom;source.source=0x100;source.high=sizeof rom;
        source.donor=donor;source.little=donor!=S3_DONOR_S1;
        rom[0x102]=2;rom[0x104]=1;rom[0x105]=0x80;
        if(source.little){rom[0x101]=2;rom[0x106]=0x40;rom[0x107]=1;rom[0x10A]=0x50;rom[0x10B]=1;}
        else{rom[0x100]=1;rom[0x107]=0x40;rom[0x10B]=0x50;}
        rom[0x140]=0xF2;rom[0x150]=0xEF;rom[0x152]=0x81;rom[0x153]=0x12;rom[0x154]=0xF2;
        memcpy(rom+0x200,source.little?native_s2:native_s1,25);
        CHECK(s3_legacy_song(&source,bank,sizeof bank,0,map,drums,error,sizeof error)>0);
        unsigned voices=(bank[0]<<8)|bank[1];CHECK(!memcmp(bank+voices,expected,sizeof expected));
    }
}
int main(int argc,char **argv)
{
    operator_registers();
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
