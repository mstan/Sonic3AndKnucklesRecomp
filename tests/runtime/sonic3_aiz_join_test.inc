static void aiz_join_test_world(unsigned act)
{
    const AizJoinMap *m=&s_aiz_join_map[act];
    const AizArt *art=act?&s_aiz_join_second:&s_aiz_main;
    memcpy(g_ram,m->chunks,0x8000);memcpy(g_ram+0x8000,m->layout,0x1000);
    memcpy(g_ram+0x9000,art->blocks,0x1800);
    g_ram[0xF600]=12;g_ram[0xFE10]=0;g_ram[0xFE11]=(uint8_t)act;
    write32(0xB000,0x123456);word(g_ram,0xEE80,act?0x10:0x2F10);
    word(g_ram,0xEE84,act?0x280:0x300);word(g_ram,0xEEC2,act?0:8);
}
static void test_aiz_join(void)
{
    static GVDP v;static uint8_t untouched[65536];
    uint32_t native[320]={0},before[796],after[796];
    CHECK(configure("off"));CHECK(configure("32:9"));width(1,1,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);presentation_reset();
    memset(s_aiz_join_map,0,sizeof s_aiz_join_map);
    memset(&s_aiz_main,0,sizeof s_aiz_main);memset(&s_aiz_join_second,0,sizeof s_aiz_join_second);
    s_aiz_join_attempted=s_aiz_join_ready=s_aiz_art_ready=1;
    s3_video_set_world_cell(NULL);s_visible_count=0;s_placement_count=0;
    /* Static placeholders must never appear in animated slots. A future
     * neighbor uses its original first frame, a live bank its installed
     * frame; transparent pixels and both flips retain normal semantics. */
    memset(g_rom+S3_ART_BANK+0xA3F00,0x12,0x17*32);
    memset(g_rom+S3_ART_BANK+0xA5880,0x34,0x460);
    memset(v.vram+0xB3*32,0x56,0x17*32);
    CHECK(aiz_join_pixel(&v,1,1,0,0,0x60B3)==49);
    CHECK(aiz_join_pixel(&v,1,1,0,0,0x68B3)==50);
    s_frame_act=1;CHECK(aiz_join_pixel(&v,1,3,0,0,0x60B3)==53);s_frame_act=0;
    CHECK(aiz_join_pixel(&v,1,1,0,0,0x60CA)==51);
    /* Distinct neighbors and an authored common overlap. A one-sided fix
     * loses the red landscape before the original rebase. */
    for(unsigned map=0;map<2;++map) {
        AizJoinMap *m=&s_aiz_join_map[map];AizArt *a=map?&s_aiz_join_second:&s_aiz_main;
        word(m->layout,0,map?153:97);
        for(unsigned row=0;row<32;++row)word(m->layout,8+row*4,0x8088);
        for(unsigned x=0;x<(map?153u:97u);++x)m->layout[0x88+x]=(uint8_t)(map || x>=94?2:1);
        for(unsigned p=0;p<128;p+=2){word(m->chunks,128+p,1);word(m->chunks,256+p,2);}
        for(unsigned p=0;p<8;p+=2){word(a->blocks,8+p,1);word(a->blocks,16+p,2);}
        memset(a->video.vram+32,0x11,32);memset(a->video.vram+64,0x22,32);
    }
    uint16_t margin_attr=0;uint8_t margin_pixel=0;
    CHECK(aiz_join_sample(&v,1,0x2F10,400,0x3100,0x300,&margin_attr,&margin_pixel));
    CHECK(margin_attr==2 && margin_pixel==2); /* current coast, not future art */
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;
    v.cram[1]=14;v.cram[2]=0xE0;v.cram[3]=0xE00;
    memset(v.vram+32,0x11,32);memset(v.vram+64,0x22,32);memset(v.vram+96,0x33,32);
    for(unsigned p=0;p<64*32;++p)word(v.vram,0xC000+p*2,2);
    aiz_join_test_world(0);word(v.vram,0xF000,(uint16_t)-0x2F10);v.vsram[0]=0x300;
    memset(&s_build,0,sizeof s_build);s_build.scene=1;s_build.aiz_join=aiz_join_tag(g_ram);
    CHECK(s_build.aiz_join==1);
    /* Rebase parity applies after the fire refresh installs the second
     * bank, not to the preceding green-forest scene. */
    s_build.aiz_join=2;s_build.camera_x=0x2F10;s_build.camera_y=0x300;s_build.count=1;
    s_build.sprites[0]=(SceneSprite){160,0,0x8003,0,0};publish_sprites();
    g_ram[0xF62A]=8;s3_video_vblank();memcpy(untouched,g_ram,sizeof untouched);
    scanline(&v,0,native,320,before,796);
    CHECK(s_native_x==238 && s_left==0x2E22);
    CHECK(view_left(0x2F10,796)==0x2D64); /* original activation bounds */
    for(int x=0;x<796;++x)CHECK(before[x]==(x>=398 && x<406?0xFF0000FFu:x<222?0xFFFF0000u:0xFF00FF00u));
    CHECK(!memcmp(untouched,g_ram,sizeof untouched));
    /* RAM rebases before this completed SAT/scroll. Its owned coordinate
     * tag keeps both margins and the actor in their original positions. */
    aiz_join_test_world(1);memcpy(untouched,g_ram,sizeof untouched);
    scanline(&v,0,native,320,after,796);
    CHECK(s_display->aiz_join==2 && s_native_x==238 && !memcmp(before,after,sizeof before));
    CHECK(!memcmp(untouched,g_ram,sizeof untouched));
    s_build.aiz_join=3;s_build.camera_x=0x10;s_build.camera_y=0x280;
    publish_sprites();s3_video_vblank();word(v.vram,0xF000,(uint16_t)-0x10);v.vsram[0]=0x280;
    memcpy(untouched,g_ram,sizeof untouched);scanline(&v,0,native,320,after,796);
    CHECK(s_native_x==238 && s_left==-222 && !memcmp(before,after,sizeof before));
    CHECK(!memcmp(untouched,g_ram,sizeof untouched));
    CHECK(aiz_join_left(0x2F10,320,1,0x3080)==0x2F10 && aiz_join_left(0x10,320,3,0x4C80)==0x10);
    CHECK(aiz_join_left(0x1000,796,0,0x4C80)==0xF12);
    word(g_ram,0xEEC2,0x10);g_ram[0xFE11]=0;word(g_ram,0x8000,97);
    CHECK(aiz_join_tag(g_ram)==2);g_ram[0xFE10]=1;CHECK(!aiz_join_tag(g_ram));
    aiz_join_test_world(1);
    /* Both serializers bound the tag; local restore must recover the
     * complete view before any native tick/publication. */
    size_t bytes=presentation_state_save(NULL,0);uint8_t *blob=(uint8_t*)malloc(bytes);CHECK(blob);
    CHECK(presentation_state_save(blob,bytes)==bytes);
    unsigned invalid=4;size_t at=sizeof(unsigned)+sizeof(int)+sizeof(double)+
        offsetof(SceneFrame,aiz_join)-offsetof(SceneFrame,count);
    memcpy(blob+at,&invalid,sizeof invalid);CHECK(!presentation_state_load(blob,bytes,1));
    invalid=3;memcpy(blob+at,&invalid,sizeof invalid);
    presentation_reset();CHECK(presentation_state_load(blob,bytes,1));
    scanline(&v,0,native,320,after,796);CHECK(s_display->aiz_join==3 && !memcmp(before,after,sizeof before));
    free(blob);
    static SceneFrame restored;blob=(uint8_t*)malloc(sizeof s_build);CHECK(blob);
    S3StateIO save={blob,sizeof s_build,0,0,1};scene_frame_state(&save,&s_build);CHECK(save.ok);
    S3StateIO load={blob,sizeof s_build,0,2,1};scene_frame_state(&load,&restored);CHECK(load.ok && restored.aiz_join==3);
    invalid=4;memcpy(blob+offsetof(SceneFrame,aiz_join),&invalid,sizeof invalid);
    S3StateIO bad={blob,sizeof s_build,0,1,1};scene_frame_state(&bad,&restored);CHECK(!bad.ok);
    free(blob);CHECK(configure("off"));s_aiz_join_ready=0;
    CHECK(aiz_join_left(0x2F10,796,1,0x3080)==0x2D64);
    s_aiz_join_attempted=0;
}
