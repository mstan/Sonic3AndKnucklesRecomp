/* The two live digits have no free adjacent tile (dust starts at $7E0).
 * Append the ROM's original ones glyph through the presentation path. */
static unsigned life_nibble(unsigned n,int ppz)
{
    if(ppz && n==12)return 13;
    if(ppz && n==15)return 14;
    return n;
}
static int life_tile_matches(const GVDP *v,unsigned tile,unsigned digit,int ppz)
{
    const uint8_t *p=g_rom+S3_LIFE_DIGITS+digit*32;
    for(unsigned i=0;i<32;++i) {
        unsigned b=life_nibble(p[i]>>4,ppz)*16+life_nibble(p[i]&15,ppz);
        if(v->vram[tile*32+i]!=b)return 0;
    }
    return 1;
}
static void life_ones_scanline(const GVDP *v,const uint8_t *ram,int line,
                               uint32_t *out,int width,int origin)
{
    unsigned count=ram[0xFE12],mode=ram[0xF600]&127;
    if(count<100 || (mode!=8 && mode!=12) || !(v->reg[1]&64))return;
    /* Retail uses one 4x2 name/count piece. Mighty/Shadow use individual
     * decimal pieces. Find their installed positions, including HUD slides. */
    int tx=0,ty=0,ux=0,uy=0,found=0;
    unsigned palette=0,sat=(v->reg[5]&127u)<<9,slot=0;
    for(unsigned visited=0;visited<80;++visited) {
        unsigned p=(sat+slot*8)&65535u,attr=vram16(v,p+4);
        unsigned size=v->vram[(p+2)&65535u]&15u;
        unsigned w=(size>>2)+1,h=(size&3)+1,base=attr&2047u;
        if((attr&0xF800u)==0xA000u && base<=0x7DFu && base+w*h>0x7DDu) {
            for(unsigned x=0;x<w;++x)for(unsigned y=0;y<h;++y) {
                unsigned tile=(base+x*h+y)&2047u;
                if(tile==0x7DDu || tile==0x7DFu) {
                    int px=(vram16(v,p+6)&511)-128+(int)x*8;
                    int py=(vram16(v,p)&511)-128+(int)y*8;
                    if(tile==0x7DDu){tx=px;ty=py;found|=1;}
                    else {ux=px;uy=py;palette=(attr>>9)&48u;found|=2;}
                }
            }
        }
        unsigned next=v->vram[(p+3)&65535u]&127u;
        if(!next || next>=80)break;
        slot=next;
    }
    if(found!=3 || ux!=tx+8 || uy!=ty || line<uy || line>=uy+8)return;
    int ppz=ram[0xFE10]>=0x40 && ram[0xFE10]<=0x43;
    /* An award can precede the native upload. Wait for its first two digits
     * rather than appending a new third digit to an older displayed count. */
    if(!life_tile_matches(v,0x7DD,count/100,ppz) ||
       !life_tile_matches(v,0x7DF,count/10%10,ppz))return;
    const uint8_t *p=g_rom+S3_LIFE_DIGITS+(count%10)*32+(line-uy)*4;
    for(int x=0;x<8;++x) {
        unsigned nib=life_nibble((p[x/2]>>((x&1)?0:4))&15,ppz);
        int dx=origin+ux+8+x;
        if(nib && dx>=0 && dx<width) {
            unsigned index=palette+nib;
            out[dx]=genesis_dac_cram_to_argb(
                (uint16_t)presentation_cram(line,index,v->cram[index]),GENESIS_DAC_NORMAL);
        }
    }
}
