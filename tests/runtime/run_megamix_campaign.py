#!/usr/bin/env python3
"""Muted campaign probes. Cheat position/protection, never HP or clear flags.

Handoff starts at S3K's final boss segment (earlier DEZ bosses bypassed),
then retains the real defeat, chase, ending and credits before Megamix.
"""
import argparse,json
from pathlib import Path
from run_stage_pack_encounters import Native,objects,u16,camera_at,verify_next
from run_stage_pack_audit import manifest,errors

def handoff(args,run,rec):
    run.step(500);ram,_=run.snapshot()
    run.player(ram,0x5c0,0xd0,freeze=True);camera_at(run,0x5c0,0x80)
    rec['bypasses']=['stage selection: S3K final boss segment; earlier DEZ bosses bypassed',
                     'arena positioning','rolling contact posture','player protection']
    rec['hits']=[];seen=set();ending=False
    for n in range(1200):
        ram,frame=run.snapshot();za=u16(ram,0xfe10)
        if za==0x3400:
            rec['transition']=verify_next(run,'3400');break
        if za!=0x1700 or ram[0xf600]!=12:
            ending=True;run.step(120);continue
        os=objects(ram)
        targets=[o for o in os if o['collision'] and (0<o['hp']<=16 or o['code']==0x805AE)
                 and 0x7fd00<=o['code']<0x83000]
        if targets:
            target=targets[0];run.player(ram,target['x'],max(0,target['y']-16),attack=True)
            run.write(0xb02e,b'\x01');run.step(4);after,aframe=run.snapshot()
            hit=next((o for o in objects(after) if o['addr']==target['addr']),None)
            if hit and hit['hp']<target['hp']:
                rec['hits'].append(dict(frame=aframe,before=target,after=hit))
                seen.add(target['code']);run.snapshot(f'hit-{len(rec["hits"])}')
        else:
            # Follow the actual fleeing boss so its chase can finish.
            ram,_=run.snapshot();x=u16(ram,0xee78)+0xa0
            run.player(ram,x,0xcd,freeze=True)
        if n%60==0:run.snapshot(f'route-{n}')
        run.step(45)
    rec['ending_observed']=ending;rec['damaged_boss_codes']=[f'{v:06x}' for v in sorted(seen)]
    rec['status']='passed' if rec.get('transition')=='passed' and ending and rec['hits'] else 'unverified'

def metallic(args,run,rec):
    rec['bypasses']=['checkpoint positioning before signpost','player protection']
    run.cmd('set_input',keys='08')
    for n in range(100):
        run.step(30)
        if 'SBZ2 tally done' in (run.case/'run.log').read_text(errors='replace'):
            rec['tally']='passed';break
    run.cmd('set_input',keys='00')
    if rec.get('tally')!='passed':return
    run.step(400)
    # Approach the switch/floor cutscene in bounded teleports, then fall
    # through the floor under gravity. Never write the floor or DLE phase.
    for x in range(0x1f00,0x2150,0x20):
        ram,_=run.snapshot();run.player(ram,x,0x5a0,freeze=True);camera_at(run,x,0x5a0);run.step(10)
    run.step(200)
    ram,_=run.snapshot('floor-before');run.player(ram,0x2080,0x5a0)
    for n in range(120):
        run.step(30);ram,frame=run.snapshot()
        if u16(ram,0xfe10)==0x3503:
            rec['transition']=verify_next(run,'3503');break
        if n%10==0:run.snapshot(f'floor-{n}')
    rec['status']='passed' if rec.get('transition')=='passed' else 'unverified'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):p.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    p.add_argument('--case',choices=['handoff','metallic'],required=True)
    args=p.parse_args();args.character=1;args.mighty=True;args.kind='bosses' if args.case=='handoff' else 'routes'
    args.out.mkdir(parents=True,exist_ok=True);rec=dict(status='unverified',case=args.case);run=None
    try:
        stage='1700' if args.case=='handoff' else '3901';start=None
        if args.case=='metallic':
            s=next(s for s in manifest(args.bank) if s['stage']==stage)
            sign=next(o for o in s['objects'] if o['id']==13);start=(sign['x']-0x40,sign['y']-0x18,0)
        run=Native(args,stage,start,args.out/'runtime-case',0)
        (handoff if args.case=='handoff' else metallic)(args,run,rec)
        run.snapshot('final')
    except Exception as e:rec.update(status='failed',error=repr(e))
    finally:
        if run:
            run.close();rec['runtime_errors']=errors((run.case/'run.log').read_text(errors='replace'))
            if rec['runtime_errors']:rec['status']='failed'
        (args.out/'result.json').write_text(json.dumps(rec,indent=2))
    print(json.dumps({k:v for k,v in rec.items() if k!='hits'}),flush=True)
    return int(rec['status']!='passed')
if __name__=='__main__':raise SystemExit(main())
