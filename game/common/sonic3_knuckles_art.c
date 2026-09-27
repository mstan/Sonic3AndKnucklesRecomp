/* Knuckles frame decoder. Mirrors Knuckles_Load_PLC ($1810E): DPLC entries
 * are (tiles-1)<<12 | source tile, copied back to back from ArtUnc_Knux; the
 * S3K mapping pieces (y.b, size.b, tile.w, x.w) then index that frame-local
 * run. Earlier pieces win, as they do in the SAT. */
#include "sonic3_knuckles_art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const KnuxArtLayout knux_art_stock = { 0x1200E0u, 0x14A8D6u, 0x14BD0Au, 251 };

static int fail(char *error, size_t size, const char *what, unsigned frame)
{
    if (error && size) snprintf(error, size, "Knuckles frame %u: %s", frame, what);
    return 0;
}
static unsigned be16(const uint8_t *rom, uint32_t a) { return (unsigned)(rom[a] << 8 | rom[a + 1]); }

enum { MAX_PIECES = 32, MAX_TILES = 64 };
typedef struct { int x, y, w, h; unsigned tile; } Piece;

static int frame_pieces(const uint8_t *rom, size_t size, const KnuxArtLayout *l, unsigned f,
                        Piece *pieces, unsigned *count, uint8_t tiles[MAX_TILES][32],
                        char *error, size_t error_size)
{
    uint32_t table = l->mappings + 2u * f, plc_table = l->dplc + 2u * f;
    if (table + 2 > size || plc_table + 2 > size) return fail(error, error_size, "table outside ROM", f);
    uint32_t map = l->mappings + (uint32_t)(int16_t)be16(rom, table);
    uint32_t plc = l->dplc + (uint32_t)(int16_t)be16(rom, plc_table);
    if (map + 2 > size || plc + 2 > size) return fail(error, error_size, "entry outside ROM", f);
    unsigned n = be16(rom, map), entries = be16(rom, plc), loaded = 0;
    if (n > MAX_PIECES) return fail(error, error_size, "too many pieces", f);
    if (plc + 2 + entries * 2u > size) return fail(error, error_size, "DPLC outside ROM", f);
    for (unsigned e = 0; e < entries; ++e) {
        unsigned w = be16(rom, plc + 2 + e * 2), run = (w >> 12) + 1, src = w & 0xFFF;
        if (loaded + run > MAX_TILES) return fail(error, error_size, "DPLC exceeds tile budget", f);
        uint32_t from = l->art + src * 32u;
        if (from + run * 32u > size) return fail(error, error_size, "art outside ROM", f);
        memcpy(tiles[loaded], rom + from, run * 32u);
        loaded += run;
    }
    if (map + 2 + n * 6u > size) return fail(error, error_size, "mapping outside ROM", f);
    for (unsigned p = 0; p < n; ++p) {
        uint32_t a = map + 2 + p * 6u;
        Piece *q = &pieces[p];
        unsigned sz = rom[a + 1];
        q->y = (int8_t)rom[a];
        q->w = (int)(((sz >> 2) & 3) + 1) * 8;
        q->h = (int)((sz & 3) + 1) * 8;
        q->tile = be16(rom, a + 2);
        q->x = (int16_t)be16(rom, a + 4);
        unsigned cells = (unsigned)(q->w / 8 * (q->h / 8));
        if ((q->tile & 0x7FF) + cells > loaded) return fail(error, error_size, "piece references unloaded tiles", f);
    }
    *count = n;
    return 1;
}

int knux_art_decode(const uint8_t *rom, size_t size, const KnuxArtLayout *l, KnuxArt *out,
                    char *error, size_t error_size)
{
    memset(out, 0, sizeof *out);
    if (!rom || l->frames > KNUX_ART_FRAMES) return fail(error, error_size, "bad layout", 0);
    static uint8_t tiles[MAX_TILES][32];
    Piece pieces[MAX_PIECES];
    size_t total = 0;
    /* Pass 1 sizes the bitmap store, pass 2 fills it. */
    for (int pass = 0; pass < 2; ++pass) {
        size_t at = 0;
        for (unsigned f = 0; f < l->frames; ++f) {
            unsigned n = 0;
            if (!frame_pieces(rom, size, l, f, pieces, &n, tiles, error, error_size)) {
                knux_art_free(out);
                return 0;
            }
            KnuxFrame *fr = &out->frames[f];
            if (!n) { memset(fr, 0, sizeof *fr); continue; }
            int x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768;
            for (unsigned p = 0; p < n; ++p) {
                if (pieces[p].x < x0) x0 = pieces[p].x;
                if (pieces[p].y < y0) y0 = pieces[p].y;
                if (pieces[p].x + pieces[p].w > x1) x1 = pieces[p].x + pieces[p].w;
                if (pieces[p].y + pieces[p].h > y1) y1 = pieces[p].y + pieces[p].h;
            }
            fr->x = (int16_t)x0; fr->y = (int16_t)y0;
            fr->width = (uint16_t)(x1 - x0); fr->height = (uint16_t)(y1 - y0);
            fr->offset = (uint32_t)at;
            at += (size_t)fr->width * fr->height;
            if (!pass) continue;
            uint8_t *dst = out->pixels + fr->offset;
            for (unsigned p = 0; p < n; ++p) {
                const Piece *q = &pieces[p];
                unsigned cols = (unsigned)q->w / 8, rows = (unsigned)q->h / 8;
                unsigned pal = (q->tile >> 13) & 3, pri = (q->tile & 0x8000) ? 0x80 : 0;
                for (int py = 0; py < q->h; ++py) for (int px = 0; px < q->w; ++px) {
                    int ix = (q->tile & 0x800) ? q->w - 1 - px : px;
                    int iy = (q->tile & 0x1000) ? q->h - 1 - py : py;
                    unsigned cell = (unsigned)(ix / 8) * rows + (unsigned)(iy / 8);
                    (void)cols;
                    const uint8_t *t = tiles[(q->tile & 0x7FF) + cell];
                    uint8_t byte = t[(iy & 7) * 4 + (ix & 7) / 2];
                    unsigned nib = (ix & 1) ? byte & 15 : byte >> 4;
                    uint8_t *o = &dst[(q->y - y0 + py) * fr->width + (q->x - x0 + px)];
                    if (nib && !*o) *o = (uint8_t)(pri | pal * 16 | nib);
                }
            }
        }
        if (!pass) {
            total = at;
            out->pixels = (uint8_t *)calloc(total ? total : 1, 1);
            if (!out->pixels) return fail(error, error_size, "out of memory", 0);
            out->size = total;
        }
    }
    out->count = l->frames;
    return 1;
}

void knux_art_free(KnuxArt *art)
{
    free(art->pixels);
    memset(art, 0, sizeof *art);
}

uint8_t knux_art_pixel(const KnuxArt *art, unsigned frame, int dx, int dy, unsigned flip)
{
    if (frame >= art->count) return 0;
    const KnuxFrame *f = &art->frames[frame];
    if (flip & 1) dx = -dx - 1;
    if (flip & 2) dy = -dy - 1;
    int x = dx - f->x, y = dy - f->y;
    if (x < 0 || y < 0 || x >= f->width || y >= f->height) return 0;
    return art->pixels[f->offset + (uint32_t)y * f->width + (uint32_t)x];
}
