/*
 * trilogy_extpal.c — extended sprite line for Sonic 1 / Sonic 2 object art.
 *
 * S1 and S2 draw badniks, bosses and signposts with the player's palette line.
 * S3K's line 0 belongs to the character (Knuckles recolours it, Super Sonic
 * cycles it), so that art keeps its own game's line-0 colours instead: the
 * converter leaves its pixels unmapped and flags its upload segments, and the
 * renderer draws flagged tiles from an extra 16-colour line (gvdp_set_ext_*).
 * The line fades step for step with CRAM (S3K's Pal_* fade routines, mirrored
 * at their hook sites) and has an underwater set below the water line.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "video/genesis_vdp.h"
#include <string.h>

#define RAM_Palette_fade_info  0xFFF626u     /* byte offset of the first colour */
#define RAM_Palette_fade_count 0xFFF627u     /* colours - 1 */

static uint8_t s_tiles[2048];               /* VRAM tile -> extended line (0 / 1) */
static uint16_t s_cur[2][16], s_tgt[2][16]; /* [0] above water, [1] below */
static int s_have;                          /* this act has an extended line */

void tri_ext_tiles(uint32_t tile, uint32_t n, int on)
{
    for (uint32_t k = 0; k < n; k++)
        s_tiles[(tile + k) & 0x7FF] = on ? 1 : 0;
}

/* A level (re)starting (tri_obj_level_entry): the last load's flags would
 * otherwise colour the title card's tiles until this act's art is uploaded */
void tri_ext_clear_tiles(void) { memset(s_tiles, 0, sizeof s_tiles); }

/* Level load: no tiles flagged yet; colours at their targets (a fade-in
 * fills them black first, exactly as it does CRAM). */
void tri_ext_level(void)
{
    memset(s_tiles, 0, sizeof s_tiles);
    s_have = 0;
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t b = t ? tri_rd32(t + TRI_BLOB_EXTPAL * 4u) : 0;
    if (!b || !tri_dir()) return;
    uint32_t e = b + (uint32_t)((tri_zone() - TRI_ZONE_BASE) * 4 + tri_act()) * 64u;
    for (int k = 0; k < 32; k++) {
        uint16_t c = tri_rd16(e + (uint32_t)k * 2u);
        s_tgt[k >> 4][k & 15] = c;
        s_have |= c != 0;
    }
    memcpy(s_cur, s_tgt, sizeof s_cur);
}

/* ---- S3K's fade steps, on the extended line ----------------------------- */
static void add_color(uint16_t *c, uint16_t t)      /* Pal_AddColor */
{
    if (*c == t) return;
    if ((uint16_t)(*c + 0x200) <= t) *c += 0x200;
    else if ((uint16_t)(*c + 0x20) <= t) *c += 0x20;
    else *c += 2;
}

static void dec_color(uint16_t *c)                  /* Pal_DecColor */
{
    if (!*c) return;
    if (*c & 0xE) *c -= 2;
    else if (*c & 0xE0) *c -= 0x20;
    else if (*c & 0xE00) *c -= 0x200;
}

static void dec_color2(uint16_t *c, uint16_t t)     /* Pal_DecColor2 (towards the target from white) */
{
    uint8_t hi = (uint8_t)(*c >> 8), lo = (uint8_t)*c, thi = (uint8_t)(t >> 8), tlo = (uint8_t)t;
    uint8_t d3 = hi & 0xE;
    if (d3 > (thi & 0xE)) d3 -= 2;
    uint8_t g = lo & 0xE0, r = lo & 0xE;
    if (g > (tlo & 0xE0)) g -= 0x20;
    if (r > (tlo & 0xE)) r -= 2;
    *c = (uint16_t)(d3 << 8 | g | r);
}

static void add_color2(uint16_t *c)                 /* Pal_AddColor2 (towards white) */
{
    if (*c == 0xEEE) return;
    if ((*c & 0xE) != 0xE) *c += 2;
    else if ((*c & 0xE0) != 0xE0) *c += 0x20;
    else if ((*c & 0xE00) != 0xE00) *c += 0x200;
}

/* the fade covers line 0 (the extended line stands in for it) */
static int covers_line0(void) { return r8(RAM_Palette_fade_info) < 0x20; }

int tri_ext_hook(uint32_t pc)
{
    if (!s_have) return 0;
    int water = r8(RAM_Water_flag) != 0;
    switch (pc) {
    case TH_PAL_FROMBLACK:
        if (!covers_line0()) return 0;
        for (int k = 0; k < 16; k++) add_color(&s_cur[0][k], s_tgt[0][k]);
        if (water) for (int k = 0; k < 16; k++) add_color(&s_cur[1][k], s_tgt[1][k]);
        return 0;
    case TH_PAL_TOBLACK:
        if (!covers_line0()) return 0;
        for (int k = 0; k < 16; k++) { dec_color(&s_cur[0][k]); dec_color(&s_cur[1][k]); }
        return 0;
    case TH_PAL_FROMWHITE:
        if (!covers_line0()) return 0;
        for (int k = 0; k < 16; k++) dec_color2(&s_cur[0][k], s_tgt[0][k]);
        if (water) for (int k = 0; k < 16; k++) dec_color2(&s_cur[1][k], s_tgt[1][k]);
        return 0;
    case TH_PAL_TOWHITE:
        if (!covers_line0()) return 0;
        for (int k = 0; k < 16; k++) { add_color2(&s_cur[0][k]); add_color2(&s_cur[1][k]); }
        return 0;
    case TH_PAL_FILLBLACK:
        if (!covers_line0()) return 0;
        memset(s_cur[0], 0, sizeof s_cur[0]);
        if (water) memset(s_cur[1], 0, sizeof s_cur[1]);
        return 0;
    case TH_PAL_FILLWHITE:
        if (!covers_line0()) return 0;
        for (int k = 0; k < 16; k++) s_cur[0][k] = 0xEEE;
        return 0;
    default:
        return 0;
    }
}

/* every frame: on in S1/S2 levels only, colours + the water line to the renderer */
void tri_ext_frame(void)
{
    extern int tri_s2end_active(void);
    if (tri_s2end_active()) return;                 /* Sonic 2's ending sets its own */
    int level = (r8(RAM_Game_mode) & 0x7F) == 0x0C;
    if (!level || !s_have || !tri_dir()) {
        gvdp_set_ext_tiles(NULL);
        return;
    }
    uint16_t top[GVDP_EXT_LINES * 16] = { 0 }, bot[GVDP_EXT_LINES * 16] = { 0 };
    memcpy(top, s_cur[0], sizeof s_cur[0]);
    memcpy(bot, s_cur[1], sizeof s_cur[1]);
    int split = 1 << 20;
    if (r8(RAM_Water_flag))
        split = (int16_t)(r16(RAM_Water_level) - r16(RAM_Camera_Y_pos_copy));
    gvdp_set_ext_colors(top, bot, split);
    gvdp_set_ext_tiles(s_tiles);
}
