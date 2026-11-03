#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
uint8_t g_ram[65536];M68KState g_cpu;
static int s_ss_pending,s_runoff,has_entry,game;
uint32_t tri_dir(void) { return has_entry?0x400040u:0; }
uint8_t tri_rd8(uint32_t a) { CHECK(a==0x400040u+TD_GAME);return (uint8_t)game; }
#include "../../game/trilogy/trilogy_tail_respawn.inc"
static void setup(void)
{
    memset(g_ram,0xA5,sizeof g_ram);memset(&g_cpu,0x6C,sizeof g_cpu);
    s_ss_pending=1;s_runoff=15;has_entry=1;game=TRI_GAME_S1;
    w8(RAM_Game_mode,0x0C);w16(RAM_Player_mode,0);w8(RAM_Ctrl_2_locked,0xFF);
    w32(RAM_Player_2,0x1365Cu);w8(RAM_Player_2+0x38,1); /* native Tails */
    w8(RAM_Player_2+OBJ_routine,2);w8(RAM_Player_2+0x2E,0x81);
    w16(RAM_Tails_CPU_routine,4);
}
static void expect(int release)
{
    uint8_t expected[65536];M68KState cpu=g_cpu;int old=s_runoff;
    memcpy(expected,g_ram,sizeof expected);
    if(release)expected[RAM_Ctrl_2_locked&65535]=1;
    tail_ring_respawn_tick();
    CHECK(!memcmp(expected,g_ram,sizeof expected));CHECK(!memcmp(&cpu,&g_cpu,sizeof cpu));
    CHECK(s_runoff==(release?(old&~10):old));
}
int main(void)
{
    setup();expect(1);expect(0); /* positive lock keeps controller blocked */
    setup();s_ss_pending=0;expect(0);
    setup();has_entry=0;expect(0);
    for(int n=0;n<=TRI_GAME_CD;++n)if(n!=TRI_GAME_S1){setup();game=n;expect(0);}
    setup();w8(RAM_Game_mode,0x34);expect(0);
    setup();w16(RAM_Player_mode,1);expect(0);
    setup();w8(RAM_Ctrl_2_locked,0);expect(0);
    setup();w32(RAM_Player_2,0);expect(0);
    setup();w8(RAM_Player_2+0x38,0);expect(0);
    setup();w8(RAM_Player_2+OBJ_routine,6);expect(0);
    setup();w8(RAM_Player_2+0x2E,0x53);expect(0); /* genuinely entered ring */
    setup();w16(RAM_Tails_CPU_routine,6);expect(0); /* ordinary ending pose */
    puts("Late ring Tails: native CPU-only catch-up release and RAM/register guards PASS");return 0;
}
