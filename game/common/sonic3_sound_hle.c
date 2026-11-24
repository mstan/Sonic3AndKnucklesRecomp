#include "sonic3_sound_driver.h"
#include "sonic3_sound_assets.h"
#include "sonic3_sound_themes.h"
#include "sonic3_cd_themes.h"
#include "sonic3_vgm.h"
#include "sonic3_state_io.h"
#include "sonic3_video.h"
#include "genesis_runtime.h"
#include "genesis_bus.h"
#include "host_atomic_file.h"
#include "sha256.h"
#include "cmd_server.h"
#include "audio/mixer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sonic3_sound_hle_assets.h"
#include "sound_hle_pause.h"
#include "audio/smps_hle.h"
#include "audio/smps_hle_synth.h"
#include "audio/pcm_hle.h"
#include "audio/event_queue.h"
#include "genesis_machine.h"
#ifdef S3_SOUND_COMBINED
#include "stage_pack.h"
#include "trilogy.h"
#include "trilogy_music_hle.h"
#endif

static int selected,active,bound,standalone;
static uint8_t pending[3],initialised,tempo,tempo_dirty;
static S3HleSoundAssets assets;
static SmpsHle music,effects;
static SmpsHleSynth music_synth,effect_synth;
static PcmHle pcm;
static PcmHleClip clips[126];
static unsigned output_channel,output_effect,stamp,write_offset,paused,fade,fade_delay,last_pause,speed_timeout;
typedef struct CampaignBindings {
    uint32_t music_root[2];
    uint8_t music_flags[2],wanted_set,effect_set,effect_content;
} CampaignBindings;
static CampaignBindings bindings;
static uint32_t default_music[2];
static unsigned campaigns_prepared;
static void hle_stop_all(void);
static char settings_file[1200],error[256];
static char prototype_path[1024];
static int prototype_selected,prototype_active,prototype_verified;
static uint8_t theme_identity[32];
static int invincibility_override,super_override;
static void player_reset(void);
static int player_bind(uint32_t *used,size_t capacity);
static void player_overlay(int line,uint32_t *out,int width);
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
    if(on && !selected){snprintf(error,sizeof error,"Enable the music library before selecting the prototype soundtrack.");return 0;}
    if(on){uint8_t *rom=prototype_rom(s3_sound_prototype_path());prototype_verified=rom!=NULL;if(!rom)return 0;free(rom);}
    prototype_selected=on!=0;*error=0;return 1;
}
int s3_sound_selected(void){return selected;}
int s3_sound_active(void){return active;}
const char *s3_sound_error(void){return error;}
int s3_sound_available(void){return 1;}
int s3_sound_select(int on)
{
    if(!on && s3_themes_enabled()){snprintf(error,sizeof error,"Disable event theme overrides before disabling the music library.");return 0;}
    if(!on && prototype_selected){snprintf(error,sizeof error,"Disable the prototype soundtrack before disabling the music library.");return 0;}
#ifdef S3_SOUND_COMBINED
    if((on || active) && s3_stage_pack_enabled){snprintf(error,sizeof error,"Disable imported campaigns and additional characters before enabling the music library; restart after changing drivers.");return 0;}
#endif
    selected=on!=0;*error=0;return 1;
}
void s3_sound_load(const char *path,const char *mode)
{
    s3_hle_sound_assets_free(&assets);
    memset(&bindings,0,sizeof bindings);memset(default_music,0,sizeof default_music);campaigns_prepared=0;
    selected=active=bound=0;initialised=tempo=tempo_dirty=0;memset(pending,0,sizeof pending);*error=0;
    memset(theme_identity,0,sizeof theme_identity);invincibility_override=super_override=0;
    s3_cd_themes_reset();
    player_reset();s3_video_set_frame_overlay(player_overlay);
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
static void fatal(void){fprintf(stderr,"[sound-hle] %s\n",*error?error:music.error);exit(1);}
static int output_owned(void){return output_effect || !effects.state.music.track[output_channel].active;}
static int host_fm(void *user,unsigned port,unsigned reg,unsigned value)
{
    (void)user;if(port>1 || reg>255 || value>255)return 0;
    if(output_owned()){
        audio_event_push_latched(stamp+write_offset,(uint8_t)(port*2+1),(uint8_t)reg,(uint8_t)value);
        /* The mixer stamps are in master cycles. YMFM samples the live key
         * state once per 144 YM clocks (1008 master cycles). A key-off must
         * survive a chip sample or a following key-on cannot retrigger. */
        write_offset += port == 0 && reg == 0x28 && !(value & 0xF0) ? 144 * 7 : 144;
    }
    return 1;
}
static int host_psg(void *user,unsigned value)
{(void)user;if(output_owned())audio_event_push(stamp+write_offset,AUDIO_PORT_PSG,(uint8_t)value);return value<=255;}
static int host_pcm(void *user,unsigned note,unsigned pan)
{
    (void)user;if(note<0x81 || note>0xFE || !pcm_hle_play(&pcm,note-0x81,pan))return 0;
    unsigned old=output_channel;output_channel=0;
    int ok=host_fm(NULL,1,0xB6,pan)&&host_fm(NULL,0,0x2B,0x80);output_channel=old;return ok;
}
static unsigned requested;
static int host_request(void *user,unsigned id){(void)user;requested=id;return 1;}
static int event(void *user,const SmpsHleEvent *e)
{
    output_effect=user==&effect_synth;output_channel=e->channel;
    if(e->kind==SMPS_HLE_REST && !e->channel){pcm_hle_stop(&pcm);return host_fm(NULL,0,0x2B,0);}
    return smps_hle_synth_event(user,e);
}
static void dac(void *user,uint32_t offset,uint8_t value)
{
    (void)user;audio_event_push_latched(stamp+offset,AUDIO_PORT_FM1_DATA,0x2A,value);
    if(!pcm.state.active){
        /* DAC and FM6 share hardware. Release it at the sample's actual end
         * so subsequent six-voice music does not lose its last FM channel. */
        audio_event_push_latched(stamp+offset,AUDIO_PORT_FM1_DATA,0x2B,0);
        audio_event_push_latched(stamp+offset,AUDIO_PORT_FM2_DATA,0xB6,music_synth.state.track[6].pan);
    }
}
static void advance(uint32_t start,uint32_t cycles)
{
    stamp=start;write_offset=0;
    if(initialised && !pcm_hle_events(&pcm,cycles,53693175,dac,NULL)){
        snprintf(error,sizeof error,"Invalid native PCM clock");fatal();
    }
}
static void bind_providers(void)
{
    SmpsHleChipOutput output={host_fm,host_psg,host_pcm,host_request,NULL};
    smps_hle_synth_init(&music_synth,assets.data,assets.bytes,output);
    smps_hle_synth_init(&effect_synth,assets.data,assets.bytes,output);
    music_synth.psg_envelopes=effect_synth.psg_envelopes=assets.psg_envelopes;
    music_synth.psg_count=effect_synth.psg_count=256;
    music_synth.modulation_envelopes=effect_synth.modulation_envelopes=assets.modulation;
    music_synth.modulation_count=effect_synth.modulation_count=32;
    smps_hle_init(&music,assets.data,assets.bytes,event,&music_synth);
    smps_hle_init(&effects,assets.data,assets.bytes,event,&effect_synth);
    for(unsigned i=0;i<126;i++)clips[i]=(PcmHleClip){assets.data+assets.samples[i].start,assets.samples[i].bytes,
        assets.samples[i].rate_numerator,assets.samples[i].rate_denominator,UINT32_MAX};
    pcm_hle_init(&pcm,clips,126);machine_set_hle_audio(advance);
}
void s3_sound_post_reset(void)
{
    if(bound)return;
    if(selected && !s3_sound_select(1))fatal();
    active=selected;prototype_active=prototype_selected;bound=1;
    size_t rom_size=
#ifdef S3_SOUND_COMBINED
        0x400000;
#else
        0x200000;
#endif
    if(!s3_hle_sound_assets(g_rom,rom_size,&assets,error,sizeof error))fatal();
    uint8_t *bank=assets.data;
    const S3SoundLayout seq={8192,0x400000,0x400100,0x400500,0x400900};
    const S3SoundPCMLayout pcm_layout={0x400A00,0x400F00,0x400F80,0xC6,126};
    /* This template contains pointer/rate metadata only, no CPU program. */
    uint8_t template_data[8192];memcpy(template_data,bank,sizeof template_data);
    S3SoundAssetStats stats={0};stats.bytes=assets.bytes;
    if(prototype_active){
        uint8_t *donor=prototype_rom(s3_sound_prototype_path());
        int ok=donor && s3_sound_build_prototype_music(donor,0x200000,&seq,bank,0x400000,&stats.bytes,standalone,error,sizeof error) &&
            s3_sound_build_prototype_pcm(donor,0x200000,&pcm_layout,bank,0x400000,&stats.bytes,error,sizeof error);
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
        if(donor==S3_DONOR_CUSTOM){
            int ok=0;
            if(s3_custom_kind(t)==S3_CUSTOM_SMPS){
                size_t bytes=0;uint8_t *raw=s3_custom_sequence(t,&bytes);S3SoundSource source;S3SoundCustomInfo info={0};
                unsigned dest=(stats.bytes+1u)&~1u,cap=0x400000u-dest;if(cap>0x8000)cap=0x8000;
                ok=raw&&s3_sound_read_source(g_rom,rom_size,&source,error,sizeof error)&&
                    s3_sound_convert_custom(raw,bytes,&source,bank+dest,cap,&info,error,sizeof error);
                if(ok){uint32_t pointer=0x01400000u|dest;unsigned at=seq.music_index-0x400000+(s3_music_target(t)-1)*4;
                    for(unsigned i=0;i<4;i++)bank[at+i]=(uint8_t)(pointer>>(24-i*8));stats.bytes=dest+info.bytes;}
                free(raw);
            }else if(s3_custom_kind(t)==S3_CUSTOM_VGM)ok=s3_vgm_theme_build(t,s3_music_target(t),s3_custom_data(t),s3_custom_hash(t),&seq,bank,0x400000,&stats.bytes,error,sizeof error);
            else ok=s3_recording_build(t,s3_music_target(t),s3_custom_data(t),s3_custom_hash(t),s3_custom_loop(t),&seq,bank,0x400000,&stats.bytes,error,sizeof error);
            if(!ok){fprintf(stderr,"[sound-driver] %s: %s\n",s3_theme_names[t],error);free(bank);exit(1);}
            fprintf(stderr,"[sound-driver] %s: custom %s\n",s3_theme_names[t],s3_custom_kind(t)==S3_CUSTOM_SMPS?"SMPS":s3_custom_kind(t)==S3_CUSTOM_VGM?"VGM":"recording");continue;
        }
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
                }
#endif
            }
        }
        unsigned target=s3_music_target(t);
        int ok=inputs[donor] && s3_sound_build_track(inputs[donor],sizes[donor],(unsigned)donor,s3_music_source(t,(unsigned)donor),target,&seq,&pcm_layout,template_data,bank,0x400000,&stats.bytes,&next_sample,&cache,error,sizeof error);
        if(!ok){
            fprintf(stderr,"[sound-driver] %s: %s\n",s3_theme_names[t],error);
            s3_sound_music_cache_free(&cache);for(unsigned d=0;d<S3_ROM_DONOR_COUNT;d++)free(owned[d]);free(bank);exit(1);
        }
        fprintf(stderr,"[sound-driver] %s: %s\n",s3_theme_names[t],choice->label);
    }
    /* Both cartridge requests must use every kind of replacement, including
     * custom SMPS and the silent entries backing VGM/recorded/CD music. */
    if(s3_music_active(S3_MUSIC_MINIBOSS))
        memcpy(bank+seq.music_index-0x400000+4*(0x2E - 1),bank+seq.music_index-0x400000+4*(0x18 - 1),4);
    s3_sound_music_cache_free(&cache);for(unsigned d=0;d<S3_ROM_DONOR_COUNT;d++)free(owned[d]);
    {
        uint8_t hashes[64];recompui_sha256_compute(bank,stats.bytes,hashes);s3_cd_theme_identity(hashes+32);recompui_sha256_compute(hashes,sizeof hashes,theme_identity);
    }

    assets.bytes=stats.bytes;s3_hle_sound_assets_index(&assets);
    for(unsigned i=0;i<2;i++)bindings.music_root[i]=default_music[i]=assets.music[0x20-1+i];
    bind_providers();
    if(!player_bind(&stats.bytes,0x400000))fatal();
    fprintf(stderr,"[sound-hle] Immutable host sequences/PCM: %u bytes; sound CPU disabled\n",assets.bytes);
}
void s3_sound_campaign_restore(void)
{
    for(unsigned i=0;i<2;i++){
        assets.music[0x20-1+i]=bindings.music_root[i]=default_music[i];bindings.music_flags[i]=0;
    }
}
void s3_sound_campaign_prepare(void)
{
#ifdef S3_SOUND_COMBINED
    if(campaigns_prepared || !s3_stage_pack_enabled)return;
    S3SoundSource source;
    if(!s3_sound_read_source(g_rom,0x400000,&source,error,sizeof error) ||
       !tri_hle_sound_prepare(&assets,&source,error,sizeof error))fatal();
    s3_hle_sound_assets_index(&assets);bind_providers();campaigns_prepared=1;
    uint8_t hashes[64];recompui_sha256_compute(assets.data,assets.bytes,hashes);
    s3_cd_theme_identity(hashes+32);recompui_sha256_compute(hashes,sizeof hashes,theme_identity);
    fprintf(stderr,"[sound-hle] Immutable campaign sound data ready: %u bytes\n",assets.bytes);
#endif
}
int s3_sound_campaign_music(unsigned content,unsigned game,unsigned slot,unsigned song)
{
#ifdef S3_SOUND_COMBINED
    if(!campaigns_prepared || slot<0x20 || slot>0x21)return 0;
    unsigned flags=0;uint32_t root=tri_hle_music_root(content,game,song,&flags);if(!root)return 0;
    unsigned index=slot-0x20;bindings.music_root[index]=assets.music[slot-1]=root;
    bindings.music_flags[index]=(uint8_t)flags;return 1;
#else
    (void)content;(void)game;(void)slot;(void)song;return 0;
#endif
}
void s3_sound_campaign_effects_apply(unsigned set)
{
    bindings.effect_set=bindings.effect_content=0;
#ifdef S3_SOUND_COMBINED
    unsigned content=(unsigned)tri_bank_current();
    if(campaigns_prepared && set>=1 && set<=2 && tri_hle_effects_available(content,set)){
        bindings.effect_set=(uint8_t)set;bindings.effect_content=(uint8_t)content;
    }
#else
    (void)set;
#endif
}
void s3_sound_campaign_effects(unsigned set,int on)
{
    if(set>2)return;
    if(on)bindings.wanted_set=(uint8_t)set;
    else if(bindings.wanted_set==set)bindings.wanted_set=0;
    else return;
    s3_sound_campaign_effects_apply(bindings.wanted_set);
}
unsigned s3_sound_campaign_effects_wanted(void){return bindings.wanted_set;}
unsigned s3_sound_campaign_effects_active(void){return bindings.effect_set;}
unsigned s3_sound_campaign_effect_root(unsigned native)
{
#ifdef S3_SOUND_COMBINED
    if(bindings.effect_set)return tri_hle_effect_root(bindings.effect_content,bindings.effect_set,native);
#endif
    return native>=0x33 && native<=0xDB?assets.effects[native-0x33]:0;
}
void s3_sound_pause(int on){paused=on!=0;}
void s3_sound_effect_restart(void)
{for(unsigned c=0;c<SMPS_HLE_TRACKS;c++){effects.state.music.track[c].effect=0;effects.state.music.track[c].continuous=0;}}
static void hle_stop_all(void)
{
    output_effect=1;
    if(!smps_hle_stop(&effects) || !smps_hle_stop(&music))fatal();
    pcm_hle_stop(&pcm);output_channel=0;host_fm(NULL,0,0x2B,0);
    memset(pending,0,sizeof pending);paused=fade=fade_delay=tempo=tempo_dirty=requested=last_pause=speed_timeout=0;
}
static int queue(unsigned native,unsigned slot)
{
    if(slot && native>=0x33 && native<=0xDF && bindings.effect_set){
        uint32_t root=s3_sound_campaign_effect_root(native);
        return !root || smps_hle_effect(&effects,root,native>=0xBC);
    }
    unsigned id=native;
    if(native==0xDC && !standalone)id=0x33;
    if((prototype_active || invincibility_override || super_override) && standalone && native==0x2C && g_ram[0xFE19])id=0x33;
    if((native<=0x32 || native>=0xDC) && !s3_cd_theme_request(id))return 1;
    if(native==0x29 && (g_ram[0xF600]&0x7F)==0x0C)s3_cd_theme_finish_clear();
    if(native<=0x32 || (native==0xDC && !standalone)){
        if(!id || !assets.music[id-1])return 1;
        fade=0;
        unsigned flags=native==0x2A?SMPS_HLE_INTERRUPT:0;
        if(native>=0x20 && native<=0x21)flags=bindings.music_flags[native-0x20];
        if(native==0x2A && s3_music_active(S3_THEME_LIFE))flags|=SMPS_HLE_ONE_PASS;
        return smps_hle_music(&music,assets.music[id-1],flags);
    }
    if(native>=0x33 && native<=0xDB){
        unsigned root=assets.effects[native-0x33];
        if(!root)return 1;return smps_hle_effect(&effects,root,native>=0xBC);
    }
    if(native==0xFF){hle_stop_all();return host_pcm(NULL,0xC6,0xC0);}
    if(native==0xE1 || native==0xE5){fade=40;fade_delay=4;tempo=0;return 1;}
    if(native==0xE4){
        unsigned owned=0;for(unsigned c=1;c<10;c++)if(effects.state.music.track[c].active)owned|=1u<<c;
        if(!smps_hle_stop(&effects))return 0;
        output_effect=0;
        for(unsigned c=1;c<10;c++)if(owned&(1u<<c)){output_channel=c;if(!smps_hle_synth_resume(&music_synth,c))return 0;}
        return 1;
    }
    if(native==0xE3){for(unsigned v=0x9F;v<=0xFF;v+=0x20)host_psg(NULL,v);return 1;}
    if(native>=0xE0 || native>=0xDD){hle_stop_all();return 1;}
    return 1;
}
#include "sonic3_music_player.inc"
int s3_sound_owns(uint32_t pc)
{
    if(pc==(standalone?0x1562:0x12CE) || pc==(standalone?0x15E2:0x1358) ||
       pc==(standalone?0x160A:0x1380) || pc==(standalone?0x164C:0x13C2))return 1;
    for(unsigned i=0;i<sizeof s3_hle_pause_sites/sizeof *s3_hle_pause_sites;i++)if(pc==s3_hle_pause_sites[i])return 1;
    return 0;
}
static void zero_move_flags(void){g_cpu.SR=(uint16_t)((g_cpu.SR&~(SR_N|SR_Z|SR_V|SR_C))|SR_Z);}
int s3_sound_hook(uint32_t pc)
{
    if(!s3_sound_owns(pc))return 0;
    stamp=machine_z80_stamp();write_offset=0;
    if(pc==(standalone?0x1562:0x12CE)){
        s3_cd_theme_driver_init();hle_stop_all();bind_providers();s3_sound_campaign_restore();
        bindings.effect_set=bindings.effect_content=0;initialised=1;zero_move_flags();return 1;
    }
    if(pc==(standalone?0x15E2:0x1358)){
        if(!s_player.open){pending[0]=(uint8_t)g_cpu.D[0];s_player.last_music=pending[0];}
    }else if(pc==(standalone?0x160A:0x1380)){
        if(!s_player.open){uint8_t id=(uint8_t)g_cpu.D[0];if(pending[1]!=id)pending[pending[1]?2:1]=id;}
    }else if(pc==(standalone?0x164C:0x13C2)){tempo=(uint8_t)g_cpu.D[0];tempo_dirty=1;}
    else{
        uint8_t value=g_rom[pc+3];paused=value==1;
        g_cpu.SR=(uint16_t)((g_cpu.SR&~(SR_N|SR_Z|SR_V|SR_C))|(value&128?SR_N:0));
        recomp_tail_call(pc+8);return 1;
    }
    zero_move_flags();return 1;
}
void s3_sound_vblank(void)
{
    if(!initialised)return;
    stamp=machine_z80_stamp();write_offset=0;
    if(s_player.request){
        unsigned request=s_player.request;s_player.request=0;player_stop();
        if(request==1){unsigned id=player_id(s_player.track);s3_cd_theme_request(id);if(!smps_hle_music(&music,assets.music[id-1],0))fatal();}
        else if(request==3 && s_player.return_music && !queue(s_player.return_music,0))fatal();
    }
    if(s_player.open){memset(pending,0,sizeof pending);paused=s_player.paused;}
    for(unsigned i=0;i<3;i++)if(pending[i]){unsigned id=pending[i];pending[i]=0;if(!queue(id,i))fatal();}
    if(tempo_dirty){speed_timeout=tempo;tempo_dirty=0;}
    smps_hle_pause(&music,paused);smps_hle_pause(&effects,paused);pcm_hle_pause(&pcm,paused);
    if(paused!=last_pause){
        output_effect=1;
        for(unsigned c=1;c<10;c++){
            output_channel=c;
            if(paused){
                if(c<7){unsigned index=c-1;host_fm(NULL,index/3,0xB4+index%3,0);}
                else host_psg(NULL,0x9F+((c-7)<<5));
            }else if(!smps_hle_synth_resume(effects.state.music.track[c].active?&effect_synth:&music_synth,c))fatal();
        }
        output_channel=0;if(paused){host_fm(NULL,0,0x2A,128);host_psg(NULL,0xFF);}
        last_pause=paused;
    }
    unsigned owned=0;for(unsigned i=0;i<10;i++)if(effects.state.music.track[i].active)owned|=1u<<i;
    int ok=smps_hle_frame(&music);
    for(unsigned check=0;check<2 && tempo && !paused && !music.state.interrupted;check++){
        if(!speed_timeout){speed_timeout=tempo;ok=ok&&smps_hle_frame(&music);}
        --speed_timeout;
    }
    if(!ok || !smps_hle_frame(&effects)){
        snprintf(error,sizeof error,"%s",music.state.failed?music.error:effects.error);fatal();
    }
    output_effect=0;
    for(unsigned i=0;i<10;i++)if((owned&(1u<<i)) && !effects.state.music.track[i].active){output_channel=i;if(!smps_hle_synth_resume(&music_synth,i))fatal();}
    if(fade && !paused && !--fade_delay){
        fade_delay=4;
        for(unsigned i=1;i<10;i++){
            SmpsHleTrack *t=&music.state.music.track[i];if(!t->active)continue;
            unsigned volume=t->volume+(i>=7?4:1);if(volume>127)volume=127;
            t->volume=(uint8_t)volume;SmpsHleEvent ev={SMPS_HLE_VOLUME,i,volume,0};if(!event(&music_synth,&ev))fatal();
        }
        if(!--fade)hle_stop_all();
    }
    if(requested){unsigned id=requested;requested=0;if(!queue(id,0))fatal();}
    s3_cd_theme_frame(paused,music.state.interrupted);
    unsigned resume=s3_cd_theme_resume_music();if(resume && !queue(resume,0))fatal();
}
int s3_sound_dispatch_miss(uint32_t pc){(void)pc;return 0;}
void s3_sound_state(S3StateIO *io)
{
    uint8_t identity[40]={'S','3','H','L',3,0,0,0},incoming[40];
    identity[5]=(uint8_t)active;identity[6]=(uint8_t)prototype_active;memcpy(identity+8,theme_identity,32);
    memcpy(incoming,identity,sizeof incoming);s3_state_peek(io,0,incoming,sizeof incoming);if(memcmp(incoming,identity,sizeof incoming))io->ok=0;
    S3_STATE(io,identity);
    CampaignBindings binding_check=bindings;
    s3_state_peek(io,0,&binding_check,sizeof binding_check);
    for(unsigned i=0;i<2;i++)if(binding_check.music_root[i] && (binding_check.music_root[i]<8192 || binding_check.music_root[i]>=assets.bytes))io->ok=0;
    if((binding_check.music_flags[0]&~3) || (binding_check.music_flags[1]&~3) || binding_check.wanted_set>2 || binding_check.effect_set>2 || binding_check.effect_content>4)io->ok=0;
    S3_STATE(io,bindings);
    if(io->mode==2 && io->ok)for(unsigned i=0;i<2;i++)assets.music[0x20-1+i]=bindings.music_root[i];
    SmpsHleState ms=music.state,es=effects.state;PcmHleState ps=pcm.state;
    s3_state_peek(io,0,&ms,sizeof ms);s3_state_peek(io,sizeof ms,&es,sizeof es);s3_state_peek(io,sizeof ms+sizeof es,&ps,sizeof ps);
    if(!smps_hle_state_valid(&music,&ms)||!smps_hle_state_valid(&effects,&es)||!pcm_hle_state_valid(&pcm,&ps))io->ok=0;
    S3_STATE(io,music.state);S3_STATE(io,effects.state);S3_STATE(io,pcm.state);
    S3_STATE(io,music_synth.state);S3_STATE(io,effect_synth.state);S3_STATE(io,pending);
    S3_STATE(io,initialised);S3_STATE(io,tempo);S3_STATE(io,tempo_dirty);S3_STATE(io,paused);S3_STATE(io,fade);S3_STATE(io,fade_delay);S3_STATE(io,requested);S3_STATE(io,last_pause);S3_STATE(io,speed_timeout);
    s3_cd_theme_state(io);player_state(io);s3_vgm_state(io);
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
#if GEN_ENABLE_TRACE
    static const char *keys[]={"\"music\"","\"sfx\"","\"tempo\""};
    for(unsigned i=0;i<3;i++){
        const char *p=strstr(json,keys[i]);if(!p)continue;p=strchr(p,':');if(!p)continue;
        char *end;long value=strtol(p+1,&end,10);if(end==p+1 || value<0 || value>255 || !initialised)continue;
        if(i==2){tempo=(uint8_t)value;tempo_dirty=1;}else pending[i]=(uint8_t)value;
    }
#else
    (void)json;
#endif
    char response[480];snprintf(response,sizeof response,"{\"id\":%d,\"ok\":true,\"backend\":\"hle\",\"selected\":%d,\"active\":%d,\"initialised\":%u,\"pending\":[%u,%u,%u],\"song\":%u,\"effect_set\":%u,\"effect_content\":%u,\"campaign_slots\":[%u,%u],\"failed\":%u}",id,selected,active,initialised,pending[0],pending[1],pending[2],music.state.song,bindings.effect_set,bindings.effect_content,bindings.music_root[0],bindings.music_root[1],music.state.failed|effects.state.failed);cmd_send_response(response);
}
