#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
uint8_t g_ram[65536];M68KState g_cpu;
static int deleted,sounds;
void o_draw(uint32_t o) { (void)o; }
void o_touch(uint32_t o) { (void)o; }
void o_asset(uint32_t o,int a) { (void)o;(void)a; }
void o_delete(uint32_t o) { w32(o,0);++deleted; }
uint32_t o_alloc(void) { return 0; }
uint32_t o_alloc_after(uint32_t o) { (void)o;return 0; }
int o_random(void) { return 0; }
void o_sfx(int id) { CHECK(id==0xB4);++sounds; }
/* Cardinal fixture; base radius zero is independent of its evolving angle. */
void o_sincos(uint8_t a,int16_t *s,int16_t *c) { (void)a;*s=256;*c=0; }
static void unused_boss_registration(TriObjFn *t);
#define s1_obj_boss_register unused_boss_registration
#include "../../game/trilogy/s1_obj_boss.c"
#undef s1_obj_boss_register
static const uint32_t boss=RAM_Object_RAM+10*OBJ_SIZE;
static const uint32_t base=RAM_Object_RAM+13*OBJ_SIZE;
static const uint32_t ball=RAM_Object_RAM+18*OBJ_SIZE;
static void setup(void)
{
    memset(g_ram,0,sizeof g_ram);memset(&g_cpu,0,sizeof g_cpu);deleted=sounds=0;
    w16(RAM_Level_frame_counter,1); /* no unrelated explosion allocation */
    w32(boss,NATIVE_S1_BASE+0x3D*4);w16(boss+OBJ_x_pos,500);w16(boss+OBJ_y_pos,300);
    w32(base,NATIVE_S1_BASE+0x48*4);w8(base+OBJ_routine,2);w32(base+PARENT,RAMPTR(boss));
    w16(base+GB_DROP,32);w16(base+OBJ_angle,0x4080);w16(base+GB_SPEED,(uint16_t)-0x200);
    w16(base+OBJ_x_pos,0x9999);w16(base+OBJ_y_pos,0x9999);
    w32(ball,NATIVE_S1_BASE+0x48*4);w8(ball+OBJ_routine,8);w32(ball+PARENT,RAMPTR(boss));
    w16(ball+GB_BASE,(uint16_t)base);w8(ball+GB_INDEX,5);w8(ball+GB_RADIUS,96);
    w8(ball+OBJ_coll_flags,0x81);
}
int main(void)
{
    setup();boss_ball(base);
    CHECK(r16(base+OBJ_x_pos)==500 && r16(base+OBJ_y_pos)==332);
    w8(base+OBJ_routine,4);w16(boss+OBJ_x_pos,493);w16(boss+OBJ_y_pos,303);boss_ball(base);
    CHECK(r16(base+OBJ_x_pos)==493 && r16(base+OBJ_y_pos)==335);
    boss_ball(ball);CHECK(r16(ball+OBJ_x_pos)==493 && r16(ball+OBJ_y_pos)==431);
    CHECK(r8(ball+OBJ_frame)==1 && r8(ball+GB_RADIUS)==96);
    w8(boss+OBJ_status,0x80);boss_ball(ball);
    CHECK(r32(ball)==NATIVE_S1_BASE+0x48*4 && r8(ball+GB_RADIUS)==95);
    CHECK(r8(ball+OBJ_coll_flags)==0 && r8(ball+OBJ_frame)==0);
    uint16_t x=r16(ball+OBJ_x_pos),y=r16(ball+OBJ_y_pos);
    w32(base,0); /* native explosion deletes its base before the ball expires */
    for(int i=1;i<=96;++i) {
        unsigned frame=r8(ball+OBJ_frame);boss_ball(ball);
        CHECK(r16(ball+OBJ_x_pos)==x && r16(ball+OBJ_y_pos)==y);
        CHECK(r8(ball+OBJ_frame)==!frame && !deleted);
        if(i<96)CHECK(r32(ball)==NATIVE_S1_BASE+0x48*4 && r8(ball+GB_RADIUS)==95-i);
        else CHECK(r32(ball)==S3K_Obj_Explosion && r8(ball+OBJ_routine)==6);
    }
    CHECK(sounds==1);
    setup();w32(base,0);boss_ball(ball);CHECK(deleted==1 && !r32(ball));
    puts("GHZ ball: moving base, original frame alternation and stationary 97-tick independent defeat lifetime PASS");
    return 0;
}
