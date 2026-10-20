"""Convert actual v3 Mighty sprite data into S3K mapping/DPLC formats.

This preserves source graphics and animation bytecode. The bytecode still
requires a v3 animation controller; S3K Sonic's frame numbers are different.
No character selector, abilities or gameplay port is implied by this tool.
"""
import hashlib
import struct

from .v3_rom import MIGHTY

def table(records):
    result=bytearray(len(records)*2);seen={}
    for i,record in enumerate(records):
        if record not in seen:
            seen[record]=len(result);result+=record
        if seen[record]>65535:raise ValueError('Native sprite offset exceeds 16 bits')
        struct.pack_into('>H',result,i*2,seen[record])
    return bytes(result)

def native_frame(mappings,dplc,art,index):
    word=lambda data,p:int.from_bytes(data[p:p+2],'big')
    p=word(mappings,index*2);pieces=[]
    for i in range(word(mappings,p)):
        y,size,tile,x=struct.unpack_from('>bBHh',mappings,p+2+i*6)
        pieces.append(dict(x=x,y=y,width=(size>>2)+1,height=(size&3)+1,
            tile=tile&0x7FF,x_flip=bool(tile&0x800),y_flip=bool(tile&0x1000),attributes=tile&0xE000))
    p=word(dplc,index*2);tiles=bytearray()
    for i in range(word(dplc,p)):
        entry=word(dplc,p+2+i*2);offset=(entry&0xFFF)*32;size=((entry>>12)+1)*32
        if offset+size>len(art):raise ValueError('Native Mighty DPLC points outside its art')
        tiles+=art[offset:offset+size]
    return pieces,bytes(tiles)

def convert_mighty(rom):
    return convert_character(rom, MIGHTY)


def convert_character(rom, character):
    frames=rom.character_frames(character);maps=[];plcs=[];art=bytearray();seen={}
    for pieces,tiles in frames:
        mapping=bytearray(struct.pack('>H',len(pieces)))
        for p in pieces:
            tile=p['tile'] | (0x800 if p['x_flip'] else 0) | (0x1000 if p['y_flip'] else 0) | p['attributes']
            size=((p['width']-1)<<2)|(p['height']-1)
            mapping+=struct.pack('>bBHh',p['y'],size,tile,p['x'])
        maps.append(bytes(mapping))
        if tiles not in seen:
            seen[tiles]=len(art)//32;art+=tiles
        start=seen[tiles];count=len(tiles)//32;entries=[]
        for offset in range(0,count,16):
            n=min(count-offset,16)
            if start+offset>=4096:raise ValueError('Mighty dynamic art exceeds the native DPLC index')
            entries.append(((n-1)<<12)|(start+offset))
        plcs.append(struct.pack('>H'+'H'*len(entries),len(entries),*entries))
    mappings=table(maps);dplc=table(plcs);art=bytes(art)
    # Decode the final native-format bytes, including every mapping piece,
    # flip, signed coordinate, palette/priority bit and dynamic tile run.
    for index,source in enumerate(frames):
        if native_frame(mappings,dplc,art,index)!=source:
            raise ValueError(f'Native Mighty frame {index:02X} differs from the v3 source')
    animations,animation_source=rom.mighty_animations()
    result=dict(mappings=mappings,dplc=dplc,art=art,
                palette=rom.palette(character['palette'])[1],animation_source=animation_source)
    report=dict(frames=len(frames),animation_entries=len(animations),native_art_tiles=len(art)//32,
        source_frame_art_sha256=hashlib.sha256(b''.join(t for _,t in frames)).hexdigest(),
        payloads={k:dict(bytes=len(v),sha256=hashlib.sha256(v).hexdigest()) for k,v in result.items()},
        checks='all native mapping/DPLC frames reconstruct exact source pieces and tiles',
        status='converted sprite data; runtime animation, abilities and UI pending',
        unused_shared_frames='preserved; not all shared Sonic/Mighty poses are active Mighty animations')
    return result,report
