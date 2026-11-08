/* Native loop addresses are independently taken from pinned retail listings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "genesis_runtime.h"
#include "sonic3_state_io.h"
#include "sonic3_state.h"
M68KState g_cpu;
uint8_t g_ram[65536];
static int actor_ready=1;
static unsigned called_pc,called_sp;
void recomp_call_addr(uint32_t pc) { called_pc=pc;called_sp=g_cpu.A[7]; }
static unsigned marker=17;
void s3_video_state(S3StateIO *io) { S3_STATE(io,marker); }
void s3_video_state_loaded(void) {}
void s3_army_state(S3StateIO *io) { (void)io; }
int s3_army_state_ready(void) { return actor_ready; }
size_t audio_event_state_size(void) { return 0; }
int audio_event_state_save(void *data,size_t size) { (void)data;return size==0; }
int audio_event_state_load(const void *data,size_t size,int apply)
{ (void)data;(void)apply;return size==0; }
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
static void caller(unsigned pc)
{
    g_cpu.A[7]=0xFFFFF8;
    for(unsigned i=0;i<4;i++)g_ram[0xFFF8+i]=(uint8_t)(pc>>(24-i*8));
}
int main(void)
{
#ifdef SONIC3_STANDALONE
    const unsigned level=0x4B20,special=0x77E6,pause=0x16C0,loop=0x77D2;
#else
    const unsigned level=0x6520,special=0x84D6,pause=0x143A,loop=0x84C2;
#endif
    /* Exact retail call lengths matter: menus use both JSR.l and BSR.w.
     * A restored menu must recreate its wait slot without resetting the
     * surrounding guest stack or replaying the screen loader. */
#ifdef SONIC3_STANDALONE
    const unsigned menus[][3]={{0x4C,0xBA8E,6},{0x38,0x96BE,6},
        {0x3C,0xA344,6},{0x40,0x9C62,6},{0xC0,0x9C62,6}};
#else
    const unsigned menus[][3]={{4,0x41DA,4},{4,0x52B2,4},{4,0x5388,4},
        {0x4C,0xC808,6},{0x38,0xA9EE,6},{0x3C,0xB4AE,6},
        {0x40,0xADF8,6},{0xC0,0xADF8,6}};
#endif
    for (unsigned i=0;i<sizeof menus/sizeof menus[0];++i) {
        unsigned mode=menus[i][0],wait=menus[i][1],length=menus[i][2];
        g_ram[0xF600]=(uint8_t)mode;g_cpu.PC=0;
        CHECK(s3_state_menu_instruction(wait));CHECK(g_cpu.PC==wait);
        g_cpu.A[7]=0xFFFDF8;
        for(unsigned b=0;b<4;++b)g_ram[0xFDF8+b]=(uint8_t)((wait+length)>>(24-b*8));
        CHECK(s3_state_at_boundary());
        CHECK(s3_state_resume_pc((uint8_t)mode)==wait);
        g_cpu.A[7]=0xFFFE00; /* Engine fiber reset uses initial SSP. */
        s3_state_call_resume(wait);
        CHECK(called_pc==wait && called_sp==0xFFFDFC);
        g_cpu.A[7]=0xFFFE00;s3_state_call_resume(wait);
        CHECK(called_sp==0xFFFE00); /* No stale continuation on another load. */
        g_cpu.A[7]=0xFFFDF9;CHECK(!s3_state_at_boundary());
        g_cpu.A[7]=0xFFFDF8;g_ram[0xFDFB]^=2;CHECK(!s3_state_at_boundary());
        CHECK(!s3_state_resume_pc((uint8_t)mode));g_ram[0xFDFB]^=2;
        CHECK(s3_state_menu_instruction(wait+length));CHECK(g_cpu.PC==0);
        CHECK(!s3_state_at_boundary());
    }
    /* Grand Prix and Match Race are separate handlers despite adjacent
     * game modes. A wrong menu PC must not become an accepted boundary. */
    g_ram[0xF600]=0x3C;
#ifdef SONIC3_STANDALONE
    s3_state_menu_instruction(0x9C62);
#else
    s3_state_menu_instruction(0xADF8);
#endif
    CHECK(!s3_state_at_boundary());
    CHECK(!s3_state_menu_instruction(0x123456));
    g_ram[0xF600]=0x34; /* SS does not have a Level_started_flag. */
    caller(special);CHECK(s3_state_at_boundary());
    caller(pause);CHECK(s3_state_at_boundary());
    caller(level);CHECK(!s3_state_at_boundary());
    caller(special-4);CHECK(!s3_state_at_boundary()); /* Initialization/fade wait. */
    CHECK(s3_state_resume_pc(0x34)==loop);
    CHECK(!s3_state_resume_pc(0xB4) && !s3_state_resume_pc(0x48));
    caller(special);g_cpu.A[7]=0xFFFFFF;CHECK(!s3_state_at_boundary());
    g_ram[0xF600]=0x0C;caller(level);CHECK(!s3_state_at_boundary());
    g_ram[0xF711]=1;CHECK(s3_state_at_boundary());
    caller(special);CHECK(!s3_state_at_boundary());
    g_ram[0xF600]=0x34;caller(special);
#ifndef SONIC3_STANDALONE
    actor_ready=0;CHECK(!s3_state_at_boundary());actor_ready=1;
#endif
    size_t size=s3_state_size();void *data=malloc(size);CHECK(data);
    CHECK(s3_state_save(data,size));marker=99;
    CHECK(s3_state_load(data,size,0) && marker==99);
    CHECK(s3_state_load(data,size,1) && marker==17);
    CHECK(!s3_state_load(data,size-1,0));
    g_ram[0xF600]=0x48;CHECK(!s3_state_save(data,size));
    free(data);puts("PASS regular and special-stage native state boundaries");return 0;
}
