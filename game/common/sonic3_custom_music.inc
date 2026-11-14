/* Validated local custom resources. Choices and files stay per host profile;
 * immutable audio data is shared by the import cache's content identity. */
#include "sonic3_vgm_format.h"
typedef struct CustomMusic {char manifest[1024],data[1200],name[160],hash[65];int ready,loop,kind,restores;} CustomMusic;
static CustomMusic custom_music[S3_MUSIC_COUNT];
static uint8_t *custom_binary(const CustomMusic *music,size_t *bytes,S3SoundCustomInfo *info)
{
    FILE *f=fopen(music->data,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);rewind(f);
    if(n<10||n>0x8000){fclose(f);return NULL;}
    uint8_t *raw=malloc((size_t)n),*converted=malloc(0x8000);int ok=raw&&converted&&fread(raw,1,(size_t)n,f)==(size_t)n;fclose(f);
    uint8_t hash[32];char text[65];if(ok){recompui_sha256_compute(raw,(size_t)n,hash);for(unsigned i=0;i<32;i++)snprintf(text+i*2,3,"%02x",hash[i]);ok=!strcmp(text,music->hash);}
    if(ok)ok=s3_sound_convert_custom(raw,(size_t)n,NULL,converted,0x8000,info,error,sizeof error);free(converted);
    if(!ok){free(raw);return NULL;}if(bytes)*bytes=(size_t)n;return raw;
}
static int custom_read(const char *path,CustomMusic *out)
{
    if(!path||!*path||strlen(path)>=sizeof out->manifest||strpbrk(path,"\r\n"))return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    CustomMusic value={0};char line[1400],source[65]="",relative[160]="";unsigned format=0,kind=0,count=0,fields=0;
    while(fgets(line,sizeof line,f)) {
        if(++count>8){fields=0;break;}line[strcspn(line,"\r\n")]=0;char *v=strchr(line,'=');if(!v){fields=0;break;}*v++=0;
        unsigned bit=0;
        if(!strcmp(line,"format")){bit=1;format=!strcmp(v,"1");}
        else if(!strcmp(line,"kind")){bit=2;kind=!strcmp(v,"pcm")?S3_CUSTOM_PCM:!strcmp(v,"smps")?S3_CUSTOM_SMPS:!strcmp(v,"vgm")?S3_CUSTOM_VGM:0;}
        else if(!strcmp(line,"name")&&*v&&strlen(v)<sizeof value.name){bit=4;snprintf(value.name,sizeof value.name,"%s",v);}
        else if(!strcmp(line,"source_sha256")&&strlen(v)==64){bit=8;snprintf(source,sizeof source,"%s",v);}
        else if(!strcmp(line,"data")&&strlen(v)<sizeof relative){bit=16;snprintf(relative,sizeof relative,"%s",v);}
        else if(!strcmp(line,"sha256")&&strlen(v)==64){bit=32;snprintf(value.hash,sizeof value.hash,"%s",v);}
        else if(!strcmp(line,"loop")&&(!strcmp(v,"0")||!strcmp(v,"1"))){bit=64;value.loop=*v=='1';}
        if(!bit||(fields&bit)){fields=0;break;}fields|=bit;
    }
    int ok=!ferror(f)&&fields==127&&format&&kind;fclose(f);
    for(unsigned i=0;ok&&i<64;i++)if(!((source[i]>='0'&&source[i]<='9')||(source[i]>='a'&&source[i]<='f')))ok=0;
    char expected[160];snprintf(expected,sizeof expected,"%s/%s",source,kind==S3_CUSTOM_SMPS?"song.bin":kind==S3_CUSTOM_VGM?"song.vgmdata":"audio.wav");if(strcmp(expected,relative))ok=0;
    snprintf(value.data,sizeof value.data,"%s",path);char *a=strrchr(value.data,'/'),*b=strrchr(value.data,92);if(!a||(b&&b>a))a=b;
    size_t prefix=a?(size_t)(a-value.data)+1:0;
    if(prefix+strlen(relative)>=sizeof value.data)return 0;snprintf(value.data+prefix,sizeof value.data-prefix,"%s",relative);
    if(!ok)return 0;value.kind=(int)kind;
    if(kind==S3_CUSTOM_SMPS){S3SoundCustomInfo info={0};uint8_t *raw=custom_binary(&value,NULL,&info);if(!raw)return 0;free(raw);value.restores=(int)info.restores;}
    else if(kind==S3_CUSTOM_VGM){uint8_t *raw=s3_vgm_read(value.data,value.hash,NULL);if(!raw)return 0;free(raw);}
    else if(!s3_recording_verify(value.data,value.hash))return 0;
    snprintf(value.manifest,sizeof value.manifest,"%s",path);value.ready=1;*out=value;return 1;
}
static void custom_load(void)
{
    memset(custom_music,0,sizeof custom_music);char path[1280],line[1200];snprintf(path,sizeof path,"%s.custom",file);
    FILE *f=fopen(path,"rb");if(!f)return;
    unsigned count=0;while(fgets(line,sizeof line,f)&&++count<=S3_MUSIC_COUNT){line[strcspn(line,"\r\n")]=0;char *v=strchr(line,'=');if(!v)continue;*v++=0;
        for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(!strcmp(line,s3_theme_ids[t]))custom_read(v,&custom_music[t]);
    }fclose(f);
}
static int custom_save(void)
{
    size_t cap=S3_MUSIC_COUNT*1100u,used=0;char *text=malloc(cap);if(!text)return 0;
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(*custom_music[t].manifest)used+=(size_t)snprintf(text+used,cap-used,"%s=%s\n",s3_theme_ids[t],custom_music[t].manifest);
    char path[1280];snprintf(path,sizeof path,"%s.custom",file);int ok=genesis_atomic_file_store(path,text,used);free(text);return ok;
}
int s3_music_custom(unsigned t){const S3ThemeChoice *c=t<S3_MUSIC_COUNT?s3_theme_choice(t,(unsigned)s3_themes[t].choice):NULL;return c&&c->donor==S3_DONOR_CUSTOM;}
int s3_custom_ready(unsigned t){return t<S3_MUSIC_COUNT&&custom_music[t].ready&&(t!=S3_THEME_LIFE||(custom_music[t].kind==S3_CUSTOM_SMPS&&custom_music[t].restores));}
int s3_custom_kind(unsigned t){return t<S3_MUSIC_COUNT?custom_music[t].kind:0;}
uint8_t *s3_custom_sequence(unsigned t,size_t *bytes){return t<S3_MUSIC_COUNT&&custom_music[t].kind==S3_CUSTOM_SMPS?custom_binary(&custom_music[t],bytes,NULL):NULL;}
const char *s3_custom_path(unsigned t){return t<S3_MUSIC_COUNT?custom_music[t].manifest:"";}
const char *s3_custom_name(unsigned t){return t<S3_MUSIC_COUNT?custom_music[t].name:"";}
const char *s3_custom_data(unsigned t){return t<S3_MUSIC_COUNT?custom_music[t].data:"";}
const char *s3_custom_hash(unsigned t){return t<S3_MUSIC_COUNT?custom_music[t].hash:"";}
int s3_custom_loop(unsigned t){return t<S3_MUSIC_COUNT&&custom_music[t].loop;}
int s3_custom_import(unsigned t,const char *path,void (*progress)(void *,float,const char *),void *user)
{
    if(t>=S3_MUSIC_COUNT||!path||!*path)return 0;
    CustomMusic next={0};char imported[1024];
    if(!custom_read(path,&next)){
        if(!s3_custom_music_import(path,s3_theme_ids[t],file,imported,error,sizeof error,progress,user))return 0;
        if(!custom_read(imported,&next)){if(!*error)snprintf(error,sizeof error,"Custom music failed validation; previous selection retained");return 0;}
    }
    if(t==S3_THEME_LIFE&&(next.kind!=S3_CUSTOM_SMPS||!next.restores)){snprintf(error,sizeof error,"1-UP needs a finite SMPS jingle that restores the previous music");return 0;}
    CustomMusic old=custom_music[t];custom_music[t]=next;
    if(!custom_save()){custom_music[t]=old;snprintf(error,sizeof error,"Unable to save the custom music selection");return 0;}
    return 1;
}
