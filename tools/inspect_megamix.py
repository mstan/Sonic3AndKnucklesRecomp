#!/usr/bin/env python3
"""Validate and inventory the exact private Megamix v3 input for the S3K port.

No converted bank or playable port is implied by a successful inventory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from megamix.v3_rom import MegamixV3,load_rom,ORDER,TABLES,SHA256,CRC32,MIGHTY
sys.path.insert(0,str(Path(__file__).resolve().parent/'trilogy'))
from mdcodecs import nemesis_decode,enigma_decode,kosinski_decode


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--rom',required=True,type=Path,help='raw v3 cartridge or the owner ZIP')
    ap.add_argument('--out',required=True,type=Path,help='private audit JSON path')
    ap.add_argument('--preview-dir',type=Path,help='optional private source-data previews')
    ap.add_argument('--check-conversion',action='store_true',help='also validate S3K static geometry conversion')
    ap.add_argument('--export-geometry-dir',type=Path,help='private static payloads; not a playable stage bank')
    ap.add_argument('--export-mighty-dir',type=Path,help='private native sprite payloads; character runtime still required')
    args=ap.parse_args();rom=MegamixV3(load_rom(args.rom))
    stages=[]
    for z,a in ORDER:
        stage=rom.stage(z,a);h=stage['header']
        art,_=nemesis_decode(rom.data,h['art'])
        blocks=bytes(enigma_decode(rom.data,h['blocks']))
        chunks,_=kosinski_decode(rom.data,h['chunks'])
        if not art or len(art)%32 or len(chunks)%512:
            raise ValueError(f"Invalid decoded geometry: {stage['name']}")
        max_block=max(int.from_bytes(chunks[i:i+2],'big')&0x7FF for i in range(0,len(chunks),2))
        if max_block>=len(blocks)//8:
            raise ValueError(f"Chunk references absent block in {stage['name']}")
        rom.blob(stage['collision_index'],len(blocks)//8)
        # Validate every referenced chunk, including alternate loop paths.
        for plane in (0,1):
            _,rows=rom.layout(z,a,plane)
            for row in rows:
                for c in row:
                    if (c&0x7F)>len(chunks)//512:
                        raise ValueError(f"Layout references absent chunk: {stage['name']}")
        stage['decoded']=dict(art_tiles=len(art)//32,blocks=len(blocks)//8,
                              chunks=len(chunks)//512,static_geometry='validated')
        if args.check_conversion or args.export_geometry_dir:
            from megamix.geometry import convert_geometry
            converted=convert_geometry(rom,z,a)
            stage['conversion']={k:v for k,v in converted.items() if isinstance(v,int)}
            if args.export_geometry_dir:
                dest=args.export_geometry_dir/stage['source_stage'];dest.mkdir(parents=True,exist_ok=True)
                for key,value in converted.items():
                    if isinstance(value,bytes):(dest/(key+'.bin')).write_bytes(value)
        stages.append(stage)
        print(stage['name'],stage['source_stage'],stage['placements'],'placements',flush=True)
    frames=rom.mighty_frames()
    from megamix.mighty import convert_mighty
    mighty_data,mighty_report=convert_mighty(rom)
    if args.export_mighty_dir:
        args.export_mighty_dir.mkdir(parents=True,exist_ok=True)
        for key,value in mighty_data.items():(args.export_mighty_dir/(key+'.bin')).write_bytes(value)
        (args.export_mighty_dir/'manifest.json').write_text(json.dumps(mighty_report,indent=2)+'\n',
                                                         encoding='utf-8',newline='\n')
    report=dict(source=dict(bytes=len(rom.data),crc32=f'{CRC32:08X}',sha256=SHA256),
                campaign_order=['Sonic 1','Sonic 2','Sonic 3 & Knuckles','Sonic Megamix'],
                tables=TABLES,stages=stages,mighty=dict(**MIGHTY,frames=len(frames),
                    frame_art_sha256=hashlib.sha256(b''.join(tiles for _,tiles in frames)).hexdigest(),
                    native_conversion=mighty_report),
                status='source data converted; stage enrollment/gameplay/Mighty controller integration pending')
    report['requires_extended_chunks']=[s['source_stage'] for s in stages
        if s.get('conversion',{}).get('requires_extended_chunks')]
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2)+'\n',newline='\n')
    if args.preview_dir:
        from megamix.preview import mighty_sheet
        args.preview_dir.mkdir(parents=True,exist_ok=True)
        mighty_sheet(rom,args.preview_dir/'mighty-source-frames.png')
    print('Inventoried',len(stages),'acts;',sum(s['placements'] for s in stages),'placements')
    if report['requires_extended_chunks']:
        print('Extended layouts required for:',','.join(report['requires_extended_chunks']))


if __name__=='__main__':main()
