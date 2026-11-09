/* Finite bonus foregrounds retain their authored geometry. The complete
 * native center remains the hardware composition, including its sprite limits. */
static void bonus_scanline(const GVDP *v,int line,unsigned zone,const uint32_t *native,int nw,
                           uint32_t *out,int width,const uint32_t *palette,
                           const uint32_t *shadow,uint8_t *priority)
{
    int origin=(width-nw)/2;
    unsigned hsbase=(v->reg[13]&63u)<<10;
    unsigned mode=v->reg[11]&3u,row=mode==0?0:mode==2?(unsigned)line&~7u:(unsigned)line;
    int hs=(int16_t)vram16(v,hsbase+row*4u+2u);
    int wy=line+(v->vsram[1]&1023u);
    unsigned base=(v->reg[4]&7u)<<13;
    int fg_width=((s_world_frame[0x8000]<<8)|s_world_frame[0x8001])*128;
    int bg_width=((s_world_frame[0x8002]<<8)|s_world_frame[0x8003])*128;
    int fg_ref_x=zone==0x15?((s_world_frame[0xEE98]<<8)|s_world_frame[0xEE99]):s_frame_fg_x;
    int fg_ref_y=zone==0x15?((s_world_frame[0xEE9C]<<8)|s_world_frame[0xEE9D]):s_frame_fg_y;
    int fg_x=unwrap(-(int16_t)vram16(v,hsbase+row*4u),fg_ref_x);
    int fg_y=line+unwrap(v->vsram[0],fg_ref_y);
    int bg_ref_x=(s_world_frame[0xEE8C]<<8)|s_world_frame[0xEE8D];
    int bg_x=unwrap(-hs,bg_ref_x),bg_y=line+unwrap(v->vsram[1],s_frame_bg_y);
    for(int x=0;x<width;++x) {
        int nx=x-origin;
        if(priority)priority[x]=0;
        if(nx>=0 && nx<nw) {out[x]=native[nx];if(priority)priority[x]=2;continue;}
        if(zone<0x13 || zone>0x15) {out[x]=0xFF000000u;continue;}
        if(!(v->reg[1]&64)) {out[x]=native[0];continue;}
        int wx=nx-hs,sy=wy;
        uint16_t attr=plane_attr(v,base,wx,sy);
        /* Pachinko's768px decorative map is wider than the streamed512px
         * name table. Read its authored chunks through the expanded view. */
        if(zone==0x14 && bg_width>0) {
            wx=(nx+bg_x)%bg_width;if(wx<0)wx+=bg_width;sy=bg_y;
            attr=world_attr(s_world_frame,wx,sy,1);
        }
        unsigned opaque=pattern_pixel(v,attr,wx,sy),pixel=opaque?opaque:v->reg[7]&63u;
        int ax=nx+fg_x;
        if(fg_width>0 && ax>=0 && ax<fg_width) {
            uint16_t a=world_attr_blocks_mask(s_world_frame,s_world_frame+0x9000,ax,fg_y,0,
                zone==0x13?0x1Cu:zone==0x15?0x3Cu:0x7Cu);
            unsigned ap=pattern_pixel(v,a,ax,fg_y);
            if(ap && ((a&0x8000u) || !(attr&0x8000u) || !opaque)) {
                attr=a;pixel=opaque=ap;
            }
        }
        if(priority && opaque && (attr&0x8000u))priority[x]|=1;
        int dark=(v->reg[12]&8) && !(attr&0x8000u);
        int ext=opaque && !(attr&0x6000u)?presentation_plane_line(attr&2047u):0;
        out[x]=ext?genesis_dac_cram_to_argb(presentation_ext_color(line,ext,pixel&15u),
            dark?GENESIS_DAC_SHADOW:GENESIS_DAC_NORMAL):dark?shadow[pixel]:palette[pixel];
    }
}
