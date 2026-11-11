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

S3ThemeSelection s3_themes[S3_THEME_COUNT];
const char *const s3_theme_ids[]={"extra-life","game-over","continue","act-clear","invincibility"};
const char *const s3_theme_names[]={"1-UP Theme","Game Over Theme","Continue Screen Theme","Act Clear Theme","Invincibility Theme"};
const char *const s3_donor_ids[]={"s1","s2","s2-aug","s2-wai","s3-proto","s3","sk","cd-us"};
const char *const s3_donor_names[]={"Sonic 1","Sonic 2","Sonic 2 (Aug 21, 1992 prototype)","Sonic 2 (Simon Wai prototype)","Sonic 3 (Nov 3, 1993 prototype)","Sonic 3","Sonic & Knuckles","Sonic CD (US)"};
static const S3ThemeChoice choices[S3_THEME_COUNT][6]={
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK}},
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s3k","Sonic 3 & Knuckles",-1},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK}},
    {{"s1-s2","Sonic 1 / Sonic 2",S3_DONOR_S1},{"s2-wai","Sonic 2 Simon Wai prototype",S3_DONOR_WAI},{"s3-proto","Sonic 3 prototype",S3_DONOR_PROTO},{"s3k","Sonic 3 & Knuckles",-1},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
    {{"s1","Sonic 1",S3_DONOR_S1},{"s2-aug","Sonic 2 Aug 21, 1992 prototype",S3_DONOR_AUG},{"s2","Sonic 2",S3_DONOR_S2},{"s3","Sonic 3",S3_DONOR_S3},{"sk","Sonic & Knuckles",S3_DONOR_SK},{"cd-us","Sonic CD (US)",S3_DONOR_CD_US}},
};
static const unsigned counts[]={4,3,3,5,6};
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
const char *s3_themes_error(void){return error;}
void s3_themes_clear_error(void){*error=0;}
const char *s3_donor_path(unsigned d){return d==S3_DONOR_PROTO&&prototype_from_alias?prototype_alias:d<S3_DONOR_COUNT?paths[d]:"";}
int s3_donor_verified(unsigned d){return d<S3_DONOR_COUNT && verified[d];}
int s3_theme_choices(unsigned t){return t<S3_THEME_COUNT?(int)counts[t]:0;}
const S3ThemeChoice *s3_theme_choice(unsigned t,unsigned c){return t<S3_THEME_COUNT && c<counts[t]?&choices[t][c]:NULL;}
static int donor_identity(const uint8_t *rom,size_t size)
{
    uint8_t hash[32];char hex[65];recompui_sha256_compute(rom,size,hash);
    for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",hash[i]);
    for(unsigned i=0;i<S3_ROM_DONOR_COUNT;i++)if(size==sizes[i]&&!strcmp(hex,hashes[i]))return (int)i;
    return -1;
}
uint8_t *s3_donor_read(unsigned d,size_t *size)
{
    if(d>=S3_ROM_DONOR_COUNT){if(d<S3_DONOR_COUNT)verified[d]=s3_cd_donor_verify(paths[d],d);return NULL;}
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
int s3_theme_donor(unsigned t)
{
    if(t>=S3_THEME_COUNT)return -1;
    const S3ThemeChoice *c=s3_theme_choice(t,(unsigned)s3_themes[t].choice);if(!c)return -1;
    int d=c->donor;
    if(!strcmp(c->id,"s1-s2") && !verified[S3_DONOR_S1] && verified[S3_DONOR_S2])d=S3_DONOR_S2;
    return host_has(d)?-1:d;
}
int s3_theme_ready(unsigned t){int d=s3_theme_donor(t);return t<S3_THEME_COUNT&&(d<0||verified[d]);}
int s3_themes_enabled(void){for(unsigned t=0;t<S3_THEME_COUNT;t++)if(s3_themes[t].enabled)return 1;return 0;}
int s3_theme_enable(unsigned t,int on)
{
    *error=0;if(t>=S3_THEME_COUNT)return 0;
    int d=s3_theme_donor(t);
    if(on && d>=0){size_t n;free(s3_donor_read((unsigned)d,&n));}
    if(on && !s3_theme_ready(t)){snprintf(error,sizeof error,"Provide the selected theme's ROM first.");return 0;}
    if(on && !s3_sound_select(1)){snprintf(error,sizeof error,"%s",s3_sound_error());return 0;}
    s3_themes[t].enabled=on!=0;return 1;
}
int s3_theme_choose(unsigned t,const char *id)
{
    *error=0;if(t>=S3_THEME_COUNT||!id)return 0;
    for(unsigned c=0;c<counts[t];c++)if(!strcmp(id,choices[t][c].id)){
        s3_themes[t].choice=(int)c;
        if(s3_theme_ready(t)){
            if(!s3_theme_enable(t,1))s3_themes[t].enabled=0;
        }else s3_themes[t].enabled=0;
        return 1;
    }
    snprintf(error,sizeof error,"Unknown theme choice");return 0;
}
void s3_themes_load(const char *settings,const char *mode)
{
    memset(s3_themes,0,sizeof s3_themes);memset(verified,0,sizeof verified);*error=0;*prototype_alias=0;prototype_from_alias=0;
    host_s3=!strcmp(mode,"sonic3");host_combined=!strcmp(mode,"sonic3k");
    snprintf(file,sizeof file,"%s",settings?settings:"settings.ini");
    char *a=strrchr(file,'/'),*b=strrchr(file,'\\');if(!a||(b&&b>a))a=b;
    size_t prefix=a?(size_t)(a-file)+1:0;
    snprintf(file+prefix,sizeof file-prefix,"%s-themes.ini",mode);
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
    char line[256];int invalid_choice[S3_THEME_COUNT]={0};
    while(fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        char *value=strchr(line,'=');if(!value)continue;*value++=0;
        for(unsigned t=0;t<S3_THEME_COUNT;t++){
            char key[80];snprintf(key,sizeof key,"%s.enabled",s3_theme_ids[t]);
            if(!strcmp(key,line))s3_themes[t].enabled=!strcmp(value,"1");
            snprintf(key,sizeof key,"%s.choice",s3_theme_ids[t]);
            if(!strcmp(key,line)){
                invalid_choice[t]=1;
                for(unsigned c=0;c<counts[t];c++)if(!strcmp(value,choices[t][c].id)){
                    s3_themes[t].choice=(int)c;invalid_choice[t]=0;break;
                }
            }
        }
    }
    fclose(f);
    for(unsigned t=0;t<S3_THEME_COUNT;t++)if(s3_themes[t].enabled&&(invalid_choice[t]||!s3_theme_ready(t)))s3_themes[t].enabled=0;
}
int s3_themes_save(void)
{
    char text[2048];size_t used=0;
    for(unsigned t=0;t<S3_THEME_COUNT;t++)used+=(size_t)snprintf(text+used,sizeof text-used,"%s.enabled=%d\n%s.choice=%s\n",s3_theme_ids[t],s3_themes[t].enabled,s3_theme_ids[t],choices[t][s3_themes[t].choice].id);
    if(!genesis_atomic_file_store(file,text,used)){snprintf(error,sizeof error,"Unable to save sound themes");return 0;}
    return 1;
}
int s3_themes_validate(void)
{
    *error=0;
    for(unsigned t=0;t<S3_THEME_COUNT;t++)if(s3_themes[t].enabled&&!s3_theme_enable(t,1))return 0;
    return 1;
}
int s3_theme_import(unsigned t,const char *path,void (*progress)(void *,float,const char *),void *user)
{
    *error=0;if(t>=S3_THEME_COUNT||!path||!*path||strpbrk(path,"\r\n"))return 0;
    int expected=s3_theme_donor(t);if(expected<0)return 0;
    char imported[1024];
    const S3ThemeChoice *choice=s3_theme_choice(t,(unsigned)s3_themes[t].choice);
    const char *expected_ids=!strcmp(choice->id,"s1-s2")?"s1,s2":s3_donor_ids[expected];
    if(!s3_sound_donor_import(path,expected_ids,library_settings,imported,error,sizeof error,progress,user))return 0;
    /* The helper identifies by digest; never adopt its path without verifying
     * the canonical donor again in the game. Other choices see it at once. */
    for(unsigned d=0;d<S3_DONOR_COUNT;d++){size_t size;free(s3_donor_read(d,&size));}
    if(!s3_theme_ready(t)){snprintf(error,sizeof error,"This ROM belongs to another theme choice. Select %s.",s3_donor_names[expected]);return 0;}
    return s3_theme_enable(t,1);
}
