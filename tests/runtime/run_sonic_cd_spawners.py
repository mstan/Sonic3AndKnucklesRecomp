#!/usr/bin/env python3
"""Source-placement CD enemy probes for generated children and contact behavior.

Player positioning, attack posture and protection are explicit bypasses. Enemy
state, child slots, respawn flags and success flags are observed only. A native
defeat explosion establishes a kill; disappearance never does. Bombs are
indestructible hazards and are tested through their natural fuse/fire sequence.
"""
import argparse
import hashlib
import json
from pathlib import Path

from run_stage_pack_audit import errors, manifest
from run_stage_pack_encounters import Native, objects, u16
from run_sonic_cd_enemies import FAMILIES


def expanded(ram, code):
    return [dict(o, subtype=ram[o['addr'] + 0x2c],
                 role=ram[o['addr'] + 0x3a], raw=ram[o['addr']:o['addr'] + 0x4a].hex())
            for o in objects(ram) if o['code'] == code]


def probe(a, stage, placement, family):
    case = a.out / f"{stage}-{placement['index']}-{family}"
    rec = dict(stage=stage, placement=placement, family=family, status='unverified',
               bypasses=['level-entry/checkpoint positioning', 'player positioning/posture',
                         'hurt protection and player revival after an invalid cheat spawn'])
    run = None
    try:
        run = Native(a, stage, (max(160, placement['x'] - 64), max(96, placement['y'] - 64), 0), case, 0)
        code = placement['code']
        ram, frame = run.snapshot('initial')
        rec['initial'] = expanded(ram, code)
        history = []
        if family == 'bigbom':
            kinds = set(); max_fires = 0; harmful = False; body_gone = False
            for tick in range(120):
                ram, frame = run.snapshot()
                obs = expanded(ram, code)
                history.append(dict(frame=frame, objects=obs))
                children = [o for o in obs if o['subtype'] & 0x80]
                kinds.update(o['subtype'] & 14 for o in children)
                fires = [o for o in children if o['subtype'] & 14 == 8]
                max_fires = max(max_fires, len(fires))
                harmful |= any(o['collision'] & 0x80 for o in fires)
                bodies = [o for o in children if o['subtype'] & 14 == 2]
                if bodies:
                    body = min(bodies, key=lambda o: abs(o['x'] - placement['x']))
                    run.player(ram, body['x'] + 40, max(0, body['y'] - 30), freeze=True)
                elif 6 in kinds:
                    body_gone = True
                if {2, 4, 6, 8} <= kinds and max_fires == 7 and harmful and body_gone and not obs:
                    rec['status'] = 'passed'; break
                run.step(8)
            rec.update(observed_kinds=sorted(kinds), max_simultaneous_fires=max_fires,
                       fires_have_hazard_collision=harmful, natural_body_cleanup=body_gone,
                       contract='body -> proximity fuse -> blast -> seven harmful fires -> natural cleanup; no kill claimed')
        else:
            # Scarab's unseen maker has zero touch; only the generated body
            # is a badnik, and only after approaching it from behind.
            defeated = []
            for attempt in range(60):
                ram, frame = run.snapshot()
                obs = expanded(ram, code)
                history.append(dict(frame=frame, objects=obs))
                if family == 'scarab':
                    targets = [o for o in obs if not o['subtype'] & 0xa0]
                elif family == 'hotaru':
                    # Its brief screen-relative lighting animation is drawn
                    # without touch response. Attack its world-space phase.
                    targets = [o for o in obs if o['role'] == 255 and o['render'] & 4]
                else:
                    targets = [o for o in obs if o['collision'] and not o['collision'] & 0xc0]
                if not targets:
                    run.step(6); continue
                target = min(targets, key=lambda o: abs(o['x'] - placement['x']))
                dx, dy = ((18 if ram[target['addr'] + 0x40] == 0 else -18), -8) if family == 'scarab' else (
                         ((0, -12), (-8, 0), (8, 0), (0, 4))[attempt % 4])
                run.player(ram, target['x'] + dx, max(0, target['y'] + dy), attack=True)
                # Freeze movement without the bit which disables native touch.
                run.write(0xb02e, b'\x01')
                run.step(1)
                after, aframe = run.snapshot()
                hit = next((o for o in objects(after) if o['addr'] == target['addr']), None)
                history.append(dict(frame=aframe, target_before=target, target_after=hit))
                # Kemusi's linked pieces clean themselves up immediately when
                # any piece dies. Read the always-on frame ring to observe the
                # native explosion before another piece removes its own slot.
                addresses = {o['addr'] for o in obs} if family == 'kemusi' else {target['addr']}
                explosion = None
                for sample in range(frame + 1, aframe + 1):
                    sample_ram = bytes.fromhex(run.cmd('get_frame', frame=sample, include='wram')['wram'])
                    hit = next((o for o in objects(sample_ram) if o['addr'] in addresses and
                                o['code'] in (0x1e5e0, 0x1e66e) and o['status'] & 0x80), None)
                    if hit:
                        explosion = dict(frame=sample, before=target, explosion=hit)
                        (case / f'defeat-frame-{sample}.ram').write_bytes(sample_ram)
                        break
                if explosion:
                    defeated.append(explosion)
                    run.snapshot('defeat')
                    rec['status'] = 'passed'; break
                run.step(2)
            rec.update(defeats=defeated, contract='real contact produced native enemy-defeat explosion')
        run.snapshot('final')
        (case / 'history.json').write_text(json.dumps(history, indent=2) + '\n')
    except Exception as exc:
        rec.update(status='failed', error=repr(exc))
    finally:
        if run:
            run.close()
            rec['errors'] = errors((case / 'run.log').read_text(errors='replace'))
            if rec['errors']: rec['status'] = 'failed'
        case.mkdir(parents=True, exist_ok=True)
        (case / 'result.json').write_text(json.dumps(rec, indent=2) + '\n')
        print(json.dumps({k: v for k, v in rec.items() if k not in ('initial', 'placement', 'defeats')}), flush=True)
    return rec


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'bank', 'out'):
        p.add_argument('--' + key, required=True, type=lambda s: Path(s).resolve())
    p.add_argument('--cases', required=True, help='Quoted stage:source-placement-index pairs')
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    a.character = 1; a.kind = 'mechanics'; a.god = True; a.target_fps = 120
    a.revive_player = True
    stages = {s['stage']: s for s in manifest(a.bank)}; results = []
    for case in a.cases.split(','):
        stage, index = case.lower().split(':')
        placement = next(o for o in stages[stage]['objects'] if o['index'] == int(index))
        family = FAMILIES[(int(stage[:2], 16) - 0x40) // 4][placement['id']]
        if family not in ('bigbom', 'scarab', 'yago', 'kemusi', 'hotaru'):
            raise ValueError('No behavioral contract for ' + family)
        results.append(probe(a, stage, placement, family))
    (a.out / 'results.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(), results=results), indent=2) + '\n')
    return int(any(r['status'] != 'passed' for r in results))


if __name__ == '__main__': raise SystemExit(main())
