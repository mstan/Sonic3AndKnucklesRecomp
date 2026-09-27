#pragma once
/* Sonic 3 family game-owned mods, composed in front of the common video mods. */
struct RecompLauncherCModProvider;

/* Loads <settings dir>/<mode>-mods.ini beside settings.ini. */
void s3_mods_load(const char *settings_path, const char *mode);
int  s3_mods_save(void);
const struct RecompLauncherCModProvider *s3_mods(const struct RecompLauncherCModProvider *common);
/* Local-only: the crowd has no netplay seat contract yet. */
int  s3_mods_netplay_allowed(void);
