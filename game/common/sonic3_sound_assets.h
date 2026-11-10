#ifndef SONIC3_SOUND_ASSETS_H
#define SONIC3_SOUND_ASSETS_H
#include <stddef.h>
#include <stdint.h>

/* Verified retail sound data, reconstructed exactly as the game's own loader
 * installs it. No music, instruments, envelopes or samples are stored in C. */
typedef struct S3SoundSource {
    uint8_t z80[0x2000];
    uint32_t rom_size;
    uint16_t music_banks, music_pointers, sfx_pointers;
    uint16_t voice_bank, modulation_pointers, volume_pointers;
    uint16_t music_count, volume_count;
    uint8_t standalone_sonic3;
} S3SoundSource;

int s3_sound_read_source(const uint8_t *rom, size_t rom_size,
                         S3SoundSource *out, char *error, size_t error_size);
/* Zero means unavailable in this cartridge (notably locked-on S3 songs when
 * running the standalone S&K ROM). IDs retain their original queue domains. */
uint32_t s3_sound_music_source(const S3SoundSource *source,
                              const uint8_t *rom, unsigned id);
uint32_t s3_sound_sfx_source(const S3SoundSource *source,
                            const uint8_t *rom, unsigned id);

typedef struct S3SoundLayout {
    uint32_t program_size;
    uint32_t music_index, sound_index, psg_index, modulation_index;
} S3SoundLayout;
typedef struct S3SoundAssetStats {
    uint32_t music, sfx, nodes, bytes;
} S3SoundAssetStats;
typedef struct S3SoundPCMLayout {
    uint32_t sample_table, map_s3, map_sk, sega_id, sample_count;
} S3SoundPCMLayout;
/* Preserve the cartridge's DAC IDs. Descriptor rates come from the driver
 * port; every waveform is decoded from the verified user's cartridge. */
int s3_sound_build_pcm(const uint8_t *rom,size_t rom_size,
                       const S3SoundPCMLayout *layout,uint8_t *bank,size_t capacity,
                       uint32_t *used,char *error,size_t error_size);
/* The bank already contains the compiled software-only program at $400000.
 * This fills its empty pointer tables and appends ROM-derived sequences,
 * instruments and envelopes. PCM sample preparation is a separate step. */
int s3_sound_build_sequences(const uint8_t *rom,size_t rom_size,
                             const S3SoundLayout *layout,uint8_t *bank,size_t capacity,
                             S3SoundAssetStats *stats,char *error,size_t error_size);
#endif
