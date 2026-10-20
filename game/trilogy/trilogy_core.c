/*
 * trilogy_core.c — expansion bank, hook chain, native code dispatch.
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "trilogy_hooks.h"
#include "stage_pack.h"
#include "campaign_mighty.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_bank;
static uint32_t s_bank_size;
static uint8_t *s_banks[3];
static uint32_t s_sizes[3];
static int s_selected=-1;
static uint32_t s_mighty[3];
static uint32_t s_shadow[3];
uint32_t tri_mighty_descriptor(void)
{ return s_selected>=0?s_mighty[s_selected]:0; }
uint32_t tri_shadow_descriptor(void)
{ return s_selected>=0?s_shadow[s_selected]:0; }

int tri_bank_select(int content)
{
    int old=s_selected;
    int i=content-1;
    if(i<0 || i>2 || !s_banks[i]) {
        i=s_banks[2]?2:s_banks[0]?0:s_banks[1]?1:-1;
    }
    s_selected=i;s_bank=i>=0?s_banks[i]:NULL;s_bank_size=i>=0?s_sizes[i]:0;
    g_rom_ext=s_bank;g_rom_ext_size=s_bank_size;
    return old+1;
}
int tri_stage_content(uint16_t stage)
{
    unsigned z=stage>>8;
    return z>=0x20&&z<=0x26?1:z>=0x28&&z<=0x32?2:z>=0x34&&z<=0x3A?3:0;
}
void tri_bank_level(void)
{
    uint16_t stage=r16(RAM_Current_zone_and_act);int c=tri_stage_content(stage);
    if(c && (!s_banks[c-1] || (c==3 && !s3_stage_pack_megamix))) {
        fprintf(stderr,"[stage-pack] unavailable stage %04X; resuming Angel Island\n",stage);
        w16(RAM_Current_zone_and_act,0);w16(RAM_Apparent_zone_and_act,0);
        w8(RAM_Last_star_post_hit,0);c=0;
    }
    tri_bank_select(c);
}

int tri_bank_loaded(void) { return s_bank != NULL; }
void tri_campaign_suspend(void)
{
    s3_stage_pack_enabled=s3_stage_pack_sonic1=s3_stage_pack_sonic2=0;
    s3_stage_pack_megamix=s3_stage_pack_mighty=s3_stage_pack_shadow=0;
    s_selected=-1;s_bank=NULL;s_bank_size=0;g_rom_ext=NULL;g_rom_ext_size=0;
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
    if(s_banks[2]) {
        uint32_t info=((uint32_t)s_banks[2][0x24]<<24)|((uint32_t)s_banks[2][0x25]<<16)|((uint32_t)s_banks[2][0x26]<<8)|s_banks[2][0x27];
        const uint8_t *p=s_banks[2]+info-TRI_BANK_BASE+12;
        s_mighty[2]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        if(s_banks[2][info-TRI_BANK_BASE+11]>=5) {
            p=s_banks[2]+info-TRI_BANK_BASE+40;
            s_shadow[2]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        }
        for(int i=0;i<2;i++)if(s_banks[i]) {
            s_mighty[i]=campaign_mighty_copy(&s_banks[i],&s_sizes[i],s_banks[2],s_sizes[2]);
            if(s_shadow[2])s_shadow[i]=campaign_shadow_copy(&s_banks[i],&s_sizes[i],s_banks[2],s_sizes[2]);
            if(!s_mighty[i] || (s_shadow[2] && !s_shadow[i])) {
                fprintf(stderr,"[stage-pack] content %d cannot attach Mighty resources; pack skipped\n",i+1);
                free(s_banks[i]);s_banks[i]=NULL;s_sizes[i]=0;s3_pack_available[i]=0;
            }
        }
    }
    s3_stage_pack_sonic1=s3_pack_available[0];s3_stage_pack_sonic2=s3_pack_available[1];
    s3_stage_pack_megamix=s3_pack_requested[2] && s3_pack_available[2];
    s3_stage_pack_mighty=s3_pack_mighty_requested && s_mighty[2]!=0;
    s3_stage_pack_shadow=s3_pack_shadow_requested && s_shadow[2]!=0;
    s3_stage_pack_enabled=s3_stage_pack_sonic1||s3_stage_pack_sonic2||s3_stage_pack_megamix||s3_stage_pack_mighty||s3_stage_pack_shadow;
    if(!s3_stage_pack_enabled)return;
    tri_screen_register();
    extern int tri_obj_audit_bank(void);
    for(int i=0;i<3;i++)if(s_banks[i]) {tri_bank_select(i+1);if(!tri_obj_audit_bank())exit(EXIT_FAILURE);}
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
    TH_ALL_CASES return 1;
    default: return 0;
    }
}

int trilogy_instruction(uint32_t pc)
{
    extern int tri_save_journal_hook(uint32_t pc);
    if(tri_save_journal_hook(pc))return 1;
    if (!s_bank)
        return 0;
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
    if (tri_water_hook(pc))
        return 1;
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
