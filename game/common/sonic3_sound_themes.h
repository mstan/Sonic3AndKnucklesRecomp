#pragma once
#include <stddef.h>
#include <stdint.h>

enum {
#define TRACK(symbol, ...) symbol,
#include "sonic3_music_catalog.inc"
#undef TRACK
    S3_MUSIC_COUNT,
    S3_THEME_COUNT = 5 /* Existing cartridge/CD event interfaces. */
};
enum { S3_MUSIC_SHARED, S3_MUSIC_SONIC3, S3_MUSIC_SANDK, S3_MUSIC_GROUPS };
enum { S3_DONOR_S1, S3_DONOR_S2, S3_DONOR_AUG, S3_DONOR_WAI,
       S3_DONOR_PROTO, S3_DONOR_S3, S3_DONOR_SK, S3_ROM_DONOR_COUNT,
       S3_DONOR_CD_US=S3_ROM_DONOR_COUNT, S3_DONOR_COUNT };
typedef struct S3ThemeChoice { const char *id, *label; int donor; } S3ThemeChoice;
typedef struct S3ThemeSelection { int enabled, choice; } S3ThemeSelection;
extern S3ThemeSelection s3_themes[S3_MUSIC_COUNT];
extern const char *const s3_theme_ids[S3_MUSIC_COUNT];
extern const char *const s3_theme_names[S3_MUSIC_COUNT];
extern const char *const s3_donor_ids[S3_DONOR_COUNT];
extern const char *const s3_donor_names[S3_DONOR_COUNT];
void s3_themes_load(const char *settings, const char *mode);
int s3_themes_save(void);
int s3_themes_validate(void);
int s3_theme_choices(unsigned theme);
const S3ThemeChoice *s3_theme_choice(unsigned theme, unsigned choice);
int s3_theme_choose(unsigned theme, const char *choice);
int s3_theme_enable(unsigned theme, int enabled);
int s3_themes_enabled(void);
int s3_theme_ready(unsigned theme);
int s3_theme_donor(unsigned theme);
int s3_donor_verified(unsigned donor);
int s3_theme_import(unsigned theme, const char *path,
                    void (*progress)(void *,float,const char *), void *user);
const char *s3_donor_path(unsigned donor);
const char *s3_themes_error(void);
void s3_themes_clear_error(void);
uint8_t *s3_donor_read(unsigned donor, size_t *size);
void s3_theme_offer_prototype(const char *path);
unsigned s3_music_group(unsigned track);
const char *s3_music_section(unsigned track);
unsigned s3_music_target(unsigned track);
unsigned s3_music_source(unsigned track, unsigned donor);
int s3_music_donor(unsigned track);
int s3_music_available(unsigned track);
int s3_music_active(unsigned track);
int s3_music_group_enabled(unsigned group);
int s3_music_group_enable(unsigned group, int enabled);
int s3_music_pick(unsigned track, const char *choice);
