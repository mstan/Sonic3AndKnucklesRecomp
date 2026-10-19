#!/usr/bin/env python3
"""Export independent private campaign packs from a verified converted bank.

No source ROMs or derived assets are distributed. Shared conversion tables
are retained in each self-contained pack; only its own stages are enrolled.
Absolute guest pointers stay intact, avoiding guesses about pointer-like art.
"""
import argparse,os,struct,zlib
from pathlib import Path

def split(data,content):
    if data[:6]!=b'TRLG\0\1' or data[-12:-8] not in (b'SPK1',b'SPK2'):
        raise ValueError('Expected a converted SPK1/SPK2 bank')
    size,crc=struct.unpack_from('>II',data,len(data)-8)
    if size!=len(data)-12 or zlib.crc32(data[:-12])!=crc:raise ValueError('Bank checksum mismatch')
    if content==3 and data[-12:-8]!=b'SPK2':raise ValueError('Megamix needs an extended bank')
    out=bytearray(data[:-12]);out[0x28:0x30]=b'CPK1'+bytes([content,1,0,0])
    found=0
    for i in range(128):
        p=0x40+i*0x40
        if out[p+0x25]!=content:out[p:p+0x40]=bytes(0x40)
        elif any(out[p:p+4]):found+=1
    if found!=(19 if content!=2 else 20):raise ValueError(f'Incomplete campaign: {found} acts')
    out+=struct.pack('>4sII',data[-12:-8],len(out),zlib.crc32(out))
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bank',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path);p.add_argument('--content',choices=['all','sonic1','sonic2','megamix'],default='all')
    a=p.parse_args();data=a.bank.read_bytes();a.out.mkdir(parents=True,exist_ok=True)
    for i,name in enumerate(('sonic1','sonic2','megamix'),1):
        if a.content not in ('all',name):continue
        dst=a.out/(name+'-stages.pak')
        if dst.resolve()==a.bank.resolve():raise ValueError('Output must differ from source')
        tmp=dst.with_suffix('.pak.tmp');tmp.write_bytes(split(data,i));os.replace(tmp,dst)
        print(f'{name}: {dst.stat().st_size} bytes; self-contained experimental content')

if __name__=='__main__':main()
