#!/usr/bin/env python3
"""Actual CD powerup pickup/expiry and last-life Game Over with music present.

Powerups come from source-placed monitors, never flags or timer writes. Position,
attack posture and protection bypass navigation. The Game Over case supplies
one remaining life and advances only the clock; death and menu return run.
Emulated stereo is captured through dummy output devices.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import copy
import hashlib
import json
from pathlib import Path
import re
from run_stage_pack_audit import errors,manifest
from run_stage_pack_encounters import Native,objects,u16
from run_sonic_cd_pause_audio import energy


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','bank','music','out'):
        p.add_argument('--'+name,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--workers',type=int,default=2)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    a.kind='mechanics';a.character=1;a.god=True;a.target_fps=240
    a.env={'TRILOGY_AUDIOLOG':'1'}
    stages={s['stage']:s for s in manifest(a.bank)}
    plan=[(s,sub) for s in ('4000','4100') for sub in (4,8)]+[('4000',None)]
    def test(item):
        stage,sub=item;label='game-over' if sub is None else 'shoes' if sub==4 else 'invincibility'
        directory=a.out/(stage+'-'+label);run=None;args=copy.copy(a)
        row=dict(stage=stage,event=label,status='failed',bypasses=[],observations=[])
        try:
            if sub is None:
                args.god=False;run=Native(args,stage,None,directory,0)
                row['bypasses']=['stage selection','one-life prerequisite','clock advanced to 9:59:59']
                run.cmd('audio_wav',action='start',path=str(directory/'event.wav'))
                run.write(0xfe12,b'\x01');run.write(0xfe22,bytes((0,9,59,59)))
                died=False;over=False;menu=False
                for i in range(200):
                    run.step(12);ram,frame=run.snapshot()
                    died|=ram[0xb005]>=6;over|=ram[0xfe12]==0
                    row['observations'].append(dict(frame=frame,mode=ram[0xf600],routine=ram[0xb005],lives=ram[0xfe12]))
                    if over and not row.get('game_over_frame'):
                        run.snapshot('game-over');row['game_over_frame']=frame
                    if died and over and ram[0xf600] in (4,0x4c):
                        run.snapshot('menu-return');menu=True;break
                run.cmd('audio_wav',action='stop')
                text=(directory/'run.log').read_text(errors='replace')
                row.update(death_observed=died,last_life_lost=over,actual_menu_return=menu,
                    tracks=list(map(int,re.findall(r'cd play track (\d+) loop',text))))
                row['status']='passed' if died and over and menu and 31 in row['tracks'] else 'failed'
            else:
                box=next(o for o in stages[stage]['objects'] if o['id']==0x19 and o['subtype']==sub)
                row['source']=box;row['bypasses']=['source-derived checkpoint-position entry','player contact attack posture','hurt protection']
                run=Native(args,stage,(box['x']-64,max(80,box['y']-64),0),directory,0)
                mask=4 if sub==4 else 2;expected=29 if sub==4 else 30
                for attempt in range(24):
                    ram,frame=run.snapshot()
                    if ram[0xb02b]&mask:break
                    boxes=[o for o in objects(ram) if o['respawn']==0xeb00+box['index'] and o['routine']<4]
                    if boxes:
                        obj=boxes[0];run.player(ram,obj['x'],max(0,obj['y']-24),attack=True)
                        run.write(0xb02e,b'\x01');run.step(4)
                    run.step(30)
                else:raise AssertionError('Actual monitor did not grant powerup')
                row['pickup_status']=ram[0xb02b];row['pickup_frame']=frame;run.snapshot('pickup')
                text=(directory/'run.log').read_text(errors='replace')
                row['pickup_tracks']=list(map(int,re.findall(r'cd play track (\d+) loop',text)))
                if expected not in row['pickup_tracks']:raise AssertionError('Owned CD powerup track not requested')
                run.player(ram,box['x']-64,max(80,box['y']-96),freeze=True)
                run.cmd('audio_wav',action='start',path=str(directory/'powerup.wav'));run.step(180)
                run.cmd('audio_wav',action='stop');row['powerup_audio']=energy(directory/'powerup.wav')
                for n in range(70):
                    run.step(24);ram,frame=run.snapshot()
                    if not ram[0xb02b]&mask:break
                else:raise AssertionError('Powerup did not expire naturally')
                row['expiry_frame']=frame;row['expired_status']=ram[0xb02b];run.snapshot('expired')
                run.cmd('audio_wav',action='start',path=str(directory/'restored.wav'));run.step(180)
                run.cmd('audio_wav',action='stop');row['restored_audio']=energy(directory/'restored.wav')
                text=(directory/'run.log').read_text(errors='replace')
                row['tracks']=list(map(int,re.findall(r'cd play track (\d+) loop',text)))
                restored=stage=='4100' or row['tracks'][-1]==3
                row['status']='passed' if (restored and min(row['powerup_audio']['rms'])>50
                    and min(row['restored_audio']['rms'])>50) else 'failed'
        except Exception as exc:row['error']=repr(exc)
        finally:
            if run:
                # Close any segment left by a failed assertion.
                try:run.cmd('audio_wav',action='stop')
                except Exception:pass
                run.close();row['runtime_errors']=errors((directory/'run.log').read_text(errors='replace'))
                if row['runtime_errors']:row['status']='failed'
            directory.mkdir(parents=True,exist_ok=True);(directory/'result.json').write_text(json.dumps(row,indent=2)+'\n')
            print(json.dumps({k:v for k,v in row.items() if k!='observations'}),flush=True)
        return row
    with ThreadPoolExecutor(max_workers=a.workers) as pool:rows=list(pool.map(test,plan))
    (a.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),results=rows),indent=2)+'\n')
    return int(any(r['status']!='passed' for r in rows))


if __name__=='__main__':raise SystemExit(main())
