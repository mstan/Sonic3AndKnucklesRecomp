/*
 * trilogy_thumbs.c — the Data Select's zone pictures, live.
 *
 * Where S3K shows a fixed picture of the selected file's zone, the trilogy
 * shows the zone itself: its real act 1 — art, 16x16 blocks, 128x128
 * chunks, layout and palette, the very data the level loads (S3K's own
 * LevelLoadBlock for its zones, the expansion bank's for Sonic 1 and 2) —
 * drawn at a quarter size into the 80x56 card, the camera gliding through
 * the act along its ground. The card is one palette line, so each zone's
 * colours are reduced to 15 (median cut over what the glide shows) when the
 * zone is picked. The 70 tiles go to VRAM in the VBlank, with the sprites.
 */
#include "trilogy_obj.h"
#include "genesis_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t tri_code_to_za(int code);

#define CARD_W      80
#define CARD_H      56
#define CARD_TILE   (0x29F + 0x31B)                  /* ArtTile_Save_Misc+$31B */
#define SCALE       4
#define SPEED       3                                /* level pixels a frame */
#define PATH_STEP   32

static uint8_t s_art[0x10000], s_blocks[0x3000], s_chunks[0x10000], s_layout[0x1000];
static uint16_t s_pal[64];
static int s_code = -1, s_ok;
static int s_xmin, s_xmax, s_ymin, s_ymax, s_camx;
static int16_t s_path[0x8000 / PATH_STEP];
static uint8_t s_lut[512];
static uint16_t s_cpal[16];
static uint8_t s_tiles[70 * 32];
static int s_pending;

/* ---- Kosinski (S3K Kos_Decomp) from the 68K address space ---------------- */
static uint32_t kos_end;                             /* where the last stream ended */
static uint32_t kos_at(uint32_t a, uint8_t *out, uint32_t len, uint32_t cap)
{
    uint32_t pos = a;
    uint16_t desc = (uint16_t)(tri_rd8(pos) | tri_rd8(pos + 1) << 8);
    pos += 2;
    int left = 16;
#define GETBIT(b) do { b = desc & 1; desc >>= 1; if (--left == 0) { desc = (uint16_t)(tri_rd8(pos) | tri_rd8(pos + 1) << 8); pos += 2; left = 16; } } while (0)
    for (;;) {
        int b;
        GETBIT(b);
        if (b) { if (len < cap) out[len] = tri_rd8(pos); len++; pos++; continue; }
        GETBIT(b);
        int cnt, off;
        if (b) {
            int lo = tri_rd8(pos), hi = tri_rd8(pos + 1);
            pos += 2;
            off = (int)(0xE000 | ((hi & 0xF8) << 5) | lo) - 0x10000;
            cnt = hi & 7;
            if (cnt) cnt += 2;
            else {
                int c = tri_rd8(pos++);
                if (c == 0) break;
                if (c == 1) continue;
                cnt = c + 1;
            }
        } else {
            int b1, b2;
            GETBIT(b1);
            GETBIT(b2);
            cnt = (b1 << 1 | b2) + 2;
            off = tri_rd8(pos++) - 0x100;
        }
        for (int k = 0; k < cnt; k++, len++) {
            int s = (int)len + off;
            uint8_t v = s >= 0 && (uint32_t)s < cap ? out[s] : 0;
            if (len < cap) out[len] = v;
        }
    }
#undef GETBIT
    kos_end = pos;
    return len;
}

/* Kosinski data at 68K address a -> out (at most cap bytes); -> its length */
uint32_t tri_kos_decode(uint32_t a, uint8_t *out, uint32_t cap) { return kos_at(a, out, 0, cap); }

static void kosm(uint32_t a, uint8_t *out, uint32_t at, uint32_t cap)   /* KosM archive -> out[at..] */
{
    uint32_t size = tri_rd16(a);
    if (size == 0xA000) size = 0x8000;
    a += 2;
    uint32_t len = at;
    while (len - at < size && len < cap) {
        /* modules decompress in their own $1000 window: back-references stay inside a module */
        uint32_t base = len;
        uint32_t n = kos_at(a, out + base, 0, cap - base);
        len = base + n;
        uint32_t ln = kos_end - a;
        a += ln + ((0u - ln) & 15u);
        if (!n) break;
    }
}

/* ---- level sampling ---------------------------------------------------------- */
static int s_fgw, s_bgw, s_fgh, s_bgh;
static uint16_t lay16(int a) { return (uint16_t)(s_layout[a & 0xFFF] << 8 | s_layout[(a + 1) & 0xFFF]); }
static int chunk_at(int bg, int x, int y)
{
    int w = bg ? s_bgw : s_fgw, h = bg ? s_bgh : s_fgh;
    if (w <= 0 || h <= 0) return 0;
    int r = y >> 7, c = x >> 7;
    if (bg) { r %= h; if (r < 0) r += h; c %= w; if (c < 0) c += w; }
    else if (r < 0 || r >= h || c < 0 || c >= w) return 0;
    int row = lay16(8 + r * 4 + (bg ? 2 : 0)) - 0x8000;
    return s_layout[(row + c) & 0xFFF];
}
static uint16_t chunk_word(int bg, int x, int y)
{
    int ch = chunk_at(bg, x, y);
    uint32_t o = (uint32_t)ch * 128u + (uint32_t)(((y >> 4) & 7) * 16 + ((x >> 4) & 7) * 2);
    return (uint16_t)(s_chunks[o & 0xFFFF] << 8 | s_chunks[(o + 1) & 0xFFFF]);
}
static int pixel(int bg, int x, int y)               /* palette index (line*16+n), 0 = see-through */
{
    if (bg) { x &= 0x7FFFFFFF; }
    uint16_t cw = chunk_word(bg, x, y);
    int b = cw & 0x3FF, xf = (cw >> 10) & 1, yf = (cw >> 11) & 1;
    int bx = ((x >> 3) & 1) ^ xf, by = ((y >> 3) & 1) ^ yf;
    uint32_t bo = (uint32_t)b * 8u + (uint32_t)(by * 2 + bx) * 2u;
    if (bo + 1 >= sizeof s_blocks) return 0;
    uint16_t tw = (uint16_t)(s_blocks[bo] << 8 | s_blocks[bo + 1]);
    int hf = ((tw >> 11) & 1) ^ xf, vf = ((tw >> 12) & 1) ^ yf;
    int px = x & 7, py = y & 7;
    if (hf) px = 7 - px;
    if (vf) py = 7 - py;
    uint8_t v = s_art[((uint32_t)(tw & 0x7FF) * 32u + (uint32_t)py * 4u + (uint32_t)(px >> 1)) & 0xFFFF];
    v = (px & 1) ? (v & 15) : (v >> 4);
    return v ? ((tw >> 13) & 3) * 16 + v : 0;
}
static uint16_t colour_at(int camx, int camy, int x, int y)
{
    int p = pixel(0, camx + x, camy + y);
    if (!p) p = pixel(1, camx / 2 + x, camy / 4 + y);
    return p ? s_pal[p] : s_pal[0x20];
}
static int solid(int x, int y) { return y >= 0 && (chunk_word(0, x, y) & 0x1000) != 0; }

/* the card's pixel: four samples of its 4x4 patch, averaged in 3-bit channels */
static int card_rgb(int camx, int camy, int ox, int oy)
{
    int r = 0, g = 0, b = 0;
    for (int k = 0; k < 4; k++) {
        uint16_t c = colour_at(camx, camy, ox * SCALE + 1 + (k & 1) * 2, oy * SCALE + 1 + (k >> 1) * 2);
        r += (c >> 1) & 7; g += (c >> 5) & 7; b += (c >> 9) & 7;
    }
    return ((r + 2) / 4) | ((g + 2) / 4) << 3 | ((b + 2) / 4) << 6;
}

static int cam_y(int x)
{
    int i = (x - s_xmin) / PATH_STEP;
    if (i < 0) i = 0;
    if (i >= (int)(sizeof s_path / sizeof s_path[0])) i = (int)(sizeof s_path / sizeof s_path[0]) - 1;
    return s_path[i];
}

/* ---- colours: median cut to 15 --------------------------------------------- */
static uint32_t s_hist[512];
typedef struct { int lo[3], hi[3]; } Box;
static uint32_t box_count(const Box *bx)
{
    uint32_t n = 0;
    for (int r = bx->lo[0]; r <= bx->hi[0]; r++)
        for (int g = bx->lo[1]; g <= bx->hi[1]; g++)
            for (int b = bx->lo[2]; b <= bx->hi[2]; b++) n += s_hist[r | g << 3 | b << 6];
    return n;
}
static void shrink(Box *bx)
{
    int lo[3] = { 7, 7, 7 }, hi[3] = { 0, 0, 0 };
    for (int r = bx->lo[0]; r <= bx->hi[0]; r++)
        for (int g = bx->lo[1]; g <= bx->hi[1]; g++)
            for (int b = bx->lo[2]; b <= bx->hi[2]; b++)
                if (s_hist[r | g << 3 | b << 6]) {
                    int v[3] = { r, g, b };
                    for (int k = 0; k < 3; k++) { if (v[k] < lo[k]) lo[k] = v[k]; if (v[k] > hi[k]) hi[k] = v[k]; }
                }
    if (lo[0] <= hi[0]) for (int k = 0; k < 3; k++) { bx->lo[k] = lo[k]; bx->hi[k] = hi[k]; }
}
static void make_palette(void)
{
    Box box[15];
    int nb = 1;
    box[0] = (Box){ { 0, 0, 0 }, { 7, 7, 7 } };
    shrink(&box[0]);
    while (nb < 15) {
        int best = -1, axis = 0;
        uint32_t score = 0;
        for (int i = 0; i < nb; i++) {
            for (int k = 0; k < 3; k++) {
                int span = box[i].hi[k] - box[i].lo[k];
                if (!span) continue;
                uint32_t s = (uint32_t)span * (box_count(&box[i]) + 1);
                if (s > score) { score = s; best = i; axis = k; }
            }
        }
        if (best < 0) break;
        Box a = box[best], b = box[best];
        uint32_t total = box_count(&box[best]), acc = 0;
        int cut = a.lo[axis];
        for (int v = a.lo[axis]; v < a.hi[axis]; v++) {
            Box s = a;
            s.lo[axis] = s.hi[axis] = v;
            acc += box_count(&s);
            cut = v;
            if (acc * 2 >= total) break;
        }
        a.hi[axis] = cut;
        b.lo[axis] = cut + 1;
        shrink(&a);
        shrink(&b);
        box[best] = a;
        box[nb++] = b;
    }
    int rep[15][3];
    for (int i = 0; i < nb; i++) {
        double s[3] = { 0, 0, 0 }, n = 0;
        for (int r = box[i].lo[0]; r <= box[i].hi[0]; r++)
            for (int g = box[i].lo[1]; g <= box[i].hi[1]; g++)
                for (int b = box[i].lo[2]; b <= box[i].hi[2]; b++) {
                    uint32_t c = s_hist[r | g << 3 | b << 6];
                    s[0] += r * (double)c; s[1] += g * (double)c; s[2] += b * (double)c; n += c;
                }
        for (int k = 0; k < 3; k++) rep[i][k] = n ? (int)(s[k] / n + 0.5) : (box[i].lo[k] + box[i].hi[k]) / 2;
    }
    memset(s_cpal, 0, sizeof s_cpal);
    for (int i = 0; i < nb; i++) s_cpal[1 + i] = (uint16_t)(rep[i][2] << 9 | rep[i][1] << 5 | rep[i][0] << 1);
    for (int c = 0; c < 512; c++) {
        int r = c & 7, g = (c >> 3) & 7, b = c >> 6, bi = 0, bd = 1 << 30;
        for (int i = 0; i < nb; i++) {
            int dr = r - rep[i][0], dg = g - rep[i][1], db = b - rep[i][2];
            int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bd) { bd = d; bi = i; }
        }
        s_lut[c] = (uint8_t)(1 + bi);
    }
}

/* ---- a zone --------------------------------------------------------------- */
static void load_zone(int code)
{
    s_code = code;
    s_ok = 0;
    uint16_t za = tri_code_to_za(code);
    int zone = za >> 8, act = za & 0xFF;
    uint32_t llb, lay, sizes, start;
    if (zone >= TRI_ZONE_BASE) {
        uint32_t dir = tri_dir_for(zone, act);
        if (!dir) return;
        llb = tri_rd32(dir + TD_LLB);
        lay = tri_rd32(dir + TD_LAYOUT);
        sizes = dir + TD_SIZES;
        start = dir + TD_START_S;
    } else {
        uint32_t i = (uint32_t)zone * 2u + (uint32_t)act;
        llb = S3K_LevelLoadBlock + i * 24u;
        lay = tri_rd32(S3K_LevelPtrs + i * 4u);
        sizes = S3K_LevelSizes + i * 8u;
        start = S3K_Sonic_Start_Locations + i * 4u;
    }
    memset(s_art, 0, sizeof s_art);
    memset(s_blocks, 0, sizeof s_blocks);
    memset(s_chunks, 0, sizeof s_chunks);
    uint32_t a0 = tri_rd32(llb) & 0xFFFFFFu, a1 = tri_rd32(llb + 4) & 0xFFFFFFu;
    kosm(a0, s_art, 0, sizeof s_art);
    if (a1 != a0) kosm(a1, s_art, tri_rd16(a0) == 0xA000 ? 0x8000 : tri_rd16(a0), sizeof s_art);
    uint32_t b0 = tri_rd32(llb + 8) & 0xFFFFFFu, b1 = tri_rd32(llb + 12) & 0xFFFFFFu;
    uint32_t n = kos_at(b0, s_blocks, 0, sizeof s_blocks);
    if (b1 != b0) kos_at(b1, s_blocks, n, sizeof s_blocks);
    uint32_t c0 = tri_rd32(llb + 16) & 0xFFFFFFu, c1 = tri_rd32(llb + 20) & 0xFFFFFFu;
    n = kos_at(c0, s_chunks, 0, sizeof s_chunks);
    if (c1 != c0) kos_at(c1, s_chunks, n, sizeof s_chunks);
    for (uint32_t k = 0; k < sizeof s_layout; k++) s_layout[k] = tri_rd8(lay + k);
    s_fgw = lay16(0); s_bgw = lay16(2); s_fgh = lay16(4); s_bgh = lay16(6);
    /* palette: Sonic's line, then the level's (its PalPoint entry) */
    memset(s_pal, 0, sizeof s_pal);
    for (int k = 0; k < 16; k++) s_pal[k] = tri_rd16(S3K_Pal_SonicTails + (uint32_t)k * 2u);
    int pid = tri_rd8(llb + 8);
    uint32_t pe = pid >= TRI_PAL_BASE ? tri_rd32(TRI_BANK_BASE + TB_PALPOINT) + (uint32_t)(pid - TRI_PAL_BASE) * 8u
                                      : S3K_PalPoint + (uint32_t)pid * 8u;
    uint32_t src = tri_rd32(pe);
    int dst = (tri_rd16(pe + 4) - 0xFC00) / 2, cnt = (tri_rd16(pe + 6) + 1) * 2;
    for (int k = 0; k < cnt && dst + k < 64; k++) if (dst + k >= 0) s_pal[dst + k] = tri_rd16(src + (uint32_t)k * 2u);
    s_xmin = (int16_t)tri_rd16(sizes);
    s_xmax = (int16_t)tri_rd16(sizes + 2);
    s_ymin = (int16_t)tri_rd16(sizes + 4);
    s_ymax = (int16_t)tri_rd16(sizes + 6);
    if (s_ymin < 0) s_ymin = 0;
    if (s_ymax < s_ymin) s_ymax = s_ymin;
    if (s_xmax < s_xmin) s_xmax = s_xmin;
    int sx = tri_rd16(start), sy = tri_rd16(start + 2);
    /* the glide: along the ground, the way the camera keeps a player low in the screen */
    int cy = sy - 128;
    double fy = cy;
    int np = (s_xmax + 320 - s_xmin) / PATH_STEP + 1;
    if (np > (int)(sizeof s_path / sizeof s_path[0])) np = (int)(sizeof s_path / sizeof s_path[0]);
    for (int i = 0; i < np; i++) {
        int x = s_xmin + i * PATH_STEP + 160;
        int target = (int)fy;
        int y0 = i ? (int)fy - 200 : sy - 64, y1 = i ? (int)fy + 600 : s_ymax + 224;   /* (the first look scans down the whole act) */
        for (int y = y0 < 0 ? 0 : y0; y < y1; y += 8)
            if (solid(x, y) && !solid(x, y - 16)) { target = y - 150; break; }
        if (!i) fy = target;
        fy += (target - fy) / 6.0;
        int v = (int)fy;
        if (v < s_ymin) v = s_ymin;
        if (v > s_ymax) v = s_ymax;
        s_path[i] = (int16_t)v;
    }
    s_camx = sx - 160 < s_xmin ? s_xmin : sx - 160;
    /* colours from a sweep over the act */
    memset(s_hist, 0, sizeof s_hist);
    for (int w = 0; w < 16; w++) {
        int x = s_camx + (s_xmax - s_camx) * w / 16;
        for (int oy = 0; oy < CARD_H; oy += 2)
            for (int ox = 0; ox < CARD_W; ox += 2) s_hist[card_rgb(x, cam_y(x), ox, oy)]++;
    }
    make_palette();
    s_ok = 1;
    fprintf(stderr, "[trilogy] data select: live card for level code %d ($%04X)\n", code, za);
}

static void render_card(void)
{
    int camy = cam_y(s_camx);
    static uint8_t px[CARD_H][CARD_W];
    for (int oy = 0; oy < CARD_H; oy++)
        for (int ox = 0; ox < CARD_W; ox++) px[oy][ox] = s_lut[card_rgb(s_camx, camy, ox, oy)];
    /* the card's six sprite pieces (frame $17), each column-major */
    static const struct { int x, y, w, h; } pc[6] = { { 0, 0, 4, 4 }, { 32, 0, 4, 4 }, { 64, 0, 2, 4 },
                                                      { 0, 32, 4, 3 }, { 32, 32, 4, 3 }, { 64, 32, 2, 3 } };
    uint8_t *t = s_tiles;
    for (int p = 0; p < 6; p++)
        for (int tx = 0; tx < pc[p].w; tx++)
            for (int ty = 0; ty < pc[p].h; ty++, t += 32)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2) {
                        int X = pc[p].x + tx * 8 + x, Y = pc[p].y + ty * 8 + y;
                        t[y * 4 + x / 2] = (uint8_t)(px[Y][X] << 4 | px[Y][X + 1]);
                    }
    s_pending = 1;
}


/* ========================================================================
 * Recorded cards (tools/trilogy/previews.py): the zone as the game draws it,
 * every colour, played back over the card through the frame overlay. The
 * card sprite itself is one key colour, so whatever S3K draws over it (the
 * selector's arrows) stays on top.
 * ====================================================================== */
#include "video/genesis_dac.h"
#include <SDL.h>

#define KEY_CRAM    0x0E0E                           /* card line colour 1 */

/* ---- inflate (RFC 1951) ------------------------------------------------ */
typedef struct { uint16_t counts[16], symbols[288]; } Tree;
typedef struct { const uint8_t *src, *end; uint32_t tag; int bits; uint8_t *dst; size_t len, cap; } Inf;

static int inf_bit(Inf *d)
{
    if (!d->bits--) { d->tag = d->src < d->end ? *d->src++ : 0; d->bits = 7; }
    int b = d->tag & 1;
    d->tag >>= 1;
    return b;
}
static unsigned inf_bits(Inf *d, int n) { unsigned v = 0; for (int i = 0; i < n; i++) v |= (unsigned)inf_bit(d) << i; return v; }
static void tree_build(Tree *t, const uint8_t *lens, int n)
{
    uint16_t offs[16];
    memset(t->counts, 0, sizeof t->counts);
    for (int i = 0; i < n; i++) t->counts[lens[i]]++;
    t->counts[0] = 0;
    for (int i = 0, sum = 0; i < 16; i++) { offs[i] = (uint16_t)sum; sum += t->counts[i]; }
    for (int i = 0; i < n; i++) if (lens[i]) t->symbols[offs[lens[i]]++] = (uint16_t)i;
}
static int tree_sym(Inf *d, const Tree *t)
{
    int sum = 0, cur = 0, len = 0;
    do { cur = 2 * cur + inf_bit(d); ++len; if (len > 15) return -1; sum += t->counts[len]; cur -= t->counts[len]; } while (cur >= 0);
    return t->symbols[sum + cur];
}
static int inf_block(Inf *d, const Tree *lt, const Tree *dt)
{
    static const uint16_t lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint8_t lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint16_t dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const uint8_t dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    for (;;) {
        int sym = tree_sym(d, lt);
        if (sym < 0) return -1;
        if (sym < 256) { if (d->len < d->cap) d->dst[d->len] = (uint8_t)sym; d->len++; continue; }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) return -1;
        size_t n = lbase[sym] + inf_bits(d, lext[sym]);
        int ds = tree_sym(d, dt);
        if (ds < 0 || ds >= 30) return -1;
        size_t dist = dbase[ds] + inf_bits(d, dext[ds]);
        if (dist > d->len) return -1;
        for (size_t k = 0; k < n; k++, d->len++)
            if (d->len < d->cap) d->dst[d->len] = d->dst[d->len - dist];
    }
}
static size_t inflate_zlib(const uint8_t *src, size_t n, uint8_t *dst, size_t cap)
{
    Inf d = { src + 2, src + n, 0, 0, dst, 0, cap };
    Tree lt, dt;
    int last;
    do {
        last = inf_bit(&d);
        int type = (int)inf_bits(&d, 2);
        if (type == 0) {
            d.bits = 0;
            if (d.src + 4 > d.end) break;
            unsigned len = d.src[0] | d.src[1] << 8;
            d.src += 4;
            for (unsigned k = 0; k < len && d.src < d.end; k++, d.len++) if (d.len < cap) dst[d.len] = *d.src++;
        } else if (type == 1) {
            uint8_t l[288 + 30];
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            tree_build(&lt, l, 288);
            for (int i = 0; i < 30; i++) l[i] = 5;
            tree_build(&dt, l, 30);
            if (inf_block(&d, &lt, &dt)) break;
        } else if (type == 2) {
            static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            unsigned hlit = inf_bits(&d, 5) + 257, hdist = inf_bits(&d, 5) + 1, hclen = inf_bits(&d, 4) + 4;
            uint8_t cl[19] = { 0 }, lens[288 + 32] = { 0 };
            for (unsigned i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)inf_bits(&d, 3);
            Tree ct;
            tree_build(&ct, cl, 19);
            for (unsigned i = 0; i < hlit + hdist;) {
                int sym = tree_sym(&d, &ct);
                if (sym < 0) return 0;
                if (sym < 16) { lens[i++] = (uint8_t)sym; continue; }
                unsigned rep; uint8_t v = 0;
                if (sym == 16) { if (!i) return 0; v = lens[i - 1]; rep = 3 + inf_bits(&d, 2); }
                else if (sym == 17) rep = 3 + inf_bits(&d, 3);
                else rep = 11 + inf_bits(&d, 7);
                while (rep-- && i < hlit + hdist) lens[i++] = v;
            }
            tree_build(&lt, lens, (int)hlit);
            tree_build(&dt, lens + hlit, (int)hdist);
            if (inf_block(&d, &lt, &dt)) break;
        } else break;
    } while (!last);
    return d.len;
}

/* ---- the recorded file ------------------------------------------------- */
static uint8_t *s_file;
static size_t s_file_len;
static int s_file_tried;
static uint8_t *s_frames;                            /* the current zone's frames (8 bpp) */
static int s_nframes, s_frames_code = -1;
static uint32_t s_argb[256];
static int s_t, s_live;                              /* playback clock; frames since a card was asked for */
static int s_cx, s_cy;                               /* the card's top-left, native screen pixels */

static void load_file(void)
{
    if (s_file_tried) return;
    s_file_tried = 1;
    char *base = SDL_GetBasePath();
    char path[1024];
    snprintf(path, sizeof path, "%strilogy_previews.bin", base ? base : "");
    SDL_free(base);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 16 && (s_file = (uint8_t *)malloc((size_t)n)) && fread(s_file, 1, (size_t)n, f) == (size_t)n && !memcmp(s_file, "TPRV", 4))
        s_file_len = (size_t)n;
    else { free(s_file); s_file = NULL; }
    fclose(f);
}

static unsigned be16h(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32h(const uint8_t *p) { return (uint32_t)be16h(p) << 16 | be16h(p + 2); }

static int load_frames(int code)
{
    if (code == s_frames_code) return s_nframes > 0;
    s_frames_code = code;
    free(s_frames);
    s_frames = NULL;
    s_nframes = 0;
    load_file();
    if (!s_file || code < 0 || (unsigned)code >= be16h(s_file + 4)) return 0;
    const uint8_t *e = s_file + 12 + code * 12;
    uint32_t off = be32h(e), size = be32h(e + 4);
    int nf = (int)be16h(e + 8);
    if (!size || off + size > s_file_len || size < 768 + 2) return 0;
    const uint8_t *pal = s_file + off;
    for (int k = 0; k < 256; k++) s_argb[k] = 0xFF000000u | (uint32_t)pal[k * 3] << 16 | (uint32_t)pal[k * 3 + 1] << 8 | pal[k * 3 + 2];
    size_t need = (size_t)nf * CARD_W * CARD_H;
    s_frames = (uint8_t *)malloc(need);
    if (!s_frames) return 0;
    if (inflate_zlib(pal + 768, size - 768, s_frames, need) != need) { free(s_frames); s_frames = NULL; return 0; }
    s_nframes = nf;
    s_t = 0;
    return 1;
}

/* the selected file shows a zone card (Load_Icon_Art, every frame; a0 = its slot) */
void tri_thumb_show(int code)
{
    uint32_t slot = A(0) & 0xFFFFFFu;
    s_cx = (int16_t)r16(slot + OBJ_x_pos) - 40 - 128;
    s_cy = (int16_t)r16(slot + OBJ_y_pos) - 120 - 128;
    if (load_frames(code)) {
        s_t++;
        s_live = 2;
        memset(s_tiles, 0x11, sizeof s_tiles);       /* the key colour, everywhere */
        s_pending = 1;
        for (int k = 0; k < 16; k++) w16(RAM_Normal_palette + 0x60u + (uint32_t)k * 2u, k == 1 ? KEY_CRAM : 0);
        return;
    }
    s_live = 0;                                      /* (no recording: drawn here, 15 colours) */
    if (code != s_code) load_zone(code);
    if (!s_ok) return;
    s_camx += SPEED;
    if (s_camx > s_xmax) s_camx = s_xmin;
    render_card();
    for (int k = 0; k < 16; k++) w16(RAM_Normal_palette + 0x60u + (uint32_t)k * 2u, s_cpal[k]);
}

void tri_thumb_vblank(void)
{
    if (s_live > 0) s_live--;
    if (!s_pending) return;
    s_pending = 0;
    memcpy(&g_machine.vdp.vram[CARD_TILE * 32], s_tiles, sizeof s_tiles);
}

/* the frame overlay (trilogy_menu.c chains it): the recorded frame where the key shows */
void tri_thumb_overlay(int line, uint32_t *out, int width)
{
    if (!s_live || !s_frames || (r8(RAM_Game_mode) & 0x7F) != 0x4C) return;
    int y = line - s_cy;
    if (y < 0 || y >= CARD_H) return;
    uint32_t key = genesis_dac_cram_to_argb(KEY_CRAM, GENESIS_DAC_NORMAL);
    const uint8_t *row = s_frames + ((size_t)((s_t / 2) % s_nframes) * CARD_H + (size_t)y) * CARD_W;
    int x0 = s_cx + (width - 320) / 2;
    for (int x = 0; x < CARD_W; x++) {
        int X = x0 + x;
        if (X < 0 || X >= width) continue;
        if (out[X] == key) out[X] = s_argb[row[x]];
    }
}
