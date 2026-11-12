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
        CHECK(bank[5]==0); /* Imported S1/S2 tracks retain their own PSG rules. */
        unsigned voices=(bank[0]<<8)|bank[1];CHECK(voices>8 && voices<(unsigned)n);
        printf("PASS donor %u theme %u source %06X bytes %d drums",donor,t,source.source,n);
        for(unsigned i=0;i<128;i++)if(drums[i])printf(" %02X",i+128);puts("");
    }
    if(donor==S3_DONOR_WAI){
        /* Audited prototype DAC playlist pitches; the three upper toms
         * previously inherited unrelated, much higher retail pitches. */
        static const unsigned expected[]={83,241,83,192,75,136,0,98,89,73,71,136,114,95};
        S3LegacySource source;CHECK(s3_legacy_music_source(rom,(size_t)size,donor,18,&source));
        uint8_t *sample=malloc(0x40000),*tom=malloc(0x40000);CHECK(sample&&tom);unsigned rate=0,tom_rate=0;
        int tom_size=s3_legacy_sample(&source,0x86,tom,0x40000,&tom_rate);CHECK(tom_size>0&&tom_rate==136);
        for(unsigned i=0;i<sizeof expected/sizeof *expected;i++){
            int n=s3_legacy_sample(&source,0x81+i,sample,0x40000,&rate);
            if(!expected[i]){CHECK(!n);continue;}
            CHECK(n>0&&rate==expected[i]);
            if(i>=11){CHECK(n==tom_size&&!memcmp(sample,tom,(size_t)n));}
        }
        CHECK(!s3_legacy_sample(&source,0x8F,sample,0x40000,&rate));free(sample);free(tom);
        puts("PASS prototype percussion identity, native pitch variants and playlist bounds");
    }
    free(rom);free(bank);return 0;
}
