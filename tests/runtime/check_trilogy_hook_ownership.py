#!/usr/bin/env python3
"""Every declared campaign hook must be owned before renderer fallthrough.

The renderer interprets an unowned gameplay hook as an object range check and
may replace D0. A missing ownership case can therefore corrupt a music request
even when the campaign mod is disabled. This checks the generated interface.
"""
from pathlib import Path
import re
import tomllib

root=Path(__file__).resolve().parents[2]
header=(root/'game/trilogy/trilogy_hooks.h').read_text(encoding='utf-8')
declared={name:int(pc,16) for name,pc in re.findall(r'^#define\s+(TH_\w+)\s+0x([0-9A-Fa-f]+)u',header,re.M)}
macro=re.search(r'^#define TH_ALL_CASES (.+)$',header,re.M).group(1)
owned={int(pc,16) for pc in re.findall(r'case 0x([0-9A-Fa-f]+)u:',macro)}
missing={name:hex(pc) for name,pc in declared.items() if pc not in owned}
assert not missing, ('Campaign hooks missing ownership',missing)
config=tomllib.loads((root/'game/sonic3k/game.toml').read_text(encoding='utf-8'))
emitted={site['addr'] for site in config['widescreen_site'] if site['kind']=='game_hook'}
assert set(declared.values())<=emitted, ('Hook declaration is not emitted',set(declared.values())-emitted)
print(f'{len(declared)} campaign hook declarations own their sites and are emitted as game hooks')
