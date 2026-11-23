#include "smps_hle.h"
#include "sonic3_sound_hle_assets.h"
#include "sonic3_sound_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct Output {uint64_t hash;unsigned events,notes,pcm,voices;} Output;
static int emit(void *user,const SmpsHleEvent *e)
{
    Output *out=user;
    out->hash=(out->hash^(unsigned)e->kind)*1099511628211ull;
    out->hash=(out->hash^e->channel)*1099511628211ull;
    out->hash=(out->hash^e->value)*1099511628211ull;
    out->hash=(out->hash^(unsigned)e->argument)*1099511628211ull;
    ++out->events;out->notes+=e->kind==SMPS_HLE_NOTE;
    out->pcm+=e->kind==SMPS_HLE_PCM;out->voices+=e->kind==SMPS_HLE_VOICE;
    return e->channel<SMPS_HLE_TRACKS;
}
int main(int argc,char **argv)
{
    CHECK(argc==2 || argc==3);FILE *f=fopen(argv[1],"rb");CHECK(f);
    CHECK(!fseek(f,0,SEEK_END));long bytes=ftell(f);rewind(f);CHECK(bytes>0);
    uint8_t *image=malloc((size_t)bytes),*before=malloc((size_t)bytes);CHECK(image&&before);
    CHECK(fread(image,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);memcpy(before,image,(size_t)bytes);
    S3HleSoundAssets assets={0};char error[256]={0};
    if(!s3_hle_sound_assets(image,(size_t)bytes,&assets,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    CHECK(!memcmp(image,before,(size_t)bytes));
    static const unsigned ids[]={0x25,0x2D,0x2F};
    for(unsigned i=0;i<sizeof ids/sizeof *ids;i++){
        unsigned id=ids[i];if(!assets.music[id-1]){CHECK(id!=0x25);continue;}
        SmpsHle sound;Output out={0};
        smps_hle_init(&sound,assets.data,assets.bytes,emit,&out);
        CHECK(smps_hle_music(&sound,assets.music[id-1],0));
        for(unsigned n=0;n<1200;n++)if(!smps_hle_frame(&sound)){fprintf(stderr,"track %02X tick %u: %s\n",id,n,sound.error);return 1;}
        CHECK(out.notes&&out.voices&&smps_hle_state_valid(&sound,&sound.state));
        smps_hle_pause(&sound,1);SmpsHleState paused=sound.state;unsigned events=out.events;
        for(unsigned n=0;n<60;n++)CHECK(smps_hle_frame(&sound));
        CHECK(!memcmp(&paused,&sound.state,sizeof paused)&&events==out.events);
        smps_hle_pause(&sound,0);SmpsHleState checkpoint=sound.state;
        out=(Output){0};for(unsigned n=0;n<180;n++)CHECK(smps_hle_frame(&sound));
        SmpsHleState expected=sound.state;Output expected_output=out;
        sound.state=checkpoint;out=(Output){0};
        for(unsigned n=0;n<180;n++)CHECK(smps_hle_frame(&sound));
        CHECK(!memcmp(&expected,&sound.state,sizeof expected));
        CHECK(out.hash==expected_output.hash&&out.events==expected_output.events);
        if(id==0x25 && assets.music[0x2A-1]){
            SmpsHleClock preceding=sound.state.music;uint32_t song=sound.state.song;
            CHECK(smps_hle_music(&sound,assets.music[0x2A-1],SMPS_HLE_INTERRUPT));
            unsigned ticks=0;
            for(;ticks<600 && sound.state.interrupted;ticks++)CHECK(smps_hle_frame(&sound));
            CHECK(!sound.state.interrupted && ticks>0 && ticks<600);
            CHECK(sound.state.song==song&&!memcmp(&preceding,&sound.state.music,sizeof preceding));
            printf("HLE native 1UP restored the preceding song/tempo/channel clocks in %u ticks\n",ticks);
        }
        SmpsHleState invalid=sound.state;invalid.music.track[1].cursor=assets.bytes+1;
        invalid.music.track[1].active=1;CHECK(!smps_hle_state_valid(&sound,&invalid));
        printf("HLE track %02X: host events, pause and same-build timeline restore pass; PCM rendering/SFX integration remain separate\n",id);
    }
    for(unsigned id=1;id<=51;id++)if(assets.music[id-1]){
        SmpsHle sound;Output out={0};smps_hle_init(&sound,assets.data,assets.bytes,emit,&out);
        CHECK(smps_hle_music(&sound,assets.music[0x25-1],0));
        CHECK(smps_hle_music(&sound,assets.music[id-1],id==0x2A?SMPS_HLE_INTERRUPT:0));
        for(unsigned tick=0;tick<1200;tick++)if(!smps_hle_frame(&sound)){
            fprintf(stderr,"Music %02X tick %u: %s\n",id,tick,sound.error);return 1;
        }
    }
    for(unsigned id=0;id<169;id++)if(assets.effects[id]){
        SmpsHle sound;Output out={0};smps_hle_init(&sound,assets.data,assets.bytes,emit,&out);
        if(!smps_hle_effect(&sound,assets.effects[id],id+0x33>=0xBC)){fprintf(stderr,"Effect %02X: %s\n",id+0x33,sound.error);return 1;}
        for(unsigned tick=0;tick<300;tick++)if(!smps_hle_frame(&sound)){
            fprintf(stderr,"Effect %02X tick %u: %s\n",id+0x33,tick,sound.error);return 1;
        }
    }
    CHECK(!memcmp(image,before,(size_t)bytes));
    s3_hle_sound_assets_free(&assets);
    const struct {const char *name;unsigned donor,song,event,interrupt;} choices[]={
        {"s2-wai",3,7,0x2A,1},{"s2-wai",3,9,0x31,0},
        {"s2-aug",2,7,0x2A,1},{"s2-aug",2,9,0x31,0},{"s2-aug",2,13,0x19,0},
        {"s2-wai",3,7,0x14,0},{"s2",1,9,0x15,0}};
    for(unsigned i=0;argc==3 && i<sizeof choices/sizeof *choices;i++){
        CHECK(s3_hle_sound_assets(image,(size_t)bytes,&assets,error,sizeof error));
        char path[1200];snprintf(path,sizeof path,"%s/%s.bin",argv[2],choices[i].name);
        FILE *donor_file=fopen(path,"rb");CHECK(donor_file);CHECK(!fseek(donor_file,0,SEEK_END));
        long donor_size=ftell(donor_file);rewind(donor_file);CHECK(donor_size>0);
        uint8_t *donor=malloc((size_t)donor_size);CHECK(donor);CHECK(fread(donor,1,(size_t)donor_size,donor_file)==(size_t)donor_size);fclose(donor_file);
        S3SoundLayout layout={8192,0x400000,0x400100,0x400500,0x400900};
        S3SoundPCMLayout pcm={0x400A00,0x400F00,0x400F80,0xC6,126};
        uint8_t metadata[8192];memcpy(metadata,assets.data,sizeof metadata);
        S3SoundMusicCache cache={0};unsigned next=70;
        if(!s3_sound_build_track(donor,(size_t)donor_size,choices[i].donor,choices[i].song,choices[i].event,
            &layout,&pcm,metadata,assets.data,0x400000,&assets.bytes,&next,&cache,error,sizeof error)){
            fprintf(stderr,"Donor %s %u: %s\n",choices[i].name,choices[i].song,error);return 1;
        }
        s3_sound_music_cache_free(&cache);s3_hle_sound_assets_index(&assets);
        SmpsHle sound;Output out={0};smps_hle_init(&sound,assets.data,assets.bytes,emit,&out);
        CHECK(smps_hle_music(&sound,assets.music[0x25-1],0));
        for(unsigned n=0;n<60;n++)CHECK(smps_hle_frame(&sound));
        SmpsHleClock previous=sound.state.music;
        CHECK(smps_hle_music(&sound,assets.music[choices[i].event-1],choices[i].interrupt?3:0));
        unsigned tick=0;
        for(;tick<12000;tick++){
            if(!smps_hle_frame(&sound)){fprintf(stderr,"Donor %s song %u tick %u: %s\n",choices[i].name,choices[i].song,tick,sound.error);return 1;}
            if(choices[i].interrupt && !sound.state.interrupted)break;
            if(!choices[i].interrupt && tick==1199)break;
        }
        if(choices[i].interrupt)CHECK(tick>300 && tick<12000 && !memcmp(&previous,&sound.state.music,sizeof previous));
        printf("Host donor %s song %u event %02X passed %u ticks%s\n",choices[i].name,choices[i].song,choices[i].event,tick,choices[i].interrupt?" and restored music":"");
        free(donor);s3_hle_sound_assets_free(&assets);
    }
    free(before);free(image);return 0;
}
