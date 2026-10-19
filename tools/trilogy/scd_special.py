#!/usr/bin/env python3
"""Sonic CD's special stage (the UFO stages), from the user's own disc, as one
blob for trilogy_scdss.c: scdss.bin beside the executable (never in the repo).

    scd_special.py --disc C:\\Users\\...\\scd_ref\\disc [--out build/Release/scdss.bin]

The stage is two programs: SPSS__.BIN, the Sub CPU's (loaded at $10000: the
objects, the stamp map and the graphics chip's trace table) and SPMM__.MMD,
the Main CPU's (at $FF2000: the VDP, the HUD, the backgrounds, the palette).
Each stage's own block of art, palette and tilemaps sits in the Sub program
at $18000 and runs copied to $FF8C00, right after the Main program. The blob
carries both programs as they are (trilogy_scdss.c reads the objects'
pointer-linked data at its own addresses), the addresses of what it needs
(found by the code that refers to them: the disc decides the layout), and
the bulk data decoded: stamps and stamp maps (Kosinski), art (Nemesis) and
the tilemaps the stages and the results draw.

scdss.bin (big-endian):
  "TSSS", u16 version (1), u16 items, items x (u32 offset, u32 length)
  item 0 Sub program ($10000..), 1 Main program ($FF2000..), 2 labels (u32
  each, LABELS order), 3..10 stage blocks ($FF8C00..), 11..18 stamps (stage
  3: its second set appended at $10000 in the stamp space), 19..26 stamp
  maps, 27 the common art (u16 count, count x (u16 VRAM address, u16 bytes /
  32), then the tiles), 28..35 each stage's background art (same layout),
  36 the results art (same layout), 37..44 each stage's tilemaps, 45 the
  common tilemaps: u16 count, count x (u8 id, u8 0, u16 VRAM address, u16
  width, u16 height, u32 offset of the words from the item's start)
  The time warp (WARP__.MMD; trilogy_scdwarp.c): 46 its art (as 27), 47 the
  background's 40 x 28 words, 48 its palette, 49 Sonic's and 50 the
  sparkles' sprites: u8 animations; each u8 frames, u8 speed, frames x (u8
  pieces, pieces x (i8 y, u8 size, u16 tile, i8 x))
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mdcodecs import kosinski_decode, nemesis_decode  # noqa: E402
from scd_mmd import scan  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SUB_BASE, MAIN_BASE, STAGE_BASE = 0x10000, 0xFF2000, 0xFF8C00
STAGE_TABLE = 0x18000

# addresses trilogy_scdss.c reads by (same order there: SSL_*)
LABELS = ['StampTypes', 'StampAnims', 'UFOPaths', 'TimeUFOPath', 'StartPos', 'SonicArt',
          'MapSpr_Sonic', 'MapSpr_Shadow', 'MapSpr_UFORings', 'MapSpr_UFOShoes', 'MapSpr_Splash',
          'MapSpr_Item', 'MapSpr_Dust', 'MapSpr_Explosion', 'MapSpr_PressStart', 'MapSpr_TimeStone',
          'MapSpr_TitleCardText', 'MapSpr_TitleCardBar', 'Pal_Stage', 'Pal_Results', 'Tilemaps',
          'StagePal']                                   # (StagePal: 8 addresses, the stage block's palettes)


def vdp_vram(cmd):
    """the VRAM address of a VDP write command"""
    return ((cmd >> 16) & 0x3FFF) | ((cmd & 3) << 14)


class Prog:
    def __init__(self, data, base):
        self.data, self.base = data, base

    def b(self, a): return self.data[a - self.base]
    def w(self, a): return int.from_bytes(self.data[a - self.base:a - self.base + 2], 'big')
    def l(self, a): return int.from_bytes(self.data[a - self.base:a - self.base + 4], 'big')
    def has(self, a): return self.base <= a < self.base + len(self.data)

    def scan(self, sig, lo=None, hi=None):
        return [(o + self.base, g, n) for o, g, n in
                scan(self.data, sig, (lo - self.base) if lo else 0, (hi - self.base) if hi else None)]

    def one(self, sig, what, lo=None, hi=None):
        hits = self.scan(sig, lo, hi)
        if not hits:
            raise SystemExit(f'scd_special: {what} not found ({sig})')
        return hits[0]


def pcx(at, disp):
    """(d8,PC,Xn): the extension word's address plus the displacement"""
    return at + 2 + (disp - 0x100 if disp & 0x80 else disp)


def pcw(at, disp):
    """(d16,PC)"""
    return at + 2 + (disp - 0x10000 if disp & 0x8000 else disp)


def sub_labels(sub):
    lab = {}
    at, (d,), _ = sub.one('C0FC 000C 23FB 00 (B)', 'InitStampAnim')
    lab['StampAnims'] = pcx(at + 4, d)
    at, (d,), _ = sub.one('E548 23FB 00 (B)', 'GetStampTypes')
    lab['StampTypes'] = pcx(at + 2, d)
    at, (d,), _ = sub.one('E548 227B 00 (B)', 'SpawnUFOs')
    lab['UFOPaths'] = pcx(at + 2, d)
    at, (d,), _ = sub.one('47FA (W) 4A12', 'TimeUFOPath')
    lab['TimeUFOPath'] = pcw(at, d)
    at, (d,), _ = sub.one('C0FC 0006 317B 00 (B) 001C', 'ObjSonic_GetStartPos')
    lab['StartPos'] = pcx(at + 4, d)
    at, (a,), _ = sub.one('43F9 (L) D040 D040 2271 0000', 'ObjSonic_LoadArt')
    lab['SonicArt'] = a
    inits = sub.scan('317C (W) 0004 217C (L) 0006')     # move.w #tile,oTile(a0); move.l #maps,oMap(a0)
    by_tile = {}
    for at, (tile, maps), _ in inits:
        by_tile.setdefault(tile, []).append(maps)
    lab['MapSpr_Sonic'] = by_tile[0x85E0][0]
    lab['MapSpr_Shadow'] = by_tile[0xE6DC][0]
    lab['MapSpr_Item'] = by_tile[0x878F][0]
    lab['MapSpr_Splash'] = by_tile[0x8582][0]
    lab['MapSpr_Dust'], lab['MapSpr_Explosion'] = by_tile[0x87AE][0], by_tile[0x87AE][1]
    lab['MapSpr_PressStart'] = by_tile[0x856A][0]
    lab['MapSpr_TimeStone'] = by_tile[0xE424][0]
    lab['MapSpr_TitleCardText'], lab['MapSpr_TitleCardBar'] = by_tile[0x8516][0], by_tile[0x8516][1]
    # the UFOs set their tile, follow their path, then their mappings
    at = sub.one('317C E440 0004', 'ObjUFO_Init')[0]
    maps = sub.scan('217C (L) 0006', at, at + 0x40)
    lab['MapSpr_UFORings'], lab['MapSpr_UFOShoes'] = maps[0][1][0], maps[1][1][0]
    return lab


def stamp_sets(sub):
    """[(stamps, stamp map)] per stage, and stage 3's second stamps"""
    hits = sub.scan('41F9 (L) 43F9 (L)')
    pairs = [(a, m) for _, (a, m), _ in hits if sub.has(a) and sub.has(m) and a > 0x30000]
    if len(pairs) != 8:
        raise SystemExit(f'scd_special: {len(pairs)} stamp loads, not 8')
    extra = sub.one('41F9 (L) 43F9 0009 0000', 'Stamps_SS3_2')[1][0]   # (lea WORDRAM2M+$10000,a1: 2M at $80000)
    return pairs, extra


def nem(prog, a):
    data, _ = nemesis_decode(prog.data, a - prog.base)
    return bytes(data)


def art_list(entries):
    out = struct.pack('>H', len(entries))
    tiles = b''
    for vram, data in entries:
        out += struct.pack('>HH', vram, len(data) // 32)
        tiles += data
    return out + tiles


def warp_items(disc):
    """The time warp cutscene's data (WARP__.MMD), found by the code that loads it"""
    mmd = (disc / 'WARP__.MMD').read_bytes()
    prog = Prog(mmd[0x100:], int.from_bytes(mmd[2:6], 'big'))
    at, (cmd, d), _ = prog.one('23FC (L) 00C0 0004 41FA (W) 6100', 'Art_TimeWarp')
    art = art_list([(vdp_vram(cmd), nem(prog, pcw(at + 10, d)))])
    at, (d,), _ = prog.one('43FA (W) 203C 6000 0003', 'Map_TimeWarpBg')
    bg = bytes(prog.data[pcw(at, d) - prog.base:pcw(at, d) - prog.base + 40 * 28 * 2])
    at, (d, _, n), _ = prog.one('41FA (W) 43F8 (W) 7E (B)', 'Pal_TimeWarp')
    pal = bytes(prog.data[pcw(at, d) - prog.base:pcw(at, d) - prog.base + (n + 1) * 4])

    def sprites(tile):
        _, (m,), _ = prog.one('317C %04X 0004 217C (L) 0006' % tile, 'MapSpr')
        anims = []
        k = 0
        while True:                                  # (the animation table: up to its first animation)
            a = prog.l(m + k * 4)
            anims.append(a)
            k += 1
            if m + k * 4 >= min(anims):
                break
        out = bytes([len(anims)])
        for a in anims:
            nf, speed = prog.b(a), prog.b(a + 1)
            out += bytes([nf, speed])
            for f in range(nf):
                fa = prog.l(a + 2 + f * 4)
                c = prog.b(fa)
                n = 0 if c & 0x80 else c + 1
                out += bytes([n]) + bytes(prog.data[fa + 1 - prog.base:fa + 1 + n * 5 - prog.base])
        return out
    return [art, bg, pal, sprites(0x8000), sprites(0x0000)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--disc', required=True, help='the extracted disc (scd_iso.py)')
    ap.add_argument('--out', default=str(ROOT / 'build' / 'Release' / 'scdss.bin'))
    a = ap.parse_args(argv)
    disc = Path(a.disc)
    sub = Prog((disc / 'SPSS__.BIN').read_bytes(), SUB_BASE)
    mmd = (disc / 'SPMM__.MMD').read_bytes()
    if int.from_bytes(mmd[2:6], 'big') != MAIN_BASE:
        raise SystemExit('scd_special: SPMM__.MMD does not load at $FF2000')
    main_ = Prog(mmd[0x100:], MAIN_BASE)

    lab = sub_labels(sub)
    # the Main program
    pal = main_.scan('41FA (W) 43F8 (W) 7E (B) 22D8 51CF FFFC')
    lab['Pal_Stage'], lab['Pal_Results'] = pcw(pal[0][0], pal[0][1][0]), pcw(pal[1][0], pal[1][1][0])
    at, (d,), _ = main_.one('CCFC 000C 207B 60 (B)', 'DrawTilemaps')
    lab['Tilemaps'] = pcx(at + 4, d) + 0xC
    # each stage's block, as the Sub program keeps it
    blocks = [(sub.l(STAGE_TABLE + k * 6), sub.w(STAGE_TABLE + k * 6 + 4)) for k in range(8)]
    stage_data = [sub.data[p - SUB_BASE:p - SUB_BASE + n] for p, n in blocks]
    # each stage's loader: background art, tilemaps, palette
    loads = main_.scan('23FC 6020 0001 00C0 0004 41F9 (L)')
    if len(loads) != 8:
        raise SystemExit(f'scd_special: {len(loads)} stage loaders, not 8')
    bounds = [at for at, _, _ in loads] + [loads[-1][0] + 0x80]
    stage_art, stage_maps, stage_pals = [], [], []
    for k in range(8):
        lo, hi = bounds[k], bounds[k + 1]
        st = Prog(stage_data[k], STAGE_BASE)
        arts = [(vdp_vram(cmd), nem(st if st.has(art) else main_, art))   # (stage 6's water: the Main program's)
                for _, (cmd, art), _ in main_.scan('23FC (L) 00C0 0004 41F9 (L)', lo, hi)]
        stage_art.append(art_list(arts))
        ids = []
        calls = sorted(main_.scan('2A3C (L) 4EB9 (L)', lo, hi) + main_.scan('7A (B) 4EB9 (L)', lo, hi))
        for at, g, n in calls:
            v = g[0]
            ids += [(v >> s) & 0xFF for s in (0, 8, 16, 24) if (v >> s) & 0xFF not in (0, 0x12, 0x13)]   # (not the HUD's)
        stage_maps.append(ids)
        pals = main_.scan('41F9 (L)', lo, hi)
        stage_pals.append([p for _, (p,), _ in pals if st.has(p)][-1])
    lab['StagePal'] = stage_pals
    # the common art (Start), the results'
    common = [(vdp_vram(cmd), nem(main_, pcw(at + 10, d)))
              for at, (cmd, d), _ in main_.scan('23FC (L) 00C0 0004 41FA (W) 6100')[:8]]   # (Start's loads)
    res = main_.one('23FC 4000 0000 00C0 0004 41FA (W)', 'Art_Results')
    results_art = art_list([(0, nem(main_, pcw(res[0] + 10, res[1][0])))])

    # tilemaps: (id, VRAM, w, h, words), resolved in a block (or the Main program)
    def tilemap(t, prog_for):
        e = lab['Tilemaps'] + (t - 1) * 0xC
        p, cmd, w, h = main_.l(e), main_.l(e + 4), main_.w(e + 8) + 1, main_.w(e + 0xA) + 1
        prog = prog_for(p)
        if prog is None:
            return None
        if prog.w(p):
            words, _ = kosinski_decode(prog.data, p + 2 - prog.base)
        else:
            words = prog.data[p + 2 - prog.base:p + 2 - prog.base + w * h * 2]
        return (t, vdp_vram(cmd), w, h, bytes(words[:w * h * 2]))

    count = 0
    while main_.has(lab['Tilemaps'] + count * 0xC + 0xB) and count < 0x60:
        p = main_.l(lab['Tilemaps'] + count * 0xC)
        if not (main_.has(p) or STAGE_BASE <= p < STAGE_BASE + 0x8000):
            break
        count += 1

    def map_table(maps):
        head = struct.pack('>H', len(maps))
        body = b''
        off = 2 + len(maps) * struct.calcsize('>BBHHHI')
        for t, vram, w, h, words in maps:
            head += struct.pack('>BBHHHI', t, 0, vram, w, h, off + len(body))
            body += words
        return head + body

    common_maps = [m for m in (tilemap(t, lambda p: main_ if main_.has(p) else None) for t in range(1, count + 1)) if m]
    per_stage = []
    for k in range(8):
        st = Prog(stage_data[k], STAGE_BASE)
        per_stage.append(map_table([m for m in (tilemap(t, lambda p, st=st: st if st.has(p) else None)
                                                for t in stage_maps[k]) if m]))

    pairs, extra = stamp_sets(sub)
    stamps, smaps = [], []
    for k, (sa, ma) in enumerate(pairs):
        s, _ = kosinski_decode(sub.data, sa - SUB_BASE)
        s = bytes(s)
        if k == 2:                                           # (stage 3: its second set at $10000 in Word RAM)
            s2, _ = kosinski_decode(sub.data, extra - SUB_BASE)
            s = s.ljust(0x10000 - 0x200, b'\0') + bytes(s2)
        stamps.append(s)
        m, _ = kosinski_decode(sub.data, ma - SUB_BASE)
        smaps.append(bytes(m))

    labels = b''
    for name in LABELS:
        v = lab[name]
        labels += b''.join(struct.pack('>I', x) for x in (v if isinstance(v, list) else [v]))
    items = [sub.data, main_.data, labels] + stage_data + stamps + smaps + [art_list(common)] + stage_art \
        + [results_art] + per_stage + [map_table(common_maps)] + warp_items(disc)
    head = b'TSSS' + struct.pack('>HH', 1, len(items))
    off = len(head) + len(items) * 8
    table, body = b'', b''
    for it in items:
        table += struct.pack('>II', off + len(body), len(it))
        body += it
        while len(body) & 3:
            body += b'\0'
    out = Path(a.out)
    out.write_bytes(head + table + body)
    for name in LABELS:
        v = lab[name]
        print(f'  {name}: ' + (' '.join(f'${x:06X}' for x in v) if isinstance(v, list) else f'${v:06X}'))
    print(f'  stages: blocks {[hex(p) for p, _ in blocks]}, tilemaps {stage_maps}, '
          f'{len(common)} common arts, {len(common_maps)} common tilemaps of {count}')
    print(f'{len(items)} items, {len(head + table + body)} bytes -> {out}')


if __name__ == '__main__':
    main()
