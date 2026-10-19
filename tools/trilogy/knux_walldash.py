"""Knuckles' wall spin dash: his spin dash frames and the spin dash dust,
turned a quarter (forwards = up, the floor = the wall on his right) from the
retail S&K art, mappings and DPLCs, then cut back into sprite pieces.

Blob (TRI_BLOB_KNUX_WALLDASH):
  u16 offset of the mapping table, u16 frame count,
  per frame: u32 offset of its tiles, u16 tile count,
  the mapping table (S3K format: word offsets, then count + 6-byte pieces;
  each frame's tile numbers start at 0: its tiles go to the object's own art),
  then the tiles.
Frames 0-5: Knuckles $86-$8B (AniKnuckles09); 6-12: dust $0A-$10
(Ani_DashSplashDrown 2).
"""
import struct

MAP_KNUX, PLC_KNUX, ART_KNUX = 0x14A8D6, 0x14BD0A, 0x1200E0
MAP_DUST, PLC_DUST, ART_DUST = 0x018DF4, 0x018EE2, 0x18A604
KNUX_FRAMES = [0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B]
DUST_FRAMES = list(range(0x0A, 0x11))
WALL = 0x0A                         # climbing: the wall is x_radius ($A) to his side


def w16(r, a):
    return struct.unpack_from('>H', r, a)[0]


def s16(r, a):
    return struct.unpack_from('>h', r, a)[0]


def frame_pixels(rom, maps, plc, art, frame):
    """{(x, y): colour} of one mapped frame, origin at the object."""
    d = plc + w16(rom, plc + frame * 2)
    tiles = []
    for i in range(w16(rom, d)):
        e = w16(rom, d + 2 + i * 2)
        tiles += [art + ((e & 0xFFF) + k) * 32 for k in range((e >> 12) + 1)]
    m = maps + s16(rom, maps + frame * 2)
    px = {}
    for i in range(w16(rom, m)):
        p = m + 2 + i * 6
        dy, size, attr, dx = struct.unpack_from('>bBHh', rom, p)
        cw, ch = ((size >> 2) & 3) + 1, (size & 3) + 1
        for ix in range(cw * 8):
            for iy in range(ch * 8):
                sx = cw * 8 - 1 - ix if attr & 0x800 else ix
                sy = ch * 8 - 1 - iy if attr & 0x1000 else iy
                t = (attr & 0x7FF) + (sx // 8) * ch + sy // 8
                if t >= len(tiles):
                    continue
                b = rom[tiles[t] + (sy & 7) * 4 + (sx & 7) // 2]
                c = (b >> 4) if (sx & 1) == 0 else (b & 15)
                if c:
                    px[(dx + ix, dy + iy)] = c
    return px


def turn(px, shift):
    """A quarter turn anticlockwise: right -> up, down -> right."""
    return {(y + shift, -x - 1): c for (x, y), c in px.items()}


def cut(px):
    """Sprite pieces (up to 4x4 tiles) covering px: (pieces, tile bytes)."""
    if not px:
        return [], b''
    x0 = min(x for x, _ in px)
    y0 = min(y for _, y in px)
    x1 = max(x for x, _ in px) + 1
    y1 = max(y for _, y in px) + 1
    pieces, data = [], bytearray()
    for by in range(y0, y1, 32):
        for bx in range(x0, x1, 32):
            used = [(c, r) for c in range(4) for r in range(4)
                    if any((bx + c * 8 + i, by + r * 8 + j) in px for i in range(8) for j in range(8))]
            if not used:
                continue
            c0, c1 = min(c for c, _ in used), max(c for c, _ in used)
            r0, r1 = min(r for _, r in used), max(r for _, r in used)
            w, h = c1 - c0 + 1, r1 - r0 + 1
            first = len(data) // 32
            for c in range(c0, c1 + 1):                 # column-major, as the VDP reads them
                for r in range(r0, r1 + 1):
                    for j in range(8):
                        for i in range(0, 8, 2):
                            x, y = bx + c * 8 + i, by + r * 8 + j
                            data.append((px.get((x, y), 0) << 4) | px.get((x + 1, y), 0))
            pieces.append((by + r0 * 8, ((w - 1) << 2) | (h - 1), first, bx + c0 * 8))
    return pieces, bytes(data)


def build(rom):
    knux = [frame_pixels(rom, MAP_KNUX, PLC_KNUX, ART_KNUX, f) for f in KNUX_FRAMES]
    dust = [frame_pixels(rom, MAP_DUST, PLC_DUST, ART_DUST, f) for f in DUST_FRAMES]
    floor = max(y for _, y in knux[0]) + 1          # where the spin dash meets the ground
    shift = WALL - floor
    frames = [cut(turn(p, shift)) for p in knux + dust]
    n = len(frames)
    head = 4 + n * 6
    maps = bytearray(n * 2)
    for i, (pieces, _) in enumerate(frames):
        struct.pack_into('>H', maps, i * 2, len(maps))
        maps += struct.pack('>H', len(pieces))
        for y, size, tile, x in pieces:
            maps += struct.pack('>bBHh', y, size, tile, x)
    tiles_at = head + len(maps)
    out = bytearray(struct.pack('>HH', head, n))
    data = bytearray()
    for pieces, td in frames:
        out += struct.pack('>IH', tiles_at + len(data), len(td) // 32)
        data += td
    out += maps + data
    counts = [len(td) // 32 for _, td in frames]
    print(f"  knuckles wall dash: {n} frames, tiles {counts[:6]} / dust {counts[6:]}")
    return bytes(out)


if __name__ == '__main__':
    import sys
    from pathlib import Path
    b = build(Path(sys.argv[1]).read_bytes())
    print(len(b))
