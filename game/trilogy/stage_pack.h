#pragma once
#include <stddef.h>
#include <stdint.h>
/* Optional S1 SYZ lossless source-line-0 patterns. Old flag-zero banks keep
 * their original layout and lossy remap. Bitmap bits are LSB-first by tile. */
#define S3_PACK_S1_PLANE_EXTPAL 1u
#define S3_PACK_S1_PLANE_MASK 0x210u
#define S3_PACK_S1_PLANE_MASK_BYTES 256u
#define S3_PACK_LEVEL_TILE_LIMIT 0x45Cu
struct RecompLauncherCModProvider;
extern int s3_stage_pack_enabled;
extern int s3_stage_pack_megamix, s3_stage_pack_mighty;
extern int s3_stage_pack_shadow;
extern int s3_pack_mighty_requested,s3_pack_shadow_requested;
extern char s3_megamix_rom_path[1024];
int s3_megamix_rom_validate(const char *path,char *error,unsigned capacity);
/* Authored Shadow life/name/X strip from the already required verified donor.
 * Cached at media validation, never loaded from disk during rendering. */
const uint8_t *s3_megamix_shadow_hud(void);
int s3_megamix_hud_decode(const uint8_t *src, size_t size, uint8_t out[384]);
extern int s3_stage_pack_sonic1, s3_stage_pack_sonic2;
extern int s3_stage_pack_cd;
extern int s3_cd_ufo_practice;
extern int s3_pack_requested[4], s3_pack_available[4];
extern char s3_pack_paths[4][1024],s3_cd_audio_path[1024];
extern char s3_stage_pack_path[1024];
void s3_stage_pack_load(const char *settings);
int s3_stage_pack_validate(const char *path, char *error, unsigned capacity);
int s3_pack_validate(const char *path, int content, char *error, unsigned capacity);
void s3_pack_refresh(void);
const struct RecompLauncherCModProvider *s3_stage_pack_mods(
    const struct RecompLauncherCModProvider *base);
const char *s3_stage_pack_state_reason(void);
const char *s3_stage_pack_sram(void);
void tri_save_sram_loaded(const char *,const uint8_t *,size_t,int);
void tri_save_sram_flushed(const char *,const uint8_t *,size_t);
unsigned tri_campaign_mask(void);
