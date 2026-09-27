#pragma once
#include "game_video.h"
extern const GameVideo sonic3_video;
int s3_video_hook(uint32_t pc);
void s3_video_command(int id, const char *json);
void s3_video_vblank(void);
unsigned s3_video_main_cpu_divisor(void);
/* Additive host actors in the custom-width renderer, after every scene sprite.
 * priority bit 0 = high-priority plane pixel, bit 1 = sprite already drawn;
 * `origin` is the output column of camera_x. */
struct GVDP;
typedef void (*S3VideoActorOverlay)(const struct GVDP *v, int line, uint32_t *out, int width,
                                    int origin, const uint32_t *palette, uint8_t *priority,
                                    int camera_x, int camera_y);
void s3_video_set_actor_overlay(S3VideoActorOverlay draw);
