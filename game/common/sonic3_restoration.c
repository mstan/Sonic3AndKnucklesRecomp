/* Optional family content assembled only from the user's retail cartridge. */
#include "sonic3_restoration.h"
#include "sonic3_gameplay.h"
#include "sonic3_state_io.h"
#include "sonic3_art.h"
#include "sonic3_video.h"
#include "genesis_runtime.h"
#ifdef S3_SOUND_COMBINED
#include "stage_pack.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned name_art,name_map,level_names;
static int bound,names_active;
int s3_restoration_netplay_allowed(void){return !names_active;}
void s3_restoration_load(void)
{bound=names_active=0;name_art=name_map=level_names=0;s3_video_set_title_card_name_map(0);}
static unsigned word(unsigned a){return (unsigned)g_rom[a]<<8|g_rom[a+1];}
static unsigned longword(unsigned a){return word(a)<<16|word(a+2);}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static size_t font(unsigned zone,uint8_t *dst,size_t cap)
{
    unsigned a=longword(0x2DA16u+zone*4);size_t used=0;
    if(a>sizeof g_rom-0x1000u)return 0;
    unsigned size=word(a);if(size>cap || size>4096)return 0;
    return art_kos(g_rom+a+2,0xFFE,dst,cap,&used)==size?size:0;
}
/* Match the native decoder's descriptor prefetch before bit16's payload. */
typedef struct { uint8_t *p;size_t n,desc;unsigned bit; } LiteralKos;
static void bit(LiteralKos *w,unsigned value)
{
    w->p[w->desc+w->bit/8]|=(uint8_t)(value<<(w->bit&7));
    if(++w->bit==16){w->bit=0;w->desc=w->n;w->n+=2;}
}
static size_t literals(uint8_t *dst,const uint8_t *src,size_t size)
{
    put16(dst,(unsigned)size);LiteralKos w={dst+2,2,0,0};
    for(size_t i=0;i<size;i++){bit(&w,1);w.p[w.n++]=src[i];}
    bit(&w,0);bit(&w,1);w.p[w.n++]=0;w.p[w.n++]=0;w.p[w.n++]=0;
    return w.n+2;
}
static void piece(uint8_t *p,unsigned size,unsigned tile,int x)
{p[0]=0;p[1]=(uint8_t)size;put16(p+2,0x8000u|tile);put16(p+4,(unsigned)x);}
void s3_restoration_post_reset(void)
{
    if(bound)return;bound=1;names_active=s3_gameplay.early_zone_names;
    if(!names_active)return;
#ifdef S3_SOUND_COMBINED
    if(s3_stage_pack_enabled)goto failed;
#endif
    uint8_t mhz[4096],lrz[4096],ssz[4096],blob[8192]={0};
    size_t n=font(7,mhz,sizeof mhz);
    if(n!=0x4E0 || font(9,lrz,sizeof lrz)!=0x360 || font(10,ssz,sizeof ssz)!=0x600)goto failed;
    /* V/A from Lava Reef, Y from Sky Sanctuary. O/L/E already exist in the
     * Mushroom Hill and shared ZONE art. No reconstructed pixels are shipped. */
    memcpy(mhz+n,lrz+(0x562-0x54D)*32,192);
    memcpy(mhz+n+192,lrz,192);
    memcpy(mhz+n+384,ssz+(0x577-0x54D)*32,192);
    n+=576;
    size_t at=literals(blob,mhz,n);at=(at+1)&~(size_t)1;
    unsigned map_offset=(unsigned)at;
    for(unsigned i=0;i<12;i++)put16(blob+at+i*2,24);
    put16(blob+at+22,26);put16(blob+at+24,0);put16(blob+at+26,14);
    unsigned original=0x2EE10u+word(0x2EE10u+22);
    if(word(original)!=10)goto failed;
    for(unsigned i=0;i<8;i++){
        memcpy(blob+at+28+i*6,g_rom+original+2+i*6,6);
        int x=(int16_t)word(original+2+i*6+4);
        put16(blob+at+28+i*6+4,(unsigned)(x-40));
    }
    const unsigned sizes[]={6,6,2,2,6,6},tiles[]={0x574,0x57A,0x556,0x556,0x51C,0x580};
    const int xs[]={48,64,80,88,96,112};
    for(unsigned i=0;i<6;i++)piece(blob+at+28+(8+i)*6,sizes[i],tiles[i],xs[i]);
    at+=112;unsigned text_offset=(unsigned)at,source=0x80E6;
    for(unsigned row=0;row<17;row++){
        unsigned size=g_rom[source]+1;
        if(row==6){
            static const char name[]="MUSHROOM VALLEY";blob[at++]=sizeof name-2;
            for(unsigned i=0;i<sizeof name-1;i++)blob[at++]=name[i]==' '?0:(uint8_t)(name[i]-'A'+30);
        } else {memcpy(blob+at,g_rom+source,size+1);at+=size+1;}
        source+=size+1;
    }
    unsigned offset=(g_rom_ext_size+1)&~1u;
    if(offset>0x3F0000u || at>sizeof blob)goto failed;
    uint8_t *bank=realloc(g_rom_ext,offset+at);
    if(!bank)goto failed;
    if(offset>g_rom_ext_size)memset(bank+g_rom_ext_size,0,offset-g_rom_ext_size);
    memcpy(bank+offset,blob,at);g_rom_ext=bank;g_rom_ext_size=offset+(unsigned)at;
    name_art=0x400000u+offset;name_map=name_art+map_offset;level_names=name_art+text_offset;
    s3_video_set_title_card_name_map(name_map);
    fprintf(stderr,"[restoration] Mushroom Valley native title/level-select name enabled\n");return;
failed:
    fprintf(stderr,"[restoration] Cannot prepare Mushroom Valley artwork from this cartridge\n");exit(1);
}
int s3_restoration_instruction(unsigned pc)
{
    if(!names_active)return 0;
    if(pc==0x7B40u){g_cpu.A[1]=level_names;return 1;}
    if(pc==0x1AD2u && g_cpu.A[1]==longword(0x2DA16u+7*4)){
        g_cpu.A[1]=name_art;return 1;
    }
    if(pc==0x2D92Cu && g_ram[0xEE4E]==7){
        unsigned o=g_cpu.A[0]&65535u;
        if(o>=0xB0DE && o+0x4A<=0xCAE2){
            g_ram[o+12]=(uint8_t)(name_map>>24);g_ram[o+13]=(uint8_t)(name_map>>16);
            g_ram[o+14]=(uint8_t)(name_map>>8);g_ram[o+15]=(uint8_t)name_map;
        }
        return 1;
    }
    return 0;
}
void s3_restoration_state(S3StateIO *io)
{
    unsigned identity=(unsigned)names_active;
    if(s3_state_peek_unsigned(io,identity)!=identity)io->ok=0;
    S3_STATE(io,identity);
}
