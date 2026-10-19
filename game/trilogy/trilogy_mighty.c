/* Mighty from the owner's exact Megamix v3 donor. Source bindings and sprite
 * bytecode are verified by the converter; host state belongs to this optional
 * character and is reset on new games/death. Stage quickstates remain blocked.
 * Native Sonic movement, solids, hurt and elemental physics remain the caller
 * contract. We translate their animation semantics, not their frame numbers.
 */
#include "trilogy_obj.h"
#include "stage_pack.h"
#include "megamix_geometry_sites.h"
#include <string.h>

enum { SH_BASIC=1,SH_FIRE=2,SH_LIGHTNING=4,SH_BUBBLE=8 };
static uint8_t inventory[4],old_super,super_saved,super_mask;
static int old_native=-1,source_anim=-1,cursor,timer,last_frame=-1,pal_tick;
static int bubble_bounce;
int tri_mighty_selected;
static uint16_t super_colors[2][4];
static int palette_cycling;
static uint32_t descriptor(void)
{
    uint32_t d=tri_rd32(TRI_BANK_BASE+0x24);
    return d && tri_rd32(d)==0x4D444931u ? tri_rd32(d+12):0;
}
static int active(void)
{
    return s3_stage_pack_mighty && tri_mighty_selected && r16(RAM_Player_mode)<2 && descriptor();
}
int tri_mighty_active(void) { return active(); }
uint16_t tri_mighty_color(int wet,unsigned color)
{
    uint32_t d=descriptor();
    if(!d)return 0;
    if(palette_cycling && color>=2 && color<6)return super_colors[wet!=0][color-2];
    uint32_t pal=tri_rd32(d+(wet?36:16));
    if(wet && r16(RAM_Current_zone_and_act)==0x3503)pal=tri_rd32(d+40);
    return tri_rd16(pal+(color&15)*2);
}
static int done(void)
{
    if(g_split_sp_popped>0){g_rte_pending=g_split_sp_popped;g_split_sp_popped=0;}
    return 1;
}
static uint8_t shield(void)
{
    if(old_super && r8(RAM_Super_Sonic_Knux_flag))return super_mask;
    uint8_t s=r8(RAM_Player_1+0x2B);
    return !(s&1)?0:((s&0x70)?(uint8_t)((s&0x70)>>3):SH_BASIC);
}
static void equip(uint8_t s)
{
    uint32_t p=RAM_Player_1,q=RAM_Shield;
    w8(p+0x2B,(r8(p+0x2B)&0x8E)|(s?1:0)|((s&14)<<3));
    memset(g_ram+(q&0xFFFF),0,OBJ_SIZE);
    if(!s)return;
    w32(q,(s&SH_FIRE)?S3K_Obj_FireShield:(s&SH_LIGHTNING)?S3K_Obj_LightningShield:S3K_Obj_BubbleShield);
    w16(q+0x42,p);
    w8(p+0x2F,0);
}
static void store(uint8_t s)
{
    if(!s)return;
    memmove(inventory+1,inventory,3);inventory[0]=s;
}
static int inventory_any(void)
{ return inventory[0]|inventory[1]|inventory[2]|inventory[3]; }

/* S1 v3 $19950-$19AB8. Inventory[0] is the low byte at $FFB3. */
static void swap_shield(int held)
{
    uint8_t current=shield();
    if(held==0x41) {
        uint8_t used=r8(RAM_Player_1+0x2F);
        if(!current) {
            for(int i=3;i>=0;i--)if(inventory[i]){current=inventory[i];inventory[i]=0;break;}
        }
        store(current);equip(0);w8(RAM_Player_1+0x2F,used);return;
    }
    if(!inventory_any() || current==15)return;
    if(r16(RAM_Ring_count)<5){o_sfx(0x5B);return;}
    w16(RAM_Ring_count,r16(RAM_Ring_count)-5);
    w8(RAM_Update_HUD_ring_count,r8(RAM_Update_HUD_ring_count)|0x81);
    uint8_t next=inventory[0];
    memmove(inventory,inventory+1,3);inventory[3]=0;
    for(int i=0;i<4;i++)if(!inventory[i]){inventory[i]=current;break;}
    equip(next);w8(RAM_Player_1+0x40,1);
    o_sfx((next&SH_FIRE)?0x3E:(next&SH_LIGHTNING)?0x41:0x3F);
}

/* Exact v3 $19AD4-$19AFA nibble operations while Super. A new pickup
 * becomes the shield restored on reversion; the previous one enters storage. */
static void super_pickup(uint8_t next)
{
    uint32_t packed=0;
    for(int i=3;i>=0;i--)packed=(packed<<8)|inventory[i];
    if(inventory[0]&0xF0) {
        packed<<=8;
        uint16_t value=(uint16_t)(packed|next);
        value=(uint16_t)((value<<4)|(value>>12));
        packed=(packed&0xFFFFFF00u)|(value&255u);
        packed&=0xFFFF0FFFu;
    } else packed|=(uint32_t)next<<4;
    for(unsigned i=0;i<4;i++){inventory[i]=(uint8_t)packed;packed>>=8;}
    super_mask|=next;equip(super_mask);
}

static int semantic_animation(int a)
{
    /* S3K ID -> v3 ID: dash, skid, spring, hanging, air, death and hurt.
     * S3-only hanging poses use the donor's hanging animation. */
    static const uint8_t translation[36]={
        0,1,2,3,4,5,6,7,8,0x1F,0x0D,0x0E,0x0F,0x10,0x11,0x11,
        0x12,0x13,0x14,0x15,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,
        0x1C,0x0E,0x0F,5,0,0x19,0x11,0x11};
    return a<36?translation[a]:0;
}
static void animate(uint32_t p,uint32_t d)
{
    int native=r8(p+OBJ_anim),ani=semantic_animation(native);
    if(native!=old_native) {
        old_native=native;source_anim=ani;cursor=0;timer=0;
        w8(p+0x21,(uint8_t)native);
    }
    uint32_t base=tri_rd32(d+20),script=base+tri_rd16(base+source_anim*2u);
    int delay=(int8_t)tri_rd8(script),offset=0,speed=(int16_t)r16(p+OBJ_ground_vel);
    if(speed<0)speed=-speed;
    unsigned facing=r8(p+OBJ_status)&1,flips=facing;
    if(delay==-1) {
        unsigned angle=r8(p+OBJ_angle);
        if(!facing)angle=(~angle)&255;
        angle=(angle+16)&255;flips=facing^((angle&128)?3:0);
        if(r8(p+OBJ_status)&0x20) {
            script=base+0x78;delay=(0x800-speed)>0?(0x800-speed)>>6:0;
        } else {
            offset=(angle>>4)&6;
            if(speed<0x600){script=base+0x58;offset=offset*3;}
            else {script=base+0x60;offset*=2;}
            delay=(0x800-speed)>0?(0x800-speed)>>8:0;
        }
    } else if(delay==-2) {
        script=base+(speed<0x600?0x68:0x70);
        delay=(0x400-speed)>0?(0x400-speed)>>8:0;
    } else if(delay==-3) {
        script=base+0x78;delay=(0x800-speed)>0?(0x800-speed)>>6:0;
    }
    w8(p+OBJ_render,(r8(p+OBJ_render)&0xFC)|flips);
    if(--timer>=0)return;
    timer=delay;
    for(int attempts=0;attempts<3;attempts++) {
        uint8_t frame=tri_rd8(script+1u+(unsigned)cursor);
        if(frame<0xFC) {
            frame=(uint8_t)(frame+offset);
            if(frame<tri_rd16(d+32))w8(p+OBJ_frame,frame);
            cursor++;return;
        }
        if(frame==0xFF){cursor=0;continue;}
        if(frame==0xFE){cursor-=tri_rd8(script+2u+(unsigned)cursor);continue;}
        if(frame==0xFD) {
            source_anim=tri_rd8(script+2u+(unsigned)cursor);
            cursor=0;timer=0;
            if(!source_anim){w8(p+OBJ_anim,0);old_native=0;}
        }
        return;
    }
}
static void load_frame(uint32_t p,uint32_t d)
{
    unsigned frame=r8(p+OBJ_frame);
    if(frame>=tri_rd16(d+32))frame=0;
    w32(p+OBJ_mappings,tri_rd32(d+4));
    w16(p+OBJ_art_tile,(r16(p+OBJ_art_tile)&0x8000)|0x680);
    if((int)frame==last_frame)return;
    last_frame=(int)frame;w8(0xFFF766,(uint8_t)frame);
    uint32_t table=tri_rd32(d+8),art=tri_rd32(d+12);
    uint32_t pc=table+tri_rd16(table+frame*2),dest=0xD000;
    unsigned entries=tri_rd16(pc);pc+=2;
    for(unsigned i=0;i<entries;i++,pc+=2) {
        unsigned entry=tri_rd16(pc),len=((entry>>12)+1)*32;
        tri_vram_queue(art+(entry&0xFFF)*32,dest,len);dest+=len;
    }
}
static void super_palette(uint32_t d)
{
    int state=r8(0xFFF65F);
    if(!state)return;
    if(state==1) {
        if(++pal_tick<16)return;
        w8(0xFFF65F,255);w8(RAM_Player_1+0x2E,0);w8(RAM_Player_1+OBJ_anim,0);pal_tick=0;
    } else if(state!=255) {
        palette_cycling=0;
        w8(0xFFF65F,0);pal_tick=0;return;
    }
    unsigned frame=((unsigned)pal_tick++/4)&15;
    for(unsigned wet=0;wet<2;wet++) {
        uint32_t src=tri_rd32(d+(wet?48:44))+frame*8;
        for(unsigned i=0;i<4;i++)super_colors[wet][i]=tri_rd16(src+i*2);
    }
    palette_cycling=1;
}
static void frame(void)
{
    uint32_t p=RAM_Player_1;
    if(r8(p+OBJ_routine)>=6) {memset(inventory,0,sizeof inventory);super_mask=super_saved=0;}
    unsigned super=r8(RAM_Super_Sonic_Knux_flag)!=0;
    if(super && !old_super) {
        super_saved=shield();super_mask=super_saved;
        for(unsigned i=0;i<4;i++)super_mask|=inventory[i];
        inventory[0]|=super_saved<<4;
        equip(super_mask);
    } else if(!super && old_super) {
        super_saved=inventory[0]>>4;inventory[0]&=15;
        if(r8(p+OBJ_status)&0x40)super_saved&=9;
        equip(super_saved);super_mask=0;
    }
    old_super=(uint8_t)super;
}
static void basic_super_attack(void)
{
    /* v3 $117032: basic shield attacks visible enemies; every elemental
     * halves its ring price. Bosses cost three times as much and lose half
     * their remaining HP through their normal hurt path. */
    unsigned mask=shield(),price=16;
    for(unsigned bit=2;bit<=8;bit<<=1)if(mask&bit)price>>=1;
    if(!(mask&1) || r16(RAM_Ring_count)<price)return;
    uint32_t p=RAM_Player_1;
    w8(p+0x2F,1);
    for(int i=2;i<110;i++) {
        uint32_t o=o_slot(i);unsigned collision=r8(o+OBJ_coll_flags);
        if(!r32(o) || !(r8(o+OBJ_render)&0x80) || !collision)continue;
        unsigned boss=r8(o+OBJ_coll_prop) &&
            ((collision&0xC0)==0xC0 || (collision&0x3F)==0x0F);
        if(!boss && !((collision>=5 && collision<=14) || collision==0xCC))continue;
        unsigned cost=price*(boss?3:1);
        if(r16(RAM_Ring_count)<cost)break;
        w16(RAM_Ring_count,r16(RAM_Ring_count)-cost);
        A(0)=RAMPTR(p);A(1)=RAMPTR(o);
        if(boss) {
            unsigned hits=r8(o+OBJ_coll_prop)/2; /* v3 keeps ceil(HP/2) */
            for(unsigned hit=0;hit<hits;hit++) {
                extern int tri_obj_boss_hit(void);
                A(0)=RAMPTR(p);A(1)=RAMPTR(o);
                if(!tri_obj_boss_hit())tri_jsr(0x100BC); /* callable Touch_Enemy entry */
            }
        } else tri_jsr(TH_KILL_ENEMY);
    }
    w8(RAM_Update_HUD_ring_count,r8(RAM_Update_HUD_ring_count)|0x81);
    w16(p+OBJ_x_vel,0);w16(p+OBJ_y_vel,0);w16(p+OBJ_ground_vel,0);
    o_sfx(0x41);
}
static int jump_ability(void)
{
    uint32_t p=RAM_Player_1;
    unsigned pressed=r8(RAM_Ctrl_1_pressed_logical),held=r8(RAM_Ctrl_1_held_logical);
    if(!(pressed&0x70))return done();
    if((held&0x30)==0x30 && (pressed&0x30)) {
        if(r8(RAM_Super_Sonic_Knux_flag)) {
            /* The native revert block is an interior label, not a callable
             * dispatch entry; write its documented state through this ABI. */
            w8(0xFFF65F,2);w16(0xFFF65C,0x1E);
            w8(RAM_Super_Sonic_Knux_flag,0);w8(0xFFF667,0);w8(0xFFF766,255);
            w8(p+0x21,1);w8(p+0x35,1);
            int wet=(r8(p+OBJ_status)&0x40)!=0;
            w16(RAM_Max_speed,wet?0x300:0x600);w16(RAM_Acceleration,wet?6:0xC);
            w16(RAM_Deceleration,wet?0x40:0x80);
            w8(p+0x2F,4);return done();
        }
        if(r16(RAM_Ring_count)>=50 && r8(RAM_Chaos_emerald_count)>=7 && r8(RAM_Update_HUD_timer)) {
            uint32_t a4=A(4);A(4)=RAMPTR(RAM_Max_speed);
            tri_jsr(0x119D2);A(4)=a4;w8(p+0x2F,4);return done();
        }
    }
    if(pressed&0x40) {
        if(r8(RAM_Super_Sonic_Knux_flag)) {if(!r8(p+0x2F))basic_super_attack();}
        else swap_shield(held);
        return done();
    }
    if(r8(p+0x2F))return done();
    if(r8(RAM_Super_Sonic_Knux_flag)) {
        unsigned s=shield(),f=(s&SH_FIRE)!=0,l=(s&SH_LIGHTNING)!=0,b=(s&SH_BUBBLE)!=0;
        unsigned down=(held&2)!=0,up=(held&1)!=0,lr=(held&12)!=0;
        uint8_t original=r8(p+0x2B);
        w8(p+0x2B,original&~2u);bubble_bounce=0;
        if(f && (lr || !(b&&down) && !(l&&up))) {
            tri_jsr(0x1192C);
            if(b&&down) {
                uint16_t vx=r16(p+OBJ_x_vel),ground=r16(p+OBJ_ground_vel);
                tri_jsr(0x119A2);bubble_bounce=1;
                if(lr){w16(p+OBJ_x_vel,vx);w16(p+OBJ_ground_vel,ground);}
            } else if(l&&up)tri_jsr(0x1197A);
        } else if(l && !(b&&down))tri_jsr(0x1197A);
        else if(b){tri_jsr(0x119A2);bubble_bounce=1;}
        w8(p+0x2B,original);
        return done();
    }
    return 0; /* native elemental / Insta-Shield / Super transform */
}
int tri_mighty_hook(uint32_t pc)
{
    /* The monitor tag is needed for the basic shield even with Sonic. */
    uint32_t e=tri_dir();
    if(pc==MG_PC_MONITOR_ICON && e && tri_rd8(e+TD_GAME)==TRI_GAME_MEGAMIX) {
        unsigned index=(uint16_t)(r16(A(0)+OBJ_respawn)-0xEB00);
        uint32_t tags=tri_rd32(tri_rd32(e+TD_EXTRA)+56);
        if(index<0x300)w8(A(1)+OBJ_subtype,0x80|tri_rd8(tags+index));
    }
    if(pc==MG_PC_MONITOR_BUBBLE && r8(A(0)+OBJ_subtype)==0x84) {
        uint32_t p=A(1)&0xFFFFFF;
        if(active() && p==RAM_Player_1) {
            if(r8(RAM_Super_Sonic_Knux_flag)){super_pickup(1);w16(A(2),r16(A(2))+1);return done();}
            store(shield());
        }
        w16(A(2),r16(A(2))+1);
        w8(p+0x2B,(r8(p+0x2B)&0x8E)|1);
        uint32_t q=p==RAM_Player_1?RAM_Shield:RAM_Shield+OBJ_SIZE;
        memset(g_ram+(q&0xFFFF),0,OBJ_SIZE);w32(q,S3K_Obj_BubbleShield);w16(q+0x42,p);
        o_sfx(0x3F);return done();
    }
    if(!active())return 0;
    uint32_t d=descriptor(),p=A(0)&0xFFFFFF;
    switch(pc) {
    case TH_GAMELOOP:
        if(r8(RAM_Game_mode)==0x4C){memset(inventory,0,sizeof inventory);old_super=0;}
        return 0;
    case TH_LEVEL_ENTRY:
        old_native=source_anim=last_frame=-1;cursor=timer=pal_tick=bubble_bounce=0;
        palette_cycling=0;return 0;
    case TH_LVL_PRE_SPRITES:frame();return 0;
    case MG_PC_MIGHTY_ANIMATE:
        if(p==RAM_Player_1){animate(p,d);return done();}return 0;
    case MG_PC_MIGHTY_PLC:case MG_PC_MIGHTY_PLC2:
        if(p==RAM_Player_1){load_frame(p,d);return done();}return 0;
    case MG_PC_MIGHTY_SUPER_PAL:super_palette(d);return done();
    case MG_PC_MIGHTY_SHIELD:
        if(p==RAM_Player_1)return jump_ability();return 0;
    case MG_PC_MIGHTY_LAND:
        if(p==RAM_Player_1 && r8(RAM_Super_Sonic_Knux_flag) && bubble_bounce) {
            bubble_bounce=0;tri_jsr(0x12232);
        }
        return 0;
    case MG_PC_MONITOR_FIRE:case MG_PC_MONITOR_LIGHTNING:case MG_PC_MONITOR_BUBBLE:
        if((A(1)&0xFFFFFF)==RAM_Player_1) {
            if(r8(RAM_Super_Sonic_Knux_flag)) {
                unsigned next=pc==MG_PC_MONITOR_FIRE?2:pc==MG_PC_MONITOR_LIGHTNING?4:8;
                super_pickup((uint8_t)next);
                w16(A(2),r16(A(2))+1);return done();
            }
            store(shield());
        }
        return 0;
    default:return 0;
    }
}
