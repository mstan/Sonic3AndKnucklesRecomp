#include "sonic3_sound_themes.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
const RecompLauncherCModProvider *s3_theme_mods(const RecompLauncherCModProvider *base);
static const char package[]="sonic3.audio.event-themes";
static int driver;
int s3_sound_select(int on){driver=on!=0;return 1;}
const char *s3_sound_error(void){return "";}
const char *s3_sound_prototype_path(void){return "";}
void audio_set_extra_source(void (*source)(int16_t *,size_t,double)){(void)source;}
static int set(const RecompLauncherCModProvider *p,const char *group,const char *track,const char *value)
{return p->feature_set_option(NULL,package,group,track,value);}
static void value(const RecompLauncherCModProvider *p,const char *group,const char *track,const char *expected)
{
    for(int i=0;i<S3_MUSIC_COUNT;i++){
        RecompLauncherCModOption o;if(!p->feature_option_get(NULL,package,group,i,&o))break;
        if(!strcmp(o.id,track)){CHECK(!strcmp(o.value,expected));return;}
    }
    CHECK(0);
}
int main(int argc,char **argv)
{
    CHECK(argc==3||argc==4);s3_themes_load(argv[1],argv[2]);
    CHECK(s3_music_source(S3_MUSIC_BOSS,S3_DONOR_WAI)==13);
    CHECK(s3_music_source(S3_MUSIC_MINIBOSS,S3_DONOR_WAI)==13);
    CHECK(s3_music_source(S3_MUSIC_FINAL,S3_DONOR_WAI)==11);
    CHECK(s3_music_source(S3_MUSIC_FINAL,S3_DONOR_S1)==12);
    CHECK(s3_music_source(S3_MUSIC_FINAL,S3_DONOR_S2)==19);
    const RecompLauncherCModProvider *p=s3_theme_mods(NULL);int combined=!strcmp(argv[2],"sonic3k");
    const char *campaign=combined?"sonic3k-campaign":!strcmp(argv[2],"sonic3")?"sonic3-campaign":"sandk-campaign";
    CHECK(p&&p->feature_count(NULL)==2&&p->package_count(NULL)==1);
    unsigned total=0,shared=0;
    for(int i=0;i<p->feature_count(NULL);i++){
        RecompLauncherCModFeature f;CHECK(p->feature_get(NULL,i,&f)&&!f.enabled&&!strcmp(f.group,"Sound"));
        CHECK(!strcmp(f.id,i?campaign:"shared-themes"));
        if(combined&&i)CHECK(!strcmp(f.name,"Sonic 3 & Knuckles Campaign"));
        CHECK(!p->feature_resource_count(NULL,package,f.id));
        for(int j=0;j<f.option_count;j++){
            RecompLauncherCModOption o;RecompLauncherCModChoice choice;
            CHECK(p->feature_option_get(NULL,package,f.id,j,&o));CHECK(o.type==RECOMP_MOD_OPTION_CHOICE&&!o.disabled);
            CHECK(!strcmp(o.value,"base")&&!strcmp(o.default_value,"base"));
            CHECK(p->feature_choice_get(NULL,package,f.id,o.id,0,&choice)&&!strcmp(choice.value,"base"));
            for(int k=1;k<o.choice_count;k++){
                CHECK(p->feature_choice_get(NULL,package,f.id,o.id,k,&choice));CHECK(strcmp(choice.value,"original"));
                if(strcmp(f.id,"shared-themes"))CHECK(!strcmp(choice.value,"s3-proto")||(!strcmp(o.id,"competition-menu")&&!strcmp(choice.value,"s3")));
            }
            if(strcmp(f.id,"shared-themes"))CHECK(o.choice_count==(!strcmp(o.id,"competition-menu")?3:2));else ++shared;
            ++total;
        }
    }
    CHECK(total==(combined?47:!strcmp(argv[2],"sonic3")?34:27));CHECK(shared==(!strcmp(argv[2],"sonic3")?14:13));
    CHECK(s3_music_available(S3_MUSIC_FINAL)&&!s3_music_available(S3_MUSIC_EMERALD));
    CHECK(s3_theme_choices(S3_MUSIC_MINIBOSS)==6&&s3_theme_choices(S3_MUSIC_BOSS)==5);
    CHECK(s3_theme_choices(S3_MUSIC_KNUCKLES)==2);
    if(s3_donor_verified(S3_DONOR_PROTO)){
        CHECK(p->feature_resource_set_path(NULL,package,campaign,"s3-proto",s3_donor_path(S3_DONOR_PROTO)));
        CHECK(!s3_themes_enabled());
    }
    CHECK(!p->set_enabled(NULL,package,1));
    if(argc==4&&!strcmp(argv[3],"--proto-only")){
        CHECK(set(p,"shared-themes","mini-boss","sk-proto")&&s3_music_active(S3_MUSIC_MINIBOSS)&&driver);
        CHECK(s3_music_donor(S3_MUSIC_MINIBOSS)==(!strcmp(argv[2],"sonic3")?S3_DONOR_PROTO:S3_DONOR_SK));
        puts("PASS mini-boss accepts the prototype donor or the host S&K cartridge");return 0;
    }
    if(argc==4){
        CHECK(set(p,"shared-themes","boss","s2-wai"));CHECK(!s3_themes_enabled()&&!driver);
        value(p,"shared-themes","boss","s2-wai");CHECK(!p->feature_enable(NULL,package,"shared-themes",1));
        CHECK(p->feature_resource_count(NULL,package,"shared-themes")==1);
        RecompLauncherCModResource r;CHECK(p->feature_resource_get(NULL,package,"shared-themes",0,&r)&&!r.verified&&!strcmp(r.id,"s2-wai"));
        CHECK(set(p,"shared-themes","boss","base")&&!p->feature_resource_count(NULL,package,"shared-themes"));
        puts("PASS missing donor retains selection, hides irrelevant pickers and prevents group enable");return 0;
    }
    CHECK(set(p,"shared-themes","extra-life","s1-s2")&&s3_music_active(S3_THEME_LIFE)&&driver);
    CHECK(set(p,"shared-themes","continue","s1-s2"));CHECK(p->feature_resource_count(NULL,package,"shared-themes")==1);
    CHECK(set(p,"shared-themes","boss","s2"));CHECK(set(p,"shared-themes","mini-boss","s2-wai"));
    CHECK(s3_music_donor(S3_MUSIC_BOSS)==S3_DONOR_S2&&s3_music_donor(S3_MUSIC_MINIBOSS)==S3_DONOR_WAI);
    CHECK(p->feature_resource_count(NULL,package,"shared-themes")==3);
    CHECK(set(p,"shared-themes","title","s3"));
    CHECK(set(p,"shared-themes","knuckles","s3"));
    CHECK(set(p,"shared-themes","final-boss","s2-wai")&&s3_music_active(S3_MUSIC_FINAL));
    CHECK(set(p,"shared-themes","final-boss","base"));
    CHECK(set(p,"shared-themes","credits","s3"));
    CHECK(s3_music_target(S3_MUSIC_CREDITS)==(!strcmp(argv[2],"sonic3")?0x26u:0x33u));
    CHECK(s3_music_source(S3_MUSIC_CREDITS,S3_DONOR_S3)==0x26&&s3_music_source(S3_MUSIC_CREDITS,S3_DONOR_SK)==0x33);
    CHECK(p->feature_enable(NULL,package,"shared-themes",0)&&!s3_themes_enabled());
    value(p,"shared-themes","mini-boss","s2-wai");value(p,"shared-themes","boss","s2");
    CHECK(p->commit(NULL,"unused-rom"));s3_themes_load(argv[1],argv[2]);CHECK(!s3_themes_enabled());
    value(p,"shared-themes","mini-boss","s2-wai");
    CHECK(p->feature_enable(NULL,package,"shared-themes",1)&&s3_music_active(S3_MUSIC_MINIBOSS));
    if(strcmp(argv[2],"sandk")){
        CHECK(set(p,campaign,"competition-menu","s3"));
        CHECK(set(p,campaign,"competition-menu","s3-proto"));
        CHECK(set(p,campaign,"cnz-1","s3-proto"));CHECK(s3_music_active(S3_MUSIC_CNZ1)&&!s3_music_active(S3_MUSIC_CNZ2));
        value(p,campaign,"cnz-2","base");
        CHECK(!set(p,campaign,"cnz-1","s1")&&!set(p,"shared-themes","cnz-1","s3-proto"));
        CHECK(set(p,campaign,"cnz-2","s3-proto")&&set(p,campaign,"cnz-1","base"));
        CHECK(!s3_music_active(S3_MUSIC_CNZ1)&&s3_music_active(S3_MUSIC_CNZ2));
        CHECK(p->feature_resource_count(NULL,package,campaign)==1);
        CHECK(p->feature_enable(NULL,package,campaign,0));value(p,campaign,"cnz-2","s3-proto");
    }
    if(strcmp(argv[2],"sonic3"))CHECK(set(p,campaign,"fbz-1","s3-proto")&&!s3_music_active(S3_MUSIC_FBZ2));
    if(combined){
        CHECK(!set(p,"sonic3-campaign","cnz-1","s3-proto")&&!set(p,"sandk-campaign","fbz-1","s3-proto"));
        CHECK(p->feature_enable(NULL,package,campaign,0));
        CHECK(!s3_music_active(S3_MUSIC_CNZ2)&&!s3_music_active(S3_MUSIC_FBZ1));
        CHECK(p->feature_enable(NULL,package,campaign,1));
        CHECK(s3_music_active(S3_MUSIC_CNZ2)&&s3_music_active(S3_MUSIC_FBZ1));
        CHECK(p->feature_resource_count(NULL,package,campaign)==1);
    }
    CHECK(!set(p,"shared-themes","boss","unknown"));
    CHECK(p->set_enabled(NULL,package,0)&&!s3_themes_enabled());
    CHECK(p->set_enabled(NULL,package,1)&&s3_music_active(S3_MUSIC_BOSS)&&s3_music_active(S3_MUSIC_MINIBOSS));
    CHECK(p->commit(NULL,"unused-rom"));s3_themes_load(argv[1],argv[2]);
    CHECK(s3_music_active(S3_MUSIC_BOSS)&&s3_music_active(S3_MUSIC_MINIBOSS));
    /* Existing per-event choices and disabled preferences survive the new grouping. */
    char path[1200];snprintf(path,sizeof path,"%s",argv[1]);
    char *a=strrchr(path,'/'),*b=strrchr(path,'\\');if(!a||(b&&b>a))a=b;
    size_t prefix=a?(size_t)(a-path)+1:0;snprintf(path+prefix,sizeof path-prefix,"%s-themes.ini",argv[2]);
    if(combined){
        FILE *old=fopen(path,"wb");CHECK(old);
        CHECK(fputs("group.0.enabled=0\ngroup.1.enabled=0\ngroup.2.enabled=1\ncnz-2.enabled=1\ncnz-2.choice=s3-proto\nfbz-1.enabled=1\nfbz-1.choice=s3-proto\n",old)>=0);CHECK(!fclose(old));
        s3_themes_load(argv[1],argv[2]);
        CHECK(!s3_music_active(S3_MUSIC_CNZ2)&&s3_music_active(S3_MUSIC_FBZ1));
        value(p,campaign,"cnz-2","s3-proto");value(p,campaign,"fbz-1","s3-proto");
        CHECK(p->commit(NULL,"unused-rom"));s3_themes_load(argv[1],argv[2]);
        CHECK(!s3_music_active(S3_MUSIC_CNZ2)&&s3_music_active(S3_MUSIC_FBZ1));
        CHECK(p->feature_enable(NULL,package,campaign,1));
        CHECK(s3_music_active(S3_MUSIC_CNZ2)&&s3_music_active(S3_MUSIC_FBZ1));
    }
    FILE *saved=fopen(path,"wb");CHECK(saved);CHECK(fputs("extra-life.choice=s3\nextra-life.enabled=1\ngame-over.choice=cd-us\ngame-over.enabled=0\n",saved)>=0);CHECK(!fclose(saved));
    s3_themes_load(argv[1],argv[2]);CHECK(s3_music_active(S3_THEME_LIFE)&&!s3_music_active(S3_THEME_OVER));
    CHECK(!strcmp(s3_theme_choice(S3_THEME_OVER,(unsigned)s3_themes[S3_THEME_OVER].choice)->id,"cd-us"));
    CHECK(p->feature_enable(NULL,package,"shared-themes",0)&&p->commit(NULL,"unused-rom"));
    s3_themes_load(argv[1],argv[2]);CHECK(!s3_themes_enabled());value(p,"shared-themes","extra-life","s3");
    saved=fopen(path,"ab");CHECK(saved);CHECK(fputs("game-over.choice=original\ngame-over.enabled=1\n",saved)>=0);CHECK(!fclose(saved));
    s3_themes_load(argv[1],argv[2]);CHECK(!s3_themes[S3_THEME_OVER].enabled);
    puts("PASS grouped music, base defaults, same-zone-only prototype choices, independent boss/mini-boss, shared pickers, donor gating, retained group choices and legacy settings");return 0;
}
