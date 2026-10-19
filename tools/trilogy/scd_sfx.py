"""Sonic CD's FM sound effects on the retail S3K sound driver: the Sonic CD sound set.

Sonic CD plays its FM effects through a Z80 driver of its own (SMPS Z80, FM
only: six FM tracks, no PSG, no music; dobby: Sound Drivers/FM), which the
Mega Drive init program (MDINIT.MMD; dobby: Mega Drive Init) uploads once and
every level, the time warp and the special stage keep using. It is found on
the player's disc by its upload loop (lea FMSFXDriver(pc),a0; lea Z80RAM,a1;
move.w #size-1,d7; move.b (a0)+,(a1)+; dbf d7) and read through its own
DriverInfo table (GetList at $0008: first id $90, the SFX index).

The effects ($90-$DF) are re-encoded for the S&K driver. Headers, notes, the
frequency table, voices (operators in S3K's order, carriers marked by TL bit
7, as S3K's), modulation and loops are the same SMPS; the rest:
  - E2 (the 68000's comm byte), EC (ring channel reset), F3 (ring channel
    swap) are dropped: S3K's driver sends every other $33 (ring) to $34
    itself, which carry CD's right ($95) and left ($A8) rings;
  - E4, EA, F4, F5 (stop) -> F2; F1 (modulation off) -> FA; E5 (loop exit)
    -> EB; a stop that can come in the same fetch as an E7 gets a 1-frame
    rest first (s2_sfx's rule: S3K's stop keys off only without E7);
  - EB (jump while the driver's jumpCondition is clear: a note held until
    Sonic's death sets it, or another effect takes the channel; $A6, $C4,
    $D1) -> at most 255 frames of the hold (F7, a loop index the effect
    leaves free), then the effect's own release;
  - S3K's driver plays effects on FM3-FM6 only: CD's FM1 tracks go to FM3,
    FM2's to FM5 (no effect but the time warp's mixes FM1/FM2 with FM3/FM5).
    The time warp ($D8) has six: three voices, each a lead in one speaker
    and the same 3-5 frames late in the other; its two FM1/FM2 leads keep
    FM4 and FM3, centred, and their late copies go, the third pair stays;
  - header entries that are not tracks ($D5, $D6 count one track too many)
    are dropped.
The disc's charge sound ($9C: a rising note held until the player's own
code sends $AB, charge stop, which S3K's never does) plays for S3K's spindash
rev as a variant that lets go of the note after 60 frames (its endless jump
an F7 loop, then a stop); the game's own $9C is the disc's. S3K's roll has no
sound: Sonic CD's has none (only the Japanese roll tunnels play $9C).

The bank then holds, as s2_sfx's S2 set (trilogy_music.c swaps it in while a
Sonic CD level, its time warp or its special stage runs):
  - Sonic CD's sound at every S3K id S3K's shared player and object code
    plays in a Sonic CD level (S3K_TO_CD);
  - S3K's own sound for what Sonic CD has none of (shields, Tails' and
    Knuckles' moves, Super Sonic: S3K_KEEP);
  - every other CD effect at an id only S3K's own zones play: the game's
    Sonic CD objects ask for CD ids (o_sfx_cd) through the table;
  - silence for anything else.

    build(disc_dir, s3k_rom, addr) -> (bank (32 KB), table blob, {CD id: S3K id})
table blob: u8 bank byte (68k addr >> 15), u16 LE Z80 pointer per S3K id
$33-$DF, u8 S3K id per CD id $90-$DF (0: none). Nothing read here goes into
the repository.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from s2_sfx import FIRST, LAST, S3kSfx, code_sfx_ids  # noqa: E402
from s3k_driver import SFX_PTRS, SND_BANK, le16, z80_image  # noqa: E402
from scd_mmd import scan  # noqa: E402

CD_FIRST, CD_LAST = 0x90, 0xDF          # FM_START .. FM_DF (_Sound IDs.i)
CD_VARIABLES = 0x1C00                   # the driver's variables (DriverInfo's last entry)
# Mega Drive Init: lea FMSFXDriver(pc),a0; lea Z80RAM,a1; move.w #size-1,d7;
# .LoadDriver: move.b (a0)+,(a1)+; dbf d7,.LoadDriver
UPLOAD_SIG = '41FA (W) 43F9 00A0 0000 3E3C (W) 12D8 51CF FFFC'

# Sonic CD driver coordination flags: parameter bytes (TrackCommand reads the
# first parameter ahead; parameterless handlers put it back)
CD_FLAG_LEN = {0xE0: 1, 0xE1: 1, 0xE2: 1, 0xE3: 0, 0xE4: 0, 0xE5: 3, 0xE6: 1, 0xE7: 0,
               0xE8: 1, 0xE9: 2, 0xEA: 0, 0xEB: 2, 0xEC: 1, 0xED: 2, 0xEE: 2, 0xEF: 1,
               0xF0: 4, 0xF1: 0, 0xF2: 0, 0xF3: 0, 0xF4: 0, 0xF5: 0, 0xF6: 2, 0xF7: 4,
               0xF8: 2, 0xF9: 0, 0xFA: 1, 0xFB: 1, 0xFC: 1, 0xFD: 1, 0xFE: 4}
CD_STOPS = (0xE4, 0xEA, 0xF2, 0xF4, 0xF5)
CD_ENDS = CD_STOPS + (0xE3, 0xF6, 0xF9)
HOLD_FRAMES = 0xFF                      # EB's hold, at most (frames)

# CD channel -> S3K SFX channel (FM3 2, FM4 4, FM5 5, FM6 6; None: dropped)
CHANNELS = {0: 2, 1: 5, 2: 2, 4: 4, 5: 5, 6: 6}
CHANNELS_SFX = {0xD8: {0: 4, 1: 2, 2: None, 4: None, 5: 5, 6: 6}}
CENTRE = {0xD8: (0, 1)}                 # CD channels whose own pans become centred

# S3K id -> Sonic CD FM id, for the sounds S3K's shared code plays in a CD level
S3K_TO_CD = {
    0x33: 0x95, 0x34: 0xA8,             # rings (right / left: FM_RING, FM_RINGL)
    0x35: 0x93, 0x37: 0x93,             # hurt / death, spikes -> FM_HURT
    0x36: 0x90,                         # skid -> FM_SKID
    0x38: 0xAD,                         # air bubble (Tidal Tempest's, AWA.C)
    0x39: 0xA2, 0x6C: 0xA2,             # splash (exit2_init's water splash)
    0x3A: 0x97,                         # shield -> FM_SHIELD
    0x3B: 0xB6,                         # drown (plawamaster)
    0x3D: 0x96,                         # Obj_Explosion (badniks, monitors) -> FM_DESTROY
    0x52: 0xB7,                         # spike move (the retracting spikes')
    0x5B: 0xBD,                         # switch (results tick) -> FM_TALLY
    0x62: 0x92,                         # jump -> FM_JUMP
    0x63: 0xAE,                         # star post -> FM_CHECKPOINT
    0x6E: 0xAC,                         # boss hit
    0xA9: 0xDF,                         # air countdown ding (plawamaster's warning)
    0xAB: (0x9C, 30),                   # spindash rev -> the charge sound, held 30 x 2 frames at most
    0xAF: 0xC8,                         # enter special stage -> FM_SSWARP
    0xB0: 0x9A,                         # register (tally end) -> FM_KACHING
    0xB1: 0x98,                         # spring -> FM_SPRING
    0xB3: 0xAF,                         # giant ring -> FM_BIGRING
    0xB4: 0x9E,                         # big explosion -> FM_EXPLODE
    0xB6: 0x91,                         # dash (spindash release) -> the charge release
    0xB8: 0x9D,                         # signpost -> FM_SIGNPOST
    0xB9: 0x94,                         # ring loss -> FM_RINGLOSS
}
# S3K effects with no Sonic CD counterpart that can play in a CD level
S3K_KEEP = [0x3E, 0x3F, 0x40, 0x41,                 # fire / bubble / lightning shields
            0x42, 0x43, 0x44, 0x45,                 # insta-shield, shield moves
            0x49, 0x4A, 0x4C, 0x7E,                 # Knuckles: thump, grab, glide land, slide
            0xBA, 0xBB, 0xBC, 0xC8,                 # Tails flying / tired, slide skids
            0x4D, 0x57,                             # capsule projectile, bubbler geyser
            0x9F]                                   # Super Sonic


def find_driver(disc):
    """-> the Z80 image of Sonic CD's FM driver (Z80 $0000..), from the disc's files"""
    disc = Path(disc)
    names = [disc / 'MDINIT.MMD'] + sorted(p for p in disc.glob('*.MMD') if p.name.upper() != 'MDINIT.MMD')
    for p in names:
        if not p.is_file():
            continue
        d = p.read_bytes()
        for off, (disp, n), _ in scan(d, UPLOAD_SIG):
            z = d[off + 2 + (disp - 0x10000 if disp & 0x8000 else disp):][:n + 1]
            if len(z) == n + 1 and z[:4] == b'\xF3\xF3\xED\x56' and z[8] == 0x21:
                info = le16(z, 9)                    # GetList: ld hl,DriverInfo
                if le16(z, info + 12) == CD_FIRST and le16(z, info + 16) == CD_VARIABLES:
                    return z
    raise SystemExit(f"{disc}: Sonic CD's FM sound driver not found (MDINIT.MMD)")


class CdSfx:
    """One Sonic CD FM effect, re-encoded for the S&K driver."""

    def __init__(self, z, sid, bound=0):
        info = le16(z, 9)
        self.bound = bound                       # (a variant: each endless F6 loop runs this many times, then stops)
        p = le16(z, le16(z, info + 6) + (sid - CD_FIRST) * 2)
        self.z, self.sid = z, sid
        self.voice_ptr, self.div = le16(z, p), z[p + 2]
        chmap = CHANNELS_SFX.get(sid, CHANNELS)
        self.tracks = []                             # (CD channel, S3K channel, data, transpose, volume)
        for i in range(z[p + 3]):
            a = p + 4 + i * 6
            if z[a] != 0x80 or z[a + 1] not in CHANNELS:
                continue                             # (not a track: $D5 / $D6's extra entry)
            if chmap.get(z[a + 1]) is not None:
                self.tracks.append((z[a + 1], chmap[z[a + 1]], le16(z, a + 2), z[a + 4], z[a + 5]))
        if len({t[1] for t in self.tracks}) != len(self.tracks):
            raise ValueError(f"Sonic CD FM ${sid:02X}: two tracks on one S3K channel")
        self.ins, self.max_voice, self.reach = {}, -1, {}
        for t in self.tracks:
            self.reach[t[0]] = self._walk(t[2])
        self.loops = {raw[1] for raw, _ in self.ins.values() if raw[0] in (0xF7, 0xE5)}
        self.stop_rest = self._stops_after_e7()
        mine = set()
        for ch in CENTRE.get(sid, ()):
            if ch in self.reach:
                others = set().union(*(r for c, r in self.reach.items() if c != ch))
                mine |= {a for a in self.reach[ch] - others if self.ins[a][0][0] == 0xE0}
        self.centre = mine

    def _walk(self, ptr):
        z, seen, todo = self.z, set(), [ptr]
        while todo:
            p = todo.pop()
            while p not in seen:
                seen.add(p)
                b = z[p]
                if b < 0xE0:
                    self.ins[p] = (bytes([b]), None)
                    p += 1
                    continue
                if b == 0xFF or b not in CD_FLAG_LEN:
                    raise ValueError(f"Sonic CD FM ${self.sid:02X}: flag {b:02X} at ${p:04X} has no S3K form")
                n = 1 + CD_FLAG_LEN[b]
                raw = bytes(z[p:p + n])
                at = {0xF6: 1, 0xF8: 1, 0xEB: 1, 0xF7: 3, 0xE5: 2}.get(b)
                tgt = raw[at] | raw[at + 1] << 8 if at else None
                if b == 0xEF:
                    self.max_voice = max(self.max_voice, raw[1] & 0x7F)
                self.ins[p] = (raw, tgt)
                if tgt is not None:
                    todo.append(tgt)
                if b in CD_ENDS:
                    break
                p += n
        return seen

    def _stops_after_e7(self):
        """stops reached in the same fetch as an E7 (no note or duration between)"""
        out, seen = set(), set()
        todo = [(t[2], False) for t in self.tracks]
        while todo:
            p, held = todo.pop()
            while (p, held) not in seen:
                seen.add((p, held))
                raw, tgt = self.ins[p]
                b = raw[0]
                if b < 0xE0:
                    held = False
                elif b == 0xE7:
                    held = True
                elif b in CD_STOPS and held:
                    out.add(p)
                if tgt is not None:
                    todo.append((tgt, held))
                if b in CD_ENDS and b >= 0xE0:
                    break
                p += len(raw)
        return out

    def _encode(self, a):
        raw, tgt = self.ins[a]
        b, p = raw[0], raw[1:]
        if b < 0xE0:
            return raw, None
        if b in (0xE2, 0xEC, 0xF3):
            return b'', None
        if b in CD_STOPS:
            return (b'\x80\x01\xF2' if a in self.stop_rest else b'\xF2'), None
        if b == 0xE0:
            return bytes([0xE0, 0xC0 | (p[0] & 0x3F) if a in self.centre else p[0]]), None
        if b in (0xE1, 0xE3, 0xE6, 0xE7, 0xE8, 0xEE, 0xF0, 0xF9, 0xFB):
            return raw, None
        if b == 0xEF:
            if p[0] & 0x80:
                raise ValueError(f"Sonic CD FM ${self.sid:02X}: voice {p[0]:02X}")
            return raw, None
        if b == 0xF1:
            return b'\xFA', None
        if b == 0xF6 and self.bound and tgt <= a:
            free = [i for i in (1, 0) if i not in self.loops]
            return bytes([0xF7, free[0], self.bound, 0, 0, 0xF2]), (3, tgt)
        if b in (0xF6, 0xF8):
            return bytes([b, 0, 0]), (1, tgt)
        if b == 0xF7:
            return bytes([0xF7, p[0], p[1], 0, 0]), (3, tgt)
        if b == 0xE5:
            return bytes([0xEB, p[0], 0, 0]), (2, tgt)
        if b == 0xEB:
            free = [i for i in (1, 0) if i not in self.loops]
            if not free:
                raise ValueError(f"Sonic CD FM ${self.sid:02X}: no loop index left for its hold")
            return bytes([0xF7, free[0], HOLD_FRAMES, 0, 0]), (3, tgt)
        raise ValueError(f"Sonic CD FM ${self.sid:02X}: flag {b:02X} has no S3K form")

    def assemble(self, base):
        hdr_len = 4 + len(self.tracks) * 6
        body, newaddr, fixups = bytearray(), {}, []
        for a in sorted(self.ins):
            newaddr[a] = base + hdr_len + len(body)
            enc, fix = self._encode(a)
            if fix:
                fixups.append((len(body) + fix[0], fix[1]))
            body += enc
        for off, tgt in fixups:
            struct.pack_into('<H', body, off, newaddr[tgt])
        vo = self.voice_ptr
        voices = bytes(self.z[vo:vo + (self.max_voice + 1) * 25]) if self.max_voice >= 0 else b''
        voice_addr = base + hdr_len + len(body) if voices else 0
        hdr = bytearray(struct.pack('<H', voice_addr)) + bytes([self.div, len(self.tracks)])
        for _, ch, ptr, tr, vol in self.tracks:
            hdr += bytes([0x80, ch]) + struct.pack('<H', newaddr[ptr]) + bytes([tr, vol])
        return bytes(hdr + body + voices)


def build(disc, s3krom, addr):
    """-> (bank bytes, table blob, {CD id: S3K id}) for a bank at 68k address addr
    (32 KB aligned)"""
    if addr & 0x7FFF:
        raise ValueError('the sound bank needs a 32 KB-aligned address')
    z = find_driver(disc)
    ram = z80_image(s3krom)
    w3 = s3krom[SND_BANK:SND_BANK + 0x8000]
    out = bytearray()
    silent = 0x8000                              # one PSG3 track that stops at once (no CD effect's channel)
    out += bytes([0, 0, 1, 1, 0x80, 0xC0]) + struct.pack('<H', silent + 10) + bytes([0, 0]) + b'\xF2\x00'
    cd_at = {}
    for sid in range(CD_FIRST, CD_LAST + 1):
        cd_at[sid] = 0x8000 + len(out)
        out += CdSfx(z, sid).assemble(0x8000 + len(out))
        if len(out) & 1:
            out.append(0)
    ptr = {}
    for k in S3K_KEEP:
        p = le16(ram, SFX_PTRS + (k - FIRST) * 2)
        ptr[k] = 0x8000 + len(out)
        out += S3kSfx(w3, p).assemble(0x8000 + len(out))
        if len(out) & 1:
            out.append(0)
    if len(out) > 0x8000:
        raise SystemExit(f"Sonic CD sound set: {len(out)} bytes do not fit a bank")
    for k, s in S3K_TO_CD.items():
        if isinstance(s, tuple):                 # (a variant, for that S3K id only)
            ptr[k] = 0x8000 + len(out)
            out += CdSfx(z, s[0], bound=s[1]).assemble(0x8000 + len(out))
            if len(out) & 1:
                out.append(0)
        else:
            ptr[k] = cd_at[s]
    if len(out) > 0x8000:
        raise SystemExit(f"Sonic CD sound set: {len(out)} bytes do not fit a bank")
    cd_id = {}
    for k, s in sorted(S3K_TO_CD.items(), reverse=True):
        if not isinstance(s, tuple):
            cd_id[s] = k                         # (several S3K ids for one sound: the lowest)
    used = code_sfx_ids()
    free = [k for k in range(0x46, 0xDC) if k not in ptr and k not in used and k != 0xAB]
    free.sort(key=lambda k: k >= 0xBC)           # (S3K's continuous ids last)
    for s in range(CD_FIRST, CD_LAST + 1):
        if s in cd_id:
            continue
        k = free.pop(0)
        ptr[k] = cd_at[s]
        cd_id[s] = k
    blob = bytearray([(addr >> 15) & 0xFF])
    for k in range(FIRST, LAST + 1):
        blob += struct.pack('<H', ptr.get(k, silent))
    for s in range(CD_FIRST, CD_LAST + 1):
        blob.append(cd_id.get(s, 0))
    return bytes(out) + bytes(0x8000 - len(out)), bytes(blob), cd_id


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Sonic CD sound set: list the conversion')
    ap.add_argument('--disc', required=True)
    ap.add_argument('--s3k', default=str(Path(__file__).resolve().parents[2] / 'game' / 'sonic3k' / 'sonic3k.bin'))
    a = ap.parse_args()
    bank, blob, ids = build(a.disc, Path(a.s3k).read_bytes(), 0x780000)
    print(f"bank: {len(bank.rstrip(bytes(1)))} of $8000 bytes used; CD id -> S3K id:")
    print(' '.join(f"{s:02X}>{ids[s]:02X}" for s in sorted(ids)))
