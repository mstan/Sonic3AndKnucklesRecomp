#!/usr/bin/env python3
"""Sonic CD level files: find and decode a level's data inside its MMD program.

Input: the files of the player's own disc (scd_iso.py extracts them). A level,
R<round><act><time>__.MMD (and DEMO11A/43C/82A.MMD), is one 68000 program with
its data, loaded whole into 2M Word RAM. The $100-byte MMD header has origin 0
(= Word RAM), so it sits at $200000, the program starts at $200100 (the
header's entry long) and file offset = address - $200000.

Every level is the same engine source assembled with its own data, so nothing
is at a fixed offset: each table is found through the instruction that
references it (SIGS / OPT_SIGS, instruction bytes taken from the R11A
disassembly and checked on all 73 files). `--dobby` compares R11A-D, DEMO11A
and R61A with the disassembly: its ripped binaries byte for byte, and the
tables it writes as source (bounds, PLC lists, sections) by value. Formats:

  LevelDataIndex  +0 long  PLC id << 24 | level tiles (Nemesis; PLC'd to VRAM 0)
                  +4 long  PLC id << 24 | blocks (Nemesis -> blockBuffer)
                  +8 long  chunks (uncompressed, read in place, $10000-aligned)
                  +13 byte S1 music id (unused), +15 byte level palette id
  block           8 bytes: 4 VDP pattern words TL TR BL BR (priority, palette,
                  flips, tile exactly as the VDP takes them)
  chunk           256x256 px, 16x16 words, $200 bytes; id n at LevelChunks +
                  (n-1)*$200, 0 = empty. Word: bits 0-9 block, 11 xflip, 12
                  yflip, 13 top solid (floor checks), 14 left/right/bottom solid
                  (wall and ceiling checks). Bit 15 (set in most levels) is never
                  read; bit 10 only reaches the collision index and is set only
                  in garbage cells (demo-overwritten chunks, QQZ1 BG chunk $71)
  layout          byte w-1, byte h-1, h rows of w chunk bytes; bit 7 = "loop"
                  chunk (collision may read another chunk: LOOP_RULES). Loaded
                  into a 128-byte-stride RAM map, FG at +0, BG at +$40
  collision       LevelCollision[word & $7FF] = shape; ColAngleMap[shape],
                  ColHeightMap[shape*16 + x], ColWidthMap[shape*16 + y]
  object          8 bytes: x, y (bits 0-11; bit 14 xflip, 15 yflip), id (bit 7
                  = has a saved-state slot), subtype, periods (bit 5 past, 6
                  present, 7 future), subtype 2; ends at x = $FFFF
  PLC list        word count-1, then (long Nemesis art, word VRAM address)

Nothing read here goes into the repository.
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mdcodecs import nemesis_decode  # noqa: E402

BASE = 0x200000                      # 2M Word RAM: MMD header, program at $200100
ROUNDS = {1: 'PPZ', 3: 'CCZ', 4: 'TTZ', 5: 'QQZ', 6: 'WWZ', 7: 'SSZ', 8: 'MMZ'}
ZONE_ID = {1: 0, 3: 1, 4: 2, 5: 3, 6: 4, 7: 5, 8: 6}   # the engine's `zone` byte
PERIOD = {'A': 1, 'B': 0, 'C': 2, 'D': 2}               # timeZone: 0 past, 1 present, 2 future
NAME_RE = re.compile(r'(?:DEMO|R)(\d)(\d)([A-D])')

# Code signatures: hex bytes, '??' any byte, '(L)' '(W)' '(B)' a captured long/word/byte.
SIGS = {
    # LoadLevelData: moveq #0,d0; lea LevelDataIndex,a2; move.l a2,-(sp); addq.l #4,a2;
    # move.l (a2)+,d1; andi.l #$3FFFFF,d1
    'LevelDataIndex': ['7000 45F9 (L) 2F0A 588A 221A 0281 003F FFFF'],
    # LoadLevelLayout.LoadPlane: moveq #0,d0; add.w d1,d0; lea LevelLayouts,a1;
    # move.w (a1,d0.w),d0; lea (a1,d0.w),a1
    'LevelLayouts': ['7000 D041 43F9 (L) 3031 0000 43F1 0000'],
    # FindLevelFloor/Wall: movea.l collisionPtr.w,a2; move.b (a2,d0.w),d0; andi.w #$FF,d0
    'collisionPtr': ['2478 (W) 1032 0000 0240 00FF'],
    # ... lea ColAngleMap,a2; move.b (a2,d0.w),(a4)
    'ColAngleMap': ['45F9 (L) 18B2 0000'],
    # add.w d0,d1; lea Col*Map,a2; move.b (a2,d1.w),d0; ext.w d0; eor.w d6,d4; btst #$C/#$B,d4
    'ColHeightMap': ['D240 45F9 (L) 1032 1000 4880 BD44 0804 000C'],
    'ColWidthMap': ['D240 45F9 (L) 1032 1000 4880 BD44 0804 000B'],
    # LevelSizeLoad_StartPos: lea LevelStartLoc,a1; moveq #0,d1; move.w (a1)+,d1 --
    # or a table indexed by demoMode: lea LevelStartLoc,a1; tst.w demoMode; bpl.s
    'LevelStartLoc': ['43F9 (L) 7200 3219', '43F9 (L) 4A79 ???????? 6A'],
    # LevelSizeLoad: lea CamBounds,a0; move.w (a0)+,d0; move.w d0,unused.w
    'CamBounds': ['41F9 (L) 3018 31C0'],
    # lea SpecChunks,a1; move.l (a1),specialChunks.w
    'SpecChunks': ['43F9 (L) 21D1'],
    # LoadPalette: lea PaletteTable,a1; lsl.w #3,d0; adda.w d0,a1; movea.l (a1)+,a2;
    # movea.w (a1)+,a3; move.w (a1)+,d7
    'PaletteTable': ['43F9 (L) E748 D2C0 2459 3659 3E19'],
    # SpawnObjects_Init: lea ObjectLayouts,a0; movea.l a0,a1; adda.w (a0),a0
    'ObjectLayouts': ['41F9 (L) 2248 D0D0'],
    # LoadPLC/InitPLC/LoadPLCImm: lea PLCLists,a1; add.w d0,d0; move.w (a1,d0.w),d0; lea (a1,d0.w),a1
    'PLCLists': ['43F9 (L) D040 3031 0000 43F1 0000'],
    # RunObjects: add.w d0,d0 (x2); lea ObjectIndex,a1; movea.l -4(a1,d0.w),a1; jsr (a1)
    'ObjectIndex': ['D040 D040 43F9 (L) 2271 00FC 4E91'],
    # LevelStart: move #$2700,sr; move.l #DemoData,demoDataPtr.w (set in every build)
    'DemoData': ['46FC 2700 21FC (L)'],
}
# Optional features: absent from levels that do not use them.
OPT_SIGS = {
    # LoadSectionArt: lea SectionRanges(pc),a1; ...; move.b d1,sectionID; move.w SectionInitPLCs(pc,d1.w),d0
    'LoadSectionArt': '43FA (W) 7000 7200 3038 ???? B059 6504 5401 60F8 13C1 ???????? 303B 10 (B)',
    # UpdateSectionArt: same walk; cmp.b sectionID,d1; bne.s; rts; ...; move.w SectionUpdatePLCs(pc,d1.w),d0
    'UpdateSectionArt': '43FA (W) 7000 7200 3038 ???? B059 6504 5401 60F8 B239 ???????? 6602 4E75 13C1 ???????? 303B 10 (B)',
    # LoadWaterPal: as LoadPalette, then suba.w #palette-waterPalette,a3
    'LoadWaterPal': '43F9 ???????? E748 D2C0 2459 3659 96FC',
    # TTZ: add.w d1,d1; add.w d1,d0 (act*8 + period*2); lea WaterHeights,a1; move.w (a1,d0.w),d0; move.w d0,..
    'WaterHeights': 'D241 D041 43F9 (L) 3031 0000 31C0',
    # LevelStart: move.w #$202F,palFadeInfo.w; jsr UpdateAnimTiles
    'UpdateAnimTiles': '31FC 202F ???? 4EB9 (L)',
    # main loop: tst.w timeStopTimer; bne.s; bsr.w PaletteCycle; jsr UpdateSectionArt
    'PaletteCycle': '4A79 ???????? 6604 6100 (W) 4EB9',
    # Sonic: cmpi.b #$2B,oAnim(a0); beq.s; jsr Player_ObjCollide; bsr.w ObjSonic_SpecialChunks; rts
    'SpecialChunkCheck': '0C28 002B 001C 6706 4EB9 ???????? 6100 (W) 4E75',
    # QQZ BG sections: moveq #0,d1; move.b BgSel(pc,d2.w),d1; moveq #0,d0; move.b d1,d0; lsr.b #2,d0;
    # move.w d0,d2; addi.b #plcbase,d0; bsr.w LoadPLC; move.w d2,d0; addq.b #palbase,d0; move.w d0,d2
    'BgSections': '7200 123B 20 (B) 7000 1001 E408 3400 0600 (W) 6100 ???? 3002 (W) 3400',
    # CycleColor (Palette Cycle Script.asm): subq.b #1,(a5); bpl.s; moveq #0,d0; move.b (a1)+,d0;
    # move.b (a1)+,d1; add.w d0,d0; lea palette.w,a3
    'CycleColor': '5315 6A?? 7000 1019 1219 D040 47F8',
    # SSZ's copy (+ moveq #0,d1) also stores a second colour into the water palette:
    # add.w d1,d1; add.w d0,d1; move.w (a2,d1.w),-$80(a3)
    'CycleColor2': '5315 6A?? 7000 7200 1019 1219 D040 47F8',
}

# GetLevelBlock (Collision Floor.asm) is byte-identical in every level file. Drawing always
# uses layout byte & $7F; collision resolves a loop chunk (bit 7 set) per zone:
#   PPZ CCZ TTZ QQZ: the object's oSprFlags bit 6 set -> chunk+1, and $29 becomes $51 --
#     Sonic 1's rule; Sonic's SpecChunks check drives bit 6 (S1 Sonic_Loops)
#   WWZ: touching loop chunk 'enter_high' sets bit 6 (and drops sprite priority); on bit 6
#     the 'high' chunks remap; any non-loop chunk clears bit 6
#   SSZ: players' sprites go high priority on loop chunks; 'bit6' chunks swap to +1 when
#     bit 6 is set; with the `layer` flag set the sprite drops to low priority and the
#     'layer' chunks swap to +1
#   MMZ: players only: high priority on loop chunks; with `layer` set, low priority and
#     the 'layer' chunks remap
# On SSZ/MMZ non-loop chunks also force the sprite to low priority.
LOOP_RULES = {
    'default': {'bit6': 'chunk+1, $29->$51'},
    'WWZ': {'enter_high': 0x14, 'high': {0x15: 0x60, 0x1E: 0x61, 0x1F: 0x62, 0x32: 0x63}},
    'SSZ': {'bit6': {0x04: 0x05, 0x06: 0x07},
            'layer': {0x28: 0x29, 0x3C: 0x3D, 0x37: 0x38, 0x2F: 0x30, 0x16: 0x17}},
    'MMZ': {'layer': {0x46: 0x6A, 0x48: 0x6B, 0x4A: 0x6C, 0x10: 0x6D, 0x63: 0x6E, 0x43: 0x6F}},
}


def _hx(h): return re.escape(bytes.fromhex(h))


# The instructions each rule comes from, checked in every file's GetLevelBlock:
# cmpi.b #from,d1; bne.s/.w; move.w #to,d1 -- or cmpi.b #n,d1; beq.s (the +1 swap).
_BNE = _hx('66') + b'(?:[^' + _hx('00') + b']|' + _hx('00') + b'..)'
_RULE_CODE = ([_hx('52410C410029') + _BNE + _hx('323C0051'), _hx('0C0100146600')]
              + [_hx('0C01%04X' % a) + _BNE + _hx('323C%04X' % b) for a, b in LOOP_RULES['WWZ']['high'].items()]
              + [_hx('0C01%04X67' % k) for k in list(LOOP_RULES['SSZ']['bit6']) + list(LOOP_RULES['SSZ']['layer'])]
              + [_hx('0C01%04X' % a) + _BNE + _hx('323C%04X' % b) for a, b in LOOP_RULES['MMZ']['layer'].items()])


def be16(b, a): return (b[a] << 8) | b[a + 1]
def be32(b, a): return (be16(b, a) << 16) | be16(b, a + 2)
def s16(v): return v - 0x10000 if v & 0x8000 else v
def s8(v): return v - 0x100 if v & 0x80 else v


_cache = {}


def _regex(sig):
    if sig not in _cache:
        out = b''
        for tok in sig.split():
            if tok in ('(L)', '(W)', '(B)'):
                out += b'(' + b'.' * {'(L)': 4, '(W)': 2, '(B)': 1}[tok] + b')'
                continue
            for i in range(0, len(tok), 2):
                out += b'.' if tok[i:i + 2] == '??' else _hx(tok[i:i + 2])
        _cache[sig] = re.compile(out, re.S)
    return _cache[sig]


def scan(data, sig, start=0, end=None):
    """[(offset, [captured ints], match length)] for every match of a signature."""
    return [(m.start(), [int.from_bytes(g, 'big') for g in m.groups()], m.end() - m.start())
            for m in _regex(sig).finditer(data, start, end or len(data))]


class Layout:
    """One LevelLayouts plane; rows[r][c] = chunk byte (bit 7 = loop chunk)."""
    def __init__(self, data, addr):
        o = addr - BASE
        self.addr = addr
        self.width, self.height = data[o] + 1, data[o + 1] + 1
        # LoadPlane copies w*h bytes as they are, also where the header claims more rows
        # than the plane stores (QQZ: the last row is the next plane's bytes)
        self.rows = [list(data[o + 2 + r * self.width:o + 2 + (r + 1) * self.width]) for r in range(self.height)]
        self.end = addr + 2 + self.width * self.height
        self.stored_rows = self.height

    def chunk_ids(self):
        return {b for row in self.rows for b in row if b}


class Level:
    def __init__(self, data, name):
        self.data = data = bytes(data)
        self.name = Path(name).stem.rstrip('_').upper()
        m = NAME_RE.match(self.name)
        if not m:
            raise ValueError(f'{name}: not a level file name')
        self.round, self.act, self.time = int(m.group(1)), int(m.group(2)) - 1, m.group(3)
        self.zone, self.zone_id = ROUNDS[self.round], ZONE_ID[self.round]
        self.period = PERIOD[self.time]
        self.good_future = {'C': True, 'D': False}.get(self.time)
        self.demo = self.name.startswith('DEMO')
        if be32(data, 2) != 0 or be32(data, 8) != BASE + 0x100:
            raise ValueError(f'{name}: MMD header is not a Word RAM program at $200100')
        self.addr = {key: self._locate(key) for key in SIGS}
        self.warnings = []
        self._level_data()
        self._layouts()
        self._collision()
        self._palettes()
        self._start_and_bounds()
        self._objects()
        self._plcs()
        self._sections()
        self._water()
        self._bg_sections()
        self._chunk_count()
        self._check_loop_rules()
        self._special_chunk_check()
        self._animation()

    # ------------------------------------------------------------- helpers
    def _locate(self, key):
        vals = {cap[0] for sig in SIGS[key] for _, cap, _ in scan(self.data, sig, 0, 0x10000)}
        if len(vals) != 1:
            raise ValueError(f'{self.name}: {key}: {len(vals)} candidates {sorted(map(hex, vals))}')
        return vals.pop()

    def _opt(self, key, start=0, end=0x10000):
        return scan(self.data, OPT_SIGS[key], start, end)

    def off(self, addr):
        o = addr - BASE
        if not 0 <= o < len(self.data):
            raise ValueError(f'{self.name}: address ${addr:X} outside the file')
        return o

    def w(self, addr): return be16(self.data, self.off(addr))
    def l(self, addr): return be32(self.data, self.off(addr))
    def b(self, addr): return self.data[self.off(addr)]
    def bytes_at(self, addr, n): return self.data[self.off(addr):self.off(addr) + n]

    def nemesis(self, addr):
        out, end = nemesis_decode(self.data, self.off(addr))
        return out, end + BASE

    def _word_table(self, a):
        """A table of word offsets from its own start, ending where the first target begins."""
        offs = []
        while not offs or len(offs) * 2 < min(offs):
            offs.append(self.w(a + len(offs) * 2))
        return offs

    # ------------------------------------------------------------- tiles, blocks, chunks
    def _level_data(self):
        ldi = self.bytes_at(self.addr['LevelDataIndex'], 16)
        # LevelStart: LoadPLCImm(LevelDataIndex[0]): the tiles at VRAM 0, in some levels more
        # level art (QQZ1/2 present $4B40, WWZ2 present $5140) and the checkpoint art
        self.plc_level = ldi[0]
        self.plc_zone = ldi[4]               # LoadLevelData: LoadPLC(LevelDataIndex[4]) once the title card ran
        self.palette_id = ldi[15]            # LoadLevelData: move.w (a2),d0; andi.w #$FF,d0
        self.addr['tiles'] = be32(ldi, 0) & 0x3FFFFF
        self.addr['blocks'] = be32(ldi, 4) & 0x3FFFFF
        self.addr['chunks'] = be32(ldi, 8) & 0x3FFFFF
        # GetLevelBlock builds chunk pointers by replacing the low word only
        if self.addr['chunks'] & 0xFFFF:
            raise ValueError(f'{self.name}: LevelChunks ${self.addr["chunks"]:X} not $10000-aligned')
        self.tiles, _ = self.nemesis(self.addr['tiles'])
        self.blocks, _ = self.nemesis(self.addr['blocks'])
        if len(self.blocks) % 8 or len(self.blocks) > 0x2000:
            raise ValueError(f'{self.name}: block data ${len(self.blocks):X} bytes')

    @property
    def ntiles(self): return len(self.tiles) // 32

    @property
    def nblocks(self): return len(self.blocks) // 8

    def chunk(self, n):
        """The 256 words of chunk n (1-based, as layouts store it), row-major."""
        a = self.off(self.addr['chunks']) + (n - 1) * 0x200
        return [be16(self.data, a + i * 2) for i in range(256)]

    def chunk_data(self):
        """Chunks 1..nchunks as one blob (chunk n at (n-1)*$200, like S1's 256x256 data)."""
        return self.bytes_at(self.addr['chunks'], self.nchunks * 0x200)

    def _chunk_count(self):
        # LevelChunks has no length: count to the highest id a plane or a loop rule can
        # reach (what follows is other level data)
        ids = set().union(*(p.chunk_ids() for p in self.planes()))
        need = {i & 0x7F for i in ids}
        for i in ids:
            if i & 0x80:
                need |= set(self.loop_alternates(i & 0x7F))
        self.nchunks = max(need) if need else 0
        if self.nchunks > 0x80:
            raise ValueError(f'{self.name}: chunk ${self.nchunks:X} beyond the 64K window')
        # cells naming blocks past the decoded count draw stale blockBuffer RAM; none on
        # the disc is solid, so a converter can blank them
        bad, solid = [], 0
        for n in sorted(need):
            ws = [v for v in self.chunk(n) if (v & 0x3FF) >= self.nblocks]
            if ws:
                bad.append(n)
                solid += sum(1 for v in ws if v & 0x6000)
        if bad:
            self.warnings.append('chunks %s use blocks >= %d (%d solid)' % (
                ' '.join('%02X' % n for n in bad), self.nblocks, solid))
        if self.demo:                        # demo inputs are assembled over chunk bytes
            d = self.addr['DemoData'] - self.addr['chunks']
            hit = [n for n in sorted(need) if (n - 1) * 0x200 < d + 0x1000 and n * 0x200 > d]
            self.warnings.append('demo inputs overwrite %s' % (
                ('chunks ' + ' '.join('%02X' % n for n in hit)) if hit else 'unused chunk bytes'))

    # ------------------------------------------------------------- layouts
    def _layouts(self):
        a = self.addr['LevelLayouts']
        self.layout_table = self._word_table(a)          # word offsets from LevelLayouts
        # every plane the table names: FG = entry 0, BG = entry 1 (LoadLevelLayout); QQZ
        # swaps the BG for entry 2 by section; the rest are 1x1 / 33x6 / 48x6 leftovers
        # of Sonic 1's table shape, never loaded
        self.layouts = {}
        offs = sorted(set(self.layout_table))
        for i, o in enumerate(offs):
            lay = Layout(self.data, a + o)
            if lay.end - BASE > len(self.data):
                continue
            if i + 1 < len(offs) and lay.end > a + offs[i + 1]:
                lay.stored_rows = (offs[i + 1] - o - 2) // lay.width
            self.layouts[o] = lay
        self.fg = self.layouts[self.layout_table[0]]
        self.bg = self.layouts[self.layout_table[1]]
        for lay, nm in ((self.fg, 'FG'), (self.bg, 'BG')):
            if lay.width > 64 or lay.height > 16:
                raise ValueError(f'{self.name}: {nm} {lay.width}x{lay.height} exceeds the 64x16 RAM map')
            if lay.stored_rows < lay.height:
                self.warnings.append('%s declares %d rows, stores %d' % (nm, lay.height, lay.stored_rows))

    def planes(self):
        """The planes the level can show: FG, BG and any QQZ section BG."""
        out = [self.fg, self.bg]
        for sec in getattr(self, 'bg_sections', []):
            lay = self.layouts[sec['layout']]
            if lay not in out:
                out.append(lay)
        return out

    def loop_alternates(self, c):
        """Chunks collision may read instead of loop chunk c (LOOP_RULES)."""
        if self.zone == 'WWZ':
            return [LOOP_RULES['WWZ']['high'][c]] if c in LOOP_RULES['WWZ']['high'] else []
        if self.zone == 'SSZ':
            return [c + 1] if c in LOOP_RULES['SSZ']['bit6'] or c in LOOP_RULES['SSZ']['layer'] else []
        if self.zone == 'MMZ':
            return [LOOP_RULES['MMZ']['layer'][c]] if c in LOOP_RULES['MMZ']['layer'] else []
        return [0x51 if c + 1 == 0x29 else c + 1]

    def _check_loop_rules(self):
        # move.l #LevelChunks,d1; lea levelLayout.w,a1; move.b (a1,d0.w),d1; beq.s; bmi.s
        g = scan(self.data, '223C ???????? 43F8 ???? 1231 0000 67?? 6B', 0, 0x10000)
        if len(g) != 1:
            raise ValueError(f'{self.name}: GetLevelBlock not found')
        code = self.data[g[0][0]:g[0][0] + 0x1C0]
        missing = [r for r in _RULE_CODE if not re.search(r, code, re.S)]
        if missing:
            raise ValueError(f'{self.name}: GetLevelBlock differs from LOOP_RULES: {missing}')
        self.addr['GetLevelBlock'] = BASE + g[0][0] - 0x1E

    # ------------------------------------------------------------- collision
    def _collision(self):
        ram = self.addr.pop('collisionPtr')
        self.addr['collisionPtr_ram'] = ram
        hits = scan(self.data, '21FB 0006 %04X 4E75 (L)' % ram, 0, 0x10000)   # LevelColIndex(pc,d0.w)
        hits += scan(self.data, '21FC (L) %04X' % ram, 0, 0x10000)            # TTZ: move.l #LevelCollision
        vals = {c[0] for _, c, _ in hits}
        if len(vals) != 1:
            raise ValueError(f'{self.name}: LevelCollision: {sorted(map(hex, vals))}')
        self.addr['LevelCollision'] = vals.pop()
        # one shape byte per block (the engine indexes it with chunk word & $7FF)
        self.collision = self.bytes_at(self.addr['LevelCollision'], self.nblocks)
        self.angles = self.bytes_at(self.addr['ColAngleMap'], 0x100)
        self.heights = self.bytes_at(self.addr['ColHeightMap'], 0x1000)
        self.widths = self.bytes_at(self.addr['ColWidthMap'], 0x1000)
        if max(self.collision, default=0) * 16 >= 0x1000:
            raise ValueError(f'{self.name}: collision shape out of range')

    # ------------------------------------------------------------- palettes
    def _palettes(self):
        a = self.addr['PaletteTable']
        self.palette_table = []              # (source, palette RAM dest, colours); None = blank
        while True:
            e = a + len(self.palette_table) * 8
            src, dst, cnt = self.l(e), self.w(e + 4), self.w(e + 6)
            if src == 0 and cnt == 0xFFFF:   # MMZ act 2 bad future: a blank slot mid-table
                self.palette_table.append(None)
                continue
            # dest: palette RAM $FB00 (some MMZ entries aim at the fade buffer at $FB80)
            if not (BASE <= src < BASE + len(self.data) and 0xFA80 <= dst < 0xFC00 and cnt < 0x20):
                break
            self.palette_table.append((src, dst, (cnt + 1) * 2))
        if len(self.palette_table) <= max(3, self.palette_id) or not self.palette_table[self.palette_id]:
            raise ValueError(f'{self.name}: palette id {self.palette_id} not in the table')
        # LevelStart loads id 3 (Sonic, line 0), LoadLevelData the level id (lines 1-3),
        # both into the fade buffer the screen fades in to
        self.palette = self.apply_palettes([3, self.palette_id])

    def palette_colours(self, pid):
        src, dst, n = self.palette_table[pid]
        return [self.w(src + i * 2) for i in range(n)]

    def apply_palettes(self, ids, pal=None):
        """64 colours after loading palette ids (in order) over pal (default black)."""
        pal = list(pal or [0] * 64) + [0] * 64
        for pid in ids:
            src, dst, n = self.palette_table[pid]
            k = (dst - 0xFB00) // 2
            pal[k:k + n] = self.palette_colours(pid)
        return pal[:64]

    def palette_ok(self):
        return all((c & 0xF111) == 0 for c in self.palette) and any(self.palette[16:])

    def _water(self):
        """TTZ: LoadWaterPal(n) under `cmpi.b #2,zone`, and the act/period height table."""
        self.water = None
        wp = self._opt('LoadWaterPal')
        wh = self._opt('WaterHeights')
        ids = set()
        if wp:
            self.addr['LoadWaterPal'] = BASE + wp[0][0]
            ids = {self.data[o - 1] for o, _ in self._calls(wp[0][0])
                   if (be16(self.data, o - 2) & 0xFF00) == 0x7000}             # moveq #n,d0
        if not (ids or wh):
            return
        self.water = {}
        if ids:
            pid = min(ids)
            self.water['palette_id'] = pid
            self.water['palette'] = self.apply_palettes([pid])
        if wh:
            ta = wh[0][1][0]
            self.addr['WaterHeights'] = ta
            self.water['height'] = self.w(ta + (self.act * 4 + self.period) * 2)

    # ------------------------------------------------------------- start / bounds
    def _start_and_bounds(self):
        a = self.addr['LevelStartLoc']
        self.start = (self.w(a), self.w(a + 2))
        v = [self.w(self.addr['CamBounds'] + i * 2) for i in range(6)]
        # word 0 unused; left, right, top, bottom; camera Y centre. The level's events
        # code (RunLevelEvents, _Events.asm) moves them while playing.
        self.bounds = {'left': v[1], 'right': v[2], 'top': s16(v[3]), 'bottom': v[4], 'y_center': v[5]}
        # Sonic's special-chunk check compares raw layout bytes (bit 7 included):
        # [loop, special loop, special roll tunnel, roll tunnel]; $7F = none
        self.special_chunks = self.bytes_at(self.addr['SpecChunks'], 4)

    def _special_chunk_check(self):
        """ObjSonic_SpecialChunks: which zones run it, and whether it is stubbed (WWZ)."""
        self.special_chunk_check = None
        sc = self._opt('SpecialChunkCheck', 0, 0x40000)
        if not sc:
            return
        o = sc[0][0] + 14
        t = o + 2 + s16(sc[0][1][0])
        self.addr['ObjSonic_SpecialChunks'] = BASE + t
        body = self.data[t:t + 0x40]
        zones = [z for z in range(1, 8) if scan(body, '0C39 %04X 00FF 1506 67' % z)]
        if scan(body, '4A39 00FF 1506 66'):
            zones.append(0)
        self.special_chunk_check = {'active': be16(self.data, t) != 0x4E75, 'zones': sorted(zones)}

    # ------------------------------------------------------------- objects
    def _objects(self):
        a = self.addr['ObjectLayouts']
        self.addr['objects'] = a + self.w(a)
        self.addr['objects_null'] = a + self.w(a + 2)
        self.objects = []
        p = self.addr['objects']
        while self.w(p) != 0xFFFF:
            r = self.bytes_at(p, 8)
            y = be16(r, 2)
            self.objects.append({
                'x': be16(r, 0), 'y': y & 0xFFF,
                'xflip': bool(y & 0x4000), 'yflip': bool(y & 0x8000),
                'id': r[4] & 0x7F, 'saved': bool(r[4] & 0x80),
                'subtype': r[5], 'subtype2': r[7], 'periods': r[6] >> 5,
                'visible': bool((r[6] >> 5) >> self.period & 1),
            })
            if (y >> 12) & 3 or r[6] & 0x1F:
                self.warnings.append('object at $%X: unused bits set' % p)
            p += 8
        oi = self.addr['ObjectIndex']
        self.object_index = []               # entry k = object id k+1
        while len(self.object_index) < 0x7F:
            t = self.l(oi + len(self.object_index) * 4)
            if not (BASE + 0x100 <= t < BASE + len(self.data)) or t & 1:
                break
            self.object_index.append(t)
        bad = sorted({o['id'] for o in self.objects if o['visible'] and not 0 < o['id'] <= len(self.object_index)})
        if bad:
            self.warnings.append('objects outside the object index: ' + ' '.join('%02X' % i for i in bad))

    def visible_objects(self):
        return [o for o in self.objects if o['visible']]

    # ------------------------------------------------------------- PLCs
    def _plcs(self):
        a = self.addr['PLCLists']
        self.plcs = []
        for o in self._word_table(a):
            p = a + o
            n = s16(self.w(p)) + 1
            self.plcs.append([(self.l(p + 2 + i * 6) & 0x3FFFFF, self.w(p + 6 + i * 6)) for i in range(max(n, 0))])

    def vram(self, plc_ids=None):
        """64K VRAM image with PLC lists decoded in place (default: the level list, the
        standard list 1 and the zone list LevelDataIndex[4])."""
        if plc_ids is None:
            plc_ids = [i for i in (self.plc_level, 1, self.plc_zone) if i]
        vram = bytearray(0x10000)
        for pid in plc_ids:
            for src, dst in self.plcs[pid]:
                art, _ = self.nemesis(src)
                vram[dst:dst + len(art)] = art[:0x10000 - dst]
        return vram

    def _sections(self):
        """Section art: by camera X the level swaps object-art PLCs (respawn = init list)."""
        self.sections = []                   # (camera X limit, init PLC id, update PLC id)
        ls, us = self._opt('LoadSectionArt', 0, 0x40000), self._opt('UpdateSectionArt', 0, 0x40000)
        if not (ls and us):
            return
        (o, (disp, e1), n1), (o2, (_, e2), n2) = ls[0], us[0]
        rng = BASE + o + 2 + s16(disp)
        ini, upd = BASE + o + n1 - 2 + s8(e1), BASE + o2 + n2 - 2 + s8(e2)
        self.addr.update({'LoadSectionArt': BASE + o, 'SectionRanges': rng,
                          'SectionInitPLCs': ini, 'SectionUpdatePLCs': upd})
        for i in range(16):
            lim = self.w(rng + i * 2)
            self.sections.append((lim, self.w(ini + i * 2), self.w(upd + i * 2)))
            if lim == 0xFFFF:
                break

    def _bg_sections(self):
        """QQZ: by player X the BG plane is reloaded from another LevelLayouts entry, with
        its own PLC and palette (d1 = selector byte = LevelLayouts word offset; d1>>2 is
        added to the PLC and palette bases)."""
        self.bg_sections = []
        q = self._opt('BgSections')
        if not q:
            return
        o, (disp, plc_base, addq), _ = q[0]
        sel = BASE + o + 4 + s8(disp)
        pal_base = ((addq >> 9) & 7) or 8                    # addq.b #n,d0
        # player X limits: a flat list (lea Ranges,a0; move.w (a0)+,d1; bmi.s; cmp.w ..,d1)
        # or 3 words per act (d0 = act*6; move.w Ranges(pc,d0.w),d1)
        rng = None
        a = scan(self.data, '41F9 (L) 3218 6B0A B2', o - 0x40, o)
        if a:
            rng = a[-1][1][0]
        b = scan(self.data, '3601 D241 D243 D241 D041 323B 00 (B) 6B', o - 0x40, o)
        if b:
            rng = BASE + b[-1][0] + 12 + s8(b[-1][1][0]) + self.act * 6
        if rng is None:
            self.warnings.append('BG section code without a range table')
            return
        self.addr.update({'BgSectionRanges': rng, 'BgSectionSelect': sel})
        for i in range(8):
            lim, s = self.w(rng + i * 2), self.b(sel + i)
            self.bg_sections.append({'x_limit': lim, 'layout': self.layout_table[s >> 1],
                                     'palette_id': pal_base + (s >> 2), 'plc': plc_base + (s >> 2)})
            if lim & 0x8000:
                break

    # ------------------------------------------------------------- animation
    def _animation(self):
        self.anim_tiles = []
        ua = self._opt('UpdateAnimTiles')
        if ua:
            self.addr['UpdateAnimTiles'] = ua[0][1][0]
            self.anim_tiles = self._anim_tiles(ua[0][1][0])
        self.palette_cycles = []
        pc = self._opt('PaletteCycle')
        if pc:
            o = pc[0][0] + 8
            t = o + 2 + s16(pc[0][1][0])
            self.addr['PaletteCycle'] = BASE + t
            self.palette_cycles = self._palette_scripts(t)

    def _anim_tiles(self, routine):
        """UpdateAnimTiles entries: lea AniArt,a1; move.w #longs-1,d6; bsr.w AnimateTiles*;
        bne.s; LVLDMA aniArtBuffer,dest,len,VRAM. Simple scripts: byte duration, byte
        frames, frame art longs. Scripted: byte steps, pad, (duration, frame) per step,
        then art longs."""
        d, o = self.data, self.off(routine)
        out, prev = [], None
        for p, (src, size, disp), _ in scan(d, '43F9 (L) 3C3C (W) 6100 (W)', o, o + 0x400):
            if prev is not None and p - prev > 0x60:
                break
            prev = p
            dma = scan(d, '3ABC (W) 31FC (W)', p, p + 0x40)
            if not dma:
                continue
            hi, lo = dma[0][1]
            dest = (hi & 0x3FFF) | ((lo & 3) << 14)
            t = p + 10 + 2 + s16(disp)
            scripted = d[t + 2] == 0x6A and d[t + 3] != 0      # bpl.s: scripted; bpl.w: simple
            e = {'script': src, 'vram': dest, 'bytes': (size + 1) * 4}
            if scripted:
                n = self.b(src)
                steps = [(self.b(src + 2 + i * 2), self.b(src + 3 + i * 2)) for i in range(n)]
                arts = max(f for _, f in steps) + 1 if steps else 0
                e.update(kind='scripted', steps=steps, art=[self.l(src + 2 + n * 2 + i * 4) for i in range(arts)])
            else:
                n = self.b(src + 1)
                e.update(kind='simple', duration=self.b(src), art=[self.l(src + 2 + i * 4) for i in range(n)])
            out.append(e)
        return out

    def _palette_scripts(self, t):
        """PaletteCycle routines built on CycleColor: lea Script,a1; lea Colours,a2; bsr.
        Script: byte palette index, byte steps, (duration, colour index) per step. Levels
        with their own cycling code (PPZ good future, CCZ past, QQZ, some SSZ) give []."""
        d = self.data
        if be16(d, t) == 0x6000:                              # bra.w past dead prototype code
            t = t + 2 + s16(be16(d, t + 2))
        cc, dual = self._opt('CycleColor', t, t + 0x400), False
        if not cc:
            cc, dual = self._opt('CycleColor2', t, t + 0x400), True
        out = []
        if not cc:
            return out
        for p, (scr, col), _ in scan(d, '43F9 (L) 45F9 (L)', t, cc[0][0]):
            idx, n = self.b(scr), self.b(scr + 1)
            steps = [(self.b(scr + 2 + i * 2), self.b(scr + 3 + i * 2)) for i in range(n)]
            ncol = max((c for _, c in steps), default=-1) + 1
            colours = [self.w(col + i * 2) for i in range(ncol)]
            e = {'index': idx, 'steps': steps, 'colours': colours, 'script': scr}
            if dual:                                          # water palette colour = colours[n + i]
                e['water_colours'] = [self.w(col + (n + i) * 2) for i in range(ncol)]
            if idx < 64 and 0 < n <= 64 and all((c & 0xF111) == 0 for c in colours + e.get('water_colours', [])):
                out.append(e)
        return out

    def _calls(self, target_off):
        """Offsets of bsr.w / jsr abs.l calls to a routine in the code area."""
        d = self.data
        out = []
        for o in range(0, 0x10000, 2):
            w0 = be16(d, o)
            if (w0 == 0x6100 and o + 2 + s16(be16(d, o + 2)) == target_off) or \
               (w0 == 0x4EB9 and be32(d, o + 2) == BASE + target_off):
                out.append((o, w0))
        return out


def load(path):
    p = Path(path)
    return Level(p.read_bytes(), p.name)


def level_files(directory):
    return sorted(p for p in Path(directory).iterdir() if NAME_RE.fullmatch(p.stem.rstrip('_').upper()))


# ------------------------------------------------------------------ validation
DOBBY = {'R11A', 'R11B', 'R11C', 'R11D', 'DEMO11A', 'R61A'}
DOBBY_ZONE = {1: 'Palmtree Panic', 6: 'Wacky Workbench'}
DOBBY_TIME = {'A': 'Present', 'B': 'Past', 'C': 'Good Future', 'D': 'Bad Future'}


def validate(lv, dobby):
    """Compare a level with the ripped binaries of the Sonic CD disassembly (JP build;
    R11A-D, DEMO11A, R61A), read in place. Returns [(item, identical)]."""
    if lv.name not in DOBBY:
        return []
    root = Path(dobby) / 'Level'
    d = root / DOBBY_ZONE[lv.round] / 'Data'
    t = DOBBY_TIME[lv.time]
    act = 'Act 1 %s' % t
    out = []

    def rip(name):
        p = d / name
        return p.read_bytes() if p.exists() else None

    def same(item, ours, ref):
        if ref is not None:
            out.append((item, bytes(ours) == bytes(ref)))

    raw = lv.data
    for lay, nm in ((lv.fg, 'Foreground'), (lv.bg, 'Background')):
        same(nm, raw[lay.addr - BASE:lay.end - BASE], rip('%s (%s).bin' % (nm, act)))
    chunks = rip('Chunks (%s).bin' % act)
    if lv.demo:                              # the demo build assembles its inputs over chunk bytes
        c, dm = rip('Chunks (Act 1 Present Demo, JE).bin'), rip('Demo (Act 1 Present, JE).bin')
        rel = lv.addr['DemoData'] - lv.addr['chunks']
        chunks = c[:rel] + dm + c[rel + len(dm):]
    if chunks is not None:
        same('Chunks', lv.bytes_at(lv.addr['chunks'], len(chunks)), chunks)
        out.append(('chunk count', lv.nchunks == len(chunks) // 0x200))
    col = rip('Collision (%s).bin' % act)
    if col is not None:
        same('Collision', lv.bytes_at(lv.addr['LevelCollision'], len(col)), col)
    same('Angles', lv.angles, (root / '_Data/Collision Angles.bin').read_bytes())
    same('Heights', lv.heights, (root / '_Data/Collision Height Map.bin').read_bytes())
    same('Widths', lv.widths, (root / '_Data/Collision Width Map.bin').read_bytes())
    for nm, addr, cur in (('Tiles', lv.addr['tiles'], lv.tiles), ('Blocks', lv.addr['blocks'], lv.blocks)):
        ref = rip('%s (%s).nem' % (nm, act))
        if ref is not None:
            same(nm + ' (packed)', lv.bytes_at(addr, len(ref)), ref)
            same(nm + ' (decoded)', cur, nemesis_decode(ref, 0)[0])
    pal = rip('Palette (%s).bin' % t)
    if pal is not None:
        same('Palette', b''.join(struct.pack('>H', c) for c in lv.palette[16:]), pal)
    sonic = (root / '_Objects/Sonic/Data/Palette.bin').read_bytes()
    same('Sonic palette', b''.join(struct.pack('>H', c) for c in lv.palette[:16]), sonic)
    if not lv.demo:
        same('Start', struct.pack('>HH', *lv.start),
             rip('Start Position (Act 1 %s).bin' % ('Present' if lv.time == 'A' else 'Past')))
    same('Objects', lv.bytes_at(lv.addr['objects'], (len(lv.objects) + 1) * 8),   # + the $FFFF record
         rip('Objects (Act 1).bin'))
    # tables the disassembly writes as source: compare the values
    src = root / DOBBY_ZONE[lv.round]

    def asm(name):
        p = src / ('%s (%s).asm' % (name, act))
        return p.read_text(errors='replace') if p.exists() else ''

    def nums(text):
        return [int(v.replace('$', '0x'), 0) & 0xFFFF for v in re.findall(r'-?\$?[0-9A-Fa-f]+', text)]

    def after(text, label, kind):
        m = re.search(r'^%s:\s*\n((?:\s*dc\.%s[^\n]*\n)+)' % (label, kind), text, re.M)
        return nums(' '.join(re.sub(r';.*', '', ln).split(None, 1)[1] for ln in m.group(1).splitlines()
                             if ln.strip())) if m else None
    scroll = asm('Scroll')
    cb = after(scroll, 'CamBounds', 'w')
    if cb:
        b = lv.bounds
        out.append(('CamBounds', cb[1:] == [b['left'], b['right'], b['top'] & 0xFFFF, b['bottom'], b['y_center']]))
    sc = after(scroll, 'SpecChunks', 'b')
    if sc:
        out.append(('SpecChunks', bytes(sc) == lv.special_chunks))
    oi = asm('Object Index')
    if oi:
        out.append(('ObjectIndex count', oi.count('dc.l') == len(lv.object_index)))
    data = asm('Data')
    order = re.findall(r'dc\.w\s+(PLC_\w+)-PLCLists', data)
    if order:
        lists = {k: re.findall(r'dc\.w\s+\$?([0-9A-Fa-f]+)\s*\n', v)
                 for k, v in re.findall(r'^(PLC_\w+):\s*\n((?:\s*dc\.[wl][^\n]*\n)+)', data, re.M)}
        ok = len(order) == len(lv.plcs)
        for k, plc in zip(order, lv.plcs):
            ref = lists[k]
            ok &= int(ref[0], 16) + 1 == len(plc) and [int(v, 16) for v in ref[1:]] == [v for _, v in plc]
        out.append(('PLC lists', ok))
    sd = asm('Section Data')
    if sd:
        rng, upd, ini = (after(sd, k, 'w') for k in ('SectionRanges', 'SectionUpdatePLCs', 'SectionInitPLCs'))
        out.append(('Sections', list(zip(rng, ini, upd)) == lv.sections))
    return out


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description='Decode every Sonic CD level file in a directory.')
    ap.add_argument('disc', help='directory of files extracted from the disc (scd_iso.py)')
    ap.add_argument('--dobby', help="the Sonic CD disassembly's root: byte-compare R11A-D, DEMO11A, R61A")
    args = ap.parse_args(argv)
    fails, checks = [], []
    print('%-8s %5s %5s %5s %5s %6s %5s %10s %4s %s' % ('level', 'tiles', 'blks', 'chnk', 'fg', 'bg', 'objs',
                                                       'start', 'pal', 'notes'))
    for p in level_files(args.disc):
        try:
            lv = load(p)
        except Exception as e:  # noqa: BLE001 - report every file, then fail
            fails.append((p.name, e))
            continue
        notes = []
        if lv.water:
            notes.append('water y=$%X pal %d' % (lv.water.get('height', 0), lv.water.get('palette_id', -1)))
        if lv.sections:
            notes.append('%d art sections' % len(lv.sections))
        if lv.bg_sections:
            notes.append('%d BG sections' % len(lv.bg_sections))
        print('%-8s %5d %5d %5d %5s %6s %5d %10s %4s %s' % (
            lv.name, lv.ntiles, lv.nblocks, lv.nchunks, '%dx%d' % (lv.fg.width, lv.fg.height),
            '%dx%d' % (lv.bg.width, lv.bg.height), len(lv.visible_objects()),
            '%X,%X' % lv.start, 'ok' if lv.palette_ok() else 'BAD', '; '.join(notes + lv.warnings)))
        if args.dobby:
            v = validate(lv, args.dobby)
            if v:
                checks.append((lv.name, v))
    for name, v in checks:
        bad = [k for k, ok in v if not ok]
        print('validate %-8s %d/%d identical%s' % (name, len(v) - len(bad), len(v),
                                                   (' -- DIFFER: ' + ', '.join(bad)) if bad else ''))
        if bad:
            fails.append((name, 'differs from the disassembly: ' + ', '.join(bad)))
    for name, e in fails:
        print('FAIL %s: %s' % (name, e))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
