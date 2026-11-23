"""Prepare native queue hooks while reading the original image unchanged.

The output contains discovery metadata only. The compiler receives the original
image by absolute path; no ROM, overlay or driver CPU software is produced.
"""
import argparse
from pathlib import Path
import re

def write(path,data):
    if isinstance(data,str):data=data.encode('utf-8')
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();game=args.game.resolve();out=args.output.resolve()
    if out==game or out.is_relative_to(game):parser.error('Metadata must be outside game source')
    mode=game.name
    image=(game/(mode+'.bin')).read_bytes()
    sites=[0x1562,0x15E2,0x160A,0x164C] if mode=='sonic3' else [0x12CE,0x1358,0x1380,0x13C2]
    pause=[]
    for pc in range(0,len(image)-7,2):
        if image[pc:pc+2]==b'\x13\xfc' and image[pc+4:pc+8]==b'\x00\xa0\x1c\x10' and image[pc+2:pc+4] in (b'\x00\x01',b'\x00\x80'):
            pause.append(pc)
    if len(pause)<3:parser.error('Original pause hooks do not match this cartridge')
    out.mkdir(parents=True,exist_ok=True)
    for path in (*game.glob('*.toml'),*game.glob('*.csv'),*game.glob('*.txt')):
        if path.name!='game.toml':write(out/path.name,path.read_bytes())
    config=(game/'game.toml').read_text(encoding='utf-8')
    config,count=re.subn(r'discovery_files\s*=\s*\[','discovery_files = [\n    "sound_hle_hooks.toml",',config,count=1)
    if count!=1:parser.error('Missing discovery file list')
    for pc in sorted(set(sites+pause)):
        config+=f'\n[[widescreen_site]]\naddr = 0x{pc:06X}\nkind = "game_hook"\n'
    write(out/'game.toml',config)
    write(out/'sound_hle_hooks.toml','[functions]\nextra = [\n'+''.join(f'    0x{pc+8:06X},\n' for pc in pause)+']\n')
    write(out/'sound_hle_pause.h','#pragma once\nstatic const unsigned s3_hle_pause_sites[] = {'+','.join(f'0x{pc:06X}u' for pc in pause)+'};\n')
    print(f'{mode}: original image retained; {len(sites)+len(pause)} native sound hooks')

if __name__=='__main__':main()
