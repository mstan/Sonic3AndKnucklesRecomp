#pragma once
#include <stddef.h>
#include <stdint.h>
size_t s3_state_size(void);
int s3_state_save(void *data, size_t size);
int s3_state_load(const void *data, size_t size, int apply);
int s3_state_at_boundary(void);
uint32_t s3_state_resume_pc(uint8_t mode);
struct S3StateIO;
void s3_video_state(struct S3StateIO *io);
void s3_army_state(struct S3StateIO *io);
int s3_army_state_ready(void);
