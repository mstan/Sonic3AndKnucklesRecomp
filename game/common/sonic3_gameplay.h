#pragma once
#include <stdint.h>
struct RecompLauncherCModProvider;
typedef struct {
    int infinite_lives,infinite_time;
    int easy_level_select,debug_mode;
    int checkpoint_bonus;
    int shield_transform;
} S3GameplayConfig;
extern S3GameplayConfig s3_gameplay;
void s3_gameplay_load(const char *settings_path,const char *mode);
int s3_gameplay_save(void);
int s3_gameplay_netplay_allowed(void);
void s3_gameplay_apply(uint8_t ram[65536]);
/* Audited native Sonic branch: zero leaves every native instruction intact.
 * A nonzero target is a tail jump into the original eligibility/transform
 * routine; the caller must exclude alternate character implementations. */
int s3_gameplay_owns(uint32_t pc);
uint32_t s3_gameplay_transform_target(uint32_t pc,const uint8_t ram[65536],
                                     uint32_t actor,int native_sonic);
const struct RecompLauncherCModProvider *s3_gameplay_mods(
    const struct RecompLauncherCModProvider *base);
