#!/usr/bin/env python3
"""Final-candidate CD captures for independent original-disc visual review.

Capture is not a fidelity pass. Keep the original/candidate identities, actual
camera/scroll observations and explicit cheat prerequisites beside each image.
No completion, collectible, object state or mechanics outcome is written.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path

from run_stage_pack_audit import errors, manifest, select_stages
from run_stage_pack_encounters import Native, u16
from run_sonic_cd_adaptive import diagnostics


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'bank', 'out'):
        p.add_argument('--' + key, required=True, type=lambda s: Path(s).resolve())
    p.add_argument('--stages', default='all')
    p.add_argument('--characters', default='1')
    p.add_argument('--location', choices=('start', 'middle'), default='start')
    p.add_argument('--workers', type=int, default=2)
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    a.kind = 'mechanics'; a.god = True; a.revive_player = True; a.target_fps = 120
    stages = select_stages(manifest(a.bank), a.stages)
    cases = [(s, c) for s in stages for c in map(int, a.characters.split(','))]

    def probe(pair):
        stage, character = pair
        fixture = argparse.Namespace(**vars(a)); fixture.character = character; fixture.mighty = character in (4, 5)
        case = a.out / f"{stage['stage']}-c{character}"
        rec = dict(stage=stage['stage'], character=character, captured=False, visual_review='unreviewed',
                   location=a.location, bypasses=['stage selection', 'player control freeze/protection/revival'])
        run = None
        try:
            start = None
            if a.location == 'middle':
                x = (stage['bounds'][0] + stage['bounds'][1]) // 2
                placement = min(stage['objects'], key=lambda o: abs(o['x'] - x))
                start = (max(160, x), max(96, placement['y'] - 48), 0)
                rec['checkpoint_position_prerequisite'] = start[:2]
            run = Native(fixture, stage['stage'], start, case, 0)
            run.write(0xb02e, b'\x81'); run.write(0xb018, bytes(6)); run.step(32)
            for label, width, height in (('native', 960, 720), ('wide', 2560, 720)):
                run.cmd('video_configure', mode='fit', window_width=width, window_height=height)
                run.step(8)
                ram, frame = run.snapshot(label)
                rec[label] = dict(frame=frame, camera=[u16(ram, x) for x in (0xee80, 0xee84)],
                    player=[u16(ram, x) for x in (0xb010, 0xb014)], routine=ram[0xb005],
                    level_frame=u16(ram, 0xfe04), bg_y=u16(ram, 0xee90), diagnostics=diagnostics(run))
            rec['captured'] = True
        except Exception as exc:
            rec['error'] = repr(exc)
        finally:
            if run:
                run.close(); rec['errors'] = errors((case / 'run.log').read_text(errors='replace'))
                if rec['errors']: rec['captured'] = False
            case.mkdir(parents=True, exist_ok=True)
            (case / 'result.json').write_text(json.dumps(rec, indent=2) + '\n')
            print(json.dumps({k: rec[k] for k in ('stage', 'character', 'captured', 'visual_review')}), flush=True)
        return rec

    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        rows = list(pool.map(probe, cases))
    (a.out / 'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(), results=rows), indent=2) + '\n')
    return int(any(not r['captured'] for r in rows))


if __name__ == '__main__': raise SystemExit(main())
