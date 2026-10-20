/*
 * scd_audio.c — Sonic CD's music and voices, from the player's own disc.
 *
 * Present and future themes (and the boss, invincibility, results...) are
 * the disc's CD-DA tracks, streamed from the BINs its CUE sheet names.
 * The past themes and the voice clips ("Future!", "Past!", Amy) belong to
 * the SMPS-PCM driver the sub CPU runs on the Sega CD's Ricoh RF5C164. The
 * driver is transcribed here routine by routine from the Sonic CD
 * disassembly (Sound Drivers/PCM/_Driver.asm, names kept), quirks included,
 * over a copy of its variables laid out byte for byte as the original's:
 * its call stack and repeat counters share track memory, and the staccato
 * bug still ends a driver tick early. Songs and samples are read from the
 * disc's SNCBNKxx.BIN, where the sub CPU loads it (PRG-RAM $40000).
 *
 * Timing as on the hardware: the RF5C164 makes one sample per 384 clocks
 * of 12.5 MHz (32552 Hz); the level-3 timer (GAIRQ3TIME = 255, ticking at
 * that rate) fires every 256 samples and the driver runs on every other
 * one. Both streams mix into the runner's audio via audio_set_extra_source,
 * paced by emulated frames.
 */
#include "scd.h"
#include "scd_internal.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCM_RATE   (12500000.0 / 384.0)
#define CD_RATE    44100.0
#define TICK_SAMPLES 512                    /* two timer interrupts of 256 */
/* Loudness against the Mega Drive's own music (both CD sources are mixed
 * after the console's audio on a Sega CD): each CD-DA track is brought to the
 * level the S3K, Sonic 1 and Sonic 2 levels' music has in this mix (BS.1770,
 * measured by scratch/loudness.py: -24.2 LUFS on average); the PCM chip as a
 * whole, so its voices keep their place against its music. */
#define TARGET_LUFS (-24.2)
#define CD_GAIN    0.55f                    /* (a track not yet measured) */
#define PCM_GAIN   0.27f

/* ------------------------------------------------------------------------
 * RF5C164
 * ---------------------------------------------------------------------- */
typedef struct { uint8_t env, pan, fdl, fdh, lsl, lsh, st, on; uint32_t addr; } RfChan;
static struct {
    RfChan ch[8];
    uint8_t wave[0x10000];
    uint8_t cur, bank, enable;
} s_rf;

static void rf_write(unsigned reg, uint8_t v)
{
    RfChan *c = &s_rf.ch[s_rf.cur];
    switch (reg) {
    case 0: c->env = v; break;
    case 1: c->pan = v; break;
    case 2: c->fdl = v; break;
    case 3: c->fdh = v; break;
    case 4: c->lsl = v; break;
    case 5: c->lsh = v; break;
    case 6:
        c->st = v;
        if (!c->on) c->addr = (uint32_t)v << 19;
        break;
    case 7:                                         /* CTRL: ON, MOD (channel / wave bank) */
        if (v & 0x40) s_rf.cur = v & 7; else s_rf.bank = v & 15;
        s_rf.enable = v >> 7;
        break;
    case 8:                                         /* ON/OFF: a held channel rests at its start */
        for (int i = 0; i < 8; i++) {
            s_rf.ch[i].on = !((v >> i) & 1);
            if (!s_rf.ch[i].on) s_rf.ch[i].addr = (uint32_t)s_rf.ch[i].st << 19;
        }
        break;
    }
}
static uint16_t rf_position(int ch) { return (uint16_t)(s_rf.ch[ch & 7].addr >> 11); }

static void rf_sample(int32_t *left, int32_t *right)
{
    int32_t l = 0, r = 0;
    if (s_rf.enable)
        for (int i = 0; i < 8; i++) {
            RfChan *c = &s_rf.ch[i];
            if (!c->on) continue;
            int lv = (c->pan & 15) * c->env, rv = (c->pan >> 4) * c->env;
            uint8_t s = s_rf.wave[(c->addr >> 11) & 0xFFFF];
            if (s == 0xFF) {                        /* loop marker */
                c->addr = (uint32_t)((c->lsh << 8) | c->lsl) << 11;
                s = s_rf.wave[(c->addr >> 11) & 0xFFFF];
                if (s == 0xFF) continue;
            }
            else c->addr += (uint32_t)((c->fdh << 8) | c->fdl);
            if (s & 0x80) { s &= 0x7F; l += (s * lv) >> 5; r += (s * rv) >> 5; }
            else { l -= (s * lv) >> 5; r -= (s * rv) >> 5; }
        }
    l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
    r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
    /* The Mega-CD PCM manual, section 4-8, specifies a 16-bit summed
     * output. Dropping six bits here incorrectly adds 10-bit quantization. */
    *left = l;
    *right = r;
}

/* ------------------------------------------------------------------------
 * SMPS-PCM (Sound Drivers/PCM/_Driver.asm)
 * ---------------------------------------------------------------------- */
/* the sub CPU's view: the bank file at PRG-RAM $40000, the chip's registers
 * and its 4 KB wave window at $FF2001 (odd bytes) */
#define PCMDRIVER  0x40000u
#define PCMWAVE    0xFF2001u
static const uint8_t *s_bank;
static uint32_t s_bank_size;
static int s_boss;
static uint32_t s_info, s_freq, s_samples, s_chanids;  /* DriverInfo, FreqTable, SampleIndex, ChannelIDs */

static uint8_t rd8(uint32_t a)
{
    a -= PCMDRIVER;
    return s_bank && a < s_bank_size ? s_bank[a] : 0;
}
static uint16_t rd16(uint32_t a) { return (uint16_t)(rd8(a) << 8 | rd8(a + 1)); }
static uint32_t rd32(uint32_t a) { return (uint32_t)rd16(a) << 16 | rd16(a + 2); }
static void wave_write(uint32_t a, uint8_t v)
{
    uint32_t k = (a - PCMWAVE) / 2;
    if (a >= PCMWAVE && !((a - PCMWAVE) & 1) && k < 0x1000) s_rf.wave[(uint32_t)s_rf.bank << 12 | k] = v;
}

/* driver variables, as _Variables.i lays them out */
enum { pdrvTempo, pdrvTempoCnt, pdrvOn, pdrvSFXPrio, pdrvCommFlag, pdrvCDDALoop, pdrvUnkCounter,
       pdrvSndPlay = 9, pdrvQueue = 10, pdrvSFXMode = 14, pdrvPauseMode = 15, pdrvPtrOffset = 16,
       pdrvFadeSteps = 20, pdrvFadeSpeed, pdrvFadeDelay, pdrvFadeCnt, pdrvUnkFadeVol,
       pdrvRhythm = 0x80, pdrvPCM1 = 0x100, pdrvPCMSFX1 = 0x500, pdrvSize = 0x900 };
enum { ptrkFlags, ptrkChannel, ptrkTickMult, ptrkPanning, ptrkDataPtr, ptrkTranspose = 8, ptrkVolume,
       ptrkStackPtr, ptrkDurCnt, ptrkDuration, ptrkStacCnt, ptrkStaccato, ptrkDetune, ptrkFreq,
       ptrkSampleBank = 18, ptrkSampleBlks, ptrkPrevSampPos, ptrkSampRAMOff = 22, ptrkSampleRAM = 24,
       ptrkSampleSize = 28, ptrkSampRemain = 32, ptrkSamplePtr = 36, ptrkSampleStart = 40,
       ptrkSampleLoop = 44, ptrkSampStac = 48, ptrkSampStacCnt, ptrkSampleMode,
       ptrkRepeatCnts = 0x40, ptrkStackBase = 0x80, ptrkSize = 0x80 };
enum { PTRK_REST = 1, PTRK_LEGATO = 4, PTRK_PLAY = 7 };
enum { PCMM_START = 0x81, PCMM_END = 0x8F, PCMS_START = 0xB0, PCMC_START = 0xE0, PCMC_END = 0xE4 };
static uint8_t V[pdrvSize];

static uint16_t vw(int a) { return (uint16_t)(V[a] << 8 | V[a + 1]); }
static uint32_t vl(int a) { return (uint32_t)vw(a) << 16 | vw(a + 2); }
static void setw(int a, uint16_t v) { V[a] = (uint8_t)(v >> 8); V[a + 1] = (uint8_t)v; }
static void setl(int a, uint32_t v) { setw(a, (uint16_t)(v >> 16)); setw(a + 2, (uint16_t)v); }
#define T(a3, f) V[(a3) + (f)]

static void rest_track(int a3)
{
    V[pdrvOn] |= (uint8_t)(1u << (T(a3, ptrkChannel) & 7));
    T(a3, ptrkFlags) |= 1 << PTRK_REST;
}
static void note_off(int a3)
{
    V[pdrvOn] |= (uint8_t)(1u << (T(a3, ptrkChannel) & 7));
    rf_write(8, V[pdrvOn]);
}
static void note_on(int a3)
{
    if (!(T(a3, ptrkFlags) & 1 << PTRK_LEGATO)) V[pdrvOn] &= (uint8_t)~(1u << (T(a3, ptrkChannel) & 7));
}
static void select_channel(int a3) { rf_write(7, (uint8_t)(T(a3, ptrkChannel) | 0xC0)); }

static void reset_driver(void)
{
    rf_write(8, 0xFF);
    uint32_t off = vl(pdrvPtrOffset);
    memset(V, 0, sizeof V);
    setl(pdrvPtrOffset, off);
    V[pdrvOn] = 0xFF;
    V[pdrvSndPlay] = 0x80;
}
static void clear_samples(void)
{
    for (int b = 0; b < 16; b++) {
        rf_write(7, (uint8_t)(0x80 | b));
        for (uint32_t i = 0; i < 0x1000; i++) wave_write(PCMWAVE + i * 2, 0xFF);
    }
}
static void load_samples(void)
{
    uint32_t a0 = s_samples, off = vl(pdrvPtrOffset);
    int32_t n = (int32_t)rd32(a0);
    a0 += 4;
    if (n <= 0) return;
    for (uint16_t d0 = (uint16_t)(n - 1);; d0--) {
        uint32_t a1 = rd32(a0) + off;
        a0 += 4;
        if (rd8(a1 + 13)) {                             /* psmpMode: static, loaded once */
            uint32_t a2 = rd32(a1) + off;
            uint16_t dest = rd16(a1 + 14);
            uint8_t d1 = (uint8_t)(((uint16_t)(dest << 4 | dest >> 12)) | 0x80);
            uint16_t d5 = dest & 0xF00, d2 = (uint16_t)rd32(a1 + 4);
            uint16_t d3 = (uint16_t)(d2 << 4 | d2 >> 12) & 15;
            for (;;) {
                rf_write(7, d1);
                uint16_t d4 = d2 > 0x1000 ? 0x1000 : d2;
                d2 = (uint16_t)(d2 + d5);
                d4 = (uint16_t)(d4 - d5 - 1);
                uint32_t a3 = PCMWAVE + (uint32_t)d5 * 2;
                for (uint16_t k = d4;; k--) {
                    wave_write(a3, rd8(a2++));
                    a3 += 2;
                    if (k == 0) break;
                }
                d2 = (uint16_t)(d2 - 0x1000);
                d1++;
                d5 = 0;
                if (d3-- == 0) break;
            }
        }
        if (d0 == 0) break;
    }
}
static void cmd_stop(void) { reset_driver(); clear_samples(); load_samples(); }
static void init_driver(void)
{
    rf_write(8, 0xFF);
    rf_write(7, 0x80);
    setl(pdrvPtrOffset, 0);                             /* loaded where it was assembled */
    cmd_stop();
}

static void stream_sample(int a3)
{
    uint32_t d6 = vl(a3 + ptrkSampRemain), a2 = vl(a3 + ptrkSamplePtr), a0 = vl(a3 + ptrkSampleRAM);
    uint32_t size = vl(a3 + ptrkSampleSize), loop = vl(a3 + ptrkSampleLoop);
    /* Undefined bytecode (including the disc's unused $FE sound) can leave
     * an empty/invalid stream. The original runs into data; never let that
     * turn the host's refill into an unbounded zero-length copy loop. */
    if (!d6 || loop >= size || a2 < PCMDRIVER || a2 - PCMDRIVER >= s_bank_size ||
        d6 > s_bank_size - (a2 - PCMDRIVER)) {
        rest_track(a3);
        note_off(a3);
        return;
    }
    setw(a3 + ptrkSampRAMOff, (uint16_t)(vw(a3 + ptrkSampRAMOff) + 0x200));
    rf_write(7, (uint8_t)(((T(a3, ptrkChannel) << 1) + T(a3, ptrkSampleBank)) | 0x80));
    uint32_t d0 = 0x200, d1 = 0x200;
    for (;;) {
        if (d6 < d0) d0 = d6;
        d6 -= d0;
        d1 -= d0;
        for (uint16_t k = (uint16_t)(d0 - 1);; k--) {
            wave_write(a0, rd8(a2++));
            a0 += 2;
            if (k == 0) break;
        }
        if (!d1) break;
        d0 = vl(a3 + ptrkSampleSize) - vl(a3 + ptrkSampleLoop);   /* loop the sample */
        a2 -= d0;
        d6 += d0;
        d0 = d1;
    }
    if (!d6) {
        d0 = vl(a3 + ptrkSampleSize) - vl(a3 + ptrkSampleLoop);
        a2 -= d0;
        d6 = d0;
    }
    setl(a3 + ptrkSampRemain, d6);
    setl(a3 + ptrkSamplePtr, a2);
    if ((int8_t)--T(a3, ptrkSampleBlks) >= 0) {
        setl(a3 + ptrkSampleRAM, a0);
        return;
    }
    setl(a3 + ptrkSampleRAM, PCMWAVE);
    if (T(a3, ptrkSampleBank)) { T(a3, ptrkSampleBlks) = 7; T(a3, ptrkSampleBank) = 0; }
    else { T(a3, ptrkSampleBlks) = 6; T(a3, ptrkSampleBank) = 1; }
}
static void start_stream(int a3)
{
    if (T(a3, ptrkSampleMode) || (T(a3, ptrkFlags) & 1 << PTRK_REST)) return;
    stream_sample(a3);
}
static void update_sample(int a3)
{
    if (T(a3, ptrkSampStacCnt) && !--T(a3, ptrkSampStacCnt)) { rest_track(a3); return; }
    if (T(a3, ptrkSampleMode) || (T(a3, ptrkFlags) & 1 << PTRK_REST)) return;
    uint16_t d0 = rf_position(T(a3, ptrkChannel)), d1 = vw(a3 + ptrkPrevSampPos);
    setw(a3 + ptrkPrevSampPos, d0);
    if (d0 < d1) setw(a3 + ptrkSampRAMOff, (uint16_t)(vw(a3 + ptrkSampRAMOff) - 0x1E00));
    d0 = (uint16_t)((d0 & 0x1FFF) + 0x1000);
    if (d0 > vw(a3 + ptrkSampRAMOff)) stream_sample(a3);
}
/* 1: the track is resting (skip right to the next track) */
static int update_freq(int a3)
{
    if (T(a3, ptrkFlags) & 1 << PTRK_REST) return 1;
    uint16_t d5 = (uint16_t)(vw(a3 + ptrkFreq) + (int8_t)T(a3, ptrkDetune));
    select_channel(a3);
    rf_write(2, (uint8_t)d5);
    rf_write(3, (uint8_t)(d5 >> 8));
    return 0;
}
static void get_freq(int a3, uint8_t d5)
{
    d5 = (uint8_t)(d5 - 0x80);
    if (!d5) { rest_track(a3); return; }
    d5 = (uint8_t)((d5 + T(a3, ptrkTranspose)) & 0x7F);
    setw(a3 + ptrkFreq, rd16(s_freq + d5 * 2u));
}
static void calc_duration(int a3, uint8_t d5)
{
    uint8_t d0 = d5, d1 = T(a3, ptrkTickMult);
    while (--d1) d0 = (uint8_t)(d0 + d5);
    T(a3, ptrkDuration) = d0;
    T(a3, ptrkDurCnt) = d0;
}

enum { CMD_OK, CMD_STOP };
static void trk_jump(uint32_t *a2)
{
    uint16_t d0 = (uint16_t)(rd8(*a2) << 8 | rd8(*a2 + 1));
    *a2 += 2;
    *a2 = *a2 + (uint32_t)(int32_t)(int16_t)d0 - 1;
}
static int track_command(int a3, uint8_t cmd, uint32_t *a2)
{
    uint32_t off = vl(pdrvPtrOffset);
    switch (cmd - 0xE0) {
    case 0x00:                                          /* panning */
        select_channel(a3);
        T(a3, ptrkPanning) = rd8(*a2);
        rf_write(1, rd8((*a2)++));
        break;
    case 0x01: T(a3, ptrkDetune) = rd8((*a2)++); break;
    case 0x02: V[pdrvCommFlag] = rd8((*a2)++); break;
    case 0x03: V[pdrvCDDALoop] = 1; break;
    case 0x06: {                                        /* volume */
        select_channel(a3);
        uint8_t d0 = rd8((*a2)++), v = T(a3, ptrkVolume);
        T(a3, ptrkVolume) = (uint8_t)(v + d0);
        int carry = (unsigned)v + d0 > 0xFF;
        if ((d0 & 0x80) ? !carry : carry) {             /* went past silence */
            if (!V[pdrvFadeSteps]) break;
            T(a3, ptrkFlags) &= ~(1 << PTRK_PLAY);
            rf_write(0, 0);
            break;
        }
        rf_write(0, T(a3, ptrkVolume));
        break;
    }
    case 0x07: T(a3, ptrkFlags) |= 1 << PTRK_LEGATO; break;
    case 0x08: T(a3, ptrkStacCnt) = rd8(*a2); T(a3, ptrkStaccato) = rd8((*a2)++); break;
    case 0x0A: V[pdrvTempoCnt] = rd8(*a2); V[pdrvTempo] = rd8((*a2)++); break;
    case 0x0B: V[pdrvQueue] = rd8((*a2)++); break;
    case 0x0F: {                                        /* instrument */
        uint32_t a0 = rd32(s_samples + 4 + rd8((*a2)++) * 4u) + off;
        T(a3, ptrkSampStac) = T(a3, ptrkSampStacCnt) = rd8(a0 + 12);
        T(a3, ptrkSampleMode) = rd8(a0 + 13);
        if (T(a3, ptrkSampleMode)) {
            select_channel(a3);
            uint16_t dest = rd16(a0 + 14);
            rf_write(6, (uint8_t)(dest >> 8));
            uint16_t loop = (uint16_t)(rd32(a0 + 8) + dest);
            rf_write(4, (uint8_t)loop);
            rf_write(5, (uint8_t)(loop >> 8));
        } else {
            uint32_t a1 = rd32(a0) + off;
            setl(a3 + ptrkSampleStart, a1);
            setl(a3 + ptrkSamplePtr, a1);
            setl(a3 + ptrkSampleSize, rd32(a0 + 4));
            setl(a3 + ptrkSampRemain, rd32(a0 + 4));
            setl(a3 + ptrkSampleLoop, rd32(a0 + 8));
            setl(a3 + ptrkSampleRAM, PCMWAVE);
            T(a3, ptrkSampleBank) = 0;
            T(a3, ptrkSampleBlks) = 7;
        }
        break;
    }
    case 0x10: case 0x11: case 0x12:                    /* stop */
        T(a3, ptrkFlags) &= ~(1 << PTRK_PLAY | 1 << PTRK_LEGATO);
        rest_track(a3);
        if (V[pdrvSFXMode]) V[pdrvSFXPrio] = 0;
        return CMD_STOP;
    case 0x14: case 0x16: trk_jump(a2); break;
    case 0x17: {                                        /* repeat */
        uint8_t idx = rd8((*a2)++), count = rd8((*a2)++);
        uint8_t *r = &V[a3 + ptrkRepeatCnts + idx];     /* (shares track memory with the call stack) */
        if (a3 + ptrkRepeatCnts + idx >= pdrvSize) break;
        if (!*r) *r = count;
        if (--*r) trk_jump(a2); else *a2 += 2;
        break;
    }
    case 0x18: {                                        /* call */
        uint8_t sp = (uint8_t)(T(a3, ptrkStackPtr) - 4);
        if (a3 + sp + 4 <= pdrvSize) setl(a3 + sp, *a2);
        T(a3, ptrkStackPtr) = sp;
        trk_jump(a2);
        break;
    }
    case 0x19: {                                        /* return */
        uint8_t sp = T(a3, ptrkStackPtr);
        *a2 = (a3 + sp + 4 <= pdrvSize ? vl(a3 + sp) : *a2) + 2;
        T(a3, ptrkStackPtr) = (uint8_t)(sp + 4);
        break;
    }
    case 0x1A: T(a3, ptrkTickMult) = rd8((*a2)++); break;
    case 0x1B: T(a3, ptrkTranspose) = (uint8_t)(T(a3, ptrkTranspose) + rd8((*a2)++)); break;
    case 0x1C: {                                        /* global tick multiplier */
        uint8_t m = rd8((*a2)++);
        for (int t = 0; t < 9; t++) V[pdrvRhythm + t * ptrkSize + ptrkTickMult] = m;
        break;
    }
    case 0x1E: case 0x1F:                              /* undefined original bytecode, not a null */
        T(a3, ptrkFlags) &= ~(1 << PTRK_PLAY);
        note_off(a3);
        return CMD_STOP;
    default: break;                                     /* original null commands */
    }
    return CMD_OK;
}
static int parse_track(int a3)
{
    uint32_t a2 = vl(a3 + ptrkDataPtr);
    T(a3, ptrkFlags) &= ~(1 << PTRK_REST);
    for (int guard = 0; guard < 4096; guard++) {
        uint8_t d5 = rd8(a2++);
        if (d5 >= 0xE0) {
            if (track_command(a3, d5, &a2) == CMD_STOP) return CMD_STOP;
            continue;
        }
        if (d5 & 0x80) {
            get_freq(a3, d5);
            d5 = rd8(a2++);
            if (d5 & 0x80) { a2--; break; }
        }
        calc_duration(a3, d5);
        break;
    }
    setl(a3 + ptrkDataPtr, a2);
    T(a3, ptrkDurCnt) = T(a3, ptrkDuration);
    if (!(T(a3, ptrkFlags) & 1 << PTRK_LEGATO)) {
        note_off(a3);
        T(a3, ptrkStacCnt) = T(a3, ptrkStaccato);
        setl(a3 + ptrkSamplePtr, vl(a3 + ptrkSampleStart));
        setl(a3 + ptrkSampRemain, vl(a3 + ptrkSampleSize));
        T(a3, ptrkSampStacCnt) = T(a3, ptrkSampStac);
        setl(a3 + ptrkSampleRAM, PCMWAVE);
        setw(a3 + ptrkPrevSampPos, 0);
        setw(a3 + ptrkSampRAMOff, 0);
        T(a3, ptrkSampleBank) = 0;
        T(a3, ptrkSampleBlks) = 7;
    }
    return CMD_OK;
}
/* 1: leave the track loop (HandleStaccato's stack slip exits UpdateTracks) */
static int update_track(int a3)
{
    if (--T(a3, ptrkDurCnt)) {
        update_sample(a3);
        if (!T(a3, ptrkStacCnt) || --T(a3, ptrkStacCnt)) return 0;
        rest_track(a3);
        return 1;
    }
    T(a3, ptrkFlags) &= ~(1 << PTRK_LEGATO);
    if (parse_track(a3) == CMD_STOP) return 0;
    start_stream(a3);
    if (update_freq(a3)) return 0;
    note_on(a3);
    return 0;
}
static void update_tracks(void)
{
    V[pdrvSFXMode] = 0;
    for (int a3 = pdrvPCM1; a3 < pdrvPCMSFX1; a3 += ptrkSize)
        if ((T(a3, ptrkFlags) & 0x80) && update_track(a3)) return;
    V[pdrvSFXMode] = 0x80;
    for (int a3 = pdrvPCMSFX1; a3 < pdrvSize; a3 += ptrkSize)
        if ((T(a3, ptrkFlags) & 0x80) && update_track(a3)) return;
}

static void proc_sound_queue(void)
{
    if (!vl(pdrvQueue)) return;
    uint8_t d3 = V[pdrvSFXPrio];
    uint32_t songp = rd32(s_info), sfxp = rd32(s_info + 0x20), cmdp = rd32(s_info + 0x24);
    uint8_t sfx_end = s_boss ? 0xBA : 0xBF;
    for (int i = 0; i < 4; i++) {
        uint8_t d0 = V[pdrvQueue + i];
        V[pdrvQueue + i] = 0;
        uint32_t table;
        if (d0 < PCMM_START) continue;
        if (d0 <= PCMM_END) table = songp + (d0 - PCMM_START);
        else if (d0 < PCMS_START) continue;
        else if (d0 <= sfx_end) table = sfxp + (d0 - PCMS_START);
        else if (d0 < PCMC_START) continue;
        else if (d0 <= PCMC_END) table = cmdp + (d0 - PCMC_START);
        else continue;
        uint8_t d2 = rd8(table);
        if (d2 < d3) continue;
        d3 = d2;
        V[pdrvSndPlay] = d0;
    }
    if (!(d3 & 0x80)) V[pdrvSFXPrio] = d3;
}
static void play_song(uint8_t d7)
{
    reset_driver();
    uint32_t a2 = rd32(rd32(s_info + 8) + ((d7 - PCMM_START) & 0x7F) * 4u) + vl(pdrvPtrOffset), a0 = a2;
    int n = rd8(a2 + 2);
    uint8_t mult = rd8(a2 + 4);
    V[pdrvTempo] = V[pdrvTempoCnt] = rd8(a2 + 5);
    a2 += 6;
    uint32_t ids = s_chanids;
    for (int a3 = pdrvRhythm; n-- > 0 && a3 < pdrvSize; a3 += ptrkSize) {
        setl(a3 + ptrkDataPtr, (uint32_t)rd16(a2) + a0);
        T(a3, ptrkTranspose) = rd8(a2 + 2);
        T(a3, ptrkVolume) = rd8(a2 + 3);
        a2 += 4;
        uint8_t ch = rd8(ids++);
        T(a3, ptrkChannel) = ch;
        rf_write(7, (uint8_t)(ch | 0xC0));
        uint8_t st = (uint8_t)((ch | 0xC0) << 5);
        rf_write(6, st);
        rf_write(5, st);
        rf_write(4, 0);
        rf_write(1, 0xFF);
        rf_write(0, T(a3, ptrkVolume));
        T(a3, ptrkTickMult) = mult;
        T(a3, ptrkStackPtr) = ptrkStackBase;
        T(a3, ptrkFlags) = 1 << PTRK_PLAY;
        T(a3, ptrkDurCnt) = 1;
    }
    V[pdrvRhythm + ptrkFlags] = 0;
    V[pdrvOn] = 0xFF;
}
static void play_sfx(uint8_t d7)
{
    uint32_t a2 = rd32(rd32(s_info + 12) + ((d7 - PCMS_START) & 0x7F) * 4u) + vl(pdrvPtrOffset), a0 = a2;
    int n = rd8(a2 + 3);
    uint8_t mult = rd8(a2 + 2);
    a2 += 4;
    while (n-- > 0) {
        int a3 = pdrvPCMSFX1 + rd8(a2 + 1) * ptrkSize;
        if (a3 + ptrkSize > pdrvSize) { a2 += 6; continue; }
        memset(&V[a3], 0, ptrkSize);
        T(a3, ptrkFlags) = rd8(a2);
        T(a3, ptrkChannel) = rd8(a2 + 1);
        setl(a3 + ptrkDataPtr, (uint32_t)rd16(a2 + 2) + a0);
        T(a3, ptrkTranspose) = rd8(a2 + 4);
        T(a3, ptrkVolume) = rd8(a2 + 5);
        a2 += 6;
        uint8_t ch = (uint8_t)(T(a3, ptrkChannel) | 0xC0);
        rf_write(7, ch);
        uint8_t st = (uint8_t)(ch << 5);
        rf_write(6, st);
        rf_write(5, st);
        rf_write(4, 0);
        rf_write(1, 0xFF);
        rf_write(0, T(a3, ptrkVolume));
        T(a3, ptrkTickMult) = mult;
        T(a3, ptrkStackPtr) = ptrkStackBase;
        T(a3, ptrkDurCnt) = 1;
        T(a3, ptrkStaccato) = 0;
        T(a3, ptrkDetune) = 0;
    }
}
static void run_command(uint8_t d7)
{
    switch (d7 - PCMC_START) {
    case 0: V[pdrvFadeSteps] = 0x60; V[pdrvFadeDelay] = 1; V[pdrvFadeSpeed] = 2; break;
    case 1: cmd_stop(); break;
    case 2: V[pdrvPauseMode] = 1; break;
    case 3: V[pdrvPauseMode] = 0x80; break;
    case 4: rf_write(8, 0xFF); break;
    }
}
static void play_sound_id(void)
{
    uint8_t d7 = V[pdrvSndPlay];
    if (!d7) { init_driver(); return; }
    if (!(d7 & 0x80)) { cmd_stop(); return; }
    V[pdrvSndPlay] = 0x80;
    if (d7 < PCMM_START) return;
    if (d7 <= PCMM_END) { play_song(d7); return; }
    if (d7 < PCMS_START) return;
    if (d7 <= (s_boss ? 0xBA : 0xBF)) { play_sfx(d7); return; }
    if (d7 >= PCMC_START && d7 <= PCMC_END) run_command(d7);
}
/* 1: paused (the driver exits) */
static int handle_pause(void)
{
    uint8_t m = V[pdrvPauseMode];
    if (!m) return 0;
    if (m & 0x80) { V[pdrvPauseMode] = 0; return 0; }
    if (m != 2) { rf_write(8, 0xFF); V[pdrvPauseMode] = 2; }
    return 1;
}
static void handle_tempo(void)
{
    if (!V[pdrvTempo] || --V[pdrvTempoCnt]) return;
    V[pdrvTempoCnt] = V[pdrvTempo];
    for (int t = 0; t < 9; t++) V[pdrvRhythm + t * ptrkSize + ptrkDurCnt]++;
}
static void handle_fade_out(void)
{
    if (!V[pdrvFadeSteps]) return;
    if (V[pdrvFadeCnt]) { V[pdrvFadeCnt]--; return; }
    if (!--V[pdrvFadeSteps]) { reset_driver(); return; }
    V[pdrvFadeCnt] = V[pdrvFadeDelay];
    uint8_t d6 = V[pdrvFadeSpeed];
    V[pdrvUnkFadeVol] = (uint8_t)(V[pdrvUnkFadeVol] + d6);
    for (int t = 0; t < 9; t++) {
        int a3 = pdrvRhythm + t * ptrkSize;
        if (!(T(a3, ptrkFlags) & 0x80)) continue;
        uint8_t v = T(a3, ptrkVolume);
        T(a3, ptrkVolume) = (uint8_t)(v - d6);
        if (v < d6) { T(a3, ptrkVolume) = 0; T(a3, ptrkFlags) &= ~(1 << PTRK_PLAY); }
        select_channel(a3);
        rf_write(0, T(a3, ptrkVolume));
    }
}
static void update_driver(void)
{
    if (!s_bank) return;
    if (s_boss) V[pdrvUnkCounter]++;
    proc_sound_queue();
    play_sound_id();
    if (handle_pause()) return;
    handle_tempo();
    handle_fade_out();
    update_tracks();
    rf_write(8, V[pdrvOn]);
}

/* The driver's tables, found through the code that uses them: the same
 * driver is assembled into every bank, its data after it. */
/* pattern: words, with 0xFFFF meaning "the 16-bit displacement" */
static uint32_t find_pcrel(const uint16_t *pat, int n)
{
    for (uint32_t i = 0x10; i + (uint32_t)n * 2 <= s_bank_size && i < 0x4000; i += 2) {
        int ok = 1, at = -1;
        for (int k = 0; k < n && ok; k++) {
            uint16_t w = (uint16_t)(s_bank[i + k * 2] << 8 | s_bank[i + k * 2 + 1]);
            if (pat[k] == 0xFFFF) at = k; else ok = w == pat[k];
        }
        if (ok && at >= 0) {
            uint32_t dpos = i + (uint32_t)at * 2;
            int16_t d = (int16_t)(s_bank[dpos] << 8 | s_bank[dpos + 1]);
            return PCMDRIVER + dpos + (uint32_t)(int32_t)d;
        }
    }
    return 0;
}

int scd_pcm_bank(const char *file)
{
    uint32_t size;
    const uint8_t *b = scd_disc_file(file, &size);
    if (!b) return 0;
    s_bank = b;
    s_bank_size = size;
    static const uint16_t get_ptrs[] = { 0x4DFA, 0xFFFF, 0x4BFA };        /* lea DriverInfo(pc),a6; lea Variables(pc),a5 */
    static const uint16_t freq[] = { 0x41FA, 0xFFFF, 0xDA2B, 0x0008 };  /* lea FreqTable(pc),a0; add.b ptrkTranspose(a3),d5 */
    static const uint16_t samp[] = { 0x101A, 0x41FA, 0xFFFF, 0x5848 };  /* move.b (a2)+,d0; lea SampleIndex(pc),a0; addq.w #4,a0 */
    static const uint16_t chan[] = { 0x47ED, 0x0080, 0x43FA, 0xFFFF };  /* lea pdrvRhythm(a5),a3; lea ChannelIDs(pc),a1 */
    s_info = find_pcrel(get_ptrs, 3);
    s_freq = find_pcrel(freq, 4);
    s_samples = find_pcrel(samp, 4);
    s_chanids = find_pcrel(chan, 4);
    s_boss = !strncmp(file, "SNCBNKB", 7);
    if (!s_info || !s_freq || !s_samples || !s_chanids) {
        fprintf(stderr, "[scd] %s: driver tables not found\n", file);
        s_bank = NULL;
        return 0;
    }
    memset(V, 0, sizeof V);
    init_driver();                                      /* (now: a command may follow at once) */
    scd_audio_init();
    return 1;
}

void scd_pcm_cmd(uint8_t id)
{
    for (int i = 0; i < 4; i++)                         /* (the sub CPU's queue slots) */
        if (!V[pdrvQueue + i]) { V[pdrvQueue + i] = id; return; }
}

/* ------------------------------------------------------------------------
 * CD-DA
 * ---------------------------------------------------------------------- */
#define CD_BLOCK 4096                               /* stereo frames per read */
static struct {
    FILE *f;
    long start, frames, pos;                         /* in stereo frames */
    int track, loop, paused;
    int16_t buf[CD_BLOCK * 2];
    long buf_at, buf_n;
    double frac;
    float vol, fade, gain;                           /* fade: seconds to silence (0 none) */
    int32_t prev[2];
} s_cd;

/* ---- each track's loudness (BS.1770: K-weighted, 400 ms blocks, gated),
 * measured the first time it plays, kept beside the executable ---------- */
static float s_lufs[128];                           /* 0: not known */
static uint32_t s_lufs_size[128];

static void lufs_load(void) { /* Measurements are cached in memory for this resource/session. */ }

static void lufs_save(void) { /* Converted inputs are never rewritten. */ }

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Biquad;
static double bq(Biquad *q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static float measure_lufs(const ScdTrack *t)
{
    FILE *f = fopen(t->path, "rb");
    if (!f) return 0;
    const double fs = CD_RATE, pi = 3.14159265358979323846;
    Biquad sh[2], hp[2];                             /* (the pre-filter's shelf, then RLB: libebur128's design) */
    {
        double K = tan(pi * 1681.974450955533 / fs), Q = 0.7071752369554196, Vh = pow(10, 3.999843853973347 / 20);
        double Vb = pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
        Biquad s = { (Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                     2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0, 0, 0 };
        double K2 = tan(pi * 38.13547087602444 / fs), Q2 = 0.5003270373238773, d = 1 + K2 / Q2 + K2 * K2;
        Biquad h = { 1, -2, 1, 2 * (K2 * K2 - 1) / d, (1 - K2 / Q2 + K2 * K2) / d, 0, 0 };
        sh[0] = sh[1] = s;
        hp[0] = hp[1] = h;
    }
    const long hop = 4410;                           /* (100 ms; a block is four) */
    long frames = (long)(t->size / 4), nhops = frames / hop;
    double *e = calloc((size_t)(nhops + 1), sizeof *e);
    uint8_t *raw = malloc((size_t)hop * 4);
    if (!e || !raw) { free(e); free(raw); fclose(f); return 0; }
    fseek(f, t->start, SEEK_SET);
    for (long h = 0; h < nhops; h++) {
        if (fread(raw, 4, (size_t)hop, f) != (size_t)hop) { nhops = h; break; }
        double sum = 0;
        for (long i = 0; i < hop; i++)
            for (int c = 0; c < 2; c++) {
                double x = (int16_t)(raw[i * 4 + c * 2] | raw[i * 4 + c * 2 + 1] << 8) / 32768.0;
                double y = bq(&hp[c], bq(&sh[c], x));
                sum += y * y;
            }
        e[h] = sum;
    }
    fclose(f);
    free(raw);
    double gated = 0, all = 0;
    long n = 0, n2 = 0;
    for (int pass = 0; pass < 2; pass++) {
        double rel = pass ? -0.691 + 10 * log10(all / (n ? n : 1)) - 10 : -70;
        for (long b = 0; b + 3 < nhops; b++) {
            double ms = (e[b] + e[b + 1] + e[b + 2] + e[b + 3]) / (4.0 * hop);
            if (ms <= 0) continue;
            double l = -0.691 + 10 * log10(ms);
            if (l <= -70 || l <= rel) continue;
            if (pass) { gated += ms; n2++; } else { all += ms; n++; }
        }
    }
    free(e);
    return n2 ? (float)(-0.691 + 10 * log10(gated / n2)) : 0;
}

static float track_gain(int track, const ScdTrack *t)
{
    lufs_load();
    if (track <= 0 || track >= 128) return CD_GAIN;
    if (!(s_lufs[track] < 0) || s_lufs_size[track] != t->size) {
        float v = measure_lufs(t);
        if (!(v < 0)) return CD_GAIN;
        s_lufs[track] = v;
        s_lufs_size[track] = t->size;
        lufs_save();
        fprintf(stderr, "[scd] track %d: %.1f LUFS\n", track, v);
    }
    float g = (float)pow(10, (TARGET_LUFS - s_lufs[track]) / 20);
    return g < 0.05f ? 0.05f : g > 1.5f ? 1.5f : g;
}

void scd_cd_stop(void)
{
    if (s_cd.f) fclose(s_cd.f);
    memset(&s_cd, 0, sizeof s_cd);
}
void scd_cd_play(int track, int loop)
{
    static int log = -1;                                       /* development: TRILOGY_AUDIOLOG */
    if (log < 0) log = getenv("TRILOGY_AUDIOLOG") != NULL;
    if (log) fprintf(stderr, "[scd] cd play track %d loop %d\n", track, loop);
    scd_audio_init();
    if (s_cd.track == track && s_cd.f && !s_cd.fade) { s_cd.loop = loop; return; }
    scd_cd_stop();
    const ScdTrack *t = scd_disc_open() ? scd_track_info(track) : NULL;
    if (!t || !t->audio) return;
    s_cd.f = fopen(t->path, "rb");
    if (!s_cd.f) return;
    s_cd.start = t->start;
    s_cd.frames = t->size / 4;
    s_cd.track = track;
    s_cd.loop = loop;
    s_cd.vol = 1;
    s_cd.gain = track_gain(track, t);
    s_cd.buf_at = -1;
}
void scd_cd_fade(void) { if (s_cd.f && !s_cd.fade) s_cd.fade = 0.75f; }
void scd_cd_pause(int paused) { s_cd.paused = paused; }
int scd_cd_track(void) { return s_cd.f ? s_cd.track : 0; }

static int cd_frame(long n, int32_t *l, int32_t *r)
{
    if (n >= s_cd.frames) {
        if (!s_cd.loop || !s_cd.frames) return 0;
        n %= s_cd.frames;
    }
    if (s_cd.buf_at < 0 || n < s_cd.buf_at || n >= s_cd.buf_at + s_cd.buf_n) {
        s_cd.buf_at = n;
        fseek(s_cd.f, s_cd.start + n * 4, SEEK_SET);
        long want = s_cd.frames - n < CD_BLOCK ? s_cd.frames - n : CD_BLOCK;
        uint8_t raw[CD_BLOCK * 4];
        s_cd.buf_n = (long)fread(raw, 4, (size_t)want, s_cd.f);
        for (long i = 0; i < s_cd.buf_n * 2; i++)       /* little-endian 16-bit */
            s_cd.buf[i] = (int16_t)(raw[i * 2] | raw[i * 2 + 1] << 8);
        if (!s_cd.buf_n) return 0;
    }
    *l = s_cd.buf[(n - s_cd.buf_at) * 2];
    *r = s_cd.buf[(n - s_cd.buf_at) * 2 + 1];
    return 1;
}

/* ------------------------------------------------------------------------
 * Mixing (the runner's extra source, at its internal rate)
 * ---------------------------------------------------------------------- */
static double s_pcm_frac;
static int s_pcm_count;
static int32_t s_pcm_prev[2], s_pcm_next[2];

static void mix(int16_t *out, size_t frames, double rate)
{
    double cd_step = CD_RATE / rate, pcm_step = PCM_RATE / rate;
    for (size_t i = 0; i < frames; i++) {
        int32_t l = 0, r = 0;
        if (s_cd.f && !s_cd.paused) {
            int32_t a[2], b[2];
            if (!cd_frame(s_cd.pos, &a[0], &a[1])) { scd_cd_stop(); }
            else {
                if (!cd_frame(s_cd.pos + 1, &b[0], &b[1])) { b[0] = a[0]; b[1] = a[1]; }
                float t = (float)s_cd.frac, g = s_cd.gain * s_cd.vol;
                l += (int32_t)((a[0] + (b[0] - a[0]) * t) * g);
                r += (int32_t)((a[1] + (b[1] - a[1]) * t) * g);
                s_cd.frac += cd_step;
                while (s_cd.frac >= 1) { s_cd.frac -= 1; s_cd.pos++; }
                if (s_cd.loop && s_cd.pos >= s_cd.frames) s_cd.pos -= s_cd.frames;
                if (s_cd.fade && (s_cd.vol -= (float)(1.0 / (s_cd.fade * rate))) <= 0) scd_cd_stop();
            }
        }
        if (s_bank) {
            while (s_pcm_frac >= 1) {                   /* chip samples (and driver ticks) due */
                s_pcm_frac -= 1;
                s_pcm_prev[0] = s_pcm_next[0];
                s_pcm_prev[1] = s_pcm_next[1];
                if (s_pcm_count++ % TICK_SAMPLES == 0) update_driver();
                rf_sample(&s_pcm_next[0], &s_pcm_next[1]);
            }
            float t = (float)s_pcm_frac;
            l += (int32_t)((s_pcm_prev[0] + (s_pcm_next[0] - s_pcm_prev[0]) * t) * PCM_GAIN);
            r += (int32_t)((s_pcm_prev[1] + (s_pcm_next[1] - s_pcm_prev[1]) * t) * PCM_GAIN);
            s_pcm_frac += pcm_step;
        }
        if (!l && !r) continue;
        l += out[i * 2];
        r += out[i * 2 + 1];
        out[i * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[i * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
}

void scd_audio_init(void)
{
    audio_set_extra_source(mix);
}

void scd_audio_reset(void)
{
    scd_cd_stop();
    if(s_bank){reset_driver();rf_write(8,0xFF);}
    s_bank=NULL;s_bank_size=0;
    s_pcm_frac=0;s_pcm_count=0;
    memset(s_pcm_prev,0,sizeof s_pcm_prev);memset(s_pcm_next,0,sizeof s_pcm_next);
}

/* development: TRILOGY_SCD_PLAY=cd:<track> | pcm:<bank file>:<id> plays at boot */
void scd_audio_dev(void)
{
    const char *e = getenv("TRILOGY_SCD_PLAY");
    if (!e || !*e) return;
    char file[64];
    unsigned id, track;
    if (sscanf(e, "cd:%u", &track) == 1) scd_cd_play((int)track, 1);
    else if (sscanf(e, "pcm:%63[^:]:%x", file, &id) == 2 && scd_pcm_bank(file)) scd_pcm_cmd((uint8_t)id);
}

void scd_audio_close(void)
{audio_set_extra_source(NULL);scd_audio_reset();scd_disc_close();}
