#pragma once
#include <stdint.h>

/* Stable IDs never depend on installed packs: S1 0..6, S2 7..17,
 * S3K 18..31, Megamix 32..38, complete 39, CD act/period variants 40..109.
 * Catalog order is S1 -> S2 -> CD -> S3K -> Megamix. Mask bits 0/1/2/3
 * enable S1/S2/Megamix/CD. Only the menu is dense. */
int campaign_count(unsigned mask);
int campaign_stable(unsigned mask,int display);
int campaign_display(unsigned mask,int stable);
/* Selected replay caption, including its game and local stage number. */
void campaign_stage_label(unsigned mask,int display,char out[8]);
typedef struct CampaignSave {
    uint8_t slots[8][64],view[80];
    unsigned mask,capabilities;
    int pending,loaded,version,read_only;
    char path[1200];
} CampaignSave;
int campaign_load(CampaignSave *s,const char *path,const uint8_t native[80],int valid,unsigned mask);
void campaign_view(CampaignSave *s,uint8_t out[80]);
void campaign_capture(CampaignSave *s,const uint8_t view[80],uint8_t native[80]);
int campaign_store(CampaignSave *s);
int campaign_confirm(CampaignSave *s);
void campaign_import(CampaignSave *s,const uint8_t legacy[80],int megamix);
int campaign_native_block(const uint8_t *raw,unsigned size,uint8_t out[80]);
/* CD collectibles are independent of native Chaos/Super Emeralds. Bits 0..13
 * in generators/projectors are round*2+act; act 3 has no such objects.
 * Stones are seven unique UFO rewards. The stable cursor names an exact
 * act/period variant; act is retained as observed runtime state. */
typedef struct CampaignCdState {
    uint16_t generators,projectors;
    uint8_t act,stones;
} CampaignCdState;
int campaign_cd_get(const CampaignSave *s,unsigned slot,CampaignCdState *out);
int campaign_cd_set(CampaignSave *s,unsigned slot,const CampaignCdState *state);
