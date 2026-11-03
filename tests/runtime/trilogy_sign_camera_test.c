/* Original S1 pre-sign camera boundary, independent of art residency. */
#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
uint8_t g_ram[65536];
M68KState g_cpu;
static int has_entry,game;
uint32_t tri_dir(void) { return has_entry?0x400040u:0; }
uint8_t tri_rd8(uint32_t a) { CHECK(a==0x400040u+TD_GAME);return (uint8_t)game; }
#include "../../game/trilogy/trilogy_sign_camera.inc"
static void setup(uint16_t camera)
{
    memset(g_ram,0xA5,sizeof g_ram);memset(&g_cpu,0x6C,sizeof g_cpu);
    has_entry=1;game=TRI_GAME_S1;
    w8(RAM_Current_act,0);w16(RAM_Debug_placement_mode,0);
    w8(RAM_Update_HUD_timer,1);w16(RAM_Camera_X_pos,camera);
    w16(RAM_Camera_max_X_pos,0x24BF);w16(RAM_Camera_min_X_pos,0x1800);
}
static void expect(uint16_t minimum)
{
    uint8_t expected[65536];M68KState cpu=g_cpu;
    memcpy(expected,g_ram,sizeof expected);
    expected[RAM_Camera_min_X_pos&65535]=(uint8_t)(minimum>>8);
    expected[(RAM_Camera_min_X_pos+1)&65535]=(uint8_t)minimum;
    s1_signpost_camera_lock();
    CHECK(!memcmp(expected,g_ram,sizeof expected));
    CHECK(!memcmp(&cpu,&g_cpu,sizeof cpu));
}
int main(void)
{
    setup(0x23BE);expect(0x1800);             /* strictly before threshold */
    setup(0x23BF);expect(0x23BF);             /* exact authored threshold */
    setup(0x2480);expect(0x23BF);expect(0x23BF);
    setup(0x2480);w8(RAM_Current_act,1);expect(0x23BF);
    setup(0x2480);w8(RAM_Current_act,2);expect(0x1800); /* original act3 skip */
    setup(0x2480);w16(RAM_Debug_placement_mode,1);expect(0x1800);
    setup(0x2480);w8(RAM_Update_HUD_timer,0);expect(0x1800);
    setup(0x2480);w8(RAM_Update_HUD_timer,0);w16(RAM_Camera_min_X_pos,0x24BF);expect(0x24BF);
    setup(0x2480);has_entry=0;expect(0x1800);
    setup(0x2480);game=TRI_GAME_S3K;expect(0x1800);
    for (int other=TRI_GAME_S2;other<=TRI_GAME_CD;++other) {
        setup(0x2480);game=other;expect(0x1800);
    }
    puts("S1 sign camera: threshold, act/debug/time/content guards and RAM/register preservation PASS");
    return 0;
}
