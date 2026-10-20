#!/usr/bin/env python3
"""Touch real MMZ shrink/grow beams with each supported character.

Beam placement, spawning, contact, radii, rings and size restoration are
observed. Player positioning, camera travel and stationary control are explicit
prerequisites; no emitter, beam, mini flag or checkpoint flag is written.
Captures need an independent visual review; radii alone do not prove drawing.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import struct
from pathlib import Path
from run_stage_pack_audit import errors, manifest
from run_stage_pack_encounters import Native, camera_at, objects, u16


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'bank', 'out'):
        p.add_argument('--' + name, required=True, type=lambda v: Path(v).resolve())
    p.add_argument('--characters', default='0,1,2,3,4,5,6,7')
    p.add_argument('--workers', type=int, default=2)
    p.add_argument('--video-mode', choices=('fit','off'), default='fit')
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    a.kind = 'mechanics'; a.god = True; a.target_fps = 120
    stage = next(s for s in manifest(a.bank) if s['stage'] == '5801')
    emitters = [o for o in stage['objects'] if o['id'] == 0x1d]
    shrink = next(o for o in emitters if o['subtype'] == 0)
    grow = min((o for o in emitters if o['subtype'] == 1),
               key=lambda o: abs(o['x']-shrink['x']) + abs(o['y']-shrink['y']))

    def test(character):
        import copy
        args = copy.copy(a); args.character = character; args.mighty = character in (4, 5)
        directory = a.out / str(character); run = None
        row = dict(character=character, status='unverified', emitters=[shrink, grow], samples=[],
                   bypasses=['checkpoint-position entry', 'player position/velocity/control', 'camera travel'],
                   visual_review='unreviewed')
        try:
            run = Native(args, '5801', (shrink['x']-48, shrink['y']+40, 0), directory, 0)
            run.cmd('video_configure', mode=a.video_mode, window_width=960, window_height=720)
            ram, _ = run.snapshot(); row['normal_radii'] = list(ram[0xb044:0xb046])
            ptr=u16(ram,0xe662);row['selected_character']=ram[ptr+2]>>4
            if row['selected_character']!=character:raise AssertionError('Requested character was not selected')
            rings = u16(ram, 0xfe20)
            for emitter, small in ((shrink, True), (grow, False)):
                x, y = emitter['x'], emitter['y']+40
                camera_at(run, x, y)
                run.write(0xb010, struct.pack('>I', x << 16))
                run.write(0xb014, struct.pack('>I', y << 16))
                run.write(0xb018, bytes(6)); run.write(0xb02e, b'\x81')
                expected = [10, 5] if small else row['normal_radii']
                for n in range(100):
                    run.step(2); ram, frame = run.snapshot()
                    sample = dict(frame=frame, small_expected=small,
                        radii=list(ram[0xb01e:0xb020]), defaults=list(ram[0xb044:0xb046]),
                        routine=ram[0xb005], rings=u16(ram, 0xfe20),
                        beam_objects=[o for o in objects(ram) if o['code']==emitter['code']])
                    row['samples'].append(sample)
                    if sample['defaults'] == expected:
                        if sample['rings'] < rings: raise AssertionError('Ray lost rings')
                        # The real emitter corridor also contains rings; the
                        # position prerequisite can collect one before contact.
                        rings = sample['rings']
                        run.snapshot('small' if small else 'grown')
                        break
                else: raise AssertionError('Actual beam never produced the requested size')
            row['status'] = 'passed'
        except Exception as exc: row.update(status='failed', error=repr(exc))
        finally:
            if run:
                run.close(); row['runtime_errors'] = errors((directory / 'run.log').read_text(errors='replace'))
                if row['runtime_errors']: row['status'] = 'failed'
            directory.mkdir(parents=True, exist_ok=True)
            (directory / 'result.json').write_text(json.dumps(row, indent=2)+'\n')
            print(json.dumps(dict(character=character, status=row['status'], error=row.get('error'))), flush=True)
        return row
    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        rows = list(pool.map(test, [int(c) for c in a.characters.split(',')]))
    (a.out / 'results.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
        bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(), results=rows), indent=2)+'\n')
    return int(any(row['status'] != 'passed' for row in rows))


if __name__ == '__main__': raise SystemExit(main())
