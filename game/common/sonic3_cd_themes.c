/* Native stereo CD event playback. Clone owns the cartridge queues and SFX;
 * a silent music entry gives the selected CD recording the music channels.
 * Only sample positions, pause and fade clocks belong in execution snapshots. */
#include "sonic3_cd_themes.h"
#include "sonic3_sound_themes.h"
#include "sonic3_state_io.h"
#include "audio.h"
#include "genesis_clocks.h"
#include "sha256.h"
#include "sonic3_vgm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Song {uint8_t *pcm;uint32_t frames,gain;uint8_t hash[32];unsigned native,custom,loop,vgm;} Song;
static Song songs[S3_MUSIC_COUNT];
typedef struct CDClock {uint64_t position,frame_position;uint32_t theme,active,paused,volume,fade,carry,deferred;} CDClock;
static CDClock clock_state;
static const unsigned events[S3_MUSIC_COUNT]={
    [S3_THEME_OVER]=0x27,[S3_THEME_CLEAR]=0x29,[S3_THEME_INVINCIBLE]=0x2C,
    [S3_MUSIC_BOSS]=0x19,[S3_MUSIC_MINIBOSS]=0x18,[S3_MUSIC_FINAL]=0x30};
static unsigned le16(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t le32(const uint8_t *p){return le16(p)|((uint32_t)le16(p+2)<<16);}
static void be32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int cd_event(unsigned t){return t<S3_MUSIC_COUNT&&events[t]!=0;}
static int cd_loop(unsigned t){if(t<S3_MUSIC_COUNT&&songs[t].custom)return (int)songs[t].loop;return t==S3_THEME_INVINCIBLE||t==S3_MUSIC_BOSS||t==S3_MUSIC_MINIBOSS||t==S3_MUSIC_FINAL;}
static int hex(const char *p,size_t n)
{if(strlen(p)!=n)return 0;for(size_t i=0;i<n;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return 1;}
static int wav(const char *path,const char *hash,Song *out)
{
    FILE *f=fopen(path,"rb");if(!f)return 0;
    if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long n=ftell(f);rewind(f);
    if(n<48||n>32*1024*1024+44){fclose(f);return 0;}
    uint8_t *raw=malloc((size_t)n);if(!raw){fclose(f);return 0;}
    int ok=fread(raw,1,(size_t)n,f)==(size_t)n;fclose(f);
    uint8_t digest[32];char actual[65];
    if(ok){recompui_sha256_compute(raw,(size_t)n,digest);for(unsigned i=0;i<32;i++)snprintf(actual+2*i,3,"%02x",digest[i]);}
    ok=ok&&hex(hash,64)&&!strcmp(actual,hash)&&!memcmp(raw,"RIFF",4)&&le32(raw+4)==(uint32_t)n-8&&
        !memcmp(raw+8,"WAVEfmt ",8)&&le32(raw+16)==16&&le16(raw+20)==1&&le16(raw+22)==2&&
        le32(raw+24)==44100&&le32(raw+28)==176400&&le16(raw+32)==4&&le16(raw+34)==16&&
        !memcmp(raw+36,"data",4)&&le32(raw+40)==(uint32_t)n-44&&!(le32(raw+40)&3);
    if(ok&&out){out->frames=((uint32_t)n-44)/4;memcpy(out->hash,digest,32);memmove(raw,raw+44,(size_t)n-44);out->pcm=raw;raw=NULL;}
    free(raw);return ok;
}
static void mix(int16_t *out,size_t frames,double rate);
int s3_recording_verify(const char *path,const char *hash){return wav(path,hash,NULL);}
static int silent_music(unsigned native,const S3SoundLayout *l,uint8_t *bank,size_t cap,uint32_t *used)
{
    unsigned dest=used?(*used+1u)&~1u:0;
    if(native<1||native>0x33||!l||!bank||!used||dest>cap||cap-dest<82)return 0;
    uint8_t *p=bank+dest;memset(p,0,82);p[1]=56;p[2]=7;p[3]=3;p[6]=p[7]=1;
    for(unsigned i=0;i<10;i++){unsigned at=i<7?8+i*4:36+(i-7)*6;p[at+1]=54;}
    p[54]=0xFF;p[55]=0x10;be32(bank+l->music_index-0x400000+(native-1)*4,0x01400000u|dest);*used=dest+82;return 1;
}
int s3_recording_build(unsigned t,unsigned native,const char *path,const char *hash,int loop,
                      const S3SoundLayout *l,uint8_t *bank,size_t cap,uint32_t *used,char *error,size_t en)
{
    Song song={0};
    if(t>=S3_MUSIC_COUNT || t==S3_THEME_LIFE || !wav(path,hash,&song)||!silent_music(native,l,bank,cap,used)) {
        free(song.pcm);
        if(error&&en)snprintf(error,en,"Provide a verified custom recording; 1-UP requires synthesized music");return 0;
    }
    song.gain=65536;song.native=native;song.custom=1;song.loop=loop!=0;
    free(songs[t].pcm);songs[t]=song;audio_set_extra_source(mix);return 1;
}
int s3_vgm_theme_build(unsigned t,unsigned native,const char *path,const char *hash,
                      const S3SoundLayout *l,uint8_t *bank,size_t cap,uint32_t *used,char *error,size_t en)
{
    if(t>=S3_MUSIC_COUNT||t==S3_THEME_LIFE||!s3_vgm_build(t,path,hash)||!silent_music(native,l,bank,cap,used)){
        if(error&&en)snprintf(error,en,"Custom VGM failed validation; 1-UP needs a restoring SMPS jingle");return 0;}
    Song song={0};song.frames=UINT32_MAX;song.gain=65536;song.native=native;song.custom=1;song.vgm=1;
    for(unsigned i=0;i<32;i++){unsigned a=(unsigned char)hash[i*2],b=(unsigned char)hash[i*2+1];song.hash[i]=(uint8_t)(((a<='9'?a-'0':a-'a'+10)<<4)|(b<='9'?b-'0':b-'a'+10));}
    free(songs[t].pcm);songs[t]=song;audio_set_extra_source(mix);return 1;
}
static int manifest_read(const char *path,unsigned donor,int wanted,Song *out,uint64_t *available)
{
    if(!path||donor!=S3_DONOR_CD_US)return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    char line[1400],source[65]="",relative[S3_MUSIC_COUNT][160]={{0}},hashes[S3_MUSIC_COUNT][65]={{0}};
    unsigned format=0,region=0,lines=0;int ok=1;
    while(fgets(line,sizeof line,f)){
        if(++lines>24){ok=0;break;}line[strcspn(line,"\r\n")]=0;
        char *value=strchr(line,'=');if(!value){ok=0;break;}*value++=0;
        if(!strcmp(line,"format"))format=!strcmp(value,"1");
        else if(!strcmp(line,"donor"))region=!strcmp(value,s3_donor_ids[donor]);
        else if(!strcmp(line,"source_sha256")&&strlen(value)==64)snprintf(source,sizeof source,"%s",value);
        else for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(cd_event(t)){
            char key[64];snprintf(key,sizeof key,"%s.path",s3_theme_ids[t]);
            if(!strcmp(line,key)&&strlen(value)<sizeof relative[t])snprintf(relative[t],sizeof relative[t],"%s",value);
            snprintf(key,sizeof key,"%s.sha256",s3_theme_ids[t]);
            if(!strcmp(line,key)&&strlen(value)==64)snprintf(hashes[t],sizeof hashes[t],"%s",value);
        }
    }
    if(ferror(f))ok=0;fclose(f);if(!ok||!format||!region||!hex(source,64))return 0;
    char root[1200];snprintf(root,sizeof root,"%s",path);char *a=strrchr(root,'/'),*b=strrchr(root,'\\');if(!a||(b&&b>a))a=b;
    size_t prefix=a?(size_t)(a-root)+1:0;
    uint64_t found=0;
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(cd_event(t)&&(wanted<0||wanted==(int)t)){
        /* Existing libraries keep their three verified event recordings.
         * Missing added tracks require reimport only when selected. */
        if(wanted<0&&t>=S3_THEME_COUNT&&!*relative[t]&&!*hashes[t])continue;
        char expected[160];snprintf(expected,sizeof expected,"%s-%s/%s.wav",s3_donor_ids[donor],source,s3_theme_ids[t]);
        if(strcmp(relative[t],expected)||!hex(hashes[t],64)||prefix+strlen(expected)>=sizeof root)return 0;
        snprintf(root+prefix,sizeof root-prefix,"%s",expected);
        if(!wav(root,hashes[t],wanted==(int)t?out:NULL))return 0;
        found|=UINT64_C(1)<<t;
    }
    if(available)*available=found;
    return 1;
}
uint64_t s3_cd_donor_available(const char *manifest,unsigned donor)
{uint64_t found=0;return manifest_read(manifest,donor,-1,NULL,&found)?found:0;}
int s3_cd_donor_verify(const char *manifest,unsigned donor){return s3_cd_donor_available(manifest,donor)!=0;}
static int32_t sample(const Song *s,uint32_t frame,unsigned channel)
{return (int16_t)le16(s->pcm+(size_t)frame*4+channel*2);}
static void mix(int16_t *out,size_t frames,double rate)
{
    if(!clock_state.active||clock_state.paused||clock_state.theme>=S3_MUSIC_COUNT||rate<1||rate>384000)return;
    if(songs[clock_state.theme].vgm){s3_vgm_mix(out,frames,rate,clock_state.volume);return;}
    const Song *s=&songs[clock_state.theme];if(!s->pcm||!s->frames)return;
    uint64_t step=(uint64_t)(44100.0*4294967296.0/rate+0.5);
    uint64_t position=clock_state.frame_position;
    for(size_t i=0;i<frames;i++){
        uint32_t at=(uint32_t)(position>>32);
        if(at>=s->frames){
            if(!cd_loop(clock_state.theme))break;
            position%=((uint64_t)s->frames<<32);at=(uint32_t)(position>>32);
        }
        unsigned next=at+1<s->frames?at+1:cd_loop(clock_state.theme)?0:at;
        unsigned fraction=(unsigned)(position&0xFFFFFFFFu)>>16;
        for(unsigned ch=0;ch<2;ch++){
            int64_t a=sample(s,at,ch),b=sample(s,next,ch);
            int64_t value=a+(((b-a)*fraction)>>16);
            value=(value*clock_state.volume*s->gain)/4294967296LL;
            value+=out[i*2+ch];out[i*2+ch]=(int16_t)(value>32767?32767:value<-32768?-32768:value);
        }
        position+=step;
    }
}
void s3_cd_themes_reset(void)
{
    s3_vgm_reset();
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++){free(songs[t].pcm);memset(&songs[t],0,sizeof songs[t]);}
    memset(&clock_state,0,sizeof clock_state);
}
int s3_cd_theme_build(unsigned t,unsigned donor,const char *manifest,const S3SoundLayout *l,
                      uint8_t *bank,size_t cap,uint32_t *used,char *error,size_t error_size)
{
    if(!cd_event(t)||!l||!bank||!used||*used>cap||cap-*used<82||!manifest_read(manifest,donor,(int)t,&songs[t],NULL)){
        if(error&&error_size)snprintf(error,error_size,"Provide the matching Sonic CD disc with audio tracks");return 0;
    }
    unsigned dest=(*used+1)&~1u;uint8_t *p=bank+dest;memset(p,0,82);p[1]=56;p[2]=7;p[3]=3;p[6]=p[7]=1;
    for(unsigned i=0;i<10;i++){unsigned off=i<7?8+i*4:8+7*4+(i-7)*6;p[off+1]=54;}
    p[54]=0xFF;p[55]=0x10;
    /* Fixed Q16 mixer levels, calibrated from the supported US recordings
     * against the existing CD campaign's -24.2 LUFS music reference. No cold
     * analysis or floating-point gain enters playback or snapshots. */
    songs[t].gain=t==S3_THEME_OVER?23795u:t==S3_THEME_CLEAR?15541u:
        t==S3_MUSIC_FINAL?20964u:(t==S3_MUSIC_BOSS||t==S3_MUSIC_MINIBOSS)?19565u:17437u;
    be32(bank+l->music_index-0x400000+(events[t]-1)*4,0x01400000u|dest);*used=dest+82;
    audio_set_extra_source(mix);return 1;
}
void s3_cd_theme_driver_init(void)
{
    /* A level may reset its cartridge driver before the longer CD clear has
     * ended. Keep only that recording; all ordinary driver state still resets. */
    if(clock_state.active&&clock_state.theme==S3_THEME_CLEAR&&clock_state.carry)return;
    else memset(&clock_state,0,sizeof clock_state);
}
void s3_cd_theme_finish_clear(void)
{
    if(clock_state.active&&clock_state.theme==S3_THEME_CLEAR)clock_state.carry=1;
}
unsigned s3_cd_theme_resume_music(void)
{
    if(clock_state.active)return 0;
    unsigned native=clock_state.deferred;
    clock_state.carry=clock_state.deferred=0;
    return native;
}
int s3_cd_theme_request(unsigned native)
{
    if(native==0x2A)return 1; /* Clone backs up/restores the silent music entry. */
    if(clock_state.active&&clock_state.theme==S3_THEME_CLEAR&&clock_state.carry){
        if(native==0xE1||native==0xE5)return 0;
        if(clock_state.carry&&native>=1&&native<=0x17){
            clock_state.deferred=native;return 0;
        }
    }
    if(native>=1&&native<=0x33){
        for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if((cd_event(t)||songs[t].custom)&&native==(songs[t].custom?songs[t].native:events[t])&&(songs[t].pcm||songs[t].vgm)){
            memset(&clock_state,0,sizeof clock_state);clock_state.theme=t;clock_state.active=1;clock_state.volume=65536;
            if(songs[t].vgm)s3_vgm_start(t);return 1;
        }
        clock_state.active=clock_state.carry=clock_state.deferred=0;
    }else if(native==0xE1||native==0xE5)clock_state.fade=1;
    else if(native==0xE0||native==0xE2||native==0xFF)clock_state.active=clock_state.carry=clock_state.deferred=0;
    return 1;
}
void s3_cd_theme_frame(int paused,int oneup)
{
    clock_state.paused=paused||oneup;
    if(clock_state.active&&songs[clock_state.theme].vgm){
        if(s3_vgm_finished()){clock_state.active=0;return;}
        s3_vgm_frame(clock_state.theme,clock_state.paused);
    }
    if(clock_state.active&&!clock_state.paused){
        const Song *s=&songs[clock_state.theme];
        uint64_t length=(uint64_t)s->frames<<32;
        if(clock_state.position>=length){
            if(!cd_loop(clock_state.theme)){clock_state.active=0;return;}
            clock_state.position%=length;
        }
        /* The qualified NTSC simulation drains 895780 master cycles per
         * tick and resets the PSG remainder, giving 3732 internal samples.
         * Advance on that same VBlank, even in turbo/replay/rollback. The
         * output mixer reads the completed interval without changing it. */
        const uint64_t step=((UINT64_C(44100)<<32)+GENESIS_PSG_SAMPLE_RATE_NTSC/2)/GENESIS_PSG_SAMPLE_RATE_NTSC;
        clock_state.frame_position=clock_state.position;
        clock_state.position+=(895780u/240u)*step;
    }
    if(clock_state.active&&clock_state.fade&&!clock_state.paused){
        if(clock_state.volume<=1457){clock_state.volume=0;clock_state.active=0;}else clock_state.volume-=1457;
    }
}
void s3_cd_theme_state(struct S3StateIO *io)
{
    CDClock incoming=clock_state;
    s3_state_peek(io,0,&incoming,sizeof incoming);
    if(incoming.theme>=S3_MUSIC_COUNT||incoming.active>1||incoming.paused>1||incoming.volume>65536||incoming.fade>1||
       incoming.carry>1||incoming.deferred>0x17||(!incoming.carry&&incoming.deferred)||
       (incoming.carry&&incoming.theme!=S3_THEME_CLEAR)||
       (incoming.active && ((!songs[incoming.theme].pcm&&!songs[incoming.theme].vgm)||!songs[incoming.theme].frames)))io->ok=0;
    S3_STATE(io,clock_state);
}
void s3_cd_theme_identity(uint8_t out[32])
{
    uint8_t hashes[S3_MUSIC_COUNT*48]={0};for(unsigned t=0;t<S3_MUSIC_COUNT;t++){uint8_t *p=hashes+t*48;memcpy(p,songs[t].hash,32);be32(p+32,songs[t].gain);be32(p+36,songs[t].native);be32(p+40,songs[t].custom);be32(p+44,songs[t].loop);}
    recompui_sha256_compute(hashes,sizeof hashes,out);
}
void s3_cd_theme_debug(unsigned *active,unsigned *theme,uint64_t *position)
{if(active)*active=clock_state.active;if(theme)*theme=clock_state.theme;if(position)*position=clock_state.position;}
