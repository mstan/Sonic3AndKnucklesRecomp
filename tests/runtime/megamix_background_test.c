#include <stdio.h>
#include <stdlib.h>
int tri_megamix_lz_scroll(int camera_x,unsigned line);
int tri_megamix_water_event(unsigned act,unsigned short x,unsigned short y,
    unsigned short *mean,unsigned short *target,unsigned char *routine);
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);} } while(0)
int main(int argc,char **argv)
{
    /* Word-negation/ASR values from v3 $9E28-$9E7C, including the odd-X
     * rounding which a C division or positive-X ratio would lose. */
    static const unsigned lines[]={0,159,160,175,176,191,192,207,208,223};
    static const int expected[]={-33,-33,-65,-65,-96,-96,-129,-129,-224,-224};
    for(unsigned i=0;i<10;i++)CHECK(tri_megamix_lz_scroll(513,lines[i])==expected[i]);
    CHECK(tri_megamix_lz_scroll(1,159)==-1);
    CHECK(tri_megamix_lz_scroll(1,160)==-1);
    CHECK(tri_megamix_lz_scroll(1,176)==0);
    CHECK(tri_megamix_lz_scroll(1,192)==-1);
    CHECK(tri_megamix_lz_scroll(1,208)==0);
    CHECK(tri_megamix_lz_scroll(0xFFFF,223)==0);
    CHECK(tri_megamix_lz_scroll(0x8000,0)==-2048);
    CHECK(tri_megamix_lz_scroll(0x8000,223)==-14336);
    for(unsigned i=0;i<224;i++)CHECK(!tri_megamix_lz_scroll(0,i));
    unsigned short mean=0x318,target=0x318;unsigned char routine=0;
    CHECK(!tri_megamix_water_event(0,0x4FF,0x300,&mean,&target,&routine) && target==0x318);
    CHECK(!tri_megamix_water_event(0,0x500,0x100,&mean,&target,&routine) && target==0x108);
    CHECK(!tri_megamix_water_event(0,0x500,0x300,&mean,&target,&routine) && target==0x318);
    CHECK(!tri_megamix_water_event(0,0x800,0x300,&mean,&target,&routine) && target==0x108);
    CHECK(!tri_megamix_water_event(0,0xD00,0x300,&mean,&target,&routine) && target==0x418);
    CHECK(!tri_megamix_water_event(0,0x800,0x400,&mean,&target,&routine) && target==0x418);
    CHECK(!tri_megamix_water_event(0,0x1600,0x400,&mean,&target,&routine) && target==0x108);
    CHECK(!tri_megamix_water_event(1,0x500,0,&mean,&target,&routine) && target==0x3C8);
    CHECK(!tri_megamix_water_event(1,0xB00,0,&mean,&target,&routine) && target==0x428);
    mean=target=0x4C8;
    CHECK(!tri_megamix_water_event(2,0,0x5EC,&mean,&target,&routine) && target==0x4C8 && !routine);
    CHECK(!tri_megamix_water_event(2,0x770,0x300,&mean,&target,&routine) && target==0x308);
    CHECK(!tri_megamix_water_event(2,0x1400,0x600,&mean,&target,&routine) && target==0x508 && mean==0x508);
    CHECK(!tri_megamix_water_event(2,0x1770,0x600,&mean,&target,&routine) && routine==1);
    CHECK(!tri_megamix_water_event(2,0x1860,0x300,&mean,&target,&routine) && target==0x188 && routine==1);
    mean=0x188;
    CHECK(!tri_megamix_water_event(2,0x1860,0x300,&mean,&target,&routine) && routine==2);
    CHECK(!tri_megamix_water_event(2,0x1AF0,0x300,&mean,&target,&routine) && mean==0x900 && target==0x900);
    CHECK(tri_megamix_water_event(2,0x1BC0,0x300,&mean,&target,&routine) && mean==0x7C0 && target==0x608 && routine==3);
    CHECK(!tri_megamix_water_event(2,0x1E00,0x300,&mean,&target,&routine) && target==0x128);
    CHECK(!tri_megamix_water_event(3,0xEFF,0,&mean,&target,&routine) && target==0x228);
    CHECK(!tri_megamix_water_event(3,0xF00,0,&mean,&target,&routine) && target==0x4C8);
    if(argc>1) {
        FILE *f=fopen(argv[1],"r");CHECK(f);unsigned a,x,y,m,t,s,speed,em,et,es,ef,count=0;
        while(fscanf(f,"%x %x %x %x %x %x %x %x %x %x %x",&a,&x,&y,&m,&t,&s,&speed,&em,&et,&es,&ef)==11) {
            unsigned short cm=(unsigned short)m,ct=(unsigned short)t;unsigned char cs=(unsigned char)s;
            int flag=tri_megamix_water_event(a,(unsigned short)x,(unsigned short)y,&cm,&ct,&cs);
            if(cm!=ct)cm=(unsigned short)(cm+(cm<ct?(int)speed:-(int)speed));
            if(cm!=em || ct!=et || cs!=es || flag!=(int)ef) {
                fprintf(stderr,"oracle row %u: act %u x %X y %X state %u: %X/%X/%u/%u != %X/%X/%u/%u\n",
                    count,a,x,y,s,cm,ct,cs,flag,em,et,es,ef);return 1;
            }
            ++count;
        }
        CHECK(feof(f));fclose(f);printf("%u independent 68000 water vectors passed\n",count);
    }
    puts("Megamix Misty Maze word arithmetic, scroll bands and v3 water events passed");return 0;
}
