#pragma once
#include <stdint.h>
typedef struct ScdGeometry {
    const uint8_t *blob;
    uint32_t size,background;
    unsigned background_width,background_height;
} ScdGeometry;
/* Return zero for a non-paged stage. Out-of-grid cells in a paged stage are
 * handled as empty. Definitions are guest addresses into the same blob. */
int scd_geometry_chunk(const ScdGeometry *,int plane,int column,int row,uint32_t *definitions,unsigned *id);
int scd_geometry_cell(const ScdGeometry *,int x,int y,int background,unsigned row_mask,uint16_t *cell);
