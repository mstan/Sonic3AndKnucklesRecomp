/* Competition shares one stage map, but each view owns its line scroll and
 * HBlank-installed VSRAM. Its short layout-row mask also wraps independently
 * of the ordinary 4096px level canvas. Rendering never changes guest state. */
static int competition_world_ready(void)
{
    const uint8_t *r=s_world_frame;
    unsigned mode=r[0xF600]&127u,zone=r[0xFE10];
    unsigned active=(r[S3_COMPETITION]<<8)|r[S3_COMPETITION+1];
    unsigned width=(r[0x8000]<<8)|r[0x8001];
    unsigned row=(r[0x8008]<<8)|r[0x8009];
    unsigned mask=(r[0xEEAE]<<8)|r[0xEEAF];
    /* Pause-B selects the menu before fading the still-installed race.
     * Its canvas remains owned until the original loader clears active/HUD. */
    return (mode==4 || mode==8 || mode==12 || mode==0x28 || mode==0x40) && active && zone>=14 && zone<=18 &&
        r[0xF711] && width>=3 && width<=256 && row>=0x8088 && row<0x9000 &&
        (mask==4 || mask==12 || mask==28);
}
static int competition_object_x(int raw,int radius,int mask)
{
    int x=((raw+radius)&mask)-radius,period=mask+1;
    /* Keep every hardware-visible alias. Outside that viewport, prefer the
     * adjacent signed world position instead of wrapping it to the far side. */
    if(x+radius>=0 && x-radius<320)return x;
    if(raw>=-period/2 && raw<320+period/2)return raw;
    return x>(period+320)/2?x-period:x;
}
/* Capture the original priority queues before either hardware view culls
 * them. The completed paired SAT signatures select this exact publication;
 * no visibility bit, queue, object lifetime or guest coordinate is changed. */
static void competition_capture_objects(void)
{
    SceneFrame *f=s_capture_frame;
    f->count=0;f->scene=4;f->title=f->preview_knux=0;
    f->world_tag=(unsigned)ram16(0xFE10)+1u;f->aiz_join=f->aiz_super_pose=0;
    memset(&f->aiz_plane,0,sizeof f->aiz_plane);
    memset(f->code,0,sizeof f->code);
    f->camera_x=ram16(0xEE80);f->camera_y=ram16(0xEE84);
    f->competition_x=ram16(0xEE68);f->competition_y=ram16(0xEE6C);
    f->competition_page=ram16(0xEF3C)!=0;s_visible_count=0;
    int mask=ram16(0xEEAA),xmask=ram16(0xEEA8);
    for(unsigned view=0;view<2;++view) {
        int cx=view?f->competition_x:f->camera_x,cy=view?f->competition_y:f->camera_y;
        for(unsigned pri=0;pri<8;++pri) {
            unsigned q=0xAC00+pri*128,bytes=g_ram[q+1];if(bytes>126)bytes=126;
            for(unsigned k=2;k<=bytes;k+=2) {
                unsigned at=k;
                if(view && pri==2 && bytes>=4 && ram16(q+2)==0xB000 && ram16(q+4)==0xB04A && k<=4)at=6-k;
                unsigned o=ram16(q+at);if(o<0xB000 || o>=0xCFCC || (o-0xB000)%0x4A || !scene_read32(0xFF0000u+o))continue;
                unsigned flags=g_ram[o+4];
                if(!(flags&4) || (flags&(view?8u:16u)))continue;
                int x=competition_object_x((int16_t)(ram16(o+0x10)-cx),g_ram[o+7],xmask),height=g_ram[o+6];
                int y=((ram16(o+0x14)-cy+height)&mask)-height+(int)view*112;
                s_capture_slot=1+(o-0xB000)/0x4A;
                unsigned map=scene_read32(0xFF0000u+o+0xC),gfx=ram16(o+0xA),frame=g_ram[o+0x22];
                if(flags&64) {
                    if(frame)add_mapping(map,frame,gfx,flags,x,y,10+(int)view,0,256);
                    unsigned count=ram16(o+0x16);if(count>8)count=8;
                    for(unsigned n=0;n<count;++n) {
                        unsigned child=o+0x18+n*6;
                        int sx=competition_object_x((int16_t)(ram16(child)-cx),128,xmask),sy=((ram16(child+2)-cy+128)&mask)-128+(int)view*112;
                        add_mapping(map,g_ram[child+5],gfx,flags,sx,sy,10+(int)view,0,256);
                    }
                } else add_mapping(map,frame,gfx,flags,x,y,10+(int)view,(flags&32)!=0,256);
            }
        }
    }
    s_capture_slot=0;
}
static int competition_scene_sprites(const GVDP *v,int line,int nw,int origin,
                                     uint32_t *out,int width,const uint32_t *palette,int ha)
{
    const SceneFrame *f=s_display;
    if(!f || f->scene!=4 || f->world_tag!=(unsigned)((s_world_frame[0xFE10]<<8)|s_world_frame[0xFE11])+1u)return 0;
    unsigned base=(v->reg[5]&127u)<<9;int same[2]={1,1};
    for(unsigned n=0;n<640;++n) {
        unsigned p=v->vram[(base+n)&65535u];
        if(p!=f->sat[n])same[0]=0;
        if(p!=f->competition_sat[n])same[1]=0;
        if(!same[0] && !same[1])return 0;
    }
    /* Identical empty native tables can hide different margin-only actors.
     * The original HBlank switches at row108; lower sprite Y includes112. */
    unsigned view=same[1] && (!same[0] || line>=108);
    int cx=view?f->competition_x:f->camera_x,cy=view?f->competition_y:f->camera_y;
    int mask=(s_world_frame[0xEEAA]<<8)|s_world_frame[0xEEAB];
    int dx=cx+(int16_t)ha,live_y=(v->vsram[0]+view*112)&mask;
    int dy=((cy-live_y+(mask+1)/2)&mask)-(mask+1)/2;
    for(unsigned n=0;n<f->count;++n) {
        const SceneSprite *s=&f->sprites[n];if(s->hud!=10+view)continue;
        int wt=((s->size>>2)&3)+1,ht=(s->size&3)+1,y=line-s->y-dy;
        if(y<0 || y>=ht*8)continue;
        if(s->attr&0x1000)y=ht*8-1-y;
        unsigned bytes;const uint8_t *patterns=presentation_sprite_patterns(s->size,&bytes);
        for(int x=0;x<wt*8;++x) {
            int dest=origin+s->x+dx+x,nx=dest-origin;
            if(dest<0 || dest>=width || (nx>=0 && nx<nw) || (s_priority[dest]&2))continue;
            int ix=(s->attr&0x800)?wt*8-1-x:x;
            unsigned tile=((s->attr&2047u)+(unsigned)(ix>>3)*(unsigned)ht+(unsigned)(y>>3))&2047u;
            uint16_t cell=(uint16_t)((s->attr&0xE000u)|tile);unsigned pixel;
            if(patterns) {
                unsigned a=tile*32u+(unsigned)(y&7)*4u+(unsigned)(ix&7)/2u;
                unsigned byte=a<bytes?patterns[a]:0;pixel=(ix&1)?byte&15:byte>>4;
                if(pixel)pixel|=(cell>>9)&48;
            } else pixel=pattern_pixel(v,(uint16_t)((cell&0x6000u)|tile),ix,y);
            if(!pixel)continue;
            s_priority[dest]|=2;
            int ext=presentation_sprite_line(s->size,tile,pixel>>4);
            uint32_t color=ext?genesis_dac_cram_to_argb(presentation_ext_color(line,ext,pixel&15),GENESIS_DAC_NORMAL):palette[pixel];
            if((cell&0x8000u) || !(s_priority[dest]&1))out[dest]=color;
        }
    }
    return 1;
}
static void competition_margin_sprites(const GVDP *v,int line,int nw,int origin,
                                      uint32_t *out,int width,const uint32_t *palette)
{
    unsigned base=(v->reg[5]&127u)<<9,slot=0;
    for(unsigned count=0;count<80;++count) {
        unsigned a=(base+slot*8)&65535u;
        int sy=(int)(vram16(v,a)&511u)-128;
        unsigned shape=v->vram[(a+2)&65535u]&15u;
        int wt=(int)(shape>>2)+1,ht=(int)(shape&3)+1;
        uint16_t attr=vram16(v,a+4);
        int sx=(int)(vram16(v,a+6)&511u)-128,y=line-sy;
        if(y>=0 && y<ht*8)for(int x=0;x<wt*8;++x) {
            int nx=sx+x,dx=origin+nx;
            if(dx<0 || dx>=width || (nx>=0 && nx<nw) || (s_priority[dx]&2))continue;
            int fx=(attr&0x800u)?wt*8-1-x:x,fy=(attr&0x1000u)?ht*8-1-y:y;
            unsigned tile=((attr&2047u)+(unsigned)(fx>>3)*(unsigned)ht+(unsigned)(fy>>3))&2047u;
            unsigned p=pattern_pixel(v,(uint16_t)((attr&0x6000u)|tile),fx,fy);
            if(!p)continue;
            s_priority[dx]|=2;
            if((attr&0x8000u) || !(s_priority[dx]&1))out[dx]=palette[p];
        }
        slot=v->vram[(a+3)&65535u]&127u;
        if(!slot)break;
    }
}
static int competition_scanline(const GVDP *v,int line,const uint32_t *native,int nw,
                               uint32_t *out,int width,GameVideoNativeRow native_row,
                               void *native_ctx,const uint32_t *palette)
{
    if(!competition_world_ready() || (nw!=320 && nw!=256) || width<nw ||
       (v->reg[12]&14) || s_priority_capacity<width)return 0;
    if(!native)native=native_row(native_ctx);
    int origin=(width-nw)/2;
    if(!(v->reg[1]&64)) {
        for(int x=0;x<width;++x)out[x]=native[0];
    } else {
        unsigned hsbase=(v->reg[13]&63u)<<10;
        int mode=v->reg[11]&3,hi=mode==0?0:mode==2?line&~7:line;
        int ha=(int16_t)vram16(v,hsbase+(unsigned)hi*4);
        int hb=(int16_t)vram16(v,hsbase+(unsigned)hi*4+2);
        int ay=line+(v->vsram[0]&1023),by=line+(v->vsram[1]&1023);
        unsigned mask=(s_world_frame[0xEEAE]<<8)|s_world_frame[0xEEAF];
        int fg_width=((s_world_frame[0x8000]<<8)|s_world_frame[0x8001])*128;
        int bg_width=((s_world_frame[0x8002]<<8)|s_world_frame[0x8003])*128;
        for(int x=0;x<width;++x) {
            int nx=x-origin,ax=nx-ha,bx=nx-hb;
            if(bg_width>0){bx%=bg_width;if(bx<0)bx+=bg_width;}
            uint16_t a=world_attr_blocks_mask(s_world_frame,s_world_frame+0x9000,ax,ay,0,mask);
            uint16_t b=world_attr_blocks_mask(s_world_frame,s_world_frame+0x9000,bx,by,1,mask);
            unsigned ap=ax>=0 && ax<fg_width?pattern_pixel(v,a,ax,ay):0;
            unsigned bp=bg_width>0?pattern_pixel(v,b,bx,by):0;
            int foreground=ap && ((a&0x8000u) || !bp || !(b&0x8000u));
            out[x]=foreground?plane_color(line,a,ap,palette):bp?plane_color(line,b,bp,palette):palette[v->reg[7]&63u];
            s_priority[x]=(uint8_t)(((a&0x8000u)&&ap)||((b&0x8000u)&&bp));
        }
        if(!competition_scene_sprites(v,line,nw,origin,out,width,palette,ha))
            competition_margin_sprites(v,line,nw,origin,out,width,palette);
    }
    memcpy(out+origin,native,(size_t)nw*sizeof *out);
    s_native_x=origin;
    if(!line)++s_frames;
    return 1;
}
