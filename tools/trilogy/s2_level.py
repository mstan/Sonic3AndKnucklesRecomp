"""Sonic 2 levels -> S3K-format bank entries.

Addresses are from the s2disasm listing, whose build is byte-identical to the
REV00 cartridge (MD5 8e2c29a1e65111fe2078359e685e7943). Nothing from the ROM
ships: build_bank.py runs this over the user's cartridge.

Sonic 2 is already close to the S3K level format: 128px chunks whose words
carry S3K's flip and two-path solidity bits, 16px blocks, two collision
indexes. Converted here:
  * layout  - S2 interleaves 16 rows of 128 FG / 128 BG chunk ids; S3K wants
              row pointers, so used widths are trimmed (s3k_layout).
  * art     - recoloured per palette line into the S3K conventions (line 0 =
              S3K player palette minus Super Sonic's cycled entries, line 1 =
              S3K common-object slots) and packed under the S3K level limit.
  * objects - common objects become S3K's (monitor, spring, star post, path
              swapper); the rest go to native S2 code at NATIVE_S2_BASE+id*4.
  * rings   - S2's packed ring groups become S3K single rings.
"""
import struct
from pathlib import Path

import s1_objects  # noqa: E402
import s2_objects

S2 = {
    'LevelArtPointers': 0x042584, 'Off_Level': 0x045A80, 'Off_ColP': 0x0049DC, 'Off_ColS': 0x004A20,
    'ColCurveMap': 0x042D50, 'ColArrayVertical': 0x042E50, 'ColArrayHorizontal': 0x043E50,
    'LevelSize': 0x00C048, 'StartLocations': 0x00C1C4, 'Off_Objects': 0x0E6800, 'AnimPatMaps': 0x040340, 'Off_Rings': 0x0E4300,
    'PalPointers': 0x002776, 'ArtLoadCues': 0x042650,
}
NATIVE_S1 = 0x7E0000
NATIVE_S2 = 0x7E1000

# trilogy zone -> [(S2 zone id, S2 act)] in act order (MTZ act 3 is S2 zone 5)
S2_ZONES = {
    'EHZ': (0x28, [(0x00, 0), (0x00, 1)]),
    'CPZ': (0x29, [(0x0D, 0), (0x0D, 1)]),
    'ARZ': (0x2A, [(0x0F, 0), (0x0F, 1)]),
    'CNZ': (0x2B, [(0x0C, 0), (0x0C, 1)]),
    'HTZ': (0x2C, [(0x07, 0), (0x07, 1)]),
    'MCZ': (0x2D, [(0x0B, 0), (0x0B, 1)]),
    'OOZ': (0x2E, [(0x0A, 0), (0x0A, 1)]),
    'MTZ': (0x2F, [(0x04, 0), (0x04, 1), (0x05, 0)]),
    'SCZ': (0x30, [(0x10, 0)]),
    'WFZ': (0x31, [(0x06, 0)]),
    'DEZ': (0x32, [(0x0E, 0)]),
}

# water (Level_InitWater / WaterHeight): (zone, act) -> (S2 underwater palette id, height)
S2_WATER = {('CPZ', 1): (0x16, 0x710), ('ARZ', 0): (0x17, 0x410), ('ARZ', 1): (0x17, 0x510)}

# zone theme (S2 music ids)
S2_ZONE_SONG = {'EHZ': 0x82, 'CPZ': 0x8E, 'ARZ': 0x87, 'CNZ': 0x89, 'HTZ': 0x86, 'MCZ': 0x8B, 'OOZ': 0x84,
                'MTZ': 0x85, 'SCZ': 0x8D, 'WFZ': 0x8F, 'DEZ': 0x8A}

# S2 line 1 -> S3K line 1 slots. S2's line 1 carries the same shared colours
# as S1's (rings, monitors, the HUD) except its dark $044 sits at index 5 and
# index 14 is a zone colour; S3K's HUD shades its digits with slot $F, so
# those two trade places relative to S1's build_bank.L1_PERM.
S2_L1_PERM = [0, 11, 2, 3, 4, 15, 1, 12, 13, 14, 8, 9, 5, 6, 7, 10]

# S2 Pal_SonicTails (PalPointers id 3)
S2_SONIC = [0x000, 0x000, 0xA22, 0xC42, 0xE44, 0xE66, 0xEEE, 0xAAA, 0x888, 0x444, 0x8AE, 0x46A, 0x00E, 0x008, 0x0AE, 0x08E]

# S2 monitor subtypes -> S3K: 0 static, 1/2 Sonic/Tails 1-up, 3 Robotnik, 4 rings, 5 shoes,
# 6 shield (-> bubble; s1_objects.elemental_shields then mixes in fire and lightning),
# 7 invincibility, 8 teleport / 9 random (1P: rings)
S2_MONITOR = {0: 0, 1: 1, 2: 1, 3: 2, 4: 3, 5: 4, 6: 7, 7: 8, 8: 3, 9: 3}


def be16(b, a): return (b[a] << 8) | b[a + 1]
def be32(b, a): return (be16(b, a) << 16) | be16(b, a + 2)


class Sonic2:
    def __init__(self, rom, bb):
        """bb: the build_bank module (shared codecs, Packer, palette tables)."""
        if len(rom) != 0x100000 or be16(rom, 0x18E) != 0xDEB8:
            raise SystemExit("Sonic 2 ROM must be the REV00 cartridge (CRC32 24AB4C3A, checksum $DEB8)")
        self.rom = rom
        self.bb = bb
        l0 = [0] + [bb.nearest(S2_SONIC[i], bb.S3K_SONIC, bb.SUPER_CYCLED) for i in range(1, 16)]
        l0[2:6] = [0xE, 0xE, 0xD, 0xC]       # S2's four blues, off Super Sonic's cycled entries
        self.tables = {0: l0, 1: S2_L1_PERM, 2: list(range(16)), 3: list(range(16))}
        self.objs = s2_objects.S2Objects(rom, self.tables)
        s2_objects.write_asset_header(Path(__file__).resolve().parents[2] / 'game' / 'trilogy' / 'trilogy_assets_s2.h')

    def kos(self, a):
        return bytes(self.bb.kosinski_decode(self.rom, a)[0])

    def art_header(self, z):
        a = S2['LevelArtPointers'] + z * 12
        r = self.rom
        return {'art': be32(r, a) & 0xFFFFFF, 'blk16': be32(r, a + 4) & 0xFFFFFF,
                'blk128': be32(r, a + 8) & 0xFFFFFF, 'pal': r[a + 8], 'plc1': r[a], 'plc2': r[a + 4]}

    def plc(self, pid):
        """S2 PLC list (ArtLoadCues): [(nemesis art, VRAM byte address)]"""
        r = self.rom
        a = S2['ArtLoadCues'] + be16(r, S2['ArtLoadCues'] + pid * 2)
        return [(be32(r, a + 2 + i * 6), be16(r, a + 6 + i * 6)) for i in range(be16(r, a) + 1)]

    def layout(self, z, act):
        """(fg rows, bg rows), each 16 rows x 128 chunk ids."""
        a = S2['Off_Level'] + be16(self.rom, S2['Off_Level'] + (z * 2 + act) * 2)
        d = self.kos(a)
        fg = [list(d[r * 256:r * 256 + 128]) for r in range(16)]
        bg = [list(d[r * 256 + 128:r * 256 + 256]) for r in range(16)]
        return fg, bg

    @staticmethod
    def trim(rows):
        w = max((max((i for i, v in enumerate(row) if v), default=-1) for row in rows), default=-1) + 1
        h = max((r for r, row in enumerate(rows) if any(row)), default=-1) + 1
        w, h = max(w, 1), max(h, 1)
        return [row[:w] for row in rows[:h]]

    def palette(self, bank, palid):
        r = self.rom
        pe = S2['PalPointers'] + palid * 8
        src, dst, cnt = be32(r, pe), be16(r, pe + 4), be16(r, pe + 6)
        pal = bytearray(r[src:src + (cnt + 1) * 4])
        if dst == 0xFB20:                         # S2 palette RAM $FB00 = S3K Normal_palette
            l1 = bytes(pal[0:32])
            for i in range(16):
                pal[S2_L1_PERM[i] * 2:S2_L1_PERM[i] * 2 + 2] = l1[i * 2:i * 2 + 2]
        return bank.add_palette(pal, 0xFC00 + (dst - 0xFB00))

    def water_palette(self, bank, palid):
        """S2 underwater palette: S3K's underwater player line (HCZ1) + S2 lines 1-3."""
        r = self.rom
        src = be32(r, S2['PalPointers'] + palid * 8)
        pal = bytearray(self.bb.S3K_WATER_LINE0) + bytearray(r[src + 0x20:src + 0x80])
        l1 = bytes(pal[0x20:0x40])
        for i in range(16):
            pal[0x20 + S2_L1_PERM[i] * 2:0x20 + S2_L1_PERM[i] * 2 + 2] = l1[i * 2:i * 2 + 2]
        return bank.add_palette(pal, 0xFC00)

    def collision(self, bank, z):
        r = self.rom
        colp = self.kos(be32(r, S2['Off_ColP'] + z * 4))
        cols = self.kos(be32(r, S2['Off_ColS'] + z * 4))
        solids = bytearray(0xC00)
        for b in range(min(len(colp), 0x300)):
            solids[b * 2] = colp[b]
        for b in range(min(len(cols), 0x300)):
            solids[0x600 + b * 2] = cols[b]
        return (bank.add(bytes(solids)),
                bank.add(r[S2['ColCurveMap']:S2['ColCurveMap'] + 0x100]),
                bank.add(r[S2['ColArrayVertical']:S2['ColArrayVertical'] + 0x1000]),
                bank.add(r[S2['ColArrayHorizontal']:S2['ColArrayHorizontal'] + 0x1000]))

    def placements(self, z, act):
        r = self.rom
        a = S2['Off_Objects'] + be16(r, S2['Off_Objects'] + (z * 2 + act) * 2)
        objs = []
        while be16(r, a) != 0xFFFF:
            objs.append((be16(r, a), be16(r, a + 2), r[a + 4], r[a + 5]))
            a += 6
        return objs

    def objects(self, z, act):
        """(S3K object layout, S3K ring layout)"""
        r = self.rom
        # S2's loader is x-only; S3K's loads an entry outside its Y window only with y-word bit 15
        # set, so every entry gets it. S2's own bit 15 ("remember state") moves to bit 12, which
        # S3K masks off (trilogy_obj.c s2_unmark reads it).
        objs = [(x, (yw & 0x6FFF) | 0x8000 | (0x1000 if yw & 0x8000 else 0), oid,
                 S2_MONITOR.get(sub, 0) if oid == 0x26 else sub)
                for x, yw, oid, sub in self.placements(z, act)]
        objs.sort(key=lambda o: o[0])
        objs = s1_objects.elemental_shields(objs, z * 2 + act)
        ob = b''.join(struct.pack('>HHBB', x, y, i, s) for x, y, i, s in objs) + b'\xFF\xFF\0\0\0\0'
        a = S2['Off_Rings'] + be16(r, S2['Off_Rings'] + (z * 2 + act) * 2)
        rings = []
        while be16(r, a) != 0xFFFF:
            x, yw = be16(r, a), be16(r, a + 2)
            a += 4
            n = ((yw >> 12) & 7) + 1
            vertical = yw & 0x8000
            y = yw & 0xFFF
            for i in range(n):
                rings.append((x, y + i * 0x18) if vertical else (x + i * 0x18, y))
        rings.sort()
        rb = b'\0\0\0\0' + b''.join(struct.pack('>HH', x, y) for x, y in rings) + b'\xFF\xFF\0\0'
        return b'\xFF\xFF\0\0\0\0' + ob, rb

    def anim_scripts(self, z):
        """Dynamic_Normal scripts of the zone (PLC_DYNANM): [(dur, art, tile, ntiles, frames bytes)]"""
        r, sym = self.rom, s2_objects.symbols()
        t = sym['PLC_DYNANM']
        if t + be16(r, t + z * 4) not in (sym['Dynamic_Normal'], sym['Dynamic_ARZ'], sym['Dynamic_CNZ'],
                                          sym['Dynamic_HTZ']):  # ARZ/CNZ: Normal unless a boss; HTZ: + cliffs/clouds
            return []
        a = t + be16(r, t + z * 4 + 2)
        out = []
        for _ in range(be16(r, a) + 1):
            a += 0 if out or a != t + be16(r, t + z * 4 + 2) else 2
            dur, art, vram, nf, nt = r[a], be32(r, a) & 0xFFFFFF, be16(r, a + 4), r[a + 6], r[a + 7]
            fl = nf * (2 if dur & 0x80 else 1)
            out.append((dur, art, vram // 32, nt, bytes(r[a + 8:a + 8 + fl])))
            a += 8 + ((fl + 1) & ~1)
        return out

    def anim_line(self, blocks, start, n):
        lines = set()
        for b in range(len(blocks) // 8):
            for k in range(4):
                w = be16(blocks, b * 8 + k * 2)
                if start <= (w & 0x7FF) < start + n:
                    lines.add((w >> 13) & 3)
        return min(lines) if lines else 2

    def pointer_table(self, s3k):
        out = bytearray()
        for i in range(256):
            a = NATIVE_S2 + i * 4
            if i == 0x26:
                a = s3k['Obj_Monitor']
            elif i == 0x41:
                a = s3k['Obj_Spring']
            elif i == 0x79:
                a = s3k['Obj_StarPost']
            elif i == 0x03:
                a = s3k['Obj_PathSwap']
            elif i == 0x74:                       # invisible solid block
                a = s3k['Obj_InvisibleBlock']
            elif i == 0x24:                       # bubble maker: S3K's bubbler (S2's own, ported)
                a = s3k['Obj_Bubbler']
            elif i == 0x0D:                       # signpost: the trilogy's standing, spinning sign
                a = NATIVE_S1 + 0x0D * 4
            out += struct.pack('>I', a)
        return bytes(out)

    def level_art(self, z):
        """The zone's 8x8 art as S2 decompresses it: HTZ and WFZ patch a supplement over the shared art."""
        art = bytearray(self.kos(self.art_header(z)['art']))
        sup = {0x07: ('ArtKos_HTZ', 0x1FC), 0x06: ('ArtKos_WFZ', 0x307)}.get(z)
        if sup:
            extra = self.kos(s2_objects.symbols()[sup[0]])
            o = sup[1] * 32
            art[o:o + len(extra)] = extra
        return bytes(art)

    def level_blocks(self, z):
        """The zone's 16x16 block table as S2 builds it: the shared table, HTZ's patch from
        block $130 (BM16_HTZ at Block_Table+$980), then the animated pattern maps."""
        r = self.rom
        blocks = bytearray(self.kos(self.art_header(z)['blk16']))
        if z == 0x07:
            patch = self.kos(s2_objects.symbols()['BM16_HTZ'])
            blocks[0x980:0x980 + len(patch)] = patch
        blocks += bytes(max(0, 0x300 * 8 - len(blocks)))   # S2's table is 768 blocks; the rest are blank (collision-only)
        # LoadAnimatedBlocks: the zone's animated pattern maps (AnimPatMaps) overwrite the end of the
        # block table at level load - blocks whose tiles are animated art (EHZ hills, CNZ slot pictures...)
        apm = S2['AnimPatMaps'] + be16(r, S2['AnimPatMaps'] + z * 2)
        if be16(r, apm):
            dst, n = be16(r, apm), (be16(r, apm + 2) + 1) * 2
            blocks[dst:dst + n] = r[apm + 4:apm + 4 + n]
        return blocks

    def convert_act(self, bank, name, act, z, s2act):
        bb, r = self.bb, self.rom
        hdr = self.art_header(z)
        vram = bytearray(0x10000)
        art = self.level_art(z)
        vram[0:len(art)] = art[:0x10000]
        for pid in (hdr['plc1'], hdr['plc2']):           # zone PLCs land on top (level bits too)
            for src, dst in self.plc(pid):
                pa = bb.nemesis_decode(r, src)[0]
                vram[dst:dst + len(pa)] = pa[:0x10000 - dst]
        blocks = self.level_blocks(z)
        chunks = self.kos(hdr['blk128'])
        fg, bg = self.layout(z, s2act)
        fg, bg = self.trim(fg), self.trim(bg)
        used_chunks = {v for row in fg + bg for v in row}
        used_blocks = set()
        for c in used_chunks:
            for i in range(64):
                used_blocks.add(be16(chunks, c * 128 + i * 2) & 0x3FF)
        scripts = self.anim_scripts(z)
        pinned = [(tile, nt) for _, _, tile, nt, _ in scripts if tile + nt <= bb.S3K_LEVEL_TILE_LIMIT]
        pk = bb.Packer(vram, self.tables, bb.S3K_LEVEL_TILE_LIMIT, pinned)
        dest = {tile: pk.relocate(tile, nt) for _, _, tile, nt, _ in scripts
                if tile + nt > bb.S3K_LEVEL_TILE_LIMIT}      # e.g. ARZ's waterfall at $557
        slot_tile = pk.relocate(0x550, 0x30) if name == 'CNZ' else 0   # ArtTile_ArtUnc_CNZSlotPics_1-3
        htz_tile = pk.relocate(0x500, 0x20) if name == 'HTZ' else 0    # ArtTile_ArtUnc_HTZMountains + Clouds
        # diagonal springs are S3K's Obj_Spring: its art at ArtTile_DiagonalSpring, loaded by the act's PLC
        diag = any(i == 0x41 and ((sub >> 4) & 7) in (3, 4) for _, _, i, sub in self.placements(z, s2act))
        plc_id = 0
        if diag:
            src, n = bb.S3K_DIAG_SPRING
            pk.reserve(0x43A, n)
            plc_id = bank.add_plc([(src, 0x43A * 32)])
        if slot_tile and __import__("os").environ.get("TRI_VRAMDBG"): print("    slot refs", sorted({(b, be16(blocks, b * 8 + k * 2) & 0x7FF) for b in used_blocks for k in range(4) if 0x550 <= (be16(blocks, b * 8 + k * 2) & 0x7FF) < 0x580})[:12])
        boss = (s2act == 1 and name != 'MTZ') or name in ('SCZ', 'WFZ', 'DEZ') or (name == 'MTZ' and z == 0x05)
        need = self.objs.act_assets(self.placements(z, s2act), name, act, boss)
        for t0, n, line in self.objs.level_groups(need):
            pk.place_group(t0, n, line)
        for b in sorted(used_blocks):
            for k in range(4):
                w = be16(blocks, b * 8 + k * 2)
                pk.place_single(w & 0x7FF, (w >> 13) & 3)
        for b in range(len(blocks) // 8):
            for k in range(4):
                o = b * 8 + k * 2
                w = be16(blocks, o)
                nt = pk.slot.get((w & 0x7FF, (w >> 13) & 3), 0) if b in used_blocks else 0
                struct.pack_into('>H', blocks, o, (w & 0xF800) | nt)
        # animated art: each script's frames recoloured for the line its blocks use
        pin_src, anim = {}, struct.pack('>H', len(scripts))
        for dur, art, tile, nt, frames in scripts:
            tile = dest.get(tile, tile)
            ids = frames[::2] if dur & 0x80 else frames
            ntiles = max(ids) + nt
            tab = self.tables[self.anim_line(blocks, tile, nt)]
            raw = bytes(r[art:art + ntiles * 32])
            rec = b''.join(bb.remap_tile(raw[k * 32:k * 32 + 32], tab) for k in range(ntiles))
            for k in range(nt):
                pin_src[tile + k] = rec[ids[0] * 32 + k * 32:ids[0] * 32 + k * 32 + 32]
            body = frames + bytes(len(frames) & 1)
            anim += struct.pack('>BBBBHHI', dur, len(ids), nt, 0, tile, 0, bank.add(rec)) + body
        anim_a = bank.add(anim) if scripts else 0
        pix = pk.art(pin_src)
        if len(pix) & 1:
            pix += bytes(1)
        art_a, art2_a = bb.level_art_modules(bank, bytes(pix))
        blocks_a = bank.add(bb.kosinski_encode(bytes(blocks)))
        chunks_a = bank.add(bb.kosinski_encode(chunks))
        pal_id = self.palette(bank, hdr['pal'])
        if pk.end() > 0x43A and diag:
            raise SystemExit(f"{name}{act + 1}: level art reaches the diagonal spring art at $43A")
        llb_a = bank.add(struct.pack('>IIIIII', (plc_id << 24) | art_a, art2_a, (pal_id << 24) | blocks_a,
                                     (pal_id << 24) | blocks_a, chunks_a, chunks_a))
        layout_a = bank.add(bb.s3k_layout(fg, bg))
        solids_a, ang_a, hgt_a, hgtr_a = self.collision(bank, z)
        sa = S2['LevelSize'] + (z * 2 + s2act) * 8
        left, right, top, bottom = (be16(r, sa + i * 2) for i in range(4))
        st = S2['StartLocations'] + (z * 2 + s2act) * 4
        sx, sy = be16(r, st), be16(r, st + 2)
        ob, rb = self.objects(z, s2act)
        objs_a = bank.add(ob, dedupe=False)
        rings_a = bank.add(rb, dedupe=False)
        objptrs_a = bank.add(self.pointer_table(bb.S3K_ADDR))
        free = (set(range(pk.end(), bb.S3K_LEVEL_TILE_LIMIT)) | (set(range(1, bb.S3K_LEVEL_TILE_LIMIT)) - pk.taken)) - pk.reserved
        if __import__("os").environ.get("TRI_VRAMDBG"): print(f"    {name}{act + 1}: level end {pk.end():X}, free level tiles {len(free)}, slot {slot_tile:X}")
        sa = S2['LevelSize'] + (z * 2 + s2act) * 8
        sign_x = None
        if not boss and any(i == 0x0D for _, _, i, _ in self.placements(z, s2act)):
            sign_x = be16(r, sa + 2) - 0x100           # Camera_Max_X_pos - $100 (CheckLoadSignpostArt)
        assets_a = self.objs.build(bank, name, free, need, pk.slot, self.placements(z, s2act), act, sign_x)
        tz = S2_ZONES[name][0]
        e = bank.entry(tz, act)
        d = bank.data
        struct.pack_into('>IIIHHHHHHHHII', d, e, llb_a, layout_a, solids_a, left, right, top, bottom,
                         sx, sy, sx, sy, objs_a + 6, rings_a)
        d[e + 0x24] = S2_ZONE_SONG[name]          # S2 song id (s2_music.py)
        d[e + 0x25] = 2                           # game: S2
        struct.pack_into('>III', d, e + 0x28, ang_a, hgt_a, hgtr_a)
        struct.pack_into('>II', d, e + 0x34, objptrs_a, assets_a)
        wpal, wy = S2_WATER.get((name, act), (0, 0))
        if wpal:
            wpal = self.water_palette(bank, wpal)
        slot_art = 0
        if slot_tile:                                    # SlotMachine_DrawSlot's pictures, in their line's colours
            tab = self.tables[self.anim_line(blocks, slot_tile, 0x30)]

            sym = s2_objects.symbols()
            raw = bytes(r[sym['ArtUnc_CNZSlotPics']:sym['ArtUnc_CNZSlotPics'] + 6 * 16 * 32])
            if self.anim_line(blocks, slot_tile, 0x30) == 0:   # S2 draws them on Sonic's line: keep S2's
                slot_art = bank.add(raw)                        # colours, shown on the extended line
                slot_tile |= 0x8000
            else:
                slot_art = bank.add(bytes(((tab[b >> 4] << 4) | tab[b & 15]) for b in raw))
        boss_pal = {('CNZ', 1): 0x1A, ('MCZ', 1): 0x19, ('OOZ', 1): 0x25}.get((name, act), 0)   # PalID_*_B (LevEvents)
        if boss_pal:
            boss_pal = self.palette(bank, boss_pal)
        cliffs_a = clouds_a = 0
        if htz_tile:                                     # Dynamic_HTZ's art, in its blocks' line colours
            tab = self.tables[self.anim_line(blocks, 0x500, 0x18)]
            sym = s2_objects.symbols()
            raw = bb.nemesis_decode(r, sym['ArtNem_HTZCliffs'])[0]       # PatchHTZTiles: 8 frames x 6 x 4 tiles
            cliffs_a = bank.add(bytes(((tab[b >> 4] << 4) | tab[b & 15]) for b in raw))
            tab = self.tables[self.anim_line(blocks, 0x518, 8)]
            raw = r[sym['ArtUnc_HTZClouds']:sym['ArtUnc_HTZClouds'] + 0x400]
            clouds_a = bank.add(bytes(((tab[b >> 4] << 4) | tab[b & 15]) for b in raw))
        pt = getattr(self.objs, 'pilot_tiles', {})
        info_a = bank.add(struct.pack('>IBBHHHIIIHH', anim_a, wpal, boss_pal, wy, slot_tile, htz_tile, slot_art,
                                      cliffs_a, clouds_a, pt.get('PILOT_TAILS', 0), pt.get('PILOT_SONIC', 0)))
        struct.pack_into('>I', d, e + 0x3C, info_a)             # S2 info (game/trilogy/trilogy.h S2I_*)
        print(f"    {name}{act + 1}: {len(used_chunks)} chunks, {pk.end()} level tiles, "
              f"fg {len(fg[0])}x{len(fg)}, bg {len(bg[0])}x{len(bg)}")

    def convert_zone(self, bank, name):
        for act, (z, s2act) in enumerate(S2_ZONES[name][1]):
            self.convert_act(bank, name, act, z, s2act)
        print(f"  S2 {name}: {len(S2_ZONES[name][1])} acts")
