/*
 * trilogy_core.c — expansion bank, hook chain, native code dispatch.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "trilogy_hooks.h"
#include "stage_pack.h"
#include "campaign_mighty.h"
#include "scd_pack.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_bank;
static uint32_t s_bank_size;
static uint8_t *s_banks[4];
static uint32_t s_sizes[4];
static int s_selected=-1;
static uint32_t s_mighty[4],s_shadow[4];
static ScdPack s_cd_pack;
static uint16_t s_cd_level=0xFFFF;
static int s_cd_dirty;
const ScdPack *tri_scd_pack(void){return &s_cd_pack;}
int tri_scd_available(void){return s3_stage_pack_cd && s_banks[3]!=NULL;}
uint32_t tri_scd_sound(void){return s_selected==3 && s_cd_pack.sound?SCD_SOUND_BASE+0x8000:0;}
int tri_scd_map(uint16_t za)
{
    if(!s_banks[3])return 0;
    if(za==s_cd_level && !s_cd_dirty)return 1;
    uint32_t size;const uint8_t *blob=scd_pack_level(&s_cd_pack,za,&size);
    if(!blob)return 0;
    uint8_t *window=s_banks[3]+SCD_WINDOW-TRI_BANK_BASE;
    memcpy(window,blob,size);memset(window+size,0,SCD_WINDOW_SIZE-size);
    s_cd_level=za;s_cd_dirty=0;return 1;
}
void tri_scd_wr16(uint32_t address,uint16_t value)
{
    if(!s_banks[3] || address<SCD_WINDOW || address-SCD_WINDOW>SCD_WINDOW_SIZE-2)return;
    uint8_t *p=s_banks[3]+address-TRI_BANK_BASE;p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value;s_cd_dirty=1;
}
uint32_t tri_mighty_descriptor(void)
{ return s_selected>=0?s_mighty[s_selected]:0; }
uint32_t tri_shadow_descriptor(void)
{ return s_selected>=0?s_shadow[s_selected]:0; }

int tri_bank_select(int content)
{
    int old=s_selected;
    int i=content-1;
    if(i<0 || i>3 || !s_banks[i]) {
        i=s_banks[2]?2:s_banks[0]?0:s_banks[1]?1:s_banks[3]?3:-1;
    }
    s_selected=i;s_bank=i>=0?s_banks[i]:NULL;s_bank_size=i>=0?s_sizes[i]:0;
    g_rom_ext=s_bank;g_rom_ext_size=s_bank_size;
    return old+1;
}
int tri_stage_content(uint16_t stage)
{
    unsigned z=stage>>8;
    return z>=0x20&&z<=0x26?1:z>=0x28&&z<=0x32?2:z>=0x34&&z<=0x3A?3:z>=0x40&&z<0x5C?4:0;
}
void tri_bank_level(void)
{
    uint16_t stage=r16(RAM_Current_zone_and_act);int c=tri_stage_content(stage);
    if(c && (!s_banks[c-1] || (c==3 && !s3_stage_pack_megamix) || (c==4 && !tri_scd_map(stage)))) {
        fprintf(stderr,"[stage-pack] unavailable stage %04X; resuming Angel Island\n",stage);
        w16(RAM_Current_zone_and_act,0);w16(RAM_Apparent_zone_and_act,0);
        w8(RAM_Last_star_post_hit,0);c=0;
    }
    tri_bank_select(c);
}

int tri_bank_loaded(void) { return s_bank != NULL; }
int tri_bank_current(void){return s_selected+1;}

/* A newer save schema must leave native saving available without any addon
 * RAM projection or save hooks. Requested settings and files remain parked. */
void tri_campaign_suspend(void)
{
    extern void scd_audio_close(void);scd_audio_close();
    s_bank=NULL;s_bank_size=0;s_selected=-1;g_rom_ext=NULL;g_rom_ext_size=0;
    s3_stage_pack_enabled=s3_stage_pack_sonic1=s3_stage_pack_sonic2=0;
    s3_stage_pack_megamix=s3_stage_pack_mighty=s3_stage_pack_shadow=0;
    s3_stage_pack_cd=0;
}

void trilogy_init(void)
{
    if (!s3_stage_pack_enabled) return;
    if (s_bank)
        return;
    for(int i=0;i<3;i++) {
    if(!s3_pack_available[i])continue;
    char error[256];
    if (!s3_pack_validate(s3_pack_paths[i],i+1,error,sizeof error)) {
        fprintf(stderr,"[stage-pack] content %d unavailable: %s\n",i+1,error);s3_pack_available[i]=0;continue;
    }
    FILE *f = fopen(s3_pack_paths[i], "rb");
    if (!f) {s3_pack_available[i]=0;continue;}
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0x2040 || n > 0x400000 || (n & 1)) {
        fprintf(stderr, "[trilogy] bad bank size %ld\n", n);
        fclose(f);
        s3_pack_available[i]=0;continue;
    }
    s_banks[i] = (uint8_t *)malloc((size_t)n);
    if (!s_banks[i] || fread(s_banks[i], 1, (size_t)n, f) != (size_t)n || memcmp(s_banks[i], "TRLG", 4) != 0) {
        fprintf(stderr, "[trilogy] bank read/magic failed\n");
        free(s_banks[i]);s_banks[i]=NULL;
        fclose(f);
        s3_pack_available[i]=0;continue;
    }
    fclose(f);
    s_sizes[i]=(uint32_t)n;
    }
    if(s3_pack_available[3]) {
        char error[256];
        if(scd_pack_open(&s_cd_pack,s3_pack_paths[3],error,sizeof error)) {
            s_banks[3]=calloc(0x2040,1);
            if(s_banks[3]){s_sizes[3]=0x2040;memcpy(s_banks[3],"TRLG\0\1",6);}
            else s3_pack_available[3]=0;
        } else {fprintf(stderr,"[scd] %s\n",error);s3_pack_available[3]=0;}
    }
    if(s_banks[2]) {
        uint32_t info=((uint32_t)s_banks[2][0x24]<<24)|((uint32_t)s_banks[2][0x25]<<16)|((uint32_t)s_banks[2][0x26]<<8)|s_banks[2][0x27];
        const uint8_t *p=s_banks[2]+info-TRI_BANK_BASE+12;
        s_mighty[2]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        if(s_banks[2][info-TRI_BANK_BASE+11]>=5) {
            p=s_banks[2]+info-TRI_BANK_BASE+40;
            s_shadow[2]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        }
        for(int i=0;i<4;i++)if(i!=2 && s_banks[i]) {
            s_mighty[i]=campaign_mighty_copy(&s_banks[i],&s_sizes[i],s_banks[2],s_sizes[2]);
            if(s_shadow[2])s_shadow[i]=campaign_shadow_copy(&s_banks[i],&s_sizes[i],s_banks[2],s_sizes[2]);
            if(!s_mighty[i] || (s_shadow[2] && !s_shadow[i])) {
                fprintf(stderr,"[stage-pack] content %d cannot attach Mighty resources; pack skipped\n",i+1);
                free(s_banks[i]);s_banks[i]=NULL;s_sizes[i]=0;s3_pack_available[i]=0;
            }
        }
    }
    if(s_banks[3]) {
        uint32_t total=SCD_SOUND_BASE-TRI_BANK_BASE+((s_cd_pack.sound_size+1)&~1u);
        uint8_t *bank=s_sizes[3]<=SCD_WINDOW-TRI_BANK_BASE && total<=0x7E0000-TRI_BANK_BASE?realloc(s_banks[3],total):NULL;
        if(!bank) {free(s_banks[3]);s_banks[3]=NULL;s_sizes[3]=0;s3_pack_available[3]=0;}
        else {
            memset(bank+s_sizes[3],0,total-s_sizes[3]);
            memcpy(bank+SCD_SOUND_BASE-TRI_BANK_BASE,s_cd_pack.sound,s_cd_pack.sound_size);
            s_banks[3]=bank;s_sizes[3]=total;
        }
    }
    s3_stage_pack_sonic1=s3_pack_available[0];s3_stage_pack_sonic2=s3_pack_available[1];
    s3_stage_pack_megamix=s3_pack_requested[2] && s3_pack_available[2];
    s3_stage_pack_mighty=s3_pack_mighty_requested && s_mighty[2]!=0;
    s3_stage_pack_shadow=s3_pack_shadow_requested && s_shadow[2]!=0;
    s3_stage_pack_cd=s3_pack_available[3];
    s3_stage_pack_enabled=s3_stage_pack_sonic1||s3_stage_pack_sonic2||s3_stage_pack_megamix||s3_stage_pack_cd||s3_stage_pack_mighty||s3_stage_pack_shadow;
    if(!s3_stage_pack_enabled)return;
    tri_screen_register();
    extern int tri_obj_audit_bank(void);
    for(int i=0;i<4;i++)if(s_banks[i]) {
        tri_bank_select(i+1);
        if(!tri_obj_audit_bank()) {
            if(i!=3)exit(EXIT_FAILURE);
            fprintf(stderr,"[scd] incomplete object contract; CD skipped, progress retained\n");
            free(s_banks[3]);s_banks[3]=NULL;s_sizes[3]=0;s3_pack_available[3]=s3_stage_pack_cd=0;
        }
    }
    s3_stage_pack_enabled=s3_stage_pack_sonic1||s3_stage_pack_sonic2||s3_stage_pack_megamix||s3_stage_pack_cd||s3_stage_pack_mighty||s3_stage_pack_shadow;
    tri_bank_select(0);
    fprintf(stderr, "[trilogy] expansion bank mapped at $400000 (%u bytes)\n", s_bank_size);
}

/* ---- reads across ROM / bank / RAM -------------------------------------- */
uint8_t tri_rd8(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x400000u) return g_rom[a];
    if (a - TRI_BANK_BASE < s_bank_size) return s_bank[a - TRI_BANK_BASE];
    if (a >= 0xFF0000u) return g_ram[a & 0xFFFF];
    return 0xFF;
}
uint16_t tri_rd16(uint32_t a) { return (uint16_t)((tri_rd8(a) << 8) | tri_rd8(a + 1)); }
uint32_t tri_rd32(uint32_t a) { return ((uint32_t)tri_rd16(a) << 16) | tri_rd16(a + 2); }

uint32_t tri_dir_for(int zone, int act)
{
    if(tri_cd_zone(zone))return s_selected==3 && s_cd_level==(uint16_t)(zone<<8|act)?SCD_WINDOW:0;
    if (!s_bank || zone < TRI_ZONE_BASE || zone >= TRI_ZONE_END || act < 0 || act > 3)
        return 0;
    uint32_t idx = (uint32_t)(zone - TRI_ZONE_BASE) * 4u + (uint32_t)act;
    if (idx >= TRI_DIR_COUNT)
        return 0;
    int c=tri_stage_content((uint16_t)(zone<<8));
    if(!c || !s3_pack_available[c-1] || s_selected!=c-1)return 0;
    uint32_t e = TRI_BANK_BASE + TRI_DIR_OFF + idx * TRI_DIR_ENTRY;
    if(tri_rd8(e+TD_GAME)==TRI_GAME_MEGAMIX && !s3_stage_pack_megamix)return 0;
    return tri_rd32(e + TD_LLB) ? e : 0;
}

uint32_t tri_dir(void) { return tri_dir_for(tri_zone(), tri_act()); }

int tri_source_zone(void)
{
    uint32_t e=tri_dir();
    return e && tri_rd8(e+TD_GAME)==TRI_GAME_MEGAMIX
        ?0x20+(int)tri_rd8(tri_rd32(e+TD_EXTRA)+24):tri_zone();
}
uint32_t tri_s1_info(void)
{
    uint32_t e=tri_dir();if(!e)return 0;
    uint32_t info=tri_rd32(e+TD_EXTRA);
    return tri_rd8(e+TD_GAME)==TRI_GAME_MEGAMIX?tri_rd32(info+28):info;
}
uint16_t tri_source_stage(uint16_t stage)
{
    uint32_t e=tri_dir();
    return e && tri_rd8(e+TD_GAME)==TRI_GAME_MEGAMIX
        ?(uint16_t)(stage+0x1400):stage;
}

/* ---- JSR-exact call into recompiled code -------------------------------- */
void tri_jsr(uint32_t addr)
{
    recomp_push_return(TRILOGY_NATIVE_BASE);      /* return slot (never executed) */
    uint32_t slot = g_cpu.A[7];
    int saved = g_split_sp_popped;
    g_split_sp_popped = 0;
    recomp_call_addr(addr);
    g_split_sp_popped = saved;
    if (g_rte_pending)
        return;
    if (g_cpu.A[7] != slot + 4u)
        g_cpu.A[7] += 4u;
}

/* ---- native routines at pseudo addresses -------------------------------- */
static TriNativeFn s_native[TRILOGY_NATIVE_MAX];
static unsigned s_native_count;

uint32_t tri_native(TriNativeFn fn)
{
    for (unsigned i = 0; i < s_native_count; i++)
        if (s_native[i] == fn)
            return TRILOGY_NATIVE_BASE + 2u * (i + 1u);
    if (s_native_count >= TRILOGY_NATIVE_MAX) {
        fprintf(stderr, "[trilogy] native table full\n");
        abort();
    }
    s_native[s_native_count++] = fn;
    return TRILOGY_NATIVE_BASE + 2u * s_native_count;
}

int trilogy_dispatch(uint32_t addr)
{
    addr &= 0xFFFFFF;
    if (!s_bank) return 0;
    if (tri_obj_dispatch(addr))
        return 1;
    if (addr < TRILOGY_NATIVE_BASE)
        return 0;
    uint32_t i = (addr - TRILOGY_NATIVE_BASE) / 2u;
    if (i == 0 || i > s_native_count || (addr & 1))
        return 0;
    s_native[i - 1]();
    return 1;
}

/* ---- hook chain --------------------------------------------------------- */
/* The widescreen renderer's hook treats any PC it doesn't list as an object
 * range check and rewrites D0, so our sites must never reach it. */
int trilogy_owns(uint32_t pc)
{
    if(tri_megamix_geometry_owns(pc))return 1;
    switch (pc) {
    CD_GEOMETRY_CASES return 1;
    TH_ALL_CASES return 1;
    default: return 0;
    }
}

int trilogy_instruction(uint32_t pc)
{
    if (pc == TH_CD_MUSIC_REQUEST || pc == TH_CD_MUSIC_TEMPO) {
        extern int tri_scd_music_request(unsigned id, int tempo);
        if (tri_scd_music_request(D(0) & 255u, pc == TH_CD_MUSIC_TEMPO)) return 1;
    }
    extern int tri_save_journal_hook(uint32_t pc);
    if(tri_save_journal_hook(pc))return 1;
    if (!s_bank)
        return 0;
    if(!s_bank)return 0;
    if (tri_megamix_geometry_hook(pc))return 1;
    if (tri_megamix_sprite_hook(pc))return 1;
    if (tri_level_hook(pc))
        return 1;
    if (tri_mighty_presentation_hook(pc))return 1;
    if (tri_mighty_hook(pc))return 1;
    if (tri_shadow_hook(pc))return 1;
    if (tri_screen_hook(pc))
        return 1;
    if (tri_titlecard_hook(pc))
        return 1;
    if (tri_flow_hook(pc))
        return 1;
    {
        extern int scd_ttz_water(uint32_t pc);
        if (scd_ttz_water(pc))                       /* (Tidal Tempest: Sonic CD's water levels) */
            return 1;
    }
    if (tri_water_hook(pc))
        return 1;
    /* Sonic CD's FindLevelFloor(2) .CheckNegFloor: looking down (a3 = $10), a
     * block solid only at its top (a negative height) is a full block with a
     * flat angle; S3K takes its own angle (Palmtree Panic's logs: $80, a
     * ceiling's, which flipped the player off them) */
    if ((pc == TH_FLOOR_NEG || pc == TH_FLOOR2_NEG) && (int16_t)A(3) == 0x10 && tri_cd_zone(tri_zone())) {
        w8(A(4), 0);
        uint16_t d0 = (uint16_t)((D(2) & 0xF) + 0x10);
        D(0) = (D(0) & 0xFFFF0000u) | d0;
        D(1) = (D(1) & 0xFFFF0000u) | (uint16_t)(0xF - d0);
        return 1;                                    /* (rts) */
    }
    /* The ground's wall ahead (CalcRoomInFront: d0 its direction, d1 the room
     * there). At speed the step ahead lands inside a tight curve's steeper part
     * (Stardust Speedway's risers: at $900 and more, a step of 9 px or more):
     * S3K, as Sonic CD, then stops the player dead at its foot. On a slope
     * already rising toward that side, the step is cut short at the wall and the
     * speed kept: the curve takes him up the next frame. Again the next frame,
     * the slope no steeper, it is a wall after all: the stop as ever */
    if ((pc == TH_ROOM_AHEAD_S || pc == TH_ROOM_AHEAD_T || pc == TH_ROOM_AHEAD_K) && (int16_t)D(1) < 0
        && tri_cd_zone(tri_zone())) {
        static uint16_t s_frame[2];
        static uint8_t s_ang[2];
        uint32_t p = A(0);
        int k = p != RAM_Player_1;
        uint8_t ang = r8(p + OBJ_angle), side = (uint8_t)(((uint8_t)D(0) + 0x20) & 0xC0);
        int16_t gv = (int16_t)r16(p + OBJ_ground_vel);
        int up = side == 0xC0 ? gv > 0 && ang >= 0xC1 && ang <= 0xF0
               : side == 0x40 ? gv < 0 && ang >= 0x10 && ang <= 0x3F : 0;
        uint16_t f = r16(RAM_Level_frame_counter);
        if (up && !(s_frame[k] == (uint16_t)(f - 1) && s_ang[k] == ang)) {
            int16_t cut = (int16_t)((uint16_t)D(1) << 8);
            w16(p + OBJ_x_vel, (uint16_t)(r16(p + OBJ_x_vel) + (side == 0xC0 ? cut : -cut)));
            D(1) &= 0xFFFF0000u;                     /* (room enough: on as he was) */
            s_frame[k] = f;
            s_ang[k] = ang;
        }
        return 0;
    }
    {
        extern void scd_ppz_hook(uint32_t pc), scd_time_hook(uint32_t pc), scd_wwz_hook(uint32_t pc);
        scd_ppz_hook(pc);                            /* (Palmtree Panic's 3D ramps; the menu runs there too) */
        scd_wwz_hook(pc);                            /* (Wacky Workbench's floors) */
        scd_time_hook(pc);                           /* (Sonic CD's time travel) */
        /* (Sonic CD's level events set the camera's bottom outright, as its
         *  bosses' do: S3K's MoveCameraY clamps to it only while it is changing) */
        if (pc == TH_LVL_POST_SPRITES && tri_cd_zone(tri_zone()) &&
            (int16_t)r16(RAM_Camera_Y_pos) > (int16_t)r16(RAM_Camera_max_Y_pos))
            w8(RAM_Camera_max_Y_pos_changing, 1);
        extern void scd_mmz_hook(uint32_t pc);
        scd_mmz_hook(pc);                            /* (Metallic Madness: layers, belts, mini Sonic) */
        extern void scd_ssz_hook(uint32_t pc);
        scd_ssz_hook(pc);                            /* (Stardust Speedway: layers, loops, sparks) */
        extern void scd_qqz_hook(uint32_t pc);
        scd_qqz_hook(pc);                            /* (Quartz Quadrant: its belts) */
        extern void scd_qqz3_hook(uint32_t pc);
        scd_qqz3_hook(pc);                           /* (Quartz Quadrant act 3: the boss's events, belt colours) */
        extern void scd_ttz_hook(uint32_t pc);
        scd_ttz_hook(pc);                            /* (Tidal Tempest: its currents, the wave strip) */
        extern void scd_ttz3_hook(uint32_t pc);
        scd_ttz3_hook(pc);                           /* (Tidal Tempest act 3: the boss's level events) */
        extern void scd_ccz_hook(uint32_t pc);
        scd_ccz_hook(pc);                            /* (Collision Chaos: tile bumpers, flipper jumps) */
        extern int scd_time_pal_hook(uint32_t pc);
        if (scd_time_pal_hook(pc))                   /* (arriving from a time warp: white, no card) */
            return 1;
    }
    if (tri_menu_hook(pc))
        return 1;
    {
        if (pc == TH_BOSS_HIT) {
            extern int tri_obj_boss_hit(void);
            return tri_obj_boss_hit();               /* (S1/S2 bosses keep state where S3K's hit writes) */
        }
        if (pc == TH_GAMELOOP) {
            extern void tri_sound_set_tick(void);
            tri_sound_set_tick();                    /* (the S2 sound set survives driver reloads) */
        }
        extern int tri_dfall_hook(uint32_t pc);
        if (tri_dfall_hook(pc))
            return 1;
        extern int tri_s2end_hook(uint32_t pc);
        if (tri_s2end_hook(pc))
            return 1;
        extern int tri_scdss_hook(uint32_t pc),tri_scdwarp_hook(uint32_t pc);
        if(tri_scdss_hook(pc) || tri_scdwarp_hook(pc))return 1;
        extern int tri_s2ss_hook(uint32_t pc);
        if (tri_s2ss_hook(pc))
            return 1;
        extern int tri_save_hook(uint32_t pc);
        if (tri_save_hook(pc))
            return 1;
        if (pc == TH_GAMELOOP && r8(RAM_Game_mode) == 0x4C) {   /* Data Select */
            extern void tri_save_dev_clear(void);
            tri_save_dev_clear();
        }
    }
    tri_ext_hook(pc);
    return 0;
}
