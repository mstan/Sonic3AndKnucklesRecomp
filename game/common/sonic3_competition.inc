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
    return (mode==8 || mode==12) && active && zone>=14 && zone<=18 &&
        r[0xF711] && width>=3 && width<=256 && row>=0x8088 && row<0x9000 &&
        (mask==4 || mask==12 || mask==28);
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
        competition_margin_sprites(v,line,nw,origin,out,width,palette);
    }
    memcpy(out+origin,native,(size_t)nw*sizeof *out);
    s_native_x=origin;
    if(!line)++s_frames;
    return 1;
}
