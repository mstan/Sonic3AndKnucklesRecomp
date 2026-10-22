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
    /* MMZ3 Bad's first 224 lines move at half the horizontal camera rate.
     * Initial expansion and resize must not wrap an 8.8 signed delta, and
     * returning to the original camera must restore its exact phase. */
    const int offsets[]={39,101,238,736};
    for(unsigned n=0;n<sizeof offsets/sizeof offsets[0];n++) {
        ScdBgInput in={0};ScdBgOutput out;int offset=offsets[n];
        if(!scd_bg_reset(s,0x5B02,&in))return 5;
        in.camera_x=(int16_t)offset;
        if(!scd_bg_step_wide(s,&in,&out,offset*256))return 6;
        for(unsigned line=0;line<224;line++)if(out.hscroll[line]!=-(112+offset/2))return 7;
        in.camera_x=0;
        if(!scd_bg_step_wide(s,&in,&out,-offset*256))return 8;
        for(unsigned line=0;line<224;line++)if(out.hscroll[line]!=-112)return 9;
    }
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
