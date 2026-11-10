#include "sonic3_sound_driver.h"
#include "sonic3_sound_assets.h"
#include "sonic3_state_io.h"
#include "genesis_runtime.h"
#include "genesis_bus.h"
#include "host_atomic_file.h"
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
    if(on && !s3_sound_available()){snprintf(error,sizeof error,"Clone Driver requires the standard Z80 backend in this build.");return 0;}
#ifdef S3_SOUND_COMBINED
    if((on || active) && s3_stage_pack_enabled){snprintf(error,sizeof error,"Disable imported campaigns before enabling Clone Driver; restart after changing drivers.");return 0;}
#endif
    selected=on!=0;*error=0;return 1;
}
void s3_sound_load(const char *path,const char *mode)
{
    selected=active=bound=0;initialised=tempo=tempo_dirty=0;memset(pending,0,sizeof pending);*error=0;
    standalone=!strcmp(mode,"sonic3");
    snprintf(settings_file,sizeof settings_file,"%s",path?path:"settings.ini");
    char *s=strrchr(settings_file,'/'),*b=strrchr(settings_file,'\\');if(!s || (b && b>s))s=b;
    size_t prefix=s?(size_t)(s-settings_file)+1:0;
    if(prefix+strlen(mode)+12>=sizeof settings_file){snprintf(error,sizeof error,"Sound settings path too long");return;}
    snprintf(settings_file+prefix,sizeof settings_file-prefix,"%s-sound.ini",mode);
    FILE *f=fopen(settings_file,"rb");if(!f)return;
    char line[128];while(fgets(line,sizeof line,f)){line[strcspn(line,"\r\n")]=0;if(!strcmp(line,"enabled=1"))selected=1;}fclose(f);
}
int s3_sound_commit(void)
{
    if(!s3_sound_select(selected))return 0;
    char text[64];int n=snprintf(text,sizeof text,"[sound_driver]\nenabled=%d\n",selected);
    if(!genesis_atomic_file_store(settings_file,text,(size_t)n)){snprintf(error,sizeof error,"Unable to save sound settings");return 0;}return 1;
}
int s3_sound_netplay_allowed(void){return !(selected || active);}
void s3_sound_post_reset(void)
{
    if(bound)return;
    if(selected && !s3_sound_select(1)){fprintf(stderr,"[sound-driver] %s\n",error);exit(1);}
    active=selected;bound=1;if(!active)return;
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
    g_rom_ext=bank;g_rom_ext_size=(stats.bytes+1)&~1u;
    memset(sound_ram,0,sizeof sound_ram);
    if(!gbus_map_expansion_ram(S3_CLONE_RAM_ADDRESS,sound_ram,sizeof sound_ram)){fprintf(stderr,"[sound-driver] RAM mapping failed\n");exit(1);}
    fprintf(stderr,"[sound-driver] Clone Driver + Mega PCM; %u songs, %u SFX; ROM-derived bank %u bytes\n",stats.music,stats.sfx,g_rom_ext_size);
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
    else if(native==0xDC && !standalone)id=0x33;
    else if(native>=0x33 && native<=0xDB)id=native+0xD;
    else if(native==0xE1 || native==0xE5)id=0xFB;
    else if(native==0xE4)id=0xFA;
    else if(native==0xFF)id=0xFC;
    else if(native>=0xE0 || (native>=0xDD && native<=0xDF))id=0xFF;
    if(native==0xE3){ /* original command silences PSG only */
        for(unsigned v=0x9F;v<=0xFF;v+=0x20)m68k_write8(0xC00011,(uint8_t)v);
        return;
    }
    if(!id)return;
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
    recomp_call_addr(S3_CLONE_GUARDED_UPDATE);g_cpu=saved;
#endif
}
int s3_sound_dispatch_miss(uint32_t pc)
{
    if(active && pc>=0x400000 && pc<0x800000){fprintf(stderr,"[sound-driver] Missing native target $%06X\n",pc);exit(1);}return 0;
}
void s3_sound_state(struct S3StateIO *io)
{
    uint8_t identity[40]={'S','3','S','D',1,0,0,0},incoming[40];identity[5]=(uint8_t)(bound?active:selected);
#ifdef S3_CLONE_DRIVER
    if(identity[5])for(unsigned i=0;i<32;i++){
        unsigned a=S3_CLONE_SHA256[i*2],b=S3_CLONE_SHA256[i*2+1];
        identity[8+i]=(uint8_t)(((a<='9'?a-'0':a-'a'+10)<<4)|(b<='9'?b-'0':b-'a'+10));
    }
#endif
    memcpy(incoming,identity,sizeof incoming);s3_state_peek(io,0,incoming,sizeof incoming);
    if(memcmp(incoming,identity,sizeof incoming))io->ok=0;
    S3_STATE(io,identity);S3_STATE(io,sound_ram);S3_STATE(io,pending);S3_STATE(io,initialised);S3_STATE(io,tempo);S3_STATE(io,tempo_dirty);
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
        char *end;long v=strtol(p+1,&end,10);if(end==p+1 || v<0 || v>255 || !active || !initialised)continue;
        uint32_t pc=i==0?(standalone?0x15E2:0x1358):i==1?(standalone?0x160A:0x1380):(standalone?0x164C:0x13C2);
        M68KState saved=g_cpu;g_cpu.D[0]=(uint32_t)v;s3_sound_hook(pc);g_cpu=saved;
    }
#else
    (void)json;
#endif
    char response[200];snprintf(response,sizeof response,"{\"id\":%d,\"ok\":true,\"selected\":%d,\"active\":%d,\"initialised\":%u,\"pending\":[%u,%u,%u]}",id,selected,active,initialised,pending[0],pending[1],pending[2]);cmd_send_response(response);
}
