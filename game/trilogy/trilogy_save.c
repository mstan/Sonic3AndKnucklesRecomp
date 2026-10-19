/*
 * trilogy_save.c — save files across the whole trilogy.
 *
 * A save slot (S3K's 10-byte format, Saved_data) keeps its level code in
 * byte 3. The journal gives every zone a stable identity: Sonic 1's seven
 * (0-6), Sonic 2's eleven (7-17), then Sonic 3 & Knuckles' fourteen as S3K
 * numbers them, offset by 18 (18-31), and Megamix's seven (32-38).
 * Data Select uses a dense catalog containing only available campaigns.
 * trilogy_save_journal.c translates its RAM view at the native IO boundary;
 * native SRAM keeps native IDs. S3K's own
 * SaveGame is redone here with that offset; Sonic 1 / Sonic 2 zones save when
 * a zone is cleared (trilogy_flow.c), and the Data Select reads the codes
 * back through its hooks: the zones a cleared file may pick, the level a code
 * starts, the live thumbnail of the zone (trilogy_thumbs.c). A new game
 * starts with the first enabled prequel, or native Angel Island.
 */
#include "trilogy_obj.h"
#include "stage_pack.h"
#include "campaign_save.h"
#include <stdio.h>
#include <stdlib.h>

#define RAM_Save_pointer                0xFFE660u
#define RAM_Saved_data                  0xFFE6ACu
#define RAM_SK_alone_flag               0xFFFFAEu
#define RAM_Continue_count              0xFFFE18u
#define RAM_SRAM_mask_interrupts_flag   0xFFEF56u

#define CODE_S2         7
#define CODE_S3K        18
#define CODE_MM         32
#define CODE_CLEAR      campaign_count(tri_campaign_mask())
static int mm_after_credits;
static const uint16_t k_mm_zones[7]={0x3400,0x3600,0x3800,0x3500,0x3700,0x3900,0x3902};

static const uint16_t k_s1_zones[7] = { 0x2000, 0x2200, 0x2400, 0x2100, 0x2300, 0x2500, 0x2502 };   /* GHZ MZ SYZ LZ SLZ SBZ FZ */
static const uint16_t k_s3k_zones[14] = { 0x000, 0x100, 0x200, 0x300, 0x500, 0x600, 0x700, 0x400, 0x800, 0x900,
                                          0x1601, 0xA00, 0xB00, 0xC00 };                         /* LevelList_DA6E */
/* SaveGame_NextLevel (zone*2 + act) */
static const uint8_t k_next_level[24][2] = {
    {1,1},{2,2},{3,3},{4,4},{8,8},{5,5},{6,6},{7,7},{9,9},{0xA,0xA},{0xC,0xC},{0xD,0xD},{0xE,0xE},
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0xA,0xB},{0xD,0} };

uint16_t tri_code_to_za(int code)
{
    code=campaign_stable(tri_campaign_mask(),code);
    if (code < 0) return 0;
    if (code < CODE_S2) return k_s1_zones[code];
    if (code < CODE_S3K) return (uint16_t)((0x28 + code - CODE_S2) << 8);
    if (code < CODE_MM) return k_s3k_zones[code - CODE_S3K];
    if(s3_stage_pack_megamix && code<39)return k_mm_zones[code-CODE_MM];
    return 0;
}

/* the code of the zone an (internal) zone/act belongs to */
int tri_za_to_code(uint16_t za)
{
    extern uint16_t tri_apparent(uint16_t za);
    uint16_t ap = tri_apparent(za);
    int z = ap >> 8;
    static const int8_t s1[7] = { 0, 3, 1, 4, 2, 5, 6 };                /* $20 GHZ, $21 LZ, $22 MZ ... $26 FZ */
    if (z >= 0x20 && z <= 0x26) return campaign_display(tri_campaign_mask(),s1[z - 0x20]);
    if (z >= 0x28 && z <= 0x32) return campaign_display(tri_campaign_mask(),CODE_S2 + z - 0x28);
    if(s3_stage_pack_megamix && z>=0x34 && z<=0x3A) {
        static const uint8_t order[7]={0,3,1,4,2,5,6};
        return campaign_display(tri_campaign_mask(),CODE_MM+order[z-0x34]);
    }
    for (int i = 0; i < 14; i++) if ((k_s3k_zones[i] >> 8) == z) return campaign_display(tri_campaign_mask(),CODE_S3K + i);
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
    {
        uint16_t za = r16(RAM_Current_zone_and_act);
        int z = za >> 8;
        int d0 = CODE_S3K + (z < 24 ? k_next_level[z][za & 1] : 0);
        uint16_t pm = r16(RAM_Player_mode);
        int completed;
        if (pm == 3) completed = d0 >= CODE_S3K + 0xC;
        else if (pm == 2) completed = d0 >= CODE_S3K + 0xD;
        else completed = d0 > CODE_S3K + 0xD || (d0 == CODE_S3K + 0xD && r8(RAM_Chaos_emerald_count) < 7);
        if(completed && s3_stage_pack_megamix) {mm_after_credits=1;d0=CODE_MM;completed=0;}
        d0=completed?CODE_CLEAR:campaign_display(tri_campaign_mask(),d0);
        if(d0<0)d0=tri_za_to_code(0);
        if(r16(RAM_SK_alone_flag)||!a1)goto done;
        if ((r8(a1) & 3) && d0 < r8(a1 + 3)) { save_lives(a1); goto done; }
        w8(a1, (uint8_t)(r8(a1) & 0xFC));
        w8(a1 + 3, (uint8_t)d0);
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

void tri_save_megamix_complete(void)
{
    uint32_t a1=slot();
    if(!a1 || r16(RAM_SK_alone_flag))return;
    uint8_t c=1;
    if(r8(RAM_Chaos_emerald_count)>=7){c++;if(r8(RAM_Super_emerald_count)>=7)c++;}
    w8(a1,c);w8(a1+3,(uint8_t)CODE_CLEAR);w16(a1+4,0);save_lives(a1);
}

void tri_thumb_show(int code);

int tri_save_hook(uint32_t pc)
{
    switch (pc) {
    case TH_GAMELOOP:
        if(mm_after_credits && s3_stage_pack_megamix && r8(RAM_Game_mode)==4) {
            mm_after_credits=0;
            w16(RAM_Current_zone_and_act,0x3400);w16(RAM_Apparent_zone_and_act,0x3400);
            w8(RAM_Last_star_post_hit,0);w16(RAM_Restart_level_flag,0);
            w8(RAM_Game_mode,0x0C);A(0)=TH_LEVEL_ENTRY;
            fprintf(stderr,"[megamix] Sonic 3 & Knuckles ending complete -> Sunny Shores\n");
        }
        return 0;
    case TH_SAVEGAME:
        if (!tri_dir()) save_game_s3k();
        return 1;                                    /* (S1/S2 zones save on their own zone clears) */
    case TH_DS_NOSAVE_START:
    case TH_DS_NEWGAME:                              /* a new game starts with Sonic 1 */
        w16(RAM_Current_zone_and_act, tri_code_to_za(0));
        w16(RAM_Apparent_zone_and_act, tri_code_to_za(0));
        return 0;
    case TH_DS_ZONE_MAX:                             /* a finished file picks from every zone before */
        D(6) = (D(6) & 0xFFFF0000u) | (uint16_t)(CODE_CLEAR-1);
        return 0;
    case TH_DS_CODE_TO_ZONE: {
        int code = D(0) & 0xFF;
        D(0) = (D(0) & 0xFFFF0000u) | tri_code_to_za(code);
        return 1;
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
    const char *ch=getenv("TRILOGY_CLEAR_CHAR");
    unsigned character=ch?(unsigned)atoi(ch):0;
    if(character>5)character=0;
    w8(a1 + 2, (uint8_t)(character<<4));             /* independent saved selection */
    w8(a1 + 3, CODE_CLEAR);
    w16(a1 + 4, 0);
    w16(a1 + 6, 0xFFFC);                             /* seven Super Emeralds */
    w8(a1 + 8, 9);
    w8(a1 + 9, 3);
    write_save();
    fprintf(stderr, "[trilogy] save file %d marked finished\n", n);
}
