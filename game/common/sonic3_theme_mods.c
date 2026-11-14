#include "sonic3_sound_themes.h"
#include "sonic3_sound_driver.h"
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include <stdio.h>
#include <string.h>
#define COPY(d,s) snprintf(d,sizeof(d),"%s",s)
static const RecompLauncherCModProvider *base;
static const char package[]="sonic3.audio.event-themes";
static const char *const group_ids[]={"shared-themes","sonic3-campaign","sandk-campaign"};
static const char *const group_names[]={"Shared Themes","Sonic 3 Campaign","Sonic & Knuckles Campaign"};
static int combined(void){return s3_music_available(S3_MUSIC_AIZ1)&&s3_music_available(S3_MUSIC_MHZ1);}
static const char *group_name(int g){return g==S3_MUSIC_SONIC3&&combined()?"Sonic 3 & Knuckles Campaign":group_names[g];}
static const char *group_key(int g){return g==S3_MUSIC_SONIC3&&combined()?"sonic3k-campaign":group_ids[g];}
static int ours(const char *p){return p&&!strcmp(p,package);}
static int group_id(const char *id)
{if(id)for(int g=0;g<S3_MUSIC_GROUPS;g++)if((g==0 || (g==1?s3_music_available(S3_MUSIC_AIZ1):s3_music_available(S3_MUSIC_MHZ1)&&!combined()))&&!strcmp(id,group_key(g)))return g;return -1;}
static int track_id(const char *id){if(id)for(int t=0;t<S3_MUSIC_COUNT;t++)if(!strcmp(id,s3_theme_ids[t]))return t;return -1;}
static int visible_choice(unsigned t,unsigned c)
{
    const S3ThemeChoice *choice=s3_theme_choice(t,c);if(!choice)return 0;
    const char *id=choice->id;
    int s3=!s3_music_available(S3_MUSIC_MHZ1);
    const char *host=s3?"s3":"sk";
    int retail=0;
    switch(t){
    case S3_THEME_LIFE:case S3_THEME_CONTINUE:case S3_THEME_INVINCIBLE:
    case S3_MUSIC_DATA:retail=!strcmp(id,host);break;
    case S3_THEME_OVER:case S3_THEME_CLEAR:retail=!strcmp(id,"s3k");break;
    case S3_MUSIC_BOSS:case S3_MUSIC_COMPETITION:retail=!strcmp(id,"s3");break;
    case S3_MUSIC_FINAL:retail=!strcmp(id,"s3")||!strcmp(id,"sk");break;
    case S3_MUSIC_KNUCKLES:retail=s3&&!strcmp(id,"s3");break;
    default:break;
    }
    if(!retail)return 1;
    /* Keep an explicit saved override selectable. A full prototype
     * soundtrack also needs one retail restoration choice per track. */
    if(s3_themes[t].enabled && s3_themes[t].choice==(int)c)return 1;
    return s3_sound_prototype_selected() &&
        (t!=S3_MUSIC_FINAL || !strcmp(id,host));
}
static int choice_at(unsigned t,int index)
{for(unsigned c=0;c<(unsigned)s3_theme_choices(t);++c)if(visible_choice(t,c)&&index--==0)return (int)c;return -1;}
static int track_at(int group,int index)
{
    static const char *const common[]={"Events","Bosses","Menus","Other music"};
    static const char *const campaign[]={"Zones","Competition","Menus","Bonus stages"};
    const char *const *sections=group==S3_MUSIC_SHARED?common:campaign;
    for(unsigned s=0;s<4;s++)for(unsigned t=0;t<S3_MUSIC_COUNT;t++)
        if(s3_music_group(t)==(unsigned)group&&s3_music_available(t)&&
           (t!=S3_MUSIC_SUPER||s3_themes[t].enabled)&&
           !strcmp(sections[s],s3_music_section(t))&&index--==0)return (int)t;
    return -1;
}
static int group_at(int index)
{for(int g=0;g<S3_MUSIC_GROUPS;g++)if(track_at(g,0)>=0&&index--==0)return g;return -1;}
static int group_count(void){int count=0;while(group_at(count)>=0)++count;return count;}
static int selected(int group)
{for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_available(t)&&s3_music_group(t)==(unsigned)group&&s3_themes[t].enabled)return 1;return 0;}
static int requirement(int group,int index,int *track)
{
    int seen[S3_DONOR_COUNT]={0};
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_group(t)==(unsigned)group&&s3_music_available(t)&&s3_themes[t].enabled){
        int d=s3_theme_donor(t);if(d>=0&&(d==S3_DONOR_CUSTOM||!seen[d]++)&&index--==0){if(track)*track=(int)t;return d;}
    }
    return -1;
}
static int packages(void *c){(void)c;return 1+(base&&base->package_count?base->package_count(base->ctx):0);}
static int package_get(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>0)return base&&base->package_get&&base->package_get(base->ctx,i-1,o);
    if(i<0||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,package);COPY(o->name,"Music Customization");COPY(o->version,"0.2.0");
    COPY(o->author,"Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,"Choose individual tracks in Shared Themes and this game's campaign section. Matching ROMs are imported once and shared across all choices and games. All tracks default to Base game.");
    o->enabled=s3_themes_enabled();return 1;
}
static int features(void *c){(void)c;return group_count()+(base&&base->feature_count?base->feature_count(base->ctx):0);}
static int feature_get(void *c,int i,RecompLauncherCModFeature *o)
{
    (void)c;int count=group_count();if(i>=count)return base&&base->feature_get&&base->feature_get(base->ctx,i-count,o);
    int g=group_at(i);if(g<0||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,group_key(g));COPY(o->package_id,package);COPY(o->package_name,"Music Customization");COPY(o->package_version,"0.2.0");
    COPY(o->name,group_name(g));COPY(o->group,"Sound");COPY(o->author,"Sonic3AndKnucklesRecomp contributors");
    COPY(o->description,"Choose each track independently. Base game leaves that track unchanged. Only ROMs needed by your selected replacements appear below, and each is shared automatically. Choosing a ready replacement or importing its ROM enables this section and Clone Driver. Disable the section to restore all its normal tracks while keeping your choices. Restart to apply; local original campaigns only.");
    o->enabled=s3_music_group_enabled((unsigned)g);
    while(track_at(g,o->option_count)>=0)++o->option_count;
    int ready=1;for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_group(t)==(unsigned)g&&s3_music_available(t)&&s3_themes[t].enabled&&!s3_theme_ready(t))ready=0;
    COPY(o->status,!ready?"Provide the matching ROMs below to enable this section":o->enabled?"Enabled; changes apply after restart":selected(g)?"Ready; enable to use the selected replacements":"All tracks use Base game");
    COPY(o->source_name,"Donor ROM details and prototype sources");COPY(o->source_url,"https://github.com/mstan/Sonic3AndKnucklesRecomp/blob/main/docs/EVENT_MUSIC.md");
    return 1;
}
static int enable(void *c,const char *p,const char *f,int on)
{(void)c;s3_themes_clear_error();if(ours(p))return group_id(f)>=0&&s3_music_group_enable((unsigned)group_id(f),on);return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);}
static int enable_package(void *c,const char *p,int on)
{
    (void)c;s3_themes_clear_error();if(!ours(p))return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);
    int any=0;for(int g=0;g<S3_MUSIC_GROUPS;g++)if(!on||selected(g)){any=1;if(!s3_music_group_enable((unsigned)g,on))return 0;}
    return any?1:s3_music_group_enable(0,1);
}
static int option_get(void *c,const char *p,const char *f,int i,RecompLauncherCModOption *o)
{
    (void)c;if(!ours(p))return base&&base->feature_option_get&&base->feature_option_get(base->ctx,p,f,i,o);
    int t=track_at(group_id(f),i);if(t<0||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,s3_theme_ids[t]);COPY(o->label,s3_theme_names[t]);COPY(o->group,s3_music_section((unsigned)t));
    COPY(o->description,"Base game keeps the current soundtrack. Matching donor ROMs are shared automatically.");
    COPY(o->value,s3_themes[t].enabled?s3_theme_choice((unsigned)t,(unsigned)s3_themes[t].choice)->id:"base");COPY(o->default_value,"base");
    o->type=RECOMP_MOD_OPTION_CHOICE;o->choice_count=1;while(choice_at((unsigned)t,o->choice_count-1)>=0)++o->choice_count;return 1;
}
static int choice_get(void *c,const char *p,const char *f,const char *opt,int i,RecompLauncherCModChoice *o)
{
    (void)c;if(!ours(p))return base&&base->feature_choice_get&&base->feature_choice_get(base->ctx,p,f,opt,i,o);
    int t=track_id(opt),g=group_id(f);if(t<0||g<0||s3_music_group((unsigned)t)!=(unsigned)g||!s3_music_available((unsigned)t)||!o)return 0;
    memset(o,0,sizeof *o);if(!i){COPY(o->value,"base");COPY(o->label,"Base game");return 1;}
    int selected_choice=choice_at((unsigned)t,i-1);const S3ThemeChoice *choice=selected_choice<0?NULL:s3_theme_choice((unsigned)t,(unsigned)selected_choice);if(!choice)return 0;COPY(o->value,choice->id);COPY(o->label,t==S3_MUSIC_SUPER?"Unused prototype theme":choice->label);return 1;
}
static int set_option(void *c,const char *p,const char *f,const char *opt,const char *v)
{
    (void)c;s3_themes_clear_error();if(!ours(p))return base&&base->feature_set_option&&base->feature_set_option(base->ctx,p,f,opt,v);
    int t=track_id(opt),g=group_id(f);return t>=0&&g>=0&&s3_music_group((unsigned)t)==(unsigned)g&&s3_music_pick((unsigned)t,v);
}
static int resource_count(void *c,const char *p,const char *f)
{
    (void)c;if(!ours(p))return base&&base->feature_resource_count?base->feature_resource_count(base->ctx,p,f):0;
    int count=0;while(requirement(group_id(f),count,NULL)>=0)++count;return count;
}
static int resource_get(void *c,const char *p,const char *f,int i,RecompLauncherCModResource *o)
{
    (void)c;if(!ours(p))return base&&base->feature_resource_get&&base->feature_resource_get(base->ctx,p,f,i,o);
    int t,d=requirement(group_id(f),i,&t);if(d<0||!o)return 0;memset(o,0,sizeof *o);
    if(d==S3_DONOR_CUSTOM){
        snprintf(o->id,sizeof o->id,"custom-%s",s3_theme_ids[t]);snprintf(o->label,sizeof o->label,"%s: custom music",s3_theme_names[t]);
        COPY(o->description,t==S3_THEME_LIFE?"Import a finite S3 SMPS jingle that restores the previous music.":"Import S3 Z80 SMPS data, Genesis VGM/VGZ or a PCM WAV recording. Your selection is saved for this game.");
        COPY(o->file_patterns,t==S3_THEME_LIFE?"*.bin,*.asm,*.ini":"*.bin,*.asm,*.vgm,*.vgz,*.wav,*.ini");COPY(o->file_description,"Custom music");COPY(o->path,s3_custom_path((unsigned)t));
        o->required=1;o->verified=s3_custom_ready((unsigned)t);o->import_on_select=1;
        COPY(o->status,o->verified?s3_custom_name((unsigned)t):"Choose a custom music file");return 1;
    }
    COPY(o->id,s3_donor_ids[d]);snprintf(o->label,sizeof o->label,"%s %s",s3_donor_names[d],d>=S3_DONOR_CD_US?"disc":"ROM");
    const S3ThemeChoice *choice=s3_theme_choice((unsigned)t,(unsigned)s3_themes[t].choice);
    COPY(o->description,!strcmp(choice->id,"s1-s2")?"Sonic 1 (JUE rev 0) or Sonic 2 (World rev 1). ZIP or raw ROM; one import covers every compatible choice.":"Provide this unmodified cartridge as a ZIP or raw ROM. Import once; every matching track and game shares it.");
    COPY(o->file_patterns,d>=S3_DONOR_CD_US?"*.cue,*.bin,*.sgcd,*.zip":"*.zip,*.bin,*.md,*.gen,*.68k,*");
    COPY(o->file_description,d>=S3_DONOR_CD_US?"Sonic CD disc or ZIP":"Genesis ROM or ZIP");
    if(d>=S3_DONOR_CD_US)COPY(o->description,"Choose your US disc as a CUE/BIN set, SGCD or ZIP with audio tracks. One import covers the event and boss themes. Import again to extend an older library.");
    COPY(o->path,s3_donor_verified((unsigned)d)?s3_donor_path((unsigned)d):"");
    o->required=1;o->verified=s3_donor_verified((unsigned)d);o->import_on_select=1;
    for(unsigned track=0;track<S3_MUSIC_COUNT;track++)
        if(s3_music_group(track)==(unsigned)group_id(f)&&s3_music_available(track)&&s3_themes[track].enabled&&s3_theme_donor(track)==d&&!s3_theme_ready(track))o->verified=0;
    COPY(o->status,o->verified?"Verified; shared with all matching tracks":
         d>=S3_DONOR_CD_US&&s3_donor_verified((unsigned)d)?"Import the disc again to add the selected tracks":"No matching donor provided");return 1;
}
static int resource_import(void *c,const char *p,const char *f,const char *r,const char *path,const char *image,
                           void (*progress)(void *,float,const char *),void *user,char *error,size_t capacity)
{
    (void)c;s3_themes_clear_error();if(!ours(p))return base&&base->feature_resource_import&&base->feature_resource_import(base->ctx,p,f,r,path,image,progress,user,error,capacity);
    int ok=0,g=group_id(f);
    if(r&&!strncmp(r,"custom-",7)){
        int t=track_id(r+7);ok=t>=0&&s3_music_available((unsigned)t)&&s3_music_group((unsigned)t)==(unsigned)g&&s3_custom_import((unsigned)t,path,progress,user);
        if(ok&&s3_music_custom((unsigned)t))ok=s3_theme_enable((unsigned)t,1);
        if(!ok&&error&&capacity)snprintf(error,capacity,"%s",s3_themes_error());return ok;
    }
    if(r)for(int i=0;;i++){
        int t,d=requirement(g,i,&t);if(d<0)break;
        if(!strcmp(r,s3_donor_ids[d])){ok=s3_theme_import((unsigned)t,path,progress,user);break;}
    }
    if(!ok&&error&&capacity)snprintf(error,capacity,"%s",s3_themes_error());return ok;
}
static int resource_set(void *c,const char *p,const char *f,const char *r,const char *path)
{
    s3_themes_clear_error();
    if(ours(p)){
        if(r&&!strncmp(r,"custom-",7)){
            int t=track_id(r+7),g=group_id(f);
            return t>=0&&g>=0&&s3_music_available((unsigned)t)&&s3_music_group((unsigned)t)==(unsigned)g&&s3_custom_import((unsigned)t,path,NULL,NULL);
        }
        /* Replay restores resources before the selected options. A verified
         * shared-library entry is valid even while this section is disabled. */
        if(group_id(f)>=0&&r&&path)for(unsigned d=0;d<S3_DONOR_COUNT;d++)
            if(!strcmp(r,s3_donor_ids[d])&&s3_donor_verified(d)&&!strcmp(path,s3_donor_path(d)))return 1;
        return resource_import(c,p,f,r,path,"",NULL,NULL,NULL,0);
    }
    return base&&base->feature_resource_set_path&&base->feature_resource_set_path(base->ctx,p,f,r,path);
}
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
    p.try_commit=NULL;p.preparation_revision=NULL;p.commit_worker_safe=0;
    p.commit=commit;p.last_error=last_error;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_theme_mods(const struct RecompLauncherCModProvider *base){return base;}
#endif
