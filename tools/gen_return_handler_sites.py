"""Export object-handler continuations from source-verified instruction starts."""
from pathlib import Path
import argparse
import hashlib
import importlib.util


def discover(engine, rom, starts):
    spec = importlib.util.spec_from_file_location(
        'disasm_return_handlers', engine / 'tests/tools/gen_disasm_labels.py')
    labels = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(labels)
    # A gap in the source instruction map must never become an instruction.
    # The longest original 68000 instruction is ten bytes.
    offsets = sorted(starts)
    instructions = {a: rom[a:b] for a, b in zip(offsets, offsets[1:])
                    if 2 <= b - a <= 10}
    return labels.return_handler_entries(instructions)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=root / 'segagenesisrecomp')
    parser.add_argument('--mode', choices=['sonic3', 'sandk', 'sonic3k'], required=True)
    args = parser.parse_args()
    folder = root / 'game' / args.mode
    rom = (folder / (args.mode + '.bin')).read_bytes()
    starts = {int(line, 16) for line in
              (folder / (args.mode + '.code_addrs.txt')).read_text().splitlines()
              if line and not line.startswith('#')}
    entries = discover(args.engine, rom, starts)
    output = '# Source-backed calls that install their return PC as an object handler.\n'
    output += '# Regenerate with tools/gen_return_handler_sites.py --mode ' + args.mode + '\n'
    output += '# ROM SHA256: ' + hashlib.sha256(rom).hexdigest() + '\n[functions]\nextra = [\n'
    output += ''.join(f'    0x{pc:06X}, # {name}\n' for pc, name in sorted(entries.items()))
    output += ']\n'
    (folder / (args.mode + '.return_handlers.toml')).write_text(output, encoding='utf-8', newline='\n')
    print(f'{args.mode}: {len(entries)} source-derived return handlers')


if __name__ == '__main__':
    main()
