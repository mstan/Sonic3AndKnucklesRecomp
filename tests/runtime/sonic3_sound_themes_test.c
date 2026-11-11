#include "sonic3_sound_themes.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
const RecompLauncherCModProvider *s3_theme_mods(const RecompLauncherCModProvider *base);
static int driver;
int s3_sound_select(int on){driver=on!=0;return 1;}
const char *s3_sound_error(void){return "";}
const char *s3_sound_prototype_path(void){return "";}
void audio_set_extra_source(void (*source)(int16_t *,size_t,double)){(void)source;}
int main(int argc,char **argv)
{
    CHECK(argc==3||argc==4); /* isolated settings path and cartridge mode */
    s3_themes_load(argv[1],argv[2]);
    const RecompLauncherCModProvider *p=s3_theme_mods(NULL);CHECK(p&&p->feature_count(NULL)==5&&p->package_count(NULL)==1);
    unsigned total=0;
    for(unsigned t=0;t<S3_THEME_COUNT;t++){
        RecompLauncherCModFeature f;RecompLauncherCModOption o;
        CHECK(p->feature_get(NULL,(int)t,&f)&&!f.enabled&&!strcmp(f.group,"Sound"));
        CHECK(p->feature_option_get(NULL,f.package_id,f.id,0,&o));CHECK(o.type==RECOMP_MOD_OPTION_CHOICE&&!o.disabled);total+=(unsigned)o.choice_count-1;
    }
    CHECK(total==21);
    if(argc==4){
        CHECK(s3_theme_choose(S3_THEME_LIFE,"s1-s2"));CHECK(!s3_themes[S3_THEME_LIFE].enabled&&!driver);
        CHECK(!s3_theme_enable(S3_THEME_LIFE,1));CHECK(!strcmp(s3_theme_choice(S3_THEME_LIFE,(unsigned)s3_themes[S3_THEME_LIFE].choice)->id,"s1-s2"));
        puts("PASS missing-donor selection remains configurable and disabled");return 0;
    }
    CHECK(s3_theme_choose(S3_THEME_LIFE,"s1-s2"));CHECK(s3_theme_ready(S3_THEME_LIFE)&&s3_themes[S3_THEME_LIFE].enabled&&driver);
    CHECK(s3_theme_choose(S3_THEME_CONTINUE,"s1-s2"));CHECK(s3_themes[S3_THEME_CONTINUE].enabled);
    CHECK(s3_theme_donor(S3_THEME_LIFE)==s3_theme_donor(S3_THEME_CONTINUE));
    RecompLauncherCModResource a,b;
    CHECK(p->feature_resource_get(NULL,"sonic3.audio.event-themes","extra-life",0,&a));
    CHECK(p->feature_resource_get(NULL,"sonic3.audio.event-themes","continue",0,&b));
    CHECK(a.verified&&b.verified&&!strcmp(a.path,b.path)&&a.import_on_select);
    CHECK(s3_theme_enable(S3_THEME_CONTINUE,0));
    CHECK(p->feature_set_option(NULL,"sonic3.audio.event-themes","continue","theme","s3"));
    CHECK(s3_themes[S3_THEME_CONTINUE].enabled&&s3_theme_ready(S3_THEME_CONTINUE));
    CHECK(s3_theme_choose(S3_THEME_INVINCIBLE,"cd-us"));CHECK(s3_themes[S3_THEME_INVINCIBLE].enabled);
    CHECK(s3_theme_choose(S3_THEME_OVER,"cd-us"));CHECK(s3_themes[S3_THEME_OVER].enabled);
    CHECK(s3_theme_donor(S3_THEME_OVER)==s3_theme_donor(S3_THEME_INVINCIBLE));
    CHECK(s3_theme_choose(S3_THEME_OVER,"original"));CHECK(!s3_themes[S3_THEME_OVER].enabled);
    CHECK(p->set_enabled(NULL,"sonic3.audio.event-themes",0));CHECK(!s3_themes_enabled());
    CHECK(p->set_enabled(NULL,"sonic3.audio.event-themes",1));CHECK(s3_themes[S3_THEME_LIFE].enabled&&s3_themes[S3_THEME_INVINCIBLE].enabled&&!s3_themes[S3_THEME_OVER].enabled&&!s3_themes[S3_THEME_CLEAR].enabled);
    CHECK(!s3_theme_choose(S3_THEME_OVER,"typo")&&!s3_theme_choose(99,"s1"));
    CHECK(p->commit(NULL,"unused-rom"));
    s3_themes_load(argv[1],argv[2]);CHECK(s3_themes[S3_THEME_LIFE].enabled&&s3_themes[S3_THEME_INVINCIBLE].enabled&&!s3_themes[S3_THEME_OVER].enabled);
    puts("PASS five configurable Sound features, 21 choices, shared cartridge/disc resources, disabled selection, donor gating, auto-enable and persisted round-trip");return 0;
}
