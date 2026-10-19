#pragma once
struct RecompLauncherCModProvider;
extern int s3_stage_pack_enabled;
extern char s3_stage_pack_path[1024];
void s3_stage_pack_load(const char *settings);
int s3_stage_pack_validate(const char *path, char *error, unsigned capacity);
const struct RecompLauncherCModProvider *s3_stage_pack_mods(
    const struct RecompLauncherCModProvider *base);
const char *s3_stage_pack_state_reason(void);
const char *s3_stage_pack_sram(void);
