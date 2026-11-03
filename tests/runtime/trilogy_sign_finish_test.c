/* Giant-ring tally must finish the sign's animation when it skips Sign_Spin.
 * Exercise the real transition; runtime captures verify the final script. */
#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
uint8_t g_ram[65536];M68KState g_cpu;
static const uint32_t sign=RAM_Object_RAM+10*OBJ_SIZE,result=RAM_Object_RAM+30*OBJ_SIZE;
static int allocations;
static uint32_t fixture_alloc(void) { ++allocations;return result; }
/* Keep unrelated flow entry points internal so linkers discard them. */
static void unused_level_entry(void);
static uint16_t unused_next(uint16_t za),unused_apparent(uint16_t za);
static void unused_complete(void),unused_registration(TriObjFn *t);
static int unused_fall(void),unused_hook(uint32_t pc);
#define tri_flow_level_entry unused_level_entry
#define tri_next_act unused_next
#define tri_apparent unused_apparent
#define tri_s1_complete unused_complete
#define tri_sbz2_fall unused_fall
#define tri_flow_hook unused_hook
#define s1_obj_flow_register unused_registration
#define o_alloc fixture_alloc
#include "../../game/trilogy/trilogy_flow.c"
#undef o_alloc
static void setup(int anim)
{
    memset(g_ram,0,sizeof g_ram);memset(&g_cpu,0,sizeof g_cpu);
    allocations=s_results_started=s_results_finished=0;
    w32(sign,NATIVE_S1_BASE+0x0D*4);w8(sign+OBJ_routine,4);
    w8(sign+OBJ_anim,(uint8_t)anim);w8(sign+0x21,(uint8_t)anim);
    w8(sign+OBJ_frame,2);w8(sign+OBJ_anim_frame,2);w8(sign+OBJ_anim_timer,1);
    w8(RAM_Update_HUD_timer,1);w16(RAM_Camera_max_X_pos,0x24BF);
}
int main(void)
{
    for(int anim=1;anim<=2;++anim) {
        setup(anim);uint8_t players[OBJ_SIZE*2];
        memcpy(players,g_ram+(RAM_Player_1&65535),sizeof players);start_act_clear();
        CHECK(r8(sign+OBJ_routine)==8 && r8(sign+OBJ_anim)==3);
        CHECK(s_results_started && allocations==1 && r32(result)==S3K_Obj_LevelResults);
        CHECK(!r8(RAM_Update_HUD_timer) && r16(RAM_Camera_min_X_pos)==0x24BF);
        CHECK(!memcmp(players,g_ram+(RAM_Player_1&65535),sizeof players));
        start_act_clear();CHECK(allocations==1 && r8(sign+OBJ_anim)==3);
    }
    setup(3);w8(sign+OBJ_routine,8);start_act_clear();CHECK(!allocations);
    puts("Ring tally: skipped spin selects final native animation, preserves players and keeps one results handoff PASS");return 0;
}
