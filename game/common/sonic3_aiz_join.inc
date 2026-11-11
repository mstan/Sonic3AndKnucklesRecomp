/* The original bombing scene rebases (X,Y) by ($2F00,$80). Treat its two
 * authored maps as neighbors for presentation only. Their chunk IDs are
 * repurposed, so each keeps its own original chunks, blocks and pattern art. */
typedef struct { uint8_t chunks[0x8000],layout[0x1000]; } AizJoinMap;
static AizJoinMap s_aiz_join_map[2];
static AizArt s_aiz_join_second;
static int s_aiz_join_attempted,s_aiz_join_ready;
static AizArt s_aiz_ship_art;
static int s_aiz_ship_art_attempted,s_aiz_ship_art_ready;

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

static int aiz_ship_art_load(void)
{
    if(s_aiz_ship_art_attempted)return s_aiz_ship_art_ready;
    s_aiz_ship_art_attempted=1;
    if(!aiz_join_load())return 0;
    s_aiz_ship_art=s_aiz_join_second;
    size_t used=0;
    if(art_kos(g_rom+S3_ART_BANK+0x1B1372,0x260,
               s_aiz_ship_art.blocks+0xAB8,0x338,&used)!=0x338 ||
       art_kosm(S3_ART_BANK+0x1B48C6,0x922,
                s_aiz_ship_art.video.vram+0x3F80,0x1160)!=0x1160)return 0;
    /* AnimateTiles owns these slots; the compressed scenery contains
     * numbered placeholders. A retired bank keeps the original first
     * animation frame after the live slots are reused by the boss. */
    static const unsigned first[]={0xB3,0xCA,0xD6,0xE2,0xE6};
    static const unsigned count[]={0x17,0xC,0xC,4,0x18};
    static const unsigned source[]={0xA3F00,0xA3600+0x3C*32,
        0xA3600+0x18*32,0xA5680,0xA4A80};
    for(unsigned i=0;i<5;++i)
        memcpy(s_aiz_ship_art.video.vram+first[i]*32,
               g_rom+S3_ART_BANK+source[i],count[i]*32);
    return s_aiz_ship_art_ready=1;
}

static int aiz_cached_tree_sample(int wx,int wy,uint16_t *attr,uint8_t *pixel)
{
    if(!aiz_ship_art_load())return 0;
    *attr=world_attr_blocks(s_world_frame,s_aiz_ship_art.blocks,wx,wy,0);
    *pixel=pattern_pixel(&s_aiz_ship_art.video,*attr,wx,wy);
    return 1;
}
static int aiz_ship_tree_sample(int wx,int wy,uint16_t *attr,uint8_t *pixel)
{
    if(wx<0x4440 || wx>=0x4880)return 0;
    return aiz_cached_tree_sample(wx,wy,attr,pixel);
}
static int aiz_retired_tree_sample(int nx,int nw,int wx,int wy,
                                    uint16_t *attr,uint8_t *pixel)
{
    /* The boss and capsule replace patterns after the native view leaves
     * this scenery. Retain the grove to the left and the finite far-right
     * cliff in their original bank; the native view and active arena stay
     * live. */
    int knux=s_world_frame[0xB038]==2;
    if(s_frame_zone || s_frame_act!=1 || s_frame_fg_x<(knux?0x4100:0x4880) ||
       (nx>=0 && nx<nw))return 0;
    /* Knuckles reaches the same boss art bank at the earlier waterfall.
     * Its preceding grove must retain its own patterns in the left margin. */
    if(knux)return wx>=0x3B80 && wx<0x4100?
        aiz_cached_tree_sample(wx,wy,attr,pixel):0;
    if(wx>=0x4B80 && wx<0x4C80 && aiz_ship_art_load()) {
        return aiz_cached_tree_sample(wx,wy,attr,pixel);
    }
    return aiz_ship_tree_sample(wx,wy,attr,pixel);
}
static unsigned aiz_tree_arrival_alpha(int camera,unsigned phase,uint32_t ship)
{
    /* The native five-row handover hides the incoming grove until the
     * battleship retires. Its wider right margin already sees that grove.
     * Introduce its existing canopy during the last second of the final
     * loop, rather than switching the entire visible tree on in one tick.
     * Do not preview it on an earlier $200 camera loop. */
    if(phase==12)return 255;
    if(phase!=8 || ship<0x3CDC0000u)return 0;
    unsigned ticks=(ship-0x3CDC0000u)/0x8800u+1u;
    if(ticks>=64 || camera+(int)ticks*4>=0x4440)return 0;
    return (64-ticks)*255u/64u;
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
static uint8_t aiz_join_pixel(const GVDP *v,unsigned map,unsigned tag,int x,int y,
                              uint16_t attr)
{
    /* Compressed level art reserves these tiles for AnimateTiles. Those
     * bytes are placeholders, not a valid waterfall/tree frame. Use the
     * installed animation when its bank is live, otherwise the original
     * initial AniPLC frame for the neighboring landscape. This lookup is
     * read-only, including during interpolation and after rewind. */
    static const unsigned first[]={0x2E6,0x2F2,0x2FE,0xB3,0xCA,0xD6,0xE2,0xE6};
    static const unsigned count[]={0xC,0xC,6,0x17,0xC,0xC,4,0x18};
    static const unsigned source[]={0xA2940+0x3C*32,0xA2940+0x18*32,0xA3240,
        0xA3F00,0xA3600+0x3C*32,0xA3600+0x18*32,0xA5680,0xA4A80};
    unsigned tile=attr&2047u;
    for(unsigned i=map?3:0;i<(map?8:3);++i) {
        if(tile<first[i] || tile>=first[i]+count[i])continue;
        const uint8_t *pattern;
        if((!map && tag==1 && s_frame_bg_event<=8) ||
           (map && s_frame_act==1))pattern=v->vram+tile*32;
        else if(map && x<0x1C0 && tile>=0xCA && tile<0xED)
            pattern=g_rom+S3_ART_BANK+0xA5880+(tile-0xCA)*32;
        else pattern=g_rom+S3_ART_BANK+source[i]+(tile-first[i])*32;
        unsigned fx=(unsigned)x&7u,fy=(unsigned)y&7u;
        if(attr&0x800u)fx^=7u;if(attr&0x1000u)fy^=7u;
        unsigned byte=pattern[fy*4+fx/2],nibble=(fx&1)?byte&15u:byte>>4;
        return nibble?(uint8_t)(((attr>>9)&48u)|nibble):0;
    }
    return pattern_pixel(map?&s_aiz_join_second.video:&s_aiz_main.video,attr,x,y);
}
static int aiz_join_sample(const GVDP *v,unsigned tag,int camera,int nx,int x,int y,
                           uint16_t *attr,uint8_t *pixel)
{
    if(!aiz_join_near(tag,camera) || (nx>=0 && nx<320) || !aiz_join_load())return 0;
    /* Before fire replaces the current map, its live chunks include the
     * completed hollow-tree reveal. A cached copy would close the trunk
     * again as the camera approaches the miniboss. Only the finite coast
     * extension needs a cached sample in this phase. */
    if(tag==1 && x<0x3080)return 0;
    /* Other parts of either level keep their live animated/native terrain.
     * Only the neighboring landscape exposed around this join is cached. */
    if(tag==3){x+=0x2F00;y+=0x80;}
    /* Before the bombing has installed the burned bank, the original
     * finite act ends at $3080. Exposing the next act here shows its
     * uninitialized animated art and a prematurely burned landscape.
     * Its coast backdrop is also drawn on A; making A transparent exposes
     * an unrelated forest behind it. Reflect that existing coast strip
     * through the inaccessible cutscene margin. Native player/camera bounds
     * stay intact. The next map joins when fire installs its actual bank. */
    if(tag==1 && x>=0x3080)x=0x307F-(x-0x3080);
    /* Main and burned art are installed before the logical act changes.
     * The native flame/redraw remains authoritative in its original view. */
    unsigned map=(unsigned)(x>=(tag==1?0x3080:0x2F00));
    if(map){x-=0x2F00;y-=0x80;}
    *attr=aiz_join_attr(map,x,y);
    *pixel=aiz_join_pixel(v,map,tag,x,y,*attr);
    return 1;
}
