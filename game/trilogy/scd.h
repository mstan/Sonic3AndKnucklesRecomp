/*
 * scd.h — Sonic CD, from the player's own disc (BIN/CUE).
 *
 * Data and music are independently selected converted private resources.
 * Original BIN/CUE input is used by tools/build_sonic_cd_pack.py only.
 */
#pragma once
#include <stdint.h>

int  scd_disc_open(void);                 /* 1 when a Sonic CD disc is available */
void scd_disc_close(void);
void scd_audio_close(void);
/* a file from the data track (e.g. "SNCBNK1B.BIN"), cached; NULL if absent */
const uint8_t *scd_disc_file(const char *name, uint32_t *size);

/* ---- music and voices (scd_audio.c) ---- */
void scd_audio_init(void);                /* registers the mix source (idempotent) */
void scd_cd_play(int track, int loop);   /* CD-DA track (2..35), whole-track repeat */
void scd_cd_stop(void);
void scd_cd_fade(void);                   /* fade out, then stop */
void scd_cd_pause(int paused);
/* Native level pause, separate from the UFO scene's own CD-only pause. */
void scd_audio_level_pause(int paused);
int  scd_cd_track(void);                  /* playing track, 0 none */
/* SMPS-PCM sound bank (the sub CPU's PRG-RAM $40000 file): SNCBNK1B.BIN
 * (Palmtree Panic) ... SNCBNKB1.BIN (boss), SNCBNKB2.BIN (final boss). */
int  scd_pcm_bank(const char *file);
void scd_pcm_cmd(uint8_t id);             /* $81 past song, $B1.. voices, $E0.. commands */
void scd_audio_reset(void);               /* silence everything (title, level exit) */

/* PCM sound ids (Sound Drivers/PCM/_Sound IDs.i) */
enum {
    SCD_PCM_PAST = 0x81,
    SCD_PCM_FUTURE = 0xB1, SCD_PCM_PASTV = 0xB2, SCD_PCM_ALRIGHT = 0xB3, SCD_PCM_GIVEUP = 0xB4,
    SCD_PCM_YES = 0xB5, SCD_PCM_YEAH = 0xB6, SCD_PCM_GIGGLE = 0xB7, SCD_PCM_AMYYELP = 0xB8,
    SCD_PCM_STOMP = 0xB9, SCD_PCM_BUMPER = 0xBA, SCD_PCM_BREAK = 0xBB,
    SCD_PCM_FADEOUT = 0xE0, SCD_PCM_STOP = 0xE1, SCD_PCM_PAUSE = 0xE2, SCD_PCM_UNPAUSE = 0xE3, SCD_PCM_MUTE = 0xE4,
};
/* CD-DA tracks (track number = CDDA id, _Include/Sound.i) */
enum {
    SCD_CD_WARP = 2, SCD_CD_R1A, SCD_CD_R1C, SCD_CD_R1D, SCD_CD_R3A, SCD_CD_R3C, SCD_CD_R3D,
    SCD_CD_R4A, SCD_CD_R4C, SCD_CD_R4D, SCD_CD_R5A, SCD_CD_R5C, SCD_CD_R5D, SCD_CD_R6A, SCD_CD_R6C, SCD_CD_R6D,
    SCD_CD_R7A, SCD_CD_R7C, SCD_CD_R7D, SCD_CD_R8A, SCD_CD_R8C, SCD_CD_R8D,
    SCD_CD_BOSS, SCD_CD_FINAL, SCD_CD_TITLE, SCD_CD_TMATK, SCD_CD_RESULTS, SCD_CD_SHOES, SCD_CD_INVINC,
    SCD_CD_GAMEOVER, SCD_CD_SPECIAL, SCD_CD_DAGARDEN, SCD_CD_INTRO, SCD_CD_ENDING,
};
