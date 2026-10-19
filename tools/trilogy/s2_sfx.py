"""Sonic 2's sound effects on the retail S3K sound driver: the S2 sound set.

The S&K driver plays every effect from one 32 KB bank (switched in by
`ld a,<bank>` before each SFX update) through a pointer table in Z80 RAM
(z80_SFXPointers, ids $33-$DF). While a Sonic 2 zone or scene runs,
trilogy_music.c points both at the bank built here:

  - every S3K id the shared player and object code plays that Sonic 2 has a
    sound for (jump, roll, rings, springs, spikes, skid, splash, drowning,
    explosions, signpost, tally...) carries Sonic 2's (S3K_TO_S2);
  - the effects Sonic 2 has no counterpart for (elemental shields and their
    moves, Tails' flight, Knuckles' glide and climb...) keep S3K's, moved into
    this bank (S3K_KEEP);
  - every other S2 effect gets an S3K id that only S3K's own zones play,
    and Sonic 2's objects ask for S2 sound ids (o_sfx_s2) through the table
    in the blob;
  - anything else is silent: no stray sound can play in a Sonic 2 zone.

S2 effects (SMPS Z80, driver 2) share the S3K header layout and are
re-encoded by s2_music's song rules (operator order, flags, rests); PSG
channels get the songs' +12 transposition. S3K effects are copied as they
are, with their pointers moved.
"""
import struct

import s2_music
from s3k_driver import S3K_FLAG_LEN, S3K_META_LEN, SFX_PTRS, SND_BANK, z80_image, le16

FIRST, LAST = 0x33, 0xDF                # S3K SFX ids (z80_SFXPointers)
S2_FIRST, S2_LAST = 0xA0, 0xF0          # SndID_Jump .. SndID_OilSlide

# S3K id -> S2 SndID, for the sounds S3K's shared code plays in an S2 zone
S3K_TO_S2 = {
    0x33: 0xB5, 0x34: 0xCE,             # rings (right / left)
    0x35: 0xA3,                         # death -> Hurt
    0x36: 0xA4,                         # skid
    0x37: 0xA6,                         # spike hit -> HurtBySpikes
    0x38: 0xAD,                         # air bubble -> InhalingBubble
    0x39: 0xAA, 0x6C: 0xAA,             # splash
    0x3A: 0xAF,                         # shield
    0x3B: 0xB2,                         # drown
    0x3C: 0xBE,                         # roll
    0x3D: 0xC1,                         # Obj_Explosion (badniks, monitors) -> Explosion
    0x52: 0xB6,                         # spike move -> SpikesMove
    0x5B: 0xCD,                         # switch (results tick) -> Blip
    0x62: 0xA0,                         # jump
    0x63: 0xA1,                         # star post -> Checkpoint
    0x6E: 0xAC,                         # boss hit
    0x70: 0xAE,                         # lava ball
    0x9F: 0xDF,                         # super transform
    0xA9: 0xC2,                         # air countdown ding -> WaterWarning
    0xAA: 0xB4,                         # bumper
    0xAB: 0xE0,                         # spindash rev
    0xAC: 0xBF,                         # continue
    0xAE: 0xE3,                         # flipper
    0xAF: 0xCA,                         # enter special stage
    0xB0: 0xC5,                         # register (tally end)
    0xB1: 0xCC,                         # spring
    0xB2: 0xED,                         # error
    0xB3: 0xC3,                         # giant ring
    0xB4: 0xC4,                         # big explosion -> BossExplosion
    0xB6: 0xBC,                         # dash -> SpindashRelease
    0xB8: 0xCF,                         # signpost
    0xB9: 0xC6,                         # ring loss -> RingSpill
}
# S3K effects with no Sonic 2 counterpart that can play in an S2 zone
S3K_KEEP = [0x3E, 0x3F, 0x40, 0x41,                 # fire / bubble / lightning shields
            0x42, 0x43, 0x44, 0x45,                 # insta-shield, shield moves
            0x49, 0x4A, 0x4C, 0x7E,                 # Knuckles: thump, grab, glide land, slide
            0xBA, 0xBB, 0xBC, 0xC8,                 # Tails flying / tired, slide skids
            0x4D, 0x57]                             # capsule projectile, bubbler geyser


class S2Sfx(s2_music.S2Song):
    """One S2 sound effect, addressed from its bank's Z80 window."""

    def __init__(self, data, base, hdr=0):          # (S2Song's walk/encode, an SFX header at offset hdr)
        self.d, self.base = data, base
        self.voice_ptr = self._lw(hdr)
        self.div = data[hdr + 2]
        n = data[hdr + 3]
        self.ins, self.ctx = {}, {}
        self.max_voice = -1
        self.chans = []
        for i in range(n):
            a = hdr + 4 + i * 6
            ch = data[a + 1]
            self.chans.append(('psg' if ch & 0x80 else 'fm', data[a], ch, self._lw(a + 2), data[a + 4], data[a + 5]))
        # nMaxPSG in noise mode is $C6 for S2's driver, $C7 for S3K's: effects
        # spell it against their header transposition ((nMaxPSG-$F8)&$FF with
        # $F8, Leaves), so the note that lands on it moves up one
        self.keep_nmax, self.tr_at = True, {}
        for kind, _, _, ptr, tr, _ in self.chans:
            seen = set(self.ins)
            self._walk(ptr, kind)
            for a in set(self.ins) - seen:
                self.tr_at[a] = tr

    def _encode(self, addr):
        raw, typ, _ = self.ins[addr]
        b = raw[0]
        if typ == 'byte' and self.ctx[addr] == 'psg' and 0x81 <= b < 0xE0 and self.noise.get(addr) \
                and (b + self.tr_at.get(addr, 0)) & 0xFF == s2_music.NMAXPSG_S2:
            return bytes([b + 1]), None
        return super()._encode(addr)

    def assemble(self, base):
        hdr_len = 4 + len(self.chans) * 6
        body, newaddr, fixups = bytearray(), {}, []
        # S3K's stop keys off only if 'do not attack' is clear, and an E7 read
        # in the same fetch as the stop (PreArrowFiring's last loop) leaves it
        # set: the note would ring on. A 1-frame rest first gives the stop a
        # fetch of its own, as S2's stop always keys off.
        held = any(self._encode(a)[0][:1] == b'\xE7' for a in self.ins)
        for a in sorted(self.ins):
            newaddr[a] = base + hdr_len + len(body)
            enc, fix = self._encode(a)
            if held and enc == b'\xF2':
                enc = b'\x80\x01\xF2'
            if fix:
                fixups.append((len(body) + fix[0], fix[1]))
            body += enc
        for off, tgt in fixups:
            struct.pack_into('<H', body, off, newaddr[tgt])
        voices = bytearray()
        vo = self._off(self.voice_ptr)
        for v in range(self.max_voice + 1):
            vc = s2_music.s2_voice_to_s3(self.d[vo + v * 25:vo + v * 25 + 25])
            alg = vc[0] & 7
            masks = [(alg == 7) << 7, (alg >= 4) << 7, (alg >= 5) << 7, 0x80]
            for i in range(4):
                vc[21 + i] = (vc[21 + i] & 0x7F) | masks[i]
            voices += vc
        voice_addr = base + hdr_len + len(body) if voices else 0
        hdr = bytearray(struct.pack('<H', voice_addr)) + bytes([self.div, len(self.chans)])
        for kind, flags, ch, ptr, tr, vol in self.chans:
            if kind == 'psg':
                tr = (tr + 12) & 0xFF
            hdr += bytes([flags, ch]) + struct.pack('<H', newaddr[ptr]) + bytes([tr, vol])
        return bytes(hdr + body + voices)


class S3kSfx:
    """One S3K sound effect from SndBank, re-placed as it is (pointers moved)."""

    def __init__(self, win, ptr):
        self.w = win
        o = ptr - 0x8000
        self.voice_ptr = le16(win, o)
        self.div, n = win[o + 2], win[o + 3]
        self.chans = [bytes(win[o + 4 + i * 6:o + 10 + i * 6]) for i in range(n)]
        self.ins, self.max_voice = {}, -1
        for c in self.chans:
            self._walk(c[2] | c[3] << 8)

    def _walk(self, ptr):
        todo = [ptr]
        while todo:
            p = todo.pop()
            while p not in self.ins:
                o = p - 0x8000
                b = self.w[o]
                if b < 0xE0:
                    self.ins[p] = (bytes([b]), None)
                    p += 1
                    continue
                n = 2 + S3K_META_LEN.get(self.w[o + 1], 0) if b == 0xFF else 1 + S3K_FLAG_LEN[b]
                raw = bytes(self.w[o:o + n])
                at = {0xF6: 1, 0xFC: 1, 0xF8: 1, 0xF7: 3, 0xEB: 2}.get(b)
                tgt = (raw[at] | raw[at + 1] << 8) if at else None
                if b == 0xEF:
                    self.max_voice = max(self.max_voice, raw[1] & 0x7F)
                self.ins[p] = (raw, (at, tgt) if at else None)
                if tgt is not None:
                    todo.append(tgt)
                if b in (0xF6, 0xFC, 0xF2, 0xE3, 0xF9):
                    break
                p += n

    def assemble(self, base):
        hdr_len = 4 + len(self.chans) * 6
        body, newaddr, fix = bytearray(), {}, []
        for a in sorted(self.ins):
            newaddr[a] = base + hdr_len + len(body)
            raw, f = self.ins[a]
            if f:
                fix.append((len(body) + f[0], f[1]))
            body += raw
        for off, tgt in fix:
            struct.pack_into('<H', body, off, newaddr[tgt])
        voices = bytes(self.w[self.voice_ptr - 0x8000:self.voice_ptr - 0x8000 + (self.max_voice + 1) * 25]) \
            if self.max_voice >= 0 and self.voice_ptr >= 0x8000 else b''
        voice_addr = base + hdr_len + len(body) if voices else 0
        hdr = bytearray(struct.pack('<H', voice_addr)) + bytes([self.div, len(self.chans)])
        for c in self.chans:
            hdr += c[:2] + struct.pack('<H', newaddr[c[2] | c[3] << 8]) + c[4:]
        return bytes(hdr + body + voices)


def code_sfx_ids(from_data=None):
    """S3K ids the trilogy's own C code plays (o_sfx, SFX_* values) outside Sonic 2's
    objects: never handed to an S2 effect, so in an S2 zone they are S2's or silent."""
    import re
    from pathlib import Path
    src = Path(__file__).resolve().parents[2] / 'game' / 'trilogy'
    if from_data or (from_data is None and not src.is_dir()):
        p = Path(__file__).resolve().parent / 'data' / 'code_sfx.txt'
        return {int(l, 16) for l in p.read_text().split()}
    ids = set()
    for f in (Path(__file__).resolve().parents[2] / 'game' / 'trilogy').glob('*.c'):
        if f.name.startswith('s2_obj_') or f.name == 'trilogy_s2ss.c':
            continue
        t = f.read_text(encoding='utf-8', errors='replace')
        ids |= {int(x, 16) for x in re.findall(r'o_sfx\((0x[0-9A-Fa-f]+)\)', t)}
        if 'o_sfx(SFX_' in t:
            ids |= {int(x, 16) for x in re.findall(r'#define\s+SFX_\w+\s+(0x[0-9A-Fa-f]+)', t)}
    return ids


def build(s2rom, s3krom, bank, sym, envs=()):
    """-> (table blob, bank bytes used, {S2 SndID: S3K id}). Blob: u8 bank byte, then a
    u16 LE Z80 pointer per S3K id $33-$DF, then a u8 S3K id per S2 SndID $A0-$F0."""
    s2bank = sym['SoundIndex'] & ~0x7FFF
    w2 = s2rom[s2bank:s2bank + 0x8000]
    si = sym['SoundIndex'] & 0x7FFF
    ram = z80_image(s3krom)
    w3 = s3krom[SND_BANK:SND_BANK + 0x8000]

    # S2's PSG volume envelopes first, where every S2 song bank has them
    # (s2_music.build_music): the driver reads an SFX's envelope with this
    # bank switched in, and a garbage one can loop forever
    out = bytearray(b''.join(envs))
    if len(out) & 1:
        out.append(0)
    ptr = {}
    silent = 0x8000 + len(out)                   # one PSG3 track that stops at once
    out += bytes([0, 0, 1, 1, 0x80, 0xC0]) + struct.pack('<H', silent + 10) + bytes([0, 0]) + b'\xF2\x00'
    s2_at = {}                                   # S2 SndID -> Z80 pointer
    for sid in range(S2_FIRST, S2_LAST + 1):
        p = le16(w2, si + (sid - S2_FIRST) * 2)
        data = S2Sfx(w2, 0x8000, p - 0x8000).assemble(0x8000 + len(out))
        s2_at[sid] = 0x8000 + len(out)
        out += data
        if len(out) & 1:
            out.append(0)
    for k in S3K_KEEP:
        p = le16(ram, SFX_PTRS + (k - FIRST) * 2)
        ptr[k] = 0x8000 + len(out)
        out += S3kSfx(w3, p).assemble(0x8000 + len(out))
        if len(out) & 1:
            out.append(0)
    if len(out) > 0x8000:
        raise SystemExit(f"S2 sound set: {len(out)} bytes do not fit a bank")
    for k, s in S3K_TO_S2.items():
        ptr[k] = s2_at[s]
    s2_id = {}
    for k, s in sorted(S3K_TO_S2.items(), reverse=True):
        s2_id[s] = k
    used = code_sfx_ids()
    free = [k for k in range(0x46, 0xDC) if k not in ptr and k not in used]   # ids only S3K's own zones play
    for s in range(S2_FIRST, S2_LAST + 1):
        if s in s2_id:
            continue
        k = free.pop(0)
        ptr[k] = s2_at[s]
        s2_id[s] = k
    addr = bank.add(bytes(out), align=0x8000, dedupe=False)
    blob = bytearray([(addr >> 15) & 0xFF])
    for k in range(FIRST, LAST + 1):
        blob += struct.pack('<H', ptr.get(k, silent))
    for s in range(S2_FIRST, S2_LAST + 1):
        blob.append(s2_id.get(s, 0))
    return bytes(blob), len(out), s2_id
