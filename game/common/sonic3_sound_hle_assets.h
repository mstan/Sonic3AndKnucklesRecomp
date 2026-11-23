#ifndef SONIC3_SOUND_HLE_ASSETS_H
#define SONIC3_SOUND_HLE_ASSETS_H
#include <stddef.h>
#include <stdint.h>
/* Decoded host data only. Neither this allocation nor its offsets are
 * installed in the cartridge or exposed as an emulated ROM overlay. */
typedef struct S3HleSoundAssets {
    uint8_t *data;
    uint32_t bytes,music[51],effects[169],psg_envelopes[256],modulation[32];
    struct {uint32_t start,bytes,rate_numerator,rate_denominator;} samples[126];
} S3HleSoundAssets;
int s3_hle_sound_assets(const uint8_t *image,size_t bytes,S3HleSoundAssets *out,char *error,size_t error_bytes);
/* Refresh host offsets after runtime-selected donor data is decoded. */
void s3_hle_sound_assets_index(S3HleSoundAssets *assets);
void s3_hle_sound_assets_free(S3HleSoundAssets *assets);
#endif
