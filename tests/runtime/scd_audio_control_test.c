/* Synthetic PCM16 stream: no ROM, disc, BIOS or converted game data. */
#include "../../game/trilogy/scd_audio.c"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
void audio_set_extra_source(AudioExtraSource source) { (void)source; }
static unsigned discarded;
void audio_discard_playback(void) { discarded++; }
int scd_disc_open(void) { return 0; }
void scd_disc_close(void) {}
const ScdTrack *scd_track_info(int number) { (void)number;return NULL; }
const uint8_t *scd_disc_file(const char *name,uint32_t *size)
{ (void)name;if(size)*size=0;return NULL; }
static void stream(int loop)
{
    static const uint8_t pcm[]={0xE8,3,0x30,0xF8,0xB8,0x0B,0x60,0xF0,0x88,0x13,0x90,0xE8,0x58,0x1B,0xC0,0xE0};
    scd_audio_reset();s_cd.f=tmpfile();CHECK(s_cd.f);
    CHECK(fwrite(pcm,1,sizeof pcm,s_cd.f)==sizeof pcm);
    s_cd.frames=4;s_cd.track=2;s_cd.loop=loop;s_cd.vol=s_cd.gain=1;s_cd.buf_at=-1;
}
int main(void)
{
    /* Cold-track preparation retires device history, but never discards
     * generated samples or interrupts a request for the already playing song. */
    ScdTrack missing={0};strcpy(missing.path,"missing-audio-fixture.bin");
    CHECK(!measure_lufs(&missing) && !discarded);
    FILE *source=fopen("loudness-fixture.bin","wb");CHECK(source);
    int16_t silence[64]={0};CHECK(fwrite(silence,1,sizeof silence,source)==sizeof silence);fclose(source);
    ScdTrack track={0};strcpy(track.path,"loudness-fixture.bin");track.size=sizeof silence;
    CHECK(!measure_lufs(&track) && discarded==1);remove(track.path);
    int16_t out[32]={0};
    stream(1);mix(out,6,44100);
    scd_cd_play(2,1);CHECK(discarded==1);
    static const int16_t expected[]={1000,-2000,3000,-4000,5000,-6000,7000,-8000,1000,-2000,3000,-4000};
    CHECK(!memcmp(out,expected,sizeof expected));CHECK(s_cd.pos==2);
    uint8_t fake_bank=0;s_bank=&fake_bank;s_bank_size=1;s_pcm_frac=.25;s_pcm_count=17;
    scd_audio_level_pause(1);for(unsigned i=0;i<32;i++)out[i]=123;
    mix(out,16,44100);for(unsigned i=0;i<32;i++)CHECK(out[i]==123);
    CHECK(s_cd.pos==2 && s_cd.frac==0 && s_pcm_frac==.25 && s_pcm_count==17);
    s_bank=NULL;s_bank_size=0;scd_audio_level_pause(0);memset(out,0,sizeof out);mix(out,2,44100);
    CHECK(out[0]==5000 && out[1]==-6000 && out[2]==7000 && out[3]==-8000);
    scd_cd_pause(1);memset(out,0,sizeof out);mix(out,2,44100);CHECK(!out[0] && s_cd.pos==0);
    scd_cd_pause(0);mix(out,1,44100);CHECK(out[0]==1000 && out[1]==-2000 && s_cd.pos==1);
    scd_cd_fade();for(unsigned i=0;i<36000;i++){out[0]=out[1]=0;mix(out,1,44100);}
    CHECK(!scd_cd_track());
    stream(0);memset(out,0,sizeof out);mix(out,6,44100);
    CHECK(!memcmp(out,expected,16) && !out[8] && !out[9] && !scd_cd_track());
    scd_audio_reset();
    puts("CD audio: stereo samples, exact loop seam, native/scene pause, resume, fade and one-shot end passed");
    return 0;
}
