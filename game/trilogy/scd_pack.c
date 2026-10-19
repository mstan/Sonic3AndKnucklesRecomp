/* Bound every resource to its own source region before enrolling CD. */
#include "scd_pack.h"
#include <stdio.h>
#include <string.h>
static unsigned w(const uint8_t *p){return ((unsigned)p[0]<<8)|p[1];}
static uint32_t l(const uint8_t *p){return ((uint32_t)w(p)<<16)|w(p+2);}
static const uint8_t *span(const uint8_t *b,uint32_t size,uint32_t at,uint32_t n)
{return at<=size && n<=size-at?b+at:NULL;}
static const uint8_t *guest(const uint8_t *b,uint32_t size,uint32_t at,uint32_t n)
{return at>=SCD_WINDOW?span(b,size,at-SCD_WINDOW,n):NULL;}
typedef struct KosBits {const uint8_t *data;uint32_t size,pos,desc;unsigned left;int error;} KosBits;
static unsigned byte(KosBits *s)
{if(s->pos>=s->size){s->error=1;return 0;}return s->data[s->pos++];}
static unsigned bit(KosBits *s)
{
    unsigned v=s->desc&1;s->desc>>=1;
    if(!--s->left){unsigned lo=byte(s),hi=byte(s);s->desc=lo|(hi<<8);s->left=16;}
    return v;
}
/* Validate every token and back-reference without allocating decoded art. */
static int kos(const uint8_t *b,uint32_t n,uint32_t address,unsigned cap,unsigned *decoded,uint32_t *consumed)
{
    const uint8_t *p=guest(b,n,address,2);if(!p)return 0;
    KosBits s={p,n-(address-SCD_WINDOW),2,(unsigned)p[0]|((unsigned)p[1]<<8),16,0};unsigned out=0;
    while(!s.error) {
        if(bit(&s)){byte(&s);if(out==cap)return 0;out++;continue;}
        int offset;unsigned count;
        if(bit(&s)) {
            unsigned lo=byte(&s),hi=byte(&s);offset=(int)(0xE000|((hi&0xF8)<<5)|lo)-0x10000;count=hi&7;
            if(count)count+=2;
            else {count=byte(&s);if(!count){if(s.error)return 0;*decoded=out;*consumed=s.pos;return 1;}if(count==1)continue;count++;}
        } else {
            unsigned first=bit(&s),second=bit(&s);count=(first<<1|second)+2;offset=(int)byte(&s)-256;
        }
        if(s.error || offset>=0 || (unsigned)-offset>out || count>cap-out)return 0;
        out+=count;
    }
    return 0;
}
static int kosm(const uint8_t *b,uint32_t n,uint32_t address,unsigned cap)
{
    const uint8_t *p=guest(b,n,address,2);if(!p)return 0;
    unsigned size=w(p);if(size==0xA000)size=0x8000;
    if(!size || size>cap)return 0;
    uint32_t a=address+2;unsigned done=0;
    while(done<size) {
        unsigned decoded;uint32_t consumed;
        if(!kos(b,n,a,size-done<4096?size-done:4096,&decoded,&consumed) || !decoded)return 0;
        if(decoded!=(size-done<4096?size-done:4096))return 0;
        done+=decoded;a+=(consumed+15)&~15u;
    }
    return 1;
}
static int cycles(const uint8_t *b,uint32_t n,uint32_t a)
{
    if(!a)return 1;
    const uint8_t *p=guest(b,n,a,2);if(!p || w(p)>32)return 0;
    unsigned count=w(p);a+=2;
    for(unsigned i=0;i<count;i++) {
        p=guest(b,n,a,3);if(!p || p[1]>=64 || !p[2])return 0;
        unsigned frames=p[2],colours=0;p=guest(b,n,a+3,frames*2);if(!p)return 0;
        for(unsigned j=0;j<frames;j++)if(p[j*2+1]+1u>colours)colours=p[j*2+1]+1u;
        if(!guest(b,n,a+3+frames*2,colours*2))return 0;
        a+=3+frames*2+colours*2;a+=a&1;
    }
    return 1;
}
static int extra(const uint8_t *b,uint32_t n)
{
    uint32_t a=l(b+0xE0);const uint8_t *p;
    if(a)for(unsigned i=0;;i++,a+=12) {
        p=guest(b,n,a,12);if(!p || i>2048 || w(p+10))return 0;
        unsigned count=w(p+6)&0x7FFF,dst=w(p+4);if(!count)break;
        if(dst>2048 || count>2048-dst || !guest(b,n,l(p),count*32))return 0;
    }
    a=l(b+0xE4);
    if(a){p=guest(b,n,a,2);if(!p || w(p)>16 || !guest(b,n,a,2+w(p)*2))return 0;}
    a=l(b+0xE8);
    if(a) {
        p=guest(b,n,a,6);if(!p)return 0;unsigned chunks=w(p),fw=w(p+2),fh=w(p+4);
        if(chunks>128 || !fw || fw>128 || !fh || fh>32)return 0;
        unsigned bytes=fw*fh;const uint8_t *grid=guest(b,n,a+6,bytes);
        if(!grid || !guest(b,n,a+6+((bytes+1)&~1u),chunks*512))return 0;
        for(unsigned i=0;i<bytes;i++)if((grid[i]&127)>chunks)return 0;
    }
    if(!cycles(b,n,l(b+0xEC)))return 0;
    a=l(b+0xF0);
    if(a) {
        p=guest(b,n,a,2);if(!p || w(p)>32)return 0;unsigned count=w(p);a+=2;
        for(unsigned i=0;i<count;i++) {
            p=guest(b,n,a,12);if(!p || !p[1] || !p[2] || w(p+6) || (p[3]&~31u))return 0;
            unsigned step=p[0]<128?1:2,frames=p[1],tiles=p[2],dst=w(p+4),max=0;uint32_t art=l(p+8);
            if(dst>2048 || tiles>2048-dst)return 0;
            p=guest(b,n,a+12,frames*step);if(!p)return 0;
            for(unsigned j=0;j<frames;j++)if(p[j*step]>max)max=p[j*step];
            if(!guest(b,n,art,(max+tiles)*32))return 0;
            a+=12+((frames*step+1)&~1u);
        }
    }
    a=l(b+0xF4);
    if(a) {
        p=guest(b,n,a,2);if(!p || w(p)>64)return 0;unsigned limits=w(p);
        const uint8_t *sets=guest(b,n,a+2+limits*2,limits+1);if(!sets)return 0;
        a+=2+limits*2+((limits+2)&~1u);p=guest(b,n,a,2);if(!p || !w(p) || w(p)>16)return 0;
        unsigned count=w(p);if(!guest(b,n,a+2,count*48+24))return 0;
        for(unsigned i=0;i<=limits;i++)if(sets[i]>=count)return 0;
        a+=2+count*48;
        for(unsigned i=0;i<4;i++) {
            uint32_t script=l(guest(b,n,a+i*4,4));if(!script)continue;
            p=guest(b,n,script,4);if(!p || p[0]+p[2]>64 || !guest(b,n,script,4+p[1]*p[2]*2))return 0;
        }
        p=guest(b,n,a+16,8);unsigned fw=w(p),fh=w(p+2);uint32_t grid=l(p+4);
        if(grid && (!fw || fw>128 || !fh || fh>32 || !guest(b,n,grid,fw*fh*2)))return 0;
    }
    return 1;
}
static int mapping(const uint8_t *b,uint32_t n,uint32_t address)
{
    const uint8_t *p=guest(b,n,address,2);if(!p)return 0;
    unsigned frames=w(p)/2;
    if(!frames || frames>256 || !guest(b,n,address,frames*2))return 0;
    for(unsigned i=0;i<frames;i++) {
        unsigned off=w(p+i*2);if(off<frames*2)return 0;
        const uint8_t *r=guest(b,n,address+off,2);
        if(!r || w(r)>80 || !guest(b,n,address+off,2+w(r)*6))return 0;
    }
    return 1;
}
static int blob(const uint8_t *b,uint32_t n)
{
    static const unsigned offsets[]={0,4,8,0x1C,0x20,0x28,0x2C,0x30,0x34,0x3C};
    static const unsigned sizes[]={24,4096,2048,2,4,256,4096,4096,1024,528};
    if(n<256 || n>SCD_WINDOW_SIZE || b[0x25]!=4)return 0;
    for(unsigned i=0;i<sizeof offsets/sizeof offsets[0];i++)if(!guest(b,n,l(b+offsets[i]),sizes[i]))return 0;
    const uint8_t *llb=guest(b,n,l(b),24);
    for(unsigned i=0;i<6;i++)if(!guest(b,n,l(llb+i*4)&0xFFFFFF,2))return 0;
    for(unsigned i=0;i<2;i++)if(!kosm(b,n,l(llb+i*4)&0xFFFFFF,65536))return 0;
    for(unsigned i=2;i<6;i++){unsigned decoded;uint32_t consumed;if(!kos(b,n,l(llb+i*4)&0xFFFFFF,i<4?0x3000:65536,&decoded,&consumed))return 0;}
    unsigned pals=w(b+0x44),plcs=w(b+0x4C);
    if(pals>16 || plcs>16 || w(b+0x46) || w(b+0x4E))return 0;
    const uint8_t *p=guest(b,n,l(b+0x40),pals*8);
    if(!p && pals)return 0;
    for(unsigned i=0;i<pals;i++) {
        const uint8_t *r=p+i*8;unsigned bytes=(w(r+6)+1)*4,dst=w(r+4);
        if(bytes>128 || dst<0xFC00 || dst>0xFC80 || bytes>0xFC80-dst || !guest(b,n,l(r),bytes))return 0;
    }
    p=guest(b,n,l(b+0x48),plcs*4);if(!p && plcs)return 0;
    for(unsigned i=0;i<plcs;i++) {
        uint32_t at=l(p+i*4);const uint8_t *r=guest(b,n,at,2);if(!r)return 0;
        unsigned count=w(r)+1;if(count>128 || !guest(b,n,at,2+count*6))return 0;
        for(unsigned k=0;k<count;k++) {
            uint32_t art=l(r+2+k*6);unsigned dst=w(r+6+k*6);
            if(dst>=0x10000 || (art!=0x190B4C && !kosm(b,n,art,65536-dst)))return 0;
        }
    }
    p=guest(b,n,l(b+0x34),1024);
    for(unsigned i=0;i<256;i++) {
        uint32_t code=l(p+i*4);
        if(code && ((code&1) || (code>=0x400000 && !(code>=0x7E2000 && code<0x7E4000 && !(code&3)))))return 0;
    }
    uint32_t objects=l(b+0x1C);int sentinel=0;
    for(unsigned i=0;i<4096;i++) {
        const uint8_t *r=guest(b,n,objects+i*6,2);if(!r)return 0;
        if(w(r)==0xFFFF){sentinel=1;break;}
        if(!guest(b,n,objects+i*6,6))return 0;
    }
    if(!sentinel)return 0;
    uint32_t assets=l(b+0x38);
    if(assets) {
        p=guest(b,n,assets,512*16);if(!p)return 0;
        for(unsigned i=0;i<512;i++) {
            const uint8_t *r=p+i*16;
            if(l(r) && !mapping(b,n,l(r)))return 0;
            if(l(r+8) && !guest(b,n,l(r+8),2))return 0;
            unsigned count=w(r+6);const uint8_t *segments=guest(b,n,l(r+12),count*8);
            if(count && (!segments || count>2048))return 0;
            for(unsigned k=0;k<count;k++) {
                const uint8_t *s=segments+k*8;unsigned tiles=w(s+6)&0x7FFF,dst=w(s+4);
                if(dst>2048 || tiles>2048-dst || !guest(b,n,l(s),tiles*32))return 0;
            }
        }
    }
    if(!kosm(b,n,l(b+0x54),0x4000) || !mapping(b,n,l(b+0x58)))return 0;
    uint32_t pager=l(b+0x50);
    if(pager) {
        p=guest(b,n,pager,22);if(!p)return 0;
        unsigned chunks=w(p),fw=w(p+2),fh=w(p+4),bw=w(p+6),bh=w(p+8);
        if(!chunks || chunks>2048 || !fw || fw>128 || !fh || fh>32 || !bw || bw>128 || !bh || bh>32 ||
           !guest(b,n,l(p+10),chunks*128))return 0;
        for(unsigned plane=0;plane<2;plane++) {
            unsigned count=plane?bw*bh:fw*fh;
            const uint8_t *grid=guest(b,n,l(p+14+plane*4),count*2);if(!grid)return 0;
            for(unsigned k=0;k<count;k++)if(w(grid+k*2)>=chunks)return 0;
        }
    }
    return extra(b,n);
}
static int levels(ScdPack *p)
{
    const uint8_t *b=p->levels;uint32_t n=p->levels_size;unsigned seen[84]={0};
    if(n<16+71*12 || memcmp(b,"TSCD",4) || w(b+4)!=2 || w(b+6)!=71 || l(b+8)!=SCD_WINDOW || l(b+12)!=SCD_WINDOW_SIZE)return 0;
    uint32_t end=16+71*12;
    for(unsigned i=0;i<71;i++) {
        const uint8_t *e=b+16+i*12;unsigned za=w(e),flags=w(e+2);uint32_t at=l(e+4),size=l(e+8);
        if(at!=end || !size || !span(b,n,at,size))return 0;
        end=at+size;
        if(za==0xFFFF) {
            if(flags!=1 || p->sound || size!=33195 || b[at+0x8000]!=(SCD_SOUND_BASE>>15))return 0;
            p->sound=b+at;p->sound_size=size;continue;
        }
        unsigned z=za>>8,a=za&255;
        if(flags || z<0x40 || z>=0x5C || a>2 || (a==2 && ((z-0x40)&3)<2) || seen[(z-0x40)*3+a]++)return 0;
        if(!blob(b+at,size))return 0;
    }
    if(end!=n || !p->sound)return 0;
    for(unsigned z=0;z<28;z++)for(unsigned a=0;a<3;a++)if(a<2 || (z&3)>=2)if(!seen[z*3+a])return 0;
    return 1;
}
const uint8_t *scd_pack_scene(const ScdPack *p,unsigned i,uint32_t *size)
{
    if(!p->scenes || i>=51)return NULL;
    const uint8_t *e=p->scenes+8+i*8;uint32_t at=l(e),n=l(e+4);
    if(size)*size=n;
    return span(p->scenes,p->scenes_size,at,n);
}
static int scenes(const ScdPack *p)
{
    const uint8_t *b=p->scenes;uint32_t n=p->scenes_size,end=8+51*8;
    if(n<end || memcmp(b,"TSSS",4) || w(b+4)!=1 || w(b+6)!=51)return 0;
    for(unsigned i=0;i<51;i++) {
        uint32_t at=l(b+8+i*8),size=l(b+12+i*8);
        if(at!=((end+3)&~3u) || !size || !span(b,n,at,size))return 0;
        for(uint32_t k=end;k<at;k++)if(b[k])return 0;
        end=at+size;
        if(i==0 && size!=0x50000)return 0;
        if(i==1 && size>0x6C00)return 0;
        if(i==2 && size!=116)return 0;
        if(i>=19 && i<=26 && size!=0x8000)return 0;
        if((i==47 && size!=2240) || (i==48 && size!=64))return 0;
        const uint8_t *item=b+at;
        if((i>=27 && i<=36) || i==46) {
            if(size<2 || w(item)>128 || !span(item,size,2,w(item)*4))return 0;
            unsigned count=w(item);uint32_t bytes=2+count*4;
            for(unsigned k=0;k<count;k++) {
                unsigned dst=w(item+2+k*4),tiles=w(item+4+k*4);
                if(tiles*32>65536-dst || !span(item,size,bytes,tiles*32))return 0;
                bytes+=tiles*32;
            }
            if(bytes!=size)return 0;
        }
        if(i>=37 && i<=45) {
            if(size<2 || w(item)>256 || !span(item,size,2,w(item)*12))return 0;
            unsigned ids[256]={0},count=w(item);
            for(unsigned k=0;k<count;k++) {
                const uint8_t *e=item+2+k*12;unsigned fw=w(e+4),fh=w(e+6),dst=w(e+2);
                if(ids[e[0]]++ || e[1] || !fw || fw>64 || !fh || fh>64 ||
                   dst+(fh-1)*128+fw*2>65536 || l(e+8)<2+count*12 || !span(item,size,l(e+8),fw*fh*2))return 0;
            }
        }
        if(i>=49) {
            if(size<3 || !item[0])return 0;
            uint32_t pos=1;
            for(unsigned k=0;k<item[0];k++) {
                const uint8_t *anim=span(item,size,pos,2);if(!anim || !anim[0] || anim[0]>16)return 0;
                unsigned frames=anim[0];pos+=2;
                for(unsigned f=0;f<frames;f++) {
                    const uint8_t *frame=span(item,size,pos,1);if(!frame || frame[0]>80 || !span(item,size,pos,1+frame[0]*5))return 0;
                    pos+=1+frame[0]*5;
                }
            }
            if(pos!=size)return 0;
        }
    }
    if(((end+3)&~3u)!=n)return 0;
    for(uint32_t k=end;k<n;k++)if(b[k])return 0;
    const uint8_t *labels=scd_pack_scene(p,2,NULL);uint32_t sub_size,main_size;
    scd_pack_scene(p,0,&sub_size);scd_pack_scene(p,1,&main_size);
    for(unsigned i=0;i<29;i++) {
        uint32_t a=l(labels+i*4);
        if(i<18){if(a<0x10000 || a-0x10000>=sub_size)return 0;}
        else if(i<21){if(a<0xFF2000 || a-0xFF2000>=main_size)return 0;}
        else {uint32_t stage_size;scd_pack_scene(p,3+i-21,&stage_size);if(a<0xFF8C00 || a-0xFF8C00+64>stage_size)return 0;}
    }
    return 1;
}
int scd_pack_open(ScdPack *out,const char *path,char *error,unsigned capacity)
{
    ScdPack p={0};
    if(!scd_resource_open(&p.resource,path,0,error,capacity))return 0;
    p.levels=scd_resource_item(&p.resource,"LEVELS",&p.levels_size);
    p.scenes=scd_resource_item(&p.resource,"SCENES",&p.scenes_size);
    if(!p.levels || !p.scenes || !levels(&p) || !scenes(&p)) {
        scd_resource_close(&p.resource);
        if(error && capacity)snprintf(error,capacity,"Invalid Sonic CD level/scene graph; rebuild the content pack");
        return 0;
    }
    *out=p;return 1;
}
int scd_pack_validate(const char *path,char *error,unsigned capacity)
{ScdPack p={0};int ok=scd_pack_open(&p,path,error,capacity);scd_resource_close(&p.resource);return ok;}
const uint8_t *scd_pack_level(const ScdPack *p,uint16_t za,uint32_t *size)
{
    if(!p->levels)return NULL;
    for(unsigned i=0;i<71;i++) {
        const uint8_t *e=p->levels+16+i*12;
        if(w(e)==za && !w(e+2)){if(size)*size=l(e+8);return p->levels+l(e+4);}
    }
    return NULL;
}
