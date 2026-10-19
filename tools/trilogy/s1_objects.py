"""Sonic 1 objects -> S3K engine: object/ring layouts, the S1 object pointer
table, and per-zone object assets (art remapped to the S3K palette
conventions, mappings converted to S3K format, S1 animation scripts as-is).

Native objects live at $7E0000 + id*4 (game/trilogy/s1_obj_*.c).
"""
import re
import struct
from pathlib import Path

from mdcodecs import nemesis_decode

NATIVE_S1 = 0x7E0000
ROOT = Path(__file__).resolve().parents[2]

# S3K object code addresses used for S1 placements (retail cartridge)
S3K_OBJ = {
    'Monitor': 0x01D566, 'Spring': None, 'StarPost': None, 'EggCapsule': None, 'SSEntryRing': 0x06166A,
}

# S1 monitor contents -> S3K monitor subtype
S1_MONITOR = {0: 0, 1: 2, 2: 1, 3: 4, 4: 7, 5: 8, 6: 3, 7: 3, 8: 3, 9: 3}

MON_FIRE, MON_LIGHTNING, MON_BUBBLE = 5, 6, 7    # S3K monitor subtypes


# Giant rings the trilogy adds: Sonic 1 has none in Scrap Brain, and every
# act 1 and 2 gets one, past the signpost as S1 places its own ((zone, act): [(x, y)]).
EXTRA_GIANT_RINGS = {(5, 0): [(0x22A8, 0x2F8)],          # SBZ1: sign $2260,$348
                     (5, 1): [(0x1F28, 0x554)]}          # SBZ2: sign $1EE0,$5A4


def elemental_shields(objs, seed):
    """S1/S2 have one shield; their shield monitors take S3K's three elements in
    turn along the act (fire, lightning, bubble, by x), each act starting the
    cycle at a different element so every one turns up everywhere.
    objs: (x, y, id, subtype) sorted by x; 0x26 = monitor."""
    out, k = [], seed
    for x, y, i, s in objs:
        if i == 0x26 and s == MON_BUBBLE:
            s = MON_FIRE + k % 3
            k += 1
        out.append((x, y, i, s))
    return out

RING_POS = [(0x10, 0), (0x18, 0), (0x20, 0), (0, 0x10), (0, 0x18), (0, 0x20), (0x10, 0x10), (0x18, 0x18),
            (0x20, 0x20), (-0x10, 0x10), (-0x18, 0x18), (-0x20, 0x20), (0x10, 8), (0x18, 0x10), (-0x10, 8), (-0x18, 0x10)]


def be16(b, a): return (b[a] << 8) | b[a + 1]
def be32(b, a): return (be16(b, a) << 16) | be16(b, a + 2)
def s8(v): return v - 256 if v & 0x80 else v


def load_symbols(from_data=None):
    d = ROOT / 'build' / 'disassembly'
    if from_data or (from_data is None and not (d / 's1.equ').exists()):
        return _data_syms('s1_syms.txt')
    sym = {}
    for line in open(d / 's1.equ'):
        n, v = line.split()
        sym[n] = int(v, 16) & 0xFFFFFFFF
    # ArtTile_* equates come from the constants source
    consts = {}
    for line in open(d / 's1' / '_Constants.asm', encoding='utf-8', errors='replace'):
        m = re.match(r"^(ArtTile_\w+):\s*equ\s+([^;]+)", line)
        if m:
            consts[m[1]] = m[2].strip()

    def ev(name, depth=0):
        e = consts[name]
        e = e.replace('$', '0x')
        e = re.sub(r"ArtTile_\w+", lambda mm: str(ev(mm[0], depth + 1)), e)
        return eval(e)
    for n in consts:
        sym[n] = ev(n)
    return sym




def _data_syms(name):
    """data/<name> (export_syms.py): the builder's copy, when there is no disassembly."""
    p = Path(__file__).resolve().parent / 'data' / name
    return {k: int(v, 16) for k, v in (l.split() for l in p.read_text().splitlines() if l.strip())}


def convert_mappings(rom, a):
    """S1 mappings (byte count, 5-byte pieces) -> S3K (word count, 6-byte pieces)."""
    first = be16(rom, a)
    nfr = first // 2
    frames = []
    for f in range(nfr):
        fa = a + be16(rom, a + f * 2)
        n = rom[fa]
        out = struct.pack('>H', n)
        for i in range(n):
            p = fa + 1 + i * 5
            y = s8(rom[p])
            size = rom[p + 1]
            tile = be16(rom, p + 2)
            x = s8(rom[p + 4])
            out += struct.pack('>bBHh', y, size, tile, x)
        frames.append(out)
    offs = bytearray()
    body = bytearray()
    for fr in frames:
        offs += struct.pack('>H', nfr * 2 + len(body))
        body += fr
    return bytes(offs + body)


def anim_blob(rom, a):
    """Copy an S1 animation table verbatim (format matches S3K Animate_Sprite).
    Size is bounded by the furthest script terminator."""
    n = be16(rom, a) // 2
    end = a + n * 2
    for i in range(n):
        p = a + be16(rom, a + i * 2)
        q = p + 1
        while rom[q] < 0xFC:
            q += 1
        q += 1
        if rom[q - 1] in (0xFE, 0xFD):
            q += 1
        end = max(end, q)
    return bytes(rom[a:end + (end & 1)])


# ---------------------------------------------------------------- assets
# id, name, nemesis label (None = level art), S1 art tile label, palette line,
# mappings label, animation label, zones
ASSETS = [
    (0, 'BRIDGE',   'Nem_Bridge',    'ArtTile_GHZ_Bridge',        2, 'Map_Bri',       None,       'GHZ'),
    (1, 'SWING',    'Nem_Swing',     'ArtTile_GHZ_MZ_Swing',      2, 'Map_Swing_GHZ', None,       'GHZ,MZ'),
    (2, 'SPIKEPOLE', 'Nem_SpikePole', 'ArtTile_GHZ_Spike_Pole',   2, 'Map_Hel',       None,       'GHZ'),
    (3, 'GBALL',    'Nem_Ball',      'ArtTile_GHZ_Giant_Ball',    2, 'Map_GBall',     None,       'GHZ'),
    (4, 'PROCK',    'Nem_PplRock',   'ArtTile_GHZ_Purple_Rock',   3, 'Map_PRock',     None,       'GHZ'),
    (5, 'SMASH',    'Nem_GhzWall1',  'ArtTile_GHZ_SLZ_Smashable_Wall', 2, 'Map_Smash', None,      'GHZ'),
    (6, 'EDGE',     'Nem_GhzWall2',  'ArtTile_GHZ_Edge_Wall',     2, 'Map_Edge',      None,       'GHZ'),
    (7, 'PLAT',     None,            'ArtTile_Level',             2, 'Map_Plat_GHZ',  None,       'GHZ'),
    (8, 'LEDGE',    None,            'ArtTile_Level',             2, 'Map_Ledge',     None,       'GHZ'),
    (9, 'SCEN',     None,            'ArtTile_Level',             2, 'Map_Scen',      None,       'GHZ'),
    (10, 'CRAB',    'Nem_Crabmeat',  'ArtTile_Crabmeat',          0, 'Map_Crab',      'Ani_Crab', 'GHZ,SYZ'),
    (11, 'BUZZ',    'Nem_Buzz',      'ArtTile_Buzz_Bomber',       0, 'Map_Buzz',      'Ani_Buzz', 'GHZ,MZ,SYZ'),
    (12, 'MISSILE', 'Nem_Buzz',      'ArtTile_Buzz_Bomber',       1, 'Map_Missile',   'Ani_Missile', 'GHZ,MZ,SYZ'),
    (13, 'CHOP',    'Nem_Chopper',   'ArtTile_Chopper',           0, 'Map_Chop',      'Ani_Chop', 'GHZ'),
    (14, 'NEWT',    'Nem_Newtron',   'ArtTile_Newtron',           0, 'Map_Newt',      'Ani_Newt', 'GHZ'),
    (15, 'NEWT2',   'Nem_Newtron',   'ArtTile_Newtron',           1, 'Map_Newt',      'Ani_Newt', 'GHZ'),
    (16, 'MOTO',    'Nem_Motobug',   'ArtTile_Moto_Bug',          0, 'Map_Moto',      'Ani_Moto', 'GHZ'),
    (18, 'SWINGCHAIN', 'Nem_Swing',   'ArtTile_GHZ_MZ_Swing',      0, 'Map_Swing_GHZ', None,       'GHZ,MZ'),
    (19, 'EGGMAN',  'Nem_Eggman',    'ArtTile_Eggman',            0, 'Map_Eggman',    'Ani_Eggman', 'GHZ,LZ,MZ,SLZ,SYZ,SBZ'),
    (20, 'BOSSITEMS', 'Nem_Weapons', 'ArtTile_Eggman_Weapons',    0, 'Map_BossItems', None,       'GHZ,LZ,MZ,SLZ,SYZ,SBZ'),
    (17, 'SPIKES',  'Nem_Spikes',    'ArtTile_Spikes',            0, 'Map_Spike',     None,       'GHZ,LZ,MZ,SLZ,SYZ,SBZ'),
    (21, 'SIGN',    'Nem_SignPost',  'ArtTile_Signpost',          0, 'Map_Sign',      'Ani_Sign', 'GHZ,LZ,MZ,SLZ,SYZ,SBZ'),
    (22, 'PRISON',  'Nem_Prison',    'ArtTile_Prison_Capsule',    0, 'Map_Pri',       'Ani_Pri',  'GHZ,LZ,MZ,SLZ,SYZ,SBZ'),
    (23, 'BONUS',   'Nem_Bonus',     'ArtTile_Hidden_Points',     0, 'Map_Bonus',     None,       'GHZ,LZ,MZ,SLZ,SYZ,SBZ'),
    (24, 'FIRE',    'Nem_MzFire',    'ArtTile_MZ_Fireball',       0, 'Map_Fire',      'Ani_Fire', 'MZ,SLZ'),
    (25, 'GFIRE',   'Nem_MzFire',    'ArtTile_MZ_Fireball',       0, 'Map_Fire',      'Ani_GFire', 'MZ'),
    (26, 'METAL',   'Nem_MzMetal',   'ArtTile_MZ_Spike_Stomper',  0, 'Map_CStom',     None,       'MZ'),
    (27, 'GLASS',   'Nem_MzGlass',   'ArtTile_MZ_Glass_Pillar',   2, 'Map_Glass',     None,       'MZ'),
    (28, 'GEYSER',  'Nem_Lava',      'ArtTile_MZ_Lava',           3, 'Map_Geyser',    'Ani_Geyser', 'MZ'),
    (29, 'LWALL',   'Nem_Lava',      'ArtTile_MZ_Lava',           3, 'Map_LWall',     'Ani_LWall', 'MZ'),
    (30, 'BASARAN', 'Nem_Basaran',   'ArtTile_Basaran',           0, 'Map_Bas',       'Ani_Bas',  'MZ'),
    (31, 'CATER',   'Nem_Cater',     'ArtTile_MZ_SYZ_Caterkiller', 1, 'Map_Cat',     None,       'MZ,SYZ,SBZ'),
    (32, 'BUTTON_MZ', 'Nem_MzSwitch', 'ArtTile_Button',           2, 'Map_But',       None,       'MZ'),
    (33, 'BUTTON',  'Nem_LzSwitch',  'ArtTile_Button',            0, 'Map_But',       None,       'SYZ,LZ,SBZ'),
    (34, 'PUSH',    'Nem_MzBlock',   'ArtTile_MZ_Block',          2, 'Map_Push',      None,       'MZ'),
    (35, 'SMAB',    'Nem_MzBlock',   'ArtTile_MZ_Block',          2, 'Map_Smab',      None,       'MZ'),
    (36, 'MBLOCK',  'Nem_MzBlock',   'ArtTile_MZ_Block',          2, 'Map_MBlock',    None,       'MZ'),
    (37, 'CFLO',    'Nem_MzBlock',   'ArtTile_MZ_Block',          2, 'Map_CFlo',      None,       'MZ'),
    (38, 'LGRASS',  None,            'ArtTile_Level',             2, 'Map_LGrass',    None,       'MZ'),
    (39, 'BRICK',   None,            'ArtTile_Level',             2, 'Map_Brick',     None,       'MZ'),
    (40, 'BOSSITEMS2', 'Nem_Weapons', 'ArtTile_Eggman_Weapons',   1, 'Map_BossItems', None,       'MZ,SYZ,SLZ,LZ,SBZ'),
    (41, 'PLAT_SYZ', None,            'ArtTile_Level',             2, 'Map_Plat_SYZ',  None,       'SYZ'),
    (42, 'LIGHT',   None,            'ArtTile_Level',             0, 'Map_Light',     None,       'SYZ'),
    (43, 'BUMPER',  'Nem_Bumper',    'ArtTile_SYZ_Bumper',        0, 'Map_Bump',      'Ani_Bump', 'SYZ'),
    (44, 'ROLLER',  'Nem_Roller',    'ArtTile_Roller',            0, 'Map_Roll',      'Ani_Roll', 'SYZ'),
    (45, 'YADRIN',  'Nem_Yadrin',    'ArtTile_Yadrin',            1, 'Map_Yad',       'Ani_Yad',  'SYZ'),
    (46, 'FBLOCK',  None,            'ArtTile_Level',             2, 'Map_FBlock',    None,       'SYZ,SLZ'),
    (47, 'SBALL',   'Nem_SyzSpike2', 'ArtTile_SYZ_Spikeball_Chain', 0, 'Map_SBall',   None,       'SYZ'),
    (48, 'BBALL',   'Nem_SyzSpike1', 'ArtTile_SYZ_Big_Spikeball', 0, 'Map_BBall',     None,       'SYZ'),
    (49, 'BOSSBLOCK', None,          'ArtTile_Level',             2, 'Map_BossBlock', None,       'SYZ'),
    (50, 'SURF',    'Nem_Water',     'ArtTile_LZ_Water_Surface',  2, 'Map_Surf',      None,       'LZ'),
    (51, 'FBLOCK_LZ', 'Nem_LzDoor1', 'ArtTile_LZ_Door',           2, 'Map_FBlock',    None,       'LZ'),
    (52, 'SBALL_LZ', 'Nem_LzSpikeBall', 'ArtTile_LZ_Spikeball_Chain', 0, 'Map_SBall2', None,      'LZ'),
    (53, 'MBLOCK_LZ', 'Nem_LzBlock3', 'ArtTile_LZ_Moving_Block',  2, 'Map_MBlockLZ',  None,       'LZ'),
    (54, 'POLE',    'Nem_LzPole',    'ArtTile_LZ_Pole',           2, 'Map_Pole',      None,       'LZ'),
    (55, 'FLAP',    'Nem_FlapDoor',  'ArtTile_LZ_Flapping_Door',  2, 'Map_Flap',      'Ani_Flap', 'LZ'),
    (56, 'HARP',    'Nem_Harpoon',   'ArtTile_LZ_Harpoon',        0, 'Map_Harp',      'Ani_Harp', 'LZ'),
    (57, 'JAWS',    'Nem_Jaws',      'ArtTile_Jaws',              1, 'Map_Jaws',      'Ani_Jaws', 'LZ'),
    (58, 'BURRO',   'Nem_Burrobot',  'ArtTile_Burrobot',          0, 'Map_Burro',     'Ani_Burro', 'LZ'),
    (59, 'ORB_LZ',  'Nem_Orbinaut',  'ArtTile_LZ_Orbinaut',       0, 'Map_Orb',       'Ani_Orb',  'LZ'),
    (60, 'LBLOCK',  'Nem_LzDoor2',   'ArtTile_LZ_Blocks',         2, 'Map_LBlock',    None,       'LZ'),
    (61, 'GAR',     'Nem_Gargoyle',  'ArtTile_LZ_Gargoyle',       2, 'Map_Gar',       None,       'LZ'),
    (62, 'GARFIRE', 'Nem_Gargoyle',  'ArtTile_LZ_Gargoyle',       0, 'Map_Gar',       None,       'LZ'),
    (63, 'LCONV',   'Nem_LzWheel',   'ArtTile_LZ_Conveyor_Belt',  2, 'Map_LConv',     None,       'LZ'),
    (64, 'LWHEEL',  'Nem_LzWheel',   'ArtTile_LZ_Conveyor_Belt',  0, 'Map_LConv',     None,       'LZ'),
    (65, 'WFALL',   'Nem_Splash',    'ArtTile_LZ_Splash',         2, 'Map_WFall',     'Ani_WFall', 'LZ'),
    (66, 'PLAT_SLZ', None,           'ArtTile_Level',             2, 'Map_Plat_SLZ',  None,       'SLZ'),
    (67, 'SWING_SLZ', 'Nem_SlzSwing', 'ArtTile_SLZ_Swing',        2, 'Map_Swing_SLZ', None,       'SLZ'),
    (68, 'SWINGCHAIN_SLZ', 'Nem_SlzSwing', 'ArtTile_SLZ_Swing',   0, 'Map_Swing_SLZ', None,       'SLZ'),
    (69, 'SCEN_SLZ', 'Nem_SlzCannon', 'ArtTile_SLZ_Fireball_Launcher', 2, 'Map_Scen', None,       'SLZ'),
    (70, 'SMASH_SLZ', 'Nem_SlzWall', 'ArtTile_GHZ_SLZ_Smashable_Wall', 2, 'Map_Smash', None,      'SLZ'),
    (71, 'CFLO_SLZ', 'Nem_SlzBlock', 'ArtTile_SLZ_Collapsing_Floor', 2, 'Map_CFlo',  None,       'SLZ'),
    (72, 'ORB_SLZ', 'Nem_Orbinaut',  'ArtTile_SLZ_Orbinaut',      1, 'Map_Orb',       'Ani_Orb',  'SLZ'),
    (73, 'BOMB',    'Nem_Bomb',      'ArtTile_Bomb',              0, 'Map_Bomb',      'Ani_Bomb', 'SLZ,SBZ'),
    (74, 'ELEV',    None,            'ArtTile_Level',             2, 'Map_Elev',      None,       'SLZ'),
    (75, 'CIRC',    None,            'ArtTile_Level',             2, 'Map_Circ',      None,       'SLZ'),
    (76, 'STAIR',   None,            'ArtTile_Level',             2, 'Map_Stair',     None,       'SLZ'),
    (77, 'PYLON',   'Nem_Pylon',     'ArtTile_SLZ_Pylon',         0, 'Map_Pylon',     None,       'SLZ'),
    (78, 'FAN',     'Nem_Fan',       'ArtTile_SLZ_Fan',           2, 'Map_Fan',       None,       'SLZ'),
    (79, 'SEESAW',  'Nem_Seesaw',    'ArtTile_SLZ_Seesaw',        0, 'Map_Seesaw',    None,       'SLZ'),
    (80, 'SSAWBALL', 'Nem_SlzSpike', 'ArtTile_SLZ_Spikeball',     0, 'Map_SSawBall',  None,       'SLZ'),
    (81, 'BSBALL',  'Nem_SlzSpike',  'ArtTile_Eggman_Spikeball',  0, 'Map_SSawBall',  None,       'SLZ'),
    # boss bomb fragments: Map_BSBall reads the two Nem_Bomb shrapnel tiles that
    # survive under the spikeball/exhaust art in PLC_Boss
    (82, 'BSFRAG',  'Nem_Bomb',      'ArtTile_Eggman_Spikeball',  0, 'Map_BSBall',    None,       'SLZ'),
    (83, 'HOG',     'Nem_BallHog',   'ArtTile_Ball_Hog',          1, 'Map_Hog',       'Ani_Hog',  'SBZ'),
    (84, 'ADOOR',   'Nem_SbzDoor1',  'ArtTile_SBZ_Door',          2, 'Map_ADoor',     'Ani_ADoor', 'SBZ'),
    (85, 'JUN',     'Nem_SbzWheel2', 'ArtTile_SBZ_Junction',      2, 'Map_Jun',       None,       'SBZ'),
    (86, 'DISC',    'Nem_SbzWheel1', 'ArtTile_SBZ_Disc',          2, 'Map_Disc',      None,       'SBZ'),
    (87, 'TRAP',    'Nem_TrapDoor',  'ArtTile_SBZ_Trap_Door',     2, 'Map_Trap',      'Ani_Spin', 'SBZ'),
    (88, 'SPIN',    'Nem_SpinPform', 'ArtTile_SBZ_Spinning_Platform', 0, 'Map_Spin',  'Ani_Spin', 'SBZ'),
    (89, 'SPINC',   'Nem_SpinPform', 'ArtTile_SBZ_Spinning_Platform', 0, 'Map_Spin',  'Ani_SpinConvey', 'SBZ'),
    (90, 'SAW',     'Nem_Cutter',    'ArtTile_SBZ_Saw',           2, 'Map_Saw',       None,       'SBZ'),
    (91, 'STOMP',   'Nem_Stomper',   'ArtTile_SBZ_Moving_Block_Short', 1, 'Map_Stomp', None,      'SBZ'),
    (92, 'STOMP_SBZ3', None,         'ArtTile_LZ_Block_2',        2, 'Map_Stomp',     None,       'LZ'),
    (93, 'VANP',    'Nem_SbzBlock',  'ArtTile_SBZ_Vanishing_Block', 2, 'Map_VanP',    'Ani_Van',  'SBZ'),
    (94, 'FLAME',   'Nem_FlamePipe', 'ArtTile_SBZ_Flamethrower',  0, 'Map_Flame',     'Ani_Flame', 'SBZ'),
    (95, 'ELEC',    'Nem_Electric',  'ArtTile_SBZ_Electric_Orb',  0, 'Map_Elec',      'Ani_Elec', 'SBZ'),
    (96, 'GIRD',    'Nem_Girder',    'ArtTile_SBZ_Girder',        2, 'Map_Gird',      None,       'SBZ'),
    (97, 'MBLOCK_SBZS', 'Nem_Stomper', 'ArtTile_SBZ_Moving_Block_Short', 1, 'Map_MBlock', None,   'SBZ'),
    (98, 'MBLOCK_SBZL', 'Nem_SlideFloor', 'ArtTile_SBZ_Moving_Block_Long', 2, 'Map_MBlock', None, 'SBZ'),
    (99, 'CFLO_SBZ', 'Nem_SbzFloor*2', 'ArtTile_SBZ_Collapsing_Floor', 2, 'Map_CFlo', None,       'SBZ'),
    (100, 'ORB_SBZ', 'Nem_Orbinaut', 'ArtTile_SBZ_Orbinaut',      0, 'Map_Orb',       'Ani_Orb',  'SBZ'),
    (101, 'SWING_SBZ', 'Nem_SyzSpike1', 'ArtTile_SBZ_Swing',      0, 'Map_BBall',     None,       'SBZ'),
    (102, 'SWINGANCH_SBZ', 'Nem_SyzSpike1', 'ArtTile_SBZ_Swing',  2, 'Map_BBall',     None,       'SBZ'),
    # SBZ2 ending: Eggman on foot, his switch, the trap floor
    (103, 'SEGG',   'Nem_Sbz2Eggman', 'ArtTile_Eggman',           0, 'Map_SEgg',      'Ani_SEgg', 'SBZ'),
    (104, 'SEGGBUT', 'Nem_LzSwitch', 'ArtTile_Eggman_Button',     0, 'Map_But',       None,       'SBZ'),
    (105, 'FFLOOR', 'Nem_SbzBlock',  'ArtTile_Eggman_Trap_Floor', 2, 'Map_FFloor',    None,       'SBZ'),
    # Final Zone
    (106, 'FZEGG',  'Nem_Sbz2Eggman', 'ArtTile_FZ_Eggman_No_Vehicle', 0, 'Map_SEgg',  'Ani_SEgg', 'SBZ'),
    (107, 'EGGCYL', 'Nem_FzBoss',    'ArtTile_FZ_Boss',           0, 'Map_EggCyl',    None,       'SBZ'),
    (108, 'FZLEGS', 'Nem_FzEggman',  'ArtTile_FZ_Eggman_Fleeing', 0, 'Map_FZLegs',    None,       'SBZ'),
    (109, 'FZDMG',  'Nem_FzEggman',  'ArtTile_FZ_Eggman_Fleeing', 0, 'Map_FZDamaged', 'Ani_FZEgg', 'SBZ'),
    (110, 'PLAUNCH', 'Nem_FzBoss',   'ArtTile_FZ_Boss',           0, 'Map_PLaunch',   'Ani_PLaunch', 'SBZ'),
    (111, 'PLASMA', 'Nem_FzBoss',    'ArtTile_FZ_Boss',           1, 'Map_Plasma',    'Ani_Plasma', 'SBZ'),
]
ASSET_COUNT = 256
ASSET_NAME = {a[0]: a[1] for a in ASSETS}
ASSET_TILE = {a[0]: a[3] for a in ASSETS}
BOSS_ASSETS = {'EGGMAN', 'BOSSITEMS', 'BOSSITEMS2', 'PRISON', 'BSBALL', 'BSFRAG',
               'SEGG', 'SEGGBUT', 'FFLOOR', 'FZEGG', 'EGGCYL', 'FZLEGS', 'FZDMG', 'PLAUNCH', 'PLASMA'}
BADNIK_ASSETS = {'CRAB', 'BUZZ', 'MISSILE', 'CHOP', 'NEWT', 'NEWT2', 'MOTO', 'BASARAN', 'CATER',
                 'ROLLER', 'YADRIN', 'JAWS', 'BURRO', 'ORB_LZ', 'ORB_SLZ', 'BOMB', 'HOG', 'ORB_SBZ'}
ASSET_REC = 16

# Mapping frames an asset can actually show (default: all). Only these frames'
# tiles get VRAM, so every asset fits in its own exclusive range.
ASSET_FRAMES = {
    'NEWT': [0, 1, 3, 4, 5, 6, 7, 8, 9, 10],     # blue: Ani_Newt .drop/.fly1/.fly2
    'NEWT2': [0, 1, 2],                          # green: Ani_Newt .fires
    ('BOSSITEMS', 'GHZ'): [0, 1],                # chain anchors
    ('BOSSITEMS2', 'MZ'): [4],                   # Eggman's lava tube
    ('BOSSITEMS2', 'SYZ'): [5],                  # Eggman's spike
    ('BOSSITEMS2', 'SLZ'): [3],                  # Eggman's bomb tube
    'BUTTON_MZ': [0, 1],                         # Nem_MzSwitch has no flashing frames
    'FBLOCK_LZ': [6, 7],                         # LZ doors only
    'GAR': [0, 1], 'GARFIRE': [2, 3],            # head (line 2) / fireball (line 0)
    'LCONV': [4], 'LWHEEL': [0, 1, 2, 3],        # platform (line 2) / wheel (line 0)
    'SBALL_LZ': [0, 1, 2],
    'SWING_SLZ': [0, 2], 'SWINGCHAIN_SLZ': [1],  # platform + anchor (line 2) / chain (line 0)
    'SMASH_SLZ': [1], 'CFLO_SLZ': [2, 3],
    'STOMP': [0, 1, 2, 3], 'STOMP_SBZ3': [4],
    'SEGGBUT': [0, 1],
    'SEGG': [0, 1, 2, 3, 4, 5, 6, 7, 8],           # SBZ2 never shows the FZ tube/cockpit frames    # SBZ1/2 stompers + door / SBZ3 big door (level art)
    'MBLOCK_SBZS': [2], 'MBLOCK_SBZL': [3], 'CFLO_SBZ': [0, 1],
    'SWING_SBZ': [0, 1], 'SWINGANCH_SBZ': [2],   # ball + chain (line 0) / anchor (line 2)
}


# Assets whose mappings also draw from a second art set loaded after the main
# one: tiles >= offset come from that Nemesis source (S1 boss exhaust flame at
# ArtTile_Eggman+$12A = Nem_Exhaust).
ASSET_EXTRA_ART = {'EGGMAN': [(0x12A, 'Nem_Exhaust')],
                   # SBZ door frame 0 = the horizontal door at ArtTile_SBZ_Horizontal_Door
                   'STOMP': [(0x1AF, 'Nem_SbzDoor2')],
                   # FZ "Eggman in the tube": tile $6F0 wraps to ArtTile_FZ_Boss+$60 (the glass)
                   'FZEGG': [(0x690, 'Nem_FzBoss')],
                   # LZ door 7 = the large horizontal door at ArtTile_LZ_Blocks
                   'FBLOCK_LZ': [(0x22, 'Nem_LzDoor2')],
                   # LZ blocks: rising platform, cork, block 1 (ArtTile_LZ_Blocks-relative)
                   'LBLOCK': [(0x69, 'Nem_LzPlatfm'), (0x11A, 'Nem_Cork'), (0x5FA, 'Nem_LzBlock1')],
                   'METAL': [(0x21B, 'Nem_Spikes')],      # stomper spikes = ArtTile_Spikes
                   # lava wall/geyser pieces draw the animated magma level tiles
                   # ($2D2, pinned in place): 'LEVEL' = absolute tile base+t
                   'GEYSER': [(0x700, 'LEVEL')], 'LWALL': [(0x700, 'LEVEL')]}


# obGfx = art tile + offset (S1 buttons use ArtTile_Button+4: their mappings
# reach 4 tiles back to the start of the art)
ASSET_GFX_OFFSET = {'BUTTON': 4}
# art loaded this many tiles past the object's art tile (PLC_SLZ: Nem_SlzWall
# at ArtTile_GHZ_SLZ_Smashable_Wall+4): mapping tile t = art tile t-shift
ASSET_ART_SHIFT = {'SMASH_SLZ': 4,
                   'SEGGBUT': -4}                 # PLC_EggmanSBZ2: switch art at ArtTile_Eggman_Button-4             # MZ loads its switch art at +4 as well


def asset_mappings(rom, sym, name, map_lbl, tile_lbl, line):
    """Converted mappings whose words are exactly what S1 drew: S1 adds obGfx
    to each mapping word as one 16-bit add, so "negative" tiles ($FDFA = -$206)
    and carries into the flag bits are real. Each word becomes (sum - line
    bits) with the tile made relative to the asset's art tile."""
    maps = convert_mappings(rom, sym[map_lbl])
    base = sym[tile_lbl] & 0x7FF
    gfx = (line << 13) + base + ASSET_GFX_OFFSET.get(name, 0)
    out = bytearray(maps)
    nfr = be16(maps, 0) // 2
    for f in range(nfr):
        fa = be16(maps, f * 2)
        for i in range(be16(maps, fa)):
            p = fa + 2 + i * 6
            eff = (gfx + be16(maps, p + 2)) & 0xFFFF
            w = ((eff - (line << 13)) & 0xF800) | (((eff & 0x7FF) - base) & 0x7FF)
            out[p + 2], out[p + 3] = w >> 8, w & 0xFF
    return bytes(out)


def decode_art(rom, sym, src):
    """Decoded tiles of a Nemesis source; 'Nem_X*2' = the art twice in a row
    (PLC_SBZ2 reloads Nem_SbzFloor 4 tiles after PLC_SBZ's copy)."""
    if src.endswith('*2'):
        art = decode_art(rom, sym, src[:-2])
        return art + art
    art, _ = nemesis_decode(rom, sym[src])
    return art


def art_source(name, nem, t):
    """(nemesis label, tile within it) for mapping tile t of asset name."""
    t -= ASSET_ART_SHIFT.get(name, 0)
    for off, extra in sorted(ASSET_EXTRA_ART.get(name, []), reverse=True):
        if t >= off:
            return extra, t - off
    return nem, t


def asset_frames(name, zone):
    return ASSET_FRAMES.get((name, zone), ASSET_FRAMES.get(name))

# Free VRAM regions for object art in 1P levels (from S3K's common PLCs):
# $45C-$493 water bubbles (dry zones only), $500-$57F zone badnik window.
# NOTE tiles $600+ are VRAM $C000+: planes, H-scroll and the sprite table.
WINDOWS_DRY = [(0x45C, 0x494), (0x500, 0x580)]
WINDOWS_WET = [(0x500, 0x580)]
# S3K's act results load their art over $520-$5AF (ArtKosM_ResultsGeneral, the
# act number, the character's name); what stays on screen meanwhile avoids it
RESULTS_TILES = set(range(0x520, 0x5B0))
RESULTS_VISIBLE = {'SIGN', 'BONUS', 'PRISON'}

# S1 object id -> assets it draws with (subtype-dependent ones handled below)
OBJ_ASSETS = {
    0x11: ['BRIDGE'], 0x15: ['SWING', 'GBALL', 'SWINGCHAIN'], 0x17: ['SPIKEPOLE'], 0x18: ['PLAT'],
    0x1A: ['LEDGE'], 0x1C: ['BRIDGE'], 0x1F: ['CRAB'], 0x22: ['BUZZ', 'MISSILE'],
    0x2B: ['CHOP'], 0x3B: ['PROCK'], 0x3C: ['SMASH'], 0x40: ['MOTO'], 0x44: ['EDGE'],
    0x36: ['SPIKES'], 0x3E: ['PRISON'], 0x0D: ['SIGN'], 0x7D: ['BONUS'],
    0x13: ['FIRE'], 0x14: ['FIRE'], 0x2F: ['LGRASS', 'GFIRE'], 0x30: ['GLASS'], 0x31: ['METAL'],
    0x32: ['BUTTON_MZ', 'BUTTON'], 0x33: ['PUSH'], 0x46: ['BRICK'], 0x4C: ['GEYSER'],
    0x4E: ['LWALL'], 0x51: ['SMAB'], 0x52: ['MBLOCK'], 0x53: ['CFLO'], 0x55: ['BASARAN'],
    0x78: ['CATER'], 0x12: ['LIGHT'], 0x43: ['ROLLER'], 0x47: ['BUMPER'], 0x50: ['YADRIN'],
    0x56: ['FBLOCK'], 0x57: ['SBALL'], 0x58: ['BBALL'],
    0x0B: ['POLE'], 0x0C: ['FLAP'], 0x16: ['HARP'], 0x2C: ['JAWS'], 0x2D: ['BURRO'],
    0x60: ['ORB_LZ'], 0x61: ['LBLOCK'], 0x62: ['GAR', 'GARFIRE'], 0x65: ['WFALL'],
    0x59: ['ELEV'], 0x5A: ['CIRC'], 0x5B: ['STAIR'], 0x5C: ['PYLON'], 0x5D: ['FAN'],
    0x5E: ['SEESAW', 'SSAWBALL'], 0x5F: ['BOMB'],
    0x1E: ['HOG'], 0x20: ['HOG'], 0x2A: ['ADOOR'], 0x66: ['JUN'], 0x67: ['DISC'], 0x6A: ['SAW'],
    0x6B: ['STOMP'], 0x6C: ['VANP'], 0x6D: ['FLAME'], 0x6E: ['ELEC'], 0x6F: ['SPINC'], 0x70: ['GIRD'],
}

# Assets every act of a zone needs whatever is placed (spawned by the zone itself)
ZONE_NEED = {'LZ': ['SURF']}
# Same object id, other art in some zones
ZONE_ASSET = {('LZ', 'FBLOCK'): 'FBLOCK_LZ', ('LZ', 'SBALL'): 'SBALL_LZ', ('LZ', 'MBLOCK'): 'MBLOCK_LZ',
              ('SYZ', 'PLAT'): 'PLAT_SYZ',
              ('SLZ', 'PLAT'): 'PLAT_SLZ', ('SLZ', 'SWING'): 'SWING_SLZ', ('SLZ', 'SWINGCHAIN'): 'SWINGCHAIN_SLZ',
              ('SLZ', 'SMASH'): 'SMASH_SLZ', ('SLZ', 'CFLO'): 'CFLO_SLZ', ('SLZ', 'ORB_LZ'): 'ORB_SLZ',
              ('SBZ', 'ORB_LZ'): 'ORB_SBZ', ('SBZ', 'CFLO'): 'CFLO_SBZ', ('LZ', 'STOMP'): 'STOMP_SBZ3',
              ('SBZ', 'SWING'): 'SWING_SBZ', ('SBZ', 'SWINGCHAIN'): 'SWING_SBZ'}

# Boss acts: assets of the boss objects spawned by level events (not placed)
BOSS_NEED = {'GHZ': ['EGGMAN', 'BOSSITEMS', 'GBALL', 'SWINGCHAIN'],
             'MZ': ['EGGMAN', 'BOSSITEMS2'],
             'SYZ': ['EGGMAN', 'BOSSITEMS2', 'BOSSBLOCK'],
             'LZ': ['EGGMAN'],
             'SLZ': ['EGGMAN', 'BOSSITEMS2', 'BSBALL', 'BSFRAG'],
             ('SBZ', 1): ['SEGG', 'SEGGBUT', 'FFLOOR'],
             ('SBZ', 2): ['EGGMAN', 'FZEGG', 'EGGCYL', 'FZLEGS', 'FZDMG', 'PLAUNCH', 'PLASMA']}


def obj_assets(oid, sub, zone=None):
    """Assets one placement (id, subtype) draws with."""
    if zone == 'SBZ' and oid == 0x52:     # MBlock_Main: 5228 short (Pal2), others long
        return ['MBLOCK_SBZS'] if sub == 0x28 else ['MBLOCK_SBZL']
    if zone == 'SBZ' and oid == 0x15:     # Swing_Main: SBZ swinging spiked ball
        return ['SWING_SBZ', 'SWINGANCH_SBZ']
    return [ZONE_ASSET.get((zone, a), a) for a in _obj_assets(oid, sub)]


def _obj_assets(oid, sub):
    if oid == 0x1C:                       # scenery: SLZ fireball launchers (0-2), GHZ bridge stump (3)
        return ['SCEN_SLZ'] if sub < 3 else ['BRIDGE']
    if oid == 0x69:                       # SBZ: trapdoors, or spinning platforms ($8x)
        return ['SPIN'] if sub & 0x80 else ['TRAP']
    if oid == 0x63:                       # conveyor: wheel ($7F), platform, platform maker ($8x)
        return ['LWHEEL'] if sub == 0x7F else ['LCONV']
    if oid == 0x15:                       # Swing_Main: type $1X = giant ball
        return ['SWING', 'SWINGCHAIN'] + (['GBALL'] if sub & 0x10 else [])
    if oid == 0x42:                       # Newtron: type 0 blue, else green + missile
        return ['NEWT2', 'MISSILE'] if sub else ['NEWT']
    return OBJ_ASSETS.get(oid, [])
WET_ZONES = ('LZ',)

# Boss arenas (S1 boss_*_x): once the boss appears the camera's left edge is
# locked here, so objects placed well left of it can never load again. Boss
# art may only use the tiles of assets whose EVERY placement lies there; the
# runtime also removes any straggler using them when the boss art is loaded.
BOSS_ARENA_X = {('GHZ', 2): 0x2960, ('LZ', 2): 0x1DE0, ('MZ', 2): 0x1800,
                ('SLZ', 2): 0x2000, ('SYZ', 2): 0x2C00}
RETIRE_MARGIN = 0x100
META_REC = ASSET_COUNT - 2   # last two records = 256-bit retired mask (8 x u32, low word first)


def mapping_tile_span(maps):
    """(lowest, highest) tile offset referenced by S3K-format mappings."""
    nfr = be16(maps, 0) // 2
    lo, hi = None, None
    for f in range(nfr):
        fa = be16(maps, f * 2)
        n = be16(maps, fa)
        for i in range(n):
            p = fa + 2 + i * 6
            size = maps[p + 1]
            w = ((size >> 2) & 3) + 1
            h = (size & 3) + 1
            t = be16(maps, p + 2) & 0x7FF
            lo = t if lo is None else min(lo, t)
            hi = max(hi or 0, t + w * h - 1)
    return (lo or 0, hi or 0)


class S1Objects:
    def __init__(self, rom, remap_tables, level_limit):
        self.rom = rom
        self.sym = load_symbols()
        self.tables = remap_tables       # line -> 16-entry pixel remap
        self.limit = level_limit

    def _place(self, bank, pending, decoded, free, zone_name):
        # Runs are unions of OVERLAPPING mapping pieces only: pieces that merely
        # touch are placed independently (mappings are retiled per tile).
        groups = {}
        for aid, nem, maps, anim_a, line in pending:
            for t0, n, pl in _piece_ranges_lines(maps, asset_frames(ASSET_NAME[aid], zone_name)):
                src, s0 = art_source(ASSET_NAME[aid], nem, t0)
                if src == 'LEVEL':
                    continue
                groups.setdefault((src, (line + pl) & 3), set()).add((s0, s0 + n))
        # art still on screen during the act results (signpost, hidden bonus)
        # stays clear of the results art S3K loads at $520-$5AF
        keep = {(art_source(ASSET_NAME[aid], nem, t0)[0], (line + pl) & 3)
                for aid, nem, maps, anim_a, line in pending if ASSET_NAME[aid] in RESULTS_VISIBLE
                for t0, n, pl in _piece_ranges_lines(maps, asset_frames(ASSET_NAME[aid], zone_name))}
        place = {}
        segs = {}
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
        def key_rank(k):
            return 0 if k in keep else 1
        for n, key, start in sorted(order, key=lambda o: (key_rank(o[1]), -o[0])):
            nem, eline = key
            raw = decoded[nem]
            if start + n > len(raw) // 32:
                print(f"    WARNING {zone_name}: {key} tiles {start}+{n} run past the art ({len(raw) // 32} tiles)")
                n = max(0, len(raw) // 32 - start)
                if n == 0:
                    continue
            tab = list(range(16)) if eline == 0 else self.tables[eline]   # line 0: own colours (extended sprite line)
            art = bytes(((tab[b >> 4] << 4) | tab[b & 15]) for b in raw[start * 32:(start + n) * 32])
            t = _find_run(free - RESULTS_TILES, n) if key in keep else None
            if t is None:
                if key in keep:
                    print(f"    WARNING {zone_name}: {key} shares the results art's tiles")
                t = _find_run(free, n)
            if t is None:
                raise SystemExit(f"{zone_name}: no VRAM for {key} group {start}+{n}")
            free -= set(range(t, t + n))
            for k in range(n):
                place[(key, start + k)] = t + k
            segs.setdefault(nem, []).append((bank.add(art), t, n, eline == 0))
        return place, segs

    def act_assets(self, zone, act):
        """Asset names the act's object layout needs."""
        rom = self.rom
        oi = self.sym['ObjPos_Index']
        a = oi + be16(rom, oi + (zone * 4 + act) * 4)
        name = ['GHZ', 'LZ', 'MZ', 'SLZ', 'SYZ', 'SBZ'][zone]
        need = set()
        while be16(rom, a) != 0xFFFF:
            oid = rom[a + 4] & 0x7F
            sub = rom[a + 5]
            need.update(obj_assets(oid, sub, name))
            a += 6
        # boss acts (spawned by level events, not placed in the layout)
        if (name in ('GHZ', 'MZ', 'SYZ', 'SLZ') and act == 2) or (name == 'LZ' and act == 2)                 or (name == 'SBZ' and act in (1, 2)):
            need.update(BOSS_NEED.get((name, act), BOSS_NEED.get(name, ['EGGMAN', 'BOSSITEMS'])))
        need.update(ZONE_NEED.get(name, []))
        return need

    def asset_max_x(self, zone, act):
        """Asset name -> rightmost x of any placement that draws with it."""
        rom = self.rom
        oi = self.sym['ObjPos_Index']
        a = oi + be16(rom, oi + (zone * 4 + act) * 4)
        mx = {}
        name = ['GHZ', 'LZ', 'MZ', 'SLZ', 'SYZ', 'SBZ'][zone]
        while be16(rom, a) != 0xFFFF:
            x = be16(rom, a)
            oid = rom[a + 4] & 0x7F
            sub = rom[a + 5]
            for n in obj_assets(oid, sub, name):
                mx[n] = max(mx.get(n, 0), x)
            a += 6
        return mx

    def level_asset_groups(self, need):
        """Contiguous tile runs (with palette line) that level-art objects draw."""
        rom, sym = self.rom, self.sym
        out = []
        for aid, name, nem, tile_lbl, line, map_lbl, ani_lbl, zones in ASSETS:
            if nem or name not in need:
                continue
            maps = asset_mappings(rom, sym, name, map_lbl, tile_lbl, line)
            base = sym[tile_lbl] & 0x7FF
            for t0, n in _piece_ranges(maps):
                out.append((base + t0, n, line))
        return out

    def build_assets(self, bank, zone_name, free_level, need=None, level_slot=None, act=None):
        """Allocate VRAM and add art/mappings/anims for one zone. used_tiles is
        the set of level tiles (< limit) referenced by the zone's blocks.

        Art is placed per referenced tile group wherever it fits; the converted
        mappings are rewritten to absolute VRAM tiles (art_tile keeps only the
        palette/priority bits), so object art never needs a contiguous run.
        Record: u32 mappings, u16 art_tile, u16 segments, u32 anim, u32 segtab
        where segtab = segments x (u32 src, u16 dst_tile, u16 tiles)."""
        rom, sym = self.rom, self.sym
        table = bytearray(ASSET_COUNT * ASSET_REC)
        wins = WINDOWS_WET if zone_name in WET_ZONES else WINDOWS_DRY
        free = set(t for a, b in wins for t in range(a, b)) | set(free_level)
        pending = []
        decoded = {}
        for aid, name, nem, tile_lbl, line, map_lbl, ani_lbl, zones in ASSETS:
            if zone_name not in zones.split(',') or (need is not None and name not in need):
                continue
            maps = asset_mappings(rom, sym, name, map_lbl, tile_lbl, line)
            anim_a = bank.add(anim_blob(rom, sym[ani_lbl])) if ani_lbl else 0
            if not nem:
                base = sym[tile_lbl] & 0x7FF
                maps = _retile(maps, lambda t: level_slot.get((base + t, line), base + t))
                maps_a = bank.add(maps)
                art_tile = (line << 13)
                struct.pack_into('>IHHII', table, aid * ASSET_REC, maps_a, art_tile, 0, anim_a, 0)
                continue
            for src in [nem] + [x for _, x in ASSET_EXTRA_ART.get(name, [])]:
                if src not in decoded and src != 'LEVEL':
                    decoded[src] = decode_art(rom, sym, src)
            pending.append((aid, nem, maps, anim_a, line))
        # Two placement passes: level assets, then boss assets (deferred:
        # uploaded when the boss appears). Every asset owns its tiles outright;
        # nothing shares VRAM with anything else.
        arena = BOSS_ARENA_X.get((zone_name, act))
        retired = set()
        if arena is not None:
            mx = self.asset_max_x(['GHZ', 'LZ', 'MZ', 'SLZ', 'SYZ', 'SBZ'].index(zone_name), act)
            retired = {n for n, x in mx.items()
                       if n in BADNIK_ASSETS and x < arena - RETIRE_MARGIN}
        retired_tiles = set()
        for boss_pass in (False, True):
            subset = [p for p in pending if (ASSET_NAME[p[0]] in BOSS_ASSETS) == boss_pass]
            pool = (free | retired_tiles) if boss_pass else free
            place, segs = self._place(bank, subset, decoded, pool, zone_name)
            for aid, nem, maps, anim_a, line in subset:
                name = ASSET_NAME[aid]

                base = sym[ASSET_TILE[aid]] & 0x7FF

                def retile(t, pl, name=name, nem=nem, line=line, base=base):
                    src, st = art_source(name, nem, t)
                    if src == 'LEVEL':
                        a = (base + t) & 0x7FF
                        return level_slot.get((a, (line + pl) & 3), a) if level_slot else a
                    # tiles outside the art (S1 drew whatever VRAM held) -> blank tile 0
                    return place.get(((src, (line + pl) & 3), st), 0)
                maps_a = bank.add(_retile_lines(maps, retile))
                lst = segs.get(nem, []) + [sg for _, x in ASSET_EXTRA_ART.get(name, []) for sg in segs.get(x, [])]
                segtab = bank.add(b''.join(struct.pack('>IHH', a, d, n | (0x8000 if x else 0)) for a, d, n, x in lst)) if lst else 0
                struct.pack_into('>IHHII', table, aid * ASSET_REC, maps_a, (line << 13),
                                 len(lst) | (0x8000 if boss_pass else 0), anim_a, segtab)
            if not boss_pass:
                # art is shared per source (Buzz + missile, Newtron + missile):
                # a source's tiles are free only if ALL its users are retired
                for nem in segs:
                    users = [ASSET_NAME[q[0]] for q in subset if q[1] == nem]
                    if users and all(u in retired for u in users):
                        for a, d, n, _ in segs[nem]:
                            retired_tiles.update(range(d, d + n))
                retired = {ASSET_NAME[q[0]] for q in subset
                           if all(ASSET_NAME[r[0]] in retired for r in subset if r[1] == q[1])}
        mask = sum(1 << aid for aid, *_ in ASSETS if ASSET_NAME[aid] in retired)
        if arena is not None:
            print(f"    {zone_name}{act + 1}: boss art reuses {len(retired_tiles)} tiles of "
                  f"{', '.join(sorted(retired)) or 'nothing'} (all placed left of ${arena - RETIRE_MARGIN:X})")
        struct.pack_into('>8I', table, META_REC * ASSET_REC,
                         *[(mask >> (32 * k)) & 0xFFFFFFFF for k in range(8)])
        return bank.add(bytes(table))

    # ------------------------------------------------------------ layouts
    def convert_objects(self, zone, act):
        """Returns (object layout bytes, ring layout bytes)."""
        rom = self.rom
        oi = self.sym['ObjPos_Index']
        a = oi + be16(rom, oi + (zone * 4 + act) * 4)
        objs = []
        rings = []
        while be16(rom, a) != 0xFFFF:
            x = be16(rom, a)
            yw = be16(rom, a + 2)
            oid = rom[a + 4] & 0x7F
            sub = rom[a + 5]
            a += 6
            y = yw & 0xFFF
            flips = (yw >> 14) & 3
            if oid == 0x25:                       # ring group -> ring layout
                n = (sub & 7)
                if n == 7:
                    n = 6
                dx, dy = RING_POS[sub >> 4]
                for i in range(n + 1):
                    rings.append((x + dx * i, y + dy * i))
                continue
            if oid == 0x26:
                sub = S1_MONITOR.get(sub, 0)
            if oid == 0x4B:                       # giant ring: S3K collected-ring bit
                sub = sum(1 for o in objs if o[2] == 0x4B)
            objs.append((x, (y | (flips << 13) | 0x8000), oid, sub))
        for x, y in EXTRA_GIANT_RINGS.get((zone, act), []):
            objs.append((x, y | 0x8000, 0x4B, sum(1 for o in objs if o[2] == 0x4B)))
        objs.sort(key=lambda o: o[0])
        objs = elemental_shields(objs, zone * 4 + act)
        rings.sort()
        ob = b''.join(struct.pack('>HHBB', x, y, i, s) for x, y, i, s in objs) + b'\xFF\xFF\0\0\0\0'
        rb = b'\0\0\0\0' + b''.join(struct.pack('>HH', x & 0xFFFF, y & 0xFFFF) for x, y in rings) + b'\xFF\xFF\0\0'
        return b'\xFF\xFF\0\0\0\0' + ob, rb

    def pointer_table(self, s3k_addr):
        """256 object code pointers for S1 ids."""
        out = bytearray()
        for i in range(256):
            a = NATIVE_S1 + i * 4
            if i == 0x26:
                a = s3k_addr['Obj_Monitor']
            elif i == 0x41:
                a = s3k_addr['Obj_Spring']
            elif i == 0x79:
                a = s3k_addr['Obj_StarPost']
            elif i == 0x71:
                a = s3k_addr['Obj_InvisibleBlock']
            elif i == 0x64:                       # S1 bubbles = S3K bubbler (S2's port of it)
                a = s3k_addr['Obj_Bubbler']
            out += struct.pack('>I', a)
        return bytes(out)


def _find_run(pool, n):
    """Best-fit contiguous run of n tiles in the set pool."""
    runs = []
    start = prev = None
    for t in sorted(pool):
        if start is None:
            start = t
        elif t != prev + 1:
            runs.append((start, prev - start + 1))
            start = t
        prev = t
    if start is not None:
        runs.append((start, prev - start + 1))
    fits = [r for r in runs if r[1] >= n]
    if not fits:
        return None
    return min(fits, key=lambda r: r[1])[0]


def _piece_ranges(maps):
    nfr = be16(maps, 0) // 2
    for f in range(nfr):
        fa = be16(maps, f * 2)
        n = be16(maps, fa)
        for i in range(n):
            p = fa + 2 + i * 6
            size = maps[p + 1]
            cnt = (((size >> 2) & 3) + 1) * ((size & 3) + 1)
            yield (be16(maps, p + 2) & 0x7FF, cnt)


def _runs(tiles):
    out = []
    start = prev = None
    for t in tiles:
        if start is None:
            start = t
        elif t != prev + 1:
            out.append((start, prev - start + 1))
            start = t
        prev = t
    if start is not None:
        out.append((start, prev - start + 1))
    return out


def _retile(maps, fn):
    """Rewrite each piece's tile index (keeping flip/palette/priority bits).
    A piece's tiles were placed contiguously from its first tile."""
    out = bytearray(maps)
    nfr = be16(maps, 0) // 2
    for f in range(nfr):
        fa = be16(maps, f * 2)
        n = be16(maps, fa)
        for i in range(n):
            p = fa + 2 + i * 6
            w = be16(maps, p + 2)
            nt = fn(w & 0x7FF)
            w = (w & 0xF800) | (nt & 0x7FF)
            out[p + 2] = w >> 8
            out[p + 3] = w & 0xFF
    return bytes(out)


def _piece_ranges_lines(maps, frames=None):
    nfr = be16(maps, 0) // 2
    for f in range(nfr):
        if frames is not None and f not in frames:
            continue
        fa = be16(maps, f * 2)
        n = be16(maps, fa)
        for i in range(n):
            p = fa + 2 + i * 6
            size = maps[p + 1]
            cnt = (((size >> 2) & 3) + 1) * ((size & 3) + 1)
            w = be16(maps, p + 2)
            yield (w & 0x7FF, cnt, (w >> 13) & 3)


def _retile_lines(maps, fn):
    out = bytearray(maps)
    nfr = be16(maps, 0) // 2
    for f in range(nfr):
        fa = be16(maps, f * 2)
        n = be16(maps, fa)
        for i in range(n):
            p = fa + 2 + i * 6
            w = be16(maps, p + 2)
            nt = fn(w & 0x7FF, (w >> 13) & 3)
            w = (w & 0xF800) | (nt & 0x7FF)
            out[p + 2] = w >> 8
            out[p + 3] = w & 0xFF
    return bytes(out)


def write_asset_header(path):
    if not Path(path).parent.is_dir():          # (no source checkout: the builder)
        return
    lines = ["/* Generated by tools/trilogy/s1_objects.py - asset ids (per-zone table). */", "#pragma once"]
    for aid, name, *_ in ASSETS:
        lines.append(f"#define S1A_{name:<10} {aid}")
    lines.append(f"#define TRI_ASSET_COUNT {ASSET_COUNT}")
    # same object, other art in some zones: {zone id, asset, zone's asset}
    zid = {'GHZ': 0x20, 'LZ': 0x21, 'MZ': 0x22, 'SLZ': 0x23, 'SYZ': 0x24, 'SBZ': 0x25}
    ids = {name: aid for aid, name, *_ in ASSETS}
    rows = ', '.join(f"{{0x{zid[z]:02X}, S1A_{a}, S1A_{b}}}" for (z, a), b in sorted(ZONE_ASSET.items()))
    lines.append(f"#define TRI_ZONE_ASSETS {{ {rows} }}")
    Path(path).write_text('\n'.join(lines) + '\n', newline='\n')
