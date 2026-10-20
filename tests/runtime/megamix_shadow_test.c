/* Actual controller entry points, synthetic RAM only. Collisions/outcomes
 * remain runtime QA; these checks cover costs, targeting and reset contracts. */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "megamix_geometry_sites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
uint8_t g_ram[65536],g_rom[0x400000];M68KState g_cpu;
int g_split_sp_popped;static int pending;int *g_rte_pending_ptr=&pending;
static int enabled=1,room=1,sound;
int tri_shadow_active(void){return enabled;}
int tri_character_transform(void){return 0;}
uint32_t o_slot(int i){return RAM_Player_1+(uint32_t)i*OBJ_SIZE;}
void o_sfx(int id){sound=id;}
int16_t o_wall_right(uint32_t p,int d){CHECK(p==RAM_Player_1 && d==16);A(4)=123;A(5)=234;A(6)=345;return (int16_t)room;}
int16_t o_wall_left(uint32_t p,int d){CHECK(p==RAM_Player_1 && d==-16);A(4)=123;A(5)=234;A(6)=345;return (int16_t)room;}
uint8_t o_atan(int16_t x,int16_t y){CHECK(!y);return x<0?128:0;}
void o_sincos(uint8_t a,int16_t *s,int16_t *c){*s=0;*c=a==128?-256:256;}
#include "../../game/trilogy/trilogy_shadow.c"
static void init(void)
{
    memset(g_ram,0,sizeof g_ram);memset(&g_cpu,0,sizeof g_cpu);enabled=1;room=1;
    A(0)=RAM_Player_1;w8(RAM_Player_1+OBJ_routine,2);
    CHECK(!tri_shadow_hook(TH_LEVEL_ENTRY));
}
static uint32_t enemy(int i,unsigned x,unsigned collision)
{
    uint32_t o=o_slot(i);w32(o,0x1234);w8(o+OBJ_render,0x80);w8(o+OBJ_coll_flags,(uint8_t)collision);
    w16(o+OBJ_x_pos,(uint16_t)x);return o;
}
int main(void)
{
    init();w8(RAM_Ctrl_1_pressed_logical,0x40);w16(RAM_Ring_count,4);
    CHECK(tri_shadow_hook(MG_PC_SHADOW_GROUND) && r16(RAM_Ring_count)==4 && !r16(RAM_Player_1+OBJ_ground_vel));
    w16(RAM_Ring_count,5);room=-1;CHECK(tri_shadow_hook(MG_PC_SHADOW_GROUND) && r16(RAM_Ring_count)==5);
    room=1;A(4)=0xFFF760;A(5)=0xFFF800;A(6)=0xFFF900;
    CHECK(tri_shadow_hook(MG_PC_SHADOW_GROUND) && !r16(RAM_Ring_count));
    CHECK(A(4)==0xFFF760 && A(5)==0xFFF800 && A(6)==0xFFF900);
    CHECK(r16(RAM_Player_1+OBJ_ground_vel)==0xC00 && sound==0xB6);
    w8(RAM_Player_1+OBJ_status,1);w8(RAM_Player_1+0x36,1);w16(RAM_Ring_count,5);
    CHECK(tri_shadow_hook(MG_PC_SHADOW_GROUND) && (int16_t)r16(RAM_Player_1+OBJ_ground_vel)==-0x1000);
    init();w8(RAM_Ctrl_1_pressed_logical,0x40);w8(RAM_Ctrl_1_held_logical,0x48);
    w8(RAM_Super_Sonic_Knux_flag,1);w16(0xFFF670,60);w8(RAM_Player_1+OBJ_status,4);
    w16(RAM_Player_1+OBJ_y_pos,100);
    CHECK(tri_shadow_hook(MG_PC_SHADOW_GROUND) && super_boost && !r16(RAM_Ring_count));
    CHECK(r16(RAM_Max_speed)==0x1100 && r16(0xFFF670)==10 && r16(RAM_Player_1+OBJ_y_pos)==95);
    CHECK(!(r8(RAM_Player_1+OBJ_status)&4) && r8(RAM_Player_1+OBJ_y_radius)==19);
    w8(RAM_Ctrl_1_held_logical,0);CHECK(!tri_shadow_hook(TH_LVL_PRE_SPRITES));
    CHECK(!super_boost && r16(RAM_Max_speed)==0xA00);
    init();w8(RAM_Player_1+OBJ_status,2);w8(RAM_Ctrl_1_pressed_logical,0x10);
    uint32_t back=enemy(2,(uint16_t)-32,5),near=enemy(3,48,5),far=enemy(4,80,6);
    CHECK(tri_shadow_hook(MG_PC_MIGHTY_SHIELD));CHECK(target==near && remaining==32);
    CHECK(!r16(RAM_Ring_count) && r16(RAM_Player_1+OBJ_x_vel)==0xC00);
    w8(near+OBJ_coll_flags,0);w8(RAM_Ctrl_1_held_logical,0x40);w16(RAM_Ring_count,6);
    CHECK(!tri_shadow_hook(MG_PC_SHADOW_AIR));CHECK(target==back && r16(RAM_Ring_count)==1);
    CHECK((int16_t)r16(RAM_Player_1+OBJ_x_vel)==-0xC00);
    w8(RAM_Ctrl_1_held_logical,0);remaining=0;
    CHECK(!tri_shadow_hook(MG_PC_SHADOW_AIR) && !target);
    previous=back;w16(RAM_Ring_count,5);CHECK(!acquire(1) && r16(RAM_Ring_count)==5);
    w16(RAM_Ring_count,6);CHECK(acquire(1) && target==far && r16(RAM_Ring_count)==1);
    w8(RAM_Player_1+OBJ_routine,4);CHECK(!tri_shadow_hook(MG_PC_SHADOW_AIR) && !target && !previous);
    init();enemy(2,129,5);enemy(3,48,0x85);w8(RAM_Player_1+OBJ_status,2);w8(RAM_Ctrl_1_pressed_logical,0x10);
    CHECK(tri_shadow_hook(MG_PC_MIGHTY_SHIELD) && !target);
    /* The actual PPZ Tamabboh uses converted normal collision shape $1A,
     * outside v3's original retail shape whitelist. Hurt/special categories
     * and screen-relative firefly animation must remain ineligible. */
    init();w8(RAM_Player_1+OBJ_status,2);w8(RAM_Ctrl_1_pressed_logical,0x10);
    uint32_t cd=enemy(2,48,0x1A);w32(cd,0x7E2174);w8(cd+OBJ_render,0x84);
    CHECK(tri_shadow_hook(MG_PC_MIGHTY_SHIELD) && target==cd);
    w8(cd+OBJ_coll_flags,0x9A);CHECK(!targetable(cd));
    w8(cd+OBJ_coll_flags,0xDA);CHECK(!targetable(cd));
    w8(cd+OBJ_coll_flags,0x1A);w8(cd+OBJ_render,0x80);CHECK(!targetable(cd));
    w8(cd+OBJ_render,0x84);w8(cd+OBJ_coll_flags,0x5A);CHECK(!targetable(cd));
    w8(cd+OBJ_coll_prop,8);CHECK(targetable(cd));
    enabled=0;CHECK(!tri_shadow_hook(MG_PC_MIGHTY_SHIELD));
    puts("Shadow ground boost cost/wall/facing/shoes and homing range/selection/chain/hurt contracts passed");return 0;
}
