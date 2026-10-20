#!/usr/bin/env python3
"""Earn a basic shield in Megamix, use that CD prerequisite, and test the area attack.

Stage selection, position/posture/protection and rings/emeralds are prerequisites.
Monitor pickup, Super, enemy defeat and attack cost are never written. A fresh
CD process is given the observed shield as a declared starting prerequisite;
this is not a cross-campaign inventory or transition check.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

from run_stage_pack_audit import errors, manifest
from run_stage_pack_encounters import Native, objects, u16


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'content', 'packs', 'donor-bank', 'donor-rom', 'out'):
        p.add_argument('--' + key, required=True, type=lambda s: Path(s).resolve())
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    a.bank = a.content; a.character = 5; a.mighty = True; a.kind = 'mechanics'
    a.god = True; a.revive_player = True; a.target_fps = 120; a.pack_mask = 12
    os.environ['MEGAMIX_TEST_ROM'] = a.donor_rom.as_posix()
    os.environ['MEGAMIX_TEST_BANK'] = (a.packs / 'megamix-stages.pak').as_posix()
    report = dict(status='failed', checks=[], exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
                  bypasses=['stage selection/order', 'player position/posture/protection/revival', 'rings/emeralds'])
    run = None

    def check(name, condition, **observations):
        row = dict(name=name, status='passed' if condition else 'failed', **observations)
        report['checks'].append(row); print(json.dumps(row), flush=True)
        if not condition: raise AssertionError(name)

    def press(keys, frames):
        run.cmd('set_input', keys='00'); run.step(2)
        before, first = run.snapshot()
        run.cmd('set_input', keys=f'{keys:02x}'); run.step(frames)
        after, last = run.snapshot(); run.cmd('set_input', keys='00')
        history = [bytes.fromhex(run.cmd('get_frame', frame=f, include='wram')['wram'])
                   for f in range(first + 1, last + 1)]
        (a.out / f'input-{len(report["checks"])}.bin').write_bytes(b''.join(history))
        return before, after, history

    try:
        data = a.donor_bank.read_bytes()
        pointer = lambda off: struct.unpack_from('>I', data, off)[0] - 0x400000
        stage = next(s for s in manifest(a.donor_bank) if s['stage'] == '3400')
        info = pointer(0x40 + ((0x34 - 0x20) * 4) * 0x40 + 0x3c)
        tags = pointer(info + 56)
        box = next(o for o in stage['objects'] if o['id'] == 0x26 and data[tags + o['index']] == 4)
        run = Native(a, '3400', (box['x'] - 60, max(96, box['y'] - 80), 0), a.out / 'case', 0)
        for attempt in range(12):
            ram, _ = run.snapshot()
            boxes = [o for o in objects(ram) if o['code'] == box['code'] and abs(o['x'] - box['x']) < 4]
            if boxes:
                obj = boxes[0]; run.player(ram, obj['x'], max(0, obj['y'] - 24), attack=True)
                run.write(0xb02e, b'\x01'); run.step(4)
            run.step(30); ram, _ = run.snapshot()
            if ram[0xb02b] & 0x71 == 1: break
        check('actual Megamix monitor grants basic shield', ram[0xb02b] & 0x71 == 1,
              shield=ram[0xb02b] & 0x71, placement=box)
        starting_shield=ram[0xb02b]&0x71
        run.close();run=None
        run=Native(a,'4000',None,a.out/'cd-case',0)
        run.write(0xb02b,bytes((starting_shield,)))
        report['bypasses'].append('observed basic shield supplied as CD starting prerequisite; inventory transfer not claimed')
        ram,_=run.snapshot()
        run.player(ram, 128, 1798, attack=True); run.write(0xb040, b'\1')
        run.write(0xffb0, b'\7'); run.write(0xfe20, b'\0\xc8')
        press(0x30, 2)
        for _ in range(40):
            run.step(8);ram,_=run.snapshot()
            if ram[0xfe19]==1 and not ram[0xb02e]:break
        ram, _ = run.snapshot('super')
        check('real B+C transforms Mighty in CD', bool(ram[0xfe19]) and ram[0xb02b] & 0x71 == 1,
              super_flag=ram[0xfe19], shield=ram[0xb02b] & 0x71)
        target_placement = next(s for s in manifest(a.content) if s['stage'] == '4000')['objects'][0]
        # The actual source table identifies this Tamabboh. Its native $1A
        # collision shape is precisely the prior area's missing category.
        for _ in range(30):
            ram, _ = run.snapshot()
            targets = [o for o in objects(ram) if o['code'] == target_placement['code'] and o['render'] & 128 and o['collision'] == 0x1a]
            if targets: break
            run.player(ram, 480, 1790, freeze=True); run.step(4)
        check('CD extended-hitbox enemy is actually spawned', bool(targets), source=target_placement)
        target = targets[0]; run.player(ram, target['x'] - 90, target['y'] - 70, attack=True)
        run.write(0xb040, b'\1'); run.write(0xfe20, b'\0\xc8')
        # A is a midair ability. The neutral input interval may land the
        # positioned player on terrain, so make a real jump before pressing A.
        press(0x20, 2)
        ram, _ = run.snapshot('jump')
        check('actual jump precedes the airborne area attack', bool(ram[0xb02a] & 2) and bool(ram[0xb040]),
              player_status=ram[0xb02a], jumping=ram[0xb040])
        before, after, history = press(0x40, 3)
        killed = [o for d in history for o in objects(d) if o['addr'] == target['addr'] and
                  o['code'] in (0x1e5e0, 0x1e66e) and o['status'] & 0x80]
        spent = u16(before, 0xfe20) - u16(after, 0xfe20)
        check('Super A defeats the CD enemy through native collision service and spends rings',
              bool(killed) and spent >= 16, explosions=killed, rings_spent=spent)
        run.snapshot('defeat'); report['status'] = 'passed'
    except Exception as exc:
        report['error'] = repr(exc)
    finally:
        if run:
            run.close(); report['runtime_errors'] = errors((run.case / 'run.log').read_text(errors='replace'))
            if report['runtime_errors']: report['status'] = 'failed'
        (a.out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(report['status'] != 'passed')


if __name__ == '__main__': raise SystemExit(main())
