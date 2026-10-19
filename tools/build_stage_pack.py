#!/usr/bin/env python3
"""Build the optional complete Sonic 1 & 2 campaign from owner-supplied ROMs.

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
        parser.add_argument('--'+game, required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        for game in ROMS:
            verify(getattr(args, game), game)
        output = args.out.resolve()
        if output in {getattr(args, game).resolve() for game in ROMS}:
            raise ValueError('Output must be a different file from every source ROM')
        output.parent.mkdir(parents=True, exist_ok=True)
        # Staging is private and on the same filesystem for an atomic replace.
        with tempfile.TemporaryDirectory(prefix='stage-pack-', dir=output.parent) as tmp:
            bank = Path(tmp)/'bank.bin'
            subprocess.run([sys.executable, str(ROOT/'tools/trilogy/build_bank.py'),
                '--s1', str(args.s1.resolve()), '--s2', str(args.s2.resolve()),
                '--s3k', str(args.s3k.resolve()), '--zones', ZONES,
                '--out', str(bank)], check=True)
            data = bank.read_bytes()
            bank.write_bytes(data+struct.pack('>4sII', b'SPK1', len(data), zlib.crc32(data)))
            os.replace(bank, output)
        print(f'Sonic 1 & 2 Stage Pack: {output} ({output.stat().st_size} bytes)')
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
