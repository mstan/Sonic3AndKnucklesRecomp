#pragma once
#include "game_video.h"
extern const GameVideo sonic3_video;
int s3_video_hook(uint32_t pc);
void s3_video_command(int id, const char *json);
void s3_video_vblank(void);
unsigned s3_video_main_cpu_divisor(void);
/* Full custom canvas, or 320 while the opt-in renderer is disabled. */
int s3_video_canvas_width(void);
/* Additive host actors in the custom-width renderer, after every scene sprite.
 * priority bit 0 = high-priority plane pixel, bit 1 = sprite already drawn;
 * `origin` is the output column of camera_x. */
struct GVDP;
typedef void (*S3VideoActorOverlay)(const struct GVDP *v, int line, uint32_t *out, int width,
                                    int origin, const uint32_t *palette, uint8_t *priority,
                                    int camera_x, int camera_y);
void s3_video_set_actor_overlay(S3VideoActorOverlay draw);

/* Game-owned expansion-stage ring layouts and Data Select previews. */
typedef unsigned (*S3VideoRingLayout)(void);
void s3_video_set_ring_layout(S3VideoRingLayout fn);
/* A game-owned HUD may widen its name while retaining native positioning.
 * Return zero to use the cartridge's ordinary mapping table. */
typedef unsigned (*S3VideoHudMapping)(void);
void s3_video_set_hud_mapping(S3VideoHudMapping fn);
typedef void (*S3VideoFrameOverlay)(int line, uint32_t *out, int width);
void s3_video_set_frame_overlay(S3VideoFrameOverlay draw);

/* Optional game-owned terrain source. Return 1 after providing a 16px cell,
 * or 0 for the ordinary RAM layout. `ram` is the complete 64 KiB published
 * work-RAM snapshot, including the stage and restart words. */
typedef int (*S3VideoWorldCell)(const uint8_t *ram, int x, int y, int background,
                              uint16_t *cell);
void s3_video_set_world_cell(S3VideoWorldCell read);
