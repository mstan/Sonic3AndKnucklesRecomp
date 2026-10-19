/* Portable reader for converted Megamix geometry. No donor code or assets. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *layout, *chunks;
    size_t layout_size, chunks_size;
    unsigned width[2], height[2], chunk_count, block_count;
} MegamixGeometry;

/* Validates the complete MGL1 layout, every chunk ID and every block index.
 * The caller owns both buffers and keeps them alive until the next open. */
int megamix_geometry_open(MegamixGeometry *g,
                         const uint8_t *layout, size_t layout_size,
                         const uint8_t *chunks, size_t chunks_size,
                         unsigned block_count);
/* Coordinates outside a plane return the guaranteed empty chunk zero.
 * Wrapping is explicit: foreground collision must not wrap a missing route. */
unsigned megamix_geometry_chunk(const MegamixGeometry *g, int plane,
                               int cx, int cy, int wrap);
uint16_t megamix_geometry_cell(const MegamixGeometry *g, int plane,
                              int x, int y, int wrap);
