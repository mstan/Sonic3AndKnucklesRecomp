#!/usr/bin/env python3
"""Cheat-assisted, private-ROM stage survey. No ROM data is published.

Survey points cover every placed-object coordinate in the bank, including
vertical branches, plus regular horizontal samples. The donor's restart-at-
checkpoint audit hook visits them with isolated settings/saves and dummy
audio. Active-object observations are coverage evidence, NOT proof of enemy
interactions, correct pixels/collision, boss defeats or successful exits.
Those require separate interaction probes.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

BOOT = ('WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\n'
        'WAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 0C\n')
NAMES = {0x20: 'Green Hill', 0x21: 'Labyrinth', 0x22: 'Marble',
         0x23: 'Star Light', 0x24: 'Spring Yard', 0x25: 'Scrap Brain',
         0x28: 'Emerald Hill', 0x29: 'Chemical Plant', 0x2a: 'Aquatic Ruin',
         0x2b: 'Casino Night', 0x2c: 'Hill Top', 0x2d: 'Mystic Cave',
         0x2e: 'Oil Ocean', 0x2f: 'Metropolis', 0x30: 'Sky Chase',
         0x31: 'Wing Fortress', 0x32: 'Death Egg',0x34:'Sunny Shores',0x35:'Misty Maze',
         0x36:'Dark Fortress',0x37:'Starry Night',0x38:'City Outskirts',0x39:'Metallic Base'}


def manifest(bank):
    data = bank.read_bytes()
    if data[:4]==b'SCDP':
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/trilogy'))
        from scd_resource import validate_pack
        offset,size=validate_pack(bank)['LEVELS'];levels=data[offset:offset+size];result=[]
        names=('Palmtree Panic','Collision Chaos','Tidal Tempest','Quartz Quadrant','Wacky Workbench','Stardust Speedway','Metallic Madness')
        periods=('Present','Past','Good Future','Bad Future')
        for i in range(struct.unpack_from('>H',levels,6)[0]):
            za,flags,offset,size=struct.unpack_from('>HHII',levels,16+i*12)
            if flags:continue
            blob=levels[offset:offset+size];word=lambda p:int.from_bytes(blob[p:p+4],'big')
            ptr,table=word(0x1C)-0x480000,word(0x34)-0x480000;placements=[]
            while True:
                x,y,oid,sub=struct.unpack_from('>HHBB',blob,ptr);ptr+=6
                if x==0xFFFF:break
                placements.append(dict(index=len(placements),x=x,y=y&0xFFF,flags=y&0xF000,id=oid,subtype=sub,code=word(table+oid*4)))
            zone,act=za>>8,za&255
            result.append(dict(stage=f'{za:04x}',name=f'{names[(zone-0x40)//4]} {act+1} {periods[(zone-0x40)&3]}',
                bounds=list(struct.unpack_from('>4H',blob,0xC)),start=list(struct.unpack_from('>2H',blob,0x14)),objects=placements))
        return result
    def u32(p): return struct.unpack_from('>I', data, p)[0]
    stages = []
    for z in range(0x20, 0x40):
        for act in range(4):
            e = 0x40 + ((z - 0x20) * 4 + act) * 0x40
            if not u32(e):
                continue
            bounds = list(struct.unpack_from('>4H', data, e + 0xc))
            start = list(struct.unpack_from('>2H', data, e + 0x14))
            ptr, table = u32(e + 0x1c) - 0x400000, u32(e + 0x34) - 0x400000
            objects = []
            while True:
                x, y, oid, sub = struct.unpack_from('>HHBB', data, ptr)
                ptr += 6
                if x == 0xffff:
                    break
                objects.append(dict(index=len(objects), x=x, y=y & 0xfff,
                                    flags=y & 0xf000, id=oid, subtype=sub,
                                    code=u32(table + oid * 4)))
            name = NAMES.get(z, f'Zone {z:02x}') + f' {act + 1}'
            if z == 0x21 and act == 3: name = 'Scrap Brain 3'
            if z == 0x25 and act == 2: name = 'Final Zone'
            if z == 0x35 and act == 3: name = 'Metallic Base 3'
            if z == 0x39 and act == 2: name = 'Final Fight'
            stages.append(dict(stage=f'{z:02x}{act:02x}', name=name,
                               bounds=bounds, start=start, objects=objects))
    return stages


def points(stage):
    # Greedy cover of actual object coordinates: a point is always near one
    # uncovered placement. Half-screen spacing also surveys empty corridors.
    objects = stage['objects']
    remaining = set(range(len(objects)))
    result = []
    while remaining:
        i = min(remaining)
        o = objects[i]
        x, y = max(0xa0, o['x'] + 0x20), max(0x60, o['y'] - 0x20)
        covered = sorted({i} | {j for j in remaining
                   if -0x90 <= objects[j]['x'] - x < 0x90
                   and -0x50 <= objects[j]['y'] - y < 0x80})
        result.append(dict(x=x, y=y, placements=covered))
        remaining.difference_update(covered)
    for x in range(0xa0, stage['bounds'][1] + 0x140, 0x100):
        nearest = min(objects, key=lambda o: abs(o['x'] - x)) if objects else {'y': stage['start'][1]}
        y = max(0x60, nearest['y'] - 0x20)
        if not any(abs(p['x'] - x) < 0x90 and abs(p['y'] - y) < 0x60 for p in result):
            result.append(dict(x=x, y=y, placements=[]))
    return sorted(result, key=lambda p: (p['x'], p['y']))


def runtime_files(exe, bank, runtime, debug_port=None, mighty=False):
    runtime.mkdir(parents=True, exist_ok=True)
    (runtime / 'provenance.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        bank_sha256=hashlib.sha256(bank.read_bytes()).hexdigest()),indent=2))
    for file in (exe, exe.parent / 'SDL2.dll'):
        shutil.copy2(file, runtime / file.name)
    megamix=bank.read_bytes()[-12:-8]==b'SPK2'
    (runtime / 'sonic3k-stage-pack.ini').write_text(f'enabled=1\nbank={bank.as_posix()}\nmegamix={int(megamix)}\nmighty={int(mighty)}\n')
    # TCP/script inputs are additive. Ignore physical keyboards/controllers
    # in unattended runs so a live device cannot change an audit fixture.
    (runtime / 'settings.ini').write_text(''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,5)))
    (runtime / 'debug.ini').write_text('[debug]\nenabled=0\n' if debug_port is None
                                    else f'[debug]\nenabled=1\nport={debug_port}\n')


def errors(log):
    bad = [line for line in log.splitlines() if any(s in line.lower() for s in
           ('unported spawned', 'dispatch miss', 'unknown pc', 'fatal', 'assertion failed',
            'unknown native', 'jsr stack mismatch', 'crash report  frame=', 'reason: watchdog:'))]
    for n in re.findall(r'(\d+) unique true-miss addrs', log):
        if int(n): bad.append(f'{n} true dispatch misses')
    return bad


def survey(args):
    stages = manifest(args.bank)
    if args.stages != 'all': stages = [s for s in stages if s['stage'] in args.stages.split(',')]
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'manifest.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        bank_sha256=hashlib.sha256(args.bank.read_bytes()).hexdigest(),
        stages=[dict(**s, points=points(s)) for s in stages]), indent=2))

    def run(stage):
        case = args.out / stage['stage']; case.mkdir(exist_ok=True)
        pts = points(stage)
        batches = []
        for batch_id, offset in enumerate(range(0, len(pts), 240)):
            batch = case / f'batch-{batch_id}'; batch.mkdir(exist_ok=True)
            runtime = batch / 'runtime'
            runtime_files(args.exe, args.bank, runtime,40000+int(stage['stage'],16))
            point_file = batch / 'points.txt'
            subset = pts[offset:offset + 240]
            point_file.write_text(''.join(f"{p['x']:X} {p['y']:X}\n" for p in subset))
            script = batch / 'input.txt'; script.write_text(BOOT + 'WAIT 250000\nEXIT\n')
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       SDL_RENDER_DRIVER='software', TRILOGY_GOD='1',
                       TRILOGY_ZONE=stage['stage'], TRILOGY_CHAR='1',
                       TRILOGY_AUDIT=str(point_file), TRILOGY_AUDIT_OUT=str(batch / 'point'),
                       TRILOGY_AUDIT_FREEZE='1')
            env['TRILOGY_AUDIT_START_FRAME']='1800'
            started = time.monotonic()
            with (batch / 'run.log').open('w') as log:
                try:
                    proc = subprocess.run([str(runtime / args.exe.name), str(args.rom),
                        '--no-launcher', '--turbo', '--max-frames', '250000',
                        '--widescreen', 'off', '--input-script', str(script)],
                        cwd=runtime, env=env, stdout=log, stderr=log, timeout=1800)
                    code = proc.returncode
                except subprocess.TimeoutExpired: code = 'timeout'
            text = (batch / 'run.log').read_text(errors='replace')
            observed = []
            for i, p in enumerate(subset):
                path = batch / f'point_{i:02d}.txt'
                record = dict(point=offset+i, **p, captured=False)
                if path.exists():
                    lines = path.read_text().splitlines()
                    record.update(captured=True, header=lines[0], active=[])
                    for line in lines[1:]:
                        if line.startswith('#'):
                            record['state'] = line
                            record['stage_matches'] = f"stage {stage['stage'].upper()} " in line
                            continue
                        row = line.split()
                        record['active'].append(dict(slot=int(row[0]), code=int(row[1],16),
                            respawn=int(row[2],16), x=int(row[3],16), y=int(row[4],16),
                            frame=int(row[5],16), render=int(row[6],16), routine=int(row[7],16),
                            subtype=int(row[8],16), status=int(row[9],16)))
                observed.append(record)
            batches.append(dict(exit=code, seconds=round(time.monotonic()-started,1),
                                errors=errors(text), points=observed,
                                completed=f'audit: {len(subset)} points' in text))
            for file in (runtime / args.exe.name, runtime / 'SDL2.dll'): file.unlink()
        captured = sum(p['captured'] for b in batches for p in b['points'])
        observed = {o['respawn'] - 0xeb00 for b in batches for p in b['points']
                    for o in p.get('active',[]) if 0xeb00 <= o['respawn'] < 0xee00}
        record = dict(stage=stage['stage'], name=stage['name'], points=len(pts),
                      captured=captured, placements=len(stage['objects']),
                      active_placement_indices=sorted(observed),
                      unobserved_placement_indices=sorted(set(range(len(stage['objects']))) - observed),
                      batches=batches)
        record['survey_complete'] = (captured == len(pts)
            and all(p.get('stage_matches',False) for b in batches for p in b['points'])
            and all(b['completed'] and b['exit']==0 and not b['errors'] for b in batches))
        (case / 'result.json').write_text(json.dumps(record,indent=2))
        print(json.dumps({k:v for k,v in record.items() if k not in ('batches','active_placement_indices','unobserved_placement_indices')}), flush=True)
        return record
    with ThreadPoolExecutor(max_workers=args.workers) as pool: results=list(pool.map(run, stages))
    (args.out / 'results.json').write_text(json.dumps(results,indent=2))
    return int(not all(r['survey_complete'] for r in results))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'): ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all'); ap.add_argument('--workers',type=int,default=2)
    args=ap.parse_args()
    return survey(args)


if __name__ == '__main__': raise SystemExit(main())
