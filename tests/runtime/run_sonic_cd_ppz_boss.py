#!/usr/bin/env python3
"""Real PPZ boss damage, escape, capsule, tally and next-round probes.

Entry position, contact attack posture, camera travel and player protection
bypass navigation/attack execution. Boss HP/routines, defeat, escape, capsule,
tally, stage selection and save flags are observed, never injected.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects,verify_next


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='4202,4302')
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='bosses';args.god=True;args.env={'TRILOGY_BOSSLOG':'1'}
    args.out.mkdir(parents=True,exist_ok=False);results=[]
    for stage in manifest(args.content):
        if stage['stage'] not in args.stages.lower().split(','):continue
        egg=next(o for o in stage['objects'] if o['code']==0x7e30a8)
        capsule=next(o for o in stage['objects'] if o['code']==0x7e3054)
        case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],status='unverified',states=[],hits_observed=[],
            bypasses=['checkpoint-position entry','player contact attack posture','hurt/death protection','camera/position travel after observed escape'])
        try:
            run=Native(args,stage['stage'],(egg['x']-0x90,0x200,0),case,0)
            run.snapshot('before');fighting=False
            for n in range(160):
                ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==egg['code']]
                if live:
                    boss=live[0];boss.update(hits=ram[boss['addr']+0x31],flags=ram[boss['addr']+0x32])
                    rec['states'].append(dict(frame=frame,boss=boss))
                    if boss['routine']==4 and boss['flags']&8:
                        fighting=True
                        if boss['hits'] not in rec['hits_observed']:rec['hits_observed'].append(boss['hits'])
                        if boss['collision'] and boss['hp']:
                            run.player(ram,boss['x'],max(0,boss['y']-12),attack=True)
                            run.write(0xb02e,b'\x01')
                    if fighting and boss['routine']==6 and not boss['hp']:
                        rec['defeat']='passed';run.snapshot('defeat');break
                run.step(20)
            if rec.get('defeat')=='passed':
                run.cmd('set_input',keys='00')
                for n in range(15):
                    run.step(60);ram,frame=run.snapshot()
                    live=[o for o in objects(ram) if o['code']==egg['code']]
                    if not live:
                        rec['escape']='passed';run.snapshot('escaped');break
                if rec.get('escape')=='passed':
                    sx,sy=u16(ram,0xb010),u16(ram,0xb014);tx,ty=capsule['x'],capsule['y']-32
                    count=max(1,(abs(tx-sx)+127)//128,(abs(ty-sy)+127)//128)
                    for k in range(1,count+1):
                        ram,frame=run.snapshot();x=sx+(tx-sx)*k//count;y=sy+(ty-sy)*k//count
                        run.player(ram,x,y,freeze=True)
                        camera=struct.pack('>II',max(0,x-160)<<16,max(0,y-96)<<16)
                        run.write(0xee78,camera);run.write(0xee80,camera);run.step(8)
                    for n in range(12):
                        ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==capsule['code']]
                        if any(o['routine']>=4 for o in live):
                            rec['capsule']='passed';run.write(0xb02e,b'\0');run.snapshot('capsule-broken');break
                        run.player(ram,capsule['x'],capsule['y']-12,attack=True);run.write(0xb02e,b'\x01');run.step(10)
                    for n in range(45):
                        run.step(90);ram,frame=run.snapshot()
                        if u16(ram,0xfe10)==0x4400:
                            rec['next']=verify_next(run,'4400');break
            run.snapshot('after');journal=(run.runtime/'sonic3k.srm.campaign').read_bytes();rec['saved_stable']=journal[62]
        except Exception as exc:rec.update(status='failed',error=repr(exc))
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                # Eight-frame game telemetry retains short damage phases
                # that can fall between host TCP observations.
                rec['hit_phases_log']=sorted(set(map(int,re.findall(r'\[boss\] f=\d+ r=04 hits=(\d+) ',log))))
                rec['status']='passed' if not rec.get('error') and not rec['errors'] and all(rec.get(k)=='passed' for k in ('defeat','escape','capsule','next')) and {1,2,3}<=set(rec['hits_observed']+rec['hit_phases_log']) and rec['saved_stable']==50 else 'failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec)
            print(json.dumps({k:v for k,v in rec.items() if k!='states'}),flush=True)
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())
