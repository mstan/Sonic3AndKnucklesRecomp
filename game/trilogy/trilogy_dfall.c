/*
 * trilogy_dfall.c — the Death Egg falls, between Launch Base and Mushroom Hill.
 *
 * Sonic & Knuckles' own title sequence (SK_Alone_Title_Screen) shows the Death
 * Egg sinking into the island's volcano and the screen shaking; the trilogy
 * plays that part when Launch Base is finished, with no Sonic and no Knuckles
 * on screen (and no SEGA logo): S&K's background and its Death Egg + mountain
 * objects only, run by the cartridge's own code (Process_Sprites,
 * Render_Sprites, SKTitle_DeformBG, VInt_1A), from the white flash to the
 * moment the title would turn to the two of them (Events_routine_fg 3). Then
 * the screen fades to black and Mushroom Hill 1 starts. Silent but for one
 * crash as the Death Egg hits the volcano.
 *
 * A native scene off S3K's GameLoop (TH_GAMELOOP), like the S2 ending.
 */
#include "trilogy_obj.h"
#include "trilogy_hooks.h"
#include "genesis_machine.h"
#include "video/genesis_vdp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GM_SCENE        0x10
#define GM_LEVEL        0x0C
#define RAM_START       0xFF0000u
#define AT_DEATHEGG     0x680         /* ArtTile_SKTitle_DeathEgg */
#define AT_MOUNTAIN     0x7A0         /* ArtTile_SKTitle_Mountain */
#define CMD_FADEOUT     0xE1
#define SFX_CRASH       0x5F          /* sfx_Crash */

enum { P_SETUP, P_RUN, P_FADE, P_DONE };
static int s_active, s_phase, s_timer, s_landed;
static uint16_t s_last_level = 0xFFFF;               /* the level last started (a Launch Base -> Mushroom Hill handover) */

static uint8_t *vram(void) { return g_machine.vdp.vram; }
static void set_reg(int r, uint8_t v) { g_machine.vdp.reg[r] = v; }

static uint32_t kos(uint32_t src, uint32_t dst)      /* Kos_Decomp -> the end of the output */
{
    A(0) = src;
    A(1) = dst;
    tri_jsr(S3K_Kos_Decomp);
    return A(1) & 0xFFFFFFu;
}

static void ram_to_vram(uint32_t src, uint32_t tile, uint32_t bytes)
{
    for (uint32_t k = 0; k < bytes; k++) vram()[(tile * 32u + k) & 0xFFFF] = r8(src + k);
}

static void setup(void)                              /* Title_Screen + SK_Alone_Title_Screen, the parts the fall uses */
{
    D(0) = CMD_FADEOUT;
    tri_jsr(S3K_Play_Music);
    set_reg(0, 0x04);                                /* HInt off */
    set_reg(2, 0x30);                                /* plane A $C000 */
    set_reg(4, 0x07);                                /* plane B $E000 */
    set_reg(16, 0x01);                               /* 64x32 */
    set_reg(18, 0x00);
    set_reg(11, 0x03);                               /* V-scroll full, H-scroll by line */
    set_reg(7, 0x00);
    set_reg(12, 0x81);                               /* H40, no shadow/highlight */
    w8(RAM_Water_full_screen_flag, 0);
    w8(RAM_Water_flag, 0);
    tri_jsr(S3K_Clear_DisplayData);
    for (uint32_t a = 0; a < 0x400; a += 2) w16(RAM_Sprite_table_input + a, 0);
    for (uint32_t a = RAM_Object_RAM; a < RAM_Kos_decomp_buffer; a += 2) w16(a, 0);
    for (uint32_t a = 0; a < 0x100; a += 2) w16(RAM_Camera_RAM + a, 0);
    tri_jsr(S3K_Init_SpriteTable);
    for (uint32_t a = 0; a < 0x100; a += 2) w16(RAM_Normal_palette + a, 0);

    kos(S3K_ArtKos_SKTitleScreenBG, RAM_START);      /* the background -> tile 0 */
    ram_to_vram(RAM_START, 0, 0x1C40);
    kos(S3K_ArtKos_SKTitle_DeathEgg, RAM_START + 0x4E00);
    ram_to_vram(RAM_START + 0x4E00, AT_DEATHEGG, 0xE40);
    kos(S3K_ArtKos_SKTitle_Mountain, RAM_START + 0x5D00);
    ram_to_vram(RAM_START + 0x5D00, AT_MOUNTAIN, 0x2A0);
    A(0) = S3K_MapEni_SKTitle_Background;            /* (the Death Egg object copies its lines on as the screen scrolls) */
    A(1) = RAM_START + 0x7CC0;
    D(0) = 0;
    tri_jsr(S3K_Eni_Decomp);
    A(1) = RAM_START + 0x7CC0;
    D(0) = 0x60000003u;                              /* vdpComm($E000,VRAM,WRITE) */
    tri_jsr(S3K_RAM_Map_Data_To_VDP);

    for (int i = 0; i < 64; i++)                     /* from the white flash (loc_523A) */
        w16(RAM_Target_palette + (uint32_t)i * 2u, tri_rd16(S3K_Pal_SKTitle_Sonic + (uint32_t)i * 2u));
    for (int i = 0; i < 64; i++) w16(RAM_Normal_palette + (uint32_t)i * 2u, (i & 15) ? 0xEEE : 0);
    w16(RAM_Palette_fade_info, 0x003F);
    w16(RAM_Palette_fade_timer, 0x16);
    w16(RAM_Pal_fade_delay2, 0);
    w32(RAM_Player_2, S3K_Obj_SKTitle_DeathEgg);
    w32(RAM_Reserved_object_3, S3K_Obj_SKTitle_Mountain);
    set_reg(1, (uint8_t)(g_machine.vdp.reg[1] | 0x40));   /* display on */
}

static void dfall_frame(void)
{
    switch (s_phase) {
    case P_SETUP:
        setup();
        s_phase = P_RUN;
        break;
    case P_RUN:                                      /* loc_52A6 / loc_537C */
    case P_FADE:
        tri_jsr(S3K_Process_Kos_Queue);
        if (r16(RAM_Palette_fade_timer)) {
            w16(RAM_Palette_fade_timer, (uint16_t)(r16(RAM_Palette_fade_timer) - 1));
            tri_jsr(S3K_Pal_FromWhite);
        }
        if (s_phase == P_FADE) {                     /* Pal_FadeToBlack, a step a frame */
            w16(RAM_Palette_fade_info, 0x003F);
            tri_jsr(S3K_Pal_ToBlack);
            if (--s_timer <= 0) s_phase = P_DONE;
        }
        tri_jsr(S3K_Process_Sprites);
        if (!s_landed && r16(RAM_Events_routine_fg) >= 1) {   /* it hit the volcano (loc_57D8: the shake starts) */
            s_landed = 1;
            o_sfx(SFX_CRASH);
            fprintf(stderr, "[trilogy] the Death Egg lands\n");
        }
        tri_jsr(S3K_Render_Sprites);
        tri_jsr(S3K_SKTitle_DeformBG);
        if (s_phase == P_RUN && r16(RAM_Events_routine_fg) >= 3) {   /* (the title would turn to Sonic and Knuckles) */
            s_phase = P_FADE;
            s_timer = 0x16;
        }
        break;
    default:                                         /* to Mushroom Hill 1 */
        s_active = 0;
        for (uint32_t a = RAM_Object_RAM; a < RAM_Kos_decomp_buffer; a += 2) w16(a, 0);
        for (uint32_t a = 0; a < 0x100; a += 2) w16(RAM_Camera_RAM + a, 0);
        w16(RAM_Events_routine_fg, 0);
        w16(RAM_Screen_shake_flag, 0);
        w8(RAM_Game_mode, GM_LEVEL);
        fprintf(stderr, "[trilogy] Death Egg fall over -> Mushroom Hill\n");
        return;
    }
    w8(RAM_V_int_routine, 0x1A);
    tri_jsr(S3K_Wait_VSync);
}

int tri_dfall_active(void) { return s_active; }

int tri_dfall_hook(uint32_t pc)
{
    if (pc != TH_GAMELOOP) return 0;
    uint8_t gm = r8(RAM_Game_mode);
    if (s_active) {
        if (gm != GM_SCENE) { s_active = 0; return 0; }
        A(0) = tri_native(dfall_frame);
        return 0;
    }
    if ((gm & 0x7F) != GM_LEVEL) {
        if (gm != GM_SCENE) s_last_level = 0xFFFF;   /* (menus, the Data Select: not a handover) */
        return 0;
    }
    uint16_t za = r16(RAM_Current_zone_and_act), last = s_last_level;
    s_last_level = za;
    {
        static int dev = -1;                         /* development: TRILOGY_DFALL=1 plays it before the first level */
        if (dev < 0) { const char *g = getenv("TRILOGY_DFALL"); dev = g && *g; }
        if (dev) { za = 0x0700; last = 0x0601; dev = 0; }
    }
    if (za != 0x0700 || (last >> 8) != 0x06) return 0;
    s_active = 1;
    s_phase = P_SETUP;
    s_landed = 0;
    w8(RAM_Game_mode, GM_SCENE);
    A(0) = tri_native(dfall_frame);
    fprintf(stderr, "[trilogy] Launch Base cleared: the Death Egg falls\n");
    return 0;
}
