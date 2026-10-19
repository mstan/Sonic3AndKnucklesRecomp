#include "stage_pack.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} }while(0)
int main(int argc,char **argv)
{
    const char *package="sonic3.stage-pack",*feature="sonic1-2-stages";
    remove("sonic3k-stage-pack.ini");
    s3_stage_pack_load("settings.ini");
    CHECK(!s3_stage_pack_enabled && !s3_stage_pack_path[0]);
    CHECK(!s3_stage_pack_sram() && !s3_stage_pack_state_reason());
    const RecompLauncherCModProvider *p=s3_stage_pack_mods(NULL);
    CHECK(p->package_count(NULL)==1 && p->feature_count(NULL)==1);
    RecompLauncherCModFeature f;CHECK(p->feature_get(NULL,0,&f));
    CHECK(!f.enabled && f.channel==RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    CHECK(p->feature_enable(NULL,package,feature,1));
    CHECK(!strcmp(s3_stage_pack_sram(),"sonic3k-stage-pack.srm"));
    CHECK(s3_stage_pack_state_reason() && !p->commit_netplay(NULL,NULL));
    CHECK(!p->commit(NULL,NULL));
    CHECK(!p->feature_resource_set_path(NULL,package,feature,"stage-bank","missing.bin"));
    CHECK(!s3_stage_pack_path[0]);
    if(argc>1) {
        CHECK(p->feature_resource_set_path(NULL,package,feature,"stage-bank",argv[1]));
        CHECK(p->commit(NULL,NULL));
        s3_stage_pack_load("settings.ini");
        CHECK(s3_stage_pack_enabled && !strcmp(s3_stage_pack_path,argv[1]));
        CHECK(!p->feature_resource_set_path(NULL,package,feature,"stage-bank","missing.bin"));
        CHECK(!strcmp(s3_stage_pack_path,argv[1]));
        RecompLauncherCModResource r;CHECK(p->feature_resource_get(NULL,package,feature,0,&r));
        CHECK(r.verified);
        FILE *bank=fopen(argv[1],"rb");CHECK(bank);
        CHECK(!fseek(bank,0,SEEK_END));long n=ftell(bank);CHECK(n>0);
        rewind(bank);unsigned char *data=malloc((size_t)n);CHECK(data);
        CHECK(fread(data,1,(size_t)n,bank)==(size_t)n);fclose(bank);
        data[0x100]^=1;
        FILE *bad=fopen("stage-pack-corrupt.bin","wb");CHECK(bad);
        CHECK(fwrite(data,1,(size_t)n,bad)==(size_t)n);CHECK(!fclose(bad));
        CHECK(!s3_stage_pack_validate("stage-pack-corrupt.bin",NULL,0));
        CHECK(!p->feature_resource_set_path(NULL,package,feature,"stage-bank","stage-pack-corrupt.bin"));
        CHECK(!strcmp(s3_stage_pack_path,argv[1]));
        free(data);remove("stage-pack-corrupt.bin");
    }
    CHECK(p->feature_enable(NULL,package,feature,0));
    CHECK(p->commit_netplay(NULL,NULL) && p->commit(NULL,NULL));
    s3_stage_pack_load("settings.ini");CHECK(!s3_stage_pack_enabled);
    CHECK(!s3_stage_pack_sram() && !s3_stage_pack_state_reason());
    remove("sonic3k-stage-pack.ini");
    puts("Stage pack: default off, persistence, invalid-file rollback, save and netplay isolation passed");
    return 0;
}
