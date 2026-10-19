/*
 * trilogy_save.c — save files across the whole trilogy.
 *
 * A save slot (S3K's 10-byte format, Saved_data) keeps its level code in
 * byte 3. The trilogy numbers every zone in campaign order: Sonic 1's seven
 * (0-6), Sonic 2's eleven (7-17), then Sonic 3 & Knuckles' fourteen as S3K
 * numbers them, offset by 18 (18-31); 32 is a finished game. S3K's own
 * SaveGame is redone here with that offset; Sonic 1 / Sonic 2 zones save when
 * a zone is cleared (trilogy_flow.c), and the Data Select reads the codes
 * back through its hooks: the zones a cleared file may pick, the level a code
 * starts, the live thumbnail of the zone (trilogy_thumbs.c). A new game
 * starts at Green Hill.
 */
#include "trilogy_obj.h"
#include <stdio.h>
#include <stdlib.h>

#define RAM_Save_pointer                0xFFE660u
#define RAM_Saved_data                  0xFFE6ACu
#define RAM_SK_alone_flag               0xFFFFAEu
#define RAM_Continue_count              0xFFFE18u
#define RAM_SRAM_mask_interrupts_flag   0xFFEF56u

#define CODE_S2         7
#define CODE_S3K        18
#define CODE_CLEAR      32

static const uint16_t k_s1_zones[7] = { 0x2000, 0x2200, 0x2400, 0x2100, 0x2300, 0x2500, 0x2502 };   /* GHZ MZ SYZ LZ SLZ SBZ FZ */
static const uint16_t k_s3k_zones[14] = { 0x000, 0x100, 0x200, 0x300, 0x500, 0x600, 0x700, 0x400, 0x800, 0x900,
                                          0x1601, 0xA00, 0xB00, 0xC00 };                         /* LevelList_DA6E */
/* SaveGame_NextLevel (zone*2 + act) */
static const uint8_t k_next_level[24][2] = {
    {1,1},{2,2},{3,3},{4,4},{8,8},{5,5},{6,6},{7,7},{9,9},{0xA,0xA},{0xC,0xC},{0xD,0xD},{0xE,0xE},
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0xA,0xB},{0xD,0} };

uint16_t tri_code_to_za(int code)
{
    if (code < 0) return 0x2000;
    if (code < CODE_S2) return k_s1_zones[code];
    if (code < CODE_S3K) return (uint16_t)((0x28 + code - CODE_S2) << 8);
    if (code < CODE_CLEAR) return k_s3k_zones[code - CODE_S3K];
    return 0x2000;
}

/* the code of the zone an (internal) zone/act belongs to */
int tri_za_to_code(uint16_t za)
{
    extern uint16_t tri_apparent(uint16_t za);
    uint16_t ap = tri_apparent(za);
    int z = ap >> 8;
    static const int8_t s1[7] = { 0, 3, 1, 4, 2, 5, 6 };                /* $20 GHZ, $21 LZ, $22 MZ ... $26 FZ */
    if (z >= 0x20 && z <= 0x26) return s1[z - 0x20];
    if (z >= 0x28 && z <= 0x32) return CODE_S2 + z - 0x28;
    for (int i = 0; i < 14; i++) if ((k_s3k_zones[i] >> 8) == z) return CODE_S3K + i;
    return 0;
}

static uint32_t slot(void) { return r32(RAM_Save_pointer) & 0xFFFFFFu; }

static void write_save(void)
{
    w8(RAM_SRAM_mask_interrupts_flag, 0xFF);
    tri_jsr(S3K_Write_SaveGame);
}

static void save_lives(uint32_t a1)                 /* loc_C4B4 */
{
    uint8_t d0 = r8(RAM_Life_count);
    if (d0 > 99) d0 = 99;
    w8(a1 + 8, d0);
    w8(RAM_Life_count, d0);
    write_save();
}

/* emeralds + continues (SaveGame_SpecialStage), after a stage the trilogy ran itself */
void tri_save_emeralds(void)
{
    if (r16(RAM_SK_alone_flag)) return;
    uint32_t a1 = slot();
    if (!a1) return;
    uint16_t d0 = 0;
    for (int i = 0; i < 7; i++) {
        d0 |= r8(RAM_Collected_emeralds_array + (uint32_t)i) & 3;
        d0 = (uint16_t)(d0 << 2);
    }
    w16(a1 + 6, d0);
    uint8_t c = r8(RAM_Continue_count);
    if (c > 99) c = 99;
    w8(a1 + 9, c);
    write_save();
}

/* a Sonic 1 / Sonic 2 zone cleared: the file resumes at next_za's zone */
void tri_save_progress(uint16_t next_za)
{
    if (r16(RAM_SK_alone_flag)) return;
    uint32_t a1 = slot();
    if (!a1) return;
    int code = tri_za_to_code(next_za);
    if (!(r8(a1) & 3) || code >= r8(a1 + 3)) {     /* (a finished file replaying an earlier zone stays finished) */
        w8(a1, (uint8_t)(r8(a1) & 0xFC));
        w8(a1 + 3, (uint8_t)code);
    }
    w16(a1 + 4, 0);
    save_lives(a1);
    fprintf(stderr, "[trilogy] saved: level code %d ($%04X)\n", code, next_za);
}

/* SaveGame for Sonic 3 & Knuckles' zones, codes offset by 18 */
static void save_game_s3k(void)
{
    uint32_t a1 = slot();
    if (r16(RAM_SK_alone_flag) || !a1) goto done;
    {
        uint16_t za = r16(RAM_Current_zone_and_act);
        int z = za >> 8;
        int d0 = CODE_S3K + (z < 24 ? k_next_level[z][za & 1] : 0);
        if ((r8(a1) & 3) && d0 < r8(a1 + 3)) { save_lives(a1); goto done; }
        w8(a1, (uint8_t)(r8(a1) & 0xFC));
        w8(a1 + 3, (uint8_t)d0);
        uint16_t pm = r16(RAM_Player_mode);
        int completed;
        if (pm == 3) completed = d0 >= CODE_S3K + 0xC;
        else if (pm == 2) completed = d0 >= CODE_S3K + 0xD;
        else completed = d0 > CODE_S3K + 0xD || (d0 == CODE_S3K + 0xD && r8(RAM_Chaos_emerald_count) < 7);
        if (completed) {
            uint8_t c = 1;
            if (r8(RAM_Chaos_emerald_count) >= 7) { c++; if (r8(RAM_Super_emerald_count) >= 7) c++; }
            w8(a1, c);
        }
        w16(a1 + 4, 0);
        save_lives(a1);
    }
done:
    w32(RAM_Collected_special_ring_array, 0);
}

void tri_thumb_show(int code);

int tri_save_hook(uint32_t pc)
{
    switch (pc) {
    case TH_SAVEGAME:
        if (!tri_dir()) save_game_s3k();
        return 1;                                    /* (S1/S2 zones save on their own zone clears) */
    case TH_DS_NOSAVE_START:
    case TH_DS_NEWGAME:                              /* a new game starts with Sonic 1 */
        w16(RAM_Current_zone_and_act, 0x2000);
        w16(RAM_Apparent_zone_and_act, 0x2000);
        return 0;
    case TH_DS_ZONE_MAX:                             /* a finished file picks from every zone before */
        D(6) = (D(6) & 0xFFFF0000u) | (uint16_t)(D(6) + CODE_S3K);
        return 0;
    case TH_DS_CODE_TO_ZONE: {
        int code = D(0) & 0xFF;
        if (code < CODE_S3K) {
            D(0) = (D(0) & 0xFFFF0000u) | tri_code_to_za(code);
            return 1;
        }
        D(0) = (D(0) & 0xFFFFFF00u) | (uint8_t)(code - CODE_S3K);
        return 0;
    }
    case TH_DS_ZONE_TENS: {                          /* "ZONE n": the trilogy's own numbering, 1-32 */
        int n = (int)((D(0) & 0xFFFF) >> 1) + 1;
        if (n / 10) { D(1) = (D(1) & 0xFFFFFF00u) | (uint8_t)(n / 10); g_cpu.SR &= ~8u; }
        else { D(1) = (D(1) & 0xFFFFFF00u) | 0xFF; g_cpu.SR |= 8u; }   /* (blank: the bpl reads N) */
        return 0;
    }
    case TH_DS_ZONE_UNITS:
        D(1) = (D(1) & 0xFFFFFF00u) | (uint8_t)(((D(0) & 0xFFFF) / 2 + 1) % 10);
        return 0;
    case TH_RENDER_HUD: {                            /* (no HUD while the zone cards are recorded) */
        static int glide = -1;
        if (glide < 0) glide = getenv("TRILOGY_GLIDE") != NULL;
        if (glide) w8(RAM_Player_1 + 0x34, 0x18);    /* (and no player: flickered out, unhurt - in every zone) */
        return glide;
    }
    case TH_DS_ZONE_ICON:
        tri_thumb_show(D(1) & 0xFF);
        return 1;
    default:
        return 0;
    }
}

/* development: TRILOGY_CLEAR_SLOT=n (1-8) makes save file n a finished
 * trilogy (Sonic 1 through Sonic 3 & Knuckles, every emerald super) */
void tri_save_dev_clear(void)
{
    static int done;
    const char *e = getenv("TRILOGY_CLEAR_SLOT");
    if (done || !e || !*e) return;
    done = 1;
    int n = atoi(e);
    if (n < 1 || n > 8) return;
    uint32_t a1 = RAM_Saved_data + (uint32_t)(n - 1) * 10u;
    w8(a1, 3);                                       /* finished, all Super Emeralds */
    w8(a1 + 1, 0);
    w8(a1 + 2, 0x00);                                /* Sonic & Tails */
    w8(a1 + 3, CODE_CLEAR);
    w16(a1 + 4, 0);
    w16(a1 + 6, 0xFFFC);                             /* seven Super Emeralds */
    w8(a1 + 8, 9);
    w8(a1 + 9, 3);
    write_save();
    fprintf(stderr, "[trilogy] save file %d marked finished\n", n);
}
