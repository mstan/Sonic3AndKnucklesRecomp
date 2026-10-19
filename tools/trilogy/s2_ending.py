"""Sonic 2's ending (EndingSequence; the trilogy skips the credits) as one bank blob.

Everything comes from the user's Sonic 2 cartridge: the Nemesis art the
ending loads (decoded), its Enigma plane maps (decoded, tile base 0), its
palettes, the sprite mappings / animation scripts of its cutscene objects.
The blob starts with a table of
(u32 offset, u32 length) records, in the order of ITEMS; trilogy_s2end.c
names them with the same indices (S2E_*).
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import s2_objects  # noqa: E402
from mdcodecs import enigma_decode  # noqa: E402

ITEMS = [
    # art (raw 4bpp tiles)
    ('art', 'ArtNem_EndingSonic'), ('art', 'ArtNem_EndingSuperSonic'), ('art', 'ArtNem_EndingTails'),
    ('art', 'ArtNem_EndingFinalTornado'), ('art', 'ArtNem_EndingPics'), ('art', 'ArtNem_EndingMiniTornado'),
    ('art', 'ArtNem_Tornado'), ('art', 'ArtNem_Clouds'), ('art', 'ArtNem_Flicky'), ('art', 'ArtNem_Eagle'),
    ('art', 'ArtNem_Chicken'),
    # Enigma plane maps (words, tile base 0)
    ('eni', 'MapEng_Ending1'), ('eni', 'MapEng_Ending2'), ('eni', 'MapEng_Ending3'), ('eni', 'MapEng_Ending4'),
    ('eni', 'MapEng_EndingTailsPlane'), ('eni', 'MapEng_EndingSonicPlane'),
    # palettes: Pal_AC7E..AD5E (Sonic, Tails, Background, Photos, Super Sonic: $E0 bytes)
    ('raw', 'Pal_AC7E', 0xE0),
    # sprite mappings (converted to S3K's 6-byte pieces)
    ('map', 'ObjCF_MapUnc_ADA2'), ('map', 'ObjB2_MapUnc_3AFF2'), ('map', 'ObjB3_MapUnc_3B32C'),
    ('map', 'Obj28_MapUnc_11E1C'),
    # animation scripts
    ('ani', 'Ani_objB2_a'), ('ani', 'Ani_objCF'), ('ani', 'Ani_objCD'),
]


def be16(b, a):
    return (b[a] << 8) | b[a + 1]


def be32(b, a):
    return (be16(b, a) << 16) | be16(b, a + 2)


def build(rom):
    sym = s2_objects.symbols()
    parts = []
    for it in ITEMS:
        kind, name = it[0], it[1]
        a = sym[name]
        if kind == 'art':
            data = s2_objects.nemesis_decode(rom, a)[0]
        elif kind == 'eni':
            data = enigma_decode(rom, a, 0)
        elif kind == 'raw':
            data = bytes(rom[a:a + it[2]])
        elif kind == 'map':
            data = s2_objects.convert_mappings(rom, a, {'Obj28_MapUnc_11E1C': 3}.get(name, s2_objects.MAP_FRAMES.get(name)))
        else:
            data = s2_objects.anim_blob(rom, a)
        if len(data) & 1:
            data += b'\0'
        parts.append(bytes(data))
    head = len(ITEMS) * 8
    out = bytearray()
    table = bytearray()
    off = head
    for d in parts:
        table += struct.pack('>II', off, len(d))
        off += len(d)
    out = table + b''.join(parts)
    return bytes(out)


if __name__ == '__main__':
    b = build(Path(sys.argv[1]).read_bytes())
    print(len(b), 'bytes')
