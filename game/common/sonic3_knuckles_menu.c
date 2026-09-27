#include "sonic3_knuckles_menu.h"
#include "genesis_runtime.h"
#define ARMY_SITES_DEFINES_ONLY
#include "sonic3_knuckles_army_sites.inc"

int s3_knuckles_menu_owns(uint32_t pc)
{
    return pc == ARMY_PC_SAVE_INIT || pc == ARMY_PC_SAVE_SLOT;
}
void s3_knuckles_menu_hook(uint32_t pc, int enabled)
{
    if (!enabled || g_ram[0xF600] != 0x4C) return;
    if (pc == ARMY_PC_SAVE_INIT) {
        /* Selector initialization runs once per menu visit. The no-save
         * object subsequently permits the usual up/down character cycling. */
        g_ram[0xEF4C] = 0; g_ram[0xEF4D] = 3;
    } else if (pc == ARMY_PC_SAVE_SLOT && (g_ram[g_cpu.A[1] & 0xFFFF] & 0x80)) {
        /* loc_D41A: save data has just been copied into this object's local
         * selection. Only an unused file (negative status byte) gets the
         * mod's default. Stock code writes it to SRAM only on confirmation. */
        unsigned o = g_cpu.A[0] & 0xFFFF;
        g_ram[(o + 0x34) & 0xFFFF] = 0; g_ram[(o + 0x35) & 0xFFFF] = 3;
    }
}
