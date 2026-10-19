/*
 * trilogy_s2end.c — Sonic 2's ending, played after the Death Egg Robot:
 * EndingSequence (the photos, Sonic in space over the Death Egg's
 * background, the Tornado's rescue with its birds and clouds, the close-up
 * of the plane), transcribed from s2disasm. Where S2 rolls its credits the
 * last pose holds, the screen fades to black, and Sonic 3's story starts:
 * Angel Island, whose own intro fades in.
 *
 * It runs as a native scene off S3K's GameLoop (TH_GAMELOOP): a small
 * object table laid out like S2's (so the objects transcribe offset for
 * offset), S2's own sprite builder over the converted mappings, and S3K's
 * VInt_6 for the palette / sprite table / H-scroll DMA. Art goes to the
 * VRAM tiles S2 used. The character is the S3 one: Sonic, Super Sonic,
 * Tails (with his tail; Sonic flies the Tornado) or Knuckles, drawn from
 * S3K's own mappings, DPLCs, animation scripts and palette - on an extended
 * sprite line, since S2's line 0 belongs to the ending's own art.
 * All data is Sonic 2's (TRI_BLOB_S2_ENDING, tools/trilogy/s2_ending.py).
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "genesis_machine.h"
#include "video/genesis_vdp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void tri_bg_band(int y0, int y1, int x);             /* trilogy_screen.c */
void tri_bg_block_tiles(int bx, int by, uint16_t out[4]);
extern const uint16_t *g_bg_tile_remap;
void tri_s2_dez_palcycle(void);

/* blob items (s2_ending.ITEMS) */
enum {
    E_ART_SONIC, E_ART_SUPERSONIC, E_ART_TAILS, E_ART_FINALTORNADO, E_ART_PICS, E_ART_MINITORNADO,
    E_ART_TORNADO, E_ART_CLOUDS, E_ART_FLICKY, E_ART_EAGLE, E_ART_CHICKEN,
    E_ENI_ENDING1, E_ENI_ENDING2, E_ENI_ENDING3, E_ENI_ENDING4, E_ENI_TAILSPLANE, E_ENI_SONICPLANE,
    E_PAL,
    E_MAP_CF, E_MAP_B2, E_MAP_B3, E_MAP_28,
    E_ANI_B2, E_ANI_CF, E_ANI_CD,
};

#define GM_SCENE        0x10          /* Game_mode while the scene runs (JumpToSegaScreen if ever reached) */
#define MAP_PLAYER      0xFF
#define PLAYER_TILE     0x5C0         /* VRAM for the S3 character's DPLC frames */
#define PLANE_A         0xC000u
#define PLANE_B         0xE000u
#define PLANE_B2        0x4000u
#define MUS_ENDING      0x95
#define TAIL_TILE       0x5E8         /* Tails' tail */

/* S2 RAM-like state */
static int s_active, s_started;
static uint16_t s_ending_routine;             /* Ending_Routine: 0 Sonic, 2 Super Sonic, 4 Tails */
static uint8_t s_super;                       /* Super_Sonic_flag */
static uint8_t s_palcycle;                    /* Ending_PalCycle_flag */
static uint8_t s_credits_trigger;             /* Credits_Trigger */
static uint16_t s_vint_sub;                   /* Ending_VInt_Subrout */
static uint32_t s_rng;                        /* RNG_seed */
static int32_t s_bgx, s_bgy;                  /* Camera_BG_X/Y_pos (16.16) */
static uint16_t s_dez_layer[36];              /* TempArray_LayerDef (SwScrl_DEZ) */
static int s_chunk_map;                       /* Chunk_Table: the plane map VInt sub 4 draws */
static uint16_t s_chunk_base;
static int s_plane64;                         /* plane A 64x64 (VInt sub 4) */

static uint32_t item(int i, uint32_t *len)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t b = t ? tri_rd32(t + TRI_BLOB_S2_ENDING * 4u) : 0;
    if (!b) { if (len) *len = 0; return 0; }
    if (len) *len = tri_rd32(b + (uint32_t)i * 8u + 4u);
    return b + tri_rd32(b + (uint32_t)i * 8u);
}

/* ---- VDP ---------------------------------------------------------------- */
static void vram_w16(uint32_t a, uint16_t v)
{
    g_machine.vdp.vram[a & 0xFFFF] = (uint8_t)(v >> 8);
    g_machine.vdp.vram[(a + 1) & 0xFFFF] = (uint8_t)v;
}

static void vram_fill(uint32_t a, uint32_t n) { for (uint32_t k = 0; k < n; k++) g_machine.vdp.vram[(a + k) & 0xFFFF] = 0; }

static void load_art(int it, uint16_t tile)
{
    uint32_t n, src = item(it, &n);
    for (uint32_t k = 0; k < n; k++) g_machine.vdp.vram[((uint32_t)tile * 32u + k) & 0xFFFF] = tri_rd8(src + k);
}

/* PlaneMapToVRAM_H40 of an Enigma map (decoded at tile base 0) */
static void plane_map(int it, uint16_t base, uint32_t addr, int w, int h)
{
    uint32_t src = item(it, NULL);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            vram_w16(addr + (uint32_t)y * 0x80u + (uint32_t)x * 2u, (uint16_t)(tri_rd16(src + (uint32_t)(y * w + x) * 2u) + base));
}

static void set_reg(int r, uint8_t v) { g_machine.vdp.reg[r] = v; }

/* ---- palettes (S2's fades; Normal_palette / Target_palette are S3K's, same layout) ---- */
static uint16_t s_ext_cur[16];                /* the character's extended line (below) */
static void fade_to_black_step(void)                 /* Pal_FadeToBlack .UpdateColour, whole palette */
{
    for (int k = 0; k < 16; k++) {
        uint16_t c = s_ext_cur[k];
        if (c & 0x00E) c -= 2;
        else if (c & 0x0E0) c -= 0x20;
        else if (c & 0xE00) c -= 0x200;
        s_ext_cur[k] = c;
    }
    for (uint32_t a = RAM_Normal_palette; a < RAM_Normal_palette + 0x80; a += 2) {
        uint16_t c = r16(a);
        if (!c) continue;
        if (c & 0x00E) c -= 2;
        else if (c & 0x0E0) c -= 0x20;
        else if (c & 0xE00) c -= 0x200;
        w16(a, c);
    }
}

/* ---- RandomNumber ------------------------------------------------------- */
static uint32_t rnd(void)
{
    uint32_t d1 = s_rng ? s_rng : 0x2A6D365Au, d0 = d1;
    d1 <<= 2; d1 += d0; d1 <<= 3; d1 += d0;
    uint16_t lo = (uint16_t)d1;
    lo = (uint16_t)(lo + (uint16_t)(d1 >> 16));
    d1 = (d1 & 0xFFFF0000u) | lo;
    d1 = (d1 >> 16) | (d1 << 16);
    s_rng = d1;
    return lo;
}

static uint32_t ror32(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

/* ========================================================================
 * Objects: S2's SST layout ($40 bytes) so the code reads like s2.asm
 * ====================================================================== */
#define NOBJ 48
static uint8_t s_o[NOBJ][0x40];
enum { O_id = 0, O_render = 1, O_art = 2, O_map = 4, O_x = 8, O_y = 0x0C, O_xv = 0x10, O_yv = 0x12, O_inertia = 0x14,
       O_prio = 0x18, O_width = 0x19, O_frame = 0x1A, O_aframe = 0x1B, O_anim = 0x1C, O_prev = 0x1D, O_atime = 0x1E,
       O_status = 0x22, O_rout = 0x24, O_rout2 = 0x25, O_sub = 0x28 };
enum { ID_SONIC = 1, ID_TAILS = 2, ID_C9 = 0xC9, ID_CA = 0xCA, ID_CB = 0xCB, ID_CC = 0xCC, ID_CD = 0xCD,
       ID_CE = 0xCE, ID_CF = 0xCF };
enum { SLOT_PLAYER = 0, SLOT_PALCHG = 3, SLOT_CUTSCENE = 4 };   /* MainCharacter, EndSeqPaletteChanger, CutScene */

#define B(o, f)        s_o[o][f]
static uint16_t W(int o, int f) { return (uint16_t)(s_o[o][f] << 8 | s_o[o][f + 1]); }
static void SW(int o, int f, int v) { s_o[o][f] = (uint8_t)(v >> 8); s_o[o][f + 1] = (uint8_t)v; }
static int16_t SWs(int o, int f) { return (int16_t)W(o, f); }
static uint32_t L(int o, int f) { return (uint32_t)W(o, f) << 16 | W(o, f + 2); }
static void SL(int o, int f, uint32_t v) { SW(o, f, (int)(v >> 16)); SW(o, f + 2, (int)v); }
#define X(o)  SWs(o, O_x)
#define Y(o)  SWs(o, O_y)

static int s_cur;                             /* the running object (AllocateObjectAfterCurrent) */

static int alloc_after(int o)
{
    for (int k = o + 1; k < NOBJ; k++) if (!B(k, O_id)) { memset(s_o[k], 0, 0x40); return k; }
    return -1;
}

static void delete_obj(int o) { memset(s_o[o], 0, 0x40); }

static void object_move(int o)                       /* ObjectMove */
{
    SL(o, O_x, L(o, O_x) + (uint32_t)((int32_t)SWs(o, O_xv) << 8));
    SL(o, O_y, L(o, O_y) + (uint32_t)((int32_t)SWs(o, O_yv) << 8));
}

/* LoadSubObject_Part3 */
static void sub_obj(int o, int map, uint16_t art, uint8_t render, uint8_t prio, uint8_t width, uint8_t coll)
{
    SL(o, O_map, (uint32_t)map);
    SW(o, O_art, art);
    B(o, O_render) |= render;
    B(o, O_prio) = prio;
    B(o, O_width) = width;
    (void)coll;
    B(o, O_rout) += 2;
}
#define SUB_B2(o)  sub_obj(o, E_MAP_B2, 0x8500, 4, 4, 0x60, 0)          /* ObjB2_SubObjData */
#define SUB_B3(o)  sub_obj(o, E_MAP_B3, 0x454F, 4, 6, 0x30, 0)          /* ObjB3_SubObjData: Clouds, line 2 */

static int child(int o, int field, uint8_t id, uint8_t sub)       /* LoadChildObject */
{
    int a1 = alloc_after(o);
    if (a1 < 0) return -1;
    SW(o, field, a1);
    B(a1, O_id) = id;
    B(a1, O_sub) = sub;
    SW(a1, 0x2C, o);
    SW(a1, O_x, X(o));
    SW(a1, O_y, Y(o));
    return a1;
}

/* AnimateSprite (S2) on a blob script table */
static void animate(int o, int it)
{
    uint32_t ani = item(it, NULL);
    uint8_t d0 = B(o, O_anim);
    if (d0 != B(o, O_prev)) { B(o, O_prev) = d0; B(o, O_aframe) = 0; B(o, O_atime) = 0; }
    if ((int8_t)--B(o, O_atime) >= 0) return;
    uint32_t a1 = ani + tri_rd16(ani + d0 * 2u);
    B(o, O_atime) = tri_rd8(a1);
    uint8_t f = tri_rd8(a1 + 1 + B(o, O_aframe));
    if (f & 0x80) {
        switch (f) {
        case 0xFF: B(o, O_aframe) = 0; f = tri_rd8(a1 + 1); break;
        case 0xFE: B(o, O_aframe) = (uint8_t)(B(o, O_aframe) - tri_rd8(a1 + 2 + B(o, O_aframe))); f = tri_rd8(a1 + 1 + B(o, O_aframe)); break;
        case 0xFD: B(o, O_anim) = tri_rd8(a1 + 2 + B(o, O_aframe)); return;
        case 0xFC: B(o, O_rout) += 2; B(o, O_atime) = 0; B(o, O_aframe)++; return;
        case 0xFB: B(o, O_aframe) = 0; B(o, O_rout2) = 0; return;
        case 0xFA: B(o, O_rout2) += 2; return;
        default: return;
        }
    }
    B(o, O_frame) = f & 0x7F;
    B(o, O_render) = (uint8_t)((B(o, O_render) & 0xFC) | (B(o, O_status) & 3));
    B(o, O_aframe)++;
}

/* ---- display lists + BuildSprites --------------------------------------- */
static uint8_t s_disp[8][NOBJ];
static int s_ndisp[8];
static void display(int o)                           /* DisplaySprite */
{
    int p = B(o, O_prio) & 7;
    if (s_ndisp[p] < NOBJ) s_disp[p][s_ndisp[p]++] = (uint8_t)o;
}

/* ---- the S3 character: S3K's own sprite data ------------------------------ */
#define S3K_AniSuperSonic_       0x012C3Au
#define S3K_Map_Knuckles_        0x14A8D6u
#define S3K_PLC_Knuckles_        0x14BD0Au
#define S3K_ArtUnc_Knux_         0x1200E0u
#define S3K_AniKnuckles_         0x017EF4u
#define S3K_Pal_SonicTails_      0x0A8A3Cu
#define S3K_Pal_Knuckles_        0x0A8AFCu
#define S3K_Map_Tails_Tail_      0x344BB8u
#define S3K_PLC_Tails_Tail_      0x344D74u
#define S3K_ArtUnc_Tails_Tail_   0x336620u
#define S3K_AniTails_Tail_       0x016196u

typedef struct { uint32_t map, plc, art, art_x, ani, pal; int extra_from; } Char;
static int s_knux;                            /* Knuckles plays Sonic's ending */

static Char cur_char(void)
{
    if (s_ending_routine == 4)
        return (Char){ S3K_Map_Tails, S3K_PLC_Tails, S3K_ArtUnc_Tails, S3K_ArtUnc_Tails_Extra, S3K_AniTails, S3K_Pal_SonicTails_, 0xD1 };
    if (s_knux)
        return (Char){ S3K_Map_Knuckles_, S3K_PLC_Knuckles_, S3K_ArtUnc_Knux_, S3K_ArtUnc_Knux_, S3K_AniKnuckles_, S3K_Pal_Knuckles_, 0x100 };
    if (s_super)
        return (Char){ S3K_Map_SuperSonic, S3K_PLC_SuperSonic, S3K_ArtUnc_Sonic, S3K_ArtUnc_Sonic_Extra, S3K_AniSuperSonic_, S3K_Pal_SonicTails_, 0xDA };
    return (Char){ S3K_Map_Sonic, S3K_PLC_Sonic, S3K_ArtUnc_Sonic, S3K_ArtUnc_Sonic_Extra, S3K_AniSonic, S3K_Pal_SonicTails_, 0xDA };
}

static void dplc(uint32_t plc, int frame, uint32_t art, uint16_t tile)   /* (Sonic_Load_PLC) */
{
    uint32_t p = plc + tri_rd16(plc + (uint32_t)frame * 2u);
    int n = tri_rd16(p);
    uint32_t dst = (uint32_t)tile * 32u;
    for (int i = 0; i < n; i++) {
        uint16_t w = tri_rd16(p + 2 + (uint32_t)i * 2u);
        uint32_t cnt = ((w >> 12) + 1u) * 32u, src = art + (uint32_t)(w & 0xFFF) * 32u;
        tri_vram_queue(src, dst, cnt);
        dst += cnt;
    }
}

static int s_player_loaded = -1, s_tail_loaded = -1;
static void player_frame_art(int frame, uint32_t *maps)
{
    Char c = cur_char();
    *maps = c.map;
    if (s_player_loaded == frame) return;
    s_player_loaded = frame;
    dplc(c.plc, frame, frame >= c.extra_from ? c.art_x : c.art, PLAYER_TILE);
}

/* The extended line: the character's own S3K colours, faded with line 0 */
static uint16_t s_ext_tgt[16];
static uint8_t s_ext_map[2048];
static int s_super_step, s_super_time;

static uint32_t super_cycle(void)                    /* PalCycle_SuperSonic (data in the S3K cartridge) */
{
    static uint32_t a;
    if (!a) {                                           /* find its first row, $E66,$C42,$822, $E88 */
        for (uint32_t p = 0x3000; p < 0x5000; p += 2)
            if (tri_rd16(p) == 0xE66 && tri_rd16(p + 2) == 0xC42 && tri_rd16(p + 4) == 0x822 && tri_rd16(p + 6) == 0xE88) { a = p; break; }
    }
    return a;
}

static void ext_init(void)
{
    Char c = cur_char();
    for (int k = 0; k < 16; k++) { s_ext_tgt[k] = tri_rd16(c.pal + (uint32_t)k * 2u); s_ext_cur[k] = 0xEEE; }
    memset(s_ext_map, 0, sizeof s_ext_map);
    for (int t = PLAYER_TILE; t < 0x600; t++) s_ext_map[t] = 1;
    s_super_step = 6;
    s_super_time = 0;
}

static void ext_super(void)                          /* Super Sonic's steady cycle (rows 6-9 of PalCycle_SuperSonic) */
{
    uint32_t cyc = super_cycle();
    if (!s_super || s_ending_routine == 4 || s_knux || !cyc) return;
    if (--s_super_time >= 0) return;
    s_super_time = 6;
    if (++s_super_step > 9) s_super_step = 6;
    for (int k = 0; k < 3; k++) s_ext_tgt[2 + k] = tri_rd16(cyc + (uint32_t)(s_super_step * 3 + k) * 2u);
    if (B(SLOT_PALCHG, O_id) != 0xC9 || B(SLOT_PALCHG, 0x35))       /* (unless line 0 is fading now) */
        for (int k = 0; k < 3; k++) s_ext_cur[2 + k] = s_ext_tgt[2 + k];
}

static void ext_apply(void)
{
    uint16_t top[GVDP_EXT_LINES * 16] = { 0 };
    memcpy(top, s_ext_cur, sizeof s_ext_cur);
    gvdp_set_ext_colors(top, top, 1 << 20);
    gvdp_set_ext_tiles(s_ext_map);
}

static uint16_t ext_down(uint16_t c, uint16_t t)     /* loc_1344C on a colour */
{
    uint16_t b = c & 0xE00, g = c & 0xE0, r = c & 0xE;
    if (b > (t & 0xE00)) b -= 0x200;
    if (g > (t & 0xE0)) g -= 0x20;
    if (r > (t & 0xE)) r -= 2;
    return (uint16_t)(b | g | r);
}

static uint16_t ext_white(uint16_t c)                /* loc_1348A on a colour */
{
    uint16_t b = c & 0xE00, g = c & 0xE0, r = c & 0xE;
    if (b < 0xE00) b += 0x200;
    if (g < 0xE0) g += 0x20;
    if (r < 0xE) r += 2;
    return (uint16_t)(b | g | r);
}

/* Tails' tail (Obj_Tails_Tail): the swish while he stands on the wing */
static uint8_t s_tail_af, s_tail_at;
static int s_tail_frame;
static void tail_animate(void)
{
    uint32_t ani = S3K_AniTails_Tail_, a1 = ani + tri_rd16(ani + 2u);   /* anim 1: Swish */
    if ((int8_t)--s_tail_at >= 0) return;
    s_tail_at = tri_rd8(a1);
    uint8_t f = tri_rd8(a1 + 1 + s_tail_af);
    if (f >= 0xFC) { s_tail_af = 0; f = tri_rd8(a1 + 1); }
    s_tail_frame = f;
    s_tail_af++;
}

static int s_nspr;
static void put_frame(uint32_t maps, int frame, int x, int y, uint16_t art, uint8_t render, uint16_t base)
{
    uint32_t f = maps + tri_rd16(maps + (uint32_t)frame * 2u);
    int n = tri_rd16(f);
    for (int i = 0; i < n && s_nspr < 80; i++) {
        uint32_t p = f + 2 + (uint32_t)i * 6u;
        int py = (int8_t)tri_rd8(p), size = tri_rd8(p + 1);
        uint16_t tile = tri_rd16(p + 2);
        int px = (int16_t)tri_rd16(p + 4);
        int w = ((size >> 2) & 3) + 1, h = (size & 3) + 1;
        if (render & 1) { px = -px - w * 8; tile ^= 0x800; }
        if (render & 2) { py = -py - h * 8; tile ^= 0x1000; }
        tile = (uint16_t)(tile + art);
        if (base) tile = (uint16_t)((tile & 0xF800) | ((tile & 0x7FF) + base));
        if ((tile & 0x7FF) >= 0x780 && (tile & 0x7FF) < 0x786) tile += 0x20;   /* Sonic's pilot tiles -> $7A0 (S3K's H-scroll sits at $F000) */
        int sx = x + px + 128, sy = y + py + 128;
        if (sx <= 128 - w * 8 || sx >= 128 + 320 || sy <= 128 - h * 8 || sy >= 128 + 224) continue;
        uint32_t e = RAM_Sprite_table + (uint32_t)s_nspr * 8u;
        w16(e, (uint16_t)sy);
        w8(e + 2, (uint8_t)size);
        w8(e + 3, (uint8_t)(s_nspr + 1));
        w16(e + 4, tile);
        w16(e + 6, (uint16_t)sx);
        s_nspr++;
    }
}

static void build_sprites(void)                      /* BuildSprites */
{
    s_nspr = 0;
    for (int p = 0; p < 8; p++) {
        for (int k = 0; k < s_ndisp[p]; k++) {
            int o = s_disp[p][k];
            if (!B(o, O_id)) continue;
            uint32_t map = L(o, O_map);
            if (map == MAP_PLAYER) {
                uint32_t maps;
                player_frame_art(B(o, O_frame), &maps);
                put_frame(maps, B(o, O_frame), X(o), Y(o), 0, B(o, O_render), PLAYER_TILE);
                if (s_ending_routine == 4 && B(o, O_anim) == 5) {   /* the tail, behind him */
                    if (s_tail_loaded != s_tail_frame) {
                        s_tail_loaded = s_tail_frame;
                        dplc(S3K_PLC_Tails_Tail_, s_tail_frame, S3K_ArtUnc_Tails_Tail_, TAIL_TILE);
                    }
                    put_frame(S3K_Map_Tails_Tail_, s_tail_frame, X(o), Y(o), 0, B(o, O_render), TAIL_TILE);
                }
            } else {
                put_frame(item((int)map, NULL), B(o, O_frame), X(o), Y(o), W(o, O_art), B(o, O_render), 0);
            }
        }
        s_ndisp[p] = 0;
    }
    if (s_nspr) w8(RAM_Sprite_table + (uint32_t)(s_nspr - 1) * 8u + 3u, 0);
    else { w32(RAM_Sprite_table, 0); w32(RAM_Sprite_table + 4, 0); }
}

/* ========================================================================
 * The character (S2's Obj01 / Obj02 under obj_control $81: it only animates)
 * ====================================================================== */
static void player_obj(int o)
{
    if (!B(o, O_rout)) {
        B(o, O_rout) = 2;
        SL(o, O_map, MAP_PLAYER);
        B(o, O_prio) = 2;
        B(o, O_render) = 4;
    }
    /* S3K's animation scripts for the S2 anim ids used here (same numbers) */
    uint32_t ani = cur_char().ani;
    if (B(o, O_anim) == 5) {                            /* the standing pose, held (S2: frame 1 for $100) */
        B(o, O_frame) = tri_rd8(ani + tri_rd16(ani + 5u * 2u) + 1u);
        if (s_ending_routine == 4) tail_animate();
        return;
    }
    uint8_t d0 = B(o, O_anim);
    if (d0 != B(o, O_prev)) { B(o, O_prev) = d0; B(o, O_aframe) = 0; B(o, O_atime) = 0; }
    uint32_t a1 = ani + tri_rd16(ani + d0 * 2u);
    uint8_t dur = tri_rd8(a1);
    if (dur >= 0xFD) {                                  /* walk / run by speed (Sonic_Animate SAnim_WalkRun) */
        int16_t sp = SWs(o, O_inertia);
        if (sp < 0) sp = (int16_t)-sp;
        if (sp >= 0x600) a1 = ani + tri_rd16(ani + 2u);  /* the run script */
        int t = (0x800 - sp) >> 8;
        dur = (uint8_t)(t < 0 ? 0 : t);
        if ((int8_t)--B(o, O_atime) >= 0) return;
        B(o, O_atime) = dur;
    } else {
        if ((int8_t)--B(o, O_atime) >= 0) return;
        B(o, O_atime) = dur;
    }
    uint8_t f = tri_rd8(a1 + 1 + B(o, O_aframe));
    if (f >= 0xFC) {                                    /* loop / hold */
        B(o, O_aframe) = 0;
        f = tri_rd8(a1 + 1);
    }
    B(o, O_frame) = f;
    B(o, O_aframe)++;
}

static void player_pose_wait(int o)                  /* mapping_frame 1, anim Wait, held $100 frames */
{
    B(o, O_anim) = 5;
    B(o, O_prev) = 5;
    B(o, O_aframe) = 0;
    B(o, O_atime) = 0xFF;
}

/* ========================================================================
 * Object C9 - palette changer (the ending's fades)
 * ====================================================================== */
static void pc_fade_down(uint32_t a, uint32_t t)     /* loc_1344C: each channel down to the target */
{
    uint8_t c = r8(a), g = tri_rd8(t) & 0xE;
    if (c > g) w8(a, (uint8_t)(c - 2));
    uint8_t d2 = tri_rd8(t + 1), d4 = r8(a + 1);
    uint8_t tg = d2 & 0xE0, tr = d2 & 0x0E, cg = d4 & 0xE0, cr = d4 & 0x0E;
    if (cg > tg) cg = (uint8_t)(cg - 0x20);
    if (cr > tr) cr = (uint8_t)(cr - 2);
    w8(a + 1, (uint8_t)(cg | cr));
}

static void pc_fade_white(uint32_t a)                /* loc_1348A: each channel up to white */
{
    uint8_t c = r8(a) & 0xE;
    if (c < 0xE) w8(a, (uint8_t)(c + 2));
    uint8_t d3 = r8(a + 1) & 0xE0, d4 = r8(a + 1) & 0x0E;
    if (d3 < 0xE0) d3 = (uint8_t)(d3 + 0x20);
    if (d4 < 0x0E) d4 = (uint8_t)(d4 + 2);
    w8(a + 1, (uint8_t)(d3 | d4));
}

static void palchanger(int o)                        /* ObjC9 */
{
    /* code (0 FromBlack, 1 down, 2 white), palette data (offset in E_PAL), target offset, length, time, amount */
    static const struct { uint8_t code; uint8_t src; uint8_t at, len, time, amount; } t[8] = {
        { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 },
        { 1, 0xA0, 0x00, 0x0F, 8, 7 },                  /* 4: Pal_AD1E (photos) */
        { 2, 0xA0, 0x00, 0x0F, 8, 7 },                  /* 6: to white */
        { 1, 0x00, 0x00, 0x1F, 4, 7 },                  /* 8: Pal_AC7E (Sonic) */
        { 1, 0x60, 0x40, 0x1F, 4, 7 },                  /* $A: Pal_ACDE (background) */
        { 1, 0xC0, 0x00, 0x0F, 4, 7 },                  /* $C: Pal_AD3E (Super Sonic) */
        { 1, 0x20, 0x00, 0x1F, 4, 7 },                  /* $E: Pal_AC9E (Tails) */
    };
    int k = (B(o, O_sub) >> 1) & 7;
    if (!B(o, O_rout)) {                                /* ObjC9_Init */
        B(o, O_rout) = 2;
        uint32_t src = item(E_PAL, NULL) + t[k].src;
        SW(o, 0x34, t[k].at);
        SW(o, 0x36, t[k].len);
        for (int i = 0; i <= t[k].len; i++) w16(RAM_Target_palette + t[k].at + (uint32_t)i * 2u, tri_rd16(src + (uint32_t)i * 2u));
        B(o, 0x30) = B(o, 0x31) = t[k].time;
        B(o, 0x32) = t[k].amount;
        B(o, 0x3A) = t[k].code;
        B(o, 0x35) = t[k].at;
        return;
    }
    if ((int8_t)--B(o, 0x30) >= 0) return;              /* ObjC9_Main */
    B(o, 0x30) = B(o, 0x31);
    if ((int8_t)--B(o, 0x32) < 0) { delete_obj(o); return; }
    uint32_t at = W(o, 0x34);
    if (!at)                                            /* line 0: the character's extended line with it */
        for (int k = 0; k < 16; k++)
            s_ext_cur[k] = B(o, 0x3A) == 1 ? ext_down(s_ext_cur[k], s_ext_tgt[k]) : B(o, 0x3A) == 2 ? ext_white(s_ext_cur[k]) : s_ext_cur[k];
    for (int i = 0; i <= W(o, 0x36); i++) {
        uint32_t a = RAM_Normal_palette + at + (uint32_t)i * 2u, tg = RAM_Target_palette + at + (uint32_t)i * 2u;
        if (B(o, 0x3A) == 1) pc_fade_down(a, tg);
        else if (B(o, 0x3A) == 2) pc_fade_white(a);
        else {
            uint16_t c = r16(a), tt = r16(tg);
            if (c != tt) w16(a, (uint16_t)((uint16_t)(c + 0x200) <= tt ? c + 0x200 : (uint16_t)(c + 0x20) <= tt ? c + 0x20 : c + 2));
        }
    }
}

/* ========================================================================
 * Object CA - the cutscene
 * ====================================================================== */
static void ca_changer(int o, uint8_t sub, uint16_t time)    /* sub_A22A */
{
    int a1 = SLOT_PALCHG;
    B(a1, O_id) = ID_C9;
    B(a1, O_sub) = sub;
    B(o, O_rout) += 2;
    SW(o, 0x3C, time);
}

static void ca_clouds(int o)                         /* loc_AB9C */
{
    SW(o, 0x30, W(o, 0x30) - 1);
    if (SWs(o, 0x30) >= 0) return;
    SW(o, 0x30, (int)(s_rng & 0x1F));
    child(o, 0x3E, ID_CB, 0);
}

static void dez_scroll(void)                         /* SwScrl_DEZ with Camera_Y_pos_diff = $100 */
{
    static const uint8_t speed[23] = { 3, 2, 4, 1, 2, 4, 3, 4, 2, 6, 3, 4, 1, 2, 4, 3, 2, 3, 4, 1, 3, 4, 2 };
    static const uint8_t rows[36] = {
        128, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        3, 5, 8, 16, 128, 128, 128,
    };
    s_bgy += 0x10000;                                   /* SetHorizVertiScrollFlagsBG: d5 = $100 << 8 */
    uint16_t *a = s_dez_layer;
    a[0] = 0;
    for (int k = 0; k < 23; k++) a[1 + k] = (uint16_t)(a[1 + k] + speed[k]);
    a[24] = (uint16_t)(a[24] + 1);
    a[25] = (uint16_t)(a[24] >> 1);
    a[26] = (uint16_t)(a[26] + 3);
    a[27] = (uint16_t)(a[27] + 2);
    a[28] = (uint16_t)(a[28] + 4);
    uint32_t d1 = (uint32_t)a[24] << 16, d0 = d1;
    d1 >>= 3;
    d0 -= d1; a[31] = (uint16_t)(d0 >> 16);
    d0 -= d1; a[30] = (uint16_t)(d0 >> 16);
    d0 -= d1; a[29] = (uint16_t)(d0 >> 16);
    a[32] = (uint16_t)(a[32] + 1);
    a[33] = a[34] = a[35] = 0;
    int bgy = s_bgy >> 16, seg = 0, end = rows[0];
    w16(RAM_V_scroll_value + 2, (uint16_t)bgy);         /* Vscroll_Factor_BG */
    for (int y = bgy & ~15; y < bgy + 224 + 16; y += 16) tri_bg_band(y, y + 16, 0);   /* LoadTilesAsYouMove */
    for (int l = 0; l < 224; l++) {
        int pos = bgy + l;
        while (pos >= end && seg < 35) end += rows[++seg];
        w32(RAM_H_scroll_buffer + (uint32_t)l * 4u, (uint16_t)-(int16_t)a[seg]);
    }
}

static void cutscene(int o)                          /* ObjCA */
{
    SW(o, 0x32, W(o, 0x32) + 1);
    if (s_ending_routine == 2) s_super = 0xFF;
    switch (B(o, O_rout)) {
    case 0: ca_changer(o, 4, 0x180); return;            /* ObjCA_Init: the photo fades in */
    case 2: case 6:                                     /* loc_A240 */
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) < 0) B(o, O_rout) += 2;
        return;
    case 4: ca_changer(o, 6, 0x80); return;             /* loc_A24E: ... to white */
    case 8: {                                           /* loc_A256: the next photo */
        uint16_t d0 = W(o, 0x2E);
        if (d0 < 0x10) {
            SW(o, 0x2E, d0 + 4);
            B(o, O_rout) = 0;
            plane_map(E_ENI_ENDING1 + d0 / 4, 0x0328, PLANE_A + 8u * 0x80u + 14u * 2u, 12, 9);
            return;
        }
        s_vint_sub = 2;
        w8(RAM_Ctrl_1_locked, 0xFF);
        s_palcycle = 0xFF;
        int p = SLOT_PLAYER;
        memset(s_o[p], 0, 0x40);
        B(p, O_id) = s_ending_routine == 4 ? ID_TAILS : ID_SONIC;
        uint8_t sub = s_ending_routine == 0 ? 8 : s_ending_routine == 2 ? 0x0C : 0x0E;
        ca_changer(o, sub, 0x80);
        SW(o, 0x3C, 0x40);
        return;
    }
    case 0x0A: {                                        /* loc_A30A: Sonic floats in space */
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) < 0) {
            ca_changer(o, 0x0A, 0x80);
            SW(o, 0x3C, 0xC0);
        }
        int p = SLOT_PLAYER;
        B(p, O_anim) = 0x0F;                            /* Float2 */
        SW(p, O_x, 0xA0);
        SW(p, O_y, 0x50);
        if (s_ending_routine == 2) {
            B(p, O_anim) = 0;                           /* Walk */
            SW(p, O_inertia, 0x1000);
        }
        return;
    }
    case 0x0C:                                          /* loc_A34C: the camera drops past the Death Egg */
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) >= 0) { dez_scroll(); return; }
        B(o, O_rout) = 0x0E;
        SW(o, 0x3C, s_ending_routine == 4 ? 0x880 : 0x100);
        return;
    case 0x0E:                                          /* loc_A38E */
        if (W(o, 0x32) == 0x1140) {
            B(o, O_rout) = 0x10;
            s_credits_trigger = 0xFF;
            return;
        }
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (!W(o, 0x3C)) child(o, 0x3E, ID_CC, 0);
        ca_clouds(o);
        return;
    default:
        return;
    }
}

/* ========================================================================
 * Object CC - the Tornado's rescue
 * ====================================================================== */
static void cc_pilot(int o)                          /* ObjB2_Animate_Pilot: the pilot's DPLC */
{
    static const uint8_t tails_frames[24] = { 0, 0, 0, 0, 1, 2, 3, 2, 1, 1, 0, 0, 0, 0, 1, 2, 3, 2, 1, 1, 4, 4, 1, 1 };
    if ((int8_t)--B(o, 0x37) >= 0) return;
    B(o, 0x37) = 8;
    int sonic = s_ending_routine == 4;                  /* (Tails alone: Sonic flies it) */
    int n = sonic ? 4 : 24, k = B(o, 0x36) + 1;
    if (k >= n) k = 0;
    B(o, 0x36) = (uint8_t)k;
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t src = t ? tri_rd32(t + TRI_BLOB_S2_PILOT * 4u) : 0;
    if (!src) return;
    src += (uint32_t)(sonic ? 5 + k : tails_frames[k]) * 6u * 32u;
    tri_vram_queue(src, 0x7A0u * 32u, 6u * 32u);
}

static void cc_hide_player(void)                     /* loc_A594 */
{
    SW(SLOT_PLAYER, O_x, 0x200);
    SW(SLOT_PLAYER, O_y, 0);
}

static void cc_birds(int o)                          /* sub_ABBA */
{
    SW(o, 0x30, W(o, 0x30) - 1);
    if (SWs(o, 0x30) >= 0 || !B(o, 0x35)) return;
    B(o, 0x35)--;
    SW(o, 0x30, (int)(s_rng & 0x0F));
    child(o, 0x3E, ID_CD, 0);
}

static void rescue(int o)                            /* ObjCC */
{
    static const uint8_t frames[3][28] = {
        { 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 0xA, 0xA, 0xA, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB },
        { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 },
        { 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 9, 9, 9, 0xA, 0xA, 0xA, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB, 0xB },
    };
    static const int16_t path[28][2] = {
        { 0xA0, 0x70 }, { 0xB0, 0x70 }, { 0xB6, 0x71 }, { 0xBC, 0x72 }, { 0xC4, 0x74 }, { 0xC8, 0x75 }, { 0xCA, 0x76 }, { 0xCC, 0x77 },
        { 0xCE, 0x78 }, { 0xD0, 0x79 }, { 0xD2, 0x7A }, { 0xD4, 0x7B }, { 0xD6, 0x7C }, { 0xD9, 0x7E }, { 0xDC, 0x81 }, { 0xDE, 0x84 },
        { 0xE1, 0x87 }, { 0xE4, 0x8B }, { 0xE7, 0x8F }, { 0xEC, 0x94 }, { 0xF0, 0x99 }, { 0xF5, 0x9D }, { 0xF9, 0xA4 }, { 0x100, 0xAC },
        { 0x108, 0xB8 }, { 0x112, 0xC4 }, { 0x11F, 0xD3 }, { 0x12C, 0xFA },
    };
    static const uint8_t super_frames[30] = {
        0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x14, 0x14, 0x14,
        0x14, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0,
    };
    static const int16_t super_path[30][2] = {
        { 0xC0, 0x90 }, { 0xB0, 0x91 }, { 0xA8, 0x92 }, { 0x9B, 0x96 }, { 0x99, 0x98 }, { 0x98, 0x99 }, { 0x99, 0x9A }, { 0x9B, 0x9C },
        { 0x9F, 0x9E }, { 0xA4, 0xA0 }, { 0xAC, 0xA2 }, { 0xB7, 0xA5 }, { 0xC4, 0xA8 }, { 0xD3, 0xAB }, { 0xDE, 0xAE }, { 0xE8, 0xB0 },
        { 0xEF, 0xB2 }, { 0xF4, 0xB5 }, { 0xF9, 0xB8 }, { 0xFC, 0xBB }, { 0xFE, 0xBE }, { 0xFF, 0xC0 }, { 0x100, 0xC2 }, { 0x101, 0xC5 },
        { 0x102, 0xC8 }, { 0x102, 0xCC }, { 0x101, 0xD1 }, { 0xFD, 0xD7 }, { 0xF9, 0xDE }, { 0xF9, 0x118 },
    };
    static const int16_t jolt[7][2] = { { -0x3A, 0x88 }, { -0x0C, 0x22 }, { -8, 0x10 }, { -4, 8 }, { -2, 4 }, { -1, 2 }, { -1, 2 } };
    static const int16_t super_away[8][2] = { { 0x60, 0x88 }, { 0x50, 0x68 }, { 0x44, 0x46 }, { 0x3C, 0x36 }, { 0x36, 0x2A }, { 0x33, 0x24 }, { 0x31, 0x20 }, { 0x30, 0x1E } };
    int p = SLOT_PLAYER;
    cc_pilot(o);
    if (!B(o, O_rout)) {                                /* ObjCC_Init */
        SUB_B2(o);
        if (r16(RAM_Player_mode) == 2) { B(o, O_frame) = 4; B(o, O_anim) = 1; }
        SW(o, O_x, -0x10);
        SW(o, O_y, 0xC0);
        SW(o, O_xv, 0x100);
        SW(o, O_yv, -0x80);
        B(o, 0x35) = 0x14;
        B(o, O_prio) = 3;
        s_vint_sub = 4;
        s_chunk_map = s_ending_routine == 4 ? E_ENI_SONICPLANE : E_ENI_TAILSPLANE;
        s_chunk_base = 0x2156;                          /* make_art_tile(EndingFinalTornado,0,1) */
        w16(RAM_Normal_palette + 0x40, 0xC00);          /* Normal_palette_line3: the sky */
        display(o);
        return;
    }
    switch (B(o, O_rout2)) {
    case 0:                                             /* loc_A474: flies in */
        if (X(o) != 0xA0) object_move(o);
        else {
            B(o, O_rout2) = 2;
            SW(o, 0x3C, 0x480);
            SW(o, 0x32, 0x40);
            B(SLOT_CUTSCENE, 0x34) = 0xFF;
            SW(o, O_xv, 0);
            SW(o, O_yv, 0);
        }
        animate(o, E_ANI_B2);
        break;
    case 2:                                             /* loc_A4B6: Sonic rides it */
        cc_birds(o);
        if (s_ending_routine == 0 || s_ending_routine == 4) {          /* sub_A524 */
            SW(p, O_y, Y(o) - (s_ending_routine == 0 ? 0x1C : 0x18));
            SW(p, O_x, X(o));
            player_pose_wait(p);
        } else if (W(o, 0x32)) {
            SW(o, 0x32, W(o, 0x32) - 1);
            SL(p, O_x, L(p, O_x) + 0x8000);
            SW(p, O_y, Y(p) + 1);
        } else {
            SW(p, O_x, 0xC0);
            SW(p, O_y, 0x90);
        }
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) >= 0) { animate(o, E_ANI_B2); break; }
        B(o, O_rout2) = 4;
        SW(o, 0x3C, 2);
        SW(o, 0x32, 0);
        B(o, O_frame) = s_ending_routine == 2 ? 0 : s_ending_routine == 4 ? 0x18 : 7;
        B(o, O_anim) = B(o, O_aframe) = B(o, O_atime) = 0;
        SL(o, O_map, E_MAP_CF);
        SW(o, O_art, 0x0000);
        SW(o, O_x, X(o) - 0x14);
        SW(o, O_y, Y(o) + 0x14);
        if (!s_super) cc_hide_player();                 /* sub_A58C */
        break;
    case 4:                                             /* loc_A5A6: the plane closes in */
        if (!s_super) cc_hide_player();
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) >= 0) break;
        SW(o, 0x3C, 2);
        {
            uint16_t d0 = W(o, 0x32);
            if (d0 < 0x1C) {
                SW(o, 0x32, d0 + 1);
                B(o, O_frame) = frames[s_ending_routine / 2][d0];
                SW(o, O_x, path[d0][0]);
                SW(o, O_y, path[d0][1]);
            } else {
                B(o, O_rout2) = 6;
                SW(o, 0x3C, 0x60);
                B(o, 0x31) = 0;
                SW(o, 0x32, 0);
            }
        }
        break;
    case 6:                                             /* loc_A6C6 */
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) < 0) {                         /* loc_A720 */
            B(o, O_rout2) = 8;
            SW(o, 0x3C, 0);
            SW(o, 0x32, 0);
            child(o, 0x3E, ID_CF, 0);                   /* the propeller */
            if (!s_super) child(o, 0x3E, ID_CE, 0);     /* Sonic and Tails jump off */
            break;
        }
        if (!s_super) break;
        if ((int8_t)--B(o, 0x31) >= 0) break;
        B(o, 0x31) += 3;
        {
            uint16_t d0 = W(o, 0x32);
            SW(o, 0x32, d0 + 4);
            if (d0 >= 0x78) break;
            if (d0 < 0x0C) {                            /* Super Sonic first */
                SW(p, O_x, super_path[d0 / 4][0]);
                SW(p, O_y, super_path[d0 / 4][1]);
                break;
            }
            cc_hide_player();
            SW(o, O_x, super_path[d0 / 4][0]);
            SW(o, O_y, super_path[d0 / 4][1]);
            B(o, O_frame) = super_frames[d0 / 4];
        }
        break;
    case 8:                                             /* loc_A7DE: the close-up jolts into place */
        cc_hide_player();
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) >= 0) break;
        SW(o, 0x3C, 2);
        {
            uint16_t d0 = W(o, 0x32);
            if (d0 < 0x1C) {
                SW(o, 0x32, d0 + 4);
                w16(RAM_H_scroll_buffer, (uint16_t)(r16(RAM_H_scroll_buffer) + jolt[d0 / 4][0]));
                w16(RAM_V_scroll_value, (uint16_t)(r16(RAM_V_scroll_value) + jolt[d0 / 4][1]));
            } else {
                B(o, O_rout2) = 0x0A;
                B(o, O_status) |= 8;                    /* (Sonic and Tails jump now) */
                B(o, 0x31) = 0;
                SW(o, 0x32, 0);
            }
        }
        break;
    default:                                            /* loc_A83E: Super Sonic flies off */
        if (!s_super) break;
        B(o, O_frame) = 0x17;
        if ((int8_t)--B(o, 0x31) >= 0) break;
        B(o, 0x31) += 3;
        {
            uint16_t d0 = W(o, 0x32);
            if (d0 >= 0x20) break;
            SW(o, 0x32, d0 + 4);
            SW(o, O_x, super_away[d0 / 4][0]);
            SW(o, O_y, super_away[d0 / 4][1]);
        }
        break;
    }
    display(o);
}

/* follow plane A's scroll (loc_A90E) */
static void follow_plane(int o)
{
    SW(o, O_x, W(o, 0x30) + r16(RAM_H_scroll_buffer));
    SW(o, O_y, W(o, 0x32) - r16(RAM_V_scroll_value));
}

static void jumpers(int o)                           /* ObjCE: Sonic and Tails jump off the plane */
{
    switch (B(o, O_rout)) {
    case 0:
        SUB_B3(o);
        SL(o, O_map, E_MAP_CF);
        SW(o, O_art, 0x8000);
        B(o, O_prio) = 1;
        B(o, O_frame) = 0x0C;
        if (s_ending_routine == 4) { B(o, O_frame) = 0x0F; SW(o, O_art, 0xA000); }
        SW(o, O_x, 0xE8); SW(o, 0x30, 0xE8);
        SW(o, O_y, 0x118); SW(o, 0x32, 0x118);
        return;
    case 2:
        if (B(W(o, 0x2C), O_status) & 8) { B(o, O_rout) = 4; SW(o, 0x3C, 0); display(o); return; }
        follow_plane(o);
        display(o);
        return;
    case 4: {
        static const int8_t a[4] = { -8, 0, -0x44, -0x38 }, b[4] = { -8, 0, -0x50, -0x40 };
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) >= 0) { display(o); return; }
        SW(o, 0x3C, 4);
        uint16_t d0 = W(o, 0x34);
        if (d0 >= 4) { B(o, O_rout) = 6; display(o); return; }
        SW(o, 0x34, d0 + 2);
        const int8_t *t = s_ending_routine == 2 ? b : a;
        SW(o, O_x, X(o) + t[d0]);
        SW(o, O_y, Y(o) + t[d0 + 1]);
        B(o, O_frame)++;
        display(o);
        return;
    }
    default:
        display(o);
        return;
    }
}

static void helix(int o)                             /* ObjCF: the propeller of the close-up */
{
    if (!B(o, O_rout)) {
        SUB_B3(o);
        SL(o, O_map, E_MAP_CF);
        SW(o, O_art, 0x8000);
        B(o, O_prio) = 3;
        B(o, O_frame) = 5;
        B(o, O_anim) = 2;
        SW(o, O_x, 0x10F); SW(o, 0x30, 0x10F);
        SW(o, O_y, 0x15E); SW(o, 0x32, 0x15E);
        return;
    }
    animate(o, E_ANI_CF);
    follow_plane(o);
    display(o);
}

static void cloud(int o)                             /* ObjCB */
{
    static const uint8_t fr[4] = { 0, 1, 2, 0 };
    static const int16_t yv[4] = { -0x300, -0x200, -0x100, -0x300 };
    switch (B(o, O_rout)) {
    case 0: {
        SUB_B3(o);
        SW(o, O_art, (W(o, O_art) & 0x1FFF) | 0x6000);
        B(o, O_width) = 0x30;
        s_rng = ror32(s_rng, 1);
        uint16_t d0 = (uint16_t)s_rng, d1 = d0;
        d0 &= 3;
        B(o, O_frame) = fr[d0];
        SW(o, O_yv, yv[d0]);
        if (B(SLOT_CUTSCENE, 0x34)) { SW(o, O_y, d1 & 0xFF); SW(o, O_x, 0x150); }
        else { SW(o, O_x, d1 & 0x1FF); SW(o, O_y, 0x100); }
        return;
    }
    case 2:
        if (B(SLOT_CUTSCENE, 0x34)) {
            B(o, O_rout) = 4;
            SW(o, O_xv, W(o, O_yv));
            SW(o, O_yv, 0);
        }
        /* fall through */
    default:
        object_move(o);
        if (B(SLOT_CUTSCENE, 0x34)) {
            if (X(o) < -0x20) { delete_obj(o); return; }
        } else if (Y(o) < 0) { delete_obj(o); return; }
        display(o);
        return;
    }
}

static void bird(int o)                              /* ObjCD */
{
    if (!B(o, O_rout)) {
        sub_obj(o, E_MAP_28, 0x0594, 4, 2, 8, 0);       /* ObjCD_SubObjData (priority 2, as in the original) */
        s_rng = ror32(s_rng, 3);
        uint32_t d1 = s_rng;
        SW(o, O_x, -0xA0 + (int)(s_rng & 0x7F));
        d1 = ror32(d1, 3) & 0xFF;
        SW(o, O_y, 8 + (int)d1);
        SW(o, O_xv, 0x100);
        SW(o, O_yv, d1 < 0x20 ? 0x20 : -0x20);
        SW(o, 0x3C, 0xC0);
        return;
    }
    switch (B(o, O_rout2)) {
    case 0:
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) < 0) {
            B(o, O_rout2) = 2;
            SW(o, 0x2E, W(o, O_yv));
            SW(o, O_xv, 0);
            SW(o, 0x32, W(o, O_y));
            SW(o, O_yv, 0x80);
            SW(o, 0x3C, 0x180);
        }
        break;
    case 2:
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) < 0) {
            B(o, O_rout2) = 4;
            SW(o, O_xv, -0x100);
            SW(o, O_yv, W(o, 0x2E));
            SW(o, 0x3C, 0xC0);
        } else {
            SW(o, O_yv, SWs(o, O_yv) + (W(o, O_y) >= W(o, 0x32) ? -4 : 4));
        }
        break;
    default:
        SW(o, 0x3C, W(o, 0x3C) - 1);
        if (SWs(o, 0x3C) < 0) { delete_obj(o); return; }
        break;
    }
    object_move(o);
    animate(o, E_ANI_CD);
    display(o);
}

static void run_objects(void)                        /* RunObjects */
{
    for (int o = 0; o < NOBJ; o++) {
        s_cur = o;
        switch (B(o, O_id)) {
        case ID_SONIC: case ID_TAILS: player_obj(o); display(o); break;
        case ID_C9: palchanger(o); break;
        case ID_CA: cutscene(o); break;
        case ID_CB: cloud(o); break;
        case ID_CC: rescue(o); break;
        case ID_CD: bird(o); break;
        case ID_CE: jumpers(o); break;
        case ID_CF: helix(o); break;
        default: break;
        }
    }
}

/* ========================================================================
 * The scene: one call per frame from GameLoop
 * ====================================================================== */
enum { PH_ENDING, PH_HOLD, PH_FADE, PH_DONE };
static int s_phase, s_timer;

static void vint_frame(void)                         /* VintID_Ending: the DMA (S3K VInt_6) */
{
    w8(RAM_Water_full_screen_flag, 0);
    w8(RAM_V_int_routine, 6);
    tri_jsr(S3K_Wait_VSync);
}

static void vint_sub(void)                           /* Ending_VInt_Subrout */
{
    uint16_t d0 = s_vint_sub;
    s_vint_sub = 0;
    if (d0 == 2) vram_fill(PLANE_A, 0x2000);
    if (d0 == 4) {
        vram_fill(PLANE_B2, 0x2000);
        vram_fill(PLANE_A, 0x2000);
        set_reg(11, 0x00);                              /* V scroll by screen, H scroll by screen */
        set_reg(4, PLANE_B2 >> 13);
        set_reg(16, 0x11);                              /* 64x64 */
        s_plane64 = 1;
        plane_map(s_chunk_map, s_chunk_base, PLANE_A + 33u * 0x80u + 22u * 2u, 23, 15);
    }
}

/* S2 loads the ending's art over its level tiles; our Death Egg tiles sit
 * elsewhere, so the BG tiles the art would cover move to tiles the BG does
 * not use (and the BG is drawn through the remap) */
static uint16_t s_bg_remap[2048];
static void protect_bg_tiles(int char_tiles)
{
    static const uint16_t art[][2] = { { 0x19, 0 }, { 0x156, 0x156 + 127 }, { 0x328, 0x493 + 109 }, { 0x500, 0x561 }, { 0x594, 0x5A4 }, { PLAYER_TILE, 0x600 } };
    uint8_t used[2048] = { 0 }, taken[2048] = { 0 };
    for (int a = 0; a < 6; a++) {
        int t0 = art[a][0], t1 = a == 0 ? 0x19 + char_tiles : art[a][1];
        for (int t = t0; t < t1 && t < 0x600; t++) taken[t] = 1;
    }
    for (int y = 0; y < 0x300; y += 16)
        for (int x = -64; x < 512 - 64; x += 16) {
            uint16_t tl[4];
            tri_bg_block_tiles(x, y, tl);
            for (int i = 0; i < 4; i++) used[tl[i]] = 1;
        }
    for (int t = 0; t < 2048; t++) s_bg_remap[t] = (uint16_t)t;
    int f = 1;
    for (int t = 1; t < 0x600; t++) {
        if (!used[t] || !taken[t]) continue;
        while (f < 0x600 && (used[f] || taken[f])) f++;
        if (f >= 0x600) break;
        memcpy(&g_machine.vdp.vram[f * 32], &g_machine.vdp.vram[t * 32], 32);
        s_bg_remap[t] = (uint16_t)f;
        taken[f] = 1;
        f++;
    }
    g_bg_tile_remap = s_bg_remap;
}

static void s2end_start(void)                        /* EndingSequence */
{
    gvdp_set_ext_tiles(NULL);
    s_super = 0;
    int tails = r16(RAM_Player_mode) == 2;
    s_knux = r16(RAM_Player_mode) == 3;                 /* (S2 has no Knuckles ending: he takes Sonic's) */
    s_ending_routine = tails ? 4 : (!s_knux && r8(RAM_Chaos_emerald_count) >= 7) ? 2 : 0;
    if (s_ending_routine == 2) s_super = 0xFF;
    ext_init();
    vram_fill(PLANE_A, 0x1000);
    set_reg(0, 0x04);                                   /* H-int off */
    set_reg(11, 0x03);                                  /* H scroll by line */
    set_reg(2, PLANE_A >> 10);
    set_reg(4, PLANE_B >> 13);
    set_reg(16, 0x01);
    set_reg(7, 0x20);                                   /* backdrop: line 2 colour 0 */
    set_reg(18, 0x00);
    {
        int ca = s_ending_routine == 0 ? E_ART_SONIC : s_ending_routine == 2 ? E_ART_SUPERSONIC : E_ART_TAILS;
        uint32_t n;
        item(ca, &n);
        protect_bg_tiles((int)(n / 32));
        load_art(ca, 0x19);
    }
    load_art(s_ending_routine == 0 ? E_ART_FLICKY : s_ending_routine == 2 ? E_ART_EAGLE : E_ART_CHICKEN, 0x594);
    load_art(E_ART_FINALTORNADO, 0x156);
    load_art(E_ART_PICS, 0x328);
    load_art(E_ART_MINITORNADO, 0x493);
    load_art(E_ART_TORNADO, 0x500);
    load_art(E_ART_CLOUDS, 0x54F);
    tri_music_play_s2(TRI_SLOT_ZONE, MUS_ENDING);
    for (uint32_t a = 0; a < 0x80; a += 2) w16(RAM_Normal_palette + a, 0xEEE);
    uint32_t pal = item(E_PAL, NULL);
    for (uint32_t a = 0; a < 0x80; a += 2) w16(RAM_Target_palette + a, tri_rd16(pal + a));
    s_bgx = 0;
    s_bgy = 0xC8 << 16;
    memset(s_dez_layer, 0, sizeof s_dez_layer);
    s_palcycle = s_credits_trigger = 0;
    s_vint_sub = 0;
    s_plane64 = 0;
    s_player_loaded = s_tail_loaded = -1;
    w16(RAM_Level_frame_counter, 0);
    w32(RAM_V_scroll_value, 0);                         /* Vscroll_Factor cleared (SwScrl_DEZ sets the BG later) */
    for (uint32_t a = 0; a < 0x380; a += 2) w16(RAM_H_scroll_buffer + a, 0);
    for (int y = 0; y < 256; y += 16) tri_bg_band(y, y + 16, 0);   /* (the Death Egg's BG, still loaded; white until its palette) */
    memset(s_o, 0, sizeof s_o);
    int c = SLOT_CUTSCENE;                              /* CutScene: ObjCA */
    B(c, O_id) = ID_CA;
    B(c, O_rout) = 6;
    SW(c, 0x3C, 0x60);
    SW(c, 0x30, 1);
    if (s_ending_routine == 4) { SW(c, 0x2E, 0x10); SW(c, 0x3C, 0x100); }
    s_rng = r32(RAM_RNG_seed);
    s_phase = PH_ENDING;
    s_started = 1;
    fprintf(stderr, "[trilogy] Sonic 2 ending (route %d)\n", s_ending_routine / 2);
}

static void s2end_finish(void)                       /* on to Sonic 3 */
{
    s_active = s_started = 0;
    g_bg_tile_remap = NULL;
    set_reg(16, 0x01);
    set_reg(4, PLANE_B >> 13);
    set_reg(11, 0x03);
    fprintf(stderr, "[trilogy] Sonic 2 ending done: on to Angel Island\n");
    w16(RAM_Current_zone_and_act, 0x0000);
    w16(RAM_Apparent_zone_and_act, 0x0000);
    w8(RAM_Last_star_post_hit, 0);
    w8(RAM_Ctrl_1_locked, 0);
    w8(RAM_Game_mode, 0x0C);
    extern void tri_save_progress(uint16_t next_za);
    tri_save_progress(0x0000);                        /* the file resumes at Angel Island */
}

static void s2end_frame(void)
{
    static int frames;
    if (!s_started) { s2end_start(); frames = 0; }
    if (!(++frames % 100) && getenv("TRILOGY_S2END")) fprintf(stderr, "[trilogy] ending frame %d\n", frames);   /* development */
    switch (s_phase) {
    case PH_ENDING:
        w16(RAM_Level_frame_counter, r16(RAM_Level_frame_counter) + 1);
        rnd();
        run_objects();
        ext_super();
        build_sprites();
        if (s_palcycle) tri_s2_dez_palcycle();
        if (s_credits_trigger) { s_phase = PH_HOLD; s_timer = 120; }   /* (no credits: the pose holds) */
        break;
    case PH_HOLD:
        if (--s_timer <= 0) { s_phase = PH_FADE; s_timer = 0x16; }
        break;
    case PH_FADE:                                       /* Pal_FadeToBlack */
        fade_to_black_step();
        if (--s_timer <= 0) s_phase = PH_DONE;
        break;
    default:
        s2end_finish();
        return;
    }
    ext_apply();
    vint_frame();
    if (s_phase == PH_ENDING) vint_sub();
}

/* Sonic 2 is over: the robot's escape faded to white (GameModeID_EndingSequence) */
void tri_s2_complete(void)
{
    fprintf(stderr, "[trilogy] Sonic 2 complete: the ending\n");
    tri_ach_act_clear(0x3200);
    {   extern void tri_s2_sound_set(int on); tri_s2_sound_set(1); }
    s_active = 1;
    s_started = 0;
    w8(RAM_Game_mode, GM_SCENE);
}

int tri_s2end_active(void) { return s_active; }

int tri_s2end_hook(uint32_t pc)
{
    if (pc != TH_GAMELOOP || !s_active) return 0;
    if (r8(RAM_Game_mode) != GM_SCENE) { s_active = 0; return 0; }
    A(0) = tri_native(s2end_frame);
    return 0;
}
