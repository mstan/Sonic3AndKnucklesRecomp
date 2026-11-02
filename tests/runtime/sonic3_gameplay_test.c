#include "sonic3_gameplay.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void native_timer(uint8_t *ram)
{
    if(ram[0xFE23]==9 && ram[0xFE24]==59 && ram[0xFE25]==59)
        ram[0xFE1A]=1;
    else ++ram[0xFE25];
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
    CHECK(p->commit(p->ctx,NULL));
    s3_gameplay_load("settings.ini","test-gameplay");
    CHECK(s3_gameplay.infinite_lives && s3_gameplay.infinite_time);
    for(int frame=0;frame<300;++frame){s3_gameplay_apply(ram);native_timer(ram);CHECK(!ram[0xFE1A]);}
    CHECK(ram[0xFE23]==9 && ram[0xFE24]==59 && ram[0xFE25]==59);
    CHECK(p->feature_enable(p->ctx,"sonic3.gameplay","infinite-time",0));
    native_timer(ram);CHECK(ram[0xFE1A]==1);
    s3_gameplay.infinite_time=1;
    const uint8_t modes[]={0,4,0x34,0x8C};
    for(unsigned i=0;i<sizeof modes;++i){ram[0xF600]=modes[i];ram[0xFE12]=1;
        memcpy(before,ram,sizeof ram);s3_gameplay_apply(ram);CHECK(!memcmp(before,ram,sizeof ram));}
    ram[0xF600]=12;ram[0xFFE9]=1;memcpy(before,ram,sizeof ram);
    s3_gameplay_apply(ram);CHECK(!memcmp(before,ram,sizeof ram));
    CHECK(p->set_enabled(p->ctx,"sonic3.gameplay",0));CHECK(s3_gameplay_netplay_allowed());
    CHECK(p->commit(p->ctx,NULL));s3_gameplay_load("settings.ini","test-gameplay");
    CHECK(!s3_gameplay.infinite_lives && !s3_gameplay.infinite_time);
    remove("test-gameplay-gameplay.ini");
    puts("Gameplay defaults, persistence, repeated lives, terminal timer, scope and netplay guards PASS");
    return 0;
}
