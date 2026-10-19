"""Title-card act numeral "3" in the style of the retail S3K "1"/"2"
(ArtKosM_TitleCardNum1/2): white face (colour 1), shaded right side
(colour 6), dark shadow (colour $F), over the 2's background splash.
Same 4x4-tile column-major layout as the retail digits."""


def _px(art):
    """32x32 pixel grid from 16 column-major tiles."""
    g = [[0] * 32 for _ in range(32)]
    for t in range(16):
        tx, ty = t // 4, t % 4
        for y in range(8):
            for x in range(8):
                b = art[t * 32 + y * 4 + x // 2]
                g[ty * 8 + y][tx * 8 + x] = (b >> 4) if x % 2 == 0 else b & 15
    return g


def _tiles(g):
    out = bytearray(16 * 32)
    for t in range(16):
        tx, ty = t // 4, t % 4
        for y in range(8):
            for x in range(0, 8, 2):
                out[t * 32 + y * 4 + x // 2] = (g[ty * 8 + y][tx * 8 + x] << 4) | g[ty * 8 + y][tx * 8 + x + 1]
    return bytes(out)


# 32x32 glyph: '1' white face, '6' shaded side, 'F' shadow, '.' keep background
GLYPH = [
    "................................",
    "........111111111111166.........",
    ".......F1111111111111666........",
    ".......F11111111111116666.......",
    ".......F111111111111166666......",
    "........FFFFFFFFFF111666666.....",
    "..................F11166666.....",
    "..................F11166666.....",
    "..................F11166666.....",
    "..................F11166666.....",
    "..................F1116666......",
    "..................F111666.......",
    "...........11111111111666.......",
    "..........F1111111111666........",
    "..........F111111111166.........",
    "..........F11111111116..........",
    "..........F111111111166.........",
    "..........F1111111111666........",
    "...........FFFFFFFF111666.......",
    "..................F1116666......",
    "..................F11166666.....",
    "..................F11166666.....",
    "..................F11166666.....",
    "..................F11166666.....",
    "..................F11166666.....",
    "........111111111111166666......",
    ".......F111111111111166666......",
    ".......F11111111111116666.......",
    ".......F1111111111111666........",
    ".......F111111111111166.........",
    "........FFFFFFFFFFFFFF..........",
    "................................",
]


CUSTOM = __import__('pathlib').Path(__file__).resolve().parents[2] / 'title_card_digits' / 'act_3_custom.png'
if not CUSTOM.exists():                   # (the shipped copy: data/)
    CUSTOM = __import__('pathlib').Path(__file__).resolve().parent / 'data' / 'act_3_custom.png'


def numeral_3(num2_art):
    if CUSTOM.exists():                   # a hand-drawn 3: 32x32, indexed with the card's palette indices
        from PIL import Image
        im = Image.open(CUSTOM)
        if im.mode != 'P' or im.size != (32, 32):
            raise SystemExit(f'{CUSTOM}: needs a 32x32 indexed (palette) PNG')
        px = list(im.getdata())
        return _tiles([[px[y * 32 + x] & 15 for x in range(32)] for y in range(32)])
    g = _px(num2_art)
    digit = {(x, y) for y in range(32) for x in range(32) if g[y][x] not in (0, 0xF)}
    for y in range(32):                   # keep the splash, drop the 2 and its shadow
        for x in range(32):
            near = any((x + dx, y + dy) in digit for dx in (-2, -1, 0, 1, 2) for dy in (-1, 0, 1))
            if g[y][x] != 0xF or near:
                g[y][x] = 0
    for y, row in enumerate(GLYPH):
        for x, c in enumerate(row):
            if c != '.':
                g[y][x] = int(c, 16)
    return _tiles(g)
