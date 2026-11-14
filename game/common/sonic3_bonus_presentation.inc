/* Finite bonus foregrounds retain their authored geometry. The complete
 * native center remains the hardware composition, including its sprite limits. */
typedef struct BonusHudPiece {int x,y;unsigned size,attr;} BonusHudPiece;
static GVDP s_bonus_hud_free;
static BonusHudPiece s_bonus_hud[12];
static unsigned s_bonus_hud_count;
static uint8_t s_bonus_hud_rows[224];
static void bonus_prepare_hud(const GVDP *v)
{
    s_bonus_hud_count=0;memset(s_bonus_hud_rows,0,sizeof s_bonus_hud_rows);
    memcpy(&s_bonus_hud_free,v,sizeof *v);
    unsigned base=(v->reg[5]&0x7Fu)<<9,index=0;
    for(unsigned n=0;n<80;++n){
        unsigned a=(base+index*8u)&65535u,attr=vram16(v,a+4),tile=attr&2047u;
        int y=(vram16(v,a)&511)-128,x=(vram16(v,a+6)&511)-128;
        unsigned size=v->vram[(a+2)&65535u],height=((size&3u)+1u)*8u;
        /* These are the native bonus HUD's reserved text/digit and life
         * banks. Position guards exclude unrelated offscreen tile users. */
        int hud=x>=0 && x<96 && ((y>=0 && y<24 && tile>=0x6CA && tile<0x700) ||
            (y>=192 && y<224 && tile>=0x7D4 && tile<0x7E0));
        if(hud && s_bonus_hud_count<12){
            s_bonus_hud[s_bonus_hud_count++]=(BonusHudPiece){x,y,size,attr};
            for(int line=y;line<y+(int)height && line<224;++line)
                if(line>=0)s_bonus_hud_rows[line]=1;
            /* Recompose the native underlay on a private VDP. X=0 would
             * mask following hardware sprites; an offscreen Y does not. */
            s_bonus_hud_free.vram[a]=s_bonus_hud_free.vram[(a+1)&65535u]=0;
        }
        index=v->vram[(a+3)&65535u]&127u;if(!index)break;
    }
}
static const uint32_t *bonus_without_hud(const GVDP *v,int line,const uint32_t *native,int nw,
                                        const uint32_t *palette,const uint32_t *shadow)
{
    static uint8_t indices[GVDP_MAX_WIDTH];static uint32_t colors[GVDP_MAX_WIDTH];
    if(line==0)bonus_prepare_hud(v);
    if(line<0 || line>=224 || !s_bonus_hud_rows[line] || nw!=320)return native;
    if(gvdp_render_scanline(&s_bonus_hud_free,line,indices)!=nw)return native;
    for(int x=0;x<nw;++x){
        unsigned p=indices[x];
        colors[x]=p<64?palette[p]:p<128?shadow[p-64]:p<192?
            genesis_dac_cram_to_argb(v->cram[p-128],GENESIS_DAC_HIGHLIGHT):0xFF000000u;
    }
    return colors;
}
static void bonus_draw_hud(const GVDP *v,int line,uint32_t *out,int width,const uint32_t *palette)
{
    for(unsigned n=0;n<s_bonus_hud_count;++n){
        const BonusHudPiece *s=&s_bonus_hud[n];
        unsigned cw=((s->size>>2)&3u)+1u,ch=(s->size&3u)+1u;
        int iy=line-s->y;if(iy<0 || iy>=(int)ch*8)continue;
        if(s->attr&0x1000)iy=(int)ch*8-1-iy;
        for(unsigned i=0;i<cw*8;++i){
            int x=s->x+(int)i;if(x<0 || x>=width)continue;
            unsigned ix=s->attr&0x800?cw*8-1-i:i;
            unsigned tile=((s->attr&2047u)+(ix/8)*ch+(unsigned)iy/8)&2047u;
            unsigned p=pattern_pixel(v,(uint16_t)((s->attr&0xE000u)|tile),(int)(ix&7),iy&7);
            if(p&15u)out[x]=palette[p];
        }
    }
}
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
    int bg_ref_x=(int16_t)((s_world_frame[0xEE8C]<<8)|s_world_frame[0xEE8D]);
    int bg_x=unwrap(-hs,bg_ref_x),bg_y=line+unwrap(v->vsram[1],s_frame_bg_y);
    for(int x=0;x<width;++x) {
        int nx=x-origin;
        if(priority)priority[x]=0;
        if(nx>=0 && nx<nw) {out[x]=native[nx];if(priority)priority[x]=2;continue;}
        /* The bonus ID precedes its layout/decompression. Until the
         * native level-ready flag publishes that world, the margins are
         * the loading card's backdrop, not stale menu words at $8000. */
        if(!s_world_frame[0xF711]){out[x]=palette[v->reg[7]&63u];continue;}
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
