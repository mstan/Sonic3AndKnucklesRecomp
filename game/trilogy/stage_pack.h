#pragma once
#include <stddef.h>
#include <stdint.h>
struct RecompLauncherCModProvider;
extern int s3_stage_pack_enabled;
extern int s3_stage_pack_megamix, s3_stage_pack_mighty;
extern int s3_stage_pack_sonic1, s3_stage_pack_sonic2;
extern int s3_pack_requested[3], s3_pack_available[3];
extern char s3_pack_paths[3][1024];
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
