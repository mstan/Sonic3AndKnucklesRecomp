/* Adapt the cartridge's save IO at its boundary. The original checksum,
 * primary/backup SRAM writer and base-game records remain authoritative. */
#include "trilogy_obj.h"
#include "stage_pack.h"
#include "campaign_save.h"
#include "megamix_geometry_sites.h"
#include <stdio.h>
#include <string.h>
#define SAVED 0xFFE6ACu
static CampaignSave journal;
static char save_path[1200];
static uint8_t loaded_native[80],saved[84];
static int ready,projected,loaded_valid;
unsigned tri_campaign_mask(void)
{ return (s3_stage_pack_sonic1?1:0)|(s3_stage_pack_sonic2?2:0)|(s3_stage_pack_megamix?4:0); }
void tri_save_sram_loaded(const char *path,const uint8_t *data,size_t size,int loaded)
{
    ready=projected=0;snprintf(save_path,sizeof save_path,"%s",path);
    loaded_valid=loaded && campaign_native_block(data,(unsigned)size,loaded_native);
    if(!s3_stage_pack_enabled || !loaded)return;
    char backup[1240];snprintf(backup,sizeof backup,"%s.pre-campaign.bak",path);
    FILE *f=fopen(backup,"rb");if(f){fclose(f);return;}
    f=fopen(backup,"wb");if(f){fwrite(data,1,size,f);fclose(f);}
}
void tri_save_sram_flushed(const char *path,const uint8_t *data,size_t size)
{
    (void)path;uint8_t native[80];
    if(!ready || !campaign_native_block(data,(unsigned)size,native))return;
    for(int i=0;i<8;i++)if(memcmp(native+i*10,journal.slots[i]+20,10))return;
    if(!campaign_confirm(&journal))fprintf(stderr,"[campaign-save] confirmation write failed; pending recovery retained\n");
}
static void import_legacy(void)
{
    char path[1240];snprintf(path,sizeof path,"%s",save_path);
    char *a=strrchr(path,'/'),*b=strrchr(path,'\\');char *name=a&&(!b||a>b)?a+1:b?b+1:path;
    const char *files[]={"sonic3k-stage-pack.srm","sonic3k-megamix.srm"};
    for(int i=0;i<2;i++) {
        snprintf(name,sizeof path-(size_t)(name-path),"%s",files[i]);
        FILE *f=fopen(path,"rb");if(!f)continue;
        uint8_t raw[16384],slots[80];int ok=fread(raw,1,sizeof raw,f)==sizeof raw && fgetc(f)==EOF;fclose(f);
        if(ok && campaign_native_block(raw,sizeof raw,slots)) {
            campaign_import(&journal,slots,i==1);
            fprintf(stderr,"[campaign-save] imported legacy %s (original retained)\n",files[i]);
        }
    }
}
int tri_save_journal_hook(uint32_t pc)
{
    if(pc==MG_PC_SAVE_LOADED && !ready) {
        if(!save_path[0])return 0;
        uint8_t native[80];for(unsigned i=0;i<80;i++)native[i]=r8(SAVED+i);
        char path[1240];snprintf(path,sizeof path,"%s.campaign",save_path);
        FILE *previous=fopen(path,"rb");int had_journal=previous!=NULL;if(previous)fclose(previous);
        if(!had_journal) {char backup[1280];snprintf(backup,sizeof backup,"%s.bak",path);previous=fopen(backup,"rb");had_journal=previous!=NULL;if(previous)fclose(previous);}
        int existed=campaign_load(&journal,path,native,loaded_valid,tri_campaign_mask());
        if(!existed && !had_journal)import_legacy();
        uint8_t view[80];campaign_view(&journal,view);
        for(unsigned i=0;i<80;i++)w8(SAVED+i,view[i]);ready=1;
        tri_jsr(S3K_Write_SaveGame);
        fprintf(stderr,"[campaign-save] ready: %d zones, mask %u, %s\n",campaign_count(journal.mask),journal.mask,existed?"journal restored":"initialized");
    }
    if(pc==MG_PC_SAVE_WRITE && ready && !projected) {
        for(unsigned i=0;i<84;i++)saved[i]=r8(SAVED+i);
        uint8_t native[80];campaign_capture(&journal,saved,native);
        if(!campaign_store(&journal)) {
            fprintf(stderr,"[campaign-save] save failed; native write deferred to protect addon progress\n");
            return 1;
        }
        for(unsigned i=0;i<80;i++)w8(SAVED+i,native[i]);projected=1;
    }
    if(pc==MG_PC_SAVE_RETURN && projected) {
        for(unsigned i=0;i<84;i++)w8(SAVED+i,saved[i]);projected=0;
    }
    return 0;
}
