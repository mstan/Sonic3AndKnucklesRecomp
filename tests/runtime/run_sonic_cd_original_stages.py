#!/usr/bin/env python3
"""Capture all original CD variants through the game's real stage-select menu.

The menu state must come from the actual Up/Down/Down/Left/Right/B title cheat.
Selection bypasses progression. Only player control is frozen for the visual
capture; no completion, collectible, enemy or level outcome is injected.
Loaded 256 KiB Word RAM must exactly equal the selected owned MMD, so a title
card or menu label alone cannot certify that the intended program was loaded.
The independent emulator is external QA software, never a runtime dependency.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import subprocess

from sonic_cd_game_reference import OriginalGame
from sonic_cd_background_reference import DISASM_PIN


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('core', 'cue', 'bios', 'menu-state', 'reference', 'files', 'out'):
        p.add_argument('--' + name, required=True, type=lambda s: Path(s).resolve())
    p.add_argument('--levels', default='all')
    p.add_argument('--settle-existing', type=lambda s: Path(s).resolve(),
                   help='Reuse previously hash-qualified original level states and wait for title cards to clear.')
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    git = ['C:/Program Files/Git/cmd/git.exe', '-c', 'core.fsmonitor=false', '-c',
           'core.fscache=false', '-c', 'core.preloadIndex=false', '-C', str(a.reference)]
    if subprocess.check_output(git + ['rev-parse', 'HEAD'], text=True).strip() != DISASM_PIN:
        raise ValueError('Unqualified disassembly source')
    text = (a.reference / 'src/Title Screen/Secrets/Stage Select.asm').read_text()
    table = text.split('Text_Selections:', 1)[1].split('Text_SelectionsEnd:', 1)[0]
    selections = re.findall(r'TEXTPTR\s+TextDat_(\w+),', table)
    targets = [(i, name) for i, name in enumerate(selections) if re.fullmatch(r'R[1345678][123][ABCD]', name)]
    if len(targets) != 70: raise ValueError(('Expected all 70 original stage/time variants', len(targets)))
    if a.levels != 'all':
        wanted = set(a.levels.upper().replace('__', '').split(','))
        targets = [(i, n) for i, n in targets if n in wanted]
        if len(targets) != len(wanted): raise ValueError('Unknown original stage')
    game = OriginalGame(a.core, a.cue, a.bios, a.out / 'system')
    rows = []
    try:
        for index, name in targets:
            case = a.out / name; case.mkdir()
            expected = (a.files / (name + '__.MMD')).read_bytes()
            if a.settle_existing:
                previous = json.loads((a.settle_existing / 'results.json').read_text())
                if previous['identity'] != game.identity: raise ValueError('Original capture identity changed')
                game.load(a.settle_existing / name / 'level.state')
            else:
                game.load(a.menu_state)
                game.run(index * 8, (5,))  # one real Down selection per eight VBlanks
                game.run(1)
                game.capture(case / 'menu.png')
                game.run(2, (3,))
            ram = None
            for attempt in range(150):
                game.run(4)
                ram = game.ram()
                if ram[0xd000] == 1 and ram[0x1957] and int.from_bytes(ram[0x1504:0x1506], 'big') >= 64:
                    memory = next(m for m in game.maps if m['name'] == 'WORDRAM')
                    raw = C.string_at(memory['ptr'], memory['length'])
                    guest = bytearray(len(raw)); guest[0::2] = raw[1::2]; guest[1::2] = raw[0::2]
                    if guest == expected: break
            else:
                game.capture(case / 'load-failure.png')
                raise RuntimeError('Original stage did not load: ' + name)
            game.write(0xd02c, b'\x01')  # source Sonic oPlayerCtrl; visual prerequisite only
            game.run(192)
            game.capture(case / 'start.png'); game.save(case / 'level.state')
            ram = game.ram()
            row = dict(level=name, menu_index=index, status='captured', visual_status='unreviewed',
                       original_mmd_sha256=hashlib.sha256(expected).hexdigest(),
                       word_ram_exactly_matches_owned_mmd=True,
                       camera=[int.from_bytes(ram[at:at+2], 'big', signed=True) for at in (0xf700, 0xf704)],
                       player=[int.from_bytes(ram[at:at+2], 'big') for at in (0xd008, 0xd00c)],
                       level_frame=int.from_bytes(ram[0x1504:0x1506], 'big'))
            (case / 'result.json').write_text(json.dumps(row, indent=2) + '\n')
            rows.append(row); print(json.dumps(row), flush=True)
            (a.out / 'results.json').write_text(json.dumps(dict(identity=game.identity,
                disassembly_pin=DISASM_PIN, menu_state_sha256=hashlib.sha256(a.menu_state.read_bytes()).hexdigest(),
                bypasses=['original built-in stage select', 'player control freeze after stage entry'],
                results=rows), indent=2) + '\n')
    finally: game.close()


if __name__ == '__main__': main()
