#include "trilogy_music_hle.h"
#include "scd_pack.h"
#include "sonic3_sound_hle_assets.h"
#include "sonic3_sound_assets.h"
#include "smps_hle.h"
#include "smps_hle_synth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t *banks[3];static size_t sizes[3];static ScdPack cd;
const uint8_t *tri_bank_resource(int content,size_t *bytes)
{if(bytes)*bytes=content>=1&&content<=3?sizes[content-1]:0;return content>=1&&content<=3?banks[content-1]:NULL;}
const ScdPack *tri_scd_pack(void){return &cd;}
static uint8_t *load(const char *path,size_t *bytes)
{
    FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    uint8_t *p=n>0?malloc((size_t)n):NULL;
    if(!p || fread(p,1,(size_t)n,f)!=(size_t)n){free(p);p=NULL;}fclose(f);*bytes=(size_t)n;return p;
}
static int fm(void *user,unsigned port,unsigned reg,unsigned value){(void)user;return port<=1&&reg<=255&&value<=255;}
static int psg(void *user,unsigned value){(void)user;return value<=255;}
static int pcm(void *user,unsigned note,unsigned pan)
{
    S3HleSoundAssets *a=user;
    return note>=0x81&&note<=0xFD&&a->samples[note-0x81].bytes&&a->samples[note-0x81].rate_numerator&&(pan&~0xC0)==0;
}
static int request(void *user,unsigned id){(void)user;return id<=255;}
int main(int argc,char **argv)
{
    CHECK(argc==6);size_t image_bytes=0;uint8_t *image=load(argv[1],&image_bytes);CHECK(image);
    for(unsigned i=0;i<3;i++){banks[i]=load(argv[2+i],&sizes[i]);CHECK(banks[i]);}
    char error[256]={0};CHECK(scd_pack_open(&cd,argv[5],error,sizeof error));
    S3HleSoundAssets a={0};S3SoundSource source;
    CHECK(s3_hle_sound_assets(image,image_bytes,&a,error,sizeof error));
    CHECK(s3_sound_read_source(image,image_bytes,&source,error,sizeof error));
    if(!tri_hle_sound_prepare(&a,&source,error,sizeof error)){fprintf(stderr,"prepare: %s\n",error);return 1;}
    s3_hle_sound_assets_index(&a);unsigned songs=0,effects=0;
    SmpsHleSynth synth;SmpsHleChipOutput output={fm,psg,pcm,request,&a};
    for(unsigned content=1;content<=4;content++){
        for(unsigned game=1;game<=3;game++)for(unsigned id=0x81;id<=0x9F;id++){
            unsigned flags=0;uint32_t root=tri_hle_music_root(content,game,id,&flags);if(!root)continue;
            smps_hle_synth_init(&synth,a.data,a.bytes,output);synth.psg_envelopes=a.psg_envelopes;synth.psg_count=256;synth.modulation_envelopes=a.modulation;synth.modulation_count=32;
            SmpsHle sequence;smps_hle_init(&sequence,a.data,a.bytes,smps_hle_synth_event,&synth);
            CHECK(smps_hle_music(&sequence,a.music[0x25-1],0));CHECK(smps_hle_music(&sequence,root,(int)flags));
            for(unsigned n=0;n<1200;n++)if(!smps_hle_frame(&sequence)){fprintf(stderr,"music content%u game%u id%02X tick%u: %s\n",content,game,id,n,sequence.error);return 1;}
            ++songs;
        }
        for(unsigned set=1;set<=2;set++)for(unsigned id=0x33;id<=0xDF;id++){
            uint32_t root=tri_hle_effect_root(content,set,id);if(!root)continue;
            smps_hle_synth_init(&synth,a.data,a.bytes,output);synth.psg_envelopes=a.psg_envelopes;synth.psg_count=256;synth.modulation_envelopes=a.modulation;synth.modulation_count=32;
            SmpsHle sequence;smps_hle_init(&sequence,a.data,a.bytes,smps_hle_synth_event,&synth);CHECK(smps_hle_effect(&sequence,root,id>=0xBC));
            for(unsigned n=0;n<300;n++)if(!smps_hle_frame(&sequence)){fprintf(stderr,"effect content%u set%u id%02X tick%u: %s\n",content,set,id,n,sequence.error);return 1;}
            ++effects;
        }
    }
    CHECK(songs>50&&effects>=346);printf("Host campaign resources: %u songs, %u effect entries, %u bytes; all output bindings pass\n",songs,effects,a.bytes);
    scd_resource_close(&cd.resource);for(unsigned i=0;i<3;i++)free(banks[i]);s3_hle_sound_assets_free(&a);free(image);return 0;
}
