#pragma once
#include <stddef.h>
#include <stdint.h>

enum { S3_THEME_LIFE, S3_THEME_OVER, S3_THEME_CONTINUE, S3_THEME_CLEAR,
       S3_THEME_INVINCIBLE, S3_THEME_COUNT };
enum { S3_DONOR_S1, S3_DONOR_S2, S3_DONOR_AUG, S3_DONOR_WAI,
       S3_DONOR_PROTO, S3_DONOR_S3, S3_DONOR_SK, S3_ROM_DONOR_COUNT,
       S3_DONOR_CD_US=S3_ROM_DONOR_COUNT, S3_DONOR_COUNT };
typedef struct S3ThemeChoice { const char *id, *label; int donor; } S3ThemeChoice;
typedef struct S3ThemeSelection { int enabled, choice; } S3ThemeSelection;
extern S3ThemeSelection s3_themes[S3_THEME_COUNT];
extern const char *const s3_theme_ids[S3_THEME_COUNT];
extern const char *const s3_theme_names[S3_THEME_COUNT];
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
