/* Knuckles & Knuckles title gag (only while the mod is enabled).
 *
 * Sonic & Knuckles title: the standing pose's name tables are mirrored so the
 * Knuckles half faces itself, and the banner is recomposed from its own VRAM
 * tiles to read KNUCKLES & KNUCKLES. Sonic 3 & Knuckles title: the red
 * "& KNUCKLES" subtitle echoes down the screen. Both get a parade of gameplay
 * Knuckles. Everything but the one-time name-table mirror is host-drawn into
 * the VDP sprite layer (behind every native sprite, so menus stay on top). */
#include "sonic3_knuckles_title.h"
#include "sonic3_knuckles_art.h"
#include "genesis_runtime.h"
#include "video/genesis_vdp.h"
#include <string.h>

#define ARMY_SITES_DEFINES_ONLY
#include "sonic3_knuckles_army_sites.inc"

enum {
    GAME_MODE = 0xF600, TITLE_MODE = 0x04,
    SK_BANNER = 0xB0DE,              /* Obj_SKTitle_Banner's slot (first dynamic) */
    S3K_AND_KNUCKLES = 0xB206,       /* Obj_TitleANDKnuckles' slot */
    /* SK_Alone_Title_Screen decodes four 40x28 standing-pose frames to
     * RAM_start+$8580/$8E40/$9700/$9FC0 (sonic3k.lst $515C-$51A0). */
    SK_POSE_FRAMES = 0x8580, SK_POSE_STRIDE = 40 * 28 * 2,
};

static uint16_t rd16(unsigned a) { a &= 0xFFFF; return (uint16_t)(g_ram[a] << 8 | g_ram[(a + 1) & 0xFFFF]); }
static uint32_t rd32(unsigned a) { return (uint32_t)rd16(a) << 16 | rd16(a + 2); }
static unsigned rom16(uint32_t a) { return (unsigned)(g_rom[a] << 8 | g_rom[a + 1]); }

static int sk_title(void)
{
    return g_ram[GAME_MODE] == TITLE_MODE && rd32(SK_BANNER + 0xC) == ARMY_MAP_SK_BANNER;
}
static int s3k_title(void)
{
    return g_ram[GAME_MODE] == TITLE_MODE && rd32(S3K_AND_KNUCKLES + 0xC) == ARMY_MAP_AND_KNUCKLES;
}
int s3_title_active(void) { return sk_title() || s3k_title(); }

/* Face-to-face: the Knuckles columns (20-39) mirrored onto Sonic's (0-19). */
static void mirror_pose_frames(void)
{
    for (unsigned f = 0; f < 4; ++f) {
        unsigned base = SK_POSE_FRAMES + f * SK_POSE_STRIDE;
        for (unsigned row = 0; row < 28; ++row)
            for (unsigned col = 0; col < 20; ++col) {
                unsigned from = base + (row * 40 + 39 - col) * 2, to = base + (row * 40 + col) * 2;
                unsigned w = rd16(from) ^ 0x0800;
                g_ram[to] = (uint8_t)(w >> 8); g_ram[to + 1] = (uint8_t)w;
            }
    }
}

int s3_title_hook(uint32_t pc, int enabled)
{
    if (!enabled) return 0;
    if (pc == ARMY_PC_SK_TITLE_FRAMES) { mirror_pose_frames(); return 0; }
    /* The recomposed banner replaces the native one in the sprite layer. */
    if (pc == ARMY_PC_DRAW && sk_title() && (g_cpu.A[0] & 0xFFFF) == SK_BANNER) return 1;
    return 0;
}

/* ---- Latched per-frame description, drawn by the host sprite layer ---- */
typedef struct { int16_t x, y; uint16_t art; uint32_t map; uint8_t frame, first, count; int8_t dx, dy; } Pieces;
typedef struct {
    int active, sk;
    Pieces pieces[6]; unsigned piece_sets;
    uint8_t remap[16];           /* Pal_Knuckles colour -> nearest live CRAM index */
    unsigned tick;
} TitleFrame;
static TitleFrame building, shown;
static unsigned tick;

static void add_pieces(uint32_t map, unsigned frame, unsigned art, int x, int y,
                       unsigned first, unsigned count, int dx, int dy)
{
    if (building.piece_sets >= 6) return;
    Pieces *p = &building.pieces[building.piece_sets++];
    p->map = map; p->frame = (uint8_t)frame; p->art = (uint16_t)art;
    p->x = (int16_t)x; p->y = (int16_t)y;
    p->first = (uint8_t)first; p->count = (uint8_t)count; p->dx = (int8_t)dx; p->dy = (int8_t)dy;
}

static unsigned rgb_distance(uint16_t a, uint16_t b)
{
    int dr = (a & 14) - (b & 14), dg = ((a >> 4) & 14) - ((b >> 4) & 14), db = ((a >> 8) & 14) - ((b >> 8) & 14);
    return (unsigned)(dr * dr * 3 + dg * dg * 4 + db * db * 2);
}

void s3_title_vblank(const GVDP *v, int enabled)
{
    memset(&building, 0, sizeof building);
    if (!enabled || !v || !s3_title_active()) { tick = 0; shown = building; return; }
    building.active = 1; building.sk = sk_title();
    building.tick = tick++;
    /* Nearest live colour for each Knuckles colour: the S3K title's CRAM has
     * no Knuckles line, but its reds, whites and greens are close. */
    for (unsigned c = 1; c < 16; ++c) {
        uint16_t want = (uint16_t)rom16(ARMY_PAL_KNUCKLES + c * 2);
        unsigned best = 1, best_d = ~0u;
        for (unsigned i = 0; i < 64; ++i) {
            if (!(i & 15)) continue;
            unsigned d = rgb_distance(want, v->cram[i]);
            if (d < best_d) { best_d = d; best = i; }
        }
        building.remap[c] = (uint8_t)best;
    }
    if (building.sk) {
        unsigned o = SK_BANNER, frame = g_ram[o + 0x22];
        uint32_t map = rd32(o + 0xC);
        uint32_t entry = map + (uint32_t)(int16_t)rom16(map + frame * 2);
        if (rom16(entry) == 30) {
            /* Map_SKTitle_Banner: pieces 0-4/6-13 SONIC (+ strip), 5 "&",
             * 14-29 the red KNUCKLES. KNUCKLES & KNUCKLES = KNUCKLES raised
             * into SONIC's place, the original KNUCKLES, and the "&". */
            int x = (int16_t)rd16(o + 0x10) - 128, y = (int16_t)rd16(o + 0x14) - 128;
            unsigned art = rd16(o + 0xA);
            add_pieces(map, frame, art, x, y, 14, 16, 0, -44);
            add_pieces(map, frame, art, x, y, 14, 16, 0, 0);
            add_pieces(map, frame, art, x, y, 5, 1, 78, -6);  /* the "&" trails the top word */
        }
    } else {
        unsigned o = S3K_AND_KNUCKLES;
        int x = (int16_t)rd16(o + 0x10) - 128, y = (int16_t)rd16(o + 0x14) - 128;
        for (int n = 1; n <= 3; ++n)
            add_pieces(rd32(o + 0xC), g_ram[o + 0x22], rd16(o + 0xA), x, y + n * 18, 0, 255, 0, 0);
    }
    shown = building;
}

/* One S3K-format mapping piece row from VRAM patterns. */
static void draw_vram_pieces(const GVDP *v, const GVDPSpriteLayer *l, const Pieces *p)
{
    uint32_t entry = p->map + (uint32_t)(int16_t)rom16(p->map + p->frame * 2u);
    unsigned count = rom16(entry);
    for (unsigned n = p->first; n < count && n < (unsigned)p->first + p->count; ++n) {
        uint32_t a = entry + 2 + n * 6;
        int py = p->y + (int8_t)g_rom[a] + p->dy, px = p->x + (int16_t)rom16(a + 4) + p->dx;
        unsigned size = g_rom[a + 1], w = ((size >> 2) & 3) + 1, h = (size & 3) + 1;
        unsigned attr = (rom16(a + 2) + p->art) & 0xFFFF;
        int iy = l->line - py;
        if (iy < 0 || iy >= (int)h * 8) continue;
        if (attr & 0x1000) iy = (int)h * 8 - 1 - iy;
        for (unsigned ix0 = 0; ix0 < w * 8; ++ix0) {
            int col = l->offset + px + (int)ix0;
            if (col < 0 || col >= l->total || l->opaque[col]) continue;
            unsigned ix = (attr & 0x800) ? w * 8 - 1 - ix0 : ix0;
            unsigned tile = ((attr & 0x7FF) + (ix / 8) * h + (unsigned)iy / 8) & 0x7FF;
            uint8_t byte = v->vram[(tile * 32 + ((unsigned)iy & 7) * 4 + (ix & 7) / 2) & 0xFFFF];
            unsigned nib = (ix & 1) ? byte & 15 : byte >> 4;
            if (!nib) continue;
            l->index[col] = (uint8_t)(((attr >> 13) & 3) * 16 + nib);
            l->opaque[col] = 1; l->high[col] = (uint8_t)(attr >> 15);
        }
    }
}

/* Runners along the ground and gliders across the sky, looping. */
static void draw_parade(const GVDPSpriteLayer *l, const KnuxArt *art)
{
    enum { RUNNERS = 9, GLIDERS = 4 };
    static const uint8_t run[4] = { 0x21, 0x22, 0x23, 0x24 };
    for (unsigned n = 0; n < RUNNERS + GLIDERS; ++n) {
        int glider = n >= RUNNERS;
        unsigned k = glider ? n - RUNNERS : n, t = shown.tick;
        int span = 320 + 96;
        int x = glider ? span - (int)((t * 3 / 2 + k * 104) % (unsigned)span) - 48
                       : (int)((t * (2 + k % 3) + k * 46) % (unsigned)span) - 48;
        int y = glider ? 36 + (int)(k * 22) : 206 - (int)(k % 3) * 3;
        unsigned frame = glider ? 0xC0 : run[(t / 4 + k) & 3], flip = glider ? 1 : 0;
        if (frame >= art->count) continue;
        const KnuxFrame *f = &art->frames[frame];
        int dy = l->line - y;
        if (dy < f->y || dy >= f->y + f->height) continue;
        int left = flip ? -(f->x + f->width) : f->x;
        for (int dx = left; dx < left + f->width; ++dx) {
            int col = l->offset + x + dx;
            if (col < 0 || col >= l->total || l->opaque[col]) continue;
            uint8_t p = knux_art_pixel(art, frame, dx, dy, flip);
            if (!p) continue;
            l->index[col] = shown.remap[p & 15];
            l->opaque[col] = 1; l->high[col] = 1;
        }
    }
}

void s3_title_draw(const GVDP *v, const GVDPSpriteLayer *l, const void *knuckles_art)
{
    if (!shown.active) return;
    for (unsigned n = 0; n < shown.piece_sets; ++n) draw_vram_pieces(v, l, &shown.pieces[n]);
    if (knuckles_art) draw_parade(l, (const KnuxArt *)knuckles_art);
}
