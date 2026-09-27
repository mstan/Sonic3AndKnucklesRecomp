/* Decode the stock standing Knuckles, resting hands and red banner from the
 * owner's ROM. Addresses are generated from the pinned source disassembly.
 * The original title updates hand tiles separately (Obj_SKTitle_HandAnim);
 * the base pose alone contains numbered development placeholders. */
#include "sonic3_title_art.h"
#include <string.h>
#define ARMY_SITES_DEFINES_ONLY
#include "sonic3_knuckles_army_sites.inc"

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
typedef struct { const uint8_t *p, *end; unsigned desc, bits; int error; } Kos;
static unsigned byte(Kos *r)
{
    if (r->p == r->end) { r->error = 1; return 0; }
    return *r->p++;
}
static unsigned bit(Kos *r)
{
    unsigned b = r->desc & 1; r->desc >>= 1;
    if (!--r->bits) { unsigned lo = byte(r); r->desc = lo | byte(r) << 8; r->bits = 16; }
    return b;
}
static size_t kos(const uint8_t *src, size_t size, uint8_t *out, size_t cap, size_t *used)
{
    Kos r = { src, src + size, 0, 16, 0 }; size_t n = 0;
    unsigned lo = byte(&r); r.desc = lo | byte(&r) << 8;
    while (!r.error) {
        if (bit(&r)) {
            unsigned v = byte(&r);
            if (r.error || n == cap) return 0;
            out[n++] = (uint8_t)v; continue;
        }
        unsigned count; int offset;
        if (bit(&r)) {
            lo = byte(&r); unsigned hi = byte(&r);
            offset = (int)(((hi & 248) << 5) | lo) - 8192; count = hi & 7;
            if (count) count += 2;
            else {
                count = byte(&r);
                if (r.error) return 0;
                if (!count) { *used = (size_t)(r.p - src); return n; }
                if (count == 1) continue;
                ++count;
            }
        } else {
            count = bit(&r) << 1; count = (count | bit(&r)) + 2;
            offset = (int)byte(&r) - 256;
        }
        if (r.error || (size_t)-offset > n || count > cap - n) return 0;
        while (count--) { out[n] = out[n - (size_t)-offset]; ++n; }
    }
    return 0;
}
static size_t kosm(const uint8_t *src, size_t size, uint8_t *out, size_t cap)
{
    if (size < 2) return 0;
    size_t total = be16(src), pos = 2, n = 0;
    if (!total || total > cap) return 0;
    while (n < total && pos < size) {
        size_t used = 0, want = total - n > 4096 ? 4096 : total - n;
        if (kos(src + pos, size - pos, out + n, want, &used) != want) return 0;
        n += want; pos += used; pos = 2 + ((pos - 2 + 15) & ~(size_t)15);
    }
    return n == total ? n : 0;
}
typedef struct { const uint8_t *src; size_t size, pos; int error; } Bits;
static unsigned bits(Bits *b, unsigned count)
{
    unsigned value = 0;
    while (count--) {
        if (b->pos / 8 >= b->size) { b->error = 1; return 0; }
        value = value << 1 | ((b->src[b->pos / 8] >> (7 - b->pos % 8)) & 1);
        ++b->pos;
    }
    return value;
}
static unsigned eni_value(Bits *b, unsigned width, unsigned flags)
{
    unsigned value = 0;
    for (int i = 4; i >= 0; --i)
        if (flags & (1u << i)) value |= bits(b, 1) << (11 + i);
    return value + bits(b, width);
}
static size_t enigma(const uint8_t *src, size_t size, uint16_t *out, size_t cap)
{
    if (size < 6 || !src[0] || src[0] > 11 || src[1] > 31) return 0;
    Bits b = { src, size, 48, 0 }; size_t n = 0;
    unsigned width = src[0], flags = src[1], inc = be16(src + 2), common = be16(src + 4);
    while (!b.error) {
        unsigned mode = bits(&b, 1) ? 4 + bits(&b, 2) : bits(&b, 1);
        unsigned count = bits(&b, 4) + 1;
        if (b.error) return 0;
        if (mode == 7 && count == 16) return n;
        if (count > cap - n) return 0;
        unsigned value = mode == 0 ? inc : mode == 1 ? common : mode == 7 ? 0 : eni_value(&b, width, flags);
        while (count--) {
            out[n++] = (uint16_t)(mode == 7 ? eni_value(&b, width, flags) : value);
            if (mode == 0 || mode == 5) ++value;
            if (mode == 6) --value;
        }
        if (mode == 0) inc = value;
    }
    return 0;
}
static unsigned pixel(const uint8_t *tiles, size_t size, unsigned attr, unsigned x, unsigned y)
{
    if (attr & 0x800) x = 7 - x;
    if (attr & 0x1000) y = 7 - y;
    size_t pos = (attr & 0x7FF) * 32u + y * 4 + x / 2;
    if (pos >= size) return 0;
    unsigned b = tiles[pos]; return x & 1 ? b & 15 : b >> 4;
}
int s3_title_art_decode(const uint8_t *rom, size_t size, S3TitleArt *out)
{
    /* Scratch only during startup; keep the 68K fiber's stack small. */
    static uint8_t pose[0x7000], hands[0x3000], word[0x2400];
    static uint16_t map[40 * 28];
    memset(out, 0, sizeof *out);
    if (size < ARMY_ART_TITLE_HANDS_END || size < ARMY_MAP_TITLE_POSE_END ||
        size < ARMY_PAL_TITLE_KNUCKLES + 32 || size < ARMY_MAP_SK_BANNER + 184) return 0;
    size_t used = 0;
    if (kosm(rom + ARMY_ART_TITLE_POSE, ARMY_ART_TITLE_POSE_END - ARMY_ART_TITLE_POSE,
             pose, sizeof pose) != sizeof pose ||
        kos(rom + ARMY_ART_TITLE_HANDS, ARMY_ART_TITLE_HANDS_END - ARMY_ART_TITLE_HANDS,
            hands, sizeof hands, &used) < 0x24A0 ||
        kosm(rom + ARMY_ART_TITLE_WORD, ARMY_ART_TITLE_WORD_END - ARMY_ART_TITLE_WORD,
             word, sizeof word) != 0x2240 ||
        enigma(rom + ARMY_MAP_TITLE_POSE, ARMY_MAP_TITLE_POSE_END - ARMY_MAP_TITLE_POSE,
               map, 40 * 28) != 40 * 28) return 0;
    /* First frame of SKTitle_AnimKnuckle1/2, matching the stock DMA ranges. */
    memcpy(pose + 0x2D * 32, hands + 0x1080, 0x5E0);
    memcpy(pose + 0x5C * 32, hands + 0x2220, 0x280);
    unsigned m = ARMY_MAP_SK_BANNER + be16(rom + ARMY_MAP_SK_BANNER);
    if (m > size || size - m < 182 || be16(rom + m) != 30) return 0;
    for (unsigned i = 14; i < 30; ++i) {
        const uint8_t *p = rom + m + 2 + i * 6;
        unsigned w = ((p[1] >> 2) & 3) + 1, h = (p[1] & 3) + 1, a = be16(p + 2);
        int x = (int16_t)be16(p + 4) + 124, y = (int8_t)p[0];
        if (x < 0 || y < 0 || x + w * 8 > S3_TITLE_WORD_W || y + h * 8 > S3_TITLE_WORD_H ||
            (a & 0x7FF) + w * h > 0x2240 / 32 || ((a >> 13) & 3) != 1) return 0;
    }
    for (unsigned y = 0; y < S3_TITLE_POSE_H; y += 8)
        for (unsigned x = 0; x < S3_TITLE_POSE_W; x += 8) {
            unsigned a = map[y / 8 * 40 + 20 + x / 8], tile = a & 0x7FF;
            if (tile >= sizeof pose / 32) return 0;
            if (((a >> 13) & 3) != 1) {
                /* Knuckles uses palette 1, but the map reuses palette 0's
                 * solid-white tile inside his gloves. White is color 11 in
                 * both palettes. Retain these shared tiles while excluding
                 * Sonic's adjacent art/placeholders from the portrait. */
                if ((a >> 13) & 3) continue;
                unsigned i = 0;
                while (i < 32 && pose[tile * 32 + i] == 0xBB) ++i;
                if (i != 32) continue;
            }
            for (unsigned dy = 0; dy < 8; ++dy)
                for (unsigned dx = 0; dx < 8; ++dx)
                    out->pose[(y + dy) * S3_TITLE_POSE_W + x + dx] =
                        (uint8_t)pixel(pose, sizeof pose, a, dx, dy);
        }
    for (unsigned i = 14; i < 30; ++i) {
        const uint8_t *p = rom + m + 2 + i * 6;
        unsigned w = ((p[1] >> 2) & 3) + 1, h = (p[1] & 3) + 1, a = be16(p + 2);
        int x = (int16_t)be16(p + 4) + 124, y = (int8_t)p[0];
        for (unsigned dy = 0; dy < h * 8; ++dy)
            for (unsigned dx = 0; dx < w * 8; ++dx) {
                unsigned ix = a & 0x800 ? w * 8 - 1 - dx : dx;
                unsigned iy = a & 0x1000 ? h * 8 - 1 - dy : dy;
                unsigned tile = (a & 0x7FF) + ix / 8 * h + iy / 8;
                unsigned color = pixel(word, sizeof word, tile, ix & 7, iy & 7);
                if (color) out->word[(y + dy) * S3_TITLE_WORD_W + x + dx] = (uint8_t)color;
            }
    }
    for (unsigned i = 0; i < 16; ++i) out->colors[i] = (uint16_t)be16(rom + ARMY_PAL_TITLE_KNUCKLES + i * 2);
    return 1;
}
