#!/usr/bin/env python3
"""Build Sonic 1, Sonic 2, or both campaigns from owner-supplied ROMs.

No ROM data is distributed. Output is private, derived data; do not commit it.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
ZONES = 'GHZ,LZ,MZ,SLZ,SYZ,SBZ,EHZ,CPZ,ARZ,CNZ,HTZ,MCZ,OOZ,MTZ,SCZ,WFZ,DEZ'
ROMS = {'s1': (0x80000, 0xAFE05EEE), 's2': (0x100000, 0x24AB4C3A),
        's3k': (0x400000, 0x63522553)}

def verify(path, game):
    data = path.read_bytes()
    size, crc = ROMS[game]
    if len(data) != size or zlib.crc32(data) != crc:
        raise ValueError(f'{game}: expected {size} bytes and CRC32 {crc:08X}; '
                         f'got {len(data)} bytes and {zlib.crc32(data):08X}')
    return data

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for game in ROMS:
        parser.add_argument('--'+game, required=game=='s3k', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if not args.s1 and not args.s2:
            raise ValueError('Select a Sonic 1 or Sonic 2 source ROM')
        inputs={game:getattr(args,game) for game in ROMS if getattr(args,game)}
        for game,path in inputs.items():
            verify(path, game)
        output = args.out.resolve()
        if output in {path.resolve() for path in inputs.values()}:
            raise ValueError('Output must be a different file from every source ROM')
        output.parent.mkdir(parents=True, exist_ok=True)
        # Staging is private and on the same filesystem for an atomic replace.
        with tempfile.TemporaryDirectory(prefix='stage-pack-', dir=output.parent) as tmp:
            bank = Path(tmp)/'bank.bin'
            zones=ZONES.split(',')
            selected=(zones[:6] if args.s1 else [])+(zones[6:] if args.s2 else [])
            sys.path.insert(0,str(ROOT/'tools/trilogy'))
            import build_bank
            command=['--s3k',str(args.s3k.resolve()),'--zones',','.join(selected),'--out',str(bank)]
            for game in ('s1','s2'):
                if getattr(args,game):command+=['--'+game,str(getattr(args,game).resolve())]
            # In-process conversion also works in the shipped frozen helper;
            # sys.executable there is the helper, not a Python interpreter.
            build_bank.main(command)
            data = bank.read_bytes()
            bank.write_bytes(data+struct.pack('>4sII', b'SPK1', len(data), zlib.crc32(data)))
            os.replace(bank, output)
        print(f'Campaign Stage Pack: {output} ({output.stat().st_size} bytes)')
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
