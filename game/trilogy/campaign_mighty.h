#pragma once
#include <stdint.h>
/* Copy only the bounded MDY3 resource graph into another expansion bank.
 * Relative map/DPLC offsets stay relative. No scanning of opaque art. */
uint32_t campaign_mighty_copy(uint8_t **dst,uint32_t *dst_size,const uint8_t *source,uint32_t source_size);
uint32_t campaign_shadow_copy(uint8_t **dst,uint32_t *dst_size,const uint8_t *source,uint32_t source_size);
