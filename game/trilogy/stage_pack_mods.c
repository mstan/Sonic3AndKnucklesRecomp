#include "stage_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

int s3_stage_pack_enabled;
int s3_stage_pack_megamix, s3_stage_pack_mighty;
char s3_stage_pack_path[1024];
static char s_settings[1024], s_error[256];
static char s_checked_path[1024], s_checked_status[256];
static int s_checked;

void s3_stage_pack_load(const char *settings)
{
    s3_stage_pack_enabled=0; s3_stage_pack_path[0]=0;
    s3_stage_pack_megamix=s3_stage_pack_mighty=0;
    s_checked=0;s_checked_path[0]=0;
    snprintf(s_settings,sizeof s_settings,"%s",settings?settings:"settings.ini");
    char *a=strrchr(s_settings,'/'),*b=strrchr(s_settings,'\\');
    char *name=a&&(!b||a>b)?a+1:b?b+1:s_settings;
    snprintf(name,sizeof s_settings-(size_t)(name-s_settings),"sonic3k-stage-pack.ini");
    FILE *f=fopen(s_settings,"rb");
    if(!f) return;
    char line[1200];
    while(fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"enabled=1")) s3_stage_pack_enabled=1;
        else if(!strcmp(line,"megamix=1"))s3_stage_pack_megamix=1;
        else if(!strcmp(line,"mighty=1"))s3_stage_pack_mighty=1;
        else if(!strncmp(line,"bank=",5)) snprintf(s3_stage_pack_path,sizeof s3_stage_pack_path,"%s",line+5);
    }
    fclose(f);
    /* Portable handoffs may name a bank relative to the settings directory. */
    if(s3_stage_pack_path[0] && s3_stage_pack_path[0]!='/' && s3_stage_pack_path[0]!='\\' &&
       !(s3_stage_pack_path[1]==':')) {
        char relative[1024];snprintf(relative,sizeof relative,"%s",s3_stage_pack_path);
        size_t prefix=(size_t)(name-s_settings);
        if(prefix+strlen(relative)<sizeof s3_stage_pack_path) {
            memcpy(s3_stage_pack_path,s_settings,prefix);
            strcpy(s3_stage_pack_path+prefix,relative);
        }
    }
}
const char *s3_stage_pack_state_reason(void)
{ return s3_stage_pack_enabled ? "Stage-pack quickstates need complete stage host-state serialization; use campaign saves." : NULL; }
const char *s3_stage_pack_sram(void)
{ return s3_stage_pack_enabled ? (s3_stage_pack_megamix?"sonic3k-megamix.srm":"sonic3k-stage-pack.srm") : NULL; }

#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#define COPY(dst,src) snprintf(dst,sizeof(dst),"%s",src)
static const char PACKAGE[]="sonic3.stage-pack", FEATURE[]="sonic1-2-stages";
static const char *feature_ids[]={FEATURE,"megamix-stages","megamix-mighty"};
static const char *feature_names[]={"Sonic 1 & 2 stages","Sonic Megamix v3 stages","Mighty from Megamix v3"};
static int *feature_flags[]={&s3_stage_pack_enabled,&s3_stage_pack_megamix,&s3_stage_pack_mighty};
static const char *feature_desc[]={
    "Play Sonic 1, Sonic 2, then Sonic 3 & Knuckles in one campaign, with separate saves.",
    "Continue after Sonic 3 & Knuckles with all 19 Megamix v3 acts. Requires the extended bank converted from your own Megamix v3 ROM.",
    "Add Mighty and Mighty & Tails to Data Select without replacing Sonic. Shield storage with Up+A, five-ring swaps with A in the air, elemental jump moves and combined Super shields. Requires an updated extended bank."};
static const char DESC[]="Play Sonic 1, Sonic 2 and Sonic 3 & Knuckles in one campaign, with optional Megamix v3 stages and Mighty. Requires a bank converted from your own ROMs. Separate saves; local play. Experimental.";
static const RecompLauncherCModProvider *base;
static int mine(const char *p) { return p&&!strcmp(p,PACKAGE); }
static int feature_index(const char *f) { if(f)for(int i=0;i<3;i++)if(!strcmp(f,feature_ids[i]))return i;return -1; }
static int own(const char *p,const char *f) { return mine(p)&&feature_index(f)>=0; }
static int packages(void *ctx) { (void)ctx;return 1+(base&&base->package_count?base->package_count(base->ctx):0); }
static int features(void *ctx) { (void)ctx;return 3+(base&&base->feature_count?base->feature_count(base->ctx):0); }
static int package_get(void *ctx,int i,RecompLauncherCModPackage *out)
{
    (void)ctx;
    if(i>0) return base&&base->package_get&&base->package_get(base->ctx,i-1,out);
    if(i<0||!out) return 0;
    memset(out,0,sizeof *out);COPY(out->id,PACKAGE);COPY(out->name,"Sonic Campaign Stage Packs");
    COPY(out->version,"0.1.0");COPY(out->author,"sonsegajp; Sonic3AndKnucklesRecomp contributors");
    COPY(out->description,DESC);COPY(out->license,"PolyForm Noncommercial 1.0.0");
    COPY(out->status,s3_stage_pack_enabled?"Enabled; separate campaign saves":"Disabled");out->enabled=s3_stage_pack_enabled;return 1;
}
static int feature_get(void *ctx,int i,RecompLauncherCModFeature *out)
{
    if(i>=3)return base&&base->feature_get&&base->feature_get(base->ctx,i-3,out);
    RecompLauncherCModPackage p;
    if(i<0||!out||!package_get(ctx,0,&p))return 0;
    memset(out,0,sizeof *out);COPY(out->id,feature_ids[i]);COPY(out->package_id,PACKAGE);
    COPY(out->package_name,p.name);COPY(out->name,feature_names[i]);COPY(out->package_version,p.version);
    COPY(out->author,i?"Team Megamix; Sonic3AndKnucklesRecomp contributors":p.author);
    COPY(out->description,feature_desc[i]);COPY(out->group,i==2?"Characters":"Stages");
    COPY(out->source_name,i?"Team Megamix v3":"SonicTrilogy stage reference");
    COPY(out->source_url,i?"https://www.headcannon.com/TeamMegamix/":"https://github.com/sonsegajp/SonicTrilogy");
    COPY(out->status,*feature_flags[i]?"Enabled; local campaign":"Disabled");out->enabled=*feature_flags[i];out->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;
}
static int feature_enable(void *ctx,const char *p,const char *f,int on)
{
    (void)ctx;s_error[0]=0;
    if(!mine(p))return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);
    if(!own(p,f))return 0;
    *feature_flags[feature_index(f)]=on!=0;
    if(on)s3_stage_pack_enabled=1;
    if(!s3_stage_pack_enabled)s3_stage_pack_megamix=s3_stage_pack_mighty=0;
    s_checked=0;return 1;
}
static int set_enabled(void *ctx,const char *p,int on)
{ return mine(p)?feature_enable(ctx,p,FEATURE,on):base&&base->set_enabled&&base->set_enabled(base->ctx,p,on); }
static int resource_count(void *ctx,const char *p,const char *f)
{ (void)ctx;return mine(p)?own(p,f):base&&base->feature_resource_count?base->feature_resource_count(base->ctx,p,f):0; }
static int resource_get(void *ctx,const char *p,const char *f,int i,RecompLauncherCModResource *out)
{
    (void)ctx;
    if(!mine(p))return base&&base->feature_resource_get&&base->feature_resource_get(base->ctx,p,f,i,out);
    if(!own(p,f)||i||!out)return 0;
    memset(out,0,sizeof *out);COPY(out->id,"stage-bank");COPY(out->label,"Converted campaign stage bank");
    COPY(out->description,feature_index(f)?"Build the extended bank with tools/build_megamix_pack.py and your Megamix v3 ROM.":"Build this file with tools/build_stage_pack.py using your Sonic 1, Sonic 2 and Sonic 3 & Knuckles ROMs.");
    COPY(out->path,s3_stage_pack_path);COPY(out->file_patterns,"*.bin");COPY(out->file_description,"Sonic 1 & 2 stage bank");
    if(!s_checked || strcmp(s_checked_path,s3_stage_pack_path)) {
        s_checked=s3_stage_pack_validate(s3_stage_pack_path,s_checked_status,sizeof s_checked_status)?2:1;
        COPY(s_checked_path,s3_stage_pack_path);
    }
    out->required=1;out->verified=s_checked==2;COPY(out->status,s_checked_status);
    if(out->verified)COPY(out->status,"Verified complete stage bank");return 1;
}
static int resource_set(void *ctx,const char *p,const char *f,const char *r,const char *path)
{
    (void)ctx;s_error[0]=0;
    if(!mine(p))return base&&base->feature_resource_set_path&&base->feature_resource_set_path(base->ctx,p,f,r,path);
    if(!own(p,f)||!r||strcmp(r,"stage-bank")||!path)return 0;
    if(strlen(path)>=sizeof s3_stage_pack_path||strchr(path,'\n')||strchr(path,'\r'))return 0;
    if(!s3_stage_pack_validate(path,s_error,sizeof s_error))return 0;
    COPY(s3_stage_pack_path,path);s_checked=0;return 1;
}
static int option_get(void *ctx,const char *p,const char *f,int i,RecompLauncherCModOption *o)
{ (void)ctx;return !mine(p)&&base&&base->feature_option_get&&base->feature_option_get(base->ctx,p,f,i,o); }
static int choice_get(void *ctx,const char *p,const char *f,const char *o,int i,RecompLauncherCModChoice *c)
{ (void)ctx;return !mine(p)&&base&&base->feature_choice_get&&base->feature_choice_get(base->ctx,p,f,o,i,c); }
static int set_option(void *ctx,const char *p,const char *f,const char *o,const char *v)
{ (void)ctx;return !mine(p)&&base&&base->feature_set_option&&base->feature_set_option(base->ctx,p,f,o,v); }
static int commit(void *ctx,const char *image)
{
    (void)ctx;s_error[0]=0;
    if(s3_stage_pack_enabled&&!s3_stage_pack_validate(s3_stage_pack_path,s_error,sizeof s_error))return 0;
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;
    char tmp[1040];snprintf(tmp,sizeof tmp,"%s.tmp",s_settings);
    FILE *f=fopen(tmp,"wb");if(!f)goto fail;
    fprintf(f,"enabled=%d\nbank=%s\nmegamix=%d\nmighty=%d\n",s3_stage_pack_enabled,s3_stage_pack_path,
            s3_stage_pack_megamix,s3_stage_pack_mighty);
    int ok=!ferror(f);if(fclose(f))ok=0;
#ifdef _WIN32
    if(ok)ok=MoveFileExA(tmp,s_settings,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(ok)ok=rename(tmp,s_settings)==0;
#endif
    if(ok)return 1;
fail:COPY(s_error,"Unable to save stage-pack settings");return 0;
}
static int commit_netplay(void *ctx,const char *image)
{
    (void)ctx;
    if(s3_stage_pack_enabled){COPY(s_error,"Disable the local-only Sonic campaign stage packs before netplay");return 0;}
    return !base||!base->commit_netplay||base->commit_netplay(base->ctx,image);
}
static const char *last_error(void *ctx)
{ (void)ctx;return *s_error?s_error:base&&base->last_error?base->last_error(base->ctx):""; }
const RecompLauncherCModProvider *s3_stage_pack_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;
    /* Preserve import/export and diagnostic callbacks owned by the base. */
    if(common)p=*common;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=package_get;p.set_enabled=set_enabled;
    p.feature_count=features;p.feature_get=feature_get;p.feature_enable=feature_enable;
    p.feature_option_get=option_get;p.feature_choice_get=choice_get;p.feature_set_option=set_option;
    p.feature_resource_count=resource_count;p.feature_resource_get=resource_get;p.feature_resource_set_path=resource_set;
    p.commit=commit;p.commit_netplay=commit_netplay;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_stage_pack_mods(const struct RecompLauncherCModProvider *base) { return base; }
#endif
