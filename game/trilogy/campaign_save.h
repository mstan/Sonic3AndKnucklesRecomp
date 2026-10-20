#pragma once
#include <stdint.h>

/* Stable IDs never depend on installed packs: S1 0..6, S2 7..17,
 * S3K 18..31, Megamix 32..38, complete 39. Only the menu is dense. */
int campaign_count(unsigned mask);
int campaign_stable(unsigned mask,int display);
int campaign_display(unsigned mask,int stable);
typedef struct CampaignSave {
    uint8_t slots[8][64],view[80];
    unsigned mask;
    int pending,loaded,read_only,version;
    char path[1200];
} CampaignSave;
int campaign_load(CampaignSave *s,const char *path,const uint8_t native[80],int valid,unsigned mask);
void campaign_view(CampaignSave *s,uint8_t out[80]);
void campaign_capture(CampaignSave *s,const uint8_t view[80],uint8_t native[80]);
int campaign_store(CampaignSave *s);
int campaign_confirm(CampaignSave *s);
void campaign_import(CampaignSave *s,const uint8_t legacy[80],int megamix);
int campaign_native_block(const uint8_t *raw,unsigned size,uint8_t out[80]);
