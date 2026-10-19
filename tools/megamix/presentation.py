"""Build Mighty-only menu and HUD data from the verified v3 character frame.

Native Sonic/Tails portraits and palettes remain available. The life head is
sampled into S3K's 16px icon footprint; the name follows its native HUD lettering.
All generated cartridge graphics stay in the owner's private bank.
"""
import struct
from .mighty import table


def hud_name(s3k):
    from mdcodecs import nemesis_decode
    # ArtNem_SonicLifeIcon / ArtNem_MilesLifeIcon / ArtNem_TailsLifeIcon.
    # These are the supported raw lock-on ROM's own 48x16 HUD strips.
    def strip(address):
        art, _ = nemesis_decode(s3k, address)
        if len(art) != 384: raise ValueError('Unexpected native life HUD art')
        def pixel(x, y):
            v=art[(x//8*2+y//8)*32+(y%8)*4+(x%8)//2]
            return v&15 if x&1 else v>>4
        return art, pixel
    sonic, sp=strip(0x190D34)
    _, mp=strip(0x18A110)
    _, tp=strip(0x35CFFE)
    def face(read, x, width):
        return [[read(16+x+i,y) in (1,5) for i in range(width)] for y in range(1,7)]
    # M, I and T retain the actual native pixels and advances. G extends C;
    # H and Y use the same two-pixel stems and six-row capital height.
    glyphs=[face(mp,1,7),face(mp,10,2),face(sp,25,6),
            [[bool(v) for v in row] for row in ((1,1,0,0,1,1),(1,1,0,0,1,1),(1,1,1,1,1,1),(1,1,0,0,1,1),(1,1,0,0,1,1),(1,1,0,0,1,1))],
            face(tp,1,6),
            [[bool(v) for v in row] for row in ((1,1,0,0,1,1),(1,1,0,0,1,1),(0,1,1,1,1,0),(0,0,1,1,0,0),(0,0,1,1,0,0),(0,0,1,1,0,0))]]
    glyphs[2][3][3:]=[True,True,True]
    glyphs[2][4][4:]=[True,True]
    glyphs[2][5]=[False,True,True,True,True,False]
    pixels={};x=0
    for glyph in glyphs:
        for y,row in enumerate(glyph,1):
            for dx,on in enumerate(row):
                if on:pixels[x+dx,y]=5 if y in (1,2,5,6) else 1
        x+=len(glyph[0])+1
    assert x<=40
    # Native HUD letters cast their dark palette-$E shadow down/right.
    shadow={(x+1,y+1):14 for x,y in pixels}
    shadow.update(pixels)
    name=bytes((shadow.get((tx+x,y),0)<<4)|shadow.get((tx+x+1,y),0)
               for tx in range(0,40,8) for y in range(8) for x in range(0,8,2))
    # Keep the native multiplication sign; the live decimal digits retain
    # their original VRAM tiles. No player/dust/level tiles are borrowed.
    name+=sonic[7*32:8*32]
    records=[]
    for i in range(6):
        start=0xDBB6+int.from_bytes(s3k[0xDBB6+i*2:0xDBB8+i*2],'big')
        count=int.from_bytes(s3k[start:start+2],'big')
        record=s3k[start+2:start+2+count*6]
        if record[-6:]!=bytes.fromhex('400d210e0010'):
            raise ValueError('Unexpected native life name mapping')
        items=[(64,12,0x210E,16),(64,0,0x2112,48),
               (72,0,0x2114,24),(72,0,0x2113,32),(72,0,0x2115,40)]
        records.append(struct.pack('>H',count+4)+record[:-6]+
                       b''.join(struct.pack('>bBHh',*p) for p in items))
    return name,table(records)


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
    name_art,hud_mappings=hud_name(s3k)
    return dict(menu_mappings=table(records), menu_art=pixels,
                hud_head=head_art, hud_name=name_art, hud_mappings=hud_mappings)
