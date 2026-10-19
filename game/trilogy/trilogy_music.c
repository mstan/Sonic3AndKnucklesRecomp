/*
 * trilogy_music.c — Sonic 1 and Sonic 2 songs on the retail S3K sound driver.
 *
 * tools/trilogy/s1_music.py and s2_music.py convert the songs into
 * S&K-driver format in the expansion bank. Here, when a trilogy level wants
 * music, two competition-mode song slots of the driver ($20 zone theme, $21
 * boss theme — never played in 1P) are pointed at the converted songs, and
 * the game's PSG volume envelopes (S1 9, S2 13) are pointed into S3K
 * envelope slots that no S3K sound effect or trilogy-level jingle uses.
 * Any S3K level restores the driver's own entries.
 * Sound effects: the Sonic 2 and Sonic CD sound sets, swapped into the
 * driver while their game's zones and scenes run (below).
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "genesis_machine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define Z80_MUSIC_BANKS   0x0B65u   /* z80_MusicBanks: bits 15-22 of the 68k address */
#define Z80_MUSIC_PTRS    0x1618u   /* z80_MusicPointers: LE Z80 window addresses */
#define Z80_VOLENV_PTRS   0x1387u   /* z80_VolEnvPointers: 1-based */
#define ENV_MAX           13

/* 1-based S3K envelope ids for game envelopes 1..13 (s2_music.ENV_SLOTS) */
static const uint8_t k_env_slot[ENV_MAX] = { 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x12, 0x06, 0x07, 0x0B, 0x10 };

static int s_saved;
static uint8_t s_bank[2], s_ptr[2][2], s_env[ENV_MAX][2];

static uint8_t *zram(void) { return g_machine.bus.z80_ram; }

static int s_game_override;                 /* a Sonic 2 scene playing its own songs, whatever the zone */

/* the converted-song table of the current act's game */
static uint32_t music_table(int *last, int *game)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t e = tri_dir();
    if (!t || (!e && !s_game_override)) return 0;
    *game = s_game_override ? s_game_override : tri_rd8(e + TD_GAME);
    if (*game == TRI_GAME_S2) { *last = 0x9F; return tri_rd32(t + TRI_BLOB_S2_MUSIC * 4u); }
    if (*game == TRI_GAME_MEGAMIX) {
        *last=0x97;return tri_rd32(tri_rd32(TRI_BANK_BASE+0x24)+16);
    }
    *last = 0x93;
    return tri_rd32(t + TRI_BLOB_S1_MUSIC * 4u);
}

/* Sonic 2's DAC samples (tools/trilogy/s2_dac.py): while an S2 song is loaded,
 * DAC_Banks (one bank byte per sample, in the driver's code) sends ids
 * $B4-$C4 to the bank holding S2's samples; otherwise the driver's own. */
static uint8_t s_dac_saved[32];
static int s_dac_have;

static uint32_t dac_banks(void)               /* `and 7Fh / ld c,a / ld b,0 / ld hl,DAC_Banks` */
{
    uint8_t *r = zram();
    for (uint32_t a = 0; a + 8 <= 0x1300; a++)
        if (r[a] == 0xE6 && r[a + 1] == 0x7F && r[a + 2] == 0x4F && r[a + 3] == 0x06 && r[a + 4] == 0x00 && r[a + 5] == 0x21)
            return (uint32_t)(r[a + 6] | r[a + 7] << 8);
    return 0;
}

static void s2_dac(int on)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t b = t ? tri_rd32(t + TRI_BLOB_S2_DAC * 4u) : 0;
    if (!b || !tri_rd8(b)) return;
    uint32_t tab = dac_banks();
    if (!tab || tab >= 0x1F00) return;
    uint8_t first = tri_rd8(b + 1), n = tri_rd8(b + 2);
    if (n > sizeof s_dac_saved) n = sizeof s_dac_saved;
    uint8_t *r = zram() + tab + (first - 0x80u);
    uint8_t bk = tri_rd8(b);
    if (on) {                                     /* (judged from the table itself: the driver may have been reloaded) */
        if (r[0] != bk || r[n - 1] != bk) { memcpy(s_dac_saved, r, n); s_dac_have = 1; memset(r, bk, n); }
    } else if (s_dac_have && r[0] == bk) {
        memcpy(r, s_dac_saved, n);
    }
}

static void save_originals(void)
{
    if (s_saved) return;
    s_saved = 1;
    uint8_t *r = zram();
    for (int i = 0; i < 2; i++) {
        int idx = TRI_SLOT_ZONE - 1 + i;
        s_bank[i] = r[Z80_MUSIC_BANKS + idx];
        s_ptr[i][0] = r[Z80_MUSIC_PTRS + idx * 2];
        s_ptr[i][1] = r[Z80_MUSIC_PTRS + idx * 2 + 1];
    }
    for (int k = 0; k < ENV_MAX; k++) {
        s_env[k][0] = r[Z80_VOLENV_PTRS + (k_env_slot[k] - 1) * 2];
        s_env[k][1] = r[Z80_VOLENV_PTRS + (k_env_slot[k] - 1) * 2 + 1];
    }
}

/* Point song slot (TRI_SLOT_ZONE / TRI_SLOT_BOSS) at a song of the act's game. */
void tri_music_patch(int slot, int song)
{
    int last = 0, game = 0;
    uint32_t t = music_table(&last, &game);
    if (!t || song < 0x81 || song > last) return;
    s2_dac(game == TRI_GAME_S2);
    int nenv = tri_rd8(t);
    if (nenv > ENV_MAX) nenv = ENV_MAX;
    uint32_t envs = t + 2, songs = envs + (uint32_t)tri_rd8(t) * 2u;
    uint32_t e = songs + (uint32_t)(song - 0x81) * 4u;
    if (!tri_rd16(e + 2)) return;                   /* not converted */
    save_originals();
    uint8_t *r = zram();
    int idx = slot - 1;
    r[Z80_MUSIC_BANKS + idx] = tri_rd8(e);
    r[Z80_MUSIC_PTRS + idx * 2] = tri_rd8(e + 2);
    r[Z80_MUSIC_PTRS + idx * 2 + 1] = tri_rd8(e + 3);
    for (int k = 0; k < nenv; k++) {
        r[Z80_VOLENV_PTRS + (k_env_slot[k] - 1) * 2] = tri_rd8(envs + k * 2u);
        r[Z80_VOLENV_PTRS + (k_env_slot[k] - 1) * 2 + 1] = tri_rd8(envs + k * 2u + 1);
    }
}

void tri_music_restore(void)
{
    s2_dac(0);
    if (!s_saved) return;
    uint8_t *r = zram();
    for (int i = 0; i < 2; i++) {
        int idx = TRI_SLOT_ZONE - 1 + i;
        r[Z80_MUSIC_BANKS + idx] = s_bank[i];
        r[Z80_MUSIC_PTRS + idx * 2] = s_ptr[i][0];
        r[Z80_MUSIC_PTRS + idx * 2 + 1] = s_ptr[i][1];
    }
    for (int k = 0; k < ENV_MAX; k++) {
        r[Z80_VOLENV_PTRS + (k_env_slot[k] - 1) * 2] = s_env[k][0];
        r[Z80_VOLENV_PTRS + (k_env_slot[k] - 1) * 2 + 1] = s_env[k][1];
    }
}

/* Patch a slot and start it through the game's own Play_Music. */
void tri_music_play(int slot, int song)
{
    tri_music_patch(slot, song);
    D(0) = (uint32_t)slot;
    tri_jsr(S3K_Play_Music);
}

/* A Sonic 2 song wherever the game is (its special stage, results, ending). */
void tri_music_play_s2(int slot, int song)
{
    s_game_override = TRI_GAME_S2;
    tri_music_play(slot, song);
    s_game_override = 0;
}

/* The current act's own theme (TD_MUSIC holds the game's song id). */
void tri_music_zone(void)
{
    uint32_t e = tri_dir();
    if (e) tri_music_play(TRI_SLOT_ZONE, tri_rd8(e + TD_MUSIC));
}

/* Sound sets: Sonic 2's (tools/trilogy/s2_sfx.py, trilogy.bin) while a Sonic 2
 * zone or scene runs, Sonic CD's (tools/trilogy/scd_sfx.py, scd.bin: placed by
 * trilogy_core.c) while a Sonic CD level, its time warp or its special stage
 * does. The driver's three SFX bank switches (`ld a,$1F` then the bank register
 * at $6000) and its SFX pointer table (z80_SFXPointers, $33-$DF) point at that
 * game's bank: its sounds for everything it has, S3K's for the rest that can
 * play there, silence otherwise. Anywhere else, and in S3K's own special and
 * bonus stages and menus, S3K's own. Each blob: u8 bank byte, u16 LE pointer per
 * S3K id $33-$DF, then u8 S3K id per game sound id. Judged from the driver's
 * RAM each time (it may have been reloaded, bringing S3K's back). */
#define Z80_SFX_PTRS      0x167Eu   /* z80_SFXPointers, from sfx $33 */
#define SFX_COUNT         (0xDF - 0x33 + 1)
#define SND_BANK_BYTE     0x1F      /* zmake68kBank($F8000) */
enum { SET_S3K, SET_S2, SET_CD };

static uint8_t s_sfx_saved[SFX_COUNT * 2];   /* S3K's table */
static int s_set_now;                        /* the set the driver has */
static int s_set_want;                       /* the set the game wants (put back after driver reloads) */

static uint32_t set_blob(int set)
{
    uint32_t b = 0;
    if (set == SET_S2) {
        uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
        b = t ? tri_rd32(t + TRI_BLOB_S2_SFX * 4u) : 0;
    } else if (set == SET_CD)
        b = tri_scd_sound();
    return b && tri_rd8(b) ? b : 0;
}

static int sfx_bank_sites(uint8_t from, uint8_t to)
{
    uint8_t *r = zram();
    int n = 0;
    for (uint32_t a = 0; a + 5 <= 0x1300; a++)    /* (driver code) */
        if (r[a] == 0x3E && r[a + 1] == from && r[a + 2] == 0x21 && r[a + 3] == 0x00 && r[a + 4] == 0x60) {
            r[a + 1] = to;
            n++;
        }
    return n;
}

static void sound_set_log(int was)                /* (development: TRILOGY_SFXLOG) */
{
    static int log = -1;
    if (log < 0) log = getenv("TRILOGY_SFXLOG") != NULL;
    if (!log || was == s_set_now) return;
    static const char *const name[] = { "S3K", "S2", "CD" };
    const uint8_t *t = zram() + Z80_SFX_PTRS;
    fprintf(stderr, "[sfx] set %s -> %s (zone $%02X, mode $%02X; $62 at $%04X)\n", name[was], name[s_set_now],
            (unsigned)r8(RAM_Current_zone), (unsigned)r8(RAM_Game_mode), (unsigned)(t[(0x62 - 0x33) * 2] | t[(0x62 - 0x33) * 2 + 1] << 8));
}

static void sound_set_apply(int set)
{
    int was = s_set_now;
    uint32_t b = set_blob(set);
    if (!b) set = SET_S3K;                        /* (that game's set not built: S3K's) */
    uint8_t *tab = zram() + Z80_SFX_PTRS;
    if (s_set_now != SET_S3K) {
        uint32_t ob = set_blob(s_set_now);
        uint8_t obank = ob ? tri_rd8(ob) : SND_BANK_BYTE;
        if (set == s_set_now && sfx_bank_sites(obank, obank)) return;   /* (in, as wanted) */
        if (sfx_bank_sites(obank, SND_BANK_BYTE))  /* (still in: S3K's back; else a reload did it) */
            memcpy(tab, s_sfx_saved, sizeof s_sfx_saved);
        s_set_now = SET_S3K;
    }
    if (set != SET_S3K && sfx_bank_sites(SND_BANK_BYTE, tri_rd8(b))) {   /* (S3K's set loaded: keep its table, load ours) */
        memcpy(s_sfx_saved, tab, sizeof s_sfx_saved);
        for (int k = 0; k < SFX_COUNT * 2; k++) tab[k] = tri_rd8(b + 1u + (uint32_t)k);
        s_set_now = set;
    }
    sound_set_log(was);
}

static void sound_set_want(int set, int on)
{
    if (on) s_set_want = set;
    else if (s_set_want == set) s_set_want = SET_S3K;
    else return;
    sound_set_apply(s_set_want);
}

void tri_s2_sound_set(int on) { sound_set_want(SET_S2, on); }
void tri_scd_sound_set(int on) { sound_set_want(SET_CD, on); }

/* the set for where the game is: Sonic 2's in Sonic 2 zones, Sonic CD's in its levels */
void tri_sound_set_for_zone(void)
{
    uint32_t e = tri_dir();
    int g = e ? tri_rd8(e + TD_GAME) : TRI_GAME_S3K;
    s_set_want = g == TRI_GAME_S2 ? SET_S2 : g == TRI_GAME_CD ? SET_CD : SET_S3K;
    sound_set_apply(s_set_want);
}

/* every frame: S3K reloads its driver (level loads, title...), which brings back
 * S3K's bank and table; while another set is wanted it goes straight back in.
 * Only in a level ($08 demo, $0C) or one of the trilogy's scenes ($10): S3K's
 * special stage, bonus stages and menus play S3K's own sounds */
void tri_sound_set_tick(void)
{
    int mode = r8(RAM_Game_mode) & 0x7F;
    int set = mode == 0x08 || mode == 0x0C || mode == 0x10 ? s_set_want : SET_S3K;
    if (set != SET_S3K || s_set_now != SET_S3K) sound_set_apply(set);
}

int tri_sound_set_foreign(void) { return s_set_now != SET_S3K; }
int tri_scd_sound_set_on(void) { return s_set_now == SET_CD; }

/* a Sonic 2 sound (SndID_*) -> the id it plays at in the S2 set (0: none) */
int tri_s2_sfx_id(int sndid)
{
    uint32_t b = set_blob(SET_S2);
    if (!b || sndid < 0xA0 || sndid > 0xF0) return 0;
    return tri_rd8(b + 1u + SFX_COUNT * 2u + (uint32_t)(sndid - 0xA0));
}

/* a Sonic CD FM sound ($90-$DF) -> the id it plays at in the CD set (0: none) */
int tri_scd_sfx_id(int fm)
{
    uint32_t b = set_blob(SET_CD);
    if (!b || fm < 0x90 || fm > 0xDF) return 0;
    return tri_rd8(b + 1u + SFX_COUNT * 2u + (uint32_t)(fm - 0x90));
}
