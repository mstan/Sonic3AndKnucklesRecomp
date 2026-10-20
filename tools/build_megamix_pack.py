#!/usr/bin/env python3
"""Extend a verified private S1/S2 stage bank with exact Megamix v3 data.

Build products contain donor data and must remain private. The input bank,
ROM and ZIP are never modified. Runtime enrollment is developed alongside
the converter; source-data conversion does not establish gameplay acceptance.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/trilogy'))
import build_bank as bb
from mdcodecs import nemesis_decode,kosinski_encode,kosm_encode
from tilepack import Packer
from megamix.v3_rom import MegamixV3,load_rom,ORDER,CRC32,SHADOW
from megamix.geometry import convert_geometry
from megamix.mighty import convert_mighty,convert_character
from megamix.presentation import presentation
from megamix.signpost import signpost
from megamix.objects import MegamixObjects,ZONE_NAMES
from build_stage_pack import verify
from s1_music import build_music
from s1_objects import asset_mappings,_piece_ranges,_retile,ASSET_COUNT,ASSET_REC

NAMES={0x34:'SUNNY SHORES',0x35:'MISTY MAZE',0x36:'DARK FORTRESS',
       0x37:'STARRY NIGHT',0x38:'CITY OUTSKIRTS',0x39:'METALLIC BASE',0x3A:'FINAL FIGHT'}

def shadow_descriptor(bank,donor,s3k):
    # Selectors from the exact v3 cartridge, never the 2005 disassembly or
    # a later CD build. Fail before publishing if any binding differs.
    proofs={0x18776:'0c380002ffbb6608217c0002b6a40004',
            0x1AC0E:'0c380002ffbb670845f90002c3d2',
            0x1AC1E:'45f90002ca1c',0x1AC7A:'0681000be2e4',
            0x53B4:'70166100ce6c',0x52BE:'701a0c380003fe116602701c',
            0x1A7F4:'43f90001aa1c0c380001ffbb670c4a38fe19670643f90001ab5e'}
    for p,code in proofs.items():
        if donor.blob(p,len(code)//2)!=bytes.fromhex(code):raise ValueError(f'Unproven Shadow binding ${p:X}')
    sprite,report=convert_character(donor,SHADOW)
    ui=presentation(donor,s3k,SHADOW)
    palettes=[donor.palette(p)[1] for p in (0x16,0x1A,0x1C)]
    record=struct.pack('>4s7I2H',b'MSW1',bank.add(sprite['mappings']),bank.add(sprite['dplc']),
        bank.add(sprite['art']),bank.add(palettes[0]),bank.add(sprite['animation_source']),
        len(sprite['art']),len(sprite['animation_source']),report['frames'],report['animation_entries'])
    record+=struct.pack('>4I',bank.add(palettes[1]),bank.add(palettes[2]),
        bank.add(donor.blob(0x11CAFC,128)),bank.add(donor.blob(0x11CC7C,128)))
    record+=struct.pack('>3I',*(bank.add(struct.pack('>IHH',bank.add(pal),0xFC00,7)) for pal in palettes))
    record+=struct.pack('>6I',bank.add(ui['menu_mappings']),bank.add(ui['menu_art']),len(ui['menu_art']),
        bank.add(ui['hud_head']),bank.add(ui['hud_name']),bank.add(ui['hud_mappings']))
    return bank.add(record)

def level_vram(donor,zone,act):
    header=donor.header(zone,act);vram=bytearray(65536)
    # $5114 overlays MB3's first 454 tiles using PLC $28. Its ordinary LZ
    # art remains the backing for level-art objects above that range.
    plcs=[header['plc1']]+([0x28] if (zone,act)==(1,3) else [])+[header['plc2']]
    for plc in plcs:
        for address,dest in donor.plc(plc):
            art,_=nemesis_decode(donor.data,address)
            vram[dest:dest+len(art)]=art[:65536-dest]
    return vram


def upgrade_mb3_art(bank,source,donor):
    """Repack only MB3 terrain and its native level-art door. No S1 input.

    The existing layout/chunk IDs, collisions, placements, music and imported
    sprite atlas are retained. Never replace tile pixels in place: old pixel
    deduplication can merge tiles that the corrected graphics distinguish.
    """
    entry=0x40+((0x35-0x20)*4+3)*0x40
    old_info=bb.be32(source,entry+0x3C)-bb.BANK_BASE
    converted=convert_geometry(donor,1,3)
    for offset,data_key in ((4,'layout16'),(12,'chunks')):
        pointer=bb.be32(source,old_info+offset)-bb.BANK_BASE
        size=bb.be32(source,old_info+offset+4)
        if source[pointer:pointer+size]!=converted[data_key]:
            raise ValueError('MB3 geometry differs; presentation upgrade cannot change collision/layout data')
    symbols={'Map_Stomp':0x1D38C,'ArtTile_LZ_Block_2':0x1F0}
    maps=asset_mappings(donor.data,symbols,'STOMP_SBZ3','Map_Stomp','ArtTile_LZ_Block_2',2)
    tables={0:bb.L0_MAP,1:bb.L1_PERM,2:list(range(16)),3:list(range(16))}
    pack=Packer(level_vram(donor,1,3),tables,bb.S3K_LEVEL_TILE_LIMIT,[])
    for start,count in _piece_ranges(maps):pack.place_group(0x1F0+start,count,2)
    blocks=bytearray(converted['blocks']);pack_blocks(pack,blocks,set())
    art1,art2=bb.level_art_modules(bank,pack.art({}));blockptr=bank.add(kosinski_encode(blocks))
    old_llb=bb.be32(source,entry)-bb.BANK_BASE
    llb=bytearray(source[old_llb:old_llb+24]);struct.pack_into('>II',llb,0,art1,art2)
    for offset in (8,12):struct.pack_into('>I',llb,offset,(bb.be32(llb,offset)&0xFF000000)|blockptr)
    struct.pack_into('>I',bank.data,entry,bank.add(llb))
    native_maps=_retile(maps,lambda tile:pack.slot[(0x1F0+tile,2)])
    assets=bb.be32(source,entry+0x38)-bb.BANK_BASE
    records=bytearray(source[assets:assets+ASSET_COUNT*ASSET_REC])
    struct.pack_into('>I',records,92*ASSET_REC,bank.add(native_maps))
    struct.pack_into('>I',bank.data,entry+0x38,bank.add(records))

def flip_tile(pixels,flags):
    rows=[pixels[i:i+4] for i in range(0,32,4)]
    if flags&1:rows=[bytes(((v&15)<<4)|(v>>4) for v in reversed(row)) for row in rows]
    if flags&2:rows.reverse()
    return b''.join(rows)

def pack_blocks(pack,blocks,pinned):
    # Static terrain may share a tile with its exact mirrored form. Preserve
    # each cell's orientation by composing the mapping's hardware flip bits.
    # Animated slots are excluded, since their future pixels will change.
    variants={}
    for t in sorted(pack.taken-pinned):
        pixels=bytes(pack.image[t*32:(t+1)*32])
        for flags in range(4):variants.setdefault(flip_tile(pixels,flags),(t,flags))
    flips={}
    for p in range(0,len(blocks),2):
        tile=bb.be16(blocks,p);key=(tile&0x7FF,(tile>>13)&3)
        pixels=pack._px(*key)
        if key not in pack.slot:
            if pixels in variants:
                t,flags=variants[pixels];pack.slot[key]=t;flips[key]=flags
            else:
                pack.place_single(*key);t=pack.slot[key]
                for flags in range(4):variants.setdefault(flip_tile(pixels,flags),(t,flags))
        t=pack.slot[key];flags=flips.get(key,0)
        # Verify the packed tile itself, independently of the index table.
        stored=bytes(pack.image[t*32:(t+1)*32]) if t not in pinned else pixels
        if flip_tile(stored,flags)!=pixels:raise ValueError('Packed terrain tile changed source pixels')
        struct.pack_into('>H',blocks,p,((tile&0xF800)^(flags<<11))|t)

def extend(source,donor,retail,s3k):
    if len(source)<0x204C or source[-12:-8]!=b'SPK1':raise ValueError('Expected the complete original S1/S2 stage bank')
    size,crc=struct.unpack_from('>II',source,len(source)-8)
    if size!=len(source)-12 or zlib.crc32(source[:-12])!=crc:raise ValueError('Damaged S1/S2 stage bank')
    bank=bb.Bank();bank.data=bytearray(source[:-12])
    # Reuse the audited core campaign's title archives byte for byte.
    old_titles=bb.be32(source,0x18)-bb.BANK_BASE
    for i in range(32):
        pointer=bb.be32(source,old_titles+i*4)
        if pointer:bank.dedupe[kosm_encode(bb._kosm_decode(source,pointer-bb.BANK_BASE))]=pointer
    ptr=bb.be32(source,8)-bb.BANK_BASE;count=bb.be16(source,12)
    bank.pals=[struct.unpack_from('>IHH',source,ptr+i*8) for i in range(count)]
    ptr=bb.be32(source,16)-bb.BANK_BASE;count=bb.be16(source,20)
    bank.plcs=[bb.be32(source,ptr+i*4) for i in range(count)]
    tables={0:bb.L0_MAP,1:bb.L1_PERM,2:list(range(16)),3:list(range(16))}
    objects=MegamixObjects(donor,retail,tables,bb.S3K_LEVEL_TILE_LIMIT)
    object_table=bank.add(objects.pointer_table(bb.S3K_ADDR))
    blob_ptr=bb.be32(source,0x20)-bb.BANK_BASE
    blobs=[bb.be32(source,blob_ptr+i*4) for i in range(54)]
    extptr=blobs[39]-bb.BANK_BASE;extpal=bytearray(source[extptr:extptr+128*64])
    animal_ptr=blobs[1]-bb.BANK_BASE;animals=bytearray(source[animal_ptr:animal_ptr+128])
    plc_ptr=blobs[2]-bb.BANK_BASE;animal_plcs=bytearray(source[plc_ptr:plc_ptr+128])
    stages=[];atlas_descriptors={}
    for z,a in ORDER:
        native_zone=0x34+z;stage=donor.stage(z,a);header=stage['header'];name=ZONE_NAMES[z]
        converted=convert_geometry(donor,z,a);blocks=bytearray(converted['blocks'])
        vram=level_vram(donor,z,a)
        need=objects.act_assets(z,a)
        pinned=bb.S1_ANIMATED.get(name,[])
        pack=Packer(vram,tables,bb.S3K_LEVEL_TILE_LIMIT,pinned)
        for first,count,line in objects.level_asset_groups(need):pack.place_group(first,count,line)
        # Group placement reserves contiguous sprite pieces. Terrain may
        # share those exact pixels without allocating another tile copy.
        pinned_tiles={t for first,n in pinned for t in range(first,first+n)}
        for t in sorted(pack.taken-pinned_tiles):
            pack.bypix.setdefault(bytes(pack.image[t*32:(t+1)*32]),t)
        pack_blocks(pack,blocks,pinned_tiles)
        pin_data={tile:bytes(vram[tile*32:(tile+1)*32]) for first,n in pinned for tile in range(first,first+n)}
        art1,art2=bb.level_art_modules(bank,pack.art(pin_data))
        dest,palette=donor.palette(header['palette']);palette=bytearray(palette)
        if dest==0xFB20:
            original=bytes(palette[:32])
            for i in range(16):palette[bb.L1_PERM[i]*2:bb.L1_PERM[i]*2+2]=original[i*2:i*2+2]
        pal_id=bank.add_palette(palette,0xFC00+dest-0xFB00)
        blockptr=bank.add(kosinski_encode(blocks))
        chunkptr=bank.add(converted['chunks'])
        # The expanded source is authoritative. Native layout RAM supplies
        # dimensions and guard-row pointers to existing engine callers.
        layout=bytearray(0x1000);fw,bw,fh,bh=struct.unpack_from('>4H',converted['layout16'],4)
        struct.pack_into('>4H',layout,0,fw,bw,fh,bh)
        for i in range(64):struct.pack_into('>H',layout,8+i*2,0x8088)
        layoutptr=bank.add(layout)
        native_chunks=bank.add(kosinski_encode(converted['chunks'][:256*128]))
        llb=bank.add(struct.pack('>6I',art1,art2,(pal_id<<24)|blockptr,
                               (pal_id<<24)|blockptr,native_chunks,native_chunks))
        fg,rows=donor.layout(z,a);raw=bytearray(512)
        for y,row in enumerate(rows):raw[y*64:y*64+len(row)]=bytes(row)
        water_pal=0;water_y=0
        if z==1:
            _,wet=donor.palette(0xD if a==3 else 0xB);wet=bytearray(wet)
            player=0xA872C+0x31*8;player=bb.be32(s3k,player)
            wet[:32]=s3k[player:player+32]
            original=bytes(wet[32:64])
            for i in range(16):wet[32+bb.L1_PERM[i]*2:34+bb.L1_PERM[i]*2]=original[i*2:i*2+2]
            water_pal=bank.add_palette(wet,0xFC00)
            water_y=donor.word(0x5872+a*2)
        raw+=donor.blob(0x97E2+z*4,4)+struct.pack('>BBH',water_pal,0,water_y)+bytes(8)
        rawptr=bank.add(raw)
        info=bytearray(struct.pack('>4s4I2H',b'MGI1',bank.add(converted['layout16']),len(converted['layout16']),
                         chunkptr,len(converted['chunks']),converted['native_blocks'],0))
        info+=struct.pack('>BBHI4sHH',z,a,0,rawptr,donor.blob(0x97E2+z*4,4),0,stage['look_shift'])
        free=set(range(pack.end(),bb.S3K_LEVEL_TILE_LIMIT))|(set(range(1,bb.S3K_LEVEL_TILE_LIMIT))-pack.taken)
        assets=objects.build_assets(bank,name,free,need,pack.slot,a)
        info+=bytes(12)+struct.pack('>I',bank.add(converted['swaps']))
        ob,rb=objects.convert_objects(z,a)
        info+=struct.pack('>II',bank.add(objects.monitor_tags),0)
        if z==1:
            wet[:32]=donor.palette(0x1B if a==3 else 0x19)[1]
            struct.pack_into('>I',info,60,bank.add(struct.pack('>IHH',bank.add(wet),0xFC00,len(wet)//4-1)))
        info_ptr=bank.add(info)
        atlas_descriptors.setdefault(name,[]).append(info_ptr)
        op=bank.add(ob,dedupe=False);rp=bank.add(rb,dedupe=False)
        e=bank.entry(native_zone,a);sx,sy=stage['start'];left,right,top,bottom=stage['bounds']
        struct.pack_into('>III8HII',bank.data,e,llb,layoutptr,bank.add(converted['solids']),
                         left,right,top,bottom,sx,sy,sx,sy,op+6,rp)
        bank.data[e+0x24]=0x8D if (z,a)==(5,2) else 0x81+z;bank.data[e+0x25]=3
        struct.pack_into('>6I',bank.data,e+0x28,bank.add(converted['angles']),bank.add(converted['heights']),
                         bank.add(converted['rotated']),object_table,assets,info_ptr)
        index=(native_zone-0x20)*4+a
        wet_actor=donor.palette(0x10 if a==3 else 0xF)[1] if z==1 else donor.palette(3)[1]
        extpal[index*64:(index+1)*64]=donor.palette(3)[1]+wet_actor
        animals[native_zone*2:native_zone*2+2]=animals[(0x20+z)*2:(0x20+z)*2+2]
        animal_plcs[(native_zone-0x20)*4:(native_zone-0x20+1)*4]=animal_plcs[z*4:(z+1)*4]
        stages.append(dict(stage=f'{native_zone:02x}{a:02x}',name=stage['name'],source=stage['source_stage'],
                           chunks=converted['native_chunks'],blocks=converted['native_blocks']))
        print(stage['name'],hex(native_zone*256+a),'bank bytes',len(bank.data),flush=True)
    for name,pointers in atlas_descriptors.items():
        state=objects.sprite_states[name]
        count=max((i for i in range(2048) if i not in state['free']),default=0)+1
        pixels=bank.add(state['pixels'][:count*32]);lines=bank.add(state['lines'][:count])
        for info in pointers:struct.pack_into('>3I',bank.data,info-bb.BANK_BASE+40,pixels,count*32,lines)
    # v3's driver selects its main song index through $F2D06. The original
    # S1 mode uses a different index; a plausible header scan is insufficient.
    if donor.blob(0xF35DE,12)!=bytes.fromhex('2879000f2d06e54f28747000'):
        raise ValueError('Unproven Megamix music index binding')
    index=donor.long(0xF2D06)
    envs=[donor.long(0xF2D22+i*4) for i in range(9)]
    if donor.blob(0xF37CE,4)!=bytes.fromhex('040700e5') or donor.blob(0xF37D8,6)!=bytes.fromhex('2879000f2d0e'):
        raise ValueError('Unproven Megamix alternate boss song binding')
    extra=donor.long(0xF2D0E)
    songs=[donor.long(index+i*4) for i in range(19)]+[donor.long(extra+i*4) for i in range(4)]
    music,converted_songs=build_music(donor.data,bank,index,envs,starts=songs)
    music_ptr=bank.add(music)
    mighty,report=convert_mighty(donor)
    ui=presentation(donor,s3k)
    mighty_ptr=bank.add(struct.pack('>4s7I2H',b'MDY3',bank.add(mighty['mappings']),bank.add(mighty['dplc']),
        bank.add(mighty['art']),bank.add(mighty['palette']),bank.add(mighty['animation_source']),
        len(mighty['art']),len(mighty['animation_source']),report['frames'],report['animation_entries'])+
        struct.pack('>4I',bank.add(donor.palette(0x19)[1]),bank.add(donor.palette(0x1B)[1]),
                     bank.add(donor.blob(0x11CAFC,128)),bank.add(donor.blob(0x11CC7C,128)))+
        struct.pack('>3I',*(bank.add(struct.pack('>IHH',bank.add(pal),dest,7)) for pal,dest in
                          [(mighty['palette'],0xFC00),(donor.palette(0x19)[1],0xFC00),(donor.palette(0x1B)[1],0xFC00)]))+
        struct.pack('>6I',bank.add(ui['menu_mappings']),bank.add(ui['menu_art']),len(ui['menu_art']),
                    bank.add(ui['hud_head']),bank.add(ui['hud_name']),bank.add(ui['hud_mappings'])))
    order=bank.add(b''.join(struct.pack('>H',int(s['stage'],16)) for s in stages))
    # v3 keeps Original-mode cycles beside the Megamix cycles. Select the
    # actual Megamix branch, including the noncontiguous LZ conveyor tables.
    for pc,code in [(0x1986,'41f900001c6e'),(0x1A4A,'41f900001cee'),
                    (0x1A5C,'41f900001d00'),(0x1B82,'45f900001dce')]:
        if donor.blob(pc,len(code)//2)!=bytes.fromhex(code):raise ValueError('Unproven Megamix palette-cycle binding')
    overrides=[0]*54
    overrides[9]=bank.add(donor.blob(0x1C6E,32))
    overrides[27]=bank.add(donor.blob(0x1CAE,32)+donor.blob(0x1CEE,18)+
                           donor.blob(0x1D00,18)+donor.blob(0x1D12,32))
    sbz=bytearray(donor.blob(0x1DCE,0x112))
    for table in (0,0x38):
        for i in range(bb.be16(sbz,table)+1):
            pos=table+2+i*6+2
            struct.pack_into('>H',sbz,pos,bb.be16(sbz,pos)-0x130)
    overrides[31]=bank.add(sbz)
    shadow=shadow_descriptor(bank,donor,s3k)
    descriptor=bank.add(struct.pack('>4sIHHIIIIIIII',b'MDI1',CRC32,len(stages),5,mighty_ptr,music_ptr,order,
                        bank.add(b''.join(struct.pack('>I',p) for p in overrides)),
                        bank.add(donor.blob(0x9FF2,256)),bank.add(donor.blob(0x1AE1C,256)),signpost(bank,donor,True),shadow))
    struct.pack_into('>I',bank.data,0x24,descriptor)
    blobs[39]=bank.add(extpal);blobs[1]=bank.add(animals);blobs[2]=bank.add(animal_plcs)
    struct.pack_into('>I',bank.data,0x20,bank.add(b''.join(struct.pack('>I',p) for p in blobs)))
    bb.ZONE_NAMES.update(NAMES);bb.build_title_cards(bank,s3k)
    data=bank.finish()
    return data+struct.pack('>4sII',b'SPK2',len(data),zlib.crc32(data)),stages,report

def upgrade_presentation(source,donor,s3k):
    """Preserve an existing verified SPK2 payload and add independent UI data."""
    if len(source)<0x204C or source[-12:-8]!=b'SPK2':raise ValueError('Expected a complete SPK2 bank')
    size,crc=struct.unpack_from('>II',source,len(source)-8)
    if size!=len(source)-12 or zlib.crc32(source[:-12])!=crc:raise ValueError('Damaged SPK2 bank')
    bank=bb.Bank();bank.data=bytearray(source[:-12])
    # Old conversions accidentally selected the adjacent Original-mode
    # starting water table. Copy only these versioned act records.
    for act in range(4):
        entry=0x40+((0x35-0x20)*4+act)*0x40
        old_info=bb.be32(source,entry+0x3C)-bb.BANK_BASE
        info_record=bytearray(source[old_info:old_info+64])
        raw=bb.be32(info_record,28)-bb.BANK_BASE
        raw_record=bytearray(source[raw:raw+528])
        if len(info_record)!=64 or info_record[:4]!=b'MGI1' or len(raw_record)!=528:
            raise ValueError('Incomplete Megamix water record')
        struct.pack_into('>H',raw_record,0x206,donor.word(0x5872+act*2))
        struct.pack_into('>I',info_record,28,bank.add(raw_record))
        struct.pack_into('>I',bank.data,entry+0x3C,bank.add(info_record))
    upgrade_mb3_art(bank,source,donor)
    info=bb.be32(source,0x24)-bb.BANK_BASE
    if source[info:info+4]!=b'MDI1' or bb.be32(source,info+4)!=CRC32:raise ValueError('Unsupported donor bank')
    mighty=bb.be32(source,info+12)-bb.BANK_BASE
    if source[mighty:mighty+4] not in (b'MDY1',b'MDY2',b'MDY3'):raise ValueError('Missing Mighty descriptor')
    ui=presentation(donor,s3k)
    record=b'MDY3'+source[mighty+4:mighty+64]+struct.pack('>6I',
        bank.add(ui['menu_mappings']),bank.add(ui['menu_art']),len(ui['menu_art']),
        bank.add(ui['hud_head']),bank.add(ui['hud_name']),bank.add(ui['hud_mappings']))
    new=bank.add(record)
    metadata=bytearray(source[info:info+28])
    struct.pack_into('>H',metadata,10,5)
    struct.pack_into('>I',metadata,12,new)
    metadata+=struct.pack('>IIII',bank.add(donor.blob(0x9FF2,256)),bank.add(donor.blob(0x1AE1C,256)),
        signpost(bank,donor,True),shadow_descriptor(bank,donor,s3k))
    struct.pack_into('>I',bank.data,0x24,bank.add(metadata))
    data=bytes(bank.data)
    return data+struct.pack('>4sII',b'SPK2',len(data),zlib.crc32(data))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('base-bank','s3k','megamix','out'):ap.add_argument('--'+key,required=True,type=Path)
    ap.add_argument('--s1',type=Path)
    ap.add_argument('--upgrade-presentation',action='store_true',help='Upgrade existing SPK2 character/background resources without reconverting stages')
    args=ap.parse_args()
    inputs=[p for p in (args.base_bank,args.s1,args.s3k,args.megamix) if p]
    if args.out.resolve() in {p.resolve() for p in inputs}:raise ValueError('Output must differ from every input')
    donor=MegamixV3(load_rom(args.megamix));s3k=verify(args.s3k,'s3k')
    if args.upgrade_presentation:
        data=upgrade_presentation(args.base_bank.read_bytes(),donor,s3k)
        stages=[];mighty=dict(status='existing verified stages retained; independent character UI and source background resources added')
    else:
        if not args.s1:ap.error('--s1 is required for a full stage conversion')
        data,stages,mighty=extend(args.base_bank.read_bytes(),donor,verify(args.s1,'s1'),s3k)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=args.out.parent,delete=False) as f:
        temporary=Path(f.name);f.write(data)
    os.replace(temporary,args.out)
    args.out.with_suffix('.manifest.json').write_text(json.dumps(dict(stages=stages,mighty=mighty,
        status='runtime integration and gameplay acceptance in progress'),indent=2)+'\n',encoding='utf-8')
    print('Megamix bank:',args.out,len(data),'bytes')

if __name__=='__main__':main()
