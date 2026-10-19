#!/usr/bin/env python3
"""Build trilogy.bin — the expansion bank (mapped at $400000) holding Sonic 1
and Sonic 2 zones converted into Sonic 3 & Knuckles engine formats.

Input: the player's own cartridges. Nothing from the ROMs is committed.
  S1: Sonic The Hedgehog (W) REV01   (header checksum $AFC7)
  S2: Sonic The Hedgehog 2 (W) REV00 (CRC32 24AB4C3A)
Output layout is documented in game/trilogy/trilogy.h.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mdcodecs import nemesis_decode, enigma_decode, kosinski_decode, kosinski_encode, kosm_encode  # noqa: E402
from s1_objects import S1Objects, write_asset_header  # noqa: E402
from tilepack import Packer, remap_tile  # noqa: E402
from act3_numeral import numeral_3  # noqa: E402
import s2_level  # noqa: E402
import s2_music  # noqa: E402
import s2_objects  # noqa: E402
from s1_music import build_music  # noqa: E402

# S1 animated level art (AnimateLevelGfx): (first tile, tiles) pinned in place
S1_ANIMATED = {
    'GHZ': [(0x35C, 16), (0x36C, 12), (0x378, 8)],
    'MZ': [(0x2D2, 16), (0x2E2, 8), (0x2F2, 6)],
    'SBZ': [(0x448, 12), (0x454, 12)],                 # smoke puffs (AniArt_SBZ)
}

# Retail S3K object code addresses used directly by converted layouts
S3K_ADDR = {'Obj_Monitor': 0x01D566, 'Obj_Spring': 0x022D06, 'Obj_StarPost': 0x02CFA8,
            'Obj_EggCapsule': 0x086540, 'Obj_SSEntryRing': 0x06166A,
            'Obj_InvisibleBlock': 0x01EC18, 'Obj_Bubbler': 0x02F938, 'Obj_PathSwap': 0x01CCAE}

BANK_BASE = 0x400000
DIR_OFF = 0x40
DIR_ENTRY = 0x40
DIR_COUNT = 0x80
PAL_BASE = 0x80
PLC_BASE = 0x80

# Sonic 1 REV01 addresses (from the byte-identical s1disasm REV01 listing).
S1 = {
    'Pal_Index': 0x002160, 'LevelSizeArray': 0x005F2A, 'StartLocArray': 0x00611E,
    'LoopTileNums': 0x00618E, 'LevelHeaders': 0x01DD16, 'ArtLoadCues': 0x01DD86,
    'AngleMap': 0x062900, 'CollArray1': 0x062A00, 'CollArray2': 0x063A00,
    'Col': [0x064A00, 0x064B9A, 0x064C62, 0x064DF2, 0x064FE6, 0x0651DA],
    'Level_Index': 0x068B96, 'ObjPos_Index': 0x06B000,
}
S1_ZONES = ['GHZ', 'LZ', 'MZ', 'SLZ', 'SYZ', 'SBZ']
S3K_WATER_LINE0 = bytes(32)    # S3K underwater player palette line (read from the S3K ROM in main)
S3K_DIAG_SPRING = (0, 0)        # ArtNem_DiagonalSpring (address, tiles), read from the S3K ROM in main
S1_BLOBS = {'Ledge_SlopeData': 0x008BEE}
S1_ZONE_SONG = {'GHZ': 0x81, 'LZ': 0x82, 'MZ': 0x83, 'SLZ': 0x84, 'SYZ': 0x85, 'SBZ': 0x86}
TRI_ZONE = {'GHZ': 0x20, 'LZ': 0x21, 'MZ': 0x22, 'SLZ': 0x23, 'SYZ': 0x24, 'SBZ': 0x25}

# S3K VRAM tiles that common objects own; level art must stay below.
S3K_LEVEL_TILE_LIMIT = 0x45C


def be16(b, a): return (b[a] << 8) | b[a + 1]
def be32(b, a): return (be16(b, a) << 16) | be16(b, a + 2)
def s16(v): return v - 0x10000 if v & 0x8000 else v


class Bank:
    def __init__(self):
        self.data = bytearray(DIR_OFF + DIR_COUNT * DIR_ENTRY)
        self.data[0:4] = b'TRLG'
        struct.pack_into('>H', self.data, 4, 1)
        self.pals = []      # PalPoint entries (src, dest, count)
        self.plcs = []      # PLC list addresses
        self.dedupe = {}

    def add(self, blob, align=2, dedupe=True):
        blob = bytes(blob)
        if dedupe and blob in self.dedupe:
            return self.dedupe[blob]
        while len(self.data) % align:
            self.data.append(0)
        a = BANK_BASE + len(self.data)
        self.data += blob
        if len(self.data) & 1:
            self.data.append(0)
        if dedupe:
            self.dedupe[blob] = a
        return a

    def add_palette(self, colors, dest, ):
        src = self.add(colors)
        self.pals.append((src, dest, len(colors) // 4 - 1))
        return PAL_BASE + len(self.pals) - 1

    def add_plc(self, entries):
        """entries: [(nemesis_bank_addr, vram_byte_addr)]"""
        if not entries:
            return 0
        blob = struct.pack('>H', len(entries) - 1)
        for src, vram in entries:
            blob += struct.pack('>IH', src, vram)
        self.plcs.append(self.add(blob))
        return PLC_BASE + len(self.plcs) - 1

    def entry(self, zone, act):
        return DIR_OFF + ((zone - 0x20) * 4 + act) * DIR_ENTRY

    def finish(self):
        pt = bytearray()
        for src, dest, cnt in self.pals:
            pt += struct.pack('>IHH', src, dest, cnt)
        pa = self.add(pt, dedupe=False) if pt else 0
        lt = b''.join(struct.pack('>I', a) for a in self.plcs)
        la = self.add(lt, dedupe=False) if lt else 0
        struct.pack_into('>IH', self.data, 0x08, pa, len(self.pals))
        struct.pack_into('>IH', self.data, 0x10, la, len(self.plcs))
        if len(self.data) > 0x3F0000:
            raise SystemExit("bank overflow")
        return bytes(self.data)


# --------------------------------------------------------------- S3K layout
def s3k_layout(fg_rows, bg_rows):
    """fg_rows/bg_rows: lists of equal-length byte rows (128px chunk ids)."""
    fw = len(fg_rows[0]) if fg_rows else 0
    bw = len(bg_rows[0]) if bg_rows else 0
    out = bytearray(0x1000)
    struct.pack_into('>HHHH', out, 0, fw, bw, len(fg_rows), len(bg_rows))
    pos = 0x88
    ptr = {}
    zero_row = None
    for r, row in enumerate(fg_rows):
        out[pos:pos + fw] = bytes(row)
        ptr[('f', r)] = pos
        pos += fw
    for r, row in enumerate(bg_rows):
        out[pos:pos + bw] = bytes(row)
        ptr[('b', r)] = pos
        pos += bw
    zero_row = pos
    pos += max(fw, bw, 1)
    if pos > 0x1000:
        raise ValueError("layout too large: $%X" % pos)
    for r in range(32):
        struct.pack_into('>H', out, 8 + r * 4, 0x8000 + ptr.get(('f', r), zero_row))
        struct.pack_into('>H', out, 8 + r * 4 + 2, 0x8000 + ptr.get(('b', r), zero_row))
    return bytes(out)


# --------------------------------------------------------------- palettes
# S1 palette line 1 -> S3K line-1 convention (S3K common objects keep their
# fixed slots: 1 white, 5/6 yellows, 11 outline, 12-14 greys, 15 dark yellow).
L1_PERM = [0, 11, 2, 3, 4, 7, 1, 12, 13, 14, 8, 9, 5, 6, 15, 10]
# S1 Pal_Sonic (line 0) and S3K Sonic palette (PalPoint 3)
S1_SONIC = [0x000, 0x000, 0x822, 0xA44, 0xC66, 0xE88, 0xEEE, 0xAAA, 0x888, 0x444, 0x8AE, 0x46A, 0x00E, 0x008, 0x004, 0x0EE]
S3K_SONIC = [0x000, 0xEEE, 0xE66, 0xC42, 0x822, 0x080, 0x00E, 0x008, 0x0AE, 0x08E, 0x8AE, 0x46A, 0xECC, 0xCAA, 0x866, 0x222]


def _rgb(c):
    return ((c >> 1) & 7, (c >> 5) & 7, (c >> 9) & 7)


def nearest(c, pal, skip=()):
    r, g, b = _rgb(c)
    best, bi = None, 1
    for i in range(1, 16):
        if i in skip:
            continue
        r2, g2, b2 = _rgb(pal[i])
        d = 3 * (r - r2) ** 2 + 4 * (g - g2) ** 2 + 2 * (b - b2) ** 2
        if best is None or d < best:
            best, bi = d, i
    return bi


# Super Sonic's palette cycle rewrites line-0 entries 2-4 (SuperHyper_PalCycle_SonicApply);
# only the player may use them, so S1 art on line 0 takes the line's other blues.
SUPER_CYCLED = (2, 3, 4)
L0_MAP = [0] + [nearest(S1_SONIC[i], S3K_SONIC, SUPER_CYCLED) for i in range(1, 16)]
L0_MAP[2:6] = [0xF, 0xE, 0xD, 0xC]     # S1's four blues keep their dark-to-light ramp


def remap_pixels(tile, table):
    out = bytearray(32)
    for i, b in enumerate(tile):
        out[i] = (table[b >> 4] << 4) | table[b & 15]
    return bytes(out)


def remap_level_tiles(vram, filled, blocks, name):
    """Recolour S1 level tiles drawn with palette lines 0/1 into the S3K
    conventions, duplicating tiles used with several lines and moving tiles
    above the S3K level-art limit. Returns (vram, blocks)."""
    vram = bytearray(vram)
    use = {}
    for i in range(len(blocks) // 2):
        w = be16(blocks, i * 2)
        use.setdefault(w & 0x7FF, set()).add((w >> 13) & 3)
    free = [t for t in range(1, S3K_LEVEL_TILE_LIMIT) if not filled[t] and t not in use]
    tables = {0: L0_MAP, 1: L1_PERM, 2: list(range(16)), 3: list(range(16))}
    slot = {}                      # (tile, line) -> new tile
    src = {t: bytes(vram[t * 32:t * 32 + 32]) for t in use}
    for t in sorted(use):
        variants = {}
        for line in sorted(use[t]):
            variants.setdefault(remap_pixels(src[t], tables[line]), []).append(line)
        first = True
        for pix, lines in variants.items():
            if first and t < S3K_LEVEL_TILE_LIMIT:
                dest = t
            else:
                if not free:
                    raise SystemExit(f"{name}: out of free VRAM tiles")
                dest = free.pop(0)
            first = False
            vram[dest * 32:dest * 32 + 32] = pix
            for line in lines:
                slot[(t, line)] = dest
    moved = 0
    for i in range(len(blocks) // 2):
        w = be16(blocks, i * 2)
        t, line = w & 0x7FF, (w >> 13) & 3
        n = slot[(t, line)]
        if n != t:
            moved += 1
        w = (w & ~0x7FF) | n
        blocks[i * 2] = w >> 8
        blocks[i * 2 + 1] = w & 0xFF
    if moved:
        print(f"    {name}: {moved} block tile refs relocated")
    used = set(slot.values())
    return vram, bytes(blocks), used


# --------------------------------------------------------------- collision
def _cell(w):
    """S1 chunk cell identity for collision: (block, xflip, yflip)."""
    return (w & 0x3FF, (w >> 11) & 1, (w >> 12) & 1)


def _s8(v):
    return v - 256 if v > 127 else v


def flip_shape(hgts, angs, shape, rel):
    """Mirror an S1 collision shape (rel = (xflip, yflip)) and rebuild both
    arrays: floor heights per column (+bottom/-top anchored) and wall widths
    per row (+right/-left anchored, the convention of S1 CollArray2)."""
    h = [_s8(hgts[shape * 16 + x]) for x in range(16)]
    bm = [[0] * 16 for _ in range(16)]
    for x in range(16):
        v = h[x]
        for y in range(16):
            if (v > 0 and y >= 16 - v) or (v < 0 and y < -v):
                bm[y][x] = 1
    if rel[0]:
        bm = [row[::-1] for row in bm]
    if rel[1]:
        bm = bm[::-1]
    n = []
    for x in range(16):
        c = [bm[y][x] for y in range(16)]
        cnt = sum(c)
        n.append(0 if not cnt else 16 if cnt == 16 else (cnt if c[15] else -cnt))
    r = []
    for y in range(16):
        row = bm[y]
        cnt = sum(row)
        r.append(0 if not cnt else 16 if cnt == 16 else (cnt if row[15] else -cnt))
    a = angs[shape]
    if a != 0xFF:
        if rel[0]:
            a = (-a) & 0xFF
        if rel[1]:
            a = (0x80 - a) & 0xFF
    return bytes(v & 0xFF for v in n), bytes(v & 0xFF for v in r), a


# --------------------------------------------------------------- Sonic 1
class Sonic1:
    def __init__(self, rom):
        self.rom = rom
        self.anim_line_map = {}       # zone name -> {animated start: palette line}
        if len(rom) != 0x80000 or be16(rom, 0x18E) != 0xAFC7:
            raise SystemExit("Sonic 1 ROM must be the REV01 cartridge (checksum $AFC7)")
        self.objects = S1Objects(rom, {0: L0_MAP, 1: L1_PERM, 2: list(range(16)), 3: list(range(16))},
                                 S3K_LEVEL_TILE_LIMIT)

    def plc(self, pid):
        rom = self.rom
        off = be16(rom, S1['ArtLoadCues'] + pid * 2)
        a = S1['ArtLoadCues'] + off
        n = be16(rom, a) + 1
        return [(be32(rom, a + 2 + i * 6), be16(rom, a + 6 + i * 6)) for i in range(n)]

    def header(self, z):
        a = S1['LevelHeaders'] + z * 16
        rom = self.rom
        return {'plc1': rom[a], 'art': be32(rom, a) & 0xFFFFFF, 'plc2': rom[a + 4],
                'blk16': be32(rom, a + 4) & 0xFFFFFF, 'blk256': be32(rom, a + 8) & 0xFFFFFF,
                'pal': rom[a + 15]}

    def palette_lines(self, palid):
        rom = self.rom
        pe = S1['Pal_Index'] + palid * 8
        ps = be32(rom, pe)
        return [be16(rom, ps + i * 2) for i in range(48)]

    def layout(self, z, act, k):
        rom = self.rom
        li = S1['Level_Index']
        a = li + be16(rom, li + (z * 4 + act) * 6 + k * 2)
        w = rom[a] + 1
        h = rom[a + 1] + 1
        return [list(rom[a + 2 + r * w:a + 2 + (r + 1) * w]) for r in range(h)]

    def convert_zone(self, bank, z):
        rom = self.rom
        name = S1_ZONES[z]
        hdr = self.header(z)
        # VRAM image from both level PLCs (S1 decodes art in place)
        vram = bytearray(0x10000)
        for pid in (hdr['plc1'], hdr['plc2']):
            for src, dst in self.plc(pid):
                art, _ = nemesis_decode(rom, src)
                vram[dst:dst + len(art)] = art[:0x10000 - dst]
        self._vram = vram
        self._blocks = bytes(enigma_decode(rom, hdr['blk16']))
        nblk = len(self._blocks) // 8
        chunks256_pre, _ = kosinski_decode(rom, hdr['blk256'])
        # Loop chunks: where the "behind" chunk uses a different block, make a
        # synthetic block = front visuals, primary shape = front, secondary =
        # behind (S3K keeps one block index per chunk cell).
        self._synth = {}
        acts = 4 if name == 'LZ' else 3
        for act in range(acts):
            for k in range(2):
                for row in self.layout(z, act, k):
                    for b in row:
                        if not (b & 0x80):
                            continue
                        v = b & 0x7F
                        v2 = v + 1 if v + 1 != 0x29 else 0x51
                        for i in range(256):
                            w = be16(chunks256_pre, (v - 1) * 512 + i * 2)
                            wb = be16(chunks256_pre, (v2 - 1) * 512 + i * 2)
                            key = (_cell(w), _cell(wb))
                            if key[0] != key[1] and key not in self._synth:
                                self._synth[key] = nblk + len(self._synth)
        blk = bytearray(self._blocks)
        for (fb, bb), nid in sorted(self._synth.items(), key=lambda kv: kv[1]):
            blk += self._blocks[fb[0] * 8:fb[0] * 8 + 8]
        self._blocks = bytes(blk)
        objptrs_a = bank.add(self.objects.pointer_table(S3K_ADDR))
        chunks256, _ = kosinski_decode(rom, hdr['blk256'])
        # palette (LevelDataLoad: SBZ3 = LZ4 has palid_SBZ3, SBZ2 and FZ palid_SBZ2)
        def level_palette(palid):
            pe = S1['Pal_Index'] + palid * 8
            psrc, pdst, pcnt = be32(rom, pe), be16(rom, pe + 4), be16(rom, pe + 6)
            # S1 palette RAM is $FB00..; S3K Normal_palette is $FC00..
            pal = bytearray(rom[psrc:psrc + (pcnt + 1) * 4])
            if pdst == 0xFB20:               # first 16 colours are palette line 1
                l1 = pal[0:32]
                for i in range(16):
                    pal[L1_PERM[i] * 2:L1_PERM[i] * 2 + 2] = l1[i * 2:i * 2 + 2]
            return bank.add_palette(pal, 0xFC00 + (pdst - 0xFB00))
        pal_id = level_palette(hdr['pal'])
        act_pal = {}
        if name == 'LZ':
            act_pal[3] = level_palette(0x0C)
        elif name == 'SBZ':
            act_pal[1] = act_pal[2] = level_palette(0x0E)
        # collision
        col = rom[S1['Col'][z]:S1['Col'][z] + nblk]
        solids = bytearray(0xC00)
        for b in range(min(nblk, 0x300)):
            solids[b * 2] = col[b]
            solids[0x600 + b * 2] = col[b]
        angs = bytearray(rom[S1['AngleMap']:S1['AngleMap'] + 0x100])
        hgts = bytearray(rom[S1['CollArray1']:S1['CollArray1'] + 0x1000])
        hgtr = bytearray(rom[S1['CollArray2']:S1['CollArray2'] + 0x1000])
        used_shapes = set(col) | {0}
        free_shapes = [i for i in range(1, 256) if i not in used_shapes]
        made = {}
        for (fb, bb), nid in self._synth.items():
            solids[nid * 2] = col[fb[0]]
            rel = (fb[1] ^ bb[1], fb[2] ^ bb[2])
            shape = col[bb[0]]
            if rel != (0, 0) and shape:
                k = (shape, rel)
                if k not in made:
                    ns = free_shapes.pop(0)
                    n, r, a = flip_shape(hgts, angs, shape, rel)
                    hgts[ns * 16:ns * 16 + 16] = n
                    hgtr[ns * 16:ns * 16 + 16] = r
                    angs[ns] = a
                    made[k] = ns
                shape = made[k]
            solids[0x600 + nid * 2] = shape
        if self._synth:
            print(f"    {name}: {len(self._synth)} synthetic loop blocks, {len(made)} mirrored shapes")
        solids_a = bank.add(solids)
        ang_a = bank.add(bytes(angs))
        hgt_a = bank.add(bytes(hgts))
        hgtr_a = bank.add(bytes(hgtr))
        acts = 4 if name == 'LZ' else 3
        for act in range(acts):
            self.convert_act(bank, z, act, chunks256, act_pal.get(act, pal_id), solids_a, ang_a, hgt_a, hgtr_a, objptrs_a)
        print(f"  S1 {name}: {nblk} blocks, pal ${pal_id:02X}, {acts} acts")

    def water_palette(self, src, bank):
        """S1 underwater palette: S3K's underwater player line (HCZ1) + S1 lines 1-3."""
        rom = self.rom
        pal = bytearray(S3K_WATER_LINE0) + bytearray(rom[src + 0x20:src + 0x80])
        l1 = bytes(pal[0x20:0x40])
        for i in range(16):
            pal[0x20 + L1_PERM[i] * 2:0x20 + L1_PERM[i] * 2 + 2] = l1[i * 2:i * 2 + 2]
        return bank.add_palette(pal, 0xFC00)

    def anim_lines(self, name):
        """Animated range start -> the palette line its blocks use (one per range)."""
        out = {}
        blk = self._blocks
        for start, n in S1_ANIMATED.get(name, []):
            seen = set()
            for b in range(len(blk) // 8):
                for k in range(4):
                    w = be16(blk, b * 8 + k * 2)
                    if start <= (w & 0x7FF) < start + n:
                        seen.add((w >> 13) & 3)
            if len(seen) > 1:
                raise SystemExit(f"{name}: animated tiles ${start:X} used on palette lines {sorted(seen)}")
            out[start] = seen.pop() if seen else 2
        self.anim_line_map[name] = out
        return out

    def convert_act(self, bank, z, act, chunks256, pal_id, solids_a, ang_a, hgt_a, hgtr_a, objptrs_a):
        rom = self.rom
        name = S1_ZONES[z]

        synth = self._synth

        def s1word(w, behind=None):
            idx = w & 0x3FF
            if behind is not None and _cell(behind) != _cell(w):
                idx = synth[(_cell(w), _cell(behind))]
            xf = (w >> 11) & 1
            yf = (w >> 12) & 1
            top = (w >> 13) & 1
            lrb = (w >> 14) & 1
            if behind is None:
                top2, lrb2 = top, lrb
            else:
                top2 = (behind >> 13) & 1
                lrb2 = (behind >> 14) & 1
            return idx | (xf << 10) | (yf << 11) | (top << 12) | (lrb << 13) | (top2 << 14) | (lrb2 << 15)

        def quad(ci, qx, qy, alt=None):
            out = bytearray()
            for r in range(8):
                for c in range(8):
                    o = ci * 512 + ((qy * 8 + r) * 16 + qx * 8 + c) * 2
                    w = be16(chunks256, o)
                    if alt is not None:
                        wb = be16(chunks256, alt * 512 + ((qy * 8 + r) * 16 + qx * 8 + c) * 2)
                        out += struct.pack('>H', s1word(w, wb))
                    else:
                        out += struct.pack('>H', s1word(w))
            return bytes(out)

        chunks = [bytes(128)]
        index = {bytes(128): 0}

        def chunk_id(blob):
            if blob not in index:
                if len(chunks) >= 256:
                    raise ValueError(f"{name}{act + 1}: more than 256 chunks")
                index[blob] = len(chunks)
                chunks.append(blob)
            return index[blob]

        def convert_rows(rows):
            out = []
            for row in rows:
                top = []
                bot = []
                for b in row:
                    if not b:
                        top += [0, 0]
                        bot += [0, 0]
                        continue
                    v = b & 0x7F
                    alt = None
                    if b & 0x80:
                        v2 = v + 1
                        if v2 == 0x29:
                            v2 = 0x51
                        alt = v2 - 1
                    ids = [chunk_id(quad(v - 1, qx, qy, alt)) for qy in range(2) for qx in range(2)]
                    top += ids[0:2]
                    bot += ids[2:4]
                out += [top, bot]
            return out

        fg = convert_rows(self.layout(z, act, 0))
        bg = convert_rows(self.layout(z, act, 1))
        # LZ3: row 2 column 6 becomes S1 chunk $4B when the water rises
        # (DynWater_LZ3) or chunk 7 when switch $F is pressed (DLE_LZ3); the
        # four 128px quadrants of each, converted now
        lz3_swap = bytes(chunk_id(quad(c - 1, qx, qy)) for c in (0x4B, 0x07)
                         for qy in range(2) for qx in range(2))             if (name == 'LZ' and act == 2) else bytes(8)
        layout_a = bank.add(s3k_layout(fg, bg))
        chunks_a = bank.add(kosinski_encode(b''.join(chunks)))
        # ---- per-act level art: only what this act can show
        need = self.objects.act_assets(z, act)
        tables = {0: L0_MAP, 1: L1_PERM, 2: list(range(16)), 3: list(range(16))}
        pinned = S1_ANIMATED.get(name, [])
        pk = Packer(self._vram, tables, S3K_LEVEL_TILE_LIMIT, pinned)
        for t0, n, line in self.objects.level_asset_groups(need):
            pk.place_group(t0, n, line)
        blocks = bytearray(self._blocks)
        used_blocks = set()
        for ch in chunks:
            for i in range(64):
                used_blocks.add(be16(ch, i * 2) & 0x3FF)
        for b in range(len(blocks) // 8):
            for k in range(4):
                o = b * 8 + k * 2
                w = be16(blocks, o)
                if b in used_blocks:
                    pk.place_single(w & 0x7FF, (w >> 13) & 3)
        for b in range(len(blocks) // 8):
            for k in range(4):
                o = b * 8 + k * 2
                w = be16(blocks, o)
                nt = pk.slot.get((w & 0x7FF, (w >> 13) & 3), 0) if b in used_blocks else 0
                w = (w & 0xF800) | nt
                blocks[o] = w >> 8
                blocks[o + 1] = w & 0xFF
        # animated ranges keep their slots; their pixels take the colour remap
        # of the palette line the zone's blocks draw them with
        lines = self.anim_lines(name)
        pin_src = {}
        for start, n in pinned:
            tab = tables[lines[start]]
            for k in range(n):
                pin_src[start + k] = remap_tile(bytes(self._vram[(start + k) * 32:(start + k + 1) * 32]), tab)
        art = pk.art(pin_src)
        if len(art) & 1:
            art += bytes(1)
        art_a, art2_a = level_art_modules(bank, art)
        blocks_a = bank.add(kosinski_encode(bytes(blocks)))
        free = set(range(pk.end(), S3K_LEVEL_TILE_LIMIT)) | (set(range(1, S3K_LEVEL_TILE_LIMIT)) - pk.taken)
        # LevelLoadBlock (24 bytes)
        llb = struct.pack('>IIIIII',
                          (0 << 24) | art_a, (0 << 24) | art2_a,
                          (pal_id << 24) | blocks_a, (pal_id << 24) | blocks_a,
                          chunks_a, chunks_a)
        llb_a = bank.add(llb)
        # sizes / start
        sa = S1['LevelSizeArray'] + (z * 4 + act) * 12
        left, right, top, bottom = (be16(rom, sa + 2), be16(rom, sa + 4), be16(rom, sa + 6), be16(rom, sa + 8))
        st = S1['StartLocArray'] + (z * 4 + act) * 4
        sx, sy = be16(rom, st), be16(rom, st + 2)
        # S1 info block: raw 256px FG layout (64x8) + loop/tunnel chunk ids
        raw = bytearray(64 * 8)
        for r, row in enumerate(self.layout(z, act, 0)[:8]):
            raw[r * 64:r * 64 + min(64, len(row))] = bytes(row[:64])
        lt = S1['LoopTileNums'] + z * 4
        # water (LZ1-3 + SBZ3 = LZ4): underwater palette id, start height (S1 WaterHeight)
        wpal, wy = 0, 0
        if name == 'LZ':
            wy = (0xB8, 0x328, 0x900, 0x228)[act]
            wpal = self.water_palette(0x27A0 if act == 3 else 0x2460, bank)
        s1info_a = bank.add(bytes(raw) + rom[lt:lt + 4] + struct.pack('>BBH', wpal, 0, wy) + lz3_swap)
        ob, rb = self.objects.convert_objects(z, act)
        assets_a = self.objects.build_assets(bank, name, free, need, pk.slot, act)
        objs_a = bank.add(ob, dedupe=False)
        rings_a = bank.add(rb, dedupe=False)
        e = bank.entry(TRI_ZONE[name], act)
        d = bank.data
        struct.pack_into('>IIIHHHHHHHHII', d, e,
                         llb_a, layout_a, solids_a, left, right, top, bottom, sx, sy, sx, sy,
                         objs_a + 6, rings_a)
        # S1 song id (s1_music.py); SBZ act 3 internal = Final Zone
        d[e + 0x24] = 0x8D if (name == 'SBZ' and act == 2) else S1_ZONE_SONG[name]
        d[e + 0x25] = 1           # game: S1
        struct.pack_into('>III', d, e + 0x28, ang_a, hgt_a, hgtr_a)
        struct.pack_into('>I', d, e + 0x3C, s1info_a)
        struct.pack_into('>II', d, e + 0x34, objptrs_a, assets_a)
        print(f"    {name}{act + 1}: {len(chunks)} chunks, {pk.end()} level tiles, fg {len(fg[0])}x{len(fg)}, bg {len(bg[0])}x{len(bg)}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--s1', required=True)
    ap.add_argument('--s2')
    ap.add_argument('--out', required=True)
    ap.add_argument('--zones', default='GHZ')
    ap.add_argument('--s3k', required=True)
    args = ap.parse_args(argv)
    bank = Bank()
    global S3K_WATER_LINE0
    _s3k = Path(args.s3k).read_bytes()
    _pp = 0x0A872C + 0x31 * 8                                  # PalPoint: HCZ1 water
    S3K_WATER_LINE0 = _s3k[be32(_s3k, _pp):be32(_s3k, _pp) + 0x20]
    global S3K_DIAG_SPRING                                     # ArtNem_DiagonalSpring (S3K PLC art)
    S3K_DIAG_SPRING = (0x190B4C, len(nemesis_decode(_s3k, 0x190B4C)[0]) // 32)
    s1 = Sonic1(Path(args.s1).read_bytes())
    s2 = None
    for name in args.zones.split(','):
        if name in S1_ZONES:
            s1.convert_zone(bank, S1_ZONES.index(name))
        elif name in s2_level.S2_ZONES:
            if not args.s2:
                raise SystemExit(f"{name}: needs --s2 <Sonic 2 REV00 ROM>")
            if s2 is None:
                s2 = s2_level.Sonic2(Path(args.s2).read_bytes(), sys.modules[__name__])
            s2.convert_zone(bank, name)
    # blobs referenced by native objects (TRI_BLOB_* in trilogy.h)
    s1rom = Path(args.s1).read_bytes()
    slope = s1rom[S1_BLOBS['Ledge_SlopeData']:S1_BLOBS['Ledge_SlopeData'] + 0x30]
    blobs = [bank.add(bytes(b for v in slope for b in (v, v)))]
    # animals: S1 Anml_VarIndex pairs (type numbering matches S3K Obj_Animal)
    s1_animals = {0x20: (0, 5), 0x21: (2, 3), 0x22: (6, 3), 0x23: (4, 5), 0x24: (4, 1), 0x25: (0, 1), 0x26: (0, 1)}
    # S2 (Obj28_ZoneAnimals) on S3K's seven: monkey/mouse -> squirrel, eagle -> flicky, bear -> pig, turtle -> seal
    s1_animals.update({0x28: (6, 5), 0x29: (0, 5), 0x2A: (2, 5), 0x2B: (4, 5), 0x2C: (6, 5), 0x2D: (6, 1),
                       0x2E: (2, 3), 0x2F: (6, 5), 0x30: (3, 1), 0x31: (6, 5), 0x32: (4, 1)})
    animal_art = {0: 0x193706, 1: 0x193308, 2: 0x193456, 3: 0x192F6E, 4: 0x19308A, 5: 0x1931D6, 6: 0x1935A8}
    types = bytearray(128)
    plcs = bytearray(32 * 4)
    for z, (a, b) in s1_animals.items():
        types[z * 2], types[z * 2 + 1] = a, b
        lst = struct.pack('>H', 1) + struct.pack('>IH', animal_art[a], 0x580 * 32) + struct.pack('>IH', animal_art[b], 0x592 * 32)
        struct.pack_into('>I', plcs, (z - 0x20) * 4, bank.add(lst))
    blobs.append(bank.add(bytes(types)))
    blobs.append(bank.add(bytes(plcs)))
    blobs.append(bank.add(bytes([0xFF, 0xFF])))
    blobs.append(bank.add(s1rom[0x0144F0:0x0144F0 + 0x100]))   # Drown_WobbleData
    blobs.append(bank.add(s1rom[0x006448:0x006448 + 0x100]))   # Lz_Scroll_Data
    # animated art, recoloured for the palette line its blocks use
    tabs = {0: L0_MAP, 1: L1_PERM, 2: list(range(16)), 3: list(range(16))}
    ghz_lines = s1.anim_line_map.get('GHZ', {})

    def anim_art(a, n, start):
        return remap_tile(s1rom[a:a + n * 32], tabs[ghz_lines.get(start, 2)])
    blobs.append(bank.add(anim_art(0x066A96, 16, 0x378)))  # Art_GhzWater (2 x 8 tiles)
    blobs.append(bank.add(anim_art(0x066C96, 32, 0x35C)))  # Art_GhzFlower1 (2 x 16)
    blobs.append(bank.add(anim_art(0x067096, 36, 0x36C)))  # Art_GhzFlower2 (3 x 12)
    blobs.append(bank.add(s1rom[0x001B7E:0x001B7E + 32]))       # Pal_GHZCyc (4 x 4 colours)
    s3krom = Path(args.s3k).read_bytes()
    blobs.append(bank.add(kosm_encode(numeral_3(_kosm_decode(s3krom, S3K_SYMS['ArtKosM_TitleCardNum2'])))))  # act 3
    blobs.append(bank.add(s1rom[0x00412E:0x00412E + 0x42]))   # S1 oscillator baselines
    blobs.append(bank.add(s1rom[0x0041C8:0x0041C8 + 0x40]))   # S1 oscillator settings
    mz_lines = s1.anim_line_map.get('MZ', {})

    def mz_art(a, n, start):
        return remap_tile(s1rom[a:a + n * 32], tabs[mz_lines.get(start, 2)])
    blobs.append(bank.add(mz_art(0x067516, 24, 0x2E2)))  # Art_MzLava1 (3 x 8 tiles)
    blobs.append(bank.add(mz_art(0x067816, 48, 0x2D2)))  # Art_MzLava2 (3 x $200 magma rows)
    blobs.append(bank.add(mz_art(0x067E16, 24, 0x2F2)))  # Art_MzTorch (4 x 6 tiles)
    blobs.append(bank.add(s1rom[0x00B7A2:0x00B7A2 + 0x4C]))  # LGrass_Data1 (mz_pfm1)
    blobs.append(bank.add(s1rom[0x00B7EE:0x00B7EE + 0x2C]))  # LGrass_Data2 (mz_pfm2)
    blobs.append(bank.add(s1rom[0x00B81A:0x00B81A + 0x4C]))  # LGrass_Data3 (mz_pfm3)
    music_tab, sizes = build_music(s1rom, bank)
    blobs.append(bank.add(music_tab))                       # S1 songs (s1_music.py)
    blobs.append(bank.add(bytes([0x20, 0x21])))             # music slots $20/$21
    blobs.append(bank.add(s1rom[0x001C6E:0x001C6E + 0x30]))  # Pal_SYZCyc1 (4 x 4) + Pal_SYZCyc2 (4 x 2)
    # HInt3 water colour list: every colour (S1 swaps the whole palette)
    xfer = list(range(0, 0x7A, 6)) + [0x7A]
    blobs.append(bank.add(struct.pack('>H', len(xfer) - 1) + b''.join(struct.pack('>H', o) for o in xfer)))
    # S3K breathing bubbles / air countdown art (ArtNem_Bubbles -> ArtTile_Bubbles $45C)
    blobs.append(bank.add(struct.pack('>HIH', 0, 0x191B46, 0x45C * 32)))
    # seesaw heightmaps (S1: a byte per 2px -> S3K: a byte per pixel)
    for a in (0x12018, 0x12048):                         # See_DataSlope, See_DataFlat
        blobs.append(bank.add(bytes(b for v in s1rom[a:a + 0x30] for b in (v, v))))
    # conveyor / spinning platform lists: ObjPosLZPlatform_Index + ObjPosSBZPlatform_Index
    # (16 entries) -> u16 offsets from the blob start, then lists (count-1, x/y/subtype words)
    hdr, body = b'', b''
    for k in range(16):
        a = 0x6B000 + be16(s1rom, 0x6B070 + k * 2)
        n = be16(s1rom, a) + 1
        hdr += struct.pack('>H', 32 + len(body))
        body += s1rom[a:a + 2 + n * 6]
    blobs.append(bank.add(hdr + body))
    blobs.append(bank.add(s1rom[0x001B9E:0x001C02]))   # Pal_LZCyc1 (4x8), LZCyc2 (3x6), LZCyc3 (3x6), SBZ3Cyc (4x8)
    blobs.append(bank.add(s1rom[0x001C4A:0x001C4A + 36]))   # Pal_SLZCyc (6 x 3 colours)
    sbz_lines = s1.anim_line_map.get('SBZ', {})
    for start in (0x448, 0x454):                            # Art_SbzSmoke, per puff's palette line
        blobs.append(bank.add(remap_tile(s1rom[0x068116:0x068116 + 84 * 32], tabs[sbz_lines.get(start, 2)])))
    blobs.append(bank.add(s1rom[0x001C9E:0x001DB0]))        # Pal_SBZCycList1/2 + Pal_SBZCyc1-10
    # pause menu: S3K's level-select font (S2 2P-options art): 0-9 * (c) : . A-Z
    blobs.append(bank.add(bytes(nemesis_decode(s3krom, S3K_SYMS['ArtNem_S22POptions'])[0][:40 * 32])))
    # Sonic 2 palette cycles (from the S2 cartridge when given)
    s2rom = Path(args.s2).read_bytes() if args.s2 else bytes(0x100000)
    blobs.append(bank.add(s2rom[0x001E6E:0x001E6E + 32]))   # CyclingPal_EHZ_ARZ_Water (4 x 4)
    if args.s2:                                               # S2 songs (s2_music.py)
        sym = s2_objects.symbols()
        drv = s2_music.saxman_decode(s2rom, sym['Snd_Driver'], sym['Snd_Driver_End'] - sym['Snd_Driver'])
        envs = s2_music.s2_envelopes(s2rom, [(drv, sym['zPSG_Env%d' % i]) for i in range(1, 14)])
        s2tab, s2sizes = s2_music.build_music(s2rom, bank, sym, envs)
        blobs.append(bank.add(s2tab))
        print(f"  S2 music: {len(s2sizes)} songs, {sum(s2sizes)} bytes")
    else:
        blobs.append(bank.add(bytes([0, 0])))
    blobs.append(bank.add(s2rom[0x02297C:0x022FF4]))       # Obj1E spin tube paths (entry/exit, then main at +$508)
    blobs.append(bank.add(s2rom[0x026594:0x0265E4] + s2rom[0x02B3B6:0x02B422]))   # Obj40 diag (0), straight ($28); Obj86 flipper x3 ($50 + $24 n)
    blobs.append(bank.add(s2rom[0x002016:0x002096]))       # CyclingPal_CPZ1 (0), CPZ2 ($36), CPZ3 ($60)
    blobs.append(bank.add(s2rom[0x001F82:0x002016]))       # CyclingPal_CNZ1/3/4 (0/$24/$36), CNZ1_B/2_B/3_B ($5E/$70/$84)
    # extended sprite line 1 (line-0 object art keeps its own game's colours): per directory
    # entry ((zone-$20)*4+act) 16 colours above water, 16 below
    s1sym = __import__("s1_objects").load_symbols()
    ext = bytearray(DIR_COUNT * 64)

    def ext_put(za, top, bottom):
        i = ((za >> 8) - 0x20) * 4 + (za & 3)
        ext[i * 64:i * 64 + 32] = top
        ext[i * 64 + 32:i * 64 + 64] = bottom or top
    s1_sonic = s1rom[s1sym['Pal_Sonic']:s1sym['Pal_Sonic'] + 32]
    for z in range(0x20, 0x27):
        for act in range(4):
            water = None
            if z == 0x21:                                        # Labyrinth (act 4 = SBZ3)
                w = s1sym['Pal_SBZ3SonWat' if act == 3 else 'Pal_LZSonWater']
                water = s1rom[w:w + 32]
            ext_put(z << 8 | act, s1_sonic, water)
    if args.s2:
        s2sym = s2_objects.symbols()
        src = be32(s2rom, s2_level.S2['PalPointers'] + 3 * 8)        # Pal_SonicTails
        s2_sonic = s2rom[src:src + 32]
        for z in range(0x28, 0x33):
            water = {0x29: 'Pal_CPZ_U', 0x2A: 'Pal_ARZ_U'}.get(z)
            for act in range(4):
                ext_put(z << 8 | act, s2_sonic, s2rom[s2sym[water]:s2sym[water] + 32] if water else None)
    blobs.append(bank.add(bytes(ext)))                      # TRI_BLOB_EXTPAL (39)
    # HTZ slopes: Obj14 seesaw heights (S2: a byte per 2px -> S3K: a byte per pixel) at 0 and $62,
    # Obj30 lava slope (SlopedSolid, as S2 stores it) at $C4
    htz = bytearray(0xC4 + 0xCC)
    if args.s2:
        s2s = s2_objects.symbols()
        for k, lbl in enumerate(('byte_21C8E', 'byte_21CBF')):
            t = s2rom[s2s[lbl]:s2s[lbl] + 0x31]
            htz[k * 0x62:k * 0x62 + 0x62] = bytes(b for v in t for b in (v, v))
        htz[0xC4:0xC4 + 0xCC] = s2rom[s2s['Obj30_SlopeData']:s2s['Obj30_SlopeData'] + 0xCC]
    blobs.append(bank.add(bytes(htz)))                      # TRI_BLOB_S2_HTZ (40)
    lava = s2rom[s2_objects.symbols()['CyclingPal_Lava']:][:0x80] if args.s2 else bytes(0x80)
    blobs.append(bank.add(bytes(lava)))                     # TRI_BLOB_S2_HTZ_LAVA (41): CyclingPal_Lava
    lantern = s2rom[s2_objects.symbols()['CyclingPal_Lantern']:][:8] if args.s2 else bytes(8)
    blobs.append(bank.add(bytes(lantern)))                  # TRI_BLOB_S2_MCZ_LANTERN (42): CyclingPal_Lantern
    oil = s2rom[s2_objects.symbols()['CyclingPal_Oil']:][:16] if args.s2 else bytes(16)
    blobs.append(bank.add(bytes(oil)))                      # TRI_BLOB_S2_OOZ_OIL (43): CyclingPal_Oil
    s2s_ = s2_objects.symbols() if args.s2 else {}
    tubes = s2rom[s2s_['off_273F2']:s2s_['Ani_obj67']] if args.s2 else bytes(4)
    blobs.append(bank.add(bytes(tubes)))                    # TRI_BLOB_S2_MTZ_TUBES (44): Obj67 paths (misc/obj67.asm)
    mtzpal = (s2rom[s2s_['CyclingPal_MTZ1']:][:12] + s2rom[s2s_['CyclingPal_MTZ2']:][:12]
              + s2rom[s2s_['CyclingPal_MTZ3']:][:20]) if args.s2 else bytes(44)
    blobs.append(bank.add(bytes(mtzpal)))                   # TRI_BLOB_S2_MTZ_PALCYC (45): CyclingPal_MTZ1 (0), 2 ($C), 3 ($18)
    pilot = s2_objects.pilot_blob(s2rom, s2s_) if args.s2 else bytes(9 * 6 * 32)
    blobs.append(bank.add(bytes(pilot)))                    # TRI_BLOB_S2_PILOT (46): Tornado pilot frames, 6 tiles each
    wfzpal = (s2rom[s2s_['CyclingPal_WFZFire']:][:32] + s2rom[s2s_['CyclingPal_WFZBelt']:][:32]
              + s2rom[s2s_['CyclingPal_WFZ1']:][:68] + s2rom[s2s_['CyclingPal_WFZ2']:][:24]) if args.s2 else bytes(156)
    blobs.append(bank.add(bytes(wfzpal)))                   # TRI_BLOB_S2_WFZ_PALCYC (47): Fire 0, Belt $20, WFZ1 $40, WFZ2 $84
    import s2_ending
    ending = s2_ending.build(s2rom) if args.s2 else bytes(8)
    blobs.append(bank.add(ending))                          # TRI_BLOB_S2_ENDING (48): Sonic 2's ending (s2_ending.py)
    import s2_special
    s2ss = s2_special.build(s2rom) if args.s2 else bytes(8)
    blobs.append(bank.add(s2ss))                            # TRI_BLOB_S2_SS (49): Sonic 2's half-pipe special stage (s2_special.py)
    import s2_sfx
    if args.s2:
        sfxtab, sfxsize, sfxids = s2_sfx.build(s2rom, s3krom, bank, s2_objects.symbols(), envs)
        print(f"  S2 sound set: {len(sfxids)} S2 effects + {len(s2_sfx.S3K_KEEP)} S3K, {sfxsize} bytes")
    else:
        sfxtab = bytes(1 + (s2_sfx.LAST - s2_sfx.FIRST + 1) * 2 + (s2_sfx.S2_LAST - s2_sfx.S2_FIRST + 1))
    blobs.append(bank.add(sfxtab))                          # TRI_BLOB_S2_SFX (50): the Sonic 2 sound set (s2_sfx.py)
    import s2_dac
    if args.s2:
        dactab, dacsize = s2_dac.build(s2rom, bank, s2_objects.symbols())
        print(f"  S2 DAC samples: {dacsize} bytes")
    else:
        dactab = bytes(3)
    blobs.append(bank.add(dactab))                          # TRI_BLOB_S2_DAC (51): Sonic 2's DAC samples bank (s2_dac.py)
    if args.s2:                                             # TRI_BLOB_S2_CNZ_BUMPERS (52): SpecialCNZBumpers_Act1/2
        b1, b2 = s2s_['SpecialCNZBumpers_Act1'], s2s_['SpecialCNZBumpers_Act2']
        act1 = bytes(6) + s2rom[b1:b2]
        act2 = s2rom[b2:b2 + (b2 - b1)]
        bumpers = struct.pack('>H', 2 + len(act1)) + act1 + act2
    else:
        bumpers = struct.pack('>H', 2) + bytes([0, 0, 0xFF, 0xFF, 0, 0])
    blobs.append(bank.add(bumpers))
    import knux_walldash                                    # TRI_BLOB_KNUX_WALLDASH (53): his spin dash + dust, turned up a wall
    blobs.append(bank.add(knux_walldash.build(Path(args.s3k).read_bytes())))
    print(f"  music: {len(sizes)} S1 songs, {sum(sizes)} bytes")
    struct.pack_into('>I', bank.data, 0x20, bank.add(b''.join(struct.pack('>I', a) for a in blobs)))
    build_title_cards(bank, Path(args.s3k).read_bytes())
    blob = bank.finish()
    Path(args.out).write_bytes(blob)
    print(f"  palettes: {len(bank.pals)} (ids ${PAL_BASE:02X}-${PAL_BASE + len(bank.pals) - 1:02X})")
    print(f"trilogy.bin: {len(blob)} bytes, CRC32 {zlib.crc32(blob):08X}")



# --------------------------------------------------------------- title cards
# Glyphs cut from the retail S3K title-card letter art. (zone, first tile at
# $54D-relative VRAM, columns). 'common' = RedAct art at $500 (E N O Z), which
# every card loads. W is M drawn with the hardware V-flip; Q is drawn here.
S3K_SYMS = {'TitleCard_LevelGfx': 0x02DA16, 'ArtKosM_TitleCardRedAct': 0x0D6F28,
            'ArtKosM_TitleCardNum2': 0x0D6E46, 'ArtNem_S22POptions': 0x0CA5E0}
GLYPHS = {
    'A': (0, 0x54D, 2), 'B': (2, 0x553, 2), 'C': (1, 0x54D, 2), 'D': (0, 0x553, 2),
    'E': ('common', 0x51C, 2), 'F': (4, 0x559, 2), 'G': (0, 0x559, 2), 'H': (1, 0x559, 2),
    'I': (0, 0x55F, 1), 'K': (10, 0x559, 2), 'L': (0, 0x562, 1), 'M': (2, 0x568, 3),
    'N': ('common', 0x522, 2), 'O': ('common', 0x528, 3), 'P': (5, 0x55C, 2),
    'R': (1, 0x562, 2), 'S': (0, 0x565, 2), 'T': (4, 0x571, 2), 'U': (6, 0x56E, 2),
    'V': (3, 0x577, 2), 'Y': (1, 0x56E, 2), 'Z': ('common', 0x531, 2),
    'W': ('vflipM', 0, 3), 'Q': ('custom', 0, 3),
}
ZONE_NAMES = {
    0x20: 'GREEN HILL', 0x21: 'LABYRINTH', 0x22: 'MARBLE', 0x23: 'STAR LIGHT',
    0x24: 'SPRING YARD', 0x25: 'SCRAP BRAIN', 0x26: 'FINAL',
    0x28: 'EMERALD HILL', 0x29: 'CHEMICAL PLANT', 0x2A: 'AQUATIC RUIN', 0x2B: 'CASINO NIGHT',
    0x2C: 'HILL TOP', 0x2D: 'MYSTIC CAVE', 0x2E: 'OIL OCEAN', 0x2F: 'METROPOLIS',
    0x30: 'SKY CHASE', 0x31: 'WING FORTRESS', 0x32: 'DEATH EGG',
}


def level_art_modules(bank, art):
    """Level art as LevelLoadBlock's two KosM modules. LoadLevelLoadBlock loads
    the second at the first's header word, and a full $8000-byte module's
    header is $A000 (tile $500), so a two-module art splits below $8000."""
    if len(art) <= 0x8000:
        a = bank.add(kosm_encode(art))
        return a, a
    return bank.add(kosm_encode(art[:LEVEL_ART_SPLIT])), bank.add(kosm_encode(art[LEVEL_ART_SPLIT:]))


LEVEL_ART_SPLIT = 0x7000


def _kosm_decode(rom, a):
    size = be16(rom, a)
    a += 2
    if size == 0xA000:
        size = 0x8000
    out = b''
    while len(out) < size:
        d, e = kosinski_decode(rom, a)
        out += d
        ln = e - a
        a += ln + ((-ln) & 15)
    return out


def _q_glyph(o_tiles):
    """O (3x3 tiles, column-major) plus a diagonal tail at the lower right."""
    px = [[0] * 24 for _ in range(24)]
    for x in range(24):
        for y in range(24):
            t = (x // 8) * 3 + y // 8
            b = o_tiles[t * 32 + (y % 8) * 4 + (x % 8) // 2]
            px[y][x] = (b >> 4) if x % 2 == 0 else b & 15
    # tail: a 2px white stroke with outline, from inside the bowl out past
    # the bottom-right edge (the font's shadow colour F is left untouched).
    for i in range(9):
        x, y = 13 + i, 15 + i
        for dx in range(2):
            if 0 <= x + dx < 24 and 0 <= y < 24:
                px[y][x + dx] = 1
        if 0 <= x + 2 < 24 and 0 <= y < 24:
            px[y][x + 2] = 0xC
        if 0 <= x - 1 < 24 and 0 <= y < 24 and px[y][x - 1] in (0, 0xF):
            px[y][x - 1] = 0xC
    out = bytearray(9 * 32)
    for x in range(24):
        for y in range(24):
            t = (x // 8) * 3 + y // 8
            o = t * 32 + (y % 8) * 4 + (x % 8) // 2
            if x % 2 == 0:
                out[o] = (out[o] & 0x0F) | (px[y][x] << 4)
            else:
                out[o] = (out[o] & 0xF0) | px[y][x]
    return bytes(out)


def build_title_cards(bank, s3k):
    lg = S3K_SYMS['TitleCard_LevelGfx']
    zone_art = {z: _kosm_decode(s3k, be32(s3k, lg + z * 4)) for z in range(14)}
    red = _kosm_decode(s3k, S3K_SYMS['ArtKosM_TitleCardRedAct'])

    def glyph_tiles(ch):
        src, base, w = GLYPHS[ch]
        if src == 'common':
            return None
        if src == 'vflipM':
            src, base, w = GLYPHS['M']
        if src == 'custom':
            return _q_glyph(red[(0x528 - 0x500) * 32:(0x528 - 0x500 + 9) * 32])
        art = zone_art[src]
        o = (base - 0x54D) * 32
        return art[o:o + w * 3 * 32]

    frames = {}
    for zone, name in ZONE_NAMES.items():
        tiles = bytearray()
        placed = {}
        pieces = []
        x = 0
        for ch in name:
            if ch == ' ':
                x += 8
                continue
            src, base, w = GLYPHS[ch]
            key = 'M' if ch == 'W' else ch
            flip = 0x1000 if ch == 'W' else 0
            if src == 'common':
                tile = base
            else:
                if key not in placed:
                    placed[key] = 0x54D + len(tiles) // 32
                    tiles += glyph_tiles(ch)
                tile = placed[key]
            pieces.append((x, ((w - 1) << 2) | 2, 0x8000 | flip | tile))
            x += w * 8
        start = 128 - x
        frame = struct.pack('>H', len(pieces))
        for px, size, tw in pieces:
            frame += struct.pack('>bBHh', 0, size, tw, start + px)
        art_a = bank.add(kosm_encode(bytes(tiles) if tiles else bytes(32)))
        frames[zone] = (art_a, frame)
    # mapping table: frame index = zone - $20 + 1 (frame 0 stays empty: the
    # multi-draw renderer treats frame 0 as "no main frame")
    offs = bytearray(66)
    body = bytearray()
    empty = struct.pack('>H', 0)
    for i in range(33):
        z = 0x20 + i - 1
        struct.pack_into('>H', offs, i * 2, 66 + len(body))
        body += frames[z][1] if (i and z in frames) else empty
    map_a = bank.add(bytes(offs + body))
    art_tab = b''.join(struct.pack('>I', frames[0x20 + i][0] if (0x20 + i) in frames else 0) for i in range(32))
    art_tab_a = bank.add(art_tab)
    struct.pack_into('>II', bank.data, 0x18, art_tab_a, map_a)
    print(f"  title cards: {len(frames)} zone names")


if __name__ == '__main__':
    main()
