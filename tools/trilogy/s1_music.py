"""Sonic 1 music (SMPS 68k) -> S3K driver (SMPS Z80, S&K driver) songs.

Reads the songs from the player's Sonic 1 cartridge and re-assembles them
for the retail S3K Z80 driver, following the cross-driver rules documented
by SMPS2ASM (SonicDriverVer 1 -> 4):
  - header pointers become little-endian Z80 bank-window addresses;
  - main tempo is converted (s1TempotoS3), PSG transposition +12;
  - coordination flags are re-encoded (return F9, transposition FB,
    tempo FF00/FF04, modulation F0 params, mod on/off F4 80 / FA, ...);
  - FM voices get their TL carrier bits from the algorithm (SMPS Z80 reads
    the bits, SMPS 68k ignores them);
  - DAC samples map to the S3K set;
  - a bare duration after a rest gets its rest note written out (S1's driver
    zeroes the frequency on a rest; S3K's keeps it and would sound it), and
    the noise channel's nMaxPSG moves from nA5 to nBb5;
  - S1's PSG volume envelopes 1-9 become S3K envelopes 30-38 (unused by
    every S3K SFX); their data sits at the start of each music bank, so the
    driver's pointers to them are the same for every S1 song.

A music bank is one 32 KB-aligned blob (the Z80 window), holding the
envelopes followed by as many songs as fit.
"""
import struct

S1_MUSIC_INDEX = 0x071A9C            # MusicIndex: 19 longs, songs $81..$93
S1_PSG_ENV = [0x719CC, 0x719E3, 0x719EA, 0x719FB, 0x71A15, 0x71A06, 0x71A3F, 0x71A5B, 0x71A83]
# S3K envelope ids for S1 envelopes 1-9: slots no S3K SFX or trilogy-level jingle uses
ENV_SLOTS = [0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x12]

# S1 coordination flag parameter counts (SMPS 68k, Sonic 1)
S1_FLAG_LEN = {0xE0: 1, 0xE1: 1, 0xE2: 1, 0xE3: 0, 0xE4: 0, 0xE5: 1, 0xE6: 1, 0xE7: 0,
               0xE8: 1, 0xE9: 1, 0xEA: 1, 0xEB: 1, 0xEC: 1, 0xED: 0, 0xEE: 0, 0xEF: 1,
               0xF0: 4, 0xF1: 0, 0xF2: 0, 0xF3: 1, 0xF4: 0, 0xF5: 1, 0xF6: 2, 0xF7: 4,
               0xF8: 2, 0xF9: 0}
END_FLAGS = (0xE3, 0xE4, 0xEE, 0xF2, 0xF6)

# S1 DAC sample -> S3K (S&K driver) sample
DAC_MAP = {0x81: 0x86,   # kick -> dKickS3
           0x82: 0x81,   # snare -> dSnareS3
           0x83: 0x9A,   # timpani -> dMidTimpaniS3
           0x88: 0x98, 0x89: 0x9A, 0x8A: 0x99, 0x8B: 0x99}   # hi/mid/low/v.low timpani


def be16(b, a):
    return (b[a] << 8) | b[a + 1]


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def conv0to256(n):
    return 256 if n == 0 else n


def s1_tempo_to_s3(n):
    if n == 1:
        raise ValueError("main tempo of 1")
    n = conv0to256(n)
    s2 = ((((n - 1) << 8) + (n >> 1)) // n) & 0xFF
    return (0x100 - ((s2 == 0) | s2)) & 0xFF


class Song:
    """One S1 song decoded into instructions keyed by ROM address."""

    def __init__(self, rom, start):
        self.rom = rom
        self.start = start
        self.voice_ptr = start + be16(rom, start)
        self.nfm, self.npsg = rom[start + 2], rom[start + 3]
        self.div, self.tempo = rom[start + 4], rom[start + 5]
        self.ins = {}             # addr -> (bytes, kind, target or None)
        self.ctx = {}             # addr -> 'dac' | 'fm' | 'psg'
        self.cdiv = {}            # addr -> channel tempo divider in effect (E5)
        self.max_voice = -1
        self.chans = []           # (kind, track addr, header extra bytes)
        a = start + 6
        self.chans.append(('dac', start + be16(rom, a), rom[a + 2:a + 4]))
        a += 4
        for _ in range(self.nfm - 1):
            self.chans.append(('fm', start + be16(rom, a), rom[a + 2:a + 4]))
            a += 4
        for _ in range(self.npsg):
            self.chans.append(('psg', start + be16(rom, a), rom[a + 2:a + 6]))
            a += 6
        for kind, ptr, _ in self.chans:
            self._walk(ptr, kind)
        # E5 (per-channel divider) does not exist in the S&K driver: such
        # songs run at divider 1 with every duration scaled by its channel's
        # divider instead.
        self.uses_e5 = any(r[0] == 0xE5 for r, _, _ in self.ins.values())

    def _walk(self, ptr, kind):
        rom = self.rom
        if not hasattr(self, 'noise'):
            self.noise, self.rest_dur = {}, set()
        todo = [(ptr, self.div, False, False)]
        while todo:
            p, cdiv, noise, resting = todo.pop()
            after_note = False
            while p not in self.ins:
                b = rom[p]
                self.ctx[p] = kind
                self.cdiv[p] = cdiv
                self.noise[p] = noise
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
                n = 1 + S1_FLAG_LEN[b]
                raw = bytes(rom[p:p + n])
                tgt = None
                if b in (0xF6, 0xF8):
                    tgt = p + 1 + 1 + s16(be16(rom, p + 1))
                elif b == 0xF7:
                    tgt = p + 3 + 1 + s16(be16(rom, p + 3))
                if b == 0xEF:
                    self.max_voice = max(self.max_voice, raw[1])
                if b == 0xE5:
                    cdiv = raw[1]
                if b == 0xF3:
                    noise = (raw[1] & 0xF0) == 0xE0
                self.ins[p] = (raw, 'flag', tgt)
                if tgt is not None:
                    todo.append((tgt, cdiv, noise, resting))
                if b in END_FLAGS:
                    break
                p += n

    # ---- re-encoding ---------------------------------------------------
    def _encode_dur(self, addr, b):
        d = b * self.cdiv[addr]
        out = bytearray()
        while d > 0x7F:
            out += bytes([0x7F, 0xE7])
            d -= 0x7F
        out.append(d)
        return bytes(out), None

    def _encode(self, addr):
        raw, typ, tgt = self.ins[addr]
        kind = self.ctx[addr]
        b = raw[0]
        if typ == 'byte':
            if kind == 'dac' and 0x81 <= b < 0xE0:
                return bytes([DAC_MAP.get(b, b)]), None
            if kind == 'psg' and b == 0xC6 and self.noise.get(addr):   # nMaxPSG: nA5 -> nBb5
                return b'\xC7', None
            if addr in self.rest_dur:                   # S3K's driver drops the rest flag on a bare duration
                if self.uses_e5:
                    enc, _ = self._encode_dur(addr, b)
                    return b'\x80' + enc, None
                return bytes([0x80, b]), None
            if b < 0x80 and self.uses_e5:
                d = b * self.cdiv[addr]
                out = bytearray()
                while d > 0x7F:
                    out += bytes([0x7F, 0xE7])
                    d -= 0x7F
                out.append(d)
                return bytes(out), None
            return raw, None
        p = raw[1:]
        if b in (0xE0, 0xE1, 0xE2, 0xE6, 0xE7, 0xEC, 0xEF, 0xF2, 0xF3):
            return raw, None
        if b == 0xE3:
            return b'\xF9', None
        if b == 0xE4:
            return b'\xE2\xFF\xF2', None
        if b == 0xE8:
            v = p[0]
            div = 1 if self.uses_e5 else self.div
            return bytes([0xE8, (max(1, v // div) if v else 0) & 0xFF]), None
        if b == 0xE9:
            return bytes([0xFB, p[0]]), None
        if b == 0xEA:
            return bytes([0xFF, 0x00, s1_tempo_to_s3(p[0])]), None
        if b == 0xEB:
            return bytes([0xFF, 0x04, p[0]]), None
        if b == 0xED:
            return b'', None
        if b == 0xEE:
            return b'\xF2', None
        if b == 0xF0:
            wait, speed, change, step = p
            return bytes([0xF0, (wait + 1) & 0xFF, speed, change, ((step + 1) * speed) & 0xFF]), None
        if b == 0xF1:
            return b'\xF4\x80', None
        if b == 0xF4:
            return b'\xFA', None
        if b == 0xF5:
            v = p[0]
            return bytes([0xF5, ENV_SLOTS[v - 1] if 1 <= v <= 9 else v]), None
        if b in (0xF6, 0xF8):
            return bytes([b, 0, 0]), (1, tgt)
        if b == 0xF7:
            return bytes([0xF7, p[0], p[1], 0, 0]), (3, tgt)
        if b == 0xF9:
            return b'\xEE\x88\x0F\xEE\x8C\x0F', None
        if b == 0xE5:
            return b'', None
        raise ValueError(f"unhandled S1 flag {b:02X}")

    def assemble(self, base):
        """Bytes of the converted song placed at Z80 address base."""
        nvoices = self.max_voice + 1
        hdr_len = 6 + 4 + (self.nfm - 1) * 4 + self.npsg * 6
        body = bytearray()
        newaddr = {}
        fixups = []
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
        for v in range(nvoices):
            vc = bytearray(self.rom[self.voice_ptr + v * 25:self.voice_ptr + v * 25 + 25])
            alg = vc[0] & 7
            masks = [(alg == 7) << 7, (alg >= 4) << 7, (alg >= 5) << 7, 0x80]   # TL4, TL3, TL2, TL1
            for i in range(4):
                vc[21 + i] = (vc[21 + i] & 0x7F) | masks[i]
            voices += vc
        voice_addr = base + hdr_len + len(body)
        hdr = bytearray(struct.pack('<H', voice_addr))
        hdr += bytes([self.nfm, self.npsg, 1 if self.uses_e5 else self.div, s1_tempo_to_s3(self.tempo)])
        for kind, ptr, extra in self.chans:
            hdr += struct.pack('<H', newaddr[ptr])
            if kind == 'dac':
                hdr += bytes(extra)
            elif kind == 'fm':
                hdr += bytes(extra)
            else:
                pitch, vol, _mod, env = extra
                hdr += bytes([(pitch + 12) & 0xFF, vol, 0,
                              ENV_SLOTS[env - 1] if 1 <= env <= 9 else env])
        return bytes(hdr + body + voices)


def s1_envelopes(rom):
    """S1 PSG envelopes, terminator $80 (hold) -> S3K $81 (hold)."""
    out = []
    for a in S1_PSG_ENV:
        e = bytearray()
        while rom[a] < 0x80:
            e.append(rom[a])
            a += 1
        e.append(0x81)
        out.append(bytes(e))
    return out


def build_music(rom, bank):
    """Add S1 songs to the expansion bank. Returns a table blob:
    9 x u16 LE envelope Z80 pointers, then per song $81..$93:
    u8 bank, u8 0, u16 LE Z80 pointer."""
    envs = s1_envelopes(rom)
    env_blob = bytearray()
    env_ptrs = []
    for e in envs:
        env_ptrs.append(0x8000 + len(env_blob))
        env_blob += e
    if len(env_blob) & 1:
        env_blob.append(0)
    songs = [be32 for be32 in (struct.unpack_from('>I', rom, S1_MUSIC_INDEX + i * 4)[0] for i in range(19))]
    entries = []
    cur = bytearray(env_blob)
    pending = []                              # (song index, offset in cur)
    banks = []

    def flush():
        nonlocal cur, pending
        if not pending:
            return
        addr = bank.add(bytes(cur), align=0x8000, dedupe=False)
        banks.append((addr, list(pending)))
        cur = bytearray(env_blob)
        pending = []

    converted = []
    for i, start in enumerate(songs):
        song = Song(rom, start)
        data = song.assemble(0x8000 + len(cur))
        if len(cur) + len(data) > 0x8000:
            flush()
            data = song.assemble(0x8000 + len(cur))
        pending.append((i, len(cur)))
        cur += data
        if len(cur) & 1:
            cur.append(0)
        converted.append(len(data))
    flush()
    table = bytearray([len(env_ptrs), 0])
    for p in env_ptrs:
        table += struct.pack('<H', p)
    ent = {}
    for addr, items in banks:
        for i, off in items:
            ent[i] = (((addr >> 15) & 0xFF), 0x8000 + off)
    for i in range(19):
        bk, ptr = ent[i]
        table += struct.pack('<BBH', bk, 0, ptr)
    return bytes(table), converted
