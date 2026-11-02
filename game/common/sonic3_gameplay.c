#include "sonic3_gameplay.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
S3GameplayConfig s3_gameplay;
static char path[1024],error[256];
static int standalone_s3,combined_mode;
void s3_gameplay_load(const char *settings_path,const char *mode)
{
    memset(&s3_gameplay,0,sizeof s3_gameplay);
    error[0]=0;standalone_s3=mode&&!strcmp(mode,"sonic3");
    combined_mode=mode&&!strcmp(mode,"sonic3k");
    snprintf(path,sizeof path,"%s",settings_path?settings_path:"settings.ini");
    char *a=strrchr(path,'/'),*b=strrchr(path,'\\');
    char *name=a&&(!b||a>b)?a+1:b?b+1:path;
    snprintf(name,sizeof path-(size_t)(name-path),"%s-gameplay.ini",mode?mode:"sonic3k");
    FILE *f=fopen(path,"rb");if(!f)return;
    char line[128];
    while(fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"infinite_lives=1"))s3_gameplay.infinite_lives=1;
        if(!strcmp(line,"infinite_time=1"))s3_gameplay.infinite_time=1;
        if(!strcmp(line,"easy_level_select=1"))s3_gameplay.easy_level_select=1;
        if(!strcmp(line,"debug_mode=1"))s3_gameplay.debug_mode=1;
        if(combined_mode&&!strcmp(line,"checkpoint_bonus=1"))s3_gameplay.checkpoint_bonus=1;
    }
    fclose(f);
}
int s3_gameplay_save(void)
{
    error[0]=0;
    char temp[1040];snprintf(temp,sizeof temp,"%s.tmp",path);
    FILE *f=fopen(temp,"wb");if(!f)goto failed;
    fprintf(f,"infinite_lives=%d\ninfinite_time=%d\neasy_level_select=%d\ndebug_mode=%d\ncheckpoint_bonus=%d\n",
        s3_gameplay.infinite_lives,s3_gameplay.infinite_time,
        s3_gameplay.easy_level_select,s3_gameplay.debug_mode,s3_gameplay.checkpoint_bonus);
    int ok=!ferror(f);if(fclose(f))ok=0;
    if(!ok)goto failed;
#ifdef _WIN32
    if(!MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))goto failed;
#else
    if(rename(temp,path))goto failed;
#endif
    return 1;
failed:
    snprintf(error,sizeof error,"Unable to save gameplay options");return 0;
}
int s3_gameplay_netplay_allowed(void)
{ return !s3_gameplay.infinite_lives && !s3_gameplay.infinite_time &&
    !s3_gameplay.easy_level_select && !s3_gameplay.debug_mode && !s3_gameplay.checkpoint_bonus; }
void s3_gameplay_apply(uint8_t ram[65536])
{
    if(!ram)return;
    unsigned demo=standalone_s3?0xFFF0:0xFFD0;
    unsigned competition=standalone_s3?0xFFD8:0xFFE8;
    if(ram[demo] || ram[demo+1] || ram[competition] || ram[competition+1])return;
    /* Unlock the existing third title choice / A+Start path. The native menu
     * still owns selection, stage initialization and save/no-save handling. */
    if(s3_gameplay.easy_level_select && ram[0xF600]==4)
        ram[standalone_s3?0xFFD0:0xFFE0]=1;
    /* All three cartridges share these variables. Exclude attract demos,
     * menus, bonus stages and Competition. The normal game owns death/retry,
     * score awards, timer progression, checkpoints and act transitions. */
    if(ram[0xF600]!=12)return;
    /* Native B/A/C placement controls, never placement/result flags. Imported
     * rounds have different object tables; do not expose native debug there. */
    if(s3_gameplay.debug_mode)
        ram[standalone_s3?0xFFFA:0xFFDA]=ram[0xFE10]<0x20;
    if(s3_gameplay.infinite_lives && ram[0xFE12]<3) {
        ram[0xFE12]=3;ram[0xFE1C]=1;
    }
    /* UpdateHUD compares 00:09:3B:3B BEFORE incrementing Timer_frame.
     * Hold only that terminal subsecond: displayed time reaches 9:59 and
     * stays there, while all unrelated frame/air/invincibility timers run. */
    if(s3_gameplay.infinite_time && !ram[0xFE22] && ram[0xFE23]==9 &&
       ram[0xFE24]==59 && ram[0xFE25]==59)ram[0xFE25]=58;
}
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
static const RecompLauncherCModProvider *base;
static const char package[]="sonic3.gameplay";
static const char *ids[]={"infinite-lives","infinite-time","easy-level-select","debug-mode","s1-checkpoint-bonus"};
static const char *names[]={"Infinite lives","Infinite time","Easier level select","Debug mode","Sonic 1 checkpoint bonus stages"};
static const char *descriptions[]={
    "Keep at least three lives, including after death and retry. Local play only.",
    "Disable the stage time limit. The clock runs normally to 9:59, then stays there. Local play only.",
    "Unlock Sound Test on the title menu. Press Up to select it, then Start; use the native stage list. Local play only.",
    "Enable native debug in original stages. B enters/exits placement, A cycles objects, C places one. Imported rounds are excluded. Local play only.",
    "Sonic 1 rounds only: collect at least 20 rings and enter the stars above an activated checkpoint to visit a native bonus stage. Local play only."};
static int own_features(void){return combined_mode?5:4;}
#define COPY(d,s) snprintf(d,sizeof(d),"%s",s)
static int own(const char *p){return p&&!strcmp(p,package);}
static int index_of(const char *f){for(int i=0;i<own_features();++i)if(f&&!strcmp(f,ids[i]))return i;return -1;}
static int *value(int i){switch(i) {
    case 0:return &s3_gameplay.infinite_lives;
    case 1:return &s3_gameplay.infinite_time;
    case 2:return &s3_gameplay.easy_level_select;
    case 3:return &s3_gameplay.debug_mode;
    default:return &s3_gameplay.checkpoint_bonus;
}}
static int packages(void *c){(void)c;return 1+(base&&base->package_count?base->package_count(base->ctx):0);}
static int get_package(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>0)return base&&base->package_get&&base->package_get(base->ctx,i-1,o);
    if(i<0||!o)return 0;
    memset(o,0,sizeof *o);
    COPY(o->id,package);COPY(o->name,"Gameplay options");COPY(o->version,"0.1.0");
    COPY(o->author,"Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,"Optional lives, time limit, native level select, debug controls and checkpoint bonuses.");
    COPY(o->license,"Project license");o->enabled=!s3_gameplay_netplay_allowed();return 1;
}
static int features(void *c){(void)c;return own_features()+(base&&base->feature_count?base->feature_count(base->ctx):0);}
static int get_feature(void *c,int i,RecompLauncherCModFeature *o)
{
    (void)c;if(i>=own_features())return base&&base->feature_get&&base->feature_get(base->ctx,i-own_features(),o);
    if(i<0||!o)return 0;
    memset(o,0,sizeof *o);
    COPY(o->id,ids[i]);COPY(o->package_id,package);COPY(o->package_name,"Gameplay options");
    COPY(o->package_version,"0.1.0");COPY(o->name,names[i]);COPY(o->group,"Gameplay");
    COPY(o->description,descriptions[i]);COPY(o->status,*value(i)?"Enabled — local play":"Disabled");
    o->enabled=*value(i);return 1;
}
static int enable(void *c,const char *p,const char *f,int on)
{
    (void)c;error[0]=0;
    if(!own(p))return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);
    int i=index_of(f);if(i<0)return 0;*value(i)=on!=0;return 1;
}
static int enable_package(void *c,const char *p,int on)
{
    if(!own(p))return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);
    for(int i=0;i<own_features();++i)if(!enable(c,p,ids[i],on))return 0;
    return 1;
}
static int commit(void *c,const char *image)
{
    (void)c;error[0]=0;
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;
    return s3_gameplay_save();
}
static int commit_netplay(void *c,const char *image)
{
    (void)c;error[0]=0;
    if(!s3_gameplay_netplay_allowed()) {
        COPY(error,"Disable gameplay options before netplay");return 0;
    }
    return !base||!base->commit_netplay||base->commit_netplay(base->ctx,image);
}
static const char *last_error(void *c)
{(void)c;return *error?error:base&&base->last_error?base->last_error(base->ctx):"";}
const RecompLauncherCModProvider *s3_gameplay_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;
    if(base)p=*base;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=get_package;p.set_enabled=enable_package;
    p.feature_count=features;p.feature_get=get_feature;p.feature_enable=enable;
    p.commit=commit;p.commit_netplay=commit_netplay;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_gameplay_mods(const struct RecompLauncherCModProvider *common)
{return common;}
#endif
