#include "sonic3_sound_driver.h"
#include "sonic3_sound_themes.h"
#include "sonic3_state_io.h"
#include "sonic3_cd_themes.h"
#include "genesis_runtime.h"
#include "sonic3_clone_program.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
M68KState g_cpu;
uint8_t g_rom[0x400000],g_ram[0x10000],*g_rom_ext;
uint32_t g_rom_ext_size;
int s3_stage_pack_enabled;
static uint8_t *mapped;
static uint32_t tail,last_queue,last_id,stop_calls;
static void (*extra_audio)(int16_t *,size_t,double);
static int same_cpu(const M68KState *a,const M68KState *b)
{
    /* Struct assignment need not preserve the padding after SR. Check the
     * guest registers rather than unspecified host padding bytes. */
    return !memcmp(a->D,b->D,sizeof a->D) && !memcmp(a->A,b->A,sizeof a->A) &&
           a->SR==b->SR && a->PC==b->PC && a->USP==b->USP;
}
int gbus_map_expansion_ram(uint32_t base,uint8_t *data,uint32_t size)
{CHECK(base==0x900000 && size==0x800);mapped=data;return 1;}
uint32_t m68k_read32(uint32_t a)
{a&=0xFFFFFF;CHECK(a>=0x400000 && a+4<=0x400000+g_rom_ext_size);const uint8_t *p=g_rom_ext+a-0x400000;return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
void m68k_write8(uint32_t a,uint8_t v){CHECK(a==0xC00011 && (v&0x9F)==0x9F);}
void recomp_call_addr(uint32_t pc)
{
    if(pc==S3_CLONE_STOP_ALL){CHECK(g_cpu.A[6]==S3_CLONE_RAM_ADDRESS);++stop_calls;}
    if(pc==S3_CLONE_QUEUE_MUSIC || pc==S3_CLONE_QUEUE_SFX || pc==S3_CLONE_QUEUE_EXTRA){last_queue=pc;last_id=g_cpu.D[0];}
    memset(&g_cpu,0xAD,sizeof g_cpu); /* driver calls may clobber any register */
}
void recomp_tail_call(uint32_t pc){tail=pc;}
int genesis_atomic_file_store(const char *p,const void *d,size_t n){(void)p;(void)d;(void)n;return 1;}
void cmd_send_response(const char *s){(void)s;}
void audio_set_extra_source(void (*source)(int16_t *,size_t,double)){extra_audio=source;}
int main(int argc,char **argv)
{
    CHECK(argc==3 || argc==4);FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(g_rom,1,sizeof g_rom,f)>=0x200000);fclose(f);
    s3_sound_load("unused-test-settings.ini",argv[2]);CHECK(!s3_sound_selected());CHECK(s3_sound_netplay_allowed());
    size_t n=s3_sound_rb_save(NULL,0);uint8_t *disabled=malloc(n),*saved=malloc(n),*after=malloc(n);CHECK(disabled&&saved&&after);
    CHECK(s3_sound_rb_save(disabled,n)==n);CHECK(s3_sound_select(1));s3_sound_post_reset();CHECK(mapped && s3_sound_active() && !s3_sound_netplay_allowed());
    uint32_t base_boss=m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x19-1));
    uint32_t base_credits=m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x33-1));
    uint32_t base_ending=m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x32-1));
    CHECK(!s3_sound_rb_load(disabled,n));
    int s3=!strcmp(argv[2],"sonic3");unsigned init=s3?0x1562:0x12CE,music=s3?0x15E2:0x1358,sfx=s3?0x160A:0x1380;
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.A[7]=0xFFFE00;g_cpu.SR=0x271F;M68KState expected=g_cpu;expected.SR=0x2714;
    CHECK(s3_sound_hook(init));CHECK(same_cpu(&g_cpu,&expected));
    for(unsigned i=0;i<0x800;i++)mapped[i]=(uint8_t)(i*37);
    unsigned song=!strcmp(argv[2],"sandk")?0xF:1;
    g_cpu.D[0]=song;CHECK(s3_sound_hook(music));CHECK(s3_sound_rb_save(saved,n)==n);
    memset(mapped,0xFA,0x800);g_cpu.D[0]=0x62;CHECK(s3_sound_hook(sfx));
    CHECK(s3_sound_rb_load(saved,n));CHECK(s3_sound_rb_save(after,n)==n && !memcmp(saved,after,n));
    saved[8]^=1;CHECK(!s3_sound_rb_load(saved,n));CHECK(s3_sound_rb_save(after,n)==n);saved[8]^=1;CHECK(!memcmp(saved,after,n));
    CHECK(!s3_sound_rb_load(saved,n-1));CHECK(!s3_sound_rb_save(after,n-1));
    /* Pending launcher selection is for a restart; current snapshots retain
     * the running driver identity. */
    CHECK(s3_sound_select(0));CHECK(s3_sound_rb_load(saved,n));CHECK(!s3_sound_netplay_allowed());
    M68KState before=g_cpu;s3_sound_vblank();CHECK(same_cpu(&g_cpu,&before));CHECK(last_queue==S3_CLONE_QUEUE_MUSIC && last_id==song);
    g_cpu.D[0]=0x33;CHECK(s3_sound_hook(sfx));before=g_cpu;s3_sound_vblank();CHECK(same_cpu(&g_cpu,&before));CHECK(last_queue==S3_CLONE_QUEUE_SFX && last_id==0x40);
    /* The logo cancels a demo fade using the internal driver's A6 contract,
     * while its external bridge preserves every guest CPU register. */
    CHECK(!stop_calls);g_cpu.A[6]=0xFFB000;g_cpu.D[0]=0xFF;
    CHECK(s3_sound_hook(music));before=g_cpu;s3_sound_vblank();
    CHECK(stop_calls==1 && last_id==0xFC && same_cpu(&g_cpu,&before));
    unsigned pause=s3_clone_pause_sites[0];uint8_t value=g_rom[pause+3];CHECK(s3_sound_hook(pause));CHECK(mapped[S3_CLONE_PAUSE_OFFSET]==value && tail==pause+8);
#ifdef S3_SOUND_COMBINED
    s3_stage_pack_enabled=1;CHECK(!s3_sound_select(1));CHECK(strstr(s3_sound_error(),"additional characters"));CHECK(!s3_sound_commit());
#endif
    if(argc==4){
        free(g_rom_ext);g_rom_ext=NULL;g_rom_ext_size=0;s3_stage_pack_enabled=0;
        s3_sound_load("unused-test-settings.ini",argv[2]);
        CHECK(!s3_sound_prototype_selected() && !s3_sound_prototype_active());
        CHECK(!s3_sound_prototype_select(1));CHECK(s3_sound_select(1));
        CHECK(!s3_sound_prototype_set_path(argv[1])); /* Retail is not the donor. */
        CHECK(!*s3_sound_prototype_path());CHECK(s3_sound_prototype_set_path(argv[3]));
        CHECK(s3_sound_prototype_verified());CHECK(s3_sound_prototype_select(1));
        CHECK(!s3_sound_select(0));
        CHECK(!s3_sound_prototype_set_path("missing-prototype.bin"));
        CHECK(!strcmp(s3_sound_prototype_path(),argv[3])); /* Keep valid selection. */
        s3_sound_post_reset();CHECK(s3_sound_prototype_active());
        if(!s3){
            uint32_t a=(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x26-1))&0xFFFFFF)-0x400000;
            uint32_t b=(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x33-1))&0xFFFFFF)-0x400000;
            CHECK(a+32<=g_rom_ext_size&&b+32<=g_rom_ext_size&&!memcmp(g_rom_ext+a,g_rom_ext+b,32));
            CHECK(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x32-1))==base_ending);
        }
        CHECK(!s3_sound_rb_load(saved,n)); /* Different soundtrack identity. */
        CHECK(s3_sound_rb_save(after,n)==n);after[6]^=1;CHECK(!s3_sound_rb_load(after,n));
        CHECK(s3_sound_prototype_select(0));CHECK(s3_sound_prototype_active());
        CHECK(s3_sound_rb_save(after,n)==n && s3_sound_rb_load(after,n));
        CHECK(s3_sound_select(0));
    }
#ifdef S3_SOUND_COMBINED
    /* The supported Steam header must provide the same built-in S3 themes
     * without modifying the bytes the guest reads. */
    size_t canonical_size=0;uint8_t *canonical=NULL;CHECK(g_rom[0x2001F0]=='U');
    for(unsigned steam=0;steam<2;steam++){
        free(g_rom_ext);g_rom_ext=NULL;g_rom_ext_size=0;s3_stage_pack_enabled=0;
        s3_sound_load("unused-test-settings.ini",argv[2]);
        CHECK(s3_theme_choose(S3_THEME_LIFE,"s3"));
        CHECK(s3_theme_choose(S3_THEME_CONTINUE,"s3"));
        CHECK(s3_theme_choose(S3_THEME_INVINCIBLE,"s3"));
        CHECK(s3_sound_selected() && s3_themes[S3_THEME_LIFE].enabled &&
            s3_themes[S3_THEME_CONTINUE].enabled && s3_themes[S3_THEME_INVINCIBLE].enabled);
        g_rom[0x2001F0]=steam?'J':'U';s3_sound_post_reset();
        CHECK(s3_sound_active() && g_rom[0x2001F0]==(steam?'J':'U'));
        if(!steam){
            canonical_size=g_rom_ext_size;CHECK(canonical_size);
            canonical=malloc(canonical_size);CHECK(canonical);memcpy(canonical,g_rom_ext,canonical_size);
        }else CHECK(g_rom_ext_size==canonical_size && !memcmp(g_rom_ext,canonical,canonical_size));
    }
    g_rom[0x2001F0]='U';free(canonical);
#endif
    if(!s3||argc==4){
        free(g_rom_ext);g_rom_ext=NULL;g_rom_ext_size=0;s3_stage_pack_enabled=0;
        s3_sound_load("unused-test-settings.ini",argv[2]);
        if(s3)CHECK(s3_sound_prototype_set_path(argv[3]));
        CHECK(s3_music_pick(S3_MUSIC_MINIBOSS,"sk-proto"));s3_sound_post_reset();
        uint32_t mini=m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x18-1));CHECK(mini);
        CHECK(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x2E - 1))==mini);
        CHECK(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x33-1))==base_credits);
        CHECK(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x19-1))==base_boss);
        CHECK(!s3_sound_prototype_active());
        CHECK(s3_sound_hook(init));
        if(!s3){g_cpu.D[0]=0xDC;CHECK(s3_sound_hook(music));s3_sound_vblank();CHECK(last_id==0x33);}
    }
    if(s3&&argc==4){
        free(g_rom_ext);g_rom_ext=NULL;g_rom_ext_size=0;
        s3_sound_load("unused-test-settings.ini",argv[2]);CHECK(s3_sound_prototype_set_path(argv[3]));
        CHECK(s3_music_pick(S3_MUSIC_SUPER,"s3-proto"));s3_sound_post_reset();CHECK(!s3_sound_prototype_active());
        CHECK(s3_sound_hook(init));
        CHECK(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x32-1))==base_ending);
        g_ram[0xFE19]=1;g_cpu.D[0]=0x2C;CHECK(s3_sound_hook(music));s3_sound_vblank();CHECK(last_id==0x33);
        g_ram[0xFE19]=0;g_cpu.D[0]=0x2C;CHECK(s3_sound_hook(music));s3_sound_vblank();CHECK(last_id==0x2C);
    }
    if(argc==4){
        free(g_rom_ext);g_rom_ext=NULL;g_rom_ext_size=0;
        s3_sound_load("unused-test-settings.ini",argv[2]);CHECK(s3_sound_prototype_set_path(argv[3]));
        CHECK(s3_music_pick(S3_MUSIC_CREDITS,"s3-proto"));s3_sound_post_reset();CHECK(!s3_sound_prototype_active());
        CHECK(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x19-1))==base_boss);
        CHECK(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x32-1))==base_ending);
        CHECK(s3_sound_hook(init));g_cpu.D[0]=s3?0x26:0xDC;CHECK(s3_sound_hook(music));s3_sound_vblank();CHECK(last_id==(s3?0x26u:0x33u));
    }
    if(s3_donor_verified(S3_DONOR_CD_US)){
        free(g_rom_ext);g_rom_ext=NULL;g_rom_ext_size=0;
        s3_sound_load("unused-test-settings.ini",argv[2]);
        CHECK(s3_music_pick(S3_THEME_INVINCIBLE,"cd-us"));s3_sound_post_reset();CHECK(extra_audio);
        CHECK(s3_sound_hook(init));g_cpu.D[0]=0x2C;CHECK(s3_sound_hook(music));s3_sound_vblank();
        unsigned active,theme;uint64_t position;int16_t pcm[32]={0};
        s3_cd_theme_debug(&active,&theme,&position);CHECK(active&&theme==S3_THEME_INVINCIBLE);
        /* Ring is native $33, while Clone uses $33 for S&K credits. Normal
         * ring and other SFX must keep the recording and its clock running. */
        static const unsigned effects[]={0x33,0x34,0x62,0xDB};
        for(unsigned i=0;i<sizeof effects/sizeof *effects;i++){
            uint64_t before_position=position;g_cpu.D[0]=effects[i];CHECK(s3_sound_hook(sfx));s3_sound_vblank();
            memset(pcm,0,sizeof pcm);extra_audio(pcm,16,44100);s3_cd_theme_debug(&active,&theme,&position);
            CHECK(active&&theme==S3_THEME_INVINCIBLE&&position>before_position);
        }
        g_cpu.D[0]=s3?0x26:0xDC;CHECK(s3_sound_hook(music));s3_sound_vblank();
        s3_cd_theme_debug(&active,&theme,&position);CHECK(!active);s3_cd_themes_reset();
        puts("PASS CD invincibility survives native ring/other SFX and stops on actual credits");
    }
    free(disabled);free(saved);free(after);free(g_rom_ext);
    puts("PASS driver RAM/pending queues round-trip; identity/bounds rejection; native queue mapping, registers/CCR, pause and unsupported-mode gates");return 0;
}
