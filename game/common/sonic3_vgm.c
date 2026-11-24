/* Music-only instances of the existing chips. Synthesis advances on VBlank;
 * the output callback is a pure reader, like the recording playback clock. */
#include "sonic3_vgm.h"
#include "sonic3_vgm_format.h"
#include "sonic3_sound_themes.h"
#include "sonic3_state_io.h"
#include "app_config.h"

void s3_vgm_fm_init(void);void s3_vgm_fm_advance(uint32_t);void s3_vgm_fm_write(uint8_t,uint8_t);void s3_vgm_fm_mask(unsigned);
size_t s3_vgm_fm_available(void);size_t s3_vgm_fm_peek(int16_t*,int16_t*,size_t);size_t s3_vgm_fm_render_pair(int16_t*,int16_t*,size_t);
size_t s3_vgm_fm_rb_save(void*,size_t);int s3_vgm_fm_rb_load(const void*,size_t);
void s3_vgm_psg_init(void);void s3_vgm_psg_advance(uint32_t);void s3_vgm_psg_write(uint8_t);void s3_vgm_psg_mask(unsigned);
size_t s3_vgm_psg_peek(int16_t*,int16_t*,size_t);size_t s3_vgm_psg_render_pair(int16_t*,int16_t*,size_t);
size_t s3_vgm_psg_rb_save(void*,size_t);int s3_vgm_psg_rb_load(const void*,size_t);

enum {VGM_FRAMES=3732,VGM_CHIP_BYTES=8192};
typedef struct VGMDocument {uint8_t *bytes;unsigned count,loop,channels;uint64_t total,start;} VGMDocument;
static VGMDocument documents[S3_MUSIC_COUNT];
static int present;
static struct VGMClock {
    uint64_t master,base;
    uint32_t theme,index,ended,mask,filter_mix,filter_start,filter_target;
    int16_t fm[2],raw_fm[2];
    int16_t filtered[VGM_FRAMES*2],raw[VGM_FRAMES*2];
} vgm;
static int16_t saturate(int value){return (int16_t)(value>32767?32767:value<-32768?-32768:value);}
int s3_vgm_ready(unsigned t){return t<S3_MUSIC_COUNT&&documents[t].bytes!=NULL;}
unsigned s3_vgm_channels(unsigned t){return s3_vgm_ready(t)?documents[t].channels:0;}
int s3_vgm_build(unsigned t,const char *path,const char *hash)
{
    if(t>=S3_MUSIC_COUNT)return 0;size_t size;uint8_t *raw=s3_vgm_read(path,hash,&size);if(!raw)return 0;
    VGMDocument d={raw,s3_vgm_u32(raw+8),s3_vgm_u32(raw+12),0,s3_vgm_u64(raw+16),s3_vgm_u64(raw+24)};
    unsigned latch=0;
    for(unsigned i=0;i<d.count;i++){const uint8_t *e=raw+32+i*16;
        if(e[8]==0){unsigned value=e[9];if(value&128)latch=(value>>4)&7;
            if((latch&1)&&(value&15)<15)d.channels|=1u<<(7+(latch>>1));
        }else if(e[8]==1){
            if(e[9]==0x28&&(e[10]&0xf0)){unsigned ch=e[10]&7;if((ch&3)<3)d.channels|=1u<<((ch&3)+(ch&4?3:0));}
            if(e[9]==0x2b&&(e[10]&128))d.channels|=64;
        }
    }
    free(documents[t].bytes);documents[t]=d;present=1;return 1;
}
void s3_vgm_reset(void)
{
    for(unsigned i=0;i<S3_MUSIC_COUNT;i++){free(documents[i].bytes);memset(&documents[i],0,sizeof documents[i]);}
    present=0;memset(&vgm,0,sizeof vgm);
}
void s3_vgm_audition(unsigned mask){vgm.mask=mask&0x7ff;}
int s3_vgm_finished(void){return vgm.ended!=0;}
void s3_vgm_start(unsigned theme)
{
    unsigned mask=vgm.mask;memset(&vgm,0,sizeof vgm);vgm.theme=theme;vgm.mask=mask?mask:0x7ff;
    vgm.filter_mix=vgm.filter_start=vgm.filter_target=g_app_config.audio_filter_bypass?1118:0;s3_vgm_fm_init();s3_vgm_psg_init();
}
static void advance(unsigned cycles)
{
    s3_vgm_fm_advance(cycles);s3_vgm_psg_advance(cycles);
    if(s3_vgm_fm_available()){
        int16_t filtered[4],raw[4];size_t n=s3_vgm_fm_peek(filtered,raw,2);
        if(n){memcpy(vgm.fm,filtered+(n-1)*2,sizeof vgm.fm);memcpy(vgm.raw_fm,raw+(n-1)*2,sizeof vgm.raw_fm);}
        s3_vgm_fm_render_pair(filtered,raw,2);
    }
}
void s3_vgm_frame(unsigned theme,int paused)
{
    if(!s3_vgm_ready(theme))return;
    memset(vgm.filtered,0,sizeof vgm.filtered);memset(vgm.raw,0,sizeof vgm.raw);
    if(paused)return;
    vgm.filter_start=vgm.filter_mix;vgm.filter_target=g_app_config.audio_filter_bypass?1118:0;vgm.filter_mix=vgm.filter_target;
    VGMDocument *d=&documents[theme];s3_vgm_fm_mask(vgm.mask&127);s3_vgm_psg_mask(vgm.mask>>7);
    for(unsigned i=0;i<VGM_FRAMES;i++){
        uint64_t target=vgm.master+240;
        while(!vgm.ended){
            uint64_t next=vgm.index<d->count?s3_vgm_u64(d->bytes+32+vgm.index*16)+vgm.base:d->total+vgm.base;
            if(next>target)break;
            advance((unsigned)(next-vgm.master));vgm.master=next;
            if(vgm.index==d->count){
                if(d->loop==UINT32_MAX){vgm.ended=1;break;}
                vgm.base+=d->total-d->start;vgm.index=d->loop;continue;
            }
            const uint8_t *e=d->bytes+32+vgm.index++*16;
            if(!e[8])s3_vgm_psg_write(e[9]);else {unsigned port=(e[8]-1)*2;s3_vgm_fm_write((uint8_t)port,e[9]);s3_vgm_fm_write((uint8_t)(port+1),e[10]);}
        }
        advance((unsigned)(target-vgm.master));vgm.master=target;
        int16_t filtered=0,raw=0,discard;s3_vgm_psg_peek(&filtered,&raw,1);s3_vgm_psg_render_pair(&discard,NULL,1);
        for(unsigned c=0;c<2;c++){
            vgm.filtered[i*2+c]=vgm.ended?0:saturate(vgm.fm[c]+filtered/8);
            vgm.raw[i*2+c]=vgm.ended?0:saturate(vgm.raw_fm[c]+raw/8);
        }
    }
}
void s3_vgm_mix(int16_t *out,size_t frames,double rate,unsigned volume)
{
    (void)rate;if(!frames)return;unsigned target=vgm.filter_target;
    for(size_t i=0;i<frames;i++){
        unsigned at=(unsigned)(i*VGM_FRAMES/frames);
        unsigned blend=vgm.filter_start;if(blend<target)blend+=(unsigned)i+1<target-blend?(unsigned)i+1:target-blend;else if(blend>target)blend-=(unsigned)i+1<blend-target?(unsigned)i+1:blend-target;
        for(unsigned c=0;c<2;c++){int a=vgm.filtered[at*2+c],b=vgm.raw[at*2+c];int value=a+(b-a)*(int)blend/1118;out[i*2+c]=saturate(out[i*2+c]+(int)((int64_t)value*volume/65536));}
    }
}
static void chip_state(S3StateIO *io,size_t(*save)(void*,size_t),int(*load)(const void*,size_t))
{
    uint8_t old[VGM_CHIP_BYTES]={0},next[VGM_CHIP_BYTES]={0};size_t n=save(old,sizeof old);unsigned size=(unsigned)n;
    if(!n||n>sizeof old){io->ok=0;return;}
    if(io->mode){s3_state_peek(io,0,&size,sizeof size);if(size!=n){io->ok=0;return;}s3_state_peek(io,sizeof size,next,n);
        if(io->ok&&save==s3_vgm_fm_rb_save&&memcmp(old,next,4))io->ok=0;
        if(io->ok){if(!load(next,n))io->ok=0;if(io->mode==1||!io->ok)load(old,n);}}
    S3_STATE(io,size);s3_state_bytes(io,old,n);
}
void s3_vgm_state(S3StateIO *io)
{
    if(!present)return;
    struct VGMClock next=vgm;s3_state_peek(io,0,&next,sizeof next);
    if(next.theme>=S3_MUSIC_COUNT||next.ended>1||next.mask>0x7ff||next.filter_mix>1118||next.filter_start>1118||next.filter_target>1118||next.master<next.base||
       (s3_vgm_ready(next.theme)&&(next.index>documents[next.theme].count||(!next.ended&&
       next.master>(next.index<documents[next.theme].count?s3_vgm_u64(documents[next.theme].bytes+32+next.index*16):documents[next.theme].total)+next.base))))io->ok=0;
    S3_STATE(io,vgm);chip_state(io,s3_vgm_fm_rb_save,s3_vgm_fm_rb_load);chip_state(io,s3_vgm_psg_rb_save,s3_vgm_psg_rb_load);
}
