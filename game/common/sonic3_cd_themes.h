#pragma once
#include "sonic3_sound_assets.h"
struct S3StateIO;
int s3_cd_donor_verify(const char *manifest,unsigned donor);
uint64_t s3_cd_donor_available(const char *manifest,unsigned donor);
void s3_cd_themes_reset(void);
int s3_recording_verify(const char *path,const char *hash);
int s3_vgm_theme_build(unsigned theme,unsigned native,const char *path,const char *hash,
                      const S3SoundLayout *layout,uint8_t *bank,size_t capacity,uint32_t *used,char *error,size_t error_size);
int s3_recording_build(unsigned theme,unsigned native,const char *path,const char *hash,int loop,
                      const S3SoundLayout *layout,uint8_t *bank,size_t capacity,uint32_t *used,char *error,size_t error_size);
int s3_cd_theme_build(unsigned theme,unsigned donor,const char *manifest,
                      const S3SoundLayout *layout,uint8_t *bank,size_t capacity,
                      uint32_t *used,char *error,size_t error_size);
/* Zero postpones the cartridge music request while the clear recording ends. */
int s3_cd_theme_request(unsigned native);
void s3_cd_theme_driver_init(void);
/* Only a real gameplay clear may continue through stage music/driver resets. */
void s3_cd_theme_finish_clear(void);
unsigned s3_cd_theme_resume_music(void);
void s3_cd_theme_frame(int paused,int oneup);
void s3_cd_theme_state(struct S3StateIO *io);
void s3_cd_theme_identity(uint8_t out[32]);
void s3_cd_theme_debug(unsigned *active,unsigned *theme,uint64_t *position);
