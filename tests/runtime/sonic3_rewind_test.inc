static void test_local_presentation_state(void)
{
    static GVDP v;uint32_t native[320]={0},expected[796],out[796];
    CHECK(configure("off"));CHECK(configure("fit"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);presentation_reset();
    s_visible_count=0;s_placement_count=2;s_placement_base=0x1234;s_loader_active=1;
    memset(s_placements,0,sizeof s_placements);s_placements[1].loaded=1;
    g_ram[0xF600]=12;g_ram[0xFE10]=0x22;write32(0xB000,0x123456);
    word(g_ram,0x8000,64);word(g_ram,0x8008,0x8100);word(g_ram,0xEE80,512);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;
    v.reg[12]=1;v.reg[13]=0x3C;v.reg[16]=1;v.cram[1]=14;
    word(v.vram,0xF000,(uint16_t)-512);memset(v.vram+32,0x11,32);
    s_build.scene=1;s_build.camera_x=512;s_build.count=1;
    s_build.aiz_super_pose=2;
    s_build.sprites[0]=(SceneSprite){-16,0,1,0,0,256,0};publish_sprites();
    /* No artificial Super art in this fixture; token ownership is verified
     * separately without asking this synthetic ROM to decode that asset. */
    s_build.aiz_super_pose=0;s_history[s_serial%3].aiz_super_pose=0;
    g_ram[0xF62A]=8;s3_video_vblank();unsigned uploaded=s_upload_serial;
    scanline(&v,0,native,320,expected,796);
    int actor_x=s_native_x-16;CHECK(actor_x>=0 && actor_x+8<s_native_x);
    CHECK(expected[actor_x]==0xFFFF0000u);
    s_background_frame[65535]=0x5A;s_world_frame[65535]=0xA5;
    s_build.aiz_super_pose=2;
    size_t bytes=presentation_state_save(NULL,0);CHECK(bytes && bytes<200000);
    uint8_t *blob=(uint8_t*)malloc(bytes);CHECK(blob);
    CHECK(presentation_state_save(blob,bytes)==bytes);
    unsigned counts[5]={s_build.count,s_history[0].count,s_history[1].count,s_history[2].count,s_display_frame.count};
    s_build.count=s_display_frame.count=SCENE_SPRITES;
    for(unsigned n=0;n<3;++n)s_history[n].count=SCENE_SPRITES;
    s_placement_count=SCENE_PLACEMENTS;s_visible_count=144;
    size_t worst=presentation_state_save(NULL,0);
    CHECK(worst<3u*1024u*1024u);
    s_build.count=counts[0];for(unsigned n=0;n<3;++n)s_history[n].count=counts[n+1];s_display_frame.count=counts[4];
    s_placement_count=2;s_visible_count=0;s_build.aiz_super_pose=0;
    printf("local presentation: fixture=%zu maximum=%zu bytes\n",bytes,worst);
    s_build.sprites[0].x=48;publish_sprites();s3_video_vblank();
    CHECK(s_upload_serial>uploaded);scanline(&v,0,native,320,out,796);
    CHECK(out[actor_x]!=expected[actor_x]);
    presentation_reset();CHECK(!s_upload_serial);s_background_frame[65535]=0;s_placement_count=0;
    size_t current_size=presentation_state_save(NULL,0);
    uint8_t *before=(uint8_t*)malloc(current_size),*after=(uint8_t*)malloc(current_size);CHECK(before&&after);
    CHECK(presentation_state_save(before,current_size)==current_size);
    CHECK(!presentation_state_load(blob,bytes-1,1));
    unsigned saved_count=0,invalid=SCENE_SPRITES+1;
    memcpy(&saved_count,blob+sizeof(unsigned)+sizeof(int)+sizeof(double),sizeof saved_count);
    memcpy(blob+sizeof(unsigned)+sizeof(int)+sizeof(double),&invalid,sizeof invalid);
    CHECK(!presentation_state_load(blob,bytes,1));
    memcpy(blob+sizeof(unsigned)+sizeof(int)+sizeof(double),&saved_count,sizeof saved_count);
    CHECK(presentation_state_load(blob,bytes,0));
    CHECK(presentation_state_save(after,current_size)==current_size && !memcmp(before,after,current_size));
    static uint8_t ram_before[65536];memcpy(ram_before,g_ram,sizeof ram_before);GVDP restored=v;
    CHECK(presentation_state_load(blob,bytes,1));
    CHECK(s_build.aiz_super_pose==2 && s_upload_serial==uploaded);
    CHECK(s_background_frame[65535]==0x5A && s_world_frame[65535]==0xA5);
    CHECK(s_placement_count==2 && s_placement_base==0x1234 && s_placements[1].loaded);
    /* First restored render, with no native tick/publication in between. */
    scanline(&v,0,native,320,out,796);CHECK(!memcmp(expected,out,sizeof out));
    CHECK(!memcmp(ram_before,g_ram,sizeof ram_before) && !memcmp(&restored,&v,sizeof v));
    free(blob);free(before);free(after);CHECK(configure("off"));

    /* Exercise the production rewind reset/load sequence before any new
     * native IRQ. The cached SS publication can legitimately precede WRAM. */
    CHECK(configure("32:9"));width(796,224,320,224);
    memset(g_ram,0,sizeof g_ram);memset(&v,0,sizeof v);presentation_reset();
    g_ram[0xF600]=0x34;word(g_ram,0xE422,0x200);word(g_ram,0xE424,0x200);
    write32(SS_EXTRA+16,0x1000);word(g_ram,SS_EXTRA+20,0x4002);
    write32(SS_EXTRA+10*8,0x1000);word(g_ram,SS_EXTRA+10*8+4,0x4002);
    for(unsigned i=0;i<16;++i)word(g_rom,0x1000+i*2,32);
    word(g_rom,0x1020,1);g_rom[0x1022]=240;g_rom[0x1023]=15;
    word(g_rom,0x1024,0);word(g_rom,0x1026,65520);
    g_ram[SS_BOARD+2*32+2]=10;memset(v.vram+64,0x11,512);
    v.reg[1]=64;v.reg[2]=0x30;v.reg[4]=7;v.reg[5]=0x7C;v.reg[12]=9;
    v.cram[56]=0xE0;v.cram[60]=0xE00;v.cram[33]=0xE;
    ss_capture();scanline(&v,0,native,320,out,796);
    scanline(&v,180,native,320,expected,796);CHECK(ss_ready && ss_x==2);
    word(g_ram,0xE422,0x900);g_ram[SS_BOARD+2*32+2]=1;
    bytes=presentation_state_save(NULL,0);blob=(uint8_t*)malloc(bytes);CHECK(blob);
    CHECK(presentation_state_save(blob,bytes)==bytes);
    ss_capture();CHECK(ss_state[SS_BOARD+2*32+2]==1);
    presentation_reset();CHECK(!ss_latched);
    memcpy(ram_before,g_ram,sizeof ram_before);restored=v;
    CHECK(presentation_state_load(blob,bytes,1));
    CHECK(ss_latched && ss_state[SS_BOARD+2*32+2]==10);
    scanline(&v,0,native,320,out,796);CHECK(ss_ready && ss_x==2);
    scanline(&v,180,native,320,out,796);CHECK(!memcmp(expected,out,sizeof out));
    CHECK(!memcmp(ram_before,g_ram,sizeof ram_before) && !memcmp(&restored,&v,sizeof v));
    free(blob);CHECK(configure("off"));
}
