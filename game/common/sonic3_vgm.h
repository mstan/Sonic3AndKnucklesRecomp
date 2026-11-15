#pragma once
#include <stddef.h>
#include <stdint.h>
struct S3StateIO;
int s3_vgm_build(unsigned theme,const char *path,const char *hash);
void s3_vgm_reset(void);
int s3_vgm_ready(unsigned theme);
unsigned s3_vgm_channels(unsigned theme);
void s3_vgm_start(unsigned theme);
void s3_vgm_frame(unsigned theme,int paused);
void s3_vgm_mix(int16_t *out,size_t frames,double rate,unsigned volume);
void s3_vgm_audition(unsigned mask);
int s3_vgm_finished(void);
void s3_vgm_state(struct S3StateIO *io);
