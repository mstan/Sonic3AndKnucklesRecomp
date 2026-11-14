/* Local audition from the cartridge's latched controller input. All music
 * sequences and chip channels keep running; selection only changes output. */
static struct {
    unsigned open,previous,track,solo,playing,paused,request,swallow,last_music,return_music;
} s_player;
static uint32_t s_player_bank_end;
static unsigned s_player_count,s_player_tracks[S3_MUSIC_COUNT+1];

static unsigned player_id(unsigned row)
{
    unsigned t=s_player_tracks[row];
    return t==S3_MUSIC_COUNT?0x32:s3_music_target(t);
}
static const char *player_name(unsigned row)
{return s_player_tracks[row]==S3_MUSIC_COUNT?(standalone?"Ending Theme":"Super Theme"):s3_theme_names[s_player_tracks[row]];}
static unsigned player_pointer(unsigned row)
{return m68k_read32(S3_CLONE_MUSIC_INDEX+4*(player_id(row)-1))&0xFFFFFEu;}
static int player_recorded(unsigned row)
{
    unsigned t=s_player_tracks[row];
    return t<S3_MUSIC_COUNT && s3_music_active(t) && s3_theme_donor(t)>=S3_DONOR_CD_US;
}
static int player_channel(unsigned row,unsigned solo)
{
    if(!solo)return 1;
    if(!s_player_count || row>=s_player_count || solo>10 || player_recorded(row))return 0;
    unsigned a=player_pointer(row);
    if(a<0x400000 || a+8>s_player_bank_end)return 0;
    unsigned fm=g_rom_ext[a-0x400000+2],psg=g_rom_ext[a-0x400000+3];
    if(fm<1 || fm>7 || psg>3 || !(solo<=7?solo<=fm:solo-8<psg))return 0;
    unsigned at=8+(solo<=7?(solo-1)*4:fm*4+(solo-8)*6),offset=a-0x400000;
    unsigned root=((unsigned)g_rom_ext[offset+at]<<8)|g_rom_ext[offset+at+1];
    if(a+root+2>s_player_bank_end)return 0;
    /* An immediately stopped header entry has no voice to audition. */
    return g_rom_ext[offset+root]!=0xFF ||
        (g_rom_ext[offset+root+1]!=0x10 && g_rom_ext[offset+root+1]!=0x18);
}
static void player_audition(void)
{
    unsigned mask=0x7FF;
    if(s_player.solo==1)mask=1u<<6;
    else if(s_player.solo<=7 && s_player.solo)mask=1u<<(s_player.solo-2);
    else if(s_player.solo>=8)mask=s_player.solo==10?(3u<<9):(1u<<(s_player.solo-1));
    audio_mixer_set_audition(s_player.open && !player_recorded(s_player.track),mask);
}
static void player_stop(void)
{
    g_cpu.A[6]=S3_CLONE_RAM_ADDRESS;recomp_call_addr(S3_CLONE_STOP_ALL);
    s3_cd_theme_request(0xE0);sound_ram[S3_CLONE_PAUSE_OFFSET]=0;
    memset(pending,0,sizeof pending);tempo_dirty=0;
}
static void player_reset(void)
{
    memset(&s_player,0,sizeof s_player);s_player_count=0;
    s_player_bank_end=0;audio_mixer_set_audition(0,0x7FF);
}
static int player_bind(uint32_t *used,size_t capacity)
{
    (void)capacity;s_player_bank_end=0x400000+*used;
    for(unsigned t=0;t<S3_MUSIC_COUNT;t++)if((s3_music_available(t) || t==S3_MUSIC_EMERALD) &&
       (t!=S3_MUSIC_SUPER || super_override || prototype_active)) {
        unsigned id=s3_music_target(t);
        if(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(id-1)))s_player_tracks[s_player_count++]=t;
    }
    if(m68k_read32(S3_CLONE_MUSIC_INDEX+4*(0x32-1)))s_player_tracks[s_player_count++]=S3_MUSIC_COUNT;
    return s_player_count!=0;
}
void s3_sound_player_frame(unsigned buttons)
{
    unsigned pressed=buttons&~s_player.previous;s_player.previous=buttons;
    unsigned mode=g_ram[0xF600]&127;
    if(!active || !initialised || !s_player_count)return;
    if(s_player.open && mode!=4 && mode!=0x28) {
        s_player.open=s_player.playing=s_player.paused=0;s_player.request=2;s_player.swallow=1;
    }
    if(!s_player.open && (mode==4 || mode==0x28) &&
       (buttons&0x60)==0x60 && (pressed&0x60)) {
        s_player.open=1;s_player.return_music=s_player.last_music;s_player.request=2;
        s_player.swallow=1;
    } else if(s_player.open && !s_player.swallow) {
        if(pressed&0x80){s_player.open=0;s_player.playing=s_player.paused=0;s_player.request=3;s_player.swallow=1;}
        else if(pressed&3) {
            s_player.track=(s_player.track+s_player_count+((pressed&1)?-1:1))%s_player_count;
            if(!player_channel(s_player.track,s_player.solo))s_player.solo=0;
            if(s_player.playing)s_player.request=1;
        } else if((pressed&12) && !player_recorded(s_player.track)) {
            do{s_player.solo=(s_player.solo+11+((pressed&4)?-1:1))%11;}
            while(!player_channel(s_player.track,s_player.solo));
        } else if(pressed&0x10){s_player.playing=1;s_player.paused=0;s_player.request=1;}
        else if((pressed&0x20) && s_player.playing)s_player.paused^=1;
        else if(pressed&0x40){s_player.playing=s_player.paused=0;s_player.request=2;}
    }
    if(s_player.open || s_player.swallow)memset(g_ram+0xF604,0,4);
    if(s_player.open && mode==4){g_ram[0xF614]=2;g_ram[0xF615]=0x58;}
    if(!buttons)s_player.swallow=0;
    player_audition();
}
static void player_state(S3StateIO *io)
{
    unsigned check[10];memcpy(check,&s_player,sizeof check);s3_state_peek(io,0,check,sizeof check);
    if(check[0]>1 || check[1]>0xFFF || check[2]>=(s_player_count?s_player_count:1) ||
       check[3]>10 || check[4]>1 || check[5]>1 || check[6]>3 || check[7]>1 ||
       check[8]>255 || check[9]>255 ||
       (check[3] && !player_channel(check[2],check[3])))io->ok=0;
    S3_STATE(io,s_player);
    if(io->mode==2 && io->ok)player_audition();
}
static const uint8_t player_font[][5]={
 {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
 {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
 {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0,0x41,0x7F,0x41,0},
 {0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
 {0x7F,2,0x0C,2,0x7F},{0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
 {0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},
 {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,8,0x14,0x63},
 {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
 {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
 {6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{8,8,8,8,8},{0x20,0x10,8,4,2},
 {8,8,0x3E,8,8},{0,0,0x40,0,0},{0x30,0x48,0x48,0x30,0x48}
};
static void player_text(int line,uint32_t *out,int width,int x,int y,const char *text,uint32_t color)
{
    int row=line-y;if(row<0 || row>=7)return;
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-/+.&";
    for(unsigned n=0;text[n] && n<49;n++) {
        unsigned c=(unsigned char)text[n];if(c>='a' && c<='z')c-=32;
        const char *p=strchr(alphabet,(int)c);if(!p)continue;
        for(int dx=0;dx<5;dx++) {
            int px=x+(int)n*6+dx;
            if(px>=0 && px<width && (player_font[p-alphabet][dx]&(1u<<row)))out[px]=color;
        }
    }
}
static void player_overlay(int line,uint32_t *out,int width)
{
    if(!active || !initialised || !s_player_count)return;
    int x=(width-320)/2+14;
    if(!s_player.open) {
        if((g_ram[0xF600]&127)==4 || g_ram[0xF600]==0x28)
            player_text(line,out,width,x,208,"A+C: MUSIC PLAYER",0xFFFFFFFF);
        return;
    }
    for(int i=0;i<width;i++)out[i]=0xFF101827;
    static const char *const channels[]={"ALL CHANNELS","DAC","FM1","FM2","FM3","FM4","FM5","FM6","PSG1","PSG2","PSG3 / NOISE"};
    player_text(line,out,width,x,12,"MUSIC PLAYER",0xFF62D9FF);
    player_text(line,out,width,x,28,player_name(s_player.track),0xFFFFFFFF);
    player_text(line,out,width,x,42,s_player.playing?(s_player.paused?"PAUSED":"PLAYING"):"STOPPED",0xFF9FE7AB);
    player_text(line,out,width,x+72,42,channels[s_player.solo],0xFFFFFFFF);
    player_text(line,out,width,x,60,"UP/DOWN: TRACK   LEFT/RIGHT: CHANNEL",0xFFCBD5E1);
    player_text(line,out,width,x,72,"B: PLAY FROM START   C: PAUSE/RESUME",0xFFCBD5E1);
    player_text(line,out,width,x,84,"A: STOP   START: BACK",0xFFCBD5E1);
    player_text(line,out,width,x,100,player_recorded(s_player.track)?"RECORDED AUDIO - FULL MIX ONLY":"CHANNELS CHANGE WITHOUT RESTARTING",0xFF9CAEC6);
    unsigned track=s_player_tracks[s_player.track];
    const char *source=prototype_active?"November 1993 prototype":"Base game";
    if(track<S3_MUSIC_COUNT && s3_music_active(track))source=s3_theme_choice(track,(unsigned)s3_themes[track].choice)->label;
    player_text(line,out,width,x,112,source,0xFFCBD5E1);
    for(int n=-3;n<=3;n++) {
        int at=(int)s_player.track+n;if(at<0 || at>=(int)s_player_count)continue;
        player_text(line,out,width,x,156+n*12,player_name((unsigned)at),n?0xFF9CAEC6:0xFF62D9FF);
    }
}
