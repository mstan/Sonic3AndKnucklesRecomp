/* Stereo fixture and playback clocks contain no disc or game assets. */
#include "../../game/common/sonic3_cd_themes.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define TRACK(symbol,id,...) id,
const char *const s3_theme_ids[]={
#include "../../game/common/sonic3_music_catalog.inc"
};
#undef TRACK
const char *const s3_donor_ids[]={"s1","s2","s2-aug","s2-wai","s3-proto","s3","sk","cd-us"};
static AudioExtraSource registered;
void audio_set_extra_source(AudioExtraSource source){registered=source;}
static void song(unsigned theme)
{
    static const uint8_t pcm[]={0xE8,3,0x30,0xF8,0xB8,0x0B,0x60,0xF0,0x88,0x13,0x90,0xE8,0x58,0x1B,0xC0,0xE0};
    songs[theme].pcm=malloc(sizeof pcm);CHECK(songs[theme].pcm);memcpy(songs[theme].pcm,pcm,sizeof pcm);songs[theme].frames=4;songs[theme].gain=32768;
}
int main(int argc,char **argv)
{
    s3_cd_themes_reset();song(S3_THEME_OVER);song(S3_THEME_CLEAR);song(S3_THEME_INVINCIBLE);
    s3_cd_theme_request(0x2C);int16_t out[32]={0};mix(out,6,44100);
    static const int16_t expected[]={500,-1000,1500,-2000,2500,-3000,3500,-4000,500,-1000,1500,-2000};
    CHECK(!memcmp(out,expected,sizeof expected));CHECK(!clock_state.position);
    s3_cd_theme_frame(0,0);CHECK(clock_state.position&&!clock_state.frame_position);
    CDClock rendered=clock_state;
    int16_t repeat[32]={0};mix(repeat,6,44100);
    CHECK(!memcmp(out,repeat,sizeof expected)&&!memcmp(&rendered,&clock_state,sizeof rendered));
    uint64_t position=clock_state.position;s3_cd_theme_frame(1,0);memset(out,0,sizeof out);mix(out,8,44100);
    CHECK(!out[0]&&clock_state.position==position);s3_cd_theme_frame(0,1);mix(out,8,44100);CHECK(clock_state.position==position);
    s3_cd_theme_frame(0,0);s3_cd_theme_request(0x2A);CHECK(clock_state.active);
    uint8_t state[sizeof clock_state];S3StateIO save={state,sizeof state,0,0,1};s3_cd_theme_state(&save);CHECK(save.ok&&save.pos==sizeof state);
    int16_t first[16]={0},second[16]={0};mix(first,8,48000);
    S3StateIO validate={state,sizeof state,0,1,1};s3_cd_theme_state(&validate);CHECK(validate.ok);
    S3StateIO load={state,sizeof state,0,2,1};s3_cd_theme_state(&load);CHECK(load.ok);mix(second,8,48000);CHECK(!memcmp(first,second,sizeof first));
    CDClock before,bad;memcpy(&before,&clock_state,sizeof before);memcpy(&bad,&clock_state,sizeof bad);bad.theme=S3_MUSIC_COUNT;memcpy(state,&bad,sizeof bad);
    validate.pos=0;validate.ok=1;s3_cd_theme_state(&validate);CHECK(!validate.ok&&!memcmp(&before,&clock_state,sizeof before));
    memcpy(&bad,&clock_state,sizeof bad);bad.theme=S3_THEME_LIFE;bad.active=1;memcpy(state,&bad,sizeof bad);
    validate.pos=0;validate.ok=1;s3_cd_theme_state(&validate);CHECK(!validate.ok&&!memcmp(&before,&clock_state,sizeof before));
    s3_cd_theme_request(0x27);s3_cd_theme_frame(0,0);memset(out,0,sizeof out);mix(out,6,44100);CHECK(clock_state.active);CHECK(!memcmp(out,expected,16)&&!out[8]&&!out[9]);
    s3_cd_theme_frame(0,0);CHECK(!clock_state.active);
    s3_cd_theme_request(0x2C);s3_cd_theme_request(1);CHECK(!clock_state.active);
    s3_cd_theme_request(0x2C);s3_cd_theme_request(0xE1);for(unsigned i=0;i<45;i++)s3_cd_theme_frame(0,0);CHECK(!clock_state.active);
    const unsigned bosses[]={S3_MUSIC_BOSS,S3_MUSIC_MINIBOSS,S3_MUSIC_FINAL};
    for(unsigned i=0;i<3;i++){
        unsigned t=bosses[i];song(t);s3_cd_theme_request(events[t]);memset(out,0,sizeof out);mix(out,6,44100);
        CHECK(clock_state.active&&clock_state.theme==t&&!memcmp(out,expected,sizeof expected));
        s3_cd_theme_frame(0,0);s3_cd_theme_frame(0,0);CHECK(clock_state.active);
        CDClock prior=clock_state;s3_cd_theme_request(0x2A);s3_cd_theme_frame(0,1);CHECK(clock_state.position==prior.position);
        s3_cd_theme_request(0x29);CHECK(clock_state.theme==S3_THEME_CLEAR);
    }
    s3_cd_themes_reset();
    if(argc>=2){
        CHECK(s3_cd_donor_verify(argv[1],S3_DONOR_CD_US));CHECK(!s3_cd_donor_verify(argv[1],S3_DONOR_S3));
        uint8_t bank[4096]={0};S3SoundLayout layout={512,0x400000,0,0,0};uint32_t used=512;char error[256];
        CHECK(s3_cd_theme_build(S3_THEME_INVINCIBLE,S3_DONOR_CD_US,argv[1],&layout,bank,sizeof bank,&used,error,sizeof error));
        CHECK(registered&&used==594);s3_cd_theme_request(0x2C);CHECK(clock_state.active);
        s3_cd_theme_frame(0,0);uint64_t position=clock_state.position;CHECK(position);
        memset(out,0,sizeof out);registered(out,16,48000);CHECK(clock_state.position==position);
        CDClock audible=clock_state;
        for(unsigned i=0;i<40;i++){s3_cd_theme_frame(0,0);registered(out,16,48000);}
        CDClock with_output=clock_state;clock_state=audible;
        for(unsigned i=0;i<40;i++)s3_cd_theme_frame(0,0);
        CHECK(!memcmp(&with_output,&clock_state,sizeof clock_state));
        if(argc==3){
            int expanded=!strcmp(argv[2],"expanded");
            uint64_t mask=s3_cd_donor_available(argv[1],S3_DONOR_CD_US);
            CHECK(mask&(UINT64_C(1)<<S3_THEME_INVINCIBLE));
            for(unsigned i=0;i<3;i++){
                unsigned t=bosses[i];CHECK(!!(mask&(UINT64_C(1)<<t))==expanded);
                CHECK(s3_cd_theme_build(t,S3_DONOR_CD_US,argv[1],&layout,bank,sizeof bank,&used,error,sizeof error)==expanded);
                if(expanded){s3_cd_theme_request(events[t]);CHECK(clock_state.active&&clock_state.theme==t);}
            }
        }
        s3_cd_themes_reset();
    }
    puts("PASS CD stereo/loop/one-shot, cartridge transition, pause/1-UP hold, fade and exact current-build rewind; invalid state rejection");return 0;
}
