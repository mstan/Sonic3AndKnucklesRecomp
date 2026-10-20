#!/usr/bin/env python3
"""Press actual TTZ buttons to enable or reverse source-table currents.

Player/camera position and normal posture are prerequisites. No switch,
current, door or object-state flag is written. Each type/period starts fresh.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
from run_stage_pack_audit import errors,manifest
from run_stage_pack_encounters import Native,objects,camera_at,u16,u32
from run_sonic_cd_gimmicks import source_tables


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','bank','out'):
        p.add_argument('--'+name,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--workers',type=int,default=2)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    a.kind='mechanics';a.character=1;a.god=True;a.target_fps=120
    stages={s['stage']:s for s in manifest(a.bank)};plan=[]
    for stage,b in source_tables(a.bank).items():
        if not 0x48<=int(stage[:2],16)<=0x4b or stage[2:]=='02':continue
        at=u32(b,0x38)-0x480000;at=u32(b,at+158*16+8)-0x480000
        if at<0:continue
        seen=set()
        while not b[at]&0x80:
            x1,y1,x2,y2,vx,vy,kind,switch=struct.unpack_from('>6H2B',b,at);at+=14
            buttons=[o for o in stages[stage]['objects'] if o['id']==0x27 and o['subtype']&31==switch]
            if kind not in (1,2) or kind in seen or not buttons:continue
            button=min(buttons,key=lambda o:abs(o['x']-(x1+x2)//2)+abs(o['y']-(y1+y2)//2))
            plan.append(dict(stage=stage,kind=kind,switch=switch,rect=[x1,y1,x2,y2],velocity=[vx,vy],button=button));seen.add(kind)
    (a.out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    def test(row):
        row=dict(row,status='failed',samples=[],bypasses=['source current/button entry','player position/posture','camera travel','hurt protection'])
        directory=a.out/(row['stage']+'-'+str(row['kind']));run=None
        try:
            x1,y1,x2,y2=row['rect'];x,y=(x1+x2)//2,(y1+y2)//2
            run=Native(a,row['stage'],(x,y,0),directory,0)
            # Completed frames can contain the table velocity or the native
            # underwater gravity update (+16 in 8.8), depending on the period.
            def matches(sample, velocity):
                return (sample['anim']==15 and sample['velocity'][0]==velocity[0]
                    and sample['velocity'][1] in (velocity[1],(velocity[1]+16)&65535))
            def current(expected):
                camera_at(run,x,y);ram,_=run.snapshot();run.player(ram,x,y)
                observed=[]
                for _ in range(8):
                    run.step(2);ram,frame=run.snapshot()
                    observed.append(dict(frame=frame,velocity=[u16(ram,0xb018),u16(ram,0xb01a)],anim=ram[0xb020],control=ram[0xb02e]))
                    if expected is not None and matches(observed[-1],expected):break
                return observed
            row['before']=current(None if row['kind']==1 else row['velocity'])
            active=lambda s:matches(s,row['velocity'])
            if row['kind']==1 and any(active(s) for s in row['before']):raise AssertionError('Unpressed gated current was already active')
            if row['kind']==2 and not any(active(s) for s in row['before']):raise AssertionError('Initial current direction not observed')
            button=row['button'];camera_at(run,button['x'],button['y'])
            pressed=False
            for attempt in range(24):
                ram,_=run.snapshot()
                buttons=[o for o in objects(ram) if o['respawn']==0xeb00+button['index'] and o['code']==button['code']]
                if buttons:
                    live=buttons[0]
                    if live['frame']==1 and ram[live['addr']+0x3f]:pressed=True;row['pressed_button']=live;break
                    run.player(ram,live['x'],max(0,live['y']-28),grounded=True)
                else:
                    run.player(ram,button['x'],max(0,button['y']-28),grounded=True)
                run.step(4)
            if not pressed:raise AssertionError('Actual button contact was not observed')
            run.snapshot('pressed')
            expected=row['velocity'] if row['kind']==1 else [(-v)&65535 for v in row['velocity']]
            row['after']=current(expected);row['expected_after']=expected
            if not any(matches(s,expected) for s in row['after']):raise AssertionError('Button did not change current behavior')
            run.snapshot('current-changed');row['status']='passed'
        except Exception as exc:row['error']=repr(exc)
        finally:
            if run:
                run.close();row['runtime_errors']=errors((directory/'run.log').read_text(errors='replace'))
                if row['runtime_errors']:row['status']='failed'
            directory.mkdir(parents=True,exist_ok=True);(directory/'result.json').write_text(json.dumps(row,indent=2)+'\n')
            print(json.dumps({k:v for k,v in row.items() if k not in ('samples','before','after')}),flush=True)
        return row
    with ThreadPoolExecutor(max_workers=a.workers) as pool:rows=list(pool.map(test,plan))
    (a.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),results=rows),indent=2)+'\n')
    return int(not rows or any(r['status']!='passed' for r in rows))


if __name__=='__main__':raise SystemExit(main())
