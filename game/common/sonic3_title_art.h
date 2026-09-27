#pragma once
/* Runtime-only S&K title assets. No decoded art is stored in the repository. */
#include <stddef.h>
#include <stdint.h>
enum { S3_TITLE_POSE_W = 160, S3_TITLE_POSE_H = 224,
       S3_TITLE_WORD_W = 248, S3_TITLE_WORD_H = 40 };
typedef struct S3TitleArt {
    uint8_t pose[S3_TITLE_POSE_W * S3_TITLE_POSE_H];
    uint8_t word[S3_TITLE_WORD_W * S3_TITLE_WORD_H];
    uint16_t colors[16];
} S3TitleArt;
/* Returns 0 for truncated/corrupt art or mappings; leaves out zeroed. */
int s3_title_art_decode(const uint8_t *rom, size_t size, S3TitleArt *out);
