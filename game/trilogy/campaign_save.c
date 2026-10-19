/* Game-owned progress journal. Native cartridge records stay 10 bytes and
 * contain native level/character values only. No host struct is serialized. */
#include "campaign_save.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

enum { COMMON=0,NATIVE=10,PROJECTED=20,RESUME=30,DONE=31,USED=32,PREVIOUS=38 };
static const unsigned counts[]={7,11,14,7},starts[]={0,7,18,32};
static unsigned present(unsigned mask,unsigned c) { return c==2 || (mask&(1u<<(c==3?2:c))); }
static unsigned content(int code) { return code<7?0:code<18?1:code<32?2:3; }
static int empty(const uint8_t *p) { return (p[0]&128)!=0; }
static uint32_t crc32(const uint8_t *p,unsigned n)
{ uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xEDB88320u&(0u-(c&1)));}return ~c; }
int campaign_count(unsigned mask)
{ int n=0;for(unsigned c=0;c<4;c++)if(present(mask,c))n+=counts[c];return n; }
int campaign_stable(unsigned mask,int display)
{
    if(display<0)return -1;
    for(unsigned c=0;c<4;c++)if(present(mask,c)) {
        if(display<(int)counts[c])return (int)starts[c]+display;
        display-=(int)counts[c];
    }
    return display==0?39:-1;
}
int campaign_display(unsigned mask,int stable)
{
    int n=0;
    for(unsigned c=0;c<4;c++) {
        if(stable>=(int)starts[c] && stable<(int)(starts[c]+counts[c]))return present(mask,c)?n+stable-(int)starts[c]:-1;
        if(present(mask,c))n+=(int)counts[c];
    }
    return stable==39?n:-1;
}
static unsigned active_bits(unsigned mask)
{ return 4|(mask&3)|((mask&4)?8:0); }
static void native_sanitize(uint8_t *p)
{
    if(empty(p))return;
    unsigned ch=p[2]>>4;if(ch>3)p[2]=(uint8_t)((p[2]&15)|((ch==5?1:0)<<4));
    if(p[3]>14)p[3]=0;
}
static void from_native(uint8_t *r,const uint8_t *p)
{
    memset(r,0,64);memcpy(r+COMMON,p,10);native_sanitize(r+COMMON);memcpy(r+NATIVE,p,10);
    native_sanitize(r+NATIVE);memcpy(r+PROJECTED,r+NATIVE,10);memcpy(r+PREVIOUS,r+NATIVE,10);
    r[RESUME]=(uint8_t)(18+(p[3]<14?p[3]:0));r[USED]=!empty(p);
    if(!empty(p) && (p[0]&3))r[DONE]=4;
}
void campaign_view(CampaignSave *s,uint8_t out[80])
{
    for(int i=0;i<8;i++) {
        uint8_t *r=s->slots[i],*p=out+i*10;
        memcpy(p,r+COMMON,10);
        if(empty(p))continue;
        int stable=r[RESUME],display=campaign_display(s->mask,stable);
        if(stable==39 || display<0) {
            stable=18+(r[NATIVE+3]<14?r[NATIVE+3]:0);
            if((r[DONE]&4) && present(s->mask,3) && !(r[DONE]&8))stable=32;
            display=campaign_display(s->mask,stable);
        }
        unsigned complete=(r[DONE]&active_bits(s->mask))==active_bits(s->mask);
        p[0]=(uint8_t)((p[0]&0xFC)|(complete?(r[COMMON]&3?r[COMMON]&3:1):0));
        p[3]=(uint8_t)(complete?campaign_count(s->mask):display);
        if(!(s->mask&4) && (p[2]>>4)>=4)p[2]=(uint8_t)((p[2]&15)|(((p[2]>>4)==5?1:0)<<4));
    }
    memcpy(s->view,out,80);
}
void campaign_capture(CampaignSave *s,const uint8_t view[80],uint8_t native[80])
{
    for(int i=0;i<8;i++) {
        uint8_t *r=s->slots[i];const uint8_t *p=view+i*10,*old=s->view+i*10;
        memcpy(r+PREVIOUS,r+PROJECTED,10);
        if(empty(p)) {from_native(r,p);memcpy(native+i*10,p,10);continue;}
        int code=campaign_stable(s->mask,p[3]);
        if(code<0)code=18;
        int changed=empty(old)||p[3]!=old[3]||((p[0]^old[0])&3);
        if(changed)r[RESUME]=(uint8_t)code;
        unsigned ch=r[COMMON+2]>>4;
        memcpy(r+COMMON,p,10);
        /* A temporary Sonic fallback must not erase parked Mighty identity. */
        if(!(s->mask&4) && ch>=4 && p[2]==old[2])r[COMMON+2]=(uint8_t)((p[2]&15)|(ch<<4));
        if(p[0]&3)r[DONE]|=(uint8_t)active_bits(s->mask);
        if(changed && code<39) {
            unsigned c=content(code);
            /* Crossing a campaign boundary certifies the preceding active
             * campaigns. A replay cannot revoke already-earned completion. */
            for(unsigned q=0;q<c;q++)if(present(s->mask,q))r[DONE]|=(uint8_t)(1u<<q);
            if(c==2) {memcpy(r+NATIVE,p,10);r[NATIVE+3]=(uint8_t)(code-18);}
        }
        r[USED]=1;
        uint8_t *n=native+i*10;memcpy(n,r+NATIVE,10);
        /* Common lives, emeralds and explicit character changes are shared. */
        memcpy(n+1,r+COMMON+1,2);memcpy(n+4,r+COMMON+4,6);
        n[0]=(uint8_t)((r[COMMON]&0xFC)|((r[DONE]&4)?(r[COMMON]&3?r[COMMON]&3:1):0));
        if(r[DONE]&4)n[3]=14;
        native_sanitize(n);memcpy(r+NATIVE,n,10);memcpy(r+PROJECTED,n,10);
    }
    memcpy(s->view,view,80);s->pending=1;
}
static int read_file(const char *path,uint8_t data[548])
{
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(data,1,548,f)==548 && fgetc(f)==EOF;fclose(f);
    uint32_t crc=((uint32_t)data[544]<<24)|((uint32_t)data[545]<<16)|((uint32_t)data[546]<<8)|data[547];
    if(!ok||memcmp(data,"SCS1",4)||data[4]!=1||data[5]>1||crc32(data,544)!=crc)return 0;
    for(unsigned i=0;i<8;i++) {
        const uint8_t *r=data+32+i*64;
        if(!empty(r) && ((r[2]>>4)>5 || r[RESUME]>39 || (r[DONE]&0xF0)))return 0;
        if(!empty(r+NATIVE) && (r[NATIVE+3]>14 || (r[NATIVE+2]>>4)>3))return 0;
    }
    return 1;
}
static int atomic_file(const char *path,const uint8_t *data,unsigned size)
{
    char tmp[1240];snprintf(tmp,sizeof tmp,"%s.tmp",path);
    FILE *f=fopen(tmp,"wb");if(!f)return 0;
    int ok=fwrite(data,1,size,f)==size && fflush(f)==0;
#ifdef _WIN32
    if(ok)ok=_commit(_fileno(f))==0;
#else
    if(ok)ok=fsync(fileno(f))==0;
#endif
    if(fclose(f))ok=0;
#ifdef _WIN32
    if(ok)ok=MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    if(ok)ok=rename(tmp,path)==0;
#endif
    return ok;
}
int campaign_store(CampaignSave *s)
{
    uint8_t old[548],data[548]={0};char backup[1240];
    snprintf(backup,sizeof backup,"%s.bak",s->path);
    /* Never replace a good backup with a damaged primary. */
    if(read_file(s->path,old) && !atomic_file(backup,old,sizeof old))return 0;
    memcpy(data,"SCS1",4);data[4]=1;data[5]=(uint8_t)s->pending;
    memcpy(data+32,s->slots,512);uint32_t crc=crc32(data,544);
    for(unsigned i=0;i<4;i++)data[544+i]=(uint8_t)(crc>>(24-i*8));
    return atomic_file(s->path,data,sizeof data);
}
int campaign_confirm(CampaignSave *s)
{
    if(!s->pending)return 1;
    s->pending=0;if(campaign_store(s))return 1;s->pending=1;return 0;
}
int campaign_load(CampaignSave *s,const char *path,const uint8_t native[80],int valid,unsigned mask)
{
    memset(s,0,sizeof *s);s->mask=mask;snprintf(s->path,sizeof s->path,"%s",path);
    uint8_t data[548];char backup[1240];snprintf(backup,sizeof backup,"%s.bak",path);
    s->loaded=read_file(path,data)||read_file(backup,data);
    if(s->loaded){memcpy(s->slots,data+32,512);s->pending=data[5]!=0;}
    for(int i=0;i<8;i++) {
        uint8_t *r=s->slots[i];const uint8_t *p=native+i*10;
        if(!s->loaded){from_native(r,p);continue;}
        if(!valid || !memcmp(p,r+PROJECTED,10) || (s->pending&&!memcmp(p,r+PREVIOUS,10)))continue;
        if(empty(p)){from_native(r,p);continue;} /* explicit native deletion */
        /* The base game may advance while packs are absent. Merge only its
         * campaign and shared stats; keep an unavailable addon cursor. */
        unsigned ch=r[COMMON+2]>>4;uint8_t fallback=r[PROJECTED+2];
        memcpy(r+NATIVE,p,10);native_sanitize(r+NATIVE);
        memcpy(r+COMMON,p,10);
        if(ch>=4 && p[2]==fallback)r[COMMON+2]=(uint8_t)((p[2]&15)|(ch<<4));
        if(p[0]&3)r[DONE]|=4;
        if(r[RESUME]>=18 && r[RESUME]<32)r[RESUME]=(uint8_t)(18+(p[3]<14?p[3]:0));
        memcpy(r+PROJECTED,r+NATIVE,10);memcpy(r+PREVIOUS,r+NATIVE,10);
    }
    campaign_view(s,s->view);return s->loaded;
}
void campaign_import(CampaignSave *s,const uint8_t legacy[80],int megamix)
{
    for(int i=0;i<8;i++) {
        const uint8_t *p=legacy+i*10;uint8_t *r=s->slots[i];
        if(empty(p) || (p[2]>>4)>5 || p[3]>39)continue;
        memcpy(r+COMMON,p,10);r[RESUME]=p[3]<39?p[3]:39;r[USED]=1;
        unsigned c=p[3]<39?content(p[3]):4;
        for(unsigned q=0;q<c;q++)r[DONE]|=(uint8_t)(1u<<q);
        if(p[0]&3){r[DONE]=megamix?15:7;r[RESUME]=39;}
        if(p[3]>=18 && p[3]<32){memcpy(r+NATIVE,p,10);r[NATIVE+3]=(uint8_t)(p[3]-18);}
        if(r[DONE]&4){r[NATIVE]=1;r[NATIVE+3]=14;}
        native_sanitize(r+NATIVE);
    }
}
int campaign_native_block(const uint8_t *raw,unsigned size,uint8_t out[80])
{
    static const unsigned locations[]={0x281,0x32D};
    for(unsigned k=0;k<2;k++) {
        uint8_t b[84];unsigned off=locations[k];if(off+166>=size)continue;
        for(unsigned i=0;i<84;i++)b[i]=raw[off+i*2];
        if(b[80]!=0x42 || b[81]!=0x44)continue;
        uint16_t sum=0;
        for(unsigned i=0;i<82;i+=2) {
            sum^=(uint16_t)((b[i]<<8)|b[i+1]);unsigned carry=sum&1;
            sum>>=1;if(carry)sum^=0x8810;
        }
        if(sum!=(uint16_t)((b[82]<<8)|b[83]))continue;
        memcpy(out,b,80);return 1;
    }
    return 0;
}
