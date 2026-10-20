/* Megamix v3 Shadow. Ground A: $197EC..$19844/$19B4E; homing:
 * $19DD0..$19F1E. Native S3K physics, solids and collision outcomes stay
 * authoritative. Host targeting state is reset at entries, hurt and death;
 * addon quickstates/netplay remain unavailable under the existing contract. */
#include "trilogy_obj.h"
#include "stage_pack.h"
#include "megamix_geometry_sites.h"

int tri_shadow_selected;
static uint32_t target,previous;
static unsigned remaining;
static int super_boost;
static int done(void)
{
    if(g_split_sp_popped>0){g_rte_pending=g_split_sp_popped;g_split_sp_popped=0;}
    return 1;
}
static int targetable(uint32_t o)
{
    if(!r32(o) || !(r8(o+OBJ_render)&0x80))return 0;
    unsigned c=r8(o+OBJ_coll_flags);
    return (c>=5 && c<=14) || c==0xCC || c==0x52 || c==0x46 || c==0x41;
}
static void steer(void)
{
    uint32_t p=RAM_Player_1;
    int16_t dx=(int16_t)(r16(target+OBJ_x_pos)-r16(p+OBJ_x_pos));
    int16_t dy=(int16_t)(r16(target+OBJ_y_pos)-r16(p+OBJ_y_pos));
    uint8_t angle=o_atan(dx,dy);int16_t sine,cosine;
    o_sincos(angle,&sine,&cosine);
    w8(p+OBJ_angle,angle);
    w16(p+OBJ_x_vel,(uint16_t)((int32_t)cosine*12));
    w16(p+OBJ_y_vel,(uint16_t)((int32_t)sine*12));
}
static int acquire(int charged)
{
    uint32_t p=RAM_Player_1,best=0;int32_t distance=0x4000;
    if(charged && r16(RAM_Ring_count)<=5)return 0;
    for(unsigned i=2;i<110;i++) {
        uint32_t o=o_slot((int)i);if(o==previous || !targetable(o))continue;
        int32_t dx=(int16_t)(r16(o+OBJ_x_pos)-r16(p+OBJ_x_pos));
        int32_t dy=(int16_t)(r16(o+OBJ_y_pos)-r16(p+OBJ_y_pos));
        /* Bounds first: avoid signed overflow with malformed distant objects. */
        if(dx<=-128 || dx>=128 || dy<=-128 || dy>=128)continue;
        if(!charged && ((r8(p+OBJ_status)&1)?dx>=16:dx<=-16))continue;
        int32_t squared=dx*dx+dy*dy;
        if(squared<distance){best=o;distance=squared;}
    }
    if(!best)return 0;
    if(charged) {
        w16(RAM_Ring_count,r16(RAM_Ring_count)-5);
        w8(RAM_Update_HUD_ring_count,r8(RAM_Update_HUD_ring_count)|0x81);
    }
    target=best;remaining=32;
    w8(p+OBJ_status,(r8(p+OBJ_status)|6)&~0x10u);
    w8(p+OBJ_anim,2);w8(p+0x1E,14);w8(p+0x1F,7);
    w8(p+0x40,1);w8(p+0x2F,1);
    steer();o_sfx(0x3E);return 1; /* native fire-dash effect */
}
static void homing_frame(void)
{
    uint32_t p=RAM_Player_1;
    if(r8(p+OBJ_routine)!=2 || !(r8(p+OBJ_status)&2) || r8(p+0x2E)) {
        target=previous=remaining=0;return;
    }
    if(!target)return;
    if(!remaining || !targetable(target)) {
        previous=target;target=0;remaining=0;
        if(r8(RAM_Ctrl_1_held_logical)&0x40)acquire(1);
        return;
    }
    --remaining;steer();
}
static int ground_action(void)
{
    uint32_t p=RAM_Player_1;
    if(!(r8(RAM_Ctrl_1_pressed_logical)&0x40))return 0;
    /* A is reserved even when the move is blocked; B/C still jump. */
    unsigned super=r8(RAM_Super_Sonic_Knux_flag)!=0,status=r8(p+OBJ_status);
    if(status&0x22 || (!super && (status&4)) || r8(p+0x2E))return done();
    /* Native player callers keep their speed-profile pointer in A4 and
     * graphics workspaces in A5/A6. Object collision helpers may clobber them,
     * just as CalcRoomOverHead does inside the retail Sonic_Jump. */
    uint32_t a4=A(4),a5=A(5),a6=A(6);
    int16_t room=(r8(p+OBJ_status)&1)?o_wall_left(p,-16):o_wall_right(p,16);
    A(4)=a4;A(5)=a5;A(6)=a6;
    if(room<0)return done();
    if(!super) {
        if(r16(RAM_Ring_count)<5){o_sfx(0x5B);return done();}
        w16(RAM_Ring_count,r16(RAM_Ring_count)-5);
        w8(RAM_Update_HUD_ring_count,r8(RAM_Update_HUD_ring_count)|0x81);
    }
    int speed=r8(p+0x36)?0x1000:0xC00;
    if(super) {
        unsigned held=r8(RAM_Ctrl_1_held_logical);
        if(held&4)status|=1;
        if(held&8)status&=~1u;
        w8(p+OBJ_status,(uint8_t)(status&~4u));
        if(status&4) {
            w8(p+OBJ_y_radius,19);w8(p+OBJ_x_radius,9);w8(p+OBJ_anim,5);
            w16(p+OBJ_y_pos,r16(p+OBJ_y_pos)+(r8(0xFFF7C6)?5:-5));
        }
        int current=(int16_t)r16(p+OBJ_ground_vel);
        if(status&1)current=-current;
        speed=current<0xC00?speed:current+0x40;
        w16(RAM_Max_speed,0x1100);super_boost=1;
        int timer=(int16_t)r16(0xFFF670); /* retail Super_frame_count */
        if(timer>10)timer=12;
        w16(0xFFF670,(uint16_t)(timer-2));
    }
    if(status&1)speed=-speed;
    w16(p+OBJ_ground_vel,(uint16_t)speed);w8(p+OBJ_anim,1);
    o_sfx(0xB6);return done(); /* native dash effect */
}
static void boost_frame(void)
{
    uint32_t p=RAM_Player_1;
    if(!super_boost)return;
    int speed=(int16_t)r16(p+OBJ_ground_vel);if(speed<0)speed=-speed;
    int super=r8(RAM_Super_Sonic_Knux_flag)!=0;
    if(!super || !(r8(RAM_Ctrl_1_held_logical)&0x40) || speed<0x600 ||
       r8(p+OBJ_routine)!=2 || r8(p+0x2E)) {
        super_boost=0;
        w16(RAM_Max_speed,(r8(p+OBJ_status)&0x40)?(super?0x500:0x300):(super?0xA00:0x600));
        return;
    }
    if(speed<0xC00)w16(p+OBJ_ground_vel,(r8(p+OBJ_status)&1)?0xF400:0xC00);
    /* v3 $11CEEE..$11CF0A resets its counter to ten. Both original
     * countdowns use BPL, so this produces eleven-frame drain intervals.
     * Native $11B3E decrements Super_frame_count ($F670), not Palette_frame. */
    if((int16_t)r16(0xFFF670)>10)w16(0xFFF670,10);
}
int tri_shadow_hook(uint32_t pc)
{
    if(!tri_shadow_active())return 0;
    uint32_t p=A(0)&0xFFFFFFu;
    switch(pc) {
    case TH_LEVEL_ENTRY:target=previous=remaining=0;super_boost=0;return 0;
    case TH_LVL_PRE_SPRITES:boost_frame();return 0;
    case MG_PC_SHADOW_GROUND:if(p==RAM_Player_1)return ground_action();return 0;
    case MG_PC_SHADOW_AIR:
        if(p==RAM_Player_1)homing_frame();
        return 0;
    case MG_PC_MIGHTY_SHIELD:
        if(p!=RAM_Player_1)return 0;
        if(tri_character_transform())return done();
        if((r8(RAM_Ctrl_1_pressed_logical)&0x30) && !target)acquire(0);
        return done();
    default:return 0;
    }
}
