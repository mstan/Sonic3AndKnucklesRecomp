#!/usr/bin/env python3
"""Paired original-disc/candidate captures at explicit source-derived locations.

Plans contain tag, stage, original_level, x and y. The original follows bounded
64-pixel position prerequisites so its real streaming drawers update normally.
The candidate loads its native checkpoint fixture at the location. These are
visual prerequisites, never proof of navigation, checkpoints or interactions.
Actual camera/player/water observations and independent identities accompany
all images. Captures remain unreviewed until inspected by a person/agent.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import shutil
from pathlib import Path

from run_stage_pack_audit import errors
from run_stage_pack_encounters import Native, u16
from run_sonic_cd_adaptive import diagnostics
from sonic_cd_game_reference import OriginalGame


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'bank', 'out', 'plan', 'core', 'cue', 'bios', 'reference-states'):
        p.add_argument('--' + name, required=True, type=lambda v: Path(v).resolve())
    p.add_argument('--original-only', action='store_true')
    p.add_argument('--match-framing', action='store_true', help='Use the original camera center for candidate-only horizontal framing')
    p.add_argument('--character', type=int, default=1, choices=range(8))
    p.add_argument('--workers', type=int, default=2)
    p.add_argument('--reuse-original', type=lambda v: Path(v).resolve())
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    a.mighty = a.character in (4,5); a.kind = 'mechanics'; a.god = True; a.revive_player = True; a.target_fps = 120
    plan = json.loads(a.plan.read_text())
    if len({row['tag'] for row in plan}) != len(plan): raise ValueError('Duplicate location tag')
    source = json.loads((a.reference_states / 'results.json').read_text())
    game = OriginalGame(a.core, a.cue, a.bios, a.out / 'original-system')
    records = []
    try:
        if source['identity'] != game.identity: raise ValueError('Original stage-state identity changed')
        previous = json.loads((a.reuse_original / 'results.json').read_text()) if a.reuse_original else None
        if previous and previous['original_identity'] != game.identity: raise ValueError('Reused original identity changed')
        for row in plan:
            level = row['original_level']; directory = a.out / row['tag']; directory.mkdir()
            proof = next(r for r in source['results'] if r['level'] == level)
            if not proof['word_ram_exactly_matches_owned_mmd']: raise ValueError('Unqualified original state')
            if previous:
                old = next(r for r in previous['results'] if r['tag'] == row['tag'])
                if any(old[k] != row[k] for k in ('stage', 'original_level', 'x', 'y')):
                    raise ValueError('Reused original location changed')
                for name in ('original.png', 'original.ram', 'original.state'):
                    shutil.copy2(a.reuse_original / row['tag'] / name, directory / name)
                records.append(dict(**row, original=old['original'], visual_review='unreviewed', bypasses=old['bypasses']))
                continue
            game.load(a.reference_states / level / 'level.state')
            ram = game.ram(); sx, sy = u16(ram, 0xd008), u16(ram, 0xd00c)
            x, y = row['x'], row['y']; count = max(1, (abs(x-sx)+63)//64, (abs(y-sy)+63)//64)
            for step in range(1, count + 1):
                px, py = sx+(x-sx)*step//count, sy+(y-sy)*step//count
                game.write(0xd02c, b'\x81')
                game.write(0xd010, bytes(6))  # same stationary velocity prerequisite as candidate
                game.write(0xd008, px.to_bytes(2, 'big')); game.write(0xd00c, py.to_bytes(2, 'big'))
                game.run(8)
            game.run(192)
            game.capture(directory / 'original.png'); game.save(directory / 'original.state')
            ram = game.ram()
            rec = dict(**row, original=dict(camera=[u16(ram, at) for at in (0xf700, 0xf704)],
                player=[u16(ram, at) for at in (0xd008, 0xd00c)],
                water=[u16(ram, at) for at in (0xf646, 0xf648, 0xf64a)],
                level_frame=u16(ram, 0x1504), original_mmd_sha256=proof['original_mmd_sha256']),
                visual_review='unreviewed', bypasses=['built-in stage select',
                    'original bounded player-position/control prerequisites', 'candidate checkpoint-position entry'])
            records.append(rec)
            print(json.dumps(dict(tag=row['tag'], original='captured', camera=rec['original']['camera'])), flush=True)
    finally: game.close()

    if a.original_only:
        (a.out / 'results.json').write_text(json.dumps(dict(original_identity=source['identity'],
            original_only=True, results=records), indent=2) + '\n')
        return 0

    def candidate(rec):
        directory = a.out / rec['tag']; run = None; rec['captured'] = False
        try:
            px = rec['original']['camera'][0] + 160 if a.match_framing else rec['x']
            py = rec['original']['player'][1] if a.match_framing else rec['y']
            rec['candidate_position_prerequisite'] = [px, py]
            if a.match_framing: rec['bypasses'].append('candidate horizontal framing position; player coordinates may differ')
            run = Native(a, rec['stage'], (px, py, 0), directory / 'candidate', 0)
            ram, _ = run.snapshot(); run.player(ram, px, py, freeze=True, grounded=True)
            # Let the native camera stream naturally to the reference's vertical
            # framing. Its air dead zone otherwise retains a different checkpoint
            # camera despite identical player coordinates. This is a visual-only
            # camera-center/posture prerequisite, not a camera-behavior pass.
            bias = (py - rec['original']['camera'][1]) & 65535
            run.write(0xee2e if a.character == 2 else 0xee2c, bias.to_bytes(2, 'big'))
            rec['bypasses'].append('candidate grounded freeze and vertical camera-center framing')
            if a.match_framing:
                # Retail S3K retains a 16-pixel horizontal dead zone after a
                # checkpoint start. Approach the requested camera from its left
                # edge, then restore the visual fixture's final player position.
                # Scrolling/streaming still run normally; camera RAM is untouched.
                run.player(ram, px - 16, py, freeze=True, grounded=True)
                run.step(32)
                ram, _ = run.snapshot()
                run.player(ram, px, py, freeze=True, grounded=True)
                rec['bypasses'].append('candidate left-edge approach through horizontal camera dead zone')
            run.step(192)
            for label, width in (('native', 960), ('wide', 2560)):
                run.cmd('video_configure', mode='fit', window_width=width, window_height=720); run.step(8)
                ram, frame = run.snapshot(label)
                rec[label] = dict(frame=frame, camera=[u16(ram, at) for at in (0xee80, 0xee84)],
                    player=[u16(ram, at) for at in (0xb010, 0xb014)],
                    water=[u16(ram, at) for at in (0xf646, 0xf648, 0xf64a)],
                    routine=ram[0xb005], actual_stage=f'{u16(ram,0xfe10):04x}', game_mode=ram[0xf600], diagnostics=diagnostics(run))
            rec['character'] = a.character
            rec['framing_matches'] = rec['native']['camera'] == rec['original']['camera']
            rec['captured'] = rec['wide']['actual_stage'] == rec['stage'] and rec['wide']['game_mode'] == 12
        except Exception as exc: rec['error'] = repr(exc)
        finally:
            if run:
                run.close(); rec['runtime_errors'] = errors((run.case / 'run.log').read_text(errors='replace'))
                if rec['runtime_errors']: rec['captured'] = False
            (directory / 'result.json').write_text(json.dumps(rec, indent=2) + '\n')
            print(json.dumps(dict(tag=rec['tag'], candidate='captured' if rec['captured'] else 'failed')), flush=True)
        return rec

    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        rows = list(pool.map(candidate, records))
    (a.out / 'results.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),
        original_identity=source['identity'], results=rows), indent=2) + '\n')
    return int(any(not r['captured'] for r in rows))


if __name__ == '__main__': raise SystemExit(main())
