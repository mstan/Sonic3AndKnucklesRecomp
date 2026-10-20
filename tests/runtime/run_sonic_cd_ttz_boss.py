#!/usr/bin/env python3
"""Observe real TTZ chase damage, route escape, bubbles, boss and capsule.

Player positioning/camera and attack posture bypass navigation and hurt.
No boss HP/routine, bubble count, gate, escape or completion flags are set.
"""
import argparse
import hashlib
import json
from pathlib import Path
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects,verify_next
from run_sonic_cd_ccz_boss import move


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='4a02,4b02')
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='bosses';args.god=True
    args.out.mkdir(parents=True,exist_ok=False);results=[]
    for stage in manifest(args.content):
        if stage['stage'] not in args.stages.lower().split(','):continue
        pod=next(o for o in stage['objects'] if o['code']==0x7e3528)
        capsule=next(o for o in stage['objects'] if o['code']==0x7e3454)
        case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],status='unverified',states=[],chase_hp=[],
                 bypasses=['checkpoint-position entry','player/camera teleports following chase route',
                           'contact attack posture','hurt/death protection','shield navigation bypassed for main-boss contact'])
        try:
            run=Native(args,stage['stage'],(pod['x']-64,pod['y']-32,0),case,0)
            run.snapshot('before');move(run,0x480,pod['y']-12,True)
            saw_pod=False
            for n in range(320):
                ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==pod['code']]
                if not live:
                    if saw_pod and rec.get('chase_defeat')=='passed':rec['chase_escape']='passed';run.snapshot('chase-escaped')
                    break
                boss=live[0];saw_pod=True;rec['chase_hp'].append(boss['hp'])
                rec['states'].append(dict(frame=frame,phase='chase',object=boss))
                if boss['hp']==0 and boss['routine'] in (4,6):
                    rec['chase_defeat']='passed'
                    # The player wraps at $800 before the pod's route wraps
                    # at $880. Stay just below the seam while it crosses.
                    move(run,boss['x'],min(0x7c0,boss['y']+32))
                else:move(run,boss['x'],max(0,boss['y']-12),bool(boss['collision']))
                run.step(12)
            if rec.get('chase_escape')=='passed':
                move(run,0xa60,0x570);run.step(30);run.snapshot('arena-entry')
                popped=False;shots=False
                for n in range(180):
                    ram,frame=run.snapshot();oslots=objects(ram)
                    eggs=[o for o in oslots if o['code']==0x7e3530]
                    if not eggs:break
                    boss=eggs[0];count=ram[boss['addr']+0x33]
                    rec['states'].append(dict(frame=frame,phase='arena',object=boss,bubbles=count))
                    shots=shots or any(o['code']==0x7e3538 for o in oslots)
                    if count==16:rec['bubble_formation']='passed'
                    if boss['routine'] in (0x12,0x14) and ram[boss['addr']+0x32]&0x80:
                        rec['defeat']='passed';run.snapshot('defeat');break
                    bubbles=[o for o in oslots if o['code']==0x7e353c and o['collision'] and o['routine']==8
                             and abs(o['x']-boss['x'])>=40]
                    if rec.get('bubble_formation')=='passed' and not popped and bubbles:
                        bubble=max(bubbles,key=lambda o:abs(o['x']-boss['x']))
                        # Lower air is a prerequisite fixture; only the real
                        # bubble touch may restore it and decrement the ring.
                        # Place and lower air in ONE player write: a later
                        # write could overwrite an already completed breath.
                        move(run,bubble['x'],max(0,bubble['y']-4),True,air=2);run.step(4)
                        after,af=run.snapshot();live_after=[o for o in objects(after) if o['addr']==bubble['addr'] and o['code']==bubble['code']]
                        if (after[0xb02c]>2 and after[boss['addr']+0x33]<count
                                and (not live_after or live_after[0]['routine']>=0xc)):
                            rec['bubble_contact']='passed';rec['air_after_bubble']=after[0xb02c]
                            run.snapshot('bubble-breathed');popped=True
                    elif popped and shots and boss['routine'] in (0xc,0xe,0x10) and boss['collision']:
                        move(run,boss['x'],max(0,boss['y']-12),True)
                    else:move(run,0xa60,0x5a0)
                    run.step(12)
                rec['shots_observed']=shots
            if rec.get('defeat')=='passed':
                move(run,0xb80,0x570)
                for n in range(25):
                    run.step(45);ram,frame=run.snapshot()
                    if not any(o['code']==0x7e3530 for o in objects(ram)):
                        rec['escape']='passed';run.snapshot('escaped');break
                move(run,capsule['x']-64,capsule['y']-48);run.step(30)
                for n in range(15):
                    ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==capsule['code']]
                    if any(o['routine']>=4 for o in live):
                        rec['capsule']='passed';run.write(0xb02e,b'\0');run.snapshot('capsule-broken');break
                    move(run,capsule['x'],capsule['y']-12,True);run.step(12)
                for n in range(45):
                    run.step(90);ram,frame=run.snapshot()
                    if u16(ram,0xfe10)==0x4c00:rec['next']=verify_next(run,'4c00');break
            run.snapshot('after');rec['saved_stable']=(run.runtime/'sonic3k.srm.campaign').read_bytes()[62]
        except Exception as exc:rec['error']=repr(exc)
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                rec['chase_hp']=sorted(set(rec['chase_hp']))
                required=('chase_defeat','chase_escape','bubble_formation','bubble_contact','defeat','escape','capsule','next')
                rec['status']='passed' if (not rec.get('error') and not rec['errors'] and
                    all(rec.get(k)=='passed' for k in required) and rec.get('shots_observed') and
                    {0,2,3,4,5}<=set(rec['chase_hp']) and rec['saved_stable']==70) else 'failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec)
            print(json.dumps({k:v for k,v in rec.items() if k!='states'}),flush=True)
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())
