#include "sonic3_sound_themes.h"
#include "sonic3_sound_driver.h"
#include "sonic3_cd_themes.h"
#include "scd_import.h"
#include "host_atomic_file.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

S3ThemeSelection s3_themes[S3_MUSIC_COUNT];
static uint64_t cd_available;
static int group_on[S3_MUSIC_GROUPS]={1,1,1};
/* Data Select moved from the campaign to Shared. Retain its saved campaign
 * switch until the player deliberately changes the Shared section. */
static int data_group_on=1;
#define TRACK(symbol,id,...) id,
const char *const s3_theme_ids[]={
#include "sonic3_music_catalog.inc"
};
#undef TRACK
#define TRACK(symbol,id,label,...) label,
const char *const s3_theme_names[]={
#include "sonic3_music_catalog.inc"
};
#undef TRACK
typedef struct MusicRow {unsigned group,target;const char *section;unsigned choices;} MusicRow;
enum {LIFE,OVER,CONTINUE,CLEAR,INVINCIBLE,DROWN,BOSS,MINIBOSS,TITLE,PROTOTYPE,MENU,CREDITS,KNUCKLES,FINAL,SPECIAL,DATA,SKY};
#define TRACK(symbol,id,label,group,section,target,choices) {group,target,section,choices},
static const MusicRow rows[]={
#include "sonic3_music_catalog.inc"
};
#undef TRACK
const char *const s3_donor_ids[]={"s1","s2","s2-aug","s2-wai","s3-proto","s3","sk","cd-us"};
const char *const s3_donor_names[]={"Sonic 1","Sonic 2","Sonic 2 (Aug 21, 1992 prototype)","Sonic 2 (Simon Wai prototype)","Sonic 3 (Nov 3, 1993 prototype)","Sonic 3","Sonic & Knuckles","Sonic CD (US)"};
static const S3ThemeChoice choices[][7]={
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK}},
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s3k","Sonic 3 & Knuckles",-1},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK}},
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s2-wai","Sonic 2 Simon Wai prototype",S3_DONOR_WAI},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"s3k","Sonic 3 & Knuckles",-1},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
    {{"s1","Sonic 1",S3_DONOR_S1},{"s2-aug","Sonic 2 Aug 21, 1992 prototype",S3_DONOR_AUG},{"s2","Sonic 2",S3_DONOR_S2},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US},{"s3-proto","Unused November 1993 prototype theme",S3_DONOR_PROTO}},
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO}},
    {{"s1","Sonic 1",S3_DONOR_S1},{"s2","Sonic 2",S3_DONOR_S2},{"s2-wai","Sonic 2 Simon Wai prototype",S3_DONOR_WAI},{"s3","Sonic 3 / Sonic & Knuckles",-1},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
    {{"s1","Sonic 1",S3_DONOR_S1},{"s2","Sonic 2",S3_DONOR_S2},{"s2-wai","Sonic 2 Simon Wai prototype",S3_DONOR_WAI},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
    {{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO}},
    {{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO}},
    {{"s3","Retail (Sonic 3)",S3_DONOR_S3},{"s3-proto","Prototype (November 3, 1993)",S3_DONOR_PROTO}},
    {{"s3","Sonic 3 (Retail)",S3_DONOR_S3},{"sk","Sonic & Knuckles (Retail)",S3_DONOR_SK},{"s3-proto","Sonic 3 Prototype",S3_DONOR_PROTO}},
    {{"s3","Sonic 3",S3_DONOR_S3},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"sk","Sonic & Knuckles",S3_DONOR_SK}},
    {{"s1","Sonic 1",S3_DONOR_S1},{"s2","Sonic 2",S3_DONOR_S2},{"s2-wai","Sonic 2 Simon Wai prototype",S3_DONOR_WAI},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
    {{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"s2","Sonic 2",S3_DONOR_S2},{"s2-wai","Sonic 2 Simon Wai prototype",S3_DONOR_WAI}},
    {{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK},{"s2","Sonic 2 Options",S3_DONOR_S2},{"s2-wai","Sonic 2 Simon Wai Level Select",S3_DONOR_WAI}},
    {{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"s3","Sonic 3 (unused)",S3_DONOR_S3}},
};
static const unsigned counts[]={4,3,3,5,7,2,6,7,3,1,2,3,3,7,3,5,2};
static const char *const hashes[]={
    "46160baa06362c711c9f1a5017cb7371026444936c8af5e93a78996cf32ff2a6",
    "193bc4064ce0daf27ea9e908ed246d87ec576cc294833badebb590b6ad8e8f6b",
    "cd1f37fb244f200531b5c93378115fcf2f665fa78ba41548a10bff0ee64f0473",
    "c2e63635703e4dac7b0a2bb383abe5a001c128ab8ab98d33519c31d75b59cc8d",
    "ace6af3187c1c2da4e71a1c24fb81f2529cb25057f093f5cb5389842a90d4abf",
    "d6404843c5ba32486f4c8c744acb7e1932069376960d886b1de37969787d3f9c",
    "6e12e6b33c26ebfcd0be433251d21cf6284eafe9f71b027bda3767ae59affec1"};
static const size_t sizes[]={0x80000,0x100000,0x100000,0x100000,0x200000,0x200000,0x200000};
static char file[1200],library_settings[1200],paths[S3_DONOR_COUNT][1024],error[256];
static int verified[S3_DONOR_COUNT],host_s3,host_combined;
static char prototype_alias[1024];
static int prototype_from_alias;
#include "sonic3_custom_music.inc"
const char *s3_themes_error(void){return error;}
void s3_themes_clear_error(void){*error=0;}
const char *s3_donor_path(unsigned d){return d==S3_DONOR_PROTO&&prototype_from_alias?prototype_alias:d<S3_DONOR_COUNT?paths[d]:"";}
int s3_donor_verified(unsigned d){return d<S3_DONOR_COUNT && verified[d];}
int s3_theme_choices(unsigned t){return t<S3_MUSIC_COUNT?(int)counts[rows[t].choices]+1:0;}
const S3ThemeChoice *s3_theme_choice(unsigned t,unsigned c){static const S3ThemeChoice custom={"custom","Custom music",S3_DONOR_CUSTOM};return t>=S3_MUSIC_COUNT?NULL:c==counts[rows[t].choices]?&custom:c<counts[rows[t].choices]?&choices[rows[t].choices][c]:NULL;}
unsigned s3_music_group(unsigned t)
{return t<S3_MUSIC_COUNT?(host_combined&&rows[t].group==S3_MUSIC_SANDK?S3_MUSIC_SONIC3:rows[t].group):S3_MUSIC_GROUPS;}
const char *s3_music_section(unsigned t){return t<S3_MUSIC_COUNT?rows[t].section:"";}
unsigned s3_music_target(unsigned t){return (t==S3_MUSIC_CREDITS&&!host_s3)||t==S3_MUSIC_SUPER?0x33:t<S3_MUSIC_COUNT?rows[t].target:0;}
int s3_music_available(unsigned t)
{return t<S3_MUSIC_COUNT&&t!=S3_MUSIC_EMERALD&&(t!=S3_MUSIC_SUPER||host_s3)&&(!rows[t].group||(rows[t].group==S3_MUSIC_SONIC3?(host_s3||host_combined):!host_s3));}
int s3_music_active(unsigned t){return s3_music_available(t)&&(t==S3_MUSIC_DATA?data_group_on:group_on[rows[t].group])&&s3_themes[t].enabled;}
unsigned s3_music_source(unsigned t,unsigned donor)
{
    static const unsigned s1[]={7,14,15,13,6,17,11,11,9};
    static const unsigned s2[]={23,26,27,25,22,30,18,18,24};
    if(t>=S3_MUSIC_COUNT)return 0;
    if(donor<=S3_DONOR_WAI){
        if(t==S3_MUSIC_SPECIAL && donor==S3_DONOR_S2)return 17;
        if(t==S3_MUSIC_DATA && donor==S3_DONOR_S2)return 16;
        /* Simon Wai's own playlist: Special Stage $89, Level Select $8A.
         * These entries precede the retail revision's Options/SS slots. */
        if(t==S3_MUSIC_SPECIAL && donor==S3_DONOR_WAI)return 8;
        if(t==S3_MUSIC_DATA && donor==S3_DONOR_WAI)return 9;
        /* Each revision's native playlist is authoritative: the Wai $93
         * entry is an early EHZ 2P song, while its boss is native $8E. */
        if(t==S3_MUSIC_FINAL)return donor==S3_DONOR_S1?12:donor==S3_DONOR_WAI?11:19;
        if(donor==S3_DONOR_WAI){
            if(t==S3_MUSIC_BOSS||t==S3_MUSIC_MINIBOSS)return 13;
            if(t==S3_THEME_INVINCIBLE)return 20;
        }
        return t<9?(donor==S3_DONOR_S1?s1[t]:s2[t]):0;
    }
    if(t==S3_MUSIC_MINIBOSS)return donor==S3_DONOR_S3?0x2E:0x18;
    if(t==S3_THEME_INVINCIBLE&&donor==S3_DONOR_PROTO)return 0x2E;
    if(t==S3_MUSIC_CREDITS)return donor==S3_DONOR_SK?0x33:0x26;
    if(t==S3_MUSIC_SUPER)return donor==S3_DONOR_PROTO?0x2E:0x32;
    return rows[t].target;
}
static int donor_identity(const uint8_t *rom,size_t size)
{
    uint8_t hash[32];char hex[65];recompui_sha256_compute(rom,size,hash);
    for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",hash[i]);
    for(unsigned i=0;i<S3_ROM_DONOR_COUNT;i++)if(size==sizes[i]&&!strcmp(hex,hashes[i]))return (int)i;
    return -1;
}
uint8_t *s3_donor_read(unsigned d,size_t *size)
{
    if(d>=S3_ROM_DONOR_COUNT){if(d<S3_DONOR_COUNT){cd_available=s3_cd_donor_available(paths[d],d);verified[d]=cd_available!=0;}return NULL;}
    FILE *f=fopen(paths[d],"rb");int from_alias=0;
    if(!f && d==S3_DONOR_PROTO && *prototype_alias){f=fopen(prototype_alias,"rb");from_alias=1;}
    if(!f){verified[d]=0;return NULL;}
    uint8_t *rom=NULL;
    if(!fseek(f,0,SEEK_END) && ftell(f)==(long)sizes[d]){
        rewind(f);rom=malloc(sizes[d]);
        if(rom && fread(rom,1,sizes[d],f)!=sizes[d]){free(rom);rom=NULL;}
    }
    fclose(f);
    if(rom && donor_identity(rom,sizes[d])!=(int)d){free(rom);rom=NULL;}
    verified[d]=rom!=NULL;if(d==S3_DONOR_PROTO)prototype_from_alias=rom&&from_alias;
    if(rom&&size)*size=sizes[d];return rom;
}
void s3_theme_offer_prototype(const char *path)
{
    if(!path||!*path||strlen(path)>=sizeof prototype_alias||strpbrk(path,"\r\n"))return;
    memmove(prototype_alias,path,strlen(path)+1);size_t size;free(s3_donor_read(S3_DONOR_PROTO,&size));
}
static int host_has(int donor)
{return donor<0 || (donor==S3_DONOR_S3 && (host_s3||host_combined)) || (donor==S3_DONOR_SK && !host_s3);}
int s3_music_donor(unsigned t)
{
    if(t>=S3_MUSIC_COUNT)return -1;
    const S3ThemeChoice *c=s3_theme_choice(t,(unsigned)s3_themes[t].choice);if(!c)return -1;
    int d=c->donor;
    if(!strcmp(c->id,"s1-s2") && !verified[S3_DONOR_S1] && verified[S3_DONOR_S2])d=S3_DONOR_S2;
    return d<0?(host_s3?S3_DONOR_S3:S3_DONOR_SK):d;
}
static const char *choice_id(unsigned t,const char *id)
{
    if(!id)return id;
    if(t==S3_MUSIC_BOSS&&!strcmp(id,"sk"))return "s3";
    if(t==S3_MUSIC_MINIBOSS&&!strcmp(id,"sk-proto"))
        return host_has(S3_DONOR_SK)||verified[S3_DONOR_SK]?"sk":"s3-proto";
    return id;
}
int s3_theme_donor(unsigned t){int d=s3_music_donor(t);return host_has(d)?-1:d;}
int s3_theme_ready(unsigned t){int d=s3_theme_donor(t);if(d==S3_DONOR_CUSTOM)return s3_custom_ready(t);return t<S3_MUSIC_COUNT&&(d<0||(d==S3_DONOR_CD_US?!!(cd_available&(UINT64_C(1)<<t)):verified[d]));}
int s3_themes_enabled(void){for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_active(t))return 1;return 0;}
int s3_theme_enable(unsigned t,int on)
{
    *error=0;if(t>=S3_MUSIC_COUNT)return 0;
    int d=s3_theme_donor(t);
    if(on && d>=0){size_t n;free(s3_donor_read((unsigned)d,&n));}
    if(on && !s3_theme_ready(t)){snprintf(error,sizeof error,"Provide the selected theme's ROM first.");return 0;}
    if(on && !s3_sound_select(1)){snprintf(error,sizeof error,"%s",s3_sound_error());return 0;}
    s3_themes[t].enabled=on!=0;if(on){group_on[rows[t].group]=1;if(t==S3_MUSIC_DATA)data_group_on=1;}return 1;
}
int s3_theme_choose(unsigned t,const char *id)
{
    *error=0;if(t>=S3_MUSIC_COUNT||!id)return 0;
    id=choice_id(t,id);
    for(unsigned c=0;c<(unsigned)s3_theme_choices(t);c++)if(!strcmp(id,s3_theme_choice(t,c)->id)){
        s3_themes[t].choice=(int)c;
        if(s3_theme_ready(t)){
            if(!s3_theme_enable(t,1))s3_themes[t].enabled=0;
        }else s3_themes[t].enabled=0;
        return 1;
    }
    snprintf(error,sizeof error,"Unknown theme choice");return 0;
}
int s3_music_group_enabled(unsigned group)
{for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_group(t)==group&&s3_music_active(t))return 1;return 0;}
static void group_set(unsigned group,int on)
{
    /* Keep the two stored campaign switches intact on load. A deliberate
     * change to the combined campaign section controls both halves. */
    group_on[group]=on;
    if(group==S3_MUSIC_SHARED)data_group_on=on;
    if(host_combined&&group==S3_MUSIC_SONIC3)group_on[S3_MUSIC_SANDK]=on;
}
int s3_music_group_enable(unsigned group,int on)
{
    *error=0;if(group>=S3_MUSIC_GROUPS)return 0;
    if(!on){group_set(group,0);return 1;}
    unsigned selected=0;int seen[S3_DONOR_COUNT]={0};
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_group(t)==group&&s3_music_available(t)&&s3_themes[t].enabled){
        ++selected;int d=s3_theme_donor(t);if(d>=0&&d<S3_DONOR_COUNT&&!seen[d]++){size_t size;free(s3_donor_read((unsigned)d,&size));}
        if(!s3_theme_ready(t)){group_set(group,0);snprintf(error,sizeof error,"Provide the selected music's matching ROM below.");return 0;}
    }
    if(!selected){snprintf(error,sizeof error,"Choose a replacement track first.");return 0;}
    if(!s3_sound_select(1)){snprintf(error,sizeof error,"%s",s3_sound_error());return 0;}
    group_set(group,1);return 1;
}
int s3_music_pick(unsigned t,const char *id)
{
    *error=0;if(!s3_music_available(t)||!id)return 0;
    id=choice_id(t,id);
    if(!strcmp(id,"base")){s3_themes[t].enabled=0;return 1;}
    for(unsigned c=0;c<(unsigned)s3_theme_choices(t);c++)if(!strcmp(id,s3_theme_choice(t,c)->id)){
        s3_themes[t].choice=(int)c;s3_themes[t].enabled=1;
        if(!s3_music_group_enable(s3_music_group(t),1)){group_set(s3_music_group(t),0);*error=0;}
        return 1;
    }
    snprintf(error,sizeof error,"Unknown music choice");return 0;
}
void s3_themes_load(const char *settings,const char *mode)
{
    memset(s3_themes,0,sizeof s3_themes);memset(verified,0,sizeof verified);cd_available=0;*error=0;*prototype_alias=0;prototype_from_alias=0;
    for(unsigned g=0;g<S3_MUSIC_GROUPS;g++)group_on[g]=1;
    data_group_on=1;
    host_s3=!strcmp(mode,"sonic3");host_combined=!strcmp(mode,"sonic3k");
    snprintf(file,sizeof file,"%s",settings?settings:"settings.ini");
    char *a=strrchr(file,'/'),*b=strrchr(file,'\\');if(!a||(b&&b>a))a=b;
    size_t prefix=a?(size_t)(a-file)+1:0;
    snprintf(file+prefix,sizeof file-prefix,"%s-themes.ini",mode);custom_load();
    snprintf(library_settings,sizeof library_settings,"%s",settings?settings:"settings.ini");
    /* Explicit profile overrides isolate QA and portable installs. Ordinary
     * installs share one donor library across all three executables. */
    const char *shared=getenv("SONIC_SOUND_DONOR_LIBRARY");
    if(shared&&*shared&&strlen(shared)<1024&&!strpbrk(shared,"\r\n")){
        snprintf(library_settings,sizeof library_settings,"%s/settings.ini",shared);
    }else if(!getenv("GENESIS_USER_DIR")){
        char *root=SDL_GetPrefPath("GenesisRecomp","SonicSoundDonors");
        if(root){snprintf(library_settings,sizeof library_settings,"%ssettings.ini",root);SDL_free(root);}
    }
    a=strrchr(library_settings,'/');b=strrchr(library_settings,'\\');if(!a||(b&&b>a))a=b;
    prefix=a?(size_t)(a-library_settings)+1:0;
    for(unsigned d=0;d<S3_DONOR_COUNT;d++){
        snprintf(paths[d],sizeof paths[d],"%.*ssonic-sound-donors/%s.%s",(int)prefix,library_settings,s3_donor_ids[d],d<S3_ROM_DONOR_COUNT?"bin":"ini");
        size_t n;free(s3_donor_read(d,&n));
    }
    s3_theme_offer_prototype(s3_sound_prototype_path());
    FILE *f=fopen(file,"rb");if(!f)return;
    char line[256];int invalid_choice[S3_MUSIC_COUNT]={0},grouped=0,data_group_explicit=0;
    while(fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        char *value=strchr(line,'=');if(!value)continue;*value++=0;
        if(!strcmp(line,"data-select.group-enabled")){data_group_on=!strcmp(value,"1");data_group_explicit=1;}
        for(unsigned g=0;g<S3_MUSIC_GROUPS;g++){
            char key[64];snprintf(key,sizeof key,"group.%u.enabled",g);
            if(!strcmp(key,line)){group_on[g]=!strcmp(value,"1");grouped=1;}
        }
        for(unsigned t=0;t<S3_MUSIC_COUNT;t++){
            char key[80];snprintf(key,sizeof key,"%s.enabled",s3_theme_ids[t]);
            if(!strcmp(key,line))s3_themes[t].enabled=!strcmp(value,"1");
            snprintf(key,sizeof key,"%s.choice",s3_theme_ids[t]);
            if(!strcmp(key,line)){
                invalid_choice[t]=1;
                const char *id=choice_id(t,value);
                for(unsigned c=0;c<(unsigned)s3_theme_choices(t);c++)if(!strcmp(id,s3_theme_choice(t,c)->id)){
                    s3_themes[t].choice=(int)c;invalid_choice[t]=0;break;
                }
            }
        }
    }
    fclose(f);
    if(!data_group_explicit)data_group_on=group_on[S3_MUSIC_SONIC3];
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_themes[t].enabled){
        if(invalid_choice[t])s3_themes[t].enabled=0;
        else if(!s3_theme_ready(t)){if(t==S3_MUSIC_DATA)data_group_on=0;else if(grouped)group_on[rows[t].group]=0;else s3_themes[t].enabled=0;}
    }
}
int s3_themes_save(void)
{
    char text[S3_MUSIC_COUNT*160+256];size_t used=0;
    for(unsigned g=0;g<S3_MUSIC_GROUPS;g++)used+=(size_t)snprintf(text+used,sizeof text-used,"group.%u.enabled=%d\n",g,group_on[g]);
    used+=(size_t)snprintf(text+used,sizeof text-used,"data-select.group-enabled=%d\n",data_group_on);
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)used+=(size_t)snprintf(text+used,sizeof text-used,"%s.enabled=%d\n%s.choice=%s\n",s3_theme_ids[t],s3_themes[t].enabled,s3_theme_ids[t],s3_theme_choice(t,(unsigned)s3_themes[t].choice)->id);
    if(!genesis_atomic_file_store(file,text,used)){snprintf(error,sizeof error,"Unable to save sound themes");return 0;}
    return 1;
}
int s3_themes_validate(void)
{
    *error=0;
    int groups[S3_MUSIC_GROUPS],data=data_group_on;memcpy(groups,group_on,sizeof groups);
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_active(t)){
        int ok=s3_theme_enable(t,1);
        memcpy(group_on,groups,sizeof groups);data_group_on=data;
        if(!ok)return 0;
    }
    return 1;
}
int s3_theme_import(unsigned t,const char *path,void (*progress)(void *,float,const char *),void *user)
{
    *error=0;if(t>=S3_MUSIC_COUNT||!path||!*path||strpbrk(path,"\r\n"))return 0;
    if(s3_music_custom(t)){
        if(!s3_custom_import(t,path,progress,user))return 0;
        s3_themes[t].enabled=1;
        if(!s3_music_group_enable(s3_music_group(t),1)){group_set(s3_music_group(t),0);*error=0;}return 1;
    }
    int expected=s3_theme_donor(t);if(expected<0)return 0;
    char imported[1024];
    const S3ThemeChoice *choice=s3_theme_choice(t,(unsigned)s3_themes[t].choice);
    const char *expected_ids=!strcmp(choice->id,"s1-s2")?"s1,s2":s3_donor_ids[expected];
    if(!s3_sound_donor_import(path,expected_ids,library_settings,imported,error,sizeof error,progress,user))return 0;
    /* The helper identifies by digest; never adopt its path without verifying
     * the canonical donor again in the game. Other choices see it at once. */
    for(unsigned d=0;d<S3_DONOR_COUNT;d++){size_t size;free(s3_donor_read(d,&size));}
    if(!s3_theme_ready(t)){snprintf(error,sizeof error,"This ROM belongs to another theme choice. Select %s.",s3_donor_names[expected]);return 0;}
    s3_themes[t].enabled=1;
    if(!s3_music_group_enable(s3_music_group(t),1)){group_set(s3_music_group(t),0);*error=0;}
    return 1;
}
