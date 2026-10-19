"""Read the owner's exact Mega Drive Megamix v3 ROM; never use retail offsets.

Bindings are proven from the v3 68000 load instructions listed in BINDINGS.
This module reads data for conversion/auditing. It does not claim that donor
object routines, stage events or Mighty are implemented in the S3K runtime.
"""
from collections import Counter
import hashlib
from pathlib import Path
import struct
import zipfile
import zlib

SIZE = 1_283_224
CRC32 = 0x66ECB4F4
SHA256 = '85d9e9ba79f0035d5854618666ff198871abe1931558496e6563b1cd90e5cdbe'
TABLES = dict(headers=0x265F0, plc=0x266D0, palettes=0x22D0,
              layouts=0xDA9D0, objects=0xE3522, sizes=0x93A2,
              starts=0x9702, loops=0x97E2, collision_index=0x6072,
              angles=0xD3264, heights=0xD3364, rotated_heights=0xD4364)
BINDINGS = {
    0xAF36: '45f9000265f0',  # Megamix header branch; Original mode uses 26660
    0xB030: '43f9000da9d0', # Megamix FG/BG layout offsets
    0x131D8: '41f9000e352220700000', # absolute long object pointers, not S1 relatives
    0x9354: '41fb004c',    # PC-indexed sizes at 93A2
    0x9688: '43fb0078',    # PC-indexed starts at 9702
    0x96F2: '41f9000097e2',
    0x1570: '43f9000266d0',
    0x222C: '43f9000022d0',
    0x606A: '21fb0006f796', # PC-indexed collision pointers at 6072
    0x1BE20: '45f9000d3264',
    0x1BE4E: '45f9000d3364',
    0x1BF8E: '45f9000d4364',
    0xAF80: '41f900068266', # MM4 (= Metallic Base 3) has different geometry
    0xAF90: '41f900068590',
    0x1876E: '217c0002a9d60004', # common Sonic/Mighty mappings
    0x1AC16: '45f90002c3d2',     # common Sonic/Mighty dynamic PLCs
    0x1AC6E: '0681000a9364',     # FFBB=1 selects Mighty's raw tile bank
    0x1A7F4: '43f90001aa1c0c380001ffbb670c4a38fe19670643f90001ab5e',
    0x1ABFC: '70001028001ab038', # load-gfx entry after both animation data sets
    0x19950: '0c380041f602',     # Up+A stores; ordinary A selects a stored shield
    0x1999A: '0c780005fe206d98', # ordinary shield swap requires five rings
    0x199A2: '04780005fe20',     # and actually spends them
    0x19AAA: '2038ffb0e1881038fe2c21c0ffb0', # normal shield storage shifts four byte slots
    0x1A746: '0c380003fe1067084a38fe106600009e', # Sonic_Loops only zones 0/3
}
MIGHTY = dict(selector=1, mappings=0x2A9D6, dplc=0x2C3D2,
              art=0xA9364, palette=0x15, animation=0x1AA1C)
ZONES = ('Sunny Shores', 'Misty Maze', 'Dark Fortress', 'Starry Night',
         'City Outskirts', 'Metallic Base')
ORDER = tuple((z,a) for z in (0,2,4,1,3) for a in range(3)) + ((5,0),(5,1),(1,3),(5,2))


def load_rom(path):
    path = Path(path)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            files = [i for i in archive.infolist() if not i.is_dir()
                     and i.filename.lower().endswith(('.bin','.md','.gen'))]
            if len(files) != 1:
                raise ValueError('Expected exactly one raw cartridge in the ZIP')
            if files[0].file_size != SIZE:
                raise ValueError('Expected the exact 1,283,224-byte Megamix v3 cartridge')
            data = archive.read(files[0])
    else:
        if path.stat().st_size != SIZE:
            raise ValueError('Expected the exact 1,283,224-byte Megamix v3 cartridge')
        data = path.read_bytes()
    if zlib.crc32(data) != CRC32 or hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('Expected Megamix v3 CRC32 66ECB4F4 / SHA256 85d9e9ba...')
    return data


class MegamixV3:
    def __init__(self, data):
        if len(data) != SIZE or hashlib.sha256(data).hexdigest() != SHA256:
            raise ValueError('Unsupported Megamix ROM identity')
        self.data = data
        for address, code in BINDINGS.items():
            if data[address:address+len(code)//2] != bytes.fromhex(code):
                raise ValueError(f'Unproven v3 data binding at ${address:06X}')

    def blob(self, address, size):
        if address < 0 or size < 0 or address + size > len(self.data):
            raise ValueError(f'ROM range outside cartridge: ${address:X}+${size:X}')
        return self.data[address:address+size]

    def word(self, address): return struct.unpack('>H',self.blob(address,2))[0]
    def long(self, address): return struct.unpack('>I',self.blob(address,4))[0]

    def header(self, zone, act=0):
        p = TABLES['headers'] + zone*16
        header = dict(address=p, plc1=self.data[p], plc2=self.data[p+4],
                      art=self.long(p)&0xFFFFFF, blocks=self.long(p+4)&0xFFFFFF,
                      chunks=self.long(p+8)&0xFFFFFF, palette=self.data[p+15])
        if (zone,act)==(1,3):
            header.update(blocks=0x68266,chunks=0x68590,plc2=0x29,palette=0x0C)
        elif zone==5 and act in (1,2):
            header['palette']=0x0E
        return header

    def plc(self, index):
        p=TABLES['plc']+self.word(TABLES['plc']+index*2)
        n=self.word(p)
        if n==0xFFFF:return []
        if n>128:raise ValueError(f'Invalid PLC ${index:X}')
        return [(self.long(p+2+i*6),self.word(p+6+i*6)) for i in range(n+1)]

    def palette(self,index):
        p=TABLES['palettes']+index*8
        return self.word(p+4),self.blob(self.long(p),(self.word(p+6)+1)*4)

    def layout(self,zone,act,plane=0):
        table=TABLES['layouts'];p=table+self.word(table+(zone*4+act)*6+plane*2)
        w,h=self.data[p]+1,self.data[p+1]+1
        if w>64 or h>8:raise ValueError(f'Invalid layout at ${p:X}')
        data=self.blob(p+2,w*h)
        return p,[list(data[y*w:(y+1)*w]) for y in range(h)]

    def objects(self,zone,act):
        p=self.long(TABLES['objects']+(zone*4+act)*4)
        items=[];previous=-1
        for _ in range(4096):
            if self.word(p)==0xFFFF:return items
            x,y,code,sub=struct.unpack('>HHBB',self.blob(p,6))
            if x<previous:raise ValueError(f'Unsorted placement at ${p:X}')
            # ID zero is an empty placement, not an absent-handler error.
            items.append(dict(address=p,x=x,y=y&0xFFF,flags=y&0xF000,
                              id=code&0x7F,remember=bool(code&0x80),subtype=sub))
            previous=x;p+=6
        raise ValueError('Unterminated object layout')

    def stage(self,zone,act):
        h=self.header(zone,act);index=zone*4+act
        # Each boundary record begins with an unused word, then X/Y limits.
        limits=struct.unpack('>6H',self.blob(TABLES['sizes']+index*12,12))
        start=struct.unpack('>HH',self.blob(TABLES['starts']+index*4,4))
        name=ZONES[zone]+f' {act+1}'
        if (zone,act)==(1,3):name='Metallic Base 3'
        if (zone,act)==(5,2):name='The Final Fight'
        objects=self.objects(zone,act)
        fg,front=self.layout(zone,act);bg,back=self.layout(zone,act,1)
        return dict(source_stage=f'{zone:02x}{act:02x}',name=name,header=h,
                    bounds=list(limits[1:5]),look_shift=limits[5],start=list(start),
                    foreground=dict(address=fg,width=len(front[0]),height=len(front)),
                    background=dict(address=bg,width=len(back[0]),height=len(back)),
                    collision_index=self.long(TABLES['collision_index']+zone*4),
                    placements=len(objects),empty_placements=sum(o['id']==0 for o in objects),
                    object_ids={f'{k:02X}':v for k,v in sorted(Counter(o['id'] for o in objects).items())})

    def mighty_frames(self):
        maps,dplc=MIGHTY['mappings'],MIGHTY['dplc']
        frames=self.word(maps)//2
        if self.word(dplc)//2!=frames:
            raise ValueError('Mighty map/DPLC frame counts disagree')
        result=[]
        for frame in range(frames):
            p=maps+self.word(maps+frame*2)
            n=self.blob(p,1)[0];pieces=[]
            for i in range(n):
                y,size,tile,x=struct.unpack('>bBHb',self.blob(p+1+i*5,5))
                pieces.append(dict(x=x,y=y,width=((size>>2)&3)+1,height=(size&3)+1,
                                   tile=tile&0x7FF,x_flip=bool(tile&0x800),y_flip=bool(tile&0x1000),
                                   attributes=tile&0xE000))
            p=dplc+self.word(dplc+frame*2);n=self.blob(p,1)[0]
            tiles=bytearray()
            for i in range(n):
                entry=self.word(p+1+i*2)
                tiles+=self.blob(MIGHTY['art']+(entry&0xFFF)*32,((entry>>12)+1)*32)
            if any(o['tile']+o['width']*o['height']>len(tiles)//32 for o in pieces):
                raise ValueError(f'Mighty frame {frame} references missing dynamic tiles')
            result.append((pieces,bytes(tiles)))
        return result

    def mighty_animations(self):
        base=MIGHTY['animation'];limit=self.word(base);offsets=[]
        # The idle script precedes walk data. The first offset alone would
        # misclassify six script bytes as three additional table entries.
        while len(offsets)*2<limit:
            offset=self.word(base+len(offsets)*2)
            if offset<2 or offset>=0x1ABFC-base:raise ValueError('Invalid Mighty animation pointer')
            offsets.append(offset);limit=min(limit,offset)
        if len(offsets)*2!=limit:raise ValueError('Invalid Mighty animation table boundary')
        return offsets,self.blob(base,0x1ABFC-base)
