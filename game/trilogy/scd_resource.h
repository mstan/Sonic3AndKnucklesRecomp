#pragma once
#include <stdint.h>
typedef struct ScdResourceItem {
    char name[16];
    uint32_t offset,size,crc;
} ScdResourceItem;
typedef struct ScdResource {
    char path[1024];
    uint8_t *data;
    uint32_t size,count;
    ScdResourceItem items[34];
} ScdResource;
/* Content is retained in bounded host memory. Audio is verified by streaming
 * and subsequently read only within its validated individual track spans. */
int scd_resource_open(ScdResource *out,const char *path,int audio,char *error,unsigned capacity);
void scd_resource_close(ScdResource *resource);
const ScdResourceItem *scd_resource_find(const ScdResource *,const char *name);
const uint8_t *scd_resource_item(const ScdResource *,const char *name,uint32_t *size);
