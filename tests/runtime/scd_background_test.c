#include "scd_background.h"
#include <stdio.h>
#include <stdlib.h>

#include "scd_background_vectors.inc"

static uint32_t hash_word(uint32_t h, int16_t v)
{
    h = (h ^ ((uint16_t)v & 255u)) * 16777619u;
    return (h ^ ((uint16_t)v >> 8)) * 16777619u;
}

int main(void)
{
    ScdBg *s = scd_bg_create();
    if (!s) return 1;
    for (unsigned replay = 0; replay < 2; replay++) {
        for (unsigned n = 0; n < sizeof vectors / sizeof *vectors; n++) {
            ScdBgInput in = {0}; ScdBgOutput out;
            in.camera_x = vectors[n].x; in.camera_y = vectors[n].y;
            in.player_x = vectors[n].px; in.player_y = vectors[n].py;
            if (!scd_bg_reset(s, vectors[n].stage, &in)) return 2;
            for (unsigned frame = 0; frame < 64; frame++)
                if (!scd_bg_step(s, &in, &out)) return 3;
            uint32_t h = hash_word(2166136261u, out.y);
            for (unsigned line = 0; line < 224; line++) h = hash_word(h, out.hscroll[line]);
            if (h != vectors[n].hash) {
                fprintf(stderr, "%04X original scroll: %08X != %08X\n",
                        vectors[n].stage, h, vectors[n].hash);
                return 4;
            }
        }
    }
    scd_bg_destroy(s);
    puts("70 original CD scroll vectors passed, including context reuse/reset");
    return 0;
}
