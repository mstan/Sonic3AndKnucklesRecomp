"""Build/verify the game-owned pinned skdisasm source; export source annotations.

Uses the engine's listing parser/exporter; game identity and source pin stay here.
Covers Sonic 3 alone (buildS3.lua), Sonic & Knuckles alone (buildSK.lua,
Sonic3_Complete=0) and the combined lock-on cartridge (S&K + S3 at $200000).
Pass --engine for a development checkout, then normal --out/--reuse/--install.
ROMs/listings are private build artifacts. Never edits the source submodule.
"""
import argparse
import importlib.util
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SKDISASM = ('game/skdisasm', '1e1b5aff82c21175c593e42c966a6ff8b1586ff3')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--engine', type=Path)
    known, rest = parser.parse_known_args()
    engine = known.engine or ROOT / ('engine-local' if (ROOT / 'engine-local').is_dir() else 'segagenesisrecomp')
    spec = importlib.util.spec_from_file_location('disassembly', engine / 'tools/sonic_disassembly.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ROOT = ROOT
    module.SOURCES = {
        'sonic3': (*SKDISASM, 'buildS3.lua', 's3built.bin', 's3.lst', 'game/sonic3/sonic3.bin'),
        'sandk': (*SKDISASM, 'buildSK.lua', 'skbuilt.bin', 'sonic3k.lst', 'game/sandk/sandk.bin'),
    }
    module.FOLDERS = {'sonic3': 'game/sonic3', 'sandk': 'game/sandk', 'sonic3k': 'game/sonic3k'}
    module.COMPOSITES = {'sonic3k': {
        'parts': [('sandk', 0), ('sonic3', 0x200000)],
        'reference': 'game/sonic3k/sonic3k.bin',
        'metadata': {'build': 'buildSK.lua + buildS3.lua', 's3_offset': '0x200000'},
    }}
    sys.argv = [sys.argv[0], *rest]
    module.main()
