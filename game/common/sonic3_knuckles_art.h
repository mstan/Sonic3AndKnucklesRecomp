#pragma once
/* Host copy of Knuckles' gameplay frames, decoded from the owner ROM's
 * uncompressed art, DPLC and sprite mappings. Pixels keep VDP semantics:
 * 0 is transparent, otherwise bits 0-5 are palette line*16+colour relative to
 * art_tile's line and bit 7 is the piece's own priority bit. */
#include <stddef.h>
#include <stdint.h>

enum { KNUX_ART_FRAMES = 256 };

typedef struct KnuxFrame {
    int16_t x, y;             /* top-left relative to the object's position */
    uint16_t width, height;
    uint32_t offset;          /* first pixel in KnuxArt.pixels */
} KnuxFrame;

typedef struct KnuxArt {
    unsigned count;
    KnuxFrame frames[KNUX_ART_FRAMES];
    uint8_t *pixels;
    size_t size;
} KnuxArt;

typedef struct KnuxArtLayout {
    uint32_t art, mappings, dplc; /* ArtUnc_Knux, Map_Knuckles, PLC_Knuckles */
    unsigned frames;
} KnuxArtLayout;

/* Stock Sonic & Knuckles addresses (combined cart and S&K alone agree). */
extern const KnuxArtLayout knux_art_stock;

/* Returns 1 on success. Rejects references outside rom[0..size). */
int  knux_art_decode(const uint8_t *rom, size_t size, const KnuxArtLayout *layout,
                     KnuxArt *out, char *error, size_t error_size);
void knux_art_free(KnuxArt *art);
/* Pixel of a decoded frame at object-relative (dx,dy) with render flips. */
uint8_t knux_art_pixel(const KnuxArt *art, unsigned frame, int dx, int dy, unsigned flip);
