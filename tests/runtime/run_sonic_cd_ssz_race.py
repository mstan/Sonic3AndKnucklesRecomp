#!/usr/bin/env python3
"""Observe the SSZ start, Metal's actual run/attacks, win and Amy ending.

Player/camera positioning bypasses racing skill and terrain traversal. Metal,
Eggman, gates, laser, finish order, Amy and results run their normal code.
No opponent, race/result or next-stage flags are written.
"""
import argparse
import hashlib
import json
from pathlib import Path
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects,verify_next
from run_sonic_cd_ccz_boss import move


def observe_loss(run,rec):
    """Let Metal win, then let Eggman's real laser kill and retry Sonic."""
    held_at_finish=False
    before=(run.runtime/'sonic3k.srm.campaign').read_bytes()
    lives=None
    for n in range(450):
        ram,frame=run.snapshot();oslots=objects(ram)
        metal=next((o for o in oslots if o['code']==0x7e3abc),None)
        egg=next((o for o in oslots if o['code']==0x7e3ab0),None)
        if lives is None:lives=ram[0xfe12];rec['lives_before']=lives
        goal=ram[egg['addr']+0x3f] if egg else 0
        rec['states'].append(dict(frame=frame,metal=metal,egg=egg,goal=goal,player_routine=ram[0xb005]))
        if goal&0xf==2 and metal and metal['routine']==0x10:rec['winner']='Metal Sonic'
        if ram[0xb005] in (6,8):
            rec['laser_death']='passed';run.snapshot('laser-death');break
        if goal&0xf==1:raise RuntimeError('Player unexpectedly won the loss fixture')
        if metal and not held_at_finish:
            if metal['x']>=0x3d00:
                # Below the real win threshold and just beyond Eggman's
                # final stop. Metal's goal clamps the loser behind the gate,
                # into the laser. Stop all player writes after this point.
                move(run,0x3e80,0x1cc);held_at_finish=True
            else:move(run,metal['x']+160,max(0,metal['y']-8))
        run.step(12)
    if rec.get('laser_death')=='passed':
        for n in range(100):
            run.step(15);ram,frame=run.snapshot()
            if ram[0xb005]==2 and u16(ram,0xfe04)<90 and ram[0xfe12]+1==lives:
                rec['retry']='passed';rec['lives_after']=ram[0xfe12];run.snapshot('retry');break
    after=(run.runtime/'sonic3k.srm.campaign').read_bytes()
    rec['cd_completion_unchanged']=not bool(after[63]&16) and (after[81:86]==before[81:86])


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='5602,5702')
    ap.add_argument('--outcome',choices=('win','loss'),default='win')
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='bosses' if args.outcome=='win' else 'mechanics';args.god=args.outcome=='win'
    args.target_fps=240
    args.out.mkdir(parents=True,exist_ok=False);results=[]
    for stage in manifest(args.content):
        if stage['stage'] not in args.stages.lower().split(','):continue
        egg=next(o for o in stage['objects'] if o['code']==0x7e3ab0)
        case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],outcome=args.outcome,status='unverified',states=[],
                 bypasses=['checkpoint-position entry','player/camera teleports following Metal',
                           'contact attack/standing posture','hurt/death protection','racing skill and terrain traversal'])
        if args.outcome=='loss':rec['bypasses']=['checkpoint-position entry','player/camera teleports','racing skill and terrain traversal']
        try:
            run=Native(args,stage['stage'],(egg['x']-160,0x1cc,0),case,0)
            run.snapshot('before');move(run,0xbe0,0x1cc)
            for n in range(120):
                ram,frame=run.snapshot();oslots=objects(ram)
                metal=next((o for o in oslots if o['code']==0x7e3abc),None)
                if metal and ram[metal['addr']+0x32]&0x20:
                    rec['race_start']='passed';run.snapshot('race-start');break
                run.step(12)
            if rec.get('race_start')=='passed' and args.outcome=='loss':observe_loss(run,rec)
            elif rec.get('race_start')=='passed':
                for n in range(450):
                    ram,frame=run.snapshot();oslots=objects(ram)
                    metal=next((o for o in oslots if o['code']==0x7e3abc),None)
                    boss=next((o for o in oslots if o['code']==egg['code']),None)
                    if not metal or not boss:break
                    goal=ram[boss['addr']+0x3f]
                    rec['states'].append(dict(frame=frame,metal=metal,egg=boss,goal=goal))
                    rec['laser_observed']=rec.get('laser_observed',False) or any(o['code']==0x7e3ab8 and o['frame'] for o in oslots)
                    rec['spike_smash']=rec.get('spike_smash',False) or any(o['code']==0x7e3ac8 and o['routine']==4 for o in oslots)
                    if metal['routine']==0xc and metal['collision']:rec['dash']='passed'
                    if metal['routine']==0xe and metal['collision'] and any(o['code']==0x7e3ac0 for o in oslots):rec['electric_field']='passed'
                    if goal&0xf==1:
                        rec['winner']='Sonic'
                        if metal['routine']==0x12 or any(o['code']==0x7e3ac4 for o in oslots):
                            rec['metal_crash']='passed';run.snapshot('metal-crashed');break
                    if goal&0xf==2:raise RuntimeError('Metal reached the finish first')
                    if metal['x']>=0x3d00:
                        # Only cross the real finish line; goal_chk owns the
                        # winner flag and Metal's subsequent barrier crash.
                        move(run,0x3e98,0x1cc)
                    elif rec.get('dash')!='passed':move(run,metal['x']+144,max(0,metal['y']-8))
                    elif rec.get('electric_field')!='passed':move(run,max(0,metal['x']-80),max(0,metal['y']-8))
                    else:move(run,metal['x']+96,max(0,metal['y']-8))
                    run.step(12)
            if rec.get('metal_crash')=='passed':
                move(run,0x3f10,0x1cc)
                for n in range(35):
                    run.step(45);ram,frame=run.snapshot()
                    if not any(o['code']==egg['code'] for o in objects(ram)):
                        rec['egg_escape']='passed';run.snapshot('escaped');break
                for n in range(20):
                    ram,frame=run.snapshot();amy=next((o for o in objects(ram) if o['code']==0x7e3acc),None)
                    if not amy:break
                    if amy['routine']>=2:
                        rec['amy_capsule']='passed';run.snapshot('amy-freed');break
                    move(run,amy['x'],max(0,amy['y']-4),True);run.step(8)
                for n in range(40):
                    run.step(12);ram,frame=run.snapshot();amy=next((o for o in objects(ram) if o['code']==0x7e3acc),None)
                    if not amy:break
                    if ram[amy['addr']+0x44]&0x81==0x81:
                        rec['amy_hug']='passed';run.snapshot('amy-hug');run.write(0xb02e,b'\0');break
                    if amy['routine'] in (4,6):move(run,amy['x']+16,amy['y'],grounded=True)
                for n in range(50):
                    run.step(90);ram,frame=run.snapshot()
                    if u16(ram,0xfe10)==0x5800:rec['next']=verify_next(run,'5800');break
            run.snapshot('after');rec['saved_stable']=(run.runtime/'sonic3k.srm.campaign').read_bytes()[62]
        except Exception as exc:rec['error']=repr(exc)
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                if args.outcome=='loss':
                    rec['status']='passed' if (not rec.get('error') and not rec['errors'] and rec.get('winner')=='Metal Sonic'
                        and all(rec.get(k)=='passed' for k in ('race_start','laser_death','retry')) and rec.get('cd_completion_unchanged')) else 'failed'
                else:rec['status']='passed' if (not rec.get('error') and not rec['errors'] and
                    all(rec.get(k)=='passed' for k in ('race_start','dash','electric_field','metal_crash','egg_escape','amy_capsule','amy_hug','next')) and
                    rec.get('winner')=='Sonic' and rec.get('laser_observed') and rec.get('spike_smash') and rec['saved_stable']==100) else 'failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec)
            print(json.dumps({k:v for k,v in rec.items() if k!='states'}),flush=True)
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())
