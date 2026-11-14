#include "sonic3_vgm.h"
#include "sonic3_vgm_format.h"
#include "sonic3_state_io.h"
#include "sonic3_sound_themes.h"
#include "audio/ym2612.h"
#include "audio/sn76489.h"
#include "app_config.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"VGM line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
AppConfig g_app_config;
size_t s3_vgm_fm_rb_save(void *,size_t);size_t s3_vgm_psg_rb_save(void *,size_t);
static void put32(uint8_t *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put64(uint8_t *p,uint64_t v){put32(p,(unsigned)v);put32(p+4,(unsigned)(v>>32));}
static unsigned count;
static uint8_t bytes[4096];
static void event(unsigned at,unsigned kind,unsigned a,unsigned b)
{uint8_t *p=bytes+32+16*count++;put64(p,at);p[8]=(uint8_t)kind;p[9]=(uint8_t)a;p[10]=(uint8_t)b;}
static unsigned energy(const int16_t *p,size_t n){unsigned sum=0;for(size_t i=0;i<n;i++)sum+=(unsigned)(p[i]<0?-p[i]:p[i]);return sum;}
int main(int argc,char **argv)
{
    CHECK(argc==2);memcpy(bytes,"S3VG",4);put32(bytes+4,1);
    event(0,0,0x80|6,0);event(0,0,0x10,0);event(0,0,0x90,0);
    event(0,1,0xb0,7);event(0,1,0xb4,0xc0);
    for(unsigned i=0;i<4;i++){event(0,1,0x30+i*4,1);event(0,1,0x40+i*4,0x18);event(0,1,0x50+i*4,0x1f);event(0,1,0x80+i*4,0x0f);}
    event(0,1,0xa4,0x22);event(0,1,0xa0,0x69);event(0,1,0x28,0xf0);
    unsigned loop=count;event(1000000,0,0x87,0);event(2000000,0,0x86,0);
    put32(bytes+8,count);put32(bytes+12,loop);put64(bytes+16,3000000);put64(bytes+24,1000000);
    size_t length=32+count*16;CHECK(s3_vgm_valid(bytes,length));bytes[43]=1;CHECK(!s3_vgm_valid(bytes,length));bytes[43]=0;
    FILE *f=fopen(argv[1],"wb");CHECK(f&&fwrite(bytes,1,length,f)==length);fclose(f);
    uint8_t hash[32];char text[65];recompui_sha256_compute(bytes,length,hash);for(unsigned i=0;i<32;i++)snprintf(text+i*2,3,"%02x",hash[i]);
    CHECK(s3_vgm_build(S3_MUSIC_AIZ2,argv[1],text));CHECK(s3_vgm_channels(S3_MUSIC_AIZ2)==0x81);
    ym2612_init();psg_init();size_t nf=ym2612_rb_save(NULL,0),np=psg_rb_save(NULL,0);uint8_t *oldf=malloc(nf),*oldp=malloc(np),*check=malloc(nf>np?nf:np);
    CHECK(oldf&&oldp&&check);ym2612_rb_save(oldf,nf);psg_rb_save(oldp,np);
    s3_vgm_start(S3_MUSIC_AIZ2);s3_vgm_audition(0x7ff);
    int16_t first[7464]={0},second[7464]={0};s3_vgm_frame(S3_MUSIC_AIZ2,0);s3_vgm_mix(first,3732,223721,65536);CHECK(energy(first,7464)>100000);
    s3_vgm_mix(second,3732,223721,65536);CHECK(!memcmp(first,second,sizeof first));
    S3StateIO measure={0};s3_vgm_state(&measure);CHECK(measure.pos>30000);uint8_t *state=malloc(measure.pos),*after=malloc(measure.pos);CHECK(state&&after);
    S3StateIO save={state,measure.pos,0,0,1};s3_vgm_state(&save);CHECK(save.ok);
    /* Corrupt event cursors and chip extents must fail without adopting any
     * part of the incoming state. */
    uint8_t *bad=malloc(measure.pos);CHECK(bad);memcpy(bad,state,measure.pos);put32(bad+20,0xffffffff);
    S3StateIO invalid={bad,measure.pos,0,1,1};s3_vgm_state(&invalid);CHECK(!invalid.ok);
    save.data=after;save.pos=0;s3_vgm_state(&save);CHECK(!memcmp(state,after,measure.pos));
    memcpy(bad,state,measure.pos);size_t clock_size=measure.pos-s3_vgm_fm_rb_save(NULL,0)-s3_vgm_psg_rb_save(NULL,0)-8;
    bad[clock_size+4]^=1;invalid.pos=0;invalid.ok=1;s3_vgm_state(&invalid);CHECK(!invalid.ok);
    save.pos=0;s3_vgm_state(&save);CHECK(!memcmp(state,after,measure.pos));free(bad);
    for(unsigned i=0;i<20;i++)s3_vgm_frame(S3_MUSIC_AIZ2,0);CHECK(!s3_vgm_finished());
    s3_vgm_audition(1u<<7);s3_vgm_frame(S3_MUSIC_AIZ2,0);memset(first,0,sizeof first);s3_vgm_mix(first,3732,223721,65536);CHECK(energy(first,7464)>1000);
    s3_vgm_audition(0);s3_vgm_frame(S3_MUSIC_AIZ2,0);s3_vgm_frame(S3_MUSIC_AIZ2,0);memset(first,0,sizeof first);s3_vgm_mix(first,3732,223721,65536);
    /* The hardware PSG filter can retain a constant rounding DC offset.
     * Muting must remove AC, without resetting its canonical filter state. */
    for(unsigned c=0;c<2;c++)for(unsigned i=1;i<3732;i++)CHECK(first[i*2+c]==first[c]);
    S3StateIO validate={state,measure.pos,0,1,1};s3_vgm_state(&validate);CHECK(validate.ok);
    S3StateIO load={state,measure.pos,0,2,1};s3_vgm_state(&load);CHECK(load.ok);
    save.data=after;save.pos=0;s3_vgm_state(&save);CHECK(save.ok&&!memcmp(state,after,measure.pos));
    memset(first,0,sizeof first);memset(second,0,sizeof second);s3_vgm_frame(S3_MUSIC_AIZ2,0);s3_vgm_mix(first,3732,223721,65536);
    load.pos=0;s3_vgm_state(&load);s3_vgm_frame(S3_MUSIC_AIZ2,0);s3_vgm_mix(second,3732,223721,65536);CHECK(!memcmp(first,second,sizeof first));
    s3_vgm_frame(S3_MUSIC_AIZ2,1);memset(second,0,sizeof second);s3_vgm_mix(second,3732,223721,65536);CHECK(!energy(second,7464));
    CHECK(ym2612_rb_save(check,nf)==nf&&!memcmp(check,oldf,nf));CHECK(psg_rb_save(check,np)==np&&!memcmp(check,oldp,np));
    free(state);free(after);free(oldf);free(oldp);free(check);s3_vgm_reset();puts("PASS VGM real FM/PSG, loop, masks, pure mix, pause, state, invalid-state rejection and cartridge-chip isolation");return 0;
}
