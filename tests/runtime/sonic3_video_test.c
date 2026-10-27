/* Test the actual renderer's row-pointer layout and host publication helpers. */
#include "../../game/common/sonic3_video.c"
uint8_t g_ram[65536],g_rom[0x400000];
uint8_t *g_rom_ext;
uint32_t g_rom_ext_size;
int gvdp_ext_line(unsigned tile) { (void)tile; return 0; }
static uint8_t test_atlas[128];
static int test_atlas_enabled;
const uint8_t *gvdp_sprite_patterns(unsigned flags,unsigned *bytes) {
    *bytes=test_atlas_enabled&&(flags&128)?sizeof test_atlas:0;
    return *bytes?test_atlas:NULL;
}
int gvdp_sprite_pattern_line(unsigned flags,unsigned tile) { (void)flags;return gvdp_ext_line(tile); }
uint16_t gvdp_ext_color(int row,int line,int nib) { (void)row;(void)line;(void)nib;return 0; }
M68KState g_cpu;
void m68k_write8(uint32_t a,uint8_t v){g_ram[a&65535]=v;}
void m68k_write16(uint32_t a,uint16_t v){m68k_write8(a,v>>8);m68k_write8(a+1,v);}
void m68k_write32(uint32_t a,uint32_t v){m68k_write16(a,v>>16);m68k_write16(a+2,v);}
/* Host-side writes from the video adapter (glue_poke*, genesis_host_mem.h). */
void glue_poke8(uint32_t a,uint8_t v){m68k_write8(a,v);}
void glue_poke16(uint32_t a,uint16_t v){m68k_write16(a,v);}
void glue_poke32(uint32_t a,uint32_t v){m68k_write32(a,v);}
void cmd_send_response(const char *json){(void)json;}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void word(uint8_t *p,unsigned a,unsigned v){p[a]=(uint8_t)(v>>8);p[a+1]=(uint8_t)v;}
static void longword(uint8_t *p,unsigned a,unsigned v){word(p,a,v>>16);word(p,a+2,v);}
static unsigned native_row_calls;
static const uint32_t *test_native_row(void *row){++native_row_calls;return row;}
static void test_sprite_atlas(void)
{
    GVDP v={0};SceneFrame scene={0};uint32_t out[64],pal[64]={0};
    /* Distinct four tiles prove column ordering and both flips. VRAM has
     * different pixels so an accidental fallback cannot satisfy this. */
    for(unsigned t=0;t<4;t++){memset(test_atlas+t*32,(t+1)*17,32);pal[t+1]=0xFF000001+t;}
    memset(v.vram,0x55,128);pal[5]=0xFF000005;
    scene.scene=1;scene.count=1;scene.sprites[0]=(SceneSprite){8,0,0x8000,0x85,0};
    const SceneFrame *previous=s_display;s_display=&scene;test_atlas_enabled=1;s_camera=s_camera_y=0;
    for(unsigned flip=0;flip<4;flip++)for(int y=0;y<16;y++) {
        scene.sprites[0].attr=(uint16_t)(0x8000|((flip&1)?0x800:0)|((flip&2)?0x1000:0));
        memset(out,0,sizeof out);memset(s_priority,0,64);
        draw_scene_sprites(&v,y,out,64,0,pal);
        for(int x=0;x<16;x++) {
            int ix=(flip&1)?15-x:x,iy=(flip&2)?15-y:y;
            CHECK(out[8+x]==pal[1+ix/8*2+iy/8]);
        }
    }
    /* Miniature characters keep their own atlas, tile ordering and flips.
     * Sampling at half size must consume every second source pixel. */
    scene.sprites[0].zoom=128;
    for(unsigned flip=0;flip<4;flip++)for(int y=0;y<8;y++) {
        scene.sprites[0].attr=(uint16_t)(0x8000|((flip&1)?0x800:0)|((flip&2)?0x1000:0));
        memset(out,0,sizeof out);memset(s_priority,0,64);
        draw_scene_sprites(&v,y,out,64,0,pal);
        for(int x=0;x<8;x++) {
            int ix=(flip&1)?15-x*2:x*2,iy=(flip&2)?15-y*2:y*2;
            CHECK(out[8+x]==pal[1+ix/8*2+iy/8]);
        }
        CHECK(!out[7] && !out[16]);
    }
    memset(out,0,sizeof out);memset(s_priority,0,64);
    draw_scene_sprites(&v,8,out,64,0,pal);CHECK(!out[8]);
    scene.sprites[0].zoom=0;
    scene.sprites[0].size=5;scene.sprites[0].attr=0x8000;
    memset(s_priority,0,64);draw_scene_sprites(&v,0,out,64,0,pal);CHECK(out[8]==pal[5]);
    scene.sprites[0].size=0x85;scene.sprites[0].attr=0x8004;
    memset(out,0,sizeof out);memset(s_priority,0,64);
    draw_scene_sprites(&v,0,out,64,0,pal);CHECK(!out[8]); /* atlas bounds */
    test_atlas_enabled=0;s_display=previous;
}
static unsigned zoom_fixture(unsigned object) { (void)object;return 128; }
static void test_object_zoom(void)
{
    memset(g_ram,0,sizeof g_ram);CHECK(configure("off"));
    s3_video_set_object_zoom(zoom_fixture);
    CHECK(enabled() && width(2560,720,320,224)==320);
    CHECK(s3_video_main_cpu_divisor()==1); /* no widescreen CPU policy */
    word(g_rom,0x6000,2);word(g_rom,0x6002,1);
    word(g_rom,0x6004,0xF005);word(g_rom,0x6006,0);word(g_rom,0x6008,0xFFF0);
    s_build.count=0;add_mapping(0x6000,0,0,0,32,32,0,0,128);
    CHECK(s_build.count==1 && s_build.sprites[0].x==24 && s_build.sprites[0].y==24);
    CHECK(s_build.sprites[0].zoom==128);
    s3_video_set_object_zoom(NULL);CHECK(!enabled());
}
static void test_file_select(void)
{
    static GVDP v;
    static uint8_t before[65536];
    uint32_t native[320],out[1792];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("fit"));
    g_ram[0xF600]=0x4C;v.reg[1]=64;v.reg[2]=0x38;v.reg[4]=6;
    v.reg[3]=0x38;v.reg[18]=0x80;
    v.reg[5]=0x7C;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=3;
    v.cram[1]=0xE;v.cram[2]=0xE0;v.cram[3]=0xE00;v.cram[4]=0xEEE;
    for(unsigned tile=1;tile<=4;++tile)memset(v.vram+tile*32,tile*17,32);
    for(unsigned row=0;row<2;++row) {
        for(unsigned col=0;col<40;++col)word(v.vram,0xE000+row*128+col*2,col<20?1:2);
        word(v.vram,0xC000+row*256+2,0x8003);
        word(v.vram,0xC000+row*256+252,0x8003);
    }
    for(unsigned x=0;x<320;++x)native[x]=0xFF123456;
    /* Capture a native-queued multi-sprite crossing the right edge and its
     * offscreen child; no SAT wrapping and no extra writes to guest state. */
    write32(0xB172,0x1234);write32(0xB17E,S3_FILE_SELECT_MAP);g_ram[0xB176]=64;
    word(g_ram,0xB182,128+316);word(g_ram,0xB186,128);g_ram[0xB194]=1;
    word(g_ram,0xB188,1);word(g_ram,0xB18A,128+330);word(g_ram,0xB18C,128);g_ram[0xB18F]=1;
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB172);
    word(g_rom,S3_FILE_SELECT_MAP+2,4);word(g_rom,S3_FILE_SELECT_MAP+4,1);word(g_rom,S3_FILE_SELECT_MAP+6,0);
    word(g_rom,S3_FILE_SELECT_MAP+8,0x8004);word(g_rom,S3_FILE_SELECT_MAP+10,0);
    memcpy(before,g_ram,sizeof before);capture_objects();
    CHECK(s_build.count==2 && s_build.sprites[0].hud==4 && s_build.sprites[1].x==330);
    publish_sprites();CHECK(!memcmp(before,g_ram,sizeof before));
    memcpy(v.vram+0xF800,g_ram+0xF800,640);
    /* Native exit changes mode before fading this same installed screen. */
    g_ram[0xF600]=4;before[0xF600]=4;
    const int widths[]={320,398,523,796,1792};
    for(unsigned w=0;w<sizeof widths/sizeof widths[0];++w)for(int scroll=0;scroll<=704;scroll+=352) {
        int canvas=widths[w],origin=(canvas-320)/2;
        word(v.vram,0xF002,(unsigned)-scroll);
        scanline(&v,0,native,320,out,canvas);
        for(int x=0;x<canvas;++x) {
            int nx=x-origin;
            if(nx>=0 && nx<320)CHECK(out[x]==native[nx]);
            if((nx>=320 && nx<324)||(nx>=330 && nx<338))CHECK(out[x]==0xFFFFFFFF);
        }
        scanline(&v,8,native,320,out,canvas);
        for(int x=0;x<canvas;++x) {
            int nx=x-origin,card=nx+scroll;
            /* Alternating 320px reflected half-panels: no black seams. */
            int band=(nx+1600)/160;
            uint32_t bg=(band%4==1 || band%4==2)?0xFFFF0000:0xFF00FF00;
            uint32_t expected=nx>=0 && nx<320?native[nx]:
                ((card>=8 && card<16)||(card>=1008 && card<1016))?0xFF0000FF:bg;
            CHECK(out[x]==expected);
        }
    }
    CHECK(!memcmp(before,g_ram,sizeof before));
}
static int extended_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    if(ram[0xFE10]!=0x34)return 0;
    CHECK(x==4096 && y==128 && background==1);
    *cell=0xC03;
    return 1;
}
static unsigned snapshot_calls;
static int snapshot_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)x;(void)y;
    /* These addresses are outside the old, truncated terrain-only copy.
     * Check a distinct byte near its far end as well as the stage identity. */
    CHECK(ram[0xFE10]==0x34 && ram[0xFFF0]==0xA5);
    ++snapshot_calls;*cell=background?1:0;return 1;
}
static void test_world_snapshot(void)
{
    static GVDP v;uint32_t native[320]={0},out[398];
    CHECK(configure("off"));CHECK(configure("fit"));width(398,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xF711]=1;
    g_ram[0xFE10]=0x34;g_ram[0xFFF0]=0xA5;
    word(g_ram,0x8000,16);word(g_ram,0x8002,4);
    word(g_ram,0x8008,0x8100);word(g_ram,0x800A,0x8140);
    for(unsigned i=0;i<4;i++)word(g_ram,0x9008+i*2,1);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=0xE;
    memset(v.vram+32,0x11,32);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    s3_video_set_world_cell(snapshot_cell);snapshot_calls=0;
    scanline(&v,0,native,320,out,398);
    CHECK(snapshot_calls && out[0]==0xFFFF0000);
    /* A later scanline uses the same published RAM, even if live RAM moves
     * into another act while that frame is being displayed. */
    g_ram[0xFE10]=0x20;g_ram[0xFFF0]=0;
    scanline(&v,1,native,320,out,398);CHECK(out[0]==0xFFFF0000);
    uint32_t lazy[398];native_row_calls=0;
    scanline_lazy(&v,1,320,lazy,398,test_native_row,native);
    CHECK(!native_row_calls && !memcmp(out,lazy,sizeof out));
    /* An unsupported mode must fetch the original row exactly once and
     * produce the same centered image as the eager entry point. */
    v.reg[12]=7;scanline(&v,0,native,320,out,398);native_row_calls=0;
    scanline_lazy(&v,0,320,lazy,398,test_native_row,native);
    CHECK(native_row_calls==1 && !memcmp(out,lazy,sizeof out));
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static unsigned hud_fixture(void) { return 0x3000; }
static int cd_edge_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)y;
    *cell=background?(uint16_t)(1+((x>>4)&1)):0;
    return 1;
}
static void test_cd_background_edges(void)
{
    static GVDP v;uint32_t native[320]={0},out[796],baseline[796];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("16:9"));g_ram[0xF600]=12;write32(0xB000,0x1234);
    word(g_ram,0x8000,64);word(g_ram,0x8002,4);word(g_ram,0x8008,0x8100);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE;v.cram[2]=0xE0;
    memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
    for(unsigned i=0;i<4;i++){word(g_ram,0x9008+i*2,1);word(g_ram,0x9010+i*2,2);}
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    s3_video_set_world_cell(cd_edge_cell);
    const int widths[]={320,398,523,796},cameras[]={0,8,16,31,39,40,56,100,7600,7800,7833,7872};
    const unsigned zones[]={0x40,0x5B};
    for(unsigned z=0;z<2;z++)for(unsigned w=0;w<4;w++) {
        g_ram[0xFE10]=(uint8_t)zones[z];int canvas=widths[w];
        word(v.vram,0xF002,0); /* fixed game background: only foreground camera moves */
        for(unsigned c=0;c<sizeof cameras/sizeof cameras[0];c++) {
            int camera=cameras[c];word(g_ram,0xEE80,camera);word(v.vram,0xF000,(unsigned)-camera);
            scanline(&v,0,native,320,out,canvas);
            if(!c)memcpy(baseline,out,(size_t)canvas*sizeof *out);
            else CHECK(!memcmp(baseline,out,(size_t)canvas*sizeof *out));
            CHECK(s_left==view_left(camera,canvas) && s_native_x==camera-s_left);
            CHECK(s3_video_presentation_camera_x(camera)==s_left+(canvas-320)/2);
        }
        /* Genuine line-scroll movement remains visible at the edge. */
        word(g_ram,0xEE80,0);word(v.vram,0xF000,0);word(v.vram,0xF002,(unsigned)-8);
        scanline(&v,0,native,320,out,canvas);
        for(int x=0;x<canvas-8;x++)CHECK(out[x]==baseline[x+8]);
    }
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_hud_mapping(void)
{
    memset(g_ram,0,sizeof g_ram);write32(0xB000,0x123456);
    g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xFE05]=8;
    s3_video_set_ring_layout(NULL);word(g_rom,0x1000,65535);
    longword(g_rom,S3_RINGS,0x1000);
    const unsigned maps[]={S3_HUD_MAP,0x3000};
    for(unsigned i=0;i<2;i++) {
        unsigned p=maps[i];word(g_rom,p,12);word(g_rom,p+12,1);
        word(g_rom,p+14,i?12:0);word(g_rom,p+16,i?2:1);word(g_rom,p+18,0);
    }
    s3_video_set_hud_mapping(hud_fixture);capture_objects();
    CHECK(s_build.count==1 && s_build.sprites[0].hud==1);
    CHECK(s_build.sprites[0].size==12 && s_build.sprites[0].attr==0x86CC);
    s3_video_set_hud_mapping(NULL);capture_objects();
    CHECK(s_build.count==1 && !s_build.sprites[0].size && s_build.sprites[0].attr==0x86CB);
}
int main(int argc,char **argv)
{
    if(argc==2) {
        FILE *rom=fopen(argv[1],"rb");CHECK(rom);
        CHECK(fread(g_rom,1,sizeof g_rom,rom)==S3_ART_BANK+0x200000u);fclose(rom);
        load_aiz_art();CHECK(s_aiz_art_ready);
        const uint8_t *sources[]={s_aiz_intro.blocks,s_aiz_main.blocks,s_aiz_intro.video.vram,s_aiz_main.video.vram};
        const unsigned sizes[]={0x1800,0x1800,0x10000,0x10000};
        const unsigned expected[]={0x6994E873u,0x81D9433Bu,0x0D697F52u,0xFF50471Du};
        for(unsigned j=0;j<4;++j) {
            unsigned hash=2166136261u;
            for(unsigned k=0;k<sizes[j];++k)hash=(hash^sources[j][k])*16777619u;
            printf("AIZ asset %u: %08x\n",j,hash);
            CHECK(hash==expected[j]);
        }
        printf("palette %04x %04x\n",s_aiz_intro.colors[2],s_aiz_main.colors[2]);
        return 0;
    }
    test_cd_background_edges();
    /* Bounded host decoding: literals, overlapping dictionary matches,
     * truncation, a reference before the output and insufficient capacity. */
    const uint8_t compressed[]={0xAF,0,'A','B','C','D',0xFC,0xFA,0,0xF8,0};
    uint8_t decoded[8];size_t used=0;
    CHECK(art_kos(compressed,sizeof compressed,decoded,sizeof decoded,&used)==8);
    CHECK(used==sizeof compressed && !memcmp(decoded,"ABCDABCD",8));
    CHECK(!art_kos(compressed,sizeof compressed-1,decoded,sizeof decoded,&used));
    CHECK(!art_kos(compressed,sizeof compressed,decoded,7,&used));
    const uint8_t invalid[]={2,0,0xFF,0xFA};
    CHECK(!art_kos(invalid,sizeof invalid,decoded,sizeof decoded,&used));
    CHECK(!enabled());CHECK(!width(2560,720,320,224));CHECK(s3_video_main_cpu_divisor()==1);
    write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xF711]=1;
    word(g_ram,0x8000,16);word(g_ram,0x8002,4);
    CHECK(stage_width()==2048);CHECK(configure("fit"));CHECK(width(4000,500,320,224)==1792);
    CHECK(s3_video_main_cpu_divisor()==4);
    g_ram[S3_COMPETITION+1]=1;CHECK(s3_video_main_cpu_divisor()==1);g_ram[S3_COMPETITION+1]=0;
    g_ram[0xF600]=0x8C;CHECK(s3_video_main_cpu_divisor()==1);g_ram[0xF600]=12;
    CHECK(configure("16:9"));CHECK(width(1,1,320,224)==398);
    CHECK(configure("21:9"));CHECK(width(1,1,320,224)==523);
    CHECK(configure("32:9"));CHECK(width(1,1,320,224)==796);
    CHECK(configure("64:9"));CHECK(width(1,1,320,224)==1593);
    CHECK(configure("stage"));CHECK(width(1,1,320,224)==2048);
    CHECK(!configure("32:0"));CHECK(!configure("nan:9"));CHECK(!configure("inf:1"));
    word(g_ram,0x8008,0x8100);word(g_ram,0x800A,0x8140);g_ram[0x8100]=g_ram[0x8140]=1;
    CHECK(level_ready());g_ram[0xF711]=0;CHECK(level_ready());
    g_ram[0xF600]=0x8C;CHECK(!level_ready());g_ram[0xF600]=12;g_ram[0xF711]=1;
    word(g_ram,0x8008,0);CHECK(!level_ready());word(g_ram,0x8008,0x8100);
    word(g_ram,128,3);for(unsigned n=0;n<4;++n)word(g_ram,0x9018+n*2,100+n);
    CHECK(world_attr(g_ram,0,0,0)==100);CHECK(world_attr(g_ram,8,8,0)==103);
    CHECK(world_attr(g_ram,0,0,1)==100);CHECK(world_attr(g_ram,2048,0,0)==0);
    /* Native Get_ChunkRow addresses signed guard chunks before a BG row,
     * rather than wrapping to the last chunk of that row. */
    g_ram[0x813F]=1;word(g_ram,128+14,3);
    CHECK(world_attr(g_ram,-1,0,1)==101);CHECK(world_attr(g_ram,-1,0,0)==0);
    word(g_ram,128,0xC03);CHECK(world_attr(g_ram,0,0,0)==(103^0x1800));
    /* Expansion terrain uses the same block flips and palette/priority bits
     * as native cells, and checks the published frame's stage identity. */
    s3_video_set_world_cell(extended_cell);g_ram[0xFE10]=0x34;
    CHECK(world_attr(g_ram,4096,128,1)==(103^0x1800));
    g_ram[0xFE10]=0x20;CHECK(world_attr(g_ram,0,0,0)==(103^0x1800));
    s3_video_set_world_cell(NULL);g_ram[0xFE10]=0;
    /* Sorted placements, native respawn pointers and matching activation cells. */
    longword(g_rom,S3_PLACEMENTS,0x100000);word(g_rom,0x100000,640);word(g_rom,0x100002,0x8060);
    g_rom[0x100004]=1;word(g_rom,0x100006,1000);word(g_rom,0x100008,0xE060);
    g_rom[0x10000A]=2;word(g_rom,0x10000C,65535);
    write32(0xEF5A,0x110000);longword(g_rom,0x110004,0x123456);longword(g_rom,0x110008,0x234567);
    longword(g_rom,0x5CC9A,0x123456);longword(g_rom,0x5CC9E,0x234567);
    CHECK(configure("10:7"));width(1,1,320,224);
    s3_video_hook(S3_LOAD_INIT);g_ram[0xF76C]=4;write16(0xEE78,1);
    CHECK(s3_video_hook(S3_LOAD_UPDATE));CHECK(!ram16(0xB0DE));
    write16(0xEE78,128);CHECK(s3_video_hook(S3_LOAD_UPDATE));
    CHECK(scene_read32(0xFFB0DE)==0x123456 && ram16(0xB0EE)==640 && ram16(0xB126)==0xEB00);
    g_cpu.D[0]=640-ram16(0xF7DA);s3_video_hook(0x1B594);CHECK((uint16_t)g_cpu.D[0]<=640);
    CHECK(configure("32:9"));width(1,1,320,224);s3_video_hook(S3_LOAD_UPDATE);
    CHECK(scene_read32(0xFFB128)==0x234567 && g_ram[0xB12C]==3 && g_ram[0xB152]==3);
    /* AIZ's art-loading miniboss cutscene must not enter the expanded
     * activation margin: its VRAM bank still belongs to a stage PLC. */
    word(g_rom,0x10000C,0x3020);word(g_rom,0x10000E,0x8060);g_rom[0x100010]=3;
    word(g_rom,0x100012,65535);
    longword(g_rom,0x11000C,S3_AIZ_MINIBOSS_CUTSCENE);
    longword(g_rom,0x5CCA2,S3_AIZ_MINIBOSS_CUTSCENE);
    word(g_ram,0x8000,256);
    s_placement_count=0;
    write16(0xEE78,0x2D80);s3_video_hook(S3_LOAD_UPDATE);
    CHECK(!(g_ram[0xEB02]&128));
    write16(0xEE78,0x2E00);s3_video_hook(S3_LOAD_UPDATE);
    CHECK(g_ram[0xEB02]&128);
    CHECK(scene_read32(0xFFB172)==S3_AIZ_MINIBOSS_CUTSCENE);
    word(g_ram,0x8000,16);
    CHECK(configure("off"));g_cpu.D[0]=12345;s3_video_hook(0x1B594);CHECK(g_cpu.D[0]==12345);
    /* Front-buffer retention through partial SAT DMA / producer rollover. */
    CHECK(configure("32:9"));write16(0xEE80,0);g_ram[0xF711]=1;
    static GVDP v;v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=0xE;
    memset(v.vram+32,0x11,32);uint32_t native[320]={0},out[796];
    s_build.count=2;s_build.scene=1;s_build.camera_x=s_build.camera_y=0;
    s_build.sprites[0]=(SceneSprite){16,0,0x8001,0,1};
    s_build.sprites[1]=(SceneSprite){700,0,0x8001,0,0};
    publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    scanline(&v,0,native,320,out,796);CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000);
    v.vram[0xF806]^=1;scanline(&v,0,native,320,out,796);
    CHECK(out[16]==0xFFFF0000 && out[700]==0xFFFF0000 && s_scene_holds==1);
    word(g_ram,0xEE80,500);word(v.vram,0xF000,(uint16_t)-500);
    scanline(&v,0,native,320,out,796);CHECK(out[16]==0xFFFF0000 && out[438]==0xFFFF0000);
    CHECK(configure("off"));CHECK(configure("fit"));CHECK(!s_display_frame.serial);
    /* S3's low-ROM title-card routines have a zero high word, not an empty
     * object slot. Six-byte static mappings must still be captured. */
    g_ram[0xF711]=0;word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB172);
    write32(0xB172,0x9000);write32(0xB17E,0x2000);write16(0xB182,144);write16(0xB186,136);
    g_ram[0xB176]=32;word(g_rom,0x2002,1);word(g_rom,0x2004,0);
    capture_objects();CHECK(s_build.count==1 && s_build.sprites[0].x==16 && s_build.sprites[0].hud==2);
    /* Never sample a half-decompressed outgoing intro background. */
    g_ram[0xF711]=1;word(g_ram,0xEEC2,0);word(g_ram,0xEEC6,0);
    scanline(&v,0,native,320,out,796);unsigned old=s_background_frame[128];
    word(g_ram,0xEEC6,0xFF00);g_ram[128]^=1;
    scanline(&v,0,native,320,out,796);CHECK(s_background_frame[128]==old);
    word(g_ram,0xEEC2,4);word(g_ram,0xEEC6,0);
    scanline(&v,0,native,320,out,796);CHECK(s_background_frame[128]==g_ram[128]);
    /* AIZ2 streams a 512px BG canvas at X=0, then scrolls/repeats it.
     * Put a green unrelated bank after it: large scroll offsets must still
     * draw the red canvas, including the extended margins. */
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xF711]=1;
    g_ram[0xFE11]=1;word(g_ram,0xEEC2,12);
    word(g_ram,0x8000,16);word(g_ram,0x8002,32);
    word(g_ram,0x8008,0x8100);word(g_ram,0x800A,0x8140);
    memset(g_ram+0x8140,2,32);memset(g_ram+0x8140,1,4);
    for(unsigned n=0;n<64;++n){word(g_ram,128+n*2,1);word(g_ram,256+n*2,2);}
    for(unsigned n=0;n<4;++n){word(g_ram,0x9008+n*2,1);word(g_ram,0x9010+n*2,2);}
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE;v.cram[2]=0xE0;
    memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
    for(unsigned n=0;n<64;++n)word(v.vram,0xE000+n*2,1);
    word(v.vram,0xF002,(uint16_t)-1024);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;
    publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    scanline(&v,0,native,320,out,796);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFFFF0000);
    CHECK(s_bg_checks>0 && s_bg_errors==0);
    /* SOZ1 uses the same 512px desert canvas, never its later pyramid
     * layout. This regression uses the unrelated green bank above. */
    const unsigned canvases[][3]={{8,0,0},{3,0,0},{3,1,8},{22,0,0}};
    for(unsigned c=0;c<sizeof canvases/sizeof canvases[0];++c) {
        g_ram[0xFE10]=(uint8_t)canvases[c][0];g_ram[0xFE11]=(uint8_t)canvases[c][1];
        word(g_ram,0xEEC2,canvases[c][2]);
        scanline(&v,0,native,320,out,796);
        for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFFFF0000);
        CHECK(s_bg_checks>0 && s_bg_errors==0);
    }
    /* LBZ1 draws its lower band from X=0 (HScroll_table+$004), while the
     * finer water deformation scrolls through it. Repeat that 512px canvas
     * only below the authored $D0 split; the upper Death Egg world remains
     * spatial. A second layout bank here is deliberately unrelated green. */
    g_ram[0xFE10]=6;g_ram[0xFE11]=0;word(g_ram,0xEEC2,0);
    word(g_ram,0x800E,0x8140);
    for(unsigned n=0;n<64*32;++n)word(v.vram,0xE000+n*2,1);
    scanline(&v,0,native,320,out,796);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFF00FF00);
    word(g_ram,0xEE90,0xD0);v.vsram[1]=0xD0;
    scanline(&v,0,native,320,out,796);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFFFF0000);
    CHECK(s_bg_checks>0 && s_bg_errors==0);
    word(g_ram,0xEE90,0);v.vsram[1]=0;
    /* HPZ can update its fine/coarse position before the next tile-column
     * streamer runs. Compare only the last Draw_BG interval: A802=$10,
     * not the pending A800=$20. The stale rightmost native tile is red,
     * while the authored expanded world and all streamed cells are green. */
    g_ram[0xFE10]=22;g_ram[0xFE11]=1;
    memset(g_ram+0x8140,2,32);word(g_ram,0x8016,0x8140);
    word(g_ram,0xA800,32);word(g_ram,0xA802,16);
    word(g_ram,0xEE90,506);v.vsram[1]=506;
    word(v.vram,0xF002,(uint16_t)-40);
    for(unsigned n=0;n<64*32;++n)word(v.vram,0xE000+n*2,2);
    word(v.vram,0xE000+31*128+44*2,1);
    scanline(&v,0,native,320,out,796);
    CHECK(s_bg_checks>0 && s_bg_errors==0 && s_bg_unstreamed>0);
    s3_video_vblank();
    word(g_ram,0xA802,32); /* next queue prepared after the displayed V-int */
    scanline(&v,0,native,320,out,796);
    CHECK(s_bg_checks>0 && s_bg_errors==0 && s_bg_unstreamed>0);
    g_ram[0xF600]=4;s3_video_vblank();g_ram[0xF600]=12;
    /* Without a matching scene, the displayed native viewport is copied
     * from hardware. Discarded provisional world samples are not errors. */
    memset(s_history,0,sizeof s_history);memset(&s_display_frame,0,sizeof s_display_frame);
    for(unsigned x=0;x<320;++x)native[x]=0xFF123456;
    scanline(&v,0,native,320,out,796);
    CHECK(!s_display && !s_bg_checks && !s_terrain_checks);
    CHECK(!memcmp(out+s_native_x,native,sizeof native));
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    memcpy(v.vram+0xF800,g_ram+0xF800,640);
    word(g_ram,0xEE90,0);v.vsram[1]=0;
    word(v.vram,0xF002,(uint16_t)-1024);
    /* FBZ's indoors/outdoors wipes publish mixed rows/columns. Keep that
     * exact plane throughout either act and every background event; do
     * not choose a complete bank early or repeat the combined layout. */
    for(unsigned n=0;n<64*32;++n)word(v.vram,0xE000+n*2,n%64<32?1:2);
    g_ram[0xFE10]=4;
    for(unsigned act=0;act<2;++act)for(unsigned event=0;event<=16;event+=4) {
        g_ram[0xFE11]=(uint8_t)act;word(g_ram,0xEEC2,event);
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;++x)
            CHECK(out[x]==(((x-s_native_x+1024)&511)<256?0xFFFF0000:0xFF00FF00));
    }
    g_ram[0xFE10]=0;
    /* The AIZ fire curtain lives in the uploaded name table while RAM
     * changes to the incoming stage. Its 16px columns have independent
     * vertical scroll. Use red/green rows over unrelated blue world art,
     * including negative native columns and more than 20 screen columns. */
    memset(g_ram+0x8100,3,16);memset(g_ram+0x8140,3,32);
    for(unsigned n=0;n<64;++n)word(g_ram,384+n*2,3);
    for(unsigned n=0;n<4;++n)word(g_ram,0x9018+n*2,3);
    v.cram[3]=0xE00;memset(v.vram+96,0x33,32);
    memset(v.vram+32,0x11,16);memset(v.vram+48,0x22,16);
    for(unsigned n=0;n<64*32;++n)word(v.vram,0xE000+n*2,0x8001);
    v.reg[11]=4;
    for(unsigned col=0;col<20;++col)v.vsram[col*2+1]=col%8;
    word(v.vram,0xF000,(uint16_t)-512);word(v.vram,0xF002,(uint16_t)-0x1060);
    word(g_ram,0xEE80,512);
    const unsigned phases[][2]={{0,12},{0,16},{0,20},{1,0},{1,4},{1,8}};
    for(unsigned phase=0;phase<sizeof phases/sizeof phases[0];++phase) {
        g_ram[0xFE11]=(uint8_t)phases[phase][0];word(g_ram,0xEEC2,phases[phase][1]);
        scanline(&v,0,native,320,out,796);
        CHECK(s_native_x==238);
        for(int x=0;x<796;++x) {
            int nx=x-238;
            unsigned column=((unsigned)nx>>4)&7;
            CHECK(out[x]==(column<4?0xFFFF0000:0xFF00FF00));
        }
    }
    /* High-priority tree canopy in the added margins must stay behind
     * opaque flames. The native viewport retains the VDP's A-over-B tie.
     * During art replacement, the staged Plane A overrides garbage RAM. */
    for(unsigned n=0;n<4;++n)word(g_ram,0x9018+n*2,0x8003);
    for(unsigned n=0;n<64*32;++n)word(v.vram,0xC000+n*2,0x8002);
    for(unsigned phase=0;phase<sizeof phases/sizeof phases[0];++phase) {
        g_ram[0xFE11]=(uint8_t)phases[phase][0];word(g_ram,0xEEC2,phases[phase][1]);
        int staged=phase==1 || phase==2 || phase==3;
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;++x) {
            int nx=x-238;
            unsigned column=((unsigned)nx>>4)&7;
            uint32_t expected=nx>=0 && nx<320?(staged?0xFF00FF00:0xFF0000FF):
                column<4?0xFFFF0000:0xFF00FF00;
            CHECK(out[x]==expected);
        }
    }
    /* Transparent flame pixels reveal foreground even in the margins. */
    memset(v.vram+32,0,32);
    scanline(&v,0,native,320,out,796);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFF0000FF);
    /* Once the staged BG redraw ends, return to the incoming world. */
    word(g_ram,0xEEC2,12);v.reg[11]=0;
    scanline(&v,0,native,320,out,796);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFF0000FF);
    /* AIZ's hollow tree reveals individual blocks in Plane A before it
     * edits complete layout chunks. Keep its uploaded mask/priority while
     * drawing the surrounding expanded world from the layout. */
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xF711]=1;
    word(g_ram,0x8000,128);word(g_ram,0x8002,128);
    for(unsigned row=0;row<32;++row) {
        word(g_ram,0x8008+row*4,0x8200);word(g_ram,0x800A+row*4,0x8300);
    }
    memset(g_ram+0x8200,1,128);memset(g_ram+0x8300,2,128);
    for(unsigned n=0;n<64;++n){word(g_ram,128+n*2,1);word(g_ram,256+n*2,2);}
    for(unsigned n=0;n<4;++n){word(g_ram,0x9008+n*2,0x8003);word(g_ram,0x9010+n*2,2);}
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE;v.cram[2]=0xE0;v.cram[3]=0xE00;
    memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);memset(v.vram+96,0x33,32);
    for(unsigned n=0;n<64*32;++n) {
        word(v.vram,0xC000+n*2,n&1?0:0x8001);
        word(v.vram,0xE000+n*2,2);
    }
    word(g_ram,0xEEC2,8);word(g_ram,0xEEC4,17);
    word(g_ram,0xEE84,0x380);v.vsram[0]=0x380;
    memset(&s_build,0,sizeof s_build);s_build.scene=1;
    publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    for(int camera=0x2C54;camera<=0x2C60;camera+=12) {
        word(g_ram,0xEE80,camera);word(v.vram,0xF000,(uint16_t)-camera);
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;++x) {
            int wx=camera+x-s_native_x;
            uint32_t expected=wx>=0x2C80 && wx<0x2D80?
                (wx&8?0xFF00FF00:0xFFFF0000):0xFF0000FF;
            CHECK(out[x]==expected);
        }
    }
    /* Finished reveal, another act/zone, and rows outside the tree keep
     * normal terrain. */
    for(unsigned case_id=0;case_id<4;++case_id) {
        word(g_ram,0xEEC4,case_id==0?0:17);
        g_ram[0xFE10]=case_id==1;g_ram[0xFE11]=case_id==2;
        word(g_ram,0xEE84,case_id==3?0x480:0x380);v.vsram[0]=case_id==3?0x480:0x380;
        scanline(&v,0,native,320,out,796);
        for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFF0000FF);
    }
    /* A queued title plane survives native 4:3 sprite culling. Paint only
     * its extensions; the authoritative native title pixels stay intact. */
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    g_ram[0xF600]=4;v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;
    v.reg[5]=0x7C;v.reg[12]=1;v.reg[16]=1;v.cram[1]=0xE;
    memset(v.vram+32,0x11,32);
    for(unsigned n=0;n<320;++n)native[n]=0xFF123456;
    write32(0xB172,0x1234);write32(0xB17E,S3_TITLE_PLANE_MAP);
    word(g_ram,0xB182,128);word(g_ram,0xB186,128);
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB172);
    word(g_rom,S3_TITLE_PLANE_MAP,2);word(g_rom,S3_TITLE_PLANE_MAP+2,1);
    word(g_rom,S3_TITLE_PLANE_MAP+4,0);word(g_rom,S3_TITLE_PLANE_MAP+6,1);
    word(g_rom,S3_TITLE_PLANE_MAP+8,65532);
    capture_objects();CHECK(s_build.count==1 && s_build.sprites[0].hud==3);
    s_build.sprites[1]=s_build.sprites[0];s_build.sprites[1].x=316;
    s_build.sprites[2]=(SceneSprite){-20,0,1,0,2};s_build.count=3;
    publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;++x) {
        uint32_t expected=x>=238 && x<558?0xFF123456:
            ((x>=234 && x<238)||(x>=558 && x<562))?0xFFFF0000:0xFF000000;
        CHECK(out[x]==expected);
    }
    /* The native special-stage row is the visual oracle. Preserve every
     * pixel, including sphere placement, operator sprites and HUD, across
     * all rows and canvas sizes. Both interfaces must produce the same row;
     * the lazy interface fetches the native composition exactly once. */
    static const int ss_canvases[]={320,398,523,796,1603};
    uint32_t ss_native[320],ss_output[1605],ss_eager[1603];
    static uint8_t before[65536];
    for(unsigned mode=0;mode<2;++mode)for(unsigned size=0;size<5;++size) {
        int canvas=ss_canvases[size],origin=(canvas-320)/2;
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=(uint8_t)(mode?0x48:0x34);
        v.reg[1]=64;v.reg[12]=8;v.cram[1]=14;
        memcpy(before,g_ram,sizeof before);
        for(int y=0;y<224;++y) {
            for(int x=0;x<320;++x)ss_native[x]=0xFF000000u|((unsigned)y<<16)|((unsigned)x*193u);
            ss_output[0]=ss_output[canvas+1]=0xDEADBEEFu;
            native_row_calls=0;
            scanline_lazy(&v,y,320,ss_output+1,canvas,test_native_row,ss_native);
            CHECK(native_row_calls==1 && ss_output[0]==0xDEADBEEFu && ss_output[canvas+1]==0xDEADBEEFu);
            scanline(&v,y,ss_native,320,ss_eager,canvas);
            CHECK(!memcmp(ss_eager,ss_output+1,(size_t)canvas*sizeof *ss_eager));
            for(int x=0;x<canvas;++x)
                CHECK(ss_output[x+1]==(x>=origin && x<origin+320?ss_native[x-origin]:0xFF000000u));
        }
        CHECK(!memcmp(before,g_ram,sizeof before));
        /* The original globe remains visible while native video retains
         * shadow/highlight during the special-stage exit fade. */
        g_ram[0xF600]=0x0C;
        scanline_lazy(&v,0,320,ss_output+1,canvas,test_native_row,ss_native);
        CHECK(s_frame_special);
    }
    test_sprite_atlas();test_object_zoom();test_file_select();test_world_snapshot();test_hud_mapping();
    puts("Sonic 3 video terrain, activation, publication, file select and Blue Spheres PASS");
    return 0;
}
