#include "campaign_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void cleanup(void)
{remove("campaign-test.sav");remove("campaign-test.sav.bak");remove("campaign-test.sav.tmp");}
int main(void)
{
    cleanup();uint8_t native[80],view[80],projection[80];CampaignSave s,t;
    memset(native,0xFF,80);memset(native,0,10);native[3]=2;native[8]=3;
    for(unsigned mask=0;mask<8;mask++) {
        int n=14+((mask&1)?7:0)+((mask&2)?11:0)+((mask&4)?7:0);
        CHECK(campaign_count(mask)==n);
        for(int d=0;d<=n;d++){int c=campaign_stable(mask,d);CHECK(c>=0&&c<=39);CHECK(campaign_display(mask,c)==d);}
        CHECK(campaign_stable(mask,n+1)==-1);
        CHECK(campaign_display(mask,40)==-1);
    }
    CHECK(!campaign_load(&s,"campaign-test.sav",native,1,39));campaign_view(&s,view);
    CHECK(view[3]==20);view[2]=0x40;view[3]=35;view[8]=9;
    campaign_capture(&s,view,projection);CHECK(projection[3]==14 && (projection[0]&3));CHECK((projection[2]>>4)==0);
    CHECK(campaign_store(&s));CHECK(campaign_confirm(&s));
    for(unsigned mask=0;mask<8;mask++) {
        CHECK(campaign_load(&t,"campaign-test.sav",projection,1,mask|((mask&4)?32:0)));campaign_view(&t,view);
        CHECK(campaign_stable(mask,view[3])>=0);CHECK((view[2]>>4)==((mask&4)?4:0));
        if(mask&4)CHECK(campaign_stable(mask,view[3])==35);
        campaign_capture(&t,view,native);CHECK(native[3]<=14 && (native[2]>>4)<=3);
        CHECK(t.slots[0][30]==35); /* save at fallback does not erase parked cursor */
    }
    CHECK(campaign_load(&t,"campaign-test.sav",projection,1,3));campaign_view(&t,view);
    view[8]=12;campaign_capture(&t,view,native);CHECK(campaign_store(&t));CHECK(campaign_confirm(&t));
    CHECK(campaign_load(&t,"campaign-test.sav",native,1,39));campaign_view(&t,view);
    CHECK(view[3]==35 && view[2]==0x40 && view[8]==12);
    /* Missing/truncated native SRAM restores journal progress, including Mighty. */
    memset(native,0xFF,80);CHECK(campaign_load(&t,"campaign-test.sav",native,0,39));campaign_view(&t,view);CHECK(view[3]==35);
    /* A valid base-game deletion is intentional and must stay deleted. */
    CHECK(campaign_load(&t,"campaign-test.sav",native,1,39));campaign_view(&t,view);CHECK(view[0]&128);
    /* Damaged primary restores its last valid backup without importing legacy again. */
    FILE *f=fopen("campaign-test.sav","wb");CHECK(f);fputs("short",f);fclose(f);
    CHECK(campaign_load(&t,"campaign-test.sav",native,0,39));campaign_view(&t,view);CHECK(view[3]==35);
    /* Interrupted native write accepts the previous projection, preserving the pending journal. */
    memcpy(native,t.slots[0]+38,10);CHECK(campaign_load(&s,"campaign-test.sav",native,1,39));
    campaign_view(&s,view);CHECK(view[3]==35);
    cleanup();puts("Campaign saves: 8 catalogs, parked progress/Mighty, shared stats, native projection, deletion and recovery passed");return 0;
}
