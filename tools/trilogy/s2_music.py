"""Sonic 2 music (SMPS Z80, Sonic 2 driver) -> S3K driver (S&K driver) songs.

Songs come from the player's Sonic 2 cartridge. Saxman-compressed songs are
decompressed (S2's driver unpacks them to Z80 RAM $1380, so their internal
pointers are based there); the rest play from their music bank window.
Re-encoding follows SMPS2ASM's driver 2 -> 3 rules:
  - main tempo s2TempotoS3, PSG header transposition +12;
  - coordination flags: return E3 -> F9, transpose E9 -> FB, tempo EA/EB ->
    FF00/FF04, modulation F1/F4 -> F4 80 / FA, modulation steps (step+1)*speed,
    note fill divided by the tempo divider, F9 -> the two FM register writes
    it performs;
  - FM voices: operators 2 and 3 trade places in every row (S2 order op4 op2 op3
    op1 -> op4 op3 op2 op1) and the TL bytes get their carrier bits from the algorithm;
  - DAC notes play Sonic 2's own samples (s2_dac.py, ids $B4-$C4);
  - on PSG tracks the FM volume flag E6 becomes EC (smpsPSGAlterVolS2) and
    nMaxPSG (in noise mode) moves from nA5 to nBb5;
  - a bare duration after a rest gets its rest note written out: S3K's driver
    clears the resting flag before every byte, so it would sound the old note; E4 (fade) -> E2 FF + stop (smpsFade);
  - S2's PSG envelopes 1-13 go to S3K envelope slots that no S3K sound effect
    or jingle uses (ENV_SLOTS, shared with the S1 songs, whose 1-9 are S2's).
"""
import struct

from s1_music import be16, s16, s1_tempo_to_s3  # noqa: F401 (shared helpers)

# 1-based S3K envelope ids for game envelopes 1..13 (free of S3K SFX and the
# jingles that still play in trilogy levels)
ENV_SLOTS = [0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x12, 0x06, 0x07, 0x0B, 0x10]

# S2 coordination flag parameter counts (S2 Z80 driver: the byte after a flag is
# pre-read; parameterless handlers put it back)
S2_FLAG_LEN = {0xE0: 1, 0xE1: 1, 0xE2: 1, 0xE3: 0, 0xE4: 0, 0xE5: 1, 0xE6: 1, 0xE7: 0,
               0xE8: 1, 0xE9: 1, 0xEA: 1, 0xEB: 1, 0xEC: 1, 0xED: 1, 0xEE: 0, 0xEF: 1,
               0xF0: 4, 0xF1: 0, 0xF2: 0, 0xF3: 1, 0xF4: 0, 0xF5: 1, 0xF6: 2, 0xF7: 4,
               0xF8: 2, 0xF9: 0}
END_FLAGS = (0xE3, 0xE4, 0xF2, 0xF6)

# S2 DAC notes $81-$91 -> the S3K ids s2_dac.py gives Sonic 2's own samples
DAC_MAP = {0x81 + i: 0xB4 + i for i in range(17)}   # hi/mid/low clap

# trilogy S2 song id -> (song label, Saxman-compressed); ids are S2's own music ids
S2_SONGS = {
    0x81: ('Mus_2PResult', True), 0x82: ('Mus_EHZ', True), 0x83: ('Mus_MCZ_2P', True), 0x84: ('Mus_OOZ', True),
    0x85: ('Mus_MTZ', True), 0x86: ('Mus_HTZ', True), 0x87: ('Mus_ARZ', True), 0x88: ('Mus_CNZ_2P', True),
    0x89: ('Mus_CNZ', True), 0x8A: ('Mus_DEZ', True), 0x8B: ('Mus_MCZ', True), 0x8C: ('Mus_EHZ_2P', True),
    0x8D: ('Mus_SCZ', True), 0x8E: ('Mus_CPZ', True), 0x8F: ('Mus_WFZ', True), 0x90: ('Mus_HPZ', True),
    0x91: ('Mus_Options', True), 0x92: ('Mus_SpecStage', True), 0x93: ('Mus_Boss', True),
    0x94: ('Mus_EndBoss', True), 0x95: ('Mus_Ending', True), 0x96: ('Mus_SuperSonic', True),
    0x97: ('Mus_Invincible', True), 0x98: ('Mus_ExtraLife', False), 0x99: ('Mus_Title', True),
    0x9A: ('Mus_EndLevel', True), 0x9B: ('Mus_GameOver', False), 0x9C: ('Mus_Continue', True),
    0x9D: ('Mus_Emerald', False), 0x9E: ('Mus_Credits', False), 0x9F: ('Mus_Drowning', True),
}
ZMUSICDATA = 0x1380
# SMPS2ASM's nMaxPSG (the noise channel's top note): nA5 for S2's driver, nBb5 for S3K's.
# Songs write it in noise mode; the boss theme's noise notes are spelled nA5 instead.
NMAXPSG_S2, NMAXPSG_S3 = 0xC6, 0xC7
NMAXPSG_KEEP = {0x93}


def saxman_decode(rom, a, size=None):
    """S2 Saxman: LE 16-bit size (songs; the driver's size is given instead),
    then LZSS with a 4 KB window aligned to $12."""
    if size is None:
        size = rom[a] | (rom[a + 1] << 8)
        a += 2
    p, end = a, a + size
    out = bytearray()
    desc, bits = 0, 0
    while p < end:
        if not bits:
            desc, bits = rom[p], 8
            p += 1
            continue
        lit = desc & 1
        desc >>= 1
        bits -= 1
        if lit:
            out.append(rom[p])
            p += 1
        else:
            lo, hi = rom[p], rom[p + 1]
            p += 2
            off = (lo | ((hi & 0xF0) << 4))
            n = (hi & 0x0F) + 3
            src = ((off + 0x12) & 0xFFF) + (len(out) & ~0xFFF)
            if src >= len(out):
                src -= 0x1000
            for _ in range(n):
                out.append(out[src] if src >= 0 else 0)
                src += 1
    return bytes(out)


def s2_voice_to_s3(v):
    """S2's driver keeps each operator row as op4, op2, op3, op1; S1's and S3K's as
    op4, op3, op2, op1 (SMPS2ASM smpsVcTotalLevel, SonicDriverVer==2): swap the
    middle two of each of the six rows."""
    vc = bytearray(v)
    for g in range(1, 25, 4):
        vc[g + 1], vc[g + 2] = vc[g + 2], vc[g + 1]
    return vc


class S2Song:
    """One S2 song: bytes `data` addressed from Z80 address `base`."""

    def __init__(self, data, base):
        self.d, self.base = data, base
        self.voice_ptr = self._lw(0)
        self.nfm, self.npsg = data[2], data[3]
        self.div, self.tempo = data[4], data[5]
        self.ins, self.ctx = {}, {}
        self.max_voice = -1
        self.chans = []
        a = 6
        for i in range(self.nfm):
            self.chans.append(('dac' if i == 0 else 'fm', self._lw(a), data[a + 2:a + 4]))
            a += 4
        for _ in range(self.npsg):
            self.chans.append(('psg', self._lw(a), data[a + 2:a + 6]))
            a += 6
        for kind, ptr, _ in self.chans:
            self._walk(ptr, kind)
        self.uses_e5 = any(r[0] == 0xE5 for r, _, _ in self.ins.values())

    def _lw(self, o):
        return self.d[o] | (self.d[o + 1] << 8)

    def _off(self, z80):
        return z80 - self.base

    def _walk(self, ptr, kind):
        d = self.d
        if not hasattr(self, 'noise'):
            self.noise, self.rest_dur = {}, set()
        todo = [(ptr, False, False)]
        while todo:
            p, noise, resting = todo.pop()
            after_note = False
            while p not in self.ins:
                o = self._off(p)
                b = d[o]
                self.ctx[p] = kind
                self.noise[p] = noise                   # (the PSG noise mode on the path here)
                if b < 0xE0:
                    self.ins[p] = (bytes([b]), 'byte', None)
                    if b >= 0x80:
                        resting, after_note = b == 0x80, True
                    else:
                        if not after_note and resting and kind != 'dac':
                            self.rest_dur.add(p)        # a bare duration after a rest
                        after_note = False
                    p += 1
                    continue
                after_note = False
                n = 1 + S2_FLAG_LEN[b]
                raw = bytes(d[o:o + n])
                tgt = None
                if b in (0xF6, 0xF8):
                    tgt = raw[1] | (raw[2] << 8)
                elif b == 0xF7:
                    tgt = raw[3] | (raw[4] << 8)
                if b == 0xEF:
                    self.max_voice = max(self.max_voice, raw[1] & 0x7F)
                self.ins[p] = (raw, 'flag', tgt)
                if b == 0xF3:
                    noise = (raw[1] & 0xF0) == 0xE0
                if tgt is not None:
                    todo.append((tgt, noise, resting))
                if b in END_FLAGS:
                    break
                p += n

    def _encode(self, addr):
        raw, typ, tgt = self.ins[addr]
        kind = self.ctx[addr]
        b = raw[0]
        if typ == 'byte':
            if kind == 'dac' and 0x81 <= b < 0xE0:
                return bytes([DAC_MAP.get(b, b)]), None
            if kind == 'psg' and b == NMAXPSG_S2 and self.noise.get(addr) and not getattr(self, 'keep_nmax', False):
                return bytes([NMAXPSG_S3]), None
            if addr in getattr(self, 'rest_dur', ()):    # S3K's driver drops the rest flag on a bare duration
                return bytes([0x80, b]), None
            return raw, None
        p = raw[1:]
        if b == 0xE6 and kind == 'psg':            # smpsPSGAlterVolS2: S2 lets the FM volume flag work on PSG
            return bytes([0xEC, p[0]]), None
        if b in (0xE0, 0xE1, 0xE6, 0xE7, 0xEC, 0xEF, 0xF2, 0xF3):
            return raw, None
        if b == 0xE2:
            return (raw, None) if p[0] not in (0xFF, 0x29) else (b'', None)
        if b == 0xE3:
            return b'\xF9', None
        if b == 0xE4:                               # smpsFade: fade the saved song back in, stop
            return b'\xE2\xFF\xF2', None
        if b == 0xE5:
            return b'', None
        if b == 0xE8:
            v = p[0]
            return bytes([0xE8, (max(1, v // max(1, self.div)) if v else 0) & 0xFF]), None
        if b == 0xE9:
            return bytes([0xFB, p[0]]), None
        if b == 0xEA:
            return bytes([0xFF, 0x00, (0x100 - ((p[0] == 0) | p[0])) & 0xFF]), None
        if b == 0xEB:
            return bytes([0xFF, 0x04, p[0]]), None
        if b in (0xED, 0xEE):
            return b'', None
        if b == 0xF0:
            wait, speed, change, step = p
            return bytes([0xF0, (wait + 1) & 0xFF, speed, change, ((step + 1) * speed) & 0xFF]), None
        if b == 0xF1:
            return b'\xF4\x80', None
        if b == 0xF4:
            return b'\xFA', None
        if b == 0xF5:
            v = p[0]
            return bytes([0xF5, ENV_SLOTS[v - 1] if 1 <= v <= 13 else v]), None
        if b in (0xF6, 0xF8):
            return bytes([b, 0, 0]), (1, tgt)
        if b == 0xF7:
            return bytes([0xF7, p[0], p[1], 0, 0]), (3, tgt)
        if b == 0xF9:
            return b'\xEE\x88\x0F\xEE\x8C\x0F', None
        raise ValueError(f"unhandled S2 flag {b:02X}")

    def assemble(self, base):
        nvoices = self.max_voice + 1
        hdr_len = 6 + self.nfm * 4 + self.npsg * 6
        body, newaddr, fixups = bytearray(), {}, []
        for a in sorted(self.ins):
            newaddr[a] = base + hdr_len + len(body)
            enc, fix = self._encode(a)
            if fix:
                fixups.append((len(body) + fix[0], fix[1]))
            body += enc
        for off, tgt in fixups:
            if tgt not in newaddr:
                raise ValueError(f"jump into undecoded data at {tgt:X}")
            struct.pack_into('<H', body, off, newaddr[tgt])
        voices = bytearray()
        vo = self._off(self.voice_ptr)
        for v in range(nvoices):
            vc = s2_voice_to_s3(self.d[vo + v * 25:vo + v * 25 + 25])
            alg = vc[0] & 7
            masks = [(alg == 7) << 7, (alg >= 4) << 7, (alg >= 5) << 7, 0x80]
            for i in range(4):
                vc[21 + i] = (vc[21 + i] & 0x7F) | masks[i]
            voices += vc
        voice_addr = base + hdr_len + len(body)
        tempo = (0x100 - ((self.tempo == 0) | self.tempo)) & 0xFF
        hdr = bytearray(struct.pack('<H', voice_addr)) + bytes([self.nfm, self.npsg, self.div, tempo])
        for kind, ptr, extra in self.chans:
            hdr += struct.pack('<H', newaddr[ptr])
            if kind != 'psg':
                hdr += bytes(extra)
            else:
                pitch, vol, mod, env = extra
                hdr += bytes([(pitch + 12) & 0xFF, vol, 0, ENV_SLOTS[env - 1] if 1 <= env <= 13 else env])   # (S2 ignores the frequency envelope byte; OOZ and the ending leave garbage there)
        return bytes(hdr + body + voices)


def s2_envelopes(rom, env_table):
    """S2 PSG envelopes 1-13 (Z80 driver data; terminator $80 hold -> S3K $81 hold).
    env_table: ROM address of each envelope (the driver is Saxman-packed in ROM, so
    the caller passes the unpacked driver and offsets)."""
    out = []
    for data, o in env_table:
        e = bytearray()
        while data[o] < 0x80:
            e.append(data[o])
            o += 1
        e.append(0x81)
        out.append(bytes(e))
    return out


def build_music(rom, bank, sym, driver_envs):
    """Add the S2 songs to the bank. Returns (table blob, sizes): u8 env count, u8 0,
    env count x u16 LE envelope pointers, then per song $81..$9F: u8 bank, u8 0, u16 LE ptr."""
    env_blob = bytearray()
    env_ptrs = []
    for e in driver_envs:
        env_ptrs.append(0x8000 + len(env_blob))
        env_blob += e
    if len(env_blob) & 1:
        env_blob.append(0)
    songs = {}
    for sid, (lbl, packed) in S2_SONGS.items():
        a = sym[lbl]
        if packed:
            songs[sid] = S2Song(saxman_decode(rom, a), ZMUSICDATA)
        else:
            songs[sid] = S2Song(rom[a & ~0x7FFF:(a & ~0x7FFF) + 0x8000][a & 0x7FFF:], 0x8000 + (a & 0x7FFF))
        songs[sid].keep_nmax = sid in NMAXPSG_KEEP
    banks, cur, pending, sizes = [], bytearray(env_blob), [], []

    def flush():
        nonlocal cur, pending
        if pending:
            banks.append((bank.add(bytes(cur), align=0x8000, dedupe=False), list(pending)))
        cur, pending = bytearray(env_blob), []

    for sid in sorted(songs):
        size = len(songs[sid].assemble(0x8000))      # size does not depend on placement
        if len(cur) + size > 0x8000:
            flush()
        data = songs[sid].assemble(0x8000 + len(cur))
        pending.append((sid, len(cur)))
        cur += data
        if len(cur) & 1:
            cur.append(0)
        sizes.append(len(data))
    flush()
    table = bytearray([len(env_ptrs), 0])
    for p in env_ptrs:
        table += struct.pack('<H', p)
    ent = {sid: (((addr >> 15) & 0xFF), 0x8000 + off) for addr, items in banks for sid, off in items}
    for sid in range(0x81, 0xA0):
        bk, ptr = ent.get(sid, (0, 0))
        table += struct.pack('<BBH', bk, 0, ptr)
    return bytes(table), sizes
