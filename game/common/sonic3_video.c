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
enum { S3_COMPETITION=0xFFD8, S3_PLACEMENTS=0x5E0D8, S3_RINGS=0x5E198,
       S3_HUD_MAP=0xE932, S3_RING_MAP=0xF87E,
       S3_LOAD_INIT=0x19CF2, S3_LOAD_UPDATE=0x19DD0,
       S3_CAPTURE=0x1938E, S3_PUBLISH=0x194B8,
       S3_TITLE_PLANE_MAP=0x4620, S3_FILE_SELECT_MAP=0xBD68,
       S3_MENU_BG_MAP=0x19D2A2,
       S3_TITLE_PLANE_SIZE=0x11,
       S3_AIZ_MINIBOSS_CUTSCENE=0x46442,
       S3_RING_RANGE=0xF6D8, S3_RING_STATUS=0xF6E2,
       S3_ART_BANK=0, S3_PAL_INTRO=0x8C314, S3_PAL_FOREST=0x8C374,
       S3_MOVING_PLATFORM_CULL=0x23E1E, S3_FLOATING_PLATFORM_CULL=0x2452A,
       S3_SWING_PLATFORM_CULL=0x24646,
       S3_HUD_LIFE_VALUE=0xEE72, S3_LIFE_DIGITS=0xF1D2 };
#else
enum { S3_COMPETITION=0xFFE8, S3_PLACEMENTS=0x1E3D98, S3_RINGS=0x1E3E58,
       S3_HUD_MAP=0xDBB6, S3_RING_MAP=0xEBEE,
       S3_LOAD_INIT=0x1B690, S3_LOAD_UPDATE=0x1B7F2,
       S3_CAPTURE=0x1AD20, S3_PUBLISH=0x1AE56,
       S3_TITLE_PLANE_MAP=0x4FE8, S3_FILE_SELECT_MAP=0xCE0E,
       S3_MENU_BG_MAP=0x39D2A2,
       S3_TITLE_PLANE_SIZE=1,
       S3_AIZ_MINIBOSS_CUTSCENE=0x684EC,
       S3_RING_RANGE=0xEA32, S3_RING_STATUS=0xEA3C,
       S3_ART_BANK=0x200000, S3_PAL_INTRO=0xA8B1C, S3_PAL_FOREST=0xA8B7C,
       S3_MOVING_PLATFORM_CULL=0x24F28, S3_FLOATING_PLATFORM_CULL=0x25634,
       S3_SWING_PLATFORM_CULL=0x25750,
       S3_HUD_LIFE_VALUE=0xE12A, S3_LIFE_DIGITS=0xE48A };
#endif

enum { VIDEO_OFF, VIDEO_FIT, VIDEO_RATIO, VIDEO_STAGE };
static int s_mode;
static void ss_capture(void);
static S3VideoObjectZoom s_object_zoom;
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
static unsigned presentation_cram(int row,unsigned color,unsigned fallback);
static void presentation_scroll(int row,int *a,int *b);
static int presentation_camera(int axis,int fallback);
static int presentation_bg_scroll(int fallback);
static void presentation_overlay_begin(int width);
static void presentation_overlay_end(int width);
static int presentation_plane_line(unsigned tile);
static uint16_t presentation_ext_color(int row,int line,int nib);
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
typedef struct { const uint8_t *p,*end; unsigned desc,bits; int error; } KosReader;
static unsigned kos_byte(KosReader *r)
{
    if(r->p==r->end){r->error=1;return 0;}
    return *r->p++;
}
static unsigned kos_bit(KosReader *r)
{
    unsigned bit=r->desc&1u;r->desc>>=1;
    if(!--r->bits) {
        unsigned low=kos_byte(r);r->desc=low|(kos_byte(r)<<8);r->bits=16;
    }
    return bit;
}
static size_t art_kos(const uint8_t *src,size_t size,uint8_t *out,size_t capacity,size_t *used)
{
    KosReader r={src,src+size,0,16,0};size_t n=0;
    unsigned low=kos_byte(&r);r.desc=low|(kos_byte(&r)<<8);
    while(!r.error) {
        if(kos_bit(&r)) {
            unsigned value=kos_byte(&r);
            if(r.error || n==capacity)return 0;
            out[n++]=(uint8_t)value;continue;
        }
        unsigned count;int offset;
        if(kos_bit(&r)) {
            unsigned lo=kos_byte(&r),hi=kos_byte(&r);
            offset=(int)(((hi&248u)<<5)|lo)-8192;count=hi&7u;
            if(!count) {
                count=kos_byte(&r);
                if(r.error)return 0;
                if(!count){*used=(size_t)(r.p-src);return n;}
                if(count==1)continue;
                ++count;
            } else count+=2;
        } else {
            count=kos_bit(&r)<<1;count=(count|kos_bit(&r))+2;
            offset=(int)kos_byte(&r)-256;
        }
        if(r.error || (size_t)-offset>n || count>capacity-n)return 0;
        while(count--){out[n]=out[n-(size_t)-offset];++n;}
    }
    return 0;
}
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
static int stage_width(void)
{
    int w=ram16(0x8000)*128;
    return w>=320 && w<=32768?w:320;
}
static int level_ready(void)
{
    unsigned w=ram16(0x8000),row=ram16(0x8008);
    return gameplay() && !(g_ram[0xF600]&128) && w>=3 && w<=256 && row>=0x8088 && row<0x9000;
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
static int enabled(void) { return s_mode != VIDEO_OFF || (s_object_zoom && s_object_zoom(0)); }
unsigned s3_video_main_cpu_divisor(void)
{
    /* LevelLoop ($650C) still waits for the real V-int every tick.
     * Expanded activation must not inherit the 7.67 MHz object's lag budget.
     * Loading, special stages, native 2P and opt-out keep original timing. */
    int mode=g_ram[0xF600];
    return s_mode != VIDEO_OFF && (mode==8 || mode==12) && gameplay() && g_ram[0xF711] ? 4 : 1;
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
static uint16_t world_attr_blocks(const uint8_t *ram,const uint8_t *blocks,int wx,int wy,int background)
{
    uint16_t block;
    if (!(s_world_cell && s_world_cell(ram,wx,wy,background,&block))) {
        unsigned header=background?0x8002u:0x8000u;
        int width=((ram[header]<<8)|ram[header+1])*128;
        if(width<=0 || (!background && (wx<0 || wx>=width)))return 0;
        unsigned row=((unsigned)wy>>5)&0x7Cu;
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
    int tx=(wx>>3)&1,ty=(wy>>3)&1;
    if(block&0x400)tx^=1;
    if(block&0x800)ty^=1;
    unsigned address=0x9000u+(block&1023u)*8u+(unsigned)(ty*2+tx)*2u;
    if(address+1>=0xA800)return 0;
    address-=0x9000;
    return (uint16_t)(((blocks[address]<<8)|blocks[address+1])^((block&0xC00u)<<1));
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
static int file_select_layout(const GVDP *v)
{
    /* Keep the outgoing menu through its fade, after Game_mode changes.
     * SaveScreen alone installs this full-screen Window/128-column B pair. */
    return (v->reg[12]&1) && (v->reg[2]&56)==0x38 && (v->reg[3]&62)==0x38 &&
        (v->reg[4]&7)==6 && v->reg[18]==0x80 && (v->reg[16]&3)==3;
}
enum { MENU_NONE, MENU_TITLE_ANIMATION, MENU_TITLE, MENU_SAVE, MENU_COMPETITION };
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
typedef struct { int x, y; uint16_t attr; uint8_t size, hud; uint16_t zoom,slot; } SceneSprite;
enum { SCENE_SPRITES = 32768, SCENE_PLACEMENTS = 1024, SCENE_SLOTS = 112 };
typedef struct {
    SceneSprite sprites[SCENE_SPRITES];
    unsigned count, serial;
    uint8_t sat[640];
    int scene; /* 1 level, 2 special, 3 native bonus, 0 menu */
    int title; /* provenance survives Game_mode's outgoing fade transition */
    int camera_x, camera_y; /* coordinates used when capturing world sprites */
    int16_t ox[SCENE_SLOTS],oy[SCENE_SLOTS];
    uint32_t code[SCENE_SLOTS];
    int preview_knux; /* waiting pose belongs to this completed display list */
} SceneFrame;
static SceneFrame s_build, s_history[3], s_display_frame;
static const SceneFrame *s_display;
static void presentation_object_delta(const SceneSprite *sprite,int *dx,int *dy);
static unsigned s_serial, s_scene_tick, s_spawned, s_pool_pressure;
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
void s3_video_vblank(void)
{
    if(s_mode!=VIDEO_OFF)ss_capture();
    if(s_interpolation_enabled && !enabled() && g_ram[0xF62A])s_ix_upload_serial=s_ix_serial;
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
static int view_left(int camera, int w)
{
    int end = stage_width()-w, left = camera-(w-320)/2;
    if (end<0) return end/2;
    if (left<0) left=0;
    return left>end ? end : left;
}
int s3_video_presentation_camera_x(int camera)
{
    if(s_mode==VIDEO_OFF || s_requested_width<=320)return camera;
    return view_left(camera,s_requested_width)+(s_requested_width-320)/2;
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
        dx=zoom_offset(dx,zoom);dy=zoom_offset(dy,zoom);
        if(y+dy>=224 || y+dy+((size&3)+1)*8*(int)zoom/256<=0)continue;
        SceneSprite *q=&s_capture_frame->sprites[s_capture_frame->count++];
        q->x=x+dx;q->y=y+dy;q->attr=attr;q->size=(uint8_t)size;q->hud=(uint8_t)anchor;q->zoom=(uint16_t)zoom;q->slot=(uint16_t)s_capture_slot;
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
            if(((int)p->x<((camera-128)&~127) || (int)p->x>=(camera&~127)+640) &&
                placement_code(p)==S3_AIZ_MINIBOSS_CUTSCENE)continue;
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
        for(o=0xB0DE;o<0xCAE2 && scene_read32(0xFF0000u+o);o+=0x4A){}
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
static void capture_objects(void)
{
    int prior_preview=s_capture_frame->preview_knux;
    s_capture_frame->count=0;s_capture_slot=0;
    memset(s_capture_frame->code,0,sizeof s_capture_frame->code);
    if(!s_capture_readonly){s_visible_count=0;++s_scene_tick;}
    s_capture_frame->scene=bonus_stage()?3:level_ready()?1:0;
    s_capture_frame->title=!s_capture_frame->scene && (g_ram[0xF600]&127)==4;
    s_capture_frame->preview_knux=s_capture_frame->scene==1 && s3_knux_preview_needed(g_ram);
#ifndef SONIC3_STANDALONE
    /* Allocation can target an earlier slot than the controller. Keep the
     * waiting pose for that one queued-display handover until the real actor
     * below has actually reached Draw_Sprite. The tag is saved with the scene. */
    if(prior_preview && s_capture_frame->scene==1 && g_ram[0xF600]==12 &&
       ram16(0xFE10)==1 && g_ram[0xB038]!=2)
        for(unsigned o=0xB000;o<0xCFCC;o+=0x4A)
            if(scene_read32(0xFF0000u+o)==0x69588)s_capture_frame->preview_knux=1;
#else
    (void)prior_preview;
#endif
    /* These stages stream machine/reel art independently of world layouts.
     * Publish only their native SAT identity so entry/exit fades retain the
     * correct composition; never add ordinary stage rings or mappings. */
    if(s_capture_frame->scene==3)return;
    int cam=ram16(0xEE80),cy=ram16(0xEE84);
    s_capture_frame->camera_x=cam;s_capture_frame->camera_y=cy;
    int left=gameplay()?view_left(cam,s_requested_width):cam-(s_requested_width-320)/2;
    int mask=ram16(0xEEAA);
    if(gameplay() && g_ram[0xF711]) {
        unsigned blink=(g_ram[0xFE05]&8)==0;
        unsigned frame=blink*((ram16(0xFE20)==0?1:0)+(g_ram[0xFE23]==9?2:0));
        int hudx=(int8_t)g_ram[0xF711];if(hudx<0)hudx+=8;
        unsigned hud=s_hud_mapping?s_hud_mapping():0;
        add_mapping(hud?hud:S3_HUD_MAP,frame,0x86CA,0,15+hudx,136,1,0,0);
        unsigned base=rings_base(),end=rings_end();
        for(unsigned p=base+4;p<end;p+=4) {
            unsigned status=0xE700+(p-base)/2;
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
            unsigned flags=g_ram[o+4],multi=flags&64;
            int x,y,anchor=0;
            if(flags&4) {
                x=(int16_t)(ram16(o+0x10)-cam);
                int radius=g_ram[o+7],height=g_ram[o+6];
                y=((ram16(o+0x14)-cy+height)&mask)-height;
                if(x+radius<left-cam || x-radius>=left-cam+s_requested_width || y+height<0 || y-height>=224)continue;
                if(!s_capture_readonly && s_visible_count<144)s_visible_objects[s_visible_count++]=o;
            } else {
                x=(int16_t)ram16(o+0x10)-128;y=(int16_t)ram16(o+0x14)-128;
                anchor=s_hud_object && s_hud_object(o)?1:2;
            }
            unsigned map=scene_read32(0xFF0000u+o+0xC),gfx=ram16(o+0xA);
            if(map==0x364016)s_capture_frame->preview_knux=0;
            unsigned zoom=(flags&4) && s_object_zoom?s_object_zoom(o):0;
            /* The title plane is queued even beyond the hardware viewport.
             * Retain its signed position instead of the wrapped/cut SAT. */
            if((g_ram[0xF600]&127)==4 && map==S3_TITLE_PLANE_MAP)anchor=3;
            /* Data Select queues every card, including those culled by the
             * native SAT builder. Preserve their signed carousel positions. */
            if(map==S3_FILE_SELECT_MAP)anchor=4;
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
        }
    }
}

static void publish_sprites(void)
{
    for(unsigned n=0;n<s_visible_count;++n) {
        unsigned o=s_visible_objects[n]; if(scene_read32(0xFF0000u+o))write8(o+4,g_ram[o+4]|128);
    }
    s_build.serial=++s_serial;memcpy(s_build.sat,g_ram+0xF800,640);
    SceneFrame *dst=&s_history[s_serial%3];
    dst->count=s_build.count;dst->serial=s_serial;dst->scene=s_build.scene;dst->title=s_build.title;
    dst->camera_x=s_build.camera_x;dst->camera_y=s_build.camera_y;
    dst->preview_knux=s_build.preview_knux;
    memcpy(dst->ox,s_build.ox,sizeof dst->ox);memcpy(dst->oy,s_build.oy,sizeof dst->oy);memcpy(dst->code,s_build.code,sizeof dst->code);
    memcpy(dst->sat,s_build.sat,640);
    memcpy(dst->sprites,s_build.sprites,s_build.count*sizeof(SceneSprite));
}
#include "sonic3_lives.inc"

int s3_video_hook(uint32_t pc)
{
    if(pc==S3_HUD_LIFE_VALUE) {
        /* Change only the formatter argument, never the earned counter. */
        if(g_cpu.D[1]>=100)g_cpu.D[1]/=10;
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
    if(pc==S3_LOAD_UPDATE && gameplay() && (enabled() || s_loader_active) && g_ram[0xFE10]<19) {
        spawn_scene();return 1;
    }
    if(!enabled() || ram16(S3_COMPETITION))return 0;
    switch(pc) {
    case S3_LOAD_INIT:return 0;
    case S3_CAPTURE:capture_objects();return 0;
    case S3_PUBLISH:publish_sprites();return 0;
    case S3_RING_RANGE:
        if(gameplay()) {g_cpu.A[1]=rings_base()+4;g_cpu.A[2]=rings_end();}
        return 0;
    case S3_RING_STATUS:
        if(gameplay())g_cpu.A[4]=0xFFFFE702u;
        return 0;
    default:
        if(gameplay()) {
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
    if(retain_level && s_display_frame.serial && s_display_frame.scene==1) {
        s_display=&s_display_frame;++s_scene_holds;return;
    }
    s_display=NULL;
    unsigned sat=(v->reg[5]&127u)<<9;
    /* Keep the front buffer's publication alive even if the producer cycles
     * through every history slot before the next native sprite DMA. */
    for(unsigned n=0;n<4;++n) {
        const SceneFrame *p=n==3?&s_display_frame:&s_history[n];
        if(!p->serial || (s_display && p->serial<s_display->serial))continue;
        int matches=1;
        for(unsigned k=0;k<640;++k)
            if(p->sat[k]!=v->vram[(sat+k)&65535u]) {matches=0;break;}
        if(matches)s_display=p;
    }
    if(!s_display) {
        ++s_scene_misses;
        int scene=bonus_stage()?3:level_ready()?1:(g_ram[0xF600]&127)==0x34?2:0;
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
        s_display_frame.title=s_display->title;
        s_display_frame.serial=s_display->serial;
        s_display_frame.camera_x=s_display->camera_x;s_display_frame.camera_y=s_display->camera_y;
        s_display_frame.preview_knux=s_display->preview_knux;
        memcpy(s_display_frame.ox,s_display->ox,sizeof s_display_frame.ox);memcpy(s_display_frame.oy,s_display->oy,sizeof s_display_frame.oy);memcpy(s_display_frame.code,s_display->code,sizeof s_display_frame.code);
        memcpy(s_display_frame.sat,s_display->sat,sizeof s_display_frame.sat);
        memcpy(s_display_frame.sprites,s_display->sprites,s_display->count*sizeof(SceneSprite));
        s_display=&s_display_frame;
    }
}
static void draw_scene_sprites(const GVDP *v,int line,uint32_t *out,int width,
                               int origin,const uint32_t *palette)
{
    if(!s_display)return;
    for(unsigned n=0;n<s_display->count;++n) {
        const SceneSprite *s=&s_display->sprites[n];
        int menu=s_display->scene==0;
        if(menu && s->hud!=(file_select_layout(v)?4:3))continue;
        int cw=((s->size>>2)&3)+1,ch=(s->size&3)+1;
        unsigned zoom=s->zoom?s->zoom:256;
        int drawn_width=cw*8*(int)zoom/256,drawn_height=ch*8*(int)zoom/256;
        unsigned pattern_bytes;
        const uint8_t *patterns=presentation_sprite_patterns(s->size,&pattern_bytes);
        int world=!s->hud && s_display->scene==1;
        int dx=0,dy=0;
        if(world && s_interpolating)presentation_object_delta(s,&dx,&dy);
        int y=s->y+(world?s_display->camera_y-s_camera_y:0)+dy;
        if(line<y || line>=y+drawn_height)continue;
        int base=s->x+(s->hud==1?0:s->hud>=2?(width-320)/2:origin)+dx;
        /* Native camera scroll may advance while a completed host frame is
         * retained. Reproject its world coordinates, not its old screen
         * coordinates; otherwise stationary rings jitter with the camera. */
        if(world)base+=s_display->camera_x-s_camera;
        int iy=(line-y)*256/(int)zoom;
        if(s->attr&0x1000)iy=ch*8-1-iy;
        for(int i=0;i<drawn_width;++i) {
            int x=base+i;
            if(x<0 || x>=width || (s_priority[x]&2))continue;
            if(menu && x>=origin && x<origin+320)continue;
            int ix=i*256/(int)zoom;
            if(s->attr&0x800)ix=cw*8-1-ix;
            uint16_t cell=(uint16_t)((s->attr&0xE000u)|
                (((s->attr&2047u)+(unsigned)(ix/8*ch+iy/8))&2047u));
            uint8_t p;
            if(patterns) {
                unsigned a=(cell&2047u)*32u+(unsigned)(iy&7)*4u+(unsigned)(ix&7)/2u;
                unsigned byte=a<pattern_bytes?patterns[a]:0;
                p=(uint8_t)(((ix&1)?byte&15:byte>>4)|((cell>>9)&48));
                if(!(p&15))continue;
            } else p=pattern_pixel(v,cell,ix,iy);
            if(!p)continue;
            s_priority[x]|=2;
            /* extended sprite lines (game-added art keeping its own colours) */
            int ext=presentation_sprite_line(s->size,cell&2047u,p>>4);
            uint32_t c=ext?genesis_dac_cram_to_argb(presentation_ext_color(line,ext,p&15),GENESIS_DAC_NORMAL):palette[p];
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
static void aiz_palette(const AizArt *art,const GVDP *v,uint32_t *out)
{
    /* Follow fades without borrowing the cutscene's unrelated colour banks.
     * AIZ's common black/white entries also cover fades to/from white. */
    for(unsigned i=0;i<64;++i) {
        unsigned color=i<16?v->cram[i]:art->colors[i-16],faded=0;
        for(unsigned shift=0;shift<12;shift+=4) {
            int lo=(v->cram[16]>>shift)&14,hi=(v->cram[17]>>shift)&14;
            int c=lo+(int)((color>>shift)&14)*(hi-lo)/14;
            faded|=(unsigned)c<<shift;
        }
        out[i]=genesis_dac_cram_to_argb((uint16_t)faded,GENESIS_DAC_NORMAL);
    }
}

static unsigned menu_plane(uint16_t attr,unsigned pixel)
{ return pixel?(pixel|64u|((attr&0x8000u)?128u:0u)):0; }
static void render_menu(const GVDP *v,int line,int nw,uint32_t *out,int width,
                        const uint32_t *palette,const GVDPRasterRow *raster)
{
    int origin=(width-nw)/2;
    unsigned hsbase=(v->reg[13]&63u)<<10;
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
        if(nx>=0 && nx<nw) {
            a=raster->plane_a[nx];
            if(s_frame_menu==MENU_TITLE_ANIMATION)b=raster->plane_b[nx];
            sp=raster->sprite[nx];opaque=raster->opaque[nx];high=raster->high[nx];
            shadow_op=raster->shadow_op[nx];hilite_op=raster->hilite_op[nx];
        }
        unsigned ah=(a&192u)==192u,bh=(b&192u)==192u,sh=high&&opaque;
        unsigned pixel=sh?sp:ah?a&63u:bh?b&63u:opaque?sp:
            (a&64u)?a&63u:(b&64u)?b&63u:raster->backdrop;
        unsigned shade=GENESIS_DAC_NORMAL;
        if(raster->shadow_mode && pixel<64) {
            int dark=!(ah||bh||sh);
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
        s_ix_overlay_seen=0;
        /* A mode-changing exit can publish a menu-tagged copy of the same
         * SAT before its old level fades. Keep the installed level through
         * that fade while its plane configuration remains unchanged; a flat
         * palette or disabled display retires it before the next screen. */
        int retain_level=s_frame_level && !level_ready() && !uniform_palette &&
            (v->reg[1]&64) && !bonus_stage() &&
            (!((g_ram[0xF600]&127)==0x34 || (g_ram[0xF600]&127)==0x48) || !(v->reg[12]&8)) &&
            v->reg[2]==s_video_frame.reg[2] && v->reg[4]==s_video_frame.reg[4] &&
            v->reg[16]==s_video_frame.reg[16];
        select_scene(v,retain_level);
        memcpy(&s_video_frame,v,sizeof s_video_frame);
        s_frame_bonus=bonus_stage() || (s_display && s_display->scene==3);
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
        int ready=level_ready();
        /* The title entry clears Current_zone before fading the old level.
         * Keep its published world/camera as well as its composition until
         * the native SAT is replaced. CRAM continues to follow every fade. */
        int hold_level=!ready && !uniform_palette && s_frame_level && s_display && s_display->scene==1 &&
            (v->reg[1]&64) && !s_frame_special && !s_frame_bonus &&
            !file_select_layout(v) && !competition_layout(v);
        s_frame_level=ready || hold_level;
        if(!hold_level) {
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
        if(!s_frame_level && !s_frame_special && !s_frame_bonus) {
            if(file_select_layout(v))s_frame_menu=MENU_SAVE;
            else if(competition_layout(v))s_frame_menu=MENU_COMPETITION;
            else if(title_layout(v) && ((g_ram[0xF600]&127)==4 ||
                    (s_display && !s_display->scene && s_display->title)))
                s_frame_menu=ram16(0xFFAE) || (ram16(0xFFBE)>=12 &&
                    ((v->reg[2]&56u)<<10)!=((v->reg[4]&7u)<<13))?MENU_TITLE:MENU_TITLE_ANIMATION;
        }
    }
    v=&s_video_frame;
    if(s_frame_bonus) {
        /* Bonus machines have a finite authored arena, not a stage layout.
         * Keep native DMA timing, sprites and palettes across every scanline
         * and fade, with stable clean margins at any requested aspect. */
        if(!native)native=native_row(native_ctx);
        int origin=(width-nw)/2;
        for(int x=0;x<width;++x)out[x]=0xFF000000u;
        int first=origin<0?-origin:0,last=origin+nw>width?width-origin:nw;
        if(last>first)memcpy(out+origin+first,native+first,(size_t)(last-first)*sizeof *out);
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
    int ship_x=(s_world_frame[0xEE98]<<8)|s_world_frame[0xEE99];
    int ship_y=(s_world_frame[0xEE9C]<<8)|s_world_frame[0xEE9D];
    unsigned fg_scroll=(aiz_ship || s_interpolating)?live_fg_scroll:v->vsram[0];
    int ship_band=aiz_ship && !((fg_scroll^(unsigned)ship_y)&1023u);
    int camera=level?unwrap(-hs_a,ship_band?ship_x:s_frame_fg_x):ram16(0xEE78);
    int camera_y=unwrap((int)fg_scroll,ship_band?ship_y:s_frame_fg_y);
    if(line==0) {
        if(level) {
            int stage=((s_world_frame[0x8000]<<8)|s_world_frame[0x8001])*128;
            s_stage_width=stage>=320 && stage<=32768?stage:320;
        }
        /* HScroll/VScroll include heat and underwater deformation. Those
         * offsets belong to the planes, never to the object camera or the
         * bounded viewport. Keep sampling the deformed planes below while
         * projecting actors against the same camera used by Render_Sprites. */
        /* GHZ/SYZ deform only B. Their next camera copy can already be in
         * RAM while the previous scroll/SAT is still installed. Project
         * those actors against the installed A camera, just like terrain,
         * rather than adding a one-pixel correction ahead of its upload. */
        int installed_camera=level && (s_frame_zone==0x20 || s_frame_zone==0x24);
        s_camera=level?presentation_camera(0,installed_camera?camera:s_frame_fg_x):camera;
        s_camera_y=level?presentation_camera(1,installed_camera?camera_y:s_frame_fg_y):camera_y;if(!s_interpolating)++s_frames;
        s_left=level?view_left(s_camera,width):camera-(width-nw)/2;
        if(level && s_display && s_display->preview_knux) {
            /* This AIZ2 waiting phase adds only 0/1px heat offsets to A.
             * Recover its installed base once, so the rigid pose follows
             * the displayed ledge without inheriting the per-row wave. */
            int base=hs_a;
            for(int y=0;y<224;++y) {
                int row=hmode==0?0:hmode==2?y&~7:y;
                int scroll=(int16_t)vram16(v,hsbase+(unsigned)row*4);
                if(scroll<base)base=scroll;
            }
            s_knux_plane_camera_x=unwrap(-base,s_frame_fg_x);
            s_knux_plane_camera_y=camera_y;
        }
    }
    int origin=level?s_camera-s_left:(width-nw)/2;
    s_native_x=origin;
    /* Imported stages' line-scroll profiles position the background. Clamping
     * the expanded foreground at a level edge moves its native viewport
     * across the canvas; sharing that changing origin with B adds a second,
     * reversible pan near the boundary. Keep the backdrop on the centered
     * canvas while foreground, actors and HUD retain the bounded view. */
    int centered_background=level && s_frame_zone>=0x20 && s_frame_zone<=0x5B;
    int bg_origin=centered_background?(width-nw)/2:origin;
    unsigned base_a=(v->reg[2]&56u)<<10,base_b=(v->reg[4]&7u)<<13;
    /* SaveScreen reverses the usual plane roles: the 320px portrait lives
     * on the full-screen Window ($E000, 64 tiles per row), while B ($C000)
     * holds the complete 1024px card strip (128 tiles per row).
     * Use the installed layout through both entry and exit fades. */
    int file_select=!level && file_select_layout(v);
    int wy=camera_y+line;
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
        (s_frame_act==1 || s_frame_bg_event<0x10);
    /* SSZ1's cloud band is a separate 512px bank at X=$1C00; entry and
     * exit replace it progressively. DEZ3's auto-scrolling runway is a
     * live Plane B canvas edited by the boss, independent of its layout. */
    int ssz_canvas=bg_world && s_frame_zone==10 && !s_frame_act && s_frame_bg_event;
    int dez3_canvas=bg_world && s_frame_zone==23;
    int bg_canvas=fbz_canvas || icz_canvas || ssz_canvas || dez3_canvas;
    int aiz_banks=bg_world && !s_frame_zone && !s_frame_act &&
        s_frame_bg_event<=8 && s_aiz_art_ready;
    if(aiz_banks){aiz_palette(&s_aiz_intro,v,intro_palette);aiz_palette(&s_aiz_main,v,forest_palette);}
    int by=bg_world?unwrap(presentation_bg_scroll(v->vsram[1]),s_frame_bg_y)+line:line+(v->vsram[1]&1023);
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
    /* HCZ2's crushing wall is a moving world, not a repeating panorama.
     * Negative coordinates to its left must remain blank. */
    if(bg_world && ((s_frame_zone==1 && s_frame_act==1 && s_frame_bg_event<=4) ||
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
    GenesisTileRow fg={0},bg={0};
    for(int x=0;x<width;++x) {
        int nx=x-origin,wx=level?nx+camera:nx-hs_a;
        int reference_bx=nx-hs_b,bx=x-bg_origin-hs_b;
        /* IntroDeform clamps each band's streaming origin at zero. Left
         * extensions still wrap the first installed 512px canvas while the
         * virtual HScroll crosses zero; later positive coast cells are a
         * continuous world, not that repeat period. */
        int source_bx=aiz_intro_canvas && reference_bx<0?(reference_bx&511):background_x;
        /* Title/menu backgrounds only initialize the visible 40 tile
         * columns; repeat that composition, not the stale 512px name table. */
        if(!level && !special && !file_select){
            /* Reflect only the title scenery at its boundary. Opposite-edge
             * wrapping makes a visible seam; stretching one pixel smears the
             * clouds into horizontal bars. Logos remain in the native center. */
            if((g_ram[0xF600]&127)==4 && (nx<0 || nx>=nw)) {
                int ax=nx%(nw*2);if(ax<0)ax+=nw*2;
                if(ax>=nw)ax=nw*2-1-ax;
                bx=ax-hs_b;
            }
            bx%=nw;if(bx<0)bx+=nw;
        }

        if(!fg.remaining) {
            uint16_t attr=level?world_attr(s_world_frame,wx,wy,0):plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
            gtile_row_begin(&fg,v->vram,attr,level?wx:nx-hs_a,level?wy:line+(v->vsram[0]&1023));
        }
        if(!bg.remaining) {
            uint16_t attr=bg_world?world_attr(s_background_frame,source_bx,by,1):plane_attr(v,base_b,bx,by);
            gtile_row_begin(&bg,v->vram,attr,bg_world?source_bx:bx,by);
        }
        uint16_t a=fg.attr,b=bg.attr;
        uint8_t ap=gtile_row_pixel(&fg),bp=gtile_row_pixel(&bg);
        if(file_select) {
            /* Keep one portrait, extending only its edge colours. Mirroring
             * the whole image repeats Sonic's face and DATA SELECT text.
             * The finite carousel still exposes its original offscreen cards. */
            int ax=nx<0?0:nx>=nw?nw-1:nx;
            a=vram16(v,0xE000u+(unsigned)(line/8*64+ax/8)*2u);
            ap=pattern_pixel(v,a,ax,line);
            if(bx<0 || bx>=1024)bp=0;
        }
        const uint32_t *apalette=palette,*bpalette=palette;
        if(bg_canvas) {
            b=plane_attr(v,base_b,reference_bx,by);
            bp=pattern_pixel(v,b,reference_bx,by);
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
        if(aiz_fire_fg) {
            a=plane_attr(v,base_a,nx-hs_a,line+(v->vsram[0]&1023));
            ap=pattern_pixel(v,a,nx-hs_a,line+(v->vsram[0]&1023));
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
        if(level && (wx<0 || wx>=s_stage_width))ap=0;
        /* Menus have a centered foreground composition, with full-width
         * scenery underneath. Special-stage planes are repeating art. */
        if(!level && !file_select && (nx<0 || nx>=nw))ap=0;
        s_priority[x]=(uint8_t)(((a&0x8000)&&ap)||((b&0x8000)&&bp));
        /* The authored transition viewport has no high-priority tree
         * canopy in front of its flame curtain. Expanded margins do: let
         * the opaque high-priority flames cover that extra scenery too.
         * Keep native overlap ordering and transparent flame edges intact. */
        int fire_curtain=aiz_fire && (nx<0 || nx>=nw) && (b&0x8000) && bp;
        int foreground=ap && !fire_curtain && ((a&0x8000) || !bp || !(b&0x8000));
        uint8_t p=foreground?ap:bp;
        uint32_t ac=ap?plane_color(line,a,ap,apalette):backdrop;
        uint32_t bc=bp?plane_color(line,b,bp,bpalette):backdrop;
        out[x]=foreground?ac:bc;
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
            } else if(level && !aiz_fire_fg) {
                uint16_t expected=plane_attr(v,base_a,nx-hs_a,line+(fg_scroll&1023));
                ++s_terrain_checks;
                int bad=attributes_differ(v,a,expected,wx,wy);
                if(bad && s_bad_y<0){s_bad_x=wx;s_bad_y=line;s_bad_attr=a;s_bad_expected=expected;s_bad_world_y=wy;s_bad_hscroll=hs_a;}
                s_terrain_errors+=bad;
            }
            if(centered_background && bg_origin!=origin) {
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
        if(((g_ram[0xF600]&127)==4 || file_select) && s_display && s_display->scene==0)
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
    life_ones_scanline(v,s_interpolating?s_world_frame:g_ram,line,out,width,origin);
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
static void scene_frame_state(S3StateIO *io, SceneFrame *frame)
{
    unsigned count = frame->count;
    s3_state_peek(io, offsetof(SceneFrame, count), &count, sizeof count);
    if (count > SCENE_SPRITES) io->ok = 0;
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
    /* Published scene frames and loader ownership affect future frames.
     * Scanline scratch and decoded ROM caches are rebuilt on demand. */
    scene_frame_state(io, &s_build);
    for (unsigned i = 0; i < 3; ++i) scene_frame_state(io, &s_history[i]);
    scene_frame_state(io, &s_display_frame);
    int display = s_display != NULL; S3_STATE(io, display);
    S3_STATE(io, s_serial); S3_STATE(io, s_scene_tick);
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
    if (io->mode == 2) {
        interpolation_reset();
        s_display = display ? &s_display_frame : NULL;
        s_frame_special = 0;
    }
}
static S3VideoFrameOverlay s_frame_overlay;
void s3_video_set_frame_overlay(S3VideoFrameOverlay draw) { s_frame_overlay = draw; }
static void overlay_scanline(const struct GVDP *v, int line, uint32_t *out, int width)
{
    if(!enabled())life_ones_scanline(v,g_ram,line,out,width,(width-320)/2);
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
static int presentation_bg_scroll(int fallback)
{
    return s_interpolating?interpolation_lerp(ix_previous()->context.video.vsram[1],
        ix_current()->context.video.vsram[1],128):fallback;
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
    unsigned slot=sprite->slot;
    const SceneFrame *a=&ix_previous()->scene,*b=&ix_current()->scene;
    if(!slot || slot>=SCENE_SLOTS || !b->code[slot] || a->code[slot]!=b->code[slot])return;
    *dx=interpolation_lerp(a->ox[slot],b->ox[slot],48)-b->ox[slot];
    *dy=interpolation_lerp(a->oy[slot],b->oy[slot],48)-b->oy[slot];
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
        /* Canonical GHZ/SYZ actors follow installed, undeformed A scroll.
         * Keep that same camera basis in the owned fractional snapshots;
         * the RAM copy may already be preparing the next native frame. */
        RenderContext *c=&f->context;
        if(c->level && (c->zone==0x20 || c->zone==0x24)) {
            unsigned base=(c->video.reg[13]&63u)<<10;
            c->fg_x=unwrap(-(int16_t)vram16(&c->video,base),c->fg_x);
            c->fg_y=unwrap(c->video.vsram[0],c->fg_y);
        }
        if(scene)f->scene=*scene;else memset(&f->scene,0,sizeof f->scene);
        /* Finite/native projections, scene replacement and host-only crowd
         * overlays retain their exact completed image rather than inventing
         * fractional art. Native CD geometry needs its own publication owner. */
        f->valid=native_width==320 && f->context.level && scene && scene->scene==1 &&
            !f->context.special && !f->context.bonus && !f->context.menu &&
            !(v->reg[12]&14) && (enabled() || !s_world_publish) && !s_ix_overlay_seen &&
            !s_actor_interpolation_blocked && !scene->preview_knux;
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
    .presentation_reset = interpolation_reset
};
