/* Compiled twice so the ordinary sprite loop retains its original shape.
 * Only the MHZ2 specialization includes the finite ship/rotor presentation.
 * Both variants traverse the same published sprite order and priorities. */
static void S3_DRAW_SCENE_FUNCTION(const GVDP *v,int line,uint32_t *out,int width,
                               int origin,const uint32_t *palette)
{
    if(!s_display || s_display->scene==4)return;
    int cinematic=ending_cinematic();
#if S3_DRAW_MHZ_ROTORS
#ifndef SONIC3_STANDALONE
    int mhz_rotors=s_frame_level && s_frame_zone==7 && s_frame_act==1 &&
        (((unsigned)s_world_frame[0xEEC0]<<8)|s_world_frame[0xEEC1])==16;
#endif
#endif
    unsigned card_slot=UINT_MAX;int card_offset=0;
    for(unsigned n=0;n<s_display->count;++n) {
        const SceneSprite *s=&s_display->sprites[n];
        unsigned slot=sprite_slot(s);
        int menu=s_display->scene==0;
        /* Results queue signed screen positions before the native SAT can
         * see them. Extend those original-size pieces through the margins;
         * the installed native row still owns the original viewport. Use
         * the published mode, including immediately after rewind. */
        int results=menu && (s_world_frame[0xF600]&127)==0x48;
        int menu_hud=menu?(results?2:file_select_layout(v)?4:3):0;
        if(menu && (s_frame_menu==MENU_TITLE_CARD ? s->hud<6 || s->hud>9 :
                    s->hud!=menu_hud && !(menu_hud==3 && s->hud==SCENE_COMPETITION_CHARACTER)))continue;
        int cw=((s->size>>2)&3)+1,ch=(s->size&3)+1;
        unsigned zoom=s->zoom?s->zoom:256;
        int drawn_width=cw*8*(int)zoom/256,drawn_height=ch*8*(int)zoom/256;
        unsigned pattern_bytes;
        const uint8_t *patterns=presentation_sprite_patterns(s->size,&pattern_bytes);
        int world=!s->hud && s_display->scene==1;
#if S3_DRAW_MHZ_ROTORS
        int native_rotor=0;
#ifndef SONIC3_STANDALONE
        if(mhz_rotors && s->hud==2 && slot>0 && slot<SCENE_SLOTS && s_display->code[slot]==0x5582E) {
            unsigned o=0xB000u+(slot-1u)*0x4Au;
            native_rotor=o==(((unsigned)s_world_frame[0xA80C]<<8)|s_world_frame[0xA80D]) ||
                         o==(((unsigned)s_world_frame[0xA80E]<<8)|s_world_frame[0xA80F]);
        }
#endif
#endif
        int dx=0,dy=0;
        if(world && s_interpolating)presentation_object_delta(s,&dx,&dy);
        if(s->hud==5 && s_interpolating)presentation_aiz_plane_delta(&dx,&dy);
        int y=s->y+(world?s_display->camera_y-s_camera_y:0)+dy;
#if S3_DRAW_MHZ_ROTORS
        int rotor_x=0;
        if(s->hud==SCENE_MHZ_ROTOR) {
            unsigned hsbase=(v->reg[13]&63u)<<10;
            int hs=(int16_t)vram16(v,hsbase),bg=0;
            presentation_scroll(0,&hs,&bg);
            int reference=((unsigned)s_world_frame[0xA800]<<8)|s_world_frame[0xA801];
            rotor_x=unwrap(-hs,reference);
            reference=((unsigned)s_world_frame[0xEE9C]<<8)|s_world_frame[0xEE9D];
            y-=unwrap((int)presentation_fg_scroll(0,v->vsram[0]),reference);
        }
#endif
        if(line<y || line>=y+drawn_height)continue;
#if S3_DRAW_MHZ_ROTORS
        int base=s->x+(s->hud==1?0:s->hud>=2?(width-320)/2:origin)+dx-rotor_x;
#else
        int base=s->x+(s->hud==1?0:s->hud>=2?(width-320)/2:origin)+dx;
#endif
        if(s->hud>=6 && s->hud<=8 && slot<SCENE_SLOTS) {
            if(card_slot!=slot) {
                card_slot=slot;
                int target=s->hud==6?160:s->hud==7?252:260;
                int distance=s_display->ox[slot]-128-target,left=INT_MAX;
                for(unsigned k=0;k<s_display->count;++k)
                    if(sprite_slot(&s_display->sprites[k])==slot && s_display->sprites[k].hud==s->hud &&
                       s_display->sprites[k].x<left)left=s_display->sprites[k].x;
                int span=320-(left-distance);
                /* Move the finite card from/to the requested right edge,
                 * without scaling a glyph or altering its native lifetime.
                 * Its settled layout stays centered and unchanged. */
                card_offset=span>0?distance*((width-320+1)/2)/span:0;
            }
            base+=card_offset;
        }
        /* Native camera scroll may advance while a completed host frame is
         * retained. Reproject its world coordinates, not its old screen
         * coordinates; otherwise stationary rings jitter with the camera. */
        if(world)base+=s_display->camera_x-s_camera;
        int iy=(line-y)*256/(int)zoom;
        if(s->size&0x20u)iy=(iy+dy)&31;
        if(s->attr&0x1000)iy=ch*8-1-iy;
#if S3_DRAW_MHZ_ROTORS
        /* A mapping piece is at most64px at the supported zoom. Resolve
         * the rotor's native/margin ownership once for that piece, leaving
         * the ordinary sprite pixel path free of optional rotor work. */
        int first=0,last=drawn_width,center=(width-320)/2;
        if((s->slot>>8)==SPRITE_MHZ_ARENA_COPY) {
            if(base<origin) {if(last>origin-base)last=origin-base;}
            else if(first<origin+320-base)first=origin+320-base;
        } else if(s->hud==SCENE_MHZ_ROTOR) {
            if(base<center) {if(last>center-base)last=center-base;}
            else if(first<center+320-base)first=center+320-base;
        } else if(native_rotor) {
            if(first<center-base)first=center-base;
            if(last>center+320-base)last=center+320-base;
        }
        for(int i=first;i<last;++i) {
#else
        for(int i=0;i<drawn_width;++i) {
#endif
            int x=base+i;
            if(x<0 || x>=width || (s_priority[x]&2))continue;
            if(cinematic && world && (x<origin || x>=origin+320) &&
               (s->slot>>8)!=SPRITE_ENDING_POSE)continue;
            if((s->slot>>8)==SPRITE_MHZ_DEPARTURE && x>=origin && x<origin+320)continue;
            /* Bit 6 marks the HCZ current's extra, margin-only brick fill. */
            if((s->size&0xC0)==0x40 && ((x>=origin && x<origin+320) ||
                                 x-origin+s_camera>=0x980))continue;
            if(menu && s_frame_menu!=MENU_TITLE_CARD && x>=origin && x<origin+320)continue;
            if(s->hud==5 && x>=(width-320)/2 && x<(width+320)/2)continue;
            if((s->size&0xD0u)==0x50u && (s_priority[x]&4u))continue;
            int ix=i*256/(int)zoom;
            if(s->size&0x20u)ix=(ix+dx)&31;
            if(s->attr&0x800)ix=cw*8-1-ix;
            uint16_t cell=(uint16_t)((s->attr&0xE000u)|
                (((s->attr&2047u)+(unsigned)(ix/8*ch+iy/8))&2047u));
            uint8_t p;
            if(s->hud==5) {
                p=aiz_plane_pixel(cell&2047u,ix&7,iy&7);
                if(!p)continue;p=(uint8_t)(p|((cell>>9)&48));
            } else if(patterns) {
                unsigned a=(cell&2047u)*32u+(unsigned)(iy&7)*4u+(unsigned)(ix&7)/2u;
                unsigned byte=a<pattern_bytes?patterns[a]:0;
                p=(uint8_t)(((ix&1)?byte&15:byte>>4)|((cell>>9)&48));
                if(!(p&15))continue;
            } else if(world && (s->slot>>8)==SPRITE_MHZ_MUSHROOM &&
                      (x<origin || x>=origin+320) && mhz_mushroom_pixel(cell,ix&7,iy&7,&p)) {
                if(!p)continue;
            } else if(s_frame_level && !s->hud && (x<origin || x>=origin+320) &&
                      giant_ring_art_pixel(s->slot>>8,cell,ix&7,iy&7,&p)) {
                if(!p)continue;
            } else if(world && (s->slot>>8)==SPRITE_LBZ_PILLAR &&
                      (x<origin || x>=origin+320) && lbz_rocket_load()) {
                p=pattern_pixel(&s_lbz_rocket.video,cell,ix,iy);
            } else if(s_frame_level && s_frame_zone==1 && !s->hud &&
                      (s->slot>>8)==SPRITE_TURBO_SPIKER &&
                      (x<origin || x>=origin+320) && turbo_art_pixel(cell,ix&7,iy&7,&p)) {
                if(!p)continue;
            } else if(s_frame_level && s_frame_zone==4 && width>320 && !s->hud &&
                      (s->slot>>8)>=SPRITE_FBZ_BUTTON && (s->slot>>8)<=SPRITE_FBZ_BLASTER &&
                      fbz_art_pixel(s->slot>>8,cell,ix&7,iy&7,&p)) {
                if(!p)continue;
            } else if(s_frame_level && s_frame_zone==4 && s_frame_act==1 && width>320 &&
                      (s->slot>>8)==SPRITE_FBZ_PREBOSS &&
                      (x<(s->hud==2?(width-320)/2:origin) ||
                       x>=(s->hud==2?(width-320)/2:origin)+320) &&
                      fbz_art_pixel(SPRITE_FBZ_PREBOSS,cell,ix&7,iy&7,&p)) {
                /* The future clouds/pillar can enter the extra view before
                 * the subboss PLC owns their native tile reservation. */
                if(!p)continue;
            } else if(s_frame_level && !s->hud && (s->slot>>8) &&
                      ((s->slot>>8)!=SPRITE_HCZ_FAN_BASE || x<origin || x>=origin+320) &&
                      native_art_pixel(s->slot>>8,cell,ix&7,iy&7,&p)) {
                if(!p)continue;
            } else if(s_frame_level && !s_frame_zone && !s->hud &&
                      (s->size&0xD0u)==SPRITE_MONKEY_ART &&
                      (cell&2047u)>=0x548 && (cell&2047u)<0x55F &&
                      (x<origin || x>=origin+320) && load_aiz_monkey_art()) {
                p=aiz_monkey_pixel(cell,ix&7,iy&7);
            } else if(slot>0 && slot<SCENE_SLOTS &&
                      s_display->code[slot]==S3_AIZ_WAVE &&
                      (x<(width-320)/2 || x>=(width+320)/2) && load_aiz_wave_art()) {
                p=aiz_wave_pixel(cell&2047u,ix&7,iy&7);
                if(!p)continue;p=(uint8_t)(p|((cell>>9)&48));
            } else if(s_display->aiz_super_pose && slot>0 && slot<SCENE_SLOTS &&
                      s_display->code[slot]==S3_AIZ_PLANE_INTRO &&
                      (x<(width-320)/2 || x>=(width+320)/2)) {
                p=aiz_super_pixel(s_display->aiz_super_pose,cell&2047u,ix&7,iy&7);
                if(!p)continue;
                p=(uint8_t)(p|((cell>>9)&48));
            } else p=pattern_pixel(v,cell,ix,iy);
            if(!p)continue;
            s_priority[x]|=2;
            /* extended sprite lines (game-added art keeping its own colours) */
            int ext=s->hud==5?0:presentation_sprite_line(s->size,cell&2047u,p>>4);
            uint32_t c=ext?genesis_dac_cram_to_argb(presentation_ext_color(line,ext,p&15),GENESIS_DAC_NORMAL):
                s->hud==SCENE_COMPETITION_CHARACTER?competition_character_color(v,p,s->attr):palette[p];
            /* The MHZ2 switch scene borrows ring brown for Knuckles' red.
             * Its original view cannot see the rings beyond the wall.
             * Those extra rings keep their own brown through stage fades;
             * the native viewport and Knuckles retain the installed palette. */
            if(!ext && (s->slot>>8)==SPRITE_WORLD_RING && s_frame_level &&
               s_frame_zone==7 && s_frame_act==1 && width>320 && p==22 &&
               (x<origin || x>=origin+320))
                c=genesis_dac_cram_to_argb(scene_art_color(0x088,v),GENESIS_DAC_NORMAL);
            /* The current's full/half brick pieces borrow palette 2 while
             * the identical backing uses palette 3. A wider view exposes
             * that recoloring sweep over a large stationary wall. Keep the
             * wall's colors on those pieces; the water-front art and the
             * original-width composition retain their native palettes.
             * Bit 4 marks the half pieces in HCZ, independently of the
             * world-phase bit used by full/repeated 32px pieces. */
            if(!ext && world && width>320 && s_frame_zone==1 && !s_frame_act &&
               (s->size&0x30u) && (s->attr&0x6000u)==0x4000u)
                c=palette[48u|(p&15u)];
            /* The wider view can expose the waiting cutscene actor before
             * its controller borrows palette 1. Preserve its authored colors
             * there while the hardware viewport and private providers own
             * their existing composition. */
            if(!ext && s_frame_level && !s->hud && (s->slot>>8)==6 &&
               (x<origin || x>=origin+320) && p>=16 && p<32)
                c=genesis_dac_cram_to_argb(scene_art_color(scene_read16(
                    S3_KNUX_PREVIEW_PALETTE+(p-16)*2),v),GENESIS_DAC_NORMAL);
            /* Screen-relative coordinates are not HUD priority: AIZ2's
             * small Eggman must still pass behind the high tree canopy. */
            if(s->hud==1 || (s->attr&0x8000) || !(s_priority[x]&1))out[x]=c;
        }
    }
}
