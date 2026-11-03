/* The original bombing scene rebases (X,Y) by ($2F00,$80). Treat its two
 * authored maps as neighbors for presentation only. Their chunk IDs are
 * repurposed, so each keeps its own original chunks, blocks and pattern art. */
typedef struct { uint8_t chunks[0x8000],layout[0x1000]; } AizJoinMap;
static AizJoinMap s_aiz_join_map[2];
static AizArt s_aiz_join_second;
static int s_aiz_join_attempted,s_aiz_join_ready;

static int aiz_join_load(void)
{
    if(s_aiz_join_attempted)return s_aiz_join_ready;
    s_aiz_join_attempted=1;load_aiz_art();
    if(!s_aiz_art_ready)return 0;
    size_t used=0;
    if(art_kos(g_rom+S3_ART_BANK+0x1ABC40,0x3990,s_aiz_join_map[0].chunks,0x8000,&used)!=0x8000 ||
       art_kos(g_rom+S3_ART_BANK+0x1B51E8,0x4010,s_aiz_join_map[1].chunks,0x8000,&used)!=0x8000 ||
       art_kos(g_rom+S3_ART_BANK+0x1B0052,0x8A0,s_aiz_join_second.blocks,0xAB8,&used)!=0xAB8 ||
       art_kos(g_rom+S3_ART_BANK+0x1B08F2,0xA80,s_aiz_join_second.blocks+0xAB8,0xCE8,&used)!=0xCE8 ||
       art_kosm(S3_ART_BANK+0x1B15D2,0x21B2,s_aiz_join_second.video.vram,0x3F80)!=0x3F80 ||
       art_kosm(S3_ART_BANK+0x1B3784,0x1A64,s_aiz_join_second.video.vram+0x3F80,0x1DA0)!=0x1DA0)return 0;
    memcpy(s_aiz_join_map[0].layout,g_rom+S3_ART_BANK+0x81420,0x1000);
    memcpy(s_aiz_join_map[1].layout,g_rom+S3_ART_BANK+0x81C6C,0x1000);
    for(unsigned n=0;n<2;++n) {
        const uint8_t *p=s_aiz_join_map[n].layout;
        if(((p[0]<<8)|p[1])!=(n?153:97))return 0;
        for(unsigned row=0;row<32;++row) {
            unsigned a=(p[8+row*4]<<8)|p[9+row*4];
            if(a && (a<0x8088 || a>0x9000u-(n?153u:97u)))return 0;
        }
    }
    return s_aiz_join_ready=1;
}

static unsigned aiz_join_tag(const uint8_t *ram)
{
    if(ram[0xFE10] || ram[0xFE11]>1 || (ram[0xF600]&127)!=12)return 0;
    unsigned w=(ram[0x8000]<<8)|ram[0x8001];
    if(w!=(ram[0xFE11]?153u:97u))return 0;
    return ram[0xFE11]?3u:((ram[0xEEC2]<<8)|ram[0xEEC3])>=0x10?2u:1u;
}
static int aiz_join_left(int camera,int width,unsigned tag,int physical_width)
{
    int left=camera-(width-320)/2,end=physical_width-width;
    if(tag && tag<=3 && width>320 && aiz_join_load()) {
        int offset=tag==3?0x2F00:0;
        left+=offset;end=0x2F00+0x4C80-width;
        if(end<0)left=end/2;else {if(left<0)left=0;if(left>end)left=end;}
        return left-offset;
    }
    if(end<0)return end/2;
    if(left<0)left=0;
    return left>end?end:left;
}
static uint16_t aiz_join_attr(unsigned map,int x,int y)
{
    const AizJoinMap *m=&s_aiz_join_map[map];
    const AizArt *art=map?&s_aiz_join_second:&s_aiz_main;
    unsigned width=((m->layout[0]<<8)|m->layout[1])*128;
    if(x<0 || (unsigned)x>=width || y<0)return 0;
    unsigned row=((unsigned)y>>5)&124u;
    unsigned a=(m->layout[8+row]<<8)|m->layout[9+row];
    if(a<0x8088 || a+(unsigned)(x>>7)>=0x9000)return 0;
    unsigned p=m->layout[a-0x8000+(unsigned)(x>>7)]*128u+
        ((unsigned)y&112u)+((unsigned)x&112u)/8u;
    unsigned block=(m->chunks[p]<<8)|m->chunks[p+1];
    unsigned tx=((unsigned)x>>3)&1u,ty=((unsigned)y>>3)&1u;
    if(block&0x400)tx^=1;
    if(block&0x800)ty^=1;
    p=(block&1023u)*8u+(ty*2+tx)*2;
    if(p+1>=sizeof art->blocks)return 0;
    return (uint16_t)(((art->blocks[p]<<8)|art->blocks[p+1])^((block&0xC00u)<<1));
}
static int aiz_join_near(unsigned tag,int camera)
{
    return tag && tag<=3 && (tag==3?camera<0x300:camera>=0x2E00);
}
static int aiz_join_sample(unsigned tag,int camera,int nx,int x,int y,
                           uint16_t *attr,uint8_t *pixel)
{
    if(!aiz_join_near(tag,camera) || (nx>=0 && nx<320) || !aiz_join_load())return 0;
    /* Other parts of either level keep their live animated/native terrain.
     * Only the neighboring landscape exposed around this join is cached. */
    if(tag==3){x+=0x2F00;y+=0x80;}
    /* Main and burned art are installed before the logical act changes.
     * The native flame/redraw remains authoritative in its original view. */
    unsigned map=(unsigned)(x>=(tag==1?0x3080:0x2F00));
    if(map){x-=0x2F00;y-=0x80;}
    *attr=aiz_join_attr(map,x,y);
    *pixel=pattern_pixel(map?&s_aiz_join_second.video:&s_aiz_main.video,*attr,x,y);
    return 1;
}
