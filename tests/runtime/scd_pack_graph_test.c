/* Exercise malformed inner graphs without any ROM or converted game data. */
#include "../../game/trilogy/scd_pack.c"
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void word(uint8_t *b,unsigned a,unsigned v){b[a]=(uint8_t)(v>>8);b[a+1]=(uint8_t)v;}
static void ptr(uint8_t *b,unsigned a,unsigned v){word(b,a,v>>16);word(b,a+2,v);}
int main(void)
{
    uint8_t b[256]={0};unsigned decoded;uint32_t consumed;
    const uint8_t literal[]={5,0,'A',0,0xF0,0};memcpy(b+32,literal,sizeof literal);
    CHECK(kos(b,sizeof b,SCD_WINDOW+32,1,&decoded,&consumed) && decoded==1 && consumed==6);
    CHECK(!kos(b,sizeof b,SCD_WINDOW+32,0,&decoded,&consumed));
    for(unsigned n=0;n<sizeof literal;n++)CHECK(!kos(b,32+n,SCD_WINDOW+32,16,&decoded,&consumed));
    b[32]=2;b[34]=0xFF;b[35]=0xF9;
    CHECK(!kos(b,sizeof b,SCD_WINDOW+32,16,&decoded,&consumed)); /* reference before output */
    memset(b+32,0,sizeof b-32);CHECK(!kos(b,sizeof b,SCD_WINDOW+32,16,&decoded,&consumed));
    word(b,30,1);memcpy(b+32,literal,sizeof literal);CHECK(kosm(b,sizeof b,SCD_WINDOW+30,16));
    word(b,30,4097);CHECK(!kosm(b,sizeof b,SCD_WINDOW+30,16));
    memset(b,0,sizeof b);CHECK(extra(b,sizeof b));
    ptr(b,0xF0,SCD_WINDOW+0x20);word(b,0x20,1);CHECK(!extra(b,sizeof b)); /* no frames */
    memset(b,0,sizeof b);ptr(b,0xE0,SCD_WINDOW+250);CHECK(!extra(b,sizeof b));
    memset(b,0,sizeof b);ptr(b,0xE8,SCD_WINDOW+0x20);word(b,0x20,128);word(b,0x22,1);word(b,0x24,1);
    CHECK(!extra(b,sizeof b)); /* missing chunk definitions */
    memset(b,0,sizeof b);ptr(b,0xEC,SCD_WINDOW+0x20);word(b,0x20,1);b[0x24]=255;
    CHECK(!extra(b,sizeof b)); /* truncated cycle frames */
    memset(b,0,sizeof b);ptr(b,0xE4,SCD_WINDOW+0x20);word(b,0x20,17);CHECK(!extra(b,sizeof b));
    CHECK(!mapping(b,sizeof b,SCD_WINDOW+250));
    /* A scenery palette owns all 48 level colours. Truncating it to the
     * former 24-colour record must not reinterpret colours as pointers. */
    memset(b,0,sizeof b);ptr(b,0xF4,SCD_WINDOW+0x20);word(b,0x24,1);
    CHECK(extra(b,0x9e));
    CHECK(!extra(b,0x9d));
    CHECK(!extra(b,0x6e));
    puts("CD inner graphs: bounded Kos/KosM tokens, truncation, back-references, animations, sections, cycles and definitions passed");
    return 0;
}
