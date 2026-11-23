#ifdef S3_SOUND_HLE
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "trilogy_music_hle.h"
#include "scd_pack.h"
#include "sonic3_sound_hle_assets.h"
#include "sonic3_sound_assets.h"
#include "audio/smps_hle.h"
#include <stdio.h>
#include <string.h>

enum { CAPACITY=0x400000, HOST_BASE=0x400000, FOREIGN_EFFECTS=173 };
typedef struct Resource {const uint8_t *data;size_t bytes;uint32_t base;} Resource;
typedef struct Root {uint32_t offset;uint8_t flags;} Root;
static Root music[4][3][32];
static uint32_t effects[4][2][FOREIGN_EFFECTS];
static uint8_t effects_present[4][2];
static uint8_t effect_ids[4][2][81];
static const uint8_t envelope_slots[13]={0x1E,0x1F,0x20,0x21,0x23,0x24,0x25,0x27,0x12,6,7,0xB,0x10};
static unsigned little(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t big(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(24-i*8));}
static const uint8_t *span(Resource r,uint32_t address,size_t count)
{
    if(address<r.base || address-r.base>r.bytes || count>r.bytes-(address-r.base))return NULL;
    return r.data+address-r.base;
}
static uint32_t pointer(Resource r,uint32_t address)
{const uint8_t *p=span(r,address,4);return p?big(p):0;}
static const uint8_t *window(Resource r,unsigned bank,size_t *bytes)
{
    uint32_t address=bank*0x8000u;const uint8_t *p=span(r,address,1);
    *bytes=p?r.bytes-(address-r.base):0;if(*bytes>0x8000)*bytes=0x8000;return p;
}
static int error_text(char *error,size_t bytes,const char *message)
{if(error && bytes)snprintf(error,bytes,"%s",message);return 0;}
static int append(S3HleSoundAssets *a,const uint8_t *data,size_t count,uint32_t *start)
{
    uint32_t at=(a->bytes+1u)&~1u;
    if(at>CAPACITY || count>CAPACITY-at)return 0;
    memcpy(a->data+at,data,count);a->bytes=at+(uint32_t)count;*start=at;return 1;
}
static int envelopes(S3HleSoundAssets *a,const uint8_t *raw,size_t bytes,const uint8_t *table,
                     unsigned songs,unsigned content,unsigned game,uint8_t map[256])
{
    unsigned count=table[0];if(count>13)return 0;
    if(!count)return 1;
    unsigned prefix=0;
    for(unsigned song=0;song<songs;song++){
        const uint8_t *entry=table+2+count*2+song*4;
        if(entry[0] && little(entry+2)>=0x8000){prefix=little(entry+2)&0x7FFF;break;}
    }
    if(!prefix || prefix>bytes)return 0;
    uint32_t at;if(!append(a,raw,prefix,&at))return 0;
    for(unsigned i=0;i<count;i++){
        unsigned p=little(table+2+i*2);if(p<0x8000 || (p&0x7FFF)>=prefix)return 0;
        unsigned id=65+((content-1)*3+(game-1))*16+i;if(id>255)return 0;
        map[envelope_slots[i]]=(uint8_t)id;
        put(a->data+0x500+(id-1)*4,HOST_BASE+at+(p&0x7FFF));
    }
    return 1;
}
static int convert(S3HleSoundAssets *a,const S3SoundSource *source,Resource r,unsigned bank,unsigned ptr,
                    int is_music,const uint8_t *psg,const uint8_t *dac,Root *out,char *error,size_t en)
{
    size_t bytes;const uint8_t *raw=window(r,bank,&bytes);
    if(!raw || ptr<0x8000 || (ptr&0x7FFF)>=bytes)return error_text(error,en,"Invalid campaign sound resource pointer");
    unsigned at=(a->bytes+1u)&~1u;if(at>CAPACITY || CAPACITY-at<0x8000)return error_text(error,en,"Campaign sound resources exceed host storage");
    S3SoundCustomInfo info={0};
    if(!s3_sound_convert_bank(raw,bytes,ptr&0x7FFF,is_music,source,psg,dac,a->data+at,0x8000,&info,error,en)){
        char reason[192];snprintf(reason,sizeof reason,"%s",error);
        snprintf(error,en,"%s bank %02X pointer %04X: %s",is_music?"Music":"Effect",bank,ptr,reason);return 0;
    }
    a->bytes=at+info.bytes;out->offset=at;out->flags=(uint8_t)(info.restores?SMPS_HLE_INTERRUPT:0);return 1;
}
static int samples(S3HleSoundAssets *a,const S3SoundSource *source,Resource r,uint32_t table,
                    unsigned content,uint8_t map[128],char *error,size_t en)
{
    if(!table)return 1;
    const uint8_t *meta=span(r,table,3);if(!meta || meta[1]!=0xB4 || meta[2]!=17)return error_text(error,en,"Invalid campaign percussion descriptor");
    size_t size;const uint8_t *raw=window(r,meta[0],&size);if(!raw)return 0;
    for(unsigned i=0;i<17;i++){
        unsigned note=0xB4+i,entry=(note-0x81)*2;
        if(entry+2>size)return 0;
        unsigned header=little(raw+entry);if(header<0x8000 || (header&0x7FFF)+5>size)return 0;
        const uint8_t *h=raw+(header&0x7FFF);unsigned count=little(h+1),ptr=little(h+3);
        if(!count || ptr<0x8000 || (ptr&0x7FFF)>size || count>size-(ptr&0x7FFF) || a->bytes>CAPACITY || 2u*count>CAPACITY-a->bytes)return 0;
        unsigned slot=70+(content-1)*17+i;uint32_t at=a->bytes;unsigned accumulator=128;
        for(unsigned k=0;k<count;k++){
            unsigned value=raw[(ptr&0x7FFF)+k];
            accumulator=(accumulator+source->z80[0x1116+(value>>4)])&255;a->data[a->bytes++]=(uint8_t)accumulator;
            accumulator=(accumulator+source->z80[0x1116+(value&15)])&255;a->data[a->bytes++]=(uint8_t)accumulator;
        }
        unsigned numerator=2u*3579545u,denominator=303u+26u*(h[0]?h[0]:256u);
        uint8_t *d=a->data+0xA00+slot*10;d[0]=0x12;d[1]=(uint8_t)(((uint64_t)numerator*256u+denominator*12550u)/(denominator*25100u));
        put(d+2,HOST_BASE+at);put(d+6,HOST_BASE+a->bytes);
        put(a->data+0x1000+slot*8,numerator);put(a->data+0x1004+slot*8,denominator);
        map[note&127]=(uint8_t)(slot+0x81);
    }
    return 1;
}
int tri_hle_sound_prepare(S3HleSoundAssets *a,const S3SoundSource *source,char *error,size_t en)
{
    memset(music,0,sizeof music);memset(effects,0,sizeof effects);memset(effects_present,0,sizeof effects_present);memset(effect_ids,0,sizeof effect_ids);
    for(unsigned content=1;content<=4;content++){
        Resource r={0};r.base=TRI_BANK_BASE;
        if(content==4){const ScdPack *pack=tri_scd_pack();r.data=pack->sound;r.bytes=pack->sound_size;r.base=SCD_SOUND_BASE;}
        else r.data=tri_bank_resource((int)content,&r.bytes);
        if(!r.data || !r.bytes)continue;
        uint8_t dac[128],psg[3][256];
        for(unsigned i=0;i<128;i++)dac[i]=(uint8_t)(0x80+i);
        for(unsigned g=0;g<3;g++)for(unsigned i=0;i<256;i++)psg[g][i]=(uint8_t)i;
        uint32_t blobs=content==4?0:pointer(r,TRI_BANK_BASE+TB_BLOBS);
        if(content<4 && !samples(a,source,r,pointer(r,blobs+TRI_BLOB_S2_DAC*4),content,dac,error,en))return error_text(error,en,"Invalid campaign percussion data");
        for(unsigned game=1;game<=3;game++){
            if(content==4 || (game==3 && content!=3))continue;
            unsigned last=game==1?0x93:game==2?0x9F:0x97,count=last-0x80;
            uint32_t address=game==3?pointer(r,pointer(r,TRI_BANK_BASE+0x24)+16):pointer(r,blobs+(game==1?TRI_BLOB_S1_MUSIC:TRI_BLOB_S2_MUSIC)*4);
            if(!address)continue;
            const uint8_t *head=span(r,address,2);if(!head || head[0]>13)return error_text(error,en,"Invalid campaign music header");
            const uint8_t *table=span(r,address,2+head[0]*2+count*4);if(!table)return 0;
            const uint8_t *entries=table+2+head[0]*2;size_t bytes=0;const uint8_t *raw=NULL;
            for(unsigned i=0;i<count;i++)if(entries[i*4] && little(entries+i*4+2)){raw=window(r,entries[i*4],&bytes);break;}
            if(!raw || !envelopes(a,raw,bytes,table,count,content,game,psg[game-1]))return error_text(error,en,"Invalid campaign music envelopes");
            for(unsigned i=0;i<count;i++){
                const uint8_t *entry=entries+i*4;if(!entry[0] || !little(entry+2))continue;
                if(!convert(a,source,r,entry[0],little(entry+2),1,psg[game-1],game==2?dac:NULL,&music[content-1][game-1][i],error,en))return 0;
            }
        }
        unsigned set=content==4?2:1;uint32_t address=content==4?SCD_SOUND_BASE+0x8000:pointer(r,blobs+TRI_BLOB_S2_SFX*4);
        if(!address)continue;
        const uint8_t *table=span(r,address,1+FOREIGN_EFFECTS*2+(set==1?81:80));if(!table || !table[0])return error_text(error,en,"Invalid campaign effect table");
        memcpy(effect_ids[content-1][set-1],table+1+FOREIGN_EFFECTS*2,set==1?81:80);
        for(unsigned i=0;i<FOREIGN_EFFECTS;i++){
            unsigned ptr=little(table+1+i*2);if(!ptr)continue;
            Root root={0};if(!convert(a,source,r,table[0],ptr,0,set==1?psg[1]:NULL,set==1?dac:NULL,&root,error,en))return 0;
            effects[content-1][set-1][i]=root.offset;
        }
        effects_present[content-1][set-1]=1;
    }
    /* A scene may borrow another campaign's songs after switching its stage
     * resource. Resolve those immutable aliases once, before gameplay. */
    for(unsigned content=0;content<4;content++){
        for(unsigned game=0;game<3;game++)for(unsigned song=0;song<32;song++)
            if(!music[content][game][song].offset)music[content][game][song]=music[game][game][song];
        for(unsigned set=0;set<2;set++)if(!effects_present[content][set]){
            unsigned owner=set==0?1:3;
            if(effects_present[owner][set]){
                memcpy(effects[content][set],effects[owner][set],sizeof effects[content][set]);
                memcpy(effect_ids[content][set],effect_ids[owner][set],sizeof effect_ids[content][set]);
                effects_present[content][set]=1;
            }
        }
    }
    return 1;
}
uint32_t tri_hle_music_root(unsigned content,unsigned game,unsigned song,unsigned *flags)
{
    if(content<1 || content>4 || game<1 || game>3 || song<0x81 || song>0xA0)return 0;
    Root r=music[content-1][game-1][song-0x81];if(flags)*flags=r.flags;return r.offset;
}
uint32_t tri_hle_effect_root(unsigned content,unsigned set,unsigned native)
{return content>=1 && content<=4 && set>=1 && set<=2 && native>=0x33 && native<=0xDF?effects[content-1][set-1][native-0x33]:0;}
int tri_hle_effects_available(unsigned content,unsigned set)
{return content>=1 && content<=4 && set>=1 && set<=2 && effects_present[content-1][set-1];}
unsigned tri_hle_effect_id(unsigned content,unsigned set,unsigned id)
{
    unsigned first=set==1?0xA0:0x90,last=set==1?0xF0:0xDF;
    return content>=1 && content<=4 && set>=1 && set<=2 && id>=first && id<=last?effect_ids[content-1][set-1][id-first]:0;
}
#endif
