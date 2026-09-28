/* Test the actual renderer's row-pointer layout and host publication helpers. */
#include "../../game/common/sonic3_video.c"
uint8_t g_ram[65536],g_rom[0x400000];
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
    puts("Sonic 3 video terrain, activation, publication and Blue Spheres PASS");
    return 0;
}
