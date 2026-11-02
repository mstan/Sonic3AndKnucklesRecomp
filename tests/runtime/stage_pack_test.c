#include "stage_pack.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static const char *ids[]={"sonic3.sonic1-stages","sonic3.sonic2-stages","sonic3.megamix","sonic3.sonic-cd","sonic3.mighty","sonic3.shadow"};
static int replay_playing;
int genesis_replay_playing(void) { return replay_playing; }
static void import_progress(void *ctx,float pct,const char *message)
{ (void)ctx;printf("import %.2f %s\n",pct,message);fflush(stdout); }
static void hud_decode_test(void)
{
    uint8_t encoded[160],out[384];
    for(unsigned mode=0;mode<2;mode++) {
        memset(encoded,0,sizeof encoded);encoded[0]=(uint8_t)(mode?128:0);encoded[1]=12;encoded[2]=255;
        unsigned bit=24;
        for(unsigned row=0;row<96;row++) {
            unsigned v=(63u<<7)|0x70u|((row%15)+1);
            for(int k=12;k>=0;k--,bit++)encoded[bit/8]|=(uint8_t)(((v>>k)&1)<<(7-(bit&7)));
        }
        CHECK(bit==159*8 && s3_megamix_hud_decode(encoded,159,out));
        uint32_t previous=0;
        for(unsigned row=0;row<96;row++) {
            uint32_t value=((row%15)+1)*0x11111111u;if(mode)value^=previous;
            previous=value;
            for(unsigned i=0;i<4;i++)CHECK(out[row*4+i]==(uint8_t)(value>>(24-i*8)));
        }
        for(unsigned n=0;n<159;n++)CHECK(!s3_megamix_hud_decode(encoded,n,out));
    }
    /* A real prefix table, not just inline runs: eight repeated nibbles
     * encoded by one zero bit per row. */
    memset(encoded,0,sizeof encoded);encoded[1]=12;encoded[2]=0x85;
    encoded[3]=0x71;encoded[4]=0;encoded[5]=255;
    CHECK(s3_megamix_hud_decode(encoded,18,out));
    for(unsigned i=0;i<384;i++)CHECK(out[i]==0x55);
    encoded[3]=0x79;CHECK(!s3_megamix_hud_decode(encoded,18,out));
}
int main(int argc,char **argv)
{
    hud_decode_test();
    remove("sonic3k-stage-pack.ini");s3_stage_pack_load("settings.ini");
    const RecompLauncherCModProvider *p=s3_stage_pack_mods(NULL);
    CHECK(p->package_count(NULL)==6 && p->feature_count(NULL)==6);
    CHECK(!s3_stage_pack_enabled && !s3_stage_pack_sram());
    {
        RecompLauncherCModResource cleared;
        CHECK(p->feature_resource_set_path(NULL,ids[3],"campaign","cd-music",""));
        CHECK(p->commit(NULL,NULL));s3_stage_pack_load("settings.ini");
        CHECK(p->feature_resource_get(NULL,ids[3],"campaign",2,&cleared));
        CHECK(!*cleared.path && !cleared.verified);
    }
    CHECK(!p->feature_resource_set_path(NULL,ids[3],"campaign","cd-disc","missing-disc.cue"));
    for(int i=0;i<2;++i) {
        RecompLauncherCModResource r;
        CHECK(p->feature_resource_count(NULL,ids[i],"campaign")==2);
        CHECK(p->feature_resource_get(NULL,ids[i],"campaign",0,&r));
        CHECK(!strcmp(r.id,"source-rom") && r.import_on_select && !r.required);
        CHECK(!p->feature_resource_set_path(NULL,ids[i],"campaign","source-rom","missing-rom.zip"));
        replay_playing=1;CHECK(p->feature_resource_set_path(NULL,ids[i],"campaign","source-rom","missing-rom.zip"));replay_playing=0;
        CHECK(p->feature_resource_get(NULL,ids[i],"campaign",1,&r) && !strcmp(r.id,"stage-bank"));
    }
    replay_playing=1;
    CHECK(p->feature_resource_set_path(NULL,ids[3],"campaign","cd-disc","missing-disc.cue"));
    CHECK(!s3_stage_pack_cd && !s3_stage_pack_enabled);
    CHECK(!p->feature_resource_set_path(NULL,ids[3],"campaign","stage-bank","missing.bin"));
    CHECK(!p->feature_resource_set_path(NULL,ids[3],"campaign","cd-music","missing.bin"));
    CHECK(!p->feature_resource_set_path(NULL,ids[3],"campaign","cd-disc","bad\npath.cue"));
    replay_playing=0;
    CHECK(!p->feature_resource_set_path(NULL,ids[3],"campaign","cd-disc","missing-disc.cue"));
    for(int i=0;i<6;i++) {
        RecompLauncherCModFeature f;CHECK(p->feature_get(NULL,i,&f));
        CHECK(!f.enabled && f.channel==RECOMP_MOD_CHANNEL_EXPERIMENTAL);
        CHECK(!strcmp(f.package_id,ids[i]));
        CHECK(p->feature_enable(NULL,ids[i],"campaign",1));
        CHECK(!s3_stage_pack_enabled);CHECK(p->commit(NULL,NULL));
        CHECK(!p->feature_resource_set_path(NULL,ids[i],"campaign","stage-bank","missing.bin"));
        if(i==2 || i>=4) {
            RecompLauncherCModResource r;
            CHECK(p->feature_resource_count(NULL,ids[i],"campaign")==2);
            CHECK(p->feature_resource_get(NULL,ids[i],"campaign",0,&r));
            CHECK(r.required && !r.verified && r.import_on_select && !strcmp(r.id,"megamix-rom"));
            CHECK(!p->feature_resource_set_path(NULL,ids[i],"campaign",r.id,"missing.bin"));
        }
        CHECK(p->feature_enable(NULL,ids[i],"campaign",0));
    }
    if((argc==4 || argc==5) && (!strcmp(argv[1],"--import") || !strcmp(argv[1],"--import-data-only") || !strcmp(argv[1],"--reject-import"))) {
        char error[256],content[1024],music[1024];RecompLauncherCModResource resource;
        int data_only=!strcmp(argv[1],"--import-data-only");
        CHECK(p->feature_resource_count(NULL,ids[3],"campaign")==3);
        CHECK(p->feature_resource_get(NULL,ids[3],"campaign",0,&resource));
        CHECK(!strcmp(resource.id,"cd-disc") && resource.import_on_select);
        CHECK(strstr(resource.file_patterns,"*.iso") && strstr(resource.file_patterns,"*.zip"));
        if(argc==5)CHECK(p->feature_resource_set_path(NULL,ids[3],"campaign","cd-music",argv[4]));
        CHECK(p->feature_enable(NULL,ids[3],"campaign",1));
        int imported=p->feature_resource_import(NULL,ids[3],"campaign","cd-disc",argv[2],argv[3],
                                                import_progress,NULL,error,sizeof error);
        if(!strcmp(argv[1],"--reject-import")) {
            CHECK(!imported && strstr(error,"missing") && !strstr(error,"Install Python"));
            CHECK(!s3_stage_pack_cd);remove("sonic3k-stage-pack.ini");
            printf("Owned-input rejection through the installed Python launcher passed: %s\n",error);return 0;
        }
        if(!imported)fprintf(stderr,"import failed: %s\n",error);
        CHECK(imported);
        CHECK(s3_stage_pack_cd && !s3_stage_pack_mighty && !s3_pack_shadow_requested);
        CHECK(p->feature_resource_get(NULL,ids[3],"campaign",1,&resource) && resource.verified);
        snprintf(content,sizeof content,"%s",resource.path);
        CHECK(p->feature_resource_get(NULL,ids[3],"campaign",2,&resource));
        CHECK(resource.verified==(!data_only || argc==5));
        if(data_only && argc==5)CHECK(!strcmp(resource.path,argv[4]));
        if(data_only && argc==4)CHECK(!*resource.path && strstr(resource.status,"No CDDA"));
        snprintf(music,sizeof music,"%s",resource.path);
        CHECK(!p->feature_resource_import(NULL,ids[3],"campaign","cd-disc","missing-disc.cue",argv[3],
                                         import_progress,NULL,error,sizeof error));
        CHECK(!strcmp(content,s3_pack_paths[3]) && !strcmp(music,s3_cd_audio_path));
        CHECK(p->commit(NULL,NULL));s3_stage_pack_load("settings.ini");
        CHECK(s3_stage_pack_cd && !strcmp(content,s3_pack_paths[3]) && !strcmp(music,s3_cd_audio_path));
        CHECK(p->feature_resource_get(NULL,ids[3],"campaign",0,&resource));
        CHECK(resource.verified && !strcmp(resource.path,argv[2]));
        if(data_only)CHECK(strstr(resource.status,argc==5?"retained":"no CDDA"));
        printf("Imported content: %s\nImported music: %s\n",content,music);
    }
    /* A same-size impostor must fail too, without requiring any donor data. */
    FILE *bad=fopen("invalid-megamix.bin","wb");CHECK(bad);
    CHECK(!fseek(bad,1283223,SEEK_SET));CHECK(fputc(0,bad)!=EOF);CHECK(!fclose(bad));
    CHECK(!s3_megamix_rom_validate("invalid-megamix.bin",NULL,0));remove("invalid-megamix.bin");
    if(argc==6) {
        for(int i=0;i<4;i++) {
            CHECK(p->feature_resource_set_path(NULL,ids[i],"campaign","stage-bank",argv[i+1]));
            CHECK(s3_pack_validate(argv[i+1],i+1,NULL,0));
            CHECK(!s3_pack_validate(argv[i+1],(i+1)%4+1,NULL,0));
        }
        /* A converted pack alone can never activate any Megamix-derived mod. */
        for(int i=2;i<6;i++)if(i!=3)CHECK(p->feature_enable(NULL,ids[i],"campaign",1));
        CHECK(!s3_pack_available[2] && !s3_stage_pack_enabled);
        for(int selected=2;selected<6;selected++)if(selected!=3) {
            CHECK(p->feature_resource_set_path(NULL,ids[selected],"campaign","megamix-rom",argv[5]));
            for(int i=2;i<6;i++)if(i!=3) {
                RecompLauncherCModResource r;
                CHECK(p->feature_resource_get(NULL,ids[i],"campaign",0,&r));
                CHECK(r.required && r.verified && !strcmp(r.path,argv[5]));
                CHECK(p->feature_resource_get(NULL,ids[i],"campaign",1,&r));
                CHECK(r.verified && !strcmp(r.path,argv[3]));
            }
        }
        char good[1024];snprintf(good,sizeof good,"%s",s3_megamix_rom_path);
        CHECK(!p->feature_resource_set_path(NULL,ids[5],"campaign","megamix-rom",argv[1]));
        CHECK(!strcmp(good,s3_megamix_rom_path)); /* rejected selection is atomic */
        for(unsigned mask=0;mask<64;mask++) {
            for(int i=0;i<6;i++)CHECK(p->feature_enable(NULL,ids[i],"campaign",(mask>>i)&1));
            CHECK(s3_stage_pack_sonic1==!!(mask&1));CHECK(s3_stage_pack_sonic2==!!(mask&2));
            CHECK(s3_stage_pack_megamix==!!(mask&4));CHECK(s3_stage_pack_mighty==!!(mask&16));
            CHECK(s3_stage_pack_cd==!!(mask&8));
            CHECK(s3_pack_shadow_requested==!!(mask&32));
            CHECK(s3_pack_available[2]==!!(mask&52));
            CHECK(s3_stage_pack_enabled==!!mask);CHECK(!s3_stage_pack_sram());
            CHECK(p->commit_netplay(NULL,NULL)==!mask);CHECK(p->commit(NULL,NULL));
            s3_stage_pack_load("settings.ini");
            CHECK(!strcmp(good,s3_megamix_rom_path));
            CHECK(s3_pack_requested[2]==!!(mask&4));
            CHECK(s3_pack_mighty_requested==!!(mask&16));CHECK(s3_pack_shadow_requested==!!(mask&32));
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
    for(int i=0;i<6;i++)CHECK(p->feature_enable(NULL,ids[i],"campaign",0));
    CHECK(!s3_stage_pack_state_reason());CHECK(p->commit_netplay(NULL,NULL));
    remove("sonic3k-stage-pack.ini");
    puts(argc==6?"Independent mods: six defaults, shared required donor, rejection, 64 permutations, migration and removal/reinstall passed":
                 "Independent mod defaults and rejection checks passed; owned combinations require five input arguments");return 0;
}
