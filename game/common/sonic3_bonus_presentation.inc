/* Native bonus arenas stay finite. Only the installed decorative B plane
 * continues into the margins; the complete native center remains untouched.
 * Both SK bonus B maps contain scenery, not the machine board or actors. */
static void bonus_scanline(const GVDP *v,int line,unsigned zone,const uint32_t *native,int nw,
                           uint32_t *out,int width,const uint32_t *palette,
                           const uint32_t *shadow)
{
    int origin=(width-nw)/2;
    unsigned hsbase=(v->reg[13]&63u)<<10;
    unsigned mode=v->reg[11]&3u,row=mode==0?0:mode==2?(unsigned)line&~7u:(unsigned)line;
    int hs=(int16_t)vram16(v,hsbase+row*4u+2u);
    int wy=line+(v->vsram[1]&1023u);
    unsigned base=(v->reg[4]&7u)<<13;
    for(int x=0;x<width;++x) {
        int nx=x-origin;
        if(nx>=0 && nx<nw) {out[x]=native[nx];continue;}
        if(zone<0x14 || zone>0x15) {out[x]=0xFF000000u;continue;}
        if(!(v->reg[1]&64)) {out[x]=native[0];continue;}
        int wx=nx-hs;
        uint16_t attr=plane_attr(v,base,wx,wy);
        unsigned opaque=pattern_pixel(v,attr,wx,wy),pixel=opaque?opaque:v->reg[7]&63u;
        int dark=(v->reg[12]&8) && !(attr&0x8000u);
        int ext=opaque && !(attr&0x6000u)?presentation_plane_line(attr&2047u):0;
        out[x]=ext?genesis_dac_cram_to_argb(presentation_ext_color(line,ext,pixel&15u),
            dark?GENESIS_DAC_SHADOW:GENESIS_DAC_NORMAL):dark?shadow[pixel]:palette[pixel];
    }
}
