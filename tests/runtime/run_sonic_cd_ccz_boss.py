#!/usr/bin/env python3
"""Real CCZ lid/platform damage, gates, escape, capsule and next round.

Player position/camera, attack posture and protection bypass route execution.
No boss HP, gate/routine, defeat, capsule or success flags are written.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects,verify_next


def move(run,x,y,attack=False,air=30,grounded=False):
    ram,frame=run.snapshot();run.player(ram,x,y,attack=attack,freeze=not attack,air=air,grounded=grounded)
    camera=struct.pack('>II',max(0,x-160)<<16,max(0,y-96)<<16)
    run.write(0xee78,camera);run.write(0xee80,camera)
    if attack:run.write(0xb02e,b'\x01')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='4602,4702')
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='bosses';args.god=True;args.env={'TRILOGY_BOSSLOG':'1'}
    args.out.mkdir(parents=True,exist_ok=False);results=[]
    for stage in manifest(args.content):
        if stage['stage'] not in args.stages.lower().split(','):continue
        lid=next(o for o in stage['objects'] if o['code']==0x7e32c0)
        capsule=next(o for o in stage['objects'] if o['code']==0x7e3254)
        case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],status='unverified',states=[],bypasses=['checkpoint-position entry','contact attack posture','hurt/death protection','camera/vertical route travel'])
        try:
            run=Native(args,stage['stage'],(lid['x']-64,lid['y']-48,0),case,0);run.snapshot('before')
            for n in range(20):
                ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==lid['code']]
                if live and live[0]['routine']>=4:
                    rec['lid']='passed';run.snapshot('lid-hit');break
                move(run,lid['x'],lid['y']-12,True);run.step(12)
            move(run,0x490,0x500);run.step(30);run.snapshot('table-entry')
            # Ascend the real column in bounded positioning steps; the
            # platform/gates run their normal player-position conditions.
            for y in range(0x480,0x1bf,-0x80):move(run,0x380,y);run.step(8)
            for n in range(120):
                ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==0x7e32c4]
                if not live:break
                boss=live[0];boss.update(hits=ram[boss['addr']+0x41],gates=ram[boss['addr']+0x40])
                rec['states'].append(dict(frame=frame,boss=boss))
                if boss['hits']>=3 and boss['routine']>=8:
                    rec['defeat']='passed';run.snapshot('defeat');move(run,0x380,0xd0);break
                if boss['collision'] and boss['routine']==4:move(run,boss['x'],max(0,boss['y']-12),True)
                run.step(16)
            if rec.get('defeat')=='passed':
                for n in range(12):
                    run.step(45);ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==0x7e32cc]
                    if not live:rec['escape']='passed';run.snapshot('escaped');break
                # Capsule contact is performed only after observed defeat.
                move(run,capsule['x']-64,capsule['y']-48);run.step(30)
                for n in range(15):
                    ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==capsule['code']]
                    if any(o['routine']>=4 for o in live):
                        rec['capsule']='passed';run.write(0xb02e,b'\0');run.snapshot('capsule-broken');break
                    move(run,capsule['x'],capsule['y']-12,True);run.step(12)
                for n in range(45):
                    run.step(90);ram,frame=run.snapshot()
                    if u16(ram,0xfe10)==0x4800:rec['next']=verify_next(run,'4800');break
            run.snapshot('after');rec['saved_stable']=(run.runtime/'sonic3k.srm.campaign').read_bytes()[62]
        except Exception as exc:rec['error']=repr(exc)
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                rec['hit_phases_log']=sorted(set(map(int,re.findall(r'\[boss3\] f=\d+ r=\w+ hits=(\d+) ',log))))
                rec['status']='passed' if not rec.get('error') and not rec['errors'] and all(rec.get(k)=='passed' for k in ('lid','defeat','escape','capsule','next')) and {1,2,3}<=set(rec['hit_phases_log']) and rec['saved_stable']==60 else 'failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec)
            print(json.dumps({k:v for k,v in rec.items() if k!='states'}),flush=True)
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())
