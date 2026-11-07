/* Local rewind presentation only: never a guest/RB digest or disk state.
 * Keep only live scene pieces, and no GVDP callbacks/provider pointers. */
static void rewind_scene(S3StateIO *io,SceneFrame *f)
{
    unsigned count=s3_state_peek_unsigned(io,f->count);
    if(count>SCENE_SPRITES){io->ok=0;return;}
    size_t tail=offsetof(SceneFrame,count);
    if(io->mode) {
        int scene=0;unsigned pose=0,join=0,world=0;
        int cx=0,cy=0;unsigned page=0;
        s3_state_peek(io,offsetof(SceneFrame,competition_x)-tail,&cx,sizeof cx);
        s3_state_peek(io,offsetof(SceneFrame,competition_y)-tail,&cy,sizeof cy);
        s3_state_peek(io,offsetof(SceneFrame,competition_page)-tail,&page,sizeof page);
        if(cx<0 || cx>65535 || cy<0 || cy>65535 || page>1){io->ok=0;return;}

        s3_state_peek(io,offsetof(SceneFrame,scene)-tail,&scene,sizeof scene);
        s3_state_peek(io,offsetof(SceneFrame,aiz_super_pose)-tail,&pose,sizeof pose);
        s3_state_peek(io,offsetof(SceneFrame,aiz_join)-tail,&join,sizeof join);
        s3_state_peek(io,offsetof(SceneFrame,world_tag)-tail,&world,sizeof world);
        AizPlanePose plane={0};
        s3_state_peek(io,offsetof(SceneFrame,aiz_plane)-tail,&plane,sizeof plane);
        if(scene<0 || scene>4 || pose>2 || join>3 || world>0x10000u || !aiz_plane_valid(&plane)){io->ok=0;return;}
    }
    s3_state_bytes(io,&f->count,sizeof *f-tail);
    s3_state_bytes(io,f->sprites,(size_t)count*sizeof f->sprites[0]);
}
static void rewind_presentation_state(S3StateIO *io)
{
    unsigned version=4;
    if(s3_state_peek_unsigned(io,version)!=version)io->ok=0;
    S3_STATE(io,version);
    int mode=s_mode;double ratio=s_ratio;
    if(io->mode) {
        int saved=0;double aspect=0;
        s3_state_peek(io,0,&saved,sizeof saved);
        s3_state_peek(io,sizeof saved,&aspect,sizeof aspect);
        if(saved!=mode || aspect!=ratio)io->ok=0;
    }
    S3_STATE(io,mode);S3_STATE(io,ratio);
    rewind_scene(io,&s_build);
    for(unsigned n=0;n<3;++n)rewind_scene(io,&s_history[n]);
    rewind_scene(io,&s_display_frame);
    int display=s_display!=NULL;
    if(s3_state_peek_unsigned(io,(unsigned)display)>1)io->ok=0;
    S3_STATE(io,display);
    /* Only prior register geometry is consulted before row zero replaces the
     * VDP copy with restored hardware. VRAM/CRAM/VSRAM remain guest authority. */
    S3_STATE(io,s_real_context.video.reg);
    S3_STATE(io,s_real_context.world);S3_STATE(io,s_real_context.background);
    s3_state_bytes(io,&s_real_context.width,
        sizeof s_real_context - offsetof(RenderContext,width));
    unsigned serial=s_serial,upload=s_upload_serial;
    s3_state_peek(io,0,&serial,sizeof serial);
    s3_state_peek(io,sizeof serial,&upload,sizeof upload);
    if(upload>serial)io->ok=0;
    S3_STATE(io,s_serial);S3_STATE(io,s_upload_serial);S3_STATE(io,s_scene_tick);
    S3_STATE(io,s_tick_was_active);S3_STATE(io,s_tick_last);
    S3_STATE(io,s_tick_zone);S3_STATE(io,s_tick_serial);
    S3_STATE(io,s_tick_samples);S3_STATE(io,s_tick_updates);
    S3_STATE(io,s_tick_lag);S3_STATE(io,s_tick_multi);S3_STATE(io,s_publication_lag);
    unsigned placements=s3_state_peek_unsigned(io,s_placement_count);
    if(placements>SCENE_PLACEMENTS){io->ok=0;return;}
    S3_STATE(io,s_placement_count);
    s3_state_bytes(io,s_placements,(size_t)placements*sizeof s_placements[0]);
    S3_STATE(io,s_placement_base);S3_STATE(io,s_loader_active);
    unsigned visible=s3_state_peek_unsigned(io,s_visible_count);
    if(visible>144){io->ok=0;return;}
    S3_STATE(io,s_visible_count);
    s3_state_bytes(io,s_visible_objects,(size_t)visible*sizeof s_visible_objects[0]);
    S3_STATE(io,s_bg_draw_at_vblank);S3_STATE(io,s_bg_draw_ready);S3_STATE(io,s_bg_draw_zone);
    if(io->mode==2 && io->ok) {
        interpolation_reset();s_render=&s_real_context;
        s_interpolating=0;s_ix_overlay_seen=0;s_native_publication_restored=0;
        s_capture_frame=&s_build;s_capture_readonly=0;s_capture_slot=0;
        s_display=display?&s_display_frame:NULL;
    }
}
static size_t presentation_state_save(void *dst,size_t capacity)
{
    S3StateIO io={dst,capacity,0,0,1};rewind_presentation_state(&io);
    return io.ok?io.pos:0;
}
static int presentation_state_load(const void *src,size_t size,int apply)
{
    if(!src)return 0;
    S3StateIO io={(uint8_t*)src,size,0,1,1};rewind_presentation_state(&io);
    if(!io.ok || io.pos!=size)return 0;
    if(apply){io.pos=0;io.mode=2;rewind_presentation_state(&io);}
    return io.ok && io.pos==size;
}
