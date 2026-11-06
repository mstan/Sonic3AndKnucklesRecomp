"""Build the original queued-Kosinski instruction module, without ROM assets."""
import argparse
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--compiler', required=True)
p.add_argument('--rom', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--mode', required=True, choices=['sonic3', 'sonic3k', 'sandk'])
a = p.parse_args()
start, restore, backup, end = ((0x1EA4, 0x1F86, 0x1F96, 0x1FA2)
    if a.mode == 'sonic3' else (0x1C1A, 0x1CFC, 0x1D0C, 0x1D18))
image = a.rom.read_bytes()
assert image[start:start+2] == bytes.fromhex('4a78')
assert image[restore:restore+2] == bytes.fromhex('4cf8')
assert image[end-2:end] == bytes.fromhex('4e75')
a.out.mkdir(parents=True, exist_ok=True)
fragment = a.out / 'kos-program.bin'
fragment.write_bytes(image[start:end])
try:
    subprocess.run([a.compiler, str(fragment), str(a.out), 'sonic3_kos_native',
                    hex(start), hex(start), hex(restore), hex(backup)], check=True)
finally:
    fragment.unlink()
