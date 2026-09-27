/* Synthetic DPLC/mapping image: frame decoding, piece order and flips. */
#include "sonic3_knuckles_art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint8_t rom[0x1000];
static void w16(unsigned a, unsigned v) { rom[a] = (uint8_t)(v >> 8); rom[a + 1] = (uint8_t)v; }
int main(void)
{
    const KnuxArtLayout l = { 0x100, 0x800, 0xA00, 2 };
    /* Art tile 0: colour 1, tile 1: colour 2, tile 2: colour 3 (row 0 col 0 = 4). */
    memset(rom + 0x100, 0x11, 32); memset(rom + 0x120, 0x22, 32); memset(rom + 0x140, 0x33, 32);
    rom[0x140] = 0x43;
    /* Frame 0 empty; frame 1 loads source tiles 2 then 0..1 (two DPLC runs). */
    w16(0x800, 4); w16(0x802, 6); w16(0x804, 0);
    w16(0x806, 3);
    rom[0x808] = (uint8_t)-8; rom[0x809] = 0; w16(0x80A, 0x0000); w16(0x80C, (unsigned)-4);  /* 8x8 tile 0 */
    rom[0x80E] = (uint8_t)-8; rom[0x80F] = 1; w16(0x810, 0x2001); w16(0x812, (unsigned)-4); /* 8x16, pal 1, hidden under piece 0 top */
    rom[0x814] = 0;           rom[0x815] = 0; w16(0x816, 0x8800 | 1); w16(0x818, 4);        /* hflip, high */
    w16(0xA00, 4); w16(0xA02, 6); w16(0xA04, 0);
    w16(0xA06, 2); w16(0xA08, 0x0002); w16(0xA0A, 0x1000);
    KnuxArt art; char error[128];
    CHECK(knux_art_decode(rom, sizeof rom, &l, &art, error, sizeof error));
    CHECK(art.count == 2 && art.frames[0].width == 0);
    const KnuxFrame *f = &art.frames[1];
    CHECK(f->x == -4 && f->y == -8 && f->width == 16 && f->height == 16);
    CHECK(knux_art_pixel(&art, 1, -4, -8, 0) == 4);          /* piece 0 = DPLC tile 0 (source 2) */
    CHECK(knux_art_pixel(&art, 1, -3, -8, 0) == 3);
    CHECK(knux_art_pixel(&art, 1, -4, 0, 0) == (16 | 2));    /* piece 1 lower cell: source tile 1, palette 1 */
    CHECK(knux_art_pixel(&art, 1, 4, 0, 0) == (0x80 | 1));   /* priority piece, source tile 0 */
    CHECK(knux_art_pixel(&art, 1, 11, 0, 0) == (0x80 | 1));
    CHECK(knux_art_pixel(&art, 1, 5, -4, 0) == 0);           /* gap in the bounding box */
    /* Render hflip mirrors about the object's x: dx maps to -dx-1. */
    CHECK(knux_art_pixel(&art, 1, 3, -8, 1) == 4);
    CHECK(knux_art_pixel(&art, 1, 3, 7, 3) == 4);
    CHECK(knux_art_pixel(&art, 1, -4, 7, 2) == 4);
    /* A piece that references an unloaded tile is rejected, not read. */
    w16(0x816, 0x0009);
    CHECK(!knux_art_decode(rom, sizeof rom, &l, &art, error, sizeof error) && strstr(error, "unloaded"));
    w16(0x816, 0x8801);
    CHECK(knux_art_decode(rom, sizeof rom, &l, &art, error, sizeof error));
    knux_art_free(&art);
    puts("Knuckles frames decode with DPLC order, piece precedence and flips");
    return 0;
}
