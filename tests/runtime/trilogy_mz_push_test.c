/* Exercise the real MZ block against the native solid routine's documented
 * signed P1 correction / side-contact contract. Runtime routes cover the
 * actual S3K collision routine and source-placed blocks separately. */
#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
uint8_t g_ram[65536];
M68KState g_cpu;
uint32_t g_o_d6;
static int correction, side = 1, wall, sounds;
int tri_source_zone(void) { return TRI_S1_MZ; }
void o_draw(uint32_t o) { (void)o; }
void o_asset(uint32_t o,int asset) { (void)o;(void)asset; }
void o_delete(uint32_t o) { w32(o,0); }
int o_out_of_range(uint32_t o,uint16_t x) { (void)o;(void)x;return 0; }
void o_move(uint32_t o) { (void)o; }
int16_t o_floor_dist(uint32_t o) { (void)o;return 0; }
int16_t o_floor_block(uint32_t o,uint16_t *b) { (void)o;*b=0;return 0; }
int16_t o_wall_right(uint32_t o,int d) { (void)o;(void)d;return (int16_t)wall; }
int16_t o_wall_left(uint32_t o,int d) { (void)o;(void)d;return (int16_t)wall; }
uint32_t o_alloc(void) { return 0; }
int o_partly_visible(uint32_t o) { (void)o;return 0; }
void o_sfx(int id) { CHECK(id==0x69);sounds++; }
int16_t o_solid(uint32_t o,int a,int b,int c,int d)
{
    (void)o;(void)a;(void)b;(void)c;(void)d;
    g_o_d6=side?(1u<<16):0;
    w16(RAM_Player_1+OBJ_x_pos,(uint16_t)(r16(RAM_Player_1+OBJ_x_pos)-correction));
    return (int16_t)side;
}
/* Keep the unrelated registration table internal to this fixture, allowing
 * every supported linker to discard handlers outside the push contract. */
static void mz_unused_registration(TriObjFn *t);
#define s1_obj_mz_register mz_unused_registration
#include "../../game/trilogy/s1_obj_mz.c"
#undef s1_obj_mz_register

static const uint32_t block=RAM_Object_RAM+10*OBJ_SIZE, other=RAM_Object_RAM+11*OBJ_SIZE;
static void setup(int direction)
{
    memset(g_ram,0,sizeof g_ram);sounds=0;wall=0;side=1;
    w8(RAM_Current_act,1);
    for(int i=0;i<2;i++) {
        uint32_t o=i?other:block;
        w32(o,NATIVE_S1_BASE+0x33*4u);w8(o+OBJ_routine,2);
        w16(o+OBJ_x_pos,0x500);w16(o+OBJ_y_pos,0x490);w8(o+OBJ_width_px,0x10);
    }
    w16(RAM_Player_1+OBJ_x_pos,(uint16_t)(0x500-direction*26));
    w8(RAM_Player_1+OBJ_status,direction<0?1:0);
    correction=direction;
}
int main(void)
{
    for(int direction=-1;direction<=1;direction+=2) {
        setup(direction);correction=0;push_block(block);
        CHECK(r16(block+OBJ_x_pos)==0x500 && !sounds);
        setup(direction);push_block(block);
        CHECK(r16(block+OBJ_x_pos)==0x500+direction && sounds==1);
        CHECK(r16(RAM_Player_1+OBJ_x_pos)==0x500-direction*26);
        CHECK(r16(RAM_Player_1+OBJ_ground_vel)==(uint16_t)(direction*0x40));
        CHECK(!r16(RAM_Player_1+OBJ_x_vel));
        setup(direction);w8(RAM_Player_1+OBJ_status,r8(RAM_Player_1+OBJ_status)|2);push_block(block);
        CHECK(r16(block+OBJ_x_pos)==0x500 && !sounds);
        setup(direction);w8(block+PB_LAVA,1);pushb_solid(block,27,16,17,0x500);
        CHECK(r16(block+OBJ_x_pos)==0x500 && !sounds);
        setup(direction);w8(RAM_Player_1+OBJ_status,r8(RAM_Player_1+OBJ_status)^1);push_block(block);
        CHECK(r16(block+OBJ_x_pos)==0x500 && !sounds);
        setup(direction);wall=-1;push_block(block);
        CHECK(r16(block+OBJ_x_pos)==0x500 && !sounds);
        setup(direction);side=0;push_block(block);
        CHECK(r16(block+OBJ_x_pos)==0x500 && !sounds);
    }
    setup(1);pushb_sound(block);CHECK(sounds==1);
    correction=0;
    for(int tick=1;tick<31;tick++) {
        push_block(block);pushb_sound(other);CHECK(sounds==1);
    }
    push_block(block);pushb_sound(other);CHECK(sounds==2);
    CHECK(!r8(block+PB_SOUND) && r8(other+PB_SOUND)==31);
    /* The native stage/death clear also resets the sound gate. */
    setup(1);pushb_sound(other);CHECK(sounds==1);
    puts("MZ push: zero/nonzero correction, both directions, contact guards and shared 31-tick sound gate passed");
    return 0;
}
