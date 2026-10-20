#!/usr/bin/env python3
"""Actual QQZ press activation, treadmill grind, escape, capsule and save.

Initial positioning, attack posture and hurt protection bypass navigation.
Right is real controller input. Occasional position resets keep the player
on the treadmill; no ground speed, belt speed, HP or success flags are set.
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
    ap.add_argument('--stages',default='4e02,4f02')
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='bosses';args.god=True
    args.out.mkdir(parents=True,exist_ok=False);results=[]
    for stage in manifest(args.content):
        if stage['stage'] not in args.stages.lower().split(','):continue
        housing=next(o for o in stage['objects'] if o['code']==0x7e36cc)
        capsule=next(o for o in stage['objects'] if o['code']==0x7e3654)
        case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],status='unverified',states=[],resets=0,
                 bypasses=['checkpoint-position entry','contact attack posture',
                           'hurt/death protection','occasional treadmill position resets','camera/route travel after defeat'])
        try:
            run=Native(args,stage['stage'],(housing['x']-96,housing['y'],0),case,0)
            run.snapshot('before');move(run,0xd70,0x270);run.step(24)
            for n in range(25):
                ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==0x7e36d0]
                if not live:break
                press=live[0]
                if press['routine']>=4:rec['press_contact']='passed';run.snapshot('press-contact');break
                move(run,press['x']-20,press['y'],True);run.step(12)
            if rec.get('press_contact')=='passed':
                ram,frame=run.snapshot();run.player(ram,0xd30,0x290)
                run.cmd('set_input',keys='08')
                for n in range(250):
                    run.step(60);ram,frame=run.snapshot();oslots=objects(ram)
                    live=[o for o in oslots if o['code']==0x7e36d0]
                    if not live:break
                    press=live[0];hp=u16(ram,press['addr']+0x44)
                    belts=[o for o in oslots if o['code']==0x7e3688]
                    rec['states'].append(dict(frame=frame,press=press,hp=hp,
                        belt_speed=u16(ram,belts[0]['addr']+0x38) if belts else None,
                        player=[u16(ram,0xb010),u16(ram,0xb014),ram[0xb02a],ram[0xb02e]]))
                    rec['bombs_observed']=rec.get('bombs_observed',False) or any(o['code'] in (0x7e36e0,0x7e36e4) for o in oslots)
                    if (hp==0 or hp&0x8000) and press['routine']>=0xa and ram[press['addr']+0x32]&4:
                        # The belt continues decrementing while it slows
                        # after defeat. Preserve the actual zero crossing,
                        # even if the host sample sees the signed underflow.
                        info=run.cmd('frame_info');first=max(info['oldest_frame'],frame-180)
                        hi=run.cmd('frame_timeseries',**{'from':first,'to':frame,'field':f'wram[{press["addr"]+0x44:X}]'})['values']
                        lo=run.cmd('frame_timeseries',**{'from':first,'to':frame,'field':f'wram[{press["addr"]+0x45:X}]'})['values']
                        values=[None if h is None or l is None else h*256+l for h,l in zip(hi,lo)]
                        (case/'hp-zero-history.json').write_text(json.dumps(dict(first_frame=first,values=values)))
                        rec['zero_hp_observed']=0 in values
                        if rec['zero_hp_observed']:rec['defeat']='passed'
                        run.snapshot('defeat');run.cmd('set_input',keys='00');break
                    x,y=u16(ram,0xb010),u16(ram,0xb014)
                    if x<0xcd0 or x>0xd88 or y>0x300 or ram[0xb02e]:
                        # Only the player's place and posture change. The
                        # next frames accelerate through normal Right input.
                        fresh,ff=run.snapshot();run.player(fresh,0xd30,0x290);rec['resets']+=1
            if rec.get('defeat')=='passed':
                for n in range(20):
                    run.step(45);ram,frame=run.snapshot()
                    if not any(o['code']==0x7e36c8 for o in objects(ram)):
                        rec['escape']='passed';run.snapshot('escaped');break
                move(run,capsule['x']-64,capsule['y']-48);run.step(30)
                for n in range(15):
                    ram,frame=run.snapshot();live=[o for o in objects(ram) if o['code']==capsule['code']]
                    if any(o['routine']>=4 for o in live):
                        rec['capsule']='passed';run.write(0xb02e,b'\0');run.snapshot('capsule-broken');break
                    move(run,capsule['x'],capsule['y']-12,True);run.step(12)
                for n in range(45):
                    run.step(90);ram,frame=run.snapshot()
                    if u16(ram,0xfe10)==0x5000:rec['next']=verify_next(run,'5000');break
            run.snapshot('after');rec['saved_stable']=(run.runtime/'sonic3k.srm.campaign').read_bytes()[62]
        except Exception as exc:rec['error']=repr(exc)
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                phases={s['press']['frame'] for s in rec['states']}
                rec['press_frames_observed']=sorted(phases)
                rec['max_belt_speed']=max((s['belt_speed'] or 0 for s in rec['states']),default=0)
                rec['status']='passed' if (not rec.get('error') and not rec['errors'] and
                    all(rec.get(k)=='passed' for k in ('press_contact','defeat','escape','capsule','next')) and
                    {1,2,3,4}<=phases and rec['max_belt_speed']>=0x500 and
                    rec.get('bombs_observed') and rec['saved_stable']==80) else 'failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec)
            print(json.dumps({k:v for k,v in rec.items() if k!='states'}),flush=True)
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())
