#include "sonic3_knuckles_menu.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t g_ram[65536];
M68KState g_cpu;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
int main(void)
{
    enum { SLOT = 0xB200, SAVE = 0xE000 };
    g_cpu.A[0] = 0xFF0000 | SLOT; g_cpu.A[1] = 0xFF0000 | SAVE;
    g_ram[0xF600] = 0x4C; g_ram[SAVE] = 0x80;
    uint8_t original[65536]; memcpy(original, g_ram, sizeof original);
    s3_knuckles_menu_hook(ARMY_PC_SAVE_SLOT, 0);
    s3_knuckles_menu_hook(ARMY_PC_SAVE_INIT, 0);
    CHECK(!memcmp(original, g_ram, sizeof original));
    s3_knuckles_menu_hook(ARMY_PC_SAVE_SLOT, 1);
    CHECK(g_ram[SLOT+0x34] == 0 && g_ram[SLOT+0x35] == 3);
    CHECK(!memcmp(original+SAVE, g_ram+SAVE, 10));
    s3_knuckles_menu_hook(ARMY_PC_SAVE_INIT, 1);
    CHECK(g_ram[0xEF4C] == 0 && g_ram[0xEF4D] == 3);
    /* Valid saved characters (including Sonic & Tails) are untouched. */
    for (int character = 0; character < 4; ++character) {
        g_ram[SAVE] = 0; g_ram[SAVE+2] = (uint8_t)(character << 4);
        g_ram[SLOT+0x35] = (uint8_t)character;
        memcpy(original, g_ram, sizeof original);
        s3_knuckles_menu_hook(ARMY_PC_SAVE_SLOT, 1);
        CHECK(!memcmp(original, g_ram, sizeof original));
    }
    g_ram[0xF600] = 0xC; g_ram[SAVE] = 0x80;
    memcpy(original, g_ram, sizeof original);
    s3_knuckles_menu_hook(ARMY_PC_SAVE_SLOT, 1);
    CHECK(!memcmp(original, g_ram, sizeof original));
    puts("Knuckles defaults affect only empty menu selections");
    return 0;
}
