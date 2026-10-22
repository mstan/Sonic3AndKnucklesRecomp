#pragma once
#include <stdint.h>

/* Background math only. Native S3K still owns its camera and foreground.
 * These are values, not an original player/RAM ABI or a serialized state. */
typedef struct ScdBg ScdBg;
typedef struct ScdBgInput {
    int16_t camera_x,camera_y,delta_x,delta_y;
    int16_t player_x,player_y,player_speed,water_y;
    uint8_t player_control,scene,locked;
} ScdBgInput;
typedef struct ScdBgOutput {
    int16_t y,hscroll[224];
} ScdBgOutput;
ScdBg *scd_bg_create(void);
void scd_bg_destroy(ScdBg *);
int scd_bg_reset(ScdBg *,uint16_t stage,const ScdBgInput *);
int scd_bg_step(ScdBg *,const ScdBgInput *,ScdBgOutput *);
/* Presentation offsets/resizes can exceed the native signed 8.8 delta.
 * Preserve the profile's accumulators and animation phase across that move. */
int scd_bg_step_wide(ScdBg *,const ScdBgInput *,ScdBgOutput *,int32_t delta_x);
