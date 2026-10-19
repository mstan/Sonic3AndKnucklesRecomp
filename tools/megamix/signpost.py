"""Exact v3 sign strips; PLC1/$D000 is monitors, not the signpost.

The actual late-stage loader at $621C selects PLC12/24/25 for Sonic,
Mighty and Shadow. Shadow is outside this port. A separate strip avoids sharing
monitor tiles or the native results tally's VRAM allocations.
"""
import struct
from mdcodecs import nemesis_decode
from s1_objects import convert_mappings,anim_blob


def signpost(bank,donor):
    proofs={0x621C:'0c380001ffbb6700001a0c380002ffbb67000030',
            0x6230:'70124ef9000015a6',0x623E:'7024',0x625E:'70254ef9000015a6',
            0x14486:'217c000146c20004317c06800002',
            0x53A8:'70156100ce784ef9000053c0',0x53B4:'70166100ce6c',
            0x52A8:'70190c380003fe116618701b',0x52BE:'701a0c380003fe116602701c'}
    for address,code in proofs.items():
        if donor.blob(address,len(code)//2)!=bytes.fromhex(code):
            raise ValueError(f'Unproven Megamix sign/player palette binding at {address:X}')
    maps=bytearray(convert_mappings(donor.data,0x146C2))
    if int.from_bytes(maps[:2],'big')!=10:raise ValueError('Unexpected v3 sign frames')
    for f in range(5):
        p=int.from_bytes(maps[f*2:f*2+2],'big')
        count=int.from_bytes(maps[p:p+2],'big')
        for i in range(count):
            pos=p+2+i*6;size=maps[pos+1]
            tile=int.from_bytes(maps[pos+2:pos+4],'big')&0x7FF
            if tile+(((size>>2)&3)+1)*((size&3)+1)>58:raise ValueError('Sign mapping exceeds source strip')
            maps[pos+1]|=0xC0
    result=struct.pack('>4sII',b'MGS1',bank.add(maps),bank.add(anim_blob(donor.data,0x146A6)))
    for plc,normal,water,mb3 in ((0x12,3,0xF,0x10),(0x24,0x15,0x19,0x1B)):
        sources=[src for src,dst in donor.plc(plc) if dst==0xD000]
        if len(sources)!=1:raise ValueError('Ambiguous v3 sign PLC')
        art,_=nemesis_decode(donor.data,sources[0])
        if len(art)!=58*32:raise ValueError('Unexpected v3 sign strip length')
        result+=struct.pack('>5I',bank.add(art),len(art),
            *(bank.add(donor.palette(p)[1][:32]) for p in (normal,water,mb3)))
    return bank.add(result)
