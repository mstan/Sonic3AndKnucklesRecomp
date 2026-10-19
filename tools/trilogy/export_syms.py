"""Write the label addresses the bank builder needs into tools/trilogy/data/,
so the builder runs without the disassemblies (their listings carry the
games' bytes and are never shipped; addresses and names alone are).

  python tools/trilogy/export_syms.py

data/s1_syms.txt, data/s2_syms.txt: "name hexvalue" per line (labels plus
the ArtTile_* equates); data/code_sfx.txt: the S3K sound ids the C layer
plays (s2_sfx.code_sfx_ids), one hex id per line.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s1_objects  # noqa: E402
import s2_objects  # noqa: E402
import s2_sfx  # noqa: E402

DATA = HERE / 'data'


def write(name, sym):
    lines = [f"{k} {v & 0xFFFFFFFF:X}" for k, v in sorted(sym.items())]
    (DATA / name).write_text('\n'.join(lines) + '\n')
    print(f"  {name}: {len(lines)} symbols")


def main():
    DATA.mkdir(exist_ok=True)
    write('s1_syms.txt', s1_objects.load_symbols(from_data=False))
    write('s2_syms.txt', s2_objects.symbols(from_data=False))
    ids = sorted(s2_sfx.code_sfx_ids(from_data=False))
    (DATA / 'code_sfx.txt').write_text(''.join(f"{i:X}\n" for i in ids))
    print(f"  code_sfx.txt: {len(ids)} ids")


if __name__ == '__main__':
    main()
