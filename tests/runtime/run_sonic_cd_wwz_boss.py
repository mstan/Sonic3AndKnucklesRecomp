#!/usr/bin/env python3
"""WWZ three-floor events, real contacts, final defeat, capsule and next.

Player/camera teleports and attack posture bypass traversal, platform rides
and hazard avoidance. No boss/event/beam/floor/defeat/completion flags change.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects,verify_next
from run_sonic_cd_ccz_boss import move


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='5202,5302')
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='bosses';args.god=True;args.env={'TRILOGY_BOSSLOG':'1'}
    args.out.mkdir(parents=True,exist_ok=False);results=[]
    for stage in manifest(args.content):
        if stage['stage'] not in args.stages.lower().split(','):continue
        egg=next(o for o in stage['objects'] if o['code']==0x7e38d0)
        capsule=next(o for o in stage['objects'] if o['code']==0x7e3854)
        case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],status='unverified',states=[],beams=[],floor_blocks=[],launches=[],hazards=[],
                 bypasses=['checkpoint-position entry','player/camera teleports','contact attack posture',
                           'hurt/death protection','floor-block rides and hazard avoidance'])
        try:
            run=Native(args,stage['stage'],(egg['x']-64,egg['y']+128,0),case,0)
            run.snapshot('before');move(run,0xac0,0x66c)
            for n in range(340):
                ram,frame=run.snapshot();oslots=objects(ram)
                live=[o for o in oslots if o['code']==egg['code']]
                if not live:break
                boss=live[0];floor=ram[boss['addr']+0x33];timer=u16(ram,boss['addr']+0x30)
                rec['states'].append(dict(frame=frame,boss=boss,floor=floor,timer=timer,hits=ram[boss['addr']+0x3f]))
                if any(o['code']==0x7e3900 for o in oslots):rec['beams'].append(floor)
                if any(o['code']==0x7e38f4 for o in oslots):rec['floor_blocks'].append(floor)
                if ram[boss['addr']+0x3c]:rec['launches'].append(floor)
                rec['hazards']+= [f'{o["code"]:06x}' for o in oslots if o['code'] in (0x7e38e0,0x7e38e4)]
                if floor==3 and boss['routine']>=0xc:
                    rec['defeat']='passed';run.snapshot('defeat');break
                if boss['routine']==6 and boss['collision']:
                    move(run,boss['x'],max(0,boss['y']-12),True);run.step(12)
                elif boss['routine']==0xa:
                    # Retain each real 840-frame event, including warnings,
                    # beams, falling blocks and their launch. Ascending to
                    # the next floor is explicitly a positioning bypass.
                    if floor==3 and 3 in rec['beams'] and 3 in rec['launches'] and boss['collision']:
                        move(run,boss['x'],max(0,boss['y']-12),True);run.step(12)
                    else:
                        target_floor=min(2,max(0,floor-1)+(1 if timer>=650 and floor<3 else 0))
                        want=(0x66c,0x46c,0x26c)[target_floor]
                        if abs(u16(ram,0xb014)-want)>16:move(run,0xa80,want)
                        run.step(30)
                else:run.step(30)
            if rec.get('defeat')=='passed':
                for n in range(32):
                    run.step(45);ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==egg['code']]
                    if any(o['routine']==0x12 and o['x']>=0xef0 for o in live):
                        rec['escape']='passed';run.snapshot('escaped');break
                move(run,capsule['x']-64,capsule['y']-48);run.step(30)
                for n in range(15):
                    ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==capsule['code']]
                    if any(o['routine']>=4 for o in live):
                        rec['capsule']='passed';run.write(0xb02e,b'\0');run.snapshot('capsule-broken');break
                    move(run,capsule['x'],capsule['y']-12,True);run.step(12)
                for n in range(45):
                    run.step(90);ram,frame=run.snapshot()
                    if u16(ram,0xfe10)==0x5400:rec['next']=verify_next(run,'5400');break
            run.snapshot('after');rec['saved_stable']=(run.runtime/'sonic3k.srm.campaign').read_bytes()[62]
        except Exception as exc:rec['error']=repr(exc)
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                rec['hit_floors']=sorted(set(int(f) for f,h in re.findall(r'\[boss6\] f=\d+ r=\w+ t=\d+ floor=(\d+) hits=(\d+)',log) if int(h)>0))
                for key in ('beams','floor_blocks','launches','hazards'):rec[key]=sorted(set(rec[key]))
                rec['status']='passed' if (not rec.get('error') and not rec['errors'] and
                    all(rec.get(k)=='passed' for k in ('defeat','escape','capsule','next')) and
                    {0,1,2}<=set(rec['hit_floors']) and all({1,2,3}<=set(rec[k]) for k in ('beams','floor_blocks','launches')) and
                    {'7e38e0','7e38e4'}<=set(rec['hazards']) and rec['saved_stable']==90) else 'failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec)
            print(json.dumps({k:v for k,v in rec.items() if k!='states'}),flush=True)
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())
