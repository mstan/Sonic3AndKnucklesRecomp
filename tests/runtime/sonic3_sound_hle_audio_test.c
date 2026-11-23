#include "smps_hle.h"
#include "smps_hle_synth.h"
#include "pcm_hle.h"
#include "sonic3_sound_hle_assets.h"
#include "audio/ym2612.h"
#include "audio/sn76489.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct Audio {
    SmpsHle sequence;SmpsHleSynth synth;PcmHle pcm;PcmHleClip clips[126];
    FILE *wave;uint32_t written;uint64_t hash,sample_carry,output_carry,energy;
    int16_t fm[2];unsigned fm_writes,psg_writes,pcm_starts;
} Audio;
static int fm(void *user,unsigned port,unsigned reg,unsigned value)
{
    Audio *a=user;if(port>1||reg>255||value>255)return 0;
    ym2612_write((uint8_t)(port*2),(uint8_t)reg);ym2612_write((uint8_t)(port*2+1),(uint8_t)value);
    ++a->fm_writes;return 1;
}
static int psg(void *user,unsigned value){Audio *a=user;psg_write((uint8_t)value);++a->psg_writes;return value<=255;}
static int pcm(void *user,unsigned note,unsigned pan)
{
    Audio *a=user;if(note<0x81||note>0xFE)return 0;
    ++a->pcm_starts;return pcm_hle_play(&a->pcm,note-0x81,pan);
}
static int request(void *user,unsigned id){(void)user;fprintf(stderr,"Unimplemented HLE request %02X\n",id);return 0;}
static int16_t saturate(int n){return (int16_t)(n<-32768?-32768:n>32767?32767:n);}
static void frame(Audio *a)
{
    CHECK(smps_hle_frame(&a->sequence));
    a->sample_carry+=896040;unsigned count=(unsigned)(a->sample_carry/240);a->sample_carry%=240;
    for(unsigned i=0;i<count;i++){
        ym2612_advance(240);psg_advance(240);
        if(ym2612_samples_available()){
            int16_t pairs[8];size_t n=ym2612_render(pairs,4);CHECK(n);
            a->fm[0]=pairs[(n-1)*2];a->fm[1]=pairs[(n-1)*2+1];
        }
        int16_t mono=0;CHECK(psg_render(&mono,1)<=1);
        int16_t pair[2]={saturate(a->fm[0]+mono/8),saturate(a->fm[1]+mono/8)};
        CHECK(pcm_hle_mix(&a->pcm,pair,1,223721));
        a->output_carry+=44100u*240u;
        if(a->output_carry>=53693175u){
            a->output_carry-=53693175u;
            for(unsigned c=0;c<2;c++){
                unsigned value=(uint16_t)pair[c];a->hash=(a->hash^value)*1099511628211ull;
                a->energy+=(uint64_t)(pair[c]<0?-pair[c]:pair[c]);
                if(a->wave){fputc(value&255,a->wave);fputc(value>>8,a->wave);}
            }
            ++a->written;
        }
    }
}
static void put16(FILE *f,unsigned value){fputc(value&255,f);fputc(value>>8,f);}
static void put32(FILE *f,unsigned value){put16(f,value);put16(f,value>>16);}
static void header(FILE *f,unsigned frames)
{
    rewind(f);fwrite("RIFF",1,4,f);put32(f,frames*4+36);fwrite("WAVEfmt ",1,8,f);
    put32(f,16);put16(f,1);put16(f,2);put32(f,44100);put32(f,44100*4);
    put16(f,4);put16(f,16);fwrite("data",1,4,f);put32(f,frames*4);
}
int main(int argc,char **argv)
{
    CHECK(argc==3);FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));
    long bytes=ftell(f);rewind(f);CHECK(bytes>0);uint8_t *image=malloc((size_t)bytes);CHECK(image);
    CHECK(fread(image,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
    S3HleSoundAssets assets={0};char error[256]={0};
    if(!s3_hle_sound_assets(image,(size_t)bytes,&assets,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    Audio a={0};
    for(unsigned i=0;i<126;i++)a.clips[i]=(PcmHleClip){assets.data+assets.samples[i].start,assets.samples[i].bytes,
        assets.samples[i].rate_numerator,assets.samples[i].rate_denominator,UINT32_MAX};
    pcm_hle_init(&a.pcm,a.clips,126);ym2612_init();psg_init();
    SmpsHleChipOutput output={fm,psg,pcm,request,&a};
    smps_hle_synth_init(&a.synth,assets.data,assets.bytes,output);
    a.synth.psg_envelopes=assets.psg_envelopes;a.synth.psg_count=256;
    a.synth.modulation_envelopes=assets.modulation;a.synth.modulation_count=32;
    smps_hle_init(&a.sequence,assets.data,assets.bytes,smps_hle_synth_event,&a.synth);
    a.wave=fopen(argv[2],"wb");CHECK(a.wave);header(a.wave,0);
    CHECK(pcm(&a,0xC6,0xC0));
    for(unsigned i=0;i<120;i++)frame(&a);
    CHECK(!a.pcm.state.active&&a.energy>10000000);
    CHECK(smps_hle_music(&a.sequence,assets.music[0x25-1],0));
    for(unsigned i=0;i<180;i++)frame(&a);
    CHECK(a.fm_writes>100 && a.psg_writes>0 && a.pcm_starts>1);
    unsigned written=a.written;header(a.wave,written);CHECK(!fclose(a.wave));a.wave=NULL;

    /* A full current-build sound checkpoint includes native sequence,
     * articulation, PCM clock, synth chips and their pending sample queues. */
    SmpsHleState seq=a.sequence.state;SmpsHleSynthState synth=a.synth.state;PcmHleState clip=a.pcm.state;
    uint64_t carry=a.sample_carry,output_carry=a.output_carry;int16_t held[2]={a.fm[0],a.fm[1]};
    size_t fm_bytes=ym2612_rb_save(NULL,0),psg_bytes=psg_rb_save(NULL,0);
    void *fm_state=malloc(fm_bytes),*psg_state=malloc(psg_bytes);CHECK(fm_state&&psg_state);
    CHECK(ym2612_rb_save(fm_state,fm_bytes)==fm_bytes&&psg_rb_save(psg_state,psg_bytes)==psg_bytes);
    a.hash=0;for(unsigned i=0;i<30;i++)frame(&a);uint64_t expected=a.hash;
    CHECK(ym2612_rb_load(fm_state,fm_bytes)&&psg_rb_load(psg_state,psg_bytes));
    a.sequence.state=seq;a.synth.state=synth;a.pcm.state=clip;a.sample_carry=carry;a.output_carry=output_carry;
    a.fm[0]=held[0];a.fm[1]=held[1];a.hash=0;
    for(unsigned i=0;i<30;i++)frame(&a);CHECK(a.hash==expected);
    /* Exercise actual instrument/envelope/sample consumers for the remaining
     * streams. Scheduling-only tests cannot expose a missing output binding.
     * Full chip rendering above is the representative audio/checkpoint case. */
    for(unsigned id=1;id<=51;id++)if(assets.music[id-1]){
        CHECK(smps_hle_music(&a.sequence,assets.music[0x25-1],0));
        CHECK(smps_hle_music(&a.sequence,assets.music[id-1],id==0x2A?SMPS_HLE_INTERRUPT:0));
        for(unsigned tick=0;tick<1200;tick++)if(!smps_hle_frame(&a.sequence)){
            fprintf(stderr,"Output binding music %02X tick %u: %s\n",id,tick,a.sequence.error);return 1;
        }
    }
    SmpsHle effects;smps_hle_init(&effects,assets.data,assets.bytes,smps_hle_synth_event,&a.synth);
    for(unsigned id=0;id<169;id++)if(assets.effects[id]){
        CHECK(smps_hle_stop(&effects));CHECK(smps_hle_effect(&effects,assets.effects[id],id+0x33>=0xBC));
        for(unsigned tick=0;tick<300;tick++)if(!smps_hle_frame(&effects)){
            fprintf(stderr,"Output binding effect %02X tick %u: %s\n",id+0x33,tick,effects.error);return 1;
        }
    }
    printf("HLE SEGA/title rendered %u stereo samples and checkpoints repeat exactly; all available music/effect output bindings pass. Runtime and acoustic qualification are separate.\n",written);
    free(fm_state);free(psg_state);s3_hle_sound_assets_free(&assets);free(image);return 0;
}
