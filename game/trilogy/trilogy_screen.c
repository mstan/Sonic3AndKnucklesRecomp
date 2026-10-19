/*
 * trilogy_screen.c — screen init/events, background drawing + deformation,
 * dynamic resize, animated art and palette cycling for trilogy zones.
 *
 * Foreground: S3K's own tile streamers (Refresh_PlaneFull /
 * DrawTilesAsYouMove) run on our converted layout.
 * Background: drawn natively per parallax band straight into plane B, then
 * the original S1/S2 deformation (transcribed) fills the line-scroll table.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "genesis_machine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define VRAM_PLANE_B  0xE000u
#define BG_ROWS       32           /* plane B is 64x32 cells */

/* ---- background state (per level) -------------------------------------- */
typedef struct {
    int32_t  bg2x, bg3x;           /* 16.16 band cameras (S1 v_bg2/3screenposx) */
    int32_t  cloud[3];             /* S1 v_bgscroll_buffer */
    int16_t  last_camx;
    int16_t  bgy;                  /* BG vertical scroll */
} BgState;
static BgState s_bg;

/* ---- plane B writer ------------------------------------------------------ */
static inline void vram_w16(uint32_t a, uint16_t v)
{
    a &= 0xFFFE;
    g_machine.vdp.vram[a] = (uint8_t)(v >> 8);
    g_machine.vdp.vram[a + 1] = (uint8_t)v;
}

/* BG layout chunk at (chunk column cx, chunk row cy), wrapping horizontally. */
static uint8_t bg_chunk(int cx, int cy)
{
    uint32_t hdr = RAM_Level_layout_header;
    int w = (int)r16(hdr + 2);
    int h = (int)r16(hdr + 6);
    if (w <= 0 || h <= 0)
        return 0;
    cx %= w; if (cx < 0) cx += w;
    cy %= h; if (cy < 0) cy += h;
    uint16_t row = r16(hdr + 8 + (uint32_t)cy * 4u + 2u);
    return r8(0xFF0000u | (uint32_t)(uint16_t)(row + cx));
}

const uint16_t *g_bg_tile_remap;          /* NULL, or a tile -> tile table for bg_block */

/* the tiles a BG block uses (for Sonic 2's ending) */
void tri_bg_block_tiles(int bx, int by, uint16_t out[4])
{
    uint8_t ch = bg_chunk(bx >> 7, by >> 7);
    uint16_t cw = r16(0xFF0000u + (uint32_t)ch * 0x80u + (uint32_t)(((by & 0x70) >> 4) * 16) + (uint32_t)(((bx & 0x70) >> 4) * 2));
    uint32_t blk = RAM_Block_table + (uint32_t)(cw & 0x3FF) * 8u;
    for (int i = 0; i < 4; i++) out[i] = r16(blk + (uint32_t)i * 2u) & 0x7FF;
}

/* Draw one 16x16 block cell of BG world (bx,by) (pixels, 16-aligned). */
static void bg_block(int bx, int by)
{
    uint8_t ch = bg_chunk(bx >> 7, by >> 7);
    uint16_t cw = r16(0xFF0000u + (uint32_t)ch * 0x80u + (uint32_t)(((by & 0x70) >> 4) * 16) + (uint32_t)(((bx & 0x70) >> 4) * 2));
    uint32_t blk = RAM_Block_table + (uint32_t)(cw & 0x3FF) * 8u;
    uint16_t t[4] = { r16(blk), r16(blk + 2), r16(blk + 4), r16(blk + 6) };
    if (cw & 0x0400) {            /* x flip */
        uint16_t a = t[0]; t[0] = t[1]; t[1] = a;
        a = t[2]; t[2] = t[3]; t[3] = a;
        for (int i = 0; i < 4; i++) t[i] ^= 0x0800;
    }
    if (cw & 0x0800) {            /* y flip */
        uint16_t a = t[0]; t[0] = t[2]; t[2] = a;
        a = t[1]; t[1] = t[3]; t[3] = a;
        for (int i = 0; i < 4; i++) t[i] ^= 0x1000;
    }
    if (g_bg_tile_remap)                            /* (Sonic 2's ending moved some BG tiles) */
        for (int i = 0; i < 4; i++) t[i] = (uint16_t)((t[i] & 0xF800) | g_bg_tile_remap[t[i] & 0x7FF]);
    int col = (bx >> 3) & 63, row = (by >> 3) & (BG_ROWS - 1);
    vram_w16(VRAM_PLANE_B + (uint32_t)(row * 64 + col) * 2u, t[0]);
    vram_w16(VRAM_PLANE_B + (uint32_t)(row * 64 + ((col + 1) & 63)) * 2u, t[1]);
    vram_w16(VRAM_PLANE_B + (uint32_t)(((row + 1) & (BG_ROWS - 1)) * 64 + col) * 2u, t[2]);
    vram_w16(VRAM_PLANE_B + (uint32_t)(((row + 1) & (BG_ROWS - 1)) * 64 + ((col + 1) & 63)) * 2u, t[3]);
}

/* Draw BG world rows [y0,y1) (16-aligned) as seen by band camera camx. */
static void bg_band(int y0, int y1, int camx)
{
    int x0 = (camx & ~15) - 64;
    for (int by = y0; by < y1; by += 16)
        for (int bx = x0; bx < x0 + 512; bx += 16)
            bg_block(bx, by);
}

/* ---- default background (unknown zones): half-speed single band -------- */
static void bg_default_event(void)
{
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    int camy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    int bx = camx / 2, byv = camy / 4;
    for (int y = 0; y < 256; y += 16)
        bg_band(y, y + 16, bx);
    for (int l = 0; l < 224; l++)
        w32(RAM_H_scroll_buffer + (uint32_t)l * 4u, ((uint32_t)(uint16_t)-camx << 16) | (uint16_t)-bx);
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bx);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)byv);
}

/* ---- Sonic 1 Green Hill (Deform_GHZ, REV01) ----------------------------- */
static void s1_anim_reset(void);
static void ghz_init(void)
{
    s1_anim_reset();
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    memset(&s_bg, 0, sizeof s_bg);
    s_bg.bg2x = (int32_t)camx << 16;     /* BgScrollSpeed: bg2/bg3 = camera x */
    s_bg.bg3x = (int32_t)camx << 16;
    s_bg.last_camx = (int16_t)camx;
}

static void ghz_event(void)
{
    int camx = (int16_t)r16(RAM_Camera_X_pos_copy);
    int camy = (int16_t)r16(RAM_Camera_Y_pos_copy);
    int32_t shift = (int32_t)(int16_t)(camx - s_bg.last_camx) << 8;   /* v_scrshiftx */
    s_bg.last_camx = (int16_t)camx;
    /* block 3 - distant mountains: shift*$30 ; block 2 - hills: shift*$80 */
    s_bg.bg3x += (shift << 5) + (shift << 6);
    s_bg.bg2x += shift << 7;
    /* Y */
    int d0 = (camy & 0x7FF) >> 5;
    d0 = 0x20 - d0;
    if (d0 < 0) d0 = 0;
    int d4 = d0;
    s_bg.bgy = (int16_t)d4;
    /* auto-scroll clouds */
    s_bg.cloud[0] += 0x10000;
    s_bg.cloud[1] += 0xC000;
    s_bg.cloud[2] += 0x8000;
    int bg3 = (int16_t)(s_bg.bg3x >> 16), bg2 = (int16_t)(s_bg.bg2x >> 16);

    /* plane B bands, each drawn where its own line scroll shows it: the three
     * auto-scrolling cloud strips (BG y $00/$20/$30), mountains (bg3),
     * hills + water (bg2) */
    bg_band(0x00, 0x20, (int16_t)(s_bg.cloud[0] >> 16) + bg3);
    bg_band(0x20, 0x30, (int16_t)(s_bg.cloud[1] >> 16) + bg3);
    bg_band(0x30, 0x40, (int16_t)(s_bg.cloud[2] >> 16) + bg3);
    bg_band(0x40, 0x70, bg3);
    bg_band(0x70, 0x100, bg2);

    uint32_t fg = (uint32_t)(uint16_t)(-camx) << 16;
    uint32_t a1 = RAM_H_scroll_buffer;
    int lines = 0;
#define PUT(v) do { if (lines < 224) { w32(a1, fg | (uint16_t)(v)); a1 += 4; lines++; } } while (0)
    int v = -((int16_t)(s_bg.cloud[0] >> 16) + bg3);
    for (int i = 0x1F - d4; i >= 0; i--) PUT(v);
    v = -((int16_t)(s_bg.cloud[1] >> 16) + bg3);
    for (int i = 0; i < 0x10; i++) PUT(v);
    v = -((int16_t)(s_bg.cloud[2] >> 16) + bg3);
    for (int i = 0; i < 0x10; i++) PUT(v);
    for (int i = 0; i < 0x30; i++) PUT(-bg3);
    for (int i = 0; i < 0x28; i++) PUT(-bg2);
    /* water: interpolate bg2 -> camera across $48+d4 lines */
    int32_t d2 = (int32_t)(int16_t)(camx - bg2);
    d2 = (d2 << 8) / 0x68;
    d2 = (int32_t)(int16_t)d2 << 8;
    uint32_t d3 = (uint32_t)(uint16_t)bg2;           /* integer in low word, frac in high */
    int32_t acc = (int32_t)bg2 << 16;
    (void)d3;
    for (int i = 0x47 + d4; i >= 0; i--) {
        PUT(-(int16_t)(acc >> 16));
        acc += d2;
    }
#undef PUT
    w16(RAM_Camera_X_pos_BG_copy, (uint16_t)bg2);
    w16(RAM_Camera_Y_pos_BG_copy, (uint16_t)d4);
}

#include "trilogy_s1bg.inc"
#include "trilogy_s2bg.inc"

/* ---- S1 animated level art + palette cycling ----------------------------- */
static uint32_t blob(int id)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    return t ? tri_rd32(t + (uint32_t)id * 4u) : 0;
}

/* LoadTiles: n tiles from the bank straight into VRAM */
static void load_tiles(uint32_t src, uint32_t tile, uint32_t n)
{
    if (!src) return;
    for (uint32_t k = 0; k < n * 32u; k++)
        g_machine.vdp.vram[(tile * 32u + k) & 0xFFFFu] = tri_rd8(src + k);
}

/* v_lani0..2 time/frame, v_pcyc_time/num */
static int8_t s_lani_time[3];
static uint8_t s_lani_frame[3];
static uint8_t s_lani3_frame;                  /* v_lani3_frame (MZ torch) */
static int16_t s_pcyc_time;
static uint16_t s_pcyc_num;
static uint8_t s_sbz_wait[2];                  /* v_lani2_frame / v_lani2_time: gap between puffs */
static uint8_t s_sbz_pal[32];                  /* v_pal_buffer: SBZ cycle timers/indexes */
static void s1_anim_reset(void)
{
    memset(s_sbz_wait, 0, sizeof s_sbz_wait);
    memset(s_sbz_pal, 0, sizeof s_sbz_pal);
    memset(s_lani_time, 0, sizeof s_lani_time);
    memset(s_lani_frame, 0, sizeof s_lani_frame);
    s_pcyc_time = 0;
    s_pcyc_num = 0;
    s_lani3_frame = 0;
}

/* AniArt_GHZ */
static void s1_ghz_anim(void)
{
    if (--s_lani_time[0] < 0) {                       /* waterfall */
        s_lani_time[0] = 6 - 1;
        int f = s_lani_frame[0]++ & 1;
        load_tiles(blob(TRI_BLOB_S1_GHZ_WATER) + (uint32_t)f * 8u * 32u, 0x378, 8);
        return;
    }
    if (--s_lani_time[1] < 0) {                       /* big flower */
        s_lani_time[1] = 0x10 - 1;
        int f = s_lani_frame[1]++ & 1;
        load_tiles(blob(TRI_BLOB_S1_GHZ_FLOWER1) + (uint32_t)f * 16u * 32u, 0x35C, 16);
        return;
    }
    if (--s_lani_time[2] < 0) {                       /* small flower */
        static const uint8_t seq[4] = { 0, 1, 2, 1 };
        s_lani_time[2] = 8 - 1;
        int f = seq[s_lani_frame[2]++ & 3];
        if (!(f & 1))
            s_lani_time[2] = 0x7F;
        load_tiles(blob(TRI_BLOB_S1_GHZ_FLOWER2) + (uint32_t)f * 12u * 32u, 0x36C, 12);
    }
}

/* AniArt_MZ: lava surface, magma (oscillating per-column byte shift of
 * Art_MzLava2 rows), torch. v_lani3_frame is the torch's own counter. */
static void s1_mz_anim(void)
{
    if (--s_lani_time[0] < 0) {                       /* AniArt_MZ_Lava */
        s_lani_time[0] = 0x14 - 1;
        uint8_t d0 = (uint8_t)(s_lani_frame[0] + 1);
        if (d0 == 3) d0 = 0;
        s_lani_frame[0] = d0;
        load_tiles(blob(TRI_BLOB_S1_MZ_LAVA) + d0 * 8u * 32u, 0x2E2, 8);
    }
    if (--s_lani_time[1] < 0) {                       /* AniArt_MZ_Magma */
        s_lani_time[1] = 2 - 1;
        uint32_t src = blob(TRI_BLOB_S1_MZ_MAGMA);
        if (!src) return;
        src += (uint32_t)s_lani_frame[0] * 0x200u;
        s_lani_frame[1]++;
        unsigned d3 = r8(RAM_Oscillating_table + 0x0A);
        uint32_t dst = 0x2D2u * 32u;
        for (int k = 0; k < 4; k++, d3 += 4) {
            unsigned idx = d3 & 0x0F;                 /* shift = idx & 3, column = idx >> 2 */
            unsigned start = (idx >> 2) * 4 + (idx & 3);
            for (int row = 0; row < 32; row++)
                for (int i = 0; i < 4; i++)
                    g_machine.vdp.vram[(dst++) & 0xFFFFu] = tri_rd8(src + (uint32_t)row * 16u + ((start + (unsigned)i) & 15u));
        }
        return;
    }
    if (--s_lani_time[2] < 0) {                       /* AniArt_MZ_Torch */
        s_lani_time[2] = 8 - 1;
        unsigned d0 = s_lani3_frame;
        s_lani3_frame = (uint8_t)((s_lani3_frame + 1) & 3);
        load_tiles(blob(TRI_BLOB_S1_MZ_TORCH) + d0 * 6u * 32u, 0x2F2, 6);
    }
}

/* PalCycle_GHZ: line 2 colours 8-11 */
static void s1_ghz_palcycle(void)
{
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 5;
    uint32_t src = blob(TRI_BLOB_S1_GHZ_PALCYC);
    if (!src) return;
    uint32_t d0 = (uint32_t)(s_pcyc_num++ & 3) * 8u;
    for (uint32_t k = 0; k < 8; k += 2)
        w16(RAM_Normal_palette + 0x50 + k, tri_rd16(src + d0 + k));
}

/* PalCycle_SYZ: line 3 colours 7-10 (Pal_SYZCyc1), 11 and 13 (Pal_SYZCyc2) */
static void s1_syz_palcycle(void)
{
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 5;
    uint32_t src = blob(TRI_BLOB_S1_SYZ_PALCYC);
    if (!src) return;
    uint32_t d0 = (uint32_t)(s_pcyc_num++ & 3) * 4u;
    for (uint32_t k = 0; k < 8; k += 2)
        w16(RAM_Normal_palette + 0x6E + k, tri_rd16(src + d0 * 2u + k));
    w16(RAM_Normal_palette + 0x76, tri_rd16(src + 0x20 + d0));
    w16(RAM_Normal_palette + 0x7A, tri_rd16(src + 0x20 + d0 + 2));
}

/* PalCycle_LZ: waterfalls (line 2 colours 11-14, both palettes) and the
 * conveyor belts (line 3 colours 11-13, own data per palette) */
static uint16_t s_pal_buffer;                  /* v_pal_buffer */
static void s1_lz_palcycle(void)
{
    extern uint8_t s1_lz_conveyrev(void);
    uint32_t src = blob(TRI_BLOB_S1_LZ_PALCYC);
    if (!src) return;
    if (--s_pcyc_time < 0) {
        s_pcyc_time = 2;
        uint32_t d0 = (uint32_t)(s_pcyc_num++ & 3) * 8u;
        uint32_t a0 = src + (tri_act() == 3 ? 0x44u : 0u);
        for (uint32_t k = 0; k < 8; k += 2) {
            w16(RAM_Normal_palette + 0x56 + k, tri_rd16(a0 + d0 + k));
            w16(RAM_Water_palette + 0x56 + k, tri_rd16(a0 + d0 + k));
        }
    }
    static const uint8_t seq[8] = { 1, 0, 0, 1, 0, 0, 1, 0 };
    if (!seq[r16(RAM_Level_frame_counter) & 7]) return;
    int d1 = s1_lz_conveyrev() ? -1 : 1;
    int d0 = (s_pal_buffer & 3) + d1;
    if (d0 >= 3) d0 = 0;
    if (d0 < 0) d0 = 2;
    s_pal_buffer = (uint16_t)d0;
    uint32_t off = (uint32_t)d0 * 6u;
    for (uint32_t k = 0; k < 6; k += 2) {
        w16(RAM_Normal_palette + 0x76 + k, tri_rd16(src + 0x20 + off + k));
        w16(RAM_Water_palette + 0x76 + k, tri_rd16(src + 0x32 + off + k));
    }
}

/* PalCycle_SLZ: line 2 colours 11, 13 and 14 */
static void s1_slz_palcycle(void)
{
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 7;
    uint16_t d0 = (uint16_t)(s_pcyc_num + 1);
    if (d0 >= 6) d0 = 0;
    s_pcyc_num = d0;
    uint32_t src = blob(TRI_BLOB_S1_SLZ_PALCYC);
    if (!src) return;
    src += (uint32_t)d0 * 6u;
    w16(RAM_Normal_palette + 0x56, tri_rd16(src));
    w16(RAM_Normal_palette + 0x5A, tri_rd16(src + 2));
    w16(RAM_Normal_palette + 0x5C, tri_rd16(src + 4));
}

/* AniArt_SBZ: two smoke puffs in the background, 12 tiles each */
static void s1_sbz_anim(void)
{
    static const uint16_t dest[2] = { 0x448, 0x454 };
    for (int k = 0; k < 2; k++) {
        if (s_sbz_wait[k]) { s_sbz_wait[k]--; continue; }
        if (--s_lani_time[k] >= 0) continue;
        s_lani_time[k] = 8 - 1;
        uint32_t art = blob(k ? TRI_BLOB_S1_SBZ_SMOKE2 : TRI_BLOB_S1_SBZ_SMOKE1);
        if (!art) return;
        int f = s_lani_frame[k]++ & 7;
        if (f) {
            load_tiles(art + (uint32_t)(f - 1) * 12u * 32u, dest[k], 12);
        } else {                               /* clear sky until the next puff */
            s_sbz_wait[k] = 180;
            load_tiles(art, dest[k], 6);
            load_tiles(art, dest[k] + 6, 6);
        }
        if (k == 0) return;                    /* the second puff only runs between the first's */
    }
}

/* PalCycle_SBZ: a list of independently timed single colours (lines 2-3),
 * then the conveyor colours (line 2 colours 12-14), reversible */
static void s1_sbz_palcycle(void)
{
    extern uint8_t s1_lz_conveyrev(void);
    uint32_t base = blob(TRI_BLOB_S1_SBZ_PALCYC);
    if (!base) return;
#define SBZ_ROM(a) (base + (uint32_t)((a) - 0x1C9Eu))
    uint32_t a2 = SBZ_ROM(tri_act() ? 0x1CD6 : 0x1C9E);
    int n = tri_rd16(a2) + 1;
    a2 += 2;
    for (int i = 0; i < n && i < 16; i++, a2 += 6) {
        uint8_t *t = &s_sbz_pal[i * 2];
        if ((int8_t)--t[0] >= 0) continue;
        t[0] = tri_rd8(a2);                    /* duration */
        uint8_t d0 = (uint8_t)(t[1] + 1);
        if (d0 >= tri_rd8(a2 + 1)) d0 = 0;     /* colour count */
        t[1] = d0;
        uint16_t src = tri_rd16(a2 + 2), dst = tri_rd16(a2 + 4);
        w16(RAM_Normal_palette + (uint32_t)(dst - 0xFB00u), tri_rd16(SBZ_ROM(src) + (uint32_t)(d0 & 0x0F) * 2u));
    }
    if (--s_pcyc_time >= 0) return;
    uint32_t a0 = SBZ_ROM(0x1D32);             /* Pal_SBZCyc4 */
    s_pcyc_time = 1;
    if (tri_act()) { a0 = SBZ_ROM(0x1D98); s_pcyc_time = 0; }   /* Pal_SBZCyc10 */
    int d1 = s1_lz_conveyrev() ? 1 : -1;
    int d0 = (int)(s_pcyc_num & 3) + d1;
    if (d0 >= 3) d0 = 0;
    if (d0 < 0) d0 = 2;
    s_pcyc_num = (uint16_t)d0;
    for (int k = 0; k < 6; k += 2)
        w16(RAM_Normal_palette + 0x58 + (uint32_t)k, tri_rd16(a0 + (uint32_t)d0 * 2u + (uint32_t)k));
#undef SBZ_ROM
}

/* ---- S2 animated level art (Dynamic_Normal) + palette cycles -------------- */
/* S2I_ANIM: u16 n; n x { u8 dur, u8 frames, u8 tiles, u8 0, u16 tile, u16 0,
 * u32 art (recoloured), frame data (tile ids, + durations when dur < 0) } */
static uint8_t s_s2_ani[16][2];                 /* Anim_Counters: duration, frame */

static void s2_anim_reset(void) { memset(s_s2_ani, 0, sizeof s_s2_ani); }

static void s2_anim(void)
{
    extern uint8_t s2_current_boss;
    if ((tri_zone() == TRI_S2_ARZ || tri_zone() == TRI_S2_CNZ) && s2_current_boss) return;   /* Dynamic_ARZ/CNZ */
    uint32_t a = tri_rd32(tri_dir() + TD_EXTRA);
    if (a) a = tri_rd32(a + S2I_ANIM);
    if (!a) return;
    int n = tri_rd16(a);
    a += 2;
    for (int i = 0; i < n && i < 16; i++) {
        int8_t dur = (int8_t)tri_rd8(a);
        int nf = tri_rd8(a + 1), nt = tri_rd8(a + 2);
        uint16_t tile = tri_rd16(a + 4);
        uint32_t art = tri_rd32(a + 8), fr = a + 12;
        uint8_t *c = s_s2_ani[i];
        if ((int8_t)--c[0] < 0) {                   /* frame over (subq / bcc) */
            int f = c[1];
            if (f >= nf) f = 0;
            c[1] = (uint8_t)(f + 1);
            c[0] = (uint8_t)(dur >= 0 ? dur : tri_rd8(fr + (uint32_t)f * 2 + 1));
            int id = tri_rd8(fr + (uint32_t)f * (dur >= 0 ? 1 : 2));
            load_tiles(art + (uint32_t)id * 32u, tile, (uint32_t)nt);
        }
        int fl = nf * (dur >= 0 ? 1 : 2);
        a = fr + (uint32_t)((fl + 1) & ~1);
    }
}

/* PalCycle_EHZ: the sea's four colours (line 1, entries 3/4 and $E/$F), every 8 frames */
static void s2_ehz_palcycle(void)
{
    static const uint8_t perm[4] = { 3, 4, 7, 10 };    /* S2_L1_PERM of 3, 4, $E, $F */
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 7;
    int f = s_pcyc_num++ & 3;
    uint32_t src = blob(TRI_BLOB_S2_EHZ_WATER);
    if (!src) return;
    for (int k = 0; k < 4; k++)                     /* move.l (a0,d0),+6 ; move.l 4(a0,d0),+$1C */
        w16(RAM_Normal_palette + 0x20 + perm[k] * 2u, tri_rd16(src + (uint32_t)f * 8u + (uint32_t)k * 2u));
}

/* PalCycle_CPZ: three chemical cycles on lines 3 and 4 (not permuted) */
static uint16_t s_cpz_f2, s_cpz_f3;

static void s2_cpz_palcycle(void)
{
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 7;
    uint32_t src = blob(TRI_BLOB_S2_CPZ_PALCYC);
    if (!src) return;
    uint16_t d0 = (uint16_t)s_pcyc_num;
    s_pcyc_num += 6;
    if (s_pcyc_num >= 0x36) s_pcyc_num = 0;
    for (int k = 0; k < 3; k++)                         /* Normal_palette_line4+$18 */
        w16(RAM_Normal_palette + 0x60 + 0x18 + k * 2u, tri_rd16(src + d0 + k * 2u));
    d0 = s_cpz_f2;
    s_cpz_f2 += 2;
    if (s_cpz_f2 >= 0x2A) s_cpz_f2 = 0;
    w16(RAM_Normal_palette + 0x60 + 0x1E, tri_rd16(src + 0x36 + d0));
    d0 = s_cpz_f3;
    s_cpz_f3 = (s_cpz_f3 + 2) & 0x1E;
    w16(RAM_Normal_palette + 0x40 + 0x1E, tri_rd16(src + 0x60 + d0));
}

/* PalCycle_ARZ: the water colours (line 3 entries 2-5) */
static void s2_arz_palcycle(void)
{
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 5;
    int f = s_pcyc_num++ & 3;
    uint32_t src = blob(TRI_BLOB_S2_EHZ_WATER);
    if (!src) return;
    for (int k = 0; k < 4; k++)
        w16(RAM_Normal_palette + 0x40 + 4 + k * 2u, tri_rd16(src + (uint32_t)f * 8u + (uint32_t)k * 2u));
}

/* PalCycle_CNZ: neon lights (lines 3 and 4), plus the boss's line-2 lights */
static uint16_t s_cnz_f4, s_cnz_t2, s_cnz_f2, s_cnz_f3, s_cnz_f2b;

static void s2_cnz_palcycle(void)
{
    extern uint8_t s2_current_boss;
    uint32_t src = blob(TRI_BLOB_S2_CNZ_PALCYC);
    if (!src) return;
    uint32_t pal = RAM_Normal_palette;
    if (--s_pcyc_time < 0) {
        s_pcyc_time = 7;
        uint16_t d0 = (uint16_t)s_pcyc_num;
        s_pcyc_num += 2;
        if (s_pcyc_num >= 6) s_pcyc_num = 0;
        static const uint8_t dst1[6] = { 0x4A, 0x4C, 0x4E, 0x56, 0x58, 0x5A };
        for (int k = 0; k < 6; k++) w16(pal + dst1[k], tri_rd16(src + d0 + k * 6u));
        for (int k = 0; k < 3; k++) w16(pal + 0x64 + k * 2u, tri_rd16(src + 0x24 + d0 + k * 6u));
        d0 = s_cnz_f4;
        s_cnz_f4 += 2;
        if (s_cnz_f4 >= 0x24) s_cnz_f4 = 0;
        w16(pal + 0x60 + 0x12, tri_rd16(src + 0x36 + d0 + 4));
        w16(pal + 0x60 + 0x14, tri_rd16(src + 0x36 + d0 + 2));
        w16(pal + 0x60 + 0x16, tri_rd16(src + 0x36 + d0));
    }
    if (!s2_current_boss) return;                       /* CNZ_SkipToBossPalCycle */
    if (s_cnz_t2--) return;
    s_cnz_t2 = 3;
    uint16_t d0 = s_cnz_f2;
    s_cnz_f2 += 2;
    if (s_cnz_f2 >= 6) s_cnz_f2 = 0;
    static const uint8_t l1[3] = { 2, 3, 4 };           /* S2 line 2 entries 2-4 (S2_L1_PERM: same slots) */
    for (int k = 0; k < 3; k++) w16(pal + 0x20 + l1[k] * 2u, tri_rd16(src + 0x5E + d0 + k * 6u));
    d0 = s_cnz_f3;
    s_cnz_f3 += 2;
    if (s_cnz_f3 >= 0x14) s_cnz_f3 = 0;
    w16(pal + 0x20 + 7 * 2, tri_rd16(src + 0x70 + d0));            /* S2 entry $E -> slot 7 */
    d0 = s_cnz_f2b;
    s_cnz_f2b = (s_cnz_f2b + 2) & 0x0E;
    w16(pal + 0x20 + 10 * 2, tri_rd16(src + 0x84 + d0));           /* S2 entry $F -> slot $A */
}

/* Dynamic_HTZ: the cliffs (6 of 48 four-tile pieces, chosen by the camera)
 * and the clouds (8 tiles rebuilt from the cloud rows), then HTZ's scripts */
static uint8_t s_htz_mtn;                 /* Anim_Counters+1 */
static void s2_htz_anim(void)
{
    uint32_t inf = tri_rd32(tri_dir() + TD_EXTRA);
    uint16_t base = inf ? tri_rd16(inf + S2I_HTZ_TILE) : 0;
    uint32_t cliffs = inf ? tri_rd32(inf + S2I_HTZ_CLIFFS) : 0, clouds = inf ? tri_rd32(inf + S2I_HTZ_CLOUDS) : 0;
    if (base && cliffs) {
        uint16_t camx = r16(RAM_Camera_X_pos);
        uint16_t d0 = (uint16_t)((camx >> 4) + ((int16_t)-(int16_t)camx >> 3) - 0x10);
        uint8_t rem = (uint8_t)(d0 % 0x30);
        {                                               /* (every frame: the level art may land after) */
            s_htz_mtn = rem;
            int lo = rem & 7, hi = (rem & 0x38) >> 3;
            for (int j = 0; j < 6; j++) {
                uint32_t src = cliffs + (uint32_t)(lo * 6 + (hi + j) % 6) * 128u;
                uint32_t dst = (uint32_t)(base + j * 4) * 32u;
                for (uint32_t b = 0; b < 128; b++) g_machine.vdp.vram[(dst + b) & 0xFFFF] = tri_rd8(src + b);
            }
        }
    }
    if (base && clouds) {
        int16_t d2 = (int16_t)((int16_t)-(int16_t)r16(RAM_Camera_X_pos) >> 3);
        uint8_t buf[0x100];
        for (int i = 0; i < 16; i++) {
            uint16_t v = (uint16_t)(d2 - s2_htz_layer[i]) & 0x1F;
            int c = v & 1;
            v >>= 1;
            if (c) v += 0x200;
            uint32_t src = clouds + (uint32_t)i * 0x20u + v;
            for (int k = 0; k < 4; k++)
                for (int b = 0; b < 4; b++) buf[k * 0x40 + i * 4 + b] = tri_rd8(src + (uint32_t)(k * 4 + b));
        }
        uint32_t dst = (uint32_t)(base + 0x18) * 32u;
        for (int b = 0; b < 0x100; b++) g_machine.vdp.vram[(dst + (uint32_t)b) & 0xFFFF] = buf[b];
    }
    s2_anim();
}
static uint16_t s_htz_pt, s_htz_pf;      /* PalCycle_Timer / PalCycle_Frame */
static void s2_htz_palcycle(void)        /* PalCycle_HTZ: the lava */
{
    static const uint8_t delay[16] = { 0x0B, 0x0B, 0x0B, 0x0A, 8, 0x0A, 0x0B, 0x0B,
                                       0x0B, 0x0B, 0x0D, 0x0F, 0x0D, 0x0B, 0x0B, 0x0B };
    if ((int16_t)--s_htz_pt >= 0) return;
    uint16_t d0 = s_htz_pf++ & 0x0F;
    s_htz_pt = delay[d0];
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t src = t ? tri_rd32(t + TRI_BLOB_S2_HTZ_LAVA * 4u) + d0 * 8u : 0;
    if (!src) return;
    static const uint8_t slot[4] = { 3, 4, 7, 10 };       /* S2 line 2 entries 3, 4, $E, $F (S2_L1_PERM) */
    for (int k = 0; k < 4; k++) w16(RAM_Normal_palette + 0x20 + slot[k] * 2u, tri_rd16(src + (uint32_t)k * 2u));
}
/* PalCycle_MCZ: the lanterns (S2 line 2 entry $B), stopped during the boss */
static void s2_mcz_palcycle(void)
{
    extern uint8_t s2_current_boss;
    if (s2_current_boss) return;
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 1;
    uint32_t src = blob(TRI_BLOB_S2_MCZ_LANTERN);
    if (!src) return;
    int f = s_pcyc_num & 6;
    s_pcyc_num = (uint16_t)((s_pcyc_num + 2) & 6);
    w16(RAM_Normal_palette + 0x20 + 9 * 2, tri_rd16(src + (uint32_t)f));   /* S2_L1_PERM[11] = 9 */
}
/* PalCycle_OOZ: the oil (S2 line 3 entries $A-$D), every 8 frames */
static void s2_ooz_palcycle(void)
{
    if (--s_pcyc_time >= 0) return;
    s_pcyc_time = 7;
    uint32_t src = blob(TRI_BLOB_S2_OOZ_OIL);
    if (!src) return;
    int f = s_pcyc_num & 6;
    s_pcyc_num = (uint16_t)((s_pcyc_num + 2) & 6);
    for (int k = 0; k < 4; k++)
        w16(RAM_Normal_palette + 0x40 + 0x14 + (uint32_t)k * 2u, tri_rd16(src + (uint32_t)f + (uint32_t)k * 2u));
}
/* PalCycle_MTZ: three cycles on S2 line 3 (entries 5, 1-3, $F) */
static int16_t s_mtz_t[3], s_mtz_f[3];
static void s2_mtz_palcycle(void)
{
    uint32_t src = blob(TRI_BLOB_S2_MTZ_PALCYC);
    if (!src) return;
    uint32_t pal = RAM_Normal_palette + 0x40;
    if (--s_mtz_t[0] < 0) {
        s_mtz_t[0] = 0x11;
        int f = s_mtz_f[0];
        s_mtz_f[0] = (int16_t)((f + 2) >= 0x0C ? 0 : f + 2);
        w16(pal + 0x0A, tri_rd16(src + (uint32_t)f));
    }
    if (--s_mtz_t[1] < 0) {
        s_mtz_t[1] = 2;
        int f = s_mtz_f[1];
        s_mtz_f[1] = (int16_t)((f + 2) >= 6 ? 0 : f + 2);
        for (int k = 0; k < 3; k++) w16(pal + 2 + (uint32_t)k * 2u, tri_rd16(src + 0x0C + (uint32_t)f + (uint32_t)k * 2u));
    }
    if (--s_mtz_t[2] < 0) {
        s_mtz_t[2] = 9;
        int f = s_mtz_f[2];
        s_mtz_f[2] = (int16_t)((f + 2) >= 0x14 ? 0 : f + 2);
        w16(pal + 0x1E, tri_rd16(src + 0x18 + (uint32_t)f));
    }
}
static void s2_mtz_init2(void) { s2_anim_reset(); s2_mtz_init(); memset(s_mtz_t, 0, sizeof s_mtz_t); memset(s_mtz_f, 0, sizeof s_mtz_f); }
/* PalCycle_WFZ (also SCZ): fire / belt, and two single colours on S2 line 3 */
uint8_t s2_wfz_fire_toggle;                     /* WFZ_SCZ_Fire_Toggle */
static int16_t s_wfz_t[3], s_wfz_f[3];
static void s2_wfz_palcycle(void)
{
    uint32_t src = blob(TRI_BLOB_S2_WFZ_PALCYC);
    if (!src) return;
    uint32_t pal = RAM_Normal_palette + 0x40;
    if (--s_wfz_t[0] < 0) {
        uint32_t a0 = src;                      /* CyclingPal_WFZFire */
        s_wfz_t[0] = 1;
        if (s2_wfz_fire_toggle) { s_wfz_t[0] = 5; a0 = src + 0x20; }   /* CyclingPal_WFZBelt */
        int f = s_wfz_f[0];
        s_wfz_f[0] = (int16_t)((f + 8) >= 0x20 ? 0 : f + 8);
        for (int k = 0; k < 4; k++) w16(pal + 0x0E + (uint32_t)k * 2u, tri_rd16(a0 + (uint32_t)f + (uint32_t)k * 2u));
    }
    if (--s_wfz_t[1] < 0) {
        s_wfz_t[1] = 3;
        int f = s_wfz_f[1];
        s_wfz_f[1] = (int16_t)((f + 2) >= 0x44 ? 0 : f + 2);
        w16(pal + 0x1C, tri_rd16(src + 0x40 + (uint32_t)f));
    }
    if (--s_wfz_t[2] < 0) {
        s_wfz_t[2] = 5;
        int f = s_wfz_f[2];
        s_wfz_f[2] = (int16_t)((f + 2) >= 0x18 ? 0 : f + 2);
        w16(pal + 0x1E, tri_rd16(src + 0x84 + (uint32_t)f));
    }
}
static void s2_wfz_pal_reset(void) { memset(s_wfz_t, 0, sizeof s_wfz_t); memset(s_wfz_f, 0, sizeof s_wfz_f); s2_wfz_fire_toggle = 0; }
static void s2_scz_init2(void) { s2_anim_reset(); s2_scz_init(); s2_wfz_pal_reset(); }
static void s2_wfz_init2(void) { s2_anim_reset(); s2_wfz_init(); s2_wfz_pal_reset(); }
static void s2_dez_init2(void) { s2_anim_reset(); s2_dez_init(); s_pcyc_num = 0; s_cpz_f2 = s_cpz_f3 = 0; }
static void s2_ooz_init2(void) { s2_anim_reset(); s2_ooz_init(); s_pcyc_num = 0; s_pcyc_time = 0; }
static void s2_mcz_init2(void) { s2_anim_reset(); s2_mcz_init(); s_pcyc_num = 0; s_pcyc_time = 0; }
static void s2_htz_init2(void) { s2_anim_reset(); s2_htz_init(); s_htz_mtn = 0xFF; s_htz_pt = s_htz_pf = 0; }
static void s2_arz_init2(void) { s2_anim_reset(); s2_arz_init(); }
static void s2_cnz_init2(void) { s2_anim_reset(); s2_cnz_init(); }
static void s2_ehz_init2(void) { s2_anim_reset(); s2_ehz_init(); }
static void s2_cpz_init2(void) { s2_anim_reset(); s2_cpz_init(); s_pcyc_num = 0; s_cpz_f2 = s_cpz_f3 = 0; }

/* for the Sonic 2 ending scene (trilogy_s2end.c) */
void tri_bg_band(int y0, int y1, int x) { bg_band(y0, y1, x); }
void tri_s2_dez_palcycle(void) { s2_cpz_palcycle(); }

/* ---- per-zone table ------------------------------------------------------ */
typedef struct {
    void (*bg_init)(void);
    void (*bg_event)(void);
    void (*resize)(void);
    void (*anim)(void);
    void (*palcycle)(void);
} ZoneScreen;

static const ZoneScreen *zone_screen(void)
{
    static const ZoneScreen ghz = { ghz_init, ghz_event, NULL, s1_ghz_anim, s1_ghz_palcycle };
    static const ZoneScreen dflt = { NULL, bg_default_event, NULL, NULL, NULL };
    static const ZoneScreen lz = { s1_lz_init, s1_lz_event, NULL, NULL, s1_lz_palcycle };
    static const ZoneScreen mz = { s1_mz_init, s1_mz_event, NULL, s1_mz_anim, NULL };
    static const ZoneScreen slz = { s1_slz_init, s1_slz_event, NULL, NULL, s1_slz_palcycle };
    static const ZoneScreen syz = { s1_syz_init, s1_syz_event, NULL, NULL, s1_syz_palcycle };
    static const ZoneScreen sbz = { s1_sbz_init, s1_sbz_event, NULL, s1_sbz_anim, s1_sbz_palcycle };
    static const ZoneScreen ehz = { s2_ehz_init2, s2_ehz_event, NULL, s2_anim, s2_ehz_palcycle };
    static const ZoneScreen cpz = { s2_cpz_init2, s2_cpz_event, NULL, s2_anim, s2_cpz_palcycle };
    static const ZoneScreen arz = { s2_arz_init2, s2_arz_event, NULL, s2_anim, s2_arz_palcycle };
    static const ZoneScreen cnz = { s2_cnz_init2, s2_cnz_event, NULL, s2_anim, s2_cnz_palcycle };
    static const ZoneScreen htz = { s2_htz_init2, s2_htz_event, NULL, s2_htz_anim, s2_htz_palcycle };
    static const ZoneScreen mcz = { s2_mcz_init2, s2_mcz_event, NULL, s2_anim, s2_mcz_palcycle };
    static const ZoneScreen ooz = { s2_ooz_init2, s2_ooz_event, NULL, s2_anim, s2_ooz_palcycle };
    static const ZoneScreen mtz = { s2_mtz_init2, s2_mtz_event, NULL, s2_anim, s2_mtz_palcycle };
    static const ZoneScreen scz = { s2_scz_init2, s2_scz_event, s2_scz_camera, s2_anim, s2_wfz_palcycle };
    static const ZoneScreen wfz = { s2_wfz_init2, s2_wfz_event, NULL, s2_anim, s2_wfz_palcycle };
    static const ZoneScreen dez = { s2_dez_init2, s2_dez_event, NULL, s2_anim, s2_cpz_palcycle };   /* PalCycle_CPZ */
    switch (tri_zone()) {
    case TRI_S1_GHZ: return &ghz;
    case TRI_S1_LZ:  return &lz;
    case TRI_S1_MZ:  return &mz;
    case TRI_S1_SLZ: return &slz;
    case TRI_S1_SYZ: return &syz;
    case TRI_S1_SBZ: return &sbz;
    case TRI_S2_EHZ: return &ehz;
    case TRI_S2_CPZ: return &cpz;
    case TRI_S2_ARZ: return &arz;
    case TRI_S2_CNZ: return &cnz;
    case TRI_S2_HTZ: return &htz;
    case TRI_S2_MCZ: return &mcz;
    case TRI_S2_OOZ: return &ooz;
    case TRI_S2_MTZ: return &mtz;
    case TRI_S2_SCZ: return &scz;
    case TRI_S2_WFZ: return &wfz;
    case TRI_S2_DEZ: return &dez;
    default:         return &dflt;
    }
}

/* ---- native screen routines (called through LevelSetup / ScreenEvents) -- */
static void n_fg_init(void)
{
    tri_jsr(S3K_Reset_TileOffsetPositionActual);
    tri_jsr(S3K_Refresh_PlaneFull);
}

/* Redraw the whole FG plane now (after a layout change). Safe from any
 * context: LevelSetup's registers are set up, the caller's are kept. */
void tri_refresh_fg(void)
{
    uint32_t d[8], a[7];
    memcpy(d, g_cpu.D, sizeof d);
    memcpy(a, g_cpu.A, sizeof a);
    tri_jsr(S3K_Reset_TileOffsetPositionActual);
    A(0) = RAM_Plane_buffer;
    A(2) = RAM_Block_table;
    A(3) = RAM_Level_layout_main;
    D(7) = 0xC000;                                      /* VRAM_Plane_A_Name_Table */
    tri_jsr(S3K_Refresh_PlaneFull);
    memcpy(g_cpu.D, d, sizeof d);
    memcpy(g_cpu.A, a, sizeof a);
}

static void n_fg_event(void)
{
    tri_player_frame();
    tri_jsr(S3K_DrawTilesAsYouMove);
}

static void n_bg_init(void)
{
    const ZoneScreen *z = zone_screen();
    if (z->bg_init) z->bg_init();
    if (z->bg_event) z->bg_event();
}

static void n_bg_event(void)
{
    const ZoneScreen *z = zone_screen();
    if (z->bg_event) z->bg_event();
}

static uint32_t s_fg_init, s_bg_init, s_fg_event, s_bg_event;

void tri_screen_register(void)
{
    s_fg_init = tri_native(n_fg_init);
    s_bg_init = tri_native(n_bg_init);
    s_fg_event = tri_native(n_fg_event);
    s_bg_event = tri_native(n_bg_event);
}

/* Do_ResizeEvents tail (camera max-Y easing), transcribed. */
static void resize_tail(void)
{
    int16_t d1 = 2;
    if (getenv("TRILOGY_CAMLOG"))                   /* development */
        fprintf(stderr, "[CAM] p %04X,%04X st %02X yv %d cam %04X,%04X max %04X tgt %04X\n",
                r16(RAM_Player_1 + 0x10), r16(RAM_Player_1 + 0x14), r8(RAM_Player_1 + 0x2A),
                (int16_t)r16(RAM_Player_1 + 0x1A), r16(RAM_Camera_X_pos), r16(RAM_Camera_Y_pos),
                r16(RAM_Camera_max_Y_pos), r16(RAM_Camera_target_max_Y_pos));
    int16_t target = (int16_t)r16(RAM_Camera_target_max_Y_pos);
    int16_t maxy = (int16_t)r16(RAM_Camera_max_Y_pos);
    uint16_t d0 = (uint16_t)(target - maxy);
    if (d0 == 0)
        return;
    if ((uint16_t)target < (uint16_t)maxy) {        /* bcc not taken: target < max */
        d1 = -d1;
        uint16_t cy = r16(RAM_Camera_Y_pos);
        if (cy > (uint16_t)target) {
            w16(RAM_Camera_max_Y_pos, (uint16_t)(cy & 0xFFFE));
        }
        w16(RAM_Camera_max_Y_pos, (uint16_t)(r16(RAM_Camera_max_Y_pos) + d1));
        w8(RAM_Camera_max_Y_pos_changing, 1);
        return;
    }
    uint16_t cy8 = (uint16_t)(r16(RAM_Camera_Y_pos) + 8);
    if (cy8 >= r16(RAM_Camera_max_Y_pos) && (r8(RAM_Player_1 + 0x2A) & 2))
        d1 = (int16_t)(d1 * 4);
    w16(RAM_Camera_max_Y_pos, (uint16_t)(r16(RAM_Camera_max_Y_pos) + d1));
    w8(RAM_Camera_max_Y_pos_changing, 1);
    /* S1/S2 cap the roll speed and have no peel-out or drop dash: going faster
     * (GHZ1's tube) outran the 2px/frame drop into the bottom kill line. The
     * limit, while still dropping, keeps the player no lower than $A0 down the screen */
    int16_t keep = (int16_t)((r16(RAM_Player_1 + 0x14) - 0xA0) & 0xFFFE);
    if (keep > (int16_t)r16(RAM_Camera_max_Y_pos))
        w16(RAM_Camera_max_Y_pos, (uint16_t)(keep < target ? keep : target));
}

int tri_screen_hook(uint32_t pc)
{
    switch (pc) {
    case TH_SETUP_FG:  if (tri_dir()) A(1) = s_fg_init;  return 0;
    case TH_SETUP_BG:  if (tri_dir()) A(1) = s_bg_init;  return 0;
    case TH_EVENT_FG:  if (tri_dir()) A(1) = s_fg_event; return 0;
    case TH_EVENT_BG:  if (tri_dir()) A(1) = s_bg_event; return 0;
    case TH_RESIZE:
        if (!tri_dir()) return 0;
        { const ZoneScreen *z = zone_screen(); if (z->resize) z->resize(); }
        if (tri_rd8(tri_dir() + TD_GAME) == TRI_GAME_S1) {
            extern void s1_dynamic_level_events(void);
            s1_dynamic_level_events();
        } else if (tri_rd8(tri_dir() + TD_GAME) == TRI_GAME_S2) {
            extern void s2_dynamic_level_events(void);
            s2_dynamic_level_events();
        }
        resize_tail();
        return 1;
    case TH_ANIM_TILES:
        if (!tri_dir()) return 0;
        { const ZoneScreen *z = zone_screen(); if (z->anim) z->anim(); }
        return 1;
    case TH_ANPAL_LOAD:
        if (!tri_dir()) return 0;
        { const ZoneScreen *z = zone_screen(); if (z->palcycle) z->palcycle(); }
        return 1;
    default:
        return 0;
    }
}
