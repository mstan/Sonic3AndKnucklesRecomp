#include "sonic3_sound_assets.h"
#include "sonic3_sound_themes.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static unsigned be16(const uint8_t *p){return ((unsigned)p[0]<<8)|p[1];}
static int final_echo_restores(const uint8_t *bank,uint32_t song,uint32_t used)
{
    unsigned pc=song+be16(bank+song+8+5*4);
    /* Follow the prototype's last FM channel, including its jump into the
     * first channel's phrase. It must restore, rather than merely stop. */
    for(unsigned guard=0;guard<1024 && pc<used;guard++){
        if(bank[pc]!=0xFF){++pc;continue;}
        if(pc+1>=used)return 0;
        unsigned op=bank[pc+1];
        if(op==4)return 1;
        if(op==0x10)return 0;
        if(op==0x14){
            if(pc+4>used)return 0;
            pc=(unsigned)((int)pc+3+(int16_t)be16(bank+pc+2));
        }else if(op==0x0D||op==1){pc+=3;}
        else if(op==0x21){pc+=6;}
        else return 0;
    }
    return 0;
}
static uint8_t *read_file(const char *path,size_t *size)
{FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);uint8_t *p=malloc((size_t)n);CHECK(p);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;}
static void mixed_retail_envelopes(char **argv,const S3SoundLayout *seq,const S3SoundPCMLayout *pcm,
                                   const uint8_t *program,const uint8_t *base,uint32_t base_size,uint8_t *bank)
{
    S3SoundSource source[2];uint8_t *rom[2];size_t size[2];unsigned ids[]={0x28,0x29};
    char error[256]={0};
    for(unsigned d=0;d<2;d++){
        rom[d]=read_file(argv[8+d],&size[d]);
        CHECK(s3_sound_read_source(rom[d],size[d],&source[d],error,sizeof error));
    }
    for(unsigned order=0;order<2;order++){
        memcpy(bank,base,base_size);uint32_t used=base_size;unsigned next=97;
        for(unsigned j=0;j<2;j++){
            unsigned d=order?1-j:j;
            CHECK(s3_sound_build_event(rom[d],size[d],S3_DONOR_S3+d,d?S3_THEME_CLEAR:S3_THEME_CONTINUE,
                ids[d],seq,pcm,program,bank,0x400000,&used,&next,error,sizeof error));
        }
        /* Both cartridge tables must still match their own ROM after the
         * other event is appended, regardless of configuration order. */
        for(unsigned d=0;d<2;d++){
            uint32_t input=s3_sound_music_source(&source[d],rom[d],ids[d]);
            unsigned original=rom[d][input+6+rom[d][input+2]*4+5];CHECK(original);
            uint32_t song=(be32(bank+seq->music_index-0x400000+4*(ids[d]-1))&0xFFFFFF)-0x400000;
            unsigned table_base=bank[song+8+bank[song+2]*4+5]-original;
            for(unsigned env=0;env<source[d].volume_count;env++){
                unsigned p=source[d].volume_pointers+env*2;
                p=source[d].z80[p]|((unsigned)source[d].z80[p+1]<<8);
                unsigned n=0;while(n<254 && !(source[d].z80[p+n]>=0x80 && source[d].z80[p+n]<=0x83))++n;
                CHECK(n<254);n+=1+(source[d].z80[p+n]==0x82);
                uint32_t actual=be32(bank+seq->psg_index-0x400000+4*(table_base+env));
                CHECK(actual>=0x400000 && actual+n<=0x400000+used);
                CHECK(!memcmp(bank+actual-0x400000,source[d].z80+p,n));
            }
        }
    }
    free(rom[0]);free(rom[1]);
}
static void individual_tracks(char **argv,const S3SoundLayout *seq,const S3SoundPCMLayout *pcm,
                              const uint8_t *program,const uint8_t *base,uint32_t base_size,uint8_t *bank)
{
    static const unsigned cases[][3]={
        {S3_DONOR_S1,17,0x31},{S3_DONOR_S2,30,0x31},{S3_DONOR_PROTO,0x31,0x31},
        {S3_DONOR_S1,11,0x19},{S3_DONOR_S2,18,0x19},{S3_DONOR_WAI,18,0x19},
        {S3_DONOR_S1,11,0x18},{S3_DONOR_S2,18,0x18},{S3_DONOR_WAI,18,0x18},
        {S3_DONOR_S3,0x2E,0x18},{S3_DONOR_SK,0x18,0x2E},{S3_DONOR_PROTO,0x18,0x2E},
        {S3_DONOR_S3,0x25,0x25},{S3_DONOR_SK,0x25,0x25},{S3_DONOR_PROTO,0x25,0x25},
        {S3_DONOR_S3,0x19,0x19},{S3_DONOR_SK,0x19,0x19},{S3_DONOR_PROTO,0x19,0x19},
        {S3_DONOR_S3,0x30,0x30},{S3_DONOR_SK,0x30,0x30},{S3_DONOR_PROTO,0x30,0x30},
        {S3_DONOR_S1,11,0x30},{S3_DONOR_S2,18,0x30},{S3_DONOR_WAI,18,0x30},
        {S3_DONOR_S3,0x26,0x33},{S3_DONOR_SK,0x33,0x26},{S3_DONOR_PROTO,0x26,0x33}
    };
    char error[256]={0};
    for(unsigned i=0;i<sizeof cases/sizeof *cases;i++){
        unsigned donor=cases[i][0],song=cases[i][1],target=cases[i][2];size_t size;uint8_t *rom=read_file(argv[3+donor],&size);
        memcpy(bank,base,base_size);uint32_t used=base_size;unsigned next=70;S3SoundMusicCache cache={0};
        int ok=s3_sound_build_track(rom,size,donor,song,target,seq,pcm,program,bank,0x400000,&used,&next,&cache,error,sizeof error);
        if(!ok)fprintf(stderr,"new donor %u music %02X: %s\n",donor,song,error);CHECK(ok);
        for(unsigned id=1;id<=51;id++)if(id!=target)CHECK(!memcmp(bank+seq->music_index-0x400000+4*(id-1),base+seq->music_index-0x400000+4*(id-1),4));
        if(target==0x31)CHECK(be32(bank+seq->music_index-0x400000+4*(target-1))&1);
        CHECK(!memcmp(bank+seq->sound_index-0x400000,base+seq->sound_index-0x400000,169*4));
        s3_sound_music_cache_free(&cache);free(rom);
    }
    /* A complete per-track prototype selection must fit together, preserve
     * the original SFX and never append duplicate envelope tables per act. */
    size_t size;uint8_t *rom=read_file(argv[3+S3_DONOR_PROTO],&size);S3SoundMusicCache cache={0};
    memcpy(bank,base,base_size);uint32_t used=base_size;unsigned next=70;uint8_t envelopes[39*4];
    for(unsigned target=1;target<=51;target++){
        if(target==0x32)continue; /* Prototype has no Ending song. */
        unsigned song=target==0x33?0x26:target==0x2E?0x18:target;
        int ok=s3_sound_build_track(rom,size,S3_DONOR_PROTO,song,target,seq,pcm,program,bank,0x400000,&used,&next,&cache,error,sizeof error);
        if(!ok)fprintf(stderr,"prototype track %02X from %02X: %s\n",target,song,error);CHECK(ok);
        if(target==1)memcpy(envelopes,bank+seq->psg_index-0x400000+64*4,sizeof envelopes);
        else CHECK(!memcmp(envelopes,bank+seq->psg_index-0x400000+64*4,sizeof envelopes));
    }
    CHECK(next<=97&&used<0x400000);CHECK(!memcmp(bank+seq->sound_index-0x400000,base+seq->sound_index-0x400000,169*4));
    /* The proposed Wai mini-boss/retail boss pairing also fits over a full
     * campaign selection without touching either the other song or SFX. */
    for(unsigned i=0;i<2;i++){
        unsigned donor=i?S3_DONOR_S2:S3_DONOR_WAI;size_t n;uint8_t *legacy=read_file(argv[3+donor],&n);
        CHECK(s3_sound_build_track(legacy,n,donor,18,i?0x19:0x18,seq,pcm,program,bank,0x400000,&used,&next,&cache,error,sizeof error));
        /* Release the conversion cache before its source ROM. */
        s3_sound_music_cache_free(&cache);free(legacy);
    }
    free(rom);printf("PASS new drowning/boss/mini-boss/title/credits conversions and 50 simultaneous prototype track entries; PCM next=%u\n",next);
}
int main(int argc,char **argv)
{
    CHECK(argc==10);size_t host_size,program_size;uint8_t *host=read_file(argv[1],&host_size),*program=read_file(argv[2],&program_size);
    CHECK(!memcmp(program,"S3CR",4)&&be32(program+4)==program_size);
    S3SoundLayout seq={(uint32_t)program_size,be32(program+60),be32(program+64),be32(program+68),be32(program+72)};
    S3SoundPCMLayout pcm={be32(program+56),be32(program+84),be32(program+88),be32(program+92),be32(program+96)};
    CHECK(pcm.sample_count==126);
    uint8_t *base=calloc(1,0x400000),*bank=malloc(0x400000);CHECK(base&&bank);memcpy(base,program,program_size);
    char error[256]={0};S3SoundAssetStats stats;
    CHECK(s3_sound_build_sequences(host,host_size,&seq,base,0x400000,&stats,error,sizeof error));
    CHECK(s3_sound_build_pcm(host,host_size,&pcm,base,0x400000,&stats.bytes,error,sizeof error));
    static const unsigned target[]={0x2A,0x27,0x28,0x29,0x2C};unsigned tested=0;
    for(unsigned d=0;d<S3_ROM_DONOR_COUNT;d++){
        size_t size;uint8_t *rom=read_file(argv[3+d],&size);uint8_t before[32],after[32];recompui_sha256_compute(rom,size,before);
        for(unsigned t=0;t<S3_THEME_COUNT;t++){
            if((d==S3_DONOR_AUG&&t!=S3_THEME_INVINCIBLE)||(d==S3_DONOR_WAI&&t!=S3_THEME_CLEAR))continue;
            memcpy(bank,base,stats.bytes);uint32_t used=stats.bytes;unsigned next=97;*error=0;
            int ok=s3_sound_build_event(rom,size,d,t,target[t],&seq,&pcm,program,bank,0x400000,&used,&next,error,sizeof error);
            if(!ok)fprintf(stderr,"donor %u theme %u: %s\n",d,t,error);CHECK(ok);
            CHECK(used>stats.bytes&&used<0x400000&&next>=97&&next<=126);
            uint32_t song=be32(bank+seq.music_index-0x400000+4*(target[t]-1))&0xFFFFFF;
            CHECK(song>=0x400000+stats.bytes&&song<0x400000+used);
            if(d==S3_DONOR_PROTO && t==S3_THEME_LIFE)
                CHECK(final_echo_restores(bank,song-0x400000,used));
            /* Only this song's entry, PSG/modulation resources and newly
             * assigned PCM descriptors may alter the original bank. */
            for(unsigned id=1;id<=51;id++)if(id!=target[t])CHECK(!memcmp(bank+seq.music_index-0x400000+4*(id-1),base+seq.music_index-0x400000+4*(id-1),4));
            CHECK(!memcmp(bank+seq.sound_index-0x400000,base+seq.sound_index-0x400000,169*4));
            for(unsigned i=97;i<next;i++){
                const uint8_t *p=bank+pcm.sample_table-0x400000+i*10;CHECK((p[0]&15)==2&&p[1]);uint32_t a=be32(p+2),b=be32(p+6);CHECK(a>=0x400000+stats.bytes&&b>a&&b<=0x400000+used);
            }
            printf("PASS event donor %u theme %u; %u appended bytes, %u additional percussion variants\n",d,t,used-stats.bytes,next-97);++tested;
        }
        recompui_sha256_compute(rom,size,after);CHECK(!memcmp(before,after,32));free(rom);
    }
    CHECK(tested==27);mixed_retail_envelopes(argv,&seq,&pcm,program,base,stats.bytes,bank);
    individual_tracks(argv,&seq,&pcm,program,base,stats.bytes,bank);
    free(host);free(program);free(base);free(bank);return 0;
}
