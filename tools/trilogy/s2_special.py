"""Sonic 2's half-pipe special stage, from the user's Sonic 2 cartridge, as one bank blob.

The track is drawn at run time by trilogy_s2ss.c (a transcription of
SSTrack_Draw) from the cartridge's own bitstreams (Map_SpecialStageTrack:
flags, uncompressed pattern indices into SSPNT_UncLUT, RLE runs from
SSPNT_RLELUT). Everything else the stage loads is here decoded: Nemesis /
Kosinski art and data, Enigma plane maps (tile base 0), palettes, sprite
mappings (converted to S3K's 6-byte pieces), the custom DPLCs and the
animation scripts. Like s2_ending.py, the blob starts with a table of
(u32 offset, u32 length) records in ITEMS order; trilogy_s2ss.c names them
with the same indices (SS_*).
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import s2_objects  # noqa: E402
from mdcodecs import enigma_decode, kosinski_decode  # noqa: E402

ROWS, COLS = 28, 128

ITEMS = [
    # track: the 56 frames' data (one region), their offsets, the two LUTs
    ('track',), ('trackofs',), ('rng', 'SSPNT_UncLUT', 'SSPNT_RLELUT'), ('rng', 'SSPNT_RLELUT', 'SSTrackSetOrientation'),
    # ArtKos_Special: one 4-byte row per tile, the loader repeats it 8 times (ssLdComprsdData)
    ('trackart',),
    ('kos', 'MiscKoz_SpecialPerspective'), ('nem', 'MiscNem_SpecialLevelLayout'), ('kos', 'MiscKoz_SpecialObjectLocations'),
    ('eni', 'MapEng_SpecialBack'), ('eni', 'MapEng_SpecialBackBottom'),
    # PlrList_SpecialStage + bombs + the players' art (DPLC source)
    ('nem', 'ArtNem_SpecialEmerald'), ('nem', 'ArtNem_SpecialMessages'), ('nem', 'ArtNem_SpecialHUD'),
    ('nem', 'ArtNem_SpecialFlatShadow'), ('nem', 'ArtNem_SpecialDiagShadow'), ('nem', 'ArtNem_SpecialSideShadow'),
    ('nem', 'ArtNem_SpecialExplosion'), ('nem', 'ArtNem_SpecialRings'), ('nem', 'ArtNem_SpecialStart'),
    ('nem', 'ArtNem_SpecialBack'), ('nem', 'ArtNem_SpecialStars'), ('nem', 'ArtNem_SpecialTailsText'),
    ('nem', 'ArtNem_SpecialBomb'), ('nem', 'ArtNem_SpecialSonicAndTails'),
    # palettes: Pal_SS (3 lines) + Pal_SS1..7, the emerald colours (word_35F92)
    ('rng', 'Pal_SS', 'Pal_SS1_2p'), ('rng', 'word_35F92', 'Obj59'),
    # sprite mappings
    ('map', 'Obj09_MapUnc_34212'), ('map', 'Obj10_MapUnc_34B3E'), ('map', 'Obj63_MapUnc_34492'),
    ('map', 'Obj88_MapUnc_34DA8'), ('map', 'Obj61_MapUnc_36508'), ('map', 'Obj5A_Obj5B_Obj60_MapUnc_3632A'),
    ('map', 'Obj5A_MapUnc_35E1E'), ('map', 'Obj59_MapUnc_3625A'), ('map', 'Obj5E_MapUnc_7070'),
    ('map', 'Obj5F_MapUnc_7240'), ('map', 'Obj5F_MapUnc_72D2'),
    # the custom DPLCs (Obj09_MapRUnc_345FA, verbatim)
    ('rng', 'Obj09_MapRUnc_345FA', 'JmpTo42_DisplaySprite'),
    # animation scripts
    ('ani', 'off_341E4'), ('ani', 'off_34B1C'), ('ani', 'Ani_obj88'), ('ani', 'Ani_obj61'),
    ('ani', 'Ani_obj5B_obj60'), ('ani', 'off_36228'),
    # Obj5F_MapUnc_7240.frame2's pieces as the game reads them (x at +8, 10-byte stride: see Obj5F loc_71B4)
    ('rng', 'Obj5F_MapUnc_7240.frame2', 'Obj5F_MapUnc_72D2'),
    # off_6DEE: the background offset tables, with the bytes after them (slow speeds index past the end)
    ('raw', 'off_6DEE', 0x100),
    # the results (Obj6F): ArtNem_TitleCard ($580), the results' letters (LoadTitleCardSS: tile 2 on),
    # ArtNem_SpecialStageResults ($590), ArtNem_HUD ($6CA), Art_Hud (digits), Pal_Result, the mappings
    ('nem', 'ArtNem_TitleCard'), ('letters',), ('nem', 'ArtNem_SpecialStageResults'), ('nem', 'ArtNem_HUD'),
    ('rng', 'Art_Hud', 'Art_LivesNums'), ('rng', 'Pal_Result', 'Pal_Result_End'), ('map', 'Obj6F_MapUnc_14ED0'),
]


def be16(b, a):
    return (b[a] << 8) | b[a + 1]


def be32(b, a):
    return (be16(b, a) << 16) | be16(b, a + 2)


def map_frames(rom, a):
    """Frame count of a mappings table whose frames are not in table order (Obj63)."""
    n, lo = 0, 0x10000
    while n * 2 < lo:
        lo = min(lo, be16(rom, a + n * 2))
        n += 1
    return n


def build(rom):
    sym = s2_objects.symbols()
    table = sym['Map_SpecialStageTrack']
    fr = [be32(rom, table + f * 4) for f in range(0x38)]
    t0 = fr[0]
    t1 = sym['ArtKos_Special']                                  # the track data ends where the art begins
    parts = []
    for it in ITEMS:
        kind = it[0]
        if kind == 'track':
            data = rom[t0:t1]
        elif kind == 'trackofs':
            data = b''.join(struct.pack('>I', a - t0) for a in fr)
        elif kind == 'trackart':
            art = kosinski_decode(rom, sym['ArtKos_Special'])[0]
            n = be16(art, 0)
            data = b''.join(art[2 + i * 4:6 + i * 4] * 8 for i in range(n))
        elif kind == 'rng':
            data = rom[sym[it[1]]:sym[it[2]]]
        elif kind == 'letters':                                  # SpecialStage_ResultsLetters: (tile, tiles) pairs
            src = s2_objects.nemesis_decode(rom, sym['ArtNem_TitleCard2'])[0]
            a, data = sym['SpecialStage_ResultsLetters'], b''
            while rom[a] < 0x80:
                data += src[rom[a] * 32:(rom[a] + rom[a + 1]) * 32]
                a += 2
        elif kind == 'raw':
            data = rom[sym[it[1]]:sym[it[1]] + it[2]]
        elif kind == 'kos':
            data = kosinski_decode(rom, sym[it[1]])[0]
        elif kind == 'nem':
            data = s2_objects.nemesis_decode(rom, sym[it[1]])[0]
        elif kind == 'eni':
            data = enigma_decode(rom, sym[it[1]], 0)
        elif kind == 'map':
            data = s2_objects.convert_mappings(rom, sym[it[1]], map_frames(rom, sym[it[1]]))
        else:
            data = s2_objects.anim_blob(rom, sym[it[1]])
        data = bytes(data)
        if len(data) & 1:
            data += b'\0'
        parts.append(data)
    head = len(ITEMS) * 8
    tab = bytearray()
    off = head
    for d in parts:
        tab += struct.pack('>II', off, len(d))
        off += len(d)
    return bytes(tab) + b''.join(parts)


# ---- development check: decode a frame the way SSTrack_Draw does --------
class Bits:
    """MSB-first bit reader over a byte stream (the decoder's d4/d5, d6/d7)."""
    def __init__(self, rom, pos):
        self.rom, self.pos, self.buf, self.left = rom, pos, 0, 0

    def fetch(self):
        b = self.rom[self.pos]
        self.pos += 1
        return b

    def bit(self):
        if not self.left:
            self.buf, self.left = self.fetch(), 8
        self.left -= 1
        return (self.buf >> self.left) & 1

    def bits(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | self.bit()
        return v


def decode_frame(rom, base, unc, unc2, rle, rle2):
    """SSTrack_Draw (unflipped): the frame's 28 rows of 128 pattern names."""
    w1 = be16(rom, base + 2)
    unc_hdr = base + w1 + 4
    w2 = be16(rom, unc_hdr + 2)
    rle_hdr = unc_hdr + w2 + 4
    F, U, R = Bits(rom, base + 4), Bits(rom, unc_hdr + 4), Bits(rom, rle_hdr + 4)
    rows = []
    for _ in range(ROWS):
        out = []
        while True:
            if F.bit():
                if U.bit():
                    w = be16(rom, unc + (unc2 + U.bits(10)) * 2)
                else:
                    w = be16(rom, unc + U.bits(6) * 2)
                out.append(w | 0x6000)
                continue
            if not R.left:
                b = R.fetch()
                if b == 0xFF:
                    break
                R.buf, R.left = b, 8
            if R.bit():
                i = R.bits(7)
                if i == 0x7F:
                    break
                e = rle + (rle2 + i) * 4
            else:
                e = rle + R.bits(6) * 4
            w, n = be16(rom, e), be16(rom, e + 2) + 1
            out += [w | 0xE000] * n
        out += [0] * (COLS - len(out))
        rows.append(out[:COLS])
    return rows


def frames(rom):
    sym = s2_objects.symbols()
    unc, rle = sym['SSPNT_UncLUT'], sym['SSPNT_RLELUT']
    unc2 = (sym['SSPNT_UncLUT_Part2'] - unc) // 2
    rle2 = (sym['SSPNT_RLELUT_Part2'] - rle) // 4
    table = sym['Map_SpecialStageTrack']
    return [decode_frame(rom, be32(rom, table + f * 4), unc, unc2, rle, rle2) for f in range(0x38)]


if __name__ == '__main__':
    b = build(Path(sys.argv[1]).read_bytes())
    print(len(b), 'bytes')
    for i, it in enumerate(ITEMS):
        o, n = struct.unpack_from('>II', b, i * 8)
        print(i, it, n)
