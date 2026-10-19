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
static CampaignCdState cd_state;
static int cd_slot=-1;
static int current_slot(void)
{
    uint32_t p=r32(0xFFE660u)&0xFFFFFFu;
    return p>=SAVED && p<SAVED+80 && (p-SAVED)%10==0?(int)((p-SAVED)/10):-1;
}
void tri_cd_save_select(void)
{
    extern void scd_time_import(uint16_t,uint16_t);
    cd_slot=current_slot();memset(&cd_state,0,sizeof cd_state);
    if(ready>0 && cd_slot>=0)campaign_cd_get(&journal,(unsigned)cd_slot,&cd_state);
    scd_time_import(cd_state.generators,cd_state.projectors);
}
uint8_t tri_cd_stones(void){return cd_state.stones;}
static void cd_capture(void)
{
    extern void scd_time_export(uint16_t *,uint16_t *);
    if(!s3_stage_pack_cd && journal.version<2)return;
    if(ready<=0 || cd_slot<0 || current_slot()!=cd_slot)return;
    scd_time_export(&cd_state.generators,&cd_state.projectors);
    if(tri_cd_zone(tri_zone()))cd_state.act=(uint8_t)tri_act();
    campaign_cd_set(&journal,(unsigned)cd_slot,&cd_state);
}
void tri_cd_save_flush(void)
{
    if(ready<=0 || cd_slot<0 || current_slot()!=cd_slot)return;
    cd_capture();
    if(tri_cd_zone(tri_zone()) && !(r8(SAVED+(unsigned)cd_slot*10)&3)) {
        extern int tri_za_to_code(uint16_t);
        int code=tri_za_to_code(r16(RAM_Current_zone_and_act));
        if(code>=0)w8(SAVED+(unsigned)cd_slot*10+3,(uint8_t)code);
    }
    tri_jsr(S3K_Write_SaveGame);
}
void tri_cd_stone_award(unsigned stone)
{if(stone<7){cd_state.stones|=(uint8_t)(1u<<stone);tri_cd_save_flush();}}
unsigned tri_campaign_mask(void)
{ return (s3_stage_pack_sonic1?1:0)|(s3_stage_pack_sonic2?2:0)|(s3_stage_pack_megamix?4:0)|(s3_stage_pack_cd?8:0); }
void tri_save_sram_loaded(const char *path,const uint8_t *data,size_t size,int loaded)
{
    ready=projected=0;cd_slot=-1;memset(&cd_state,0,sizeof cd_state);snprintf(save_path,sizeof save_path,"%s",path);
    loaded_valid=loaded && campaign_native_block(data,(unsigned)size,loaded_native);
    if(!s3_stage_pack_enabled || !loaded)return;
    char backup[1240];snprintf(backup,sizeof backup,"%s.pre-campaign.bak",path);
    FILE *f=fopen(backup,"rb");if(f){fclose(f);return;}
    f=fopen(backup,"wb");if(f){fwrite(data,1,size,f);fclose(f);}
}
void tri_save_sram_flushed(const char *path,const uint8_t *data,size_t size)
{
    (void)path;uint8_t native[80];
    if(ready<=0 || !campaign_native_block(data,(unsigned)size,native))return;
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
        if(journal.read_only) {
            extern void tri_campaign_suspend(void);
            tri_campaign_suspend();
            ready=-1;
            fprintf(stderr,"[campaign-save] newer companion retained; using native cartridge saving\n");
            return 0;
        }
        if(!existed && !had_journal)import_legacy();
        uint8_t view[80];campaign_view(&journal,view);
        for(unsigned i=0;i<80;i++)w8(SAVED+i,view[i]);ready=1;
        tri_jsr(S3K_Write_SaveGame);
        fprintf(stderr,"[campaign-save] ready: %d zones, mask %u, %s\n",campaign_count(journal.mask),journal.mask,existed?"journal restored":"initialized");
    }
    if(pc==MG_PC_SAVE_WRITE && ready>0 && !projected) {
        cd_capture();
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
