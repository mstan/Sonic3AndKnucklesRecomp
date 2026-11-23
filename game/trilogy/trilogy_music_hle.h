#pragma once
#include <stddef.h>
#include <stdint.h>
struct S3HleSoundAssets;
struct S3SoundSource;
int tri_hle_sound_prepare(struct S3HleSoundAssets *,const struct S3SoundSource *,char *,size_t);
uint32_t tri_hle_music_root(unsigned content,unsigned game,unsigned song,unsigned *flags);
uint32_t tri_hle_effect_root(unsigned content,unsigned set,unsigned native);
int tri_hle_effects_available(unsigned content,unsigned set);
unsigned tri_hle_effect_id(unsigned content,unsigned set,unsigned id);
