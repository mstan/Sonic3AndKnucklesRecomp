#include "sonic3_sound_driver.h"
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include <stdio.h>
#include <string.h>
static const RecompLauncherCModProvider *base;
static const char package[]="sonic3.audio.clone-driver",feature[]="sound-driver";
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
    COPY(o->name,p.name);COPY(o->author,p.author);COPY(o->description,p.description);COPY(o->group,"Audio");
    COPY(o->source_name,p.source_name);COPY(o->source_url,p.source_url);COPY(o->status,p.status);o->enabled=p.enabled;return 1;
}
static int enable(void *c,const char *p,const char *f,int on)
{(void)c;*error=0;if(ours(p))return f&&!strcmp(f,feature)&&s3_sound_select(on);return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);}
static int enable_package(void *c,const char *p,int on)
{if(ours(p))return enable(c,p,feature,on);return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);}
static int commit(void *c,const char *image)
{
    (void)c;*error=0;if(!s3_sound_select(s3_sound_selected()))return 0;
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;return s3_sound_commit();
}
static int netplay(void *c,const char *image)
{
    (void)c;if(!s3_sound_netplay_allowed()){COPY(error,"Disable Clone Driver and restart before netplay.");return 0;}
    return !base||!base->commit_netplay||base->commit_netplay(base->ctx,image);
}
static const char *last_error(void *c)
{(void)c;return *error?error:*s3_sound_error()?s3_sound_error():base&&base->last_error?base->last_error(base->ctx):"";}
const RecompLauncherCModProvider *s3_sound_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;if(base)p=*base;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=package_get;p.set_enabled=enable_package;p.feature_count=features;p.feature_get=feature_get;p.feature_enable=enable;
    p.commit=commit;p.commit_netplay=netplay;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_sound_mods(const struct RecompLauncherCModProvider *base){return base;}
#endif
