"""Sonic 2 object assets -> S3K engine.

Per act: the art each placed (and level-event spawned) S2 object draws with,
recoloured to the S3K palette conventions and placed in free VRAM, plus its
mappings converted to S3K format (S2 pieces are 8 bytes with a 2P tile word;
S3K's are 6) and its animation script as-is (S2's AnimateSprite format is
S1's, which o_animate transcribes).

Labels come from the s2disasm listing (build/disassembly/s2/s2.lst), whose
build is byte-identical to the REV01 cartridge. Native S2 objects live at
$7E1000 + id*4 (game/trilogy/s2_obj_*.c); asset ids are S2A_* in
game/trilogy/trilogy_assets_s2.h (an act's table is S1's or S2's, so the two
id spaces never meet).
"""
import re
import struct
from pathlib import Path

from mdcodecs import nemesis_decode
from s1_objects import RESULTS_TILES, _find_run, _piece_ranges_lines, _retile_lines, ASSET_COUNT, ASSET_REC

ROOT = Path(__file__).resolve().parents[2]
DIS = ROOT / 'build' / 'disassembly' / 's2'


def be16(b, a): return (b[a] << 8) | b[a + 1]
def s8(v): return v - 256 if v & 0x80 else v


_SYM = None


def symbols(from_data=None):
    """Listing symbol table + ArtTile_* equates."""
    global _SYM
    if _SYM is not None and from_data is None:
        return _SYM
    if from_data or (from_data is None and not (DIS / 's2.lst').exists()):
        p = Path(__file__).resolve().parent / 'data' / 's2_syms.txt'
        _SYM = {k: int(v, 16) for k, v in (l.split() for l in p.read_text().splitlines() if l.strip())}
        return _SYM
    sym = {}
    lines = (DIS / 's2.lst').read_text(encoding='latin1').splitlines()
    i = next(k for k, l in enumerate(lines) if 'Symbol Table' in l)
    for l in lines[i:]:
        for m in re.finditer(r'\*?\s*([A-Za-z_][A-Za-z0-9_.]*)\s*:\s*([0-9A-F]+)\s+C\b', l):
            sym[m.group(1)] = int(m.group(2), 16)
    for l in (DIS / 's2.constants.asm').read_text(encoding='latin1').splitlines():
        m = re.match(r'^(ArtTile_\w+)\s*=\s*\$([0-9A-Fa-f]+)', l)
        if m:
            sym[m.group(1)] = int(m.group(2), 16)
    for l in (DIS / 's2.constants.asm').read_text(encoding='latin1').splitlines():
        m = re.match(r'^(ArtTile_\w+)\s*=\s*(ArtTile_\w+)\s*([+-])\s*(\$[0-9A-Fa-f]+|\d+)', l)   # e.g. CNZBoss_Fudge
        if m and m.group(2) in sym:
            v = m.group(4)
            d = int(v[1:], 16) if v[0] == '$' else int(v)
            sym[m.group(1)] = sym[m.group(2)] + (d if m.group(3) == '+' else -d)
    _SYM = sym
    return sym


# Mapping tables whose frame count isn't first-offset/2 (another table follows the offsets)
MAP_FRAMES = {'ObjA7_ObjA8_ObjA9_Obj98_MapUnc_3921A': 7, 'ObjAA_MapUnc_39228': 9,
              'Obj8D_MapUnc_36CF0': 5, 'Obj90_MapUnc_36CFA': 3, 'Obj90_MapUnc_36D00': 1,
              'Obj89_MapUnc_30E04': 12, 'Obj51_MapUnc_320EA': 21, 'Obj52_MapUnc_302BC': 17,
              'Obj55_MapUnc_33756': 22}


def convert_mappings(rom, a, nfr=None):
    """S2 mappings (word count, 8-byte pieces) -> S3K (word count, 6-byte pieces)."""
    nfr = nfr or be16(rom, a) // 2
    frames = []
    for f in range(nfr):
        fa = a + ((be16(rom, a + f * 2) ^ 0x8000) - 0x8000)    # signed: Obj15_MapUnc_102DE reuses 10256's frames
        n = be16(rom, fa)
        out = struct.pack('>H', n)
        for i in range(n):
            p = fa + 2 + i * 8
            out += struct.pack('>bBHh', s8(rom[p]), rom[p + 1], be16(rom, p + 2), (be16(rom, p + 6) ^ 0x8000) - 0x8000)
        frames.append(out)
    offs, body = bytearray(), bytearray()
    for fr in frames:
        offs += struct.pack('>H', nfr * 2 + len(body))
        body += fr
    return bytes(offs + body)


def anim_blob(rom, a):
    """An S2 animation table verbatim, up to its furthest script terminator."""
    n = be16(rom, a) // 2
    end = a + n * 2
    for i in range(n):
        q = a + be16(rom, a + i * 2) + 1
        while rom[q] < 0xFC:
            q += 1
        q += 2 if rom[q] in (0xFE, 0xFD) else 1
        end = max(end, q)
    return bytes(rom[a:end + (end & 1)])


# ---------------------------------------------------------------- assets
# id, name, nemesis art (None = level art), ArtTile label, palette line, mappings, animation, zones
ASSETS = [
    (0, 'BUZZER',    'ArtNem_Buzzer',     'ArtTile_ArtNem_Buzzer',     0, 'Obj4B_MapUnc_2D2EA', 'Ani_obj4B', 'EHZ'),
    (1, 'MASHER',    'ArtNem_Masher',     'ArtTile_ArtNem_Masher',     0, 'Obj5C_MapUnc_2D442', 'Ani_obj5C', 'EHZ'),
    (2, 'COCONUTS',  'ArtNem_Coconuts',   'ArtTile_ArtNem_Coconuts',   0, 'Obj9D_Obj98_MapUnc_37D96', 'Ani_obj09', 'EHZ'),
    (3, 'BRIDGE',    'ArtNem_EHZ_Bridge', 'ArtTile_ArtNem_EHZ_Bridge', 2, 'Obj11_MapUnc_FC70', None, 'EHZ'),
    (4, 'WATERFALL', 'ArtNem_Waterfall',  'ArtTile_ArtNem_Waterfall',  1, 'Obj49_MapUnc_20C50', None, 'EHZ'),
    (5, 'PLAT_EHZ',  None,                'ArtTile_ArtKos_LevelArt',   2, 'Obj18_MapUnc_107F6', None, 'EHZ,HTZ'),
    (6, 'SPIKES',    'ArtNem_Spikes',     'ArtTile_ArtNem_Spikes',     1, 'Obj36_MapUnc_15B68', None,
     'EHZ,CPZ,ARZ,CNZ,HTZ,MCZ,OOZ,MTZ'),
    (7, 'HSPIKES',   'ArtNem_HorizSpike', 'ArtTile_ArtNem_HorizSpike', 1, 'Obj36_MapUnc_15B68', None,
     'EHZ,CPZ,ARZ,CNZ,HTZ,MCZ,OOZ,MTZ'),
    (8, 'SIGNPOST',  'ArtNem_Signpost',   'ArtTile_ArtNem_Signpost',   0, 'Obj0D_MapUnc_195BE', 'Ani_obj0D',
     'EHZ,CPZ,ARZ,CNZ,HTZ,MCZ,OOZ,MTZ'),
    (9, 'CAPSULE',   'ArtNem_Capsule',    'ArtTile_ArtNem_Capsule',    1, 'Obj3E_MapUnc_3F436', 'Ani_obj3E',
     'EHZ,CPZ,ARZ,CNZ,HTZ,MCZ,OOZ,MTZ'),
    # EHZ boss (Obj56): the flying pod (top on line 0, bottom on line 1), the drill car, its propeller
    (10, 'EGGPOD',   'ArtNem_Eggpod',     'ArtTile_ArtNem_Eggpod_1',   0, 'Obj56_MapUnc_2FAF8', 'Ani_obj56_c', 'EHZ'),
    (11, 'EGGPOD1',  'ArtNem_Eggpod',     'ArtTile_ArtNem_Eggpod_1',   1, 'Obj56_MapUnc_2FAF8', 'Ani_obj56_c', 'EHZ'),
    (12, 'EHZBOSS',  'ArtNem_EHZBoss',    'ArtTile_ArtNem_EHZBoss',    0, 'Obj56_MapUnc_2FA58', 'Ani_obj56_b', 'EHZ'),
    (13, 'EHZBOSS1', 'ArtNem_EHZBoss',    'ArtTile_ArtNem_EHZBoss',    1, 'Obj56_MapUnc_2FA58', 'Ani_obj56_b', 'EHZ'),
    (14, 'CHOPPERS', 'ArtNem_EggChoppers', 'ArtTile_ArtNem_EggChoppers', 1, 'Obj56_MapUnc_2F970', 'Ani_obj56_a', 'EHZ'),
    # CPZ
    (15, 'CPZBLOCK', 'ArtNem_CPZStairBlock', 'ArtTile_ArtNem_CPZStairBlock', 3, 'Obj6B_MapUnc_2800E', None, 'CPZ'),
    (16, 'ELEVATOR', 'ArtNem_CPZElevator', 'ArtTile_ArtNem_CPZElevator', 3, 'Obj19_MapUnc_2222A', None, 'CPZ'),
    (17, 'BOOSTER',  'ArtNem_CPZBooster', 'ArtTile_ArtNem_CPZBooster', 3, 'Obj1B_MapUnc_223E2', None, 'CPZ'),
    # Spiny (A5 floor, A6 wall, Obj98 shot): one art, three animation tables
    (18, 'SPINY',    'ArtNem_Spiny',      'ArtTile_ArtNem_Spiny',      1, 'ObjA5_ObjA6_Obj98_MapUnc_38CCA', 'Ani_objA5', 'CPZ'),
    (19, 'SPINYW',   'ArtNem_Spiny',      'ArtTile_ArtNem_Spiny',      1, 'ObjA5_ObjA6_Obj98_MapUnc_38CCA', 'Ani_objA6', 'CPZ'),
    (20, 'SPINYSHOT', 'ArtNem_Spiny',     'ArtTile_ArtNem_Spiny',      1, 'ObjA5_ObjA6_Obj98_MapUnc_38CCA', 'Ani_SpinyShot', 'CPZ'),
    # Grabber (A7 body, A8 legs, A9 box) and its string (AA)
    (21, 'GRABBER',  'ArtNem_Grabber',    'ArtTile_ArtNem_Grabber',    1, 'ObjA7_ObjA8_ObjA9_Obj98_MapUnc_3921A', 'Ani_objA7', 'CPZ'),
    (22, 'GRABSTR',  'ArtNem_Grabber',    'ArtTile_ArtNem_Grabber',    1, 'ObjAA_MapUnc_39228', None, 'CPZ'),
    (23, 'TIPFLOOR', 'ArtNem_CPZAnimatedBits', 'ArtTile_ArtNem_CPZAnimatedBits', 3, 'Obj0B_MapUnc_201A0', 'Ani_obj0B', 'CPZ'),
    (24, 'BLUEBALL', 'ArtNem_CPZDroplet', 'ArtTile_ArtNem_CPZDroplet', 3, 'Obj1D_MapUnc_22576', None, 'CPZ'),
    (25, 'BARRIER',  'ArtNem_ConstructionStripes', 'ArtTile_ArtNem_ConstructionStripes_2', 1, 'Obj2D_MapUnc_11822', None, 'CPZ'),
    (26, 'CPZMETAL', 'ArtNem_CPZMetalBlock', 'ArtTile_ArtNem_CPZMetalBlock', 3, 'Obj32_MapUnc_23886', None, 'CPZ'),
    (27, 'LEVERSPRING', 'ArtNem_LeverSpring', 'ArtTile_ArtNem_LeverSpring', 0, 'Obj40_MapUnc_265F4', 'Ani_obj40', 'CPZ,ARZ,MCZ'),
    (28, 'CPZPLAT',  'ArtNem_CPZStairBlock', 'ArtTile_ArtNem_CPZStairBlock', 3, 'Obj7A_MapUnc_29564', None, 'CPZ'),
    (29, 'TUBESPRING', 'ArtNem_CPZTubeSpring', 'ArtTile_ArtNem_CPZTubeSpring', 0, 'Obj7B_MapUnc_29780', 'Ani_obj7B', 'CPZ'),
    # spawned by the level, not placed: water surfaces (Obj04), the CPZ foreground pylon (Obj7C)
    (30, 'WATERSURF', 'ArtNem_WaterSurface', 'ArtTile_ArtNem_WaterSurface', 0, 'Obj04_MapUnc_20A0E', None, 'CPZ'),
    (31, 'WATERSURF2', 'ArtNem_WaterSurface2', 'ArtTile_ArtNem_WaterSurface', 0, 'Obj04_MapUnc_20AFE', None, 'ARZ'),
    (32, 'CPZPYLON', 'ArtNem_CPZMetalThings', 'ArtTile_ArtNem_CPZMetalThings', 2, 'Obj7C_MapUnc_2103C', None, 'CPZ'),
    # CPZ boss (Obj5D): pod (vehicle line 1, Eggman line 0), its jet, the pump/pipe/container art on lines 1 and 3
    (33, 'CPZPOD',   'ArtNem_Eggpod',     'ArtTile_ArtNem_Eggpod_3',   0, 'Obj5D_MapUnc_2ED8C', 'Ani_obj5D_b', 'CPZ'),
    (34, 'CPZPOD1',  'ArtNem_Eggpod',     'ArtTile_ArtNem_Eggpod_3',   1, 'Obj5D_MapUnc_2ED8C', 'Ani_obj5D_b', 'CPZ'),
    (35, 'CPZJET',   'ArtNem_EggpodJets', 'ArtTile_ArtNem_EggpodJets_1', 0, 'Obj5D_MapUnc_2EE88', None, 'CPZ'),
    (36, 'CPZBOSS',  'ArtNem_CPZBoss',    'ArtTile_ArtNem_CPZBoss',    1, 'Obj5D_MapUnc_2EADC', 'Ani_Obj5D_Dripper', 'CPZ'),
    (37, 'CPZBOSS3', 'ArtNem_CPZBoss',    'ArtTile_ArtNem_CPZBoss',    3, 'Obj5D_MapUnc_2EADC', 'Ani_Obj5D_Dripper', 'CPZ'),
    # ARZ: level-art objects, then the arrow shooter, leaves and badniks
    (38, 'PLAT_ARZ', None,                'ArtTile_ArtKos_LevelArt',   2, 'Obj18_MapUnc_1084E', None, 'ARZ'),
    (39, 'ARZSWING', None,                'ArtTile_ArtKos_LevelArt',   0, 'Obj15_Obj83_MapUnc_1021E', None, 'ARZ'),
    (40, 'ARZCOLLAPSE', None,             'ArtTile_ArtKos_LevelArt',   2, 'Obj1F_MapUnc_1115E', None, 'ARZ'),
    (41, 'ARROW',    'ArtNem_ArrowAndShooter', 'ArtTile_ArtNem_ArrowAndShooter', 0, 'Obj22_MapUnc_25804', 'Ani_obj22', 'ARZ'),
    (42, 'ARZPILLAR', None,               'ArtTile_ArtKos_LevelArt',   1, 'Obj23_MapUnc_259E6', None, 'ARZ'),
    (43, 'RISEPILLAR', None,              'ArtTile_ArtKos_LevelArt',   1, 'Obj2B_MapUnc_25C6E', None, 'ARZ'),
    (44, 'LEAVES',   'ArtNem_Leaves',     'ArtTile_ArtNem_Leaves',     3, 'Obj2C_MapUnc_2631E', None, 'ARZ'),
    (45, 'ARZBLOCK', None,                'ArtTile_ArtKos_LevelArt',   0, 'Obj82_MapUnc_2A476', None, 'ARZ'),
    (46, 'WHISP',    'ArtNem_Whisp',      'ArtTile_ArtNem_Whisp',      1, 'Obj8C_MapUnc_36A4E', 'Ani_obj8C', 'ARZ'),
    (47, 'GROUNDER', 'ArtNem_Grounder',   'ArtTile_ArtNem_Grounder',   1, 'Obj8D_MapUnc_36CF0', 'Ani_obj8D_a', 'ARZ'),
    (48, 'GROUNDERB', 'ArtNem_Grounder',  'ArtTile_ArtNem_Grounder',   1, 'Obj8D_MapUnc_36CF0', 'Ani_obj8D_b', 'ARZ'),
    (49, 'GRROCKS',  'ArtNem_Grounder',   'ArtTile_ArtNem_Grounder',   2, 'Obj90_MapUnc_36CFA', None, 'ARZ'),
    (50, 'GRWALL',   None,                'ArtTile_ArtKos_LevelArt',   0, 'Obj90_MapUnc_36D00', None, 'ARZ'),
    (51, 'CHOPCHOP', 'ArtNem_ChopChop',   'ArtTile_ArtNem_ChopChop',   1, 'Obj91_MapUnc_36EF6', 'Ani_obj91', 'ARZ'),
    # ARZ boss (Obj89): the hammer Eggmobile (ARZBoss + Eggpod art), the pillars, eyes and arrows
    (52, 'ARZBOSS',  'ArtNem_ARZBoss',    'ArtTile_ArtNem_ARZBoss',    0, 'Obj89_MapUnc_30E04', 'Ani_obj89_b', 'ARZ'),
    (53, 'ARZPILLARS', 'ArtNem_ARZBoss',  'ArtTile_ArtNem_ARZBoss',    0, 'Obj89_MapUnc_30D68', 'Ani_obj89_a', 'ARZ'),
    # CNZ
    (54, 'ROUNDBUMP', 'ArtNem_CNZRoundBumper', 'ArtTile_ArtNem_CNZRoundBumper', 2, 'Obj44_MapUnc_1F85A', 'Ani_obj44', 'CNZ'),
    (55, 'LAUNCHV',  'ArtNem_CNZVertPlunger', 'ArtTile_ArtNem_CNZVertPlunger', 0, 'Obj85_MapUnc_2B07E', None, 'CNZ'),
    (56, 'LAUNCHD',  'ArtNem_CNZDiagPlunger', 'ArtTile_ArtNem_CNZDiagPlunger', 0, 'Obj85_MapUnc_2B0EC', None, 'CNZ'),
    (57, 'FLIPPER',  'ArtNem_CNZFlipper', 'ArtTile_ArtNem_CNZFlipper', 2, 'Obj86_MapUnc_2B45A', 'Ani_obj86', 'CNZ'),
    (58, 'SNAKE',    'ArtNem_CNZSnake',   'ArtTile_ArtNem_CNZSnake',   2, 'ObjD2_MapUnc_2B694', None, 'CNZ'),
    (59, 'BIGBLOCK', 'ArtNem_BigMovingBlock', 'ArtTile_ArtNem_BigMovingBlock', 2, 'ObjD4_MapUnc_2B9CA', None, 'CNZ'),
    (60, 'CNZELEV',  'ArtNem_CNZElevator', 'ArtTile_ArtNem_CNZElevator', 2, 'ObjD5_MapUnc_2BB40', None, 'CNZ'),
    (61, 'CAGE',     'ArtNem_CNZCage',    'ArtTile_ArtNem_CNZCage',    0, 'ObjD6_MapUnc_2BEBC', 'Ani_objD6', 'CNZ'),
    (62, 'BONUSSPIKE', 'ArtNem_CNZBonusSpike', 'ArtTile_ArtNem_CNZBonusSpike', 0, 'ObjD3_MapUnc_2B8D4', None, 'CNZ'),
    (63, 'HEXBUMP',  'ArtNem_CNZHexBumper', 'ArtTile_ArtNem_CNZHexBumper', 2, 'ObjD7_MapUnc_2C626', 'Ani_objD7', 'CNZ'),
    # the saucer bumper's palette line drops 2 -> 1 -> 0 with each hit
    (64, 'MINIBUMP2', 'ArtNem_CNZMiniBumper', 'ArtTile_ArtNem_CNZMiniBumper', 2, 'ObjD8_MapUnc_2C8C4', 'Ani_objD8', 'CNZ'),
    (65, 'MINIBUMP1', 'ArtNem_CNZMiniBumper', 'ArtTile_ArtNem_CNZMiniBumper', 1, 'ObjD8_MapUnc_2C8C4', 'Ani_objD8', 'CNZ'),
    (66, 'MINIBUMP0', 'ArtNem_CNZMiniBumper', 'ArtTile_ArtNem_CNZMiniBumper', 0, 'ObjD8_MapUnc_2C8C4', 'Ani_objD8', 'CNZ'),
    (67, 'CRAWL',    'ArtNem_Crawl',      'ArtTile_ArtNem_Crawl',      0, 'ObjC8_MapUnc_3D450', 'Ani_objC8', 'CNZ'),
    # CNZ boss (Obj51): the zapper Eggmobile - mappings relative to CNZBoss_Fudge (CNZBoss - $60)
    (68, 'CNZBOSS',  'ArtNem_CNZBoss',    'ArtTile_ArtNem_CNZBoss_Fudge', 0, 'Obj51_MapUnc_320EA', 'Ani_obj51', 'CNZ'),
    # HTZ
    (69, 'SEESAW',   'ArtNem_HtzSeeSaw',  'ArtTile_ArtNem_HtzSeeSaw',  0, 'Obj14_MapUnc_21CF0', None, 'HTZ'),
    (70, 'SOLBALL',  'ArtNem_Sol',        'ArtTile_ArtNem_Sol',        0, 'Obj14_MapUnc_21D7C', None, 'HTZ'),
    (71, 'ZIPLINE',  'ArtNem_HtzZipline', 'ArtTile_ArtNem_HtzZipline', 2, 'Obj16_MapUnc_21F14', None, 'HTZ'),
    (72, 'LVL1C',    None,                'ArtTile_ArtKos_LevelArt',   2, 'Obj1C_MapUnc_113D6', None, 'HTZ'),
    (73, 'SMASHGND', None,                'ArtTile_ArtKos_LevelArt',   2, 'Obj2F_MapUnc_236FA', None, 'HTZ'),
    # badniks drawn with absolute tiles of several PLC arts (see EXTRA_ART)
    (74, 'SPIKER',   'ArtNem_Spiker',     'ArtTile_ArtKos_LevelArt',   0, 'Obj92_Obj93_MapUnc_37092', 'Ani_obj92', 'HTZ'),
    (75, 'SOL',      'ArtNem_Sol',        'ArtTile_ArtKos_LevelArt',   0, 'Obj95_MapUnc_372E6', 'Ani_obj95_a', 'HTZ'),
    (76, 'SOLFIRE',  'ArtNem_Sol',        'ArtTile_ArtKos_LevelArt',   0, 'Obj95_MapUnc_372E6', 'Ani_obj95_b', 'HTZ'),
    (77, 'REXON',    'ArtNem_Rexon',      'ArtTile_ArtNem_Rexon',      3, 'Obj94_Obj98_MapUnc_37678', None, 'HTZ'),
    (78, 'REXONFIRE', 'ArtNem_Rexon',     'ArtTile_ArtNem_Rexon',      1, 'Obj94_Obj98_MapUnc_37678', None, 'HTZ'),
    (79, 'HTZROCK',  'ArtNem_HtzRock',    'ArtTile_ArtNem_HtzRock',    2, 'Obj32_MapUnc_23852', None, 'HTZ'),
    (80, 'HTZBARRIER', 'ArtNem_HtzValveBarrier', 'ArtTile_ArtNem_HtzValveBarrier', 1, 'Obj2D_MapUnc_11822', None, 'HTZ,WFZ'),
    # HTZ boss (Obj52): one mapping table, the Eggmobile frames on Eggpod_2 (+ HTZBoss art from $60),
    # the flamethrower / lava ball frames on HTZBoss; its smoke; the ground fire (Obj20)
    (81, 'HTZBOSS',  'ArtNem_Eggpod',     'ArtTile_ArtNem_Eggpod_2',   0, 'Obj52_MapUnc_302BC', 'Ani_obj52', 'HTZ'),
    (82, 'HTZFLAME', 'ArtNem_HTZBoss',    'ArtTile_ArtNem_HTZBoss',    0, 'Obj52_MapUnc_302BC', 'Ani_obj52', 'HTZ'),
    (83, 'HTZSMOKE', 'ArtNem_BossSmoke',  'ArtTile_ArtNem_BossSmoke_2', 0, 'Obj52_MapUnc_30258', None, 'HTZ'),
    (84, 'HTZFIRE',  'ArtNem_HtzFireball1', 'ArtTile_ArtNem_HtzFireball1', 0, 'Obj20_MapUnc_23294', 'Ani_obj20', 'HTZ'),
    # MCZ: level-art platforms (swing chain / Obj7A, the swinging spike ball, stomper, brick, sliding spikes)
    (85, 'MCZPLAT',  None,                'ArtTile_ArtKos_LevelArt',   0, 'Obj15_Obj7A_MapUnc_10256', None, 'MCZ'),
    (86, 'MCZBALL',  None,                'ArtTile_ArtKos_LevelArt',   0, 'Obj15_MapUnc_102DE', None, 'MCZ'),
    (87, 'MCZCOLLAPSE', 'ArtNem_MCZCollapsePlat', 'ArtTile_ArtNem_MCZCollapsePlat', 3, 'Obj1F_MapUnc_11106', None, 'MCZ'),
    (88, 'STOMPER',  None,                'ArtTile_ArtKos_LevelArt',   2, 'Obj2A_MapUnc_11666', None, 'MCZ'),
    (89, 'CRATE',    'ArtNem_Crate',      'ArtTile_ArtNem_Crate',      3, 'Obj6A_MapUnc_27D30', None, 'MCZ'),
    (90, 'MCZBRICK', None,                'ArtTile_ArtKos_LevelArt',   1, 'Obj75_MapUnc_28D8A', None, 'MCZ'),
    (91, 'SLIDESPIKE', None,              'ArtTile_ArtKos_LevelArt',   0, 'Obj76_MapUnc_28F3A', None, 'MCZ'),
    (92, 'GATELOG',  'ArtNem_MCZGateLog', 'ArtTile_ArtNem_MCZGateLog', 3, 'Obj77_MapUnc_29064', 'Ani_obj77', 'MCZ'),
    (93, 'VINESWITCH', 'ArtNem_VineSwitch', 'ArtTile_ArtNem_VineSwitch', 3, 'Obj7F_MapUnc_29938', None, 'MCZ'),
    (94, 'VINE',     'ArtNem_VinePulley', 'ArtTile_ArtNem_VinePulley', 3, 'Obj80_MapUnc_29C64', None, 'MCZ'),
    (95, 'DRAWBRIDGE', 'ArtNem_MCZGateLog', 'ArtTile_ArtNem_MCZGateLog', 3, 'Obj81_MapUnc_2A24E', None, 'MCZ'),
    (96, 'CRAWLTON', 'ArtNem_Crawlton',   'ArtTile_ArtNem_Crawlton',   1, 'Obj9E_MapUnc_37FF2', None, 'MCZ'),
    (97, 'FLASHER',  'ArtNem_Flasher',    'ArtTile_ArtNem_Flasher',    0, 'ObjA3_MapUnc_388F0', 'Ani_objA3_a', 'MCZ'),
    (98, 'FLASHERB', 'ArtNem_Flasher',    'ArtTile_ArtNem_Flasher',    0, 'ObjA3_MapUnc_388F0', 'Ani_objA3_b', 'MCZ'),
    (99, 'FLASHERC', 'ArtNem_Flasher',    'ArtTile_ArtNem_Flasher',    0, 'ObjA3_MapUnc_388F0', 'Ani_objA3_c', 'MCZ'),
    # MCZ boss (Obj57): the drill Eggmobile (MCZBoss + Eggpod_4 art from $140); its falling stones / spikes
    (100, 'MCZBOSS', 'ArtNem_MCZBoss',    'ArtTile_ArtNem_MCZBoss',    0, 'Obj57_MapUnc_316EC', 'Ani_obj57', 'MCZ'),
    (101, 'MCZROCKS', 'ArtUnc_FallingRocks', 'ArtTile_ArtUnc_FallingRocks', 0, 'Obj57_MapUnc_316EC', None, 'MCZ'),
    # OOZ
    (102, 'OOZELEV', 'ArtNem_OOZElevator', 'ArtTile_ArtNem_OOZElevator', 3, 'Obj19_MapUnc_2222A', None, 'OOZ'),
    (103, 'OILFALL',  'ArtNem_Oilfall2',  'ArtTile_ArtNem_Oilfall2',   2, 'Obj1C_MapUnc_11406', None, 'OOZ'),
    (104, 'OILFALL2', 'ArtNem_Oilfall2',  'ArtTile_ArtNem_Oilfall2',   2, 'Obj1C_MapUnc_114AE', None, 'OOZ'),
    (105, 'OOZCOLLAPSE', 'ArtNem_OOZPlatform', 'ArtTile_ArtNem_OOZPlatform', 3, 'Obj1F_MapUnc_110C6', None, 'OOZ'),
    (106, 'BURNERLID', 'ArtNem_BurnerLid', 'ArtTile_ArtNem_BurnerLid', 3, 'Obj33_MapUnc_23DDC', None, 'OOZ'),
    (107, 'BURNER',   'ArtNem_OOZBurn',   'ArtTile_ArtNem_OOZBurn',    3, 'Obj33_MapUnc_23DF0', 'Ani_obj33', 'OOZ'),
    (108, 'STRIPEDV', 'ArtNem_StripedBlocksVert', 'ArtTile_ArtNem_StripedBlocksVert', 3, 'Obj3D_MapUnc_250BA', None, 'OOZ'),
    (109, 'STRIPEDH', 'ArtNem_StripedBlocksHoriz', 'ArtTile_ArtNem_StripedBlocksHoriz', 3, 'Obj3D_MapUnc_250BA', None, 'OOZ'),
    (110, 'FANH',     'ArtNem_OOZFanHoriz', 'ArtTile_ArtNem_OOZFanHoriz', 3, 'Obj3F_MapUnc_2AA12', None, 'OOZ'),
    (111, 'FANV',     'ArtNem_OOZFanHoriz', 'ArtTile_ArtNem_OOZFanHoriz', 3, 'Obj3F_MapUnc_2AAC4', None, 'OOZ'),
    (112, 'SPIKYTHING', 'ArtNem_SpikyThing', 'ArtTile_ArtNem_SpikyThing', 2, 'Obj43_MapUnc_23FE0', None, 'OOZ'),
    (113, 'PUSHSPRING', 'ArtNem_PushSpring', 'ArtTile_ArtNem_PushSpring', 2, 'Obj45_MapUnc_2451A', None, 'OOZ'),
    (114, 'LAUNCHBALL', 'ArtNem_LaunchBall', 'ArtTile_ArtNem_LaunchBall', 3, 'Obj48_MapUnc_254FE', None, 'OOZ'),
    (115, 'OCTUS',    'ArtNem_Octus',     'ArtTile_ArtNem_Octus',      1, 'Obj4A_MapUnc_2CBFE', 'Ani_obj4A', 'OOZ'),
    (116, 'AQUIS',    'ArtNem_Aquis',     'ArtTile_ArtNem_Aquis',      1, 'Obj50_MapUnc_2CF94', 'Ani_obj50', 'OOZ'),
    (117, 'OOZSWING', 'ArtNem_OOZSwingPlat', 'ArtTile_ArtNem_OOZSwingPlat', 2, 'Obj15_MapUnc_101E8', None, 'OOZ'),
    (118, 'OOZBOSS',  'ArtNem_OOZBoss',   'ArtTile_ArtNem_OOZBoss',    0, 'Obj55_MapUnc_33756', 'Ani_obj55', 'OOZ'),
    # MTZ: level-art platforms / stompers / barrier / steam spring / gears, then the PLC objects and badniks
    (119, 'MTZBLOCK', None,               'ArtTile_ArtKos_LevelArt',   3, 'Obj65_Obj6A_Obj6B_MapUnc_26EC8', None, 'MTZ'),
    (120, 'MTZCOG',   'ArtNem_MtzCog',    'ArtTile_ArtNem_MtzCog',     3, 'Obj65_MapUnc_26F04', None, 'MTZ'),
    (121, 'MTZBARRIER', None,             'ArtTile_ArtKos_LevelArt',   3, 'Obj2D_MapUnc_11822', None, 'MTZ'),
    (122, 'TWINSTOMP', None,              'ArtTile_ArtKos_LevelArt',   1, 'Obj64_MapUnc_26A5C', None, 'MTZ'),
    (123, 'STEAMSPRING', None,            'ArtTile_ArtKos_LevelArt',   3, 'Obj42_MapUnc_2686C', None, 'MTZ'),
    (124, 'STEAM',    'ArtNem_MtzSteam',  'ArtTile_ArtNem_MtzSteam',   1, 'Obj42_MapUnc_2686C', None, 'MTZ'),
    (125, 'BUTTON',   'ArtNem_Button',    'ArtTile_ArtNem_Button',     0, 'Obj47_MapUnc_24D96', None, 'MTZ'),
    (126, 'SPINFLASH', 'ArtNem_MtzSpinTubeFlash', 'ArtTile_ArtNem_MtzSpinTubeFlash', 3, 'Obj67_MapUnc_27548', 'Ani_obj67', 'MTZ'),
    (127, 'SPIKEBLOCK', 'ArtNem_MtzSpikeBlock', 'ArtTile_ArtNem_MtzSpikeBlock', 3, 'Obj68_Obj6D_MapUnc_27750', None, 'MTZ'),
    (128, 'MTZSPIKE', 'ArtNem_MtzSpike',  'ArtTile_ArtNem_MtzSpike',   1, 'Obj68_Obj6D_MapUnc_27750', None, 'MTZ'),
    (129, 'NUT',      'ArtNem_MtzAsstBlocks', 'ArtTile_ArtNem_MtzAsstBlocks', 1, 'Obj69_MapUnc_27A26', None, 'MTZ'),
    (130, 'LAVACUP',  'ArtNem_LavaCup',   'ArtTile_ArtNem_LavaCup',    3, 'Obj6C_MapUnc_28372', None, 'MTZ'),
    (131, 'MTZGEAR',  None,               'ArtTile_ArtKos_LevelArt',   3, 'Obj6E_MapUnc_2852C', None, 'MTZ'),
    (132, 'WHEELINDENT', 'ArtNem_MtzWheelIndent', 'ArtTile_ArtNem_MtzWheelIndent', 3, 'Obj6E_MapUnc_2852C', None, 'MTZ'),
    (133, 'MTZWHEEL', 'ArtNem_MtzWheel',  'ArtTile_ArtNem_MtzWheel',   3, 'Obj70_MapUnc_28786', None, 'MTZ'),
    (134, 'LAVABUBBLE', 'ArtNem_MtzLavaBubble', 'ArtTile_ArtNem_MtzLavaBubble', 2, 'Obj71_MapUnc_11576', 'Ani_obj71', 'MTZ'),
    (135, 'BOLTEND2', 'ArtNem_BoltEnd_Rope', 'ArtTile_ArtNem_BoltEnd_Rope', 2, 'Obj1C_MapUnc_11552', None, 'MTZ'),
    (136, 'BOLTEND1', 'ArtNem_BoltEnd_Rope', 'ArtTile_ArtNem_BoltEnd_Rope', 1, 'Obj1C_MapUnc_11552', None, 'MTZ'),
    (137, 'SHELLCRACKER', 'ArtNem_Shellcracker', 'ArtTile_ArtNem_Shellcracker', 0, 'Obj9F_MapUnc_38314', 'Ani_obj9F', 'MTZ'),
    (138, 'SLICER',   'ArtNem_MtzMantis', 'ArtTile_ArtNem_MtzMantis',  1, 'ObjA1_MapUnc_385E2', 'Ani_objA1', 'MTZ'),
    (139, 'SLICERP',  'ArtNem_MtzMantis', 'ArtTile_ArtNem_MtzMantis',  1, 'ObjA1_MapUnc_385E2', 'Ani_objA2', 'MTZ'),
    (140, 'ASTERON',  'ArtNem_MtzSupernova', 'ArtTile_ArtNem_MtzSupernova', 0, 'ObjA4_Obj98_MapUnc_38A96', 'Ani_objA4', 'MTZ'),
    # MTZ boss (Obj54 + orbs Obj53): MTZBoss art, Eggpod_4 from $184, EggpodJets_2 from $1E4
    (141, 'MTZBOSS',  'ArtNem_MTZBoss',   'ArtTile_ArtNem_MTZBoss',    0, 'Obj54_MapUnc_32DC6', 'Ani_obj53', 'MTZ'),
    # SCZ (+ WFZ): the Tornado (its pilot's six tiles come from Tails' / Sonic's DPLC, see PILOT_*),
    # its thruster, clouds, propellers, badniks
    (142, 'TORNADO',  'ArtNem_Tornado',   'ArtTile_ArtNem_Tornado',    0, 'ObjB2_MapUnc_3AFF2', 'Ani_objB2_a', 'SCZ,WFZ'),
    (143, 'THRUSTER', 'ArtNem_TornadoThruster', 'ArtTile_ArtNem_TornadoThruster', 0, 'ObjB2_MapUnc_3B292', 'Ani_objB2_b', 'SCZ,WFZ'),
    (144, 'CLOUDS',   'ArtNem_Clouds',    'ArtTile_ArtNem_Clouds',     2, 'ObjB3_MapUnc_3B32C', None, 'SCZ'),
    (145, 'VPROP',    'ArtNem_WfzVrtclPrpllr', 'ArtTile_ArtNem_WfzVrtclPrpllr', 1, 'ObjB4_MapUnc_3B3BE', 'Ani_objB4', 'SCZ,WFZ'),
    (146, 'HPROP',    'ArtNem_WfzHrzntlPrpllr', 'ArtTile_ArtNem_WfzHrzntlPrpllr', 1, 'ObjB5_MapUnc_3B548', 'Ani_objB5', 'SCZ,WFZ'),
    (147, 'BALKIRY',  'ArtNem_Balkrie',   'ArtTile_ArtNem_Balkrie',    0, 'ObjAC_MapUnc_393CC', None, 'SCZ'),
    (148, 'NEBULA',   'ArtNem_Nebula',    'ArtTile_ArtNem_Nebula',     1, 'Obj99_Obj98_MapUnc_3789A', 'Ani_obj99', 'SCZ'),
    (149, 'TURTLOID', 'ArtNem_Turtloid',  'ArtTile_ArtNem_Turtloid',   0, 'Obj9A_Obj98_MapUnc_37B62', 'Ani_obj9A', 'SCZ'),
    (150, 'TURTLEJET', 'ArtNem_Turtloid', 'ArtTile_ArtNem_Turtloid',   0, 'Obj9A_Obj98_MapUnc_37B62', 'Ani_obj9C', 'SCZ'),
    (151, 'TURTLESHOT', 'ArtNem_Turtloid', 'ArtTile_ArtNem_Turtloid',  0, 'Obj9A_Obj98_MapUnc_37B62', 'Ani_TurtloidShot', 'SCZ'),
    # WFZ: platforms, hook, Clucker (+ its base and shot), tilting platform, wall turret, the laser
    # that downs the Tornado, belt wheel, the getaway ship's thrust, belt / gun platforms, catapult,
    # breakable plating, the rivet; the laser boss and Eggman (absolute tiles of three PLC arts)
    (152, 'WFZFLOAT', 'ArtNem_WfzFloatingPlatform', 'ArtTile_ArtNem_WfzFloatingPlatform', 1, 'Obj19_MapUnc_2222A', None, 'WFZ'),
    (153, 'ROBOPLAT', 'ArtNem_WfzFloatingPlatform', 'ArtTile_ArtNem_WfzFloatingPlatform', 1, 'ObjC5_MapUnc_3CEBC', None, 'WFZ'),
    (154, 'WFZHOOK',  'ArtNem_WfzHook',   'ArtTile_ArtNem_WfzHook_Fudge', 1, 'Obj80_MapUnc_29DD0', None, 'WFZ'),
    (155, 'CLUCKBASE', 'ArtNem_WfzScratch', 'ArtTile_ArtNem_WfzScratch', 0, 'ObjAD_Obj98_MapUnc_395B4', None, 'WFZ'),
    (156, 'CLUCKER',  'ArtNem_WfzScratch', 'ArtTile_ArtNem_WfzScratch', 0, 'ObjAD_Obj98_MapUnc_395B4', 'Ani_objAE_a', 'WFZ'),
    (157, 'CLUCKERB', 'ArtNem_WfzScratch', 'ArtTile_ArtNem_WfzScratch', 0, 'ObjAD_Obj98_MapUnc_395B4', 'Ani_objAE_b', 'WFZ'),
    (158, 'CLUCKERC', 'ArtNem_WfzScratch', 'ArtTile_ArtNem_WfzScratch', 0, 'ObjAD_Obj98_MapUnc_395B4', 'Ani_objAE_c', 'WFZ'),
    (159, 'CLUCKSHOT', 'ArtNem_WfzScratch', 'ArtTile_ArtNem_WfzScratch', 0, 'ObjAD_Obj98_MapUnc_395B4', 'Ani_CluckerShot', 'WFZ'),
    (160, 'TILTPLAT', 'ArtNem_WfzTiltPlatforms', 'ArtTile_ArtNem_WfzTiltPlatforms', 1, 'ObjB6_MapUnc_3B856', 'Ani_objB6', 'WFZ'),
    (161, 'TURRET',   'ArtNem_WfzWallTurret', 'ArtTile_ArtNem_WfzWallTurret', 0, 'ObjB8_Obj98_MapUnc_3BA46', 'Ani_WallTurretShot', 'WFZ'),
    (162, 'HLASER',   'ArtNem_WfzHrzntlLazer', 'ArtTile_ArtNem_WfzHrzntlLazer', 2, 'ObjB9_MapUnc_3BB18', None, 'WFZ'),
    (163, 'BELTWHEEL', 'ArtNem_WfzConveyorBeltWheel', 'ArtTile_ArtNem_WfzConveyorBeltWheel', 2, 'ObjBA_MapUnc_3BB70', None, 'WFZ'),
    (164, 'THRUST',   'ArtNem_WfzThrust', 'ArtTile_ArtNem_WfzThrust',  2, 'ObjBC_MapUnc_3BC08', None, 'WFZ'),
    (165, 'BELTPLAT', 'ArtNem_WfzBeltPlatform', 'ArtTile_ArtNem_WfzBeltPlatform', 3, 'ObjBD_MapUnc_3BD3E', 'Ani_objBD', 'WFZ'),
    (166, 'GUNPLAT',  'ArtNem_WfzGunPlatform', 'ArtTile_ArtNem_WfzGunPlatform', 3, 'ObjBE_MapUnc_3BE46', 'Ani_objBE', 'WFZ'),
    (167, 'CATAPULT', 'ArtNem_WfzLaunchCatapult', 'ArtTile_ArtNem_WfzLaunchCatapult', 1, 'ObjC0_MapUnc_3C098', None, 'WFZ'),
    (168, 'PANELS',   'ArtNem_BreakPanels', 'ArtTile_ArtNem_BreakPanels', 3, 'ObjC1_MapUnc_3C280', 'Ani_objC1', 'WFZ'),
    (169, 'RIVET',    'ArtNem_WfzSwitch', 'ArtTile_ArtNem_WfzSwitch',  1, 'ObjC2_MapUnc_3C3C2', None, 'WFZ'),
    (170, 'WFZBOSS',  'ArtNem_WFZBoss',   'ArtTile_ArtNem_WFZBoss',    0, 'ObjC5_MapUnc_3CCD8', 'Ani_objC5', 'WFZ'),
    (171, 'ROBOTNIK', 'ArtNem_RobotnikUpper', 'ArtTile_ArtKos_LevelArt', 0, 'ObjC6_MapUnc_3D0EE', 'Ani_objC5_objC6', 'WFZ,DEZ'),
    # DEZ: barrier and Eggman's door (construction stripes), Silver Sonic (+ its LED anims, the
    # window Eggman watches from), the Death Egg Robot (+ jet / targeting anims), its bombs' fire
    (172, 'DEZBARRIER', 'ArtNem_ConstructionStripes', 'ArtTile_ArtNem_ConstructionStripes_1', 1, 'Obj2D_MapUnc_11822', None, 'DEZ'),
    (173, 'DEZDOOR',  'ArtNem_ConstructionStripes', 'ArtTile_ArtNem_ConstructionStripes_1', 1, 'ObjC6_MapUnc_3D1DE', 'Ani_objC6', 'DEZ'),
    (174, 'MECHA',    'ArtNem_SilverSonic', 'ArtTile_ArtNem_SilverSonic', 1, 'ObjAF_Obj98_MapUnc_39E68', 'off_39DE2', 'DEZ'),
    (175, 'MECHALED', 'ArtNem_SilverSonic', 'ArtTile_ArtNem_SilverSonic', 1, 'ObjAF_Obj98_MapUnc_39E68', 'off_39E30', 'DEZ'),
    (176, 'DEZWINDOW', 'ArtNem_DEZWindow', 'ArtTile_ArtNem_DEZWindow', 0, 'ObjAF_MapUnc_3A08C', 'Ani_objAF_c', 'DEZ'),
    (177, 'DEZBOSS',  'ArtNem_DEZBoss',   'ArtTile_ArtNem_DEZBoss',    0, 'ObjC7_MapUnc_3E5F8', 'Ani_objC7_a', 'DEZ'),
    (178, 'DEZJET',   'ArtNem_DEZBoss',   'ArtTile_ArtNem_DEZBoss',    0, 'ObjC7_MapUnc_3E5F8', 'Ani_objC7_b', 'DEZ'),
    (179, 'DEZTARGET', 'ArtNem_DEZBoss',  'ArtTile_ArtNem_DEZBoss',    0, 'ObjC7_MapUnc_3E5F8', 'Ani_objC7_c', 'DEZ'),
    (180, 'FIERY',    'ArtNem_FieryExplosion', 'ArtTile_ArtNem_FieryExplosion', 0, 'Obj58_MapUnc_2D50A', None, 'DEZ'),
]
# Uncompressed art: bytes
ART_UNC_SIZE = {'ArtUnc_FallingRocks': 0x100}
ASSET_NAME = {a[0]: a[1] for a in ASSETS}
# Mapping frames an asset actually draws (one art, several palette lines)
ASSET_FRAMES = {'DEZBARRIER': {0}, 'EGGPOD': set(range(1, 7)), 'EGGPOD1': {0},
                'EHZBOSS': {0}, 'EHZBOSS1': set(range(1, 8)),
                'SPIKES': set(range(0, 4)), 'HSPIKES': set(range(4, 8)), 'GRABSTR': set(range(0, 5)),
                'CPZPOD': set(range(1, 7)), 'CPZPOD1': {0},
                'CPZBOSS': {0, 1, 2, 3, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x1C, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24},
                'HTZBOSS': set(range(1, 12)) | {16}, 'HTZFLAME': {12, 13, 14, 15},
                'MCZBOSS': set(range(0, 21)) - {0xD, 0x14}, 'MCZROCKS': {0xD, 0x14},
                'STRIPEDV': {0, 1}, 'STRIPEDH': {2, 3},
                'STEAMSPRING': {7}, 'STEAM': set(range(7)), 'SPIKEBLOCK': {4}, 'MTZSPIKE': {0, 1, 2, 3},
                'MTZGEAR': {0, 1, 2}, 'WHEELINDENT': {3}, 'MTZBARRIER': {1}, 'BOLTEND2': {0, 1}, 'BOLTEND1': {2},
                'CPZBOSS3': set(range(4, 10)) | set(range(0xE, 0x1C)) | {0x1D, 0x1F, 0x25, 0x26, 0x27}}

# S2 object id -> assets it draws with
OBJ_ASSETS = {
    0x4B: ['BUZZER'], 0x5C: ['MASHER'], 0x9D: ['COCONUTS'], 0x11: ['BRIDGE'], 0x49: ['WATERFALL'],
    0x18: ['PLAT_EHZ'], 0x0D: ['SIGNPOST'], 0x3E: ['CAPSULE'],
    0x6B: ['CPZBLOCK'], 0x19: ['ELEVATOR'], 0x1B: ['BOOSTER'],
    0xA5: ['SPINY', 'SPINYSHOT'], 0xA6: ['SPINYW', 'SPINYSHOT'], 0xA7: ['GRABBER', 'GRABSTR'],
    0x0B: ['TIPFLOOR'], 0x1D: ['BLUEBALL'], 0x2D: ['BARRIER'], 0x32: ['CPZMETAL'], 0x40: ['LEVERSPRING'],
    0x78: ['CPZBLOCK'], 0x7A: ['CPZPLAT'], 0x7B: ['TUBESPRING'],
    0x15: ['ARZSWING'], 0x22: ['ARROW'], 0x23: ['ARZPILLAR'], 0x2B: ['RISEPILLAR'], 0x2C: ['LEAVES'],
    0x82: ['ARZBLOCK'], 0x83: ['ARZSWING'], 0x8C: ['WHISP'], 0x91: ['CHOPCHOP'],
    0x8D: ['GROUNDER', 'GROUNDERB', 'GRROCKS', 'GRWALL'], 0x8E: ['GROUNDER', 'GROUNDERB', 'GRROCKS', 'GRWALL'],
    0x44: ['ROUNDBUMP'], 0x86: ['FLIPPER'], 0xD2: ['SNAKE'], 0xD4: ['BIGBLOCK'], 0xD5: ['CNZELEV'],
    0x14: ['SEESAW', 'SOLBALL'], 0x16: ['ZIPLINE'], 0x2F: ['SMASHGND'], 0x92: ['SPIKER'],
    0x95: ['SOL', 'SOLFIRE'], 0x94: ['REXON', 'REXONFIRE'], 0x96: ['REXON', 'REXONFIRE'],
    0x2A: ['STOMPER'], 0x6A: ['CRATE'], 0x76: ['SLIDESPIKE'], 0x77: ['GATELOG'], 0x7F: ['VINESWITCH'],
    0x81: ['DRAWBRIDGE'], 0x9E: ['CRAWLTON'], 0xA3: ['FLASHER', 'FLASHERB', 'FLASHERC'],
    0x33: ['BURNERLID', 'BURNER'], 0x43: ['SPIKYTHING'], 0x45: ['PUSHSPRING'], 0x48: ['LAUNCHBALL'],
    0x4A: ['OCTUS'], 0x50: ['AQUIS'],
    0x42: ['STEAMSPRING', 'STEAM'], 0x47: ['BUTTON'], 0x64: ['TWINSTOMP'], 0x67: ['SPINFLASH'],
    0x68: ['SPIKEBLOCK', 'MTZSPIKE'], 0x69: ['NUT'], 0x6C: ['LAVACUP'], 0x6D: ['MTZSPIKE'], 0x70: ['MTZWHEEL'],
    0x71: ['LAVABUBBLE'], 0x9F: ['SHELLCRACKER'], 0xA1: ['SLICER', 'SLICERP'], 0xA4: ['ASTERON'],
    0xB2: ['TORNADO', 'THRUSTER'], 0xB3: ['CLOUDS'], 0xB4: ['VPROP'], 0xB5: ['HPROP'],
    0xAC: ['BALKIRY', 'TURTLEJET'], 0x99: ['NEBULA'], 0x9A: ['TURTLOID', 'TURTLESHOT'],
    0xAD: ['CLUCKBASE'], 0xAE: ['CLUCKER', 'CLUCKERB', 'CLUCKERC', 'CLUCKSHOT'], 0xB6: ['TILTPLAT'],
    0xB8: ['TURRET'], 0xB9: ['HLASER'], 0xBA: ['BELTWHEEL'], 0xBC: ['THRUST'], 0xBD: ['BELTPLAT'],
    0xBE: ['GUNPLAT'], 0xC0: ['CATAPULT'], 0xC1: ['PANELS'], 0xC2: ['RIVET'],
    0xC5: ['WFZBOSS', 'ROBOTNIK', 'ROBOPLAT'],
    0xC6: ['ROBOTNIK', 'DEZDOOR'], 0xC7: ['DEZBOSS', 'DEZJET', 'DEZTARGET', 'FIERY'],
    0xD6: ['CAGE', 'BONUSSPIKE'], 0xD7: ['HEXBUMP'], 0xD8: ['MINIBUMP2', 'MINIBUMP1', 'MINIBUMP0'], 0xC8: ['CRAWL'],
}
# Assets of objects the level itself spawns, per (zone, act index)
LEVEL_NEED = {('CPZ', 0): ['CPZPYLON'], ('CPZ', 1): ['CPZPYLON', 'WATERSURF'],
              ('ARZ', 0): ['WATERSURF2'], ('ARZ', 1): ['WATERSURF2']}
# Boss acts: assets of objects spawned by level events
BOSS_NEED = {'EHZ': ['EGGPOD', 'EGGPOD1', 'EHZBOSS', 'EHZBOSS1', 'CHOPPERS'],
             'CPZ': ['CPZPOD', 'CPZPOD1', 'CPZJET', 'CPZBOSS', 'CPZBOSS3'],
             'ARZ': ['ARZBOSS', 'ARZPILLARS'],
             'CNZ': ['CNZBOSS'],
             'HTZ': ['HTZBOSS', 'HTZFLAME', 'HTZSMOKE', 'HTZFIRE'],
             'MCZ': ['MCZBOSS', 'MCZROCKS'],
             'OOZ': ['OOZBOSS'],
             'MTZ': ['MTZBOSS'],
             'DEZ': ['MECHA', 'MECHALED', 'DEZWINDOW']}
BOSS_ASSETS = {'CAPSULE', 'EGGPOD', 'EGGPOD1', 'EHZBOSS', 'EHZBOSS1', 'CHOPPERS',
               'CPZPOD', 'CPZPOD1', 'CPZJET', 'CPZBOSS', 'CPZBOSS3', 'ARZBOSS', 'ARZPILLARS', 'CNZBOSS',
               'HTZBOSS', 'HTZFLAME', 'HTZSMOKE', 'HTZFIRE', 'MCZBOSS', 'MCZROCKS', 'OOZBOSS', 'MTZBOSS',
               'WFZBOSS', 'ROBOTNIK', 'ROBOPLAT'}
# Boss arenas (left edge the camera locks to) and how far left of it retired art must sit
BOSS_ARENA_X = {('EHZ', 1): 0x28F0, ('CPZ', 1): 0x2A20, ('ARZ', 1): 0x2A40, ('CNZ', 1): 0x2860, ('HTZ', 1): 0x2EE0,
               ('MCZ', 1): 0x2080, ('OOZ', 1): 0x2880, ('MTZ', 2): 0x2AB0, ('WFZ', 0): 0x2880}
RETIRE_MARGIN = 0x100
# Boss arenas above the water: the bubble art's window is free for the boss
BOSS_WINDOWS = {('ARZ', 1): [(0x45C, 0x494)]}
# Art uploaded only after the boss (trilogy code says when) into tiles of boss art gone by then
LATE_ASSETS = {('ARZ', 1): ['CAPSULE']}
LATE_FREES = {('ARZ', 1): ['ARZPILLARS']}           # the pillars sink into the floor at the defeat
# Free VRAM for object art (S3K common PLCs): $45C-$493 water bubbles (dry zones), $500-$57F badnik window
WINDOWS_DRY = [(0x45C, 0x494), (0x500, 0x580)]
WINDOWS_WET = [(0x500, 0x580)]
RESULTS_VISIBLE = {'SIGNPOST', 'CAPSULE'}           # on screen during the act results
WET = ('CPZ', 'ARZ')
META_REC = ASSET_COUNT - 2


# Mappings reaching past their art into another one: asset -> [(first tile, nemesis art)]
EXTRA_ART = {'ARZBOSS': [(0x120, 'ArtNem_Eggpod')],       # ArtTile_ArtNem_Eggpod_4 - ARZBoss = $120
             'CNZBOSS': [(0x159, 'ArtNem_Eggpod'),         # Eggpod_4 - CNZBoss_Fudge = $159
                         (0x60, 'ArtNem_CNZBoss')],        # CNZBoss - CNZBoss_Fudge = $60
             'HTZBOSS': [(0x60, 'ArtNem_HTZBoss')],         # HTZBoss - Eggpod_2 = $60
             'MCZBOSS': [(0x140, 'ArtNem_Eggpod')],         # Eggpod_4 - MCZBoss = $140
             'TORNADO': [(0x2A0, 'PILOT_TAILS'),            # ArtTile_ArtUnc_Tails - Tornado: the pilot's head
                         (0x280, 'PILOT_SONIC')],           # ArtTile_ArtUnc_Sonic - Tornado (Tails alone)
             'WFZHOOK': [(-4, 'ArtNem_WfzHook')],
             'DEZWINDOW': [(0x1EC, 'ArtNem_RobotnikLower'),  # Robotnik* - DEZWindow: Eggman at the window
                           (0x1A0, 'ArtNem_RobotnikRunning'),
                           (0x188, 'ArtNem_RobotnikUpper')],          # WfzHook_Fudge = WfzHook + 4
             'ROBOTNIK': [(0x564, 'ArtNem_RobotnikLower'),  # absolute VRAM tiles (PLC_WfzBoss)
                          (0x518, 'ArtNem_RobotnikRunning'),
                          (0x500, 'ArtNem_RobotnikUpper')],
             'MTZBOSS': [(0x1E4, 'ArtNem_EggpodJets'),      # EggpodJets_2 - MTZBoss = $1E4
                         (0x184, 'ArtNem_Eggpod')],         # Eggpod_4 - MTZBoss = $184
             'SPIKER': [(0x520, 'ArtNem_Spiker'), (0x3DE, 'ArtNem_Sol')],       # absolute VRAM tiles
             'SOL': [(0x3DE, 'ArtNem_Sol'), (0x39E, 'ArtNem_HtzFireball1')],
             'SOLFIRE': [(0x3DE, 'ArtNem_Sol'), (0x39E, 'ArtNem_HtzFireball1')]}


# The Tornado's pilot: six tiles of the frame Tails' (Sonic's) DPLC loads (Obj B2 Animate_Pilot)
PILOT_FRAMES = {'PILOT_TAILS': ('MapRUnc_Tails', 'ArtUnc_Tails', [0x10, 1, 2, 3, 4]),
                'PILOT_SONIC': ('MapRUnc_Sonic', 'ArtUnc_Sonic', [0x2D, 0x2E, 0x2F, 0x30])}


def pilot_tiles(rom, sym, name, frame):
    dplc, art, _ = PILOT_FRAMES[name]
    a = sym[dplc]
    fa = a + ((rom[a + frame * 2] << 8) | rom[a + frame * 2 + 1])
    n = (rom[fa] << 8) | rom[fa + 1]
    out = b''
    for i in range(n):
        w = (rom[fa + 2 + i * 2] << 8) | rom[fa + 3 + i * 2]
        cnt, off = (w >> 12) + 1, w & 0xFFF
        out += bytes(rom[sym[art] + off * 32:sym[art] + (off + cnt) * 32])
        if len(out) >= 6 * 32:
            break
    return (out + bytes(6 * 32))[:6 * 32]


def pilot_blob(rom, sym):
    """Every pilot frame, six tiles each: Tails $10,1,2,3,4 then Sonic $2D-$30."""
    return b''.join(pilot_tiles(rom, sym, k, f) for k in ('PILOT_TAILS', 'PILOT_SONIC') for f in PILOT_FRAMES[k][2])


def art_source(name, nem, t):
    """(art, tile within it) of mapping tile t of an asset"""
    for first, src in EXTRA_ART.get(name, []):
        if t >= first:
            return src, t - first
    return nem, t


def obj_assets(oid, sub, zone):
    if oid == 0x36:                       # spikes: subtype $3x/$4x... sideways
        return ['HSPIKES'] if (sub & 0xF0) in (0x10, 0x50) else ['SPIKES']
    if oid == 0x1C and zone == 'EHZ':     # bridge stakes
        return ['BRIDGE']
    if oid == 0x85:                       # launcher: vertical / diagonal plunger
        return ['LAUNCHD'] if sub else ['LAUNCHV']
    if oid == 0x18 and zone == 'ARZ':     # Obj18_Init: ARZ mappings
        return ['PLAT_ARZ']
    if zone == 'HTZ':                     # Obj1C / Obj2D / Obj32: Hill Top's art
        if oid == 0x1C:
            return ['ZIPLINE'] if sub in (3, 4, 5, 6) else ['LVL1C'] if sub in (7, 8) else []
        if oid == 0x2D:
            return ['HTZBARRIER']
        if oid == 0x32:
            return ['HTZROCK']
    if oid == 0x1F:                       # collapsing platform: per-zone art
        return {'ARZ': ['ARZCOLLAPSE'], 'MCZ': ['MCZCOLLAPSE'], 'OOZ': ['OOZCOLLAPSE']}.get(zone, [])
    if zone == 'MTZ':
        if oid in (0x6A, 0x6B):
            return ['MTZBLOCK']
        if oid == 0x65:                   # block; frame 2 (or subtype < 0: its cog) is the cog
            return ['MTZCOG'] if ((sub >> 2) & 0x1C) >> 2 == 2 else ['MTZBLOCK', 'MTZCOG'] if sub & 0x80 else ['MTZBLOCK']
        if oid == 0x2D:
            return ['MTZBARRIER']
        if oid == 0x1C:
            return ['BOLTEND1'] if sub == 3 else ['BOLTEND2']
        if oid == 0x6E:                   # frame 3: the wheel indent
            return ['WHEELINDENT'] if ((sub >> 3) & 0x0E) >> 1 == 3 else ['MTZGEAR']
    if zone == 'DEZ' and oid == 0x2D:
        return ['DEZBARRIER']
    if zone == 'WFZ':
        if oid == 0x2D:                   # Obj2D_Init's default art
            return ['HTZBARRIER']
        if oid == 0x19:
            return ['WFZFLOAT']
        if oid == 0x80:
            return ['WFZHOOK']
        if oid == 0xB2:                   # its children: the ship's flames, the thruster
            return ['TORNADO', 'THRUSTER', 'THRUST']
    if zone == 'OOZ':
        if oid == 0x19:
            return ['OOZELEV']
        if oid == 0x1C:                   # oil falls
            return ['OILFALL'] if sub < 0x10 else ['OILFALL2']
        if oid == 0x15:
            return ['OOZSWING']
        if oid == 0x3D:                   # striped block: vertical / horizontal art
            return ['STRIPEDH'] if sub else ['STRIPEDV']
        if oid == 0x3F:                   # fan: subtype < 0 blows upward
            return ['FANV'] if sub & 0x80 else ['FANH']
    if zone == 'MCZ':
        if oid == 0x15:                   # swing: subtype $4x is the spike ball on a chain
            return ['MCZPLAT', 'MCZBALL'] if (sub & 0x70) == 0x40 else ['MCZPLAT']
        if oid == 0x7A:
            return ['MCZPLAT']
        if oid == 0x75:                   # $xF: the brick, otherwise the spike ball on its chain
            return ['MCZBRICK']
        if oid == 0x80:
            return ['VINE']
    return OBJ_ASSETS.get(oid, [])


class S2Objects:
    def __init__(self, rom, tables):
        self.rom = rom
        self.sym = symbols()
        self.tables = tables

    def maps(self, name):
        rec = next(a for a in ASSETS if a[1] == name)
        return convert_mappings(self.rom, self.sym[rec[5]], MAP_FRAMES.get(rec[5]))

    def act_assets(self, layout, zone, act, boss):
        """Assets the act's object layout (list of (x, y, id, sub)) needs."""
        need = set()
        for _, _, oid, sub in layout:
            need.update(obj_assets(oid, sub, zone))
        need.update(LEVEL_NEED.get((zone, act), []))
        if boss:
            need.update(BOSS_NEED.get(zone, []))
            if zone not in ('SCZ', 'WFZ', 'DEZ'):   # no capsules from Sky Chase on
                need.add('CAPSULE')
        return need

    def level_groups(self, need):
        """(tile, count, line) runs of level art drawn by level-art objects."""
        out = []
        for aid, name, nem, tile_lbl, line, map_lbl, ani_lbl, zones in ASSETS:
            if nem or name not in need:
                continue
            base = self.sym[tile_lbl] & 0x7FF
            for t0, n, pl in _piece_ranges_lines(self.maps(name)):
                out.append((base + t0, n, (line + pl) & 3))
        return out

    def build(self, bank, zone, free_level, need, level_slot, layout=(), act=0, sign_x=None):
        rom, sym = self.rom, self.sym
        table = bytearray(ASSET_COUNT * ASSET_REC)
        self.pilot_tiles = {}
        free = set(t for a, b in (WINDOWS_WET if zone in WET else WINDOWS_DRY) for t in range(a, b)) | set(free_level)
        decoded = {}
        # Boss art may reuse the tiles of assets whose objects are all placed
        # well left of the arena (the camera never goes back; trilogy_obj.c
        # evicts any still alive when the boss art comes in).
        arena = BOSS_ARENA_X.get((zone, act))
        deferred = set(BOSS_ASSETS)
        if arena is None and sign_x is not None:
            # CheckLoadSignpostArt: the signpost art comes in when the camera locks
            # $100 short of the act's end, as in S2 (trilogy_s2dle.c)
            arena = sign_x
            deferred.add('SIGNPOST')
        retired, retired_tiles = set(), set()
        if arena is not None:
            mx = {}
            for x, _, oid, sub in layout:
                for a in obj_assets(oid, sub, zone):
                    mx[a] = max(mx.get(a, 0), x)
            retired = {a for a, x in mx.items() if x < arena - RETIRE_MARGIN
                       and a not in deferred and a not in LEVEL_NEED.get((zone, act), [])}
        late = set(LATE_ASSETS.get((zone, act), []))
        late_tiles = set()
        for pss in (0, 1, 2):                           # level, boss (deferred), late (after the boss)
            boss_pass = pss > 0
            if pss == 0:
                pool = free
            elif pss == 1:
                pool = free | retired_tiles | set(t for a, b in BOSS_WINDOWS.get((zone, act), []) for t in range(a, b))
            else:
                pool = pool | late_tiles
            groups = {}
            keep = set()                                # (on screen during the act results: clear of their art)
            todo = []
            for aid, name, nem, tile_lbl, line, map_lbl, ani_lbl, zones in ASSETS:
                if zone not in zones.split(',') or name not in need:
                    continue
                if pss != (2 if name in late else 1 if name in deferred else 0):
                    continue
                maps = convert_mappings(rom, sym[map_lbl], MAP_FRAMES.get(map_lbl))
                anim_a = bank.add(anim_blob(rom, sym[ani_lbl])) if ani_lbl else 0
                base = sym[tile_lbl] & 0x7FF
                if not nem:
                    maps = _retile_lines(maps, lambda t, pl, b=base, l=line: level_slot.get((b + t, (l + pl) & 3), b + t))
                    struct.pack_into('>IHHII', table, aid * ASSET_REC, bank.add(maps), line << 13, 0, anim_a, 0)
                    continue
                for src in [nem] + [x for _, x in EXTRA_ART.get(name, [])]:
                    if src not in decoded:
                        decoded[src] = (bytes(rom[sym[src]:sym[src] + ART_UNC_SIZE[src]]) if src in ART_UNC_SIZE
                                        else pilot_tiles(rom, sym, src, PILOT_FRAMES[src][2][0]) if src in PILOT_FRAMES
                                        else nemesis_decode(rom, sym[src])[0])
                for t0, n, pl in _piece_ranges_lines(maps, ASSET_FRAMES.get(name)):
                    run = None                          # split the piece range where the art source changes
                    for t in range(t0, t0 + n + 1):
                        cur = art_source(name, nem, t) if t < t0 + n else None
                        if run and (cur is None or cur[0] != run[0] or cur[1] != run[2]):
                            groups.setdefault((run[0], (line + pl) & 3), set()).add((run[1], run[2]))
                            if name in RESULTS_VISIBLE:
                                keep.add((run[0], (line + pl) & 3))
                            run = None
                        if cur:
                            run = [cur[0], cur[1], cur[1] + 1] if run is None else [run[0], run[1], run[2] + 1]
                todo.append((aid, nem, maps, anim_a, line))
            place, segs = {}, {}
            order = []
            for key, ivs in groups.items():
                cur = None
                for a, b in sorted(ivs):
                    if cur and a < cur[1]:
                        cur[1] = max(cur[1], b)
                        continue
                    if cur:
                        order.append((cur[1] - cur[0], key, cur[0]))
                    cur = [a, b]
                if cur:
                    order.append((cur[1] - cur[0], key, cur[0]))
            for n, key, start in sorted(order, key=lambda o: (o[1] not in keep, -o[0])):
                nem, eline = key
                raw = decoded[nem]
                n = min(n, max(0, len(raw) // 32 - start))
                if not n:
                    continue
                tab = list(range(16)) if eline == 0 else self.tables[eline]   # line 0: own colours (extended sprite line)
                art = bytes(((tab[b >> 4] << 4) | tab[b & 15]) for b in raw[start * 32:(start + n) * 32])
                t = _find_run(pool - RESULTS_TILES, n) if key in keep else None
                if t is None:
                    if key in keep:
                        print(f"    WARNING {zone}{act + 1}: {key[0]} shares the results art's tiles")
                    t = _find_run(pool, n)
                if t is None:
                    raise SystemExit(f"{zone}{act + 1}: no VRAM for {key} {start}+{n} (pool {len(pool)}, retired {sorted(retired)}, boss pass {boss_pass}, order {[(o[1][0][7:], o[1][1], o[0]) for o in order]})")
                pool -= set(range(t, t + n))
                for k in range(n):
                    place[(key, start + k)] = t + k
                segs.setdefault(nem, []).append((bank.add(art), t, n, eline == 0))
            for src in PILOT_FRAMES:                    # where the pilot's tiles went (S2I_PILOT)
                if ((src, 0), 0) in place:
                    self.pilot_tiles[src] = place[((src, 0), 0)]
            for aid, nem, maps, anim_a, line in todo:
                name = ASSET_NAME[aid]

                def retile(t, pl, nem=nem, line=line, name=name):
                    src, st = art_source(name, nem, t)
                    return place.get(((src, (line + pl) & 3), st), 0)
                lst = [sg for src in [nem] + [x for _, x in EXTRA_ART.get(name, [])] for sg in segs.get(src, [])]
                segtab = bank.add(b''.join(struct.pack('>IHH', a, d, n | (0x8000 if x else 0)) for a, d, n, x in lst)) if lst else 0
                struct.pack_into('>IHHII', table, aid * ASSET_REC, bank.add(_retile_lines(maps, retile)),
                                 line << 13, len(lst) | (0x8000 if boss_pass else 0), anim_a, segtab)
            if pss == 1:                                # art gone by the time the late art comes in
                for aid, nem, *_ in todo:
                    if ASSET_NAME[aid] in LATE_FREES.get((zone, act), []):
                        for _, d, n, _ in segs.get(nem, []):
                            late_tiles.update(range(d, d + n))
                        retired.add(ASSET_NAME[aid])
            if not boss_pass:
                # one art serves several assets: its tiles are free only if ALL its users retire
                users = {}
                for aid, nem, *_ in todo:
                    for src in [nem] + [x for _, x in EXTRA_ART.get(ASSET_NAME[aid], [])]:
                        users.setdefault(src, set()).add(ASSET_NAME[aid])
                retired = {u for nem, us in users.items() if us <= retired for u in us}
                for nem, us in users.items():
                    if us <= retired:
                        for _, d, n, _ in segs.get(nem, []):
                            retired_tiles.update(range(d, d + n))
        if arena is not None:
            print(f"    {zone}{act + 1}: boss art reuses {len(retired_tiles)} tiles of "
                  f"{', '.join(sorted(retired)) or 'nothing'} (all placed left of ${arena - RETIRE_MARGIN:X})")
        mask = sum(1 << aid for aid, *_ in ASSETS if ASSET_NAME[aid] in retired)
        struct.pack_into('>8I', table, META_REC * ASSET_REC, *[(mask >> (32 * k)) & 0xFFFFFFFF for k in range(8)])
        return bank.add(bytes(table))


def write_asset_header(path):
    if not Path(path).parent.is_dir():          # (no source checkout: the builder)
        return
    lines = ["/* Generated by tools/trilogy/s2_objects.py - Sonic 2 asset ids (per-act table). */", "#pragma once"]
    for aid, name, *_ in ASSETS:
        lines.append(f"#define S2A_{name:<10} {aid}")
    Path(path).write_text('\n'.join(lines) + '\n', newline='\n')
