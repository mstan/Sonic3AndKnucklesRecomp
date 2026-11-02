#include "sonic3_gameplay.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
S3GameplayConfig s3_gameplay;
static char path[1024],error[256];
void s3_gameplay_load(const char *settings_path,const char *mode)
{
    memset(&s3_gameplay,0,sizeof s3_gameplay);
    snprintf(path,sizeof path,"%s",settings_path?settings_path:"settings.ini");
    char *a=strrchr(path,'/'),*b=strrchr(path,'\\');
    char *name=a&&(!b||a>b)?a+1:b?b+1:path;
    snprintf(name,sizeof path-(size_t)(name-path),"%s-gameplay.ini",mode);
    FILE *f=fopen(path,"rb");if(!f)return;
    char line[128];
    while(fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"infinite_lives=1"))s3_gameplay.infinite_lives=1;
        if(!strcmp(line,"infinite_time=1"))s3_gameplay.infinite_time=1;
    }
    fclose(f);
}
int s3_gameplay_save(void)
{
    char temp[1040];snprintf(temp,sizeof temp,"%s.tmp",path);
    FILE *f=fopen(temp,"wb");if(!f)goto failed;
    fprintf(f,"infinite_lives=%d\ninfinite_time=%d\n",
        s3_gameplay.infinite_lives,s3_gameplay.infinite_time);
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
{ return !s3_gameplay.infinite_lives && !s3_gameplay.infinite_time; }
void s3_gameplay_apply(uint8_t ram[65536])
{
    /* All three cartridges share these variables. Exclude attract demos,
     * menus, bonus stages and Competition. The normal game owns death/retry,
     * score awards, timer progression, checkpoints and act transitions. */
    if(!ram || ram[0xF600]!=12 || ram[0xFFE8] || ram[0xFFE9])return;
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
static const char *ids[]={"infinite-lives","infinite-time"};
static const char *names[]={"Infinite lives","Infinite time"};
static const char *descriptions[]={
    "Keep at least three lives, including after death and retry. Local play only.",
    "Disable the stage time limit. The clock runs normally to 9:59, then stays there. Local play only."};
#define COPY(d,s) snprintf(d,sizeof(d),"%s",s)
static int own(const char *p){return p&&!strcmp(p,package);}
static int index_of(const char *f){for(int i=0;i<2;++i)if(f&&!strcmp(f,ids[i]))return i;return -1;}
static int *value(int i){return i?&s3_gameplay.infinite_time:&s3_gameplay.infinite_lives;}
static int packages(void *c){(void)c;return 1+(base&&base->package_count?base->package_count(base->ctx):0);}
static int get_package(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>0)return base&&base->package_get&&base->package_get(base->ctx,i-1,o);
    if(i<0||!o)return 0;
    memset(o,0,sizeof *o);
    COPY(o->id,package);COPY(o->name,"Gameplay options");COPY(o->version,"0.1.0");
    COPY(o->author,"Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,"Optional changes to lives and the stage time limit.");
    COPY(o->license,"Project license");o->enabled=!s3_gameplay_netplay_allowed();return 1;
}
static int features(void *c){(void)c;return 2+(base&&base->feature_count?base->feature_count(base->ctx):0);}
static int get_feature(void *c,int i,RecompLauncherCModFeature *o)
{
    (void)c;if(i>=2)return base&&base->feature_get&&base->feature_get(base->ctx,i-2,o);
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
    return enable(c,p,ids[0],on)&&enable(c,p,ids[1],on);
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
