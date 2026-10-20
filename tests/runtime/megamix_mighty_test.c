/* Exercise the actual character controller: inventory ordering/cost, Super
 * storage packing, directed move dispatch, animation and DPLC bounds. The
 * optional private bank argument checks every translated animation against
 * the exact converted donor bytes without checking assets into the repo. */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
uint8_t g_ram[65536],g_rom[0x400000];
static uint8_t bank[0x400000];
uint8_t *g_rom_ext=bank;
uint32_t g_rom_ext_size=sizeof bank;
M68KState g_cpu;
int g_split_sp_popped;
static int pending;
int *g_rte_pending_ptr=&pending;
int s3_stage_pack_mighty=1,s3_stage_pack_megamix=1,s3_stage_pack_enabled=1;
int s3_stage_pack_shadow,tri_shadow_selected;
uint32_t tri_shadow_descriptor(void){return 0;}
static unsigned calls[16],call_count,queued;
uint32_t tri_dir(void){return 0;}
uint8_t tri_rd8(uint32_t a)
{
    if(a>=0xFF0000)return g_ram[a&65535];
    CHECK(a>=0x400000 && a-0x400000<g_rom_ext_size);return bank[a-0x400000];
}
uint16_t tri_rd16(uint32_t a){return (uint16_t)((tri_rd8(a)<<8)|tri_rd8(a+1));}
uint32_t tri_rd32(uint32_t a){return ((uint32_t)tri_rd16(a)<<16)|tri_rd16(a+2);}
uint32_t tri_mighty_descriptor(void){uint32_t d=tri_rd32(0x400024);return d?tri_rd32(d+12):0;}
void tri_jsr(uint32_t pc)
{
    if(call_count<16)calls[call_count++]=pc;
    if(pc==0x1192C){w16(RAM_Player_1+OBJ_x_vel,0x800);w16(RAM_Player_1+OBJ_ground_vel,0x800);w16(RAM_Player_1+OBJ_y_vel,0);}
    if(pc==0x1197A)w16(RAM_Player_1+OBJ_y_vel,0xFA80);
    if(pc==0x119A2){w16(RAM_Player_1+OBJ_x_vel,0);w16(RAM_Player_1+OBJ_ground_vel,0);w16(RAM_Player_1+OBJ_y_vel,0x800);}
    w8(RAM_Player_1+0x2F,1);
}
void tri_vram_queue(uint32_t src,uint32_t dest,uint32_t bytes)
{
    CHECK(src>=0x400000 && src-0x400000+bytes<=g_rom_ext_size);
    CHECK(dest>=0xD000 && dest+bytes<=0xD400);queued++;
}
void o_sfx(int sound){(void)sound;}
uint32_t o_slot(int index){return RAM_Player_1+(uint32_t)index*OBJ_SIZE;}
int tri_obj_boss_hit(void){return 0;}
#include "../../game/trilogy/trilogy_mighty.c"
static void clear(void)
{
    tri_mighty_selected=1;
    memset(g_ram,0,sizeof g_ram);memset(inventory,0,sizeof inventory);
    old_super=super_mask=super_saved=0;call_count=0;
}
int main(int argc,char **argv)
{
    clear();equip(SH_FIRE);store(SH_BASIC);store(SH_LIGHTNING);store(SH_BUBBLE);
    w16(RAM_Ring_count,4);swap_shield(0x40);
    CHECK(shield()==SH_FIRE && r16(RAM_Ring_count)==4 && inventory[0]==SH_BUBBLE);
    w16(RAM_Ring_count,20);swap_shield(0x40);
    CHECK(shield()==SH_BUBBLE && r16(RAM_Ring_count)==15);
    CHECK(inventory[0]==SH_LIGHTNING && inventory[1]==SH_BASIC && inventory[2]==SH_FIRE && !inventory[3]);
    w8(RAM_Player_1+0x2F,1);swap_shield(0x41);
    CHECK(!shield() && inventory[0]==SH_BUBBLE && r8(RAM_Player_1+0x2F)==1);
    CHECK(r16(RAM_Ring_count)==15);
    swap_shield(0x40);CHECK(shield()==SH_BUBBLE && !r8(RAM_Player_1+0x2F));
    equip(SH_BASIC);CHECK(r8(RAM_Player_1+0x2B)==1); /* no bubble immunity/move */
    clear();equip(SH_FIRE);store(SH_BASIC);store(SH_LIGHTNING);
    w8(RAM_Super_Sonic_Knux_flag,1);frame();
    CHECK(shield()==7 && inventory[0]==0x24);
    super_pickup(SH_BUBBLE);
    CHECK(shield()==15 && inventory[0]==0x82 && inventory[1]==4 && inventory[2]==1);
    w8(RAM_Super_Sonic_Knux_flag,0);frame();
    CHECK(shield()==SH_BUBBLE && inventory[0]==SH_FIRE && inventory[1]==SH_LIGHTNING && inventory[2]==SH_BASIC);
    /* Super fire+bubble diagonal retains fire's horizontal velocity. */
    clear();old_super=1;super_mask=15;equip(15);w8(RAM_Super_Sonic_Knux_flag,1);
    w8(RAM_Ctrl_1_pressed_logical,0x10);w8(RAM_Ctrl_1_held_logical,0x1A);
    jump_ability();CHECK(call_count==2 && calls[0]==0x1192C && calls[1]==0x119A2);
    CHECK(r16(RAM_Player_1+OBJ_x_vel)==0x800 && r16(RAM_Player_1+OBJ_y_vel)==0x800 && bubble_bounce);
    w8(RAM_Player_1+0x2F,0);call_count=0;
    w8(RAM_Ctrl_1_held_logical,0x11);jump_ability();
    CHECK(call_count==1 && calls[0]==0x1197A && !bubble_bounce);
    call_count=0;w8(RAM_Ctrl_1_pressed_logical,0x30);w8(RAM_Ctrl_1_held_logical,0x30);
    jump_ability();CHECK(!r8(RAM_Super_Sonic_Knux_flag) && !call_count);
    CHECK(r16(RAM_Max_speed)==0x600 && r16(RAM_Acceleration)==12 && r16(RAM_Deceleration)==0x80);
    /* CD's $1A hitbox is a normal enemy; its converted boss can have that
     * same shape with HP. Preserve the normal kill/hurt service and costs. */
    clear();equip(SH_BASIC);w16(RAM_Ring_count,100);
    uint32_t cd=o_slot(2);w32(cd,0x7E2174);w8(cd+OBJ_render,0x84);w8(cd+OBJ_coll_flags,0x1A);
    basic_super_attack();CHECK(call_count==1 && calls[0]==TH_KILL_ENEMY && r16(RAM_Ring_count)==84);
    call_count=0;w8(cd+OBJ_coll_prop,7);w16(RAM_Ring_count,100);
    basic_super_attack();CHECK(call_count==3 && calls[0]==0x100BC && r16(RAM_Ring_count)==52);
    call_count=0;w8(cd+OBJ_coll_flags,0x9A);w16(RAM_Ring_count,100);
    basic_super_attack();CHECK(!call_count && r16(RAM_Ring_count)==100);
    if(argc>1) {
        FILE *f=fopen(argv[1],"rb");CHECK(f);g_rom_ext_size=(uint32_t)fread(bank,1,sizeof bank,f);fclose(f);
        CHECK(g_rom_ext_size>0x2040 && descriptor());uint32_t d=descriptor();
        for(unsigned anim=0;anim<36;anim++)for(unsigned direction=0;direction<4;direction++) {
            old_native=source_anim=last_frame=-1;timer=cursor=0;w8(RAM_Player_1+OBJ_anim,(uint8_t)anim);
            w8(RAM_Player_1+OBJ_status,direction&1);w8(RAM_Player_1+OBJ_angle,(uint8_t)(direction*64));
            w16(RAM_Player_1+OBJ_ground_vel,direction&2?0x800:0x200);
            for(unsigned frame=0;frame<600;frame++) {
                animate(RAM_Player_1,d);CHECK(r8(RAM_Player_1+OBJ_frame)<176);load_frame(RAM_Player_1,d);
            }
        }
        CHECK(queued>1000);
        printf("Private bank: all 36 S3 animation states in four orientations, %u bounded DPLC uploads\n",queued);
    }
    puts("Mighty inventory cost/order, basic shield, Super packing/restore and directed move contracts passed");return 0;
}
