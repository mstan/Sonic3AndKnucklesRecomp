#include "campaign_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void cleanup(void)
{remove("campaign-test.sav");remove("campaign-test.sav.bak");remove("campaign-test.sav.tmp");remove("campaign-test.sav.pre-v2.bak");}
static void read_raw(const char *path,uint8_t data[548])
{FILE *f=fopen(path,"rb");CHECK(f);CHECK(fread(data,1,548,f)==548);CHECK(fgetc(f)==EOF);CHECK(!fclose(f));}
static void write_raw(const char *path,uint8_t data[548])
{
    uint32_t c=~0u;for(unsigned j=0;j<544;j++){c^=data[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&(0u-(c&1)));}c=~c;
    for(unsigned k=0;k<4;k++)data[544+k]=(uint8_t)(c>>(24-k*8));
    FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(data,1,548,f)==548);CHECK(!fclose(f));
}
int main(void)
{
    cleanup();uint8_t native[80],view[80],projection[80];CampaignSave s,t;
    memset(native,0xFF,80);memset(native,0,10);native[3]=2;native[8]=3;
    for(unsigned mask=0;mask<16;mask++) {
        int n=14+((mask&1)?7:0)+((mask&2)?11:0)+((mask&4)?7:0)+((mask&8)?28:0);
        CHECK(campaign_count(mask)==n);
        for(int d=0;d<=n;d++){int c=campaign_stable(mask,d);CHECK(c>=0&&c<=67);CHECK(campaign_display(mask,c)==d);}
        CHECK(campaign_stable(mask,n+1)==-1);
        CHECK((campaign_display(mask,40)>=0)==!!(mask&8));
        if(mask&8) {
            int cd=campaign_display(mask,40),base=campaign_display(mask,18);
            CHECK(cd==((mask&1)?7:0)+((mask&2)?11:0));CHECK(base==cd+28);
        }
        if(mask&4)CHECK(campaign_display(mask,32)==n-7);
    }
    CHECK(!campaign_load(&s,"campaign-test.sav",native,1,7));campaign_view(&s,view);
    CHECK(view[3]==20);view[2]=0x40;view[3]=35;view[8]=9;
    campaign_capture(&s,view,projection);CHECK(projection[3]==14 && (projection[0]&3));CHECK((projection[2]>>4)==0);
    CHECK(campaign_store(&s));CHECK(campaign_confirm(&s));
    for(unsigned mask=0;mask<8;mask++) {
        CHECK(campaign_load(&t,"campaign-test.sav",projection,1,mask));campaign_view(&t,view);
        CHECK(campaign_stable(mask,view[3])>=0);CHECK((view[2]>>4)==((mask&4)?4:0));
        if(mask&4)CHECK(campaign_stable(mask,view[3])==35);
        campaign_capture(&t,view,native);CHECK(native[3]<=14 && (native[2]>>4)<=3);
        CHECK(t.slots[0][30]==35); /* save at fallback does not erase parked cursor */
    }
    CHECK(campaign_load(&t,"campaign-test.sav",projection,1,3));campaign_view(&t,view);
    view[8]=12;campaign_capture(&t,view,native);CHECK(campaign_store(&t));CHECK(campaign_confirm(&t));
    CHECK(campaign_load(&t,"campaign-test.sav",native,1,7));campaign_view(&t,view);
    CHECK(view[3]==35 && view[2]==0x40 && view[8]==12);
    /* Missing/truncated native SRAM restores journal progress, including Mighty. */
    memset(native,0xFF,80);CHECK(campaign_load(&t,"campaign-test.sav",native,0,7));campaign_view(&t,view);CHECK(view[3]==35);
    /* A valid base-game deletion is intentional and must stay deleted. */
    CHECK(campaign_load(&t,"campaign-test.sav",native,1,7));campaign_view(&t,view);CHECK(view[0]&128);
    /* Damaged primary restores its last valid backup without importing legacy again. */
    FILE *f=fopen("campaign-test.sav","wb");CHECK(f);fputs("short",f);fclose(f);
    CHECK(campaign_load(&t,"campaign-test.sav",native,0,7));campaign_view(&t,view);CHECK(view[3]==35);
    /* Interrupted native write accepts the previous projection, preserving the pending journal. */
    memcpy(native,t.slots[0]+38,10);CHECK(campaign_load(&s,"campaign-test.sav",native,1,7));
    campaign_view(&s,view);CHECK(view[3]==35);
    /* A real v1 byte fixture must migrate without renumbering old IDs. */
    uint8_t raw[548],original[548],again[548];read_raw("campaign-test.sav.bak",raw);
    raw[4]=1;raw[6]=0;write_raw("campaign-test.sav",raw);read_raw("campaign-test.sav",original);
    CHECK(campaign_load(&s,"campaign-test.sav",projection,0,15));CHECK(s.version==1);
    CHECK(s.slots[0][30]==35);CHECK(campaign_store(&s));CHECK(s.version==2);
    read_raw("campaign-test.sav.pre-v2.bak",again);CHECK(!memcmp(original,again,548));
    CHECK(campaign_store(&s));read_raw("campaign-test.sav.pre-v2.bak",again);CHECK(!memcmp(original,again,548));
    /* Park CD period, exact act, generators/projectors and unique stones. */
    campaign_view(&s,view);view[0]=0;view[2]=0x50;view[3]=(uint8_t)campaign_display(15,49);
    campaign_capture(&s,view,projection);
    CampaignCdState cd={0x1234,0x0234,1,0x55},restored;
    CHECK(campaign_cd_set(&s,0,&cd));CHECK(campaign_store(&s));CHECK(campaign_confirm(&s));
    for(unsigned mask=0;mask<16;mask++) {
        CHECK(campaign_load(&t,"campaign-test.sav",projection,1,mask));
        CHECK(campaign_cd_get(&t,0,&restored));CHECK(restored.act==1 && restored.stones==0x55);
        CHECK(restored.generators==0x1234 && restored.projectors==0x0234);
        campaign_view(&t,view);CHECK((view[2]>>4)==((mask&4)?5:1));
        if(mask&8)CHECK(campaign_stable(mask,view[3])==49);
        campaign_capture(&t,view,native);CHECK(t.slots[0][30]==49);
        CHECK(native[3]<=14 && (native[2]>>4)<=3);
    }
    /* Native-only progress merges shared stats; deletion clears CD forever. */
    memcpy(native,projection,80);native[8]=17;
    CHECK(campaign_load(&t,"campaign-test.sav",native,1,0));CHECK(t.slots[0][30]==49);
    CHECK(t.slots[0][8]==17);CHECK(campaign_store(&t));
    CHECK(campaign_load(&t,"campaign-test.sav",native,1,15));
    CHECK(campaign_cd_get(&t,0,&restored));CHECK(restored.stones==0x55);
    memset(native,0xFF,10);CHECK(campaign_load(&t,"campaign-test.sav",native,1,0));
    CHECK(campaign_cd_get(&t,0,&restored));CHECK(!restored.stones && !restored.generators);
    CHECK(campaign_store(&t));CHECK(campaign_load(&t,"campaign-test.sav",native,1,15));CHECK(t.view[0]&128);
    /* Future versions/capabilities in either companion are never overwritten. */
    read_raw("campaign-test.sav",raw);raw[4]=3;write_raw("campaign-test.sav",raw);
    read_raw("campaign-test.sav",original);
    CHECK(!campaign_load(&t,"campaign-test.sav",native,1,15));CHECK(t.read_only);CHECK(!campaign_store(&t));
    read_raw("campaign-test.sav",again);CHECK(!memcmp(original,again,548));
    raw[4]=2;raw[6]=0x81;write_raw("campaign-test.sav",raw);
    CHECK(!campaign_load(&t,"campaign-test.sav",native,1,15));CHECK(t.read_only);
    write_raw("campaign-test.sav.bak",raw);raw[6]=1;write_raw("campaign-test.sav",raw);
    CHECK(!campaign_load(&t,"campaign-test.sav",native,1,15));CHECK(t.read_only);CHECK(!campaign_store(&t));
    /* Failed flush retains the pending recovery state. */
    t.read_only=0;t.pending=1;snprintf(t.path,sizeof t.path,"campaign-test-no-such-directory/save");
    CHECK(!campaign_confirm(&t));CHECK(t.pending);
    cleanup();puts("Campaign saves: 16 catalogs, v1 migration, immutable backup, CD/Mighty parking, deletion, future-schema and failed-write recovery passed");return 0;
}
