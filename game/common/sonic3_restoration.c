/* Optional family artwork assembled from the user's verified cartridges. */
#include "sonic3_restoration.h"
#include "sonic3_gameplay.h"
#include "sonic3_state_io.h"
#include "sonic3_art.h"
#include "sonic3_video.h"
#include "sonic3_sound_driver.h"
#include "genesis_runtime.h"
#ifdef S3_SOUND_COMBINED
#include "stage_pack.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned name_art,name_map,level_names;
static int bound,names_active;
enum { PROTO_PALETTES=1,PROTO_BADNIKS=2,PROTO_BACKGROUNDS=4 };
static unsigned selected,active,donor_address;
static int standalone_s3,standalone_sk;
static char settings_file[1200],error[256];
typedef struct {uint8_t *pixels;unsigned width,height;uint16_t colors[64],water[64];} Background;
static Background backgrounds[3];
static int prepare_backgrounds(const uint8_t *rom);
static int background_pixel(const uint8_t *ram,int x,int y,int sx,int sy,uint16_t *attr,uint8_t *pixel,uint16_t *color);
int s3_restoration_select(unsigned component,int on)
{
    *error=0;
    if(component!=PROTO_PALETTES && component!=PROTO_BADNIKS && component!=PROTO_BACKGROUNDS)return 0;
    if(standalone_sk && component==PROTO_BADNIKS)return 0;
#ifdef S3_SOUND_COMBINED
    if(on && s3_stage_pack_enabled){snprintf(error,sizeof error,"Disable imported campaigns before enabling prototype graphics.");return 0;}
#endif
    if(on){uint8_t *rom=s3_sound_prototype_read();if(!rom){snprintf(error,sizeof error,"%s",s3_sound_error());return 0;}free(rom);}
    if(on)selected|=component;else selected&=~component;return 1;
}
int s3_restoration_netplay_allowed(void){return !names_active && !selected && !active;}
void s3_restoration_load(void)
{
    bound=names_active=0;selected=active=donor_address=0;name_art=name_map=level_names=0;*settings_file=*error=0;
    for(unsigned i=0;i<3;i++){free(backgrounds[i].pixels);memset(&backgrounds[i],0,sizeof backgrounds[i]);}
    s3_video_set_title_card_name_map(0);s3_video_set_background_override(NULL);s3_video_set_palette_source(NULL);
}
void s3_restoration_settings(const char *path,const char *mode)
{
    s3_restoration_load();standalone_s3=!strcmp(mode,"sonic3");standalone_sk=!strcmp(mode,"sandk");
    snprintf(settings_file,sizeof settings_file,"%s",path?path:"settings.ini");
    char *a=strrchr(settings_file,'/'),*b=strrchr(settings_file,'\\');
    char *name=a&&(!b||a>b)?a+1:b?b+1:settings_file;
    snprintf(name,sizeof settings_file-(size_t)(name-settings_file),"%s-restoration.ini",mode);
    FILE *f=fopen(settings_file,"rb");if(!f)return;
    char line[128];while(fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"prototype_palettes=1"))selected|=PROTO_PALETTES;
        if(!standalone_sk && !strcmp(line,"prototype_badniks=1"))selected|=PROTO_BADNIKS;
        if(!strcmp(line,"prototype_backgrounds=1"))selected|=PROTO_BACKGROUNDS;
    }fclose(f);
}
static unsigned word(unsigned a){return (unsigned)g_rom[a]<<8|g_rom[a+1];}
static unsigned longword(unsigned a){return word(a)<<16|word(a+2);}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static unsigned bg_word(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static size_t bg_tiles(const uint8_t *rom,unsigned address,uint8_t *out,size_t cap)
{
    size_t total=bg_word(rom+address),pos=2,n=0;
    if(total>cap)return 0;
    while(n<total){
        size_t used=0,want=total-n>4096?4096:total-n;
        if(pos>=0x200000u-address || art_kos(rom+address+pos,0x200000u-address-pos,out+n,want,&used)!=want)return 0;
        n+=want;pos+=used;pos=2+((pos-2+15)&~(size_t)15);
    }return n;
}
static int prepare_backgrounds(const uint8_t *rom)
{
    /* Decode only the donor's visual graph. None of these chunks, blocks or
     * layouts enter guest RAM or the native collision/object system. */
    static const unsigned source[3][7]={
        {0x1DE572,0x152B8E,0x153E60,0x153B70,0x156742,0x152F7E,0x1546F0},
        {0x1E340E,0x16CDCC,0,0x1705EE,0,0x16DB1C,0},
        {0x1E4278,0x17315E,0,0x176ED0,0,0x173E9E,0}};
    uint8_t *scratch=malloc(0x1A000);if(!scratch)return 0;
    uint8_t *blocks=scratch,*chunks=scratch+0x2000,*tiles=scratch+0xA000;
    int ok=0;
    for(unsigned which=0;which<3;which++){
        if((standalone_sk && which==0) || (standalone_s3 && which>0))continue;
        const unsigned *s=source[which];size_t used=0;
        size_t nb=art_kos(rom+s[1],0x200000u-s[1],blocks,0x2000,&used);
        if(!nb)goto done;
        if(s[2]){size_t n=art_kos(rom+s[2],0x200000u-s[2],blocks+nb,0x2000-nb,&used);if(!n)goto done;nb+=n;}
        size_t nc=art_kos(rom+s[3],0x200000u-s[3],chunks,0x8000,&used);
        if(!nc)goto done;
        if(s[4]){size_t n=art_kos(rom+s[4],0x200000u-s[4],chunks+nc,0x8000-nc,&used);if(!n)goto done;nc+=n;}
        size_t nt=bg_tiles(rom,s[5],tiles,0x10000);if(!nt)goto done;
        if(s[6]){size_t n=bg_tiles(rom,s[6],tiles+nt,0x10000-nt);if(!n)goto done;nt+=n;}
        if(which==0){
            /* Dynamic_Hz_1 installs these raw background banks after the
             * level tiles. Their placeholders in KosM are unrelated art.
             * This artwork option uses the donor's complete static banks;
             * native foreground water animation still runs unchanged. */
            if(nt<0x5500)goto done;
            memcpy(tiles+0x5100,rom+0x8A9E0,0x400);
            memcpy(tiles+0x4D00,rom+0x8B1E0,0x400);
        }
        const uint8_t *layout=rom+s[0];unsigned wide=bg_word(layout+2),high=bg_word(layout+6);
        if(!wide || wide>32 || !high || high>32)goto done;
        Background *bg=&backgrounds[which];bg->width=wide*128;bg->height=high*128;
        /* FBZ's repeating panorama occupies the first four chunks per axis.
         * Act 2's larger layout also contains blank columns and other banks;
         * wrapping into those exposes the backdrop at the expanded edge. */
        if(which)bg->width=bg->height=512;
        unsigned pal=which?0x1E9DD4u:0x1E9B54u;
        for(unsigned i=16;i<64;i++)bg->colors[i]=(uint16_t)bg_word(rom+pal+(i-16)*2);
        for(unsigned i=0;i<64;i++)bg->water[i]=(uint16_t)bg_word(rom+0x1E9C14u+i*2);
        bg->pixels=malloc((size_t)bg->width*bg->height);if(!bg->pixels)goto done;
        for(unsigned y=0;y<bg->height;y++)for(unsigned x=0;x<bg->width;x++){
            unsigned row=bg_word(layout+10+(y>>7)*4);
            if(row<0x8000 || (size_t)s[0]+row-0x8000+(x>>7)>=0x200000)goto done;
            unsigned ci=(unsigned)layout[row-0x8000+(x>>7)]*128+((y&112u)+(x&112u)/8);
            if(ci+1>=nc)goto done;
            unsigned block=bg_word(chunks+ci),tx=(x>>3)&1,ty=(y>>3)&1;
            if(block&0x400)tx^=1;if(block&0x800)ty^=1;
            unsigned bi=(block&1023u)*8+(ty*2+tx)*2;if(bi+1>=nb)goto done;
            unsigned a=bg_word(blocks+bi)^((block&0xC00u)<<1),px=x&7,py=y&7;
            if(a&0x800)px^=7;if(a&0x1000)py^=7;
            unsigned ti=(a&2047u)*32+py*4+px/2;if(ti>=nt)goto done;
            unsigned p=tiles[ti];p=px&1?p&15:p>>4;
            bg->pixels[(size_t)y*bg->width+x]=p?(uint8_t)(p|((a>>9)&48u)|((a>>8)&128u)):0;
        }
    }ok=1;
done:free(scratch);return ok;
}
static int background_pixel(const uint8_t *ram,int x,int y,int sx,int sy,uint16_t *attr,uint8_t *pixel,uint16_t *color)
{
    if(!(active&PROTO_BACKGROUNDS) || (ram[0xF600]&127)!=12 || !ram[0xF711] || ram[0xF664])return 0;
    unsigned which;
    if(!standalone_sk && ram[0xFE10]==1 && !ram[0xFE11] && bg_word(ram+0xEE80)<0x3680)which=0;
    else if(!standalone_s3 && ram[0xFE10]==4 && ram[0xFE11]<2 && !ram[0xF7AA])which=1+ram[0xFE11];
    else return 0;
    const Background *bg=&backgrounds[which];if(!bg->pixels)return 0;
    if(!attr || !pixel)return 1;
    if(which){
        /* The prototype's Level_Deform ($341EA) uses one-eighth X/Y scroll.
         * Retail FBZ selects unrelated indoor/outdoor canvases, so feeding
         * its VScroll into this donor layout can select an empty region. */
        x=sx+((int16_t)bg_word(ram+0xEE80)>>3);
        y=sy+((int16_t)bg_word(ram+0xEE84)>>3);
    }
    x%=(int)bg->width;y%=(int)bg->height;if(x<0)x+=(int)bg->width;if(y<0)y+=(int)bg->height;
    unsigned p=bg->pixels[(size_t)y*bg->width+(unsigned)x];
    *attr=(uint16_t)(((p&48u)<<9)|((p&128u)<<8));*pixel=(uint8_t)(p&63u);
    if(color){
        *color=0xFFFF;
        if((active&PROTO_PALETTES) && *pixel>=16)
            *color=!which && (int16_t)bg_word(ram+0xEE84)+sy>=(int)bg_word(ram+0xF646)?bg->water[*pixel]:bg->colors[*pixel];
    }return 1;
}
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
static unsigned palette_source(unsigned address)
{
    if(!(active&PROTO_PALETTES))return address;
    static const unsigned retail_s3[]={0x8C594,0x8C5F4,0x8C654,0x8C6D4,0x8C9D4,0x8CA34,0x8CA94,0x8CB14};
    static const unsigned retail_sk[]={0xA8D9C,0xA8DFC,0xA8E5C,0xA8EDC,0xA929C,0xA92FC,0xA935C,0xA93DC};
    static const unsigned donor[]={0x1E9B54,0x1E9BB4,0x1E9C14,0x1E9C94,0x1E9EF4,0x1E9FD4,0x1EA034,0x1EA0B4};
    /* The wide HCZ boss retains stage bricks from the locked-on cartridge's
     * palette bank, while native palette loaders use the S&K copy. */
    if(!standalone_s3){
        if(address==0x28C594u)address=0xA8D9C;
        if(address==0x28C654u)address=0xA8E5C;
    }
    for(unsigned n=0;n<8;n++)if(address==(standalone_s3?retail_s3[n]:retail_sk[n]))return donor_address+donor[n];
    if(!standalone_s3 && (address==0xA909Cu || address==0xA90FCu))return donor_address+0x1E9DD4u;
    return address;
}
void s3_restoration_post_reset(void)
{
    if(bound)return;bound=1;names_active=s3_gameplay.early_zone_names;
    active=selected;
#ifdef S3_SOUND_COMBINED
    if(active && s3_stage_pack_enabled){fprintf(stderr,"[restoration] Disable imported campaigns before prototype graphics\n");exit(1);}
#endif
    if(active){
        uint8_t *donor=s3_sound_prototype_read();
        if(!donor){fprintf(stderr,"[restoration] %s\n",s3_sound_error());exit(1);}
        if((active&PROTO_BACKGROUNDS) && !prepare_backgrounds(donor)){free(donor);fprintf(stderr,"[restoration] Cannot decode prototype backgrounds\n");exit(1);}
        if(active&PROTO_BACKGROUNDS)s3_video_set_background_override(background_pixel);
        /* Keep the base cartridge immutable. Native asset consumers read the
         * verified donor through the same extended ROM bus as custom fonts. */
        unsigned offset=(g_rom_ext_size+1)&~1u;
        if(offset>0x200000u){free(donor);fprintf(stderr,"[restoration] Prototype graphics ROM bank is full\n");exit(1);}
        uint8_t *bank=realloc(g_rom_ext,offset+0x200000u);
        if(!bank){free(donor);fprintf(stderr,"[restoration] Cannot allocate prototype graphics\n");exit(1);}
        if(offset>g_rom_ext_size)memset(bank+g_rom_ext_size,0,offset-g_rom_ext_size);
        memcpy(bank+offset,donor,0x200000);free(donor);
        g_rom_ext=bank;g_rom_ext_size=offset+0x200000u;donor_address=0x400000u+offset;
        if(active&PROTO_PALETTES)s3_video_set_palette_source(palette_source);
        fprintf(stderr,"[restoration] Verified November prototype graphics enabled (mask %u)\n",active);
    }
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
    /* At these sites MOVEA has already fetched the palette source, before
     * any original copy/fade logic runs. No colors are written every frame. */
    const unsigned *sites;
    static const unsigned s3_pal[]={0x3500,0x351C,0x3534,0x3550};
    static const unsigned sk_pal[]={0x3DCA,0x3DE6,0x3DFE,0x3E1A};
    sites=standalone_s3?s3_pal:sk_pal;
    for(unsigned i=0;i<4;i++)if(pc==sites[i]){
        g_cpu.A[2]=palette_source(g_cpu.A[2]);
        return 1;
    }
    if(pc==(standalone_s3?0x1D5Cu:0x1AD2u)){
        if(!names_active)return 1;
    }
    if(pc==(standalone_s3?0x1932u:0x16A8u)){
        /* Both streams use non-XOR Nemesis. The prototype needs nine tiles,
         * within the retail sixteen-tile reservation. Queued loads retain
         * the tile count in D2; immediate loads keep the row count in A5. */
        if((active&PROTO_BADNIKS) && g_cpu.A[0]==(standalone_s3?0x16A3E2u:0x36A3E2u)){
            g_cpu.A[0]=donor_address+0x11667Eu;
            if((g_cpu.D[2]&65535u)==16)g_cpu.D[2]=(g_cpu.D[2]&0xFFFF0000u)|9u;
            else if(!g_cpu.D[2] && g_cpu.A[5]==128)g_cpu.A[5]=72;
        }
        return 1;
    }
    if(pc==(standalone_s3?0x529ACu:0x8403Cu)){
        unsigned o=g_cpu.A[0]&65535u;
        if((active&PROTO_BADNIKS) && o>=0xB0DE && o+0x4A<=0xCAE2){
            unsigned map=(unsigned)g_ram[o+12]<<24|(unsigned)g_ram[o+13]<<16|(unsigned)g_ram[o+14]<<8|g_ram[o+15];
            if(map==(standalone_s3?0x160EB4u:0x360EB4u)){
                map=donor_address+0x10DE4Eu;
                g_ram[o+12]=(uint8_t)(map>>24);g_ram[o+13]=(uint8_t)(map>>16);g_ram[o+14]=(uint8_t)(map>>8);g_ram[o+15]=(uint8_t)map;
            }
        }
        return 1;
    }
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
    identity=active;
    if(s3_state_peek_unsigned(io,identity)!=identity)io->ok=0;
    S3_STATE(io,identity);
}

#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include "host_atomic_file.h"
static const RecompLauncherCModProvider *base;
static const char package[]="sonic3.restoration.graphics";
static const char *feature_ids[]={"prototype-palettes","prototype-badniks","prototype-backgrounds"};
static int ours(const char *p){return p&&!strcmp(p,package);}
static int own_count(void){return standalone_sk?2:3;}
static int component_index(int i){return standalone_sk && i==1?2:i;}
#define COPY(d,s) snprintf(d,sizeof(d),"%s",s)
static int package_count(void *c){(void)c;return 1+(base&&base->package_count?base->package_count(base->ctx):0);}
static int package_get(void *c,int i,RecompLauncherCModPackage *o)
{
    (void)c;if(i>0)return base&&base->package_get&&base->package_get(base->ctx,i-1,o);
    if(i<0||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,package);COPY(o->name,"November 1993 prototype graphics");COPY(o->version,"0.1.0");
    COPY(o->author,"Sonic3AndKnucklesRecomp contributors");COPY(o->description,"Optional graphics read from your verified November 3, 1993 Sonic 3 prototype cartridge.");
    COPY(o->source_name,"Hidden Palace prototype documentation");COPY(o->source_url,"https://hiddenpalace.org/Sonic_the_Hedgehog_3_(Nov_3,_1993_prototype)");
    COPY(o->license,"User-supplied cartridge assets");o->enabled=selected!=0;return 1;
}
static int feature_count(void *c){(void)c;return own_count()+(base&&base->feature_count?base->feature_count(base->ctx):0);}
static int feature_get(void *c,int i,RecompLauncherCModFeature *o)
{
    if(i>=own_count())return base&&base->feature_get&&base->feature_get(base->ctx,i-own_count(),o);
    RecompLauncherCModPackage p;if(i<0||!o||!package_get(c,0,&p))return 0;memset(o,0,sizeof *o);
    i=component_index(i);
    COPY(o->id,feature_ids[i]);COPY(o->package_id,package);COPY(o->package_name,p.name);COPY(o->package_version,p.version);
    COPY(o->name,i==2?"Prototype backgrounds":i?"Prototype Hydrocity badnik art":"Prototype stage palettes");COPY(o->group,"Restoration");COPY(o->author,p.author);
    COPY(o->description,i?"Use the prototype Bugernaut graphics in Hydrocity. Native movement, collision and swarming behavior are preserved. Requires your verified donor ROM. Default off; restart to apply. Local play only.":standalone_sk?"Use the prototype Flying Battery colors with the complete retail stage. Requires your verified donor ROM. Default off; restart to apply. Local play only.":standalone_s3?"Use the prototype Hydrocity and Launch Base colors with the complete retail stages. Underwater colors use the native transitions and fades. Requires your verified donor ROM. Default off; restart to apply. Local play only.":"Use the prototype Hydrocity, Flying Battery and Launch Base colors with the complete retail stages. Underwater colors use the native transitions and fades. Requires your verified donor ROM. Default off; restart to apply. Local play only.");
    if(i==2)COPY(o->description,standalone_sk?"Use the prototype Flying Battery background artwork and its original slow parallax with complete retail terrain. Boss arenas and moving collision scenery retain their native graphics. Enable prototype palettes for the earlier colors. Requires your donor ROM. Default off; restart to apply. Local play only.":standalone_s3?"Use the prototype Hydrocity Act 1 background artwork with retail parallax and complete retail terrain. The miniboss arena retains its native graphics. Enable prototype palettes for the earlier colors. Requires your donor ROM. Default off; restart to apply. Local play only.":"Use the prototype Hydrocity Act 1 and Flying Battery background artwork. Flying Battery uses its original slow parallax; retail terrain is preserved. Boss arenas and moving collision scenery retain their native graphics. Enable prototype palettes for the earlier colors. Requires your donor ROM. Default off; restart to apply. Local play only.");
    COPY(o->source_name,p.source_name);COPY(o->source_url,p.source_url);
    unsigned bit=1u<<i;o->enabled=(selected&bit)!=0;
    COPY(o->status,(selected&bit)!=(active&bit)?"Restart to apply the selection":active&bit?"Enabled":s3_sound_prototype_verified()?"Verified donor; disabled":"Select your November 3, 1993 prototype ROM");return 1;
}
static int enable(void *c,const char *p,const char *f,int on)
{
    (void)c;*error=0;if(!ours(p))return base&&base->feature_enable&&base->feature_enable(base->ctx,p,f,on);
    int i;for(i=0;i<own_count();i++)if(f&&!strcmp(f,feature_ids[component_index(i)]))break;if(i==own_count())return 0;
    return s3_restoration_select(1u<<component_index(i),on);
}
static int package_enable(void *c,const char *p,int on)
{
    if(!ours(p))return base&&base->set_enabled&&base->set_enabled(base->ctx,p,on);
    for(int i=0;i<own_count();i++)if(!enable(c,p,feature_ids[component_index(i)],on))return 0;return 1;
}
static int commit(void *c,const char *image)
{
    (void)c;*error=0;
    for(int i=0;i<own_count();i++)if(!enable(c,package,feature_ids[component_index(i)],(selected&(1u<<component_index(i)))!=0))return 0;
    if(base&&base->commit&&!base->commit(base->ctx,image))return 0;
    char text[128];int n=snprintf(text,sizeof text,"prototype_palettes=%u\nprototype_badniks=%u\nprototype_backgrounds=%u\n",(selected&1)!=0,(selected&2)!=0,(selected&4)!=0);
    if(!genesis_atomic_file_store(settings_file,text,(size_t)n)){COPY(error,"Unable to save restoration options.");return 0;}return 1;
}
static int netplay(void *c,const char *image)
{
    if(!s3_restoration_netplay_allowed()){COPY(error,"Disable restoration options and restart before netplay.");return 0;}
    return !base||!base->commit_netplay||base->commit_netplay(base->ctx,image);
}
static const char *last_error(void *c){(void)c;return *error?error:base&&base->last_error?base->last_error(base->ctx):"";}
static int resources(void *c,const char *p,const char *f)
{(void)c;if(ours(p))return f&&(!strcmp(f,feature_ids[0])||!strcmp(f,feature_ids[2])||(!standalone_sk&&!strcmp(f,feature_ids[1])));return base&&base->feature_resource_count?base->feature_resource_count(base->ctx,p,f):0;}
static int resource_get(void *c,const char *p,const char *f,int i,RecompLauncherCModResource *o)
{
    if(!ours(p))return base&&base->feature_resource_get&&base->feature_resource_get(base->ctx,p,f,i,o);
    if(!resources(c,p,f)||i||!o)return 0;memset(o,0,sizeof *o);
    COPY(o->id,"prototype-rom");COPY(o->label,"November 3, 1993 prototype ROM");
    COPY(o->description,"Shared donor for prototype music and graphics. Extract the ROM archive first. Expected 2,097,152 bytes; SHA-1 fc87a7b411302a576028b7ccd5911b1f266b59b3.");
    COPY(o->file_patterns,"*.bin,*.md,*.gen");COPY(o->file_description,"Genesis ROM");COPY(o->path,s3_sound_prototype_path());
    o->required=1;o->verified=s3_sound_prototype_verified();COPY(o->status,o->verified?"Verified November 3, 1993 prototype":"Choose your unmodified donor ROM");return 1;
}
static int resource_path(void *c,const char *p,const char *f,const char *r,const char *path)
{
    if(ours(p))return resources(c,p,f)&&r&&!strcmp(r,"prototype-rom")&&s3_sound_prototype_set_path(path);
    return base&&base->feature_resource_set_path&&base->feature_resource_set_path(base->ctx,p,f,r,path);
}
const RecompLauncherCModProvider *s3_restoration_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;base=common;if(base)p=*base;else memset(&p,0,sizeof p);
    p.package_count=package_count;p.package_get=package_get;p.set_enabled=package_enable;p.feature_count=feature_count;p.feature_get=feature_get;p.feature_enable=enable;
    p.commit=commit;p.commit_netplay=netplay;p.last_error=last_error;p.feature_resource_count=resources;p.feature_resource_get=resource_get;p.feature_resource_set_path=resource_path;return &p;
}
#else
const struct RecompLauncherCModProvider *s3_restoration_mods(const struct RecompLauncherCModProvider *base){return base;}
#endif
