#pragma once
#include "sonic3_sound_assets.h"
struct S3StateIO;
int s3_cd_donor_verify(const char *manifest,unsigned donor);
void s3_cd_themes_reset(void);
int s3_cd_theme_build(unsigned theme,unsigned donor,const char *manifest,
                      const S3SoundLayout *layout,uint8_t *bank,size_t capacity,
                      uint32_t *used,char *error,size_t error_size);
void s3_cd_theme_request(unsigned native);
void s3_cd_theme_frame(int paused,int oneup);
void s3_cd_theme_state(struct S3StateIO *io);
void s3_cd_theme_identity(uint8_t out[32]);
void s3_cd_theme_debug(unsigned *active,unsigned *theme,uint64_t *position);
