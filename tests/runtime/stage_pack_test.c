#include "stage_pack.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
int main(int argc,char **argv)
{
    static const char *ids[]={"sonic3.sonic1-stages","sonic3.sonic2-stages","sonic3.megamix"};
    remove("sonic3k-stage-pack.ini");s3_stage_pack_load("settings.ini");
    const RecompLauncherCModProvider *p=s3_stage_pack_mods(NULL);
    CHECK(p->package_count(NULL)==3 && p->feature_count(NULL)==3);
    CHECK(!s3_stage_pack_enabled && !s3_stage_pack_sram());
    for(int i=0;i<3;i++) {
        RecompLauncherCModFeature f;CHECK(p->feature_get(NULL,i,&f));
        CHECK(!f.enabled && f.channel==RECOMP_MOD_CHANNEL_EXPERIMENTAL);
        CHECK(!strcmp(f.package_id,ids[i]));
        CHECK(p->feature_enable(NULL,ids[i],"campaign",1));
        CHECK(s3_pack_requested[i]);CHECK(!s3_stage_pack_enabled);
        CHECK(p->commit(NULL,NULL)); /* absent pack is a safe fallback */
        CHECK(!p->feature_resource_set_path(NULL,ids[i],"campaign","stage-bank","missing.bin"));
        CHECK(p->feature_enable(NULL,ids[i],"campaign",0));
    }
    if(argc==4) {
        for(int i=0;i<3;i++) {
            CHECK(p->feature_resource_set_path(NULL,ids[i],"campaign","stage-bank",argv[i+1]));
            CHECK(s3_pack_validate(argv[i+1],i+1,NULL,0));
            CHECK(!s3_pack_validate(argv[i+1],(i+1)%3+1,NULL,0));
        }
        for(unsigned mask=0;mask<8;mask++) {
            for(int i=0;i<3;i++)CHECK(p->feature_enable(NULL,ids[i],"campaign",(mask>>i)&1));
            CHECK(s3_stage_pack_sonic1==!!(mask&1));CHECK(s3_stage_pack_sonic2==!!(mask&2));
            CHECK(s3_stage_pack_megamix==!!(mask&4));CHECK(s3_stage_pack_mighty==!!(mask&4));
            CHECK(s3_stage_pack_enabled==!!mask);CHECK(!s3_stage_pack_sram());
            CHECK(p->commit_netplay(NULL,NULL)==!mask);CHECK(p->commit(NULL,NULL));
            s3_stage_pack_load("settings.ini");
            for(int i=0;i<3;i++){CHECK(s3_pack_requested[i]==!!(mask&(1<<i)));CHECK(s3_pack_available[i]==s3_pack_requested[i]);}
        }
        /* A path can disappear without clearing the requested configuration. */
        char good[1024];snprintf(good,sizeof good,"%s",s3_pack_paths[2]);
        snprintf(s3_pack_paths[2],1024,"missing.bin");s3_pack_refresh();
        CHECK(s3_pack_requested[2]&&!s3_stage_pack_megamix&&!s3_stage_pack_mighty);
        snprintf(s3_pack_paths[2],1024,"%s",good);s3_pack_refresh();CHECK(s3_stage_pack_mighty);
    }
    for(int i=0;i<3;i++)CHECK(p->feature_enable(NULL,ids[i],"campaign",0));
    CHECK(!s3_stage_pack_state_reason());CHECK(p->commit_netplay(NULL,NULL));
    remove("sonic3k-stage-pack.ini");puts("Independent experimental mods: defaults, 8 permutations, Mighty entitlement, missing/readded packs and native SRAM passed");return 0;
}
