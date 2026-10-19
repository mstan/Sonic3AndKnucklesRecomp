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
 */
#define TRILOGY_INTERNAL 1
#include "trilogy.h"
#include "genesis_machine.h"
#include <string.h>
#include <stdio.h>

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

/* The Sonic 2 sound set (tools/trilogy/s2_sfx.py): while a Sonic 2 zone or
 * scene runs, the driver's three SFX bank switches (`ld a,$1F` then the bank
 * register at $6000) and its SFX pointer table (z80_SFXPointers, $33-$DF)
 * point at the S2 bank: S2's sounds for everything S2 has, S3K's for the
 * rest that can play there, silence otherwise. Anywhere else S3K's own.
 * Judged from the driver's RAM each time (it may have been reloaded). */
#define Z80_SFX_PTRS      0x167Eu   /* z80_SFXPointers, from sfx $33 */
#define SFX_COUNT         (0xDF - 0x33 + 1)
#define SND_BANK_BYTE     0x1F      /* zmake68kBank($F8000) */

static uint8_t s_sfx_saved[SFX_COUNT * 2];
static int s_sfx_on;

static uint32_t sound_set(void)
{
    uint32_t t = tri_rd32(TRI_BANK_BASE + TB_BLOBS);
    uint32_t b = t ? tri_rd32(t + TRI_BLOB_S2_SFX * 4u) : 0;
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

void tri_s2_sound_set(int on)
{
    uint32_t b = sound_set();
    if (!b) return;
    uint8_t bank = tri_rd8(b);
    uint8_t *tab = zram() + Z80_SFX_PTRS;
    if (on) {
        if (sfx_bank_sites(SND_BANK_BYTE, bank)) {   /* (S3K's set loaded: keep its table, load ours) */
            memcpy(s_sfx_saved, tab, sizeof s_sfx_saved);
            for (int k = 0; k < SFX_COUNT * 2; k++) tab[k] = tri_rd8(b + 1u + (uint32_t)k);
        }
        s_sfx_on = 1;
    } else {
        if (sfx_bank_sites(bank, SND_BANK_BYTE)) memcpy(tab, s_sfx_saved, sizeof s_sfx_saved);
        s_sfx_on = 0;
    }
}

/* the set for where the game is: Sonic 2's in Sonic 2 zones */
void tri_sound_set_for_zone(void)
{
    uint32_t e = tri_dir();
    tri_s2_sound_set(e && tri_rd8(e + TD_GAME) == TRI_GAME_S2);
}

/* every frame: S3K reloads its driver (level loads, title...), which brings back
 * S3K's bank and table; while the S2 set is wanted it goes straight back in */
void tri_sound_set_tick(void)
{
    if (s_sfx_on) tri_s2_sound_set(1);
}

int tri_s2_sound_set_on(void) { return s_sfx_on; }

/* a Sonic 2 sound (SndID_*) -> the id it plays at in the S2 set (0: none) */
int tri_s2_sfx_id(int sndid)
{
    uint32_t b = sound_set();
    if (!b || sndid < 0xA0 || sndid > 0xF0) return 0;
    return tri_rd8(b + 1u + SFX_COUNT * 2u + (uint32_t)(sndid - 0xA0));
}
