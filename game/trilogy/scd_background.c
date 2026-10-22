/* Sonic CD background math, isolated from game/CPU ABI and rendering.
 * The generated profiles derive from the CC0 Windows/Gems decompilation,
 * with separately recorded original Sega CD corrections/qualification.
 * No MMD/ROM bytes or interpreter are included in this runtime component. */
#include "scd_background.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* The source names low/high halves explicitly. Index by the host's byte
 * order, including targets without predefined endian macros. */
static int bg_hi(void)
{
    const uint16_t one=1;
    return *(const uint8_t *)&one==1;
}
static int bg_byte(int from_low) { return bg_hi()?from_low:3-from_low; }
static int32_t bg_shl(int32_t value,unsigned shift)
{
    return shift<32?(int32_t)((uint32_t)value<<shift):0;
}
/* One Windows-source initializer uses a variable signed shift count. Match
 * its qualified x86 count masking without an undefined C shift or relying
 * on the host's right-shift treatment of negative signed values. */
static int32_t bg_asr(int32_t value,uint32_t shift)
{
    shift&=31;
    if(!shift)return value;
    uint32_t bits=(uint32_t)value>>shift;
    if(value<0)bits|=UINT32_MAX<<(32-shift);
    return (int32_t)bits;
}
/* DIVU.W followed by EXT.L. A 68000 quotient overflow leaves its dividend
 * unchanged; then EXT.L sign-extends the low word. MMZ3 Good uses that path
 * for its negative initial foreground/background difference. */
static int32_t bg_divu_ext(uint32_t value,uint16_t divisor)
{
    if(!divisor)return 0;
    uint32_t quotient=value/divisor;
    return (int16_t)(quotient>65535?value:quotient);
}
/* Work-array counters have logical low/high SHORT order from the Windows
 * source, irrespective of the host byte order. Access their actual short
 * objects; no long-pointer overlay or strict-aliasing exception is needed. */
static int32_t bg_work_get(const int16_t *p)
{ return (int32_t)((uint16_t)p[0] | (uint32_t)(uint16_t)p[1]<<16); }
static void bg_work_set(int16_t *p,int32_t value)
{ p[0]=(int16_t)value;p[1]=(int16_t)((uint32_t)value>>16); }
static void bg_work_add(int16_t *p,int32_t value)
{ bg_work_set(p,(int32_t)((uint32_t)bg_work_get(p)+(uint32_t)value)); }
typedef union { int32_t l;int16_t words[2];int8_t bytes[4]; } int_union;
typedef union { uint32_t l;uint16_t words[2];uint8_t bytes[4]; } uint_union;
typedef union { int16_t w;int8_t bytes[2]; } short_union;
typedef union { uint16_t w;uint8_t bytes[2]; } ushort_union;
typedef struct { int_union xposi,yposi;short_union xspeed;uint8_t actfree[3]; } BgPlayer;

struct ScdBg {
    int profile;
    BgPlayer actwk[1];
    uint8_t enkeino;
    int16_t hscrollwork[256];
    int32_t linework[128];
    int_union *lphscrollbuff,output[1024];
    int_union scra_h_posit,scra_v_posit,scrb_h_posit,scrb_v_posit;
    int_union scrc_h_posit,scrc_v_posit,scrz_h_posit,scrz_v_posit,vscroll;
    int32_t scra_hz;
    int16_t scra_vz,waterposi;
    uint8_t scrb_h_count,scrb_v_count,scrc_h_count,scrz_h_count;
    ushort_union scrflaga,scrflagb,scrflagc,scrflagz;
    short_union scroll_start,waterdirec;
};
typedef struct BgProfile {
    void (*init)(ScdBg *,int16_t,int16_t);
    void (*step)(ScdBg *);
} BgProfile;

#include "scd_background_profiles.inc"

static void input(ScdBg *s,const ScdBgInput *in)
{
    s->scra_h_posit.words[bg_hi()]=in->camera_x;
    s->scra_v_posit.words[bg_hi()]=in->camera_y;
    s->scra_hz=in->delta_x;s->scra_vz=in->delta_y;
    s->actwk[0].xposi.words[bg_hi()]=in->player_x;
    s->actwk[0].yposi.words[bg_hi()]=in->player_y;
    s->actwk[0].xspeed.w=in->player_speed;
    s->actwk[0].actfree[2]=in->player_control;
    s->enkeino=in->scene;s->waterposi=in->water_y;
    s->scroll_start.bytes[bg_hi()]=(int8_t)in->locked;
    s->lphscrollbuff=s->output;
}
ScdBg *scd_bg_create(void)
{
    ScdBg *s=calloc(1,sizeof *s);if(s)s->profile=-1;return s;
}
void scd_bg_destroy(ScdBg *s) { free(s); }
int scd_bg_reset(ScdBg *s,uint16_t stage,const ScdBgInput *in)
{
    if(!s || !in)return 0;
    memset(s,0,sizeof *s);s->profile=-1;
    unsigned zone=stage>>8,act=stage&255;
    if(zone<0x40 || zone>0x5b || act>2 || (act==2 && (zone&3)<2))return 0;
    s->profile=(int)(((zone-0x40)>>2)*10+(act<2?act*4:8)+(act<2?(zone&3):(zone&3)-2));
    input(s,in);profiles[s->profile].init(s,in->camera_x,in->camera_y);return 1;
}
int scd_bg_step_wide(ScdBg *s,const ScdBgInput *in,ScdBgOutput *out,int32_t delta_x)
{
    if(!s || !in || !out || s->profile<0 || s->profile>=70)return 0;
    input(s,in);s->scra_hz=delta_x;profiles[s->profile].step(s);
    out->y=s->scrb_v_posit.words[bg_hi()];
    for(unsigned i=0;i<224;i++)out->hscroll[i]=s->output[i].words[1-bg_hi()];
    return 1;
}
int scd_bg_step(ScdBg *s,const ScdBgInput *in,ScdBgOutput *out)
{ return scd_bg_step_wide(s,in,out,in?in->delta_x:0); }
