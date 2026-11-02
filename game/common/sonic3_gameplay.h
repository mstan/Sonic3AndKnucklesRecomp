#pragma once
#include <stdint.h>
struct RecompLauncherCModProvider;
typedef struct {
    int infinite_lives,infinite_time;
    int easy_level_select,debug_mode;
    int checkpoint_bonus;
} S3GameplayConfig;
extern S3GameplayConfig s3_gameplay;
void s3_gameplay_load(const char *settings_path,const char *mode);
int s3_gameplay_save(void);
int s3_gameplay_netplay_allowed(void);
void s3_gameplay_apply(uint8_t ram[65536]);
const struct RecompLauncherCModProvider *s3_gameplay_mods(
    const struct RecompLauncherCModProvider *base);
