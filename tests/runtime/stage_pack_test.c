#include "stage_pack.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static const char *ids[]={"sonic3.sonic1-stages","sonic3.sonic2-stages","sonic3.megamix","sonic3.mighty","sonic3.shadow"};
int main(int argc,char **argv)
{
    remove("sonic3k-stage-pack.ini");s3_stage_pack_load("settings.ini");
    const RecompLauncherCModProvider *p=s3_stage_pack_mods(NULL);
    CHECK(p->package_count(NULL)==5 && p->feature_count(NULL)==5);
    CHECK(!s3_stage_pack_enabled && !s3_stage_pack_sram());
    for(int i=0;i<5;i++) {
        RecompLauncherCModFeature f;CHECK(p->feature_get(NULL,i,&f));
        CHECK(!f.enabled && f.channel==RECOMP_MOD_CHANNEL_EXPERIMENTAL);
        CHECK(!strcmp(f.package_id,ids[i]));
        CHECK(p->feature_enable(NULL,ids[i],"campaign",1));
        CHECK(!s3_stage_pack_enabled);CHECK(p->commit(NULL,NULL));
        CHECK(!p->feature_resource_set_path(NULL,ids[i],"campaign","stage-bank","missing.bin"));
        if(i>=2) {
            RecompLauncherCModResource r;
            CHECK(p->feature_resource_count(NULL,ids[i],"campaign")==2);
            CHECK(p->feature_resource_get(NULL,ids[i],"campaign",0,&r));
            CHECK(r.required && !r.verified && !strcmp(r.id,"megamix-rom"));
            CHECK(!p->feature_resource_set_path(NULL,ids[i],"campaign",r.id,"missing.bin"));
        }
        CHECK(p->feature_enable(NULL,ids[i],"campaign",0));
    }
    /* A same-size impostor must fail too, without requiring any donor data. */
    FILE *bad=fopen("invalid-megamix.bin","wb");CHECK(bad);
    CHECK(!fseek(bad,1283223,SEEK_SET));CHECK(fputc(0,bad)!=EOF);CHECK(!fclose(bad));
    CHECK(!s3_megamix_rom_validate("invalid-megamix.bin",NULL,0));remove("invalid-megamix.bin");
    if(argc==5) {
        for(int i=0;i<3;i++) {
            CHECK(p->feature_resource_set_path(NULL,ids[i],"campaign","stage-bank",argv[i+1]));
            CHECK(s3_pack_validate(argv[i+1],i+1,NULL,0));
            CHECK(!s3_pack_validate(argv[i+1],(i+1)%3+1,NULL,0));
        }
        /* A converted pack alone can never activate any Megamix-derived mod. */
        for(int i=2;i<5;i++)CHECK(p->feature_enable(NULL,ids[i],"campaign",1));
        CHECK(!s3_pack_available[2] && !s3_stage_pack_enabled);
        for(int selected=2;selected<5;selected++) {
            CHECK(p->feature_resource_set_path(NULL,ids[selected],"campaign","megamix-rom",argv[4]));
            for(int i=2;i<5;i++) {
                RecompLauncherCModResource r;
                CHECK(p->feature_resource_get(NULL,ids[i],"campaign",0,&r));
                CHECK(r.required && r.verified && !strcmp(r.path,argv[4]));
                CHECK(p->feature_resource_get(NULL,ids[i],"campaign",1,&r));
                CHECK(r.verified && !strcmp(r.path,argv[3]));
            }
        }
        char good[1024];snprintf(good,sizeof good,"%s",s3_megamix_rom_path);
        CHECK(!p->feature_resource_set_path(NULL,ids[4],"campaign","megamix-rom",argv[1]));
        CHECK(!strcmp(good,s3_megamix_rom_path)); /* rejected selection is atomic */
        for(unsigned mask=0;mask<32;mask++) {
            for(int i=0;i<5;i++)CHECK(p->feature_enable(NULL,ids[i],"campaign",(mask>>i)&1));
            CHECK(s3_stage_pack_sonic1==!!(mask&1));CHECK(s3_stage_pack_sonic2==!!(mask&2));
            CHECK(s3_stage_pack_megamix==!!(mask&4));CHECK(s3_stage_pack_mighty==!!(mask&8));
            CHECK(s3_pack_shadow_requested==!!(mask&16));
            CHECK(s3_pack_available[2]==!!(mask&28));
            CHECK(s3_stage_pack_enabled==!!mask);CHECK(!s3_stage_pack_sram());
            CHECK(p->commit_netplay(NULL,NULL)==!mask);CHECK(p->commit(NULL,NULL));
            s3_stage_pack_load("settings.ini");
            CHECK(!strcmp(good,s3_megamix_rom_path));
            CHECK(s3_pack_requested[2]==!!(mask&4));
            CHECK(s3_pack_mighty_requested==!!(mask&8));CHECK(s3_pack_shadow_requested==!!(mask&16));
        }
        /* Disappearance parks all three, retaining independent requests and path. */
        snprintf(s3_megamix_rom_path,1024,"missing.bin");s3_pack_refresh();
        CHECK(s3_pack_requested[2] && s3_pack_mighty_requested && s3_pack_shadow_requested);
        CHECK(!s3_stage_pack_megamix && !s3_stage_pack_mighty && !s3_pack_available[2]);
        snprintf(s3_megamix_rom_path,1024,"%s",good);s3_pack_refresh();CHECK(s3_stage_pack_mighty);
        /* Old combined settings migrate their three requests but still require a ROM. */
        FILE *f=fopen("sonic3k-stage-pack.ini","wb");CHECK(f);
        fprintf(f,"format=2\nmegamix=1\nmegamix_bank=%s\n",argv[3]);CHECK(!fclose(f));
        s3_stage_pack_load("settings.ini");
        CHECK(s3_pack_requested[2] && s3_pack_mighty_requested && s3_pack_shadow_requested && !s3_pack_available[2]);
    }
    for(int i=0;i<5;i++)CHECK(p->feature_enable(NULL,ids[i],"campaign",0));
    CHECK(!s3_stage_pack_state_reason());CHECK(p->commit_netplay(NULL,NULL));
    remove("sonic3k-stage-pack.ini");puts("Independent mods: five defaults, shared required donor, rejection, 32 permutations, migration and removal/reinstall passed");return 0;
}
