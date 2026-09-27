#pragma once
/* "Knuckles & Knuckles": an opt-in Sonic & Knuckles / Sonic 3 & Knuckles
 * mode. When Knuckles plays alone, a crowd of additional native Knuckles
 * objects follows him. Each extra has a logical player seat (the first extra
 * is player 2); a connected controller on that seat drives it, otherwise the
 * ported Tails CPU follow logic does. Stock behaviour is untouched while off. */
#include <stddef.h>
#include <stdint.h>

struct GVDP;

enum { S3_ARMY_MAX = 34 };            /* 35 Knuckles including player 1 */
extern const unsigned s3_army_sizes[4];

typedef struct S3ArmyConfig {
    int enabled;
    unsigned size;                    /* extra Knuckles, one of s3_army_sizes */
} S3ArmyConfig;
extern S3ArmyConfig s3_army;

void s3_army_defaults(void);
int  s3_army_valid_size(unsigned size);

/* Runtime (game fiber). */
void s3_army_init(void);                  /* decode art; register host sprites */
int  s3_army_owns(uint32_t pc);           /* one of this mod's hook sites */
int  s3_sk_instruction_hook(uint32_t pc); /* this mod composed with sonic3_video */
unsigned s3_sk_main_cpu_divisor(void);    /* crowd headroom composed with sonic3_video */
int  s3_army_hook(uint32_t pc);           /* instruction hook sites */
void s3_army_vblank(void);                /* latch the published crowd */
int  s3_army_active(void);                /* crowd exists this frame */

/* Logical player seat (0 = player 1) that drives extra k. */
unsigned s3_army_seat(unsigned k);

/* Custom-width renderer support: draw the latched crowd into a composed row.
 * priority bit 1 marks sprite-owned pixels, bit 0 high-priority planes;
 * camera_x/y is the camera of the frame being composed. */
void s3_army_draw_wide(const struct GVDP *v, int line, uint32_t *out, int width,
                       int origin, const uint32_t *palette, uint8_t *priority,
                       int camera_x, int camera_y);

/* Debug command: {"cmd":"knuckles_army"} */
void s3_army_command(int id, const char *json);
