/* Test the actual renderer's row-pointer layout and host publication helpers. */
#include "../../game/common/sonic3_video.c"
uint8_t g_ram[65536],g_rom[0x400000];
uint8_t *g_rom_ext;
uint32_t g_rom_ext_size;
static int test_private_planes;
static int test_time_private_palette;
int gvdp_ext_line(unsigned tile) { return test_private_planes && tile>=1 && tile<=2?(int)tile:0; }
static uint8_t test_atlas[128];
static int test_atlas_enabled;
const uint8_t *gvdp_sprite_patterns(unsigned flags,unsigned *bytes) {
    *bytes=test_atlas_enabled&&(flags&128)?sizeof test_atlas:0;
    return *bytes?test_atlas:NULL;
}
int gvdp_sprite_pattern_line(unsigned flags,unsigned tile) { (void)flags;return gvdp_ext_line(tile); }
int gvdp_sprite_palette_line(unsigned flags,unsigned tile,unsigned source) {
    if(test_time_private_palette && source==1 && (tile==0x6F2 || tile==0x6F3))return 2;
    return gvdp_sprite_pattern_line(flags,tile);
}
uint16_t gvdp_ext_color(int row,int line,int nib) {
    if(test_time_private_palette && line==2 && (nib==1 || nib==15))
        return (uint16_t)(row<32?(nib==1?0xE:0xE00):(nib==1?0xE0:0xEEE));
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
    /* Atlas bank 1 uses bit 6 too; it is not a margin-only current strip. */
    scene.sprites[0].size=0xC5;scene.sprites[0].zoom=256;scene.sprites[0].attr=0x8000;
    memset(out,0,sizeof out);memset(s_priority,0,64);
    draw_scene_sprites(&v,0,out,64,0,pal);CHECK(out[8]==pal[1]);
    scene.sprites[0].zoom=0;
    scene.sprites[0].size=5;scene.sprites[0].attr=0x8000;
    memset(s_priority,0,64);draw_scene_sprites(&v,0,out,64,0,pal);CHECK(out[8]==pal[5]);
    scene.sprites[0].size=0x85;scene.sprites[0].attr=0x8004;
    memset(out,0,sizeof out);memset(s_priority,0,64);
    draw_scene_sprites(&v,0,out,64,0,pal);CHECK(!out[8]); /* atlas bounds */
    test_atlas_enabled=0;s_display=previous;
}
static void test_aiz_monkey_owned_art(void)
{
    static GVDP v;static SceneFrame scene;uint32_t out[796],pal[64]={0};
    memset(&v,0,sizeof v);memset(&scene,0,sizeof scene);
    const SceneFrame *old=s_display;s_display=&scene;
    int level=s_frame_level;unsigned zone=s_frame_zone;
    s_frame_level=1;s_frame_zone=0;s_camera=s_camera_y=0;
    scene.scene=1;scene.count=2;
    scene.sprites[0]=(SceneSprite){-200,0,0x2548,0x10,0,256,1};
    scene.sprites[1]=(SceneSprite){100,0,0x2548,0,0,256,1};
    memset(s_aiz_monkey_art,0x12,sizeof s_aiz_monkey_art);
    s_aiz_monkey_ready=s_aiz_monkey_attempted=1;
    memset(v.vram+0x548*32,0x55,32);
    pal[17]=0xFF110000;pal[18]=0xFF220000;pal[21]=0xFF550000;
    memset(out,0,sizeof out);memset(s_priority,0,sizeof out/4);
    draw_scene_sprites(&v,0,out,796,238,pal);
    CHECK(out[38]==pal[17] && out[39]==pal[18]);
    CHECK(out[338]==pal[21]); /* native viewport retains the installed bank */
    /* Another actor can own these same tile numbers during the act2 ship.
     * Its edge pixels must use its installed art, never the monkey texture. */
    scene.sprites[0].size=0;
    memset(out,0,sizeof out);memset(s_priority,0,sizeof out/4);
    draw_scene_sprites(&v,0,out,796,238,pal);CHECK(out[38]==pal[21]);
    scene.sprites[0].size=0x10;

    scene.sprites[0].attr=0x2547;memset(v.vram+0x547*32,0x55,32);
    memset(out,0,sizeof out);memset(s_priority,0,sizeof out/4);
    draw_scene_sprites(&v,0,out,796,238,pal);CHECK(out[38]==pal[21]);
    scene.sprites[0].attr=0x2548;s_frame_zone=1;
    memset(out,0,sizeof out);memset(s_priority,0,sizeof out/4);
    draw_scene_sprites(&v,0,out,796,238,pal);CHECK(out[38]==pal[21]);
    /* Mapping capture establishes ownership before publication. A reuse
     * of the same tile range by a different mapping carries no such tag. */
    SceneFrame *previous_capture=s_capture_frame;s_capture_frame=&scene;
    scene.count=0;s_capture_slot=1;
    word(g_rom,S3_MONKEY_MAP,2);word(g_rom,S3_MONKEY_MAP+2,1);
    memset(g_rom+S3_MONKEY_MAP+4,0,6);
    add_mapping(S3_MONKEY_MAP,0,0x548,0,0,0,0,0,256);
    CHECK(scene.count==1 && (scene.sprites[0].size&0xD0)==SPRITE_MONKEY_ART);
    word(g_rom,0x1000,2);word(g_rom,0x1002,1);memset(g_rom+0x1004,0,6);
    add_mapping(0x1000,0,0x548,0,0,0,0,0,256);
    CHECK(scene.count==2 && !(scene.sprites[1].size&SPRITE_MONKEY_ART));
    s_capture_frame=previous_capture;
    s_display=old;s_frame_level=level;s_frame_zone=zone;
    s_aiz_monkey_ready=s_aiz_monkey_attempted=0;
}
static unsigned zoom_fixture(unsigned object) { (void)object;return 128; }
static void test_hcz_surface_extent(void)
{
    static GVDP v;uint32_t out[796],pal[64]={0};
    CHECK(configure("32:9"));s_requested_width=796;g_ram[0xFE10]=1;
    memset(&s_build,0,sizeof s_build);s_build.scene=1;
    word(g_rom,S3_HCZ_SURFACE_MAP+2,0x10);unsigned p=S3_HCZ_SURFACE_MAP+0x10;
    word(g_rom,p,3);
    for(unsigned n=0;n<3;++n) {
        word(g_rom,p+2+n*6,0x000C);word(g_rom,p+4+n*6,0);
        word(g_rom,p+6+n*6,(unsigned)(-96+(int)n*64));
    }
    add_mapping(S3_HCZ_SURFACE_MAP,1,0x842E,4,96,100,0,0,256);
    add_mapping(S3_HCZ_SURFACE_MAP,1,0x842E,4,288,100,0,0,256);
    CHECK(s_build.count==6);
    extend_hcz_surface(S3_HCZ_SURFACE_MAP,1,0x842E,4,96,100,256);
    CHECK(s_build.count>6);
    for(unsigned n=6;n<s_build.count;++n)
        CHECK(s_build.sprites[n].x+32<=0 || s_build.sprites[n].x>=320);
    memset(&v,0,sizeof v);memset(v.vram+0x42E*32,0x11,128);pal[1]=0xFF123456;
    memset(out,0,sizeof out);memset(s_priority,0,sizeof out/4);
    const SceneFrame *old=s_display;s_display=&s_build;s_camera=s_camera_y=0;
    draw_scene_sprites(&v,100,out,796,238,pal);
    for(int x=0;x<796;++x)CHECK(out[x]==(((x-238)&63)<32?pal[1]:0));
    unsigned count=s_build.count;s_requested_width=320;
    extend_hcz_surface(S3_HCZ_SURFACE_MAP,1,0x842E,4,96,100,256);CHECK(s_build.count==count);
    s_requested_width=796;g_ram[0xFE10]=0;
    extend_hcz_surface(S3_HCZ_SURFACE_MAP,1,0x842E,4,96,100,256);CHECK(s_build.count==count);
    s_display=old;CHECK(configure("off"));
}
static void test_native_sprite_art_ownership(void)
{
    GVDP v={0};SceneFrame scene={0};uint32_t out[32],pal[64]={0};
    const SceneFrame *previous=s_display;int level=s_frame_level;
    CHECK(native_art_owner(S3_EXPLOSION_MAP)==1);
    CHECK(native_art_owner(S3_ART_BANK+0x161CB8u)==2);
    CHECK(native_art_owner(S3_ART_BANK+0x30502u)==3);
    CHECK(native_art_owner(S3_SPRING_MAP)==4);
    CHECK(native_art_owner(S3_SPRING_MAP+22u)==4);
    CHECK(native_art_owner(S3_ART_BANK+0x164016u)==5);
    CHECK(!native_art_owner(0));
    scene.scene=1;scene.count=1;s_display=&scene;s_frame_level=1;s_camera=s_camera_y=0;
    pal[1]=0xFFFF0000;pal[2]=0xFF00FF00;
    for(unsigned owner=1;owner<=4;++owner) {
        const NativeArt *a=&s_native_art[owner-1];
        memset(s_native_art_cache[owner-1].pixels,0x11,a->bytes);
        s_native_art_cache[owner-1].pixels[0]=0x10;
        s_native_art_cache[owner-1].attempted=s_native_art_cache[owner-1].ready=1;
        memset(v.vram+a->tile*32,0x22,a->bytes+32);
        scene.sprites[0]=(SceneSprite){4,0,(uint16_t)a->tile,0,0,256,(uint16_t)(owner<<8)};
        memset(out,0,sizeof out);memset(s_priority,0,32);draw_scene_sprites(&v,0,out,32,0,pal);
        CHECK(out[4]==pal[1] && !out[5] && out[6]==pal[1]);
        scene.sprites[0].attr=(uint16_t)(a->tile+a->bytes/32);
        memset(out,0,sizeof out);memset(s_priority,0,32);draw_scene_sprites(&v,0,out,32,0,pal);
        CHECK(out[4]==pal[2]); /* unrelated tiles keep their live owner */
        scene.sprites[0].attr=(uint16_t)a->tile;scene.sprites[0].slot=0;
        memset(out,0,sizeof out);memset(s_priority,0,32);draw_scene_sprites(&v,0,out,32,0,pal);CHECK(out[4]==pal[2]);
    }
    /* Mapping capture publishes the identity with the immutable scene. */
    s_capture_frame=&s_build;memset(&s_build,0,sizeof s_build);
    word(g_rom,S3_EXPLOSION_MAP,2);word(g_rom,S3_EXPLOSION_MAP+2,1);
    memset(g_rom+S3_EXPLOSION_MAP+4,0,6);
    add_mapping(S3_EXPLOSION_MAP,0,0x5A0,4,4,0,0,0,256);
    CHECK(s_build.count==1 && (s_build.sprites[0].slot>>8)==1);
    CHECK(sprite_slot(&s_build.sprites[0])==s_capture_slot);
    /* Art identity must not obscure the object used by fractional motion. */
    SceneFrame *a=&ix_previous()->scene,*b=&ix_current()->scene;
    unsigned ac=a->code[7],bc=b->code[7];
    int16_t ax=a->ox[7],ay=a->oy[7],bx=b->ox[7],by=b->oy[7];
    double phase=s_interpolation_phase;s_interpolation_phase=.5;
    a->code[7]=b->code[7]=0x1234;a->ox[7]=100;b->ox[7]=108;a->oy[7]=50;b->oy[7]=54;
    SceneSprite moving={0,0,0,0,0,256,0x107};int dx=0,dy=0;
    presentation_object_delta(&moving,&dx,&dy);CHECK(dx==-4 && dy==-2);
    a->code[7]=ac;b->code[7]=bc;a->ox[7]=ax;a->oy[7]=ay;b->ox[7]=bx;b->oy[7]=by;
    s_interpolation_phase=phase;
    uint8_t pixel=99;CHECK(!native_art_pixel(65535,0,0,0,&pixel) && pixel==99);
    memset(s_native_art_cache,0,sizeof s_native_art_cache);s_display=previous;s_frame_level=level;
}
static void test_waiting_knuckles_palette(void)
{
    static GVDP v;static SceneFrame scene;uint32_t out[796],pal[64]={0};
    CHECK(configure("32:9"));width(796,224,320,224);
    memset(&v,0,sizeof v);memset(&scene,0,sizeof scene);
    v.cram[17]=0xEEE;memset(v.vram+32,0x22,32);
    word(g_rom,S3_KNUX_PREVIEW_PALETTE+4,0xE00);
    pal[18]=0xFFFF0000;scene.scene=1;scene.count=1;
    scene.sprites[0]=(SceneSprite){-230,0,0x2001,0,0,256,0x500};
    s_display=&scene;s_frame_level=1;s_camera=s_camera_y=0;
    GVDP original=v;static uint8_t ram[65536];memcpy(ram,g_ram,sizeof ram);
    memset(out,0,sizeof out);memset(s_priority,0,796);
    draw_scene_sprites(&v,0,out,796,238,pal);
    CHECK(out[8]==genesis_dac_cram_to_argb(0xE00,GENESIS_DAC_NORMAL));
    scene.sprites[0].x=8;memset(out,0,sizeof out);memset(s_priority,0,796);
    draw_scene_sprites(&v,0,out,796,238,pal);
    CHECK(out[246]==pal[18]);
    CHECK(!memcmp(&v,&original,sizeof v) && !memcmp(ram,g_ram,sizeof ram));
    s_display=NULL;CHECK(configure("off"));
}
static void test_hcz_rush_extent(void)
{
    static GVDP v;uint32_t out[1603],pal[64]={0};
    CHECK(configure("32:9"));g_ram[0xFE10]=1;g_ram[0xFE11]=0;
    word(g_ram,0xEE78,0x680);
    memset(&v,0,sizeof v);memset(v.vram+0x392*32,0x11,16*32);
    memset(v.vram+0x3AA*32,0x22,16*32);pal[33]=0xFF123456;pal[34]=0xFF654321;
    const SceneFrame *old=s_display;s_display=&s_build;
    for(unsigned frame=0;frame<4;++frame) {
        unsigned p=S3_HCZ_RUSH_MAP+0x10+frame*16;
        word(g_rom,S3_HCZ_RUSH_MAP+frame*2,p-S3_HCZ_RUSH_MAP);word(g_rom,p,2);
        word(g_rom,p+2,0xE00F);word(g_rom,p+4,0x18);word(g_rom,p+6,frame<2?0x50:0x30);
        word(g_rom,p+8,0x000F);word(g_rom,p+10,0x30);word(g_rom,p+12,frame<2?0x60:0x20);
        for(unsigned width=796;width<=1603;width+=807) {
            s_requested_width=(int)width;memset(&s_build,0,sizeof s_build);
            s_build.scene=1;s_build.camera_x=s_camera=0x680;s_camera_y=0;
            extend_hcz_rush(S3_HCZ_RUSH_MAP,frame,0x437A,4,0x40,80,256);
            CHECK(s_build.count>0);
            int origin=((int)width-320)/2;
            for(unsigned row=0;row<2;++row) {
                memset(out,0,sizeof out);memset(s_priority,0,width);
                draw_scene_sprites(&v,row?80:48,out,(int)width,origin,pal);
                for(int x=0;x<(int)width;++x) {
                    int nx=x-origin;
                    CHECK(out[x]==(nx>=320 && nx<0x300?pal[33+row]:0));
                }
            }
        }
    }
    /* The closed current uses its authored two-row vertical extent. Its
     * higher opening must remain dry before the water starts moving. */
    memset(out,0,sizeof out);memset(s_priority,0,1603);
    draw_scene_sprites(&v,16,out,1603,641,pal);
    CHECK(!out[641+350]);
    /* Moving water still fills the raised opening, clipped by real roof. */
    memset(&s_build,0,sizeof s_build);s_build.scene=1;
    s_build.camera_x=s_camera=0x680;s_camera_y=0;
    extend_hcz_rush(S3_HCZ_RUSH_MAP,0,0x437A,4,0x40,80,256);
    memset(out,0,sizeof out);memset(s_priority,0,1603);
    s_priority[641+400]=4;
    draw_scene_sprites(&v,16,out,1603,641,pal);
    CHECK(out[641+350]==pal[33] && !out[641+400]);
    unsigned count=s_build.count;s_requested_width=320;
    extend_hcz_rush(S3_HCZ_RUSH_MAP,0,0x437A,4,0x40,80,256);CHECK(s_build.count==count);
    s_requested_width=796;g_ram[0xFE11]=1;
    extend_hcz_rush(S3_HCZ_RUSH_MAP,0,0x437A,4,0x40,80,256);CHECK(s_build.count==count);
    g_ram[0xFE11]=0;
    extend_hcz_rush(S3_HCZ_RUSH_MAP,0,0x437A,5,0x40,80,256);CHECK(s_build.count==count);
    s_display=old;CHECK(configure("off"));
}
static void test_hcz_brick_fractional_phase(void)
{
    static GVDP v;uint32_t out[796],palette[64]={0};
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    memset(&v,0,sizeof v);memset(&s_build,0,sizeof s_build);
    for(unsigned col=0;col<4;col++)for(unsigned row=0;row<4;row++)
        memset(v.vram+(1+col*4+row)*32,(int)((col+1)*17),32);
    for(unsigned n=1;n<=4;n++)palette[n]=0xFF000000u+n;
    s_build.scene=1;s_build.count=1;s_build.sprites[0].x=320;
    s_build.sprites[0].y=0;s_build.sprites[0].size=0x2F;
    s_build.sprites[0].attr=1;s_build.sprites[0].zoom=256;s_build.sprites[0].slot=1;
    const SceneFrame *old=s_display;s_display=&s_build;
    memset(&ix_previous()->scene,0,sizeof ix_previous()->scene);
    memset(&ix_current()->scene,0,sizeof ix_current()->scene);
    memset(ix_current()->pattern_bytes,0,sizeof ix_current()->pattern_bytes);
    memset(ix_current()->sprite_lines,0,sizeof ix_current()->sprite_lines);
    ix_previous()->scene.code[1]=ix_current()->scene.code[1]=1;
    ix_previous()->scene.ox[1]=0;ix_current()->scene.ox[1]=32;
    s_camera=s_camera_y=0;s_interpolating=1;
    for(unsigned phase=1;phase<4;phase++) {
        s_interpolation_phase=phase*.25;
        memset(out,0,sizeof out);memset(s_priority,0,796);
        draw_scene_sprites(&v,0,out,796,238,palette);
        /* Advancing a32px repeating brick field must not slide its texture
         * by8/16/24px between native frames. Its leading edge still moves. */
        CHECK(out[558]==palette[1]);
    }
    s_interpolating=0;s_display=old;CHECK(configure("off"));
}
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
static void test_level_select_word_edges(void)
{
    static GVDP v;uint32_t native[320],out[1603];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("32:9"));g_ram[0xF600]=0x28;
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    for(unsigned t=0;t<6;++t) {
        memset(v.vram+t*32,(int)(t+1)*17,32);v.cram[49+t]=(uint16_t)(t*0x222);
    }
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)
        word(v.vram,0xE000+(y*64+x)*2,0x6000+(x<40?(x+1)%6:5));
    for(unsigned x=0;x<320;++x)native[x]=0xFF123456;
    memset(&s_build,0,sizeof s_build);publish_sprites();
    const int canvases[]={320,398,523,796,1603};
    for(unsigned c=0;c<5;++c)for(int scroll=0;scroll<48;scroll+=7) {
        word(v.vram,0xF002,(unsigned)-scroll);
        scanline(&v,0,native,320,out,canvases[c]);int origin=(canvases[c]-320)/2;
        for(int x=0;x<canvases[c];++x) {
            int nx=x-origin,bx=(nx+scroll)%48;if(bx<0)bx+=48;
            uint32_t expected=genesis_dac_cram_to_argb(v.cram[49+(bx/8+1)%6],GENESIS_DAC_NORMAL);
            if(out[x]!=(nx>=0 && nx<320?native[nx]:expected))
                fprintf(stderr,"level-select width%d scroll%d x%d actual%08x expected%08x layout%d level%d\n",canvases[c],scroll,x,out[x],expected,level_select_layout(&v),s_frame_level);
            CHECK(out[x]==(nx>=0 && nx<320?native[nx]:expected));
        }
    }
    /* Native mode/zone assignments happen before the outgoing menu fades.
     * Neither title reflection nor a bonus renderer owns those old planes. */
    const unsigned destinations[][2]={{4,0},{0x8C,0x1300},{0x8C,0x1400},{0x8C,0x1500}};
    word(v.vram,0xF002,0);
    for(unsigned scroll_mode=0;scroll_mode<2;++scroll_mode)
    for(unsigned d=0;d<sizeof destinations/sizeof *destinations;++d) {
        v.reg[11]=scroll_mode?3:0;
        g_ram[0xF600]=(uint8_t)destinations[d][0];word(g_ram,0xFE10,destinations[d][1]);
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;++x) {
            int nx=x-238,bx=nx%48;if(bx<0)bx+=48;
            uint32_t expected=genesis_dac_cram_to_argb(v.cram[49+(bx/8+1)%6],GENESIS_DAC_NORMAL);
            CHECK(out[x]==(nx>=0 && nx<320?native[nx]:expected));
        }
        CHECK(!s_frame_bonus);
    }
    CHECK(configure("off"));
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
        v.reg[1]=64;v.cram[1]=0xE;
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
        /* The installed bonus survives an early mode/zone assignment, then
         * retires at the completed native fade, including full white. */
        g_ram[0xFE10]=0;g_ram[0xF600]=0x0C;
        scanline(&v,0,native,320,out,796);CHECK(s_frame_bonus && !s_frame_level);
        g_ram[0xF600]=0x8C;
        scanline(&v,0,native,320,out,796);CHECK(s_frame_bonus && !s_frame_level);
        for(unsigned i=0;i<64;i++)v.cram[i]=0xEEE;
        scanline(&v,0,native,320,out,796);
        CHECK(!s_frame_bonus);
        for(unsigned x=0;x<796;x++)CHECK(out[x]==genesis_dac_cram_to_argb(0xEEE,GENESIS_DAC_NORMAL));
    }
    CHECK(configure("off"));
}
static void test_bonus_margins(void)
{
    static GVDP v;uint32_t native[320],out[796],palette[64],shadow[64];
    memset(&v,0,sizeof v);v.reg[1]=64;v.reg[4]=7;v.reg[16]=1;v.reg[13]=0x3F;v.reg[11]=3;
    v.cram[1]=0xE;v.cram[2]=0xE00;
    for(unsigned n=0;n<64;n++) {
        palette[n]=genesis_dac_cram_to_argb(v.cram[n],GENESIS_DAC_NORMAL);
        shadow[n]=genesis_dac_cram_to_argb(v.cram[n],GENESIS_DAC_SHADOW);
    }
    memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
    for(unsigned n=0;n<64*32;n++)word(v.vram,0xE000+n*2,1);
    word(v.vram,0xE000+63*2,2);
    for(unsigned n=0;n<320;n++)native[n]=0xFF123400u+n;
    bonus_scanline(&v,0,0x15,native,320,out,796,palette,shadow,NULL);
    CHECK(out[237]==palette[2] && out[558]==palette[1]);
    for(unsigned n=0;n<320;n++)CHECK(out[238+n]==native[n]);
    /* Use the installed per-row scroll, not a stretched finite native image. */
    word(v.vram,0xFC00+8*4+2,8);
    bonus_scanline(&v,8,0x14,native,320,out,796,palette,shadow,NULL);
    CHECK(out[237]==palette[1]);
    test_private_planes=1;
    bonus_scanline(&v,80,0x14,native,320,out,796,palette,shadow,NULL);
    CHECK(out[558]==genesis_dac_cram_to_argb(0xE0,GENESIS_DAC_NORMAL));
    v.reg[12]=8;
    bonus_scanline(&v,80,0x14,native,320,out,796,palette,shadow,NULL);
    CHECK(out[558]==genesis_dac_cram_to_argb(0xE0,GENESIS_DAC_SHADOW));
    test_private_planes=0;
}
static void test_gumball_authored_extent(void)
{
    static GVDP v;uint32_t native[320],out[796],palette[64],shadow[64];
    memset(&v,0,sizeof v);memset(s_world_frame,0,sizeof s_world_frame);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[13]=0x3C;v.reg[16]=1;v.reg[11]=7;
    v.cram[1]=0xE;v.cram[2]=0xE00;v.cram[3]=0xE0;
    for(unsigned i=0;i<64;++i)palette[i]=shadow[i]=genesis_dac_cram_to_argb(v.cram[i],GENESIS_DAC_NORMAL);
    for(unsigned i=1;i<4;++i)memset(v.vram+i*32,(int)(i*17),32);
    for(unsigned i=0;i<64*32;++i)word(v.vram,0xE000+i*2,2);
    word(v.vram,0xF000,(unsigned)-96);
    word(s_world_frame,0x8000,4);word(s_world_frame,0x8002,4);
    word(s_world_frame,0x8008,0x8100);word(s_world_frame,0x8028,0x8140);
    memset(s_world_frame+0x8100,1,4);memset(s_world_frame+0x8140,2,4);
    for(unsigned i=0;i<64;++i){word(s_world_frame,128+i*2,1);word(s_world_frame,256+i*2,3);}
    for(unsigned i=0;i<4;++i){word(s_world_frame,0x9008+i*2,1);word(s_world_frame,0x9018+i*2,3);}
    s_frame_fg_x=96;s_frame_fg_y=1024;s_frame_bg_y=0;
    for(unsigned i=0;i<320;++i)native[i]=0xFF123400u+i;
    bonus_scanline(&v,0,0x13,native,320,out,796,palette,shadow,NULL);
    for(int x=0;x<796;++x) {
        int nx=x-238,wx=nx+96;
        CHECK(out[x]==(nx>=0 && nx<320?native[nx]:wx>=0 && wx<512?palette[1]:palette[2]));
    }
    /* The authored outer walls wrap their eight layout rows. The entire
     * native chamber, including its independent machine column, is intact. */
}
static int bonus_world_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)y;CHECK(x>=0 && x<(background?768:640));
    *cell=background?(x>=512?3:2):1;return 1;
}
static void test_bonus_authored_planes(void)
{
    static GVDP v;uint32_t native[320],out[796],palette[64],shadow[64];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    memset(s_world_frame,0,sizeof s_world_frame);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[16]=1;v.reg[13]=0x3C;
    v.cram[1]=0xE;v.cram[2]=0xE00;v.cram[3]=0xE0;
    for(unsigned i=0;i<64;i++)palette[i]=shadow[i]=genesis_dac_cram_to_argb(v.cram[i],GENESIS_DAC_NORMAL);
    for(unsigned n=1;n<4;n++)memset(v.vram+n*32,(int)(n*17),32);
    for(unsigned n=0;n<64*32;n++)word(v.vram,0xE000+n*2,2);
    word(v.vram,0xF000,(unsigned)-160);word(v.vram,0xF002,(unsigned)-96);
    word(s_world_frame,0x8000,5);word(s_world_frame,0x8002,6);
    for(unsigned n=1;n<4;n++)for(unsigned i=0;i<4;i++)word(s_world_frame,0x9000+n*8+i*2,n);
    s_frame_fg_x=160;s_frame_fg_y=0;s_frame_bg_y=0;
    for(unsigned n=0;n<320;n++)native[n]=0xFF123400u+n;
    s3_video_set_world_cell(bonus_world_cell);
    bonus_scanline(&v,0,0x14,native,320,out,796,palette,shadow,NULL);
    /* Finite foreground crosses the native edge; decorative B wraps its
     * authored768px map instead of the partially installed512px cache. */
    CHECK(out[237]==palette[1]);
    CHECK(out[0]==palette[3] && out[795]==palette[3]);
    for(unsigned n=0;n<320;n++)CHECK(out[238+n]==native[n]);
    /* Glowing Sphere reaches camera B=-4 at its left wall. A 16-bit
     * unsigned reference adds 65536, which changes the phase of its 768px
     * authored map by 256px and selects an unrelated strip in the margin. */
    word(s_world_frame,0x8000,0);word(s_world_frame,0xEE8C,0xFFFC);
    word(v.vram,0xF002,4);
    bonus_scanline(&v,0,0x14,native,320,out,796,palette,shadow,NULL);
    CHECK(out[0]==palette[3]);
    for(unsigned n=0;n<320;n++)CHECK(out[238+n]==native[n]);
    s3_video_set_world_cell(NULL);
}
static void test_glowing_ring_ownership(void)
{
#ifndef SONIC3_STANDALONE
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);g_ram[0xF600]=12;word(g_ram,0xFE10,0x1400);
    write32(0xB000,0x1234);word(g_ram,0xEE80,160);word(g_ram,0xEEAA,0xFFF);
    const unsigned pointer=S3_RINGS+0x14*8,table=0x180000;
    uint8_t old_pointer[4],old_map[8],old_table[20];
    memcpy(old_pointer,g_rom+pointer,4);memcpy(old_map,g_rom+S3_RING_MAP,8);
    memcpy(old_table,g_rom+table,20);s3_video_set_ring_layout(NULL);
    longword(g_rom,pointer,table);
    const unsigned xs[]={64,400,600,800};
    for(unsigned i=0;i<4;++i){word(g_rom,table+i*4,xs[i]);word(g_rom,table+i*4+2,100);}
    longword(g_rom,table+16,0xFFFFFFFFu);
    word(g_rom,S3_RING_MAP,0);word(g_rom,S3_RING_MAP+2,0);
    word(g_rom,S3_RING_MAP+4,1);word(g_rom,S3_RING_MAP+6,0);
    /* The vertically sorted bonus list has no leading sentinel and owns
     * its separate status table. Do not invent rings after collection. */
    word(g_ram,0x8604,0x8000);uint8_t before[65536];memcpy(before,g_ram,sizeof before);
    capture_objects();CHECK(s_build.scene==3 && s_build.count==2);
    CHECK(s_build.sprites[0].x==-96 && s_build.sprites[1].x==240);
    CHECK(!s_build.sprites[0].slot && !s_build.sprites[0].hud);
    CHECK(!memcmp(before,g_ram,sizeof before));
    word(g_ram,0x8600,0x8000);capture_objects();
    CHECK(s_build.count==1 && s_build.sprites[0].x==240);
    memcpy(g_rom+pointer,old_pointer,4);memcpy(g_rom+S3_RING_MAP,old_map,8);
    memcpy(g_rom+table,old_table,20);CHECK(configure("off"));
#endif
}
static void test_slots_complete_board(void)
{
#ifndef SONIC3_STANDALONE
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&s_build,0,sizeof s_build);
    word(g_rom,0x1D64,0);word(g_rom,0x1D64+128,256);
    word(g_ram,0xEE78,960);word(g_ram,0xEE7C,960);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
        word(g_ram,0x7800+(y*16+x)*4,(unsigned)((int)x*24-180));
        word(g_ram,0x7802+(y*16+x)*4,(unsigned)((int)y*24-180));
    }
    g_ram[0x3000+43*128+32]=g_ram[0x3000+43*128+63]=1;
    write32(0x7008,0x10000);word(g_ram,0x700C,0);word(g_ram,0x700E,1);
    word(g_rom,0x10000,0x10);g_rom[0x10010]=1;
    g_rom[0x10011]=0xF4;g_rom[0x10012]=0xA;word(g_rom,0x10013,0);g_rom[0x10015]=0xF4;
    s_build.scene=3;capture_slots_board();
    CHECK(s_build.count==2);
    CHECK(s_build.sprites[0].x==-224 && s_build.sprites[1].x==520);
    CHECK(s_build.sprites[0].y==-8 && s_build.sprites[1].y==-8);
    CHECK(s_build.sprites[0].attr==1 && s_build.sprites[1].attr==1);
    /* A partially initialized matrix cannot publish a different board pose. */
    s_build.count=0;word(g_ram,0xEE78,961);capture_slots_board();CHECK(!s_build.count);
    word(g_ram,0xEE78,960);g_ram[0xF600]=12;g_ram[0xFE10]=0x15;g_ram[0xF62A]=8;
    g_ram[0xF806]=0x55;capture_slots_vblank();
    CHECK(s_build.count==0 && s_history[s_serial%3].count==2);
    CHECK(s_history[s_serial%3].sat[6]==0x55);
    capture_slots_vblank();
    CHECK(s_build.count==0 && s_history[s_serial%3].count==2);
    CHECK(configure("off"));
#endif
}
static void test_bonus_entry_publication(void)
{
    GVDP v={0};v.reg[5]=0x7C;
    memset(g_ram,0,sizeof g_ram);CHECK(configure("off"));CHECK(configure("32:9"));
    g_ram[0xF600]=12;g_ram[0xFE10]=1;
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.count=1;
    s_build.sprites[0].x=10;publish_sprites();
    memcpy(v.vram+0xF800,g_ram+0xF800,640);select_scene(&v,0);
    CHECK(s_display && s_display->sprites[0].x==10);
    /* The last level SAT can finish DMA after the bonus zone is assigned.
     * Retain its matching publication, not the earlier displayed pose. */
    s_build.sprites[0].x=20;g_ram[0xF806]=1;publish_sprites();
    memcpy(v.vram+0xF800,g_ram+0xF800,640);g_ram[0xFE10]=0x13;
    select_scene(&v,1);CHECK(s_display && s_display->scene==1 && s_display->sprites[0].x==20);
    /* A premature bonus tag with the same installed SAT contains no level
     * mappings. It must not replace the outgoing level during its fade. */
    s_build.scene=3;s_build.count=0;publish_sprites();
    select_scene(&v,1);CHECK(s_display && s_display->scene==1 && s_display->sprites[0].x==20);
    v.vram[0xF806]^=2;select_scene(&v,1);
    CHECK(s_display && s_display->scene==1 && s_display->sprites[0].x==20);
    CHECK(configure("off"));
}
static void test_native_demo_publication(void)
{
    GVDP v={0};v.reg[5]=0x7C;
    memset(g_ram,0,sizeof g_ram);CHECK(configure("off"));CHECK(configure("32:9"));
    g_ram[0xF600]=12;g_ram[0xFE10]=1;
    write32(0xB000,1);word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    s_loader_active=0;
    word(g_ram,S3_DEMO,1);
    g_ram[0xF711]=1;g_ram[0xF600]=8;CHECK(s3_video_main_cpu_divisor()==1);
    /* Wide drawing must not alter the native demo's loader, ring interval,
     * cull result or simulated render-visible flag. */
    g_cpu.D[0]=0x12345678u;g_cpu.A[1]=0x1234;g_cpu.A[2]=0x5678;g_cpu.A[4]=0x9ABC;
    CHECK(!s3_video_hook(S3_LOAD_UPDATE));CHECK(!s_loader_active);
    s3_video_hook(S3_RING_RANGE);s3_video_hook(S3_RING_STATUS);s3_video_hook(S3_SWING_PLATFORM_CULL);
    CHECK(g_cpu.D[0]==0x12345678u && g_cpu.A[1]==0x1234 &&
          g_cpu.A[2]==0x5678 && g_cpu.A[4]==0x9ABC);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.count=1;
    s_visible_count=1;s_visible_objects[0]=0xB000;g_ram[0xB004]=4;
    s_build.sprites[0].x=10;publish_sprites();CHECK(g_ram[0xB004]==4);
    memcpy(v.vram+0xF800,g_ram+0xF800,640);select_scene(&v,0);
    CHECK(s_display && s_display->sprites[0].x==10);
    /* A real Start changes the mode before the last outgoing level SAT is
     * queued. Prefer that newly installed pose, then hold it during fade. */
    s_frame_level=1;s_frame_zone=1;s_frame_act=0;g_ram[0xF600]=4;
    capture_objects();CHECK(s_build.scene==1 && !s_build.title);
    s_build.count=1;s_build.sprites[0].x=20;g_ram[0xF806]=1;publish_sprites();
    memcpy(v.vram+0xF800,g_ram+0xF800,640);select_scene(&v,1);
    CHECK(s_display && s_display->scene==1 && s_display->sprites[0].x==20);
    v.vram[0xF806]^=2;select_scene(&v,1);
    CHECK(s_display && s_display->sprites[0].x==20);
    word(g_ram,S3_DEMO,0);g_ram[0xF600]=12;
    CHECK(s3_video_main_cpu_divisor()==4);
    s_visible_count=1;s_visible_objects[0]=0xB000;publish_sprites();
    CHECK(g_ram[0xB004]==132); /* Ordinary gameplay retains wide visibility. */
    /* A margin-only waiting enemy must not upload over a returning card.
     * Native SAT visibility still starts it; wide visibility resumes when
     * the card has retired. */
    write32(0xB000,S3_WAIT_ONSCREEN);g_ram[0xB004]=4;
    write32(0xB0DE,1);write32(0xB0EA,S3_TITLE_CARD_MAP);
    publish_sprites();CHECK(g_ram[0xB004]==4);
    g_ram[0xB004]=132;publish_sprites();CHECK(g_ram[0xB004]==132);
    write32(0xB0DE,0);g_ram[0xB004]=4;
    publish_sprites();CHECK(g_ram[0xB004]==132);
    CHECK(configure("off"));
}
static void test_title_card_edges(void)
{
    GVDP v={0};uint32_t native[320],out[1603];
    memset(g_ram,0,sizeof g_ram);CHECK(configure("off"));CHECK(configure("32:9"));
    g_ram[0xF600]=0x88;g_ram[0xFE10]=2;
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=14;
    memset(v.vram+32,0x11,32);
    for(unsigned n=0;n<320;++n)native[n]=0xFF123456u;
    unsigned o=0xB172;write32(o,0x9000);write32(o+0xC,S3_TITLE_CARD_MAP);
    write16(o+0x10,448);write16(o+0x14,128);
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,(uint16_t)o);
    unsigned p=S3_TITLE_CARD_MAP+0x40;
    word(g_rom,p,1);g_rom[p+2]=g_rom[p+3]=0;
    word(g_rom,p+4,1);word(g_rom,p+6,0);
    const unsigned frames[]={6,3,2,1},targets[]={0x120,0x17C,0x184,0xC0};
    for(unsigned n=0;n<4;++n) {
        word(g_rom,S3_TITLE_CARD_MAP+frames[n]*2,0x40);
        g_ram[o+0x22]=(uint8_t)frames[n];write16(o+0x46,(uint16_t)targets[n]);
        capture_objects();CHECK(s_build.count==1 && s_build.sprites[0].hud==6+n);
    }
    g_ram[o+0x22]=6;write16(o+0x46,0x120);
    capture_objects();publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    /* A mapping whose native left reaches320 must reach the requested right
     * edge. This uses finite8px art, including odd adaptive canvas widths. */
    const int canvases[]={320,398,796,1603};
    for(unsigned n=0;n<4;++n) {
        scanline(&v,0,native,320,out,canvases[n]);CHECK(s_frame_menu==MENU_TITLE_CARD);
        for(int x=0;x<canvases[n];++x)CHECK(out[x]==0xFF000000u);
    }
    /* The next entering pixel is already at the expanded edge, rather than
     * first appearing at the old320px clip. No native clipped copy remains. */
    write16(o+0x10,447);capture_objects();publish_sprites();
    scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;++x)CHECK(out[x]==(x>=793?0xFFFF0000u:0xFF000000u));
    write16(o+0x10,0x120);capture_objects();publish_sprites();
    scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;++x)CHECK(out[x]==(x>=398 && x<406?0xFFFF0000u:0xFF000000u));
    /* Native SAT lists the name before the red banner, so its opaque text
     * stays in front where they overlap. Preserve that first-sprite rule. */
    s_display_frame.count=2;s_display_frame.sprites[1]=s_display_frame.sprites[0];
    s_display_frame.sprites[1].hud=9;s_display_frame.sprites[1].slot=7;
    s_display_frame.sprites[1].attr=2;memset(v.vram+64,0x22,32);v.cram[2]=0xE00;
    uint32_t colors[64];for(unsigned i=0;i<64;++i)colors[i]=genesis_dac_cram_to_argb(v.cram[i],GENESIS_DAC_NORMAL);
    memset(s_priority,0,796);for(int x=0;x<796;++x)out[x]=0xFF000000u;
    draw_scene_sprites(&v,0,out,796,238,colors);
    for(int x=0;x<796;++x)CHECK(out[x]==(x>=398 && x<406?0xFFFF0000u:0xFF000000u));
    size_t bytes=presentation_state_save(NULL,0);uint8_t *blob=(uint8_t*)malloc(bytes);CHECK(blob);
    CHECK(presentation_state_save(blob,bytes)==bytes);
    s_frame_menu=MENU_NONE;s_display_frame.sprites[0].hud=0;s_display=NULL;
    CHECK(presentation_state_load(blob,bytes,1));free(blob);
    CHECK(s_frame_menu==MENU_TITLE_CARD && s_display==&s_display_frame);
    CHECK(s_display->sprites[0].hud==6 && s_display->sprites[1].hud==9);
    memset(s_priority,0,796);for(int x=0;x<796;++x)out[x]=0xFF000000u;
    draw_scene_sprites(&v,0,out,796,238,colors);
    for(int x=0;x<796;++x)CHECK(out[x]==(x>=398 && x<406?0xFFFF0000u:0xFF000000u));
    CHECK(title_card_black(&v));word(v.vram,0xC000,1);CHECK(!title_card_black(&v));
    word(v.vram,0xC000,0);v.vram[0]=1;CHECK(!title_card_black(&v));v.vram[0]=0;
    /* A bonus card with a different authored target keeps its native layout. */
    write16(o+0x46,0xC8);capture_objects();CHECK(s_build.sprites[0].hud==2);
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
    word(v.vram,0xE000+128+10*2,0x829F);
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
        for(unsigned x=0;x<320;x++)raster.plane_a[x]=(uint8_t)(64+(x<160?1:2));
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
static void test_competition_character_palette(void)
{
    static GVDP v;static SceneFrame scene;uint32_t out[796],pal[64]={0};
    memset(&v,0,sizeof v);memset(&scene,0,sizeof scene);
    const SceneFrame *old=s_display;s_display=&scene;s_frame_menu=MENU_COMPETITION;
    scene.count=1;scene.sprites[0]=(SceneSprite){-40,0,0x4001,0,SCENE_COMPETITION_CHARACTER,256,1};
    memset(v.vram+32,0x22,32);word(g_rom,S3_ART_BANK+0xA55E + (34-16)*2,0x84E);
    v.cram[34]=0xE22;pal[34]=genesis_dac_cram_to_argb(v.cram[34],GENESIS_DAC_NORMAL);
    /* Full, incoming and outgoing native white steps. The source portrait
     * remains red when the live bank is replaced by a blue stage palette. */
    const unsigned white[]={0xEEE,0x400,0xE20,0xEE,0xE,0};
    const unsigned expected[]={0x84E,0x400,0x844,0xC,0,0};
    for(unsigned phase=0;phase<sizeof white/sizeof white[0];phase++) {
        v.cram[17]=(uint16_t)white[phase];memset(out,0,sizeof out);memset(s_priority,0,796);
        draw_scene_sprites(&v,0,out,796,238,pal);
        CHECK(out[198]==genesis_dac_cram_to_argb((uint16_t)expected[phase],GENESIS_DAC_NORMAL));
        CHECK(out[237]==0 && out[238]==0);
    }
    /* Low-priority characters retain native shadow/highlight dimming after
     * their card leaves the hardware viewport. The selected sprite stays lit. */
    v.cram[17]=0xEEE;v.reg[12]=8;
    memset(out,0,sizeof out);memset(s_priority,0,796);
    draw_scene_sprites(&v,0,out,796,238,pal);
    CHECK(out[198]==genesis_dac_cram_to_argb(0x84E,GENESIS_DAC_SHADOW));
    scene.sprites[0].attr|=0x8000;memset(out,0,sizeof out);memset(s_priority,0,796);
    draw_scene_sprites(&v,0,out,796,238,pal);
    CHECK(out[198]==genesis_dac_cram_to_argb(0x84E,GENESIS_DAC_NORMAL));
    scene.sprites[0].attr&=~0x8000;v.reg[12]=0;
    /* An identical tile in unrelated mappings keeps its installed palette. */
    scene.sprites[0].hud=3;memset(out,0,sizeof out);memset(s_priority,0,796);
    draw_scene_sprites(&v,0,out,796,238,pal);CHECK(out[198]==pal[34]);
    /* Private atlases retain their own palette instead of borrowing retail colours. */
    scene.sprites[0].hud=SCENE_COMPETITION_CHARACTER;test_private_planes=1;
    memset(v.vram+32,0x11,32);memset(out,0,sizeof out);memset(s_priority,0,796);
    draw_scene_sprites(&v,0,out,796,238,pal);
    CHECK(out[198]==genesis_dac_cram_to_argb(0xE,GENESIS_DAC_NORMAL));
    test_private_planes=0;s_display=old;s_frame_menu=MENU_NONE;
}
static void test_competition_race(void)
{
    static GVDP v,before;uint32_t native[320],out[796];static uint8_t ram[65536];
    CHECK(configure("off"));CHECK(configure("32:9"));width(1,1,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);presentation_reset();
    s3_video_set_world_cell(NULL);s3_video_set_world_publish(NULL);
    g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xFE10]=14;
    word(g_ram,S3_COMPETITION,1);word(g_ram,0xEEAE,12);
    word(g_ram,0x8000,8);word(g_ram,0x8002,8);
    for(unsigned row=0;row<4;++row) {
        word(g_ram,0x8008+row*4,0x8100+row*8);
        word(g_ram,0x800A+row*4,0x8200+row*8);
        memset(g_ram+0x8200+row*8,(int)(row%3)+1,8);
    }
    for(unsigned n=1;n<=3;++n) {
        for(unsigned i=0;i<128;i+=2)word(g_ram,n*128+i,n);
        for(unsigned i=0;i<8;i+=2)word(g_ram,0x9000+n*8+i,n);
        memset(v.vram+n*32,(int)(n*17),32);
    }
    v.reg[1]=64;v.reg[2]=0x20;v.reg[4]=5;v.reg[5]=0x7C;
    v.reg[11]=3;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=3;
    v.cram[1]=14;v.cram[2]=0xE0;v.cram[3]=0xE00;
    for(unsigned i=0;i<320;++i)native[i]=0xFF010203u+i;
    memcpy(ram,g_ram,sizeof ram);before=v;
    scanline(&v,0,native,320,out,796);
    CHECK(out[0]==0xFFFF0000u && !memcmp(out+238,native,sizeof native));
    CHECK(!memcmp(g_ram,ram,sizeof ram) && !memcmp(&v,&before,sizeof v));
    /* The second view installs a different VSRAM during the same frame.
     * It must not reuse line zero's background or a guessed split row. */
    v.vsram[1]=128;scanline(&v,1,native,320,out,796);
    CHECK(out[0]==0xFF00FF00u && !memcmp(out+238,native,sizeof native));
    v.vsram[1]=16;scanline(&v,112,native,320,out,796);
    CHECK(out[0]==0xFF00FF00u && !memcmp(out+238,native,sizeof native));
    /* Competition's short row mask wraps its map; ordinary levels keep
     * their existing 4096px mask. Foreground terrain extends in each view. */
    CHECK(world_attr_blocks_mask(g_ram,g_ram+0x9000,0,512,1,12)==1);
    CHECK(world_attr(g_ram,0,512,1)==0);
    memset(g_ram+0x8100,3,8);v.vsram[1]=0;
    scanline(&v,0,native,320,out,796);CHECK(out[558]==0xFF0000FFu);
    for(unsigned i=0;i<8;i+=2)word(g_ram,0x9008+i,0x8001);
    scanline(&v,0,native,320,out,796);CHECK(out[558]==0xFFFF0000u);
    for(unsigned i=0;i<8;i+=2)word(g_ram,0x9018+i,0x8003);
    scanline(&v,0,native,320,out,796);CHECK(out[558]==0xFF0000FFu);
    /* Keep a native SAT piece whole when it crosses the viewport edge.
     * Its original ordering/priority applies only to the added pixels. */
    memset(g_ram+0x8100,0,8);word(v.vram,0xF800,128);
    word(v.vram,0xF804,0x8003);word(v.vram,0xF806,128+318);
    scanline(&v,0,native,320,out,796);
    CHECK(out[557]==native[319] && out[558]==0xFF0000FFu && out[564]==0xFFFF0000u);
    /* Time Attack chooses its menu before the outgoing race fades. Keep
     * both view scrolls and sprite ownership until the native loader clears. */
    g_ram[0xF600]=0xC0;memcpy(ram,g_ram,sizeof ram);
    scanline(&v,0,native,320,out,796);
    CHECK(competition_world_ready() && out[558]==0xFF0000FFu);
    CHECK(!memcmp(out+238,native,sizeof native) && !memcmp(g_ram,ram,sizeof ram));
    word(g_ram,S3_COMPETITION,0);scanline(&v,0,native,320,out,796);
    CHECK(!competition_world_ready());
    CHECK(configure("off"));
}
static void test_competition_pre_cull(void)
{
    static GVDP v;static uint8_t before[65536];uint32_t native[320],out[796];
    CHECK(configure("off"));CHECK(configure("32:9"));width(1,1,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);presentation_reset();
    g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xFE10]=14;
    word(g_ram,S3_COMPETITION,1);word(g_ram,0xEEA8,1023);word(g_ram,0xEEAA,511);word(g_ram,0xEEAE,12);
    word(g_ram,0x8000,8);word(g_ram,0x8002,8);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,400);word(g_ram,0xEE6C,16);word(g_ram,0xEF3C,65535);
    word(g_ram,0xAC00,4);word(g_ram,0xAC02,0xB000);word(g_ram,0xAC04,0xB094);
    for(unsigned n=0;n<2;++n) {
        unsigned o=0xB000+n*0x94,map=0x1000+n*16;
        longword(g_ram,o,0x1234);g_ram[o+4]=(uint8_t)(4+(n?8:0));g_ram[o+6]=16;
        longword(g_ram,o+12,map);word(g_ram,o+10,0x8000);word(g_ram,o+16,n?730:350);word(g_ram,o+20,n?24:32);
        word(g_rom,map,2);word(g_rom,map+2,1);word(g_rom,map+4,0);word(g_rom,map+6,n?2:3);word(g_rom,map+8,0);
    }
    g_ram[0x7880+600]=42;g_ram[0x7D80+600]=92;g_ram[0xEF3E]=255;
    CHECK(competition_object_x(-864,12,1023)==160);
    CHECK(competition_object_x(-162,12,511)==-162 && competition_object_x(350,12,511)==350);
    memcpy(before,g_ram,sizeof before);s3_video_hook(S3_CAPTURE);
    CHECK(s_build.scene==4 && s_build.count==3 && !memcmp(g_ram,before,sizeof before));
    s3_video_hook(S3_COMP_PUBLISH);s3_video_vblank();
    CHECK(!memcmp(g_ram,before,sizeof before) && s_history[s_serial%3].competition_sat[600]==92);
    v.reg[1]=64;v.reg[2]=0x20;v.reg[4]=5;v.reg[5]=0x7C;
    v.reg[11]=3;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=3;
    v.cram[1]=14;v.cram[2]=0xE0;v.cram[3]=0xE00;
    memset(v.vram+64,0x22,32);memset(v.vram+96,0x33,32);
    memcpy(v.vram+0xF800,g_ram+0x7880,640);
    for(unsigned y=0;y<224;++y)word(v.vram,0xF000+y*4,(uint16_t)-400);
    for(unsigned x=0;x<320;++x)native[x]=0xFF010203u+x;
    scanline(&v,0,native,320,out,796);scanline(&v,32,native,320,out,796);
    CHECK(out[188]==0xFF0000FFu && !memcmp(out+238,native,sizeof native));
    scanline(&v,24,native,320,out,796);CHECK(out[568]==0xFF00FF00u);
    memcpy(v.vram+0xF800,g_ram+0x7D80,640);v.vsram[0]=(16-112)&1023;
    word(v.vram,0xF000+128*4,0);scanline(&v,128,native,320,out,796);
    CHECK(out[588]==0xFF0000FFu && !memcmp(out+238,native,sizeof native));
    /* The pre-cull publications, paired upload and lower camera must also
     * survive a local rewind snapshot while the race is paused. */
    size_t bytes=presentation_state_save(NULL,0);uint8_t *blob=(uint8_t*)malloc(bytes);CHECK(blob);
    CHECK(presentation_state_save(blob,bytes)==bytes);
    memset(&s_display_frame,0,sizeof s_display_frame);s_display=NULL;
    CHECK(presentation_state_load(blob,bytes,1));
    size_t page_offset=sizeof(unsigned)+sizeof(int)+sizeof(double)+offsetof(SceneFrame,competition_page)-offsetof(SceneFrame,count);
    unsigned bad_page=2;memcpy(blob+page_offset,&bad_page,sizeof bad_page);
    CHECK(!presentation_state_load(blob,bytes,0));free(blob);
    scanline(&v,128,native,320,out,796);CHECK(out[588]==0xFF0000FFu);
    CHECK(!memcmp(g_ram,before,sizeof before));
    CHECK(configure("off"));
}
static void test_competition_margin_sprites(void)
{
    static GVDP v;GVDPRasterRow raster={0};uint32_t native[320]={0},out[796];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("fit"));
    g_ram[0xF600]=0x40;
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=9;v.reg[16]=0x11;v.cram[4]=0xEEE;
    memset(v.vram+4*32,0x44,32);
    word(v.vram,0xC000+50*2,0x8004);
    write32(0xB172,0x1234);write32(0xB17E,0x1000);g_ram[0xB176]=64;
    word(g_ram,0xB182,128+316);word(g_ram,0xB186,128);g_ram[0xB194]=1;
    word(g_ram,0xB188,1);word(g_ram,0xB18A,128+330);word(g_ram,0xB18C,128);g_ram[0xB18F]=1;
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB172);
    word(g_rom,0x1002,4);word(g_rom,0x1004,1);word(g_rom,0x1006,0);
    word(g_rom,0x1008,0x8004);word(g_rom,0x100A,0);
    capture_objects();CHECK(s_build.count==2 && s_build.sprites[0].hud==3);
    publish_sprites();memcpy(v.vram+0xF800,g_ram+0xF800,640);
    raster.width=raster.count=320;raster.display=1;
    scanline_lazy(&v,0,320,out,796,test_native_row,native,&raster);
    for(unsigned x=330;x<338;x++)CHECK(out[238+x]==0xFFFFFFFFu);
    for(unsigned x=400;x<408;x++)CHECK(out[238+x]==0xFFFFFFFFu);
}
static void test_results_margin_sprites(void)
{
    static GVDP v;SceneFrame scene={0};uint32_t out[796],pal[64]={0};
    const SceneFrame *previous=s_display;
    unsigned saved_mode=s_world_frame[0xF600];
    memset(&v,0,sizeof v);memset(v.vram+32,0x11,32);pal[1]=0xFFFF0000;
    scene.scene=0;scene.count=3;
    scene.sprites[0]=(SceneSprite){-200,0,0x8001,0,2};
    scene.sprites[1]=(SceneSprite){520,0,0x8001,0,2};
    scene.sprites[2]=(SceneSprite){100,0,0x8001,0,2};
    s_display=&scene;
    for(unsigned phase=0;phase<3;phase++) {
        /* A live mode change must not alter a restored/published result.
         * Both signed off-native anchors retain eight-pixel glyphs. */
        s_world_frame[0xF600]=(uint8_t)(phase==1?4:0x48);
        memset(out,0,sizeof out);memset(s_priority,0,sizeof out/sizeof *out);
        draw_scene_sprites(&v,0,out,796,238,pal);
        for(int x=0;x<796;x++) {
            int glyph=(x>=38&&x<46)||(x>=758&&x<766);
            CHECK(out[x]==(phase!=1&&glyph?pal[1]:0));
        }
    }
    memset(out,0,sizeof out);memset(s_priority,0,320);
    draw_scene_sprites(&v,0,out,320,0,pal);
    for(int x=0;x<320;x++)CHECK(!out[x]); /* native composition stays owned */
    /* Exercise the complete composition caller, not only its sprite helper.
     * The native row owns the center while queued signed pieces can already
     * be visible on either side of it. */
    CHECK(configure("off"));CHECK(configure("fit"));
    memset(g_ram,0,sizeof g_ram);g_ram[0xF600]=0x48;
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=0xE;
    s_build=scene;s_visible_count=0;publish_sprites();
    memcpy(v.vram+0xF800,g_ram+0xF800,640);
    uint32_t native[320]={0};
    scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;x++) {
        int glyph=(x>=38&&x<46)||(x>=758&&x<766);
        CHECK(out[x]==(glyph?pal[1]:x>=238&&x<558?0:0xFF000000u));
    }
    s_display=previous;s_world_frame[0xF600]=(uint8_t)saved_mode;
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
            CHECK(out[x]==genesis_dac_cram_to_argb(v.cram[p],(ui || nx<0 || nx>=512)?GENESIS_DAC_NORMAL:GENESIS_DAC_SHADOW));
        }
        for(unsigned x=0;x<320;x++)raster.plane_a[x]|=128;
        for(unsigned col=0;col<64;col++)word(v.vram,0xC000+col*2,0x8000);
        scanline_lazy(&v,0,320,out,canvas,test_native_row,native,&raster);
        for(int x=0;x<canvas;x++) {
            int nx=x-origin,sx=(int)((int64_t)x*320/canvas);
            unsigned p=nx>=80&&nx<88?3:nx>=280&&nx<288?1:nx>=200&&nx<208?4:sx<160?1:2;
            CHECK(out[x]==genesis_dac_cram_to_argb(v.cram[p],GENESIS_DAC_NORMAL));
        }
        memset(v.vram+0xC000,0,128);
        memcpy(raster.plane_a,saved.plane_a,sizeof raster.plane_a);
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
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.world_tag=0x3401;publish_sprites();
    CHECK(s_history[s_serial%3].world_tag==0x3401);
    s3_video_set_world_cell(snapshot_cell);snapshot_calls=0;
    scanline(&v,0,native,320,out,398);
    CHECK(snapshot_calls && out[0]==0xFFFF0000);
    /* Incoming zone assignment can precede its world/SAT publication even
     * with the stale HUD-ready byte set. Keep the outgoing immutable world. */
    /* A later scanline uses the same published RAM, even if live RAM moves
     * into another act while that frame is being displayed. */
    g_ram[0xFE10]=0x20;g_ram[0xFFF0]=0;
    scanline(&v,1,native,320,out,398);CHECK(out[0]==0xFFFF0000);
    uint32_t lazy[398];native_row_calls=0;
    scanline_lazy(&v,1,320,lazy,398,test_native_row,native,NULL);
    CHECK(!native_row_calls && !memcmp(out,lazy,sizeof out));
    g_ram[0xF600]=0x8C;g_ram[0xF711]=1;
    scanline(&v,0,native,320,lazy,398);
    CHECK(s_frame_level && s_frame_zone==0x34 && s_world_frame[0xFFF0]==0xA5);
    CHECK(!memcmp(out,lazy,sizeof out));
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
    const unsigned cases[][2]={{0x20,0},{0x24,0},{0,4},{0,8}};
    for(unsigned z=0;z<sizeof cases/sizeof *cases;++z) {
        CHECK(configure("off"));CHECK(configure("fit"));width(398,224,320,224);
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xFE10]=(uint8_t)cases[z][0];
        word(g_ram,0xEEC2,cases[z][1]);
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
static void test_imported_pylon_wrap(void)
{
    static GVDP v;uint32_t native[320]={0},out[796],locked[796];
    const int cameras[]={0,64,128,176,768,800,978,1000,1100,1216};
    CHECK(configure("off"));CHECK(configure("32:9"));width(3200,900,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(0xB000,0x123456);g_ram[0xF600]=12;g_ram[0xFE10]=0x23;
    word(g_ram,0x8000,12);word(g_ram,0x8008,0x8100);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE00;memset(v.vram+32,0x11,32);
    write32(0xB172,0x7E0170);write32(0xB17E,0x100000);
    word(g_ram,0xB17C,0x8000);word(g_ram,0xB186,136);
    word(g_rom,0x100000,2);word(g_rom,0x100002,1);
    word(g_rom,0x100004,0);word(g_rom,0x100006,1);word(g_rom,0x100008,0);
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB172);
    uint8_t before[65536];
    for(unsigned n=0;n<sizeof cameras/sizeof *cameras;++n) {
        int camera=cameras[n];word(g_ram,0xEE80,camera);word(g_ram,0xEE78,camera);
        word(g_ram,0xB182,-2*camera);word(v.vram,0xF000,-camera);
        memcpy(before,g_ram,sizeof before);capture_objects();publish_sprites();s3_video_vblank();
        CHECK(!memcmp(before,g_ram,sizeof before));
        memcpy(v.vram+0xF800,g_ram+0xF800,640);
        scanline(&v,0,native,320,out,796);scanline(&v,8,native,320,out,796);
        const int phases[]={146,146,146,146,110,46,202,202,202,202};
        int phase=phases[n];
        for(int x=0;x<796;++x)if((out[x]==0xFF0000FFu)!=(((x-phase+512)%512)<8)) {fprintf(stderr,"pylon camera%d x%d phase%d color%x count%u first%d hud%d\n",camera,x,phase,out[x],s_build.count,s_build.sprites[0].x,s_build.sprites[0].hud);CHECK(0);}
        if(n==0 || n==6)memcpy(locked,out,sizeof locked);
        if((n>0 && n<4)||n>6)CHECK(!memcmp(locked,out,sizeof locked));
    }
    /* Real renderer publications must take the short wrapped path and
     * keep both clamped foreground views stationary at fractional phases. */
    const int pairs[][3]={{248,264,110},{0,176,146},{978,1216,202}};
    static uint32_t image[796*224];
    for(unsigned n=0;n<3;++n) {
        interpolation_configure(1);
        for(unsigned tick=0;tick<2;++tick) {
            int camera=pairs[n][tick];word(g_ram,0xFE04,tick+1);
            word(g_ram,0xEE80,camera);word(g_ram,0xEE78,camera);
            word(g_ram,0xB182,-2*camera);word(v.vram,0xF000,-camera);
            capture_objects();publish_sprites();s3_video_vblank();
            memcpy(v.vram+0xF800,g_ram+0xF800,640);
            for(int y=0;y<224;++y) {
                scanline(&v,y,native,320,out,796);
                interpolation_capture(&v,y,out,796,320,NULL);
            }
        }
        CHECK(interpolate(.5,image,796,224));
        for(int x=0;x<796;++x)
            CHECK((image[8*796+x]==0xFF0000FFu)==(((x-pairs[n][2]+512)%512)<8));
        interpolation_configure(0);
    }
    CHECK(configure("off"));
}
static void test_doomsday_scrolling_viewport(void)
{
#ifndef SONIC3_STANDALONE
    static GVDP v;uint32_t native[320]={0},out[796];
    const int cameras[]={0,0x9B5,0x4000};
    for(int ratio=0;ratio<2;++ratio)for(unsigned k=0;k<3;++k) {
        CHECK(configure("off"));CHECK(configure(ratio?"32:9":"16:9"));
        int canvas=width(3200,900,320,224),camera=cameras[k],origin=(canvas-320)/2;
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;g_ram[0xF711]=1;word(g_ram,0xFE10,0x0C00);
        /* The flying scene scrolls beyond its small scenery layout. */
        word(g_ram,0x8000,20);word(g_ram,0x8008,0x8088);
        word(g_ram,0xEE78,camera);word(g_ram,0xEE80,camera);
        word(g_ram,0xEE7C,160);word(g_ram,0xEE84,160);word(g_ram,0xEEAA,0xFFF);
        write32(0xB000,0x10A94);g_ram[0xB004]=132;g_ram[0xB006]=g_ram[0xB007]=24;
        word(g_ram,0xB010,camera+138);word(g_ram,0xB014,260);
        word(g_ram,0xB00A,0x8001);write32(0xB00C,0x100000);
        word(g_rom,0x100000,2);word(g_rom,0x100002,1);
        word(g_rom,0x100004,0);word(g_rom,0x100006,0);word(g_rom,0x100008,0);
        word(g_ram,0xAD00,2);word(g_ram,0xAD02,0xB000);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=0xE;
        memset(v.vram+32,0x11,32);word(v.vram,0xF000,-camera);
        uint8_t before[65536];memcpy(before,g_ram,sizeof before);
        capture_objects();publish_sprites();s3_video_vblank();
        memcpy(v.vram+0xF800,g_ram+0xF800,640);
        scanline(&v,0,native,320,out,canvas);scanline(&v,100,native,320,out,canvas);
        for(int x=0;x<8;++x)CHECK(out[origin+138+x]==0xFFFF0000u);
        CHECK(s_left==camera-origin && s3_video_presentation_camera_x(camera)==camera);
        CHECK(!memcmp(before,g_ram,sizeof before));
        /* Finite ordinary levels retain their existing edge clamp. */
        g_ram[0xFE10]=7;
        if(camera>2560)CHECK(view_left(camera,canvas)==2560-canvas);
    }
    CHECK(configure("off"));
#endif
}
static void test_uploaded_scene_camera(void)
{
    static GVDP v;uint32_t native[320]={0},out[398];
    const unsigned zones[]={0,4};
    for(unsigned z=0;z<sizeof zones/sizeof *zones;++z) {
        CHECK(configure("off"));CHECK(configure("fit"));width(398,224,320,224);
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;g_ram[0xFE10]=(uint8_t)zones[z];g_ram[0xF62A]=8;
        write32(0xB000,0x123456);word(g_ram,0xEEC2,8);
        word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
        for(unsigned i=0;i<4;++i)word(g_ram,0x9008+i*2,2);
        word(g_ram,0xEE80,64);word(g_ram,0xEE84,100);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.vsram[0]=100;
        word(v.vram,0xF000,(uint16_t)-64);
        v.cram[1]=0xEEE;v.cram[2]=14;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
        memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.count=1;
        s_build.camera_x=64;s_build.camera_y=100;
        s_build.sprites[0]=(SceneSprite){40,20,0x8001,0,0};s_visible_count=0;
        publish_sprites();unsigned uploaded=s_serial;s3_video_vblank();
        memcpy(v.vram+0xF800,g_ram+0xF800,640);
        /* Camera-follow leaves the native SAT byte-identical. The next main
         * loop may already publish a new world pose before its VBlank DMA. */
        s_build.camera_x=65;s_build.camera_y=99;publish_sprites();
        word(g_ram,0xEE80,65);word(g_ram,0xEE84,99);
        s3_video_set_world_cell(deformed_cell);
        static uint8_t before[65536];memcpy(before,g_ram,sizeof before);GVDP untouched=v;
        interpolation_configure(1);
        for(int y=0;y<224;++y) {
            scanline(&v,y,native,320,out,398);
            interpolation_capture(&v,y,out,398,320,NULL);
        }
        CHECK(s_display && s_display->serial==uploaded);
        CHECK(s_camera==64 && s_camera_y==100);
        CHECK(ix_current()->scene.serial==uploaded);
        CHECK(ix_current()->context.fg_x==64 && ix_current()->context.fg_y==100);
        scanline(&v,20,native,320,out,398);
        for(int x=78;x<=87;++x)CHECK((out[x]==0xFFFFFFFFu)==(x>=79 && x<87));
        CHECK(!memcmp(before,g_ram,sizeof before) && !memcmp(&untouched,&v,sizeof v));
        /* The newer pose becomes eligible only with the next native upload. */
        s3_video_vblank();select_scene(&v,0);CHECK(s_display->serial==s_serial);
        interpolation_configure(0);s3_video_set_world_cell(NULL);CHECK(configure("off"));
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
    /* FBZ uses the installed B canvas, including its mixed redraw rows. */
    for(unsigned i=0;i<64*32;i++)word(v.vram,0xE000+i*2,1+((i%64>>1)&1));
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    s3_video_set_world_cell(intro_canvas_cell);
    const int cameras[]={32,176,336};
    for(unsigned c=0;c<sizeof cameras/sizeof *cameras;c++) {
        int camera=cameras[c];word(g_ram,0xEE80,camera);
        word(v.vram,0xF000,(unsigned)-camera);word(v.vram,0xF002,(unsigned)-(camera/2));
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;x++) {
            int source=x-238+camera/2;
            CHECK(out[x]==(source<512?0xFFFF0000u:0xFF00FF00u));
        }
    }
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_aiz_intro_live_palette(void)
{
    GVDP v={0};uint32_t out[64];uint16_t saved[48];
    memcpy(saved,s_aiz_intro.colors,sizeof saved);
    s_frame_zone=s_frame_act=s_frame_bg_event=0;s_frame_fg_x=0;
    /* FadeFromBlack clips each channel; proportional scaling is different. */
    for(unsigned i=0;i<64;i++)v.cram[i]=(uint16_t)(i&1?0x200:0);
    v.cram[16]=0;v.cram[17]=0x200;
    for(unsigned i=0;i<48;i++)s_aiz_intro.colors[i]=0xE88;
    aiz_palette(&s_aiz_intro,&v,out);
    for(unsigned i=0;i<64;i++)CHECK(out[i]==genesis_dac_cram_to_argb(v.cram[i],GENESIS_DAC_NORMAL));
    /* Shared player colours are already live even for the other art bank. */
    aiz_palette(&s_aiz_main,&v,out);
    for(unsigned i=0;i<16;i++)CHECK(out[i]==genesis_dac_cram_to_argb(v.cram[i],GENESIS_DAC_NORMAL));
    memcpy(s_aiz_intro.colors,saved,sizeof saved);
}
static void test_aiz_cached_native_fade(void)
{
    GVDP v={0};uint32_t out[64];uint16_t saved[48],expected[48]={0};
    memcpy(saved,s_aiz_intro.colors,sizeof saved);
    word(s_world_frame,0xEE50,22);s_frame_bg_event=8;s_frame_fg_x=0x1300;
    /* Original Pal_AddColor, applied independently to each cached target.
     * Green $00A0 finishes at tick5, before white starts its green channel. */
    const uint16_t targets[]={0xA0,4,0xE88,0xC02,0xEEE,0};
    for(unsigned i=0;i<48;i++)s_aiz_intro.colors[i]=targets[i%6];
    for(unsigned step=0;step<=21;step++) {
        unsigned blue=step<7?step:7,green=step<=7?0:step<14?step-7:7,red=step<=14?0:step-14;
        v.cram[17]=(uint16_t)(blue*0x200+green*0x20+red*2);
        aiz_palette(&s_aiz_intro,&v,out);
        for(unsigned i=0;i<48;i++) {
            CHECK(out[i+16]==genesis_dac_cram_to_argb(expected[i],GENESIS_DAC_NORMAL));
            if(expected[i]!=targets[i%6]) {
                if(expected[i]+0x200<=targets[i%6])expected[i]+=0x200;
                else if(expected[i]+0x20<=targets[i%6])expected[i]+=0x20;
                else expected[i]+=2;
            }
        }
    }
    word(s_world_frame,0xEE50,0);memcpy(s_aiz_intro.colors,saved,sizeof saved);
}
static int tree_palette_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{ (void)ram;(void)x;(void)y;*cell=background?0:1;return 1; }
static void test_aiz_tree_palette_boundary(void)
{
    static GVDP v;static uint8_t before[65536];uint32_t native[320],out[796];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    g_ram[0xF600]=12;write32(0xB000,0x1234);word(g_ram,0x8000,97);
    word(g_ram,0x8008,0x8100);word(g_ram,0xEEC2,8);
    word(g_ram,0xEE80,0x2D80);word(g_ram,0xEE84,0x2E0);word(g_ram,0xEE90,0x2E0);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.vsram[0]=v.vsram[1]=0x2E0;
    word(v.vram,0xF000,(unsigned)-0x2D80);v.cram[17]=0xEEE;v.cram[47]=0xC02;
    memset(v.vram+32,0xFF,32);
    for(unsigned i=0;i<4;i++)word(g_ram,0x9008+i*2,0xC001);
    for(unsigned x=0;x<320;x++)native[x]=genesis_dac_cram_to_argb(0xC02,GENESIS_DAC_NORMAL);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.camera_x=0x2D80;s_build.camera_y=0x2E0;publish_sprites();
    s3_video_set_world_cell(tree_palette_cell);memcpy(before,g_ram,sizeof before);GVDP unchanged=v;
    scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;x++)CHECK(out[x]==genesis_dac_cram_to_argb(x<238?4:0xC02,GENESIS_DAC_NORMAL));
    CHECK(!memcmp(before,g_ram,sizeof before) && !memcmp(&unchanged,&v,sizeof v));
    /* No world/palette correction is allowed in the true native viewport. */
    CHECK(!memcmp(out+238,native,sizeof native));
    CHECK(!aiz_tree_color(0x2D00,0x300,0,320,47));
    CHECK(!aiz_tree_color(0x2D80,0x300,-1,320,47));
    CHECK(!aiz_tree_color(0x2D00,0x480,-1,320,47));
    CHECK(!aiz_tree_color(0x2D00,0x300,-1,320,46));
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
    scanline(&v,0,native,320,out,320);CHECK(!memcmp(out,native,sizeof native));
}
static void test_hcz_tunnel_palette_retirement(void)
{
    static GVDP v;static uint8_t before[65536];uint32_t native[320],out[1603];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("32:9"));
    g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xFE10]=1;
    write32(0xB000,0x1234);word(g_ram,0x8000,97);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,0x960);word(g_ram,0xEE84,0x580);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.vsram[0]=0x580;
    word(v.vram,0xF000,(unsigned)-0x960);v.cram[17]=0xEEE;
    const unsigned old_colors[]={0xB80,0x240,0x220};
    const unsigned incoming[]={0xCEE,0xACE,0x8A};
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.world_tag=0x101;
    s_build.camera_x=0x960;s_build.camera_y=0x580;publish_sprites();
    s3_video_set_world_cell(tree_palette_cell);
    const int widths[]={320,398,523,796,1603};
    for(unsigned color=0;color<3;++color) {
        v.cram[56+color]=(uint16_t)incoming[color];
        memset(v.vram+32,(int)(8+color)*17,32);
        for(unsigned i=0;i<4;++i)word(g_ram,0x9008+i*2,0x6001);
        uint32_t live=genesis_dac_cram_to_argb(incoming[color],GENESIS_DAC_NORMAL);
        uint32_t retained=genesis_dac_cram_to_argb(old_colors[color],GENESIS_DAC_NORMAL);
        for(unsigned i=0;i<320;++i)native[i]=live;
        for(unsigned c=0;c<5;++c) {
            int w=widths[c],origin=(w-320)/2;
            width(w,224,320,224);memcpy(before,g_ram,sizeof before);GVDP unchanged=v;
            scanline(&v,0,native,320,out,w);
            for(int x=0;x<w;++x) {
                int nx=x-origin,wx=0x960+nx;
                CHECK(out[x]==(nx<0 && wx<0x900?retained:live));
            }
            CHECK(!memcmp(out+origin,native,sizeof native));
            CHECK(!memcmp(before,g_ram,sizeof before) && !memcmp(&unchanged,&v,sizeof v));
        }
    }
    /* Another host area/act, the original pre-switch camera and rows outside
     * the current tunnel retain their installed palette everywhere. */
    for(unsigned c=0;c<5;++c) {
        unsigned camera=c==2?0x8E0:0x960,cy=c==3?0x4F0:c==4?0x600:0x580;
        g_ram[0xFE10]=c==0?2:1;g_ram[0xFE11]=c==1;
        word(g_ram,0xEE80,camera);word(g_ram,0xEE84,cy);
        word(v.vram,0xF000,(unsigned)-camera);v.vsram[0]=(uint16_t)cy;
        s_build.camera_x=(int)camera;s_build.camera_y=(int)cy;
        s_build.world_tag=(unsigned)ram16(0xFE10)+1u;publish_sprites();
        width(796,224,320,224);scanline(&v,0,native,320,out,796);
        for(unsigned x=0;x<796;++x)CHECK(out[x]==native[0]);
    }
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_hcz_boss_foreground_palette(void)
{
    static GVDP v;static uint8_t before[65536];uint32_t native[320],out[796];
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("32:9"));
    g_ram[0xF600]=12;g_ram[0xF711]=1;g_ram[0xFE10]=1;
    write32(0xB000,0x1234);word(g_ram,0x8000,160);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,0x3680);word(g_ram,0xEE84,0x638);word(g_ram,0xF646,0x6A0);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.vsram[0]=0x638;
    word(v.vram,0xF000,(unsigned)-0x3680);v.cram[17]=0xEEE;v.cram[20]=0xE;
    word(g_rom,S3_ART_BANK+0x8C594+8,0xE0);
    word(g_rom,S3_ART_BANK+0x8C654+32+8,0xE00);
    memset(v.vram+32,0x44,32);
    for(unsigned i=0;i<4;++i)word(g_ram,0x9008+i*2,0x2001);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.world_tag=0x101;
    s_build.camera_x=0x3680;s_build.camera_y=0x638;publish_sprites();
    s3_video_set_world_cell(tree_palette_cell);
    uint32_t live=genesis_dac_cram_to_argb(0xE,GENESIS_DAC_NORMAL);
    for(unsigned x=0;x<320;++x)native[x]=live;
    width(796,224,320,224);memcpy(before,g_ram,sizeof before);GVDP unchanged=v;
    scanline(&v,0,native,320,out,796);
    uint32_t dry=genesis_dac_cram_to_argb(0xE0,GENESIS_DAC_NORMAL);
    for(int x=0;x<796;++x)CHECK(out[x]==(x<238?dry:live));
    scanline(&v,104,native,320,out,796);
    uint32_t wet=genesis_dac_cram_to_argb(0xE00,GENESIS_DAC_NORMAL);
    for(int x=0;x<796;++x)CHECK(out[x]==(x<238?wet:live));
    CHECK(!memcmp(before,g_ram,sizeof before) && !memcmp(&unchanged,&v,sizeof v));
    /* The incoming act and the boss's native viewport keep their palette. */
    g_ram[0xFE11]=1;s_build.world_tag=0x102;publish_sprites();
    scanline(&v,0,native,320,out,796);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==live);
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_aiz_intro_owned_pose(void)
{
    static GVDP v;static SceneFrame restored;uint32_t out[796],pal[64]={0};
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    g_ram[0xF600]=12;word(g_ram,0x8000,97);word(g_ram,0x8008,0x8100);
    write32(0xB000,0x1234);write32(0xB172,S3_AIZ_PLANE_INTRO);
    write32(0xB17E,S3_SUPER_MAP);word(g_ram,0xB17C,0x680);
    word(g_ram,0xB182,120);word(g_ram,0xB186,144);g_ram[0xB194]=33;
    word(g_ram,0xAC00,2);word(g_ram,0xAC02,0xB172);
    word(g_rom,S3_SUPER_MAP+33*2,0x100);word(g_rom,S3_SUPER_MAP+0x100,1);
    word(g_rom,S3_SUPER_MAP+0x102,4);word(g_rom,S3_SUPER_MAP+0x104,0);word(g_rom,S3_SUPER_MAP+0x106,0);
    word(g_rom,S3_SUPER_PLC+33*2,0x100);word(g_rom,S3_SUPER_PLC+0x100,1);word(g_rom,S3_SUPER_PLC+0x102,0x1001);
    memset(g_rom+0x100020,0x11,32);memset(g_rom+0x100040,0x22,32);
    memset(v.vram+0x680*32,0x99,64);v.reg[5]=0x7C;
    s_capture_frame=&s_build;s_capture_readonly=0;capture_objects();
    CHECK(s_build.aiz_super_pose==1 && s_build.count==1 && !g_ram[0xFE19]);
    publish_sprites();select_scene(&v,0);
    CHECK(s_display && s_display->aiz_super_pose==1);
    /* Exact pose lives with the publication, independently of newer RAM. */
    g_ram[0xB194]=34;pal[1]=0xFF112233;pal[9]=0xFF998877;
    memset(out,0,sizeof out);memset(s_priority,0,796);
    GVDP unchanged=v;draw_scene_sprites(&v,16,out,796,0,pal);
    for(int x=230;x<238;x++)CHECK(out[x]==pal[1]);
    for(int x=238;x<246;x++)CHECK(out[x]==pal[9]); /* native centre untouched */
    CHECK(!memcmp(&v,&unchanged,sizeof v) && !g_ram[0xFE19]);
    uint8_t *state=(uint8_t *)malloc(sizeof s_build);CHECK(state);
    S3StateIO save={state,sizeof s_build,0,0,1};scene_frame_state(&save,&s_build);CHECK(save.ok);
    memset(&restored,0,sizeof restored);S3StateIO load={state,sizeof s_build,0,2,1};
    scene_frame_state(&load,&restored);CHECK(load.ok && restored.aiz_super_pose==1);
    unsigned bad_pose=3;memcpy(state+offsetof(SceneFrame,aiz_super_pose),&bad_pose,sizeof bad_pose);
    S3StateIO bad={state,sizeof s_build,0,1,1};scene_frame_state(&bad,&restored);CHECK(!bad.ok);free(state);
    write32(0xB17E,0x1000);capture_objects();CHECK(!s_build.aiz_super_pose);
    s_display=NULL;CHECK(configure("off"));
}
static int ship_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;
    if(background){*cell=0;return 1;}
    /* Original AIZ2 second-camera coordinates must not be unwrapped into
     * the ordinary camera's neighboring 1024px page. */
    CHECK(y>=0xA00 || (y>=0x1C0 && y<0x300));
    if(y>=0xA00){CHECK(x>=0x3E00 && x<0x4300);*cell=1;}
    else {CHECK(x>=0x4100 && x<0x4800);*cell=2;}
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
    /* Native DoShipLoop wraps by $200. Its installed ground canvas differs
     * from the adjacent raw-layout columns; those columns must not appear
     * in either margin. Preserve the separate HInt ship band above it. */
    word(g_ram,0xEEB2,4);word(g_ram,0xEEC0,8);
    v.cram[3]=0xE00;memset(v.vram+96,0x33,32);
    for(unsigned i=0;i<64*32;i++)word(v.vram,0xC000+i*2,3);
    v.vsram[0]=0xAB3;scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;x++)CHECK(out[x]==0xFFFF0000u);
    v.vsram[0]=0x1C0;scanline(&v,65,native,320,out,796);
    /* Unused native columns can still contain the unique outgoing ledge.
     * They must not repeat over the incoming flat ground in a wide view. */
    for(int x=0;x<796;x++) {
        int wx=0x4288+x-238;
        CHECK(out[x]==(x>=558 && wx>=0x4200 && wx<0x4440?0xFF00FF00u:0xFF0000FFu));
    }
    /* EndRefresh retires the ship before the installed foreground rows.
     * Keep their canvas through the grove instead of exposing raw partial
     * tree columns when the upper ship flag clears. */
    word(g_ram,0xEED6,0);word(g_ram,0xEEC0,16);
    v.vsram[0]=0x1C0;scanline(&v,0,native,320,out,796);
    scanline(&v,65,native,320,out,796);
    for(int x=0;x<796;x++) {
        int wx=0x4288+x-238;
        CHECK(out[x]==(wx>=0x4380 && wx<0x4880?0xFF0000FFu:0xFF00FF00u));
    }
    /* The incoming complete grove is a spatial layout again; a retained
     * outgoing canvas must not repeat over its future columns. */
    word(g_ram,0xEE80,0x4580);
    for(unsigned y=0;y<224;y++)word(v.vram,0xF000+y*4,(uint16_t)-0x4580);
    scanline(&v,0,native,320,out,796);scanline(&v,65,native,320,out,796);
    for(int x=0;x<796;x++) {
        int wx=0x4580+x-238;
        CHECK(out[x]==(wx>=0x4380 && wx<0x4640?0xFF0000FFu:0xFF00FF00u));
    }
    word(g_ram,0xEE80,0x4288);
    for(unsigned y=0;y<224;y++)word(v.vram,0xF000+y*4,(unsigned)-(y<65?0x3FB5:0x4288));
    word(g_ram,0xEED6,0xFF00);word(g_ram,0xEEC0,8);
    word(g_ram,0xEEB2,0);
    v.vsram[0]=0xAB3;scanline(&v,0,native,320,out,796);
    v.vsram[0]=0x1C0;scanline(&v,65,native,320,out,796);
    for(int x=0;x<796;x++)CHECK(out[x]==0xFF00FF00u);
    /* The installed row can lag the published ship bob by one pixel,
     * including across a 1024px boundary. Its scroll remains owned by the
     * ship; ordinary HInt ground rows must keep the foreground camera. */
    const unsigned ship_y[]={0xAB3,0xC00};
    for(unsigned i=0;i<sizeof ship_y/sizeof *ship_y;i++)for(int delta=-1;delta<=1;delta+=2) {
        word(g_ram,0xEE9C,ship_y[i]);
        v.vsram[0]=(ship_y[i]+delta)&1023u;
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;x++)CHECK(out[x]==0xFFFF0000u);
        v.vsram[0]=0x1C0;scanline(&v,65,native,320,out,796);
        for(int x=0;x<796;x++)CHECK(out[x]==0xFF00FF00u);
    }
    word(g_ram,0xEE9C,0xAB3);
    /* A ship bob between completed frames must not turn its upper band
     * into ground during fractional presentation. The two cameras use
     * different 1024px pages, so choosing the wrong bank is observable. */
    v.vsram[0]=0xAB3;scanline(&v,0,native,320,out,796);
    memcpy(ix_current()->cram[0],v.cram,sizeof v.cram);
    ix_previous()->fg_scroll[0]=0xAB1;ix_current()->fg_scroll[0]=0xAB3;
    ix_previous()->hs[0][0]=ix_current()->hs[0][0]=-0x3FB5;
    ix_previous()->hs[0][1]=ix_current()->hs[0][1]=0;
    s_interpolating=1;s_interpolation_phase=.5;
    scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;x++)CHECK(out[x]==0xFFFF0000u);
    s_interpolating=0;
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
    const unsigned zones[]={0,0x100,0x200,0x201,0x300,0x400,0x800,0x2000,0x2200,0x2800,0x3400,0x4000,0x5B00};
    for(unsigned z=0;z<sizeof zones/sizeof *zones;z++)for(unsigned w=0;w<4;w++) {
        word(g_ram,0xFE10,zones[z]);int canvas=widths[w];
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
static int panorama_source_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)y;
    *cell=background?(x>=0 && x<512?1:2):0;return 1;
}
static void test_streamed_background_sources(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];uint8_t before[65536];
    const struct { unsigned zone,event,bank,source; } cases[]={
        {0x101,4,0,0},{0x101,8,0,1},{0x101,12,0,1},{0x101,16,0,2},
        {0x201,4,0,2},{0x201,4,12,2},{0x201,4,4,0},{0x201,4,8,0},
        {0x201,8,0,1},{0x201,8,8,0},
        {0x500,0,0,1},{0x500,4,0,0},{0x500,8,0,1},
        {0x500,16,0,0},{0x501,4,0,1}
    };
    for(unsigned n=0;n<sizeof cases/sizeof *cases;++n) {
        CHECK(configure("off"));CHECK(configure("32:9"));
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;word(g_ram,0xFE10,cases[n].zone);write32(0xB000,0x1234);
        word(g_ram,0x8000,64);word(g_ram,0x8002,20);word(g_ram,0x8008,0x8100);
        word(g_ram,0xEEC2,cases[n].event);word(g_ram,0xEED2,cases[n].bank);word(g_ram,0xEE80,0x1500);
        word(g_ram,0xEECA,0xFFFF);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
        v.cram[1]=0xE;v.cram[2]=0xE0;v.cram[3]=0xE00;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);memset(v.vram+96,0x33,32);
        for(unsigned i=0;i<4;++i){word(g_ram,0x9008+i*2,1);word(g_ram,0x9010+i*2,2);}
        word(v.vram,0xF000,(unsigned)-0x1500);word(v.vram,0xF002,(unsigned)-0x500);
        for(unsigned i=0;i<64*32;++i)word(v.vram,0xE000+i*2,i&1?3:1);
        memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
        s3_video_set_world_cell(panorama_source_cell);memcpy(before,g_ram,sizeof before);
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;++x) {
            int nx=x-s_native_x;
            uint32_t expected=cases[n].source==0?0xFF00FF00u:cases[n].source==2?0xFFFF0000u:
                ((unsigned)(nx+0x500)&8?0xFF0000FFu:0xFFFF0000u);
            if(out[x]!=expected)fprintf(stderr,"zone=%04X event=%u bank=%u x=%d nx=%d color=%08X expected=%08X\n",cases[n].zone,cases[n].event,cases[n].bank,x,nx,out[x],expected);
            CHECK(out[x]==expected);
        }
        CHECK(!memcmp(before,g_ram,sizeof before));
        s3_video_set_world_cell(NULL);
    }
    CHECK(configure("off"));
}
static void test_icz_partial_background_rows(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];uint8_t before[65536];
    const struct { unsigned event,y,pending,remaining,world; } cases[]={
        {8,0xD0,0x2C0,13,0},{8,0x20,0x2C0,13,1},
        {12,0xD0,0x2C0,13,1},{12,0x20,0x2C0,13,1},
        {8,0xE0,0x2E0,15,1},{12,0xE0,0x2E0,15,1},
        {8,0x20,0x1F0,0,0},{8,0xF0,0x1F0,0,1},
        {12,0x20,0x1F0,0,1},{12,0xF0,0x1F0,0,1}
    };
    for(unsigned n=0;n<sizeof cases/sizeof *cases;++n) {
        CHECK(configure("off"));CHECK(configure("32:9"));
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;word(g_ram,0xFE10,0x500);write32(0xB000,0x1234);
        word(g_ram,0x8000,64);word(g_ram,0x8002,20);word(g_ram,0x8008,0x8100);
        word(g_ram,0xEEC2,cases[n].event);word(g_ram,0xEE80,0x1500);
        word(g_ram,0xEEC8,cases[n].pending);word(g_ram,0xEECA,cases[n].remaining);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.vsram[1]=cases[n].y;
        v.cram[1]=0xE;v.cram[2]=0xE0;v.cram[3]=0xE00;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);memset(v.vram+96,0x33,32);
        for(unsigned i=0;i<4;++i){word(g_ram,0x9008+i*2,1);word(g_ram,0x9010+i*2,2);}
        word(v.vram,0xF000,(unsigned)-0x1500);word(v.vram,0xF002,(unsigned)-0x500);
        for(unsigned i=0;i<64*32;++i)word(v.vram,0xE000+i*2,i&1?3:1);
        memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
        s3_video_set_world_cell(panorama_source_cell);memcpy(before,g_ram,sizeof before);
        scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;++x) {
            int nx=x-s_native_x;
            uint32_t canvas=((unsigned)(nx+0x500)&8)?0xFF0000FFu:0xFFFF0000u;
            uint32_t expected=cases[n].world && (nx<0 || nx>=320)?0xFF00FF00u:canvas;
            CHECK(out[x]==expected);
        }
        CHECK(!memcmp(before,g_ram,sizeof before));s3_video_set_world_cell(NULL);
    }
    CHECK(configure("off"));
}
static void test_icz_outdoor_snow_edge(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];uint8_t before[65536];
    const unsigned cases[][3]={{0x500,0,1},{0x500,4,0},{0x501,0,0},{0x100,0,0}};
    for(unsigned n=0;n<sizeof cases/sizeof *cases;++n) {
        CHECK(configure("off"));CHECK(configure("32:9"));presentation_reset();
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);s3_video_set_world_cell(NULL);
        g_ram[0xF600]=12;word(g_ram,0xFE10,cases[n][0]);write32(0xB000,0x1234);
        word(g_ram,0x8000,64);word(g_ram,0x8002,4);word(g_ram,0x8008,0x8100);
        word(g_ram,0x800A,0x8120);word(g_ram,0xEEAE,0x3C);
        word(g_ram,0xEE80,0x300);word(g_ram,0xEEC2,cases[n][1]);g_ram[0x8108]=1;
        for(unsigned i=0;i<64;++i)word(g_ram,128+i*2,1);
        for(unsigned i=0;i<4;++i)word(g_ram,0x9008+i*2,0x4002);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[33]=0xEEE;
        memset(v.vram+64,0x11,32);word(v.vram,0xF000,(unsigned)-0x300);
        memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
        memcpy(before,g_ram,sizeof before);scanline(&v,0,native,320,out,796);
        for(int x=0;x<796;++x) {
            int nx=x-s_native_x,wx=nx+0x300;
            int snow=(wx>=0x400 && wx<0x480) || (cases[n][2] && nx<0 && wx>=0);
            CHECK(out[x]==(snow?0xFFFFFFFFu:0xFF000000u));
        }
        CHECK(!memcmp(before,g_ram,sizeof before));
    }
    CHECK(configure("off"));
}
static int empty_arena_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{ (void)ram;(void)x;(void)y;(void)background;*cell=0;return 1; }
static int cnz_boss_background_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    (void)ram;(void)x;
    *cell=background?(y>=0x200 && y<0x300?1:2):0;return 1;
}
static void test_native_vertical_layout_wrap(void)
{
    memset(g_ram,0,sizeof g_ram);s3_video_set_world_cell(NULL);
    word(g_ram,0x8000,16);word(g_ram,0x8002,16);
    for(unsigned background=0;background<2;++background) {
        unsigned table=0x8008+background*2;
        word(g_ram,table,0x8100);word(g_ram,table+0x3C,0x8120);
        word(g_ram,table+0x40,0x8140);word(g_ram,table+0x7C,0x8140);
    }
    g_ram[0x8100]=1;g_ram[0x8120]=2;g_ram[0x8140]=3;
    for(unsigned chunk=1;chunk<=3;++chunk)for(unsigned cell=0;cell<64;++cell)
        word(g_ram,chunk*128+cell*2,chunk+3);
    for(unsigned block=4;block<=6;++block)for(unsigned cell=0;cell<4;++cell)
        word(g_ram,0x9000+block*8+cell*2,(block-3)*0x111);
    for(unsigned background=0;background<2;++background) {
        word(g_ram,0xEEAE,0x3C);
        CHECK(world_attr(g_ram,0,-8,(int)background)==0x222);
        CHECK(world_attr(g_ram,0,0x800,(int)background)==0x111);
        word(g_ram,0xEEAE,0x7C);
        CHECK(world_attr(g_ram,0,-8,(int)background)==0x333);
        CHECK(world_attr(g_ram,0,0x800,(int)background)==0x333);
        CHECK(world_attr(g_ram,0,0x1000,(int)background)==0x111);
    }
}
static void test_cnz_boss_background_band(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];uint8_t before[65536];
    const unsigned cases[][3]={{0x300,4,0},{0x300,8,1},{0x300,12,0},{0x301,8,0}};
    for(unsigned n=0;n<sizeof cases/sizeof *cases;++n) {
        CHECK(configure("off"));CHECK(configure("32:9"));presentation_reset();
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;word(g_ram,0xFE10,cases[n][0]);write32(0xB000,0x1234);
        word(g_ram,0x8000,128);word(g_ram,0x8002,32);word(g_ram,0x8008,0x8100);
        word(g_ram,0xEE80,0x31E0);word(g_ram,0xEE8C,0x567);word(g_ram,0xEEC2,cases[n][1]);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.vsram[1]=0x167;
        v.cram[1]=0xE;v.cram[2]=0xE0;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
        for(unsigned i=0;i<4;++i){word(g_ram,0x9008+i*2,1);word(g_ram,0x9010+i*2,2);}
        word(v.vram,0xF000,(unsigned)-0x31E0);word(v.vram,0xF002,(unsigned)-0x260);
        memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
        s3_video_set_world_cell(cnz_boss_background_cell);memcpy(before,g_ram,sizeof before);
        scanline(&v,0,native,320,out,796);
        /* The fixed column stream repeats its authored 256px bank through
         * the descending room. Other phases and act2 retain spatial Y. */
        for(unsigned x=0;x<796;++x)CHECK(out[x]==(cases[n][2]?0xFFFF0000u:0xFF00FF00u));
        CHECK(!memcmp(before,g_ram,sizeof before));s3_video_set_world_cell(NULL);
    }
    CHECK(configure("off"));
}
static void test_mgz2_falling_arena(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];uint8_t before[65536];
    CHECK(configure("off"));CHECK(configure("32:9"));presentation_reset();
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    g_ram[0xF600]=12;word(g_ram,0xFE10,0x201);write32(0xB000,0x1234);
    word(g_ram,0x8000,128);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEEC0,4);word(g_ram,0xEE80,0x3C80);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[11]=7;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE;v.cram[3]=0xE00;
    memset(v.vram+32,0x11,32);memset(v.vram+96,0x33,32);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)
        word(v.vram,0xC000+(y*64+x)*2,y&1?3:1);
    for(unsigned c=0;c<20;++c)v.vsram[c*2]=(uint16_t)((c&1)*8);
    word(v.vram,0xF000,(unsigned)-0x3C80);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;publish_sprites();
    s3_video_set_world_cell(empty_arena_cell);memcpy(before,g_ram,sizeof before);
    /* Cleared layout must not remove the installed falling floor. Each
     * native 16px column retains its own scroll; adjacent scenery does not
     * acquire a repeated copy of that floor. */
    scanline(&v,0,native,320,out,796);
    for(int x=0;x<796;++x) {
        int nx=x-s_native_x;
        unsigned expected=nx<0 || nx>=384?0xFF000000u:
            ((nx>=320 || (nx/16)&1)?0xFF0000FFu:0xFFFF0000u);
        CHECK(out[x]==expected);
    }
    CHECK(!memcmp(before,g_ram,sizeof before));
    word(g_ram,0xFE10,0x101);publish_sprites();scanline(&v,0,native,320,out,796);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFF000000u);
    s3_video_set_world_cell(NULL);CHECK(configure("off"));
}
static void test_mgz2_perspective_owner(void)
{
    CHECK(configure("off"));CHECK(configure("32:9"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);g_ram[0xF600]=12;word(g_ram,0xFE10,0x201);
    write32(0xB000,0x1234);word(g_ram,0x8000,64);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,800);word(g_ram,0xEE84,100);word(g_ram,0xEEAA,0x7FF);
    const unsigned objects[]={0xB094,0xB0DE,0xB128,0xB172,0xB1BC};
    const unsigned maps[]={S3_ART_BANK+0x162608u,S3_ART_BANK+0x160058u,0x3000};
    for(unsigned i=0;i<3;++i) {
        word(g_rom,maps[i],2);word(g_rom,maps[i]+2,1);
        word(g_rom,maps[i]+4,0);word(g_rom,maps[i]+6,1);word(g_rom,maps[i]+8,0);
    }
    word(g_ram,0xAC00,10);
    for(unsigned i=0;i<5;++i) {
        unsigned o=objects[i];word(g_ram,0xAC02+i*2,o);
        write32(o,i?0x1234:S3_MGZ_END_BOSS);g_ram[o+4]=4;g_ram[o+6]=g_ram[o+7]=16;
        write32(o+12,maps[i==1?1:i==4?2:0]);
        word(g_ram,o+16,1200);word(g_ram,o+20,140);
        if(i)word(g_ram,o+0x46,i==3?objects[2]:objects[0]);
    }
    g_ram[objects[0]+5]=0x1E;
    uint8_t before[65536];memcpy(before,g_ram,sizeof before);
    capture_objects();CHECK(s_build.count==1 && sprite_slot(&s_build.sprites[0])==7);
    CHECK(!memcmp(before,g_ram,sizeof before));
    /* The scaled pass remains; body, pilot and nested attachments return
     * together for the next actual foreground entrance. */
    g_ram[objects[0]+5]=0x20;capture_objects();CHECK(s_build.count==5);
    g_ram[objects[0]+5]=0x1E;word(g_ram,0xFE10,0x200);
    capture_objects();CHECK(s_build.count==5);
    word(g_ram,0xFE10,0x201);write32(objects[0],0x1234);
    capture_objects();CHECK(s_build.count==5);
    CHECK(configure("off"));
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

static void test_first_hud_fade(void)
{
    static GVDP v;uint32_t native[320]={0},out[796];
    CHECK(configure("off"));CHECK(configure("32:9"));width(1,1,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    write32(0xB000,0x123456);g_ram[0xF600]=0x8C;g_ram[0xFE05]=8;
    word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    /* Title-card/loading publications remain native until the initial HUD
     * actually starts; the native entry sets this after installing planes. */
    CHECK(!level_ready());capture_objects();CHECK(s_build.scene==0);
    g_ram[0xF711]=1;CHECK(level_ready());
    word(g_rom,S3_RINGS,0);longword(g_rom,S3_RINGS,0x1000);word(g_rom,0x1000,65535);
    word(g_rom,S3_HUD_MAP,12);word(g_rom,S3_HUD_MAP+12,1);
    word(g_rom,S3_HUD_MAP+14,0);word(g_rom,S3_HUD_MAP+16,1);word(g_rom,S3_HUD_MAP+18,1);
    capture_objects();CHECK(s_build.scene==1 && s_build.count==1 && s_build.sprites[0].hud==1);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=14;
    memset(v.vram+0x6CB*32,0x11,32);native[16]=0xFFFF0000;
    publish_sprites();s3_video_vblank();
    scanline(&v,0,native,320,out,796);scanline(&v,136,native,320,out,796);
    CHECK(s_frame_level && s_display && s_display->scene==1);
    CHECK(out[17]==0xFFFF0000 && out[255]!=0xFFFF0000);
    /* The original per-row fade remains authoritative at the same anchor. */
    v.cram[1]=6;scanline(&v,0,native,320,out,796);scanline(&v,136,native,320,out,796);
    CHECK(out[17]==genesis_dac_cram_to_argb(6,GENESIS_DAC_NORMAL));
    g_ram[0xF711]=0;CHECK(!level_ready());
    g_ram[0xF711]=1;word(g_ram,0x8008,0);CHECK(!level_ready());
    word(g_ram,0x8008,0x8100);write32(0xB000,0);CHECK(!level_ready());
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
static void time_test_font(void)
{
    for(unsigned d=0;d<10;d++)for(unsigned y=0;y<16;y++)for(unsigned x=0;x<8;x+=2) {
        unsigned a=(x+y+d)%4,b=(x+1+y+d)%4;
        a=a==1?1:a==2?15:0;b=b==1?1:b==2?15:0;
        g_rom[S3_TIME_DIGITS+d*64+y*4+x/2]=(uint8_t)(a*16+b);
    }
}
static void time_test_sat(GVDP *v,unsigned minute,int slide)
{
    memset(v,0,sizeof *v);v->reg[1]=64;v->reg[5]=0x7C;v->reg[12]=1;
    word(v->vram,0xF800,24+128);v->vram[0xF802]=13;
    word(v->vram,0xF804,0xA6F2);word(v->vram,0xF806,56+slide+128);
    memcpy(v->vram+0x6F2*32,g_rom+S3_TIME_DIGITS+(minute%10)*64,64);
    v->cram[17]=0xEEE;v->cram[31]=0x222;
}
static void test_time_minute_tens(void)
{
    GVDP v;uint32_t out[800];uint8_t before[65536];
    memset(g_ram,0,sizeof g_ram);time_test_font();
    g_ram[0xF600]=12;g_ram[0xFE10]=1;
    for(unsigned minute=0;minute<=100;minute++)for(int slide=-60;slide<=8;slide+=68)
        for(int origin=0;origin<=238;origin+=238) {
            time_test_sat(&v,minute,slide);g_ram[0xFE23]=(uint8_t)minute;
            GVDP unchanged=v;memcpy(before,g_ram,sizeof before);
            for(unsigned y=0;y<16;y++) {
                for(unsigned x=0;x<800;x++)out[x]=0x12345678;
                time_tens_scanline(&v,g_ram,24+(int)y,out+1,796,origin);
                CHECK(out[0]==0x12345678 && out[797]==0x12345678);
                for(int x=0;x<796;x++) {
                    unsigned expected=0x12345678;int gx=x-origin-48-slide;
                    if(minute>=10 && minute<=99 && gx>=0 && gx<8) {
                        unsigned b=g_rom[S3_TIME_DIGITS+(minute/10)*64+y*4+(unsigned)gx/2];
                        unsigned n=(b>>((gx&1)?0:4))&15;
                        if(n)expected=genesis_dac_cram_to_argb(v.cram[16+n],GENESIS_DAC_NORMAL);
                    }
                    CHECK(out[x+1]==expected);
                }
            }
            CHECK(!memcmp(&v,&unchanged,sizeof v) && !memcmp(g_ram,before,sizeof before));
        }
    /* Ordinary gameplay and the installed HUD/glyph are separate guards. */
    for(unsigned gate=0;gate<11;gate++) {
        memset(g_ram,0,sizeof g_ram);g_ram[0xF600]=12;g_ram[0xFE10]=1;g_ram[0xFE23]=12;
        time_test_sat(&v,12,0);
        if(gate==0)g_ram[0xF600]=4;
        if(gate>=1 && gate<=3)g_ram[0xFE10]=(uint8_t)(0x12+gate);
        if(gate==4)word(g_ram,0xFE08,1);
        if(gate==5)word(g_ram,S3_COMPETITION,1);
        if(gate==6)v.reg[1]=0;
        if(gate==7)word(v.vram,0xF804,0);
        if(gate==8)v.vram[0x6F2*32]^=1;
        if(gate==9)v.vram[0x6F3*32+31]^=1;
        memset(out,0,sizeof out);
        time_tens_scanline(&v,g_ram,28,out,320,0);
        if(gate<10)for(unsigned x=0;x<320;x++)CHECK(!out[x]);
        else CHECK(out[49] || out[50] || out[51]);
    }
    /* RAM advances before queued native glyph DMA: retain the validated
     * old complete minute, and never guess beyond that one native step. */
    const unsigned previous_minutes[]={9,10,19,98};
    for(unsigned q=0;q<sizeof previous_minutes/sizeof *previous_minutes;q++)
        for(unsigned uploaded=0;uploaded<2;uploaded++) {
            unsigned previous=previous_minutes[q],minute=previous+1,displayed=uploaded?minute:previous;
            time_test_sat(&v,displayed,0);g_ram[0xFE23]=(uint8_t)minute;
            for(unsigned y=0;y<16;y++) {
                memset(out,0,sizeof out);time_tens_scanline(&v,g_ram,24+(int)y,out,320,0);
                for(unsigned x=0;x<8;x++) {
                    unsigned n=0;
                    if(displayed>=10) {
                        unsigned b=g_rom[S3_TIME_DIGITS+(displayed/10)*64+y*4+x/2];
                        n=(b>>((x&1)?0:4))&15;
                    }
                    CHECK(out[48+x]==(n?genesis_dac_cram_to_argb(v.cram[16+n],GENESIS_DAC_NORMAL):0));
                }
            }
        }
    g_ram[0xFE23]=12;
    CHECK(configure("off"));
    /* Both halves retain the native row's fade and optional CD palette. */
    for(unsigned private_line=0;private_line<2;private_line++)for(unsigned y=0;y<16;y++) {
        test_time_private_palette=(int)private_line;time_test_sat(&v,12,0);
        for(unsigned fade=0;fade<8;fade++) {
            v.cram[17]=v.cram[31]=(uint16_t)(fade*0x222);
            memset(out,0,sizeof out);overlay_scanline(&v,24+(int)y,out,320);
            for(unsigned x=0;x<8;x++) {
                unsigned b=g_rom[S3_TIME_DIGITS+64+y*4+x/2],n=(b>>((x&1)?0:4))&15;
                unsigned color=private_line?gvdp_ext_color(24+(int)y,2,(int)n):fade*0x222;
                CHECK(out[48+x]==(n?genesis_dac_cram_to_argb((uint16_t)color,GENESIS_DAC_NORMAL):0));
            }
        }
    }
    test_time_private_palette=0;CHECK(configure("off"));
}
#include "sonic3_rewind_test.inc"
static void test_presentation_restore_before_tick(void)
{
    static GVDP v;static uint8_t menu_ram[65536],saved_ram[65536];
    uint32_t native[320],expected[796],out[796];
    CHECK(configure("off"));CHECK(configure("fit"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    memset(&s_build,0,sizeof s_build);memset(s_history,0,sizeof s_history);
    memset(&s_display_frame,0,sizeof s_display_frame);s_display=NULL;
    s_real_context.level=s_real_context.special=s_real_context.bonus=0;
    g_ram[0xF600]=0x28;v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;
    v.reg[5]=0x7C;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=14;
    for(unsigned x=0;x<320;++x)native[x]=0xFF123400u+x;
    scanline(&v,0,native,320,expected,796);memcpy(menu_ram,g_ram,sizeof menu_ram);
    /* Same installed SAT/plane geometry, but the host now owns a level.
     * This reproduces the fade-retention ambiguity on rewind to level select. */
    g_ram[0xF600]=12;write32(0xB000,0x123456);
    word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    s_build.scene=1;s_build.aiz_super_pose=1;publish_sprites();scanline(&v,0,native,320,out,796);
    CHECK(s_frame_level && s_display && s_display->scene==1);
    memcpy(g_ram,menu_ram,sizeof menu_ram);memcpy(saved_ram,g_ram,sizeof saved_ram);
    GVDP restored=v;sonic3_video.presentation_reset();
    /* No simulation or object publication between restore and this render. */
    scanline(&v,0,native,320,out,796);
    CHECK(!s_frame_level && !s_display);
    CHECK(!s_build.aiz_super_pose && !s_display_frame.aiz_super_pose);
    CHECK(!memcmp(expected,out,sizeof out));
    CHECK(!memcmp(saved_ram,g_ram,sizeof saved_ram) && !memcmp(&restored,&v,sizeof v));
    CHECK(configure("off"));
}

static void test_native_state_keeps_restored_publication(void)
{
    static GVDP v;uint32_t native[320]={0},expected[796],out[796];
    static uint8_t ram_before[65536];
    CHECK(configure("off"));CHECK(configure("fit"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
    s_real_context.level=s_real_context.special=s_real_context.bonus=0;
    s_display=NULL;memset(&s_display_frame,0,sizeof s_display_frame);
    memset(s_history,0,sizeof s_history);memset(&s_build,0,sizeof s_build);s_visible_count=0;
    g_ram[0xF600]=12;g_ram[0xFE10]=0x22;write32(0xB000,0x123456);
    word(g_ram,0x8000,64);word(g_ram,0x8008,0x8100);word(g_ram,0xEE80,512);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=14;
    word(v.vram,0xF000,(uint16_t)-512);memset(v.vram+32,0x11,32);
    s_build.scene=1;s_build.camera_x=512;s_build.count=1;
    s_build.sprites[0]=(SceneSprite){-16,0,1,0,0,256,0};publish_sprites();
    g_ram[0xF62A]=8;s3_video_vblank();unsigned uploaded=s_upload_serial;
    scanline(&v,0,native,320,expected,796);
    int actor_x=s_native_x-16;CHECK(actor_x>=0 && actor_x+8<s_native_x);
    CHECK(expected[actor_x]==0xFFFF0000u);
    S3StateIO measure={0};measure.ok=1;s3_video_state(&measure);
    uint8_t *blob=(uint8_t*)malloc(measure.pos);CHECK(blob);
    S3StateIO io={blob,measure.pos,0,0,1};s3_video_state(&io);CHECK(io.ok);
    s_build.sprites[0].x=48;publish_sprites();s3_video_vblank();
    CHECK(s_upload_serial>uploaded);scanline(&v,0,native,320,out,796);
    CHECK(out[actor_x]!=expected[actor_x]);
    io=(S3StateIO){blob,measure.pos,0,2,1};s3_video_state(&io);CHECK(io.ok);
    CHECK(s_upload_serial==uploaded);s3_video_state_loaded();
    memcpy(ram_before,g_ram,sizeof ram_before);GVDP restored=v;
    sonic3_video.presentation_reset();scanline(&v,0,native,320,out,796);
    CHECK(!memcmp(expected,out,sizeof out));
    CHECK(!memcmp(ram_before,g_ram,sizeof ram_before) && !memcmp(&restored,&v,sizeof v));
    /* The marker is consumed once: a following raw RB reset must retire it. */
    sonic3_video.presentation_reset();scanline(&v,0,native,320,out,796);
    CHECK(!s_display && !s_upload_serial && out[actor_x]!=expected[actor_x]);
    free(blob);CHECK(configure("off"));
}

static void test_time_interpolation(void)
{
    GVDP v;static uint32_t row[320],image[320*224];
    memset(g_ram,0,sizeof g_ram);time_test_font();time_test_sat(&v,12,0);
    CHECK(configure("off"));s_world_cell=NULL;s_world_publish=NULL;s_actor_overlay=NULL;s_frame_overlay=NULL;
    g_ram[0xF600]=12;g_ram[0xFE10]=1;g_ram[0xFE23]=12;g_ram[0xF711]=1;
    write32(0xB000,0x123456);word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
    memset(s_ix_history,0,sizeof s_ix_history);s_ix_upload_serial=0;
    test_time_private_palette=1;interpolation_configure(1);
    for(unsigned tick=1;tick<=2;tick++) {
        SceneFrame *f=&s_ix_history[1];memset(f,0,sizeof *f);f->serial=tick;f->scene=1;
        memcpy(f->sat,v.vram+0xF800,640);word(g_ram,0xFE04,tick);
        for(int y=0;y<224;y++) {
            memset(row,0,sizeof row); /* prove redraw rather than image reuse */
            interpolation_capture(&v,y,row,320,320,NULL);
        }
    }
    CHECK(ix_current()->valid && ix_previous()->valid && !s_ix_overlay_seen);
    test_time_private_palette=0;g_ram[0xFE23]=99;memset(v.vram,0,sizeof v.vram);
    CHECK(interpolate(.5,image,320,224));
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<8;x++) {
        unsigned b=g_rom[S3_TIME_DIGITS+64+y*4+x/2],n=(b>>((x&1)?0:4))&15;
        unsigned color=y<8?(n==1?0xE:0xE00):(n==1?0xE0:0xEEE);
        CHECK(image[(24+y)*320+48+x]==(n?genesis_dac_cram_to_argb((uint16_t)color,GENESIS_DAC_NORMAL):0xFF000000));
    }
    CHECK(g_ram[0xFE23]==99);interpolation_configure(0);
}
static void test_knux_preview_publication(void)
{
#ifdef SONIC3_STANDALONE
    const unsigned wait=0x473C6,draw_wait=0x53CE2,fade=0x47360,
        timer_wait=0x53008,capsule=0x47390,handover=0x4744A,
        initializer=0x446F2,actor=0x44926;
#else
    const unsigned wait=0x69526,draw_wait=0x85668,fade=0x69482,
        timer_wait=0x8488A,capsule=0x694A4,handover=0x69588,
        initializer=0x61D4C,actor=0x61F98;
#endif
    const unsigned map=S3_ART_BANK+0x164016;
    memset(g_ram,0,sizeof g_ram);memset(&s_build,0,sizeof s_build);
    CHECK(configure("32:9"));width(796,224,320,224);
    g_ram[0xF600]=12;word(g_ram,0xFE10,1);write32(0xB000,0x1234);
    word(g_ram,0x8000,256);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEE80,0x4A38);word(g_ram,0xEE84,0x15A);word(g_ram,0xEEAA,0x7FF);
    write32(0xB206,wait);
    capture_objects();CHECK(s_build.preview_knux);publish_sprites();
    CHECK(s_history[s_serial%3].preview_knux);
    write32(0xB206,draw_wait);write32(0xB23A,fade);
    capture_objects();CHECK(s_build.preview_knux);publish_sprites();
    CHECK(s_history[s_serial%3].preview_knux);
    write32(0xB23A,0x1234);capture_objects();CHECK(!s_build.preview_knux);
    write32(0xB206,timer_wait);write32(0xB23A,capsule);
    capture_objects();CHECK(s_build.preview_knux);publish_sprites();
    CHECK(s_history[s_serial%3].preview_knux);
    write32(0xB23A,0x1234);capture_objects();CHECK(!s_build.preview_knux);
    write32(0xB206,wait);capture_objects();publish_sprites();
    /* The earlier allocated slot has not reached its initializer/Draw_Sprite. */
    write32(0xB206,handover);write32(0xB128,initializer);
    capture_objects();CHECK(s_build.preview_knux);publish_sprites();
    CHECK(s_history[s_serial%3].preview_knux);
    /* The real pose is now queued in this same completed display list. */
    write32(0xB128,actor);write32(0xB134,map);
    g_ram[0xB12C]=5;g_ram[0xB12E]=24;g_ram[0xB12F]=28;g_ram[0xB14A]=22;
    word(g_ram,0xB132,0xA4DA);word(g_ram,0xB138,0x4B8E);word(g_ram,0xB13C,0x17D);
    word(g_rom,map+44,0x80);word(g_rom,map+0x80,1);
    word(g_rom,map+0x82,0xF800);word(g_rom,map+0x84,0);word(g_rom,map+0x86,0);
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
    /* The original waiting pose follows the installed ledge between native
     * frames too. A live camera one upload ahead is not its anchor. */
    static uint32_t half[796*224];
    s_actor_overlay=NULL;s_frame_overlay=NULL;s_world_publish=NULL;
    s_actor_interpolation_blocked=0;s_ix_overlay_seen=0;
    interpolation_configure(1);
    for(unsigned tick=0;tick<2;++tick) {
        word(g_ram,0xEE80,0x4A00+tick*8);word(g_ram,0xFE04,tick+1);
        for(int y=0;y<224;++y)
            word(v.vram,0xF000+(unsigned)y*4,(unsigned)(-0x49F8-(int)tick*8+(y%3==0)));
        memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.preview_knux=1;
        s_build.camera_x=0x49F8+tick*8;s_build.camera_y=0x160;
        publish_sprites();
        for(int y=0;y<224;++y) {
            scanline(&v,y,native,320,out,796);
            interpolation_capture(&v,y,out,796,320,NULL);
        }
        CHECK(ix_current()->valid);
    }
    CHECK(interpolate(.5,half,796,224));
    CHECK(half[(0x17D-0x160)*796 + 238 + 0x4B8E - 0x49FC]==0xFFFF0000);
    CHECK(half[(0x17D-0x160)*796 + 238 + 0x4B8E - 0x4A00]!=0xFFFF0000);
    interpolation_configure(0);
    memset(&s3_knux_preview_cache,0,sizeof s3_knux_preview_cache);
    CHECK(configure("off"));
}

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
    /* AIZ intro, fire and act2 must keep their existing camera policy. */
    const unsigned cases[][5]={{0x20,0,0,1},{0x24,0,0,1},{0,0,4,1},
        {0,0,8,1},{0,0,0,0},{0,0,12,0},{0,1,8,0},{0x21,0,0,0},{0,0,8,0,1}};
    for(unsigned z=0;z<sizeof cases/sizeof *cases;++z) {
        CHECK(configure("off"));interpolation_configure(0);
        CHECK(configure("fit"));width(398,224,320,224);interpolation_configure(1);
        memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);
        g_ram[0xF600]=12;g_ram[0xFE10]=(uint8_t)cases[z][0];
        g_ram[0xFE11]=(uint8_t)cases[z][1];word(g_ram,0xEEC2,cases[z][2]);write32(0xB000,0x123456);
        word(g_ram,0x8000,16);word(g_ram,0x8008,0x8100);
        for(unsigned i=0;i<4;i++)word(g_ram,0x9008+i*2,2);
        v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
        v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
        if(cases[z][4])v.reg[11]=3;
        v.cram[1]=0xEEE;v.cram[2]=0xE;
        memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);
        s3_video_set_world_cell(deformed_cell);s_world_publish=NULL;
        s_ix_overlay_seen=0;test_atlas_enabled=test_private_planes=0;
        int undeformed=(int)cases[z][3];
        for(int phase=0;phase<2;++phase) {
            /* As in the real spring capture, the RAM camera is ahead of
             * the scroll/SAT currently installed by VBlank. */
            word(g_ram,0xEE80,(unsigned)(66+phase*4));
            word(g_ram,0xEE84,(unsigned)(98+phase*4));word(g_ram,0xFE04,phase+1);
            word(v.vram,0xF000,(unsigned)(-64-phase*4));v.vsram[0]=(uint16_t)(100+phase*4);
            if(cases[z][4])for(unsigned y=1;y<224;++y)
                word(v.vram,0xF000+y*4,(unsigned)(-64-phase*4+(int)(y&1)));
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
        /* Deformed LZ and the excluded AIZ phases keep the non-plane
         * camera; their scroll deformation is not an actor offset. */
        CHECK(ix_current()->context.fg_x==(undeformed?68:70));
        CHECK(ix_current()->context.fg_y==(undeformed?104:102));
        s3_video_set_world_cell(NULL);interpolation_configure(0);CHECK(configure("off"));
    }
}
/* Original actor deletion is simulated here; render ownership must survive
 * that boundary without keeping a guest slot or borrowing overwritten art. */
static void test_aiz_wave_original_art(void)
{
    uint8_t compressed[]={0,1,0x87,0x71,0,0xFF,0},decoded[32];
    CHECK(art_nem(compressed,sizeof compressed,decoded,sizeof decoded)==32);
    for(unsigned i=0;i<32;++i)CHECK(decoded[i]==0x77);
    compressed[0]=128;CHECK(art_nem(compressed,sizeof compressed,decoded,sizeof decoded)==32);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<4;++x)CHECK(decoded[y*4+x]==(y&1?0:0x77));
    CHECK(!art_nem(compressed,sizeof compressed-1,decoded,sizeof decoded));
    CHECK(!art_nem(compressed,sizeof compressed,decoded,sizeof decoded-1));
    static GVDP v;uint32_t native[320]={0},row[796];
    CHECK(configure("off"));CHECK(configure("fit"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);presentation_reset();
    g_ram[0xF600]=12;write32(0xB000,0x123456);word(g_ram,0x8000,64);word(g_ram,0x8008,0x8100);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xEEE;v.cram[2]=0xE;v.cram[3]=0xEE;
    /* Tile4DE was transparent in the wave, then gets Knuckles' pixels. */
    memset(s_aiz_wave_art,0,sizeof s_aiz_wave_art);s_aiz_wave_art_ready=s_aiz_wave_art_attempted=1;
    s_aiz_wave_art[(0x4DE - 0x3D1)*32]=0x10;
    memset(v.vram+0x4DE*32,0x33,32);
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.count=2;s_build.code[1]=S3_AIZ_WAVE;
    s_build.sprites[0]=(SceneSprite){-8,0,0x4DE,0,2,256,1};
    s_build.sprites[1]=(SceneSprite){8,0,0x4DE,0,2,256,1};
    publish_sprites();scanline(&v,0,native,320,row,796);
    CHECK(row[230]==0xFFFFFFFFu && row[231]!=0xFFFFFF00u);
    CHECK(row[246]==0xFFFFFF00u); /* native center keeps installed art */
    /* The pose/pixel ownership survives a new guest pose and art upload. */
    g_ram[0xB022]=99;memset(v.vram+0x4DE*32,0x22,32);
    scanline(&v,0,native,320,row,796);
    CHECK(row[230]==0xFFFFFFFFu && row[246]==0xFFFF0000u);
    CHECK(!memcmp(&s_display_frame.sprites[0],&s_build.sprites[0],sizeof(SceneSprite)));
    s_aiz_wave_art_ready=s_aiz_wave_art_attempted=0;CHECK(configure("off"));
}
static void test_aiz_plane_retirement(void)
{
    static GVDP v;static uint32_t native[320],row[796],half[796*224];
    CHECK(configure("off"));interpolation_configure(0);CHECK(configure("fit"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);presentation_reset();
    s_world_publish=NULL;s_actor_overlay=NULL;test_atlas_enabled=0;s_actor_interpolation_blocked=0;
    g_ram[0xF600]=12;write32(0xB000,0x123456);word(g_ram,0x8000,64);word(g_ram,0x8008,0x8100);
    word(g_ram,0xEEAA,0x7FF);word(g_ram,0xEE80,1600);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=0xE;v.cram[3]=0xE0;v.cram[7]=0xE00;
    word(v.vram,0xF000,(uint16_t)-1600);
    /* Minimal original-style mapping frames; the production path uses the
     * owned ROM's complete body, propeller and exhaust mapping tables. */
    for(unsigned frame=0;frame<7;++frame) {
        unsigned offset=16+frame*8;word(g_rom,S3_AIZ_PLANE_MAP+frame*2,offset);
        word(g_rom,S3_AIZ_PLANE_MAP+offset,1);g_rom[S3_AIZ_PLANE_MAP+offset+2]=0;
        g_rom[S3_AIZ_PLANE_MAP+offset+3]=0;word(g_rom,S3_AIZ_PLANE_MAP+offset+4,frame);
        word(g_rom,S3_AIZ_PLANE_MAP+offset+6,0);
    }
    const uint8_t prop[]={0,1,2,3,4,3,2,0xFC},jet[]={0,5,6,0xFC};
    memcpy(g_rom+S3_AIZ_PLANE_PROP_ANIM,prop,sizeof prop);
    memcpy(g_rom+S3_AIZ_PLANE_EXHAUST_ANIM,jet,sizeof jet);
    memset(s_aiz_plane_art,0x11,sizeof s_aiz_plane_art);s_aiz_plane_art_ready=1;
    memset(v.vram+0x529*32,0x11,7*32);
    const unsigned slots[]={0xB1BC,0xB206,0xB250};
    const unsigned code[]={S3_AIZ_PLANE_DEPART,S3_AIZ_PLANE_PROP,S3_AIZ_PLANE_EXHAUST};
    const unsigned frame[]={0,3,5};
    word(g_ram,0xAC00,6);word(g_ram,0xFE04,100);
    for(unsigned n=0;n<3;++n) {
        unsigned o=slots[n];word(g_ram,0xAC02+n*2,o);write32(o,code[n]);
        write32(o+12,S3_AIZ_PLANE_MAP);word(g_ram,o+10,0x529);
        word(g_ram,o+16,35+(n==1?56:n==2?24:0));word(g_ram,o+20,281+(n==1?4:n==2?24:0));
        g_ram[o+0x22]=(uint8_t)frame[n];g_ram[o+0x23]=(uint8_t)(n==1?4:0);
        if(n)word(g_ram,o+0x46,slots[0]);
    }
    word(g_ram,slots[0]+0x1A,0xC0);
    capture_objects();CHECK(s_build.aiz_plane.phase==1 && s_build.aiz_plane.mask==7);
    CHECK(s_build.count==3 && s_build.aiz_plane.object[0].x==-93);
    publish_sprites();
    /* Owned fractional snapshots can interpolate the retirement handover. */
    interpolation_configure(1);s_ix_overlay_seen=0;
    for(int y=0;y<224;++y){scanline(&v,y,native,320,row,796);interpolation_capture(&v,y,row,796,320,NULL);}
    for(unsigned n=0;n<3;++n)write32(slots[n],0);
    word(g_ram,0xAC00,0);word(g_ram,0xFE04,101);
    static uint8_t guest_before[65536];memcpy(guest_before,g_ram,sizeof guest_before);
    capture_objects();publish_sprites();
    CHECK(!memcmp(guest_before,g_ram,sizeof guest_before));
    CHECK(s_build.aiz_plane.phase==2 && s_build.aiz_plane.object[0].x==-97);
    CHECK(s_build.aiz_plane.y_velocity==0xB0 && s_build.aiz_plane.y_fraction==0xB000);
    CHECK(s_build.aiz_plane.object[1].frame==2 && s_build.aiz_plane.object[2].frame==6);
    CHECK(s_build.count==3 && s_build.sprites[0].hud==5);
    AizPlanePose invalid_pose=s_build.aiz_plane;
    invalid_pose.mask=8;CHECK(!aiz_plane_valid(&invalid_pose));invalid_pose=s_build.aiz_plane;
    invalid_pose.object[1].cursor=6;CHECK(!aiz_plane_valid(&invalid_pose));invalid_pose=s_build.aiz_plane;
    invalid_pose.y_velocity=0xC1;CHECK(!aiz_plane_valid(&invalid_pose));
    /* The reused VRAM tile now has unrelated blue pixels. Retiring art is
     * decoded from the original ROM, with the current row's native palette. */
    memset(v.vram+0x529*32,0x77,7*32);
    for(int y=0;y<224;++y){scanline(&v,y,native,320,row,796);interpolation_capture(&v,y,row,796,320,NULL);}
    CHECK(ix_current()->valid && ix_previous()->valid);
    SceneFrame before=s_build;AizPlanePose selected=s_display_frame.aiz_plane;
    GVDP device=v;CHECK(interpolate(.5,half,796,224));
    CHECK(half[153*796+143]==0xFFFF0000u && half[153*796+142]!=0xFFFF0000u);
    CHECK(!memcmp(&before,&s_build,sizeof before) && !memcmp(&device,&v,sizeof v));
    CHECK(!memcmp(guest_before,g_ram,sizeof guest_before));
    CHECK(!memcmp(&selected,&s_display_frame.aiz_plane,sizeof selected));
    for(int x=238;x<558;++x)CHECK(half[153*796+x]!=0xFFFF0000u);
    interpolation_configure(0);scanline(&v,0,native,320,row,796);
    scanline(&v,153,native,320,row,796);CHECK(row[141]==0xFFFF0000u);
    v.cram[1]=6;scanline(&v,153,native,320,row,796);CHECK(row[141]==0xFF770000u);v.cram[1]=14;
    /* Both supported state paths own the complete current pose. Invalid
     * compact metadata is rejected before any part is applied. */
    size_t bytes=presentation_state_save(NULL,0);uint8_t *blob=(uint8_t*)malloc(bytes);CHECK(blob);
    CHECK(presentation_state_save(blob,bytes)==bytes);
    size_t at=sizeof(unsigned)+sizeof(int)+sizeof(double)+offsetof(SceneFrame,aiz_plane)-offsetof(SceneFrame,count);
    unsigned phase=0;memcpy(&phase,blob+at,sizeof phase);unsigned invalid=3;
    memcpy(blob+at,&invalid,sizeof invalid);CHECK(!presentation_state_load(blob,bytes,1));
    CHECK(s_build.aiz_plane.phase==2);memcpy(blob+at,&phase,sizeof phase);
    memset(&s_build.aiz_plane,0,sizeof s_build.aiz_plane);CHECK(presentation_state_load(blob,bytes,1));
    CHECK(s_build.aiz_plane.phase==2 && s_build.aiz_plane.object[0].x==-97);free(blob);
    S3StateIO measure={0};measure.ok=1;s3_video_state(&measure);
    blob=(uint8_t*)malloc(measure.pos);CHECK(blob);
    S3StateIO io={blob,measure.pos,0,0,1};s3_video_state(&io);CHECK(io.ok);
    memset(&s_build.aiz_plane,0,sizeof s_build.aiz_plane);
    io=(S3StateIO){blob,measure.pos,0,2,1};s3_video_state(&io);CHECK(io.ok);
    CHECK(s_build.aiz_plane.phase==2 && s_build.aiz_plane.object[0].x==-97);free(blob);
    /* Original mapping bounds decide retirement, rather than a frame count. */
    unsigned ticks=0;
    while(s_build.aiz_plane.phase && ticks++<100) {
        word(g_ram,0xFE04,102+ticks);capture_objects();
    }
    CHECK(!s_build.aiz_plane.phase && !s_build.count);
    /* Video Off and native-only FPS cannot acquire a retiring plane. */
    CHECK(configure("off"));s_capture_frame=&s_ix_build;s_capture_readonly=1;
    capture_objects();CHECK(!s_ix_build.aiz_plane.phase);s_capture_frame=&s_build;s_capture_readonly=0;
    s_aiz_plane_art_ready=0;
}
static void test_blue_spheres(void)
{
    GVDP v={0};M68KState cpu_before=g_cpu;
    /* Draw_SSSprites reads the 32x32 board at $8000 in S3, and at
     * SStage_layout_buffer+$100 ($F100) in S&K/lock-on. Keep the fixture's
     * source addresses independent of the renderer constants. */
#ifdef SONIC3_STANDALONE
    const unsigned native_board=0x8000;
#else
    const unsigned native_board=0xF100;
#endif
    /* Split GET/PERFECT banners must leave the whole canvas despite their
     * original +/-192px endpoint; only anchors move, never glyph spacing. */
    for(int canvas=320;canvas<=1600;canvas+=320) {
        int x,left,right,second;
        CHECK(ss_banner_x(0xA57B,132,96,canvas,1,&x));CHECK(x==(canvas-320)/2+132);
        CHECK(ss_banner_x(0xA57B,-60,96,canvas,1,&left));CHECK(left+32<=0);
        CHECK(ss_banner_x(0xA583,356,96,canvas,1,&right));CHECK(right>=canvas);
        CHECK(ss_banner_x(0xA55F,32,96,canvas,0,&left));
        CHECK(ss_banner_x(0xA565,64,96,canvas,0,&second));CHECK(second-left==32);
        /* Right GET arrow wraps to -96 in the native 9-bit SAT. */
        CHECK(ss_banner_x(0xAEF8,-96,96,canvas,0,&right));CHECK(right>=canvas);
        CHECK(!ss_banner_x(0xC589,72,8,canvas,0,&x));
    }
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
    /* Original stationary checker boundary: the first lower quadrant is
     * row 163, independent of the expanded canvas width. */
    CHECK(py>162 && py<163);
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
    memset(v.vram+64,0x11,512);memset(g_ram+native_board,2,1024);
#ifdef SONIC3_STANDALONE
    /* Unrelated S3 RAM at the lock-on address must not replace its board. */
    g_ram[0xF100]=2;
#endif
    g_ram[0xF600]=0x34;ss_capture();
    static uint8_t before[65536];memcpy(before,g_ram,sizeof before);
    ss_begin(&v,1603);CHECK(ss_ready && ss_count>80 && ss_margin_count>80);
    CHECK(!memcmp(before,g_ram,sizeof before));
    CHECK(ss_texture[2][0][32*64+32]==33);
    write16(0xE422,512);ss_begin(&v,1603);CHECK(ss_x==0); /* no mixed publication */
    ss_capture();ss_begin(&v,1603);CHECK(ss_x==2);
    ss_begin(&v,398);CHECK(ss_ready && ss_width==398);
    ss_begin(&v,320);CHECK(ss_ready && ss_width==320);
    /* A collected contact marker still has the original blue artwork.
     * The guest changes it to red later; host visibility must not shorten
     * that grace period, including during a turn. */
    memset(g_ram+native_board,0,1024);word(g_ram,0xE422,0);word(g_ram,0xE424,0);
    write32(SS_EXTRA+10*8,0x1000);word(g_ram,SS_EXTRA+10*8+4,0x4002);
    write32(SS_EXTRA+1*8,0x1000);word(g_ram,SS_EXTRA+1*8+4,0x0002);
    uint32_t contact_blue[796],contact_grace[796],contact_palette[64]={0};
    contact_palette[33]=0xFF4488EE;contact_palette[1]=0xFFEE4444;
    g_ram[native_board]=2;ss_capture();ss_begin(&v,796);CHECK(ss_count==1);
    ss_scanline(&v,155,contact_blue,796,contact_palette,contact_palette,0);
    CHECK(contact_blue[398]==contact_palette[33]);
    g_ram[native_board]=10;memcpy(before,g_ram,sizeof before);
    ss_capture();ss_begin(&v,796);CHECK(ss_count==1 && ss_spheres[0].kind==10);
    ss_scanline(&v,155,contact_grace,796,contact_palette,contact_palette,0);
    CHECK(!memcmp(contact_blue,contact_grace,sizeof contact_blue));
    CHECK(!memcmp(before,g_ram,sizeof before));
    g_ram[native_board]=1;ss_capture();ss_begin(&v,796);
    ss_scanline(&v,155,contact_grace,796,contact_palette,contact_palette,0);
    CHECK(contact_grace[398]==contact_palette[1]);
    /* Render_Sprites precedes Draw_SSSprites in the original, so its
     * earlier character entry wins even over a near contact sphere. */
    unsigned saved_sat=v.reg[5];v.reg[5]=0x7C;
    memset(v.vram+0xF800,0,8);word(v.vram,0xF800,128+160);
    word(v.vram,0xF804,0x2780);word(v.vram,0xF806,128+156);
    memset(v.vram+0x780*32,0x33,32);contact_palette[19]=0xFF00FF00;
    g_ram[native_board]=2;word(g_ram,0xE424,32);g_ram[0xE426]=0x80;
    ss_capture();ss_begin(&v,796);
    CHECK(ss_count==1 && ss_spheres[0].depth<ss_distance);
    ss_scanline(&v,160,contact_grace,796,contact_palette,contact_palette,0);
    CHECK(contact_grace[398]==contact_palette[19]);
    /* A ground shadow stays beneath a contact sphere. The character keeps
     * its native priority above both; uncovered ground is still shaded. */
    uint32_t contact_shadow[64];memcpy(contact_shadow,contact_palette,sizeof contact_shadow);
    contact_palette[56]=0xFF886644;contact_palette[60]=0xFFAA8844;
    contact_shadow[56]=0xFF443322;contact_shadow[60]=0xFF554422;
    contact_shadow[33]=0xFF224477;v.reg[12]=9;
    ss_scanline(&v,160,contact_grace,796,contact_palette,contact_shadow,0);
    uint32_t sphere_in_front=contact_grace[392];
    v.vram[0xF803]=1;word(v.vram,0xF808,128+160);
    word(v.vram,0xF80C,0xE781);word(v.vram,0xF80E,128+154);
    memset(v.vram+0x781*32,0xFF,32);
    ss_scanline(&v,160,contact_grace,796,contact_palette,contact_shadow,0);
    CHECK(contact_grace[392]==sphere_in_front);
    CHECK(contact_grace[398]==contact_palette[19]);
    g_ram[native_board]=0;ss_capture();ss_begin(&v,796);
    ss_scanline(&v,160,contact_grace,796,contact_palette,contact_shadow,0);
    CHECK(contact_grace[392]==contact_shadow[56] || contact_grace[392]==contact_shadow[60]);
    memset(v.vram+0xF808,0,8);memset(v.vram+0x781*32,0,32);v.reg[12]=0;
    memset(v.vram+0xF800,0,8);memset(v.vram+0x780*32,0,32);v.reg[5]=(uint8_t)saved_sat;
    /* Preserve the original mapping-center registration while moving,
     * turning and resizing. Opaque distance-frame radii vary; aligning their
     * bottoms instead moves the art relative to the fixed character. */
    uint32_t checker[1600],checker_colors[64]={0};
    checker_colors[56]=0xFF00FFFF;checker_colors[60]=0xFFFF00FF;
    for(unsigned turn=0;turn<8;++turn)for(unsigned motion=0;motion<3;++motion)
    for(int canvas=320;canvas<=1600;canvas+=320) {
        double angle=turn*6.2831853071795864769/8;
        int ix=(int)round(-2*sin(angle)),iy=(int)round(-2*cos(angle));
        memset(g_ram+native_board,0,1024);
        g_ram[native_board+((unsigned)iy&31)*32+((unsigned)ix&31)]=2;
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
        CHECK(fabs(sphere->x+sphere->w*.5-(px+(canvas-1)*.5))<=.501);
        CHECK(fabs(sphere->y+sphere->h*.5-(py-5))<=.501);
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
    /* Stage skies inherit SHI priority even where plane A is transparent.
     * Native A coverage extends across the panorama without repeating HUD. */
    v.reg[16]=0x11;v.cram[1]=0xEEE;memset(v.vram+32,0x11,32);
    for(unsigned x=0;x<64;++x) {
        word(v.vram,0xC000+x*2,0x8000);
        word(v.vram,0xE000+x*2,1);
    }
    scanline_lazy(&v,0,320,out,796,test_native_row,native,NULL);
    for(unsigned x=0;x<796;++x)CHECK(out[x]==0xFFFFFFFFu);
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
    /* The native priority-one HUD icon covers plane A where they overlap. */
    memset(&v,0,sizeof v);v.reg[2]=0x30;v.reg[5]=0x7C;
    word(v.vram,0xF800,128+8);v.vram[0xF802]=10;
    word(v.vram,0xF804,0xC589);word(v.vram,0xF806,128+72);
    memset(v.vram+0x589*32,0x33,9*32);
    for(unsigned x=0;x<40;++x)word(v.vram,0xC000+(x+40)*2,2);
    memset(v.vram+64,0x22,32);
    uint32_t hud_colors[64]={0};hud_colors[2]=0xFF112233;hud_colors[35]=0xFFABCDEF;
    ss_count=0;ss_ready=1;ss_color_phase=0;ss_clear=0;
    ss_scanline(&v,10,out,796,hud_colors,hud_colors,0);
    CHECK(out[72]==hud_colors[35] && out[79]==hud_colors[35]);
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

#include "sonic3_aiz_join_test.inc"
int main(int argc,char **argv)
{
    if(argc==2) {
        FILE *rom=fopen(argv[1],"rb");CHECK(rom);
        CHECK(fread(g_rom,1,sizeof g_rom,rom)==S3_ART_BANK+0x200000u);fclose(rom);
        CHECK(load_menu_map());
        for(unsigned n=0;n<40*28;n++)CHECK((s_menu_map[n]&2047u)<0x29Fu);
        s_menu_map_ready=0;
        load_aiz_art();CHECK(s_aiz_art_ready);
        CHECK(aiz_join_load());
        CHECK(load_aiz_monkey_art());
        printf("AIZ joined original-map caches: %zu bytes\n",sizeof s_aiz_join_map+sizeof s_aiz_join_second);
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
    /* A placed capsule must survive an approach/backtrack before the boss
     * locks the camera. Native deletion intentionally keeps its loaded bit. */
    word(g_rom,0x100012,0x4560);word(g_rom,0x100014,0x8060);g_rom[0x100016]=4;
    word(g_rom,0x100018,65535);
    longword(g_rom,0x110010,S3_EGG_CAPSULE);longword(g_rom,0x5CCA6,S3_EGG_CAPSULE);
    s_placement_count=0;
    write16(0xEE78,16919);s3_video_hook(S3_LOAD_UPDATE);
    CHECK(!(g_ram[0xEB03]&128));
    write16(0xEE78,16886);s3_video_hook(S3_LOAD_UPDATE);
    CHECK(!(g_ram[0xEB03]&128));
    write16(0xEE78,0x4390);s3_video_hook(S3_LOAD_UPDATE);
    CHECK(g_ram[0xEB03]&128);
    CHECK(scene_read32(0xFFB1BC)==S3_EGG_CAPSULE);
    /* Both object loaders must leave HCZ1's fixed splash slot free. Fill
     * the two earlier slots and verify a real placement goes past it. */
    write32(0xB172,0);write32(0xB1BC,0);
    longword(g_rom,S3_PLACEMENTS+8,0x100000); /* HCZ1 placement table */
    word(g_rom,0x100000,640);word(g_rom,0x100002,0x8060);
    word(g_rom,0x100006,65535);
    g_rom[0x100004]=1;g_ram[0xEB00]=0;
    word(g_ram,0xFE10,0x100);write16(0xEE78,128);s_placement_count=0;
    s3_video_hook(S3_LOAD_UPDATE);
    CHECK(!scene_read32(0xFFB172));
    CHECK(scene_read32(0xFFB1BC)==0x123456);
    word(g_ram,0xFE10,0);
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
        /* At SOZ's clamped edge B deliberately keeps its centered native
         * parallax; a column under A's displaced viewport is not its oracle. */
        if(c<2)CHECK(s_bg_unstreamed>0 && s_bg_errors==0);
        else CHECK(s_bg_checks>0 && s_bg_errors==0);
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
        int bg_anchor=act?s_native_x:238;
        for(int x=0;x<796;++x)
            CHECK(out[x]==(((x-bg_anchor+1024)&511)<256?0xFFFF0000:0xFF00FF00));
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
    test_sprite_atlas();test_aiz_monkey_owned_art();test_hcz_surface_extent();test_hcz_rush_extent();test_hcz_brick_fractional_phase();test_object_zoom();test_level_select_word_edges();test_file_select();test_competition_character_palette();test_competition_race();test_competition_pre_cull();test_competition_margin_sprites();test_results_margin_sprites();test_menu_layers();test_world_snapshot();test_hud_mapping();test_first_hud_fade();
    test_deformation_keeps_actors_still();test_streamed_background_sources();test_icz_partial_background_rows();test_icz_outdoor_snow_edge();test_native_vertical_layout_wrap();test_cnz_boss_background_band();test_mgz2_falling_arena();test_mgz2_perspective_owner();test_native_sprite_art_ownership();test_waiting_knuckles_palette();
    test_s1_undeformed_installed_camera();test_imported_pylon_wrap();test_doomsday_scrolling_viewport();test_uploaded_scene_camera();
    test_screen_actor_priority();
    test_aiz_intro_canvas_edge();test_aiz_intro_live_palette();test_aiz_cached_native_fade();test_aiz_tree_palette_boundary();test_hcz_tunnel_palette_retirement();test_hcz_boss_foreground_palette();test_aiz_intro_owned_pose();
    test_aiz_wave_original_art();test_aiz_plane_retirement();
    test_aiz2_ship_cameras();
    test_private_plane_colors();
    test_hud_object_anchor();
    test_native_bonus();test_bonus_margins();test_gumball_authored_extent();test_bonus_authored_planes();test_glowing_ring_ownership();test_slots_complete_board();test_bonus_entry_publication();test_native_demo_publication();
    test_title_card_edges();
    test_knux_preview_publication();
    test_knux_preview_installed_scroll();
    test_interpolation_owned_and_readonly();
    test_s1_interpolation_installed_camera();
    test_three_digit_lives();test_life_interpolation();
    test_time_minute_tens();test_time_interpolation();
    test_presentation_restore_before_tick();test_native_state_keeps_restored_publication();
    test_local_presentation_state();
    test_blue_spheres();
    test_aiz_join();
    puts("Sonic 3 video terrain, activation, publication, file select, Blue Spheres and immutable interpolation PASS");
    return 0;
}
