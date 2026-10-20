#!/usr/bin/env python3
"""Exercise source-table current, bounce, bar, pole and conveyor locations.

Plans come from the supplied converted owned-disc tables. Player position,
posture, velocity and camera travel are declared prerequisites. Controllers,
switches, currents, launch velocities, catches and releases are never written.
Switch-gated/inverted currents require a separate actual switch interaction.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import sys
from run_stage_pack_audit import errors, manifest
from run_stage_pack_encounters import Native, camera_at, u16, u32


def source_tables(bank):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/trilogy'))
    from scd_resource import validate_pack
    at, size = validate_pack(bank)['LEVELS']; raw = bank.read_bytes()[at:at+size]
    result = {}
    for i in range(u16(raw, 6)):
        za, flags, at, size = struct.unpack_from('>HHII', raw, 16+i*12)
        if not flags: result[f'{za:04x}'] = raw[at:at+size]
    return result


def blocks(blob, wanted):
    at = u32(blob, 0xe8)-0x480000
    if at < 0: return []
    count, width, height = struct.unpack_from('>3H', blob, at)
    grid = blob[at+6:at+6+width*height]; chunks = at+6+((width*height+1)&~1)
    result = []
    for cy in range(height):
        for cx in range(width):
            chunk = grid[cy*width+cx]&127
            if not 0 < chunk <= count: continue
            for n in range(256):
                word = u16(blob, chunks+(chunk-1)*512+n*2)
                if word&0x7ff in wanted:
                    result.append(dict(x=cx*256+(n%16)*16+8, y=cy*256+(n//16)*16+8, word=word))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'bank', 'out'):
        p.add_argument('--'+name, required=True, type=lambda v: Path(v).resolve())
    p.add_argument('--checks', default='currents,bounce,bar,pole,belt')
    p.add_argument('--workers', type=int, default=2)
    p.add_argument('--retry-results', type=lambda v:Path(v).resolve())
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    a.kind = 'mechanics'; a.god = True; a.character = 1; a.target_fps = 120
    tables = source_tables(a.bank); stages = {s['stage']:s for s in manifest(a.bank)}
    plan = []; checks = a.checks.split(',')
    for stage, blob in tables.items():
        zone, act = int(stage[:2],16), int(stage[2:],16)
        if 'currents' in checks and 0x48 <= zone <= 0x4b and act < 2:
            at = u32(blob, 0x38)-0x480000
            at = u32(blob, at+158*16+8)-0x480000
            seen = set()
            while at >= 0 and not blob[at]&0x80:
                x1,y1,x2,y2,vx,vy,kind,switch = struct.unpack_from('>6H2B',blob,at); at += 14
                key = (vx,vy,kind)
                if kind != 1 and key not in seen and x2-x1 >= 24 and y2-y1 >= 24:
                    plan.append(dict(stage=stage, kind='currents', x=(x1+x2)//2, y=(y1+y2)//2,
                        rect=[x1,y1,x2,y2], velocity=[vx,vy], current_type=kind, switch=switch))
                    seen.add(key)
        if 0x50 <= zone <= 0x53 and act < 2:
            for kind, block in (('bounce',543), ('bar',345), ('pole',259)):
                if kind not in checks: continue
                if (kind == 'pole') != (act == 1): continue
                choices = blocks(blob, {block})
                choices = [v for v in choices if v['x'] > 256 and v['y'] > 64]
                if choices:
                    v = min(choices, key=lambda v: abs(v['x']-stages[stage]['start'][0])+abs(v['y']-stages[stage]['start'][1]))
                    plan.append(dict(stage=stage, kind=kind, **v))
        if 'belt' in checks and 0x58 <= zone <= 0x5b and act == 1:
            choices = blocks(blob, set(range(270,277))); seen = set()
            for v in sorted(choices, key=lambda v:v['x']):
                sign = (-1 if v['word']&0x800 else 1) * (-1 if v['word']&0x7ff==276 else 1)
                if sign in seen or v['x'] < 256 or v['y'] < 64: continue
                seen.add(sign); plan.append(dict(stage=stage, kind='belt', direction=sign, **v))
    if a.retry_results:
        prior=json.loads((a.retry_results/'results.json').read_text())['results']
        keys={(r['stage'],r['kind'],r['x'],r['y']) for r in prior if r['status']!='passed'}
        plan=[r for r in plan if (r['stage'],r['kind'],r['x'],r['y']) in keys]
    (a.out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')

    def test(index_row):
        index, row = index_row; row = dict(row, status='unverified', samples=[])
        directory = a.out/f"{index:03}-{row['stage']}-{row['kind']}"; run = None
        row['bypasses'] = ['checkpoint-position entry', 'player posture/position/velocity', 'camera travel', 'hurt protection']
        try:
            x, y, kind = row['x'], row['y'], row['kind']
            if kind in ('bounce','belt'): y -= 21
            if kind == 'bar': y += 24
            run = Native(a,row['stage'],(x,y,0),directory,0)
            camera_at(run,x,y)
            ram,_ = run.snapshot(); began = run.cmd('frame_info')['current_frame']
            run.player(ram,x,y,grounded=kind in ('bounce','belt'))
            for attempt in range(50 if kind in ('bounce','currents') else 12):
                run.step(2);ram,frame = run.snapshot()
                sample = dict(frame=frame, player=[u16(ram,0xb010),u16(ram,0xb014)],
                    velocity=[u16(ram,0xb018),u16(ram,0xb01a)],control=ram[0xb02e],
                    routine=ram[0xb005],anim=ram[0xb020],status=ram[0xb02a])
                row['samples'].append(sample)
                if kind=='bounce' and attempt < 2:
                    # Command polling can miss a short launch velocity at 120
                    # FPS. Inspect the completed-frame ring from before the
                    # zero-velocity posture write instead of widening the
                    # expected launch into a generic upward-motion assertion.
                    info=run.cmd('frame_info')
                    for f in range(max(began,info['oldest_frame']),min(info['current_frame'],began+32)):
                        state=bytes.fromhex(run.cmd('get_frame',frame=f,include='wram')['wram'])
                        if u16(state,0xb01a)==0xea00 and state[0xb02a]&6==6:
                            row['exact_launch_frame']=f
                good = (kind=='currents' and sample['velocity']==row['velocity'] and sample['anim']==15
                    or kind=='bounce' and 'exact_launch_frame' in row
                    or kind=='bar' and sample['control']==1 and sample['anim']==0x14
                    or kind=='pole' and sample['control']==1 and sample['anim']==2
                    or kind=='belt' and (sample['player'][0]-x)*row['direction']>0)
                if good: break
            else: raise AssertionError('Source-location behavior not observed')
            run.snapshot('engaged')
            if kind in ('bar','pole'):
                first = sample['player']; run.cmd('set_input',keys='08');run.step(8)
                ram,_=run.snapshot();row['held_motion']=[u16(ram,0xb010),u16(ram,0xb014)]
                if row['held_motion']==first: raise AssertionError('Caught player did not move')
                pressed=run.cmd('frame_info')['current_frame']
                run.cmd('set_input',keys='40');run.step(24);run.cmd('set_input',keys='00')
                ram,_=run.snapshot('released');row['release_control']=ram[0xb02e]
                info=run.cmd('frame_info');row['release_frames']=[]
                for f in range(max(pressed,info['oldest_frame']),info['current_frame']):
                    state=bytes.fromhex(run.cmd('get_frame',frame=f,include='wram')['wram'])
                    if not state[0xb02e]&1:
                        row['release_frames'].append(dict(frame=f,control=state[0xb02e],
                            velocity=state[0xb018:0xb01c].hex()))
                if not row['release_frames']: raise AssertionError('Jump input did not release player')
            if kind=='currents':
                # Keep the actual floating animation until the current's exit
                # hook changes it; the general posture helper would reset it.
                run.write(0xb010,struct.pack('>I',0x50<<16));run.write(0xb014,struct.pack('>I',0x80<<16))
                run.write(0xb018,bytes(6));run.step(4);ram,_=run.snapshot('released')
                row['exit_anim']=ram[0xb020]
                if row['exit_anim'] not in (0,5): raise AssertionError('Current did not release floating pose')
            row['status']='passed'
        except Exception as exc: row.update(status='failed',error=repr(exc))
        finally:
            if run:
                run.close(); row['runtime_errors']=errors((directory/'run.log').read_text(errors='replace'))
                if row['runtime_errors']: row['status']='failed'
            directory.mkdir(parents=True,exist_ok=True);(directory/'result.json').write_text(json.dumps(row,indent=2)+'\n')
            print(json.dumps({k:v for k,v in row.items() if k not in ('samples','runtime_errors')}),flush=True)
        return row
    with ThreadPoolExecutor(max_workers=a.workers) as pool: rows=list(pool.map(test,enumerate(plan)))
    (a.out/'results.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
        bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),results=rows),indent=2)+'\n')
    return int(not rows or any(r['status']!='passed' for r in rows))


if __name__=='__main__':raise SystemExit(main())
