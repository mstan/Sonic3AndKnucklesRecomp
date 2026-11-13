#include "sonic3_sound_driver.h"
#include "sonic3_sound_assets.h"
#include "sonic3_sound_themes.h"
#include "sonic3_cd_themes.h"
#include "sonic3_state_io.h"
#include "genesis_runtime.h"
#include "genesis_bus.h"
#include "host_atomic_file.h"
#include "sha256.h"
#include "cmd_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef S3_CLONE_DRIVER
#include "sonic3_clone_program.h"
#endif
#ifdef S3_SOUND_COMBINED
#include "stage_pack.h"
#endif

static int selected,active,bound,standalone;
static uint8_t sound_ram[0x800],pending[3],initialised,tempo,tempo_dirty;
static char settings_file[1200],error[256];
static char prototype_path[1024];
static int prototype_selected,prototype_active,prototype_verified;
static uint8_t theme_identity[32];
static int invincibility_override,super_override;
int s3_sound_prototype_selected(void){return prototype_selected;}
int s3_sound_prototype_active(void){return prototype_active;}
int s3_sound_prototype_verified(void){return prototype_verified||s3_donor_verified(S3_DONOR_PROTO);}
const char *s3_sound_prototype_path(void){return *prototype_path?prototype_path:s3_donor_verified(S3_DONOR_PROTO)?s3_donor_path(S3_DONOR_PROTO):prototype_path;}
static uint8_t *prototype_rom(const char *path)
{
    FILE *f=path && *path?fopen(path,"rb"):NULL;
    if(!f){snprintf(error,sizeof error,"Select your Sonic 3 November 3, 1993 prototype ROM.");return NULL;}
    uint8_t *rom=NULL;
    if(!fseek(f,0,SEEK_END) && ftell(f)==0x200000){
        rewind(f);rom=(uint8_t*)malloc(0x200000);
        if(rom && fread(rom,1,0x200000,f)!=0x200000){free(rom);rom=NULL;}
    }
    fclose(f);
    if(!rom){snprintf(error,sizeof error,"Expected an unmodified 2 MiB Sonic 3 November 3, 1993 prototype ROM.");return NULL;}
    S3SoundSource source;
    if(!s3_sound_read_prototype(rom,0x200000,&source,error,sizeof error)){free(rom);return NULL;}
    return rom;
}
int s3_sound_prototype_set_path(const char *path)
{
    if(!path || strlen(path)>=sizeof prototype_path || strpbrk(path,"\r\n")){
        snprintf(error,sizeof error,"Invalid prototype ROM path.");return 0;
    }
    if(*path){uint8_t *rom=prototype_rom(path);if(!rom)return 0;free(rom);}
    snprintf(prototype_path,sizeof prototype_path,"%s",path);
    if(*path)s3_theme_offer_prototype(path);
    prototype_verified=*path!=0;*error=0;return 1;
}
uint8_t *s3_sound_prototype_read(void){return prototype_rom(s3_sound_prototype_path());}
int s3_sound_prototype_select(int on)
{
    if(on && !selected){snprintf(error,sizeof error,"Enable Clone Driver before selecting the prototype soundtrack.");return 0;}
    if(on){uint8_t *rom=prototype_rom(s3_sound_prototype_path());prototype_verified=rom!=NULL;if(!rom)return 0;free(rom);}
    prototype_selected=on!=0;*error=0;return 1;
}
int s3_sound_selected(void){return selected;}
int s3_sound_active(void){return active;}
const char *s3_sound_error(void){return error;}
int s3_sound_available(void){
#if defined(S3_CLONE_DRIVER) && !defined(GENESIS_Z80_RECOMP)
    return 1;
#else
    return 0;
#endif
}
int s3_sound_select(int on)
{
    if(!on && s3_themes_enabled()){snprintf(error,sizeof error,"Disable event theme overrides before disabling Clone Driver.");return 0;}
    if(!on && prototype_selected){snprintf(error,sizeof error,"Disable the prototype soundtrack before disabling Clone Driver.");return 0;}
    if(on && !s3_sound_available()){snprintf(error,sizeof error,"Clone Driver requires the standard Z80 backend in this build.");return 0;}
#ifdef S3_SOUND_COMBINED
    if((on || active) && s3_stage_pack_enabled){snprintf(error,sizeof error,"Disable imported campaigns and additional characters before enabling Clone Driver; restart after changing drivers.");return 0;}
#endif
    selected=on!=0;*error=0;return 1;
}
void s3_sound_load(const char *path,const char *mode)
{
    selected=active=bound=0;initialised=tempo=tempo_dirty=0;memset(pending,0,sizeof pending);*error=0;
    memset(theme_identity,0,sizeof theme_identity);invincibility_override=super_override=0;
    s3_cd_themes_reset();
    prototype_selected=prototype_active=prototype_verified=0;*prototype_path=0;
    standalone=!strcmp(mode,"sonic3");
    snprintf(settings_file,sizeof settings_file,"%s",path?path:"settings.ini");
    char *s=strrchr(settings_file,'/'),*b=strrchr(settings_file,'\\');if(!s || (b && b>s))s=b;
    size_t prefix=s?(size_t)(s-settings_file)+1:0;
    if(prefix+strlen(mode)+12>=sizeof settings_file){snprintf(error,sizeof error,"Sound settings path too long");return;}
    snprintf(settings_file+prefix,sizeof settings_file-prefix,"%s-sound.ini",mode);
    FILE *f=fopen(settings_file,"rb");
    char line[1152];while(f && fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"enabled=1"))selected=1;
        if(!strcmp(line,"prototype_music=1"))prototype_selected=1;
        if(!strncmp(line,"prototype_rom=",14) && strlen(line+14)<sizeof prototype_path)
            snprintf(prototype_path,sizeof prototype_path,"%s",line+14);
    }if(f)fclose(f);
    s3_themes_load(path,mode);
    if(s3_themes_enabled())selected=1;
    if(*s3_sound_prototype_path()){
        uint8_t *rom=prototype_rom(s3_sound_prototype_path());prototype_verified=rom!=NULL;free(rom);
        if(prototype_verified)s3_theme_offer_prototype(s3_sound_prototype_path());
        if(!prototype_selected)*error=0;
    }
}
int s3_sound_commit(void)
{
    if(!s3_sound_select(selected))return 0;
    if(!s3_sound_prototype_select(prototype_selected))return 0;
    char text[1200];int n=snprintf(text,sizeof text,"[sound_driver]\nenabled=%d\nprototype_music=%d\nprototype_rom=%s\n",selected,prototype_selected,s3_sound_prototype_path());
    if(!genesis_atomic_file_store(settings_file,text,(size_t)n)){snprintf(error,sizeof error,"Unable to save sound settings");return 0;}return s3_themes_save();
}
int s3_sound_netplay_allowed(void){return !(selected || active);}
void s3_sound_post_reset(void)
{
    if(bound)return;
    if(selected && !s3_sound_select(1)){fprintf(stderr,"[sound-driver] %s\n",error);exit(1);}
    if(prototype_selected && !selected){fprintf(stderr,"[sound-driver] Prototype soundtrack requires Clone Driver\n");exit(1);}
    active=selected;prototype_active=prototype_selected;bound=1;if(!active)return;
#ifdef S3_CLONE_DRIVER
    if(g_rom_ext){fprintf(stderr,"[sound-driver] Expansion ROM already in use\n");exit(1);}
    uint8_t *bank=calloc(1,0x400000);if(!bank){fprintf(stderr,"[sound-driver] Allocation failed\n");exit(1);}
    memcpy(bank,s3_clone_program,sizeof s3_clone_program);
    const S3SoundLayout seq={S3_CLONE_PAYLOAD_SIZE,S3_CLONE_MUSIC_INDEX,S3_CLONE_SOUND_INDEX,S3_CLONE_PSG_INDEX,S3_CLONE_MODENV_INDEX};
    const S3SoundPCMLayout pcm={S3_CLONE_SAMPLE_TABLE,S3_CLONE_DAC_MAP_S3,S3_CLONE_DAC_MAP_SK,S3_CLONE_SEGA_ID,S3_CLONE_SAMPLE_COUNT};
    S3SoundAssetStats stats;size_t rom_size=standalone?0x200000:
#ifdef S3_SOUND_COMBINED
        0x400000;
#else
        0x200000;
#endif
    if(!s3_sound_build_sequences(g_rom,rom_size,&seq,bank,0x400000,&stats,error,sizeof error) ||
       !s3_sound_build_pcm(g_rom,rom_size,&pcm,bank,0x400000,&stats.bytes,error,sizeof error)){
        fprintf(stderr,"[sound-driver] %s\n",error);free(bank);exit(1);
    }
    if(prototype_active){
        uint8_t *donor=prototype_rom(s3_sound_prototype_path());
        int ok=donor && s3_sound_build_prototype_music(donor,0x200000,&seq,bank,0x400000,&stats.bytes,standalone,error,sizeof error) &&
            s3_sound_build_prototype_pcm(donor,0x200000,&pcm,bank,0x400000,&stats.bytes,error,sizeof error);
        free(donor);
        if(!ok){fprintf(stderr,"[sound-driver] %s\n",error);free(bank);exit(1);}
        fprintf(stderr,"[sound-driver] Verified November 3, 1993 prototype soundtrack; retail SFX retained\n");
    }
    invincibility_override=s3_music_active(S3_THEME_INVINCIBLE);
    super_override=s3_music_active(S3_MUSIC_SUPER);
    if(standalone && (invincibility_override||super_override) && !prototype_active)
        memcpy(bank+seq.music_index-0x400000+4*(0x33-1),bank+seq.music_index-0x400000+4*(0x2C-1),4);
    unsigned next_sample=prototype_active?97:70;
    const uint8_t *inputs[S3_ROM_DONOR_COUNT]={0};size_t sizes[S3_ROM_DONOR_COUNT]={0};
    uint8_t *owned[S3_ROM_DONOR_COUNT]={0};S3SoundMusicCache cache={0};
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if(s3_music_active(t)){
        const S3ThemeChoice *choice=s3_theme_choice(t,(unsigned)s3_themes[t].choice);
        int donor=choice->donor;
        if(donor>=S3_DONOR_CD_US){
            if(!s3_cd_theme_build(t,(unsigned)donor,s3_donor_path((unsigned)donor),&seq,bank,0x400000,&stats.bytes,error,sizeof error)){
                fprintf(stderr,"[sound-driver] %s: %s\n",s3_theme_names[t],error);free(bank);exit(1);
            }
            fprintf(stderr,"[sound-driver] %s: %s\n",s3_theme_names[t],choice->label);continue;
        }
        donor=s3_music_donor(t);
        if(!inputs[donor]){
            inputs[donor]=g_rom;sizes[donor]=rom_size;
            if(s3_theme_donor(t)>=0){owned[donor]=s3_donor_read((unsigned)donor,&sizes[donor]);inputs[donor]=owned[donor];}
            else if(donor==S3_DONOR_S3 || donor==S3_DONOR_SK){
                sizes[donor]=0x200000;
#ifdef S3_SOUND_COMBINED
                if(donor==S3_DONOR_S3){
                    inputs[donor]=g_rom+0x200000;
                    /* Normalize only the verified Steam cartridge's private
                     * donor view, never the bytes read by the guest. */
                    if(inputs[donor][0x1F0]=='J'){
                        owned[donor]=malloc(sizes[donor]);
                        if(owned[donor]){memcpy(owned[donor],inputs[donor],sizes[donor]);owned[donor][0x1F0]='U';}
                        inputs[donor]=owned[donor];
                    }
                }
#endif
            }
        }
        unsigned target=s3_music_target(t);
        int ok=inputs[donor] && s3_sound_build_track(inputs[donor],sizes[donor],(unsigned)donor,s3_music_source(t,(unsigned)donor),target,&seq,&pcm,s3_clone_program,bank,0x400000,&stats.bytes,&next_sample,&cache,error,sizeof error);
        if(!ok){
            fprintf(stderr,"[sound-driver] %s: %s\n",s3_theme_names[t],error);
            s3_sound_music_cache_free(&cache);for(unsigned d=0;d<S3_ROM_DONOR_COUNT;d++)free(owned[d]);free(bank);exit(1);
        }
        if(t==S3_MUSIC_MINIBOSS){
            static const unsigned aliases[]={0x18,0x2E};
            for(unsigned i=0;i<2;i++)memcpy(bank+seq.music_index-0x400000+4*(aliases[i]-1),bank+seq.music_index-0x400000+4*(target-1),4);
        }
        fprintf(stderr,"[sound-driver] %s: %s\n",s3_theme_names[t],choice->label);
    }
    s3_sound_music_cache_free(&cache);for(unsigned d=0;d<S3_ROM_DONOR_COUNT;d++)free(owned[d]);
    if(s3_themes_enabled()){
        uint8_t hashes[64];recompui_sha256_compute(bank,stats.bytes,hashes);s3_cd_theme_identity(hashes+32);recompui_sha256_compute(hashes,sizeof hashes,theme_identity);
    }
    g_rom_ext=bank;g_rom_ext_size=(stats.bytes+1)&~1u;
    memset(sound_ram,0,sizeof sound_ram);
    if(!gbus_map_expansion_ram(S3_CLONE_RAM_ADDRESS,sound_ram,sizeof sound_ram)){fprintf(stderr,"[sound-driver] RAM mapping failed\n");exit(1);}
    fprintf(stderr,"[sound-driver] Clone Driver + Mega PCM; %u songs, %u SFX; ROM-derived bank %u bytes; program=%s\n",stats.music,stats.sfx,g_rom_ext_size,S3_CLONE_SHA256);
#endif
}
int s3_sound_owns(uint32_t pc)
{
#ifdef S3_CLONE_DRIVER
    uint32_t init=standalone?0x1562:0x12CE,music=standalone?0x15E2:0x1358,sfx=standalone?0x160A:0x1380,t=standalone?0x164C:0x13C2;
    if(pc==init || pc==music || pc==sfx || pc==t)return 1;
    for(unsigned i=0;i<sizeof s3_clone_pause_sites/sizeof *s3_clone_pause_sites;i++)if(pc==s3_clone_pause_sites[i])return 1;
#else
    (void)pc;
#endif
    return 0;
}
#ifdef S3_CLONE_DRIVER
static void zero_move_flags(void){g_cpu.SR=(uint16_t)((g_cpu.SR&~(SR_N|SR_Z|SR_V|SR_C))|SR_Z);}
static void queue(unsigned native,unsigned slot)
{
    unsigned id=0;
    if(native>=1 && native<=0x32)id=native;
    else if(native==0xDC && !standalone)id=0x33; /* S&K credits */
    else if(native>=0x33 && native<=0xDB)id=native+0xD;
    else if(native==0xE1 || native==0xE5)id=0xFB;
    else if(native==0xE4)id=0xFA;
    else if(native==0xFF){
        /* The logo supersedes a demo's active fade. StopAllSound is an
         * internal entry with A6 pointing at the driver's private RAM. */
        g_cpu.A[6]=S3_CLONE_RAM_ADDRESS;
        recomp_call_addr(S3_CLONE_STOP_ALL);id=0xFC;
    }
    else if(native>=0xE0 || (native>=0xDD && native<=0xDF))id=0xFF;
    if(native==0xE3){ /* original command silences PSG only */
        for(unsigned v=0x9F;v<=0xFF;v+=0x20)m68k_write8(0xC00011,(uint8_t)v);
        return;
    }
    if(!id)return;
    /* Sonic 3's $32 is Ending. Its unused relocated-credits slot can hold
     * the separate Super replacement without changing the native ending. */
    if((prototype_active || invincibility_override || super_override) && standalone && native==0x2C && g_ram[0xFE19])id=0x33;
    /* Native SFX start at $33, which is also Clone's relocated S&K credits
     * entry. Only native music/control requests may stop a CD recording. */
    if(native<=0x32 || native>=0xDC)
        s3_cd_theme_request(id<=0x33 && id!=native?id:native);
    if(id<=0x33 && !m68k_read32(S3_CLONE_MUSIC_INDEX+4*(id-1)))return;
    g_cpu.D[0]=id;recomp_call_addr(slot?S3_CLONE_QUEUE_SFX:S3_CLONE_QUEUE_MUSIC);
}
#endif
int s3_sound_hook(uint32_t pc)
{
#ifdef S3_CLONE_DRIVER
    if(!active || !s3_sound_owns(pc))return 0;
    uint32_t init=standalone?0x1562:0x12CE,music=standalone?0x15E2:0x1358,sfx=standalone?0x160A:0x1380,t=standalone?0x164C:0x13C2;
    if(pc==init){
        s3_cd_theme_request(0xE0);
        M68KState saved=g_cpu;memset(sound_ram,0,sizeof sound_ram);memset(pending,0,sizeof pending);tempo=tempo_dirty=0;
        recomp_call_addr(S3_CLONE_INITIALISE);g_cpu=saved;initialised=1;zero_move_flags();return 1;
    }
    if(pc==music)pending[0]=(uint8_t)g_cpu.D[0];
    else if(pc==sfx){uint8_t id=(uint8_t)g_cpu.D[0];if(pending[1]!=id)pending[pending[1]?2:1]=id;}
    else if(pc==t){tempo=(uint8_t)g_cpu.D[0];tempo_dirty=1;}
    else{
        uint8_t value=g_rom[pc+3];sound_ram[S3_CLONE_PAUSE_OFFSET]=value;
        g_cpu.SR=(uint16_t)((g_cpu.SR&~(SR_N|SR_Z|SR_V|SR_C))|(value&128?SR_N:0));
        recomp_tail_call(pc+8);return 1;
    }
    zero_move_flags();return 1;
#else
    (void)pc;return 0;
#endif
}
void s3_sound_vblank(void)
{
#ifdef S3_CLONE_DRIVER
    if(!active || !initialised)return;
    M68KState saved=g_cpu;
    for(unsigned i=0;i<3;i++)if(pending[i]){queue(pending[i],i);pending[i]=0;}
    if(tempo_dirty){g_cpu.D[0]=tempo?0xFD:0xFE;recomp_call_addr(S3_CLONE_QUEUE_EXTRA);tempo_dirty=0;}
    recomp_call_addr(S3_CLONE_GUARDED_UPDATE);
    /* In the pinned software RAM layout bitfield2 immediately follows the
     * main tempo byte; bit zero is the driver's 1-UP backup flag. */
    s3_cd_theme_frame(sound_ram[S3_CLONE_PAUSE_OFFSET]!=0,sound_ram[S3_CLONE_TEMPO_OFFSET+1]&1);
    g_cpu=saved;
#endif
}
int s3_sound_dispatch_miss(uint32_t pc)
{
    if(active && pc>=0x400000 && pc<0x800000){fprintf(stderr,"[sound-driver] Missing native target $%06X\n",pc);exit(1);}return 0;
}
void s3_sound_state(struct S3StateIO *io)
{
    uint8_t identity[72]={'S','3','S','D',2,0,0,0},incoming[72];identity[5]=(uint8_t)(bound?active:selected);
    identity[6]=(uint8_t)(bound?prototype_active:prototype_selected);
#ifdef S3_CLONE_DRIVER
    if(identity[5])for(unsigned i=0;i<32;i++){
        unsigned a=S3_CLONE_SHA256[i*2],b=S3_CLONE_SHA256[i*2+1];
        identity[8+i]=(uint8_t)(((a<='9'?a-'0':a-'a'+10)<<4)|(b<='9'?b-'0':b-'a'+10));
    }
#endif
    memcpy(identity+40,theme_identity,sizeof theme_identity);
    memcpy(incoming,identity,sizeof incoming);s3_state_peek(io,0,incoming,sizeof incoming);
    if(memcmp(incoming,identity,sizeof incoming))io->ok=0;
    S3_STATE(io,identity);S3_STATE(io,sound_ram);S3_STATE(io,pending);S3_STATE(io,initialised);S3_STATE(io,tempo);S3_STATE(io,tempo_dirty);
    s3_cd_theme_state(io);
}
size_t s3_sound_rb_save(void *dst,size_t cap)
{
    S3StateIO measure={0};s3_sound_state(&measure);
    if(!dst)return measure.pos;
    if(cap<measure.pos)return 0;
    S3StateIO io={(uint8_t*)dst,cap,0,0,1};s3_sound_state(&io);return io.ok?io.pos:0;
}
int s3_sound_rb_load(const void *src,size_t len)
{
    if(!src || len!=s3_sound_rb_save(NULL,0))return 0;
    S3StateIO io={(uint8_t*)src,len,0,1,1};s3_sound_state(&io);if(!io.ok || io.pos!=len)return 0;
    io.pos=0;io.mode=2;s3_sound_state(&io);return io.ok && io.pos==len;
}
void s3_sound_command(int id,const char *json)
{
    /* Dev TCP audition uses the same native queue bridge as the cartridge.
     * It never changes a track pointer or a tested gameplay result. */
#if GEN_ENABLE_TRACE && defined(S3_CLONE_DRIVER)
    static const char *keys[]={"\"music\"","\"sfx\"","\"tempo\""};
    for(unsigned i=0;i<3;i++){
        const char *p=strstr(json,keys[i]);if(!p)continue;p=strchr(p,':');if(!p)continue;
        char *end;long v=strtol(p+1,&end,10);if(end==p+1 || v<0 || v>255 || (active && !initialised))continue;
        uint32_t pc=i==0?(standalone?0x15E2:0x1358):i==1?(standalone?0x160A:0x1380):(standalone?0x164C:0x13C2);
        M68KState saved=g_cpu;g_cpu.D[0]=(uint32_t)v;
        if(active)s3_sound_hook(pc);else recomp_call_addr(pc);
        g_cpu=saved;
    }
#else
    (void)json;
#endif
    unsigned cd_active,cd_theme;uint64_t cd_position;s3_cd_theme_debug(&cd_active,&cd_theme,&cd_position);
    char response[384];snprintf(response,sizeof response,"{\"id\":%d,\"ok\":true,\"selected\":%d,\"active\":%d,\"initialised\":%u,\"pending\":[%u,%u,%u],\"cd\":{\"active\":%u,\"theme\":%u,\"position_q32\":%llu}}",id,selected,active,initialised,pending[0],pending[1],pending[2],cd_active,cd_theme,(unsigned long long)cd_position);cmd_send_response(response);
}
