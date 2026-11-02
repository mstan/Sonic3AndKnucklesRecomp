#include "sonic3_gameplay.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#define REMOVE_DIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(p) mkdir(p,0700)
#define REMOVE_DIR(p) rmdir(p)
#endif
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void native_timer(uint8_t *ram)
{
    if(ram[0xFE23]==9 && ram[0xFE24]==59 && ram[0xFE25]==59)
        ram[0xFE1A]=1;
    else ++ram[0xFE25];
}
static void test_native_access(void)
{
    static uint8_t ram[65536],before[65536];
    const char *modes[]={"sonic3","sonic3k","sandk"};
    for(unsigned m=0;m<3;++m) {
        s3_gameplay_load("unwritten-gameplay-fixture/settings.ini",modes[m]);
        const RecompLauncherCModProvider *provider=s3_gameplay_mods(NULL);
        RecompLauncherCModFeature feature;CHECK(provider->feature_get(provider->ctx,2,&feature));
        CHECK(strstr(feature.description,m==2?"Hold A and press Start":"Press Up")!=NULL);
        s3_gameplay=(S3GameplayConfig){0,0,1,1,0,0};
        unsigned select=m?0xFFE0:0xFFD0,debug=m?0xFFDA:0xFFFA;
        unsigned demo=m?0xFFD0:0xFFF0,competition=m?0xFFE8:0xFFD8;
        memset(ram,0,sizeof ram);ram[0xF600]=4;ram[0xFE12]=1;
        memset(ram+0xE660,0x77,4);memcpy(before,ram,sizeof ram);
        s3_gameplay_apply(ram);CHECK(ram[select]==1);before[select]=1;
        CHECK(!memcmp(before,ram,sizeof ram)); /* No forced mode/slot/SRAM selection. */
        memset(ram,0,sizeof ram);ram[0xF600]=12;memcpy(before,ram,sizeof ram);
        s3_gameplay_apply(ram);CHECK(ram[debug]==1);before[debug]=1;
        CHECK(!memcmp(before,ram,sizeof ram) && !ram[0xFE08]);
        /* Imported rounds never receive a native object-placement table. */
        for(unsigned z=0x20;z<0x60;++z) {
            ram[0xFE10]=(uint8_t)z;ram[debug]=1;s3_gameplay_apply(ram);
            CHECK(!ram[debug] && !ram[0xFE08] && ram[0xF600]==12);
        }
        /* Each byte of both native words must exclude demo/Competition. */
        s3_gameplay=(S3GameplayConfig){1,1,1,1,0,0};
        const unsigned guards[]={demo,demo+1,competition,competition+1};
        for(unsigned i=0;i<4;++i)for(unsigned title=0;title<2;++title) {
            memset(ram,0,sizeof ram);ram[0xF600]=title?4:12;ram[guards[i]]=1;
            ram[0xFE23]=9;ram[0xFE24]=59;ram[0xFE25]=59;
            memcpy(before,ram,sizeof ram);s3_gameplay_apply(ram);
            CHECK(!memcmp(before,ram,sizeof ram));
        }
        /* Disabling options preserves even native cheats earned by the player. */
        memset(ram,0x5A,sizeof ram);ram[0xF600]=12;ram[demo]=ram[demo+1]=0;
        ram[competition]=ram[competition+1]=0;memcpy(before,ram,sizeof ram);
        s3_gameplay=(S3GameplayConfig){0};s3_gameplay_apply(ram);
        CHECK(!memcmp(before,ram,sizeof ram));
    }
}
static int base_context;
static int base_count(void *c){CHECK(c==&base_context);return 1;}
static int base_package(void *c,int i,RecompLauncherCModPackage *o)
{CHECK(c==&base_context&&i==0);memset(o,0,sizeof *o);strcpy(o->id,"fixture");return 1;}
static int base_feature(void *c,int i,RecompLauncherCModFeature *o)
{CHECK(c==&base_context&&i==0);memset(o,0,sizeof *o);strcpy(o->id,"base-feature");return 1;}
static int base_enable(void *c,const char *p,const char *f,int on)
{CHECK(c==&base_context&&!strcmp(p,"fixture")&&!strcmp(f,"base-feature"));return on==1;}
static int base_option(void *c,const char *p,const char *f,int i,RecompLauncherCModOption *o)
{(void)o;CHECK(c==&base_context&&!strcmp(p,"fixture")&&!strcmp(f,"base-feature"));return i==3;}
static int base_commit(void *c,const char *image)
{CHECK(c==&base_context&&!strcmp(image,"owned.bin"));return 1;}
static void test_provider_composition(void)
{
    RecompLauncherCModProvider b={0};b.ctx=&base_context;
    b.package_count=b.feature_count=base_count;b.package_get=base_package;
    b.feature_get=base_feature;b.feature_enable=base_enable;
    b.feature_option_get=base_option;b.commit_netplay=base_commit;
    const RecompLauncherCModProvider *p=s3_gameplay_mods(&b);
    CHECK(p->ctx==&base_context && p->package_count(p->ctx)==2 && p->feature_count(p->ctx)==6);
    RecompLauncherCModPackage package;RecompLauncherCModFeature feature;
    CHECK(p->package_get(p->ctx,1,&package)&&!strcmp(package.id,"fixture"));
    CHECK(p->feature_get(p->ctx,5,&feature)&&!strcmp(feature.id,"base-feature"));
    CHECK(p->feature_enable(p->ctx,"fixture","base-feature",1));
    CHECK(p->feature_option_get(p->ctx,"fixture","base-feature",3,NULL));
    CHECK(p->commit_netplay(p->ctx,"owned.bin"));
    CHECK(!p->feature_enable(p->ctx,"sonic3.gameplay","unknown",1));
    for(int i=0;i<5;++i) {
        CHECK(p->feature_get(p->ctx,i,&feature));
        CHECK(!strcmp(feature.package_id,"sonic3.gameplay"));
        CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,1));
        CHECK(!s3_gameplay_netplay_allowed());
        CHECK(p->feature_enable(p->ctx,feature.package_id,feature.id,0));
        CHECK(s3_gameplay_netplay_allowed());
    }
    s3_gameplay_mods(NULL);
}
static void test_checkpoint_option(void)
{
    const char *dir="test-gameplay-modes";
    CHECK(MAKE_DIR(dir)==0); /* Never reuse/replace an owner's settings. */
    const char *modes[]={"sonic3","sandk","sonic3k"};
    for(unsigned i=0;i<3;++i) {
        char path[128];snprintf(path,sizeof path,"%s/%s-gameplay.ini",dir,modes[i]);
        s3_gameplay_load("test-gameplay-modes/settings.ini",modes[i]);
        const RecompLauncherCModProvider *p=s3_gameplay_mods(NULL);
        CHECK(p->feature_count(p->ctx)==(i==2?6:5));
        CHECK(!s3_gameplay.checkpoint_bonus && s3_gameplay_netplay_allowed());
        if(i==2) {
            CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","s1-checkpoint-bonus",1));
            CHECK(s3_gameplay.checkpoint_bonus && !s3_gameplay_netplay_allowed());
            CHECK(p->commit(p->ctx,NULL));
        } else {
            CHECK(!p->feature_enable(p->ctx,"sonic3.gameplay","s1-checkpoint-bonus",1));
            FILE *f=fopen(path,"wb");CHECK(f);fputs("checkpoint_bonus=1\n",f);CHECK(!fclose(f));
        }
        s3_gameplay_load("test-gameplay-modes/settings.ini",modes[i]);
        CHECK(s3_gameplay.checkpoint_bonus==(i==2));
        CHECK(remove(path)==0);
    }
    CHECK(REMOVE_DIR(dir)==0);
}
static void test_shield_transform(void)
{
    static uint8_t ram[65536],before[65536];
    const char *modes[]={"sonic3","sonic3k","sandk"};
    for(unsigned m=0;m<3;++m) {
        s3_gameplay_load("unwritten-gameplay-fixture/settings.ini",modes[m]);
        unsigned pc=m?0x1192C:0x12946,target=m?0x119D2:0x129EC;
        unsigned demo=m?0xFFD0:0xFFF0,competition=m?0xFFE8:0xFFD8;
        CHECK(s3_gameplay_owns(pc)&&!s3_gameplay_owns(pc+2));
        memset(ram,0,sizeof ram);ram[0xF600]=12;ram[0xB02A]=2;
        ram[0xB02B]=0x11;ram[0xF602]=1;ram[0xF603]=0x20;
        ram[0xFE1E]=1;ram[0xFE21]=50;if(m)ram[0xFFB0]=7;else ram[0xFFB1]=7;
        memcpy(before,ram,sizeof ram);
        CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));
        s3_gameplay.shield_transform=1;
        for(unsigned shield=0x10;shield<=0x40;shield<<=1)for(unsigned jump=0x10;jump<=0x40;jump<<=1) {
            ram[0xB02B]=(uint8_t)(1|shield);ram[0xF603]=(uint8_t)jump;
            memcpy(before,ram,sizeof ram);
            CHECK(s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1)==target);
            CHECK(!memcmp(before,ram,sizeof ram));
            CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,0)); /* Shadow/Mighty. */
            CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB04Au,1));
            CHECK(!s3_gameplay_transform_target(pc+2,ram,0xFFFFB000u,1));
        }
        /* Every unavailable case leaves the native shield action intact. */
        const unsigned blocked[]={0xB02F,0xFE19,0xFE08,demo,demo+1,competition,competition+1};
        for(unsigned i=0;i<sizeof blocked/sizeof blocked[0];++i) {
            unsigned p=blocked[i];uint8_t old=ram[p];ram[p]=1;
            CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));ram[p]=old;
        }
        const unsigned required[]={0xF602,0xF603,0xFE1E,0xB02A};
        for(unsigned i=0;i<sizeof required/sizeof required[0];++i) {
            unsigned p=required[i];uint8_t old=ram[p];ram[p]=0;
            CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));ram[p]=old;
        }
        ram[0xFE21]=49;CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));ram[0xFE21]=50;
        ram[0xB02B]=0x43;CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));
        ram[0xB02B]=1;CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));
        ram[0xB02B]=0;CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));CHECK(!ram[0xB02B]);
        ram[0xB02B]=0x41;
        const uint8_t menus[]={0,4,8,0x28,0x34,0x8C};
        for(unsigned i=0;i<sizeof menus;++i){ram[0xF600]=menus[i];CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));}
        ram[0xF600]=12;ram[0xFFB0]=0;ram[0xFFB1]=6;
        CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));
        if(m) {
            ram[0xFFB0]=7;ram[0xFFBA]=1;
            CHECK(!s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1));
            ram[0xFFB1]=7;CHECK(s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1)==target);
        } else {
            ram[0xFFB1]=7;ram[0xFFBA]=1;
            CHECK(s3_gameplay_transform_target(pc,ram,0xFFFFB000u,1)==target);
        }
        CHECK(!s3_gameplay_transform_target(pc,NULL,0xFFFFB000u,1));
    }
}
int main(void)
{
    static uint8_t ram[65536],before[65536];
    remove("test-gameplay-gameplay.ini");
    s3_gameplay_load("settings.ini","test-gameplay");
    CHECK(!s3_gameplay.infinite_lives && !s3_gameplay.infinite_time);
    ram[0xF600]=12;ram[0xFE12]=1;ram[0xFE23]=9;ram[0xFE24]=59;ram[0xFE25]=59;
    memcpy(before,ram,sizeof ram);s3_gameplay_apply(ram);CHECK(!memcmp(before,ram,sizeof ram));
    const RecompLauncherCModProvider *p=s3_gameplay_mods(NULL);
    CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","infinite-lives",1));
    CHECK(!s3_gameplay_netplay_allowed() && !p->commit_netplay(p->ctx,NULL));
    for(int death=0;death<20;++death) {
        s3_gameplay_apply(ram);CHECK(ram[0xFE12]>=3);
        --ram[0xFE12];CHECK(ram[0xFE12]!=0);
    }
    ram[0xFE12]=99;s3_gameplay_apply(ram);CHECK(ram[0xFE12]==99);
    CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","infinite-time",1));
    CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","easy-level-select",1));
    CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","debug-mode",1));
    CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","shield-transform",1));
    CHECK(p->commit(p->ctx,NULL));
    s3_gameplay_load("settings.ini","test-gameplay");
    CHECK(s3_gameplay.infinite_lives && s3_gameplay.infinite_time &&
          s3_gameplay.easy_level_select && s3_gameplay.debug_mode && s3_gameplay.shield_transform);
    for(int frame=0;frame<300;++frame){s3_gameplay_apply(ram);native_timer(ram);CHECK(!ram[0xFE1A]);}
    CHECK(ram[0xFE23]==9 && ram[0xFE24]==59 && ram[0xFE25]==59);
    CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","infinite-time",0));
    native_timer(ram);CHECK(ram[0xFE1A]==1);
    s3_gameplay.infinite_time=1;s3_gameplay.easy_level_select=0;
    const uint8_t modes[]={0,4,0x34,0x8C};
    for(unsigned i=0;i<sizeof modes;++i){ram[0xF600]=modes[i];ram[0xFE12]=1;
        memcpy(before,ram,sizeof ram);s3_gameplay_apply(ram);CHECK(!memcmp(before,ram,sizeof ram));}
    ram[0xF600]=12;ram[0xFFE9]=1;memcpy(before,ram,sizeof ram);
    s3_gameplay_apply(ram);CHECK(!memcmp(before,ram,sizeof ram));
    CHECK(p->set_enabled(p->ctx,"sonic3.gameplay",0));CHECK(s3_gameplay_netplay_allowed());
    CHECK(p->commit(p->ctx,NULL));s3_gameplay_load("settings.ini","test-gameplay");
    CHECK(!s3_gameplay.infinite_lives && !s3_gameplay.infinite_time);
    remove("test-gameplay-gameplay.ini");
    test_native_access();test_provider_composition();test_checkpoint_option();test_shield_transform();
    puts("Gameplay defaults, persistence, lives/time, three-variant native access, demo/Competition scope and provider composition PASS");
    return 0;
}
