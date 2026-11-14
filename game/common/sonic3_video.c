/* Opt-in Sonic 3 family world renderer (combined S3&K first).
 * Stock S&K ROM: Render_Sprites $1AD20, Load_Sprites $1B690.
 * Native collision, decompression and tile streaming remain untouched.
 * Host scene publication follows the actual native SAT upload.
 */
#include "sonic3_video.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include "video/genesis_dac.h"
#include "video/genesis_tile_row.h"
#include "cmd_server.h"
#include "sonic3_state.h"
#include "sonic3_state_io.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sonic3_knuckles_preview.inc"

/* The standalone S3 cartridge has the same object/layout formats, with a
 * different engine address map. S&K and the combined cartridge share code. */
#ifdef SONIC3_STANDALONE
enum { S3_COMPETITION=0xFFD8, S3_DEMO=0xFFF0, S3_PLACEMENTS=0x5E0D8, S3_RINGS=0x5E198,
       S3_HUD_MAP=0xE932, S3_RING_MAP=0xF87E,
       S3_LOAD_INIT=0x19CF2, S3_LOAD_UPDATE=0x19DD0,
       S3_CAPTURE=0x1938E, S3_PUBLISH=0x194B8, S3_COMP_PUBLISH=0x19A4C,
       S3_TITLE_PLANE_MAP=0x4620, S3_FILE_SELECT_MAP=0xBD68,
       S3_TITLE_CARD_MAP=0x2D90C, S3_HCZ_SURFACE_MAP=0x1CF2E, S3_HCZ_RUSH_MAP=0x2E60C,
       S3_MENU_BG_MAP=0x19D2A2,
       S3_TITLE_PLANE_SIZE=0x11,
       S3_AIZ_MINIBOSS_CUTSCENE=0x46442, S3_EGG_CAPSULE=0x544E0,
       S3_HCZ_LARGE_FAN=0x2F2D8, S3_HCZ_WATER_RUSH=0x2E4BA,
       S3_RING_RANGE=0xF6D8, S3_RING_STATUS=0xF6E2,
       S3_ART_BANK=0, S3_PAL_INTRO=0x8C314, S3_PAL_FOREST=0x8C374,
       S3_MOVING_PLATFORM_CULL=0x23E1E, S3_FLOATING_PLATFORM_CULL=0x2452A,
       S3_SWING_PLATFORM_CULL=0x24646,
       S3_HUD_LIFE_VALUE=0xEE72, S3_LIFE_DIGITS=0xF1D2, S3_TIME_DIGITS=0xEED2,
       S3_AIZ_WAVE=0x45CB6, S3_AIZ_PLANE_DEPART=0x45BDC, S3_AIZ_PLANE_PROP=0x45C26,
       S3_AIZ_PLANE_EXHAUST=0x45C64, S3_AIZ_PLANE_MAP=0x164470,
       S3_AIZ_PLANE_PROP_ANIM=0x45E6B, S3_AIZ_PLANE_EXHAUST_ANIM=0x45E73,
       S3_AIZ_PLANE_INTRO=0x4584E, S3_SUPER_MAP=0x141194, S3_SUPER_PLC=0x142A60,
       S3_MGZ_END_BOSS=0x4A0F8, S3_WAIT_ONSCREEN=0x53FFC };
#else
enum { S3_COMPETITION=0xFFE8, S3_DEMO=0xFFD0, S3_PLACEMENTS=0x1E3D98, S3_RINGS=0x1E3E58,
       S3_HUD_MAP=0xDBB6, S3_RING_MAP=0xEBEE,
       S3_LOAD_INIT=0x1B690, S3_LOAD_UPDATE=0x1B7F2,
       S3_CAPTURE=0x1AD20, S3_PUBLISH=0x1AE56, S3_COMP_PUBLISH=0x1B3EA,
       S3_TITLE_PLANE_MAP=0x4FE8, S3_FILE_SELECT_MAP=0xCE0E,
       S3_TITLE_CARD_MAP=0x2EE10, S3_HCZ_SURFACE_MAP=0x1F2CE, S3_HCZ_RUSH_MAP=0x22E60C,
       S3_MENU_BG_MAP=0x39D2A2,
       S3_TITLE_PLANE_SIZE=1,
       S3_AIZ_MINIBOSS_CUTSCENE=0x684EC, S3_EGG_CAPSULE=0x86540,
       S3_HCZ_LARGE_FAN=0x308FA, S3_HCZ_WATER_RUSH=0x2FDA4,
       S3_RING_RANGE=0xEA32, S3_RING_STATUS=0xEA3C,
       S3_ART_BANK=0x200000, S3_PAL_INTRO=0xA8B1C, S3_PAL_FOREST=0xA8B7C,
       S3_MOVING_PLATFORM_CULL=0x24F28, S3_FLOATING_PLATFORM_CULL=0x25634,
       S3_SWING_PLATFORM_CULL=0x25750,
       S3_HUD_LIFE_VALUE=0xE12A, S3_LIFE_DIGITS=0xE48A, S3_TIME_DIGITS=0xE18A,
       S3_AIZ_WAVE=0x678DA, S3_AIZ_PLANE_DEPART=0x67800, S3_AIZ_PLANE_PROP=0x6784A,
       S3_AIZ_PLANE_EXHAUST=0x67888, S3_AIZ_PLANE_MAP=0x364470,
       S3_AIZ_PLANE_PROP_ANIM=0x67A8F, S3_AIZ_PLANE_EXHAUST_ANIM=0x67A97,
       S3_AIZ_PLANE_INTRO=0x67472, S3_SUPER_MAP=0x146816, S3_SUPER_PLC=0x148378,
       S3_MGZ_END_BOSS=0x6C31A, S3_WAIT_ONSCREEN=0x85AD2 };
#endif

enum { S3_MONKEY_MAP = S3_ART_BANK+0x161776u, SPRITE_MONKEY_ART = 0x10,
       SPRITE_ENDING_POSE = 8 };
#ifdef SONIC3_STANDALONE
enum { S3_EXPLOSION_MAP=0x1C38A, S3_EXPLOSION_ART=0x15E6C0,
       S3_CAPSULE_MAP=0x163F22, S3_CAPSULE_ART=0x182180,
       S3_SPRING_MAP=0x21DEA, S3_SPRING_ART=0x15EFFC };
#else
enum { S3_EXPLOSION_MAP=0x1E758, S3_EXPLOSION_ART=0x19200A,
       S3_CAPSULE_MAP=0x86BFC, S3_CAPSULE_ART=0xDD990,
       S3_SPRING_MAP=0x2375C, S3_SPRING_ART=0x1927FE };
#endif
static unsigned native_art_owner(unsigned map)
{
#ifdef SONIC3_STANDALONE
    if(map==0x5217Cu)return SPRITE_ENDING_POSE;
#endif
    if(map==S3_EXPLOSION_MAP)return 1;
    if(map==S3_ART_BANK+0x161CB8u)return 2;
    if(map==S3_ART_BANK+0x30502u)return 3;
    if(map==S3_SPRING_MAP || map==S3_SPRING_MAP+22u)return 4;
    if(map==S3_CAPSULE_MAP || map==S3_ART_BANK+0x163F22u)return 5;
    if(map==S3_ART_BANK+0x164016u)return 6;
    return 0;
}
enum { VIDEO_OFF, VIDEO_FIT, VIDEO_RATIO, VIDEO_STAGE };
static int s_mode;
static void ss_capture(void);
static S3VideoObjectZoom s_object_zoom;
static S3VideoBackground s_background_override;
void s3_video_set_background_override(S3VideoBackground read){s_background_override=read;}
static S3VideoPaletteSource s_palette_source;
void s3_video_set_palette_source(S3VideoPaletteSource read){s_palette_source=read;}
void s3_video_set_object_zoom(S3VideoObjectZoom fn) { s_object_zoom = fn; }
static double s_ratio = 16.0 / 9.0;
static int s_requested_width = 320;
static unsigned s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors;
static unsigned s_bg_checks, s_bg_errors, s_bg_unstreamed;
static unsigned s_scene_misses, s_scene_holds;
static unsigned s_uninitialized_scroll_lines;
static unsigned s_terrain_unstreamed;
static int s_bad_x,s_bad_y;
static unsigned s_bad_attr,s_bad_expected;
static int s_bad_world_y,s_bad_hscroll;
static int s_bg_bad_x,s_bg_bad_y,s_bg_bad_a,s_bg_bad_expected;
/* Separate drawing context: fractional presentation borrows an owned copy and
 * cannot modify the canonical scene/loader history or its saved background. */
typedef struct {
    GVDP video;
    uint8_t world[0x10000],background[0x10000];
    int width,left,camera,stage_width,native_x,camera_y,knux_x,knux_y;
    int level,special,bonus,fg_x,fg_y,bg_y,menu;
    unsigned zone,act,bg_event,tree_reveal;
    int bg_band[2];
} RenderContext;
static RenderContext s_real_context;
static RenderContext *s_render=&s_real_context;
#define s_width (s_render->width)
#define s_left (s_render->left)
#define s_camera (s_render->camera)
#define s_stage_width (s_render->stage_width)
#define s_native_x (s_render->native_x)
#define s_camera_y (s_render->camera_y)
#define s_knux_plane_camera_x (s_render->knux_x)
#define s_knux_plane_camera_y (s_render->knux_y)
#define s_video_frame (s_render->video)
#define s_world_frame (s_render->world)
#define s_background_frame (s_render->background)
#define s_frame_level (s_render->level)
#define s_frame_special (s_render->special)
#define s_frame_bonus (s_render->bonus)
#define s_frame_fg_x (s_render->fg_x)
#define s_frame_fg_y (s_render->fg_y)
#define s_frame_bg_y (s_render->bg_y)
#define s_frame_zone (s_render->zone)
#define s_frame_act (s_render->act)
#define s_frame_bg_event (s_render->bg_event)
#define s_frame_tree_reveal (s_render->tree_reveal)
#define s_frame_bg_band (s_render->bg_band)
#define s_frame_menu (s_render->menu)
static int s_interpolation_enabled,s_interpolating,s_ix_overlay_seen;
static double s_interpolation_phase;
static void interpolation_reset(void);
static int interpolation_lerp(int a,int b,int limit)
{
    int d=(int16_t)(uint16_t)(b-a);
    if(d>limit || d<-limit)return b;
    return b-d+(int)(d*s_interpolation_phase+(d<0?-.5:.5));
}
static unsigned presentation_fg_scroll(int row,unsigned fallback);
static unsigned presentation_fg_identity(int row,unsigned fallback);
static unsigned presentation_cram(int row,unsigned color,unsigned fallback);
static void presentation_scroll(int row,int *a,int *b);
static int presentation_camera(int axis,int fallback);
static int installed_actor_camera(const GVDP *v,int level,int zone,int act,int event);
static int presentation_bg_scroll(int fallback);
static unsigned presentation_column_scroll(unsigned column,unsigned fallback);
static void presentation_overlay_begin(int width);
static void presentation_overlay_end(int width);
static int presentation_plane_line(unsigned tile);
static uint16_t presentation_ext_color(int row,int line,int nib);
static uint16_t scene_art_color(unsigned color,const GVDP *v);
static const uint8_t *presentation_sprite_patterns(unsigned flags,unsigned *bytes);
static int presentation_sprite_line(unsigned flags,unsigned tile,unsigned source);
static int s_bg_draw_at_vblank[2],s_bg_draw_ready;
static unsigned s_bg_draw_zone;
static void scene_mode_changed(int was_enabled);

/* AIZ reuses its intro's VRAM and block definitions for the forest, and
 * temporarily installs Knuckles' palette during the emerald cutscene.
 * An expanded view sees both banks simultaneously. Decode their original
 * ROM assets into host-only storage; never preload them into guest memory. */
typedef struct { uint8_t blocks[0x1800]; GVDP video; uint16_t colors[48]; } AizArt;
static AizArt s_aiz_intro,s_aiz_main;
static int s_aiz_art_ready,s_aiz_art_attempted;
#include "sonic3_art.h"
static size_t art_kosm(unsigned address,size_t compressed,uint8_t *out,size_t capacity)
{
    if(compressed<2 || address>0x400000u-compressed)return 0;
    const uint8_t *src=g_rom+address;
    size_t total=(size_t)((src[0]<<8)|src[1]),pos=2,n=0;
    if(total==0xA000)total=0x8000;
    if(total>capacity)return 0;
    while(n<total && pos<compressed) {
        size_t used=0,expected=total-n>4096?4096:total-n;
        size_t got=art_kos(src+pos,compressed-pos,out+n,expected,&used);
        if(got!=expected)return 0;
        n+=got;pos+=used;pos=2+((pos-2+15)&~(size_t)15);
    }
    return n==total?n:0;
}
static void load_aiz_art(void)
{
    if(s_aiz_art_attempted)return;
    s_aiz_art_attempted=1;
    size_t used=0;
    if(art_kos(g_rom+S3_ART_BANK+0x1A374A,0x200,s_aiz_intro.blocks,0x268,&used)!=0x268 ||
       art_kos(g_rom+S3_ART_BANK+0x1A394A,0xC40,s_aiz_intro.blocks+0x268,0x1058,&used)!=0x1058 ||
       art_kosm(S3_ART_BANK+0x1A566A,0xE12,s_aiz_intro.video.vram,0x17C0)!=0x17C0 ||
       art_kosm(S3_ART_BANK+0x1A647C,0x2FD2,s_aiz_intro.video.vram+0x17C0,0x6260)!=0x6260)return;
    memcpy(s_aiz_main.blocks,s_aiz_intro.blocks,0x268);
    memcpy(s_aiz_main.video.vram,s_aiz_intro.video.vram,0x17C0);
    if(art_kos(g_rom+S3_ART_BANK+0x1A458A,0x10E0,s_aiz_main.blocks+0x268,0x14F0,&used)!=0x14F0 ||
       art_kosm(S3_ART_BANK+0x1A944E,0x27F2,s_aiz_main.video.vram+0x17C0,0x4920)!=0x4920)return;
    for(unsigned i=0;i<48;++i) {
        s_aiz_intro.colors[i]=(uint16_t)((g_rom[S3_PAL_INTRO+i*2]<<8)|g_rom[S3_PAL_INTRO+i*2+1]);
        s_aiz_main.colors[i]=(uint16_t)((g_rom[S3_PAL_FOREST+i*2]<<8)|g_rom[S3_PAL_FOREST+i*2+1]);
    }
    s_aiz_art_ready=1;
}

static uint16_t ram16(unsigned a)
{
    a &= 0xFFFFu;
    return (uint16_t)((g_ram[a] << 8) | g_ram[(a + 1) & 0xFFFFu]);
}
static uint16_t vram16(const GVDP *v, unsigned a)
{
    return (uint16_t)((v->vram[a & 0xFFFFu] << 8) | v->vram[(a + 1) & 0xFFFFu]);
}
/* AIZ1's water can deform A below its surface. Only use the installed
 * foreground as the rigid actor camera when that displayed A is uniform. */
static int installed_actor_camera(const GVDP *v,int level,int zone,int act,int event)
{
    if(!level)return 0;
    if(zone==0x20 || zone==0x24)return 1;
    if(zone || act || (event!=4 && event!=8))return 0;
    unsigned mode=v->reg[11]&3u,base=(v->reg[13]&63u)<<10;
    uint16_t first=vram16(v,base);
    for(unsigned y=1;y<224;++y) {
        unsigned row=mode==0?0:mode==2?y&~7u:y;
        if(vram16(v,base+row*4)!=first)return 0;
    }
    return 1;
}

static int bonus_stage(void)
{
    int mode=g_ram[0xF600]&127,zone=g_ram[0xFE10];
#ifdef SONIC3_STANDALONE
    return (mode==8 || mode==12) && zone==0x13;
#else
    return (mode==8 || mode==12) && zone>=0x13 && zone<=0x15;
#endif
}
static int gameplay(void)
{
    int mode=g_ram[0xF600]&127;
    return (mode==8 || mode==12) && !bonus_stage() &&
        (ram16(0xB000)||ram16(0xB002)) && !ram16(S3_COMPETITION);
}
static int native_demo(void) { return ram16(S3_DEMO)!=0; }
static int demo_level_exit(void)
{
    return native_demo() && (g_ram[0xF600]&127)==4 && s_frame_level &&
        s_frame_zone==g_ram[0xFE10] && s_frame_act==g_ram[0xFE11];
}
static int stage_width(void)
{
    int w=ram16(0x8000)*128;
    return w>=320 && w<=32768?w:320;
}
static int level_ready(void)
{
    unsigned w=ram16(0x8000),row=ram16(0x8008);
    /* Initial Render_Sprites runs in mode $8C after the level planes and
     * main actors are installed. Its first HUD must already own the wide
     * anchor; earlier title-card/loading lists still have F711 cleared. */
    return gameplay() && (!(g_ram[0xF600]&128) || g_ram[0xF711]) &&
        w>=3 && w<=256 && row>=0x8088 && row<0x9000;
}
static int configure(const char *mode)
{
    if (!mode) return 0;
    int was_enabled = s_mode != VIDEO_OFF;
    if (!strcmp(mode, "off")) s_mode = VIDEO_OFF;
    else if (!strcmp(mode, "fit") || !strcmp(mode, "adaptive")) s_mode = VIDEO_FIT;
    else if (!strcmp(mode, "stage")) s_mode = VIDEO_STAGE;
    else {
        char *end;
        double w = strtod(mode, &end);
        if (end == mode || *end != ':') return 0;
        const char *den = end + 1;
        double h = strtod(den, &end);
        if (end == den || *end || !(w > 0) || !(h > 0) ||
            !(w / h > 0) || w / h > (double)INT_MAX / 480.0) return 0;
        s_ratio = w / h; s_mode = VIDEO_RATIO;
    }
    scene_mode_changed(was_enabled);
    return 1;
}
static int expanded_world(void) { return s_mode != VIDEO_OFF || (s_object_zoom && s_object_zoom(0)); }
static int enabled(void) { return expanded_world() ||
    (s_background_override && s_background_override(g_ram,0,0,0,0,NULL,NULL,NULL)); }
unsigned s3_video_main_cpu_divisor(void)
{
    /* LevelLoop ($650C) still waits for the real V-int every tick.
     * Expanded activation must not inherit the 7.67 MHz object's lag budget.
     * Four times the budget still delayed a wide bridge contact by one tick;
     * eight completes that object pass before V-int, matching native contact.
     * Loading, recorded demos, special stages, native 2P and opt-out keep
     * original timing. Demo activation also remains native. */
    int mode=g_ram[0xF600];
    return s_mode != VIDEO_OFF && !native_demo() && (mode==8 || mode==12) && gameplay() && g_ram[0xF711] ? 8 : 1;
}
static int width(int dw, int dh, int nw, int nh)
{
    if (!enabled()) return 0;
    double result = nw;
    if (s_mode == VIDEO_STAGE) {
        if (gameplay()) s_stage_width = stage_width();
        result = s_stage_width > nw ? s_stage_width : nw;
    }
    else if (s_mode == VIDEO_RATIO) result = nh * s_ratio;
    else if (s_mode != VIDEO_OFF && dw > 0 && dh > 0) result = (double)nh * dw / dh;
    /* Bound by representable dimensions and the SDL texture limit (runner),
     * never by an aspect preset. Even a stage-length viewport is supported. */
    if (result > INT_MAX - 1.0) result = INT_MAX - 1.0;
    int pixels = (int)(result + 0.5);
    s_requested_width = pixels < nw ? nw : pixels;
    return s_requested_width;
}
int s3_video_canvas_width(void) { return enabled()?s_requested_width:320; }

/* Full 16-bit scroll is unwrapped against the live camera's 10-bit VDP
 * position. This avoids the one-frame camera/streaming seam seen in SMB. */
static int unwrap(int value, int reference)
{
    return reference + ((value - reference + 512) & 1023) - 512;
}
static int native_terrain_streamed(int wy, int copied_camera_y)
{
    /* Native foreground streaming uses 16px rows at copied-camera offsets -16 and
     * +224. Live VScroll can run ahead of that copy during a fast fall.
     * Outside this precise coverage, the native name table is not a terrain
     * oracle: the enhanced renderer must still draw the real stage block. */
    int top = (copied_camera_y & ~15) - 16;
    return wy >= top && wy < top + 256;
}
static S3VideoWorldCell s_world_cell;
void s3_video_set_world_cell(S3VideoWorldCell read) { s_world_cell=read; }
static S3VideoWorldPublish s_world_publish;
void s3_video_set_world_publish(S3VideoWorldPublish publish) { s_world_publish=publish; }
static int world_block(const uint8_t *ram,int wx,int wy,int background,unsigned row_mask,uint16_t *result)
{
    uint16_t block;
    if (!(s_world_cell && s_world_cell(ram,wx,wy,background,&block))) {
        unsigned header=background?0x8002u:0x8000u;
        int width=((ram[header]<<8)|ram[header+1])*128;
        if(width<=0 || (!background && (wx<0 || wx>=width)))return 0;
        unsigned row=((unsigned)wy>>5)&row_mask;
        unsigned p=0x8008u+row+(background?2u:0u);
        unsigned rowaddr=(ram[p]<<8)|ram[p+1];
        /* Get_ChunkRow uses a signed chunk offset. A moving background can
         * expose the guard chunks preceding its row (HCZ wall/HPZ/LRZ3);
         * wrapping that offset to the row's far end selects different art. */
        int cell=(int)rowaddr+(wx>>7);
        if(cell<0x8088 || cell>=0x9000)return 0;
        unsigned address=ram[cell]*128u+((unsigned)wy&112u)+((unsigned)wx&112u)/8u;
        block=(uint16_t)((ram[address]<<8)|ram[address+1]);
    }
    *result=block;return 1;
}
static uint16_t world_attr_blocks_mask(const uint8_t *ram,const uint8_t *blocks,int wx,int wy,int background,unsigned row_mask)
{
    uint16_t block;if(!world_block(ram,wx,wy,background,row_mask,&block))return 0;
    int tx=(wx>>3)&1,ty=(wy>>3)&1;
    if(block&0x400)tx^=1;
    if(block&0x800)ty^=1;
    unsigned address=0x9000u+(block&1023u)*8u+(unsigned)(ty*2+tx)*2u;
    if(address+1>=0xA800)return 0;
    address-=0x9000;
    return (uint16_t)(((blocks[address]<<8)|blocks[address+1])^((block&0xC00u)<<1));
}
static uint16_t world_attr_blocks(const uint8_t *ram,const uint8_t *blocks,int wx,int wy,int background)
{
    /* Get_ChunkRow follows the level's published layout mask. ICZ1 loops
     * at 2048px; treating negative/bottom-edge Y as a 4096px world reads
     * unrelated rows instead of the other side of that loop. */
    unsigned mask=((unsigned)ram[0xEEAE]<<8)|ram[0xEEAF];
    return world_attr_blocks_mask(ram,blocks,wx,wy,background,mask==0x3C?0x3Cu:0x7Cu);
}
static uint16_t world_attr(const uint8_t *ram,int wx,int wy,int background)
{ return world_attr_blocks(ram,ram+0x9000,wx,wy,background); }
static uint8_t pattern_pixel(const GVDP *v, uint16_t attr, int x, int y)
{
    int fx = x & 7, fy = y & 7;
    if (attr & 0x0800) fx ^= 7;
    if (attr & 0x1000) fy ^= 7;
    unsigned a = (attr & 2047u) * 32u + (unsigned)fy * 4u + (unsigned)fx / 2u;
    int byte = v->vram[a & 0xFFFFu];
    int nibble = fx & 1 ? byte & 15 : byte >> 4;
    return nibble ? (uint8_t)(((attr >> 9) & 48) | nibble) : 0;
}
static uint32_t plane_color(int line,uint16_t attr,unsigned pixel,const uint32_t *palette)
{
    int ext=pixel && !(attr&0x6000u)?presentation_plane_line(attr&2047u):0;
    return ext?genesis_dac_cram_to_argb(presentation_ext_color(line,ext,pixel&15u),GENESIS_DAC_NORMAL):palette[pixel];
}
static int plane_tiles(int bits) { return bits == 1 ? 64 : bits == 3 ? 128 : 32; }
static uint16_t plane_attr(const GVDP *v, unsigned base, int x, int y)
{
    int wt = plane_tiles(v->reg[16] & 3), ht = plane_tiles((v->reg[16] >> 4) & 3);
    int col = (x >> 3) & (wt - 1), row = (y >> 3) & (ht - 1);
    return vram16(v, base + (unsigned)(row * wt + col) * 2u);
}
#include "sonic3_aiz_join.inc"
#include "sonic3_bonus_presentation.inc"
static int file_select_layout(const GVDP *v)
{
    /* Keep the outgoing menu through its fade, after Game_mode changes.
     * SaveScreen alone installs this full-screen Window/128-column B pair. */
    return (v->reg[12]&1) && (v->reg[2]&56)==0x38 && (v->reg[3]&62)==0x38 &&
        (v->reg[4]&7)==6 && v->reg[18]==0x80 && (v->reg[16]&3)==3;
}
enum { MENU_NONE, MENU_TITLE_ANIMATION, MENU_TITLE, MENU_SAVE, MENU_COMPETITION, MENU_TITLE_CARD };
static int title_layout(const GVDP *v)
{
    /* TitleAnim_FlipBuffer displays either name table through BOTH planes;
     * only the completed title splits actor A and scenery B again. */
    unsigned a=v->reg[2]&56,b=v->reg[4]&7;
    return (v->reg[12]&1) && (a==0x30 || a==0x38) && (b==6 || b==7) &&
        v->reg[16]==S3_TITLE_PLANE_SIZE && (v->reg[11]&3)==3 && !v->reg[18];
}
static int competition_layout(const GVDP *v)
{
    return (v->reg[12]&1) && (v->reg[2]&56)==0x30 &&
        (v->reg[4]&7)==7 && v->reg[16]==0x11 && !(v->reg[11]&3) && !v->reg[18];
}
static int level_select_layout(const GVDP *v)
{
    /* S3 inherits line scrolling from Title; S&K installs plane scrolling.
     * Both publish the same word map before the incoming mode is selected. */
    unsigned scroll=v->reg[11]&3;
    if(!(v->reg[12]&1) || (v->reg[2]&56)!=0x30 || (v->reg[4]&7)!=7 ||
       v->reg[16]!=1 || (scroll!=0 && scroll!=3) || v->reg[18])return 0;
    /* MapEni_S22POptions repeats six animated SONIC/MILES tiles. The
     * initialized 40-column view ends in the middle of that word. Identify
     * its installed map so the outgoing fade keeps the same repeat too. */
    for(unsigned x=0;x<12;++x)
        if(vram16(v,0xE000+x*2)!=(uint16_t)(0x6000+(x+1)%6))return 0;
    return 1;
}
static int menu_raster_enabled(const GVDP *v)
{
    int mode=g_ram[0xF600]&127;
    int menu=mode==4 || mode==0x38 || mode==0x3C || mode==0x40 || mode==0x44 || mode==0x4C;
    return (menu || s_frame_menu) &&
        (file_select_layout(v) || title_layout(v) || competition_layout(v));
}

/* SaveScreen overwrites the background name table with its finite carousel.
 * Reconstruct the unobscured, original 40x28 scenery map, using the same ROM
 * data and resident guest patterns. No guest upload or art replacement occurs. */
typedef struct { const uint8_t *p; unsigned bit,limit; int error; } MenuBits;
static unsigned menu_bits(MenuBits *b,unsigned count)
{
    unsigned value=0;
    while(count--) {
        if(b->bit>=b->limit){b->error=1;return 0;}
        value=value*2+((b->p[b->bit/8]>>(7-b->bit%8))&1);++b->bit;
    }
    return value;
}
static unsigned menu_eni_value(MenuBits *b,unsigned width,unsigned flags)
{
    unsigned value=0;
    for(int i=4;i>=0;--i)if(flags&(1u<<i))value|=menu_bits(b,1)<<(11+i);
    return value+menu_bits(b,width);
}
static uint16_t s_menu_map[40*28];
static int s_menu_map_ready;
static int load_menu_map(void)
{
    if(s_menu_map_ready)return 1;
    const uint8_t *p=g_rom+S3_MENU_BG_MAP;
    if(!p[0] || p[0]>11 || p[1]>31)return 0;
    MenuBits b={p,48,0x202*8,0};unsigned n=0,inc=(p[2]<<8)|p[3],common=(p[4]<<8)|p[5];
    while(!b.error) {
        unsigned mode=menu_bits(&b,1)?4+menu_bits(&b,2):menu_bits(&b,1);
        unsigned count=menu_bits(&b,4)+1;
        if(mode==7 && count==16){s_menu_map_ready=n==40*28;return s_menu_map_ready;}
        if(count>40*28-n)return 0;
        unsigned value=mode==0?inc:mode==1?common:mode==7?0:menu_eni_value(&b,p[0],p[1]);
        while(count--) {
            s_menu_map[n++]=(uint16_t)((mode==7?menu_eni_value(&b,p[0],p[1]):value)+1);
            if(mode==0 || mode==5)++value;
            if(mode==6)--value;
        }
        if(mode==0)inc=value;
    }
    return 0;
}

/* Enhanced scene data lives on the host: no SAT coordinate wrap, 80-piece
 * ceiling, or 20-sprites-per-line ceiling. Native BuildSprites still executes
 * for the hardware pass and publishes a signature used to align our list
 * with the exact VBlank upload displayed by the VDP. */
/* The low slot byte identifies the native object; its high byte owns ROM art.
 * Keep each piece at 16 bytes so maximum rewind histories retain their budget. */
typedef struct { int x, y; uint16_t attr; uint8_t size, hud; uint16_t zoom,slot; } SceneSprite;
static unsigned sprite_slot(const SceneSprite *s) { return s->slot&255u; }
enum { IMPORTED_S1_PYLON = 0x7E0170u };
enum { SCENE_SPRITES = 32768, SCENE_PLACEMENTS = 1024, SCENE_SLOTS = 112, SCENE_COMPETITION_CHARACTER = 12 };
/* Only render ownership survives the original off-left deletion. Original
 * mappings/art remain in ROM; no guest object or provider pointer is held. */
typedef struct { int16_t x,y; uint8_t frame,cursor,timer,flip; } AizPlaneObject;
typedef struct {
    unsigned phase,mask; /* 0 absent, 1 native departing, 2 margin-only */
    uint16_t tick,slot;
    int16_t y_velocity;uint16_t y_fraction;uint8_t swing_down;
    AizPlaneObject object[3]; /* body, propeller, exhaust */
} AizPlanePose;
static int aiz_plane_valid(const AizPlanePose *p)
{
    if(p->phase>2 || p->mask>7 || p->swing_down>1 ||
       p->y_velocity < -0xC0 || p->y_velocity > 0xC0)return 0;
    if(!p->phase)return p->mask==0;
    if(!(p->mask&1) || p->slot<0xB000 || p->slot>=0xCFCC ||
       (p->slot-0xB000)%0x4A || p->object[0].frame)return 0;
    if(p->phase==1 && p->object[0].x < -96)return 0;
    if(p->phase==2 && p->object[0].x >= -96)return 0;
    for(unsigned n=0;n<3;++n)if(p->mask&(1u<<n)) {
        const AizPlaneObject *o=&p->object[n];
        if(o->flip>3 || o->timer>127)return 0;
        if(n==1 && (o->frame<1 || o->frame>4 || o->cursor>5))return 0;
        if(n==2 && (o->frame<5 || o->frame>6 || o->cursor>1))return 0;
    }
    return 1;
}

typedef struct {
    SceneSprite sprites[SCENE_SPRITES];
    unsigned count, serial;
    uint8_t sat[640];
    int scene; /* 1 level, 2 special, 3 native bonus, 0 menu */
    int title; /* provenance survives Game_mode's outgoing fade transition */
    unsigned world_tag; /* captured zone/act + 1; zero for non-world lists */
    int camera_x, camera_y; /* coordinates used when capturing world sprites */
    int16_t ox[SCENE_SLOTS],oy[SCENE_SLOTS];
    uint32_t code[SCENE_SLOTS];
    int preview_knux; /* waiting pose belongs to this completed display list */
    unsigned aiz_super_pose; /* original Super DPLC pose 33/34, or zero */
    AizPlanePose aiz_plane;
    unsigned aiz_join; /* owned original map/art phase across the native rebase */
    uint8_t competition_sat[640]; /* paired lower-view upload */
    int competition_x,competition_y;
    unsigned competition_page;
    int bonus_card_valid,bonus_card_x,bonus_card_y,bonus_card_exit;
    unsigned bonus_card_gfx,bonus_card_tick;
} SceneFrame;
static void copy_bonus_card(SceneFrame *dst,const SceneFrame *src)
{
    dst->bonus_card_valid=src->bonus_card_valid;dst->bonus_card_x=src->bonus_card_x;
    dst->bonus_card_y=src->bonus_card_y;dst->bonus_card_exit=src->bonus_card_exit;
    dst->bonus_card_gfx=src->bonus_card_gfx;dst->bonus_card_tick=src->bonus_card_tick;
}
static SceneFrame s_build, s_history[3], s_display_frame;
static const SceneFrame *s_display;
static void presentation_object_delta(const SceneSprite *sprite,int *dx,int *dy);
static void presentation_aiz_plane_delta(int *dx,int *dy);
static unsigned s_serial,s_upload_serial,s_scene_tick,s_spawned,s_pool_pressure;
static unsigned s_tick_samples,s_tick_updates,s_tick_lag,s_tick_multi;
static unsigned s_publication_lag;
static int s_tick_was_active;
static unsigned s_tick_last,s_tick_zone,s_tick_serial;
/* This history is deliberately absent from s3_video_state. Native-only FPS
 * capture never touches s_build/history/serial, visibility or host activation. */
static SceneFrame s_ix_build,s_ix_history[3];
static unsigned s_ix_serial,s_ix_upload_serial;
static SceneFrame *s_capture_frame=&s_build;
static unsigned s_capture_slot;
static int s_capture_readonly;
static void interpolation_native_publish(void);
static void competition_capture_objects(void);
static void capture_slots_vblank(void);
void s3_video_vblank(void)
{
    capture_slots_vblank();
    if(s_mode!=VIDEO_OFF)ss_capture();
    if(s_interpolation_enabled && !enabled() && g_ram[0xF62A])s_ix_upload_serial=s_ix_serial;
    /* The V-int uploads the last completed display list. Camera-follow can
     * leave its SAT byte-identical to a newer, not-yet-uploaded world pose. */
    if(enabled() && (g_ram[0xF62A] || (ram16(S3_COMPETITION) && g_ram[0xEF3E])))s_upload_serial=s_serial;
    unsigned tick=ram16(0xFE04),zone=ram16(0xFE10);
    /* The following V-int uploads this completed tile queue. Main-thread
     * deformation may advance its RAM counters again before scanline zero,
     * so retain the draw positions belonging to the displayed VDP cache. */
    s_bg_draw_ready=enabled() && g_ram[0xF600]==12 && g_ram[0xF711];
    if(s_bg_draw_ready) {
        s_bg_draw_at_vblank[0]=(int16_t)ram16(0xA802);
        s_bg_draw_at_vblank[1]=(int16_t)ram16(0xA806);
        s_bg_draw_zone=zone;
    }
    int active=enabled() && g_ram[0xF600]==12 && gameplay() &&
        g_ram[0xF711] && g_ram[0xB005]<6 && !ram16(0xF63A);
    /* Sample at the same IRQ entry, not end-of-wall-frame: native variable
     * V-int DMA debt can shift the next tick across that later sample point. */
    if(active && s_tick_was_active && zone==s_tick_zone) {
        unsigned delta=(uint16_t)(tick-s_tick_last);
        ++s_tick_samples;s_tick_updates+=delta;
        s_tick_lag+=delta==0;s_tick_multi+=delta>1;
        s_publication_lag+=s_serial==s_tick_serial;
    }
    s_tick_was_active=active;s_tick_last=tick;s_tick_zone=zone;s_tick_serial=s_serial;
}
static unsigned s_visible_objects[144], s_visible_count;
typedef struct { unsigned address; uint16_t x, y; uint8_t id, subtype, loaded; uint16_t state; } Placement;
static Placement s_placements[SCENE_PLACEMENTS];
static unsigned s_placement_count, s_placement_base;
static int s_loader_active;

static uint8_t scene_read8(unsigned a)
{
    a &= 0xFFFFFFu;
    if (a < 0x400000u) return g_rom[a];
    if (a >= 0xFF0000u) return g_ram[a & 65535u];
    /* the opt-in expansion bank (game-added zones keep their layouts there) */
    return g_rom_ext && a - 0x400000u < g_rom_ext_size ? g_rom_ext[a - 0x400000u] : 0;
}
static uint16_t scene_read16(unsigned a) { return (uint16_t)((scene_read8(a)<<8)|scene_read8(a+1)); }
static uint32_t scene_read32(unsigned a) { return ((uint32_t)scene_read16(a)<<16)|scene_read16(a+2); }
static void write8(unsigned a, unsigned v) { glue_poke8(0xFF0000u|(a&65535u),(uint8_t)v); }
static void write16(unsigned a, unsigned v) { glue_poke16(0xFF0000u|(a&65535u),(uint16_t)v); }
static void write32(unsigned a, unsigned v) { glue_poke32(0xFF0000u|(a&65535u),v); }
static int scene_view_left(int camera,int w,unsigned join,int physical_width,
                           unsigned zone,unsigned act)
{
#ifndef SONIC3_STANDALONE
    /* The flying scene continues beyond its finite scenery layout. Keep
     * actors, activation and presentation centered on the native camera. */
    if(zone==0x0C && !act)return camera-(w-320)/2;
#else
    (void)zone;(void)act;
#endif
    return aiz_join_left(camera,w,join,physical_width);
}
static int view_left(int camera, int w)
{
    return scene_view_left(camera,w,0,stage_width(),g_ram[0xFE10],g_ram[0xFE11]);
}
#include "sonic3_camera_handoff.inc"

int s3_video_presentation_camera_x(int camera)
{
    if(s_mode==VIDEO_OFF || s_requested_width<=320)return camera;
    return scene_view_left(camera,s_requested_width,aiz_join_tag(g_ram),stage_width(),
        g_ram[0xFE10],g_ram[0xFE11])+
        (s_requested_width-320)/2;
}
static void activation_bounds(int camera,int w,int *lo,int *hi)
{
    int left=view_left(camera,w);
    /* out_of_range tests 128px-rounded positions. OPL must only instantiate
     * cells that this same culler retains. Loading at left+w+320 with an
     * unrounded camera let objects spawn one cell early, get deleted, and
     * remain marked loaded until they left the window (missing platforms). */
    *lo=(left-128)&~127;
    *hi=((left+w+192)&~127)+128;
}

static void scene_mode_changed(int was_enabled)
{
    if(was_enabled!=enabled()) {
        s_display=NULL;s_display_frame.serial=0;
        s_upload_serial=0;
        memset(&s_build.aiz_plane,0,sizeof s_build.aiz_plane);
        s_frame_menu=MENU_NONE;s_frame_level=0;
        for(unsigned n=0;n<3;++n)s_history[n].serial=0;
    }
    /* Once adopted, the host loader also maintains a native-width window
     * on opt-out until the next level init, preserving existing object state. */
}
static int zoom_offset(int offset, unsigned zoom)
{
    int scaled = offset * (int)zoom;
    return scaled >= 0 ? scaled / 256 : -((255 - scaled) / 256);
}
static void add_mapping(unsigned map,unsigned frame,unsigned gfx,unsigned flags,
                        int x,int y,int anchor,int static_mapping,unsigned zoom)
{
    if (zoom < 64 || zoom > 512) zoom = 256;
    unsigned p=map;int count=1;
    if(!static_mapping) {
        p+=(int16_t)scene_read16(p+(frame&255u)*2u);
        count=scene_read16(p);p+=2;
    }
    if(count>1024)return;
    for(int n=0;n<count && s_capture_frame->count<SCENE_SPRITES;++n,p+=6) {
        int dy=(int8_t)scene_read8(p),size=scene_read8(p+1);
        int dx=(int16_t)scene_read16(p+4);
        uint16_t attr=(uint16_t)(scene_read16(p+2)+gfx);
        if(flags&1){dx=-dx-(((size>>2)&3)+1)*8;attr^=0x800;}
        if(flags&2){dy=-dy-((size&3)+1)*8;attr^=0x1000;}
        /* A full brick repeats every32px while the water front advances.
         * Its coverage moves; fractional drawing keeps the texture in world phase. */
        if(map==S3_HCZ_RUSH_MAP && zoom==256 &&
           (scene_read16(p+2)==0x18 || scene_read16(p+2)==0x30))
            size|=size==15?0x20:0x10;
        /* In AIZ, bit4 records immutable monkey-art ownership. The zone
         * separates it from HCZ's half-brick and extended-roof uses. */
        if(map==S3_MONKEY_MAP)size|=SPRITE_MONKEY_ART;
        dx=zoom_offset(dx,zoom);dy=zoom_offset(dy,zoom);
        if(y+dy>=224 || y+dy+((size&3)+1)*8*(int)zoom/256<=0)continue;
        SceneSprite *q=&s_capture_frame->sprites[s_capture_frame->count++];
        q->x=x+dx;q->y=y+dy;q->attr=attr;q->size=(uint8_t)size;q->hud=(uint8_t)anchor;q->zoom=(uint16_t)zoom;
        q->slot=(uint16_t)(s_capture_slot|(native_art_owner(map)<<8));
    }
}

static void extend_hcz_surface(unsigned map,unsigned frame,unsigned gfx,unsigned flags,
                               int x,int y,unsigned zoom)
{
    if(map!=S3_HCZ_SURFACE_MAP || !(flags&4) || g_ram[0xFE10]!=1 ||
       !enabled() || s_requested_width<=320)return;
    /* The native waterline publishes two 192px strips. Repeat its original
     * moving/paused pose through the extended view without adding objects or
     * duplicating either native strip. Their 32px gaps and phase stay intact. */
    int reach=s_requested_width/192+2;
    for(int n=-reach;n<=reach;++n)if(n!=0 && n!=1)
        add_mapping(map,frame,gfx,flags,x+n*192,y,0,0,zoom);
}
static void extend_hcz_rush(unsigned map,unsigned frame,unsigned gfx,unsigned flags,
                            int x,int y,unsigned zoom)
{
    if(map!=S3_HCZ_RUSH_MAP || frame>3 || flags!=4 || ram16(0xFE10)!=0x100 ||
       !enabled() || s_requested_width<=320 || (zoom && zoom!=256))return;
    /* The moving front owns a finite field of32px blue bricks. Continue
     * those original patterns in the margins, stopping at native retirement. */
    unsigned p=map+(int16_t)scene_read16(map+frame*2),count=scene_read16(p);p+=2;
    if(count>32)return;
    int end=0x980-ram16(0xEE78),right=(s_requested_width+320+1)/2;
    if(end>right)end=right;
    for(unsigned n=0;n<count;++n) {
        unsigned piece=p+n*6,attr=scene_read16(piece+2);
        if(scene_read8(piece+1)!=15 || (attr!=0x18 && attr!=0x30))continue;
        int dy=(int8_t)scene_read8(piece),dx=(int16_t)scene_read16(piece+4),last=1,top=1,first=dx;
        for(unsigned k=0;k<count;++k) {
            unsigned other=p+k*6,oa=scene_read16(other+2);
            if(scene_read8(other+1)!=15 || (oa!=0x18 && oa!=0x30))continue;
            int oy=(int8_t)scene_read8(other),ox=(int16_t)scene_read16(other+4);
            if(oy<dy)top=0;
            if(oy==dy && oa==attr){if(ox>dx)last=0;if(ox<first)first=ox;}
        }
        if(!last || y+dy>=224 || y+dy+32<=0)continue;
        for(int sx=x+dx+32;sx<end && s_capture_frame->count<SCENE_SPRITES;sx+=32) {
            SceneSprite *q=&s_capture_frame->sprites[s_capture_frame->count++];
            q->x=sx;q->y=y+dy;q->attr=(uint16_t)(gfx+attr);
            q->size=0x6F;q->hud=0;q->zoom=256;q->slot=(uint16_t)s_capture_slot;
        }
        /* Only moving water fills the raised opening. The closed frames
         * retain their authored upper edge rather than tinting the dry roof.
         * Bit4 marks this roof fill, bit5 the repeating texture, bit6 margins. */
        if(top && frame<2)for(int sy=y+dy-32;sy+32>0;sy-=32) {
            int sx=x+first,margin=(s_requested_width-320)/2;
            while(sx+32<=-margin)sx+=32;
            for(;sx<end && s_capture_frame->count<SCENE_SPRITES;sx+=32) {
                SceneSprite *q=&s_capture_frame->sprites[s_capture_frame->count++];
                q->x=sx;q->y=sy;q->attr=(uint16_t)(gfx+attr);
                q->size=0x7F;q->hud=0;q->zoom=256;q->slot=(uint16_t)s_capture_slot;
            }
        }
    }
}
static void extend_icz_snow(unsigned map,unsigned frame,unsigned gfx,unsigned flags,
                            int x,int y,unsigned zoom)
{
    if(map!=S3_ART_BANK+0x163C44u || frame!=0x22 || flags!=4 ||
       ram16(0xFE10)!=0x501 || !enabled() || s_requested_width<=320 ||
       (zoom && zoom!=256) || x+ram16(0xEE80)!=0x4894 ||
       y+ram16(0xEE84)!=0x6D0)return;
    /* The last authored snow pile ends inside the expanded exit view.
     * Continue its two original pieces to the right edge, only while that
     * real pile is published. Slot owner 7 clips this presentation-only
     * continuation outside the native viewport; no collision object is added. */
    int right=(s_requested_width+320+1)/2;
    for(int sx=x+32;sx-16<right;sx+=32){
        unsigned first=s_capture_frame->count;
        add_mapping(map,frame,gfx,flags,sx,y,0,0,zoom);
        for(unsigned n=first;n<s_capture_frame->count;++n)
            s_capture_frame->sprites[n].slot=(uint16_t)(s_capture_slot|(7u<<8));
    }
}
static void load_placements(void)
{
    unsigned zone=ram16(0xFE10),index=(zone>>8)*8u+(zone&1u)*4u;
    unsigned base=scene_read32(S3_PLACEMENTS+index);
    if(base==s_placement_base && s_placement_count)return;
    s_placement_base=base;s_placement_count=0;
    if(base>=0x400000u)return;
    for(unsigned p=base;p+6<=0x400000u && s_placement_count<768;p+=6) {
        unsigned x=scene_read16(p);if(x==65535u)break;
        Placement *q=&s_placements[s_placement_count];
        q->address=p;q->x=(uint16_t)x;q->y=scene_read16(p+2);
        q->id=scene_read8(p+4);q->subtype=scene_read8(p+5);
        q->state=(uint16_t)(0xEB00+s_placement_count++);q->loaded=0;
    }
}
static unsigned placement_code(const Placement *p)
{
#ifdef SONIC3_STANDALONE
    return scene_read32(0x5CC96u+p->id*4u);
#else
    return scene_read32(scene_read32(0xFFEF5Au)+p->id*4u);
#endif
}
static void spawn_scene(void)
{
    load_placements();s_loader_active=1;
    int camera=ram16(0xEE78),cy=ram16(0xEE7C),w=enabled()?s_requested_width:320;
    int lo,hi;activation_bounds(camera,w,&lo,&hi);
    int player=ram16(0xB010);s_pool_pressure=0;
    int ylo=(cy&~127)-128,yhi=(cy&~127)+512;
    for(unsigned i=0;i<s_placement_count;++i) {
        Placement *p=&s_placements[i];
        if((int)p->x<lo || (int)p->x>=hi)p->loaded=0;
    }
    for(unsigned pass=0;pass<s_placement_count;++pass) {
        int best=-1,distance=INT_MAX;
        for(unsigned i=0;i<s_placement_count;++i) {
            Placement *p=&s_placements[i];
            if(p->loaded || (g_ram[p->state]&128) || (int)p->x<lo || (int)p->x>=hi)continue;
            /* This cutscene's initializer loads PLC $5A into the same VRAM
             * bank that AIZ1_Resize subsequently uses for PLC $0C at camera
             * $2E00. Early wide activation reverses their native order and
             * overwrites the miniboss with vines/trees. Keep this scripted
             * object inside Load_Sprites' native 128px-aligned interval.
             * Addresses: Obj_AIZMinibossCutscene in the pinned S3/S&K lists. */
            unsigned code=placement_code(p);
            /* Capsules permanently retain their loaded bit when culled.
             * Early wide activation followed by a short camera backtrack
             * can delete one before the native boss approach locks scrolling,
             * leaving no capsule after victory. HCZ's gate and large fan
             * also change the global current inhibit/palette on activation.
             * Preserve the native timing of these scripted operations. */
            if(((int)p->x<((camera-128)&~127) || (int)p->x>=(camera&~127)+640) &&
                (code==S3_AIZ_MINIBOSS_CUTSCENE || code==S3_EGG_CAPSULE ||
                 code==S3_HCZ_LARGE_FAN || code==S3_HCZ_WATER_RUSH))continue;
            int y=p->y&4095;
            if(!(p->y&0x8000)) {
                if((int16_t)ram16(0xEE18)<0) {
                    int mask=ram16(0xEEAA),dy=(y-ylo)&mask;
                    if(dy>640)continue;
                } else if(y<ylo || y>yhi)continue;
            }
            int d=abs((int)p->x-player);
            if(d<distance){best=(int)i;distance=d;}
        }
        if(best<0)break;
        Placement *p=&s_placements[best];unsigned o;
        for(o=0xB0DE;o<0xCAE2;o+=0x4A) {
            /* Same reservation as the native allocators: HCZ1's fixed
             * splash must not overwrite a live act-transition controller. */
            if(o==0xB172 && ram16(0xFE10)==0x100 && !ram16(S3_COMPETITION))continue;
            if(!scene_read32(0xFF0000u+o))break;
        }
        if(o>=0xCAE2){++s_pool_pressure;break;}
        write16(o+0x10,p->x);write16(o+0x14,p->y&4095);
        unsigned flip=(p->y>>13)&3;
        write8(o+4,flip);write8(o+0x2A,flip);write16(o+0x48,p->state);
        write8(o+0x2C,p->subtype);write8(p->state,g_ram[p->state]|128);
        write32(o,placement_code(p));
        p->loaded=1;++s_spawned;
    }
    unsigned r=0,l=0;
    while(r<s_placement_count && s_placements[r].x<(unsigned)((camera&~127)+640))++r;
    while(l<s_placement_count && (int)s_placements[l].x<((camera-128)&~127))++l;
    write32(0xF772,s_placement_base+r*6);write32(0xF776,s_placement_base+l*6);
    write16(0xF77A,0xEB00+r);write16(0xF77C,0xEB00+l);
    write16(0xF76E,camera&0xFF80);write16(0xF770,cy&0xFF80);
    write16(0xF7DA,(camera-128)&0xFF80);write16(0xEF64,(cy-128)&0xFF80);
}
static S3VideoRingLayout s_ring_layout;
void s3_video_set_ring_layout(S3VideoRingLayout fn) { s_ring_layout = fn; }
static S3VideoHudMapping s_hud_mapping;
void s3_video_set_hud_mapping(S3VideoHudMapping fn) { s_hud_mapping = fn; }
static S3VideoHudObject s_hud_object;
void s3_video_set_hud_object(S3VideoHudObject fn) { s_hud_object = fn; }
static unsigned s_title_name_map;
void s3_video_set_title_card_name_map(unsigned map) { s_title_name_map=map; }
static unsigned rings_base(void)
{
    unsigned own=s_ring_layout?s_ring_layout():0;
    if(own)return own;
    unsigned index=(g_ram[0xFE10]*8u)+((g_ram[0xFE11]&1u)*4u);
    /* Ring layouts begin with an x=0 sentinel, native status index zero. */
    return scene_read32(S3_RINGS+index);
}
static unsigned rings_end(void)
{
    unsigned p=rings_base();
    if(p>=0x400000u+(g_rom_ext?g_rom_ext_size:0u))return p;
    for(unsigned n=0;n<512 && scene_read16(p)!=65535u;++n)p+=4;
    return p;
}
/* Capture original child poses while native owns them. After its authored
 * x<$20 retirement, continue the same -4px motion only in the wide margin. */
static void aiz_plane_object(AizPlaneObject *o,unsigned slot)
{
    o->x=(int16_t)(ram16(slot+0x10)-128);o->y=(int16_t)(ram16(slot+0x14)-128);
    o->frame=g_ram[slot+0x22];o->cursor=g_ram[slot+0x23];
    o->timer=g_ram[slot+0x24];o->flip=g_ram[slot+4]&3u;
}
static void aiz_plane_animate(AizPlaneObject *o,unsigned sequence)
{
    o->timer=(uint8_t)(o->timer-1);
    if((int8_t)o->timer>=0)return;
    o->cursor=(uint8_t)(o->cursor+1);
    unsigned value=scene_read8(sequence+1+o->cursor);
    if(value==0xFC){o->cursor=0;value=scene_read8(sequence+1);}
    o->frame=(uint8_t)value;o->timer=scene_read8(sequence);
}
static void capture_aiz_plane(void)
{
    AizPlanePose previous=s_capture_frame->aiz_plane,*p=&s_capture_frame->aiz_plane;
    memset(p,0,sizeof *p);
    if(s_mode==VIDEO_OFF || s_requested_width<=320 ||
       s_capture_frame->scene!=1 || (g_ram[0xF600]&127)!=12 || ram16(0xFE10))return;
    unsigned body=0;
    for(unsigned o=0xB000;o<0xCFCC;o+=0x4A)
        if(scene_read32(0xFF0000u+o)==S3_AIZ_PLANE_DEPART &&
           scene_read32(0xFF0000u+o+12)==S3_AIZ_PLANE_MAP){body=o;break;}
    if(body) {
        p->phase=1;p->mask=1;p->tick=ram16(0xFE04);p->slot=(uint16_t)body;
        aiz_plane_object(&p->object[0],body);
        p->y_velocity=(int16_t)ram16(body+0x1A);p->y_fraction=ram16(body+0x16);
        p->swing_down=g_ram[body+0x38]&1u;
        for(unsigned o=0xB000;o<0xCFCC;o+=0x4A) {
            unsigned code=scene_read32(0xFF0000u+o);
            unsigned n=code==S3_AIZ_PLANE_PROP?1:code==S3_AIZ_PLANE_EXHAUST?2:0;
            if(n && ram16(o+0x46)==body && scene_read32(0xFF0000u+o+12)==S3_AIZ_PLANE_MAP) {
                p->mask|=1u<<n;aiz_plane_object(&p->object[n],o);
            }
        }
        if(!aiz_plane_valid(p))memset(p,0,sizeof *p);
        return;
    }
    if(!previous.phase || !aiz_plane_valid(&previous))return;
    unsigned delta=(uint16_t)(ram16(0xFE04)-previous.tick);
    int next_x=previous.object[0].x-(int)delta*4;
    /* A different native deletion must never create this visual owner. */
    if(delta>32767 || next_x<INT16_MIN || (previous.phase==1 && next_x>=-96))return;
    *p=previous;p->phase=2;p->tick=ram16(0xFE04);
    for(unsigned n=0;n<3;++n)if(p->mask&(1u<<n))
        p->object[n].x=(int16_t)(p->object[n].x-(int)delta*4);
    for(unsigned tick=0;tick<delta;++tick) {
        /* Original Swing_Setup1 / Swing_UpAndDown / MoveSprite2: C0
         * velocity limit, 10 acceleration, then signed 8.8 integration. */
        int velocity=p->y_velocity;
        if(!p->swing_down) {
            velocity-=0x10;
            if(velocity<=-0xC0){p->swing_down=1;velocity+=0x10;}
        } else {
            velocity+=0x10;
            if(velocity>=0xC0){p->swing_down=0;velocity-=0x10;}
        }
        p->y_velocity=(int16_t)velocity;
        uint32_t y=((uint32_t)(uint16_t)p->object[0].y<<16)|p->y_fraction;
        y+=(uint32_t)(velocity*256);p->y_fraction=(uint16_t)y;
        int dy=(int16_t)(y>>16)-p->object[0].y;
        for(unsigned n=0;n<3;++n)if(p->mask&(1u<<n)) {
            p->object[n].y=(int16_t)(p->object[n].y+dy);
            if(n)aiz_plane_animate(&p->object[n],n==1?S3_AIZ_PLANE_PROP_ANIM:S3_AIZ_PLANE_EXHAUST_ANIM);
        }
    }
}
static void append_aiz_plane(void)
{
    AizPlanePose *p=&s_capture_frame->aiz_plane;
    if(p->phase!=2)return;
    unsigned first=s_capture_frame->count;s_capture_slot=0;
    int visible=0;
    for(unsigned n=0;n<3;++n)if(p->mask&(1u<<n)) {
        const AizPlaneObject *o=&p->object[n];
        add_mapping(S3_AIZ_PLANE_MAP,o->frame,0x529,o->flip,o->x,o->y,5,0,256);
    }
    for(unsigned n=first;n<s_capture_frame->count;++n) {
        const SceneSprite *q=&s_capture_frame->sprites[n];
        int right=q->x+(((q->size>>2)&3)+1)*8+(s_requested_width-320)/2;
        if(right>0)visible=1;
    }
    if(!visible){s_capture_frame->count=first;memset(p,0,sizeof *p);}
}
static int mgz2_parked_body(unsigned o,unsigned map)
{
    if(ram16(0xFE10)!=0x201 ||
       (map!=S3_ART_BANK+0x162608u && map!=S3_ART_BANK+0x160058u))return 0;
    /* The perspective pass owns a separate scaled object. Its full-size
     * body, pilot and nested attachments wait outside the native viewport
     * until that pass ends. They are not extra actors in the wider world. */
    for(unsigned depth=0;depth<3;++depth) {
        if(scene_read32(0xFF0000u+o)==S3_MGZ_END_BOSS)return g_ram[o+5]==0x1E;
        unsigned parent=ram16(o+0x46);
        if(parent<0xB000 || parent>=0xCFE0 || (parent-0xB000)%0x4A)return 0;
        o=parent;
    }
    return 0;
}
static void capture_objects(void)
{
    if(ram16(S3_COMPETITION)){competition_capture_objects();return;}
    int prior_preview=s_capture_frame->preview_knux;
    int bonus_card_found=0;
    s_capture_frame->count=0;s_capture_slot=0;s_capture_frame->aiz_super_pose=0;
    memset(s_capture_frame->code,0,sizeof s_capture_frame->code);
    if(!s_capture_readonly){s_visible_count=0;++s_scene_tick;}
    /* Start changes the mode before the last demo Render_Sprites call.
     * That completed SAT still belongs to the outgoing level, not Title. */
    s_capture_frame->scene=bonus_stage()?3:(level_ready() || demo_level_exit())?1:0;
    if(s_capture_frame->scene!=3)s_capture_frame->bonus_card_valid=0;
    s_capture_frame->world_tag=s_capture_frame->scene==1?(unsigned)ram16(0xFE10)+1u:0u;
    s_capture_frame->aiz_join=s_capture_frame->scene==1?aiz_join_tag(g_ram):0;
    s_capture_frame->title=!s_capture_frame->scene && (g_ram[0xF600]&127)==4;
    s_capture_frame->preview_knux=s_capture_frame->scene==1 && s3_knux_preview_needed(g_ram);
    /* Allocation can target an earlier slot than the controller. Keep the
     * waiting pose for that one queued-display handover until the real actor
     * below has actually reached Draw_Sprite. The tag is saved with the scene. */
    if(prior_preview && s_capture_frame->scene==1 && g_ram[0xF600]==12 &&
       ram16(0xFE10)==1 && g_ram[0xB038]!=2)
        for(unsigned o=0xB000;o<0xCFCC;o+=0x4A)
            if(scene_read32(0xFF0000u+o)==S3_KNUX_PREVIEW_HANDOVER)s_capture_frame->preview_knux=1;
    /* Gumball's finite chamber keeps its native actors and machine column;
     * only the authored outer scenery extends beyond that chamber. The two
     * SK bonuses retain complete mappings; Glowing Sphere also owns the
     * separate, vertically sorted ring list sampled below. */
    if(s_capture_frame->scene==3 && g_ram[0xFE10]==0x13) {
        memset(&s_capture_frame->aiz_plane,0,sizeof s_capture_frame->aiz_plane);return;
    }
    capture_aiz_plane();
    int cam=ram16(0xEE80),cy=ram16(0xEE84);
    s_capture_frame->camera_x=cam;s_capture_frame->camera_y=cy;
    int left=gameplay()?view_left(cam,s_requested_width):cam-(s_requested_width-320)/2;
    int mask=ram16(0xEEAA);
    int level_hud=gameplay() && g_ram[0xF711];
    int glowing_rings=s_capture_frame->scene==3 && g_ram[0xFE10]==0x14;
    if(level_hud) {
        unsigned blink=(g_ram[0xFE05]&8)==0;
        unsigned frame=blink*((ram16(0xFE20)==0?1:0)+(g_ram[0xFE23]==9?2:0));
        int hudx=(int8_t)g_ram[0xF711];if(hudx<0)hudx+=8;
        unsigned hud=s_hud_mapping?s_hud_mapping():0;
        add_mapping(hud?hud:S3_HUD_MAP,frame,0x86CA,0,15+hudx,136,1,0,0);
    }
    if(level_hud || glowing_rings) {
        unsigned base=rings_base(),end=base?rings_end():0;
        /* Glowing Sphere starts with a real ring and keeps its native
         * consumption states in the layout buffer, not the stage table. */
        for(unsigned p=base+(glowing_rings?0:4);p<end;p+=4) {
            unsigned status=(glowing_rings?0x8600:0xE700)+(p-base)/2;
            if(ram16(status)&0x8000)continue;
            int x=scene_read16(p),y=((scene_read16(p+2)-cy+8)&mask)-8;
            if(x<left-16 || x>=left+s_requested_width+16 || y<-16 || y>=240)continue;
            unsigned fr=g_ram[status+1]?g_ram[status+1]:g_ram[0xFEB3];
            unsigned m=S3_RING_MAP+fr*8;
            if(s_capture_frame->count<SCENE_SPRITES) {
                SceneSprite *q=&s_capture_frame->sprites[s_capture_frame->count++];
                q->x=x-cam+(int16_t)scene_read16(m+6);q->y=y+(int16_t)scene_read16(m);
                q->attr=scene_read16(m+4);q->size=(uint8_t)scene_read16(m+2);q->hud=0;q->zoom=256;q->slot=0;
            }
        }
    }
    for(unsigned pri=0;pri<8;++pri) {
        unsigned queue=0xAC00+pri*128,bytes=ram16(queue);if(bytes>126)bytes=126;
        for(unsigned k=2;k<=bytes;k+=2) {
            unsigned o=ram16(queue+k);if(o<0xB000 || o>=0xCFE0 || !scene_read32(0xFF0000u+o))continue;
            s_capture_slot=1+(o-0xB000)/0x4A;
            if(s_capture_slot>=SCENE_SLOTS)continue;
            s_capture_frame->ox[s_capture_slot]=(int16_t)ram16(o+0x10);
            s_capture_frame->oy[s_capture_slot]=(int16_t)ram16(o+0x14);
            s_capture_frame->code[s_capture_slot]=scene_read32(0xFF0000u+o);
            /* A native child can retire one queue later than its parent. */
            if(s_capture_frame->aiz_plane.phase==2 && ram16(o+0x46)==s_capture_frame->aiz_plane.slot &&
               (s_capture_frame->code[s_capture_slot]==S3_AIZ_PLANE_PROP ||
                s_capture_frame->code[s_capture_slot]==S3_AIZ_PLANE_EXHAUST))continue;
            unsigned flags=g_ram[o+4],multi=flags&64;
            int x,y,anchor=0;
            if(flags&4) {
                x=(int16_t)(ram16(o+0x10)-cam);
                int radius=g_ram[o+7],height=g_ram[o+6];
                y=((ram16(o+0x14)-cy+height)&mask)-height;
                if(x+radius<left-cam || x-radius>=left-cam+s_requested_width || y+height<0 || y-height>=224)continue;
                if(!s_capture_readonly && s_capture_frame->scene!=3 && s_visible_count<144)s_visible_objects[s_visible_count++]=o;
            } else {
                x=(int16_t)ram16(o+0x10)-128;y=(int16_t)ram16(o+0x14)-128;
                anchor=s_hud_object && s_hud_object(o)?1:2;
            }
            /* Imported SLZ pylons retain the original 9-bit SAT phase.
             * Use the clamped foreground view for their second camera term;
             * never let a screen-space decoration become centered UI. */
            int pylon=s_capture_frame->scene==1 && !(flags&4) &&
                s_capture_frame->code[s_capture_slot]==IMPORTED_S1_PYLON;
            if(pylon) {
                int view_camera=left+(s_requested_width-320)/2;
                x=((ram16(o+0x10)+cam-view_camera)&511)-128;
                anchor=0;
                s_capture_frame->ox[s_capture_slot]=(int16_t)(cam+x);
                s_capture_frame->oy[s_capture_slot]=(int16_t)(cy+y);
            }
            unsigned map=scene_read32(0xFF0000u+o+0xC),gfx=ram16(o+0xA);
            if(mgz2_parked_body(o,map))continue;
#ifndef SONIC3_STANDALONE
            unsigned frame=g_ram[o+0x22];
            if(s_capture_frame->scene==3 && map==S3_TITLE_CARD_MAP && !(flags&4) &&
               ((frame==19 && ram16(o+0x46)==0xC8) ||
                (frame==20 && ram16(o+0x46)==0x128))){
                /* BONUS and STAGE have identical motion, but the native
                 * viewport retires STAGE first. Keep one rigid composition
                 * until both words have crossed the expanded canvas. */
                if(frame==19 || !bonus_card_found){
                    unsigned parent=ram16(o+0x48);
                    s_capture_frame->bonus_card_x=x-(frame==20?96:0);
                    s_capture_frame->bonus_card_y=y;
                    s_capture_frame->bonus_card_gfx=gfx;
                    s_capture_frame->bonus_card_exit=parent>=0xB000 && parent<0xCFE0 &&
                        ram16(parent+0x32)!=0;
                }
                bonus_card_found=1;continue;
            }
#endif
            /* ObjArray_TtlCard's original screen-space targets. Bonus-card
             * layouts and unrelated mappings keep their existing projection. */
            if((map==S3_TITLE_CARD_MAP || (s_title_name_map && map==s_title_name_map)) && !(flags&4)) {
                unsigned target=ram16(o+0x46),frame=g_ram[o+0x22];
                if(frame>=4 && target==0x120)anchor=6;
                else if(frame==3 && target==0x17C)anchor=7;
                else if(frame==2 && target==0x184)anchor=8;
                else if(frame==1 && target==0xC0)anchor=9;
            }
            if(s_capture_frame->scene==1 && ram16(0xFE10)==0 &&
               s_capture_frame->code[s_capture_slot]==S3_AIZ_PLANE_INTRO && map==S3_SUPER_MAP &&
               (g_ram[o+0x22]==33 || g_ram[o+0x22]==34))
                s_capture_frame->aiz_super_pose=(unsigned)g_ram[o+0x22]-32;

            if(map==S3_KNUX_PREVIEW_MAP)s_capture_frame->preview_knux=0;
            unsigned zoom=(flags&4) && s_object_zoom?s_object_zoom(o):0;
            /* The title plane is queued even beyond the hardware viewport.
             * Retain its signed position instead of the wrapped/cut SAT. */
            if((g_ram[0xF600]&127)==4 && map==S3_TITLE_PLANE_MAP)anchor=3;
            /* Competition queues signed screen-space cards beyond the SAT's
             * viewport too. Keep their original mappings through the margin. */
            unsigned mode=g_ram[0xF600]&127;
            if(!(flags&4) && mode>=0x38 && mode<=0x44)anchor=3;
            if(anchor==3 && map==S3_ART_BANK+0x1A1AEE)
                anchor=SCENE_COMPETITION_CHARACTER;
            /* Data Select queues every card, including those culled by the
             * native SAT builder. Preserve their signed carousel positions. */
            if(map==S3_FILE_SELECT_MAP)anchor=4;
            unsigned mapped_first=s_capture_frame->count;
            if(multi) {
                if(g_ram[o+0x22])add_mapping(map,g_ram[o+0x22],gfx,flags,x,y,anchor,0,zoom);
                unsigned count=ram16(o+0x16);if(count>8)count=8;
                for(unsigned n=0;n<count;++n) {
                    unsigned child=o+0x18+n*6;
                    int sx=(int16_t)ram16(child)-128,sy=(int16_t)ram16(child+2)-128;
                    if(flags&4){sx=(int16_t)(ram16(child)-cam);sy=((ram16(child+2)-cy+128)&mask)-128;}
                    add_mapping(map,g_ram[child+5],gfx,flags,sx,sy,anchor,0,zoom);
                }
            } else add_mapping(map,g_ram[o+0x22],gfx,flags,x,y,anchor,(flags&32)!=0,zoom);
            if(pylon) {
                unsigned end=s_capture_frame->count;
                int lo=left-cam,hi=lo+s_requested_width;
                for(unsigned n=mapped_first;n<end;++n) {
                    const SceneSprite source=s_capture_frame->sprites[n];
                    int cw=(((source.size>>2)&3)+1)*8,repeat=source.x;
                    while(repeat+cw>lo)repeat-=512;
                    for(repeat+=512;repeat<hi;repeat+=512)
                        if(repeat!=source.x && s_capture_frame->count<SCENE_SPRITES) {
                            SceneSprite fill=source;fill.x=repeat;
                            s_capture_frame->sprites[s_capture_frame->count++]=fill;
                        }
                }
            }
            if(multi)extend_hcz_surface(map,g_ram[o+0x22]?g_ram[o+0x22]:g_ram[o+0x1D],
                gfx,flags,x,y,zoom);
            else extend_hcz_rush(map,g_ram[o+0x22],gfx,flags&127,x,y,zoom);
            if(!multi)extend_icz_snow(map,g_ram[o+0x22],gfx,flags&127,x,y,zoom);
        }
    }
    if(s_capture_frame->scene==3){
        unsigned tick=ram16(0xFE04);
        if(bonus_card_found)s_capture_frame->bonus_card_valid=1;
        else if(s_capture_frame->bonus_card_valid && s_capture_frame->bonus_card_exit){
            if(tick!=s_capture_frame->bonus_card_tick)s_capture_frame->bonus_card_x+=32;
        }else s_capture_frame->bonus_card_valid=0;
        s_capture_frame->bonus_card_tick=tick;
        if(s_capture_frame->bonus_card_x+(s_requested_width-320)/2>=s_requested_width)
            s_capture_frame->bonus_card_valid=0;
        if(s_capture_frame->bonus_card_valid){
            /* Loading has not installed the bonus ring/object tables yet.
             * Only the real card belongs to this publication; interpreting
             * leftover menu entries as bonus actors exposes stale VRAM. */
            if(!g_ram[0xF711])s_capture_frame->count=0;
            s_capture_slot=0;
            add_mapping(S3_TITLE_CARD_MAP,19,s_capture_frame->bonus_card_gfx,0,
                s_capture_frame->bonus_card_x,s_capture_frame->bonus_card_y,2,0,0);
            add_mapping(S3_TITLE_CARD_MAP,20,s_capture_frame->bonus_card_gfx,0,
                s_capture_frame->bonus_card_x+96,s_capture_frame->bonus_card_y,2,0,0);
        }
    }
    append_aiz_plane();
}

static void publish_sprites(void)
{
    int card=0;
    if((g_ram[0xF600]&127)==12 && !bonus_stage())
        for(unsigned o=0xB0DE;o<0xCAE2;o+=0x4A)
            if(scene_read32(0xFF0000u+o) && scene_read32(0xFF000Cu+o)==S3_TITLE_CARD_MAP) {
                card=1;break;
            }
    /* Demo inputs were authored for native object activation/visibility.
     * Showing an actor in a margin must not change its next simulated tick. */
    for(unsigned n=0;!native_demo() && n<s_visible_count;++n) {
        unsigned o=s_visible_objects[n],code=scene_read32(0xFF0000u+o);
        /* Waiting enemies load their DPLC only after the native SAT sees
         * them. During a returning title card those tiles still hold its
         * banner. Keep native visibility until the card retires, then let
         * the expanded view start the enemy normally. */
        if(code && !(card && code==S3_WAIT_ONSCREEN))write8(o+4,g_ram[o+4]|128);
    }
    s_build.serial=++s_serial;
    unsigned sat=s_build.scene==4 && s_build.competition_page?0x7880:0xF800;
    memcpy(s_build.sat,g_ram+sat,640);
    if(s_build.scene==4)memcpy(s_build.competition_sat,g_ram+(s_build.competition_page?0x7D80:0x7B00),640);
    SceneFrame *dst=&s_history[s_serial%3];
    dst->count=s_build.count;dst->serial=s_serial;dst->scene=s_build.scene;dst->title=s_build.title;
    copy_bonus_card(dst,&s_build);
    dst->world_tag=s_build.world_tag;
    dst->camera_x=s_build.camera_x;dst->camera_y=s_build.camera_y;
    dst->preview_knux=s_build.preview_knux;dst->aiz_super_pose=s_build.aiz_super_pose;
    dst->aiz_plane=s_build.aiz_plane;
    dst->aiz_join=s_build.aiz_join;
    memcpy(dst->competition_sat,s_build.competition_sat,sizeof dst->competition_sat);
    dst->competition_x=s_build.competition_x;dst->competition_y=s_build.competition_y;
    dst->competition_page=s_build.competition_page;
    memcpy(dst->ox,s_build.ox,sizeof dst->ox);memcpy(dst->oy,s_build.oy,sizeof dst->oy);memcpy(dst->code,s_build.code,sizeof dst->code);
    memcpy(dst->sat,s_build.sat,640);
    memcpy(dst->sprites,s_build.sprites,s_build.count*sizeof(SceneSprite));
}
/* Slots appends its rotating board after Render_Sprites. Publish it at
 * the same V-int which uploads the finished SAT, not the earlier actor queue. */
static int slots_shift8(int value)
{
    return value>=0?value/256:-((-value+255)/256);
}
static void capture_slots_board(void)
{
#ifndef SONIC3_STANDALONE
    int cx=ram16(0xEE78),cy=ram16(0xEE7C),angle=g_ram[0xE400]&0xFC;
    int sine=(int16_t)scene_read16(0x1D64u+(unsigned)angle*2u);
    int cosine=(int16_t)scene_read16(0x1D64u+(unsigned)(angle+64)*2u);
    /* The completed native matrix identifies this angle/camera publication.
     * Do not expose an incomplete board during initialization or a lag IRQ. */
    const unsigned corners[]={0,15,240,255};
    for(unsigned n=0;n<4;n++) {
        int x=(int)(corners[n]&15u)*24-cx%24-180;
        int y=(int)(corners[n]>>4)*24-cy%24-180;
        unsigned at=0x7800u+corners[n]*4u;
        if((int16_t)ram16(at)!=slots_shift8(cosine*x-sine*y) ||
           (int16_t)ram16(at+2)!=slots_shift8(sine*x+cosine*y))return;
    }
    int margin=(s_requested_width-320)/2;
    for(int row=32;row<64;row++)for(int col=32;col<64;col++) {
        unsigned kind=g_ram[0x3000+row*128+col];if(!kind || kind>19)continue;
        int dx=(col-cx/24)*24-cx%24-180,dy=(row-cy/24)*24-cy%24-180;
        int x=160+slots_shift8(cosine*dx-sine*dy),y=112+slots_shift8(sine*dx+cosine*dy);
        unsigned record=0x7000+kind*8,map=scene_read32(0xFF0000u+record)&0xFFFFFFu;
        if(!map)continue;
        unsigned frame=ram16(record+4),gfx=ram16(record+6);
        unsigned at=map+scene_read16(map+frame*2u),count=scene_read8(at++);
        if(count>32)continue;
        for(unsigned n=0;n<count && s_build.count<SCENE_SPRITES;n++,at+=5) {
            int py=y+(int8_t)scene_read8(at),px=x+(int8_t)scene_read8(at+4);
            unsigned size=scene_read8(at+1),w=(((size>>2)&3)+1)*8,h=((size&3)+1)*8;
            if(py>=224 || py+(int)h<=0 || px>=320+margin || px+(int)w<=-margin)continue;
            SceneSprite *q=&s_build.sprites[s_build.count++];
            q->x=px;q->y=py;q->attr=(uint16_t)(scene_read16(at+2)+gfx);
            q->size=(uint8_t)size;q->hud=0;q->slot=0;q->zoom=256;
        }
    }
#endif
}
static void capture_slots_vblank(void)
{
    if(enabled() && g_ram[0xF62A]==8 && bonus_stage() &&
       g_ram[0xFE10]==0x15 && s_build.scene==3) {
        unsigned queued=s_build.count;
        capture_slots_board();publish_sprites();
        /* Keep the queued actor base reusable after a restored wait or lag
         * IRQ. The published history owns the completed board pieces. */
        s_build.count=queued;
    }
}
#include "sonic3_lives.inc"

int s3_video_hook(uint32_t pc)
{
    if(pc==S3_HUD_LIFE_VALUE) {
        /* Change only the formatter argument, never the earned counter. */
        if(g_cpu.D[1]>=100)g_cpu.D[1]/=10;
        return 0;
    }
    if(ram16(S3_COMPETITION)) {
        if(enabled()) {
            if(pc==S3_CAPTURE)competition_capture_objects();
            else if(pc==S3_COMP_PUBLISH)publish_sprites();
        }
        return 0;
    }
    if(s_interpolation_enabled && !enabled()) {
        if(pc==S3_CAPTURE) {
            s_capture_frame=&s_ix_build;s_capture_readonly=1;capture_objects();
            s_capture_frame=&s_build;s_capture_readonly=0;
        } else if(pc==S3_PUBLISH)interpolation_native_publish();
    }
    if(pc==S3_LOAD_INIT && !g_ram[0xF76C]) {
        s_placement_base=s_placement_count=0;s_loader_active=0;
    }
    /* Earlier activation changes age-based platform phases and the recorded
     * demo's collisions. Keep native loading/rings/culling while continuing
     * to capture and draw the same original-size actors across the canvas. */
    if(native_demo() && pc!=S3_CAPTURE && pc!=S3_PUBLISH)return 0;
    /* A background replacement needs host composition, while native-size
     * gameplay keeps the cartridge's loader, ring ranges and culling. */
    if(pc==S3_LOAD_UPDATE && gameplay() && (expanded_world() || s_loader_active) && g_ram[0xFE10]<19) {
        spawn_scene();return 1;
    }
    if(!enabled() || ram16(S3_COMPETITION))return 0;
    switch(pc) {
    case S3_LOAD_INIT:return 0;
    case S3_CAPTURE:capture_objects();return 0;
    case S3_PUBLISH:publish_sprites();return 0;
    case S3_RING_RANGE:
        if(gameplay() && expanded_world()) {g_cpu.A[1]=rings_base()+4;g_cpu.A[2]=rings_end();}
        return 0;
    case S3_RING_STATUS:
        if(gameplay() && expanded_world())g_cpu.A[4]=0xFFFFE702u;
        return 0;
    default:
        if(gameplay() && expanded_world()) {
            int x=(uint16_t)((uint16_t)g_cpu.D[0]+ram16(0xF7DA));
            int lo,hi;activation_bounds(ram16(0xEE78),s_requested_width,&lo,&hi);
            /* These platform families compare against their saved range,
             * rather than the usual immediate $280. Without the matching
             * hook, early wide activation deletes them while the host loader
             * still considers their placement loaded. Keep their authored
             * range/physics intact and only translate this cull comparison. */
            unsigned limit=640;
            if(pc==S3_MOVING_PLATFORM_CULL || pc==S3_FLOATING_PLATFORM_CULL ||
               pc==S3_SWING_PLATFORM_CULL) {
                limit=ram16(g_cpu.A[0]+0x42);
                /* Long sweeps bias their saved anchor by $100 and extend
                 * the original range by the same amount. */
                if(limit>640)hi+=(int)limit-640;
            }
            g_cpu.D[0]=(g_cpu.D[0]&0xFFFF0000u)|
                ((x<lo || x>=hi)?limit+1u:limit/2u);
        }
        return 0;
    }
}


#include "sonic3_blue_spheres.inc"

static uint8_t *s_priority;
static int s_priority_capacity;
static S3VideoActorOverlay s_actor_overlay;
static int s_actor_interpolation_blocked;
void s3_video_set_actor_interpolation_blocked(int blocked) { s_actor_interpolation_blocked=blocked!=0; }
void s3_video_set_actor_overlay(S3VideoActorOverlay draw) { s_actor_overlay=draw; }
static void select_scene(const GVDP *v,int retain_level)
{
    /* Bonus selection can precede the last outgoing level SAT DMA. Prefer
     * that matching level publication before holding the previous pose. */
    if(retain_level && !bonus_stage() && !demo_level_exit() &&
       s_display_frame.serial && s_display_frame.scene==1) {
        s_display=&s_display_frame;++s_scene_holds;return;
    }
    s_display=NULL;
    unsigned sat=(v->reg[5]&127u)<<9;
    /* Keep the front buffer's publication alive even if the producer cycles
     * through every history slot before the next native sprite DMA. */
    for(unsigned n=0;n<4;++n) {
        const SceneFrame *p=n==3?&s_display_frame:&s_history[n];
        if(retain_level && p->scene!=1)continue;
        if((p->scene==1 || p->scene==4) && s_upload_serial && p->serial>s_upload_serial)continue;
        if(!p->serial || (s_display && p->serial<s_display->serial))continue;
        int matches=1;
        for(unsigned k=0;k<640;++k)
            if(p->sat[k]!=v->vram[(sat+k)&65535u]) {matches=0;break;}
        if(matches)s_display=p;
    }
    if(!s_display) {
        ++s_scene_misses;
        int scene=retain_level?1:ram16(S3_COMPETITION)?4:bonus_stage()?3:level_ready()?1:(g_ram[0xF600]&127)==0x34?2:0;
        /* An in-progress native SAT is not a custom-scene publication. Keep
         * the completed scene until its replacement is ready, without
         * flashing the native centered HUD or dropping host-only rings. */
        if(scene && s_display_frame.serial && s_display_frame.scene==scene) {
            s_display=&s_display_frame;++s_scene_holds;
        }
    }
    /* Keep the selected front buffer immutable for all displayed scanlines. */
    if(s_display && s_display!=&s_display_frame) {
        s_display_frame.count=s_display->count;s_display_frame.scene=s_display->scene;
        copy_bonus_card(&s_display_frame,s_display);
        s_display_frame.title=s_display->title;
        s_display_frame.world_tag=s_display->world_tag;
        s_display_frame.serial=s_display->serial;
        s_display_frame.camera_x=s_display->camera_x;s_display_frame.camera_y=s_display->camera_y;
        s_display_frame.preview_knux=s_display->preview_knux;s_display_frame.aiz_super_pose=s_display->aiz_super_pose;
        s_display_frame.aiz_plane=s_display->aiz_plane;
        s_display_frame.aiz_join=s_display->aiz_join;
        memcpy(s_display_frame.competition_sat,s_display->competition_sat,sizeof s_display_frame.competition_sat);
        s_display_frame.competition_x=s_display->competition_x;s_display_frame.competition_y=s_display->competition_y;
        s_display_frame.competition_page=s_display->competition_page;
        memcpy(s_display_frame.ox,s_display->ox,sizeof s_display_frame.ox);memcpy(s_display_frame.oy,s_display->oy,sizeof s_display_frame.oy);memcpy(s_display_frame.code,s_display->code,sizeof s_display_frame.code);
        memcpy(s_display_frame.sat,s_display->sat,sizeof s_display_frame.sat);
        memcpy(s_display_frame.sprites,s_display->sprites,s_display->count*sizeof(SceneSprite));
        s_display=&s_display_frame;
    }
}
/* The original intro changes to Super mappings while still outside its
 * 320px view, before enabling the Super DPLC bank. Extend that hidden pose
 * with its matching original ROM art; keep the native viewport authoritative. */
static uint8_t aiz_super_pixel(unsigned pose,unsigned tile,int x,int y)
{
    if(pose<1 || pose>2 || tile<0x680u)return 0;
    unsigned table=S3_SUPER_PLC,frame=32+pose;
    unsigned p=table+((g_rom[table+frame*2]<<8)|g_rom[table+frame*2+1]);
    unsigned count=(g_rom[p]<<8)|g_rom[p+1],local=tile-0x680u;p+=2;
    if(count>32)return 0;
    for(unsigned i=0;i<count;i++,p+=2) {
        unsigned entry=(g_rom[p]<<8)|g_rom[p+1],tiles=(entry>>12)+1;
        if(local<tiles) {
            unsigned address=0x100000u+((entry&4095u)+local)*32u+(unsigned)y*4u+(unsigned)x/2u;
            unsigned byte=g_rom[address];return (uint8_t)((x&1)?byte&15u:byte>>4);
        }
        local-=tiles;
    }
    return 0;
}
/* AIZ loads Knuckles at tile4DA after the splash leaves the native view.
 * That reuses sixteen tiles of its final wave pose. The wider margin still
 * sees the original wave, so sample its original ROM art there. */
static size_t art_nem(const uint8_t *src,size_t size,uint8_t *out,size_t capacity)
{
    uint8_t runs[512]={0},colors[512]={0};
    if(size<3)return 0;
    size_t bytes=(((unsigned)src[0]<<8|src[1])&0x7FFFu)*32u;
    if(!bytes || bytes>capacity || size>UINT_MAX/8u)return 0;
    size_t p=2;unsigned nib=0;
    while(p<size && src[p]!=255) {
        unsigned entry=src[p++];
        if(entry&128){nib=entry&15;continue;}
        unsigned length=entry&15,run=((entry>>4)&7)+1;
        if(!length || length>8 || p>=size || src[p]>=(1u<<length))return 0;
        unsigned index=(1u<<length)|src[p++];runs[index]=(uint8_t)run;colors[index]=(uint8_t)nib;
    }
    if(p>=size)return 0;
    MenuBits bits={src,(unsigned)(p+1)*8,(unsigned)size*8,0};size_t used=0;
    memset(out,0,bytes);
    while(used<bytes*2 && !bits.error) {
        unsigned code=0,run=0,color=0;
        for(unsigned length=1;length<=8 && !bits.error;length++) {
            code=(code<<1)|menu_bits(&bits,1);
            if(length==6 && code==63){unsigned v=menu_bits(&bits,7);run=(v>>4)+1;color=v&15;break;}
            unsigned index=(1u<<length)|code;
            if(runs[index]){run=runs[index];color=colors[index];break;}
        }
        if(bits.error || !run || run>bytes*2-used)return 0;
        while(run--){out[used/2]|=(uint8_t)(color<<((used&1)?0:4));++used;}
    }
    if(bits.error || used!=bytes*2)return 0;
    if(src[0]&128)for(size_t row=1;row<bytes/4;row++)
        for(unsigned byte=0;byte<4;byte++)out[row*4+byte]^=out[(row-1)*4+byte];
    return bytes;
}
static uint8_t s_aiz_wave_art[0x2B00];
static int s_aiz_wave_art_ready,s_aiz_wave_art_attempted;
static int load_aiz_wave_art(void)
{
    if(!s_aiz_wave_art_attempted) {
        s_aiz_wave_art_attempted=1;
        s_aiz_wave_art_ready=art_nem(g_rom+S3_ART_BANK+0x1481A0,0x540,
            s_aiz_wave_art,sizeof s_aiz_wave_art)==sizeof s_aiz_wave_art;
    }
    return s_aiz_wave_art_ready;
}
static uint8_t aiz_wave_pixel(unsigned tile,int x,int y)
{
    if(tile<0x3D1 || tile>=0x3D1+sizeof s_aiz_wave_art/32)return 0;
    unsigned byte=s_aiz_wave_art[(tile-0x3D1)*32u+(unsigned)y*4u+(unsigned)x/2u];
    return (uint8_t)((x&1)?byte&15:byte>>4);
}
static uint8_t s_aiz_plane_art[0x1100];
static int s_aiz_plane_art_ready;
static uint8_t aiz_plane_pixel(unsigned tile,int x,int y)
{
    if(tile<0x529 || tile>=0x529+sizeof s_aiz_plane_art/32)return 0;
    if(!s_aiz_plane_art_ready) {
        if(art_kosm(S3_ART_BANK+0x182624,0x7A2,s_aiz_plane_art,sizeof s_aiz_plane_art)!=sizeof s_aiz_plane_art)return 0;
        s_aiz_plane_art_ready=1;
    }
    unsigned byte=s_aiz_plane_art[(tile-0x529)*32u+(unsigned)y*4u+(unsigned)x/2u];
    return (uint8_t)((x&1)?byte&15:byte>>4);
}
/* The title card temporarily borrows Monkey Dude's tiles during a bonus
 * return. Its culled native objects remain visible in the extended view.
 * Bind their original mappings to their own ROM art outside that viewport. */
static uint8_t s_aiz_monkey_art[0x2E0];
static int s_aiz_monkey_ready,s_aiz_monkey_attempted;
static int load_aiz_monkey_art(void)
{
    if(!s_aiz_monkey_attempted) {
        s_aiz_monkey_attempted=1;
        s_aiz_monkey_ready=art_kosm(S3_ART_BANK+0x16800C,0x1F2,
            s_aiz_monkey_art,sizeof s_aiz_monkey_art)==sizeof s_aiz_monkey_art;
    }
    return s_aiz_monkey_ready;
}
static uint8_t aiz_monkey_pixel(unsigned attr,int x,int y)
{
    unsigned tile=attr&2047u;
    if(tile<0x548 || tile>=0x548+sizeof s_aiz_monkey_art/32)return 0;
    unsigned byte=s_aiz_monkey_art[(tile-0x548)*32u+(unsigned)y*4u+(unsigned)x/2u];
    unsigned p=(x&1)?byte&15u:byte>>4;
    return p?(uint8_t)(p|((attr>>9)&48u)):0;
}
/* Extra visible objects can outlive their shared native VRAM reservation.
 * Bind only these known mappings to their original ROM patterns. The guest
 * PLC queues, object lifetimes, palettes and private atlases stay authoritative. */
typedef struct { unsigned address,compressed,bytes,tile,kosm; } NativeArt;
static const NativeArt s_native_art[]={
    {S3_EXPLOSION_ART,0x3A5,0x880,0x5A0,0},
    {S3_ART_BANK+0x16E0C4u,0x212,0x3E0,0x530,1},
    {S3_ART_BANK+0x17060Eu,0xA2,0x160,0x574,1},
    {S3_SPRING_ART,0x2EF,0x600,0x494,0},
    {S3_CAPSULE_ART,0x4A3,0x8C0,0x494,0}
};
static struct { uint8_t pixels[0x8C0];int attempted,ready; } s_native_art_cache[5];
static int native_art_pixel(unsigned owner,unsigned attr,int x,int y,uint8_t *pixel)
{
    if(!owner || owner>sizeof s_native_art/sizeof *s_native_art)return 0;
    const NativeArt *a=&s_native_art[owner-1];unsigned tile=attr&2047u;
    if(tile<a->tile || tile>=a->tile+a->bytes/32)return 0;
    if(!s_native_art_cache[owner-1].attempted) {
        s_native_art_cache[owner-1].attempted=1;
        uint8_t *dst=s_native_art_cache[owner-1].pixels;
        size_t bytes=a->kosm?art_kosm(a->address,a->compressed,dst,a->bytes):
            art_nem(g_rom+a->address,a->compressed,dst,a->bytes);
        s_native_art_cache[owner-1].ready=bytes==a->bytes;
    }
    if(!s_native_art_cache[owner-1].ready)return 0;
    unsigned b=s_native_art_cache[owner-1].pixels[(tile-a->tile)*32u+(unsigned)y*4u+(unsigned)x/2u];
    unsigned p=(x&1)?b&15u:b>>4;
    *pixel=p?(uint8_t)(p|((attr>>9)&48u)):0;return 1;
}
static int title_card_black(const GVDP *v)
{
    if(!s_display || s_display->scene || v->reg[16]!=1)return 0;
    int card=0;
    for(unsigned n=0;n<s_display->count;++n)
        if(s_display->sprites[n].hud>=6 && s_display->sprites[n].hud<=9)card=1;
    if(!card)return 0;
    unsigned a=(v->reg[2]&56u)<<10,b=(v->reg[4]&7u)<<13;
    if(a==b)return 0;
    for(unsigned i=0;i<32;++i)if(v->vram[i])return 0;
    for(unsigned i=0;i<4096;++i)
        if(v->vram[(a+i)&65535u] || v->vram[(b+i)&65535u])return 0;
    return 1;
}
/* The stage thumbnail borrows the character bank after its card leaves
 * the hardware viewport. Margin portraits retain their original menu colours.
 * The unchanged Sonic white entry identifies both original B/G/R fades. */
static uint32_t competition_character_color(const GVDP *v,unsigned pixel,unsigned attr)
{
    if(pixel<16 || pixel>=64)return genesis_dac_cram_to_argb(v->cram[pixel&63],GENESIS_DAC_NORMAL);
    unsigned color=scene_read16(S3_ART_BANK+0xA55E + (pixel-16)*2);
    unsigned white=v->cram[17],steps=((white&14)+((white>>4)&14)+((white>>8)&14))/2;
    int incoming=((white>>8)&14)>=(white&14);
    if(!incoming)steps=21-steps;
    unsigned faded=0;
    for(int shift=8;shift>=0;shift-=4) {
        unsigned part=(color>>(unsigned)shift)&14,n=part/2;
        unsigned used=steps<n?steps:n;steps-=used;
        faded|=(incoming?used*2:part-used*2)<<(unsigned)shift;
    }
    unsigned shade=(v->reg[12]&8u) && !(attr&0x8000u)?GENESIS_DAC_SHADOW:GENESIS_DAC_NORMAL;
    return genesis_dac_cram_to_argb((uint16_t)faded,(int)shade);
}
/* The ending's artificial camera rise belongs to a finite stage. Extra
 * gameplay terrain and decorations have no authored role in that shot. Keep
 * the real ending pose as it enters the wider view and retain the HUD. */
static int ending_cinematic(void)
{
#ifdef SONIC3_STANDALONE
    if(!s_frame_level || s_frame_zone!=6 || s_frame_act!=1 || s_frame_bg_event<16)return 0;
    unsigned o=((unsigned)s_world_frame[0xFAA4]<<8)|s_world_frame[0xFAA5];
    if(o>=0xB000 && o<0xCFCC && !((o-0xB000)%0x4A)) {
        unsigned pc=((unsigned)s_world_frame[o]<<24)|((unsigned)s_world_frame[o+1]<<16)|
            ((unsigned)s_world_frame[o+2]<<8)|s_world_frame[o+3];
        if(pc>=0x51148 && pc<=0x511CC)return 1;
    }
    /* The pose retires that controller while the camera is still rising. */
    if(s_display)for(unsigned n=0;n<s_display->count;++n)
        if((s_display->sprites[n].slot>>8)==SPRITE_ENDING_POSE)return 1;
#endif
    return 0;
}
static void draw_scene_sprites(const GVDP *v,int line,uint32_t *out,int width,
                               int origin,const uint32_t *palette)
{
    if(!s_display || s_display->scene==4)return;
    int cinematic=ending_cinematic();
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
        int dx=0,dy=0;
        if(world && s_interpolating)presentation_object_delta(s,&dx,&dy);
        if(s->hud==5 && s_interpolating)presentation_aiz_plane_delta(&dx,&dy);
        int y=s->y+(world?s_display->camera_y-s_camera_y:0)+dy;
        if(line<y || line>=y+drawn_height)continue;
        int base=s->x+(s->hud==1?0:s->hud>=2?(width-320)/2:origin)+dx;
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
        for(int i=0;i<drawn_width;++i) {
            int x=base+i;
            if(x<0 || x>=width || (s_priority[x]&2))continue;
            if(cinematic && world && (x<origin || x>=origin+320) &&
               (s->slot>>8)!=SPRITE_ENDING_POSE)continue;
            if((s->slot>>8)==7 && x<origin+320)continue;
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
            } else if(s_frame_level && !s->hud && (s->slot>>8) && native_art_pixel(s->slot>>8,cell,ix&7,iy&7,&p)) {
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
static int attributes_differ(const GVDP *v,uint16_t a,uint16_t b,int x,int y)
{
    if(a==b)return 0;
    /* Compare visible pixels/priority, not irrelevant transparent-tile flags. */
    for(int i=0;i<8;++i) {
        uint8_t ap=pattern_pixel(v,a,x+i,y),bp=pattern_pixel(v,b,x+i,y);
        if(ap!=bp || (ap && ((a^b)&0x8000)))return 1;
    }
    return 0;
}
/* The native level-entry fade adds one B, then G, then R step per tick.
 * Its white entry exposes the installed step, even when main RAM has already
 * prepared the following palette. Cached art must run that same operation. */
static uint16_t scene_art_color(unsigned color,const GVDP *v)
{
    if((int16_t)((s_world_frame[0xEE50]<<8)|s_world_frame[0xEE51])>0) {
        unsigned white=v->cram[17],steps=((white&14)+((white>>4)&14)+((white>>8)&14))/2;
        unsigned faded=0;
        for(int shift=8;shift>=0;shift-=4) {
            unsigned target=(color>>(unsigned)shift)&14,part=steps*2;
            if(part>target)part=target;
            faded|=part<<(unsigned)shift;steps-=part/2;
        }
        return (uint16_t)faded;
    }
    unsigned white=v->cram[17],sum=(white&14)+((white>>4)&14)+((white>>8)&14);
    if(sum<42&&!v->cram[16]){
        unsigned steps=(42-sum)/2,faded=0;
        /* Pal_DecColor removes red, green, then blue independently for
         * every entry. Cached margin colors must follow those same steps. */
        for(unsigned shift=0;shift<12;shift+=4){
            unsigned target=(color>>shift)&14,remove=steps*2;
            if(remove>target)remove=target;
            faded|=(target-remove)<<shift;steps-=remove/2;
        }
        return (uint16_t)faded;
    }
    unsigned faded=0;
    for(unsigned shift=0;shift<12;shift+=4) {
        int lo=(v->cram[16]>>shift)&14,hi=(v->cram[17]>>shift)&14;
        int c=lo+(int)((color>>shift)&14)*(hi-lo)/14;
        faded|=(unsigned)c<<shift;
    }
    return (uint16_t)faded;
}
static uint16_t restored_art_color(unsigned color,const GVDP *v)
{
    /* The donor's stage entry 16 can be nonblack (FBZ is $0462), so use
     * character black with stage white. The character palette reloads
     * before the stage after death and must not reveal the donor early. */
    unsigned white=v->cram[17],black=v->cram[0],faded=0;
    unsigned sum=(white&14)+((white>>4)&14)+((white>>8)&14);
    if((int16_t)((s_world_frame[0xEE50]<<8)|s_world_frame[0xEE51])>0){
        unsigned steps=sum/2;
        for(int shift=8;shift>=0;shift-=4){
            unsigned target=(color>>(unsigned)shift)&14,part=steps*2;
            if(part>target)part=target;
            faded|=part<<(unsigned)shift;steps-=part/2;
        }
    } else if(sum<42 && !black){
        unsigned steps=(42-sum)/2;
        for(unsigned shift=0;shift<12;shift+=4){
            unsigned target=(color>>shift)&14,remove=steps*2;
            if(remove>target)remove=target;
            faded|=(target-remove)<<shift;steps-=remove/2;
        }
    } else for(unsigned shift=0;shift<12;shift+=4){
        int lo=(black>>shift)&14,hi=(white>>shift)&14;
        faded|=(unsigned)(lo+(int)((color>>shift)&14)*(hi-lo)/14)<<shift;
    }
    return (uint16_t)faded;
}
static int aiz_tree_color(int wx,int wy,int nx,int nw,uint8_t pixel)
{
    /* Native AIZ1_Resize changes this entry from $0004 to sky $0C02 at
     * camera $2D80 ($0E40 for Knuckles). Only the extra viewport still sees
     * the original finite tree by then. Keep its authored palette ownership. */
    return !s_frame_zone && !s_frame_act && s_frame_bg_event==8 &&
        s_frame_fg_x>=0x2D80 && (nx<0 || nx>=nw) && pixel==47 &&
        wx>=0x2C80 && wx<0x2D80 && wy>=0x280 && wy<0x480;
}
static void aiz_palette(const AizArt *art,const GVDP *v,uint32_t *out)
{
    /* Follow fades without borrowing the cutscene's unrelated colour banks.
     * AIZ's common black/white entries also cover fades to/from white. */
    int installed=art==&s_aiz_intro && !s_frame_bg_event && s_frame_fg_x<0x1300;
    for(unsigned i=0;i<64;++i) {
        /* This is the installed intro bank, including its native component
         * fade and palette cycling. Scaling those already-faded values
         * would darken the extension twice. Other banks keep their own art. */
        if(installed || i<16) {
            out[i]=genesis_dac_cram_to_argb(v->cram[i],GENESIS_DAC_NORMAL);continue;
        }
        out[i]=genesis_dac_cram_to_argb(scene_art_color(art->colors[i-16],v),GENESIS_DAC_NORMAL);
    }
}

static unsigned menu_plane(uint16_t attr,unsigned pixel)
{ return pixel?(pixel|64u|((attr&0x8000u)?128u:0u)):0; }
static void render_menu(const GVDP *v,int line,int nw,uint32_t *out,int width,
                        const uint32_t *palette,const GVDPRasterRow *raster)
{
    int origin=(width-nw)/2;
    unsigned hsbase=(v->reg[13]&63u)<<10;
    int hs_a=(int16_t)vram16(v,hsbase);
    int hs_b=(int16_t)vram16(v,hsbase+2);
    int clean_save=s_frame_menu==MENU_SAVE && load_menu_map();
    for(int x=0;x<width;++x) {
        int nx=x-origin,sx=(int)((int64_t)x*nw/width);
        unsigned a=0,b=0,sp=0,opaque=0,high=0,shadow_op=0,hilite_op=0;
        if(s_frame_menu==MENU_SAVE) {
            /* The decoration fills the canvas. Every card and portrait stays
             * at its original scale, including the full 1024px carousel. */
            if(clean_save && line<224) {
                uint16_t attr=s_menu_map[line/8*40+sx/8];
                b=menu_plane(attr,pattern_pixel(v,attr,sx,line));
            } else b=raster->plane_b[sx];
            int bx=nx-hs_b;
            if(bx>=0 && bx<1024) {
                uint16_t attr=plane_attr(v,0xC000,bx,line+(v->vsram[1]&1023));
                if((attr&2047u)>=0x29Fu) {
                    unsigned p=pattern_pixel(v,attr,bx,line+(v->vsram[1]&1023));
                    if(p)b=menu_plane(attr,p);
                }
            }
        } else if(s_frame_menu!=MENU_TITLE_ANIMATION) {
            b=raster->plane_b[sx];
        }
        /* Read the finite UI's actual SHI coverage. Extending a moving
         * edge pixel would turn a card's shadow into an infinite band. */
        if(s_frame_menu==MENU_COMPETITION && (nx<0 || nx>=nw)) {
            a=128u; /* Unobscured scenery outside the finite UI is normal. */
            int ax=nx-hs_a,ay=line+(v->vsram[0]&1023);
            /* The stage selector also stores offscreen card columns in its
             * finite plane A table. Expose them without wrapping that UI. */
            if(ax>=0 && ax<plane_tiles(v->reg[16]&3)*8) {
                unsigned base_a=(v->reg[2]&56u)<<10;
                uint16_t attr=plane_attr(v,base_a,ax,ay);
                unsigned p=pattern_pixel(v,attr,ax,ay);
                a=menu_plane(attr,p)|((attr&0x8000u)?128u:0u);
            }
        }
        if(nx>=0 && nx<nw) {
            a=raster->plane_a[nx];
            /* SaveScreen's full-screen Window is the same unobscured menu
             * scenery reconstructed above. Keeping it centered over the
             * widened copy repeats the mural and makes a vertical seam. */
            if(clean_save) {
                uint16_t attr=vram16(v,0xE000u+(unsigned)(line/8*64+nx/8)*2u);
                if((attr&2047u)<0x29Fu)a=0;
            }
            if(s_frame_menu==MENU_TITLE_ANIMATION)b=raster->plane_b[nx];
            sp=raster->sprite[nx];opaque=raster->opaque[nx];high=raster->high[nx];
            shadow_op=raster->shadow_op[nx];hilite_op=raster->hilite_op[nx];
        }
        unsigned ah=(a&192u)==192u,bh=(b&192u)==192u,sh=high&&opaque;
        unsigned pixel=sh?sp:ah?a&63u:bh?b&63u:opaque?sp:
            (a&64u)?a&63u:(b&64u)?b&63u:raster->backdrop;
        unsigned shade=GENESIS_DAC_NORMAL;
        if(raster->shadow_mode && pixel<64) {
            int dark=!((a|b)&128u)&&!sh;
            if(shadow_op)dark=1;
            if(hilite_op){if(dark)dark=0;else shade=GENESIS_DAC_HIGHLIGHT;}
            if(dark)shade=GENESIS_DAC_SHADOW;
        }
        out[x]=pixel>=GVDP_HOST_PALETTE_BASE?raster->host_palette[pixel-GVDP_HOST_PALETTE_BASE]:
            shade==GENESIS_DAC_NORMAL?palette[pixel]:
            genesis_dac_cram_to_argb(v->cram[pixel],(int)shade);
        s_priority[x]=(uint8_t)((ah||bh)|(opaque?2u:0u));
    }
    draw_scene_sprites(v,line,out,width,origin,palette);
    if((s_frame_menu==MENU_TITLE || s_frame_menu==MENU_TITLE_ANIMATION) && s_actor_overlay)
        s_actor_overlay(v,line,out,width,origin,palette,s_priority,s_camera,s_camera_y);
    if(!line){++s_frames;s_native_x=origin;}
}

#include "sonic3_competition.inc"

static int hcz2_panorama_position(int line,int *scroll,int *y)
{
    /* HCZ2_Deform and ApplyDeformation use the original cartridge's band
     * lengths/index. Its normal panorama is ready in the layout before the
     * moving-wall name table is cleared and refreshed. */
    unsigned table=S3_ART_BANK+0x3C7D2u,index=table+46u;
    int shake=0;
#ifndef SONIC3_STANDALONE
    shake=(int16_t)(((unsigned)s_world_frame[0xEECE]<<8)|s_world_frame[0xEECF]);
#endif
    int cy=s_frame_fg_y-shake;
    *y=(cy>=0?cy/4:-((-cy+3)/4))+shake+line;
    int row=*y;unsigned band;
    for(band=0;band<23;band++){
        unsigned height=scene_read16(table+band*2u);
        if(!height)return 0;
        if(row<(int)height)break;
        row-=(int)height;
    }
    if(band==23)return 0;
    for(unsigned group=0;group<7;group++){
        unsigned count=scene_read8(index++)+1u;
        if(count>23)return 0;
        for(unsigned n=0;n<count;n++)if(scene_read8(index++)==band*2u){
            *scroll=s_frame_fg_x*(int)(8u-group)/16;return 1;
        }
    }
    return 0;
}
static void hcz2_panorama_art(unsigned attr,int y,GenesisTileRow *row)
{
    /* AnimateTiles_HCZ2 does not install these parallax strips until the
     * normal scroll differences change. Build the same row from cartridge
     * art for the early margin, without changing VRAM or its DMA queue. */
    static const unsigned tiles[]={0x2D2,0x2D6,0x2DE,0x2EE,0x31E};
    static const unsigned source[]={0xA87A0,0xA8BA0,0xA93A0,0xAA3A0};
    unsigned tile=attr&2047u,bank;
    if(tile<tiles[0] || tile>=tiles[4])return;
    bank=0;while(tile>=tiles[bank+1])++bank;
    unsigned bytes=(tiles[bank+1]-tiles[bank])*32u;
    int cx=s_frame_fg_x,base=cx*4/16;
    unsigned phase=(unsigned)(cx*(bank<2?3:2)/16-base)&(bank==3?63u:31u);
    unsigned low=bank==3?(0u-phase)&7u:phase&7u;
    unsigned rotation=bank==3?((phase-1u)&56u)*(bytes/64u):(phase&24u)*(bytes/32u);
    unsigned fy=(unsigned)y&7u;if(attr&0x1000u)fy^=7u;
    unsigned offset=((tile-tiles[bank])*32u+fy*4u+rotation)%bytes;
    unsigned at=S3_ART_BANK+source[bank]+low*bytes+offset;
    row->bits=(uint32_t)scene_read8(at)<<24|(uint32_t)scene_read8(at+1)<<16|
        (uint32_t)scene_read8(at+2)<<8|scene_read8(at+3);
}
static void render_scanline_base(const GVDP *v, int line, const uint32_t *native, int nw,
                            uint32_t *out, int width,
                            GameVideoNativeRow native_row, void *native_ctx,
                            const GVDPRasterRow *raster,int *hud_origin)
{
    *hud_origin=(width-nw)/2;
    /* Publish one immutable art/scroll snapshot per frame. Palettes and the
     * scripted foreground VSRAM switch remain live across native HBlank. */
    unsigned live_fg_scroll=presentation_fg_scroll(line,v->vsram[0]);
    uint32_t palette[64],shadow[64],intro_palette[64],forest_palette[64];
    int uniform_palette=1;
    for(int i=0;i<64;++i) {
        palette[i]=genesis_dac_cram_to_argb(presentation_cram(line,i,v->cram[i]),GENESIS_DAC_NORMAL);
        shadow[i]=genesis_dac_cram_to_argb(presentation_cram(line,i,v->cram[i]),GENESIS_DAC_SHADOW);
        if(palette[i]!=palette[0])uniform_palette=0;
    }
    if(line==0 && !s_interpolating) {
        s_handoff_palette_capture=0;
        s_ix_overlay_seen=0;
        /* A mode-changing exit can publish a menu-tagged copy of the same
         * SAT before its old level fades. Keep the installed level through
         * that fade while its plane configuration remains unchanged; a flat
         * palette or disabled display retires it before the next screen. */
        int same_planes=v->reg[2]==s_video_frame.reg[2] && v->reg[4]==s_video_frame.reg[4] &&
            v->reg[16]==s_video_frame.reg[16];
        int previous_flat=1;
        for(unsigned i=1;i<64;i++)if((s_video_frame.cram[i]&0xEEE)!=(s_video_frame.cram[0]&0xEEE))
            previous_flat=0;
        int retain_bonus=s_frame_bonus && !bonus_stage() && !uniform_palette &&
            (v->reg[1]&64) && same_planes;
        int retain_level=s_frame_level && !level_ready() && !uniform_palette && !previous_flat &&
            (v->reg[1]&64) &&
            (!((g_ram[0xF600]&127)==0x34 || (g_ram[0xF600]&127)==0x48) || !(v->reg[12]&8)) &&
            same_planes;
        select_scene(v,retain_level);
        memcpy(&s_video_frame,v,sizeof s_video_frame);
        /* Zone selection precedes the outgoing level's native palette fade.
         * Conversely, a bonus exit can select the next zone before its own
         * image fades. Retire either installed scene at the flat palette or
         * video-layout change, never at the early WRAM zone assignment. */
        /* Level select assigns the incoming zone before fading its old
         * planes. Its installed word pattern still owns that entire fade. */
        s_frame_bonus=!retain_level && !uniform_palette && !level_select_layout(v) &&
            (bonus_stage() || retain_bonus);
        /* HUD visibility is not level readiness: AIZ hides it for the whole
         * landing/emerald cutscene while displaying the real stage planes. */
        /* Special-stage mode is selected before the outgoing level fades.
         * Its projection starts when native SHI video is installed; the exit
         * likewise retains that projection until the video mode ends. */
        s_frame_special=(v->reg[12]&8) && (v->reg[1]&64) &&
            ((g_ram[0xF600]&127)==0x34 || (g_ram[0xF600]&127)==0x48 || s_frame_special);
        if(s_frame_special && s_mode!=VIDEO_OFF && width>320)ss_begin(&s_video_frame,width);
        else ss_ready=0;
        if(!s_frame_special)ss_latched=0;
        /* During $8C, readiness also needs the incoming level's installed
         * publication. A zone assignment after a bonus fade can still own
         * the old bonus SAT even with stale level/HUD RAM present. */
        /* Load_Level can assign the incoming zone while the outgoing SAT,
         * HUD-ready byte and world still belong to the previous level. Only
         * its matching publication can authorize the new world snapshot. */
        int ready=level_ready() && (!(g_ram[0xF600]&128) ||
            (s_display && s_display->scene==1)) &&
            (!s_display || !s_display->world_tag ||
             s_display->world_tag==(unsigned)ram16(0xFE10)+1u);
        /* The title entry clears Current_zone before fading the old level.
         * Keep its published world/camera as well as its composition until
         * the native SAT is replaced. CRAM continues to follow every fade. */
        /* A completed fade ends retention even if its last HUD/list still
         * qualifies as ready. New character colors must not revive that
         * world on the next tick while level RAM is being cleared. */
        int hold_level=!ready && !uniform_palette && !previous_flat && s_frame_level && s_display && s_display->scene==1 &&
            (v->reg[1]&64) && !s_frame_special && !s_frame_bonus &&
            !file_select_layout(v) && !competition_layout(v);
        s_frame_level=!s_frame_bonus && (ready || hold_level);
        if(!hold_level && !retain_bonus) {
            memcpy(s_world_frame,g_ram,sizeof s_world_frame);
            if(s_world_publish)s_world_publish(s_world_frame);
            s_frame_zone=g_ram[0xFE10];
            s_frame_act=g_ram[0xFE11];s_frame_bg_event=ram16(0xEEC2);
            s_frame_tree_reveal=ram16(0xEEC4);
            if(s_frame_level && !s_frame_zone && !s_frame_act)load_aiz_art();
            /* The intro queues the main-level block definitions before its
             * two-row-at-a-time name-table refresh. Keep the outgoing background
             * definitions intact while Kosinski is overwriting their RAM. */
            int replacing_intro=s_frame_level && s_frame_zone==0 && s_frame_act==0 &&
                s_frame_bg_event==0 && ram16(0xEEC6);
            if(!replacing_intro)memcpy(s_background_frame,s_world_frame,sizeof s_background_frame);
            s_frame_fg_x=ram16(0xEE80);s_frame_fg_y=ram16(0xEE84);s_frame_bg_y=ram16(0xEE90);
            /* Draw_BG records each completed coarse draw position in the
             * second word. The first word can already hold the next deformation
             * while the game fiber is still preparing its tile-column update. */
            s_frame_bg_band[0]=(int16_t)ram16(0xA802);
            s_frame_bg_band[1]=(int16_t)ram16(0xA806);
            if(s_bg_draw_ready && s_bg_draw_zone==ram16(0xFE10)) {
                s_frame_bg_band[0]=s_bg_draw_at_vblank[0];
                s_frame_bg_band[1]=s_bg_draw_at_vblank[1];
            }
        }
        s_width=width;s_requested_width=width;
        s_terrain_checks=s_terrain_errors=s_bg_checks=s_bg_errors=s_bg_unstreamed=0;
        s_uninitialized_scroll_lines=s_terrain_unstreamed=0;
        s_bad_x=s_bad_y=-1;s_bad_attr=s_bad_expected=0;
        s_bg_bad_x=s_bg_bad_y=-1;s_bg_bad_a=s_bg_bad_expected=0;
        if(width>s_priority_capacity) {
            uint8_t *p=(uint8_t *)realloc(s_priority,(size_t)width);
            if(p){s_priority=p;s_priority_capacity=width;}
        }
        s_frame_menu=MENU_NONE;
        if(!s_frame_level && !s_frame_special && !s_frame_bonus && !level_select_layout(v)) {
            if(title_card_black(v))s_frame_menu=MENU_TITLE_CARD;
            else if(file_select_layout(v))s_frame_menu=MENU_SAVE;
            else if(competition_layout(v))s_frame_menu=MENU_COMPETITION;
            else if(title_layout(v) && ((g_ram[0xF600]&127)==4 ||
                    (s_display && !s_display->scene && s_display->title)))
                s_frame_menu=ram16(0xFFAE) || (ram16(0xFFBE)>=12 &&
                    ((v->reg[2]&56u)<<10)!=((v->reg[4]&7u)<<13))?MENU_TITLE:MENU_TITLE_ANIMATION;
        }
    }
    if(competition_scanline(v,line,native,nw,out,width,native_row,native_ctx,palette))return;
    v=&s_video_frame;
    const uint8_t *card_ram=s_interpolating?s_world_frame:g_ram;
    if(s_display && s_display->scene==3 && s_display->bonus_card_valid && !card_ram[0xF711]){
        /* The bonus loading card temporarily retains the level-select
         * plane registers. Its finite words do not make that old menu's
         * offscreen name-table cells into valid margin scenery. */
        if(!native)native=native_row(native_ctx);
        int origin=(width-nw)/2;
        for(int x=0;x<width;++x){out[x]=palette[v->reg[7]&63u];s_priority[x]=(uint8_t)(x>=origin && x<origin+nw?2:0);}
        if(origin>=0)memcpy(out+origin,native,(size_t)nw*sizeof *out);
        draw_scene_sprites(v,line,out,width,origin,palette);
        if(!line){++s_frames;s_native_x=origin;}
        return;
    }
    if(s_frame_bonus) {
        if(!native)native=native_row(native_ctx);
        int origin=(width-nw)/2;
        if(width>nw && (s_frame_zone==0x14 || s_frame_zone==0x15))
            native=bonus_without_hud(v,line,native,nw,palette,shadow);
        bonus_scanline(v,line,(unsigned)s_frame_zone,native,nw,out,width,palette,shadow,s_priority);
        if(s_frame_zone==0x14 || s_frame_zone==0x15)
            draw_scene_sprites(v,line,out,width,origin,palette);
        if(width>nw && (s_frame_zone==0x14 || s_frame_zone==0x15)){
            bonus_draw_hud(v,line,out,width,palette);*hud_origin=0;
        }
        if(line==0){++s_frames;s_native_x=origin;}
        return;
    }
    uint32_t backdrop=palette[v->reg[7]&63];
    /* During a completed fade the game can clear/reload name tables before
     * retiring its old player/display list. A flat palette displays no stage
     * geometry and is not a valid tile-streaming comparison. Keep the full
     * canvas flat; never expose in-progress stage decompression. */
    if(uniform_palette && !(v->reg[12]&8)) {
        for(int x=0;x<width;++x)out[x]=palette[0];
        if(line==0){++s_frames;s_native_x=(width-nw)/2;}
        return;
    }
    /* Display-off/fades fill the same full-width canvas, not black sidebars.
     * A rare unsupported mode also retains the selected output dimensions. */
    if((nw!=320 && nw!=256) || !(v->reg[1]&64) || (v->reg[12]&6) ||
       ((v->reg[12]&8) && !s_frame_special && !s_frame_menu) || s_priority_capacity<width) {
        if(!native)native=native_row(native_ctx);
        uint32_t fill=!(v->reg[1]&64)?native[0]:backdrop;
        for(int x=0;x<width;++x)out[x]=fill;
        int x0=(width-nw)/2;
        if(x0>=0)memcpy(out+x0,native,(size_t)nw*sizeof(uint32_t));
        if(line==0){++s_fallbacks;s_native_x=x0;}
        return;
    }
    if(s_frame_menu==MENU_TITLE_CARD) {
        /* Both installed planes and tile zero are blank: the native row is
         * only a clipped copy of this finite UI. Draw its original pieces
         * once across the canvas, including the centered settled layout. */
        int origin=(width-nw)/2;
        for(int x=0;x<width;++x){out[x]=backdrop;s_priority[x]=0;}
        draw_scene_sprites(v,line,out,width,origin,palette);
        if(!line){++s_frames;s_native_x=origin;}
        return;
    }
    if(s_frame_menu && raster && raster->display && raster->width==(unsigned)nw && raster->start==0) {
        render_menu(v,line,nw,out,width,palette,raster);
        return;
    }
    if(s_frame_menu) {
        /* Private host art/palette providers retain their native renderer.
         * Without its explicit layer masks, never infer transparency from
         * final colors or stretch its UI. The lazy native image is memoized. */
        if(!native)native=native_row(native_ctx);
        int origin=(width-nw)/2;
        for(int x=0;x<width;++x){out[x]=backdrop;s_priority[x]=0;}
        if(origin>=0)memcpy(out+origin,native,(size_t)nw*sizeof *out);
        draw_scene_sprites(v,line,out,width,origin,palette);
        if((s_frame_menu==MENU_TITLE || s_frame_menu==MENU_TITLE_ANIMATION) && s_actor_overlay)
            s_actor_overlay(v,line,out,width,origin,palette,s_priority,s_camera,s_camera_y);
        if(!line){++s_frames;s_native_x=origin;}
        return;
    }
    unsigned hsbase=(v->reg[13]&63u)<<10;
    int hmode=v->reg[11]&3,hi=hmode==0?0:hmode==2?line&~7:line;
    int hs_a=(int16_t)vram16(v,hsbase+(unsigned)hi*4);
    int hs_b=(int16_t)vram16(v,hsbase+(unsigned)hi*4+2);
    presentation_scroll(line,&hs_a,&hs_b);
    if(s_frame_special) {
        if(s_mode!=VIDEO_OFF && width>320 && ss_ready) {
            ss_scanline(v,line,out,width,palette,shadow,hs_b);
            if(!line){++s_frames;s_native_x=(width-nw)/2;}
            return;
        }
        /* Native-sized canvases and unavailable projection data retain the
         * complete native composition, including sprites, shadow/highlight,
         * HUD and exit fade. */
        if(!native)native=native_row(native_ctx);
        int origin=(width-nw)/2;
        for(int x=0;x<width;++x)out[x]=0xFF000000u;
        int first=origin<0?-origin:0,last=origin+nw>width?width-origin:nw;
        if(last>first)memcpy(out+origin+first,native+first,(size_t)(last-first)*sizeof *out);
        if(line==0){++s_frames;s_native_x=(width-nw)/2;}
        return;
    }
    int level=s_frame_level,special=s_frame_special;
    /* The next player object can exist while the title-card SAT is still
     * displayed. Do not expose the incoming level before its own display
     * list/planes reach VBlank. */
    if(s_display && s_display->scene!=1)level=0;
    int missing_scroll=0;
    /* AIZ2's battleship borrows the top of A for a second world camera.
     * HInt6 restores ordinary foreground VSRAM below that band. Unwrapping
     * either camera against the other, or holding line-zero VSRAM below the
     * interrupt, samples the forest instead of the ship/ground. Use the
     * installed scroll identity, not a guessed scanline or frame count. */
    int aiz_ship=level && !s_frame_zone && s_frame_act==1 &&
        ((s_world_frame[0xEED6]<<8)|s_world_frame[0xEED7]);
    /* MGZ2 clears the arena's layout before its installed tiles fall in
     * independently scrolled columns. Keep the surrounding world attached
     * to the camera, rather than moving it with the first falling column. */
    int mgz2_collapse=level && s_frame_zone==2 && s_frame_act==1 &&
        ((s_world_frame[0xEEC0]<<8)|s_world_frame[0xEEC1])==4 && (v->reg[11]&4);
    /* LBZ1 publishes independently falling 16px foreground columns.
     * Their first scroll value cannot become the whole world's camera. */
    int lbz1_columns=level && s_frame_zone==6 && !s_frame_act && (v->reg[11]&4);
    int ship_x=(s_world_frame[0xEE98]<<8)|s_world_frame[0xEE99];
    int ship_y=(s_world_frame[0xEE9C]<<8)|s_world_frame[0xEE9D];
    unsigned fg_scroll=(aiz_ship || s_interpolating)?live_fg_scroll:v->vsram[0];
    /* A fractional scroll can lie between two bobbing ship positions.
     * Select the camera from this row's completed HInt identity, then
     * interpolate its position. A fractional value cannot identify a bank. */
    unsigned fg_identity=presentation_fg_identity(line,live_fg_scroll);
    int plane_x=s_frame_fg_x,plane_y=s_frame_fg_y;
    if(level && width>320 && s_display && aiz_join_near(s_display->aiz_join,s_display->camera_x)) {
        /* The next map assignment can precede this SAT/scroll upload.
         * Unwrap both planes against the completed list's coordinate space. */
        plane_x=s_display->camera_x;plane_y=s_display->camera_y;
    }
    /* A newly published bob can precede its VSRAM upload. Match the
     * installed scroll to the nearest camera in the 10-bit VDP domain,
     * rather than requiring exact equality with the next ship position. */
    int ship_distance=abs(unwrap((int)fg_identity,ship_y)-ship_y);
    int ground_distance=abs(unwrap((int)fg_identity,plane_y)-plane_y);
    int ship_band=aiz_ship && ship_distance<=ground_distance;
    int camera=level?unwrap(-hs_a,ship_band?ship_x:plane_x):ram16(0xEE78);
    int camera_y=(mgz2_collapse || lbz1_columns)?plane_y:unwrap((int)fg_scroll,ship_band?ship_y:plane_y);
    if(line==0) {
        if(level) {
            int stage=((s_world_frame[0x8000]<<8)|s_world_frame[0x8001])*128;
            s_stage_width=stage>=320 && stage<=32768?stage:320;
        }
        /* HScroll/VScroll include heat and underwater deformation. Those
         * offsets belong to the planes, never to the object camera or the
         * bounded viewport. Keep sampling the deformed planes below while
         * projecting actors against the same camera used by Render_Sprites. */
        /* GHZ/SYZ and AIZ1 above its deformed water use a rigid A.
         * Their next camera copy can already be in RAM while the previous scroll/SAT is still installed. Project
         * those actors against the installed A camera, just like terrain,
         * rather than adding a one-pixel correction ahead of its upload. */
        int installed_camera=installed_actor_camera(v,level,s_frame_zone,s_frame_act,s_frame_bg_event);
        int owned_camera=s_upload_serial && s_display && s_display->scene==1;
        int object_x=owned_camera?s_display->camera_x:s_frame_fg_x;
        int object_y=owned_camera?s_display->camera_y:s_frame_fg_y;
        s_camera=level?presentation_camera(0,installed_camera?camera:object_x):camera;
        s_camera_y=level?presentation_camera(1,installed_camera?camera_y:object_y):camera_y;if(!s_interpolating)++s_frames;
        s_left=level?scene_view_left(s_camera,width,s_display?s_display->aiz_join:0,
            s_stage_width,s_frame_zone,s_frame_act):
            camera-(width-nw)/2;
        if(level && width>320)handoff_art_capture(v,s_world_frame,s_camera,s_frame_zone,s_frame_act,s_frame_bg_event);
        int framed=handoff_origin(s_camera,s_camera-s_left,width,s_stage_width,
            s_frame_zone,s_frame_act,s_frame_bg_event,
            ((unsigned)s_world_frame[0xFE04]<<8)|s_world_frame[0xFE05],
            ((unsigned)s_world_frame[0xF63A]<<8)|s_world_frame[0xF63B],level);
        s_left=s_camera-framed;
        if(level && s_display && s_display->preview_knux) {
            /* This AIZ2 waiting phase adds only 0/1px heat offsets to A.
             * Recover its installed base once, so the rigid pose follows
             * the displayed ledge without inheriting the per-row wave. */
            int base=hs_a;
            for(int y=0;y<224;++y) {
                int row=hmode==0?0:hmode==2?y&~7:y;
                int scroll=(int16_t)vram16(v,hsbase+(unsigned)row*4),other=0;
                presentation_scroll(row,&scroll,&other);
                if(scroll<base)base=scroll;
            }
            s_knux_plane_camera_x=unwrap(-base,s_frame_fg_x);
            s_knux_plane_camera_y=presentation_camera(1,camera_y);
        }
    }
    if(!s_interpolating && s_handoff_palette_capture && line<224)
        memcpy(s_handoff_art.colors[line],palette,sizeof palette);
    int origin=level?s_camera-s_left:(width-nw)/2;
    s_native_x=origin;
    /* Imported stages' line-scroll profiles position the background. Clamping
     * the expanded foreground at a level edge moves its native viewport
     * across the canvas; sharing that changing origin with B adds a second,
     * reversible pan near the boundary. Keep the backdrop on the centered
     * canvas while foreground, actors and HUD retain the bounded view. */
    /* The intro's screen-relative actors and deformation scroll share a
     * centered cinematic canvas. Foreground edge clamping must not reverse
     * its half-speed cloud camera while the native view moves across it. */
    int intro_background=level && !s_frame_zone && !s_frame_act && !s_frame_bg_event;
    /* FBZ1's indoor/outdoor canvas and SOZ1's normal desert have their own
     * native parallax too. Moving their B origin with the edge-clamped A
     * viewport adds a nearly full-speed reverse pan at the left boundary. */
    int fbz_background=level && s_frame_zone==4 && !s_frame_act;
    int soz_background=level && s_frame_zone==8 && !s_frame_act && !s_frame_bg_event;
    /* HCZ1's parallax also belongs to the screen-centered B canvas. Its
     * left-clamped foreground must not add another pan to the water bands. */
    int hcz_background=level && s_frame_zone==1 && !s_frame_act;
    int lbz_background=level && s_frame_zone==6 && !s_frame_act;
    /* Distant MGZ/CNZ parallax keeps the same centered screen space as
     * those scenes. MGZ2's collidable moving-room banks instead belong to
     * the world, so retain their foreground-relative projection. */
    unsigned mgz2_bank=((unsigned)s_world_frame[0xEED2]<<8)|s_world_frame[0xEED3];
    int mgz2_panorama=level && s_frame_zone==2 && s_frame_act==1 &&
        (mgz2_bank==0 || mgz2_bank==12);
    int mgz_background=level && s_frame_zone==2 && (!s_frame_act || mgz2_panorama);
    int cnz_background=level && s_frame_zone==3 && !s_frame_act && !s_frame_bg_event;
    int restored_canvas=level && s_background_override &&
        s_background_override(s_world_frame,0,0,0,0,NULL,NULL,NULL);
    int centered_background=level && ((s_frame_zone>=0x20 && s_frame_zone<=0x5B) ||
        intro_background || fbz_background || soz_background || hcz_background || lbz_background ||
        mgz_background || cnz_background || s_frame_zone==7 || restored_canvas);
    int bg_origin=centered_background?(width-nw)/2:origin;
    unsigned base_a=(v->reg[2]&56u)<<10,base_b=(v->reg[4]&7u)<<13;
    /* SaveScreen reverses the usual plane roles: the 320px portrait lives
     * on the full-screen Window ($E000, 64 tiles per row), while B ($C000)
     * holds the complete 1024px card strip (128 tiles per row).
     * Use the installed layout through both entry and exit fades. */
    int file_select=!level && file_select_layout(v);
    int level_select=!level && !special && !file_select && level_select_layout(v);
    int menu_background_period=level_select?48:nw;
    int wy=camera_y+line;
    /* HCZ1_Resize restores three shared colors at camera X=$900, Y=$500.
     * Only the extra left view still contains the retired current tunnel.
     * Keep its original colors there, following the native scene fade; the
     * incoming world, native viewport and guest palette remain authoritative. */
    int hcz_tunnel=level && s_frame_zone==1 && !s_frame_act &&
        s_frame_fg_x>=0x900 && wy>=0x500 && wy<0x600;
    uint32_t hcz_tunnel_colors[3];
    if(hcz_tunnel) {
        static const unsigned colors[]={0xB80,0x240,0x220};
        for(unsigned i=0;i<3;++i)
            hcz_tunnel_colors[i]=genesis_dac_cram_to_argb(scene_art_color(colors[i],v),GENESIS_DAC_NORMAL);
    }
    /* The HCZ1 boss borrows palette 1 after locking its native arena.
     * The wider left view still contains stage bricks using that line;
     * retain their dry/wet stage colours instead of the boss damage flash. */
    int hcz_boss=level && s_frame_zone==1 && !s_frame_act && s_frame_fg_x>=0x3680;
    uint32_t hcz_boss_colors[16];
    if(hcz_boss) {
        unsigned water=((unsigned)s_world_frame[0xF646]<<8)|s_world_frame[0xF647];
        int wet=wy>=(int)water;
        unsigned source=S3_ART_BANK+(wet?0x8C654u:0x8C594u);
        if(s_palette_source)source=s_palette_source(source);
        if(wet)source+=32;
        for(unsigned i=0;i<16;++i)
            hcz_boss_colors[i]=genesis_dac_cram_to_argb(scene_art_color(scene_read16(source+i*2),v),GENESIS_DAC_NORMAL);
    }
    int bg_world=level && !special;
    /* The fire is a streamed Plane B composition, not the background at
     * the world's current layout coordinates. AIZ1BGE_FireTransition
     * writes rows from layout X=$1000 while replacing foreground blocks;
     * AIZ2BGE_WaitFire continues at X=$200 before a gradual BG redraw.
     * Follow the uploaded name table through that redraw so the flames
     * cover the same in-progress art replacement as on the native VDP. */
    int aiz_fire=bg_world && s_frame_zone==0 &&
        ((s_frame_act==0 && s_frame_bg_event>=0xC && s_frame_bg_event<=0x14) ||
         (s_frame_act==1 && s_frame_bg_event<=8));
    /* FireRefresh builds a temporary foreground from layout X=$180,
     * then AIZ2BGE_FireRedraw installs the incoming stage. RAM's current
     * chunks no longer describe the displayed foreground in between. */
    int aiz_fire_fg=aiz_fire &&
        ((s_frame_act==0 && s_frame_bg_event>=0x10) ||
         (s_frame_act==1 && s_frame_bg_event==0));
    /* DoShipLoop repeats a streamed 512px foreground, not the neighboring
     * stage columns exposed by a wider view. Keep its installed ground
     * composition across the $200 camera wrap. The ship's independent
     * upper band still samples its own complete world art. */
    int aiz_loop_fg=bg_world && aiz_ship && !s_frame_zone && s_frame_act==1 &&
        ((s_world_frame[0xEEB2]<<8)|s_world_frame[0xEEB3])==4 &&
        ((s_world_frame[0xEEC0]<<8)|s_world_frame[0xEEC1])>=8 && !ship_band;
    /* EndRefresh installs complete grove rows while DoShipLoop is still
     * active, then clears only the ship's upper-band flag. Keep that
     * published foreground canvas over the outgoing partial columns. The
     * complete incoming grove keeps its spatial layout and finite extent. */
    int aiz_grove_fg=bg_world && !s_frame_zone && s_frame_act==1 &&
        ((s_world_frame[0xEEC0]<<8)|s_world_frame[0xEEC1])>=16 &&
        s_frame_fg_x<0x4880;
    unsigned tree_arrival=0;
    if(aiz_ship && !s_frame_zone && s_frame_act==1) {
        uint32_t ship=((uint32_t)s_world_frame[0xEE98]<<24)|
            ((uint32_t)s_world_frame[0xEE99]<<16)|
            ((unsigned)s_world_frame[0xEE9A]<<8)|s_world_frame[0xEE9B];
        tree_arrival=aiz_tree_arrival_alpha(s_frame_fg_x,
            (s_world_frame[0xEEC0]<<8)|s_world_frame[0xEEC1],ship);
    }
    /* FBZ streams complete 512px rows from one of two layout canvases
     * (X=0 indoors, X=$200 outdoors). Its doorway events replace that
     * canvas a row/column at a time. The uploaded plane is authoritative
     * for both the selected bank and every mixed transition frame. */
    int fbz_canvas=bg_world && s_frame_zone==4;
    /* ICZ1's outdoor bank starts at layout X=$1880; the snow and cave
     * entrance then replace it in strips. ICZ2 also swaps complete indoor
     * and outdoor canvases. Keep those uploaded compositions through each
     * redraw rather than treating their source banks as adjacent scenery. */
    int icz_canvas=bg_world && s_frame_zone==5 &&
        (s_frame_act==1 || (s_frame_bg_event<0x10 && s_frame_bg_event!=4));
    /* BigSnowFall is a finite spatial snow bank streamed normally by
     * DrawBGAsYouMove, not a repeating 512px canvas. Keep its real extent;
     * the subsequent partial redraws still use the uploaded composition. */
    /* SSZ1's cloud band is a separate 512px bank at X=$1C00; entry and
     * exit replace it progressively. DEZ3's auto-scrolling runway is a
     * live Plane B canvas edited by the boss, independent of its layout. */
    int ssz_canvas=bg_world && s_frame_zone==10 && !s_frame_act && s_frame_bg_event;
    int dez3_canvas=bg_world && s_frame_zone==23;
    /* HCZ2 replaces its moving wall in two bottom-up passes before the
     * normal panorama is installed. Preserve those mixed uploaded rows. */
    int hcz2_canvas=bg_world && s_frame_zone==1 && s_frame_act==1 &&
        (s_frame_bg_event==8 || s_frame_bg_event==12);
    /* The AIZ2 grove/ship backdrop is also replaced bottom-up. During that
     * pass the uploaded rows retain the outgoing composition. */
    int aiz2_canvas=bg_world && !s_frame_zone && s_frame_act==1 && s_frame_bg_event==16;
    int mgz2_canvas=mgz2_panorama && s_frame_bg_event==8;
    int bg_canvas=fbz_canvas || icz_canvas || ssz_canvas || dez3_canvas || hcz2_canvas || aiz2_canvas || mgz2_canvas;
    int aiz_banks=bg_world && !s_frame_zone && !s_frame_act &&
        s_frame_bg_event<=8 && s_aiz_art_ready;
    if(aiz_banks){aiz_palette(&s_aiz_intro,v,intro_palette);aiz_palette(&s_aiz_main,v,forest_palette);}
    int by=bg_world?unwrap(presentation_bg_scroll(v->vsram[1]),s_frame_bg_y)+line:line+(v->vsram[1]&1023);
    /* Once the cave camera is selected, its full spatial layout is already
     * available. Extend it beyond the native viewport while the uploaded
     * plane catches up; those unseen columns can still contain blank tiles. */
    int icz_margin_world=bg_world && s_frame_zone==5 && !s_frame_act && s_frame_bg_event==12;
    if(bg_world && s_frame_zone==5 && !s_frame_act &&
       s_frame_bg_event==8) {
        unsigned remaining=((unsigned)s_world_frame[0xEECA]<<8)|s_world_frame[0xEECB];
        if(remaining<=15) {
            /* Each native pass replaces sixteen 16px rows, bottom first.
             * Its pending position is already below the last uploaded row.
             * Track their physical plane rows through the vertical wrap:
             * pass 8 retires finite snow for a repeating panorama. */
            unsigned mask=(unsigned)(plane_tiles((v->reg[16]>>4)&3)*8-1);
            unsigned pending=((unsigned)s_world_frame[0xEEC8]<<8)|s_world_frame[0xEEC9];
            unsigned first=(pending+16)&mask;
            int replaced=((((unsigned)by&~15u)-first)&mask)<(15-remaining)*16;
            icz_margin_world=!replaced;
        }
    }
    /* HCZ2's first wipe retires a spatial moving wall. Unreplaced rows
     * only have the native columns in VRAM; use their full wall layout in
     * the margins until the complete incoming 512px row is installed. */
    int hcz2_margin_world=0;
    if(hcz2_canvas && s_frame_bg_event==8) {
        unsigned remaining=((unsigned)s_world_frame[0xEECA]<<8)|s_world_frame[0xEECB];
        if(remaining<=15) {
            unsigned mask=(unsigned)(plane_tiles((v->reg[16]>>4)&3)*8-1);
            unsigned pending=((unsigned)s_world_frame[0xEEC8]<<8)|s_world_frame[0xEEC9];
            unsigned first=(pending+16)&mask;
            hcz2_margin_world=((((unsigned)by&~15u)-first)&mask)>=(15-remaining)*16;
        }
    }
    /* CNZ1BGE_Boss streams only the $200..$2FF background rows with
     * Draw_TileColumn(d1=$200,d6=$10). Their 256px canvas scrolls while
     * the room descends; the neighboring layout rows are other scenes. */
    if(bg_world && s_frame_zone==3 && !s_frame_act && s_frame_bg_event==8)
        by=0x200+(by&255);
    int bg_width=bg_world?((s_world_frame[0x8002]<<8)|s_world_frame[0x8003])*128:0;
    /* AIZ1's upper parallax bands are a fixed 512px canvas. Its layout
     * also stores the intro coastline and fire art farther right: those
     * are separate compositions, not the repeat period of the forest. */
    if(bg_world && s_frame_zone==0 && s_frame_act==0) {
        if(by<0x220)bg_width=512;
        else if(by<0x380)bg_width=30*128; /* lake section, before the fire banks */
        /* The flying intro starts with a negative virtual BG camera.
         * Its streamer clamps that camera to zero, leaving the first 512px
         * canvas in VRAM while hardware HScroll wraps through it. */
        if(s_frame_bg_event==0 && hs_b>0)bg_width=512;
    }
    /* AIZ2BGE_Normal streams whole 512px rows from layout X=0 (d1=0,
     * d6=$20), then scrolls that repeating canvas for its heat shimmer.
     * The rest of the layout contains the outgoing fire and other scenes;
     * treating it as one wide world reads unrelated tile banks. */
    if(bg_world && s_frame_zone==0 && s_frame_act==1 && s_frame_bg_event>=8)
        bg_width=512;
    /* SOZ1's desert also uses Draw_TileRow(X=0, d6=$20). The rest
     * of its layout belongs to the rising pyramid/boss presentation. */
    if(bg_world && s_frame_zone==8 && s_frame_act==0 && s_frame_bg_event==0)
        bg_width=512;
    /* LBZ1_BGDrawArray splits at Y=$D0. Its lower band is streamed from
     * the fixed X=0 entry at HScroll_table+$004; LBZ1_Deform moves finer
     * water bands through that 512px canvas. The upper Death Egg remains
     * a spatial world layout. */
    if(bg_world && s_frame_zone==6 && !s_frame_act && by>=0xD0)
        bg_width=512;
    /* CNZ's normal city parallax is another X=0, 32-block row. Its other
     * layout banks are used as collidable scenery during the act-1 boss. */
    if(bg_world && s_frame_zone==3 &&
       ((!s_frame_act && !s_frame_bg_event) || (s_frame_act==1 && s_frame_bg_event>=4)))
        bg_width=512;
    /* HCZ2BGE_Normal streams X=0, d6=$20: only the first 512px are
     * its repeating panorama. The remaining layout belongs to the moving
     * crushing wall and must never be sampled as normal parallax. */
    if(bg_world && s_frame_zone==1 && s_frame_act==1 && s_frame_bg_event>=16)
        bg_width=512;
    /* MGZ2's normal and post-lift scenery also streams a 32-block canvas.
     * The other layout banks are physical moving rooms used only by events
     * 4/8; scrolling the panorama through them substitutes unrelated walls. */
    if(mgz2_panorama)
        bg_width=512;
    /* HCZ2's crushing wall is a moving world, not a repeating panorama.
     * Negative coordinates to its left must remain blank. */
    if(bg_world && ((s_frame_zone==1 && s_frame_act==1 && (s_frame_bg_event<=4 || hcz2_margin_world)) ||
       s_frame_zone==22))
        bg_width=0;
    /* LRZ3's distant lava cavern occupies the first 512px. Its upper
     * parallax camera stays within that panorama; the remaining layout
     * is the close, collidable boss arena entered by the later events. */
    if(bg_world && s_frame_zone==22 && !s_frame_act && !s_frame_bg_event)
        bg_width=512;
    int aiz_intro_canvas=bg_world && !s_frame_zone && !s_frame_act && !s_frame_bg_event;
    int background_x=-bg_origin-hs_b;
    if(bg_width>0){background_x%=bg_width;if(background_x<0)background_x+=bg_width;}
    int cinematic=ending_cinematic();
    int handoff_loading=bg_world && width>320 &&
        handoff_art_loading(s_frame_zone,s_frame_act,s_frame_bg_event);
    const uint8_t *fg_world=handoff_loading?s_handoff_art.world:s_world_frame;
    const uint8_t *bg_world_data=handoff_loading?s_handoff_art.world:s_background_frame;
    const uint8_t *world_patterns=handoff_loading?s_handoff_art.video.vram:v->vram;
    int incoming_x=0,incoming_y=0;
    int hcz2_incoming=bg_world && width>320 && s_frame_zone==1 && s_frame_act==1 &&
        s_frame_bg_event<=12 && hcz2_panorama_position(line,&incoming_x,&incoming_y);
    GenesisTileRow fg={0},bg={0},incoming={0};
    /* The snowboard hill begins partway across each wrapped layout row.
     * Its unused leading chunks are empty on the cartridge. Extend the
     * solid snow at that boundary into the extra left view; do not expose
     * rectangular holes behind the hill, or change its native geometry. */
    int icz_snow_edge=0;
    uint16_t icz_snow_attr=0;
    if(bg_world && s_frame_zone==5 && !s_frame_act && !s_frame_bg_event) {
        unsigned p=0x8008u+(((unsigned)wy>>5)&0x3Cu);
        unsigned row=((unsigned)s_world_frame[p]<<8)|s_world_frame[p+1];
        if(row>=0x8088 && row+16<=0x9000)for(unsigned i=0;i<16;++i) {
            if(!s_world_frame[row+i])continue;
            uint16_t attr=world_attr(s_world_frame,(int)i*128,wy,0);
            /* A bank ahead of the camera does not cover the sky behind it.
             * Extend only a solid snow row already at the native left edge. */
            if(attr==0x4002 && world_attr(s_world_frame,camera,wy,0)==0x4002){
                icz_snow_edge=(int)i*128;icz_snow_attr=attr;
            }
            break;
        }
    }
    for(int x=0;x<width;++x) {
        int nx=x-origin,wx=level?nx+camera:nx-hs_a;
        int reference_bx=nx-hs_b,bx=x-bg_origin-hs_b;
        /* IntroDeform clamps each band's streaming origin at zero. Left
         * extensions still wrap the first installed 512px canvas while the
         * virtual HScroll crosses zero; later positive coast cells are a
         * continuous world, not that repeat period. */
        int source_bx=aiz_intro_canvas && bx<0?(bx&511):background_x;
        /* Title/menu backgrounds only initialize the visible 40 tile
         * columns; repeat that composition, not the stale 512px name table. */
        if(!level && !special && !file_select){
            /* Reflect only the title scenery at its boundary. Opposite-edge
             * wrapping makes a visible seam; stretching one pixel smears the
             * clouds into horizontal bars. Logos remain in the native center. */
            if((g_ram[0xF600]&127)==4 && !level_select && (nx<0 || nx>=nw)) {
                int ax=nx%(nw*2);if(ax<0)ax+=nw*2;
                if(ax>=nw)ax=nw*2-1-ax;
                bx=ax-hs_b;
            }
            int repeat=nx<0 || nx>=nw?menu_background_period:nw;
            bx%=repeat;if(bx<0)bx+=repeat;
            if(menu_background_period!=nw && (nx==0 || nx==nw))bg.remaining=0;
        }

        if(!fg.remaining) {
            uint16_t attr=level?world_attr(fg_world,wx,wy,0):plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
            gtile_row_begin(&fg,world_patterns,attr,level?wx:nx-hs_a,level?wy:line+(v->vsram[0]&1023));
        }
        if(!bg.remaining) {
            uint16_t attr=bg_world?world_attr(bg_world_data,source_bx,by,1):plane_attr(v,base_b,bx,by);
            gtile_row_begin(&bg,world_patterns,attr,bg_world?source_bx:bx,by);
        }
        uint16_t a=fg.attr,b=bg.attr;
        uint8_t ap=gtile_row_pixel(&fg),bp=gtile_row_pixel(&bg);
        if(nx<0 && wx>=0 && wx<icz_snow_edge && !ap) {
            a=icz_snow_attr;ap=pattern_pixel(v,a,wx,wy);
        }
        if(file_select) {
            /* Keep one portrait, extending only its edge colours. Mirroring
             * the whole image repeats Sonic's face and DATA SELECT text.
             * The finite carousel still exposes its original offscreen cards. */
            int ax=nx<0?0:nx>=nw?nw-1:nx;
            a=vram16(v,0xE000u+(unsigned)(line/8*64+ax/8)*2u);
            ap=pattern_pixel(v,a,ax,line);
            if(bx<0 || bx>=1024)bp=0;
        }
        /* Primary foreground colors remain native; only the retired
         * secondary background palette needs to outlive its bank. */
        const uint32_t *apalette=palette;
        const uint32_t *bpalette=handoff_loading?s_handoff_art.colors[line]:palette;
        if(bg_canvas && !((icz_margin_world || hcz2_margin_world) && (nx<0 || nx>=nw))) {
            int canvas_x=centered_background?bx:reference_bx;
            b=plane_attr(v,base_b,canvas_x,by);
            bp=pattern_pixel(v,b,canvas_x,by);
        }
        if(hcz2_incoming && nx>=nw && wx>=0xC10){
            /* The extra right view reaches the next room before the native
             * viewport. Draw its complete panorama through both refresh
             * passes; preserve the moving wall and every native column. */
            int px=(nx+incoming_x)&511;
            if(!incoming.remaining){
                gtile_row_begin(&incoming,v->vram,
                    world_attr(s_background_frame,px,incoming_y,1),px,incoming_y);
                hcz2_panorama_art(incoming.attr,incoming_y,&incoming);
            }
            b=incoming.attr;bp=gtile_row_pixel(&incoming);bpalette=palette;
        }
        if(aiz_fire) {
            unsigned vs=1;
            if(v->reg[11]&4) {
                /* AIZTrans_WavyFlame advances its 16-entry wave by two
                 * per 16px column: extend that eight-column period into
                 * either margin. Keep every native column verbatim. */
                unsigned col=nx>=0 && nx<nw?(unsigned)nx/16:((unsigned)nx/16)&7;
                vs=col*2+1;
            }
            int fire_y=line+(v->vsram[vs]&1023);
            b=plane_attr(v,base_b,reference_bx,fire_y);
            bp=pattern_pixel(v,b,reference_bx,fire_y);
        }
        /* The first streamed loop still holds the unique outgoing ledge
         * in unused columns. Its incoming right margin is authored flat
         * ground, not another copy of that ledge. */
        int incoming_chase=aiz_loop_fg && nx>=nw && wx>=0x4200 && wx<0x4440;
        if(aiz_fire_fg || (aiz_loop_fg && !incoming_chase) ||
           (aiz_grove_fg && wx>=0x4380 && wx<0x4640)) {
            int ay=line+(((aiz_loop_fg || aiz_grove_fg)?fg_scroll:v->vsram[0])&1023);
            a=plane_attr(v,base_a,nx-hs_a,ay);
            ap=pattern_pixel(v,a,nx-hs_a,ay);
        }
        if(lbz1_columns) {
            if(nx>=0 && nx<nw) {
                unsigned column=(unsigned)nx/16;
                unsigned scroll=presentation_column_scroll(column*2,v->vsram[column*2]);
                int ax=nx-hs_a,ay=line+(scroll&1023);
                a=plane_attr(v,base_a,ax,ay);ap=pattern_pixel(v,a,ax,ay);
            } else {
                /* LBZ1_FGVScrollArray owns ten columns at $3B60..$3BFF.
                 * All other scenery keeps the ordinary copied camera. */
                int ay=plane_y+line;
                if(wx>=0x3B60 && wx<0x3C00) {
                    unsigned at=0xA930u+(1u+(unsigned)(wx-0x3B60)/16u)*2u;
                    ay=(int16_t)(((unsigned)s_world_frame[at]<<8)|s_world_frame[at+1])+line;
                }
                a=world_attr(s_world_frame,wx,ay,0);ap=pattern_pixel(v,a,wx,ay);
            }
        }
        if(mgz2_collapse && wx>=0x3C80 && wx<0x3E00) {
            int column=nx<0?0:nx>=nw?(nw-1)/16:nx/16;
            unsigned scroll=presentation_column_scroll((unsigned)column*2,v->vsram[column*2]);
            int ax=nx-hs_a,ay=line+(scroll&1023);
            a=plane_attr(v,base_a,ax,ay);
            ap=pattern_pixel(v,a,ax,ay);
        }
        if(aiz_banks && (nx<0 || nx>=nw)) {
            /* The hollow tree at $1300 shares primary blocks across both
             * banks. The coast behind it and forest ahead need independent
             * art. Only extend beyond the native viewport; its live animated
             * art, palette changes and scripted reveals remain authoritative. */
            int intro=wx<0x1300;
            const AizArt *art=intro?&s_aiz_intro:&s_aiz_main;
            if(intro || s_frame_bg_event<8) {
                a=world_attr_blocks(s_world_frame,art->blocks,wx,wy,0);
                ap=pattern_pixel(&art->video,a,wx,wy);
                apalette=intro?intro_palette:forest_palette;
            }
            if(s_frame_bg_event==0) {
                art=by>=0x380?&s_aiz_intro:&s_aiz_main;
                b=world_attr_blocks(s_background_frame,art->blocks,source_bx,by,1);
                bp=pattern_pixel(&art->video,b,source_bx,by);
                bpalette=by>=0x380?intro_palette:forest_palette;
            }
        }
        int joined=level && s_display && width>320 &&
            aiz_join_sample(v,s_display->aiz_join,s_camera,nx,wx,wy,&a,&ap);
        int handoff_fg=level && width>320 &&
            handoff_art_sample(s_frame_zone,s_frame_act,wx,wy,&a,&ap);
        if(joined)apalette=palette;
        if(handoff_fg){joined=1;apalette=palette;}
        int handoff_bg=level && width>320 &&
            handoff_art_background(s_frame_zone,s_frame_act,wx,x,line,width,nw,&b,&bp);
        if(handoff_bg)bpalette=s_handoff_art.colors[line];
        if(level && aiz_retired_tree_sample(nx,nw,wx,wy,&a,&ap))apalette=palette;
        /* AIZ1_ScreenEvent / AIZ_TreeReveal selectively upload the hollow
         * tree's 16px blocks before committing whole 128px layout chunks.
         * Its locked camera keeps the entire 256px reveal strip resident
         * in Plane A. Use that published composition, including its mask
         * and priority, instead of jumping between the coarse chunk edits.
         * The rest of the expanded world still comes from its own layout. */
        if(bg_world && !s_frame_zone && !s_frame_act && s_frame_tree_reveal &&
           wx>=0x2C80 && wx<0x2D80 && wy>=0x280 && wy<0x480) {
            int ax=nx-hs_a,ay=line+(v->vsram[0]&1023);
            a=plane_attr(v,base_a,ax,ay);
            ap=pattern_pixel(v,a,ax,ay);
            apalette=palette;
        }
        if(cinematic && (nx<0 || nx>=nw))ap=0;
        if(level && !joined && (wx<0 || wx>=s_stage_width))ap=0;
        /* Menus have a centered foreground composition, with full-width
         * scenery underneath. Special-stage planes are repeating art. */
        if(!level && !file_select && (nx<0 || nx>=nw))ap=0;
        uint16_t restored_color=0xFFFF;
        int restored_background=level && s_background_override &&
            s_background_override(s_world_frame,bx,by,x-bg_origin,line,&b,&bp,&restored_color);
        s_priority[x]=(uint8_t)(((a&0x8000)&&ap)||((b&0x8000)&&bp));
        if(level && s_frame_zone==1 && !s_frame_act && ap && (nx<0 || nx>=nw) &&
           wx>=0x360 && wx<0x980) {
            /* The nonsolid blue backing is on A too. Clip new roof fill to
             * actual terrain blocks, not every opaque foreground pixel. */
            uint16_t roof=0;
            if(world_block(s_world_frame,wx,wy,0,0x7Cu,&roof) && (roof&0xF000u))s_priority[x]|=4u;
        }
        /* The authored transition viewport has no high-priority tree
         * canopy in front of its flame curtain. Expanded margins do: let
         * the opaque high-priority flames cover that extra scenery too.
         * Keep native overlap ordering and transparent flame edges intact. */
        int fire_curtain=aiz_fire && (nx<0 || nx>=nw) && (b&0x8000) && bp;
        int foreground=ap && !fire_curtain && ((a&0x8000) || !bp || !(b&0x8000));
        uint8_t p=foreground?ap:bp;
        uint32_t owned_backdrop=(handoff_loading || handoff_bg)?
            s_handoff_art.colors[line][s_handoff_art.video.reg[7]&63]:backdrop;
        uint32_t ac=ap?plane_color(line,a,ap,apalette):owned_backdrop;
        if(aiz_tree_color(wx,wy,nx,nw,ap))
            ac=genesis_dac_cram_to_argb(scene_art_color(4,v),GENESIS_DAC_NORMAL);
        if(hcz_tunnel && nx<0 && wx>=0x360 && wx<0x900 && ap>=56 && ap<=58)
            ac=hcz_tunnel_colors[ap-56];
        if(hcz_boss && nx<0 && wx<0x3680 && ap>=16 && ap<32)
            ac=hcz_boss_colors[ap-16];
        uint32_t bc=bp?(restored_background && restored_color!=0xFFFF?
            genesis_dac_cram_to_argb(restored_art_color(restored_color,v),GENESIS_DAC_NORMAL):plane_color(line,b,bp,bpalette)):owned_backdrop;
        out[x]=foreground?ac:bc;
        if(tree_arrival && nx>=nw) {
            uint16_t tree_attr;uint8_t tree_pixel;
            if(aiz_ship_tree_sample(s_camera+nx,s_frame_fg_y+line,&tree_attr,&tree_pixel) &&
               (tree_attr&0x8000u) && tree_pixel) {
                uint32_t color=plane_color(line,tree_attr,tree_pixel,palette),mixed=0xFF000000u;
                for(unsigned shift=0;shift<24;shift+=8) {
                    unsigned a=(color>>shift)&255u,b=(out[x]>>shift)&255u;
                    mixed|=((a*tree_arrival+b*(255-tree_arrival)+127)/255)<<shift;
                }
                out[x]=mixed;s_priority[x]|=1;
            }
        }
        /* Outside a native special-stage projection only planes are drawn,
         * so plane priority alone
         * determines shadow; native operator sprites remain in the center. */
        if(special && (v->reg[12]&8) && !s_priority[x])
            out[x]=shadow[p?p:v->reg[7]&63];
        /* With no published scene the final path below copies the native
         * viewport over this provisional composition. Comparing those
         * discarded samples would report errors in pixels never displayed. */
        if(!s_interpolating && nx>=0 && nx<nw && !(x&7) && !missing_scroll && (!level || s_display)) {
            if(level && !native_terrain_streamed(wy,s_frame_fg_y)) {
                ++s_terrain_unstreamed;
            } else if(level && !aiz_fire_fg && !mgz2_collapse) {
                uint16_t expected=plane_attr(v,base_a,nx-hs_a,line+(fg_scroll&1023));
                ++s_terrain_checks;
                int bad=attributes_differ(v,a,expected,wx,wy);
                if(bad && s_bad_y<0){s_bad_x=wx;s_bad_y=line;s_bad_attr=a;s_bad_expected=expected;s_bad_world_y=wy;s_bad_hscroll=hs_a;}
                s_terrain_errors+=bad;
            }
            if(restored_background) {
                /* Optional replacement artwork is compared with its donor,
                 * rather than the unchanged native name table. */
                ++s_bg_unstreamed;
            } else if(centered_background && bg_origin!=origin) {
                /* This edge presentation intentionally moves B relative to
                 * the native foreground window. Its same-column pixels are
                 * not a presentation oracle until the view is centered. */
                ++s_bg_unstreamed;
            } else if(bg_world && s_frame_zone==0 && s_frame_act==0 && s_frame_bg_event==4) {
                /* Native intro->forest refresh uploads two rows per frame.
                 * Until finished, the old name table is not an oracle for
                 * the new world layout, which the opt-in renderer shows. */
                ++s_bg_unstreamed;
            } else if(bg_canvas) {
                /* A streamed composition has no single world-layout bank
                 * to compare against during its directional wipes. */
                ++s_bg_unstreamed;
            } else if(bg_world && s_frame_zone==22 && s_frame_act==1 &&
                      (reference_bx<(s_frame_bg_band[by<512?0:1]&~15) ||
                       reference_bx>=(s_frame_bg_band[by<512?0:1]&~15)+336)) {
                /* HPZ_BGDrawArray streams two bands (split at Y=$200),
                 * while HPZ_BGDeformArray scrolls ten finer bands. Some
                 * fine-band pixels can see outside Draw_TileColumn's
                 * 21-block interval. Those stale native cells are not a
                 * reference for the expanded world's actual layout. */
                ++s_bg_unstreamed;
            } else if(bg_world && !aiz_fire) {
                int rx=reference_bx;
                if(bg_width>0){rx%=bg_width;if(rx<0)rx+=bg_width;}
                uint16_t expected=plane_attr(v,base_b,reference_bx,by);
                ++s_bg_checks;
                uint16_t actual=world_attr(s_background_frame,rx,by,1);
                int bad=attributes_differ(v,actual,expected,reference_bx,by);
                if(bad && s_bg_bad_x<0){s_bg_bad_x=rx;s_bg_bad_y=by;s_bg_bad_a=actual;s_bg_bad_expected=expected;}
                s_bg_errors+=bad;
            }

        }
        ++background_x;
        if(bg_width>0 && background_x==bg_width)background_x=0;
    }
    if(!level) {
        if(!native)native=native_row(native_ctx);
        /* Logos/copyright/menus remain centered at their original pixel
         * scale. The full-width background follows the live scene palette. */
        if(origin>=0)memcpy(out+origin,native,(size_t)nw*sizeof(uint32_t));
        if(((g_ram[0xF600]&127)==4 || file_select ||
            (s_world_frame[0xF600]&127)==0x48) && s_display && s_display->scene==0)
            draw_scene_sprites(v,line,out,width,origin,palette);
        if((g_ram[0xF600]&127)==4 && s_actor_overlay)
            s_actor_overlay(v,line,out,width,origin,palette,s_priority,s_camera,s_camera_y);
    } else if(s_display) {
        /* This static waiting pose belongs to the ledge already installed in
         * the foreground. The live object camera can advance while that
         * scroll snapshot is retained; sampling it would slide the preview
         * ahead of the ledge during the approach. */
        if(s_frame_level && s_display->preview_knux)
            s3_knux_preview_scanline(g_rom,line,out,width,origin,
                s_knux_plane_camera_x,s_knux_plane_camera_y);
        *hud_origin=0;
        draw_scene_sprites(v,line,out,width,origin,palette);
        if(s_actor_overlay && !s_interpolating) {
            presentation_overlay_begin(width);
            s_actor_overlay(v,line,out,width,origin,palette,s_priority,s_camera,s_camera_y);
            presentation_overlay_end(width);
        }
    } else {
        /* No matching DMA yet (e.g. load/fade): preserve the live native
         * scene while the host display list catches its next publication. */
        if(!native)native=native_row(native_ctx);
        int first=origin<0?-origin:0,last=origin+nw>width?width-origin:nw;
        if(last>first)memcpy(out+origin+first,native+first,(size_t)(last-first)*sizeof(uint32_t));
    }
}

static void render_scanline(const GVDP *v,int line,const uint32_t *native,int nw,
                            uint32_t *out,int width,GameVideoNativeRow native_row,
                            void *native_ctx,const GVDPRasterRow *raster)
{
    int origin;
    render_scanline_base(v,line,native,nw,out,width,native_row,native_ctx,raster,&origin);
    const uint8_t *ram=s_interpolating?s_world_frame:g_ram;
    life_ones_scanline(v,ram,line,out,width,origin);
    time_tens_scanline(v,ram,line,out,width,origin);
    handoff_dip(out,width);
}

static void scanline(const GVDP *v,int line,const uint32_t *native,int nw,uint32_t *out,int width)
{
    render_scanline(v,line,native,nw,out,width,NULL,NULL,NULL);
}
static void scanline_lazy(const GVDP *v,int line,int nw,uint32_t *out,int width,
                          GameVideoNativeRow native_row,void *native_ctx,const GVDPRasterRow *raster)
{
    render_scanline(v,line,NULL,nw,out,width,native_row,native_ctx,raster);
}

void s3_video_command(int id, const char *json)
{
    (void)json;
    char reply[2048];
    snprintf(reply, sizeof reply,
        "{\"id\":%d,\"enabled\":%d,\"width\":%d,\"view_left\":%d,\"camera\":%d,"
        "\"native_x\":%d,\"stage_width\":%d,\"frames\":%u,\"fallback_frames\":%u,"
        "\"terrain_checks\":%u,\"terrain_errors\":%u,\"background_checks\":%u,\"background_errors\":%u,\"background_unstreamed\":%u,"
        "\"sprites\":%u,\"scene\":%d,\"spawned\":%u,\"pool_pressure\":%u,\"native_uninitialized_scroll_lines\":%u,"
        "\"native_unstreamed_terrain_samples\":%u,\"first_bad_x\":%d,\"first_bad_y\":%d,\"first_bad_attr\":%u,\"first_bad_expected\":%u,\"first_bad_world_y\":%d,\"first_bad_hscroll\":%d,"
        "\"scene_match_misses\":%u,\"scene_held_frames\":%u,\"main_cpu_divisor\":%u,"
        "\"aiz_art_ready\":%d,\"aiz_main_palette\":%u,\"aiz_main_attr\":%u,\"aiz_main_pixel\":%u,"
        "\"bg_bad_x\":%d,\"bg_bad_y\":%d,\"bg_bad_attr\":%d,\"bg_bad_expected\":%d,\"bg_event\":%u,"
        "\"tick_samples\":%u,\"tick_updates\":%u,\"tick_lag\":%u,\"tick_multi\":%u,\"publication_lag\":%u,"
        "\"blue_spheres\":%d,\"special_spheres\":%u,\"special_margin_spheres\":%u}",
        id, enabled(), s_width, s_left, s_camera, s_native_x, s_stage_width,
        s_frames, s_fallbacks, s_terrain_checks, s_terrain_errors,s_bg_checks,s_bg_errors,s_bg_unstreamed,
        s_display?s_display->count:0,s_display?s_display->scene:-1,s_spawned,s_pool_pressure,s_uninitialized_scroll_lines,
        s_terrain_unstreamed,s_bad_x,s_bad_y,s_bad_attr,s_bad_expected,s_bad_world_y,s_bad_hscroll,
        s_scene_misses,s_scene_holds,s3_video_main_cpu_divisor(),
        s_aiz_art_ready,s_aiz_main.colors[2],world_attr_blocks(s_world_frame,s_aiz_main.blocks,0x1300,0x390,0),
        pattern_pixel(&s_aiz_main.video,0x582C,0x1300,0x390),
        s_bg_bad_x,s_bg_bad_y,s_bg_bad_a,s_bg_bad_expected,s_frame_bg_event,
        s_tick_samples,s_tick_updates,s_tick_lag,s_tick_multi,s_publication_lag,
        s_frame_special,s_mode!=VIDEO_OFF && s_requested_width>320 && ss_ready?ss_count:0u,
        s_mode!=VIDEO_OFF && s_requested_width>320 && ss_ready?ss_margin_count:0u);
    cmd_send_response(reply);
}
static int s_native_publication_restored;
void s3_video_state_loaded(void) { s_native_publication_restored=1; }
static void scene_frame_state(S3StateIO *io, SceneFrame *frame)
{
    int scene=frame->scene,cx=frame->competition_x,cy=frame->competition_y;
    unsigned page=frame->competition_page;
    s3_state_peek(io,offsetof(SceneFrame,scene),&scene,sizeof scene);
    s3_state_peek(io,offsetof(SceneFrame,competition_x),&cx,sizeof cx);
    s3_state_peek(io,offsetof(SceneFrame,competition_y),&cy,sizeof cy);
    s3_state_peek(io,offsetof(SceneFrame,competition_page),&page,sizeof page);
    if(scene<0 || scene>4 || cx<0 || cx>65535 || cy<0 || cy>65535 || page>1)io->ok=0;
    unsigned count = frame->count;
    s3_state_peek(io, offsetof(SceneFrame, count), &count, sizeof count);
    if (count > SCENE_SPRITES) io->ok = 0;
    unsigned pose=frame->aiz_super_pose;
    s3_state_peek(io,offsetof(SceneFrame,aiz_super_pose),&pose,sizeof pose);
    if(pose>2)io->ok=0;
    unsigned world=0;s3_state_peek(io,offsetof(SceneFrame,world_tag),&world,sizeof world);
    if(world>0x10000u)io->ok=0;
    unsigned join=0;s3_state_peek(io,offsetof(SceneFrame,aiz_join),&join,sizeof join);
    if(join>3)io->ok=0;
    int card[4]={frame->bonus_card_valid,frame->bonus_card_x,frame->bonus_card_y,frame->bonus_card_exit};
    s3_state_peek(io,offsetof(SceneFrame,bonus_card_valid),card,sizeof card);
    if(card[0]<0 || card[0]>1 || card[3]<0 || card[3]>1 ||
       card[1] < -1024 || card[1]>8192 || card[2] < -256 || card[2]>512)io->ok=0;
    AizPlanePose plane=frame->aiz_plane;
    s3_state_peek(io,offsetof(SceneFrame,aiz_plane),&plane,sizeof plane);
    if(!aiz_plane_valid(&plane))io->ok=0;
    S3_STATE(io, *frame);
}
void s3_video_state(S3StateIO *io)
{
    int mode = s_mode; double ratio = s_ratio;
    if (io->mode) {
        if (io->pos > io->size || sizeof mode + sizeof ratio > io->size - io->pos) { io->ok = 0; return; }
        memcpy(&mode, io->data + io->pos, sizeof mode);
        memcpy(&ratio, io->data + io->pos + sizeof mode, sizeof ratio);
        if (mode != s_mode || ratio != s_ratio) io->ok = 0;
    }
    S3_STATE(io, mode); S3_STATE(io, ratio);
    handoff_state(io);
    /* Published scene frames and loader ownership affect future frames.
     * Scanline scratch and decoded ROM caches are rebuilt on demand. */
    scene_frame_state(io, &s_build);
    for (unsigned i = 0; i < 3; ++i) scene_frame_state(io, &s_history[i]);
    scene_frame_state(io, &s_display_frame);
    int display = s_display != NULL; S3_STATE(io, display);
    unsigned serial=s_serial,upload=s_upload_serial;
    s3_state_peek(io,0,&serial,sizeof serial);
    s3_state_peek(io,sizeof serial,&upload,sizeof upload);
    if(upload>serial)io->ok=0;
    S3_STATE(io,s_serial);S3_STATE(io,s_upload_serial);S3_STATE(io,s_scene_tick);
    S3_STATE(io, s_tick_was_active); S3_STATE(io, s_tick_last);
    S3_STATE(io, s_tick_zone); S3_STATE(io, s_tick_serial);
    S3_STATE(io, s_tick_samples); S3_STATE(io, s_tick_updates);
    S3_STATE(io, s_tick_lag); S3_STATE(io, s_tick_multi); S3_STATE(io, s_publication_lag);
    S3_STATE(io, s_placements);
    if (s3_state_peek_unsigned(io, s_placement_count) > SCENE_PLACEMENTS) io->ok = 0;
    S3_STATE(io, s_placement_count); S3_STATE(io, s_placement_base);
    S3_STATE(io, s_loader_active); S3_STATE(io, s_visible_objects);
    if (s3_state_peek_unsigned(io, s_visible_count) > 144) io->ok = 0;
    S3_STATE(io, s_visible_count); S3_STATE(io, s_background_frame);
    /* The projection uses the last completed native sphere publication,
     * which can precede live collision RAM. Retain it for a cold/paused load. */
    S3_STATE(io, ss_state);
    if (s3_state_peek_unsigned(io, (unsigned)ss_latched) > 1) io->ok = 0;
    S3_STATE(io, ss_latched);
    if (io->mode == 2) {
        interpolation_reset();
        s_display = display ? &s_display_frame : NULL;
        s_frame_special = 0;
    }
}
static S3VideoFrameOverlay s_frame_overlay;
void s3_video_set_frame_overlay(S3VideoFrameOverlay draw) { s_frame_overlay = draw; }
/* Competition fills the hardware card banks with its two playfields and HUD.
 * Draw its unused native name-card layout from the cartridge instead. The
 * actual native child owns position/lifetime; no art or result flag is written. */
static void competition_card_scanline(const GVDP *v,int line,uint32_t *out,int width)
{
    unsigned zone=g_ram[0xFE10];
    if((g_ram[0xF600]&127)!=12 || zone<14 || zone>18 || !(v->reg[1]&64))return;
    static uint8_t art[5][2048],ready[5];
    unsigned course=zone-14,table=S3_ART_BANK?0x2DA16u:0x2CD22u;
    unsigned source=scene_read32(table+zone*4);
    if(!ready[course]) {
        if(art_kosm(source,0x800,art[course],sizeof art[course])!=sizeof art[course])return;
        ready[course]=1;
    }
    /* S&K inserted more story frames without updating its unused course
     * object. The locked-on S3 half retains the matching nine-piece layout. */
    unsigned map=S3_TITLE_CARD_MAP,layout=S3_ART_BANK+0x2D90Cu;
    unsigned entry=layout+scene_read16(layout+18*2);
    unsigned pieces=scene_read16(entry);if(pieces>16)return;
    for(unsigned o=0xB000;o<0xCAE2;o+=0x4A) {
        if(scene_read32(0xFF0000u+o+12)!=map || g_ram[o+0x22]!=18 ||
           scene_read32(0xFF0000u+o)!=(S3_ART_BANK?0x2D9CCu:0x2CCD8u))continue;
        int ox=(int16_t)ram16(o+16)-128+(width-320)/2,oy=(int16_t)ram16(o+20)-128;
        for(unsigned n=0;n<pieces;++n) {
            unsigned p=entry+2+n*6,shape=g_rom[p+1],tile=scene_read16(p+2);
            int x=ox+(int16_t)scene_read16(p+4),y=oy+(int8_t)g_rom[p];
            int cw=(shape>>2)+1,ch=(shape&3)+1,iy=line-y;
            if(iy<0 || iy>=ch*8)continue;
            if(tile&0x1000)iy=ch*8-1-iy;
            for(int dx=0;dx<cw*8;++dx) {
                if(x+dx<0 || x+dx>=width)continue;
                int ix=tile&0x800?cw*8-1-dx:dx;
                unsigned pattern=(tile&2047u)+(unsigned)(ix/8*ch+iy/8);
                if(pattern<0x500 || pattern>=0x540)continue;
                unsigned byte=art[course][(pattern-0x500)*32+(iy&7)*4+(ix&7)/2];
                unsigned color=(ix&1)?byte&15:byte>>4;if(!color)continue;
                color|=(tile>>9)&48;
                out[x+dx]=genesis_dac_cram_to_argb(v->cram[color],GENESIS_DAC_NORMAL);
            }
        }
    }
}
static void overlay_scanline(const struct GVDP *v, int line, uint32_t *out, int width)
{
    if(!enabled()) {
        life_ones_scanline(v,g_ram,line,out,width,(width-320)/2);
        time_tens_scanline(v,g_ram,line,out,width,(width-320)/2);
    }
    competition_card_scanline(v,line,out,width);
    if (s_frame_overlay) s_frame_overlay(line, out, width);
}

/* Owned, disposable host snapshots. They never enter a save/RB stream. */
enum { IX_LINES=224, IX_BANKS=2 };
typedef struct {
    RenderContext context;
    SceneFrame scene;
    uint32_t *image;
    size_t capacity;
    int width,lines,native_width,valid;
    unsigned tick;
    int16_t hs[IX_LINES][2];
    uint16_t cram[IX_LINES][64],fg_scroll[IX_LINES];
    uint16_t ext_colors[IX_LINES][48];
    uint8_t patterns[IX_BANKS][65536],plane_lines[2048];
    uint8_t sprite_lines[3][4][2048];
    unsigned pattern_bytes[IX_BANKS];
} InterpSnapshot;
static InterpSnapshot s_ix[2];
static unsigned s_ix_front;
static uint8_t *s_ix_before_overlay;
static int s_ix_overlay_capacity;
static InterpSnapshot *ix_current(void) { return &s_ix[s_ix_front]; }
static InterpSnapshot *ix_previous(void) { return &s_ix[s_ix_front^1u]; }
static void interpolation_reset(void)
{
    ss_ready=ss_latched=0;
    for(unsigned n=0;n<2;++n){s_ix[n].valid=0;s_ix[n].lines=0;}
    for(unsigned n=0;n<3;++n)s_ix_history[n].serial=0;
    s_ix_build.serial=s_ix_serial=s_ix_upload_serial=0;
}

static void presentation_reset(void)
{
    int preserve=s_native_publication_restored;s_native_publication_restored=0;
    interpolation_reset();
    /* Rollback restores chips and WRAM, not host scene publications. Fade
     * retention must never borrow a scene from the abandoned future. The
     * native-center fallback remains until the restored producer publishes. */
    s_render=&s_real_context;s_interpolating=0;s_ix_overlay_seen=0;
    /* Native quickstates restore their own matching publication. Raw rollback
     * has no such data; retire only those abandoned future publications. */
    if(!preserve) {
        memset(&s_build,0,sizeof s_build);
        memset(s_history,0,sizeof s_history);
        memset(&s_display_frame,0,sizeof s_display_frame);s_display=NULL;
        s_upload_serial=0;
    }
    s_capture_frame=&s_build;s_capture_readonly=0;s_capture_slot=0;
    s_real_context.level=s_real_context.special=s_real_context.bonus=0;
    s_real_context.menu=MENU_NONE;
    s_bg_draw_ready=0;
    /* Keep ROM caches, aspect/settings, loader state and the background copy
     * separately restored by the native quickstate reader. */
}
static void interpolation_configure(int on)
{
    on=on!=0;
    if(on!=s_interpolation_enabled){interpolation_reset();s_interpolation_enabled=on;}
}
static void interpolation_native_publish(void)
{
    s_ix_build.serial=++s_ix_serial;
    memcpy(s_ix_build.sat,g_ram+0xF800,sizeof s_ix_build.sat);
    s_ix_history[s_ix_serial%3]=s_ix_build;
}
static const SceneFrame *interpolation_native_scene(const GVDP *v)
{
    const SceneFrame *best=NULL;
    unsigned sat=(v->reg[5]&127u)<<9;
    for(unsigned n=0;n<3;++n) {
        const SceneFrame *f=&s_ix_history[n];
        if(!f->serial || (best && f->serial<=best->serial) ||
           (s_ix_upload_serial && f->serial>s_ix_upload_serial))continue;
        unsigned k=0;
        while(k<640 && f->sat[k]==v->vram[(sat+k)&65535u])++k;
        if(k==640)best=f;
    }
    return best;
}
static int presentation_plane_line(unsigned tile)
{ return s_interpolating?ix_current()->plane_lines[tile&2047u]:gvdp_ext_line(tile); }
static uint16_t presentation_ext_color(int row,int line,int nib)
{
    if(!s_interpolating)return gvdp_ext_color(row,line,nib);
    if(row<0 || row>=IX_LINES || line<1 || line>3 || nib<0 || nib>=16)return 0;
    return ix_current()->ext_colors[row][(line-1)*16+nib];
}
static const uint8_t *presentation_sprite_patterns(unsigned flags,unsigned *bytes)
{
    if(!s_interpolating)return gvdp_sprite_patterns(flags,bytes);
    unsigned bank=(flags>>6)&1u;
    *bytes=(flags&128u)?ix_current()->pattern_bytes[bank]:0;
    return *bytes?ix_current()->patterns[bank]:NULL;
}
static int presentation_sprite_line(unsigned flags,unsigned tile,unsigned source)
{
    if(!s_interpolating)return gvdp_sprite_palette_line(flags,tile,source);
    unsigned bank=(flags&128u)?1u+((flags>>6)&1u):0;
    return ix_current()->sprite_lines[bank][source&3u][tile&2047u];
}
static unsigned presentation_cram(int row,unsigned color,unsigned fallback)
{ return s_interpolating && row>=0 && row<IX_LINES?ix_current()->cram[row][color]:fallback; }
static unsigned presentation_fg_scroll(int row,unsigned fallback)
{
    if(!s_interpolating || row<0 || row>=IX_LINES)return fallback;
    return (unsigned)interpolation_lerp(ix_previous()->fg_scroll[row],ix_current()->fg_scroll[row],128);
}
static unsigned presentation_fg_identity(int row,unsigned fallback)
{
    return s_interpolating && row>=0 && row<IX_LINES?
        ix_current()->fg_scroll[row]:fallback;
}
static int presentation_bg_scroll(int fallback)
{
    return s_interpolating?interpolation_lerp(ix_previous()->context.video.vsram[1],
        ix_current()->context.video.vsram[1],128):fallback;
}
static unsigned presentation_column_scroll(unsigned column,unsigned fallback)
{
    if(!s_interpolating || column>=40)return fallback;
    int b=ix_current()->context.video.vsram[column];
    int a=unwrap(ix_previous()->context.video.vsram[column],b);
    return (unsigned)interpolation_lerp(a,b,128);
}
static void presentation_scroll(int row,int *a,int *b)
{
    if(s_interpolating && row>=0 && row<IX_LINES) {
        *a=interpolation_lerp(ix_previous()->hs[row][0],ix_current()->hs[row][0],128);
        *b=interpolation_lerp(ix_previous()->hs[row][1],ix_current()->hs[row][1],128);
    }
}
static int presentation_camera(int axis,int fallback)
{
    if(!s_interpolating)return fallback;
    return axis?interpolation_lerp(ix_previous()->context.fg_y,ix_current()->context.fg_y,128):
                interpolation_lerp(ix_previous()->context.fg_x,ix_current()->context.fg_x,128);
}
static void presentation_object_delta(const SceneSprite *sprite,int *dx,int *dy)
{
    unsigned slot=sprite_slot(sprite);
    const SceneFrame *a=&ix_previous()->scene,*b=&ix_current()->scene;
    if(!slot || slot>=SCENE_SLOTS || !b->code[slot] || a->code[slot]!=b->code[slot])return;
    int ax=a->ox[slot],ay=a->oy[slot];
    if(b->code[slot]==IMPORTED_S1_PYLON) {
        ax=b->ox[slot]+((ax-b->ox[slot]+256)&511)-256;
        ay=b->oy[slot]+((ay-b->oy[slot]+32)&63)-32;
    }
    *dx=interpolation_lerp(ax,b->ox[slot],48)-b->ox[slot];
    *dy=interpolation_lerp(ay,b->oy[slot],48)-b->oy[slot];
}
static void presentation_aiz_plane_delta(int *dx,int *dy)
{
    const AizPlanePose *a=&ix_previous()->scene.aiz_plane,*b=&ix_current()->scene.aiz_plane;
    if(!a->phase || b->phase!=2 || a->slot!=b->slot)return;
    *dx=interpolation_lerp(a->object[0].x,b->object[0].x,48)-b->object[0].x;
    *dy=interpolation_lerp(a->object[0].y,b->object[0].y,48)-b->object[0].y;
}
static void presentation_overlay_begin(int width)
{
    if(!s_interpolation_enabled)return;
    if(width>s_ix_overlay_capacity) {
        uint8_t *p=(uint8_t*)realloc(s_ix_before_overlay,(size_t)width);
        if(!p){s_ix_overlay_seen=1;return;}
        s_ix_before_overlay=p;s_ix_overlay_capacity=width;
    }
    memcpy(s_ix_before_overlay,s_priority,(size_t)width);
}
static void presentation_overlay_end(int width)
{
    if(!s_interpolation_enabled || width>s_ix_overlay_capacity)return;
    for(int x=0;x<width;++x)
        if((s_priority[x]&2) && !(s_ix_before_overlay[x]&2)){s_ix_overlay_seen=1;break;}
}
static void interpolation_capture(const GVDP *v,int line,const uint32_t *image,
                                  int width,int native_width,const GVDPRasterRow *raster)
{
    (void)raster;
    if(!s_interpolation_enabled || line<0 || line>=IX_LINES || width<320)return;
    if(line==0) {
        s_ix_front^=1u;
        InterpSnapshot *f=ix_current();
        size_t pixels=(size_t)width*IX_LINES;
        f->valid=0;f->lines=0;f->width=width;f->native_width=native_width;f->tick=ram16(0xFE04);
        if(pixels>f->capacity) {
            uint32_t *p=(uint32_t*)realloc(f->image,pixels*sizeof *p);
            if(!p)return;
            f->image=p;f->capacity=pixels;
        }
        const SceneFrame *scene=enabled()?s_display:interpolation_native_scene(v);
        if(enabled())f->context=s_real_context;
        else {
            RenderContext *c=&f->context;
            memset(c,0,sizeof *c);c->video=*v;
            memcpy(c->world,g_ram,sizeof c->world);memcpy(c->background,g_ram,sizeof c->background);
            c->level=level_ready();c->zone=g_ram[0xFE10];c->act=g_ram[0xFE11];
            c->fg_x=ram16(0xEE80);c->fg_y=ram16(0xEE84);c->bg_y=ram16(0xEE90);
            c->bg_event=ram16(0xEEC2);c->tree_reveal=ram16(0xEEC4);
            c->bg_band[0]=(int16_t)ram16(0xA802);c->bg_band[1]=(int16_t)ram16(0xA806);
            c->width=width;
            if(c->level && !c->zone && !c->act)load_aiz_art();
        }
        /* Rigid-foreground actors follow installed, undeformed A scroll.
         * Keep that same camera basis in the owned fractional snapshots;
         * the RAM copy may already be preparing the next native frame. */
        RenderContext *c=&f->context;
        if(installed_actor_camera(&c->video,c->level,c->zone,c->act,c->bg_event)) {
            unsigned base=(c->video.reg[13]&63u)<<10;
            c->fg_x=unwrap(-(int16_t)vram16(&c->video,base),c->fg_x);
            c->fg_y=unwrap(c->video.vsram[0],c->fg_y);
        } else if(c->level && scene && scene->scene==1 &&
                  (enabled()?s_upload_serial:s_ix_upload_serial)) {
            /* Heat/water deformation belongs to the planes. The rigid
             * object camera belongs to this uploaded display list. */
            c->fg_x=scene->camera_x;c->fg_y=scene->camera_y;
        }
        if(scene)f->scene=*scene;else memset(&f->scene,0,sizeof f->scene);
        /* Finite/native projections, scene replacement and host-only crowd
         * overlays retain their exact completed image rather than inventing
         * fractional art. Native CD geometry needs its own publication owner. */
        f->valid=native_width==320 && f->context.level && scene && scene->scene==1 &&
            !f->context.special && !f->context.bonus && !f->context.menu &&
            !(v->reg[12]&14) && (enabled() || !s_world_publish) && !s_ix_overlay_seen &&
            !s_actor_interpolation_blocked;
        for(unsigned bank=0;bank<IX_BANKS;++bank) {
            unsigned bytes=0;const uint8_t *patterns=gvdp_sprite_patterns(128u|(bank<<6),&bytes);
            if(bytes>65536)bytes=0;
            f->pattern_bytes[bank]=patterns?bytes:0;
            if(patterns && bytes)memcpy(f->patterns[bank],patterns,bytes);
        }
        for(unsigned tile=0;tile<2048;++tile) {
            f->plane_lines[tile]=(uint8_t)gvdp_ext_line(tile);
            for(unsigned bank=0;bank<3;++bank)for(unsigned source=0;source<4;++source)
                f->sprite_lines[bank][source][tile]=(uint8_t)gvdp_sprite_palette_line(
                    bank?128u|((bank-1u)<<6):0,tile,source);
        }
        s_ix_overlay_seen=0;
    }
    InterpSnapshot *f=ix_current();
    if(!f->image || f->capacity<(size_t)width*IX_LINES || f->width!=width)return;
    memcpy(f->image+(size_t)line*width,image,(size_t)width*sizeof *image);
    memcpy(f->cram[line],v->cram,sizeof f->cram[line]);f->fg_scroll[line]=v->vsram[0];
    unsigned base=(f->context.video.reg[13]&63u)<<10,mode=f->context.video.reg[11]&3;
    unsigned row=mode==0?0:mode==2?(unsigned)line&~7u:(unsigned)line;
    f->hs[line][0]=(int16_t)vram16(&f->context.video,base+row*4);
    f->hs[line][1]=(int16_t)vram16(&f->context.video,base+row*4+2);
    for(int ext=1;ext<=3;++ext)for(int n=0;n<16;++n)
        f->ext_colors[line][(ext-1)*16+n]=gvdp_ext_color(line,ext,n);
    f->lines=line+1;
    if(s_ix_overlay_seen)f->valid=0;
}
static int interpolate(double phase,uint32_t *image,int stride,int height)
{
    InterpSnapshot *f=ix_current(),*p=ix_previous();
    if(!s_interpolation_enabled || !f->valid || !p->valid || f->lines!=IX_LINES ||
       p->lines!=IX_LINES || f->width!=p->width || height!=IX_LINES || stride<f->width ||
       f->context.zone!=p->context.zone || f->context.act!=p->context.act ||
       (unsigned)(uint16_t)(f->tick-p->tick)>2)return 0;
    if(phase<=0 || phase>=1) {
        const InterpSnapshot *source=phase<=0?p:f;
        for(int y=0;y<IX_LINES;++y)memcpy(image+(size_t)y*stride,source->image+(size_t)y*f->width,(size_t)f->width*sizeof *image);
        return 1;
    }
    if(f->width>s_priority_capacity) {
        uint8_t *q=(uint8_t*)realloc(s_priority,(size_t)f->width);
        if(!q)return 0;s_priority=q;s_priority_capacity=f->width;
    }
    RenderContext *saved_render=s_render;
    const SceneFrame *saved_scene=s_display;
    s_render=&f->context;s_display=&f->scene;s_interpolating=1;s_interpolation_phase=phase;
    for(int y=0;y<IX_LINES;++y) {
        memcpy(f->context.video.cram,f->cram[y],sizeof f->cram[y]);
        render_scanline(&f->context.video,y,f->image+(size_t)y*f->width,f->native_width,
            image+(size_t)y*stride,f->width,NULL,NULL,NULL);
    }
    memcpy(f->context.video.cram,f->cram[0],sizeof f->cram[0]);
    s_interpolating=0;s_display=saved_scene;s_render=saved_render;
    return 1;
}

#include "sonic3_rewind.inc"

const GameVideo sonic3_video = {
    .configure = configure,
    .enabled = enabled,
    .width = width,
    .scanline = scanline,
    .overlay_scanline = overlay_scanline,
    .raster_enabled = menu_raster_enabled,
    .scanline_presented = scanline_lazy,
    .presentation_configure = interpolation_configure,
    .presentation_capture = interpolation_capture,
    .interpolate = interpolate,
    .presentation_reset = presentation_reset,
    .presentation_state_save = presentation_state_save,
    .presentation_state_load = presentation_state_load
};
