#pragma once
/* The game-owned hook chain is shared with its composition regression test. */
#include "genesis_runtime.h"
#include "genesis_bus.h"
#include "sonic3_sound_driver.h"
#include "sonic3_restoration.h"
#include "sonic3_state.h"
#include "sonic3_kos.h"
#include "sonic3_gameplay.h"
#include "sonic3_video.h"
#include "sonic3_knuckles_army.h"
#include "trilogy.h"

static int s3k_instruction(uint32_t pc)
{
    if(s3_sound_active() && s3_sound_owns(pc))return s3_sound_hook(pc);
    if(s3_restoration_instruction(pc))return 0;
    if (s3_state_menu_instruction(pc)) return 0;
    if(s3_kos_owns(pc))return s3_kos_instruction(pc);
    if(s3_gameplay_title_card_sprite(pc,g_ram,g_cpu.A[0]))return 1;
    if(s3_gameplay_owns(pc)) {
        uint32_t entry=s3_gameplay_zone_entry(pc,g_ram);
        if(entry){recomp_tail_call(entry);return 1;}
        uint32_t departure=s3_gameplay_zone_departure(pc,g_ram,g_cpu.A[0]);
        if(departure){recomp_tail_call(departure);return 1;}
        uint32_t card=s3_gameplay_title_card(pc,g_ram,g_cpu.A[0]);
        if(card){recomp_tail_call(card);return 1;}
        s3_gameplay_allocation_hook(pc,g_ram,g_cpu.A[1],&g_cpu.SR);
        s3_gameplay_nemesis_pop(pc,g_ram,&g_cpu.SR);
        s3_gameplay_palette_reset(pc,g_ram);
        s3_gameplay_hud_reset(pc,g_ram);
        s3_gameplay_zone_order(pc,g_ram,&g_cpu.D[0]);
        s3_gameplay_timer_hook(pc,g_ram,&g_cpu.D[1],&g_cpu.SR);
        if(s3_gameplay_skip_shield(pc,g_ram,g_cpu.A[0],!tri_character_active()))g_cpu.SR|=4u;
        return 0; /* This private site must not reach the renderer default. */
    }
    if (trilogy_instruction(pc)) return 1;
    /* New hook sites must never reach the renderer's object-range default,
     * even with the pack disabled. Shared army sites retain their old chain. */
    if (trilogy_owns(pc) && !s3_army_owns(pc)) return 0;
    /* With the original driver, retain CD's music/tempo interception above.
     * New audio-only sites still must not reach the renderer's default. */
    if(s3_sound_owns(pc))return 0;
    return s3_sk_instruction_hook(pc);
}
