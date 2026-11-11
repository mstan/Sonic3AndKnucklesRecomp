#include "sonic3_sound_themes.h"
#include "sonic3_sound_driver.h"
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include <stdio.h>
#include <string.h>
#define COPY(d,s) snprintf(d,sizeof(d),"%s",s)
static const RecompLauncherCModProvider *base;
static const char package[]="sonic3.audio.event-themes";
static int ours(const char *p){return p&&!strcmp(p,package);}
static int theme(const char *f){if(f)for(int t=0;t<S3_THEME_COUNT;t++)if(!strcmp(f,s3_theme_ids[t]))return t;return -1;}
static int packages(void *c){(void)c;return 1+(base&&base->package_count?base->package_count(base->ctx):0);}
static int package_get(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>0)return base&&base->package_get&&base->package_get(base->ctx,i-1,o);
    if(i<0||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,package);COPY(o->name,"Event Music");COPY(o->version,"0.1.0");
    COPY(o->author,"Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,"Choose individual event themes from your own Sonic games. Donors are shared across all theme choices. Default off.");
    o->enabled=s3_themes_enabled();return 1;
}
static int features(void *c){(void)c;return S3_THEME_COUNT+(base&&base->feature_count?base->feature_count(base->ctx):0);}
static int feature_get(void *c,int i,RecompLauncherCModFeature *o)
{
    (void)c;if(i>=S3_THEME_COUNT)return base&&base->feature_get&&base->feature_get(base->ctx,i-S3_THEME_COUNT,o);
    if(i<0||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,s3_theme_ids[i]);COPY(o->package_id,package);COPY(o->package_name,"Event Music");COPY(o->package_version,"0.1.0");
    COPY(o->name,s3_theme_names[i]);COPY(o->group,"Sound");COPY(o->author,"Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,"Choose a theme, then provide its ROM if requested. ZIP files are extracted and verified automatically. Each donor is stored once and reused by every theme. Choosing an available theme or importing its ROM enables this option and Clone Driver. Restart to apply; local original campaigns only.");
    o->enabled=s3_themes[i].enabled;o->option_count=1;
    COPY(o->status,!s3_theme_ready(i)?"Choose the matching ROM below to enable this theme":o->enabled?"Enabled; changes apply after restart":"Ready; enable to use this theme");
    const S3ThemeChoice *choice=s3_theme_choice(i,s3_themes[i].choice);
    if(choice && choice->donor==S3_DONOR_PROTO){COPY(o->source_name,"Hidden Palace: November 3, 1993 prototype");COPY(o->source_url,"https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype)");}
    if(choice && choice->donor==S3_DONOR_WAI){COPY(o->source_name,"Hidden Palace: Simon Wai prototype");COPY(o->source_url,"https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Simon_Wai_prototype)");}
    if(choice && choice->donor==S3_DONOR_AUG){COPY(o->source_name,"Hidden Palace: August 21, 1992 prototype");COPY(o->source_url,"https://hiddenpalace.org/Sonic_the_Hedgehog_2_(Aug_21,_1992_prototype)");}
    return 1;
}
static int enable(void *c,const char *p,const char *f,int on)
{(void)c;s3_themes_clear_error();if(ours(p))return theme(f)>=0&&s3_theme_enable((unsigned)theme(f),on);return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);}
static int enable_package(void *c,const char *p,int on)
{
    (void)c;s3_themes_clear_error();if(!ours(p))return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);
    int configured=0;
    for(unsigned t=0;t<S3_THEME_COUNT;t++)if(s3_themes[t].choice){configured=1;if(on&&!s3_theme_ready(t))return s3_theme_enable(t,1);}
    if(on&&!configured)return s3_theme_enable(0,1);
    for(unsigned t=0;t<S3_THEME_COUNT;t++)if((!on||s3_themes[t].choice)&&!s3_theme_enable(t,on))return 0;return 1;
}
static int option_get(void *c,const char *p,const char *f,int i,RecompLauncherCModOption *o)
{
    (void)c;if(!ours(p))return base&&base->feature_option_get&&base->feature_option_get(base->ctx,p,f,i,o);
    int t=theme(f);if(t<0||i||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,"theme");COPY(o->label,s3_theme_names[t]);COPY(o->description,"Choose freely while enabled or disabled. Matching ROMs are shared automatically.");
    COPY(o->value,s3_theme_choice(t,s3_themes[t].choice)->id);COPY(o->default_value,"original");
    o->type=RECOMP_MOD_OPTION_CHOICE;o->choice_count=s3_theme_choices(t);return 1;
}
static int choice_get(void *c,const char *p,const char *f,const char *opt,int i,RecompLauncherCModChoice *o)
{
    (void)c;if(!ours(p))return base&&base->feature_choice_get&&base->feature_choice_get(base->ctx,p,f,opt,i,o);
    int t=theme(f);const S3ThemeChoice *choice=t<0?NULL:s3_theme_choice(t,(unsigned)i);
    if(!opt||strcmp(opt,"theme")||!choice||!o)return 0;memset(o,0,sizeof *o);COPY(o->value,choice->id);COPY(o->label,choice->label);return 1;
}
static int set_option(void *c,const char *p,const char *f,const char *opt,const char *v)
{
    (void)c;s3_themes_clear_error();if(!ours(p))return base&&base->feature_set_option&&base->feature_set_option(base->ctx,p,f,opt,v);
    return theme(f)>=0&&opt&&!strcmp(opt,"theme")&&s3_theme_choose((unsigned)theme(f),v);
}
static int resource_count(void *c,const char *p,const char *f)
{(void)c;if(ours(p))return theme(f)>=0&&s3_theme_donor((unsigned)theme(f))>=0;return base&&base->feature_resource_count?base->feature_resource_count(base->ctx,p,f):0;}
static int resource_get(void *c,const char *p,const char *f,int i,RecompLauncherCModResource *o)
{
    (void)c;if(!ours(p))return base&&base->feature_resource_get&&base->feature_resource_get(base->ctx,p,f,i,o);
    int t=theme(f),d=t<0?-1:s3_theme_donor((unsigned)t);if(d<0||i||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,"donor");snprintf(o->label,sizeof o->label,"%s ROM",s3_donor_names[d]);
    const S3ThemeChoice *choice=s3_theme_choice((unsigned)t,s3_themes[t].choice);
    COPY(o->description,choice&&!strcmp(choice->id,"s1-s2")?"Provide Sonic 1 (JUE rev 0) or Sonic 2 (World rev 1). ZIP or raw ROM; one import covers every compatible theme.":"Provide this unmodified cartridge as a ZIP or raw ROM. Import once; all themes share it automatically.");
    COPY(o->file_patterns,d>=S3_DONOR_CD_US?"*.cue,*.bin,*.sgcd,*.zip":"*.zip,*.bin,*.md,*.gen");
    COPY(o->file_description,d>=S3_DONOR_CD_US?"Sonic CD disc or ZIP":"Genesis ROM or ZIP");
    if(d>=S3_DONOR_CD_US){snprintf(o->label,sizeof o->label,"%s disc",s3_donor_names[d]);COPY(o->description,"Choose your owned disc as a CUE/BIN set, SGCD or ZIP with audio tracks. Import once for Game Over, Act Clear and Invincibility. Data-only ISOs have no CD-DA audio.");}
    COPY(o->path,s3_donor_verified((unsigned)d)?s3_donor_path((unsigned)d):"");
    o->required=1;o->verified=s3_donor_verified((unsigned)d);o->import_on_select=1;
    COPY(o->status,o->verified?"Verified donor; shared with all matching themes":"No matching ROM provided");return 1;
}
static int resource_import(void *c,const char *p,const char *f,const char *r,const char *path,const char *image,
                           void (*progress)(void *,float,const char *),void *user,char *error,size_t capacity)
{
    (void)c;s3_themes_clear_error();if(!ours(p))return base&&base->feature_resource_import&&base->feature_resource_import(base->ctx,p,f,r,path,image,progress,user,error,capacity);
    int ok=theme(f)>=0&&r&&!strcmp(r,"donor")&&s3_theme_import((unsigned)theme(f),path,progress,user);
    if(!ok&&error&&capacity)snprintf(error,capacity,"%s",s3_themes_error());return ok;
}
static int resource_set(void *c,const char *p,const char *f,const char *r,const char *path)
{s3_themes_clear_error();if(ours(p))return resource_import(c,p,f,r,path,"",NULL,NULL,NULL,0);return base&&base->feature_resource_set_path&&base->feature_resource_set_path(base->ctx,p,f,r,path);}
static int commit(void *c,const char *image)
{(void)c;if(!s3_themes_validate())return 0;if(base&&base->commit&&!base->commit(base->ctx,image))return 0;return s3_themes_save();}
static const char *last_error(void *c)
{(void)c;return *s3_themes_error()?s3_themes_error():base&&base->last_error?base->last_error(base->ctx):"";}
const RecompLauncherCModProvider *s3_theme_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;if(base)p=*base;else memset(&p,0,sizeof p);
    p.package_count=packages;p.package_get=package_get;p.set_enabled=enable_package;p.feature_count=features;p.feature_get=feature_get;p.feature_enable=enable;
    p.feature_option_get=option_get;p.feature_choice_get=choice_get;p.feature_set_option=set_option;
    p.feature_resource_count=resource_count;p.feature_resource_get=resource_get;p.feature_resource_set_path=resource_set;p.feature_resource_import=resource_import;
    /* Game-owned selections are not part of the catalog's prepared ticket. */
    p.try_commit=NULL;p.preparation_revision=NULL;p.commit_worker_safe=0;
    p.commit=commit;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_theme_mods(const struct RecompLauncherCModProvider *base){return base;}
#endif
