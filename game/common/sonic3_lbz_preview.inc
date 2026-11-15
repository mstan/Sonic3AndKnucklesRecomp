/* The next act starts at old-world X=$4000; its incoming room edge also
 * replaces the final 32px before that boundary.
 * Own its immutable cartridge art separately while the guest still needs
 * the outgoing act's chunks, patterns and palette. */
static struct {
    int attempted,ready;
    uint8_t world[65536];
    GVDP video;
    uint16_t colors[48];
} s_lbz_preview;

static int lbz_preview_load(void)
{
    if(s_lbz_preview.attempted)return s_lbz_preview.ready;
    s_lbz_preview.attempted=1;
    size_t used=0;
    if(art_kos(g_rom+S3_ART_BANK+0x1EAF04,0x24D0,s_lbz_preview.world,0x8000,&used)!=0x8000 ||
       art_kos(g_rom+S3_ART_BANK+0x1DEA8C,0x540,s_lbz_preview.world+0x9000,0x6B8,&used)!=0x6B8 ||
       art_kos(g_rom+S3_ART_BANK+0x1E5F70,0xA40,s_lbz_preview.world+0x96B8,0xDE8,&used)!=0xDE8 ||
       art_kosm(S3_ART_BANK+0x1DFABC,0x1752,s_lbz_preview.video.vram,0x33A0)!=0x33A0 ||
       art_kosm(S3_ART_BANK+0x1E77D0,0x17A2,s_lbz_preview.video.vram+0x33A0,0x4260)!=0x4260)return 0;
    memcpy(s_lbz_preview.world+0x8000,g_rom+S3_ART_BANK+0x8A6FE,0xF28);
    if(s_lbz_preview.world[0x8000]!=0 || s_lbz_preview.world[0x8001]!=154)return 0;
    s_lbz_preview.world[0xEEAF]=0x7C;
#ifdef SONIC3_STANDALONE
    unsigned palette=0x8CA34;
#else
    unsigned palette=0xA92FC;
#endif
    for(unsigned i=0;i<48;++i)s_lbz_preview.colors[i]=scene_read16(palette+i*2);
    return s_lbz_preview.ready=1;
}

/* This arena's future viewport is wholly above the water deformation.
 * Reproduce the original cloud bands with their first native clock value;
 * scrolling starts when the guest actually installs the incoming act. */
static int lbz_preview_cloud_position(int camera_x,int camera_y,int shake,int line,int *x,int *y)
{
    static const unsigned lengths[]={0xC0,0x40,0x38,0x18,0x28,0x10,0x10,0x10,0x18,0x40,0x20,0x10,0x20};
    static const unsigned order[]={11,7,5,10,6,3,12,8,9,1,4,2,0};
    int n=camera_y-shake-0x5F0;
    int by=(n*27-(n<0?63:0))/64+0x2C0+shake+line;
    if(by<0)return 0;
    unsigned band=0,end=0;
    for(;band<13;++band){end+=lengths[band];if((unsigned)by<end)break;}
    if(band==13)return 0;
    unsigned factor=1;while(factor<=13 && order[factor-1]!=band)++factor;
    *x=((camera_x-0x3A00)*(int)factor)/64;*y=by;return 1;
}

static void lbz_preview_sample(int wx,int wy,int bx,int by,
                               uint16_t *a,uint8_t *ap,uint16_t *b,uint8_t *bp)
{
    int fx=wx-0x3A00;bx&=511;
    *a=world_attr(s_lbz_preview.world,fx,wy,0);
    *ap=pattern_pixel(&s_lbz_preview.video,*a,fx,wy);
    *b=world_attr(s_lbz_preview.world,bx,by,1);
    *bp=pattern_pixel(&s_lbz_preview.video,*b,bx,by);
}

/* The first incoming room's girder and idle cup assembly share the art
 * already resident in both acts. Render their authored waiting poses only;
 * native activation, movement and collision still begin in the next act. */
static void lbz_preview_sprites(const GVDP *v,int line,uint32_t *out,int width,
                                int origin,int camera_x,int camera_y,const uint32_t *palette,uint8_t *priority)
{
    static const struct { unsigned map,frame,gfx,flip; int x,y; } poses[]={
        {0x2B3AA,0x17,0x2433,0,0x5F0,0x1C0},
        {0x2619A,0,0x440D,0,0x620,0x210},
        {0x2619A,1,0x440D,0,0x648,0x210},
        {0x2619A,2,0x440D,0,0x660,0x210},
        {0x2B3AA,0x14,0x440D,1,0x660,0x1E8},
        {0x2619A,4,0x440D,0,0x660,0x250}
    };
    for(unsigned n=0;n<sizeof poses/sizeof *poses;++n) {
        unsigned map=S3_ART_BANK+poses[n].map;
        unsigned p=map+(int16_t)scene_read16(map+poses[n].frame*2);
        unsigned count=scene_read16(p);p+=2;if(count>16)continue;
        for(unsigned piece=0;piece<count;++piece,p+=6) {
            int size=scene_read8(p+1),cw=((size>>2)&3)+1,ch=(size&3)+1;
            int dx=(int16_t)scene_read16(p+4),dy=(int8_t)scene_read8(p);
            unsigned attr=(scene_read16(p+2)+poses[n].gfx)&65535u;
            if(poses[n].flip&1){dx=-dx-cw*8;attr^=0x800;}
            int y=poses[n].y+dy-camera_y;
            if(line<y || line>=y+ch*8)continue;
            int iy=line-y;if(attr&0x1000)iy=ch*8-1-iy;
            int base=origin+0x3A00+poses[n].x+dx-camera_x;
            for(int i=0;i<cw*8;++i) {
                int x=base+i,wx=camera_x+x-origin;
                if(x<origin+320 || x<0 || x>=width || wx<0x3FE0 || (priority[x]&2))continue;
                int ix=(attr&0x800)?cw*8-1-i:i;
                unsigned cell=(attr&0xE000u)|(((attr&2047u)+(unsigned)(ix/8*ch+iy/8))&2047u);
                uint8_t pixel=pattern_pixel(v,(uint16_t)cell,ix,iy);
                if(!pixel)continue;
                priority[x]|=2;
                if((attr&0x8000) || !(priority[x]&1))out[x]=palette[pixel];
            }
        }
    }
}
