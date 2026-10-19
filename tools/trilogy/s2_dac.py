"""Sonic 2's DAC samples on the retail S3K sound driver.

S3K's driver finds a DAC note's sample through DAC_Banks (one bank byte per
sample id, in the driver's Z80 RAM) and a pointer table at the start of that
bank's Z80 window: pointer -> (loop counter, length, location). S2's seven
samples (kick, snare, clap, scratch, timpani, tom, bongo - jman2050 DPCM, the
same delta table and nibble order as S3K's) are copied from the player's
cartridge into a 32 KB bank of their own, with one entry per S2 note $81-$91
(sample + loop counter, zDACMasterPlaylist). In S2 songs those notes become
S3K ids $B4-$C4 (S3K's hip-hop kicks and voice clips, which nothing that plays
in an S2 zone uses); trilogy_music.c points DAC_Banks at this bank for them
while an S2 song is loaded and restores the driver's own entries otherwise.

Loop counters: S2's zWriteToDAC spends 295 Z80 cycles per byte plus 26 per
counter step, S3K's playback loop 303: the same counter plays within 1-2% of
S2's pitch, so S2's counters are kept.
"""
import struct

import s2_music

FIRST_ID = 0xB4                       # S3K id for S2 note $81
N_NOTES = 17                          # S2 notes $81-$91
N_TABLE = 0xC4 - 0x81 + 1             # entries in a S3K DAC bank's pointer table


def build(s2rom, bank, sym):
    """-> table blob: u8 bank byte (68k address bits 15-22), u8 first S3K id, u8 count."""
    drv = s2_music.saxman_decode(s2rom, sym['Snd_Driver'], sym['Snd_Driver_End'] - sym['Snd_Driver'])
    p, m = sym['zDACPtrTbl'], sym['zDACMasterPlaylist']
    dac_bank = sym['SndDAC_Start'] & ~0x7FFF
    samples = []                      # (data) per S2 sample $81-$87
    for i in range(7):
        ptr = drv[p + i * 4] | drv[p + i * 4 + 1] << 8
        ln = drv[p + i * 4 + 2] | drv[p + i * 4 + 3] << 8
        a = dac_bank + (ptr & 0x7FFF)
        samples.append(bytes(s2rom[a:a + ln]))
    notes = [(drv[m + i * 2] - 0x81, drv[m + i * 2 + 1]) for i in range(N_NOTES)]

    out = bytearray(N_TABLE * 2)
    entries_at = len(out)
    out += bytes(N_TABLE * 5)                        # (rate, len, ptr) per id
    sample_at = []
    for data in samples:
        sample_at.append(len(out))
        out += data
    silent = len(out)
    out += b'\x88'                                   # (a byte of no change, for unused ids)
    for idx in range(N_TABLE):
        e = entries_at + idx * 5
        struct.pack_into('<H', out, idx * 2, 0x8000 + e)
        k = idx - (FIRST_ID - 0x81)
        if 0 <= k < N_NOTES:
            s, rate = notes[k]
            struct.pack_into('<BHH', out, e, rate, len(samples[s]), 0x8000 + sample_at[s])
        else:
            struct.pack_into('<BHH', out, e, 1, 1, 0x8000 + silent)
    if len(out) > 0x8000:
        raise SystemExit("S2 DAC samples do not fit a bank")
    addr = bank.add(bytes(out), align=0x8000, dedupe=False)
    return bytes([(addr >> 15) & 0xFF, FIRST_ID, N_NOTES]), sum(len(x) for x in samples)
