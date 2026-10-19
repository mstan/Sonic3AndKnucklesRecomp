"""Build Mighty-only menu and HUD data from the verified v3 character frame.

Native Sonic/Tails portraits and palettes remain available. The life head is
sampled into S3K's 16px icon footprint; the name uses a compact 5px HUD font.
All generated cartridge graphics stay in the owner's private bank.
"""
import struct
from .mighty import table


def presentation(donor, s3k):
    offsets, animation = donor.mighty_animations()
    frame = animation[offsets[5] + 1]  # actual standing/wait animation
    pieces, pixels = donor.mighty_frames()[frame]
    maps = 0xCE0E
    records = []
    for i in range(36):
        p = maps + int.from_bytes(s3k[maps+i*2:maps+i*2+2], 'big')
        count = int.from_bytes(s3k[p:p+2], 'big')
        records.append(s3k[p:p+2+count*6])

    def avatar(x):
        output = []
        for p in pieces:
            attributes = p['attributes'] & 0x8000
            # Host sprite patterns avoid every tile owned by the native menu.
            tile = p['tile']
            tile |= attributes | (0x800 if p['x_flip'] else 0) | (0x1000 if p['y_flip'] else 0)
            output.append(struct.pack('>bBHh', p['y']-20,
                0x80|(p['width']-1)*4+p['height']-1, tile, p['x']+x))
        return output

    tails = records[6]
    tails_pieces = []
    for p in range(2, len(tails), 6):
        y, size, tile, x = struct.unpack_from('>bBHh', tails, p)
        tails_pieces.append(struct.pack('>bBHh', y, size, tile, x+16))
    for index, items in [(8, avatar(-12)+tails_pieces), (9, avatar(0))]:
        records[index] = struct.pack('>H', len(items))+b''.join(items)

    # Reconstruct source pixels before selecting the standing head.
    canvas = {}
    for p in pieces:
        width, height = p['width']*8, p['height']*8
        for y in range(height):
            for x in range(width):
                sx = width-1-x if p['x_flip'] else x
                sy = height-1-y if p['y_flip'] else y
                tile = p['tile'] + sx//8*p['height'] + sy//8
                value = pixels[tile*32+(sy&7)*4+(sx&7)//2]
                color = value&15 if sx&1 else value>>4
                if color: canvas[(p['x']+x, p['y']+y)] = color
    top = min(y for x, y in canvas)
    def head(x, y): return canvas.get((x-8, y+top), 0)
    def tile(read, tx, ty):
        return bytes((read(tx+x, ty+y)<<4)|read(tx+x+1, ty+y)
                     for y in range(8) for x in range(0,8,2))
    head_art = b''.join(tile(head,x,y) for x in (0,8) for y in (0,8))
    font = {
        'M':(17,27,21,21,17,17,17), 'I':(7,2,2,2,2,2,7),
        'G':(14,17,16,23,17,17,14), 'H':(17,17,17,31,17,17,17),
        'T':(31,4,4,4,4,4,4), 'Y':(17,17,10,4,4,4,4)}
    def label(x,y):
        if y>=7 or x>=30: return 0
        return 5 if font['MIGHTY'[x//5]][y] & (1<<(4-x%5)) else 0
    name_art = b''.join(tile(label,x,0) for x in (0,8,16,24))
    return dict(menu_mappings=table(records), menu_art=pixels,
                hud_head=head_art, hud_name=name_art)
