#pragma once
#include "scd_resource.h"
enum { SCD_WINDOW=0x480000,SCD_WINDOW_SIZE=0x180000,SCD_SOUND_BASE=0x600000 };
typedef struct ScdPack {
    ScdResource resource;
    const uint8_t *levels,*scenes,*sound;
    uint32_t levels_size,scenes_size,sound_size;
} ScdPack;
int scd_pack_open(ScdPack *,const char *,char *,unsigned);
int scd_pack_validate(const char *,char *,unsigned);
const uint8_t *scd_pack_level(const ScdPack *,uint16_t,uint32_t *);
const uint8_t *scd_pack_scene(const ScdPack *,unsigned,uint32_t *);
