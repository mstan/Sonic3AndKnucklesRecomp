#pragma once
/* Knuckles & Knuckles title gag; see sonic3_knuckles_title.c. */
#include <stdint.h>
#include "video/genesis_vdp.h"

int  s3_title_active(void);
/* Instruction hook share: returns 1 to replace the routine at pc. */
int  s3_title_hook(uint32_t pc, int enabled);
/* Latch this frame's title composition (at V-int, before the SAT upload). */
void s3_title_vblank(const GVDP *v, int enabled);
/* Host sprite layer: draw the latched composition. */
void s3_title_draw(const GVDP *v, const GVDPSpriteLayer *layer, const void *knuckles_art);
