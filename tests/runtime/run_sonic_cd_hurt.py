#!/usr/bin/env python3
"""Real uncurled enemy contact in each CD round, without hurt immunity.

Source-derived entry/position, ten prerequisite rings and a normal airborne
posture bypass navigation. Ring loss and the native hurt routine are observed;
enemy HP, collision, player hurt/death and success flags are never written.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from run_stage_pack_audit import errors,manifest
from run_stage_pack_encounters import Native,objects,u16
from run_sonic_cd_enemies import FAMILIES


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','bank','out'):
        p.add_argument('--'+name,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--workers',type=int,default=2)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    a.kind='mechanics';a.character=1;a.god=False;a.target_fps=120
    stages=manifest(a.bank);plan=[]
    for rnd in range(7):
        stage=next(s for s in stages if s['stage']==f'{0x40+rnd*4:02x}00')
        target=next(o for o in stage['objects'] if o['id'] in FAMILIES[rnd]
            and FAMILIES[rnd][o['id']] not in ('bigbom','scarab','hotaru','kemusi') and o['subtype']==0)
        plan.append((stage,target,FAMILIES[rnd][target['id']]))
    def test(item):
        stage,target,family=item;directory=a.out/stage['stage'];run=None
        row=dict(stage=stage['stage'],family=family,source=target,status='failed',samples=[],
            bypasses=['checkpoint-position entry','player contact position/posture','ten prerequisite rings'])
        try:
            run=Native(a,stage['stage'],(max(80,target['x']-96),max(80,target['y']-48),0),directory,0)
            run.write(0xfe20,b'\x00\x0a');began=run.cmd('frame_info')['current_frame']
            for attempt in range(16):
                ram,frame=run.snapshot()
                if ram[0xb005]==4 and u16(ram,0xfe20)==0:break
                enemies=[o for o in objects(ram) if o['code']==target['code'] and o['collision']
                    and not o['collision']&0xc0 and o['render']&128]
                if enemies:
                    live=enemies[0];run.player(ram,live['x'],live['y'])
                    run.write(0xb034,b'\0');run.write(0xb02b,b'\0')
                run.step(3)
            info=run.cmd('frame_info')
            for frame in range(max(began,info['oldest_frame']),info['current_frame']):
                ram=bytes.fromhex(run.cmd('get_frame',frame=frame,include='wram')['wram'])
                if ram[0xb005]==4 and u16(ram,0xfe20)==0:
                    row['samples'].append(dict(frame=frame,routine=ram[0xb005],rings=0,
                        velocity=ram[0xb018:0xb01c].hex(),invulnerability=ram[0xb034]))
            run.snapshot('hurt')
            if row['samples']:row['status']='passed'
        except Exception as exc:row['error']=repr(exc)
        finally:
            if run:
                run.close();row['runtime_errors']=errors((directory/'run.log').read_text(errors='replace'))
                if row['runtime_errors']:row['status']='failed'
            directory.mkdir(parents=True,exist_ok=True);(directory/'result.json').write_text(json.dumps(row,indent=2)+'\n')
            print(json.dumps({k:v for k,v in row.items() if k!='samples'}),flush=True)
        return row
    with ThreadPoolExecutor(max_workers=a.workers) as pool:rows=list(pool.map(test,plan))
    (a.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),results=rows),indent=2)+'\n')
    return int(any(r['status']!='passed' for r in rows))


if __name__=='__main__':raise SystemExit(main())
