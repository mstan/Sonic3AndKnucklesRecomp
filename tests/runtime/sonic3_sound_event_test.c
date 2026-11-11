#include "sonic3_sound_assets.h"
#include "sonic3_sound_themes.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint8_t *read_file(const char *path,size_t *size)
{FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);uint8_t *p=malloc((size_t)n);CHECK(p);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;}
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
    CHECK(tested==27);free(host);free(program);free(base);free(bank);return 0;
}
