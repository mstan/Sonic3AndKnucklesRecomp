/* Stage-pack integration. Original stage implementation by sonsegajp.
 * The optional packs use the host Mods page and stock pause/character rules. */
#include "trilogy.h"
#include "sonic3_video.h"
extern void tri_thumb_overlay(int line, uint32_t *out, int width);
void tri_ach_act_clear(uint16_t za) { (void)za; }
void tri_ach_wall_broken(void) {}
int tri_option_special_stages(void) { return 1; } /* Blue Spheres */
int tri_menu_hook(uint32_t pc)
{
    (void)pc;
    s3_video_set_frame_overlay(tri_thumb_overlay);
    return 0;
}
