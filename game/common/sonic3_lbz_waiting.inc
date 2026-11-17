/* The native encounter allocates its empty ship and running actor only
 * after the cartridge camera approaches. Their stationary arrival poses
 * can already be seen in a wider view. Keep those extra pixels tied to
 * original art until each real published actor takes ownership. */
#ifdef SONIC3_STANDALONE
enum { S3_LBZ_END_BOSS=0x50418, S3_LBZ_EMPTY_SHIP=0x5A84E,
       S3_LBZ_RUN=0x5086A, S3_LBZ_RUN_X=0x3B60 };
#else
enum { S3_LBZ_END_BOSS=0x738E0, S3_LBZ_EMPTY_SHIP=0x8D2B6,
       S3_LBZ_RUN=0x73D74, S3_LBZ_RUN_X=0x3B70 };
#endif
static struct { uint8_t pixels[0xAE0];int attempted,ready; } s_lbz_waiting_art[2];
static int lbz_waiting_load(unsigned actor)
{
    static const unsigned address[]={0x18041E,0x181002},compressed[]={0x55C,0x464},bytes[]={0xA40,0xAE0};
    if(actor>=2)return 0;
    if(!s_lbz_waiting_art[actor].attempted) {
        s_lbz_waiting_art[actor].attempted=1;
        s_lbz_waiting_art[actor].ready=art_nem(g_rom+S3_ART_BANK+address[actor],compressed[actor],
            s_lbz_waiting_art[actor].pixels,bytes[actor])==bytes[actor];
    }
    return s_lbz_waiting_art[actor].ready;
}
static unsigned lbz_waiting_needed(const uint8_t *ram,const SceneFrame *scene)
{
    if(!scene || scene->scene!=1 || scene->world_tag!=0x602 ||
       ram[0xF600]!=12 || ram[0xFE10]!=6 || ram[0xFE11]!=1 ||
       ram[0xB038]>1 || ram[0xB02E]==0x83 || scene->camera_x<0x3500)return 0;
    unsigned waiting=scene->camera_x<0x3A20?3u:0u;
    for(unsigned o=0xB000;o<=0xCAE2;o+=0x4A) {
        unsigned code=((unsigned)ram[o]<<24)|((unsigned)ram[o+1]<<16)|
            ((unsigned)ram[o+2]<<8)|ram[o+3];
        /* Check_CameraInRange replaces the entry with its return PC once.
         * Both that entry and the stable routine dispatcher own the arrival. */
        if((code==S3_LBZ_END_BOSS || code==S3_LBZ_END_BOSS - 10u) && ram[o+5]<=4)waiting=3;
    }
    for(unsigned n=0;n<scene->count;++n) {
        unsigned slot=sprite_slot(&scene->sprites[n]);
        if(slot>=SCENE_SLOTS)continue;
        if(scene->code[slot]==S3_LBZ_EMPTY_SHIP)waiting&=~1u;
        if(scene->code[slot]==S3_LBZ_RUN)waiting&=~2u;
    }
    return waiting;
}
static void lbz_waiting_scanline(int line,uint32_t *out,int width,int origin,const uint32_t *palette)
{
    if(width<=320 || !s_frame_level || s_frame_zone!=6 || s_frame_act!=1)return;
    int world_y=line+s_camera_y;
    if(world_y<0x5C8 || (world_y>=0x5F8 && world_y<0x634) || world_y>=0x674)return;
    unsigned waiting=lbz_waiting_needed(s_world_frame,s_display);
    for(unsigned actor=0;actor<2;++actor) {
        if(!(waiting&(1u<<actor)) || !lbz_waiting_load(actor))continue;
        unsigned map=S3_ART_BANK+(actor?0x1601CAu:0x160058u),frame=actor?0:10;
        unsigned p=map+(int16_t)scene_read16(map+frame*2),count=scene_read16(p);p+=2;
        if(count>16)continue;
        int x=actor?S3_LBZ_RUN_X:0x3BDE,y=actor?0x5E0:0x654;
        unsigned tile=actor?0x4A9:0x52E,gfx=actor?0x4A9:0x852E;
        for(unsigned n=0;n<count;++n,p+=6) {
            unsigned size=scene_read8(p+1);if(size>15)continue;
            int cw=((size>>2)&3)+1,ch=(size&3)+1;
            int dy=(int8_t)scene_read8(p),dx=(int16_t)scene_read16(p+4);
            unsigned attr=(scene_read16(p+2)+gfx)&65535u;
            if(!actor){dx=-dx-cw*8;attr^=0x800u;}
            int iy=line-(y+dy-s_camera_y);if(iy<0 || iy>=ch*8)continue;
            if(attr&0x1000u)iy=ch*8-1-iy;
            int base=origin+x+dx-s_camera;
            for(int i=0;i<cw*8;++i) {
                int sx=base+i;if(sx<0 || sx>=width || (sx>=origin && sx<origin+320) || (s_priority[sx]&2u))continue;
                int ix=(attr&0x800u)?cw*8-1-i:i;
                unsigned cell=((attr&2047u)+(unsigned)(ix/8*ch+iy/8))&2047u;
                unsigned a=(cell-tile)*32u+(unsigned)(iy&7)*4u+(unsigned)(ix&7)/2u;
                if(cell<tile || a>=(actor?0xAE0u:0xA40u))continue;
                unsigned byte=s_lbz_waiting_art[actor].pixels[a],pixel=(ix&1)?byte&15u:byte>>4;
                if(!pixel)continue;pixel|=(attr>>9)&48u;
                if((attr&0x8000u) || !(s_priority[sx]&1u))out[sx]=palette[pixel];
                s_priority[sx]|=2u;
            }
        }
    }
}
