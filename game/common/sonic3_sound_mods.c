#include "sonic3_sound_driver.h"
const struct RecompLauncherCModProvider *s3_theme_mods(const struct RecompLauncherCModProvider *base);
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include <stdio.h>
#include <string.h>
static const RecompLauncherCModProvider *base;
static const char package[]="sonic3.audio.clone-driver",feature[]="sound-driver",prototype[]="prototype-soundtrack";
static const char saved_soundtrack[]="saved-soundtrack";
static char error[256];
#define COPY(d,s) snprintf(d,sizeof(d),"%s",s)
static int ours(const char *p){return p&&!strcmp(p,package);}
static int packages(void *c){(void)c;return 1+(base&&base->package_count?base->package_count(base->ctx):0);}
static int package_get(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>0)return base&&base->package_get&&base->package_get(base->ctx,i-1,o);
    if(i<0||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,package);COPY(o->name,"Clone Driver + Mega PCM");COPY(o->version,"0.1.0");
    COPY(o->author,"Clone Driver and Mega PCM contributors");
    COPY(o->description,"Play the cartridge's music and sound effects with Clone Driver and Mega PCM. Sound data is prepared automatically from your ROM. Restart after changing. Local play; original campaigns only.");
    COPY(o->source_name,"S3K Clone Driver");COPY(o->source_url,"https://github.com/TheBlad768/skdisasm-clone-driver");
    COPY(o->license,"Driver licenses included in CLONE_DRIVER_NOTICES.txt");
    COPY(o->status,!s3_sound_available()?"Unavailable with this build's Z80 backend":s3_sound_selected()!=s3_sound_active()?"Restart to apply the selection":s3_sound_active()?"Enabled":"Disabled");
    o->enabled=s3_sound_selected();return 1;
}
static int features(void *c){(void)c;return 1+(base&&base->feature_count?base->feature_count(base->ctx):0);}
static int feature_get(void *c,int i,RecompLauncherCModFeature *o)
{
    if(i>0)return base&&base->feature_get&&base->feature_get(base->ctx,i-1,o);
    RecompLauncherCModPackage p;if(i<0||!o||!package_get(c,0,&p))return 0;memset(o,0,sizeof *o);
    COPY(o->id,feature);COPY(o->package_id,package);COPY(o->package_name,p.name);COPY(o->package_version,p.version);
    COPY(o->name,p.name);COPY(o->author,p.author);COPY(o->description,p.description);COPY(o->group,"Sound");
    COPY(o->source_name,p.source_name);COPY(o->source_url,p.source_url);COPY(o->status,p.status);o->enabled=p.enabled;
    o->option_count=s3_sound_prototype_selected()?1:0;
    /* Keep legacy IDs importable for saved profiles and diagnostic replays.
     * New choices belong to the individual-track Music Customization menu. */
    return 1;
}
static int saved_option(const char *f,const char *opt)
{return f&&opt&&!strcmp(f,feature)&&!strcmp(opt,saved_soundtrack);}
static int option_get(void *c,const char *p,const char *f,int i,RecompLauncherCModOption *o)
{
    (void)c;if(!ours(p))return base&&base->feature_option_get&&base->feature_option_get(base->ctx,p,f,i,o);
    if(!f||strcmp(f,feature)||i||!o||!s3_sound_prototype_selected())return 0;
    memset(o,0,sizeof *o);COPY(o->id,saved_soundtrack);COPY(o->label,"Saved default soundtrack");COPY(o->group,"Music");
    COPY(o->description,"Your previously saved November 1993 soundtrack is retained. Choose Base game to restore retail defaults without changing individual track choices or their section switches. Restart to apply.");
    COPY(o->value,"s3-proto");COPY(o->default_value,"base");o->type=RECOMP_MOD_OPTION_CHOICE;o->choice_count=2;return 1;
}
static int choice_get(void *c,const char *p,const char *f,const char *opt,int i,RecompLauncherCModChoice *o)
{
    (void)c;if(!ours(p))return base&&base->feature_choice_get&&base->feature_choice_get(base->ctx,p,f,opt,i,o);
    if(!saved_option(f,opt)||!o||i<0||i>1)return 0;
    memset(o,0,sizeof *o);COPY(o->value,i?"s3-proto":"base");COPY(o->label,i?"November 1993 prototype":"Base game");return 1;
}
static int set_option(void *c,const char *p,const char *f,const char *opt,const char *value)
{
    (void)c;if(!ours(p))return base&&base->feature_set_option&&base->feature_set_option(base->ctx,p,f,opt,value);
    if(!saved_option(f,opt)||!value)return 0;
    if(!strcmp(value,"base"))return s3_sound_prototype_select(0);
    if(!strcmp(value,"s3-proto"))return s3_sound_select(1)&&s3_sound_prototype_select(1);
    return 0;
}
static int enable(void *c,const char *p,const char *f,int on)
{(void)c;*error=0;if(ours(p))return f&&(!strcmp(f,feature)?s3_sound_select(on):!strcmp(f,prototype)?s3_sound_prototype_select(on):0);return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);}
static int enable_package(void *c,const char *p,int on)
{if(ours(p))return enable(c,p,feature,on);return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);}
static int commit(void *c,const char *image)
{
    (void)c;*error=0;if(!s3_sound_select(s3_sound_selected()) || !s3_sound_prototype_select(s3_sound_prototype_selected()))return 0;
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;return s3_sound_commit();
}
static int netplay(void *c,const char *image)
{
    (void)c;if(!s3_sound_netplay_allowed()){COPY(error,"Disable Clone Driver and restart before netplay.");return 0;}
    return !base||!base->commit_netplay||base->commit_netplay(base->ctx,image);
}
static const char *last_error(void *c)
{(void)c;return *error?error:*s3_sound_error()?s3_sound_error():base&&base->last_error?base->last_error(base->ctx):"";}
static int resource_count(void *c,const char *p,const char *f)
{
    (void)c;if(ours(p))return f&&(!strcmp(f,prototype)||(!strcmp(f,feature)&&s3_sound_prototype_selected()));
    return base&&base->feature_resource_count?base->feature_resource_count(base->ctx,p,f):0;
}
static int resource_get(void *c,const char *p,const char *f,int i,RecompLauncherCModResource *o)
{
    (void)c;
    if(!ours(p))return base&&base->feature_resource_get&&base->feature_resource_get(base->ctx,p,f,i,o);
    if(!f || (strcmp(f,prototype)&&(strcmp(f,feature)||!s3_sound_prototype_selected())) || i || !o)return 0;
    memset(o,0,sizeof *o);COPY(o->id,"prototype-rom");COPY(o->label,"November 3, 1993 prototype ROM");
    COPY(o->description,"Sonic the Hedgehog 3 (Nov 3, 1993 prototype), 2,097,152 bytes. SHA-1 fc87a7b411302a576028b7ccd5911b1f266b59b3. Extract ROM archives first. The game verifies the donor again at startup.");
    COPY(o->file_patterns,"*.bin,*.md,*.gen,*.68k,*");COPY(o->file_description,"Genesis ROM");
    COPY(o->path,s3_sound_prototype_path());o->required=1;o->verified=s3_sound_prototype_verified();
    COPY(o->status,o->verified?"Verified November 3, 1993 prototype":"Choose your unmodified donor ROM");return 1;
}
static int resource_path(void *c,const char *p,const char *f,const char *r,const char *path)
{
    (void)c;
    /* Replay imports resources before options, so accept this saved resource
     * on the driver even before its soundtrack option has been restored. */
    if(ours(p))return f&&(!strcmp(f,prototype)||!strcmp(f,feature))&&r&&!strcmp(r,"prototype-rom")&&s3_sound_prototype_set_path(path);
    return base&&base->feature_resource_set_path&&base->feature_resource_set_path(base->ctx,p,f,r,path);
}
const RecompLauncherCModProvider *s3_sound_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;if(base)p=*base;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=package_get;p.set_enabled=enable_package;p.feature_count=features;p.feature_get=feature_get;p.feature_enable=enable;
    p.feature_option_get=option_get;p.feature_choice_get=choice_get;p.feature_set_option=set_option;
    p.feature_resource_count=resource_count;p.feature_resource_get=resource_get;p.feature_resource_set_path=resource_path;
    p.commit=commit;p.commit_netplay=netplay;p.last_error=last_error;return s3_theme_mods(&p);
}
#else
const struct RecompLauncherCModProvider *s3_sound_mods(const struct RecompLauncherCModProvider *base){return base;}
#endif
