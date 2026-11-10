#pragma once
#include <stddef.h>
#include <stdint.h>
struct S3StateIO;
struct RecompLauncherCModProvider;
void s3_sound_load(const char *settings,const char *mode);
int s3_sound_select(int on);
int s3_sound_selected(void);
int s3_sound_active(void);
int s3_sound_available(void);
const char *s3_sound_error(void);
int s3_sound_commit(void);
int s3_sound_netplay_allowed(void);
void s3_sound_post_reset(void);
void s3_sound_vblank(void);
int s3_sound_owns(uint32_t pc);
int s3_sound_hook(uint32_t pc);
int s3_sound_dispatch_miss(uint32_t pc);
void s3_sound_state(struct S3StateIO *io);
size_t s3_sound_rb_save(void *dst,size_t cap);
int s3_sound_rb_load(const void *src,size_t len);
void s3_sound_command(int id,const char *json);
const struct RecompLauncherCModProvider *s3_sound_mods(const struct RecompLauncherCModProvider *base);
