/* Test the actual renderer's row-pointer layout and host publication helpers. */
#include "../../game/common/sonic3_video.c"
uint8_t g_ram[65536],g_rom[0x400000];
uint8_t *g_rom_ext;
uint32_t g_rom_ext_size;
static int test_private_planes;
int gvdp_ext_line(unsigned tile) { return test_private_planes && tile>=1 && tile<=2?(int)tile:0; }
static uint8_t test_atlas[128];
static int test_atlas_enabled;
const uint8_t *gvdp_sprite_patterns(unsigned flags,unsigned *bytes) {
    *bytes=test_atlas_enabled&&(flags&128)?sizeof test_atlas:0;
    return *bytes?test_atlas:NULL;
}
int gvdp_sprite_pattern_line(unsigned flags,unsigned tile) { (void)flags;return gvdp_ext_line(tile); }
int gvdp_sprite_palette_line(unsigned flags,unsigned tile,unsigned source) { (void)source;return gvdp_sprite_pattern_line(flags,tile); }
uint16_t gvdp_ext_color(int row,int line,int nib) {
    return test_private_planes && nib==1?(uint16_t)(line==1?(row>=80?0xE0:0xE):line==2?0xE00:0):0;
}
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
static void test_native_bonus(void)
{
    static GVDP v;uint32_t native[320],out[796];
    static uint8_t before[65536];
#ifdef SONIC3_STANDALONE
    const unsigned last=0x13;
#else
    const unsigned last=0x15;
#endif
    CHECK(configure("off"));CHECK(configure("32:9"));
    for(unsigned zone=0x13;zone<=last;zone++) {
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;g_ram[0xFE10]=(uint8_t)zone;g_ram[0xF711]=1;
        write32(0xB000,0x123456);word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
        CHECK(bonus_stage() && !gameplay() && !level_ready());
        CHECK(s3_video_main_cpu_divisor()==1);
        g_cpu.D[0]=0x12345678;g_cpu.A[1]=0x87654321;g_cpu.A[4]=0x76543210;
        s3_video_hook(S3_RING_RANGE);s3_video_hook(S3_RING_STATUS);s3_video_hook(0x1A8D2);
        CHECK(g_cpu.D[0]==0x12345678 && g_cpu.A[1]==0x87654321 && g_cpu.A[4]==0x76543210);
        capture_objects();CHECK(s_build.scene==3 && !s_build.count);publish_sprites();
        memcpy(before,g_ram,sizeof before);
        for(int y=0;y<224;y++) {
            for(int x=0;x<320;x++)native[x]=0xFF000000u|((unsigned)y<<16)|(unsigned)x;
            native_row_calls=0;scanline_lazy(&v,y,320,out,796,test_native_row,native,NULL);
            CHECK(native_row_calls==1);
            for(int x=0;x<796;x++)CHECK(out[x]==(x>=238 && x<558?native[x-238]:0xFF000000u));
        }
        CHECK(!memcmp(before,g_ram,sizeof before));
        /* The outgoing bonus publication survives a mode/zone change before
         * the next native SAT upload, including a fully white fade. */
        g_ram[0xFE10]=0;g_ram[0xF600]=0x8C;
        for(unsigned i=0;i<64;i++)v.cram[i]=0xEEE;
        scanline(&v,0,native,320,out,796);
        CHECK(s_frame_bonus && out[0]==0xFF000000u && out[238]==native[0]);
    }
    CHECK(configure("off"));
}
static void test_file_select(void)
{
    static GVDP v;
    GVDPRasterRow raster={0};raster.width=raster.count=320;raster.display=1;
    static uint8_t before[65536];
    uint32_t native[320],out[1792];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("fit"));
    g_ram[0xF600]=0x4C;v.reg[1]=64;v.reg[2]=0x38;v.reg[4]=6;
    v.reg[3]=0x38;v.reg[18]=0x80;
    v.reg[5]=0x7C;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=3;
    v.cram[1]=0xE;v.cram[2]=0xE0;v.cram[3]=0xE00;v.cram[4]=0xEEE;
    for(unsigned tile=1;tile<=4;++tile)memset(v.vram+tile*32,tile*17,32);
    memset(v.vram+0x29F*32,0x33,32);
    for(unsigned row=0;row<2;++row) {
        for(unsigned col=0;col<40;++col)word(v.vram,0xE000+row*128+col*2,col<20?1:2);
        word(v.vram,0xC000+row*256+2,0x829F);
        word(v.vram,0xC000+row*256+252,0x829F);
    }
    for(unsigned n=0;n<40*28;++n)s_menu_map[n]=(n%40)<20?1:2;
    s_menu_map_ready=1;
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
        memset(raster.plane_a,0,sizeof raster.plane_a);
        memset(raster.opaque,0,sizeof raster.opaque);
        for(unsigned x=316;x<320;x++){raster.opaque[x]=raster.high[x]=1;raster.sprite[x]=4;}
        native_row_calls=0;
        scanline_lazy(&v,0,320,out,canvas,test_native_row,native,&raster);
        CHECK(!native_row_calls && s_frame_menu==MENU_SAVE && menu_raster_enabled(&v));
        for(int x=0;x<canvas;++x) {
            int nx=x-origin;
            if((nx>=320 && nx<324)||(nx>=330 && nx<338))CHECK(out[x]==0xFFFFFFFF);
        }
        memset(raster.opaque,0,sizeof raster.opaque);
        for(unsigned x=80;x<88;x++)raster.plane_a[x]=0xC3;
        scanline_lazy(&v,8,320,out,canvas,test_native_row,native,&raster);
        for(int x=0;x<canvas;++x) {
            int nx=x-origin,card=nx+scroll;
            /* Stretch only unobscured scenery. The eight-pixel portrait and
             * every carousel card remain eight pixels at every canvas size. */
            uint32_t bg=(int64_t)x*320/canvas<160?0xFFFF0000:0xFF00FF00;
            uint32_t expected=(nx>=80 && nx<88)||(card>=8 && card<16)||
                (card>=1008 && card<1016)?0xFF0000FF:bg;
            CHECK(out[x]==expected);
        }
    }
    CHECK(!memcmp(before,g_ram,sizeof before));
    s_menu_map_ready=0;
}
static void test_menu_layers(void)
{
    static GVDP v;static uint8_t before[65536];
    GVDPRasterRow raster={0},saved;uint32_t out[1792],native[320]={0};
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("fit"));
    g_ram[0xF600]=4;word(g_ram,0xFFBE,12);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[11]=3;v.reg[12]=1;v.reg[16]=S3_TITLE_PLANE_SIZE;
    v.cram[1]=0xE;v.cram[2]=0xE0;v.cram[3]=0xE00;v.cram[4]=0xEEE;
    memset(&s_build,0,sizeof s_build);s_build.title=1;publish_sprites();
    raster.width=raster.count=320;raster.display=1;
    for(unsigned x=0;x<320;x++)raster.plane_b[x]=(uint8_t)(64+(x<160?1:2));
    for(unsigned x=80;x<88;x++)raster.plane_a[x]=0xC3;
    /* Red foreground over red native scenery is still opaque when the
     * widened scenery under it becomes green. Final-color equality is not
     * an opacity mask. All foreground/sprite spans retain eight pixels. */
    for(unsigned x=280;x<288;x++)raster.plane_a[x]=0xC1;
    for(unsigned x=200;x<208;x++) {
        raster.sprite[x]=4;raster.opaque[x]=raster.high[x]=1;
    }
    saved=raster;memcpy(before,g_ram,sizeof before);
    const int widths[]={320,398,523,796,1792};
    for(unsigned i=0;i<sizeof widths/sizeof widths[0];i++) {
        int canvas=widths[i],origin=(canvas-320)/2;
        for(unsigned phase=0;phase<3;phase++) {
            /* A new mode is selected before the outgoing title fades. The
             * installed planes/SAT retain composition, including all edges. */
            g_ram[0xF600]=(uint8_t)(phase?0x4C:4);
            v.cram[1]=(uint16_t)(phase==2?6:14);
            native_row_calls=0;
            scanline_lazy(&v,0,320,out,canvas,test_native_row,native,&raster);
            CHECK(!native_row_calls && s_frame_menu==MENU_TITLE);
            for(int x=0;x<canvas;x++) {
                int nx=x-origin,sx=(int)((int64_t)x*320/canvas);
                unsigned p=nx>=80&&nx<88?3:nx>=280&&nx<288?1:
                    nx>=200&&nx<208?4:sx<160?1:2;
                CHECK(out[x]==genesis_dac_cram_to_argb(v.cram[p],GENESIS_DAC_NORMAL));
            }
        }
        /* Early title B is the other page of Sonic's animation. Keep both
         * actor planes centered so neither page repeats into the margins. */
        g_ram[0xF600]=4;word(g_ram,0xFFBE,1);v.cram[1]=14;
        scanline_lazy(&v,0,320,out,canvas,test_native_row,native,&raster);
        CHECK(s_frame_menu==MENU_TITLE_ANIMATION);
        for(int x=0;x<canvas;x++) {
            int nx=x-origin;
            unsigned p=nx<0||nx>=320?0:nx>=80&&nx<88?3:nx>=280&&nx<288?1:
                nx>=200&&nx<208?4:nx<160?1:2;
            CHECK(out[x]==genesis_dac_cram_to_argb(v.cram[p],GENESIS_DAC_NORMAL));
        }
        word(g_ram,0xFFBE,12);
        /* Every native page-flip pairing is still finite actor art, even
         * when the next frame counter has already advanced to twelve. */
        for(unsigned table=0;table<2;table++) {
            v.reg[2]=(uint8_t)(table?0x38:0x30);v.reg[4]=(uint8_t)(table?7:6);
            scanline_lazy(&v,0,320,out,canvas,test_native_row,native,&raster);
            CHECK(s_frame_menu==MENU_TITLE_ANIMATION && menu_raster_enabled(&v));
            if(canvas>320)CHECK(out[0]==0xFF000000u && out[canvas-1]==0xFF000000u);
        }
        v.reg[2]=0x30;v.reg[4]=7;
        /* Competition's native shadow/highlight is composed for the whole
         * canvas. High-priority UI and actors retain normal brightness. */
        g_ram[0xF600]=0x40;v.reg[11]=0;v.reg[12]=9;v.reg[16]=0x11;raster.shadow_mode=1;
        scanline_lazy(&v,0,320,out,canvas,test_native_row,native,&raster);
        CHECK(s_frame_menu==MENU_COMPETITION);
        for(int x=0;x<canvas;x++) {
            int nx=x-origin,sx=(int)((int64_t)x*320/canvas);
            int ui=(nx>=80&&nx<88)||(nx>=280&&nx<288)||(nx>=200&&nx<208);
            unsigned p=nx>=80&&nx<88?3:nx>=280&&nx<288?1:nx>=200&&nx<208?4:sx<160?1:2;
            CHECK(out[x]==genesis_dac_cram_to_argb(v.cram[p],ui?GENESIS_DAC_NORMAL:GENESIS_DAC_SHADOW));
        }
        v.reg[11]=3;v.reg[12]=1;v.reg[16]=S3_TITLE_PLANE_SIZE;raster.shadow_mode=0;
    }
    g_ram[0xF600]=4;
    CHECK(!memcmp(before,g_ram,sizeof before) && !memcmp(&saved,&raster,sizeof raster));
    raster.sprite[200]=GVDP_HOST_PALETTE_BASE+1;raster.host_palette[1]=0xFF123456u;
    scanline_lazy(&v,0,320,out,796,test_native_row,native,&raster);
    CHECK(out[238+200]==0xFF123456u); /* private menu art/color stays native size */
    /* A loading level may share title register geometry and publish an
     * empty scene. Only an actual title publication can retain title mode. */
    memset(&s_build,0,sizeof s_build);publish_sprites();g_ram[0xF600]=0x8C;
    scanline_lazy(&v,0,320,out,796,test_native_row,native,&raster);
    CHECK(s_frame_menu==MENU_NONE);
    CHECK(configure("off"));
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
    scanline_lazy(&v,1,320,lazy,398,test_native_row,native,NULL);
    CHECK(!native_row_calls && !memcmp(out,lazy,sizeof out));
    /* Title entry resets the live zone before fading the old SAT. Keep the
     * published world and its camera across that transition, using live CRAM. */
    g_ram[0xF600]=4;g_ram[0xFE10]=0;word(g_ram,0xEE80,0x1234);
    /* The producer can tag the same still-installed SAT as title before
     * its outgoing palette fade. Its previous level publication survives. */
    memset(&s_build,0,sizeof s_build);s_build.title=1;publish_sprites();
    uint8_t published[65536];memcpy(published,s_world_frame,sizeof published);
    scanline(&v,0,native,320,lazy,398);
    CHECK(s_frame_level && s_frame_zone==0x34 && !memcmp(published,s_world_frame,sizeof published));
    CHECK(!memcmp(out,lazy,sizeof out));
    /* A giant ring selects SpecialStage while the outgoing world still
     * fades to white. Retire the wide world only once native SHI is installed. */
    g_ram[0xF600]=0x34;native_row_calls=0;
    scanline_lazy(&v,0,320,lazy,398,test_native_row,native,NULL);
    CHECK(s_frame_level && !s_frame_special && !native_row_calls && !memcmp(out,lazy,sizeof out));
    for(unsigned i=0;i<64;i++)v.cram[i]=0xEEE;
    scanline_lazy(&v,0,320,lazy,398,test_native_row,native,NULL);
    for(unsigned i=0;i<398;i++)CHECK(lazy[i]==0xFFFFFFFFu);
    v.reg[12]=9;native_row_calls=0;
    scanline_lazy(&v,0,320,lazy,398,test_native_row,native,NULL);
    CHECK(s_frame_special && native_row_calls==1);
    memset(v.cram,0,sizeof v.cram);v.cram[1]=14;g_ram[0xF600]=4;
    /* An unsupported mode must fetch the original row exactly once and
     * produce the same centered image as the eager entry point. */
    v.reg[12]=7;scanline(&v,0,native,320,out,398);native_row_calls=0;
    scanline_lazy(&v,0,320,lazy,398,test_native_row,native,NULL);
    CHECK(native_row_calls==1 && !memcmp(out,lazy,sizeof out));
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static unsigned hud_fixture(void) { return 0x3000; }
static int hud_object_fixture(unsigned object) { return object==0xB0DE; }
static void test_hud_object_anchor(void)
{
    static GVDP v;uint32_t out[398],pal[64]={0};
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("fit"));
    write32(0xB0DE,0x1234);write32(0xB0DE + 0xC,0x6000);
    word(g_ram,0xB0DE + 0x10,128+68);word(g_ram,0xB0DE + 0x14,128+210);
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB0DE);
    word(g_rom,0x6000,2);word(g_rom,0x6002,1);word(g_rom,0x6004,0);
    word(g_rom,0x6006,0);word(g_rom,0x6008,0);
    s3_video_set_hud_object(hud_object_fixture);capture_objects();
    CHECK(s_build.count==1 && s_build.sprites[0].hud==1);
    const SceneFrame *old=s_display;s_display=&s_build;s_build.scene=1;
    memset(v.vram,0x11,32);pal[1]=0xFFFFFFFFu;
    memset(out,0,sizeof out);memset(s_priority,0,sizeof out/sizeof *out);
    draw_scene_sprites(&v,210,out,398,39,pal);
    CHECK(out[68]==pal[1] && !out[68+39]);
    s_display=old;s3_video_set_hud_object(NULL);capture_objects();
    CHECK(s_build.count==1 && s_build.sprites[0].hud==2);
    CHECK(configure("off"));
}
static int deformed_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)x;(void)y;*cell=background?0:1;return 1;
}
static void test_screen_actor_priority(void)
{
    static GVDP v;SceneFrame scene={0};uint32_t out[398],pal[64]={0};
    const SceneFrame *previous=s_display;
    memset(&v,0,sizeof v);memset(v.vram+32,0x11,32);pal[1]=0xFFFF0000u;
    scene.scene=1;scene.count=1;scene.sprites[0]=(SceneSprite){20,0,1,0,2};s_display=&scene;
    for(unsigned phase=0;phase<3;phase++) {
        for(unsigned x=0;x<398;x++){out[x]=0xFF00FF00u;s_priority[x]=(uint8_t)(phase!=0 && x!=61);}
        scene.sprites[0].attr=(uint16_t)(phase==2?0x8001:1);
        draw_scene_sprites(&v,0,out,398,39,pal);
        for(unsigned x=0;x<398;x++) {
            int visible=x>=59 && x<67 && (phase!=1 || x==61);
            CHECK(out[x]==(visible?0xFFFF0000u:0xFF00FF00u));
        }
    }
    s_display=previous;
}
static void test_deformation_keeps_actors_still(void)
{
    static GVDP v;uint32_t native[320]={0},out[398],plane[2];
    CHECK(configure("off"));CHECK(configure("fit"));width(398,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xFE10]=1;
    word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,64);word(g_ram,0xEE84,100);
    for(unsigned i=0;i<4;i++)word(g_ram,0x9008+i*2,2);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xEEE;v.cram[2]=0xE;v.cram[3]=0xE0;
    memset(v.vram+32,0x11,32);
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<4;x++)v.vram[64+y*4+x]=x<2?0x22:0x33;
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.count=1;
    s_build.camera_x=64;s_build.camera_y=100;
    s_build.sprites[0]=(SceneSprite){40,20,0x8001,0,0};s_visible_count=0;
    publish_sprites();s3_video_set_world_cell(deformed_cell);
    for(unsigned phase=0;phase<2;phase++) {
        word(v.vram,0xF000,(unsigned)(-64+(int)phase*4));v.vsram[0]=100+phase*3;
        scanline(&v,0,native,320,out,398);
        for(int y=19;y<=28;y++) {
            scanline(&v,y,native,320,out,398);
            for(int x=78;x<=87;x++)
                CHECK((out[x]==0xFFFFFFFFu)==(y>=20 && y<28 && x>=79 && x<87));
        }
        plane[phase]=out[0];
    }
    CHECK(plane[0]!=plane[1]); /* plane deformation is still visible */
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_s1_undeformed_installed_camera(void)
{
    static GVDP v;uint32_t native[320]={0},out[398];
    const unsigned zones[]={0x20,0x24};
    for(unsigned z=0;z<sizeof zones/sizeof *zones;++z) {
        CHECK(configure("off"));CHECK(configure("fit"));width(398,224,320,224);
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xFE10]=(uint8_t)zones[z];
        word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
        for(unsigned i=0;i<4;i++)word(g_ram,0x9008+i*2,2);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
        v.cram[1]=0xEEE;v.cram[2]=0xE;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
        memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.count=1;
        s_build.camera_x=64;s_build.camera_y=100;
        s_build.sprites[0]=(SceneSprite){40,20,0x8001,0,0};s_visible_count=0;
        publish_sprites();s3_video_set_world_cell(deformed_cell);
        for(int phase=0;phase<3;phase++) {
            /* The game is preparing a newer copy; only the installed scroll
             * has reached this displayed frame's foreground and SAT. */
            word(g_ram,0xEE80,(unsigned)(66+phase));
            word(g_ram,0xEE84,(unsigned)(97+phase));
            word(v.vram,0xF000,(unsigned)(-64-phase));
            v.vsram[0]=(uint16_t)(100+phase);
            scanline(&v,0,native,320,out,398);
            CHECK(s_camera==64+phase && s_camera_y==100+phase);
            for(int y=16;y<=28;y++) {
                scanline(&v,y,native,320,out,398);
                for(int x=74;x<=89;x++) {
                    int actor=x>=79-phase && x<87-phase && y>=20-phase && y<28-phase;
                    CHECK((out[x]==0xFFFFFFFFu)==actor);
                }
            }
        }
        s3_video_set_world_cell(NULL);CHECK(configure("off"));
    }
}
static int cd_edge_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)y;
    *cell=background?(uint16_t)(1+((x>>4)&1)):0;
    return 1;
}
static int intro_canvas_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)y;*cell=background?(x<512?1:2):0;return 1;
}
static void test_aiz_intro_canvas_edge(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    g_ram[0xF600]=12;write32(0xB000,0x1234);
    word(g_ram,0x8000,97);word(g_ram,0x8002,66);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE84,0x390);word(g_ram,0xEE90,0x390);v.vsram[0]=v.vsram[1]=0x390;
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE;v.cram[2]=0xE0;
    memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
    for(unsigned i=0;i<4;i++){word(g_ram,0x9008+i*2,1);word(g_ram,0x9010+i*2,2);}
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    s3_video_set_world_cell(intro_canvas_cell);
    const int cameras[]={32,176,336};
    for(unsigned c=0;c<sizeof cameras/sizeof *cameras;c++) {
        int camera=cameras[c];word(g_ram,0xEE80,camera);
        word(v.vram,0xF000,(unsigned)-camera);word(v.vram,0xF002,(unsigned)-(camera/2));
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;x++) {
            int source=x-s_native_x+camera/2;
            CHECK(out[x]==(source<512?0xFFFF0000u:0xFF00FF00u));
        }
    }
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static int ship_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;
    if(background){*cell=0;return 1;}
    /* Original AIZ2 second-camera coordinates must not be unwrapped into
     * the ordinary camera's neighboring 1024px page. */
    CHECK(y>=0xA00 || (y>=0x1C0 && y<0x300));
    if(y>=0xA00){CHECK(x>=0x3E00 && x<0x4300);*cell=1;}
    else {CHECK(x>=0x4100 && x<0x4600);*cell=2;}
    return 1;
}
static int private_plane_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)x;(void)y;*cell=background?2:1;return 1;
}
static void test_private_plane_colors(void)
{
    static GVDP v;uint32_t native[320]={0},out[398];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("fit"));width(398,224,320,224);
    g_ram[0xF600]=12;g_ram[0xFE10]=0x26;write32(0xB000,0x1234);
    word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xEEE;v.cram[17]=0xE00;
    memset(v.vram+32,0x11,64);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    s3_video_set_world_cell(private_plane_cell);test_private_planes=1;
    for(unsigned phase=0;phase<4;phase++) {
        uint16_t fa=phase==2?0x2001:phase==3?0:1,ba=phase==1?0x8002:2;
        for(unsigned i=0;i<4;i++){word(g_ram,0x9008+i*2,fa);word(g_ram,0x9010+i*2,ba);}
        for(unsigned row=0;row<=80;row+=80) {
            scanline(&v,0,native,320,out,398);
            scanline(&v,(int)row,native,320,out,398);
            uint32_t color=phase?0xFF0000FFu:row>=80?0xFF00FF00u:0xFFFF0000u;
            for(unsigned x=0;x<398;x++)CHECK(out[x]==color);
        }
    }
    test_private_planes=0;s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_aiz2_ship_cameras(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    g_ram[0xF600]=12;g_ram[0xFE11]=1;write32(0xB000,0x1234);
    word(g_ram,0x8000,256);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,0x4288);word(g_ram,0xEE84,0x1C0);
    word(g_ram,0xEE98,0x3FB5);word(g_ram,0xEE9C,0xAB3);word(g_ram,0xEED6,0xFF00);
    word(g_ram,0xEEC2,0x14);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[11]=3;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE;v.cram[2]=0xE0;
    memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
    for(unsigned i=0;i<4;i++){word(g_ram,0x9008+i*2,1);word(g_ram,0x9010+i*2,2);}
    for(unsigned y=0;y<224;y++)word(v.vram,0xF000+y*4,(unsigned)-(y<65?0x3FB5:0x4288));
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    s3_video_set_world_cell(ship_cell);
    v.vsram[0]=0xAB3;scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;x++)CHECK(out[x]==0xFFFF0000u);
    scanline(&v,64,native,320,out,796);
    for(int x=0;x<796;x++)CHECK(out[x]==0xFFFF0000u);
    /* The actual VDP interrupt changes only live VSRAM; the published RAM,
     * camera, art, and actor origin stay immutable for this frame. */
    v.vsram[0]=0x1C0;scanline(&v,65,native,320,out,796);
    for(int x=0;x<796;x++)CHECK(out[x]==0xFF00FF00u);
    CHECK(s_camera==0x4288 && s_camera_y==0x1C0);
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_campaign_background_edges(void)
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
    const unsigned zones[]={0x20,0x22,0x28,0x34,0x40,0x5B};
    for(unsigned z=0;z<sizeof zones/sizeof *zones;z++)for(unsigned w=0;w<4;w++) {
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

static void life_test_font(void)
{
    for(unsigned d=0;d<10;++d)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;x+=2) {
        unsigned a=(x+y+d)%5,b=(x+1+y+d)%5;
        a=a==1?1:a==2?12:a==3?15:0;
        b=b==1?1:b==2?12:b==3?15:0;
        g_rom[S3_LIFE_DIGITS+d*32+y*4+x/2]=(uint8_t)(a*16+b);
    }
}
static void life_test_sat(GVDP *v,int modified,int slide,unsigned count,int ppz)
{
    memset(v,0,sizeof *v);v->reg[1]=64;v->reg[5]=0x7C;v->reg[12]=1;
    word(v->vram,0xF800,modified?208:200);
    v->vram[0xF802]=(uint8_t)(modified?0:13);v->vram[0xF803]=(uint8_t)(modified?1:0);
    word(v->vram,0xF800,(modified?208:200)+128);
    word(v->vram,0xF804,modified?0xA7DD:0xA7D8);
    word(v->vram,0xF806,(modified?47:31)+slide+128);
    if(modified) {
        word(v->vram,0xF808,208+128);word(v->vram,0xF80C,0xA7DF);
        word(v->vram,0xF80E,55+slide+128);
    }
    for(unsigned digit=0;digit<2;++digit)for(unsigned i=0;i<32;++i) {
        unsigned d=digit?count/10%10:count/100;
        unsigned b=g_rom[S3_LIFE_DIGITS+d*32+i];
        unsigned hi=b>>4,lo=b&15;
        if(ppz){if(hi==12)hi=13;else if(hi==15)hi=14;if(lo==12)lo=13;else if(lo==15)lo=14;}
        v->vram[(0x7DD+digit*2)*32+i]=(uint8_t)(hi*16+lo);
    }
    v->cram[17]=0x222;v->cram[28]=0xEEE;v->cram[31]=0x444;
    v->cram[29]=0xE0;v->cram[30]=0xE00;
}
static void test_three_digit_lives(void)
{
    GVDP v;uint32_t out[800];uint8_t before[65536];
    memset(g_ram,0,sizeof g_ram);life_test_font();
    for(unsigned count=0;count<256;++count) {
        memset(&g_cpu,0xA5,sizeof g_cpu);g_cpu.D[1]=count;
        g_ram[0xFE12]=(uint8_t)count;memcpy(before,g_ram,sizeof before);
        M68KState expected=g_cpu;expected.D[1]=count>=100?count/10:count;
        CHECK(!s3_video_hook(S3_HUD_LIFE_VALUE));
        CHECK(!memcmp(&g_cpu,&expected,sizeof expected));
        CHECK(!memcmp(g_ram,before,sizeof before));
    }
    for(unsigned count=0;count<256;++count)for(int modified=0;modified<2;++modified)
        for(int ppz=0;ppz<2;++ppz)for(int shift=-66;shift<=12;shift+=78) {
            life_test_sat(&v,modified,shift,count>=100?count:100,ppz);
            g_ram[0xF600]=12;g_ram[0xFE12]=(uint8_t)count;g_ram[0xFE10]=(uint8_t)(ppz?0x40:1);
            GVDP unchanged=v;memcpy(before,g_ram,sizeof before);
            for(int y=0;y<8;++y) {
                for(unsigned x=0;x<800;++x)out[x]=0x12345678;
                life_ones_scanline(&v,g_ram,208+y,out+1,796,0);
                CHECK(out[0]==0x12345678 && out[797]==0x12345678);
                for(int x=0;x<796;++x) {
                    int gx=x-(63+shift);unsigned expected=0x12345678;
                    if(count>=100 && gx>=0 && gx<8) {
                        unsigned b=g_rom[S3_LIFE_DIGITS+(count%10)*32+y*4+gx/2];
                        unsigned n=(b>>((gx&1)?0:4))&15;
                        if(ppz){if(n==12)n=13;else if(n==15)n=14;}
                        if(n)expected=genesis_dac_cram_to_argb(v.cram[16+n],GENESIS_DAC_NORMAL);
                    }
                    CHECK(out[x+1]==expected);
                }
            }
            CHECK(!memcmp(&v,&unchanged,sizeof v) && !memcmp(g_ram,before,sizeof before));
        }
    /* Native/off overlay, centered fallback, current fades and hidden HUD. */
    CHECK(configure("off"));life_test_sat(&v,0,0,100,0);
    g_ram[0xF600]=12;g_ram[0xFE12]=100;g_ram[0xFE10]=1;
    for(unsigned fade=0;fade<8;++fade) {
        v.cram[17]=v.cram[28]=v.cram[31]=(uint16_t)(fade*0x222);
        memset(out,0,sizeof out);overlay_scanline(&v,211,out,320);
        for(int x=0;x<8;++x) {
            unsigned b=g_rom[S3_LIFE_DIGITS+12+x/2],n=(b>>((x&1)?0:4))&15;
            CHECK(out[63+x]==(n?genesis_dac_cram_to_argb((uint16_t)(fade*0x222),GENESIS_DAC_NORMAL):0));
        }
    }
    /* A fractional row owns its HBlank/fade colors independently of the
     * current/native VDP fallback palette. */
    for(unsigned i=0;i<64;++i){v.cram[i]=0xE00;ix_current()->cram[211][i]=0xE;}
    s_interpolating=1;memset(out,0,sizeof out);
    life_ones_scanline(&v,g_ram,211,out,320,0);
    for(int x=0;x<8;++x) {
        unsigned b=g_rom[S3_LIFE_DIGITS+12+x/2],n=(b>>((x&1)?0:4))&15;
        CHECK(out[63+x]==(n?genesis_dac_cram_to_argb(0xE,GENESIS_DAC_NORMAL):0));
    }
    s_interpolating=0;
    memset(out,0,sizeof out);life_ones_scanline(&v,g_ram,211,out,796,238);
    CHECK(!out[63]);
    v.reg[1]=0;memset(out,0,sizeof out);overlay_scanline(&v,211,out,320);
    for(unsigned x=0;x<320;++x)CHECK(!out[x]);
    v.reg[1]=64;word(v.vram,0xF804,0);overlay_scanline(&v,211,out,320);
    for(unsigned x=0;x<320;++x)CHECK(!out[x]);
    life_test_sat(&v,0,0,100,0);memset(v.vram+0x7DD*32,0x99,32);
    overlay_scanline(&v,211,out,320);for(unsigned x=0;x<320;++x)CHECK(!out[x]);
    CHECK(configure("off"));
}

static void test_life_interpolation(void)
{
    GVDP v;static uint32_t row[320],image[320*224];
    memset(g_ram,0,sizeof g_ram);life_test_font();life_test_sat(&v,1,0,100,0);
    CHECK(configure("off"));s_world_cell=NULL;s_world_publish=NULL;s_actor_overlay=NULL;
    g_ram[0xF600]=12;g_ram[0xFE10]=1;g_ram[0xFE12]=100;g_ram[0xF711]=1;
    write32(0xB000,0x123456);word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    memset(s_ix_history,0,sizeof s_ix_history);s_ix_upload_serial=0;
    interpolation_configure(1);
    for(unsigned tick=1;tick<=2;++tick) {
        SceneFrame *f=&s_ix_history[1];memset(f,0,sizeof *f);f->serial=tick;f->scene=1;
        memcpy(f->sat,v.vram+0xF800,640);word(g_ram,0xFE04,tick);
        for(int y=0;y<224;++y) {
            memset(row,0,sizeof row);overlay_scanline(&v,y,row,320);
            interpolation_capture(&v,y,row,320,320,NULL);
        }
    }
    CHECK(ix_current()->valid && ix_previous()->valid && !s_ix_overlay_seen);
    g_ram[0xFE12]=255; /* fractional rendering must retain the captured 100 */
    CHECK(interpolate(.5,image,320,224));
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        unsigned b=g_rom[S3_LIFE_DIGITS+y*4+x/2],n=(b>>((x&1)?0:4))&15;
        CHECK(image[(208+y)*320+63+x]==(n?genesis_dac_cram_to_argb(v.cram[16+n],GENESIS_DAC_NORMAL):0xFF000000));
    }
    CHECK(g_ram[0xFE12]==255);interpolation_configure(0);
}
#ifndef SONIC3_STANDALONE
static void test_knux_preview_publication(void)
{
    memset(g_ram,0,sizeof g_ram);memset(&s_build,0,sizeof s_build);
    CHECK(configure("32:9"));width(796,224,320,224);
    g_ram[0xF600]=12;word(g_ram,0xFE10,1);write32(0xB000,0x1234);
    word(g_ram,0x8000,256);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,0x4A38);word(g_ram,0xEE84,0x15A);word(g_ram,0xEEAA,0x7FF);
    write32(0xB206,0x69526);
    capture_objects();CHECK(s_build.preview_knux);publish_sprites();
    CHECK(s_history[s_serial%3].preview_knux);
    /* The earlier allocated slot has not reached its initializer/Draw_Sprite. */
    write32(0xB206,0x69588);write32(0xB128,0x61D4C);
    capture_objects();CHECK(s_build.preview_knux);publish_sprites();
    CHECK(s_history[s_serial%3].preview_knux);
    /* The real pose is now queued in this same completed display list. */
    write32(0xB128,0x61F98);write32(0xB134,0x364016);
    g_ram[0xB12C]=5;g_ram[0xB12E]=24;g_ram[0xB12F]=28;g_ram[0xB14A]=22;
    word(g_ram,0xB132,0xA4DA);word(g_ram,0xB138,0x4B8E);word(g_ram,0xB13C,0x17D);
    word(g_rom,0x364016+44,0x80);word(g_rom,0x364096,1);
    word(g_rom,0x364098,0xF800);word(g_rom,0x36409A,0);word(g_rom,0x36409C,0);
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB128);
    capture_objects();CHECK(!s_build.preview_knux && s_build.count==1);publish_sprites();
    CHECK(!s_history[s_serial%3].preview_knux);
    word(g_ram,0xAC00,0);capture_objects();CHECK(!s_build.preview_knux);
    word(g_ram,0xFE10,0);capture_objects();CHECK(!s_build.preview_knux);
    CHECK(configure("off"));
}
static void test_knux_preview_installed_scroll(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];
    CHECK(configure("32:9"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(0xB000,0x1234);g_ram[0xF600]=12;word(g_ram,0xFE10,1);
    word(g_ram,0x8000,256);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,0x4A00);word(g_ram,0xEE84,0x160);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[11]=3;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=14;
    for(int line=0;line<224;++line)
        word(v.vram,0xF000+(unsigned)line*4,(unsigned)(-0x49F8+(line%3==0)));
    v.vsram[0]=0x160;
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.preview_knux=1;
    publish_sprites();
    /* A synthetic opaque anchor isolates camera selection from art decoding. */
    memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
    s3_knux_preview_cache.rom=g_rom;s3_knux_preview_cache.ready=1;
    s3_knux_preview_cache.pixels[32*64+32]=1;
    s3_knux_preview_cache.colors[1]=0xFFFF0000;
    scanline(&v,0,native,320,out,796);
    scanline(&v,0x17D-0x160,native,320,out,796);
    CHECK(out[238 + 0x4B8E - 0x49F8]==0xFFFF0000);
    CHECK(out[238 + 0x4B8E - 0x4A00]==0xFF000000);
    memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
    CHECK(configure("off"));
}
#endif

static void test_interpolation_owned_and_readonly(void)
{
    GVDP v;
    CHECK(configure("off"));interpolation_configure(0);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    s_world_cell=NULL;s_world_publish=NULL;s_actor_overlay=NULL;
    g_ram[0xF600]=12;g_ram[0xFE10]=1;g_ram[0xF711]=1;
    write32(0xB000,0x123456);word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    v.reg[1]=64;v.reg[5]=0x7C;v.reg[12]=1;v.reg[13]=0x3C;
    v.cram[1]=14;
    S3StateIO measure={0};measure.ok=1;s3_video_state(&measure);
    uint8_t *before=(uint8_t*)malloc(measure.pos),*after=(uint8_t*)malloc(measure.pos);
    CHECK(before && after);
    S3StateIO io={before,measure.pos,0,0,1};s3_video_state(&io);CHECK(io.ok);
    uint8_t ram_before[65536];memcpy(ram_before,g_ram,sizeof ram_before);
    M68KState cpu_before=g_cpu;
    interpolation_configure(1);CHECK(!enabled() && s3_video_main_cpu_divisor()==1);
    CHECK(width(1920,1080,320,224)==0);
    s3_video_hook(S3_CAPTURE);s3_video_hook(S3_PUBLISH);
    io=(S3StateIO){after,measure.pos,0,0,1};s3_video_state(&io);
    CHECK(io.ok && !memcmp(before,after,measure.pos));
    CHECK(!memcmp(g_ram,ram_before,sizeof ram_before) && !memcmp(&g_cpu,&cpu_before,sizeof g_cpu));

    /* Two real completed captures own private patterns and per-line colors.
     * Moving one object must move every mapping piece by the same fraction. */
    test_atlas_enabled=test_private_planes=1;memset(test_atlas,0,sizeof test_atlas);
    memset(test_atlas+32,0x11,32);
    memset(s_ix_history,0,sizeof s_ix_history);s_ix_upload_serial=0;
    static uint32_t row[320],half[320*224];
    for(unsigned tick=1;tick<=2;++tick) {
        SceneFrame *f=&s_ix_history[1];memset(f,0,sizeof *f);
        f->serial=tick;f->scene=1;f->count=1;
        f->sprites[0]=(SceneSprite){(int)(100+(tick-1)*8),10,1,0x80,0,256,1};
        f->code[1]=0x123456;f->ox[1]=(int16_t)(100+(tick-1)*8);f->oy[1]=10;
        word(g_ram,0xFE04,tick);
        for(unsigned x=0;x<320;++x)row[x]=0xFF123400u+tick;
        for(int y=0;y<224;++y)interpolation_capture(&v,y,row,320,320,NULL);
    }
    CHECK(ix_current()->valid && ix_previous()->valid);
    memset(test_atlas,0x22,sizeof test_atlas);test_atlas_enabled=test_private_planes=0;
    io=(S3StateIO){before,measure.pos,0,0,1};s3_video_state(&io);
    memcpy(ram_before,g_ram,sizeof ram_before);cpu_before=g_cpu;
    CHECK(interpolate(0,half,320,224) && half[0]==0xFF123401);
    CHECK(interpolate(1,half,320,224) && half[0]==0xFF123402);
    CHECK(interpolate(.5,half,320,224));
    CHECK(interpolation_lerp(32760,-32760,48)==-32768);
    for(int x=100;x<116;++x)CHECK(half[10*320+x]==(x>=104 && x<112?0xFFFF0000u:0xFF000000u));
    /* Normal foreground Y uses the owned fractional VSRAM too, independently
     * of AIZ's special ship band. Distinct rows expose a stepping plane. */
    InterpSnapshot *latest=ix_current(),*prior=ix_previous();
    latest->scene.count=prior->scene.count=0;
    latest->context.fg_y=latest->context.video.vsram[0]=8;prior->context.fg_y=0;
    for(int y=0;y<224;++y) {
        latest->fg_scroll[y]=8;prior->fg_scroll[y]=0;
        for(int n=1;n<=8;++n)latest->cram[y][n]=(uint16_t)((n&7)*2);
    }
    for(unsigned y=0;y<8;++y)memset(latest->context.video.vram+y*4,(int)((y+1)*17),4);
    CHECK(interpolate(.5,half,320,224));
    CHECK(half[0]==genesis_dac_cram_to_argb(latest->cram[0][5],GENESIS_DAC_NORMAL));
    io=(S3StateIO){after,measure.pos,0,0,1};s3_video_state(&io);
    CHECK(io.ok && !memcmp(before,after,measure.pos));
    CHECK(!memcmp(g_ram,ram_before,sizeof ram_before) && !memcmp(&g_cpu,&cpu_before,sizeof g_cpu));
    interpolation_reset();CHECK(!interpolate(.5,half,320,224));
    interpolation_configure(0);free(before);free(after);
}

static void test_s1_interpolation_installed_camera(void)
{
    static GVDP v;static uint32_t row[398],half[398*224];uint32_t native[320]={0};
    const unsigned zones[]={0x20,0x24,0x21};
    for(unsigned z=0;z<sizeof zones/sizeof *zones;++z) {
        CHECK(configure("off"));interpolation_configure(0);
        CHECK(configure("fit"));width(398,224,320,224);interpolation_configure(1);
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;g_ram[0xFE10]=(uint8_t)zones[z];write32(0xB000,0x123456);
        word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
        for(unsigned i=0;i<4;i++)word(g_ram,0x9008+i*2,2);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
        v.cram[1]=0xEEE;v.cram[2]=0xE;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
        s3_video_set_world_cell(deformed_cell);s_world_publish=NULL;
        s_ix_overlay_seen=0;test_atlas_enabled=test_private_planes=0;
        int undeformed=zones[z]!=0x21;
        for(int phase=0;phase<2;++phase) {
            /* As in the real spring capture, the RAM camera is ahead of
             * the scroll/SAT currently installed by VBlank. */
            word(g_ram,0xEE80,(unsigned)(66+phase*4));
            word(g_ram,0xEE84,(unsigned)(98+phase*4));word(g_ram,0xFE04,phase+1);
            word(v.vram,0xF000,(unsigned)(-64-phase*4));v.vsram[0]=(uint16_t)(100+phase*4);
            memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.count=1;
            s_build.camera_x=64+phase*4;s_build.camera_y=100+phase*4;
            s_build.sprites[0]=(SceneSprite){40-phase*4,20-phase*4,0x8001,0,0};
            s_visible_count=0;publish_sprites();
            for(int y=0;y<224;++y) {
                scanline(&v,y,native,320,row,398);
                interpolation_capture(&v,y,row,398,320,NULL);
            }
        }
        CHECK(ix_current()->valid && ix_previous()->valid);
        CHECK(interpolate(.5,half,398,224));
        int left=undeformed?77:75,top=undeformed?18:20;
        for(int y=15;y<30;++y)for(int x=72;x<88;++x)
            CHECK((half[y*398+x]==0xFFFFFFFFu)==(x>=left && x<left+8 && y>=top && y<top+8));
        /* LZ keeps the non-plane camera: water deformation is not an actor
         * offset. The new basis is deliberately limited to GHZ/SYZ. */
        CHECK(ix_current()->context.fg_x==(undeformed?68:70));
        CHECK(ix_current()->context.fg_y==(undeformed?104:102));
        s3_video_set_world_cell(NULL);interpolation_configure(0);CHECK(configure("off"));
    }
}
static void test_blue_spheres(void)
{
    GVDP v={0};M68KState cpu_before=g_cpu;
    /* Blue Spheres' forward and inverse projections agree on the same
     * surface, including expanded margins. Behind-horizon points are not
     * mistaken for the near intersection. */
    for(int stretch=1;stretch<=6;++stretch)for(int z=0;z<5;++z)for(int x=-3;x<=3;++x) {
        double px,py,d;
        CHECK(ss_project(x,z,stretch,&px,&py,&d));
        SSPoint p=ss_intersect(px,py,stretch);
        CHECK(fabs(p.x-x)<.0001 && fabs(p.z-z)<.0001 && fabs(p.depth-d)<.0001);
    }
    CHECK(!ss_intersect(0,0,1).depth);
    CHECK(ss_intersect(-790,200,5).depth>0);
    double px,py,depth;
    CHECK(ss_project(0,0,1,&px,&py,&depth));
    CHECK(fabs(px)<.001 && fabs(py-(112+ss_focal*ss_ground_lift/ss_distance))<.001);
    CHECK(ss_project(2,1,1,&px,&py,&depth));
    CHECK(fabs(px-155)<3);
    CHECK(!ss_project(100,0,1,&px,&py,&depth));
    /* Full native board, more than 80 host spheres, and no guest writes.
     * Test without copyrighted assets using synthetic mappings/art. */
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(SS_EXTRA+16,0x1000);word(g_ram,SS_EXTRA+20,0x4002);
    for(unsigned i=0;i<16;++i)word(g_rom,0x1000+i*2,32);
    word(g_rom,0x1020,1);g_rom[0x1022]=240;g_rom[0x1023]=15;
    word(g_rom,0x1024,0);word(g_rom,0x1026,65520);
    memset(v.vram+64,0x11,512);memset(g_ram+0xF100,2,1024);
    g_ram[0xF600]=0x34;ss_capture();
    static uint8_t before[65536];memcpy(before,g_ram,sizeof before);
    ss_begin(&v,1603);CHECK(ss_ready && ss_count>80 && ss_margin_count>80);
    CHECK(!memcmp(before,g_ram,sizeof before));
    CHECK(ss_texture[2][0][32*64+32]==33);
    write16(0xE422,512);ss_begin(&v,1603);CHECK(ss_x==0); /* no mixed publication */
    ss_capture();ss_begin(&v,1603);CHECK(ss_x==2);
    ss_begin(&v,398);CHECK(ss_ready && ss_width==398);
    ss_begin(&v,320);CHECK(ss_ready && ss_width==320);
    /* A sprite's visible base stays on its actual board intersection while
     * moving, turning and resizing. This would fail with the old separate
     * sphere-camera lift even though the ground's inverse test passed. */
    uint32_t checker[1600],checker_colors[64]={0};
    checker_colors[56]=0xFF00FFFF;checker_colors[60]=0xFFFF00FF;
    for(unsigned turn=0;turn<8;++turn)for(unsigned motion=0;motion<3;++motion)
    for(int canvas=320;canvas<=1600;canvas+=320) {
        double angle=turn*6.2831853071795864769/8;
        int ix=(int)round(-2*sin(angle)),iy=(int)round(-2*cos(angle));
        memset(g_ram+0xF100,0,1024);
        g_ram[0xF100+((unsigned)iy&31)*32+((unsigned)ix&31)]=2;
        word(g_ram,0xE422,motion*32);word(g_ram,0xE424,motion*16);g_ram[0xE426]=(uint8_t)(turn*32);
        ss_capture();ss_begin(&v,canvas);CHECK(ss_ready && ss_count>=1);
        double dx=ix-ss_x,dy=iy-ss_y;
        CHECK(ss_project(dx*ss_cos-dy*ss_sin,-dx*ss_sin-dy*ss_cos,ss_stretch,&px,&py,&depth));
        /* Very wide views can see the periodic board again. Select this
         * cell's nearest projected copy, not an arbitrary depth-sort slot. */
        const SSSphere *sphere=NULL;double nearest=1e30;
        for(unsigned n=0;n<ss_count;++n) {
            double distance=fabs(ss_spheres[n].x+ss_spheres[n].w*.5-(px+(canvas-1)*.5));
            if(distance<nearest){nearest=distance;sphere=&ss_spheres[n];}
        }
        CHECK(sphere!=NULL);
        CHECK(ss_texture_floor[2][sphere->lod]==48);
        CHECK(fabs(sphere->x+sphere->w*.5-(px+(canvas-1)*.5))<=.501);
        CHECK(fabs(sphere->y+48*sphere->h/64.0-py)<=.501);
        /* Render the four quadrants around this object's board position.
         * Opposite quadrants must match and adjacent ones must differ. This
         * checks the actual checkerboard phase, not just projection algebra. */
        unsigned count=ss_count;ss_count=0;
        for(int qy=0;qy<2;++qy)for(int qx=0;qx<2;++qx) {
            double wx=ix+(qx?.1:-.1)-ss_x,wy=iy+(qy?.1:-.1)-ss_y;
            CHECK(ss_project(wx*ss_cos-wy*ss_sin,-wx*ss_sin-wy*ss_cos,ss_stretch,&px,&py,&depth));
            int col=(int)floor(px+(canvas-1)*.5+.5),row=(int)floor(py+.5);
            CHECK(col>=0 && col<canvas && row>=32 && row<224);
            ss_scanline(&v,row,checker,canvas,checker_colors,checker_colors,0);
            unsigned parity=(ix-(qx==0)+iy-(qy==0))&1;
            CHECK(checker[col]==checker_colors[parity?60:56]);
        }
        ss_count=count;
    }
    CHECK(!memcmp(&cpu_before,&g_cpu,sizeof g_cpu));
    /* Widescreen uses the corrected projection by default. Native Off and
     * a 320-pixel canvas retain the exact completed native composition. */
    CHECK(configure("32:9"));width(796,224,320,224);
    g_ram[0xF600]=0x34;v.reg[1]=64;v.reg[12]=8;
    v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.cram[56]=14;v.cram[60]=0xE0;s3_video_vblank();
    uint32_t native[320],out[796];
    for(unsigned x=0;x<320;++x)native[x]=0xFF123456;
    native_row_calls=0;
    scanline_lazy(&v,0,320,out,796,test_native_row,native,NULL);
    scanline_lazy(&v,180,320,out,796,test_native_row,native,NULL);
    CHECK(ss_ready && native_row_calls==0);
    CHECK(out[0]==0xFFFF0000 || out[0]==0xFF00FF00);
    /* The native exit keeps the last board until SHI display retirement. */
    g_ram[0xF600]=0x0C;ss_capture();
    scanline_lazy(&v,0,320,out,796,test_native_row,native,NULL);
    CHECK(s_frame_special && ss_latched && ss_ready);
    CHECK(configure("off") && !enabled());native_row_calls=0;
    scanline_lazy(&v,180,320,out,796,test_native_row,native,NULL);
    CHECK(native_row_calls==1 && out[0]==0xFF000000 && out[238]==native[0]);
    CHECK(configure("fit") && width(320,224,320,224)==320);
    native_row_calls=0;scanline_lazy(&v,0,320,out,320,test_native_row,native,NULL);
    CHECK(native_row_calls==1 && !memcmp(out,native,sizeof native) && !ss_ready);
    /* Retained native actors honor the existing private atlas and palette
     * contract in the projected stage, independently of the sphere artwork. */
    memset(v.vram+0xF800,0,8);word(v.vram,0xF800,128+80);
    word(v.vram,0xF804,0x4001);word(v.vram,0xF806,128+100);v.vram[0xF802]=128;
    memset(test_atlas,0,sizeof test_atlas);memset(test_atlas+32,0x11,32);
    memset(v.vram+32,0x22,32);test_atlas_enabled=test_private_planes=1;
    uint32_t colors[64]={0};colors[33]=0xFFFF0000;colors[34]=0xFF0000FF;
    memset(out,0,sizeof out);ss_native_sprites(&v,80,out,796,colors,colors,1);
    CHECK(!out[337] && !out[346]);
    for(unsigned x=338;x<346;++x)CHECK(out[x]==0xFF00FF00);
    test_atlas_enabled=test_private_planes=0;
    interpolation_reset();CHECK(!ss_latched && !ss_ready);
}

int main(int argc,char **argv)
{
    if(argc==2) {
        FILE *rom=fopen(argv[1],"rb");CHECK(rom);
        CHECK(fread(g_rom,1,sizeof g_rom,rom)==S3_ART_BANK+0x200000u);fclose(rom);
        CHECK(load_menu_map());
        for(unsigned n=0;n<40*28;n++)CHECK((s_menu_map[n]&2047u)<0x29Fu);
        s_menu_map_ready=0;
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
    test_campaign_background_edges();
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
    /* AIZ's x=$1870 platform can enter the extended window while its
     * original rounded distance ($300) still exceeds the native $280.
     * All three variable-range platform families must survive that early
     * load, and still retire after leaving their matching active window. */
    CHECK(configure("32:9"));width(1,1,320,224);
    word(g_ram,0x8000,256);write16(0xEE78,0x1580);write16(0xF7DA,0x1500);
    g_cpu.A[0]=0xFFFFB000u;
    const unsigned platform_culls[]={S3_MOVING_PLATFORM_CULL,S3_FLOATING_PLATFORM_CULL,S3_SWING_PLATFORM_CULL};
    for(unsigned n=0;n<3;n++)for(unsigned limit=640;limit<=896;limit+=256) {
        write16(0xB042,limit);
        g_cpu.D[0]=0xA5A50300u;s3_video_hook(platform_culls[n]);
        CHECK((g_cpu.D[0]&0xFFFF0000u)==0xA5A50000u && (uint16_t)g_cpu.D[0]<=limit);
        CHECK(ram16(0xB042)==limit); /* Never change the object's saved motion/range. */
        int lo,hi;activation_bounds(0x1580,796,&lo,&hi);
        if(limit>640)hi+=(int)limit-640;
        const int anchors[]={lo-128,lo,hi-128,hi};
        for(unsigned k=0;k<4;k++) {
            g_cpu.D[0]=0xA5A50000u|(uint16_t)(anchors[k]-0x1500);
            s3_video_hook(platform_culls[n]);
            CHECK(((uint16_t)g_cpu.D[0]>limit)==(k==0 || k==3));
            CHECK((g_cpu.D[0]&0xFFFF0000u)==0xA5A50000u);
        }
        CHECK(configure("off"));g_cpu.D[0]=0xA5A50300u;
        s3_video_hook(platform_culls[n]);CHECK(g_cpu.D[0]==0xA5A50300u);
        CHECK(configure("32:9"));width(1,1,320,224);
    }
    word(g_ram,0x8000,16);
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
    CHECK(configure("off"));CHECK(configure("32:9"));
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
    /* Missing projection metadata falls back to the complete native row at
     * every canvas size. Both interfaces preserve sprites/operators/HUD; the
     * lazy interface fetches the native composition exactly once. Valid wide
     * data and native-sized output are exercised by test_blue_spheres. */
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
            scanline_lazy(&v,y,320,ss_output+1,canvas,test_native_row,ss_native,NULL);
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
        scanline_lazy(&v,0,320,ss_output+1,canvas,test_native_row,ss_native,NULL);
        CHECK(s_frame_special);
    }
    test_sprite_atlas();test_object_zoom();test_file_select();test_menu_layers();test_world_snapshot();test_hud_mapping();
    test_deformation_keeps_actors_still();
    test_s1_undeformed_installed_camera();
    test_screen_actor_priority();
    test_aiz_intro_canvas_edge();
    test_aiz2_ship_cameras();
    test_private_plane_colors();
    test_hud_object_anchor();
    test_native_bonus();
#ifndef SONIC3_STANDALONE
    test_knux_preview_publication();
    test_knux_preview_installed_scroll();
#endif
    test_interpolation_owned_and_readonly();
    test_s1_interpolation_installed_camera();
    test_three_digit_lives();test_life_interpolation();
    test_blue_spheres();
    puts("Sonic 3 video terrain, activation, publication, file select, Blue Spheres and immutable interpolation PASS");
    return 0;
}
