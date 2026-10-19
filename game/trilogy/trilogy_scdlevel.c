/*
 * trilogy_scdlevel.c — Sonic CD levels at run time (tools/trilogy/scd_level.py).
 *
 * The level about to start is in the bank's window (tri_scd_map); its blob
 * opens with the directory entry and a small header: its own palette and PLC
 * tables (ids $F0..), the chunk pager, its title card, and a stop command for
 * S3K's music while the disc plays the level's CD-DA track.
 *
 * Full virtual chunks: S3K's byte layout cannot name every CD chunk. Retail
 * terrain hooks read the complete 16-bit grid; the custom renderer reads a
 * frame snapshot of the same definitions. Distant collision remains present.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "scd.h"
#include "scd_pack.h"
#include "scd_geometry.h"
extern const ScdPack *tri_scd_pack(void);
#include "sonic3_video.h"
#include "genesis_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    W_PALS = 0x40, W_PALCOUNT = 0x44, W_PLCS = 0x48, W_PLCCOUNT = 0x4C,
    W_PAGER = 0x50, W_CARD = 0x54, W_CARDMAP = 0x58, W_STOP = 0x5C, W_EXTPAL = 0x60, W_TOUCH = 0xA0,
};
#define RAM_Chunk_table   0xFF0000u
#define RAM_Camera_X_pos  0xFFEE78u
#define MAX_CHUNKS        2048

static uint32_t window(void) { return tri_cd_zone(tri_zone()) ? tri_dir() : 0; }

uint32_t tri_scd_palpoint(unsigned id)            /* the PalPoint entry of window palette id */
{
    uint32_t w = window();
    if (!w || id < TRI_PAL_CD || id - TRI_PAL_CD >= tri_rd16(w + W_PALCOUNT)) return 0;
    return tri_rd32(w + W_PALS) + (id - TRI_PAL_CD) * 8u;
}

uint32_t tri_scd_plc(unsigned id)                 /* the PLC list of window PLC id */
{
    uint32_t w = window();
    if (!w || id < TRI_PLC_CD || id - TRI_PLC_CD >= tri_rd16(w + W_PLCCOUNT)) return 0;
    return tri_rd32(tri_rd32(w + W_PLCS) + (id - TRI_PLC_CD) * 4u);
}

void tri_scd_load_palette(unsigned id)            /* a window palette, into the palette and its fade target */
{
    uint32_t e = tri_scd_palpoint(id);
    if (!e) return;
    uint32_t src = tri_rd32(e), dst = 0xFF0000u | tri_rd16(e + 4);
    unsigned n = (tri_rd16(e + 6) + 1u) * 2u;
    for (unsigned k = 0; k < n; k++) {
        uint16_t c = tri_rd16(src + k * 2u);
        w16(dst + k * 2u, c);
        w16(dst + 0x80 + k * 2u, c);
    }
}

uint32_t tri_scd_card_art(void) { uint32_t w = window(); return w ? tri_rd32(w + W_CARD) : 0; }
uint32_t tri_scd_card_map(void) { uint32_t w = window(); return w ? tri_rd32(w + W_CARDMAP) : 0; }
uint32_t tri_scd_stop_byte(void) { uint32_t w = window(); return w ? w + W_STOP : 0; }
/* ---- palette cycles: the level's scripts (scd_level.py palette_cycles,
 * header +$EC), run as Level/Palette Cycle Script.asm's CycleColor ------- */
#define SCD_CYCLES 32
static uint8_t s_cyc_timer[SCD_CYCLES], s_cyc_step[SCD_CYCLES];
static uint32_t s_cyc_table;
static uint16_t s_cyc_frame;

int tri_scd_palcycle_step(int k) { return k >= 0 && k < SCD_CYCLES ? s_cyc_step[k] : 0; }

void tri_scd_palcycle(void)
{
    uint32_t w = window(), t = w ? tri_rd32(w + 0xEC) : 0;
    uint16_t fc = r16(RAM_Level_frame_counter);
    if (t != s_cyc_table || fc < s_cyc_frame) {          /* (a new level, or the act restarted) */
        memset(s_cyc_timer, 0, sizeof s_cyc_timer);
        memset(s_cyc_step, 0, sizeof s_cyc_step);
        s_cyc_table = t;
    }
    s_cyc_frame = fc;
    if (!t) return;
    {
        extern int scd_wwz3_palcycle_timers(void);           /* (Wacky Workbench's boss: colchg_stop / start) */
        int v = scd_wwz3_palcycle_timers();
        if (v >= 0) for (int k = 0; k < 6; k++) s_cyc_timer[k] = (uint8_t)v;
    }
    extern int scd_wwz3_palcycle_black(int k);
    unsigned n = tri_rd16(t);
    uint32_t a = t + 2;
    for (unsigned k = 0; k < n && k < SCD_CYCLES; k++) {
        uint8_t idx = tri_rd8(a + 1), nf = tri_rd8(a + 2);
        uint32_t fr = a + 3, cols = fr + nf * 2u;
        unsigned ncol = 0;
        for (unsigned f = 0; f < nf; f++) if (tri_rd8(fr + f * 2u + 1) + 1u > ncol) ncol = tri_rd8(fr + f * 2u + 1) + 1u;
        if ((int8_t)--s_cyc_timer[k] < 0) {               /* CycleColor */
            uint8_t st = (uint8_t)(s_cyc_step[k] + 1);
            if (st >= nf) st = 0;
            s_cyc_step[k] = st;
            s_cyc_timer[k] = tri_rd8(fr + st * 2u);
            w16(RAM_Normal_palette + idx * 2u, scd_wwz3_palcycle_black((int)k) ? 0 :
                tri_rd16(cols + tri_rd8(fr + st * 2u + 1) * 2u));
        }
        a = cols + ncol * 2u;
        a += a & 1;
    }
}

/* ---- animated level art (scd_level.py anim_tiles, header +$F0): Sonic CD's
 * AnimateTilesSimple / AnimateTilesScript, each into slots of its own (per
 * palette line its blocks use). Wacky Workbench's good future (flags bit 0)
 * turns its sparks on for 30 frames, off (blank) for 90 ------------------- */
#define SCD_ANIMS 32
static uint8_t s_ani[SCD_ANIMS][2];                /* aniArtTimers, aniArtFrames */
static uint32_t s_ani_table;
static uint16_t s_ani_frame;
static uint8_t s_sparks, s_sparks_time, s_sparks_shown;

int tri_scd_sparks_on(void) { return s_sparks; }   /* (aniArtFrames+2: the good future's sparks live) */

static void ani_tiles(uint32_t src, unsigned tile, unsigned n)
{
    for (unsigned k = 0; k < n * 32u; k++)
        g_machine.vdp.vram[(tile * 32u + k) & 0xFFFFu] = src ? tri_rd8(src + k) : 0;
}

void tri_scd_anim(void)
{
    uint32_t w = window(), t = w ? tri_rd32(w + 0xF0) : 0;
    uint16_t fc = r16(RAM_Level_frame_counter);
    if (t != s_ani_table || fc < s_ani_frame) {          /* (a new level, or the act restarted) */
        memset(s_ani, 0, sizeof s_ani);
        s_ani_table = t;
        s_sparks = s_sparks_time = 0;
        s_sparks_shown = 1;
    }
    s_ani_frame = fc;
    if (!t) return;
    if ((int8_t)--s_sparks_time < 0) {                   /* (5338 F66E: on 30, off 90) */
        s_sparks ^= 1;
        s_sparks_time = s_sparks ? 30 : 90;
    }
    unsigned n = tri_rd16(t);
    uint32_t a = t + 2;
    for (unsigned i = 0; i < n && i < SCD_ANIMS; i++) {
        int8_t dur = (int8_t)tri_rd8(a);
        unsigned nf = tri_rd8(a + 1), nt = tri_rd8(a + 2), flags = tri_rd8(a + 3);
        unsigned tile = tri_rd16(a + 4);
        uint32_t art = tri_rd32(a + 8), fr = a + 12;
        a = fr + ((nf * (dur >= 0 ? 1u : 2u) + 1u) & ~1u);
        if ((flags & 1) && !s_sparks) {                  /* (off: blank, once) */
            if (s_sparks_shown) ani_tiles(0, tile, nt);
            s_sparks_shown = 0;
            continue;
        }
        if (flags & 6) {                                 /* (Wacky Workbench act 3: by its boss's flag bit 7) */
            extern uint8_t scd_wwz3_boss_flags(void);
            int beam = (scd_wwz3_boss_flags() & 0x80) != 0;
            if ((flags & 2) && beam) {                   /* (the shaft's sparks off: blank, once) */
                if (!(s_ani[i][1] & 0x80)) { ani_tiles(0, tile, nt); s_ani[i][1] = 0x80; }
                continue;
            }
            if ((flags & 4) && !beam) continue;          /* (the beam's: only then) */
            if (s_ani[i][1] & 0x80) s_ani[i][1] = 0;
        }
        uint8_t *c = s_ani[i];
        if (flags & 16) {                                /* (Quartz Quadrant's background: its section's set) */
            extern int scd_qqz_bg_frame(void);
            unsigned want = (unsigned)scd_qqz_bg_frame();
            if (want >= nf) want = 0;
            if (c[1] != want + 1) {
                c[1] = (uint8_t)(want + 1);
                ani_tiles(art + tri_rd8(fr + want) * 32u, tile, nt);
            }
            continue;
        }
        if (flags & 8) {                                 /* (Metallic Madness act 3's curtain: held at frame 0) */
            extern int scd_mmz3_shut_flag(void);
            if (scd_mmz3_shut_flag() && c[1] <= 1) continue;
        }
        unsigned step = dur >= 0 ? 1u : 2u, f = c[1] ? c[1] - 1u : 0;
        int redo = (flags & 1) && !s_sparks_shown;     /* (back on: its picture again) */
        if ((int8_t)--c[0] < 0) {                        /* (subq.b #1,(a2); bpl) */
            f = c[1] >= nf ? 0 : c[1];
            c[1] = (uint8_t)(f + 1);
            c[0] = (uint8_t)(dur >= 0 ? dur : (int8_t)tri_rd8(fr + f * 2u + 1));
            redo = 1;
        }
        if (redo) ani_tiles(art + tri_rd8(fr + f * step) * 32u, tile, nt);
        if (flags & 1) s_sparks_shown = 1;
    }
}

/* The Sonic CD block (its number, flags off) at a point of the act: the
 * player code of some rounds keys gimmicks on it (scramapad). 0 = none. */
uint16_t tri_scd_block_at(int x, int y)
{
    uint32_t w = window(), t = w ? tri_rd32(w + 0xE8) : 0;   /* (u16 chunks, w, h; the FG; the chunks) */
    if (!t || x < 0 || y < 0) return 0;
    unsigned fw = tri_rd16(t + 2), fh = tri_rd16(t + 4), col = (unsigned)x >> 8, row = (unsigned)y >> 8;
    if (col >= fw || row >= fh) return 0;
    unsigned ch = tri_rd8(t + 6 + row * fw + col) & 0x7F;
    if (!ch || ch > tri_rd16(t)) return 0;
    uint32_t chunks = t + 6 + ((fw * fh + 1u) & ~1u);
    uint32_t a = chunks + (ch - 1u) * 0x200u + ((((unsigned)y >> 4) & 15u) * 16u + (((unsigned)x >> 4) & 15u)) * 2u;
    return (uint16_t)(tri_rd16(a) & 0x7FF);
}

/* Sonic CD's block_wrt, for an empty block (0): the 16 px block at a point
 * goes from the act, its picture and collision (the 128 px chunk it is in,
 * here and paged; as on the disc, every place that chunk is) and its CD
 * number (+$E8). For this attempt: the level is copied afresh at the next
 * start (tri_scd_wr16). Other blocks: only the CD number. */
void tri_scd_wr16(uint32_t a, uint16_t v);        /* trilogy_core.c */
void tri_scd_block_write(int x, int y, uint16_t block)
{
    uint32_t w = window(), t = w ? tri_rd32(w + 0xE8) : 0;
    if (!w || x < 0 || y < 0) return;
    if (t) {
        unsigned fw = tri_rd16(t + 2), fh = tri_rd16(t + 4), col = (unsigned)x >> 8, row = (unsigned)y >> 8;
        unsigned ch = col < fw && row < fh ? tri_rd8(t + 6 + row * fw + col) & 0x7F : 0;
        if (ch && ch <= tri_rd16(t)) {
            uint32_t a = t + 6 + ((fw * fh + 1u) & ~1u) + (ch - 1u) * 0x200u
                       + ((((unsigned)y >> 4) & 15u) * 16u + (((unsigned)x >> 4) & 15u)) * 2u;
            tri_scd_wr16(a, (uint16_t)((tri_rd16(a) & 0xF800) | (block & 0x7FF)));
        }
    }
    uint32_t pg = tri_rd32(w + W_PAGER);
    if (block) return;
    unsigned col = (unsigned)x >> 7, row = (unsigned)y >> 7;
    unsigned at = ((((unsigned)y >> 4) & 7u) * 8u + (((unsigned)x >> 4) & 7u)) * 2u;
    if (!pg) {                                    /* (not paged: the chunk table as loaded; the next
                                                   *  start loads it afresh) */
        if (row >= 32u) return;
        uint16_t lrow = r16(RAM_Level_layout_header + 8u + row * 4u);
        uint8_t ch = r8(0xFF0000u | (uint16_t)(lrow + col));
        if (!ch) return;
        w16(RAM_Chunk_table + ch * 128u + at, 0);
        tri_refresh_fg();
        return;
    }
    unsigned n = tri_rd16(pg), fw = tri_rd16(pg + 2), fh = tri_rd16(pg + 4);
    if (col >= fw || row >= fh) return;
    unsigned v = tri_rd16(tri_rd32(pg + 14) + (row * fw + col) * 2u);
    if (!v || v >= n || v >= MAX_CHUNKS) return;
    tri_scd_wr16(tri_rd32(pg + 10) + v * 128u + at, 0);
    tri_refresh_fg();
}

uint32_t tri_scd_extpal(void) { uint32_t w = window(); return w ? w + W_EXTPAL : 0; }   /* (32 colours) */

/* Object art by section (scd_level.py: +$E0 segments, +$E4 sections) */
uint32_t tri_scd_art_segments(void) { uint32_t w = window(); return w ? tri_rd32(w + 0xE0) : 0; }

int tri_scd_section(void)                         /* (UpdateSectionArt: the first end past the camera) */
{
    uint32_t w = window(), t = w ? tri_rd32(w + 0xE4) : 0;
    if (!t) return 0;
    unsigned n = tri_rd16(t);
    uint16_t camx = r16(RAM_Camera_X_pos);
    for (unsigned k = 0; k < n; k++)
        if (camx < tri_rd16(t + 2 + k * 2u)) return (int)k;
    return n ? (int)n - 1 : 0;
}

/* A Sonic CD collision type as S3K's: its kind, and S3K's size with the same box */
uint8_t tri_scd_coll(uint8_t type)
{
    uint32_t w = window();
    return (uint8_t)((type & 0xC0) | (w && (type & 0x3F) ? tri_rd8(w + W_TOUCH + (type & 0x3Fu)) : (type & 0x3F)));
}

/* ---- music: the level's track off the disc ---------------------------- */
/* The round's PCM bank (the sub CPU's sound file) is loaded for every period:
 * it has the voices ("Past!", "Future!") and, in the past, the music. */
static int s_bank_round = -1;
void tri_scd_level_music(void)
{
    uint32_t w = window();
    if (!w) {
        if (scd_cd_track()) scd_cd_stop();
        if (s_bank_round >= 0) { scd_audio_reset(); s_bank_round = -1; }
        return;
    }
    int track = tri_rd8(w + TD_MUSIC), round = (tri_zone() - TRI_CD_BASE) >> 2;
    scd_audio_init();
    if (!scd_disc_open()) { scd_cd_stop(); return; }
    static const int number[] = { 1, 3, 4, 5, 6, 7, 8 };
    {
        char bank[16];
        snprintf(bank, sizeof bank, "SNCBNK%dB.BIN", number[round % 7]);
        s_bank_round = scd_pcm_bank(bank) ? round : -1;
    }
    if (track) {
        scd_pcm_cmd(SCD_PCM_STOP);
        if (scd_cd_track() != track) scd_cd_play(track, 1);
    } else {                                      /* (the past: the bank's song) */
        scd_cd_stop();
        scd_pcm_cmd(SCD_PCM_PAST);
    }
}

void tri_scd_voice(int id) { if (s_bank_round >= 0) scd_pcm_cmd((uint8_t)id); }

/* ---- chunk pager ------------------------------------------------------- */
static uint32_t s_pager;                          /* the pager this state belongs to */
static uint32_t s_bg_grid;                        /* (another background: Quartz Quadrant's middle; 0 the level's) */
static unsigned s_bg_w, s_bg_h;
/* Complete virtual chunks are shared by the retail terrain hooks and the
 * custom renderer. Paging never drops collision or off-camera geometry. */
void tri_scd_page(int force)
{
    uint32_t w=window(),pg=w?tri_rd32(w+W_PAGER):0;
    if(!pg){s_pager=0;return;}
    if(pg!=s_pager || force>1){s_pager=pg;s_bg_grid=0;}
    unsigned bw=s_bg_grid?s_bg_w:tri_rd16(pg+6),bh=s_bg_grid?s_bg_h:tri_rd16(pg+8);
    w16(RAM_Level_layout_header+2,(uint16_t)bw);
    w16(RAM_Level_layout_header+6,(uint16_t)bh);
}
int tri_scd_chunk(int plane,int column,int row,uint32_t *definitions,unsigned *id)
{
    if(!window() || tri_bank_current()!=4)return 0;
    ScdGeometry g={g_rom_ext+SCD_WINDOW-TRI_BANK_BASE,SCD_WINDOW_SIZE,s_bg_grid,s_bg_w,s_bg_h};
    return scd_geometry_chunk(&g,plane,column,row,definitions,id);
}
static uint8_t s_render_blob[SCD_WINDOW_SIZE];
static uint32_t s_render_size,s_render_bg;
static unsigned s_render_bgw,s_render_bgh;
static uint16_t s_render_stage;
void tri_scd_publish(const uint8_t *ram)
{
    uint16_t za=(uint16_t)((ram[RAM_Current_zone_and_act&65535]<<8)|ram[(RAM_Current_zone_and_act+1)&65535]);
    s_render_size=0;
    const ScdPack *pack=tri_scd_pack();uint32_t size;
    const uint8_t *source=scd_pack_level(pack,za,&size);
    if(!source || size>sizeof s_render_blob)return;
    int live=tri_dir() && tri_bank_current()==4 && za==r16(RAM_Current_zone_and_act);
    if(live)memcpy(s_render_blob,g_rom_ext+SCD_WINDOW-TRI_BANK_BASE,size);
    else memcpy(s_render_blob,source,size);
    s_render_size=size;s_render_stage=za;
    s_render_bg=live?s_bg_grid:0;s_render_bgw=s_bg_w;s_render_bgh=s_bg_h;
}
int tri_scd_video_cell(const uint8_t *ram,int x,int y,int background,uint16_t *cell)
{
    uint16_t za=(uint16_t)((ram[RAM_Current_zone_and_act&65535]<<8)|ram[(RAM_Current_zone_and_act+1)&65535]);
    if(!s_render_size || za!=s_render_stage)return 0;
    unsigned mask=((unsigned)ram[RAM_Layout_row_index_mask&65535]<<8)|ram[(RAM_Layout_row_index_mask+1)&65535];
    ScdGeometry g={s_render_blob,s_render_size,s_render_bg,s_render_bgw,s_render_bgh};
    return scd_geometry_cell(&g,x,y,background,mask,cell);
}

void tri_scd_bg_reset(void) { s_bg_grid = 0; }   /* (a level's start: its own, paged as it loads) */

/* The background from another grid of the pager's chunks (0: the level's own);
 * paged in at once (Quartz Quadrant's open middle). */
void tri_scd_set_bg(uint32_t grid, unsigned w, unsigned h)
{
    if (grid == s_bg_grid) return;
    s_bg_grid = grid;
    s_bg_w = w;
    s_bg_h = h;
    tri_scd_page(1);
}
