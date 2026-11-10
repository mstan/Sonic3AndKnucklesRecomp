#include "sonic3_sound_driver.h"
#include "sonic3_state_io.h"
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
static uint32_t tail,last_queue,last_id;
int gbus_map_expansion_ram(uint32_t base,uint8_t *data,uint32_t size)
{CHECK(base==0x900000 && size==0x800);mapped=data;return 1;}
uint32_t m68k_read32(uint32_t a)
{a&=0xFFFFFF;CHECK(a>=0x400000 && a+4<=0x400000+g_rom_ext_size);const uint8_t *p=g_rom_ext+a-0x400000;return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
void m68k_write8(uint32_t a,uint8_t v){CHECK(a==0xC00011 && (v&0x9F)==0x9F);}
void recomp_call_addr(uint32_t pc)
{
    if(pc==S3_CLONE_QUEUE_MUSIC || pc==S3_CLONE_QUEUE_SFX || pc==S3_CLONE_QUEUE_EXTRA){last_queue=pc;last_id=g_cpu.D[0];}
    memset(&g_cpu,0xAD,sizeof g_cpu); /* driver calls may clobber any register */
}
void recomp_tail_call(uint32_t pc){tail=pc;}
int genesis_atomic_file_store(const char *p,const void *d,size_t n){(void)p;(void)d;(void)n;return 1;}
void cmd_send_response(const char *s){(void)s;}
int main(int argc,char **argv)
{
    CHECK(argc==3);FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(g_rom,1,sizeof g_rom,f)>=0x200000);fclose(f);
    s3_sound_load("unused-test-settings.ini",argv[2]);CHECK(!s3_sound_selected());CHECK(s3_sound_netplay_allowed());
    size_t n=s3_sound_rb_save(NULL,0);uint8_t *disabled=malloc(n),*saved=malloc(n),*after=malloc(n);CHECK(disabled&&saved&&after);
    CHECK(s3_sound_rb_save(disabled,n)==n);CHECK(s3_sound_select(1));s3_sound_post_reset();CHECK(mapped && s3_sound_active() && !s3_sound_netplay_allowed());
    CHECK(!s3_sound_rb_load(disabled,n));
    int s3=!strcmp(argv[2],"sonic3");unsigned init=s3?0x1562:0x12CE,music=s3?0x15E2:0x1358,sfx=s3?0x160A:0x1380;
    memset(&g_cpu,0,sizeof g_cpu);g_cpu.A[7]=0xFFFE00;g_cpu.SR=0x271F;M68KState expected=g_cpu;expected.SR=0x2714;
    CHECK(s3_sound_hook(init));CHECK(!memcmp(&g_cpu,&expected,sizeof g_cpu));
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
    M68KState before=g_cpu;s3_sound_vblank();CHECK(!memcmp(&g_cpu,&before,sizeof g_cpu));CHECK(last_queue==S3_CLONE_QUEUE_MUSIC && last_id==song);
    g_cpu.D[0]=0x33;CHECK(s3_sound_hook(sfx));before=g_cpu;s3_sound_vblank();CHECK(!memcmp(&g_cpu,&before,sizeof g_cpu));CHECK(last_queue==S3_CLONE_QUEUE_SFX && last_id==0x40);
    unsigned pause=s3_clone_pause_sites[0];uint8_t value=g_rom[pause+3];CHECK(s3_sound_hook(pause));CHECK(mapped[S3_CLONE_PAUSE_OFFSET]==value && tail==pause+8);
#ifdef S3_SOUND_COMBINED
    s3_stage_pack_enabled=1;CHECK(!s3_sound_select(1));CHECK(!s3_sound_commit());
#endif
    free(disabled);free(saved);free(after);free(g_rom_ext);
    puts("PASS driver RAM/pending queues round-trip; identity/bounds rejection; native queue mapping, registers/CCR, pause and unsupported-mode gates");return 0;
}
