#include "campaign_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static const char *path="shadow-test.sav";
static void cleanup(void)
{remove(path);remove("shadow-test.sav.bak");remove("shadow-test.sav.tmp");remove("shadow-test.sav.pre-shadow.bak");}
static void read_bytes(const char *p,uint8_t b[548])
{FILE *f=fopen(p,"rb");CHECK(f);CHECK(fread(b,1,548,f)==548 && fgetc(f)==EOF);fclose(f);}
static void write_bytes(const char *p,uint8_t b[548])
{FILE *f=fopen(p,"wb");CHECK(f);CHECK(fwrite(b,1,548,f)==548);CHECK(!fclose(f));}
int main(void)
{
    uint8_t native[80],view[80],projection[80],baseline[548],actual[548];CampaignSave s,t;
    for(unsigned ch=6;ch<=7;ch++)for(unsigned complete=0;complete<2;complete++) {
        cleanup();memset(native,0xFF,80);memset(native,0,10);native[3]=2;native[8]=3;
        CHECK(!campaign_load(&s,path,native,1,7));campaign_view(&s,view);
        CHECK(campaign_store(&s));read_bytes(path,baseline);CHECK(baseline[4]==1);
        s.mask=23;campaign_view(&s,view);view[2]=(uint8_t)((ch<<4)|5);view[3]=(uint8_t)(complete?39:35);
        view[0]=(uint8_t)complete;view[8]=9;
        campaign_capture(&s,view,projection);CHECK((projection[2]>>4)==(ch&1) && projection[3]<=14);
        CHECK(campaign_store(&s) && campaign_confirm(&s));
        read_bytes("shadow-test.sav.pre-shadow.bak",actual);CHECK(!memcmp(baseline,actual,548));
        read_bytes(path,baseline);CHECK(baseline[4]==3 && baseline[6]==2);
        for(unsigned mask=0;mask<8;mask++)for(unsigned upgraded=0;upgraded<2;upgraded++) {
            unsigned installed=mask|(upgraded?16:0);
            CHECK(campaign_load(&t,path,projection,1,installed));campaign_view(&t,view);
            CHECK((view[2]>>4)==((installed&16)?ch:ch&1));
            CHECK((view[2]&15)==5 && view[8]==9);
            view[8]=12;campaign_capture(&t,view,native);CHECK((native[2]>>4)==(ch&1) && native[3]<=14);
            CHECK(t.slots[0][2]>>4==ch && t.slots[0][30]==(complete?39:35));
            t.mask=23;campaign_view(&t,view);CHECK((view[2]>>4)==ch && view[8]==12);
        }
        /* Explicit deletion/recreation erases parked identity. */
        CHECK(campaign_load(&t,path,projection,1,0));campaign_view(&t,view);view[0]=128;
        campaign_capture(&t,view,native);CHECK(campaign_store(&t));
        CHECK(campaign_load(&t,path,native,1,23));campaign_view(&t,view);CHECK(view[0]&128);
        /* Unknown primary OR backup cannot be overwritten, even alongside a
         * valid understood record. Known CD v2 is migrated separately by the combined test. */
        for(unsigned future=0;future<2;future++) {
            memcpy(actual,baseline,548);actual[4]=(uint8_t)(future?5:4);
            write_bytes(path,actual);write_bytes("shadow-test.sav.bak",baseline);
            CHECK(!campaign_load(&t,path,projection,1,23) && t.read_only);
            CHECK(!campaign_store(&t));uint8_t after[548];read_bytes(path,after);CHECK(!memcmp(actual,after,548));
            write_bytes(path,baseline);write_bytes("shadow-test.sav.bak",actual);
            CHECK(!campaign_load(&t,path,projection,1,23) && t.read_only);CHECK(!campaign_store(&t));
            read_bytes("shadow-test.sav.bak",after);CHECK(!memcmp(actual,after,548));
        }
    }
    /* Character toggles are independent of all three stage toggles. */
    for(unsigned ch=4;ch<=7;ch++)for(unsigned complete=0;complete<2;complete++) {
        cleanup();memset(native,0xFF,80);memset(native,0,10);native[3]=2;native[8]=3;
        CHECK(!campaign_load(&s,path,native,1,55));campaign_view(&s,view);
        view[2]=(uint8_t)(ch<<4);view[3]=(uint8_t)(complete?39:35);view[0]=(uint8_t)complete;
        campaign_capture(&s,view,projection);CHECK(campaign_store(&s) && campaign_confirm(&s));
        for(unsigned toggles=0;toggles<32;toggles++) {
            unsigned mask=(toggles&7)|((toggles&8)?32:0)|(toggles&16);
            unsigned enabled=ch<6?mask&32:mask&16;
            CHECK(campaign_load(&t,path,projection,1,mask));campaign_view(&t,view);
            CHECK((view[2]>>4)==(enabled?ch:ch&1));
            CHECK(campaign_count(mask)==campaign_count(mask&7));
            campaign_capture(&t,view,native);
            CHECK((native[2]>>4)==(ch&1) && native[3]<=14);
            CHECK(t.slots[0][2]>>4==ch && t.slots[0][30]==(complete?39:35));
            t.mask=55;campaign_view(&t,view);CHECK((view[2]>>4)==ch);
        }
    }
    cleanup();puts("Shadow: both identities, completed/normal files, all pack/old-resource permutations, immutable migration and future companions passed");return 0;
}
