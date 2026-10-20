#include "stage_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

int s3_stage_pack_enabled,s3_stage_pack_sonic1,s3_stage_pack_sonic2;
int s3_stage_pack_megamix,s3_stage_pack_mighty;
int s3_stage_pack_shadow;
int s3_pack_requested[3],s3_pack_available[3];
char s3_pack_paths[3][1024],s3_stage_pack_path[1024];
static char s_settings[1024],s_error[256],s_status[3][256];
static int s_checked[3],s_verified[3];
static const char *keys[]={"sonic1","sonic2","megamix"};

void s3_pack_refresh(void)
{
    memset(s_checked,0,sizeof s_checked);
    for(int i=0;i<3;i++)s3_pack_available[i]=s3_pack_requested[i] &&
        s3_pack_validate(s3_pack_paths[i],i+1,s_status[i],sizeof s_status[i]);
    s3_stage_pack_sonic1=s3_pack_available[0];
    s3_stage_pack_sonic2=s3_pack_available[1];
    s3_stage_pack_megamix=s3_stage_pack_mighty=s3_pack_available[2];
    s3_stage_pack_shadow=0; /* enrolled after the loaded bank's versioned graph */
    s3_stage_pack_enabled=s3_stage_pack_sonic1||s3_stage_pack_sonic2||s3_stage_pack_megamix;
}
void s3_stage_pack_load(const char *settings)
{
    memset(s3_pack_requested,0,sizeof s3_pack_requested);
    memset(s3_pack_paths,0,sizeof s3_pack_paths);s3_stage_pack_path[0]=0;
    snprintf(s_settings,sizeof s_settings,"%s",settings?settings:"settings.ini");
    char *a=strrchr(s_settings,'/'),*b=strrchr(s_settings,'\\');
    char *name=a&&(!b||a>b)?a+1:b?b+1:s_settings;
    snprintf(name,sizeof s_settings-(size_t)(name-s_settings),"sonic3k-stage-pack.ini");
    FILE *f=fopen(s_settings,"rb");int v2=0,legacy=0,mm=0;
    char line[1200];
    while(f && fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"format=2"))v2=1;
        if(!strcmp(line,"enabled=1"))legacy=1;
        if(!strcmp(line,"megamix=1"))mm=1;
        if(!strncmp(line,"bank=",5))snprintf(s3_stage_pack_path,sizeof s3_stage_pack_path,"%s",line+5);
        for(int i=0;i<3;i++) {
            char key[32];snprintf(key,sizeof key,"%s=",keys[i]);
            if(!strncmp(line,key,strlen(key)))s3_pack_requested[i]=atoi(line+strlen(key))!=0;
            snprintf(key,sizeof key,"%s_bank=",keys[i]);
            if(!strncmp(line,key,strlen(key)))snprintf(s3_pack_paths[i],1024,"%s",line+strlen(key));
        }
    }
    if(f)fclose(f);
    if(!v2 && legacy) {
        s3_pack_requested[0]=s3_pack_requested[1]=1;s3_pack_requested[2]=mm;
        for(int i=0;i<3;i++)snprintf(s3_pack_paths[i],1024,"%s",s3_stage_pack_path);
    }
    for(int i=0;i<3;i++) {
        if(!s3_pack_paths[i][0])snprintf(s3_pack_paths[i],1024,"mods/%s-stages.pak",keys[i]);
        if(s3_pack_paths[i][0]!='/' && s3_pack_paths[i][0]!='\\' && s3_pack_paths[i][1]!=':') {
            char relative[1024];snprintf(relative,sizeof relative,"%s",s3_pack_paths[i]);
            size_t prefix=(size_t)(name-s_settings);
            if(prefix+strlen(relative)<1024) {
                memcpy(s3_pack_paths[i],s_settings,prefix);strcpy(s3_pack_paths[i]+prefix,relative);
            }
        }
    }
    s3_pack_refresh();
}
const char *s3_stage_pack_state_reason(void)
{ return s3_stage_pack_enabled?"Experimental stage packs use campaign saves; quickstates are unavailable.":NULL; }
const char *s3_stage_pack_sram(void) { return NULL; }

#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#define COPY(dst,src) snprintf(dst,sizeof(dst),"%s",src)
static const char *ids[]={"sonic3.sonic1-stages","sonic3.sonic2-stages","sonic3.megamix"};
static const char *names[]={"Sonic 1 stages (Experimental)","Sonic 2 stages (Experimental)","Sonic Megamix + Mighty + Shadow (Experimental)"};
static const char *descs[]={
    "Add all Sonic 1 stages before Sonic 3 & Knuckles. Works independently or with the other campaign packs.",
    "Add all Sonic 2 stages before Sonic 3 & Knuckles, after Sonic 1 when installed.",
    "Add all 19 Megamix v3 acts after Sonic 3 & Knuckles, plus Mighty and Shadow with optional Tails partners. Shadow requires an updated converted resource. Works independently of Sonic 1 and Sonic 2 packs."};
static const RecompLauncherCModProvider *base;
static int index_of(const char *p) { if(p)for(int i=0;i<3;i++)if(!strcmp(p,ids[i]))return i;return -1; }
static int own(const char *p,const char *f) { return index_of(p)>=0 && f && !strcmp(f,"campaign"); }
static int packages(void *c) { (void)c;return 3+(base&&base->package_count?base->package_count(base->ctx):0); }
static int features(void *c) { (void)c;return 3+(base&&base->feature_count?base->feature_count(base->ctx):0); }
static int package_get(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>=3)return base&&base->package_get&&base->package_get(base->ctx,i-3,o);
    if(i<0||!o)return 0;
    memset(o,0,sizeof *o);COPY(o->id,ids[i]);COPY(o->name,names[i]);COPY(o->version,"0.2.0-experimental");
    COPY(o->author,i==2?"Team Megamix; Sonic3AndKnucklesRecomp contributors":"sonsegajp; Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,descs[i]);COPY(o->license,"PolyForm Noncommercial 1.0.0");
    COPY(o->status,!s3_pack_requested[i]?"Disabled":s3_pack_available[i]?"Enabled; local campaign":"Pack unavailable; progress retained");
    o->enabled=s3_pack_requested[i];return 1;
}
static int feature_get(void *c,int i,RecompLauncherCModFeature *o)
{
    if(i>=3)return base&&base->feature_get&&base->feature_get(base->ctx,i-3,o);
    RecompLauncherCModPackage p;if(i<0||!o||!package_get(c,i,&p))return 0;
    memset(o,0,sizeof *o);COPY(o->id,"campaign");COPY(o->package_id,p.id);COPY(o->package_name,p.name);
    COPY(o->name,names[i]);COPY(o->package_version,p.version);COPY(o->author,p.author);
    COPY(o->description,descs[i]);COPY(o->group,"Stages");COPY(o->status,p.status);
    COPY(o->source_name,i==2?"Team Megamix v3":"SonicTrilogy stage reference");
    COPY(o->source_url,i==2?"https://www.headcannon.com/TeamMegamix/":"https://github.com/sonsegajp/SonicTrilogy");
    o->enabled=s3_pack_requested[i];o->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;return 1;
}
static int set_enabled(void *c,const char *p,int on)
{
    (void)c;int i=index_of(p);if(i<0)return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);
    s3_pack_requested[i]=on!=0;s3_pack_refresh();return 1;
}
static int feature_enable(void *c,const char *p,const char *f,int on)
{ return index_of(p)<0?base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on):own(p,f)&&set_enabled(c,p,on); }
static int resource_count(void *c,const char *p,const char *f)
{ (void)c;return index_of(p)>=0?own(p,f):base&&base->feature_resource_count?base->feature_resource_count(base->ctx,p,f):0; }
static int resource_get(void *c,const char *p,const char *f,int n,RecompLauncherCModResource *o)
{
    (void)c;int i=index_of(p);if(i<0)return base&&base->feature_resource_get&&base->feature_resource_get(base->ctx,p,f,n,o);
    if(!own(p,f)||n||!o)return 0;
    memset(o,0,sizeof *o);COPY(o->id,"stage-bank");COPY(o->label,"Campaign content pack");
    COPY(o->description,"Choose your locally converted content pack. Missing packs are skipped; saves and the selected path are retained.");
    COPY(o->path,s3_pack_paths[i]);COPY(o->file_patterns,"*.pak,*.bin");COPY(o->file_description,"Converted campaign content");
    if(!s_checked[i]) {
        s_verified[i]=s3_pack_validate(s3_pack_paths[i],i+1,s_status[i],sizeof s_status[i]);s_checked[i]=1;
    }
    o->required=0;o->verified=s_verified[i];COPY(o->status,s_status[i]);
    if(o->verified)COPY(o->status,"Verified content pack");return 1;
}
static int resource_set(void *c,const char *p,const char *f,const char *r,const char *path)
{
    (void)c;int i=index_of(p);s_error[0]=0;
    if(i<0)return base&&base->feature_resource_set_path&&base->feature_resource_set_path(base->ctx,p,f,r,path);
    if(!own(p,f)||!r||strcmp(r,"stage-bank")||!path||strlen(path)>=1024||strchr(path,'\n')||strchr(path,'\r'))return 0;
    if(*path&&!s3_pack_validate(path,i+1,s_error,sizeof s_error))return 0;
    snprintf(s3_pack_paths[i],1024,"%s",path);s3_pack_refresh();return 1;
}
static int option_get(void *c,const char *p,const char *f,int i,RecompLauncherCModOption *o)
{ (void)c;return index_of(p)<0&&base&&base->feature_option_get&&base->feature_option_get(base->ctx,p,f,i,o); }
static int choice_get(void *c,const char *p,const char *f,const char *o,int i,RecompLauncherCModChoice *v)
{ (void)c;return index_of(p)<0&&base&&base->feature_choice_get&&base->feature_choice_get(base->ctx,p,f,o,i,v); }
static int set_option(void *c,const char *p,const char *f,const char *o,const char *v)
{ (void)c;return index_of(p)<0&&base&&base->feature_set_option&&base->feature_set_option(base->ctx,p,f,o,v); }
static int commit(void *c,const char *image)
{
    (void)c;s_error[0]=0;
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;
    char tmp[1040];snprintf(tmp,sizeof tmp,"%s.tmp",s_settings);
    FILE *f=fopen(tmp,"wb");if(!f)goto fail;
    fprintf(f,"format=2\n");
    for(int i=0;i<3;i++)fprintf(f,"%s=%d\n%s_bank=%s\n",keys[i],s3_pack_requested[i],keys[i],s3_pack_paths[i]);
    int ok=!ferror(f);if(fclose(f))ok=0;
#ifdef _WIN32
    if(ok)ok=MoveFileExA(tmp,s_settings,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(ok)ok=rename(tmp,s_settings)==0;
#endif
    if(ok)return 1;
fail:COPY(s_error,"Unable to save campaign settings");return 0;
}
static int commit_netplay(void *c,const char *image)
{
    (void)c;if(s3_stage_pack_enabled){COPY(s_error,"Disable experimental campaign packs before netplay");return 0;}
    return !base||!base->commit_netplay||base->commit_netplay(base->ctx,image);
}
static const char *last_error(void *c)
{ (void)c;return *s_error?s_error:base&&base->last_error?base->last_error(base->ctx):""; }
const RecompLauncherCModProvider *s3_stage_pack_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;if(common)p=*common;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=package_get;p.set_enabled=set_enabled;
    p.feature_count=features;p.feature_get=feature_get;p.feature_enable=feature_enable;
    p.feature_option_get=option_get;p.feature_choice_get=choice_get;p.feature_set_option=set_option;
    p.feature_resource_count=resource_count;p.feature_resource_get=resource_get;p.feature_resource_set_path=resource_set;
    p.commit=commit;p.commit_netplay=commit_netplay;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_stage_pack_mods(const struct RecompLauncherCModProvider *base) { return base; }
#endif
