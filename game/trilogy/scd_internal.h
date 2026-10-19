/* scd_internal.h — shared between the Sonic CD modules (not the S3K layer). */
#pragma once
#include <stdint.h>

typedef struct {
    int number, audio;
    char path[1024];          /* the BIN holding this track */
    long start, size;         /* bytes from INDEX 01, raw 2352-byte sectors */
} ScdTrack;
const ScdTrack *scd_track_info(int number);
