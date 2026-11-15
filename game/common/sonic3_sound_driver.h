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
int s3_sound_prototype_select(int on);
int s3_sound_prototype_selected(void);
int s3_sound_prototype_active(void);
int s3_sound_prototype_set_path(const char *path);
const char *s3_sound_prototype_path(void);
/* Caller owns the verified 2 MiB donor buffer. NULL retains the error text. */
uint8_t *s3_sound_prototype_read(void);
int s3_sound_prototype_verified(void);
const char *s3_sound_error(void);
int s3_sound_commit(void);
int s3_sound_netplay_allowed(void);
void s3_sound_post_reset(void);
void s3_sound_vblank(void);
/* Call after native VBlank has latched its controller row. */
void s3_sound_player_frame(unsigned latched_buttons);
int s3_sound_owns(uint32_t pc);
int s3_sound_hook(uint32_t pc);
int s3_sound_dispatch_miss(uint32_t pc);
void s3_sound_state(struct S3StateIO *io);
size_t s3_sound_rb_save(void *dst,size_t cap);
int s3_sound_rb_load(const void *src,size_t len);
void s3_sound_command(int id,const char *json);
const struct RecompLauncherCModProvider *s3_sound_mods(const struct RecompLauncherCModProvider *base);
