#include "stage_pack.h"
#include "scd_resource.h"
#include "scd_import.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#endif

int s3_stage_pack_enabled,s3_stage_pack_sonic1,s3_stage_pack_sonic2;
int s3_stage_pack_megamix,s3_stage_pack_mighty;
int s3_stage_pack_shadow,s3_stage_pack_cd,s3_cd_ufo_practice;
int s3_pack_mighty_requested,s3_pack_shadow_requested;
char s3_megamix_rom_path[1024];
int s3_pack_requested[4],s3_pack_available[4];
char s3_pack_paths[4][1024],s3_stage_pack_path[1024],s3_cd_audio_path[1024];
static char s_settings[1024],s_error[256],s_status[4][256];
static int s_checked[4],s_verified[4],s_rom_verified;
static char s_rom_status[256],s_music_status[256];
static int s_music_checked,s_music_verified;
static char s_cd_disc_path[1024];
static const char *keys[]={"sonic1","sonic2","megamix","sonic_cd"};
static void read_path(char dst[1024],const char *src)
{
    size_t n=strlen(src);
    if(n>=1024)dst[0]=0;else memcpy(dst,src,n+1);
}

void s3_pack_refresh(void)
{
    memset(s_checked,0,sizeof s_checked);
    s_rom_verified=s3_megamix_rom_validate(s3_megamix_rom_path,s_rom_status,sizeof s_rom_status);
    for(int i=0;i<4;i++) {
        int requested=s3_pack_requested[i] || (i==2 && (s3_pack_mighty_requested || s3_pack_shadow_requested));
        s3_pack_available[i]=requested && (i!=2 || s_rom_verified) &&
            s3_pack_validate(s3_pack_paths[i],i+1,s_status[i],sizeof s_status[i]);
    }
    s3_stage_pack_sonic1=s3_pack_available[0];
    s3_stage_pack_sonic2=s3_pack_available[1];
    s3_stage_pack_megamix=s3_pack_requested[2] && s3_pack_available[2];
    s3_stage_pack_mighty=s3_pack_mighty_requested && s3_pack_available[2];
    s3_stage_pack_cd=s3_pack_available[3];
    s3_stage_pack_shadow=0; /* enrolled after the loaded bank's versioned graph */
    s3_stage_pack_enabled=s3_pack_available[0]||s3_pack_available[1]||s3_pack_available[2]||s3_pack_available[3];
}
void s3_stage_pack_load(const char *settings)
{
    memset(s3_pack_requested,0,sizeof s3_pack_requested);
    s3_pack_mighty_requested=s3_pack_shadow_requested=0;s3_megamix_rom_path[0]=0;
    s3_cd_audio_path[0]=s_cd_disc_path[0]=0;s_music_checked=0;s3_cd_ufo_practice=0;
    memset(s3_pack_paths,0,sizeof s3_pack_paths);s3_stage_pack_path[0]=0;
    snprintf(s_settings,sizeof s_settings,"%s",settings?settings:"settings.ini");
    char *a=strrchr(s_settings,'/'),*b=strrchr(s_settings,'\\');
    char *name=a&&(!b||a>b)?a+1:b?b+1:s_settings;
    snprintf(name,sizeof s_settings-(size_t)(name-s_settings),"sonic3k-stage-pack.ini");
    FILE *f=fopen(s_settings,"rb");int v2=0,v3=0,legacy=0,mm=0;
    char line[1200];
    while(f && fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"format=2"))v2=1;
        if(!strcmp(line,"format=3"))v2=v3=1;
        if(!strncmp(line,"mighty=",7))s3_pack_mighty_requested=atoi(line+7)!=0;
        if(!strncmp(line,"shadow=",7))s3_pack_shadow_requested=atoi(line+7)!=0;
        if(!strncmp(line,"megamix_rom=",12))read_path(s3_megamix_rom_path,line+12);
        if(!strncmp(line,"sonic_cd_audio=",15))read_path(s3_cd_audio_path,line+15);
        if(!strncmp(line,"sonic_cd_disc=",14))read_path(s_cd_disc_path,line+14);
        if(!strncmp(line,"sonic_cd_ufo_practice=",22)) {
            int value=atoi(line+22);if(value>=0 && value<=8)s3_cd_ufo_practice=value;
        }
        if(!strcmp(line,"enabled=1"))legacy=1;
        if(!strcmp(line,"megamix=1"))mm=1;
        if(!strncmp(line,"bank=",5))read_path(s3_stage_pack_path,line+5);
        for(int i=0;i<4;i++) {
            char key[32];snprintf(key,sizeof key,"%s=",keys[i]);
            if(!strncmp(line,key,strlen(key)))s3_pack_requested[i]=atoi(line+strlen(key))!=0;
            snprintf(key,sizeof key,"%s_bank=",keys[i]);
            if(!strncmp(line,key,strlen(key)))read_path(s3_pack_paths[i],line+strlen(key));
        }
    }
    if(f)fclose(f);
    if(!v2 && legacy) {
        s3_pack_requested[0]=s3_pack_requested[1]=1;s3_pack_requested[2]=mm;
        for(int i=0;i<3;i++)snprintf(s3_pack_paths[i],1024,"%s",s3_stage_pack_path);
    }
    if(!v3)s3_pack_mighty_requested=s3_pack_shadow_requested=s3_pack_requested[2];
    for(int i=0;i<4;i++) {
        if(!s3_pack_paths[i][0]) {
            if(i==3)snprintf(s3_pack_paths[i],1024,"mods/sonic-cd-stages.pak");
            else snprintf(s3_pack_paths[i],1024,"mods/%s-stages.pak",keys[i]);
        }
        if(s3_pack_paths[i][0]!='/' && s3_pack_paths[i][0]!='\\' && s3_pack_paths[i][1]!=':') {
            char relative[1024];memcpy(relative,s3_pack_paths[i],sizeof relative);
            size_t prefix=(size_t)(name-s_settings);
            if(prefix+strlen(relative)<1024) {
                memcpy(s3_pack_paths[i],s_settings,prefix);strcpy(s3_pack_paths[i]+prefix,relative);
            }
        }
    }
    if(s3_megamix_rom_path[0] && s3_megamix_rom_path[0]!='/' && s3_megamix_rom_path[0]!='\\' && s3_megamix_rom_path[1]!=':') {
        char relative[1024];snprintf(relative,sizeof relative,"%s",s3_megamix_rom_path);
        size_t prefix=(size_t)(name-s_settings);
        if(prefix+strlen(relative)<1024) {
            memcpy(s3_megamix_rom_path,s_settings,prefix);strcpy(s3_megamix_rom_path+prefix,relative);
        }
    }
    if(!s3_cd_audio_path[0])snprintf(s3_cd_audio_path,sizeof s3_cd_audio_path,"mods/sonic-cd-audio.pak");
    if(s3_cd_audio_path[0]!='/' && s3_cd_audio_path[0]!='\\' && s3_cd_audio_path[1]!=':') {
        char relative[1024];snprintf(relative,sizeof relative,"%s",s3_cd_audio_path);
        size_t prefix=(size_t)(name-s_settings);
        if(prefix+strlen(relative)<1024){memcpy(s3_cd_audio_path,s_settings,prefix);strcpy(s3_cd_audio_path+prefix,relative);}
    }
    s3_pack_refresh();
}
const char *s3_stage_pack_state_reason(void)
{ return s3_stage_pack_enabled?"Experimental stages and characters use campaign saves; quickstates are unavailable.":NULL; }
const char *s3_stage_pack_sram(void) { return NULL; }

#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#define COPY(dst,src) snprintf(dst,sizeof(dst),"%s",src)
enum { MOD_COUNT=6 };
static const char *ids[]={"sonic3.sonic1-stages","sonic3.sonic2-stages","sonic3.megamix","sonic3.sonic-cd","sonic3.mighty","sonic3.shadow"};
static const char *names[]={"Sonic 1 stages (Experimental)","Sonic 2 stages (Experimental)","Sonic Megamix stages (Experimental)","Sonic CD stages (Experimental)","Mighty (Experimental)","Shadow (Experimental)"};
static const char *descs[]={
    "Add all Sonic 1 stages before Sonic 3 & Knuckles. Works independently or with the other campaign packs.",
    "Add all Sonic 2 stages before Sonic 3 & Knuckles, after Sonic 1 when installed.",
    "Add all 19 Megamix v3 acts after Sonic 3 & Knuckles. Requires your Megamix v3 ROM and locally converted content pack, shared with Mighty and Shadow.",
    "Play Sonic CD stages with S3K characters and abilities, between Sonic 2 and Sonic 3 & Knuckles. Select your owned US disc to import stages and music together. No Sega CD BIOS required.",
    "Add Mighty and Mighty & Tails to Data Select, independently of Megamix stages and Shadow. Requires your Megamix v3 ROM and locally converted content pack.",
    "Add Shadow and Shadow & Tails to Data Select, independently of Megamix stages and Mighty. Requires your Megamix v3 ROM and an updated locally converted content pack."};
static const RecompLauncherCModProvider *base;
static int requested(int i) { return i==4?s3_pack_mighty_requested:i==5?s3_pack_shadow_requested:s3_pack_requested[i]; }
static int available(int i) { return s3_pack_available[i<4?i:2]; }
static int index_of(const char *p) { if(p)for(int i=0;i<MOD_COUNT;i++)if(!strcmp(p,ids[i]))return i;return -1; }
static int own(const char *p,const char *f) { return index_of(p)>=0 && f && !strcmp(f,"campaign"); }
static int packages(void *c) { (void)c;return MOD_COUNT+(base&&base->package_count?base->package_count(base->ctx):0); }
static int features(void *c) { (void)c;return MOD_COUNT+(base&&base->feature_count?base->feature_count(base->ctx):0); }
static int package_get(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>=MOD_COUNT)return base&&base->package_get&&base->package_get(base->ctx,i-MOD_COUNT,o);
    if(i<0||!o)return 0;
    memset(o,0,sizeof *o);COPY(o->id,ids[i]);COPY(o->name,names[i]);COPY(o->version,"0.2.0-experimental");
    COPY(o->author,(i==2 || i>=4)?"Team Megamix; Sonic3AndKnucklesRecomp contributors":"sonsegajp; Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,descs[i]);COPY(o->license,"PolyForm Noncommercial 1.0.0");
    COPY(o->status,!requested(i)?"Disabled":(i==2 || i>=4)&&!s_rom_verified?"Megamix v3 ROM required; progress retained":available(i)?"Enabled; local content":"Content pack unavailable; progress retained");
    o->enabled=requested(i);return 1;
}
static int feature_get(void *c,int i,RecompLauncherCModFeature *o)
{
    if(i>=MOD_COUNT)return base&&base->feature_get&&base->feature_get(base->ctx,i-MOD_COUNT,o);
    RecompLauncherCModPackage p;if(i<0||!o||!package_get(c,i,&p))return 0;
    memset(o,0,sizeof *o);COPY(o->id,"campaign");COPY(o->package_id,p.id);COPY(o->package_name,p.name);
    COPY(o->name,names[i]);COPY(o->package_version,p.version);COPY(o->author,p.author);
    COPY(o->description,descs[i]);COPY(o->group,i>=4?"Additional characters":"Stages");COPY(o->status,p.status);
    COPY(o->source_name,(i==2 || i>=4)?"Team Megamix v3":"Sonic Anthology by sonsegajp");
    COPY(o->source_url,(i==2 || i>=4)?"https://www.headcannon.com/TeamMegamix/":"https://github.com/sonsegajp/SonicAnthology");
    o->enabled=requested(i);o->channel=RECOMP_MOD_CHANNEL_EXPERIMENTAL;o->option_count=i==3?1:0;return 1;
}
static int set_enabled(void *c,const char *p,int on)
{
    (void)c;int i=index_of(p);if(i<0)return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);
    if(i==4)s3_pack_mighty_requested=on!=0;
    else if(i==5)s3_pack_shadow_requested=on!=0;
    else s3_pack_requested[i]=on!=0;
    s3_pack_refresh();return 1;
}
static int feature_enable(void *c,const char *p,const char *f,int on)
{ return index_of(p)<0?base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on):own(p,f)&&set_enabled(c,p,on); }
static int resource_count(void *c,const char *p,const char *f)
{ (void)c;return index_of(p)>=0?(own(p,f)?(index_of(p)==3?3:index_of(p)>=2?2:1):0):base&&base->feature_resource_count?base->feature_resource_count(base->ctx,p,f):0; }
static void check_music(void)
{
    if(s_music_checked)return;
    ScdResource music={0};s_music_verified=scd_resource_open(&music,s3_cd_audio_path,1,s_music_status,sizeof s_music_status);
    scd_resource_close(&music);s_music_checked=1;
}
static int resource_get(void *c,const char *p,const char *f,int n,RecompLauncherCModResource *o)
{
    (void)c;int i=index_of(p);if(i<0)return base&&base->feature_resource_get&&base->feature_resource_get(base->ctx,p,f,n,o);
    if(!own(p,f)||n<0||n>=(i==3?3:i>=2?2:1)||!o)return 0;
    if(i==3 && n==0) {
        memset(o,0,sizeof *o);COPY(o->id,"cd-disc");COPY(o->label,"Sonic CD (US) disc");
        COPY(o->description,"Select your owned CUE/BIN disc or the same .sgcd image used by SonicCDRecomp. Stages and all CD music are imported together. Extract archives first.");
        COPY(o->path,s_cd_disc_path);COPY(o->file_patterns,"*.cue,*.sgcd");
        COPY(o->file_description,"Sonic CD disc");o->import_on_select=1;
        if(!s_checked[3]){s_verified[3]=s3_pack_validate(s3_pack_paths[3],4,s_status[3],sizeof s_status[3]);s_checked[3]=1;}
        check_music();o->verified=*s_cd_disc_path && s_verified[3] && s_music_verified;
        COPY(o->status,o->verified?"Imported stages and music are ready":*s_cd_disc_path?
             "Imported resources unavailable; select the disc to restore them":"Select your disc to import stages and music");
        return 1;
    }
    if(i==3)n--; /* keep the two manually converted resource choices */
    if(i==3 && n==1) {
        memset(o,0,sizeof *o);COPY(o->id,"cd-music");COPY(o->label,"CD music");
        COPY(o->description,"Present and future music from your converted disc. Missing music leaves stages, past music and voices available.");
        COPY(o->path,s3_cd_audio_path);COPY(o->file_patterns,"*.pak");COPY(o->file_description,"Converted Sonic CD music");
        check_music();
        o->required=0;o->verified=s_music_verified;
        COPY(o->status,s_music_verified?"Verified music resource":"Music unavailable; stage progress retained");return 1;
    }
    memset(o,0,sizeof *o);
    if((i==2 || i>=4) && n==0) {
        COPY(o->id,"megamix-rom");COPY(o->label,"Sonic Megamix v3 ROM");
        COPY(o->description,"Select your raw Megamix v3 cartridge (.bin, .md or .gen). This path is shared automatically by Megamix stages, Mighty and Shadow. Extract ZIP archives first.");
        COPY(o->path,s3_megamix_rom_path);COPY(o->file_patterns,"*.bin,*.md,*.gen");
        COPY(o->file_description,"Sonic Megamix v3 cartridge");o->required=1;o->verified=s_rom_verified;
        COPY(o->status,s_rom_status);return 1;
    }
    i=i<4?i:2;COPY(o->id,"stage-bank");COPY(o->label,"Campaign content pack");
    COPY(o->description,"Choose your locally converted content pack. Missing packs are skipped; saves and the selected path are retained.");
    COPY(o->path,s3_pack_paths[i]);COPY(o->file_patterns,"*.pak,*.bin");COPY(o->file_description,"Converted campaign content");
    if(!s_checked[i]) {
        s_verified[i]=s3_pack_validate(s3_pack_paths[i],i+1,s_status[i],sizeof s_status[i]);s_checked[i]=1;
    }
    o->required=i==2;o->verified=s_verified[i];COPY(o->status,s_status[i]);
    if(o->verified)COPY(o->status,"Verified content pack");return 1;
}
static int resource_set(void *c,const char *p,const char *f,const char *r,const char *path)
{
    (void)c;int i=index_of(p);s_error[0]=0;
    if(i<0)return base&&base->feature_resource_set_path&&base->feature_resource_set_path(base->ctx,p,f,r,path);
    if(i==3 && own(p,f) && r && !strcmp(r,"cd-disc")) {
        if(!path || *path){COPY(s_error,"Use the disc importer to prepare stages and music together.");return 0;}
        s_cd_disc_path[0]=0;return 1; /* clearing a source never deletes imported resources */
    }
    if(i==3 && own(p,f) && r && !strcmp(r,"cd-music")) {
        if(!path || strlen(path)>=1024 || strchr(path,'\n') || strchr(path,'\r'))return 0;
        ScdResource music={0};
        if(*path && !scd_resource_open(&music,path,1,s_error,sizeof s_error))return 0;
        scd_resource_close(&music);snprintf(s3_cd_audio_path,sizeof s3_cd_audio_path,"%s",path);s_cd_disc_path[0]=0;s_music_checked=0;return 1;
    }
    if(!own(p,f)||!r||!path||strlen(path)>=1024||strchr(path,'\n')||strchr(path,'\r'))return 0;
    if((i==2 || i>=4) && !strcmp(r,"megamix-rom")) {
        if(*path&&!s3_megamix_rom_validate(path,s_error,sizeof s_error))return 0;
        snprintf(s3_megamix_rom_path,sizeof s3_megamix_rom_path,"%s",path);s3_pack_refresh();return 1;
    }
    if(strcmp(r,"stage-bank"))return 0;
    i=i<4?i:2;
    if(*path&&!s3_pack_validate(path,i+1,s_error,sizeof s_error))return 0;
    if(i==3)s_cd_disc_path[0]=0;
    snprintf(s3_pack_paths[i],1024,"%s",path);s3_pack_refresh();return 1;
}
static int option_get(void *c,const char *p,const char *f,int i,RecompLauncherCModOption *o)
{
    (void)c;if(index_of(p)<0)return base&&base->feature_option_get&&base->feature_option_get(base->ctx,p,f,i,o);
    if(index_of(p)!=3 || !own(p,f) || i || !o)return 0;
    memset(o,0,sizeof *o);COPY(o->id,"ufo-practice");COPY(o->label,"UFO practice");
    COPY(o->description,"Choose a UFO stage, then start No Save. Practice returns to Palmtree Panic and awards no Time Stones. Saved games keep their normal campaign.");
    snprintf(o->value,sizeof o->value,"%d",s3_cd_ufo_practice);COPY(o->default_value,"0");
    o->type=RECOMP_MOD_OPTION_CHOICE;o->choice_count=9;return 1;
}
static int choice_get(void *c,const char *p,const char *f,const char *o,int i,RecompLauncherCModChoice *v)
{
    (void)c;if(index_of(p)<0)return base&&base->feature_choice_get&&base->feature_choice_get(base->ctx,p,f,o,i,v);
    if(index_of(p)!=3 || !own(p,f) || !o || strcmp(o,"ufo-practice") || i<0 || i>8 || !v)return 0;
    memset(v,0,sizeof *v);snprintf(v->value,sizeof v->value,"%d",i);
    if(i)snprintf(v->label,sizeof v->label,i==8?"UFO stage 8 (secret)":"UFO stage %d",i);
    else COPY(v->label,"Normal campaign");return 1;
}
static int set_option(void *c,const char *p,const char *f,const char *o,const char *v)
{
    (void)c;if(index_of(p)<0)return base&&base->feature_set_option&&base->feature_set_option(base->ctx,p,f,o,v);
    if(index_of(p)!=3 || !own(p,f) || !o || strcmp(o,"ufo-practice") || !v || v[0]<'0' || v[0]>'8' || v[1])return 0;
    s3_cd_ufo_practice=v[0]-'0';return 1;
}
static int commit(void *c,const char *image)
{
    (void)c;s_error[0]=0;
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;
    char tmp[1040];snprintf(tmp,sizeof tmp,"%s.tmp",s_settings);
    FILE *f=fopen(tmp,"wb");if(!f)goto fail;
    fprintf(f,"format=3\nmighty=%d\nshadow=%d\nmegamix_rom=%s\n",s3_pack_mighty_requested,s3_pack_shadow_requested,s3_megamix_rom_path);
    for(int i=0;i<4;i++)fprintf(f,"%s=%d\n%s_bank=%s\n",keys[i],s3_pack_requested[i],keys[i],s3_pack_paths[i]);
    fprintf(f,"sonic_cd_audio=%s\nsonic_cd_ufo_practice=%d\n",s3_cd_audio_path,s3_cd_ufo_practice);
    fprintf(f,"sonic_cd_disc=%s\n",s_cd_disc_path);
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
static int resource_import(void *c,const char *p,const char *f,const char *r,
                           const char *path,const char *image,
                           void (*progress)(void *,float,const char *),void *user,
                           char *error,size_t capacity)
{
    (void)c;
    if(index_of(p)!=3 || !own(p,f) || !r || strcmp(r,"cd-disc"))
        return base&&base->feature_resource_import&&base->feature_resource_import(
            base->ctx,p,f,r,path,image,progress,user,error,capacity);
    if(!path||strlen(path)>=sizeof s_cd_disc_path||strchr(path,'\n')||strchr(path,'\r')) {
        snprintf(error,capacity,"Invalid Sonic CD disc path.");return 0;
    }
    char content[1024],music[1024];
    if(!s3_cd_import(path,image,s_settings,content,music,error,capacity,progress,user))return 0;
    unsigned error_capacity=capacity>UINT_MAX?UINT_MAX:(unsigned)capacity;
    ScdResource audio={0};
    if(!s3_pack_validate(content,4,error,error_capacity))return 0;
    if(!scd_resource_open(&audio,music,1,error,error_capacity))return 0;
    scd_resource_close(&audio);
    /* Adopt the complete verified pair together, preserving every mod switch
     * and all in-game progress. A failed conversion never reaches this point. */
    COPY(s_cd_disc_path,path);COPY(s3_pack_paths[3],content);COPY(s3_cd_audio_path,music);
    s_music_checked=0;s3_pack_refresh();return 1;
}
const RecompLauncherCModProvider *s3_stage_pack_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;if(common)p=*common;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=package_get;p.set_enabled=set_enabled;
    p.feature_count=features;p.feature_get=feature_get;p.feature_enable=feature_enable;
    p.feature_option_get=option_get;p.feature_choice_get=choice_get;p.feature_set_option=set_option;
    p.feature_resource_count=resource_count;p.feature_resource_get=resource_get;p.feature_resource_set_path=resource_set;
    p.feature_resource_import=resource_import;
    p.commit=commit;p.commit_netplay=commit_netplay;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_stage_pack_mods(const struct RecompLauncherCModProvider *base) { return base; }
#endif
