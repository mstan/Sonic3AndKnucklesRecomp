/* Far-edge poses use the cartridge mappings. Native actors still own
 * collision, cutscene timing, allocation and the transition to IceCap. */
typedef struct {
    unsigned map,frame,attr;
    int x,y;
    const uint8_t *art;
    unsigned first_tile,tile_count;
} MarginPose;

static void margin_pose_scanline(const MarginPose *pose,int line,uint32_t *out,
                                 int width,int origin,const uint32_t *palette)
{
    unsigned p=pose->map+(int16_t)scene_read16(pose->map+pose->frame*2);
    unsigned count=scene_read16(p);p+=2;
    if(count>32)return;
    for(unsigned n=0;n<count;++n,p+=6) {
        unsigned shape=scene_read8(p+1);
        if(shape>15)return;
        int cw=(shape>>2)+1,ch=(shape&3)+1;
        int iy=line-pose->y-(int8_t)scene_read8(p);
        if(iy<0 || iy>=ch*8)continue;
        int px=pose->x+(int16_t)scene_read16(p+4);
        unsigned attr=(scene_read16(p+2)+pose->attr)&65535u;
        if(attr&0x1000u)iy=ch*8-1-iy;
        for(int dx=0;dx<cw*8;++dx) {
            int x=px+dx;
            if(x<0 || x>=width || (x>=origin && x<origin+320) || (s_priority[x]&2u))continue;
            int ix=attr&0x800u?cw*8-1-dx:dx;
            unsigned tile=(attr+ix/8*ch+iy/8)&2047u;
            uint8_t color=0;
            if(pose->art) {
                if(tile<pose->first_tile || tile-pose->first_tile>=pose->tile_count)continue;
                unsigned byte=pose->art[(tile-pose->first_tile)*32u+(iy&7)*4u+(ix&7)/2u];
                unsigned pixel=(ix&1)?byte&15u:byte>>4;
                if(pixel)color=(uint8_t)(pixel|((attr>>9)&48u));
            } else {
                native_art_pixel(5,(attr&0xF800u)|tile,ix&7,iy&7,&color);
            }
            if(!color)continue;
            if((attr&0x8000u) || !(s_priority[x]&1u))out[x]=palette[color];
            s_priority[x]|=2u;
        }
    }
}

#ifndef SONIC3_STANDALONE
static unsigned margin_actor_code(const uint8_t *actor)
{
    return (unsigned)actor[0]<<24|(unsigned)actor[1]<<16|
           (unsigned)actor[2]<<8|actor[3];
}

static void cnz_waiting_scanline(int line,uint32_t *out,int width,
                                int origin,const uint32_t *palette)
{
    if(width<=320 || !s_frame_level || s_frame_zone!=3 || s_frame_act!=1 ||
       s_camera<0x4650 || s_camera>=0x4B20 || (s_world_frame[0xF600]&127)!=12)return;

    /* The boss placement and each controller phase are visible in the
     * published actor snapshot. Scan only where a waiting pose can be seen. */
    int sx=origin-s_camera,sy=-s_camera_y;
    int boss_x=sx+0x4940,boss_y=sy+0x270;
    int cap_x=sx+0x4990,cap_y=sy+0x2E0;
    int cannon_x=sx+0x4B20,cannon_y=sy+0x2A8;
    int boss_row=boss_x+48>=origin+320 && boss_x-48<width &&
                 line>=boss_y-36 && line<boss_y+40;
    int cap_row=cap_x+32>=origin+320 && cap_x-32<width &&
                line>=cap_y-48 && line<cap_y+32;
    int cannon_row=cannon_x+32>=origin+320 && cannon_x-32<width &&
                   line>=cannon_y-40 && line<cannon_y+24;
    if(!boss_row && !cap_row && !cannon_row)return;

    int waiting=0,defeated=0,capsule=0,cannon_ready=0;
    for(unsigned o=0xB094;o<=0xCAE2;o+=0x4A) {
        const uint8_t *actor=s_world_frame+o;
        unsigned code=margin_actor_code(actor);
        if(code==0x6E47E || code==0x6E4B8 ||
           (code==0x6E4C6 && !actor[0x05]))waiting=1;
        if(code==0x8488A && margin_actor_code(actor+0x34)==0x6E6E4)defeated=1;
        if(code==0x86540 && actor[0x10]==0x49 && actor[0x11]==0x90)capsule=1;
        if(code==0x6E778)cannon_ready=1;
    }

    if(boss_row && waiting) {
        /* Native PLC $6E and palette line 1, before the guest loads them. */
        static uint8_t boss[0x920],ship[0xA40];
        static int ready;
        if(!ready)
            ready=(art_nem(g_rom+0x36EA04,0x513,boss,sizeof boss)==sizeof boss &&
                   art_nem(g_rom+0xD771E,0x55C,ship,sizeof ship)==sizeof ship)?1:-1;
        if(ready<0)return;
        uint32_t colors[64];
        memcpy(colors,palette,sizeof colors);
        for(unsigned n=0;n<16;++n)
            colors[16+n]=genesis_dac_cram_to_argb(scene_read16(0x6EE48+n*2),GENESIS_DAC_NORMAL);
        const MarginPose hull={0x3609AA,0,0x2430,boss_x,boss_y,boss,0x430,sizeof boss/32};
        const MarginPose pilot={0x6820C,9,0x852E,boss_x,boss_y-8,ship,0x52E,sizeof ship/32};
        margin_pose_scanline(&pilot,line,out,width,origin,colors);
        margin_pose_scanline(&hull,line,out,width,origin,colors);
    }

    if(cap_row && defeated && !capsule) {
        const MarginPose lid={S3_CAPSULE_MAP,5,0x8494,cap_x,cap_y-0x24,NULL,0,0};
        const MarginPose body={S3_CAPSULE_MAP,0,0x8494,cap_x,cap_y,NULL,0,0};
        margin_pose_scanline(&lid,line,out,width,origin,palette);
        margin_pose_scanline(&body,line,out,width,origin,palette);
    }

    if(cannon_row && cannon_ready) {
        static uint8_t base_art[0x1BE0];
        static int ready;
        if(!ready)
            ready=art_nem(g_rom+0x394AB6,0xDBE,base_art,sizeof base_art)==sizeof base_art?1:-1;
        if(ready<0)return;
        /* Resting barrel frame 4 reads the first 12 tiles of its native DPLC. */
        const MarginPose base={0x230A32,9,0x4374,cannon_x,cannon_y,base_art,0x351,sizeof base_art/32};
        const MarginPose barrel={0x230A32,4,0x4374,cannon_x,cannon_y,g_rom+0x28CE74,0x448,12};
        margin_pose_scanline(&base,line,out,width,origin,palette);
        margin_pose_scanline(&barrel,line,out,width,origin,palette);
    }
}
#endif
