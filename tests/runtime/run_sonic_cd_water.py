#!/usr/bin/env python3
"""Compare live CD water targets with independent original-disc observations.

Supply --original-results from --capture-original, or capture them with the
separately supplied reference core, owned BIOS/disc and qualified stage states.
Only stage/position/control/rings/air prerequisites are written. Water levels,
underwater flags, movement constants, drowning and retry are always observed.
Output devices are dummy; the original core consumes audio without a device.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import struct
from pathlib import Path

from run_stage_pack_audit import errors
from run_stage_pack_encounters import Native, u16

LEVELS = [f'R4{act}{period}' for act in (1, 2, 3)
          for period in ('ABCD' if act < 3 else 'CD')]


def positions(level):
    if level[2] == '1':
        return [(0x400, 0x180), (0x700, 0x180), (0xa00, 0x180), (0x1a00, 0x180)]
    if level[2] == '2':
        return [(0x300, 0x800), (0xc80, 0x800), (0xe80, 0x800),
                (0x1180, 0x200), (0x1400, 0x800), (0x1680, 0x800), (0x1a00, 0x800)]
    return [(0x500, 0x500), (0x900, 0x500), (0xb80, 0x500)]


def capture_original(a):
    from sonic_cd_game_reference import OriginalGame
    directory = a.out / 'original'; directory.mkdir()
    game = OriginalGame(a.core, a.cue, a.bios, directory / 'system')
    rows = []
    try:
        qualified = json.loads((a.reference_states / 'results.json').read_text())
        if qualified['identity'] != game.identity:
            raise ValueError('Original stage-state identity changed')
        for level in LEVELS:
            proof = next(r for r in qualified['results'] if r['level'] == level)
            if not proof['word_ram_exactly_matches_owned_mmd']:
                raise ValueError('Unqualified original stage state: ' + level)
            game.load(a.reference_states / level / 'level.state')
            for x, y in positions(level):
                game.write(0xd008, x.to_bytes(2, 'big'))
                game.write(0xd00c, y.to_bytes(2, 'big'))
                game.run(7); input_ram = game.ram()
                game.run(1); ram = game.ram()
                if ram[0xd024] != 2: raise RuntimeError('Original player is not alive')
                rows.append(dict(level=level, position=[x, y],
                    water=[u16(ram, at) for at in (0xf646, 0xf648, 0xf64a)],
                    speed=ram[0xf64c], camera=[u16(ram, at) for at in (0xf700, 0xf704)],
                    water_input_camera=[u16(input_ram, at) for at in (0xf700, 0xf704)],
                    player_routine=ram[0xd024]))
            (directory / (level + '.ram')).write_bytes(ram)
        result = dict(identity=game.identity, results=rows,
            bypasses=['built-in stage select', 'player position and control freeze'])
        (directory / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        return result
    finally: game.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'content', 'out'):
        p.add_argument('--' + name, required=True, type=lambda v: Path(v).resolve())
    p.add_argument('--original-results', type=lambda v: Path(v).resolve())
    p.add_argument('--capture-original', action='store_true')
    for name in ('core', 'cue', 'bios', 'reference-states'):
        p.add_argument('--' + name, type=lambda v: Path(v).resolve())
    p.add_argument('--levels', default='all', help='Original level names, all, or none for character-only checks')
    p.add_argument('--characters', default='0,1,2,3,4,5,6,7')
    p.add_argument('--workers', type=int, default=2)
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    a.bank = a.content; a.kind = 'mechanics'; a.god = True; a.target_fps = 120
    if a.capture_original:
        if not all(getattr(a, n) for n in ('core', 'cue', 'bios', 'reference_states')):
            p.error('Original capture needs core, cue, bios and reference-states')
        reference = capture_original(a)
    elif a.original_results:
        reference = json.loads(a.original_results.read_text())
    else: p.error('Supply original-results or capture-original')
    levels = LEVELS if a.levels == 'all' else [] if a.levels == 'none' else a.levels.upper().split(',')
    if any(level not in LEVELS for level in levels): p.error('Unknown original water level')
    cases = [('targets', level) for level in levels]
    cases += [('physics', int(c)) for c in a.characters.split(',') if c]

    def probe(case):
        kind, identity = case
        stage = f'{0x48 + ord(identity[3]) - ord("A"):02x}{int(identity[2]) - 1:02x}' if kind == 'targets' else '4800'
        fixture = argparse.Namespace(**vars(a)); fixture.character = 1 if kind == 'targets' else identity
        fixture.mighty = fixture.character in (4, 5)
        directory = a.out / f'{kind}-{identity}'; run = None
        record = dict(kind=kind, identity=identity, stage=stage, status='failed', checks=[],
            bypasses=['stage selection', 'player position/control', 'hurt protection'])

        def check(name, passed, **data):
            record['checks'].append(dict(name=name, passed=bool(passed), **data))
            if not passed: raise AssertionError(name)

        try:
            run = Native(fixture, stage, None, directory, 0)
            ram, _ = run.snapshot('start')
            check('water initialized independently of other stage packs', ram[0xf730] == 1 and u16(ram, 0xf648) < 0x1000,
                  water=[u16(ram, at) for at in (0xf646, 0xf648, 0xf64a)])
            # This is after title-card PLC completion, the former corrupting path.
            run.step(100); ram, _ = run.snapshot('card-done')
            check('title-card completion retains intact water and stage',
                  ram[0xf730] == 1 and u16(ram, 0xfe10) == int(stage, 16) and ram[0xf600] == 12)
            if kind == 'targets':
                for index, oracle in enumerate(r for r in reference['results'] if r['level'] == identity):
                    x, y = oracle['position']; ram, _ = run.snapshot()
                    run.player(ram, x, y, freeze=True); run.step(8)
                    ram, _ = run.snapshot(f'point-{index}')
                    actual, expected = u16(ram, 0xf64a), oracle['water'][2]
                    # Act 3's entrance intentionally holds water below the viewport.
                    if identity[2] == '3' and x < 0x7c0:
                        actual = (actual - u16(ram, 0xee84)) & 65535
                        expected = (expected - oracle['water_input_camera'][1]) & 65535
                    check('target matches actual original program at source branch', actual == expected,
                          position=[x, y], actual=actual, original=expected, speed=ram[0xf64c])
                    bob = (u16(ram, 0xf646) - u16(ram, 0xf648) + 32768) % 65536 - 32768
                    check('surface remains within original four-pixel bob', -4 <= bob <= 4, bob=bob)
            else:
                def position(x, y):
                    # Preserve the live underwater bit. Rebuilding a generic
                    # attack posture would clear it before the exit check runs.
                    run.write(0xb010, struct.pack('>II', x << 16, y << 16))
                    run.write(0xb018, bytes(6)); run.write(0xb02e, b'\x01')

                # Tails keeps his movement constants in the retail P2 block,
                # including Tails Alone in player slot 1.
                speeds = (0xfec0, 0xfec2, 0xfec4) if identity == 2 else (0xf760, 0xf762, 0xf764)
                dry = [u16(ram, at) for at in speeds]
                position(0x400, 0x340); run.step(8)
                wet, _ = run.snapshot('underwater')
                check('actual water entry sets native underwater flag and movement profile',
                      wet[0xb02a] & 0x40 and [u16(wet, at) for at in speeds] == [0x300, 6, 0x40],
                      player_status=wet[0xb02a], profile=[u16(wet, at) for at in speeds])
                position(0x400, 0x180); run.step(8)
                dry_ram, _ = run.snapshot('out-of-water')
                check('actual water exit restores air and dry movement',
                      not dry_ram[0xb02a] & 0x40 and dry_ram[0xb02c] == 30 and
                      [u16(dry_ram, at) for at in speeds] == dry,
                      dry=dry, restored=[u16(dry_ram, at) for at in speeds])
                if identity == 1:
                    position(0x400, 0x340); run.step(8)
                    record['bypasses'].append('air advanced to one second for natural drowning')
                    run.write(0xb02c, b'\x01'); died = False; lives = dry_ram[0xfe12]
                    for _ in range(150):
                        run.step(8); ram, _ = run.snapshot()
                        died |= ram[0xb005] in (6, 8)
                        if died and ram[0xfe12] == lives - 1 and ram[0xb005] == 2: break
                    run.snapshot('drowning-retry')
                    check('air expiry causes natural drowning and a real water-stage retry',
                          died and ram[0xfe12] == lives - 1 and ram[0xb005] == 2 and ram[0xf730] == 1,
                          death_observed=died, lives_before=lives, lives_after=ram[0xfe12])
            record['status'] = 'passed'
        except Exception as exc: record['error'] = repr(exc)
        finally:
            if run:
                run.close(); record['runtime_errors'] = errors((directory / 'run.log').read_text(errors='replace'))
                if record['runtime_errors']: record['status'] = 'failed'
            directory.mkdir(exist_ok=True)
            (directory / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
            print(json.dumps({k: record[k] for k in ('kind', 'identity', 'status')}), flush=True)
        return record

    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        rows = list(pool.map(probe, cases))
    (a.out / 'results.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(a.content.read_bytes()).hexdigest(),
        original_identity=reference['identity'], results=rows), indent=2) + '\n')
    return int(any(r['status'] != 'passed' for r in rows))


if __name__ == '__main__': raise SystemExit(main())
