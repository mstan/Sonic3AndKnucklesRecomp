#include "campaign_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static const char *path="combined-test.sav";
static void cleanup(void)
{const char *suffix[]={"",".bak",".tmp",".pre-v2.bak",".pre-shadow.bak",".pre-v3-cap3.bak"};for(unsigned i=0;i<6;i++){char p[100];snprintf(p,sizeof p,"%s%s",path,suffix[i]);remove(p);}}
static void read_bytes(const char *p,uint8_t b[548])
{FILE *f=fopen(p,"rb");CHECK(f);CHECK(fread(b,1,548,f)==548 && fgetc(f)==EOF);fclose(f);}
int main(void)
{
    uint8_t native[80],view[80],projection[80],before[548],snapshot[548],after[548];
    for(unsigned source=1;source<=2;source++)for(unsigned complete=0;complete<2;complete++) {
        cleanup();memset(native,0xFF,80);memset(native,0,10);native[8]=3;
        unsigned ch=source==1?5:7,oldmask=source==1?40:48;
        CampaignSave s,t;CHECK(!campaign_load(&s,path,native,1,oldmask));campaign_view(&s,view);
        view[2]=(uint8_t)(ch<<4);campaign_capture(&s,view,projection);
        if(source==1){CampaignCdState cd={0x1234,0x0234,1,0x55};CHECK(campaign_cd_set(&s,0,&cd));}
        CHECK(campaign_store(&s) && campaign_confirm(&s));read_bytes(path,before);
        CHECK(before[4]==(source==1?2:3) && before[6]==source);
        CHECK(campaign_load(&s,path,projection,1,63));campaign_view(&s,view);
        view[2]=(uint8_t)(ch<<4);view[3]=(uint8_t)campaign_display(63,complete?39:49);view[0]=(uint8_t)complete;
        campaign_capture(&s,view,projection);
        CampaignCdState cd={0x1234,0x0234,1,0x55};CHECK(campaign_cd_set(&s,0,&cd));
        CHECK(campaign_store(&s) && campaign_confirm(&s));read_bytes(path,after);
        CHECK(after[4]==3 && after[6]==3);
        read_bytes("combined-test.sav.pre-v3-cap3.bak",snapshot);CHECK(!memcmp(before,snapshot,548));
        for(unsigned mask=0;mask<64;mask++) {
            CHECK(campaign_load(&t,path,projection,1,mask));campaign_view(&t,view);
            CHECK(t.version==3 && t.capabilities==3 && !t.read_only);
            CHECK((view[2]>>4)==((mask&(ch==5?32:16))?ch:1));
            CampaignCdState got;CHECK(campaign_cd_get(&t,0,&got));
            CHECK(got.act==1 && got.stones==0x55 && got.generators==0x1234 && got.projectors==0x0234);
            view[8]=12;campaign_capture(&t,view,native);
            CHECK((native[2]>>4)<=3 && native[3]<=14 && t.slots[0][30]==(complete?39:49));
            CHECK(campaign_store(&t));read_bytes(path,after);CHECK(after[4]==3 && after[6]==3);
            t.mask=63;campaign_view(&t,view);CHECK((view[2]>>4)==ch && view[8]==12);
        }
        read_bytes("combined-test.sav.pre-v3-cap3.bak",snapshot);CHECK(!memcmp(before,snapshot,548));
    }
    cleanup();puts("Combined CD/Shadow: v2 and v3cap2 migration, immutable backups, all64 masks, completed/normal parking and native projection passed");return 0;
}
